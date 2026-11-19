from pathlib import Path
import hashlib,json,shutil,xml.etree.ElementTree as ET
r=Path(__file__).resolve().parents[1]; out=r/'out/Studio'
manifest=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(manifest['exe']).name=='MoleculeStudio-Progress-20261117-r22.exe'
assert sha(manifest['exe'])==manifest['exe_sha256']
for row in manifest['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
for name,digest in manifest['studio_sources'].items():assert sha(r/'studio'/name)==digest,name
for name,digest in manifest['backend_sources'].items():assert sha(r/'studio'/name)==digest,name
current=r/'work/test-r22-current'; arcs=r/'work/test-r22-arcs'
assert not (current/'failure.txt').exists() and not (arcs/'error.txt').exists()
drag=json.loads((current/'live-arc-drag-checks.json').read_text())
assert drag['cases']==72 and drag['mouse_move_frames']==2952
assert all(v is True for k,v in drag.items() if k not in ['cases','mouse_move_frames'])
pastes=json.loads((arcs/'arc-parameter-checks.json').read_text())
assert pastes['independent_pastes']==20 and pastes['endpoint_bend_lock']
assert sha(out/'interface.ini')==sha(r/'work/manual-r21/interface-before.ini')
arrow=ET.parse(r/'work/manual-r21/free-drag-ui.cdxml').find('.//arrow')
params=dict(part.split('=',1) for part in arrow.attrib['MoleculeStudioArrow'].split(';') if '=' in part)
assert abs(float(params['sag'])+88.714285714285765)<1e-7
assert abs(float(params['ey'])-float(params['by']))<1e-8
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
shutil.copyfile(out/'manifest.json',dest/'r22-build-manifest.json')
shutil.copyfile(current/'live-arc-drag-checks.json',dest/'r22-箭头拖动测试.json')
report=f'''箭头自由拖动与鼠标捕捉改进 — r22

EXE：{manifest['exe']}
SHA256：{manifest['exe_sha256'].upper()}
桌面快捷方式已指向此版本，工作目录仍为 out/Studio。已打开新版空白窗口，保留用户原 r20 窗口和未保存内容。

原因与修改
1. 原先保存的全局弧度/曲率锁在每个移动事件后覆盖鼠标计算值，造成只能使用一种固定弧度。现在直接拖动中点始终自由调弯；锁定仍影响新建弧线和端点调整。参数复制粘贴的锁定规则不变。
2. 原弧线工具还要求在中点两像素内按下。现在中点捕捉统一为 12 个逻辑像素，随 DPI 调整；悬浮提示、鼠标光标和按下抓取使用同一判定。
3. 增加带白色衬边的绿色中点捕捉环、拖动光标与“拖动中点调弯 · 两端固定”提示；拖动时捕捉环随曲线中点移动。
4. 保留按下时的抓取偏移，避免刚拖动时弧线跳变。整个拖动只提交一个撤销记录，预览不改文档；Esc 可取消。

验证
最终 r22 --current-fixes-check 与 --arc-check 均退出 0。
72 组：三种锁定状态 × 选中/未选中 × 三种缩放 × 两种 DPI × 直线/已有弧线。
2952 次 WM_MOUSEMOVE：弧度连续变化，跨越直线位置，端点不动，悬浮捕捉与实际抓取一致，松开结果等于预览，单步撤销和取消通过。
20 项独立参数粘贴回归通过；端点调整仍遵守弧度锁。投影编辑、转换及机理测试随当前修复套件通过。
computer use：在与 r22 相同交互实现的 r21 中，使用用户原有 Locked=1 设置，实际新画虚线反应箭头，再依次拖为浅上弧、深上弧、下弧；一次 Ctrl+Z 回到深上弧，捕捉环与提示可见。结果另存为 work/manual-r21/free-drag-ui.cdxml。
r22 已实际启动，显示正常空白画布。
新增测试最初误用了弦中点作为已有弧线中点，已修正测试位置并完整重跑通过。
最终 EXE 与 manifest 所列源码哈希核对通过。测试前的 interface.ini 已原样恢复。未修改已安装目录的程序。

主要源码
studio/engine/arrows_tools.cpp
studio/engine/session.cpp
studio/arrow_bend_capture.hpp
studio/studio.cpp
studio/canvas_guides.hpp
studio/app_actions.hpp
studio/live_arc_drag_checks.hpp
studio/arc_parameter_checks.hpp
studio/free_arc_checks.hpp
'''
(dest/'箭头自由拖动与捕捉-r22.txt').write_text(report,encoding='utf-8')
print(json.dumps({'manifest_verified':True,'preferences_preserved':True,'cases':drag['cases'],'frames':drag['mouse_move_frames'],'report':str(dest/'箭头自由拖动与捕捉-r22.txt')},ensure_ascii=False))
