#!/bin/sh
set -eu
src=$(dirname "$(realpath "$0")")
root=$(realpath "$src/../..")
python3 "$src/build_engine.py"
arch=$(dpkg --print-architecture)
version=0.2.0~dev256
out=${1:-"$PWD/moleculestudio-native_${version}_${arch}.deb"}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
pkg="$stage/pkg"
mkdir -p "$stage/debian" "$pkg/DEBIAN" "$pkg/usr/bin" "$pkg/usr/share/applications" \
    "$pkg/usr/share/doc/moleculestudio-native" "$pkg/usr/share/pixmaps" "$pkg/usr/share/mime/packages"
mkdir -p "$pkg/usr/share/moleculestudio-native"
install -m 644 "$src/convert_structure.py" "$src/update.py" "$src/office_ole.py" "$src/office_emf.py" "$root/studio/chemistry_3d.py" "$root/studio/chemistry_open.py" \
    "$root/studio/branched_alkyl_layout.py" "$root/studio/projection_stereo.py" "$root/studio/nicknames.json" \
    "$pkg/usr/share/moleculestudio-native/"
install -m 644 "$src/run_ai.py" "$root/studio/ai_assistant.py" "$root/studio/ai_scene.py" \
    "$root/studio/ai_attachments.py" "$root/studio/ai_library.py" "$root/studio/ai_local_sources.py" "$root/studio/ai_dsh.py" \
    "$root/studio/dsh_proxy.py" \
    "$root/studio/ai_ocsr_bridge.py" "$root/studio/ai_ring_catalog.json" "$root/studio/AI_TOOL_REFERENCE.json" \
    "$pkg/usr/share/moleculestudio-native/"
mkdir -p "$pkg/usr/share/moleculestudio-native/assets"
cp -R "$root/studio/assets/office" "$root/studio/assets/ribbon" "$pkg/usr/share/moleculestudio-native/assets/"
cp -R "$root/studio/reagent_templates" "$root/studio/bio_templates" "$pkg/usr/share/moleculestudio-native/"
mkdir -p "$pkg/usr/share/moleculestudio-native/skills"
cp -R "$root/skills/moleculestudio-draw" "$pkg/usr/share/moleculestudio-native/skills/"
install -m 755 "$root/out/linux-native/moleculestudio" "$pkg/usr/bin/moleculestudio-native"
install -m 644 "$root/studio/assets/app-icons/moleculestudio-professional.png" "$pkg/usr/share/pixmaps/org.moleculestudio.Native.png"
install -m 644 "$src/moleculestudio-mime.xml" "$pkg/usr/share/mime/packages/org.moleculestudio.Native.xml"
install -m 644 "$root/LICENSE" "$root/NOTICE" "$root/THIRD_PARTY_NOTICES.md" "$pkg/usr/share/doc/moleculestudio-native/"
cat > "$stage/debian/control" <<'CONTROL'
Source: moleculestudio-native
Section: science
Priority: optional
Maintainer: MoleculeStudio

Package: moleculestudio-native
Architecture: any
Description: MoleculeStudio native molecular drawing editor (development build)
CONTROL
deps=$(cd "$stage" && dpkg-shlibdeps -O -e"$pkg/usr/bin/moleculestudio-native")
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: moleculestudio-native
Architecture: $arch
Version: $version
Depends: fonts-freefont-ttf, fonts-liberation, fonts-urw-base35, gir1.2-gdkpixbuf-2.0, gir1.2-gsf-1, python3-gi, python3-pil, python3-rdkit, shared-mime-info, ${deps#shlibs:Depends=}
Recommends: fonts-noto-cjk, fonts-noto-core, emf2svg, libwmf-bin
Suggests: default-jre-headless, libopsin-java, moleculestudio-naming, moleculestudio-nmr, moleculestudio-ocsr, moleculestudio-office, moleculestudio-dsh
Section: science
Priority: optional
Maintainer: MoleculeStudio
Description: Native molecular drawing editor (development build)
CONTROL
cat > "$pkg/usr/share/applications/org.moleculestudio.Native.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=MoleculeStudio Native
Name[zh_CN]=MoleculeStudio 原生开发版
Exec=moleculestudio-native %F
Icon=org.moleculestudio.Native
Terminal=false
StartupNotify=true
Categories=Science;Chemistry;
MimeType=application/x-moleculestudio-document;chemical/x-cdxml;chemical/x-cdx;application/vnd.chemdraw+xml;
DESKTOP
dpkg-deb --root-owner-group --build "$pkg" "$out"
