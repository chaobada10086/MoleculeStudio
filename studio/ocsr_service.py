"""MoleculeStudio OCSR worker.

Runs in an isolated Python runtime (torch CPU + MolScribe).  The native editor
starts this file once and talks to it with one XML request per line on stdin;
one XML response per line is written to stdout.  Nothing is uploaded: PDFs,
crops and model weights stay on this machine.

The worker never returns a SMILES-derived layout.  MolScribe's atom
coordinates, bonds and wedge/hash bonds are the primary result; the SMILES and
Molfile are saved as auxiliary representations.

Protocol
--------
stdin : <request id="1" op="render" .../>\n
stdout: <response id="1" ok="1" .../>\n

Operations: ping, status, render, detect, export_crop, recognize, confirm.

Crop exports are rebuilt from the PDF vector content at a higher raster
resolution instead of upscaling the 150-400 DPI page preview.  The same path
feeds MolScribe, so small structures no longer reach the model as a ~200 px
screenshot enlarged to 384 px.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import sys
import time
import traceback
import uuid
import warnings
import xml.etree.ElementTree as ET
from pathlib import Path

warnings.filterwarnings("ignore")

APP_DIR = Path(__file__).resolve().parent
SERVICE_VERSION = "1.3.1"

# --- MoleculeStudio fine-tune integration (added by ocsr-finetune/integration) ---
# Runtime knobs, read once per model load.  Defaults match the "768 px, long
# decode" plan; set MOLSCRIBE_INPUT_SIZE=384 / MOLSCRIBE_MAX_LEN=480 to get the
# stock behaviour back without editing files again.
FINETUNE_PATCH_ID = "ocsr-finetune-v3"
FINETUNE_SIZES = (384, 512, 768, 1024)
# Footgun guard: the stock release weights collapse at 768, so the *auto* default
# must follow the model source.  MOLSCRIBE_INPUT_SIZE still wins when set.
FINETUNE_DEFAULT_SIZE = 768          # fine-tuned checkpoints (experimental)
FINETUNE_STOCK_SIZE = 384            # stock swin_base_char_aux_1m680k.pth
FINETUNE_DEFAULT_MAX_LEN = 2000
FINETUNE_MIN_MAX_LEN = 480
STOCK_MODEL_NAMES = ("swin_base_char_aux_1m680k.pth",)


def is_finetuned_model_path(model_path) -> bool:
    """True when the checkpoint is NOT the stock MolScribe release."""
    if not model_path:
        return False
    name = Path(str(model_path)).name.lower()
    if name.startswith("finetuned_"):
        return True
    return name not in STOCK_MODEL_NAMES


def finetune_default_input_size(model_path) -> int:
    return FINETUNE_DEFAULT_SIZE if is_finetuned_model_path(model_path) else FINETUNE_STOCK_SIZE


def finetune_int_env(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        return int(raw)
    except ValueError:
        print("[ocsr] ignoring invalid %s=%r (using %d)" % (name, raw, default), file=sys.stderr)
        return default


def resolve_finetune_input_size(model_path=None) -> int:
    """MOLSCRIBE_INPUT_SIZE wins; otherwise 384 for stock weights, 768 for fine-tuned."""
    size = finetune_int_env("MOLSCRIBE_INPUT_SIZE", finetune_default_input_size(model_path))
    if size in FINETUNE_SIZES:
        return size
    nearest = min(FINETUNE_SIZES, key=lambda s: (abs(s - size), s))
    print("[ocsr] MOLSCRIBE_INPUT_SIZE=%d unsupported, using %d" % (size, nearest), file=sys.stderr)
    return nearest


def finetune_model_candidates(models_dir: Path) -> list:
    """Env override first, then models\finetuned_*.pth (newest first)."""
    out = []
    env_model = os.environ.get("MOLSCRIBE_MODEL_FILE", "").strip()
    if env_model:
        path = Path(env_model)
        out.append(path if path.is_absolute() else models_dir / env_model)
    try:
        finetuned = sorted(models_dir.glob("finetuned_*.pth"),
                           key=lambda p: p.stat().st_mtime, reverse=True)
        out.extend(finetuned)
    except OSError:
        pass
    return out

# Fine-tuned MolScribe emits aromatic atom tokens in SMILES style (c, n, o, se, cl ...).
# The editor's element mapper expects proper element symbols, so normalise the model
# output once, right where the atom list is materialised.
_LOWER_AROMATIC = {"c", "n", "o", "s", "p", "b", "se", "as", "te", "si", "cl", "br", "i", "f"}


def normalize_model_atom_symbol(symbol: str) -> str:
    text = (symbol or "").strip()
    if not text:
        return text
    low = text.lower()
    if low in _LOWER_AROMATIC:
        return low.capitalize()
    match = re.fullmatch(r"\[?([a-z]{1,2})(?:H\d*|[+-]\d*|@@|@)*\]?", text)
    if match:
        return match.group(1).capitalize()
    return text


def normalize_model_atom_symbols(atoms) -> None:
    for atom in atoms:
        if isinstance(atom, dict) and "atom_symbol" in atom:
            atom["atom_symbol"] = normalize_model_atom_symbol(atom.get("atom_symbol", ""))
# --- large selection downscaling (task-17) ---------------------------------
def _env_int(name, default, minimum, maximum):
    try:
        value = int(str(os.environ.get(name, "")).strip())
    except Exception:
        return default
    if value < minimum or value > maximum:
        return default
    return value


# env-configurable; defaults kept identical to the reviewed behaviour
MAX_SELECTION_PX = _env_int("MOLSCRIBE_MAX_SELECTION_PX", 4096, 512, 20000)
HARD_MAX_SELECTION_PX = _env_int("MOLSCRIBE_HARD_MAX_SELECTION_PX", 20000, MAX_SELECTION_PX, 60000)


def downscale_selection_image(raw, max_side: int = MAX_SELECTION_PX):
    """Return (model_input_image, scale) with the longest side <= max_side.

    Only the model input is resized.  MolScribe normalises coordinates after its
    own CropWhite + Resize(384), so normalised coordinates (and therefore the
    restored pixel positions computed from the *untouched* crop box) stay valid.
    """
    from PIL import Image

    longest = max(raw.width, raw.height)
    if longest <= max_side:
        return raw, 1.0
    scale = max_side / float(longest)
    size = (max(40, int(round(raw.width * scale))),
            max(40, int(round(raw.height * scale))))
    return raw.resize(size, Image.LANCZOS), scale
# --- end fine-tune integration ---
SCHEMA = "molecule-studio-ocsr/1"

_runtime_handles = []


def prepare_native_runtime():
    """Use the addon's app-local C++ runtime, independent of installed VC++ software."""
    if os.name != "nt" or _runtime_handles:
        return
    import ctypes
    directory = Path(sys.executable).resolve().parent
    if (directory / "msvcp140.dll").is_file():
        _runtime_handles.append(os.add_dll_directory(str(directory)))
        for name in ("msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
                     "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "concrt140.dll"):
            path = directory / name
            if path.is_file():
                _runtime_handles.append(ctypes.WinDLL(str(path)))

# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

def sha256_file(path: Path, limit: int | None = None) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            block = fh.read(1024 * 1024)
            if not block:
                break
            h.update(block)
            if limit is not None:
                limit -= len(block)
                if limit <= 0:
                    break
    return h.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def xml_response(ok: bool, **attrs) -> ET.Element:
    node = ET.Element("response", {"ok": "1" if ok else "0"})
    for key, value in attrs.items():
        if value is None:
            continue
        if isinstance(value, bool):
            value = "1" if value else "0"
        node.set(str(key), str(value))
    return node


def emit(node: ET.Element) -> None:
    sys.stdout.write(ET.tostring(node, encoding="unicode") + "\n")
    sys.stdout.flush()


def fail(message: str, **attrs) -> ET.Element:
    node = xml_response(False, error=message[:2000], **attrs)
    return node


def safe_under(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except Exception:
        return False


def load_nicknames() -> dict:
    for candidate in (APP_DIR / "nicknames.json", Path.cwd() / "nicknames.json"):
        if candidate.is_file():
            try:
                return json.loads(candidate.read_text(encoding="utf-8"))
            except Exception:
                return {}
    return {}


# ---------------------------------------------------------------------------
# MolScribe model wrapper
# ---------------------------------------------------------------------------

class OcsrEngine:
    def __init__(self, models_dir: Path, device: str = "cpu", model_file: str = ""):
        self.models_dir = models_dir
        self.device_name = device
        self.model_file = model_file
        self.model = None
        self.model_path: Path | None = None
        self.load_seconds: float | None = None
        self.load_error = ""
        self.nicknames = load_nicknames()
        self.input_size = resolve_finetune_input_size()
        self.max_len = max(FINETUNE_MIN_MAX_LEN,
                           finetune_int_env("MOLSCRIBE_MAX_LEN", FINETUNE_DEFAULT_MAX_LEN))
        self.model_source = "pending"

    def candidates(self) -> list[Path]:
        # fine-tune integration: MOLSCRIBE_MODEL_FILE > finetuned_*.pth > stock ckpt
        names = list(finetune_model_candidates(self.models_dir))
        if self.model_file:
            names.append(self.model_file)
        names += ["swin_base_char_aux_1m680k.pth", "swin_base_char_aux_1m.pth"]
        result: list[Path] = []
        for name in names:
            path = Path(name)
            if not path.is_absolute():
                path = self.models_dir / path
            try:
                if path.is_file() and path.stat().st_size > 100 * 1024 * 1024 and path not in result:
                    result.append(path)
            except OSError:
                continue
        return result

    def status(self, include_hash: bool = False) -> dict:
        paths = self.candidates()
        path = paths[0] if paths else None
        info = {
            "service_version": SERVICE_VERSION,
            "device": self.device_name,
            "torch": "",
            "cuda": False,
            "model_loaded": self.model is not None,
            "model_path": str(path) if path else "",
            "model_size": path.stat().st_size if path else 0,
            "model_sha256": "",
            "models_available": [p.name for p in paths],
            "load_seconds": self.load_seconds,
            "load_error": self.load_error,
            "input_size": self.input_size,
            "max_len": self.max_len,
            "model_source": self.model_source,
            "input_size_source": getattr(self, "input_size_source", ""),
            "finetune_patch": FINETUNE_PATCH_ID,
        }
        # Status is the native client's startup handshake (60 s deadline).
        # Importing torch here can exceed that on a cold disk / antivirus scan.
        # Load native inference dependencies only when recognition is requested.
        info["dependency_check"] = "deferred"
        try:
            torch = sys.modules.get("torch")
            if torch is not None:
                info["torch"] = torch.__version__
                info["cuda"] = bool(torch.cuda.is_available())
                info["dependency_check"] = "loaded"
            else:
                from importlib.metadata import version
                info["torch"] = version("torch")
        except Exception as exc:
            info["torch"] = "unavailable: " + str(exc)[:200]
        if path and include_hash:
            digest_path = path.with_suffix(path.suffix + ".sha256")
            try:
                if digest_path.is_file():
                    info["model_sha256"] = digest_path.read_text(encoding="ascii").strip()
                else:
                    digest = sha256_file(path)
                    digest_path.write_text(digest, encoding="ascii")
                    info["model_sha256"] = digest
            except Exception:
                pass
        return info

    def ensure_loaded(self, progress=None) -> None:
        if self.model is not None:
            return
        paths = self.candidates()
        if not paths:
            raise RuntimeError(
                "未找到 MolScribe 模型权重。请将 swin_base_char_aux_1m.pth（约 1.06 GiB）"
                "放入 ocsr_runtime\\models，或重新安装 OCR 组件。"
            )
        self.model_path = paths[0]
        if progress:
            progress("正在加载本地 MolScribe 模型（首次约 2–20 秒，不联网）…")
        prepare_native_runtime()
        import torch
        from molscribe import MolScribe

        device = torch.device(self.device_name if self.device_name == "cpu" or torch.cuda.is_available() else "cpu")
        started = time.time()
        self.model = MolScribe(str(self.model_path), device=device)
        self.load_seconds = round(time.time() - started, 2)
        self._apply_finetune_runtime()
        self.load_error = ""

    def _apply_finetune_runtime(self) -> None:
        """Re-point the MolScribe transform at MOLSCRIBE_INPUT_SIZE and raise the
        decode length limit.  Both are load-time only: no reload required."""
        model_path = getattr(self, "model_path", None)
        size = resolve_finetune_input_size(model_path)
        self.input_size_source = ("env" if os.environ.get("MOLSCRIBE_INPUT_SIZE", "").strip()
                                  else ("auto-finetuned" if is_finetuned_model_path(model_path)
                                        else "auto-stock"))
        max_len = max(FINETUNE_MIN_MAX_LEN,
                      finetune_int_env("MOLSCRIBE_MAX_LEN", FINETUNE_DEFAULT_MAX_LEN))
        try:
            from molscribe.dataset import get_transforms
            from molscribe.utils import FORMAT_INFO
            for fmt in ("chartok_coords", "atomtok_coords"):
                if fmt in FORMAT_INFO:
                    FORMAT_INFO[fmt]["max_len"] = max_len
            self.model.transform = get_transforms(size, augment=False)
        except Exception as exc:  # keep the worker usable, fall back to stock 384
            print("[ocsr] fine-tune runtime patch failed: %r; falling back to 384/480" % (exc,),
                  file=sys.stderr)
            size, max_len = 384, 480
        self.input_size = int(size)
        self.max_len = int(max_len)
        if os.environ.get("MOLSCRIBE_MODEL_FILE", "").strip():
            self.model_source = "env:MOLSCRIBE_MODEL_FILE"
        elif self.model_path is not None and self.model_path.name.startswith("finetuned_"):
            self.model_source = "auto:finetuned"
        else:
            self.model_source = "baseline"
        print("[ocsr] model=%s source=%s input_size=%d max_len=%d"
              % (self.model_path.name if self.model_path else "?", self.model_source,
                 self.input_size, self.max_len), file=sys.stderr)

    def recognize(self, image_path: Path, return_atoms: bool = True) -> dict:
        self.ensure_loaded()
        import torch

        started = time.time()
        output = self.model.predict_image_file(
            str(image_path), return_atoms_bonds=return_atoms, return_confidence=True
        )
        elapsed = round(time.time() - started, 3)
        output["_seconds"] = elapsed
        output["_device"] = str(self.model.device)
        return output


# ---------------------------------------------------------------------------
# PDF / image rendering
# ---------------------------------------------------------------------------

def render_source(path: Path, page: int, dpi: int, cache: Path, max_pages: int = 600) -> dict:
    cache.mkdir(parents=True, exist_ok=True)
    if not path.is_file():
        raise ValueError("文件不存在：" + str(path))
    if path.stat().st_size > 300 * 1024 * 1024:
        raise ValueError("文件超过 300 MB，请先拆分")
    suffix = path.suffix.lower()
    key = hashlib.sha1(f"{path.resolve()}|{path.stat().st_mtime_ns}|{page}|{dpi}".encode("utf-8")).hexdigest()[:20]
    # GDI+ keeps displayed PNGs open without write/delete sharing. Never
    # overwrite a published render, even across dialogs or worker processes.
    render_id = uuid.uuid4().hex
    if suffix == ".pdf":
        import pypdfium2 as pdfium

        try:
            document = pdfium.PdfDocument(str(path))
        except Exception as exc:
            raise ValueError("PDF 无法打开，可能已损坏或有密码保护：" + str(exc)[:200]) from None
        try:
            count = len(document)
            if not 1 <= count <= max_pages:
                raise ValueError(f"PDF 需要 1–{max_pages} 页，当前 {count} 页；请先拆分")
            if not 1 <= page <= count:
                raise ValueError(f"页码超出范围（1–{count}）")
            pdf_page = document[page - 1]
            width_pt, height_pt = pdf_page.get_size()
            scale = max(0.5, min(8.0, dpi / 72.0))
            bitmap = pdf_page.render(scale=scale)
            image = bitmap.to_pil().convert("RGB")
        finally:
            try:
                document.close()
            except Exception:
                pass
        out = cache / f"page-{key}-{render_id}.png"
        image.save(out, "PNG")
        return {
            "kind": "pdf",
            "page": page,
            "pages": count,
            "dpi": dpi,
            "image": str(out),
            "width": image.width,
            "height": image.height,
            "page_width_pt": round(width_pt, 3),
            "page_height_pt": round(height_pt, 3),
            "source_size": path.stat().st_size,
        }
    if suffix in (".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff"):
        from PIL import Image

        if path.stat().st_size > 40 * 1024 * 1024:
            raise ValueError("图片超过 40 MB，请先压缩")
        with Image.open(path) as original:
            original.load()
            image = original.convert("RGB")
        if image.width * image.height > 80_000_000:
            raise ValueError("图片像素过大")
        out = cache / f"image-{key}-{render_id}.png"
        image.save(out, "PNG")
        return {
            "kind": "image",
            "page": 1,
            "pages": 1,
            "dpi": dpi,
            "image": str(out),
            "width": image.width,
            "height": image.height,
            "page_width_pt": round(image.width * 72.0 / dpi, 3),
            "page_height_pt": round(image.height * 72.0 / dpi, 3),
            "source_size": path.stat().st_size,
        }
    raise ValueError("仅支持 PDF、PNG、JPEG、WebP、BMP、TIFF")


# ---------------------------------------------------------------------------
# Vector crop export (PDF source only)
# ---------------------------------------------------------------------------
#
# The page preview is rendered at 150-400 DPI because it has to fit on screen.
# A structure that occupies a few centimetres is then only 150-400 px wide.
# MolScribe resizes its input to 384 px, so cropping that preview first and
# enlarging it feeds the model a blurry, already-resampled image.
#
# These helpers rebuild the selected rectangle directly from the PDF vector
# content at a higher DPI (default 600, capped at 1200) and optionally save a
# single-page vector PDF whose CropBox is exactly the selection. Coordinates
# entering this module are always pixels in the displayed page image, so the
# native dialog and the AI bridge can keep using their existing selection
# rectangles.

CROP_MAX_PIXELS = 24_000_000
CROP_PREVIEW_MAX_SIDE = 2400
VECTOR_CROP_DPI_DEFAULT = 600
VECTOR_CROP_DPI_MAX = 1200


def _clamp_crop_rect(crop_rect, width: int, height: int):
    x, y, w, h = (int(v) for v in crop_rect)
    if w <= 0 or h <= 0:
        raise ValueError("选区太小（至少 40×40 像素）")
    x = max(0, min(width - 1, x))
    y = max(0, min(height - 1, y))
    w = min(w, width - x)
    h = min(h, height - y)
    if w < 40 or h < 40:
        raise ValueError("选区超出页面范围或太小")
    return x, y, w, h


def _pdf_region_points(page, crop_rect, base_dpi: int):
    """Return the selection as PDF points (left, bottom, right, top)."""
    if base_dpi < 72:
        raise ValueError("页面渲染 DPI 无效")
    page_width, page_height = page.get_size()
    x, y, w, h = crop_rect
    left = max(0.0, min(page_width, x * 72.0 / base_dpi))
    right = max(left, min(page_width, (x + w) * 72.0 / base_dpi))
    # PDF canvas coordinates grow upward from the bottom-left corner.
    top = max(0.0, min(page_height, page_height - y * 72.0 / base_dpi))
    bottom = max(0.0, min(top, page_height - (y + h) * 72.0 / base_dpi))
    if right - left < 1.0 or top - bottom < 1.0:
        raise ValueError("选区超出页面范围或太小")
    return left, bottom, right, top, page_width, page_height


def render_vector_crop(path: Path, page_number: int, crop_rect, base_dpi: int,
                       target_dpi: int, cache: Path, max_pages: int = 600,
                       max_side: int = CROP_PREVIEW_MAX_SIDE) -> dict:
    """Rasterise one PDF region from vector content at a higher DPI.

    crop_rect is in pixels of the page image rendered at base_dpi. The
    returned image is written under cache and is always RGB with a white
    background. max_side limits the long edge for model input; pass 0 for an
    unconstrained export.
    """
    cache.mkdir(parents=True, exist_ok=True)
    if not path.is_file():
        raise ValueError("文件不存在：" + str(path))
    if path.suffix.lower() != ".pdf":
        raise ValueError("矢量裁剪仅支持 PDF 源文件")
    if path.stat().st_size > 300 * 1024 * 1024:
        raise ValueError("文件超过 300 MB，请先拆分")

    import pypdfium2 as pdfium
    from PIL import Image

    try:
        document = pdfium.PdfDocument(str(path))
    except Exception as exc:
        raise ValueError("PDF 无法打开，可能已损坏或有密码保护：" + str(exc)[:200]) from None
    try:
        count = len(document)
        if not 1 <= count <= max_pages:
            raise ValueError(f"PDF 需要 1–{max_pages} 页，当前 {count} 页；请先拆分")
        if not 1 <= page_number <= count:
            raise ValueError(f"页码超出范围（1–{count}）")
        page = document[page_number - 1]
        left, bottom, right, top, page_width, page_height = _pdf_region_points(
            page, crop_rect, base_dpi)
        requested = max(int(base_dpi), min(VECTOR_CROP_DPI_MAX, int(target_dpi or VECTOR_CROP_DPI_DEFAULT)))
        scale = requested / 72.0
        estimated_w = max(1, int(math.ceil((right - left) * scale)))
        estimated_h = max(1, int(math.ceil((top - bottom) * scale)))
        if estimated_w * estimated_h > CROP_MAX_PIXELS:
            scale *= math.sqrt(CROP_MAX_PIXELS / float(estimated_w * estimated_h))
            requested = max(int(base_dpi), int(math.floor(scale * 72.0)))
        # pypdfium2's crop argument is an amount cut off from each edge after
        # rotation, expressed in PDF canvas units (1/72 inch), not a box.
        crop_amounts = (left, bottom, page_width - right, page_height - top)
        bitmap = page.render(scale=scale, crop=crop_amounts,
                             fill_color=(255, 255, 255, 255), may_draw_forms=True)
        try:
            image = bitmap.to_pil().convert("RGB")
        finally:
            bitmap.close()
    finally:
        try:
            document.close()
        except Exception:
            pass

    rendered_width = max(1, image.width)
    rendered_height = max(1, image.height)
    downsampled = False
    if max_side and max(image.width, image.height) > max_side:
        factor = max_side / float(max(image.width, image.height))
        image = image.resize((max(1, round(image.width * factor)),
                              max(1, round(image.height * factor))),
                             Image.LANCZOS)
        downsampled = True

    stamp = time.strftime("%Y%m%d-%H%M%S")
    nonce = hashlib.sha1(f"{time.time_ns()}|{os.getpid()}".encode()).hexdigest()[:6]
    out = cache / f"vector-{stamp}-{nonce}.png"
    image.save(out, "PNG")
    effective_dpi = scale * 72.0 * (image.width / float(rendered_width))
    return {
        "image": str(out),
        "width": image.width,
        "height": image.height,
        "dpi": round(effective_dpi, 3),
        "requested_dpi": int(target_dpi or VECTOR_CROP_DPI_DEFAULT),
        "base_dpi": int(base_dpi),
        "vector": True,
        "downsampled": downsampled,
        "source_box_pt": [round(left, 4), round(bottom, 4), round(right, 4), round(top, 4)],
        "crop_rect_px": [int(crop_rect[0]), int(crop_rect[1]), int(crop_rect[2]), int(crop_rect[3])],
        "page_size_px": [int(round(page_width * base_dpi / 72.0)),
                         int(round(page_height * base_dpi / 72.0))],
        "rendered_size_px": [rendered_width, rendered_height],
    }


def _export_vector_pdf(path: Path, page_number: int, box, cache: Path) -> Path:
    """Save one page with the selection as its CropBox (true vector export)."""
    import pypdfium2 as pdfium

    left, bottom, right, top = box
    stamp = time.strftime("%Y%m%d-%H%M%S")
    nonce = hashlib.sha1(f"{time.time_ns()}|{os.getpid()}|pdf".encode()).hexdigest()[:6]
    out = cache / f"vector-{stamp}-{nonce}.pdf"
    document = pdfium.PdfDocument(str(path))
    try:
        exported = pdfium.PdfDocument.new()
        try:
            exported.import_pages(document, [page_number - 1])
            page = exported[0]
            page.set_cropbox(left, bottom, right, top)
            exported.save(str(out))
        finally:
            exported.close()
    finally:
        document.close()
    if not out.is_file() or out.stat().st_size < 100:
        raise ValueError("矢量 PDF 导出失败")
    return out


def export_crop(path: Path, page_number: int, crop_rect, base_dpi: int,
                target_dpi: int, cache: Path, formats: str = "png+pdf") -> dict:
    """Export the selection as high-resolution PNG and/or vector-cropped PDF."""
    wanted = {item.strip().lower() for item in str(formats).replace(",", "+").split("+") if item.strip()}
    if not wanted:
        wanted = {"png", "pdf"}
    if path.suffix.lower() == ".pdf":
        prepared = render_vector_crop(path, page_number, crop_rect, base_dpi,
                                      target_dpi, cache, max_side=0)
        prepared["pdf"] = ""
        if "pdf" in wanted or "vector" in wanted:
            box = prepared["source_box_pt"]
            prepared["pdf"] = str(_export_vector_pdf(
                path, page_number, (box[0], box[1], box[2], box[3]), cache))
            prepared["pdf_size"] = Path(prepared["pdf"]).stat().st_size
        prepared["formats"] = "png+pdf" if prepared["pdf"] else "png"
        return prepared

    from PIL import Image

    if not path.is_file():
        raise ValueError("文件不存在：" + str(path))
    with Image.open(path) as original:
        original.load()
        image = original.convert("RGB")
    x, y, w, h = _clamp_crop_rect(crop_rect, image.width, image.height)
    raw = image.crop((x, y, x + w, y + h))
    stamp = time.strftime("%Y%m%d-%H%M%S")
    nonce = hashlib.sha1(f"{time.time_ns()}|{os.getpid()}|img".encode()).hexdigest()[:6]
    out = cache / f"vector-{stamp}-{nonce}.png"
    raw.save(out, "PNG")
    return {
        "image": str(out), "pdf": "", "width": raw.width, "height": raw.height,
        "dpi": int(base_dpi), "requested_dpi": int(base_dpi), "base_dpi": int(base_dpi),
        "vector": False, "downsampled": False,
        "source_box_pt": [], "crop_rect_px": [x, y, w, h],
        "page_size_px": [image.width, image.height],
        "rendered_size_px": [raw.width, raw.height], "formats": "png",
    }


# ---------------------------------------------------------------------------
# Structure region detection
# ---------------------------------------------------------------------------
#
# A born-digital PDF keeps every bond as a path object.  Grouping those paths
# by proximity gives structure boxes without guessing from a screenshot.  The
# clustering below works on page-space bounds, so it is independent of the
# preview DPI and never cuts a structure because the page was displayed small.

PDF_OBJECT_PATH = 2
PDF_OBJECT_FORM = 5


def _compose_matrix(parent, child):
    pa, pb, pc, pd, pe, pf = parent
    ca, cb, cc, cd, ce, cf = child
    return (pa * ca + pc * cb, pb * ca + pd * cb,
            pa * cc + pc * cd, pb * cc + pd * cd,
            pa * ce + pc * cf + pe, pb * ce + pd * cf + pf)


def _transform_box(box, matrix):
    l, b, r, t = box
    a, bb, c, d, e, f = matrix
    xs = [a * x + c * y + e for x, y in ((l, b), (r, b), (l, t), (r, t))]
    ys = [bb * x + d * y + f for x, y in ((l, b), (r, b), (l, t), (r, t))]
    return min(xs), min(ys), max(xs), max(ys)


def _pdf_path_boxes(page, max_objects: int = 50000):
    """Return path bounds in page space, descending into form XObjects."""
    import ctypes
    import pypdfium2.raw as raw

    found = []
    stack = [(page.raw, (1.0, 0.0, 0.0, 1.0, 0.0, 0.0), 0)]
    total = 0
    while stack:
        container, matrix, depth = stack.pop()
        try:
            count = raw.FPDFPage_CountObjects(container) if depth == 0 \
                else raw.FPDFFormObj_CountObjects(container)
        except Exception:
            continue
        for index in range(count):
            if total >= max_objects:
                return found
            try:
                obj = raw.FPDFPage_GetObject(container, index) if depth == 0 \
                    else raw.FPDFFormObj_GetObject(container, index)
            except Exception:
                continue
            total += 1
            try:
                if not raw.FPDFPageObj_GetIsActive(obj):
                    continue
            except Exception:
                pass
            try:
                obj_type = raw.FPDFPageObj_GetType(obj)
            except Exception:
                continue
            if obj_type == PDF_OBJECT_FORM:
                try:
                    form = raw.FS_MATRIX()
                    raw.FPDFPageObj_GetMatrix(obj, ctypes.byref(form))
                    child_matrix = (form.a, form.b, form.c, form.d, form.e, form.f)
                    stack.append((obj, _compose_matrix(matrix, child_matrix), depth + 1))
                except Exception:
                    pass
                continue
            if obj_type != PDF_OBJECT_PATH:
                continue
            try:
                left = ctypes.c_float()
                bottom = ctypes.c_float()
                right = ctypes.c_float()
                top = ctypes.c_float()
                if not raw.FPDFPageObj_GetBounds(obj, ctypes.byref(left), ctypes.byref(bottom),
                                                 ctypes.byref(right), ctypes.byref(top)):
                    continue
                box = _transform_box((left.value, bottom.value, right.value, top.value), matrix)
                if box[2] - box[0] < 0.05 or box[3] - box[1] < 0.05:
                    continue
                stroke = ctypes.c_float()
                try:
                    raw.FPDFPageObj_GetStrokeWidth(obj, ctypes.byref(stroke))
                except Exception:
                    pass
                closed = False
                segments = 0
                try:
                    segments = int(raw.FPDFPath_CountSegments(obj))
                    getter = getattr(raw, "FPDFPath_GetPathSegment", None)
                    closer = getattr(raw, "FPDFPathSegment_GetClose", None)
                    if getter and closer:
                        for seg_index in range(min(segments, 64)):
                            if closer(getter(obj, seg_index)):
                                closed = True
                                break
                except Exception:
                    segments = 0
                found.append({
                    "bbox": box,
                    "stroke": float(stroke.value) if stroke else 0.0,
                    "closed": closed,
                    "segments": segments,
                })
            except Exception:
                continue
    return found


def _cluster_boxes(items, gap: float):
    """Union boxes whose expanded rectangles overlap, using a spatial hash."""
    count = len(items)
    if not count:
        return []
    parent = list(range(count))

    def find(value):
        while parent[value] != value:
            parent[value] = parent[parent[value]]
            value = parent[value]
        return value

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    cell = max(4.0, gap * 4.0)
    grid = {}
    for index, item in enumerate(items):
        l, b, r, t = item["bbox"]
        x0 = int(math.floor((l - gap) / cell))
        x1 = int(math.floor((r + gap) / cell))
        y0 = int(math.floor((b - gap) / cell))
        y1 = int(math.floor((t + gap) / cell))
        for cx in range(x0, x1 + 1):
            for cy in range(y0, y1 + 1):
                for other in grid.get((cx, cy), ()):
                    l2, b2, r2, t2 = items[other]["bbox"]
                    if l - gap <= r2 and l2 <= r + gap and b - gap <= t2 and b2 <= t + gap:
                        union(index, other)
                grid.setdefault((cx, cy), []).append(index)

    groups = {}
    for index in range(count):
        groups.setdefault(find(index), []).append(index)
    return list(groups.values())


def _region_score(width: float, height: float, paths: int, closed: int) -> float:
    score = 0.10
    if width >= 10 and height >= 10:
        score += 0.18
    if width >= 18 and height >= 15:
        score += 0.20
    if paths >= 6:
        score += 0.14
    if paths >= 12:
        score += 0.10
    aspect = max(width / max(height, 0.01), height / max(width, 0.01))
    if 0.30 <= aspect <= 4.0:
        score += 0.18
    if 15 <= width <= 220 and 15 <= height <= 220:
        score += 0.12
    if closed:
        score += 0.08
    if aspect > 8.0 or aspect < 0.12:
        score -= 0.35
    if height < 8 or width < 8:
        score -= 0.30
    if paths < 3:
        score -= 0.25
    if height < 26 and width > 34 and paths <= 12:
        score -= 0.12  # reagent/formula text is usually a short wide run
    return max(0.0, min(1.0, score))


def _raster_regions(image, dpi: int):
    """Fallback for scanned PDFs and plain images.

    Dilating the ink merges each structure's bonds and atom labels into one
    blob.  Text lines stay short and are removed by the height/aspect filter.
    This is deliberately conservative: it returns fewer, larger candidates and
    the caller is told that the result is a raster estimate.
    """
    import cv2
    import numpy as np

    array = np.array(image.convert("L"))
    _, ink = cv2.threshold(array, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
    dilated = cv2.dilate(ink, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)),
                         iterations=1)
    count, _, stats, _ = cv2.connectedComponentsWithStats(dilated, 8)
    min_width = max(40, int(dpi * 0.15))
    min_height = max(40, int(dpi * 0.18))
    min_area = max(1000, int(dpi * dpi * 0.012))
    regions = []
    for index in range(1, count):
        x, y, w, h, area = (int(v) for v in stats[index])
        if w < min_width or h < min_height or area < min_area:
            continue
        if max(w / max(h, 1), h / max(w, 1)) > 4.5:
            continue
        score = min(0.85, 0.55 + min(0.2, area / float(max(1, dpi * dpi * 0.08))))
        if h < 26:
            score -= 0.15
        regions.append({"bbox": (x, y, x + w, y + h), "paths": 1, "closed": 0,
                        "score": max(0.0, score), "stroke": 0.0, "segments": 0})
    return regions


def _draw_detect_preview(page, page_height: float, regions, dpi: int, cache: Path) -> str:
    from PIL import ImageDraw

    preview_dpi = min(150, max(96, int(dpi)))
    scale = preview_dpi / 72.0
    image = page.render(scale=scale).to_pil().convert("RGB")
    draw = ImageDraw.Draw(image)
    for index, region in enumerate(regions, 1):
        l, b, r, t = region["bbox"]
        rect = (l * scale, (page_height - t) * scale, r * scale, (page_height - b) * scale)
        draw.rectangle(rect, outline=(220, 30, 30), width=3)
        if len(regions) <= 80:
            draw.text((rect[0] + 2, max(0, rect[1] - 12)), str(index), fill=(0, 80, 220))
    stamp = time.strftime("%Y%m%d-%H%M%S")
    nonce = hashlib.sha1(f"{time.time_ns()}|{os.getpid()}|detect".encode()).hexdigest()[:6]
    out = cache / f"detect-{stamp}-{nonce}.png"
    image.save(out, "PNG")
    return str(out)


def detect_structures(path: Path, page_number: int, dpi: int, cache: Path,
                      max_pages: int = 600, max_regions: int = 120,
                      min_score: float = 0.35) -> dict:
    """Detect candidate structure boxes on one PDF page or image."""
    cache.mkdir(parents=True, exist_ok=True)
    if not path.is_file():
        raise ValueError("文件不存在：" + str(path))
    if path.stat().st_size > 300 * 1024 * 1024:
        raise ValueError("文件超过 300 MB，请先拆分")

    import pypdfium2 as pdfium

    method = "pdf_vector"
    warnings = []
    if path.suffix.lower() == ".pdf":
        try:
            document = pdfium.PdfDocument(str(path))
        except Exception as exc:
            raise ValueError("PDF 无法打开，可能已损坏或有密码保护：" + str(exc)[:200]) from None
        try:
            count = len(document)
            if not 1 <= count <= max_pages:
                raise ValueError(f"PDF 需要 1–{max_pages} 页，当前 {count} 页；请先拆分")
            if not 1 <= page_number <= count:
                raise ValueError(f"页码超出范围（1–{count}）")
            page = document[page_number - 1]
            page_width, page_height = page.get_size()
            paths = _pdf_path_boxes(page)
            if len(paths) >= 4:
                groups = _cluster_boxes(paths, 1.0)
                regions = []
                for ids in groups:
                    l = min(paths[i]["bbox"][0] for i in ids)
                    b = min(paths[i]["bbox"][1] for i in ids)
                    r = max(paths[i]["bbox"][2] for i in ids)
                    t = max(paths[i]["bbox"][3] for i in ids)
                    width, height = r - l, t - b
                    if width < 8 or height < 8 or width * height < 60:
                        continue
                    closed = sum(1 for i in ids if paths[i]["closed"])
                    score = _region_score(width, height, len(ids), closed)
                    if score < min_score:
                        continue
                    regions.append({"bbox": (l, b, r, t), "paths": len(ids),
                                    "closed": closed, "score": score,
                                    "stroke": max(paths[i]["stroke"] for i in ids),
                                    "segments": max(paths[i]["segments"] for i in ids)})
            else:
                method = "raster"
                warnings.append("该页没有可用的矢量路径，已改用图像分割；复杂插图请人工核对框选。")
                render_dpi = max(150, min(300, int(dpi)))
                scale = render_dpi / 72.0
                image = page.render(scale=scale).to_pil().convert("RGB")
                regions = _raster_regions(image, render_dpi)
                for region in regions:
                    l, b, r, t = region["bbox"]
                    region["bbox"] = (l * 72.0 / render_dpi, (image.height - t) * 72.0 / render_dpi,
                                      r * 72.0 / render_dpi, (image.height - b) * 72.0 / render_dpi)
            # Sort in reading order (top first, then left).
            regions.sort(key=lambda item: (-item["bbox"][3], item["bbox"][0]))
            regions = regions[:max_regions]
            preview = _draw_detect_preview(page, page_height, regions, dpi, cache)
        finally:
            try:
                document.close()
            except Exception:
                pass
    else:
        from PIL import Image

        method = "raster"
        with Image.open(path) as original:
            original.load()
            image = original.convert("RGB")
        page_width = image.width * 72.0 / max(72, dpi)
        page_height = image.height * 72.0 / max(72, dpi)
        regions = _raster_regions(image, dpi)
        for region in regions:
            l, b, r, t = region["bbox"]
            region["bbox"] = (l * 72.0 / dpi, (image.height - t) * 72.0 / dpi,
                              r * 72.0 / dpi, (image.height - b) * 72.0 / dpi)
        regions.sort(key=lambda item: (-item["bbox"][3], item["bbox"][0]))
        regions = regions[:max_regions]
        preview = ""

    page_width_px = int(round(page_width * dpi / 72.0))
    page_height_px = int(round(page_height * dpi / 72.0))
    output = []
    for index, region in enumerate(regions, 1):
        l, b, r, t = region["bbox"]
        x = max(0, int(round(l * dpi / 72.0)))
        y = max(0, int(round((page_height - t) * dpi / 72.0)))
        w = max(1, int(round((r - l) * dpi / 72.0)))
        h = max(1, int(round((t - b) * dpi / 72.0)))
        w = min(w, page_width_px - x)
        h = min(h, page_height_px - y)
        if w < 8 or h < 8:
            continue
        output.append({
            "index": len(output) + 1,
            "crop": [round(l / max(page_width, 1e-6), 6),
                     round((page_height - t) / max(page_height, 1e-6), 6),
                     round((r - l) / max(page_width, 1e-6), 6),
                     round((t - b) / max(page_height, 1e-6), 6)],
            "bbox_pt": [round(l, 2), round(b, 2), round(r, 2), round(t, 2)],
            "pixels": [x, y, w, h],
            "score": round(float(region["score"]), 4),
            "paths": int(region["paths"]),
            "closed_paths": int(region["closed"]),
            "stroke": round(float(region.get("stroke") or 0.0), 3),
        })
    return {
        "method": method,
        "page": page_number,
        "dpi": dpi,
        "page_width_pt": round(page_width, 3),
        "page_height_pt": round(page_height, 3),
        "page_size_px": [page_width_px, page_height_px],
        "count": len(output),
        "regions": output,
        "preview": preview,
        "warnings": warnings,
    }


# ---------------------------------------------------------------------------
# Molfile / geometry handling
# ---------------------------------------------------------------------------

def _field_int(line: str, start: int, end: int) -> int:
    try:
        text = line[start:end].strip()
        return int(text) if text else 0
    except Exception:
        return 0


def _strip_brackets(symbol: str) -> str:
    value = (symbol or "").strip()
    if len(value) >= 2 and value.startswith("[") and value.endswith("]"):
        value = value[1:-1]
    return value


_ELEMENT_SYMBOLS = None


def _valid_element(symbol: str):
    global _ELEMENT_SYMBOLS
    from rdkit import Chem
    if _ELEMENT_SYMBOLS is None:
        _ELEMENT_SYMBOLS = {Chem.GetPeriodicTable().GetElementSymbol(i) for i in range(1, 119)}
    return symbol in _ELEMENT_SYMBOLS


def _element_from_symbol(symbol: str):
    from rdkit import Chem
    if not _valid_element(symbol):
        return None
    try:
        number = int(Chem.GetPeriodicTable().GetAtomicNumber(symbol))
        return number if 0 < number <= 118 else None
    except Exception:
        return None


def _model_atomic_number(symbol: str):
    """Best-effort atomic number for a model atom token such as C, [NH4+], [OEt]."""
    text = _strip_brackets(symbol)
    if not text:
        return None
    if text in ("*", "A", "Q", "X"):
        return 0
    if text == "R" or (text[0] == "R" and text[1:].isdigit()):
        return 0
    if re.fullmatch(r"[A-Z][a-z]?", text):
        number = _element_from_symbol(text)
        if number:
            return number
    # Strip isotope / hydrogen / charge / tetrahedral markers before looking up
    # the element.  This keeps model tokens such as [C@@H] and [NH4+] useful
    # without treating OEt or Ph as an element.
    marked = re.fullmatch(r"\[?(?:\d+)?([A-Z][a-z]?)(?:(?:@@|@)?H?\d*[+-]?\d*)?\]?", text)
    if marked:
        number = _element_from_symbol(marked.group(1))
        if number:
            return number
    aromatic = re.fullmatch(r"\[?(?:\d+)?(n|o|s|p|se|as|te)(?:H?\d*)?[+-]?\d*\]?", text)
    if aromatic:
        number = _element_from_symbol(aromatic.group(1).capitalize())
        if number:
            return number
    return None


def _model_alias_key(symbol: str) -> str:
    text = _strip_brackets(symbol)
    if not text:
        return ""
    try:
        atomic = _model_atomic_number(text)
    except Exception:
        atomic = None
    if atomic is not None and atomic > 0:
        return ""
    return text


def _alias_key(value: str) -> str:
    return re.sub(r"\s+", "", _strip_brackets(value or "")).casefold()


def _atom_compatible(model_atom: dict, molfile_atom: dict) -> bool:
    model_alias = _model_alias_key(model_atom.get("atom_symbol", ""))
    molfile_alias = (molfile_atom.get("alias") or "").strip()
    if model_alias or molfile_alias:
        if model_alias and molfile_alias:
            return _alias_key(model_alias) == _alias_key(molfile_alias)
        if (model_alias or "").casefold() in ("r", "a", "*") and int(molfile_atom.get("element") or 0) == 0:
            return True
        return False
    model_element = _model_atomic_number(model_atom.get("atom_symbol", ""))
    molfile_element = int(molfile_atom.get("element") or 0)
    if model_element is None:
        return False
    if molfile_element == 0:
        return model_element == 0
    return model_element == molfile_element


def parse_molblock(molblock: str) -> dict:
    """Parse a V2000 Molfile, keeping chemistry AND the raw wedge endpoints.

    RDKit is the authority for formal charge, absolute isotope, explicit
    hydrogen count and sanitisation problems.  The raw atom/bond block is still
    parsed because RDKit intentionally clears V2000 wedge codes while
    sanitising; those codes and the begin/end order are needed for the CDXML
    wedge display and for the stereo evidence trail.
    """
    from rdkit import Chem

    lines = molblock.replace("\r\n", "\n").replace("\r", "\n").splitlines()
    if len(lines) < 4:
        raise ValueError("模型没有返回可解析的 Molfile")
    counts = lines[3]
    if "V3000" in counts:
        # Normal molecules can be normalised without relayout. Coordination,
        # queries and enhanced stereo must not be flattened into ordinary bonds.
        molecule = Chem.MolFromMolBlock(molblock, sanitize=False, removeHs=False, strictParsing=False)
        if molecule is None: raise ValueError('RDKit 无法解析模型返回的 V3000 结构')
        unsupported_bonds = sorted({str(b.GetBondType()) for b in molecule.GetBonds()
                                    if b.GetBondType() not in (Chem.BondType.SINGLE, Chem.BondType.DOUBLE, Chem.BondType.TRIPLE, Chem.BondType.AROMATIC)})
        if unsupported_bonds:
            raise ValueError('识别结果包含暂不支持安全插入的配位/查询键（' + ', '.join(unsupported_bonds) + '）；请核实是否为试剂或催化剂，不会将其改成普通单键。')
        if any(a.HasQuery() or a.GetAtomicNum()==0 for a in molecule.GetAtoms()) or molecule.GetStereoGroups() or Chem.GetMolSubstanceGroups(molecule):
            raise ValueError('V3000 结构含查询原子、未定义基团或特殊立体/结构组，无法安全转换；已阻止插入。')
        try:
            Chem.SanitizeMol(molecule)
            if hasattr(Chem, 'ReapplyMolBlockWedging'): Chem.ReapplyMolBlockWedging(molecule)
            converted = Chem.MolToMolBlock(molecule, kekulize=False, forceV3000=False)
            if 'V3000' in converted.splitlines()[3]: raise ValueError('该结构无法用 V2000 无损表示')
            roundtrip = Chem.MolFromMolBlock(converted, removeHs=False)
            if roundtrip is None or Chem.MolToSmiles(roundtrip, isomericSmiles=True)!=Chem.MolToSmiles(molecule, isomericSmiles=True):
                raise ValueError('转换后的结构或立体信息与原结果不一致')
        except Exception as exc:
            raise ValueError('V3000 安全转换未通过：' + str(exc)[:220]) from None
        return parse_molblock(converted)
    try:
        na, nb = int(counts[0:3]), int(counts[3:6])
    except Exception:
        raise ValueError("Molfile 计数行无效")
    if not (0 < na <= 400 and 0 <= nb <= 1000):
        raise ValueError(f"Molfile 原子/键数量不合理：{na}/{nb}")

    raw_atoms = []
    for idx in range(na):
        line = lines[4 + idx]
        try:
            x = float(line[0:10]); y = float(line[10:20]); z = float(line[20:30])
            symbol = line[31:34].strip() or "C"
            mass_diff = _field_int(line, 34, 36)
            charge_code = _field_int(line, 36, 39)
            parity = _field_int(line, 39, 42)
            h_field = _field_int(line, 42, 45)
            valence = _field_int(line, 48, 51)
        except Exception:
            raise ValueError(f"Molfile 第 {idx + 1} 个原子无法解析")
        raw_atoms.append({
            "x": x, "y": y, "z": z, "symbol": symbol,
            "mass_diff": mass_diff, "charge_code": charge_code,
            "parity": parity, "h_field": h_field, "valence": valence,
        })

    raw_bonds = []
    for idx in range(nb):
        line = lines[4 + na + idx]
        try:
            a1 = int(line[0:3]) - 1
            a2 = int(line[3:6]) - 1
            order = int(line[6:9])
            stereo = int(line[9:12]) if len(line) >= 12 else 0
        except Exception:
            raise ValueError(f"Molfile 第 {idx + 1} 根键无法解析")
        if not (0 <= a1 < na and 0 <= a2 < na) or a1 == a2:
            raise ValueError("Molfile 键端点越界")
        if order not in (1, 2, 3, 4):
            raise ValueError(f"Molfile 键级 {order} 不在支持范围")
        # V2000 bond stereo: 0 none, 1 up, 6 down, 3 either/unknown
        # (the code RDKit writes for an explicitly unspecified double bond).
        # Code 3 is chemistry, not a display wedge; it must survive as an
        # "unspecified" state instead of being rejected or silently completed.
        if stereo not in (0, 1, 3, 6):
            raise ValueError(f"Molfile 键立体代码 {stereo} 不在支持范围")
        raw_bonds.append({"begin": a1, "end": a2, "order": order, "stereo": stereo})

    aliases = {}
    unsupported = []
    i = 4 + na + nb
    while i < len(lines):
        line = lines[i]
        if line.startswith("A ") and len(line) >= 4:
            try:
                atom_index = int(line[3:].strip()) - 1
            except Exception:
                atom_index = -1
            if 0 <= atom_index < na and i + 1 < len(lines):
                label = lines[i + 1].strip()
                if label:
                    aliases[atom_index] = label
                i += 2
                continue
        elif line.startswith("M  "):
            code = line[3:6].strip()
            if code not in ("CHG", "ISO", "END"):
                unsupported.append(line.strip()[:160])
        elif line.startswith("V30"):
            unsupported.append(line.strip()[:160])
        i += 1

    rd_mol = Chem.MolFromMolBlock(molblock, sanitize=False, removeHs=False, strictParsing=False)
    if rd_mol is None:
        raise ValueError("RDKit 无法解析该 Molfile")
    if rd_mol.GetNumAtoms() != na:
        raise ValueError(f"RDKit 原子数 {rd_mol.GetNumAtoms()} 与 Molfile 计数 {na} 不一致")

    sanitize_error = ""
    try:
        Chem.SanitizeMol(rd_mol)
    except Exception as exc:
        sanitize_error = str(exc).replace("\n", " ")[:300]
        # Retain the sanitisation failure as a hard insertion gate, but initialise
        # the property cache so malformed candidates can still be reviewed.
        try: rd_mol.UpdatePropertyCache(strict=False)
        except Exception: pass
    problems = []
    try:
        problems = [str(p).replace("\n", " ")[:300] for p in Chem.DetectChemistryProblems(rd_mol)]
    except Exception:
        pass

    atoms = []
    for idx, raw in enumerate(raw_atoms):
        atom = rd_mol.GetAtomWithIdx(idx)
        element = int(atom.GetAtomicNum())
        isotope = int(atom.GetIsotope())
        charge = int(atom.GetFormalCharge())
        h_count = None
        if element not in (0, 1):
            try: h_count = max(0, int(atom.GetNumImplicitHs()) + int(atom.GetNumExplicitHs()))
            except RuntimeError:
                h_count = None
                if not sanitize_error: sanitize_error = '原子氢数无法验证，已阻止插入'
        alias = aliases.get(idx, "")
        if not alias and atom.HasProp("molFileAlias"):
            try:
                alias = atom.GetProp("molFileAlias")
            except Exception:
                alias = ""
        atoms.append({
            "index": idx,
            "x": raw["x"], "y": raw["y"], "z": raw["z"],
            "symbol": raw["symbol"],
            "element": element,
            "charge": charge,
            "isotope": isotope,
            "mass_diff": raw["mass_diff"],
            "h_field": raw["h_field"],
            "h_count": h_count,
            "alias": alias,
            "parity": raw["parity"],
        })

    bonds = []
    for bond in raw_bonds:
        bonds.append({
            "begin": bond["begin"], "end": bond["end"],
            "order": bond["order"], "stereo": bond["stereo"],
        })

    return {
        "atoms": atoms,
        "bonds": bonds,
        "aliases": aliases,
        "unsupported": unsupported,
        "sanitize_error": sanitize_error,
        "problems": problems,
        "rdkit_mol": rd_mol,
    }


def connected_components(na: int, bonds: list[dict]) -> list[list[int]]:
    parent = list(range(na))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for bond in bonds:
        ra, rb = find(bond["begin"]), find(bond["end"])
        if ra != rb:
            parent[rb] = ra
    groups: dict[int, list[int]] = {}
    for i in range(na):
        groups.setdefault(find(i), []).append(i)
    return sorted(groups.values(), key=lambda g: (-len(g), min(g)))


def _molfile_normalized(atom: dict, image_size: tuple[int, int]) -> tuple[float, float]:
    width, height = image_size
    ratio = float(width) / max(1.0, float(height))
    # MolScribe's _convert_graph_to_smiles multiplies the normalised image x by
    # the *original input* aspect ratio and flips y into the Molfile axis.
    x = float(atom["x"]) / max(1e-9, ratio * 10.0)
    y = (1.0 - float(atom["y"])) / 10.0
    return x, y


def map_molfile_to_model(parsed: dict, model_atoms: list[dict], model_bonds: list[dict], image_size: tuple[int, int]) -> dict:
    """Map MolScribe's model atoms to the Molfile atoms explicitly.

    The Molfile is produced by RDKit from the same graph, but its atom order is
    not part of any contract.  Match by element/alias plus normalised geometry
    and then verify the complete bond topology before accepting the mapping.
    """
    if len(model_atoms) != len(parsed["atoms"]):
        raise ValueError(
            f"模型原子数 {len(model_atoms)} 与 Molfile 原子数 {len(parsed['atoms'])} 不一致，"
            "无法建立可靠映射"
        )
    if not model_atoms:
        raise ValueError("模型没有返回原子")

    normalized = [_molfile_normalized(atom, image_size) for atom in parsed["atoms"]]
    count = len(model_atoms)
    cost = []
    for mi, model_atom in enumerate(model_atoms):
        row = []
        for mfj, molfile_atom in enumerate(parsed["atoms"]):
            dx = float(model_atom["x"]) - normalized[mfj][0]
            dy = float(model_atom["y"]) - normalized[mfj][1]
            distance = math.hypot(dx, dy)
            if not _atom_compatible(model_atom, molfile_atom):
                row.append(1.0e6 + distance)
            else:
                row.append(distance)
        cost.append(row)

    assignment = [None] * count
    try:
        from scipy.optimize import linear_sum_assignment

        rows, cols = linear_sum_assignment(cost)
        for row, col in zip(rows, cols):
            assignment[int(row)] = int(col)
    except Exception:
        used = set()
        for mi in range(count):
            choices = sorted(range(count), key=lambda j: cost[mi][j])
            for mfj in choices:
                if mfj not in used:
                    used.add(mfj)
                    assignment[mi] = mfj
                    break

    if any(value is None for value in assignment):
        raise ValueError("无法为每个模型原子找到 Molfile 对应原子")
    if len(set(assignment)) != count:
        raise ValueError("模型原子到 Molfile 原子的映射不唯一")

    max_distance = max(cost[mi][mfj] for mi, mfj in enumerate(assignment))
    if max_distance >= 1.0e5:
        raise ValueError("模型原子与 Molfile 原子的元素/缩写不一致，无法可靠映射")
    if max_distance > 0.12:
        raise ValueError(
            f"模型原子与 Molfile 坐标归一化后偏差过大（{max_distance:.4f}），"
            "无法确认索引映射"
        )

    mf_to_model = [None] * count
    for mi, mfj in enumerate(assignment):
        mf_to_model[mfj] = mi

    model_edges = set()
    for bond in model_bonds:
        endpoints = list(bond.get("endpoint_atoms", ()))
        if len(endpoints) == 2:
            model_edges.add(frozenset((int(endpoints[0]), int(endpoints[1]))))
    molfile_edges = set()
    for bond in parsed["bonds"]:
        molfile_edges.add(frozenset((mf_to_model[bond["begin"]], mf_to_model[bond["end"]])))
    if model_edges and molfile_edges != model_edges:
        raise ValueError("模型键连接关系与 Molfile 不一致，无法建立可靠映射")

    return {
        "model_to_molfile": assignment,
        "molfile_to_model": mf_to_model,
        "max_normalized_distance": round(max_distance, 8),
    }


def restore_model_pixel(atom: dict, box: tuple[int, int, int, int], pad: int = 5) -> tuple[float, float]:
    left, top, right, bottom = box
    width = max(1, int(right) - int(left) + pad * 2)
    height = max(1, int(bottom) - int(top) + pad * 2)
    x = (float(left) - pad) + float(atom["x"]) * width
    y = (float(top) - pad) + float(atom["y"]) * height
    return x, y


def build_candidate(parsed: dict, model_atoms: list[dict], model_bonds: list[dict],
                    image_size: tuple[int, int], box: tuple[int, int, int, int],
                    mapping: dict) -> dict:
    left, top, right, bottom = box
    width = max(1, int(right) - int(left) + 10)
    height = max(1, int(bottom) - int(top) + 10)
    model_to_molfile = mapping["model_to_molfile"]

    confidence_by_pair = {}
    for bond in model_bonds:
        endpoints = list(bond.get("endpoint_atoms", ()))
        if len(endpoints) == 2:
            confidence_by_pair[frozenset((int(endpoints[0]), int(endpoints[1])))] = float(
                bond.get("confidence", 1.0)
            )

    atoms = []
    for mi, model_atom in enumerate(model_atoms):
        mfj = model_to_molfile[mi]
        molfile_atom = parsed["atoms"][mfj]
        x_px = (int(left) - 5) + float(model_atom["x"]) * width
        y_px = (int(top) - 5) + float(model_atom["y"]) * height
        alias = molfile_atom.get("alias") or _model_alias_key(model_atom.get("atom_symbol", ""))
        atoms.append({
            "candidate_index": mi,
            "model_index": mi,
            "molfile_index": mfj,
            "model_symbol": model_atom.get("atom_symbol", ""),
            "molfile_symbol": molfile_atom.get("symbol", ""),
            "symbol": molfile_atom.get("symbol", "") or model_atom.get("atom_symbol", ""),
            "element": int(molfile_atom.get("element") or 0),
            "alias": alias or "",
            "charge": int(molfile_atom.get("charge") or 0),
            "isotope": int(molfile_atom.get("isotope") or 0),
            "h_count": molfile_atom.get("h_count"),
            "x_norm": float(model_atom["x"]),
            "y_norm": float(model_atom["y"]),
            "x_px": x_px,
            "y_px": y_px,
            "confidence": float(model_atom.get("confidence", 1.0)),
        })

    atoms_by_model = {atom["model_index"]: atom for atom in atoms}
    bonds = []
    for idx, bond in enumerate(parsed["bonds"]):
        begin_model = mapping["molfile_to_model"][bond["begin"]]
        end_model = mapping["molfile_to_model"][bond["end"]]
        begin_atom = atoms_by_model[begin_model]
        end_atom = atoms_by_model[end_model]
        bonds.append({
            "candidate_index": idx,
            "begin": begin_model,
            "end": end_model,
            "model_begin": begin_model,
            "model_end": end_model,
            "molfile_begin": bond["begin"],
            "molfile_end": bond["end"],
            "order": int(bond["order"]),
            "stereo": int(bond["stereo"]),
            "bond_type": {1: "single", 2: "double", 3: "triple", 4: "aromatic"}.get(
                int(bond["order"]), "single"
            ),
            "confidence": confidence_by_pair.get(frozenset((begin_model, end_model)), 1.0),
            "length_px": math.hypot(
                float(begin_atom["x_px"]) - float(end_atom["x_px"]),
                float(begin_atom["y_px"]) - float(end_atom["y_px"]),
            ),
        })

    return {
        "atoms": atoms,
        "bonds": bonds,
        "components": connected_components(len(atoms), bonds),
        "mapping": mapping,
        "image_size": [int(image_size[0]), int(image_size[1])],
        "preprocess_box": [int(left), int(top), int(right), int(bottom)],
        "pad": 5,
    }


def subset_candidate(candidate: dict, keep: list[int]) -> dict:
    ordered = sorted(set(int(i) for i in keep))
    if not ordered:
        raise ValueError("候选分子没有原子")
    remap = {old: new for new, old in enumerate(ordered)}
    atoms = []
    for new, old in enumerate(ordered):
        atom = dict(candidate["atoms"][old])
        atom["source_candidate_index"] = old
        atom["candidate_index"] = new
        atoms.append(atom)
    bonds = []
    for bond in candidate["bonds"]:
        if bond["begin"] in remap and bond["end"] in remap:
            item = dict(bond)
            item["source_candidate_index"] = bond["candidate_index"]
            item["candidate_index"] = len(bonds)
            item["begin"] = remap[bond["begin"]]
            item["end"] = remap[bond["end"]]
            bonds.append(item)
    return {
        "atoms": atoms,
        "bonds": bonds,
        "components": connected_components(len(atoms), bonds),
        "mapping": candidate["mapping"],
        "image_size": list(candidate["image_size"]),
        "preprocess_box": list(candidate["preprocess_box"]),
        "pad": candidate["pad"],
        "selected_from": ordered,
    }


def _cdxml_h_count(atom: dict):
    """Hydrogens we may safely freeze in CDXML.

    Carbon skeletons stay on the host's valence calculation so that editing a
    bond updates the implied hydrogens.  Heteroatoms, charged/isotopic atoms and
    explicit hydrogen counts must be written, otherwise OH/NH/charged N would be
    silently changed.
    """
    value = atom.get("h_count")
    if value is None:
        return None
    element = int(atom.get("element") or 0)
    if element in (0, 1):
        return None
    if element == 6 and not (atom.get("isotope") or atom.get("charge")):
        return None
    return max(0, min(8, int(value)))


def _median_bond_length(candidate: dict, default: float = 1.0) -> float:
    lengths = []
    for bond in candidate["bonds"]:
        value = float(bond.get("length_px", 0.0))
        if math.isfinite(value) and value > 1e-6:
            lengths.append(value)
    if not lengths:
        return default
    lengths.sort()
    return lengths[len(lengths) // 2]


def build_cdxml(candidate: dict) -> str:
    """Build standard CDXML from the final candidate graph.

    Canonical elements do NOT get a hand-written <t> label.  The host derives
    the label from Element + Charge + Isotope + NumHydrogens, so hydrogen counts
    stay chemical semantics instead of becoming display-only text.  Nickname
    atoms keep their printed abbreviation in <t>.
    """
    atoms = candidate["atoms"]
    bonds = candidate["bonds"]
    median = _median_bond_length(candidate, 1.0)
    scale = 30.0 / max(median, 1e-6)
    xs = [float(atom["x_px"]) for atom in atoms]
    ys = [float(atom["y_px"]) for atom in atoms]
    cx = (min(xs) + max(xs)) * 0.5 if xs else 0.0
    cy = (min(ys) + max(ys)) * 0.5 if ys else 0.0

    root = ET.Element("CDXML", {
        "BondLength": "30", "LabelFont": "3", "LabelSize": "10",
        "LineWidth": "0.6", "BondSpacing": "18",
    })
    fonts = ET.SubElement(root, "fonttable")
    ET.SubElement(fonts, "font", {"id": "3", "charset": "iso-8859-1", "name": "Arial"})
    page = ET.SubElement(root, "page", {"id": "1"})
    frag = ET.SubElement(page, "fragment", {"id": "2"})

    for idx, atom in enumerate(atoms):
        alias = str(atom.get("alias") or "")
        element = int(atom.get("element") or 0)
        attrs = {
            "id": str(10 + idx),
            "Element": "6" if alias else str(element if element > 0 else 6),
            "p": f"{(float(atom['x_px']) - cx) * scale:.8f} {(float(atom['y_px']) - cy) * scale:.8f}",
        }
        if alias:
            attrs["NodeType"] = "Nickname"
        else:
            if int(atom.get("charge") or 0):
                attrs["Charge"] = str(int(atom["charge"]))
            if int(atom.get("isotope") or 0):
                attrs["Isotope"] = str(int(atom["isotope"]))
            h_count = _cdxml_h_count(atom)
            if h_count is not None:
                attrs["NumHydrogens"] = str(h_count)
            if element == 0:
                attrs["NodeType"] = "RGroup"
            if element == 6 and (atom.get("charge") or atom.get("isotope")):
                attrs["LabelDisplay"] = "Shown"
            if element == 1:
                attrs["LabelDisplay"] = "Shown"
        node = ET.SubElement(frag, "n", attrs)
        if alias:
            text = ET.SubElement(node, "t")
            ET.SubElement(text, "s", {"font": "3", "size": "10", "face": "96"}).text = alias
        elif element == 0:
            text = ET.SubElement(node, "t")
            label = atom.get("model_symbol") or atom.get("symbol") or "R"
            label = _strip_brackets(label) or "R"
            ET.SubElement(text, "s", {"font": "3", "size": "10", "face": "96"}).text = label

    for idx, bond in enumerate(bonds):
        order = int(bond.get("order") or 1)
        attrs = {
            "id": str(1000 + idx),
            "B": str(10 + int(bond["begin"])),
            "E": str(10 + int(bond["end"])),
            "Order": "1.5" if order == 4 else str(order),
        }
        if int(bond.get("stereo") or 0) == 1:
            attrs["Display"] = "WedgeBegin"
        elif int(bond.get("stereo") or 0) == 6:
            attrs["Display"] = "WedgedHashBegin"
        elif int(bond.get("stereo") or 0) == 3 and order == 2:
            # ChemDraw's Wavy double bond means "E/Z explicitly unknown".
            # Writing this is what prevents a trans-looking 2D layout from
            # being silently re-interpreted as E by the reader.
            attrs["Display"] = "Wavy"
        ET.SubElement(frag, "b", attrs)
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding="unicode")


def element_number(symbol: str) -> int:
    from rdkit import Chem
    try:
        return int(Chem.GetPeriodicTable().GetAtomicNumber(symbol))
    except Exception:
        return 6


def _element_symbol(number: int) -> str:
    from rdkit import Chem
    try:
        return Chem.GetPeriodicTable().GetElementSymbol(int(number))
    except Exception:
        return "C"


def parse_cdxml(cdxml: str) -> dict:
    root = ET.fromstring(cdxml.split("?>", 1)[-1] if cdxml.startswith("<?xml") else cdxml)
    nodes = []
    id_to_index = {}
    for node in root.iter("n"):
        node_id = int(node.get("id"))
        p = (node.get("p") or "0 0").split()
        x, y = float(p[0]), float(p[1]) if len(p) > 1 else 0.0
        alias = ""
        if node.get("NodeType") == "Nickname":
            alias = "".join(part.text or "" for part in node.iter("s")).strip()
        nodes.append({
            "id": node_id,
            "index": len(nodes),
            "element": int(node.get("Element") or 6),
            "x": x,
            "y": y,
            "charge": int(node.get("Charge") or 0),
            "isotope": int(node.get("Isotope") or 0),
            "h_count": int(node.get("NumHydrogens")) if node.get("NumHydrogens") is not None else None,
            "alias": alias,
            "node_type": node.get("NodeType") or "Element",
        })
        id_to_index[node_id] = len(nodes) - 1
    bonds = []
    for bond in root.iter("b"):
        display = (bond.get("Display") or "").strip()
        stereo = 1 if display in ("WedgeBegin", "WedgeEnd") else 6 if display in (
            "WedgedHashBegin", "WedgedHashEnd", "HashBegin", "HashEnd"
        ) else 3 if display == "Wavy" else 0
        begin = id_to_index.get(int(bond.get("B")))
        end = id_to_index.get(int(bond.get("E")))
        if begin is None or end is None:
            raise ValueError("CDXML 键端点无法解析")
        order_text = bond.get("Order") or "1"
        order = 4 if order_text == "1.5" else int(float(order_text))
        bonds.append({
            "index": len(bonds),
            "begin": begin,
            "end": end,
            "order": order,
            "stereo": stereo,
            "display": display,
        })
    return {"atoms": nodes, "bonds": bonds, "id_to_index": id_to_index}


def check_cdxml_consistency(candidate: dict, cdxml: str) -> dict:
    errors = []
    try:
        parsed = parse_cdxml(cdxml)
    except Exception as exc:
        return {"status": "failed", "errors": ["CDXML 无法回读：" + str(exc)[:300]]}
    if len(parsed["atoms"]) != len(candidate["atoms"]):
        errors.append(f"CDXML 原子数 {len(parsed['atoms'])} 与候选 {len(candidate['atoms'])} 不一致")
    if len(parsed["bonds"]) != len(candidate["bonds"]):
        errors.append(f"CDXML 键数 {len(parsed['bonds'])} 与候选 {len(candidate['bonds'])} 不一致")
    for idx, atom in enumerate(candidate["atoms"]):
        if idx >= len(parsed["atoms"]):
            break
        got = parsed["atoms"][idx]
        alias = str(atom.get("alias") or "")
        if got["alias"] != alias:
            errors.append(f"原子 {idx} 缩写丢失：{alias!r} -> {got['alias']!r}")
        if not alias:
            if got["element"] != int(atom.get("element") or 0) and int(atom.get("element") or 0) != 0:
                errors.append(f"原子 {idx} 元素丢失：{atom.get('element')} -> {got['element']}")
            if got["charge"] != int(atom.get("charge") or 0):
                errors.append(f"原子 {idx} 电荷丢失：{atom.get('charge')} -> {got['charge']}")
            if got["isotope"] != int(atom.get("isotope") or 0):
                errors.append(f"原子 {idx} 同位素丢失：{atom.get('isotope')} -> {got['isotope']}")
            expected_h = _cdxml_h_count(atom)
            if expected_h is not None and got["h_count"] != int(expected_h):
                errors.append(f"原子 {idx} 氢数丢失：{expected_h} -> {got['h_count']}")
    for idx, bond in enumerate(candidate["bonds"]):
        if idx >= len(parsed["bonds"]):
            break
        got = parsed["bonds"][idx]
        if (got["begin"], got["end"]) != (int(bond["begin"]), int(bond["end"])):
            errors.append(f"键 {idx} 端点映射错误")
        if got["order"] != int(bond.get("order") or 1):
            errors.append(f"键 {idx} 键级丢失：{bond.get('order')} -> {got['order']}")
        expected_stereo = int(bond.get("stereo") or 0)
        if expected_stereo in (1, 3, 6) and got["stereo"] != expected_stereo:
            errors.append(f"键 {idx} 立体编码丢失：{expected_stereo} -> {got['stereo']}")
        elif expected_stereo not in (1, 3, 6) and got["stereo"] in (1, 6):
            errors.append(f"键 {idx} 出现了模型未指定的楔键：{got['stereo']}")

    # Affine check: the candidate pixel graph may only be translated and
    # uniformly scaled into CDXML points.  A negative or unequal scale would be
    # a mirror / anisotropic stretch and must fail.
    if len(candidate["atoms"]) >= 2:
        xs = [float(a["x_px"]) for a in candidate["atoms"]]
        ys = [float(a["y_px"]) for a in candidate["atoms"]]
        X = [float(a["x"]) for a in parsed["atoms"]]
        Y = [float(a["y"]) for a in parsed["atoms"]]
        mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
        mX, mY = sum(X) / len(X), sum(Y) / len(Y)
        vx = sum((x - mx) ** 2 for x in xs)
        vy = sum((y - my) ** 2 for y in ys)
        ax = sum((xs[i] - mx) * (X[i] - mX) for i in range(len(xs))) / vx if vx > 1e-12 else 0.0
        ay = sum((ys[i] - my) * (Y[i] - mY) for i in range(len(ys))) / vy if vy > 1e-12 else 0.0
        if vx > 1e-12 and vy > 1e-12:
            if ax <= 0 or ay <= 0 or abs(ax - ay) > max(1e-6, abs(ax) * 1e-5):
                errors.append(f"坐标变换不是等比正缩放：sx={ax:.8f}, sy={ay:.8f}")
            bx, by = mX - ax * mx, mY - ay * my
            residual = max(
                max(abs(ax * xs[i] + bx - X[i]), abs(ay * ys[i] + by - Y[i]))
                for i in range(len(xs))
            )
            if residual > 1e-5:
                errors.append(f"坐标回读残差过大：{residual:.8f}")
    return {
        "status": "passed" if not errors else "failed",
        "errors": errors,
        "atom_count": len(parsed["atoms"]),
        "bond_count": len(parsed["bonds"]),
    }


def _build_rdkit_from_candidate(candidate: dict, y_sign: float = -1.0) -> object:
    from rdkit import Chem

    rw = Chem.RWMol()
    for atom in candidate["atoms"]:
        element = int(atom.get("element") or 0)
        if element == 0:
            rd_atom = Chem.Atom(0)
            if atom.get("alias"):
                rd_atom.SetProp("dummyLabel", str(atom["alias"]))
        else:
            rd_atom = Chem.Atom(element)
        if int(atom.get("charge") or 0):
            rd_atom.SetFormalCharge(int(atom["charge"]))
        if int(atom.get("isotope") or 0):
            rd_atom.SetIsotope(int(atom["isotope"]))
        if atom.get("h_count") is not None and element not in (0, 1):
            rd_atom.SetNumExplicitHs(int(atom["h_count"]))
        rw.AddAtom(rd_atom)
    for bond in candidate["bonds"]:
        order = {1: Chem.BondType.SINGLE, 2: Chem.BondType.DOUBLE,
                 3: Chem.BondType.TRIPLE, 4: Chem.BondType.AROMATIC}.get(
            int(bond.get("order") or 1), Chem.BondType.SINGLE
        )
        rw.AddBond(int(bond["begin"]), int(bond["end"]), order)
    mol = rw.GetMol()
    conf = Chem.Conformer(mol.GetNumAtoms())
    conf.Set3D(False)
    for idx, atom in enumerate(candidate["atoms"]):
        conf.SetAtomPosition(idx, (float(atom["x_px"]), y_sign * float(atom["y_px"]), 0.0))
    mol.AddConformer(conf, assignId=True)
    for atom in mol.GetAtoms():
        atom.SetChiralTag(Chem.ChiralType.CHI_UNSPECIFIED)
    for bond in mol.GetBonds():
        bond.SetBondDir(Chem.BondDir.NONE)
    for bond in candidate["bonds"]:
        if int(bond.get("stereo") or 0) in (1, 6):
            rd_bond = mol.GetBondBetweenAtoms(int(bond["begin"]), int(bond["end"]))
            if rd_bond is not None:
                rd_bond.SetBondDir(
                    Chem.BondDir.BEGINWEDGE if int(bond["stereo"]) == 1 else Chem.BondDir.BEGINDASH
                )
    try:
        Chem.AssignChiralTypesFromBondDirs(mol)
        Chem.AssignStereochemistry(mol, force=True, cleanIt=True)
    except Exception:
        pass
    return mol


def _ez_label(bond) -> str:
    """Return the CIP E/Z label of an already-assigned RDKit bond.

    This is deliberately not derived from "which side of the double bond the
    first neighbour happens to lie on".  RDKit assigns E/Z from the CIP
    priorities and the 2D/3D geometry; the descriptor is the chemical name.
    """
    from rdkit import Chem
    stereo = bond.GetStereo()
    if stereo == Chem.BondStereo.STEREOE:
        return "E"
    if stereo == Chem.BondStereo.STEREOZ:
        return "Z"
    if stereo == getattr(Chem.BondStereo, "STEREOTRANS", None):
        return "E"
    if stereo == getattr(Chem.BondStereo, "STEREOCIS", None):
        return "Z"
    return ""


def _stereo_state(bond) -> str:
    from rdkit import Chem
    label = _ez_label(bond)
    if label:
        return label
    if bond.GetStereo() == Chem.BondStereo.STEREOANY:
        return "UNSPECIFIED"
    return "NONE"


def _bond_order_int(bond) -> int:
    from rdkit import Chem
    if bond.GetBondType() == Chem.BondType.AROMATIC:
        return 4
    try:
        return int(round(float(bond.GetBondTypeAsDouble())))
    except Exception:
        return 1


def _candidate_bond(candidate: dict, a: int, b: int):
    for bond in candidate["bonds"]:
        if {int(bond["begin"]), int(bond["end"])} == {int(a), int(b)}:
            return bond
    return None


def _readback_atom_map(candidate: dict, cdx_parsed: dict, readback) -> tuple[list[int] | None, str]:
    """Verify that RDKit's CDXML readback is atom-for-atom the candidate graph.

    The mapping is not inferred from neighbour traversal order.  Our CDXML
    writer emits nodes in candidate order; this function checks that the
    readback preserved that order (element/charge/isotope/hydrogen and every
    bond), and that the 2D coordinates differ only by the expected uniform
    scale + translation + y-axis convention.  If any check fails we return no
    mapping and the caller records "not_verified" instead of guessing.
    """
    from rdkit import Chem
    if readback is None:
        return None, "RDKit CDXML 回读未返回分子"
    n = len(candidate["atoms"])
    if readback.GetNumAtoms() != n:
        return None, f"RDKit 回读原子数 {readback.GetNumAtoms()} 与候选 {n} 不一致"
    if cdx_parsed is None or len(cdx_parsed["atoms"]) != n:
        return None, "CDXML 节点数与候选不一致，无法建立显式原子映射"
    for idx, atom in enumerate(candidate["atoms"]):
        host = readback.GetAtomWithIdx(idx)
        node = cdx_parsed["atoms"][idx]
        alias = str(atom.get("alias") or "")
        expected_element = int(atom.get("element") or 0)
        if not alias and int(host.GetAtomicNum()) != expected_element:
            return None, f"原子 {idx} 元素在 RDKit 回读中改变"
        if alias and int(host.GetAtomicNum()) not in (0, expected_element):
            return None, f"原子 {idx} 缩写元素在 RDKit 回读中改变"
        if int(host.GetFormalCharge()) != int(atom.get("charge") or 0):
            return None, f"原子 {idx} 电荷在 RDKit 回读中改变"
        if int(host.GetIsotope()) != int(atom.get("isotope") or 0):
            return None, f"原子 {idx} 同位素在 RDKit 回读中改变"
        expected_h = atom.get("h_count")
        if not alias and expected_h is not None:
            if int(host.GetTotalNumHs()) != int(expected_h):
                return None, f"原子 {idx} 氢数在 RDKit 回读中改变"
        if int(node.get("element") or 0) != expected_element and not alias:
            return None, f"原子 {idx} 在 CDXML 中的元素与候选不一致"

    expected_bonds = set()
    for bond in candidate["bonds"]:
        a, b = sorted((int(bond["begin"]), int(bond["end"])))
        expected_bonds.add((a, b, int(bond.get("order") or 1)))
    got_bonds = set()
    for bond in readback.GetBonds():
        a, b = sorted((int(bond.GetBeginAtomIdx()), int(bond.GetEndAtomIdx())))
        got_bonds.add((a, b, _bond_order_int(bond)))
    if expected_bonds != got_bonds:
        return None, "RDKit 回读的键连接/键级与候选不一致"

    conf = readback.GetConformer()
    src_x = [float(node["x"]) for node in cdx_parsed["atoms"]]
    src_y = [-float(node["y"]) for node in cdx_parsed["atoms"]]
    dst_x = [float(conf.GetAtomPosition(i).x) for i in range(n)]
    dst_y = [float(conf.GetAtomPosition(i).y) for i in range(n)]

    def fit(src, dst):
        ms = sum(src) / len(src)
        md = sum(dst) / len(dst)
        den = sum((v - ms) ** 2 for v in src)
        if den < 1e-12:
            return 0.0, md
        scale = sum((src[i] - ms) * (dst[i] - md) for i in range(len(src))) / den
        return scale, md - scale * ms

    sx, tx = fit(src_x, dst_x)
    sy, ty = fit(src_y, dst_y)
    if sy <= 0:
        # Some readers keep the CDXML y direction; accept that convention only
        # after re-fitting, never by mirroring the chemistry.
        src_y = [-v for v in src_y]
        sy, ty = fit(src_y, dst_y)
    if sx <= 0 or sy <= 0:
        return None, "RDKit 回读坐标包含负缩放或镜像"
    if abs(sx - sy) > max(1e-6, abs(sx) * 1e-4):
        return None, f"RDKit 回读坐标不是等比缩放：sx={sx:.8f}, sy={sy:.8f}"
    residual = 0.0
    for i in range(n):
        residual = max(residual, abs(sx * src_x[i] + tx - dst_x[i]),
                       abs(sy * src_y[i] + ty - dst_y[i]))
    if residual > 1e-4 * max(1.0, abs(sx)):
        return None, f"RDKit 回读坐标残差过大：{residual:.8f}"
    return list(range(n)), ""


def validate_stereo(parsed: dict, candidate: dict, cdxml: str) -> dict:
    """Validate the stereo that the model actually specified.

    The comparison layers are explicit:
      * model Molfile -> candidate graph (index map, charges, isotopes, H);
      * candidate graph -> CDXML (structure/geometry/wedge encoding);
      * CDXML -> RDKit Chem.MolsFromCDXML (this is NOT the MoleculeStudio host
        reader; the host round-trip is a separate, explicitly recorded layer).

    E/Z applicability is decided before preservation.  A double bond whose two
    ends do not each carry two different substituents (including implicit H)
    has no E/Z and is "not_applicable"; a definable but unspecified double
    bond is "not_specified" and is preserved with a Wavy bond, never guessed.
    """
    from rdkit import Chem

    errors = []
    not_verified = []
    checked = []
    double_bonds = []
    readback_status = "not_applicable"
    readback_atoms = 0

    # --- wedge endpoint layer (model Molfile -> candidate) -----------------
    wedge_bonds = []
    for bond in parsed["bonds"]:
        if int(bond.get("stereo") or 0) not in (1, 6):
            continue
        begin = candidate["mapping"]["molfile_to_model"][bond["begin"]]
        end = candidate["mapping"]["molfile_to_model"][bond["end"]]
        if not (0 <= begin < len(candidate["atoms"]) and 0 <= end < len(candidate["atoms"])):
            errors.append(f"模型楔键端点越界：{bond['begin']}->{bond['end']}")
            continue
        wedge_bonds.append((begin, end, int(bond["stereo"])))
    endpoint_validation = {
        "status": "passed" if not errors else "failed",
        "checked": len(wedge_bonds),
        "errors": list(errors),
    }

    # --- candidate -> CDXML wedge/unknown encoding --------------------------
    cdx_parsed = None
    try:
        cdx_parsed = parse_cdxml(cdxml)
    except Exception as exc:
        not_verified.append("CDXML 无法回读：" + str(exc)[:200])
    if cdx_parsed is not None:
        cdx_wedges = sorted(
            (int(b["begin"]), int(b["end"]), int(b["stereo"]))
            for b in cdx_parsed["bonds"] if int(b["stereo"]) in (1, 6)
        )
        if sorted(wedge_bonds) != cdx_wedges:
            errors.append(f"楔键端点/方向在 CDXML 中改变：模型 {sorted(wedge_bonds)} / CDXML {cdx_wedges}")
        elif wedge_bonds:
            checked.append({"kind": "wedge_bonds", "count": len(wedge_bonds), "status": "passed"})

    reference = None
    try:
        reference = Chem.MolFromMolBlock(parsed.get("molfile", ""), removeHs=False)
    except Exception:
        reference = None
    if reference is None:
        not_verified.append("无法从 Molfile 重建 RDKit 参考分子")
        status = "not_verified"
        return {
            "status": status,
            "endpoint_validation": endpoint_validation,
            "checked": checked,
            "errors": errors,
            "not_verified_reason": "；".join(dict.fromkeys(not_verified)),
            "specified_wedges": len(wedge_bonds),
            "stereo_preserved": False,
            "readback_engine": "RDKit Chem.MolsFromCDXML",
            "readback_status": "not_verified",
            "host_roundtrip": False,
            "double_bonds": double_bonds,
            "unspecified_double_bonds": [],
        }

    try:
        Chem.AssignStereochemistry(reference, force=True, cleanIt=True)
    except Exception:
        pass
    try:
        ref_centers = dict(Chem.FindMolChiralCenters(
            reference, includeUnassigned=True, useLegacyImplementation=False
        ))
    except Exception:
        ref_centers = {}
    molfile_to_candidate = {
        int(atom["molfile_index"]): int(atom["candidate_index"]) for atom in candidate["atoms"]
    }
    ref_specified = []
    for atom in reference.GetAtoms():
        if atom.GetChiralTag() != Chem.ChiralType.CHI_UNSPECIFIED:
            ref_specified.append(int(atom.GetIdx()))

    potential = {}
    try:
        for info in Chem.FindPotentialStereo(reference, cleanIt=False, flagPossible=True):
            if info.type == Chem.StereoType.Bond_Double:
                potential[int(info.centeredOn)] = info
    except Exception as exc:
        not_verified.append("RDKit 无法判断双键 E/Z 适用性：" + str(exc)[:200])

    # Record every double bond, including ones that chemically cannot have E/Z.
    for bond in reference.GetBonds():
        if bond.GetBondType() != Chem.BondType.DOUBLE:
            continue
        mf_begin, mf_end = int(bond.GetBeginAtomIdx()), int(bond.GetEndAtomIdx())
        cand_begin = molfile_to_candidate.get(mf_begin)
        cand_end = molfile_to_candidate.get(mf_end)
        item = {
            "molfile_begin": mf_begin,
            "molfile_end": mf_end,
            "begin": cand_begin,
            "end": cand_end,
            "applicable": bond.GetIdx() in potential,
            "specified": False,
            "reference": _ez_label(bond),
            "host": "",
            "status": "not_applicable",
            "reason": "",
        }
        if bond.GetIdx() not in potential:
            item["reason"] = "该双键端点没有两个不同取代基（含隐式 H），化学上不存在 E/Z"
        double_bonds.append(item)

    # No stereo at all: no host readback is needed for a "not_applicable" claim.
    if not wedge_bonds and not ref_specified and not potential:
        status = "failed" if errors else ("not_verified" if not_verified else "not_applicable")
        return {
            "status": status,
            "endpoint_validation": endpoint_validation,
            "checked": checked,
            "errors": errors,
            "not_verified_reason": "；".join(dict.fromkeys(not_verified)),
            "specified_wedges": len(wedge_bonds),
            "stereo_preserved": False if status == "failed" else None,
            "readback_engine": "RDKit Chem.MolsFromCDXML",
            "readback_status": "not_applicable",
            "host_roundtrip": False,
            "double_bonds": double_bonds,
            "unspecified_double_bonds": [],
        }

    # --- CDXML -> RDKit readback (explicitly NOT the MoleculeStudio host) ---
    readback = None
    try:
        mols = Chem.MolsFromCDXML(cdxml, removeHs=False)
        if mols:
            readback = mols[0]
            for extra in mols[1:]:
                readback = Chem.CombineMols(readback, extra)
    except Exception as exc:
        not_verified.append("RDKit CDXML 回读失败：" + str(exc)[:200])
    atom_map = None
    if readback is None:
        not_verified.append("RDKit CDXML 回读未返回分子")
        readback_status = "not_verified"
    else:
        readback_atoms = int(readback.GetNumAtoms())
        atom_map, map_error = _readback_atom_map(candidate, cdx_parsed, readback)
        if atom_map is None:
            not_verified.append("RDKit 回读原子映射不可靠：" + map_error)
            readback_status = "not_verified"
        else:
            readback_status = "passed"
        try:
            Chem.AssignStereochemistry(readback, force=True, cleanIt=True)
        except Exception:
            pass

    if atom_map is not None:
        try:
            cdx_centers = dict(Chem.FindMolChiralCenters(
                readback, includeUnassigned=True, useLegacyImplementation=False
            ))
        except Exception:
            cdx_centers = {}
        for mf_idx in ref_specified:
            cand_idx = molfile_to_candidate.get(mf_idx)
            if cand_idx is None:
                not_verified.append(f"Molfile 手性原子 {mf_idx} 没有候选映射")
                continue
            expected = ref_centers.get(mf_idx, "?")
            got = cdx_centers.get(cand_idx, "?")
            atom = candidate["atoms"][cand_idx]
            dummy_near = atom.get("alias") or any(
                candidate["atoms"][int(b["end"] if int(b["begin"]) == cand_idx else b["begin"])].get("alias")
                for b in candidate["bonds"]
                if cand_idx in (int(b["begin"]), int(b["end"]))
            )
            if dummy_near and expected == got:
                not_verified.append(f"原子 {cand_idx} 的缩写参与立体中心，CIP 仅按缩写字面原子校验")
            if expected != got:
                errors.append(f"手性中心 {cand_idx} CIP 改变：{expected} -> {got}")
            else:
                checked.append({"kind": "tetrahedral", "atom": cand_idx,
                                "cip": expected, "status": "passed"})

        for item in double_bonds:
            if not item["applicable"]:
                continue
            begin, end = item["begin"], item["end"]
            if begin is None or end is None:
                not_verified.append("双键原子没有候选映射")
                continue
            candidate_bond = _candidate_bond(candidate, begin, end)
            if candidate_bond is None:
                errors.append(f"候选图中找不到双键 {begin}={end}")
                item["status"] = "failed"
                continue
            ref_label = item["reference"]
            host_bond = readback.GetBondBetweenAtoms(int(atom_map[begin]), int(atom_map[end]))
            if host_bond is None:
                errors.append(f"RDKit 回读中找不到双键 {begin}={end}")
                item["status"] = "failed"
                continue
            host_label = _ez_label(host_bond)
            if not host_label and host_bond.GetStereo() == Chem.BondStereo.STEREOANY:
                host_label = "UNSPECIFIED"
            item["host"] = host_label
            item["specified"] = bool(ref_label)
            if ref_label:
                item["status"] = "passed" if host_label == ref_label else "failed"
                if host_label != ref_label:
                    errors.append(
                        f"双键 {begin}={end} 的 E/Z 改变：{ref_label} -> {host_label or '未指定'}"
                    )
                else:
                    checked.append({"kind": "double_bond", "begin": begin, "end": end,
                                    "reference": ref_label, "host": host_label,
                                    "geometry": ref_label, "status": "passed"})
            else:
                # The model explicitly did not specify E/Z.  If the readback
                # invents one, that is a conversion failure, not a pass.
                if host_label in ("E", "Z"):
                    item["status"] = "failed"
                    errors.append(
                        f"双键 {begin}={end} 在模型中未指定 E/Z，但 CDXML 被解释为 {host_label}"
                    )
                else:
                    item["status"] = "not_specified"
                    item["reason"] = "模型未指定 E/Z；CDXML 以 Wavy 双键保留未指定状态"
                    checked.append({"kind": "double_bond", "begin": begin, "end": end,
                                    "reference": "未指定", "host": "未指定",
                                    "status": "not_specified"})

    unspecified = [
        {"begin": item["begin"], "end": item["end"]}
        for item in double_bonds if item.get("status") == "not_specified"
    ]
    if errors:
        status = "failed"
    elif not_verified:
        status = "not_verified"
    elif unspecified:
        status = "not_specified"
    elif checked:
        status = "passed"
    else:
        status = "not_applicable"
    return {
        "status": status,
        "endpoint_validation": endpoint_validation,
        "checked": checked,
        "errors": errors,
        "not_verified_reason": "；".join(dict.fromkeys(not_verified)),
        "specified_wedges": len(wedge_bonds),
        "stereo_preserved": True if status == "passed" else (False if status == "failed" else None),
        "readback_engine": "RDKit Chem.MolsFromCDXML",
        "readback_status": readback_status,
        "readback_atoms": readback_atoms,
        "host_roundtrip": False,
        "double_bonds": double_bonds,
        "unspecified_double_bonds": unspecified,
    }


# ---------------------------------------------------------------------------
# Candidate preview / overlay rendering
# ---------------------------------------------------------------------------

def crop_white_box(image) -> tuple[int, int, int, int]:
    """The exact CropWhite ink box used by MolScribe's inference transform."""
    import numpy as np
    arr = np.asarray(image.convert("RGB"))
    mask = np.any(arr != 255, axis=2)
    if not mask.any():
        return 0, 0, image.width, image.height
    rows = np.where(mask.any(axis=1))[0]
    cols = np.where(mask.any(axis=0))[0]
    return int(cols[0]), int(rows[0]), int(cols[-1]) + 1, int(rows[-1]) + 1


def _preview_font(size: int):
    from PIL import ImageFont
    for name in ("arial.ttf", "segoeui.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except Exception:
            continue
    return ImageFont.load_default()


def _charge_suffix(charge: int) -> str:
    if not charge:
        return ""
    marker = "+" if charge > 0 else "−"
    return marker if abs(charge) == 1 else f"{abs(charge)}{marker}"


def _hydrogen_suffix(h_count) -> str:
    if h_count is None:
        return ""
    try:
        h = int(h_count)
    except Exception:
        return ""
    if h <= 0:
        return ""
    return "H" if h == 1 else f"H{h}"


def _display_label(atom: dict) -> str:
    """Compose one review label without any early return losing chemistry.

    Order is isotope prefix, element/alias, hydrogen count, charge suffix.
    Ordinary skeletal carbon stays hidden; carbon with isotope or charge is
    shown even when the host would normally suppress its element symbol.
    Nickname labels (Ph, OEt, ...) remain exact unless they themselves carry
    an isotope/charge, in which case those attributes are appended rather than
    thrown away.
    """
    alias = str(atom.get("alias") or "")
    element = int(atom.get("element") or 0)
    isotope = int(atom.get("isotope") or 0)
    charge = int(atom.get("charge") or 0)
    h_count = atom.get("h_count")
    prefix = str(isotope) if isotope else ""
    suffix = _charge_suffix(charge)

    if alias:
        # Preserve the printed abbreviation verbatim; only add chemistry that
        # would otherwise be invisible.
        return f"{prefix}{alias}{suffix}"

    symbol = _element_symbol(element)
    if element == 6 and not isotope and not charge:
        # Skeletal carbon is normally implicit.  Hydrogens alone do not make
        # it a label; charged/isotopic carbon is handled below.
        return ""
    if element == 1:
        # Explicit hydrogen/deuterium/tritium atom.  D and T are written as
        # 2H/3H so the isotope and the element are both unambiguous.
        return f"{prefix}{symbol}{_hydrogen_suffix(h_count)}{suffix}"
    if element == 0:
        return f"{prefix}{symbol}{suffix}"

    label = f"{prefix}{symbol}{_hydrogen_suffix(h_count)}{suffix}"
    return label


def _draw_bond(draw, a: dict, b: dict, bond: dict, color, width: int) -> None:
    x1, y1 = float(a["x_px"]), float(a["y_px"])
    x2, y2 = float(b["x_px"]), float(b["y_px"])
    dx, dy = x2 - x1, y2 - y1
    length = max(1.0, math.hypot(dx, dy))
    nx, ny = -dy / length, dx / length
    order = int(bond.get("order") or 1)
    stereo = int(bond.get("stereo") or 0)
    if stereo == 1:
        draw.polygon((x1, y1, x2 + nx * width * 2.4, y2 + ny * width * 2.4,
                      x2 - nx * width * 2.4, y2 - ny * width * 2.4), fill=color)
        return
    if stereo == 6:
        steps = 7
        for i in range(1, steps):
            t = i / steps
            px, py = x1 + dx * t, y1 + dy * t
            half = width * 2.4 * t
            draw.line((px + nx * half, py + ny * half, px - nx * half, py - ny * half),
                      fill=color, width=max(1, width - 1))
        return
    if order == 2:
        offset = width * 1.6
        draw.line((x1 + nx * offset, y1 + ny * offset, x2 + nx * offset, y2 + ny * offset),
                  fill=color, width=width)
        draw.line((x1 - nx * offset, y1 - ny * offset, x2 - nx * offset, y2 - ny * offset),
                  fill=color, width=width)
    elif order == 3:
        offset = width * 1.8
        draw.line((x1, y1, x2, y2), fill=color, width=width)
        for sign in (-1, 1):
            draw.line((x1 + nx * offset * sign, y1 + ny * offset * sign,
                       x2 + nx * offset * sign, y2 + ny * offset * sign), fill=color, width=width)
    elif order == 4:
        draw.line((x1, y1, x2, y2), fill=color, width=width)
        offset = width * 1.6
        steps = max(2, int(length / 8))
        for i in range(steps):
            if i % 2:
                continue
            t0, t1 = i / steps, min(1.0, (i + 0.6) / steps)
            draw.line((x1 + dx * t0 + nx * offset, y1 + dy * t0 + ny * offset,
                       x1 + dx * t1 + nx * offset, y1 + dy * t1 + ny * offset),
                      fill=color, width=max(1, width - 1))
    else:
        draw.line((x1, y1, x2, y2), fill=color, width=width)


def _draw_labels(draw, atoms: list[dict], font, strong: bool = True,
                 image_size: tuple[int, int] | None = None) -> None:
    width, height = image_size if image_size else (0, 0)
    for atom in atoms:
        label = _display_label(atom)
        low = float(atom.get("confidence", 1.0)) < 0.75
        color = (190, 45, 35) if low else (10, 95, 175)
        if not label:
            if low:
                x, y = float(atom["x_px"]), float(atom["y_px"])
                r = 4
                draw.ellipse((x - r, y - r, x + r, y + r), outline=color, width=2)
            continue
        try:
            box = draw.textbbox((0, 0), label, font=font, stroke_width=2)
        except Exception:
            box = (0, 0, len(label) * 8, 14)
        w, h = box[2] - box[0], box[3] - box[1]
        x = float(atom["x_px"]) - w / 2
        y = float(atom["y_px"]) - h / 2
        # Labels for atoms near the crop edge must remain fully visible.  The
        # isotope prefix of 2H is exactly what gets clipped first if we let the
        # text box run outside the image.  Clamp only the review label; bond
        # geometry and overlay alignment are unchanged.
        if width and height:
            x = max(2.0, min(width - w - 2.0, x))
            y = max(2.0, min(height - h - 2.0, y))
        if strong:
            draw.rectangle((x - 3, y - 2, x + w + 3, y + h + 2), fill=(255, 255, 255))
        draw.text((x, y), label, font=font, fill=color, stroke_width=2,
                  stroke_fill=(255, 255, 255))


def draw_candidate_previews(crop_path: Path, candidate: dict, raw_model: dict, out_paths: dict) -> None:
    from PIL import Image, ImageDraw

    image = Image.open(crop_path).convert("RGB")
    median = _median_bond_length(candidate, 20.0)
    width = max(2, int(round(median * 0.055)))
    font = _preview_font(max(11, min(30, int(round(median * 0.24)))))
    atom_by_index = {int(atom["candidate_index"]): atom for atom in candidate["atoms"]}

    def paint(target, strong_labels: bool) -> None:
        draw = ImageDraw.Draw(target)
        for bond in candidate["bonds"]:
            begin = atom_by_index.get(int(bond["begin"]))
            end = atom_by_index.get(int(bond["end"]))
            if begin is None or end is None:
                continue
            _draw_bond(draw, begin, end, bond, (12, 105, 190), width)
        _draw_labels(draw, candidate["atoms"], font, strong_labels, image.size)

    result = Image.new("RGB", image.size, (255, 255, 255))
    paint(result, True)
    result.save(out_paths["result"], "PNG")

    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    paint(overlay, True)
    overlay.save(out_paths["overlay"], "PNG")
    soft = overlay.copy()
    soft.putalpha(soft.getchannel("A").point(lambda a: int(a * 0.78)))
    Image.alpha_composite(image.convert("RGBA"), soft).convert("RGB").save(out_paths["blend"], "PNG")

    raw_atoms = raw_model.get("atoms", [])
    raw_by_index = {i: atom for i, atom in enumerate(raw_atoms)}
    raw_overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    raw_draw = ImageDraw.Draw(raw_overlay)
    for bond in raw_model.get("bonds", []):
        endpoints = list(bond.get("endpoint_atoms", ()))
        if len(endpoints) != 2:
            continue
        begin = raw_by_index.get(int(endpoints[0]))
        end = raw_by_index.get(int(endpoints[1]))
        if begin is None or end is None:
            continue
        _draw_bond(raw_draw, begin, end, {"order": {"single": 1, "double": 2, "triple": 3,
                                                     "solid wedge": 1, "dashed wedge": 1}.get(
            str(bond.get("bond_type", "single")), 1),
            "stereo": 1 if bond.get("bond_type") == "solid wedge" else 6 if bond.get("bond_type") == "dashed wedge" else 0},
            (120, 90, 160), max(1, width - 1))
    raw_overlay.save(out_paths["raw_overlay"], "PNG")
    soft_raw = raw_overlay.copy()
    soft_raw.putalpha(soft_raw.getchannel("A").point(lambda a: int(a * 0.75)))
    Image.alpha_composite(image.convert("RGBA"), soft_raw).convert("RGB").save(
        out_paths["raw_blend"], "PNG"
    )


# ---------------------------------------------------------------------------
# Recognition pipeline
# ---------------------------------------------------------------------------

def recognize(engine: OcsrEngine, page_image: Path, crop_rect: tuple[int, int, int, int],
              cache: Path, records: Path, source: dict, dpi: int,
              prepared: dict | None = None) -> dict:
    from PIL import Image

    selection_scale = 1.0
    vector_info = None
    if prepared:
        prepared_path = Path(prepared.get("image") or "")
        if not prepared_path.is_file() or not safe_under(prepared_path, cache):
            raise ValueError("矢量裁剪图不存在或不在 OCSR 缓存目录中")
        raw = Image.open(prepared_path).convert("RGB")
        raw.load()
        x, y, w, h = (int(v) for v in prepared.get("crop_rect_px") or crop_rect)
        page_size_px = [int(v) for v in (prepared.get("page_size_px") or [raw.width, raw.height])]
        vector_info = prepared
        if raw.width < 40 or raw.height < 40:
            raise ValueError("矢量裁剪图太小，请重新框选")
    else:
        if not page_image.is_file():
            raise ValueError("页面图片不存在，请重新渲染")
        image = Image.open(page_image).convert("RGB")
        x, y, w, h = crop_rect
        if w < 40 or h < 40:
            raise ValueError("选区太小（至少 40×40 像素）")
        if max(w, h) > HARD_MAX_SELECTION_PX:
            raise ValueError(
                f"选区太大（{max(w, h)} 像素，超过 {HARD_MAX_SELECTION_PX} 像素上限），请缩小范围"
            )
        if max(w, h) > MAX_SELECTION_PX:
            selection_scale = MAX_SELECTION_PX / float(max(w, h))
        x = max(0, min(image.width - 1, x))
        y = max(0, min(image.height - 1, y))
        w = min(w, image.width - x)
        h = min(h, image.height - y)
        if w < 40 or h < 40:
            raise ValueError("选区超出页面范围或太小")
        page_size_px = [image.width, image.height]
        raw = image.crop((x, y, x + w, y + h))

    stamp = time.strftime("%Y%m%d-%H%M%S")
    nonce = hashlib.sha1(f"{time.time_ns()}|{os.getpid()}".encode()).hexdigest()[:6]
    stem = f"ocsr-{stamp}-{nonce}"
    cache.mkdir(parents=True, exist_ok=True)
    records.mkdir(parents=True, exist_ok=True)

    pad = 6
    padded = Image.new("RGB", (raw.width + pad * 2, raw.height + pad * 2), (255, 255, 255))
    padded.paste(raw, (pad, pad))
    crop_path = cache / f"{stem}-crop.png"
    padded.save(crop_path, "PNG")

    model_input_path = crop_path
    model_input_px = [padded.width, padded.height]
    if selection_scale < 1.0:
        model_raw, model_scale = downscale_selection_image(raw)
        model_pad = max(2, int(round(pad * model_scale)))
        model_image = Image.new("RGB",
                                (model_raw.width + model_pad * 2,
                                 model_raw.height + model_pad * 2), (255, 255, 255))
        model_image.paste(model_raw, (model_pad, model_pad))
        model_input_path = cache / f"{stem}-model-input.png"
        model_image.save(model_input_path, "PNG")
        model_input_px = [model_image.width, model_image.height]
    result = engine.recognize(model_input_path, return_atoms=True)
    molfile = result.get("molfile") or ""
    if not molfile.strip():
        raise ValueError("模型没有返回 Molfile，可能是选区中缺少完整结构")
    try:
        parsed = parse_molblock(molfile)
    except Exception:
        (cache / f'{stem}-rejected.mol').write_text(molfile, encoding='utf-8')
        raise
    parsed["molfile"] = molfile
    if not parsed["atoms"]:
        raise ValueError("模型返回了空结构")
    model_atoms = list(result.get("atoms", []))
    model_bonds = list(result.get("bonds", []))
    # fine-tune integration: c/n/o/se/... -> C/N/O/Se/... so element mapping works
    normalize_model_atom_symbols(model_atoms)
    if not model_atoms:
        raise ValueError("模型没有返回原子坐标，无法还原预处理坐标")

    box = crop_white_box(padded)
    mapping = map_molfile_to_model(parsed, model_atoms, model_bonds,
                                   (int(model_input_px[0]), int(model_input_px[1])))
    candidate = build_candidate(parsed, model_atoms, model_bonds,
                                (padded.width, padded.height), box, mapping)

    # Keep the untouched model result, including its restored pixel coordinates,
    # so a reviewer can always compare the final candidate with the raw output.
    raw_model = {
        "atoms": [],
        "bonds": [dict(bond) for bond in model_bonds],
        "preprocess_box": list(box),
        "preprocess_pad": 5,
    }
    for idx, atom in enumerate(model_atoms):
        item = dict(atom)
        item["x_px"], item["y_px"] = restore_model_pixel(atom, box, 5)
        item["model_index"] = idx
        raw_model["atoms"].append(item)

    components = candidate["components"]
    fragments = []
    for index, group in enumerate(components):
        group_atoms = [candidate["atoms"][i] for i in group]
        fragments.append({
            "index": index,
            "atom_count": len(group),
            "atoms": list(group),
            "symbols": [atom.get("symbol") or atom.get("model_symbol") or "?" for atom in group_atoms],
            "charge": sum(int(atom.get("charge") or 0) for atom in group_atoms),
            "aliases": [atom.get("alias") for atom in group_atoms if atom.get("alias")],
        })
    multi_fragment = len(components) > 1

    warnings_list = []
    if selection_scale < 1.0:
        warnings_list.append(
            "图像过大已自动缩小到 %d×%d（×%.3f），细节可能丢失；"
            "大结构建议框小一点或分块识别。" % (
                model_input_px[0], model_input_px[1], selection_scale)
        )
    if parsed.get("unsupported"):
        warnings_list.append(
            "Molfile 含未支持的属性（" + "，".join(parsed["unsupported"][:4]) +
            "）；已阻止插入，避免把未知属性静默改成普通原子。"
        )
    if parsed.get("sanitize_error"):
        warnings_list.append("RDKit 化学规范化失败：" + parsed["sanitize_error"] + "；已阻止插入。")
    if parsed.get("problems"):
        warnings_list.append("化学校验警告：" + "；".join(parsed["problems"][:6]))
    if multi_fragment:
        labels = []
        for fragment in fragments:
            labels.append(f"#{fragment['index'] + 1} {fragment['atom_count']} 原子 " +
                          "".join(fragment["symbols"])[:32])
        warnings_list.append(
            f"模型返回 {len(components)} 个断开片段（{'; '.join(labels)}）。"
            "没有自动删除任何片段（盐、反离子等可能是真实组分）；已禁止插入。"
            "请重新框选单个结构后再识别。"
        )
    if parsed["aliases"]:
        for idx, label in sorted(parsed["aliases"].items()):
            known = label in engine.nicknames
            if known:
                warnings_list.append(f"缩写 {label} 按原图标签保留；本地缩写库含该条目，可在编辑器中展开。")
            else:
                warnings_list.append(f"缩写 {label} 未在本地缩写库中；仅保留标签，未猜测其结构。")
        warnings_list.append("缩写标签按原图外观保留；插入后其为可编辑的 Nickname 原子，化学含义见记录文件。")

    endpoint_validation = {"status": "passed", "checked": 0, "errors": []}
    cdxml = build_cdxml(candidate)
    consistency = check_cdxml_consistency(candidate, cdxml)
    stereo = validate_stereo(parsed, candidate, cdxml)
    endpoint_validation = stereo.get("endpoint_validation", endpoint_validation)
    if consistency["status"] != "passed":
        warnings_list.append("CDXML 回读一致性检查失败：" + "；".join(consistency["errors"][:6]))
    def level_word(status: str) -> str:
        return {"passed": "通过", "failed": "失败", "not_verified": "未验证",
                "not_applicable": "不适用", "not_executed": "未执行",
                "not_specified": "未指定"}.get(status, status)

    validation_levels = {
        "model_molfile_to_candidate": {
            "status": "passed",
            "executed": True,
            "method": "Molfile 解析 + RDKit 化学属性 + 显式模型/Molfile 索引映射 + 键拓扑校验",
            "atom_count": len(candidate["atoms"]),
            "bond_count": len(candidate["bonds"]),
            "max_normalized_distance": mapping.get("max_normalized_distance"),
        },
        "candidate_to_cdxml": {
            "status": consistency["status"],
            "executed": True,
            "method": "由最终候选图生成 CDXML，并回读 XML 检查原子/键/属性/坐标仿射/楔键/Wavy",
            "atom_count": consistency.get("atom_count"),
            "bond_count": consistency.get("bond_count"),
        },
        "rdkit_cdxml_readback": {
            "status": stereo.get("readback_status", "not_verified"),
            "executed": stereo.get("readback_status") not in ("not_applicable",),
            "method": "RDKit Chem.MolsFromCDXML — 不是 MoleculeStudio 宿主读取器",
            "engine": stereo.get("readback_engine", "RDKit Chem.MolsFromCDXML"),
            "note": "仅比较 RDKit 能读出的显式立体；缩写/未知键型可能无法验证。",
        },
        "molecule_studio_host_roundtrip": {
            "status": "not_executed",
            "executed": False,
            "method": "MoleculeStudio Session::open / save / reopen",
            "note": "本次识别没有执行宿主往返；独立 host_probe 只验证指定样例，不代表每次识别。",
        },
    }
    warnings_list.append(
        "校验层级（如实记录）：模型 Molfile→候选图 [已执行/" +
        level_word(validation_levels["model_molfile_to_candidate"]["status"]) +
        "]；候选图→CDXML [已执行/" + level_word(consistency["status"]) +
        "]；CDXML→RDKit 回读 [" +
        ("已执行" if validation_levels["rdkit_cdxml_readback"]["executed"] else "未执行") + "/" +
        level_word(validation_levels["rdkit_cdxml_readback"]["status"]) +
        "]；MoleculeStudio 宿主导入/保存/重开 [未执行]。"
        "转换保持性通过不等于识别与原图一致；RDKit 回读通过也不等于宿主验证通过。"
    )
    if stereo["status"] == "failed":
        warnings_list.append("立体保持校验失败：" + "；".join(stereo["errors"][:6]) +
                             "；已禁止插入。")
    elif stereo["status"] == "not_verified":
        warnings_list.append("立体保持校验未验证：" + (stereo.get("not_verified_reason") or "缺少可验证依据") +
                             "。没有声称已通过。")
    elif stereo["status"] == "not_specified":
        warnings_list.append("双键存在可定义 E/Z，但模型未指定；CDXML 用 Wavy 键保留未指定状态，"
                             "没有自动补全为 E 或 Z。")
    elif stereo["status"] == "not_applicable":
        warnings_list.append("未发现可定义的显式立体信息（例如同端取代基相同或末端双键）；"
                             "未自行补全手性或 E/Z。")
    elif stereo["status"] == "passed":
        warnings_list.append("立体保持校验通过（模型 Molfile ↔ CDXML 的 RDKit 回读；"
                             "未执行 MoleculeStudio 宿主往返）。")

    wedge_count = sum(1 for bond in candidate["bonds"] if int(bond.get("stereo") or 0) in (1, 6))
    if wedge_count:
        warnings_list.append(f"保留 {wedge_count} 根实/虚楔键；端点与方向见记录中的立体证据。")

    confidence = result.get("confidence")
    quality_ok = True
    if confidence is not None:
        warnings_list.append("模型总体置信度仅供筛查，不代表识别正确概率。")
        if float(confidence) < 0.35:
            quality_ok = False
            warnings_list.append(
                "模型总体置信度低于 0.35，结果很可能是文字/箭头或错误图元；"
                "已禁止插入，请重新框选完整结构。"
            )
    low_atoms = [a for a in model_atoms if float(a.get("confidence", 1.0)) < 0.75]
    if low_atoms:
        warnings_list.append(f"{len(low_atoms)} 个原子的模型置信度低于 0.75，已在叠加图中标红。")

    cdxml_path = records / f"{stem}.cdxml"
    cdxml_path.write_text(cdxml, encoding="utf-8")
    candidate_path = records / f"{stem}-candidate.json"
    candidate_path.write_text(json.dumps({
        "atoms": candidate["atoms"],
        "bonds": candidate["bonds"],
        "components": components,
        "mapping": mapping,
        "preprocess_box": list(box),
        "image_size": [padded.width, padded.height],
    }, ensure_ascii=False, indent=1), encoding="utf-8")

    overlay_path = cache / f"{stem}-overlay.png"
    blend_path = cache / f"{stem}-blend.png"
    result_path = cache / f"{stem}-result.png"
    raw_overlay_path = cache / f"{stem}-raw-overlay.png"
    raw_blend_path = cache / f"{stem}-raw-blend.png"
    draw_candidate_previews(crop_path, candidate, raw_model, {
        "result": result_path,
        "overlay": overlay_path,
        "blend": blend_path,
        "raw_overlay": raw_overlay_path,
        "raw_blend": raw_blend_path,
    })

    insertable = bool(
        quality_ok
        and not multi_fragment
        and not parsed.get("unsupported")
        and not parsed.get("sanitize_error")
        and endpoint_validation.get("status") == "passed"
        and consistency["status"] == "passed"
        and stereo["status"] != "failed"
    )
    graph_json = json.dumps(candidate, sort_keys=True, ensure_ascii=False).encode("utf-8")
    record = {
        "schema": SCHEMA,
        "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "service_version": SERVICE_VERSION,
        "source": {
            "kind": source.get("kind", "pdf"),
            "path": source.get("path", ""),
            "page": source.get("page", 1),
            "render_dpi": dpi,
            "page_image": str(page_image),
            "page_image_sha256": sha256_file(page_image) if page_image.is_file() else "",
            "page_size_px": page_size_px,
            "crop_rect_px": [x, y, w, h],
            "crop_padding_px": pad,
            "selection_original_px": [x - pad, y - pad, w + pad * 2, h + pad * 2],
            "selection_downscaled": {
                "applied": bool(selection_scale < 1.0),
                "scale": round(float(selection_scale), 6),
                "original_px": [int(w), int(h)],
                "model_input_px": [int(model_input_px[0]), int(model_input_px[1])],
                "max_side_px": MAX_SELECTION_PX,
                "hard_limit_px": HARD_MAX_SELECTION_PX,
                "note": "超大选区只对模型输入等比缩图；像素坐标仍按原尺寸 CropWhite 框恢复，"
                        "叠加图与插入位置不受影响。",
            },
            "vector_crop": ({
                "applied": True,
                "image": str(vector_info.get("image", "")),
                "requested_dpi": vector_info.get("requested_dpi"),
                "effective_dpi": vector_info.get("dpi"),
                "base_dpi": vector_info.get("base_dpi"),
                "downsampled": bool(vector_info.get("downsampled")),
                "source_box_pt": vector_info.get("source_box_pt") or [],
                "rendered_size_px": vector_info.get("rendered_size_px") or [],
                "note": "模型输入由 PDF 矢量内容按该 DPI 重建；页面预览 DPI 仅用于框选。",
            } if vector_info else {"applied": False}),
        },
        "input_image": {
            "path": str(crop_path),
            "width": padded.width,
            "height": padded.height,
            "sha256": sha256_file(crop_path),
        },
        "model_preprocess": {
            "operation": "CropWhite(pad=5) -> Resize(%d,%d); model coordinates are normalised to the cropped+padded image" % (engine.input_size, engine.input_size),
            "crop_white_box_in_input_px": [int(box[0]), int(box[1]), int(box[2]), int(box[3])],
            "pad": 5,
            "model_input_size": [engine.input_size, engine.input_size],
            "model_input_px": [int(model_input_px[0]), int(model_input_px[1])],
            "selection_scale": round(float(selection_scale), 6),
            "restore_formula": "x_px=(left-5)+x_norm*((right-left)+10); y_px=(top-5)+y_norm*((bottom-top)+10)",
        },
        "transform": {
            "image_space": "input crop pixels, origin top-left, y down",
            "canvas_space": "CDXML points, origin top-left, y down",
            "operation": "restore model normalised coordinates to input pixels, then translate and uniformly scale to 30 pt median bond; no rotation, mirror or relayout",
            "uniform_scale": True,
            "rotation_deg": 0,
            "mirror_applied": False,
            "wedge_swap": False,
            "molfile_coordinates_used_for_geometry": False,
            "cdxml_scale": round(30.0 / max(_median_bond_length(candidate, 1.0), 1e-6), 8),
            "stereo_validation": stereo["status"],
        },
        "mapping": {
            "model_to_molfile": mapping["model_to_molfile"],
            "molfile_to_model": mapping["molfile_to_model"],
            "max_normalized_distance": mapping["max_normalized_distance"],
            "candidate_to_model": [int(atom["model_index"]) for atom in candidate["atoms"]],
            "candidate_to_molfile": [int(atom["molfile_index"]) for atom in candidate["atoms"]],
        },
        "model": {
            "name": "MolScribe",
            "version": "1.1.1",
            "checkpoint": engine.model_path.name if engine.model_path else "",
            "checkpoint_size": engine.model_path.stat().st_size if engine.model_path else 0,
            "checkpoint_sha256": engine.status(include_hash=True).get("model_sha256", ""),
            "license": "MIT (code and model card); see ocsr_runtime/LICENSES",
            "device": str(result.get("_device", engine.device_name)),
            "inference_seconds": result.get("_seconds"),
            "load_seconds": engine.load_seconds,
            "input_size": engine.input_size,
            "max_len": engine.max_len,
            "model_source": engine.model_source,
        },
        "result": {
            "smiles": result.get("smiles", ""),
            "confidence": confidence,
            "atoms": model_atoms,
            "bonds": model_bonds,
            "molfile": molfile,
            "main_fragment_atoms": max((len(group) for group in components), default=0),
            "fragments": len(components),
            "fragment_details": fragments,
            "discarded_fragments": 0,
            "aliases": parsed["aliases"],
            "wedge_bonds": wedge_count,
        },
        "raw_model": raw_model,
        "candidate": {
            "atoms": candidate["atoms"],
            "bonds": candidate["bonds"],
            "components": components,
            "preprocess_box": list(box),
            "image_size": [padded.width, padded.height],
            "graph_sha256": hashlib.sha256(graph_json).hexdigest(),
        },
        "validation": {
            "rdkit_parse": not parsed.get("problems") and not parsed.get("sanitize_error"),
            "problems": parsed.get("problems", []),
            "unsupported_molfile_properties": parsed.get("unsupported", []),
            "levels": validation_levels,
            "level_summary": {
                "model_molfile_to_candidate": validation_levels["model_molfile_to_candidate"]["status"],
                "candidate_to_cdxml": consistency["status"],
                "rdkit_cdxml_readback": validation_levels["rdkit_cdxml_readback"]["status"],
                "molecule_studio_host_roundtrip": "not_executed",
            },
            "endpoint_validation": endpoint_validation,
            "stereo_validation": stereo,
            "candidate_cdxml_consistency": consistency,
            "quality_gate_passed": quality_ok,
            "insertable": insertable,
        },
        "warnings": warnings_list,
        "review": {
            "status": "pending",
            "inserted_at": "",
            "document": "",
            "note": "必须由用户在并排/叠加预览中确认后才能插入画布。",
        },
        "artifacts": {
            "crop_png": str(crop_path),
            "overlay_png": str(overlay_path),
            "blend_png": str(blend_path),
            "result_png": str(result_path),
            "raw_overlay_png": str(raw_overlay_path),
            "raw_blend_png": str(raw_blend_path),
            "candidate_json": str(candidate_path),
            "cdxml": str(cdxml_path),
            "cdxml_sha256": sha256_bytes(cdxml.encode("utf-8")),
        },
    }
    record_path = records / f"{stem}.json"
    record_path.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")

    # Human-readable stereo summary.  Keep "not applicable", "not specified",
    # "not verified" and "failed" distinct; never collapse them into "passed".
    if stereo["errors"]:
        stereo_detail = "；".join(stereo["errors"][:8])
    elif stereo["status"] == "not_verified":
        stereo_detail = stereo.get("not_verified_reason") or "缺少可验证依据"
    elif stereo["status"] == "not_specified":
        pairs = "，".join(f"{d['begin']}={d['end']}" for d in stereo.get("unspecified_double_bonds", []))
        stereo_detail = ("存在可定义 E/Z 但模型未指定（" + pairs +
                         "）；CDXML 以 Wavy 双键保留，未补全。")
    elif stereo["status"] == "passed":
        parts = []
        for item in stereo.get("checked", []):
            if item.get("kind") == "double_bond":
                parts.append(f"E/Z {item.get('begin')}={item.get('end')} {item.get('reference')}")
            elif item.get("kind") == "tetrahedral":
                parts.append(f"CIP 原子 {item.get('atom')} {item.get('cip')}")
            elif item.get("kind") == "wedge_bonds":
                parts.append(f"{item.get('count')} 根楔键")
        stereo_detail = "；".join(parts) + "（仅 RDKit 回读比较）"
    else:
        stereo_detail = "无可定义的 E/Z 或显式手性中心"

    return {
        "crop": crop_path,
        "overlay": overlay_path,
        "blend": blend_path,
        "result": result_path,
        "raw_overlay": raw_overlay_path,
        "raw_blend": raw_blend_path,
        "cdxml": cdxml_path,
        "record": record_path,
        "candidate": candidate_path,
        "confidence": confidence,
        "atoms": len(candidate["atoms"]),
        "bonds": len(candidate["bonds"]),
        "fragments": len(components),
        "warnings": warnings_list,
        "insertable": insertable,
        "stereo_ok": stereo["status"] == "passed",
        "stereo_status": stereo["status"],
        "stereo_detail": stereo_detail,
        "readback_engine": stereo.get("readback_engine", "RDKit Chem.MolsFromCDXML"),
        "host_roundtrip": False,
        "stereo_preserved": stereo.get("stereo_preserved"),
        "double_bonds": stereo.get("double_bonds", []),
        "validation_levels": validation_levels,
        "vector": bool(vector_info),
        "vector_dpi": vector_info.get("dpi") if vector_info else None,
        "vector_requested_dpi": vector_info.get("requested_dpi") if vector_info else None,
        "vector_downsampled": bool(vector_info.get("downsampled")) if vector_info else False,
        "input_width": padded.width,
        "input_height": padded.height,
        "width": padded.width,
        "height": padded.height,
    }



# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------

def attribute_int(node, name, default=0, low=-10**9, high=10**9):
    try:
        value = int(node.get(name, default))
    except Exception:
        value = default
    return max(low, min(high, value))


def attribute_float(node, name, default=0.0, low=-1e9, high=1e9):
    try:
        value = float(node.get(name, default))
    except Exception:
        value = default
    return max(low, min(high, value))


def dispatch(engine: OcsrEngine, request: ET.Element, cache: Path, records: Path) -> ET.Element:
    op = request.get("op", "")
    rid = request.get("id", "")
    if op == "ping":
        return xml_response(True, id=rid, service=SERVICE_VERSION, pong="1")
    if op == "status":
        info = engine.status(include_hash=request.get("hash", "0") == "1")
        node = xml_response(True, id=rid, **{k: (json.dumps(v, ensure_ascii=False) if isinstance(v, (list, dict)) else v)
                                             for k, v in info.items()})
        return node
    if op == "render":
        source = Path(request.get("file", ""))
        result = render_source(source, attribute_int(request, "page", 1, 1, 10000),
                               attribute_int(request, "dpi", 300, 72, 600), cache)
        return xml_response(True, id=rid, **result)
    if op == "detect":
        source = Path(request.get("file", ""))
        result = detect_structures(
            source,
            attribute_int(request, "page", 1, 1, 10000),
            attribute_int(request, "dpi", 300, 72, 600),
            cache,
            max_regions=attribute_int(request, "max_regions", 120, 1, 400),
            min_score=attribute_float(request, "min_score", 0.35, 0.0, 1.0),
        )
        node = xml_response(True, id=rid, method=result["method"], page=result["page"],
                            dpi=result["dpi"], count=result["count"],
                            page_width_pt=result["page_width_pt"],
                            page_height_pt=result["page_height_pt"],
                            page_size_px=",".join(map(str, result["page_size_px"])),
                            preview=result["preview"],
                            warnings=json.dumps(result["warnings"], ensure_ascii=False))
        for region in result["regions"]:
            ET.SubElement(node, "region", {
                "index": str(region["index"]),
                "crop": ",".join(f"{v:.6f}" for v in region["crop"]),
                "bbox_pt": ",".join(f"{v:.2f}" for v in region["bbox_pt"]),
                "pixels": ",".join(map(str, region["pixels"])),
                "score": f"{region['score']:.4f}",
                "paths": str(region["paths"]),
                "closed_paths": str(region["closed_paths"]),
                "stroke": f"{region['stroke']:.3f}",
            })
        return node
    if op == "export_crop":
        source = Path(request.get("file", ""))
        crop = (
            attribute_int(request, "crop_x", 0, -100000, 100000),
            attribute_int(request, "crop_y", 0, -100000, 100000),
            attribute_int(request, "crop_w", 0, 0, 100000),
            attribute_int(request, "crop_h", 0, 0, 100000),
        )
        result = export_crop(
            source,
            attribute_int(request, "page", 1, 1, 10000),
            crop,
            attribute_int(request, "dpi", 300, 72, 600),
            attribute_int(request, "target_dpi", VECTOR_CROP_DPI_DEFAULT, 72, VECTOR_CROP_DPI_MAX),
            cache,
            request.get("formats", "png+pdf"),
        )
        payload = {k: (json.dumps(v, ensure_ascii=False) if isinstance(v, (list, dict)) else v)
                   for k, v in result.items() if v is not None}
        return xml_response(True, id=rid, **payload)
    if op == "recognize":
        page_image = Path(request.get("page_image", ""))
        if not safe_under(page_image, cache):
            raise ValueError("页面图片不在 OCSR 缓存目录中")
        crop = (
            attribute_int(request, "crop_x", 0, -100000, 100000),
            attribute_int(request, "crop_y", 0, -100000, 100000),
            attribute_int(request, "crop_w", 0, 0, 100000),
            attribute_int(request, "crop_h", 0, 0, 100000),
        )
        source = {
            "kind": request.get("source_kind", "pdf"),
            "path": request.get("source_path", ""),
            "page": attribute_int(request, "page", 1, 1, 10000),
        }
        dpi = attribute_int(request, "dpi", 300, 72, 600)
        prepared = None
        use_vector = str(request.get("vector", "0")).strip().lower() in ("1", "true", "yes", "auto")
        source_file = Path(source["path"]) if source["path"] else None
        if use_vector and source["kind"] == "pdf" and source_file and source_file.suffix.lower() == ".pdf":
            prepared = render_vector_crop(
                source_file, source["page"], crop, dpi,
                attribute_int(request, "target_dpi", VECTOR_CROP_DPI_DEFAULT, 72, VECTOR_CROP_DPI_MAX),
                cache,
            )
        result = recognize(engine, page_image, crop, cache, records, source, dpi, prepared=prepared)
        node = xml_response(True, id=rid)
        for key in ("crop", "overlay", "blend", "result", "cdxml", "record"):
            node.set(key, str(result[key]))
        node.set("confidence", "" if result["confidence"] is None else f"{result['confidence']:.6f}")
        node.set("atoms", str(result["atoms"]))
        node.set("bonds", str(result["bonds"]))
        node.set("fragments", str(result["fragments"]))
        node.set("insertable", "1" if result["insertable"] else "0")
        node.set("stereo_ok", "1" if result["stereo_ok"] else "0")
        node.set("stereo_status", result.get("stereo_status", "not_verified"))
        node.set("stereo_detail", result.get("stereo_detail", ""))
        node.set("readback_engine", result.get("readback_engine", "RDKit Chem.MolsFromCDXML"))
        node.set("host_roundtrip", "1" if result.get("host_roundtrip") else "0")
        node.set("stereo_preserved", "" if result.get("stereo_preserved") is None else
                 ("1" if result.get("stereo_preserved") else "0"))
        node.set("validation_levels", json.dumps(result.get("validation_levels", {}), ensure_ascii=False))
        node.set("candidate", str(result.get("candidate", "")))
        node.set("raw_overlay", str(result.get("raw_overlay", "")))
        node.set("raw_blend", str(result.get("raw_blend", "")))
        node.set("width", str(result["width"]))
        node.set("height", str(result["height"]))
        node.set("vector", "1" if result.get("vector") else "0")
        node.set("vector_dpi", "" if result.get("vector_dpi") is None else f"{result['vector_dpi']:.3f}")
        node.set("vector_requested_dpi", str(result.get("vector_requested_dpi") or ""))
        node.set("vector_downsampled", "1" if result.get("vector_downsampled") else "0")
        node.set("input_width", str(result.get("input_width", result["width"])))
        node.set("input_height", str(result.get("input_height", result["height"])))
        for warning in result["warnings"]:
            ET.SubElement(node, "warning").text = warning
        return node
    if op == "confirm":
        record_path = Path(request.get("record", ""))
        if not safe_under(record_path, records):
            raise ValueError("记录文件不在 OCSR 记录目录中")
        if not record_path.is_file():
            raise ValueError("记录文件不存在")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        record.setdefault("review", {})
        record["review"]["status"] = "confirmed"
        record["review"]["inserted_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        record["review"]["document"] = request.get("document", "")[:512]
        record["review"]["note"] = request.get("note", "用户在叠加预览中确认并插入")[:512]
        record_path.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
        # Fixed-name registry for the AI bridge.  It never accepts a path from
        # the model: the assistant can only re-insert a user-confirmed record.
        registry = records / "last-confirmed.json"
        registry.write_text(json.dumps({
            "record": str(record_path),
            "cdxml": record.get("artifacts", {}).get("cdxml", ""),
            "source": record.get("source", {}),
            "model": record.get("model", {}),
            "review": record.get("review", {}),
            "warnings": record.get("warnings", []),
        }, ensure_ascii=False, indent=1), encoding="utf-8")
        return xml_response(True, id=rid, record=str(record_path), registry=str(registry))
    raise ValueError("未知操作：" + op[:40])


def main(argv=None) -> int:
    # The native client speaks UTF-8 on both pipes.  Windows' default console
    # code page (often GBK) would otherwise mojibake warnings and paths.
    try:
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    prepare_native_runtime()
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--serve", action="store_true")
    parser.add_argument("--models", default=str(APP_DIR / "ocsr_runtime" / "models"))
    parser.add_argument("--cache", default=str(APP_DIR / "OCSRCache"))
    parser.add_argument("--records", default=str(APP_DIR / "recognition_records"))
    parser.add_argument("--device", default=os.environ.get("MOLECULE_STUDIO_OCSR_DEVICE", "cpu"))
    parser.add_argument("--model", default=os.environ.get("MOLECULE_STUDIO_OCSR_MODEL", ""))
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--file", default="")
    parser.add_argument("--page", type=int, default=1)
    parser.add_argument("--dpi", type=int, default=300)
    args, _ = parser.parse_known_args(argv)

    cache = Path(args.cache).resolve()
    records = Path(args.records).resolve()
    engine = OcsrEngine(Path(args.models).resolve(), args.device, args.model)

    if args.render:
        try:
            result = render_source(Path(args.file), args.page, args.dpi, cache)
            node = xml_response(True, **result)
        except Exception as exc:
            node = fail(str(exc))
        emit(node)
        return 0 if node.get("ok") == "1" else 2

    # Keep stdout clean for the protocol; import-time warnings go to stderr.
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        rid = ""
        try:
            request = ET.fromstring(line)
            rid = request.get("id", "")
            node = dispatch(engine, request, cache, records)
        except Exception as exc:
            node = fail(str(exc), id=rid)
            traceback.print_exc(file=sys.stderr)
        emit(node)
    return 0


if __name__ == "__main__":
    # The 0.95 native client discards stderr. Keep worker failures locally so
    # another machine can report an actionable error without an AI API key.
    log_stream = None
    try:
        log_dir = APP_DIR / "OCSRCache"
        try:
            log_dir.mkdir(parents=True, exist_ok=True)
            log_stream = (log_dir / "last-service-error.log").open("w", encoding="utf-8")
        except OSError:
            import tempfile
            log_dir = Path(tempfile.gettempdir()) / "MoleculeStudio-OCSR"
            log_dir.mkdir(parents=True, exist_ok=True)
            log_stream = (log_dir / "last-service-error.log").open("w", encoding="utf-8")
        sys.stderr = log_stream
        sys.stderr.reconfigure(line_buffering=True)
        raise SystemExit(main())
    except Exception:
        traceback.print_exc()
        raise SystemExit(2)
    finally:
        if log_stream is not None:
            log_stream.flush()
