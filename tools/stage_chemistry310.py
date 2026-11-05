"""Stage an independent chemistry runtime from the published, verified OCSR build.
Core must already be extracted from the official ZIP; no live installation is used.
"""
from pathlib import Path
import argparse,zipfile,shutil
p=argparse.ArgumentParser();p.add_argument('--ocsr',type=Path,required=True);p.add_argument('--core',type=Path,required=True);a=p.parse_args()
base='MoleculeStudio/ocsr_runtime/python/cpython-3.10-windows-x86_64-none/'
site='MoleculeStudio/ocsr_runtime/venv/Lib/site-packages/'
with zipfile.ZipFile(a.ocsr) as z:
 for entry in z.infolist():
  n=entry.filename;rel=None
  if n.startswith(base):rel='chemistry_python/'+n[len(base):]
  elif n.startswith(site):
   tail=n[len(site):]
   if tail.startswith(('_virtualenv.','_virtualenv/')):continue
   rel='chemistry_python/Lib/site-packages/'+tail
  elif n.startswith('MoleculeStudio/legal/OCSR/'):rel='legal/Chemistry310/'+n[len('MoleculeStudio/legal/OCSR/'):]
  if rel is None or entry.is_dir() or '__pycache__' in rel or rel.endswith('.pyc'):continue
  target=a.core/rel
  if not target.resolve().is_relative_to(a.core.resolve()):raise ValueError(rel)
  target.parent.mkdir(parents=True,exist_ok=True)
  with z.open(entry) as source,target.open('wb') as dest:shutil.copyfileobj(source,dest)
(a.core/'chemistry_backend.ini').write_text('[Backend]\nPython=python\\python.exe\nChemistryPython=chemistry_python\\python.exe\n',encoding='utf-16')
print('Staged independent Python 3.10 runtime')
