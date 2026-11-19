from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r41.exe' and sha(m['exe'])==m['exe_sha256']
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
for name,value in m['studio_sources'].items():assert sha(r/'studio'/name)==value,name
for name,value in m['backend_sources'].items():assert sha(r/'studio'/name)==value,name
headers=sorted(list((r/'engine').rglob('*.hpp'))+list((r/'engine').rglob('*.h'))+[r/'studio'/n for n in ['interop.hpp','clipboard_image.hpp','arrow_interop.hpp','compound_catalog.hpp','polyatomic_ion_entries.hpp']]+list((r/'studio/engine').glob('*.hpp'))+list((r/'studio/engine').glob('*.inc')))
h=hashlib.sha256()
for p in headers:h.update(str(p.relative_to(r)).encode());h.update(p.read_bytes())
assert h.hexdigest()==m['headers_sha256'],'engine header fingerprint'

results=json.loads((r/'work/r41-test-results.json').read_text(encoding='utf-8-sig'))
assert results=={'v2':0,'cycle':0,'current':0,'mechanism':0,'compat':0,'arc':0},results
for suite in results:
 for name in ['failure.txt','error.txt']:assert not (r/f'work/test-r41-{suite}'/name).exists(),(suite,name)
c=json.loads((r/'work/test-r41-v2/catalytic-v2-checks.json').read_text());assert c['metal_format_cases']==32 and all(v for v in c.values())
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
for a,b in [(out/'manifest.json','r41-build-manifest.json'),(r/'work/test-r41-v2/smooth-catalytic-demo.cdxml','r41-平滑催化循环.cdxml'),(r/'work/test-r41-v2/smooth-catalytic-demo.r62doc','r41-平滑催化循环.r62doc'),(r/'work/test-r41-v2/smooth-catalytic-demo.png','r41-平滑催化循环.png'),(r/'work/test-r41-v2/catalytic-v2-checks.json','r41-catalytic-v2-checks.json')]:shutil.copyfile(a,dest/b)
shutil.copyfile(r/'work/interface-before-r38.ini',out/'interface.ini')
(dest/'催化循环升级-r41.txt').write_text('图形页新增独立椭圆工具。圆或椭圆边框右键 → 添加催化循环路径，打开合并设置页，继承中心、半径、旋转角；确认后转换为带留白的平滑路径，整体操作可撤销。\n设置页同时显示循环与配合物的真实结构预览。新建时可选择金属、氧化态和末端配体；价态支持罗马/阿拉伯数字与括号/上标/同行组合。激发态 * 独立上标。\n生成的配合物支持右键官能团收起/展开，保留实际原子和键；元数据支持原生及 CDXML 保存重开与内部复制。节点以金属原子为定位点，路径避让实际文字和配体。\n普通弧形反应箭头交叉时，后画的曲线形成可编辑的 S 形平滑过渡，起终点和先画的箭头保留。电子转移机理箭头继续使用原有路由。\n最终 EXE 六组检查均通过：32 种金属显示组合、真实结构收起展开、继承椭圆几何、预览、S 形转换、保存重开、旧循环、当前修复、机理生成、兼容与全箭头样式。界面实际检查了椭圆绘制、右键入口、尺寸继承、预览、激发态勾选和右键展开。\nEXE：'+m['exe']+'\nSHA256：'+m['exe_sha256'].upper(),encoding='utf-8')
print(json.dumps({'verified':True,'exe':m['exe'],'sha256':m['exe_sha256'],'tests':results}))
