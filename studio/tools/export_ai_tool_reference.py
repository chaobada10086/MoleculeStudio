"""Keep shipped AI schemas and tool tables synchronized with registered tools."""
from pathlib import Path
import sys,json,re
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
import ai_assistant as ai
from ai_plugins import draw_plugin as plugin
base_count=len(ai.TOOLS);plugin.install_plugin(vars(ai));functions=[t['function'] for t in ai.TOOLS];plugin_count=len(functions)-base_count
(root/'AI_TOOL_REFERENCE.json').write_text(json.dumps(dict(application='MoleculeStudio 0.95',revision='2026-11-01',plugin_version=plugin.PLUGIN_VERSION,count=len(functions),functions=functions),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
path=root/'AI_TOOL_API.md'
if path.exists():
 text=path.read_text(encoding='utf-8');start=text.index('| 函数 | 用途 |',text.index('## 实际工具目录'));end=text.index('## 数据与接口',start)
 rows='\n'.join('| `'+f['name']+'` | '+f['description'].replace('|',' / ')+' |' for f in functions)
 text=text[:start]+'| 函数 | 用途 |\n|---|---|\n'+rows+'\n\n'+text[end:]
 text=re.sub(r'基础工具 \d+ 个，插件工具 \d+ 个，合计 \d+ 个',f'基础工具 {base_count} 个，插件工具 {plugin_count} 个，合计 {len(functions)} 个',text)
 path.write_text(text,encoding='utf-8')
