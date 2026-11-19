from pathlib import Path
from PIL import Image
import shutil
repo=Path.cwd();desktop=Path(r'C:\Users\ChaobaDa\Desktop')
summary='''MoleculeStudio 接续更新 2026-11-16 r4
本段优先于历史记录。
当前测试版：C:\\Users\\ChaobaDa\\Documents\\GitHub\\MoleculeStudio\\out\\Studio\\MoleculeStudio-Progress-20261116-r4.exe
用户本轮要求：
1. 工具按钮缩小：纯图标从 24px 降为 18px，按钮为 24×22 逻辑像素，三行间距缩小；生成按钮窄化、每个 22px 高，仍竖排一列。
2. 悬停提示基于系统字体加 2 磅，按 DPI 更新字体并放宽换行区域；字体资源随窗口释放。
3. 270° 残留来自旧的 Arrow/Preset 配置和旧测试选择。固定角度普通弧线预设在启动与选择时迁移到统一的自由弧线 58；预览不再显示固定角度名称。机理箭头 49 和鱼钩 57 不迁移，原文件几何不改。
4. 测试不再通过 chooseArrowPreset 改写用户当前箭头设置。布局回归自行展开工具栏，避免受用户收起状态影响。
构建 work/progress-build-29.log；检查 --current-fixes-check work/test-progress-29-current 和 --arrow-shortcut-check work/test-progress-29-arrows，均退出 0。
桌面快捷方式已指向 r4。安装版与 asdf.cdxml 未覆盖。
以前报告的完整 chrome-check 描边用例失败尚未在本轮处理；本轮只对上述改动及相关机理、箭头功能报告回归结果。

—— 以下为历史记录 ——

'''
backup=repo/'work/ribbon-r4-backup'
for p in [desktop/'MoleculeStudio交接文档-20261116.txt',repo/'docs/MOLECULESTUDIO_HANDOFF_20261116.txt']:
 shutil.copy2(p,backup/p.name);p.write_text(summary+p.read_text(encoding='utf-8-sig'),encoding='utf-8')
out=Path(r'C:\Users\ChaobaDa\Documents\Codex\2026-11-16\c-users-chaobada-desktop-moleculestudio-20261116\outputs')
preview=Image.new('RGB',(900,368),'#dae7e2')
for i,tab in enumerate([1,6]):
 im=Image.open(repo/f'work/test-progress-29-current/ribbon-tab-{tab}-900.png');preview.paste(im.crop((0,0,900,180)),(0,i*188))
preview.save(out/'MoleculeStudio-r4-工具栏预览.png')
