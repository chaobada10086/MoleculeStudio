from pathlib import Path
p=Path('work/build-r7-dev.py');s=p.read_text().replace("if not obj.exists() or Path(entry['path']).stat().st_mtime>obj.stat().st_mtime:","if Path(entry['path']).name!='session.cpp' or not obj.exists() or Path(entry['path']).stat().st_mtime>obj.stat().st_mtime:");p.write_text(s)
