from pathlib import Path
import hashlib,json,difflib,shutil
root=Path.cwd();out=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');out.mkdir(parents=True,exist_ok=True)
m=json.loads((root/'out/Studio/manifest.json').read_text());exe=Path(m['exe']);sha=hashlib.sha256(exe.read_bytes()).hexdigest()
assert exe.name=='MoleculeStudio-Progress-20261116-r10.exe' and sha==m['exe_sha256']
for rel,want in m['studio_sources'].items():assert hashlib.sha256((root/'studio'/rel).read_bytes()).hexdigest()==want,rel
for entry in m['engine_sources'].values():assert hashlib.sha256(Path(entry['path']).read_bytes()).hexdigest()==entry['sha256']
studio=root/'studio';snap=root/'engine'
headers=sorted(list(snap.rglob('*.hpp'))+list(snap.rglob('*.h'))+[studio/'interop.hpp',studio/'clipboard_image.hpp',studio/'arrow_interop.hpp',studio/'compound_catalog.hpp',studio/'polyatomic_ion_entries.hpp']+list((studio/'engine').glob('*.hpp'))+list((studio/'engine').glob('*.inc')))
h=hashlib.sha256()
for file in headers:h.update(str(file.relative_to(root)).encode());h.update(file.read_bytes())
assert h.hexdigest()==m['headers_sha256']
checks=root/'work/test-r10-release-current-final'
assert not (checks/'failure.txt').exists()
assert json.loads((checks/'projection-branch-checks.json').read_text())['conversion_pairs']==25
assert json.loads((checks/'independent-branches.json').read_text())['conversion_pairs']==25
assert json.loads((checks/'projection-spacing-checks.json').read_text())['endpoint_cases']==360
assert json.loads((checks/'independent-chemistry.json').read_text())['checks']==92
patch=''
for rel in ('engine/projection_molecule.hpp','engine/sprout_layout.hpp','projection_checks.hpp','projection_branch_checks.hpp'):
    before=root/'work/r10-backup'/Path(rel).name
    old=before.read_text(encoding='utf-8').splitlines(True) if before.exists() else []
    patch+=''.join(difflib.unified_diff(old,(studio/rel).read_text(encoding='utf-8').splitlines(True),fromfile='a/studio/'+rel,tofile='b/studio/'+rel))
(out/'projection-branches-r10.patch').write_text(patch,encoding='utf-8')
shutil.copyfile(root/'out/Studio/manifest.json',out/'r10-build-manifest.json')
shutil.copyfile(checks/'projection-branches.png',out/'r10-projection-branches.png')
(out/'投影接环与支链转换修复-r10.txt').write_text(f'''MoleculeStudio r10
程序：{exe}
SHA256：{sha.upper()}

投影端点接环优先沿投影外侧生长。转换投影时，支链绕连接原子旋转并平移，支链几何保持刚性，避免环折入费歇尔骨架或纽曼圆环。
发布 EXE --current-fixes-check 通过，覆盖真实 7 接环快捷键、五种视图的 25 组环/叔丁基/溴转换、环内几何、分子式/分子量与撤销重做。
独立 RDKit 验证 25 组复杂支链转换的异构身份一致、五元环保留。
已有 360 个标签间距案例、70 次元素快捷键及 92 项独立化学与构型检查通过。
源码、头文件和构建清单已核对。

测试图：{out/'r10-projection-branches.png'}
详细记录：{checks}
''',encoding='utf-8-sig')
print(json.dumps({'exe':str(exe),'sha256':sha.upper(),'verified':True},ensure_ascii=False))
