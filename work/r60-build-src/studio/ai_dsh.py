"""Advanced drawing: hand planning and verification to the bundled DeepSeek Harness.

The native AI window sets <advanced>1</advanced> when the user enables 高级绘图.
This module only launches the bundled headless DSH; the assistant bridge keeps the
canvas tools, validates every returned call and applies the resulting actions in
one undo transaction. DSH never receives the API key on disk and never writes
outside its workspace.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
from pathlib import Path

BASE = Path(__file__).resolve().parent
DEFAULT_CORE = BASE / "ai"
LEGACY_CORE = BASE / "AI 套件" / "dsh_core"
DEFAULT_WORKSPACE = Path.home() / "Documents" / "MoleculeStudio Workspace"


def core_dir() -> Path:
    configured = os.environ.get("MOLECULE_STUDIO_DSH_CORE")
    return Path(configured) if configured else (DEFAULT_CORE if DEFAULT_CORE.is_dir() else LEGACY_CORE)


def runtime_paths(core: Path):
    """Support the compact layout and existing installations."""
    if (core / "node.exe").is_file():
        return core / "node.exe", core / "node_modules" / "@deepseek-ai" / "dsh" / "lib" / "bin.js"
    return core / "node" / "node.exe", core / "dsh" / "node_modules" / "@deepseek-ai" / "dsh" / "lib" / "bin.js"


def _writable(path: Path) -> bool:
    try:
        path.mkdir(parents=True, exist_ok=True)
        probe = path / ".write-test"
        probe.write_text("", encoding="utf-8")
        probe.unlink()
        return True
    except OSError:
        return False


def workspace_dir() -> Path:
    configured = os.environ.get("MOLECULE_STUDIO_WORKSPACE")
    fallback = Path(os.environ.get("LOCALAPPDATA") or Path.home()) / "MoleculeStudio" / "AI" / "dsh_workspace"
    for candidate in (Path(configured) if configured else DEFAULT_WORKSPACE, fallback):
        if _writable(candidate):
            return candidate
    return fallback


def available() -> bool:
    core = core_dir()
    node, binary = runtime_paths(core)
    return all(
        path.is_file()
        for path in (
            node,
            binary,
            core / "overlay.yml",
        )
    )


def _clean(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text or "")


def run_task(task: str, api_key: str, timeout: float = 1800, progress=None, base_url: str = "", model: str = "") -> str:
    """Run one headless DSH task and return its final message.

    MOLECULE_STUDIO_DSH_STUB may point at a text file; it is used by the offline
    regression tests so the whole advanced loop can be exercised without network.
    """
    stub = os.environ.get("MOLECULE_STUDIO_DSH_STUB")
    if stub:
        return Path(stub).read_text(encoding="utf-8")
    if not available():
        raise ValueError("高级绘图组件不完整，请修复或重新安装 AI 套件")
    core = core_dir()
    workspace = workspace_dir()
    workspace.mkdir(parents=True, exist_ok=True)
    node, binary = runtime_paths(core)
    overlay = core / "overlay.yml"
    env = os.environ.copy()
    env["DEEPSEEK_API_KEY"] = api_key
    env["DSH_HOME"] = str(core / "home")
    env["DSH_PERMISSION_MODE"] = "workspace-write"
    env["DSH_TELEMETRY_DISABLED"] = "1"
    if base_url:
        env["DEEPSEEK_BASE_URL"] = base_url
    if model:
        env["MOLECULE_STUDIO_DSH_MODEL"] = model
    python_dir = BASE / "python"
    env["PATH"] = (str(python_dir) + ";" if python_dir.is_dir() else "") + str(node.parent) + ";" + env.get("PATH", "")
    command = [str(node), str(binary), "headless", "--patch", str(overlay), "--", "-"]
    if progress:
        progress("高级绘图：正在规划…")
    try:
        result = subprocess.run(
            command,
            input=task,
            cwd=str(workspace),
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    except subprocess.TimeoutExpired:
        raise ValueError("高级绘图超过 %d 秒未完成，已终止；请拆分任务后重试" % timeout)
    if result.returncode:
        detail = _clean(result.stderr or result.stdout).strip()[-1200:]
        raise ValueError("高级绘图执行失败：" + (detail or "未知错误"))
    output = _clean(result.stdout).strip()
    if not output:
        raise ValueError("高级绘图没有返回内容，请重试")
    return output


def extract_object(text: str, preferred=("plan", "fixes")):
    """Return the last top-level plan/fixes object, ignoring nested lookalikes."""
    candidates = []
    text = text or ''
    position = 0
    decoder = json.JSONDecoder()
    while position < len(text):
        position = text.find('{', position)
        if position < 0: break
        try:
            value, end = decoder.raw_decode(text, position)
        except Exception:
            position += 1
            continue
        if isinstance(value, dict):
            candidates.append(value)
        position = end
    matching = [value for value in candidates if any(key in value for key in preferred)]
    if matching:
        return matching[-1]
    return candidates[-1] if candidates else None
