#!/bin/sh
set -eu
source=$(dirname "$(realpath "$0")")/office
version=0.2.0~dev251
out=${1:-"$PWD/moleculestudio-office_${version}_all.deb"}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
extension="$stage/usr/lib/libreoffice/share/extensions/moleculestudio"
mkdir -p "$stage/DEBIAN" "$extension/META-INF"
install -m644 "$source/../office_ole.py" "$source/../office_emf.py" "$source/description.xml" "$source/moleculestudio.py" "$source/Addons.xcu" \
    "$source/presentation_extents.py" "$source/writer_preview.py" "$source/Filters.xcu" "$source/Jobs.xcu" "$source/ProtocolHandler.xcu" "$extension/"
install -m644 "$source/META-INF/manifest.xml" "$extension/META-INF/"
cat >"$stage/DEBIAN/control" <<CONTROL
Package: moleculestudio-office
Version: $version
Architecture: all
Depends: moleculestudio-native (>= 0.2.0~dev232), gir1.2-gdkpixbuf-2.0, libreoffice-core, python3-uno, zip
Recommends: libreoffice-writer, libreoffice-impress, libreoffice-gtk3
Section: science
Priority: optional
Maintainer: MoleculeStudio
Description: MoleculeStudio drawing objects for LibreOffice Writer and Impress
 Insert drawings, edit their chemical structures in MoleculeStudio, and apply
 the resulting vector preview and editable data to the Office document.
CONTROL
dpkg-deb --root-owner-group --build "$stage" "$out"
