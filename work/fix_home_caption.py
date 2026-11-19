from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8');s=s.replace('y+4+float(i%rows)*26,24,24','y+3+float(i%rows)*26,24,24');p.write_text(s,encoding='utf-8')
