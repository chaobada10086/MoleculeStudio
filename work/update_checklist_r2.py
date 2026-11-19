from pathlib import Path
from docx import Document
import shutil
source=Path(r'C:\Users\ChaobaDa\Desktop\MoleculeStudio改动核对清单-20261116.docx')
out=Path(r'C:\Users\ChaobaDa\Documents\Codex\2026-11-16\c-users-chaobada-desktop-moleculestudio-20261116\outputs');out.mkdir(exist_ok=True)
backup=Path('work/checklist-backup');backup.mkdir(exist_ok=True);shutil.copy2(source,backup/source.name)
d=Document(source)
replacements={
'核对：把窗口宽度缩到约 1450 逻辑像素以下，文字与排列分组隐藏，常用工具显示图标及下拉入口。':'核对：逐步改变窗口宽度，六组工具应始终可访问；展开时按可用宽度增加图标列，组菜单可访问其余工具。',
'核对：选择工具点住一个斑点，可在板内移动到新位置或另一列，其他点不动。':'核对：可跨列拖动；拖动预览与落点均吸附目标样品列，不能停在列间。Rf 限制在 0–1，其他点不动。',
'核对：按 Ctrl 拖动一个点，原点保持原位，目标处新增一点。':'核对：按 Ctrl 拖动一个点，原点保持原位；副本吸附目标样品列，Rf 限制在 0–1。',
'核对：改变不透明度，尾迹应逐层减淡；参考了 TLC 拖尾照片和教学资料。':'核对：修改不透明度后即时更新连续渐淡的拖尾；弹窗与画布使用相同几何。ChemDraw 显示仍需本轮复核。',
'核对：勾选拖尾并改变长度，观察斑点向原点方向拉伸的范围。':'核对：勾选拖尾并改变长度，预览即时更新；应用后画布显示相同连续拖尾，导出图像保留效果。',
'□  ChemDraw 26 实心斑点与拖尾   ［已实机核对］':'□  ChemDraw 26 实心斑点与拖尾   ［此前已核对，本轮待复核］',
'□  本次测试版已通过 --arc-check；报告见 work/test-progress-18-arc。':'□  箭头沿用此前回归：work/test-progress-18-arc；本轮未调整箭头。',
'□  本次测试版已通过 --table-check；报告见 work/test-progress-18-table，使用 ChemDraw 26 保存回来的 TLC 文件。':'□  本轮 TLC 检查覆盖列吸附、Rf 边界、连续拖尾和保存往返；结果见 work/test-progress-23-table。',
'□  本次测试版已通过 --template-library-check 与 --current-fixes-check；报告分别见 work/test-progress-18-library、work/test-progress-19-current。':'□  本轮界面检查覆盖 900–1500 逻辑像素；结果见 work/test-progress-23-current。模板库沿用此前回归。',
}
for p in d.paragraphs:
 text=p.text
 new=replacements.get(text,text).replace('MoleculeStudio-Progress-20261116.exe','MoleculeStudio-Progress-20261116-r2.exe')
 if new!=text:
  if p.runs:
   p.runs[0].text=new
   for r in p.runs[1:]:r.text=''
  else:p.add_run(new)
d.save(out/source.name)
