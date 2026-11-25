"""Preserve original OLE dimensions in presentations exported by Impress."""
import posixpath
import re
import struct
import subprocess
import xml.etree.ElementTree as ET
from xml.parsers import expat


def _embedded_extent(package, member, codec):
    if not member.startswith("ppt/embeddings/") or package.getinfo(member).file_size > 128 * 1024 * 1024:
        return None
    data = package.read(member)
    if len(data) < 512 or data[:8] != bytes.fromhex("d0cf11e0a1b11ae1"):
        return None
    shift = struct.unpack_from("<H", data, 30)[0]
    if shift not in (9, 12):
        return None
    directory = (1 + struct.unpack_from("<I", data, 48)[0]) << shift
    if data[directory + 80:directory + 96] != codec.CLASS.bytes_le:
        return None
    root = codec.Gsf.InfileMSOle.new(codec.Gsf.InputMemory.new_clone(data))
    stream = root.child_by_name("MSXT") if root else None
    if not stream or stream.size != 8:
        return None
    width, height = struct.unpack("<ii", bytes(stream.read(8)))
    return (width * 360, height * 360) if min(width, height) > 0 else None


def _slide_extents(package, name, extents, codec):
    xml = package.read(name)
    if b"MoleculeStudio.Document.1" not in xml:
        return None
    ns = "http://schemas.openxmlformats.org/presentationml/2006/main"
    rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
    parent, leaf = posixpath.split(name)
    relationships = ET.fromstring(package.read(posixpath.join(parent, "_rels", leaf + ".rels")))
    targets = {entry.get("Id"): entry.get("Target") for entry in relationships
               if entry.get("Type") == rel + "/oleObject" and entry.get("TargetMode", "Internal") == "Internal"}
    parser = expat.ParserCreate(namespace_separator=" ")
    edits = []

    def object_tag(tag, attrs):
        if tag != ns + " oleObj" or attrs.get("progId") != "MoleculeStudio.Document.1":
            return
        target = targets.get(attrs.get(rel + " id"))
        if not target or attrs.get("showAsIcon") in ("1", "true"):
            return
        member = posixpath.normpath(posixpath.join(parent, target)).lstrip("/")
        if member not in extents:
            extents[member] = _embedded_extent(package, member, codec)
        extent = extents[member]
        if not extent or (attrs.get("imgW"), attrs.get("imgH")) == tuple(map(str, extent)):
            return
        index = parser.CurrentByteIndex
        match = _START_TAG.match(xml, index)
        if not match:
            raise ValueError("Invalid presentation object tag")
        # A byte-local edit retains namespace prefixes used in mc:Requires and
        # leaves the user's position and display-size transforms untouched.
        original = _IMAGE_SIZE.sub(b"", match.group())
        cut = -2 if original.endswith(b"/>") else -1
        size = f' imgW="{extent[0]}" imgH="{extent[1]}"'.encode("ascii")
        edits.append((index, match.end(), original[:cut] + size + original[cut:]))

    parser.StartElementHandler = object_tag
    parser.Parse(xml, True)
    if not edits:
        return None
    parts = []
    end = 0
    for start, stop, replacement in edits:
        parts.extend((xml[end:start], replacement))
        end = stop
    parts.append(xml[end:])
    return b"".join(parts)


_START_TAG = re.compile(rb"""<(?:[^<>"']|"[^"]*"|'[^']*')*>""")
_IMAGE_SIZE = re.compile(rb"""\s+img[WH]\s*=\s*(?:"[^"]*"|'[^']*')""")


def presentation_extents(source, folder, codec):
    """Update just the slide parts of a fresh Impress export in its private staging area."""
    from zipfile import ZipFile, is_zipfile
    if not is_zipfile(source):
        return source
    updates = []
    extents = {}
    with ZipFile(source) as package:
        members = package.namelist()
        if any(name.startswith("_xmlsignatures/") for name in members):
            return source
        for member in members:
            if re.fullmatch(r"ppt/slides/slide[0-9]+\.xml", member):
                data = _slide_extents(package, member, extents, codec)
                if data is not None:
                    file = folder / member
                    file.parent.mkdir(parents=True, exist_ok=True)
                    file.write_bytes(data)
                    updates.append(member)
    if not updates:
        return source
    # Copy untouched media as compressed ZIP entries, including large audio/video.
    output = folder / "presentation.pptx"
    subprocess.run(["/usr/bin/zip", "-q", str(source), "--out", str(output), "-@"], cwd=folder,
                   input="\n".join(updates) + "\n", text=True, capture_output=True, check=True)
    return output

