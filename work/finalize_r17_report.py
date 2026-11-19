from pathlib import Path
import hashlib,json,shutil,xml.etree.ElementTree as ET
root=Path.cwd();studio=root/'studio';snap=root/'engine'
out=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');out.mkdir(parents=True,exist_ok=True)
m=json.loads((root/'out/Studio/manifest.json').read_text());exe=Path(m['exe']);sha=hashlib.sha256(exe.read_bytes()).hexdigest()
assert exe.name=='MoleculeStudio-Progress-20261117-r17.exe' and sha==m['exe_sha256']
for rel,want in m['studio_sources'].items():assert hashlib.sha256((studio/rel).read_bytes()).hexdigest()==want,rel
for rel,entry in m['engine_sources'].items():assert hashlib.sha256(Path(entry['path']).read_bytes()).hexdigest()==entry['sha256'],rel
for rel,want in m['backend_sources'].items():
    for base in (studio,root/'out/Studio'):assert hashlib.sha256((base/rel).read_bytes()).hexdigest()==want,rel
headers=sorted(list(snap.rglob('*.hpp'))+list(snap.rglob('*.h'))+[studio/'interop.hpp',studio/'clipboard_image.hpp',studio/'arrow_interop.hpp',studio/'compound_catalog.hpp',studio/'polyatomic_ion_entries.hpp']+list((studio/'engine').glob('*.hpp'))+list((studio/'engine').glob('*.inc')))
h=hashlib.sha256()
for file in headers:h.update(str(file.relative_to(root)).encode());h.update(file.read_bytes())
assert h.hexdigest()==m['headers_sha256']
checks=root/'work/test-r17-current';arcs=root/'work/test-r17-arcs'
assert not(checks/'failure.txt').exists() and not(arcs/'error.txt').exists()
load=lambda folder,name:json.loads((folder/name).read_text())
assert load(checks,'projection-mechanism-checks.json')['cases']==20
assert load(checks,'independent-projection-reactions.json')['cases']==20
assert load(checks,'projection-spacing-checks.json')['endpoint_cases']==360
assert load(checks,'projection-checks.json')['hover_element_shortcuts']==70
assert load(checks,'independent-chemistry.json')['checks']==92
assert load(checks,'independent-branches.json')['ring_clears_projection_bonds']
assert load(checks,'live-arc-drag-checks.json')['mouse_move_frames']==5
assert load(arcs,'arc-parameter-checks.json')['independent_pastes']==20
matrix=root/'work/test-r17-secondary-sn2'
assert load(matrix,'sn2-rs-matrix.json')['starting_configurations']==['R','S']
for folder in ('work/test-r14-general','work/test-r14-mechanism'):
    assert not(root/folder/'failure.txt').exists()
assert load(root/'work/test-r14-general','tests.json')['save_reload']
assert (root/'work/test-r14-mechanism/mechanism-step-checks.json').exists()
fixed=Path('C:/Users/ChaobaDa/Desktop/asdf-ChemDraw.cdxml');assert ET.parse(fixed).getroot().tag=='CDXML'
shutil.copyfile(root/'out/Studio/manifest.json',out/'r17-build-manifest.json')
for source,name in [('ribbon-tab-5-900.png','r17-template-ribbon.png'),('ribbon-tab-6-900.png','r17-generation-buttons.png'),('projection-mechanism-0-1-0.png','r17-sawhorse-elimination.png'),('projection-mechanism-0-2-0.png','r17-newman-elimination.png'),('projection-branches.png','r17-projection-branches.png')]:shutil.copyfile(checks/source,out/name)
report=f'''MoleculeStudio r17 · 投影机理、箭头交互与工具栏
日期：2026-11-17
程序：{exe}
SHA256：{sha.upper()}

本轮完成
1. 投影接环朝外，投影转换时刚性旋转支链并避开其余键，修复环压入费歇尔骨架、纽曼圆环和锯架轴的问题。
2. 三种投影共用机理计算中的立体信息。消除后按局部三维信息确定 E/Z，按正常键长与 120° 排布双键；SN2 反应中心反转，旁观中心保持，使用明确的四面体楔键画法。
3. 修正 Et 等嵌套缩写的内部连接坐标，防止在识别相邻楔键时错误判定构型。
4. B⁻ 到投影 H 的箭头按实际字形停靠，优先排除穿过反应物键线的路线；纽曼前点与后圆的原子语义分开，隐藏轴关联可正确校验。
5. 反应箭头中部拖拽实时更新完整箭头预览，鼠标松开后提交，一步撤销。选中/未选中箭头走相同编辑路径。
6. 右键支持复制/粘贴圆心、半径、弧度、曲率、画布起始角度。粘贴只改对应独立参数；半径与曲率按 1/R 联动。画布向右为 0°、向下为 90°。
7. 修复锁定弧度后手柄拖到弦线时绕过锁定的问题。
8. 工具栏按实际是否需要第二行压缩，模板按钮按文字实际宽度计算；图标尺寸保持一致。
9. 生成箭头的四种样式为相邻图形按钮，悬浮提示名称、选中深色提示；点击生成时使用选定样式。
10. 保存 .cdxml 时输出 XML。桌面 asdf.cdxml 的原始内容是 R62EXT 原生数据，已生成修复文件 {fixed}，并保留 asdf-原始备份.r62doc。

验证
最终 EXE --current-fixes-check、--arc-check 通过。
SN2/E2 20 个视图及手性组合：独立 RDKit 核对 E/Z、反转及旁观中心保持，五种视图产物一致。
额外 20 个二级碳底物 SN2 R/S 矩阵：覆盖 R→S 和 S→R、两种旁观手性及五种视图，通过。
360 个标签间距案例、70 次元素快捷键、25 组接环转换、92 项独立化学/绘制构型检查通过。
5 个实际 WM_MOUSEMOVE 连续预览帧，核对松手前文档不变、松手后一次提交及撤销。
20 组圆弧独立参数粘贴，核对其余参数保持、大小弧/正反方向及撤销重做。
同轮 r14 的完整 --test、--mechanism-step-check 通过；之后的修改仅进一步限制投影碱箭头穿线，并在最终投影专项中复验。
源码、后端副本、全部头文件指纹与构建清单一致。

已收到用户睡觉后可进行 computer-use 实测的授权，但工具再次返回“被实体 Esc 停止”，因此没有继续界面操作。ChemDraw 实际打开及统一界面验收尚未完成，不能将自动回归算作这项验收。

详细测试：{checks}
圆弧测试：{arcs}
SN2 R/S 矩阵：{matrix}
'''
(out/'投影机理与箭头交互修复-r17.txt').write_text(report,encoding='utf-8-sig')
print(json.dumps({'exe':str(exe),'sha256':sha.upper(),'all_hashes_verified':True,'report':str(out/'投影机理与箭头交互修复-r17.txt')},ensure_ascii=False))
