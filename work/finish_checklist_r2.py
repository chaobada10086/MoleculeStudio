from pathlib import Path
for p in [Path(r'C:\Users\ChaobaDa\Desktop\MoleculeStudio交接文档-20261116.txt'),Path('docs/MOLECULESTUDIO_HANDOFF_20261116.txt')]:
 s=p.read_text(encoding='utf-8');s=s.replace('Word 核对清单已同步并通过 Word 导出 PDF、逐页查看五页排版；原文件备份在 work/checklist-backup。','更新清单另存桌面 MoleculeStudio改动核对清单-20261116-r2.docx；原清单因正在打开未覆盖。新版已通过 Word 导出 PDF、逐页查看五页排版；原文件备份在 work/checklist-backup。',1);p.write_text(s,encoding='utf-8')
