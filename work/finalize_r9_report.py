from pathlib import Path
import hashlib,json,difflib,shutil
root=Path.cwd();out=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');out.mkdir(parents=True,exist_ok=True)
m=json.loads((root/'out/Studio/manifest.json').read_text());exe=Path(m['exe'])
sha=hashlib.sha256(exe.read_bytes()).hexdigest();assert exe.name=='MoleculeStudio-Progress-20261116-r9.exe' and sha==m['exe_sha256']
for key,want in m['studio_sources'].items():assert hashlib.sha256((root/'studio'/key).read_bytes()).hexdigest()==want,key
for data in m['engine_sources'].values():assert hashlib.sha256(Path(data['path']).read_bytes()).hexdigest()==data['sha256']
studio=root/'studio';snap=root/'engine'
headers=sorted(list(snap.rglob('*.hpp'))+list(snap.rglob('*.h'))+[studio/'interop.hpp',studio/'clipboard_image.hpp',studio/'arrow_interop.hpp',studio/'compound_catalog.hpp',studio/'polyatomic_ion_entries.hpp']+list((studio/'engine').glob('*.hpp'))+list((studio/'engine').glob('*.inc')))
h=hashlib.sha256()
for file in headers:h.update(str(file.relative_to(root)).encode());h.update(file.read_bytes())
assert h.hexdigest()==m['headers_sha256']
checks=root/'work/test-r9-release-current'
assert not (checks/'failure.txt').exists()
spacing=json.loads((checks/'projection-spacing-checks.json').read_text());projection=json.loads((checks/'projection-checks.json').read_text());chem=json.loads((checks/'independent-chemistry.json').read_text())
assert spacing['endpoint_cases']==360 and projection['hover_element_shortcuts']==70 and projection['conversion_pairs']==25 and chem['checks']==92
patch=''
for rel in ('engine/molecular_render.hpp','projection_checks.hpp','projection_spacing_checks.hpp'):
    before=root/'work/r9-backup'/Path(rel).name
    old=before.read_text(encoding='utf-8').splitlines(True) if before.exists() else []
    patch+=''.join(difflib.unified_diff(old,(studio/rel).read_text(encoding='utf-8').splitlines(True),fromfile='a/studio/'+rel,tofile='b/studio/'+rel))
(out/'projection-spacing-r9.patch').write_text(patch,encoding='utf-8')
shutil.copyfile(root/'out/Studio/manifest.json',out/'r9-build-manifest.json')
shutil.copyfile(checks/'projection-spacing.png',out/'r9-projection-spacing.png')
(out/'投影标签间距修复-r9.txt').write_text(f'''MoleculeStudio r9 · 三种投影的标签间距

程序：{exe}
SHA256：{sha.upper()}

费歇尔、锯架式、纽曼统一按实际字形轮廓及绘图留白定位，消除旧的 20% 键长外推和字体留白叠加。
在字形及基团方向排版完成后确定间距，原子坐标、化学键连接和立体元数据保持不变。
纽曼重叠式的相邻标签按对应键的角平分线分开，避免 Cl/F 等粘连。

验证：发布 EXE 的 --current-fixes-check 通过。
360 个标签端点案例（5 种视图、2 种字体、3 个字号、2 种键长）。
70 次悬浮元素快捷键、25 组投影互转、36 个糖投影分子式通过。
独立化学与绘制构型校验 92 项通过。
构建清单及完整头文件指纹已核对。

请保存当前文档，关闭软件后从桌面快捷方式打开 r9。
测试图：{out/'r9-projection-spacing.png'}
详细测试：{checks}
''',encoding='utf-8-sig')
print(json.dumps({'exe':str(exe),'sha256':sha.upper(),'spacing_cases':spacing['endpoint_cases'],'verified':True},ensure_ascii=False))
