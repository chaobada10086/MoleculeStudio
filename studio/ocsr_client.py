# -*- coding: utf-8 -*-
"""OCSR 模块（A+C 版）Python 调用示例 / 命令行工具（ocsr_client.py）。

运行环境：本目录自带的 ocsr_runtime\venv\Scripts\python.exe
用法：
    python ocsr_client.py status                 # 查看服务/模型状态
    python ocsr_client.py recognize <图片路径>   # 识别一张图片（也可写作 识别）
    python ocsr_client.py recognize <图片路径> --json 结果.json

作为库使用：
    from ocsr_client import OCSR
    ocsr = OCSR()
    print(ocsr.status()["input_size"], ocsr.status()["max_len"])
    r = ocsr.recognize_image("molecule.png")
    print(r["smiles"], r["insertable"])
    ocsr.close()
"""
import json
import shutil
import tempfile
import subprocess
import sys
import uuid
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent
PY = ROOT / "ocsr_runtime" / "venv" / "Scripts" / "python.exe"
SERVICE = ROOT / "ocsr_service.py"
MODELS = ROOT / "ocsr_runtime" / "models"
# 注意：OpenCV 在 Windows 上无法读取含中文的路径，因此缓存目录放到 TEMP（纯 ASCII），
# 这样即使本模块放在中文路径下也能正常识别。
CACHE = Path(tempfile.gettempdir()) / "MoleculeStudio-OCSR-cache"
RECORDS = ROOT / "recognition_records"


def fix_venv():
    """把 venv 的 home 指向本目录内的 python，使整个文件夹可以随意移动。"""
    cfg = ROOT / "ocsr_runtime" / "venv" / "pyvenv.cfg"
    base = ROOT / "ocsr_runtime" / "python" / "cpython-3.10-windows-x86_64-none"
    cfg.write_text(
        "home = %s\ninclude-system-site-packages = false\nversion_info = 3.10\n" % base,
        encoding="utf-8",
    )


class OCSR:
    def __init__(self, device="cpu", verbose=False):
        fix_venv()
        CACHE.mkdir(parents=True, exist_ok=True)
        RECORDS.mkdir(parents=True, exist_ok=True)
        cmd = [
            str(PY), str(SERVICE), "--serve",
            "--models", str(MODELS), "--cache", str(CACHE), "--records", str(RECORDS),
            "--device", device,
        ]
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=None if verbose else subprocess.DEVNULL,
            text=True, encoding="utf-8", bufsize=1,
        )

    def request(self, xml: str) -> dict:
        self.proc.stdin.write(xml.strip() + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("OCSR 服务已退出（详见 stderr / OCSRCache\last-service-error.log）")
        return dict(ET.fromstring(line).attrib)

    def status(self) -> dict:
        r = self.request('<request op="status"/>')
        if r.get("ok") != "1":
            raise RuntimeError(r.get("error", "status 失败"))
        return r

    def recognize_image(self, image_path, source_kind="image") -> dict:
        from PIL import Image

        img = Path(image_path).expanduser().resolve()
        if not img.is_file():
            raise FileNotFoundError(str(img))
        cached = CACHE / ("page-" + uuid.uuid4().hex[:10] + img.suffix.lower())
        shutil.copy2(img, cached)
        with Image.open(cached) as im:
            w, h = im.size
        xml = (
            '<request op="recognize" page_image="%s" crop_x="0" crop_y="0" crop_w="%d" crop_h="%d" '
            'source_kind="%s" source_path="%s" dpi="300" vector="0"/>' % (cached, w, h, source_kind, img)
        )
        r = self.request(xml)
        if r.get("ok") != "1":
            raise RuntimeError("识别失败：" + r.get("error", ""))
        # 注意：响应里的 "result" 是结果预览图（PNG），"record" 才是记录 JSON
        record_path = Path(r.get("record", ""))
        record = json.loads(record_path.read_text(encoding="utf-8")) if record_path.is_file() else {}
        res = record.get("result", {})
        return {
            "smiles": res.get("smiles", ""),
            "molfile": res.get("molfile", ""),
            "atoms": int(r.get("atoms") or 0),
            "bonds": int(r.get("bonds") or 0),
            "confidence": float(r.get("confidence") or 0.0),
            "insertable": r.get("insertable") == "1",
            "fragments": int(r.get("fragments") or 0),
            "overlay": r.get("overlay", ""),
            "result_image": r.get("result", ""),
            "cdxml": r.get("cdxml", ""),
            "record": str(record_path),
        }

    def close(self):
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        self.proc.terminate()


def main(argv):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    action = argv[1]
    ocsr = OCSR()
    try:
        if action == "status":
            print(json.dumps(ocsr.status(), ensure_ascii=False, indent=2))
            return 0
        if action in ("识别", "recognize"):
            if len(argv) < 3:
                print("用法：python 调用示例.py 识别 <图片路径> [--json out.json]")
                return 2
            r = ocsr.recognize_image(argv[2])
            out = None
            if "--json" in argv:
                out = argv[argv.index("--json") + 1]
                Path(out).write_text(json.dumps(r, ensure_ascii=False, indent=1), encoding="utf-8")
            print("SMILES   :", r["smiles"])
            print("原子/键  :", r["atoms"], "/", r["bonds"], " 片段:", r["fragments"])
            print("置信度   :", round(r["confidence"], 4), " 可插入:", r["insertable"])
            print("叠加图   :", r["overlay"])
            print("记录文件 :", r["record"])
            if out:
                print("已写入   :", out)
            return 0
        print("未知命令：", action)
        return 2
    finally:
        ocsr.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv))