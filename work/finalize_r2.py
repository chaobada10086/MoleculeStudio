from pathlib import Path
import shutil
repo=Path.cwd();desktop=Path(r'C:\Users\ChaobaDa\Desktop');backup=repo/'work/handoff-backup-r2';backup.mkdir(exist_ok=True)
summary='''MoleculeStudio 接续更新 2026-11-16 r2
以下状态优先于后附的历史记录。

用户确认：此前提到的重大 Bug 已于凌晨自行修复，本轮不再把它作为待复现任务，也未针对它改代码。
当前测试版：C:\\Users\\ChaobaDa\\Documents\\GitHub\\MoleculeStudio\\out\\Studio\\MoleculeStudio-Progress-20261116-r2.exe
本轮完成：
1. 开始页按实际工具宽度决定压缩；六个工具组保持可访问，展开时按空间逐列增加图标，不再固定四组各三个图标。已检查 900、1100、1300、1500 逻辑像素。
2. TLC 画布拖动、Ctrl 复制、单击加点和设置预览拖动吸附目标样品列；右键复制也直接使用目标列中心。
3. TLC 单击加点限制在起点线至溶剂前沿之间；画布和预览拖动均保持 Rf 0–1。未擅自改写导入文件的原始异常测量数据。
4. 实际构建使用 studio/engine/experimental_render_bridge.cpp，之前未绘制拖尾；现与弹窗共用连续渐淡的拖尾几何。长度、不透明度修改后即时更新预览。
构建：work/progress-build-23.log。回归：--current-fixes-check work/test-progress-23-current、--table-check work/test-progress-23-table，均退出 0。TLC 检查包括此前 ChemDraw 保存的兼容样例。
Word 核对清单已同步并通过 Word 导出 PDF、逐页查看五页排版；原文件备份在 work/checklist-backup。
桌面快捷方式已指向 r2；安装版和用户原 asdf.cdxml 未改。
仍未完成：投影 Atom/Bond 普通编辑；箭头逐档人工比对和虚线实时手动核验；多语言和不同 DPI 的完整人工检查。本轮未在 ChemDraw 中重新验收拖尾的形状与透明度；CDXML 继续导出原生 Tail 属性并保留私有参数，不声称跨软件视觉完全一致。

—— 以下为原交接记录，仅作历史参考 ——

'''
for p in [desktop/'MoleculeStudio交接文档-20261116.txt',repo/'docs/MOLECULESTUDIO_HANDOFF_20261116.txt']:
 if p.exists():
  shutil.copy2(p,backup/p.name);p.write_text(summary+p.read_text(encoding='utf-8-sig'),encoding='utf-8')
output=Path(r'C:\Users\ChaobaDa\Documents\Codex\2026-11-16\c-users-chaobada-desktop-moleculestudio-20261116\outputs\MoleculeStudio改动核对清单-20261116.docx')
shutil.copy2(output,desktop/output.name)
