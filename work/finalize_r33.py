from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r33.exe' and sha(m['exe'])==m['exe_sha256']
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
for name,value in m['studio_sources'].items():assert sha(r/'studio'/name)==value,name
for name,value in m['backend_sources'].items():assert sha(r/'studio'/name)==value,name
headers=sorted(list((r/'engine').rglob('*.hpp'))+list((r/'engine').rglob('*.h'))+[r/'studio'/n for n in ['interop.hpp','clipboard_image.hpp','arrow_interop.hpp','compound_catalog.hpp','polyatomic_ion_entries.hpp']]+list((r/'studio/engine').glob('*.hpp'))+list((r/'studio/engine').glob('*.inc')))
h=hashlib.sha256()
for p in headers:h.update(str(p.relative_to(r)).encode());h.update(p.read_bytes())
assert h.hexdigest()==m['headers_sha256'],'engine header fingerprint'
for suite in ['color','current','mechanism','compat']:assert not (r/f'work/test-r33-{suite}/failure.txt').exists(),suite
def load(suite,name):return json.loads((r/f'work/test-r33-{suite}'/name).read_text())
assert all(load('color','mechanism-colour-checks.json').values())
assert load('current','independent-projection-reactions.json')['cases']==20
assert load('current','projection-mechanism-checks.json')['cases']==20
assert load('mechanism','mechanism-step-checks.json')['undo_redo']
assert load('compat','cdxml-color-checks.json')['colour_override']
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
for source,name in [(out/'manifest.json','r33-build-manifest.json'),(r/'work/test-r33-color/mechanism-colour-checks.json','r33-mechanism-colour-checks.json'),(r/'work/test-r33-current/independent-projection-reactions.json','r33-projection-stereo-checks.json'),(r/'work/test-r33-color/mechanism-colour-E2.png','r33-机理保留颜色.png'),(r/'work/test-r33-color/mechanism-colour-E2.cdxml','r33-机理保留颜色.cdxml')]:shutil.copyfile(source,dest/name)
shutil.copyfile(r/'work/manual-r32/interface-before.ini',out/'interface.ini')
(dest/'机理颜色验收-r33.txt').write_text(f'''MoleculeStudio r33 — 机理生成保留颜色
EXE：{m['exe']}
SHA256：{m['exe_sha256'].upper()}

生成下一步默认继承对应原子、保留的键以及键级变化后的键的颜色。支持直接设置的对象颜色、导入的颜色与逐字颜色。示例中苯环继续为蓝色，绿色单键变成绿色双键，离去 Br 和转移到 BH 的 H 保留红色。新生成而无对应原键的键使用正常默认颜色，不从不相关的结构或当前画笔取色。原反应物保留。
CDXML 导出同时处理导入的 studio.color 和对象覆盖色；没有自定义文字子节点的普通元素也能保存统一的标签颜色。优先级与画布一致：对象覆盖色优先于逐字颜色。

最终 r33 检查：颜色专项、当前修复回归、机理生成回归、ChemDraw/CDXML 兼容检查全部退出 0。颜色专项验证蓝色苯环、绿色单变双键、红色离去基团、逐字 H 颜色、通用碱颜色、原结构不变、撤销重做、原生与 CDXML 保存重开、直接按箭头编辑的备用路径及质子操作的结构复制。三种投影的五种视图、两种手性、E2/SN2 共 20 个组合验证颜色继承；独立 RDKit 复核 E/Z、SN2 反转和旁观中心保持，均通过。

oc 自动摄氏度转换未加入：IUPAC 保护基缩写规则中 oc 表示 oxycarbonyl（–OCO–），脂质命名缩写中 Oc 表示 octyl（辛基）。
https://publications.iupac.org/pac/pdf/2013/pdf/8501x0307.pdf
https://iupac.qmul.ac.uk/lipid/appABC.html
''',encoding='utf-8')
print(json.dumps({'verified':True,'exe':m['exe'],'sha256':m['exe_sha256'],'projection_cases':20}))
