from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r35.exe' and sha(m['exe'])==m['exe_sha256']
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
for name,value in m['studio_sources'].items():assert sha(r/'studio'/name)==value,name
for name,value in m['backend_sources'].items():assert sha(r/'studio'/name)==value,name
headers=sorted(list((r/'engine').rglob('*.hpp'))+list((r/'engine').rglob('*.h'))+[r/'studio'/n for n in ['interop.hpp','clipboard_image.hpp','arrow_interop.hpp','compound_catalog.hpp','polyatomic_ion_entries.hpp']]+list((r/'studio/engine').glob('*.hpp'))+list((r/'studio/engine').glob('*.inc')))
h=hashlib.sha256()
for p in headers:h.update(str(p.relative_to(r)).encode());h.update(p.read_bytes())
assert h.hexdigest()==m['headers_sha256'],'engine header fingerprint'
results=json.loads((r/'work/r35-test-results.json').read_text(encoding='utf-8-sig'))
assert results=={'sharp':0,'arc':0,'current':0,'mechanism':0,'compat':0},results
for suite in results:
 for name in ['failure.txt','error.txt']:assert not (r/f'work/test-r35-{suite}'/name).exists(),(suite,name)
def load(suite,name):return json.loads((r/f'work/test-r35-{suite}'/name).read_text())
assert load('sharp','sharp-arrow-checks.json')['sharp_tip_cases']==1260
assert load('sharp','sharp-arrow-checks.json')['equilibrium_cases']==180
assert load('arc','arrow-style-audit.json')['finite_geometry']
assert load('mechanism','mechanism-step-checks.json')['undo_redo']
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
for source,name in [(out/'manifest.json','r35-build-manifest.json'),(r/'work/test-r35-sharp/sharp-arrow-checks.json','r35-sharp-arrow-checks.json'),(r/'work/test-r35-arc/arrow-style-audit.json','r35-all-arrow-styles.json'),(r/'work/test-r35-sharp/sharp-arrow-preview.png','r35-箭头样式.png'),(r/'work/test-r35-sharp/sharp-arrow-preview.cdxml','r35-箭头样式.cdxml')]:shutil.copyfile(source,dest/name)
shutil.copyfile(r/'work/interface-before-r34.ini',out/'interface.ini')
(dest/'箭头样式验收-r35.txt').write_text(f'''MoleculeStudio r35
EXE：{m['exe']}
SHA256：{m['exe_sha256'].upper()}

鱼钩箭头采用用户后附参考图的单侧、细长实心尖头，弧线接入肩部，避免粗线帽盖住尖端。普通实心箭头采用机理箭头的内凹尾勾轮廓。左右趋势分别缩短上方/下方箭头，短箭头约为长箭头的四成，分别与长箭头右端/左端对齐。平衡箭头仍等长。同步生成下一步和按钮图标；原箭头参数继续可编辑。
不等长可逆箭头与鱼钩头使用可编辑矢量曲线导出，避免 ChemDraw 的固定头部和居中短箭头改变显示。普通实心箭头保持原生箭头属性。

最终 EXE 的尖端专项、弧线与所有箭头样式、当前修复、机理生成、ChemDraw/CDXML 兼容检查均退出 0。专项覆盖 1260 个尖端和 180 个可逆箭头组合；全样式矩阵覆盖各预设、五档尺寸、四个方向和两档线宽。另以独立几何探针验证鱼钩接合和双弧线各自的接合。实际 ChemDraw 窗口打开导出文件，核对箭头尖端、尾勾、偏向和对齐。
桌面快捷方式与 .cdxml 现有 MoleculeStudio 打开入口更新到 r35。当前用户的旧窗口保留，保存后从桌面重新启动即可。
''',encoding='utf-8')
print(json.dumps({'verified':True,'exe':m['exe'],'sha256':m['exe_sha256'],'tests':results}))
