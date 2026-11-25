#!/bin/sh
set -eu
source_dir=$(realpath "$1")
version=0.2.0~dev13
out=${2:-"$PWD/moleculestudio-nmr_${version}_all.deb"}
package=$(mktemp -d)
trap 'rm -rf "$package"' EXIT
runtime="$package/usr/share/moleculestudio-nmr"
docs="$package/usr/share/doc/moleculestudio-nmr"
mkdir -p "$package/DEBIAN" "$runtime" "$docs"

python3 - "$source_dir" "$runtime" <<'PY'
import hashlib
import json
from pathlib import Path
import shutil
import sys

source, target = map(Path, sys.argv[1:3])
manifest = json.loads((source / 'CSP5-manifest.json').read_text(encoding='utf-8'))
if manifest.get('version') != '0.3.1':
    raise SystemExit('MoleculeStudio requires the CSP5 0.3.1 CPU predictor')
checkpoints = {
    Path(entry['path'].replace('\\', '/')).parts[-2]: entry['sha256']
    for entry in manifest['models']
}
wanted = {'CSP5q-1H', 'CSP5q-13C', 'CSP5q-19F'}
for model in sorted(wanted):
    relative = Path('csp5/models') / model / 'best_model.pt'
    data = (source / relative).read_bytes()
    if hashlib.sha256(data).hexdigest() != checkpoints.get(model):
        raise SystemExit(f'Checkpoint hash mismatch: {model}')
    destination = target / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
preprocessor = Path('csp5/_runtime/Predict_SMILES_FF/preprocessor_orig.p')
(target / preprocessor).parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(source / preprocessor, target / preprocessor)
for path in (source / 'csp5').rglob('*'):
    if not path.is_file() or path.suffix not in {'.py', '.json', '.txt', '.yaml', '.yml', '.md', '.csv', '.tsv'}:
        continue
    relative = path.relative_to(source)
    if 'models' in relative.parts and not wanted.intersection(relative.parts):
        continue
    destination = target / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(path, destination)
if not (target / 'csp5/__init__.py').is_file():
    raise SystemExit('Missing CSP5 Python package')
PY
install -m 644 "$source_dir/CSP5-LICENSE.txt" "$docs/copyright"
install -m 644 "$source_dir/CSP5-NOTICES.txt" "$source_dir/CSP5-manifest.json" "$docs/"
cat > "$package/DEBIAN/control" <<CONTROL
Package: moleculestudio-nmr
Architecture: all
Version: $version
Depends: moleculestudio-native (>= 0.2.0~dev13), python3-torch, python3-pandas, python3-sklearn, python3-pyarrow, python3-tqdm
Section: science
Priority: optional
Maintainer: MoleculeStudio
Description: Offline CSP5q nuclear magnetic resonance prediction for MoleculeStudio
 Includes local CPU inference and the 1H, 13C and 19F prediction checkpoints.
CONTROL
dpkg-deb --root-owner-group --build "$package" "$out"
