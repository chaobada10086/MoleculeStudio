"""Rebuild the pinned offline chemistry runtime without changing system Java/PATH.
Run with the Python configured for MoleculeStudio (RDKit-capable Python 3.10+).
"""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
import zipfile

studio = Path(__file__).resolve().parent
output = studio.parent/'out/Studio'
runtime = output/'chemistry_runtime'
runtime.mkdir(parents=True, exist_ok=True)
manifest = json.loads((studio/'open_chemistry_dependencies.json').read_text(encoding='utf-8'))


def fetch(item):
    target = runtime/item['file']
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != item['sha256']:
        request = urllib.request.Request(item['url'], headers={'User-Agent': 'MoleculeStudio-setup'})
        with urllib.request.urlopen(request, timeout=120) as response:
            payload = response.read()
        if hashlib.sha256(payload).hexdigest() != item['sha256']:
            raise RuntimeError('Dependency checksum mismatch: '+item['file'])
        target.write_bytes(payload)
    print('Verified '+item['file'], flush=True)


with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(fetch, manifest['downloads']))
if not (runtime/'java/bin/java.exe').exists():
    stage = runtime/'java-unpacked'
    zipfile.ZipFile(runtime/'java.zip').extractall(stage)
    next(stage.iterdir()).rename(runtime/'java')
site = runtime/'python-site'
subprocess.run([sys.executable, '-m', 'pip', 'install', '--target', str(site), '--upgrade',
                '--no-deps', 'nispo==0.1.7'], check=True)
try:
    import rdkit
except ImportError:
    subprocess.run([sys.executable, '-m', 'pip', 'install', '--target', str(site),
                    'rdkit==2026.3.6'], check=True)
shutil.copy2(studio/'chemistry_open.py', output/'chemistry_open.py')
shutil.copy2(studio/'OPEN_CHEMISTRY_NOTICES.md', runtime/'THIRD_PARTY_NOTICES.md')
(output/'chemistry_backend.ini').write_text('[Backend]\nPython='+sys.executable+'\n', encoding='utf-16')
(runtime/'download-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('Offline chemistry runtime ready: '+str(runtime))
