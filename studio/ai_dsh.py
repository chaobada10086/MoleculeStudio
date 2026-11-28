"""Advanced drawing: hand planning and verification to the bundled DeepSeek Harness.

The native AI window sets <advanced>1</advanced> when the user enables 高级绘图.
This module only launches the bundled headless DSH; the assistant bridge keeps the
canvas tools, validates every returned call and applies the resulting actions in
one undo transaction. Each worker receives a private workspace and a temporary
loopback credential. OS-level enforcement depends on the installed DSH runtime.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import shutil
import tempfile
import uuid
from contextlib import contextmanager
from pathlib import Path

BASE = Path(__file__).resolve().parent
DEFAULT_CORE = BASE / "ai"
LEGACY_CORE = BASE / "AI 套件" / "dsh_core"



def core_dir() -> Path:
    configured = os.environ.get("MOLECULE_STUDIO_DSH_CORE")
    return Path(configured) if configured else (DEFAULT_CORE if DEFAULT_CORE.is_dir() else LEGACY_CORE)


def runtime_paths(core: Path):
    """Support the compact layout and existing installations."""
    if (core / "node.exe").is_file():
        return core / "node.exe", core / "node_modules" / "@deepseek-ai" / "dsh" / "lib" / "bin.js"
    return core / "node" / "node.exe", core / "dsh" / "node_modules" / "@deepseek-ai" / "dsh" / "lib" / "bin.js"


def _lock_lease(stream):
    # The OS releases the lease if the native host kills the Python worker.
    if os.name == "nt":
        import msvcrt
        stream.seek(0)
        msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
    else:
        import fcntl
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)


def _remove_session(root, session):
    if (session.parent == root and re.fullmatch(r"run-[0-9a-f]{32}", session.name)
            and session.resolve() == session):
        shutil.rmtree(session)


def _reap_sessions(root):
    for session in root.glob("run-*"):
        lease = session / "lease"
        try:
            if (not re.fullmatch(r"run-[0-9a-f]{32}", session.name)
                    or session.resolve() != session or lease.resolve() != lease
                    or not (session / "work").is_dir()):
                continue
            with lease.open("r+b") as stream:
                _lock_lease(stream)
            # Each session name has one creator and is never reused.
            _remove_session(root, session)
        except OSError:
            continue


@contextmanager
def task_workspace():
    parent = Path(os.environ.get("LOCALAPPDATA") or tempfile.gettempdir())
    root = parent / "MoleculeStudio" / "AI" / "sessions"
    root.mkdir(parents=True, exist_ok=True)
    root = root.resolve()
    _reap_sessions(root)
    session = root / ("run-" + uuid.uuid4().hex)
    session.mkdir(mode=0o700)
    try:
        with (session / "lease").open("x+b") as lease:
            lease.write(b"1")
            lease.flush()
            _lock_lease(lease)
            workspace = session / "work"
            workspace.mkdir(mode=0o700)
            yield workspace
    finally:
        try:
            _remove_session(root, session)
        except OSError:
            # A scanner or a terminating child may still hold a file open.
            pass


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


def run_task(task: str, session_token: str, *, workspace: Path, base_url: str, timeout: float = 1800, progress=None, model: str = "") -> str:
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
    node, binary = runtime_paths(core)
    overlay = core / "overlay.yml"
    runtime = workspace.parent / "runtime"
    runtime.mkdir(exist_ok=True)
    env = {name: os.environ[name] for name in
           ("SystemRoot", "WINDIR", "COMSPEC", "PATHEXT", "LANG", "LC_ALL", "NUMBER_OF_PROCESSORS")
           if name in os.environ}
    for name in ("HOME", "USERPROFILE", "LOCALAPPDATA", "APPDATA", "TEMP", "TMP", "TMPDIR"):
        env[name] = str(runtime)
    env["DEEPSEEK_API_KEY"] = session_token
    env["DSH_HOME"] = str(runtime / "dsh")
    env["NO_PROXY"] = "127.0.0.1,localhost"
    env["DSH_PERMISSION_MODE"] = "workspace-write"
    env["DSH_TELEMETRY_DISABLED"] = "1"
    if base_url:
        env["DEEPSEEK_BASE_URL"] = base_url
    if model:
        env["MOLECULE_STUDIO_DSH_MODEL"] = model
    python_dir = BASE / "python"
    paths = [str(node.parent)]
    if python_dir.is_dir():
        paths.append(str(python_dir))
    if os.name == "nt":
        paths.append(str(Path(env["SystemRoot"]) / "System32"))
    env["PATH"] = os.pathsep.join(paths)
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
        detail = _clean(result.stderr or result.stdout).replace(session_token, "[redacted]").strip()[-1200:]
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
