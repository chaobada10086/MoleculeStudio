from pathlib import Path
import difflib, hashlib, json, shutil

root=Path.cwd()
out=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs')
out.mkdir(parents=True,exist_ok=True)
m=json.loads((root/'out/Studio/manifest.json').read_text())
exe=Path(m['exe']);sha=hashlib.sha256(exe.read_bytes()).hexdigest()
assert exe.name=='MoleculeStudio-Progress-20261116-r8.exe' and sha==m['exe_sha256']
for key,want in m['studio_sources'].items():
    assert hashlib.sha256((root/'studio'/key).read_bytes()).hexdigest()==want,key
for key,data in m['engine_sources'].items():
    assert hashlib.sha256(Path(data['path']).read_bytes()).hexdigest()==data['sha256'],key
for key,want in m['backend_sources'].items():
    for base in ('studio','out/Studio'):
        assert hashlib.sha256((root/base/key).read_bytes()).hexdigest()==want,key
current=root/'work/test-r8-release-current-verified'
tables=root/'work/test-r8-release-table'
general=root/'work/test-r8-dev-general'
for directory in (current,tables,general):
    assert not (directory/'failure.txt').exists(),directory
p=json.loads((current/'projection-checks.json').read_text())
assert p['hover_element_shortcuts']==70 and p['conversion_pairs']==25
assert json.loads((general/'tests.json').read_text())['shortcut_defaults_and_customization']
assert all(json.loads((tables/'tlc-checks.json').read_text()).values())
assert json.loads((current/'independent-chemistry.json').read_text())['checks']==92
files=['studio.cpp','app_actions.hpp','editor_hotkeys.hpp','projection_actions.hpp','projection_checks.hpp','engine/core_document.cpp']
patch=''
for name in files:
    before=(root/'work/r8-backup'/Path(name).name).read_text(encoding='utf-8').splitlines(True)
    after=(root/'studio'/name).read_text(encoding='utf-8').splitlines(True)
    patch+=''.join(difflib.unified_diff(before,after,fromfile='a/studio/'+name,tofile='b/studio/'+name))
(out/'projection-hotkeys-r8.patch').write_text(patch,encoding='utf-8')
shutil.copyfile(root/'out/Studio/manifest.json',out/'r8-build-manifest.json')
for width in (900,1500):
    shutil.copyfile(current/f'ribbon-tab-4-{width}.png',out/f'r8-table-ribbon-{width}.png')
report=f'''MoleculeStudio r8 · 投影快捷键及工具栏图标修复

程序：{exe}
SHA256：{sha.upper()}
构建清单：{root/'out/Studio/manifest.json'}

修复：
1. 鼠标悬浮在费歇尔、锯架式、纽曼的原子/端点时，B → Br、L → Cl，及其他原子快捷键采用普通分子的统一逻辑。
2. 已处理的快捷键不再被后续 WM_CHAR 当作文字输入，避免弹出标签编辑框。
3. 替换旧基团或其他元素时清理旧标签，并重新推算氢数；保留连接关系、投影与立体元数据。一次撤销恢复原基团。
4. 投影捕捉提示与普通原子快捷键使用相同捕捉距离。
5. 工具栏自动收起文字后，图标保持展开时的绘制尺寸。

验证：
70 次真实 Win32 键盘/字符消息回归通过；覆盖 5 种投影视图、1/2/4 倍缩放、B/L/C/N/O、已编辑基团替换及撤销重做。
16 种分子模板、25 组投影转换、36 个糖投影分子式通过。
独立 RDKit 化学与绘制构型检查 92 项通过。
发布 EXE 的 --current-fixes-check、--table-check（含 TLC）通过。
同一源码调试构建的完整 --test 回归通过。
900 与 1500 像素窗口截图已检查图标尺寸。
源码及后端副本与构建清单哈希一致。

请保存当前文档，重新从桌面 MoleculeStudio 快捷方式打开 r8。
测试明细：{current}
源码补丁：{out/'projection-hotkeys-r8.patch'}
'''
(out/'投影快捷键与图标修复-r8.txt').write_text(report,encoding='utf-8-sig')
print(json.dumps({'exe':str(exe),'sha256':sha.upper(),'verified':True},ensure_ascii=False))
