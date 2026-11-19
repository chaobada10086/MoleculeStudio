from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r23.exe'
assert sha(m['exe'])==m['exe_sha256']
for n,v in m['studio_sources'].items():assert sha(r/'studio'/n)==v,n
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
test=r/'work/test-r23-current'
assert not (test/'failure.txt').exists()
d=json.loads((test/'live-arc-drag-checks.json').read_text())
assert d['cases']==72 and d['mouse_move_frames']==2952
s=(r/'studio/arrow_bend_capture.hpp').read_text(encoding='utf-8')
assert '0x2563EBFF' in s and 'deviceRadiusToDocument(9)' in s
assert 'deviceRadiusToDocument(2.5)' in s and 'deviceRadiusToDocument(5)' in s
assert 'drawArrowBendHint' not in s
assert 'drawArrowBendHint' not in (r/'studio/canvas_guides.hpp').read_text(encoding='utf-8')
shutil.copyfile(r/'work/manual-r23/interface-before.ini',out/'interface.ini')
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
shutil.copyfile(out/'manifest.json',dest/'r23-build-manifest.json')
(dest/'箭头捕捉样式-r23.txt').write_text(f'''r23 箭头中点捕捉样式调整
EXE：{m['exe']}
SHA256：{m['exe_sha256'].upper()}
与普通原子悬浮捕捉一致：蓝色 0x2563EBFF、屏幕半径9像素、线宽2.5像素、白色衬边5像素；不随文档缩放放大。
已删除中点悬浮和拖动过程的文字说明，以及绘制提示框的代码。
自由拖动和抓取范围保持原有行为。
最终 r23 --current-fixes-check 退出0，72组拖动和2952次鼠标移动回归通过。
构建清单与实际EXE、界面源码及引擎源码哈希一致。测试前界面设置已恢复。
''',encoding='utf-8')
print(json.dumps({'verified':True,'sha256':m['exe_sha256'],'drag_cases':d['cases']}))
