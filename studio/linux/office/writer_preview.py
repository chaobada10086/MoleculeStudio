"""Use vector replacements for MoleculeStudio OLE frames in a Writer print copy."""

import shutil
import struct
from xml.dom import minidom
from zipfile import ZipFile


DRAW = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
TEXT = "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
XLINK = "http://www.w3.org/1999/xlink"


def member(node):
    return node.getAttributeNS(XLINK, "href").removeprefix("./")


def root_class_offset(header, length):
    if len(header) != 512 or not header.startswith(bytes.fromhex("d0cf11e0a1b11ae1")):
        return None
    shift = struct.unpack_from("<H", header, 30)[0]
    if shift not in (9, 12):
        return None
    offset = ((1 + struct.unpack_from("<I", header, 48)[0]) << shift) + 80
    return offset if offset + 16 <= length else None


def molecule_object(archive, name, class_id):
    if name not in archive.NameToInfo:
        return False
    with archive.open(name) as stream:
        offset = root_class_offset(stream.read(512), archive.getinfo(name).file_size)
        if offset is None:
            return False
        stream.seek(offset)
        return stream.read(16) == class_id


def print_copy(source, destination, class_id):
    """Return the number of frames changed, preserving other embedded objects."""
    with ZipFile(source) as incoming:
        parts = {}
        count = 0
        for part in ("content.xml", "styles.xml"):
            if part not in incoming.namelist():
                continue
            with minidom.parseString(incoming.read(part)) as xml:
                # The displayed filename belongs to the live document. Its
                # expansion is already present in the Writer-generated snapshot.
                for field in xml.getElementsByTagNameNS(TEXT, "file-name"):
                    field.setAttributeNS(TEXT, "text:fixed", "true")
                    parts[part] = None
                for obj in list(xml.getElementsByTagNameNS(DRAW, "object-ole")):
                    frame = obj.parentNode
                    if (frame.namespaceURI, frame.localName) != (DRAW, "frame"):
                        continue
                    pictures = [n for n in frame.childNodes if n.namespaceURI == DRAW and n.localName == "image"]
                    if not any(member(pic) in incoming.NameToInfo for pic in pictures):
                        continue
                    if molecule_object(incoming, member(obj), class_id):
                        frame.removeChild(obj).unlink()
                        count += 1
                        parts[part] = None
                if part in parts:
                    parts[part] = xml.toxml(encoding="UTF-8")
        if count:
            with ZipFile(destination, "w") as outgoing:
                for info in incoming.infolist():
                    if info.filename in parts:
                        outgoing.writestr(info, parts[info.filename])
                    else:
                        with incoming.open(info) as src, outgoing.open(info, "w") as dst:
                            shutil.copyfileobj(src, dst)
        return count
