from pathlib import Path
import json,hashlib,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r27.exe' and sha(m['exe'])==m['exe_sha256']
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256']
for name,value in m['studio_sources'].items():assert sha(r/'studio'/name)==value,name
for n in ['table','current']:assert not (r/f'work/test-r27-{n}/failure.txt').exists()
tlc=json.loads((r/'work/test-r27-table/tlc-checks.json').read_text())
assert tlc['double_click_no_extra_spot'] and tlc['default_three_spots'] and tlc['deferred_click_single_undo']
shutil.copyfile(r/'work/manual-r25/interface-before.ini',out/'interface.ini')
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
shutil.copyfile(out/'manifest.json',dest/'r27-build-manifest.json')
shutil.copyfile(r/'work/test-r27-table/tlc-checks.json',dest/'r27-TLC检查.json')
(dest/'四宫格与TLC双击修复-r27.txt').write_text(f'''四宫格与 TLC 双击修复 — r27
EXE：{m['exe']}
SHA256：{m['exe_sha256'].upper()}
生成箭头四个按钮改成两行两列；正常布局与自动压缩布局均用 computer use 检查。选中按钮颜色加深仍有效。
TLC 板内单击加点延后到系统双击判定结束；双击取消待加点并进入详细编辑，编辑器打开前不修改斑点与撤销记录。普通单击仍加一个斑点，可单步撤销。
新建默认三条样品列、各一个斑点，共三个点。已保存的自定义模板继续生效。
最终版本 --table-check（含 TLC 专项）和 --current-fixes-check 都退出0。
专项以真实 WM_LBUTTONDOWN/UP/DBLCLK 事件打开详细编辑器，核对斑点数不变，取消后延迟计时器也不会再加点；另核对单击、撤销和默认三点。
EXE、引擎源码、界面源码与 manifest 哈希一致。界面设置恢复到测试前。保留用户旧版窗口及未保存内容。
''',encoding='utf-8')
print(json.dumps({'verified':True,'sha256':m['exe_sha256'],'double_click_no_extra_spot':tlc['double_click_no_extra_spot'],'default_three_spots':tlc['default_three_spots']}))
