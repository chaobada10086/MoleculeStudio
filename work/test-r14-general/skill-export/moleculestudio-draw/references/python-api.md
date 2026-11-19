# 原有 Python 绘图接口（开发者模式）

代码来源：MoleculeStudio 0.95 的 studio/ai_assistant.py、ai_scene.py、ai_actions.hpp。完整 42 项函数参数保存在同目录 AI_TOOL_REFERENCE.json；这是版本快照，其他版本以其本地 schema 为准。

需要安装 AI addon 中的 Python 模块（无需启动内置 AI、无需配置大模型密钥）；核心化学能力仍依赖 Core。组合排版工具需要 ai_plugins。OCSR 工具还需要 OCSR addon。web_search 是单独的网络服务能力，不是离线画图前提。

用程序自带 Python 运行如下代码；APP 换成实际安装目录：
```python
import sys
from pathlib import Path
import xml.etree.ElementTree as ET
APP = Path(r"D:/MoleculeStudio")
sys.path.insert(0, str(APP))
import ai_assistant as api
request = ET.Element("request")
manifest = APP / "ai_plugins" / "plugin.xml"
if manifest.is_file():
    plugins = ET.SubElement(request, "plugins")
    plugins.append(ET.parse(manifest).getroot())
    statuses = api.load_plugins(request)
    if any(s.get("ok") != "true" for s in statuses):
        raise RuntimeError(statuses)
engine = api.DrawingTools({"selected": 0, "bond_length": 30,
                          "line_width": 0.6, "text_size": 10,
                          "font": "Arial", "color": "#000000"})
# 使用同一个 engine 维持本轮查询、已创建对象与引用。
result = engine.call("search_drawing_templates", {"query": "benzene"})
print(result)
# 按实际返回 ID 和 schema 调用 draw_library_template，不能虚构 ID。
```

调用形式是 `engine.call(function_name, arguments_dict)`，不是 HTTP 请求，也不是对 ai_assistant.py 的 CLI 子命令。lookup_compound 与 draw_compound 要用同一个 engine；CID 不能脱离本轮查询凭空提供。对象引用不能跨轮复用。

`engine.actions` 是 XML 元素列表；构造 `<result ok="true"><actions>…</actions></result>` 只是宿主交换格式，不是可直接打开的 CDXML。当前没有通用外部注入入口，不要使用 --ai-check 等测试开关冒充生产接口。外部客户端要直接编辑现有画布，需要另行实现原生适配器，获取 aiContext 快照并保留文档 ID/serial 检查、批量回滚和单步撤销。

函数选择：结构库先 search_drawing_templates 再 draw_library_template；真实命名分子先 lookup_compound 再 draw_compound；用户给定 SMILES 用 draw_smiles；反应排版 draw_reaction，多步 draw_scheme；已有对象先 inspect_document，再用返回 refs 编辑。layout_report 用于查看布局。参数逐项查 JSON，不猜测字段。
