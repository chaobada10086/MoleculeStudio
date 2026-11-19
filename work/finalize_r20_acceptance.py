from pathlib import Path
import hashlib,json,shutil,xml.etree.ElementTree as ET

root=Path.cwd();studio=root/'studio';snap=root/'engine'
out=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs')
m=json.loads((root/'out/Studio/manifest.json').read_text(encoding='utf-8'))
exe=Path(m['exe']);sha=hashlib.sha256(exe.read_bytes()).hexdigest()
assert exe.name=='MoleculeStudio-Progress-20261117-r20.exe' and sha==m['exe_sha256']
for rel,want in m['studio_sources'].items():assert hashlib.sha256((studio/rel).read_bytes()).hexdigest()==want,rel
for rel,entry in m['engine_sources'].items():assert hashlib.sha256(Path(entry['path']).read_bytes()).hexdigest()==entry['sha256'],rel
for rel,want in m['backend_sources'].items():
    for base in (studio,root/'out/Studio'):assert hashlib.sha256((base/rel).read_bytes()).hexdigest()==want,rel
headers=sorted(list(snap.rglob('*.hpp'))+list(snap.rglob('*.h'))+[studio/'interop.hpp',studio/'clipboard_image.hpp',studio/'arrow_interop.hpp',studio/'compound_catalog.hpp',studio/'polyatomic_ion_entries.hpp']+list((studio/'engine').glob('*.hpp'))+list((studio/'engine').glob('*.inc')))
h=hashlib.sha256()
for file in headers:h.update(str(file.relative_to(root)).encode());h.update(file.read_bytes())
assert h.hexdigest()==m['headers_sha256']
previous=json.loads((out/'r19-build-manifest.json').read_text(encoding='utf-8'))
for key in ('engine_sources','backend_sources','headers_sha256'):assert m[key]==previous[key],key
load=lambda p:json.loads((root/p).read_text(encoding='utf-8-sig'))
for folder in ('test-r20-current','test-r20-arcs','test-r19-general','test-r19-table'):
    assert not(root/'work'/folder/'failure.txt').exists(),folder
    assert not(root/'work'/folder/'error.txt').exists(),folder
for folder in ('test-r20-chrome','test-r19-chrome-baseline'):
    assert (root/'work'/folder/'failure.txt').read_text(encoding='utf-8').strip()=='menu chooses real tool'
assert load('work/test-r20-current/projection-mechanism-checks.json')['appended_products_in_fragments']==20
assert load('work/test-r20-arcs/arc-parameter-checks.json')['independent_pastes']==20
assert load('work/test-r19-general/tests.json')['save_reload']
assert load('work/test-r19-table/tlc-checks.json')['native_and_cdxml_roundtrip']
assert load('work/test-r19-current/independent-appended-products.json')['appended_product_cases']==20
assert load('work/test-r19-current/independent-projection-reactions.json')['cases']==20
assert load('work/test-r19-current/independent-chemistry.json')['checks']==92
manual=root/'work/manual-r17'
arcs=load('work/manual-r17/arc-ui-independent-checks.json')
assert len(arcs)==5 and all(v['passed'] for v in arcs.values())
assert load('work/manual-r17/r20-style-ui.json')['four_styles_one_click']
chemistry=load('work/manual-r17/r19-manual-independent.json')
assert chemistry['E2']['matches']==2 and chemistry['SN2']['matches']==2
assert chemistry['TLC']['native_outer_border_roundtrip']
assert ET.parse('C:/Users/ChaobaDa/Desktop/asdf-ChemDraw.cdxml').getroot().tag=='CDXML'
shutil.copyfile(root/'out/Studio/manifest.json',out/'r20-build-manifest.json')
evidence={'arc_UI_parameter_checks':arcs,'ChemDraw_and_UI_generated_products':chemistry,
          'generation_styles':load('work/manual-r17/r20-style-ui.json'),
          'appended_products':load('work/test-r19-current/independent-appended-products.json')}
