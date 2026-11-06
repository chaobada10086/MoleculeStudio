import hashlib
import json
import subprocess
import tempfile
import zipfile
from pathlib import Path

exe=Path(__file__).resolve().parents[1]/'dist/MoleculeStudio-OfflineInstall.exe'
with tempfile.TemporaryDirectory(prefix='ms-offline-test-', dir='\\\\?\\'+tempfile.gettempdir()) as temp:
    base=Path(temp)
    rel='ai/node_modules/'+('long-directory/'*15)+'LICENSE.APACHE'
    data=b'license fixture'
    manifest={'files':{rel:{'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}}}
    archive=base/'fixture.zip'
    with zipfile.ZipFile(archive,'w') as z:
        z.writestr('MoleculeStudio/'+rel,data)
        z.writestr('MoleculeStudio/MANIFEST-AI.json',json.dumps(manifest))
    target=base/('installation-path-'*5)
    command=[str(exe),'--extract-test',str(archive),str(target)]
    assert len(str(target/rel))>260
    assert subprocess.run(command).returncode==0
    assert (target/rel).read_bytes()==data
    countfile=base/'progress.txt'
    assert subprocess.run([str(exe),'--progress-test',str(archive),str(target),str(countfile)]).returncode==0
    assert countfile.read_text()=='2'  # existing payload and manifest both count
    assert subprocess.run(command).returncode==0
    (target/rel).write_bytes(b'user modification')
    assert subprocess.run(command).returncode==1
    assert (target/rel).read_bytes()==b'user modification'
    with zipfile.ZipFile(archive,'w') as z:
        z.writestr('MoleculeStudio/../outside.txt',data)
        z.writestr('MoleculeStudio/MANIFEST-AI.json',json.dumps({'files':{}}))
    assert subprocess.run(command).returncode==1
    assert not (base/'outside.txt').exists()
print('PASS: >260-character paths, repeat extraction, modified-file protection, traversal rejection')
