#!/bin/sh
set -eu
src=$(dirname "$(realpath "$0")")
out=${1:-"$PWD/moleculestudio-preview_0.1.0_all.deb"}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/DEBIAN" "$stage/usr/bin" "$stage/usr/share/applications"
install -m 755 "$src/editor.py" "$stage/usr/bin/moleculestudio-preview"
cat > "$stage/DEBIAN/control" <<'CONTROL'
Package: moleculestudio-preview
Version: 0.1.0
Architecture: all
Maintainer: MoleculeStudio
Depends: python3 (>= 3.9), python3-gi, python3-gi-cairo, gir1.2-gtk-3.0
Description: MoleculeStudio Linux CDXML editor preview
CONTROL
cat > "$stage/usr/share/applications/moleculestudio-preview.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=MoleculeStudio (Linux Preview)
Name[zh_CN]=MoleculeStudio（Linux 预览版）
Exec=moleculestudio-preview %f
Terminal=false
Categories=Education;Science;Chemistry;
DESKTOP
dpkg-deb --root-owner-group --build "$stage" "$out"
