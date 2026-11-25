from pathlib import Path
import sys
import xml.etree.ElementTree as ET

folder = Path(__file__).resolve().parent
if not (folder / "ai_assistant.py").is_file():
    sys.path.insert(0, str(folder.parent))
import ai_assistant

sys.stdout.reconfigure(encoding="utf-8")
try:
    payload = sys.stdin.buffer.read(2_000_001)
    if len(payload) > 2_000_000:
        raise ValueError("请求过大")
    request = ET.fromstring(payload)
    request.set("attachment_access", "selection")
    result = ai_assistant.run(request)
except Exception as error:
    result = ET.Element("result", {"ok": "false"})
    message = str(error) if isinstance(error, ValueError) else "AI 服务执行失败（" + type(error).__name__ + "）"
    ET.SubElement(result, "message").text = ai_assistant.redact_secret(message)[:1000]
print(ET.tostring(result, encoding="unicode"))
