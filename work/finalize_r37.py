from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r37.exe' and sha(m['exe'])==m['exe_sha256']
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
for name,value in m['studio_sources'].items():assert sha(r/'studio'/name)==value,name
for name,value in m['backend_sources'].items():assert sha(r/'studio'/name)==value,name
headers=sorted(list((r/'engine').rglob('*.hpp'))+list((r/'engine').rglob('*.h'))+[r/'studio'/n for n in ['interop.hpp','clipboard_image.hpp','arrow_interop.hpp','compound_catalog.hpp','polyatomic_ion_entries.hpp']]+list((r/'studio/engine').glob('*.hpp'))+list((r/'studio/engine').glob('*.inc')))
h=hashlib.sha256()
for p in headers:h.update(str(p.relative_to(r)).encode());h.update(p.read_bytes())
assert h.hexdigest()==m['headers_sha256'],'engine header fingerprint'

results=json.loads((r/'work/r37-test-results.json').read_text(encoding='utf-8-sig'))
assert results=={'cycle':0,'current':0,'mechanism':0,'compat':0},results
for suite in results:
 for name in ['failure.txt','error.txt']:assert not (r/f'work/test-r37-{suite}'/name).exists(),(suite,name)
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
for a,b in [(out/'manifest.json','r37-build-manifest.json'),(r/'work/test-r37-cycle/catalytic-demo.cdxml','r37-催化循环示例.cdxml'),(r/'work/test-r37-cycle/catalytic-demo.r62doc','r37-催化循环示例.r62doc'),(r/'work/test-r37-cycle/catalytic-demo.png','r37-催化循环示例.png'),(r/'work/test-r37-cycle/catalytic-checks.json','r37-catalytic-checks.json')]:shutil.copyfile(a,dest/b)
shutil.copyfile(r/'work/interface-before-r36.ini',out/'interface.ini')
(dest/'催化循环试用-r37.txt').write_text('机理绘制 → 催化循环\n空白选区点击顺/逆时针循环：创建 3–12 个金属占位节点。已有结构：框选 3–12 个独立分子后排布，保留各分子内部坐标、颜色和键长。半径自动为较大结构留白。选中整套循环再次排布可改变宽高和方向。\n选一根循环箭头，再点加入试剂/放出产物，插入 R 占位并创建绑定支线；也可同时选中一个独立分子作为试剂/产物。支线可按普通弧线编辑。拖动中间体或试剂，连接跟随，可撤销。\n选一个金属中心点击金属与配体，可编辑元素、氧化态上标及最多八个末端配体（英文逗号分隔）。非末端结构保留；氧化态与电荷分开。空白选区可插入新配合物。\n本版为绘图辅助工具；完整金属催化循环的化学步骤由用户指定。\n专项验证了绑定、节点内部键长保持、反向重排复用原箭头、拖动联动及单步撤销重做、原生与 CDXML 保存重开。原有机理和兼容回归通过。\nEXE：'+m['exe']+'\nSHA256：'+m['exe_sha256'].upper(),encoding='utf-8')
print(json.dumps({'verified':True,'exe':m['exe'],'sha256':m['exe_sha256'],'tests':results}))