(out/'r20-实机验收数据.json').write_text(json.dumps(evidence,indent=2,ensure_ascii=False),encoding='utf-8')
report=f'''MoleculeStudio r20 · computer-use 实机验收
日期：2026-11-17
程序：{exe}
SHA256：{sha.upper()}

这次 computer-use 已恢复正常，已实际操作 MoleculeStudio 与 ChemDraw。

本次实测发现并追加修复
1. 生成箭头样式变化未计入工具栏重绘条件，深色选中提示会停在旧按钮。现已立即刷新。
2. 提示框可能挡住下一行按钮。现让提示框转发鼠标事件，点击样式时关闭提示框。
3. 打开 CDXML 后新增产物、支链或连接已有分子，保存时新增原子/键可能落在分子容器之外，独立读取器漏读。现按连接关系归入同一个 fragment；需要时合并连接的分子容器，保留原子、键编号和坐标。

实机检查
• 曲线箭头：直接拖动未选中箭头的中点变成弧线；拖过弦线可反向；一步撤销恢复。
• 五项右键复制/粘贴均逐项点击并保存结果。独立数值核对：圆心、半径、弧度、曲率、画布起始角度均生效，其他独立参数保持。半径和曲率依照 1/R 联动。
• 三种投影：实际完成费歇尔→锯架→纽曼→费歇尔右键转换；各视图 B/L 修改端基生效，捕捉提示和分子量随编辑刷新。
• 机理生成：实际框选锯架 E2、纽曼 SN2 的反应物和电子箭头，点击生成下一步，生成整理后的产物。保存结果的分子身份与独立验证过的产物一致。
• ChemDraw：修复后的桌面 asdf-ChemDraw.cdxml 能打开；修复容器后的 E2/SN2 文件能完整显示新增产物。TLC 外边框实际可见，另存后的 ShowBorders="yes" 和 18 个斑点均保留。
• 工具栏：小窗口模板页在空间足够时保留文字，表格页放不下时压缩，图标尺寸一致。四种生成箭头按钮有悬浮文字，选中深色提示正确。

自动验证
• r20：--current-fixes-check、--arc-check 均退出 0，四个样式按钮的单击切换与提示关闭已实机复验。
• r19：完整 --test、启用 TLC 的 --table-check 均退出 0；r20 与 r19 的全部引擎、后端、头文件指纹完全一致，后续仅改了提示框交互。
• 独立 RDKit：20 组 SN2/E2 视图/手性组合、92 项化学与绘制构型、25 组接环转换通过。
• 20 组“读取 CDXML→追加产物→保存”可完整读回；新增支链和连接已有 fragment 的产物为单个连通的丙烷分子。
• 360 个标签间距、70 次元素快捷键、5 个连续鼠标预览帧、20 组圆弧参数粘贴和弧度锁定过零检查通过。
• 同轮额外 20 个二级碳 SN2 R/S 组合已验证 R→S、S→R 和旁观中心保持，记录位于 work/test-r17-secondary-sn2。

额外检查的已知限制
--chrome-check 退出 3，停在旧断言“menu chooses real tool”；r19 基线也在相同位置失败。该检查仍要求旧弧线预设逐一保留，而现有 chooseArrowPreset 已将非机理旧弧线预设统一映射到自由弧线 58，arrow_shortcut_checks.hpp 已采用这一规则，chrome_checks.hpp 尚未同步。此项不算通过；本次未扩展修改这套旧自检。

实机操作记录与样本：{manual}
最终专项测试：{root/'work/test-r20-current'}
完整回归：{root/'work/test-r19-general'}
已保留用户原先的 r9 未保存文档；安装目录程序未替换。测试期间的界面设置已恢复，桌面快捷方式指向 r20。
'''
(out/'computer-use-验收-r20.txt').write_text(report,encoding='utf-8-sig')
old=out/'投影机理与箭头交互修复-r17.txt'
if old.exists():
    s=old.read_text(encoding='utf-8-sig')
    a='已收到用户睡觉后可进行 computer-use 实测的授权，但工具再次返回“被实体 Esc 停止”，因此没有继续界面操作。ChemDraw 实际打开及统一界面验收尚未完成，不能将自动回归算作这项验收。'
    b='后续补充（2026-11-17）：computer-use 已恢复并完成实际界面与 ChemDraw 检查，追加修复已纳入 r20。最终验收记录见同目录 computer-use-验收-r20.txt。'
    old.write_text(s.replace(a,b),encoding='utf-8-sig')
print(json.dumps({'exe':str(exe),'sha256':sha.upper(),'all_source_hashes_verified':True,'report':str(out/'computer-use-验收-r20.txt')},ensure_ascii=False))
