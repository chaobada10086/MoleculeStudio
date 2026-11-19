from pathlib import Path
p=Path('work/build-r7-dev.py');s=p.read_text().replace("out/'MoleculeStudio-r7-dev.exe'","out/'MoleculeStudio-r7-native-dev.exe'");p.write_text(s)
