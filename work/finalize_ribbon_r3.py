from pathlib import Path
from PIL import Image
import shutil
repo=Path.cwd();desktop=Path(r'C:\Users\ChaobaDa\Desktop')
summary='''MoleculeStudio 接续更新 2026-11-16 r3
本段优先于后面的历史状态。
测试版：C:\\Users\\ChaobaDa\\Documents\\GitHub\\MoleculeStudio\\out\\Studio\\MoleculeStudio-Progress-20261116-r3.exe
用户新要求已实现：
1. 低分辨率时只保留工具按钮图标，保留组名和悬停提示。常用页在可用宽度小于 1100 逻辑像素或带文字布局放不下时，改为三行图标；不再直接缩成每组三个按钮。宽度足够恢复带文字布局。
2. 生成下一步、下一步 + 质子转移、下一步 − 质子三个按钮改为同宽长方形，竖排一列；恢复后两项入口，复用现有命令。生成下一步的四种箭头模式菜单仍保留。
3. 箭头选择在纯图标模式显示当前箭头，颜色显示当前色块，线宽有图标入口。
构建 work/progress-build-25.log；当前修复检查 work/test-progress-25-current，包含图形、文字、表格、模板、机理页在 640/900/1100/1500 逻辑像素的布局检查。当前修复检查已退出 0。
桌面快捷方式已更新到 r3。r2 的 TLC 列吸附、Rf 边界和连续拖尾修复仍保留。本轮未改写原 asdf.cdxml 或安装版。
未完成项仍为投影 Atom/Bond 编辑、箭头人工比对、ChemDraw 拖尾视觉复核和完整多语言/DPI 人工验收。

—— 以下为历史记录 ——

'''
for p in [desktop/'MoleculeStudio交接文档-20261116.txt',repo/'docs/MOLECULESTUDIO_HANDOFF_20261116.txt']:
 s=p.read_text(encoding='utf-8-sig');shutil.copy2(p,repo/'work/ribbon-r3-backup'/p.name);p.write_text(summary+s,encoding='utf-8')
out=Path(r'C:\Users\ChaobaDa\Documents\Codex\2026-11-16\c-users-chaobada-desktop-moleculestudio-20261116\outputs');out.mkdir(exist_ok=True)
preview=Image.new('RGB',(900,408),'#dae7e2')
for i,tab in enumerate([1,6]):
 im=Image.open(repo/f'work/test-progress-25-current/ribbon-tab-{tab}-900.png');preview.paste(im.crop((0,0,900,200)),(0,i*208))
preview.save(out/'MoleculeStudio-r3-工具栏预览.png')
