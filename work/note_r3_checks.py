from pathlib import Path
note='额外验证说明：--chrome-check 在 r3 和未改动的 r2 上均在 graphicStyleCheck 的“fixture has visible stroke”用例失败，报告位于 work/test-progress-25-chrome 和 work/test-progress-25-chrome-baseline；因此不将整套 chrome-check 写成通过。此前的 DPI/页签布局过程未抛出错误。\n'
for p in [Path(r'C:\Users\ChaobaDa\Desktop\MoleculeStudio交接文档-20261116.txt'),Path('docs/MOLECULESTUDIO_HANDOFF_20261116.txt')]:
 s=p.read_text(encoding='utf-8');s=s.replace('桌面快捷方式已更新到 r3。',note+'桌面快捷方式已更新到 r3。',1);p.write_text(s,encoding='utf-8')
