#!/bin/sh
set -eu
src=$(dirname "$(realpath "$0")")
python=$(realpath "$1")/bin/python
modeldir=$(realpath "$2")
version=0.2.0~dev253
arch=$(dpkg --print-architecture)
out=${3:-"$PWD/moleculestudio-ocsr_${version}_$arch.deb"}
out=$(realpath -m "$out")
package=$(mktemp -d "$(dirname "$out")/.moleculestudio-ocsr.XXXXXX")
trap 'rm -rf "$package"' EXIT
umask 022

base=$("$python" -c 'import sys; assert sys.version_info[:2] == (3, 10); print(sys.base_prefix)')
site=$("$python" -c 'import sysconfig; print(sysconfig.get_path("purelib"))')
runtime="$package/usr/lib/moleculestudio-ocsr/python"
assets="$package/usr/share/moleculestudio-ocsr"
docs="$package/usr/share/doc/moleculestudio-ocsr"
mkdir -p "$runtime" "$assets/models" "$docs" "$package/DEBIAN"
cp -a "$base/." "$runtime/"
cp -a "$site/." "$runtime/lib/python3.10/site-packages/"
find "$runtime" -type d -name __pycache__ -prune -exec rm -rf '{}' +

checkpoint=swin_base_char_aux_1m680k.pth
expected=dc832a7a0f303ac3c9f6e114397b01df1e94b36a714888034833e12a439303bf
printf '%s  %s\n' "$expected" "$modeldir/$checkpoint" | sha256sum --check --status
install -m 644 "$modeldir/$checkpoint" "$modeldir/MODELS.json" "$assets/models/"
install -m 644 "$src/../ocsr_service.py" "$src/../nicknames.json" "$assets/"
install -m 644 "$src/../../LICENSE" "$src/../../THIRD_PARTY_NOTICES.md" "$docs/"
"$python" -c 'from importlib.metadata import distributions; print("\n".join(sorted({d.metadata["Name"]+"=="+d.version for d in distributions()})))' > "$docs/runtime-packages.txt"
"$runtime/bin/python3.10" -c 'import sys, cv2, pypdfium2, torch; from molscribe import MolScribe; assert torch.__version__.startswith("2.5.1"); assert sys.prefix == sys.argv[1]' "$runtime"

cat > "$package/DEBIAN/control" <<CONTROL
Package: moleculestudio-ocsr
Architecture: $arch
Version: $version
Depends: moleculestudio-native (>= 0.2.0~dev14), libc6 (>= 2.34), libstdc++6, libgomp1, libglib2.0-0t64, libgl1, libx11-6, libxcb1
Section: science
Priority: optional
Maintainer: MoleculeStudio
Description: Offline image and PDF structure recognition for MoleculeStudio
 Includes the MolScribe CPU runtime and the MoleculeStudio reference checkpoint.
CONTROL
dpkg-deb --root-owner-group -Zzstd -z5 --threads-max=4 --build "$package" "$out"
