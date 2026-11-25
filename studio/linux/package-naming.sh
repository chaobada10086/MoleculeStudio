#!/bin/sh
set -eu
source_dir=$(realpath "$1")
version=0.2.0~dev17
out=${2:-"$PWD/moleculestudio-naming_${version}_all.deb"}
package=$(mktemp -d)
trap 'rm -rf "$package"' EXIT
site="$package/usr/share/moleculestudio-naming"
docs="$package/usr/share/doc/moleculestudio-naming"
mkdir -p "$package/DEBIAN" "$site" "$docs"

python3 - "$source_dir" "$site" "$docs" <<'PY'
from importlib.metadata import distributions
from pathlib import Path
import shutil
import sys

source, target, docs = map(Path, sys.argv[1:4])
metadata = next((dist for dist in distributions(path=[str(source)])
                 if (dist.metadata['Name'] or '').lower() == 'nispo'), None)
if metadata is None or metadata.version != '0.1.7' or not (source / 'nispo/__init__.py').is_file():
    raise SystemExit('MoleculeStudio requires the NISPO 0.1.7 Python package')
for path in (source / 'nispo').rglob('*'):
    if path.is_file() and path.suffix in {'.py', '.json', '.csv', '.gz', '.xml'}:
        destination = target / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
for license_file, output in [('LICENSE', 'copyright'), ('THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')]:
    licenses = source / 'nispo-0.1.7.dist-info/licenses'
    shutil.copyfile(licenses / license_file, docs / output)
PY

cat > "$package/DEBIAN/control" <<CONTROL
Package: moleculestudio-naming
Architecture: all
Version: $version
Depends: moleculestudio-native (>= 0.2.0~dev17), python3-rdkit, default-jre-headless, libopsin-java
Section: science
Priority: optional
Maintainer: MoleculeStudio
Description: Offline systematic chemical naming for MoleculeStudio
 Includes NISPO and validates generated names with the installed OPSIN runtime.
CONTROL
dpkg-deb --root-owner-group --build "$package" "$out"
