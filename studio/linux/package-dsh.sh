#!/bin/sh
set -eu

src=$(dirname "$(realpath "$0")")
root=$(realpath "$src/../..")
version=0.2.0~dev57
arch=$(dpkg --print-architecture)
output=${1:-"$PWD/moleculestudio-dsh_${version}_${arch}.deb"}
stage=$(mktemp -d "${TMPDIR:-/var/tmp}/moleculestudio-dsh-package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
export TMPDIR="$stage"
pkg="$stage/pkg"
kit="$pkg/usr/share/moleculestudio-native/ai"
docs="$pkg/usr/share/doc/moleculestudio-dsh"
install -d "$kit" "$docs" "$pkg/DEBIAN"
install -m644 "$src/dsh/package.json" "$src/dsh/package-lock.json" "$kit/"
npm ci --prefix "$kit" --omit=dev --ignore-scripts --no-audit --no-fund
node "$kit/node_modules/@deepseek-ai/dsh/lib/bin.js" --version
install -m644 "$root/studio/dsh_overlay.yml" "$kit/overlay.yml"
install -m644 "$root/THIRD_PARTY_NOTICES.md" "$docs/"
cp -R "$root/licenses/npm" "$docs/licenses"

cat > "$pkg/DEBIAN/control" <<CONTROL
Package: moleculestudio-dsh
Version: $version
Architecture: $arch
Depends: moleculestudio-native (>= 0.2.0~dev57), nodejs (>= 22), bash, bubblewrap, ca-certificates
Section: science
Priority: optional
Maintainer: MoleculeStudio
Description: Optional DeepSeek Harness for MoleculeStudio advanced drawing
 Runs the headless planning and verification workflow in a workspace scoped
 to the current request. The GTK editor validates and applies drawing actions.
CONTROL
dpkg-deb --root-owner-group -Zzstd -z8 --build "$pkg" "$output"
