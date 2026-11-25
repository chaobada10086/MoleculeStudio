"""LibreOffice commands for embedding and editing MoleculeStudio drawings."""

import base64
import hashlib
import json
import logging
import os
import threading
import importlib.util
import shutil
from functools import lru_cache
from contextlib import contextmanager, suppress
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory, mkdtemp
from uuid import uuid4
import zlib

import uno
import unohelper
from com.sun.star.awt import Point, Size, XCallback, XMouseClickHandler
from com.sun.star.beans import PropertyValue
from com.sun.star.frame import XDispatch, XDispatchProvider, XDispatchProviderInterceptor, XInterceptorInfo
from com.sun.star.document import XDocumentEventListener, XExporter, XFilter, XImporter, XUndoAction
from com.sun.star.document.MacroExecMode import NEVER_EXECUTE
from com.sun.star.document.UpdateDocMode import NO_UPDATE
from com.sun.star.lang import XInitialization
from com.sun.star.task import XJob
from com.sun.star.xml import AttributeData


IMPLEMENTATION = "org.moleculestudio.office.ProtocolHandler"
PROTOCOL = "moleculestudio.office:"
MIME = "application/x-moleculestudio-document-selection"
EXECUTABLE = "/usr/bin/moleculestudio-native"
ATTRIBUTE = "molecule:source"
NAMESPACE = "urn:moleculestudio:office:1"
MAX_DOCUMENT = 32 * 1024 * 1024
MAX_EMBEDDED = 3 * 1024 * 1024
PROPERTY_PREFIX = "MoleculeStudioData_"
DRAWING_MARKER = re.compile(r"\s*\[(MoleculeStudioData_[0-9a-f]{32})\]$")


def presentation_extent(size):
    scale = min(1.0, 15000 / size.Width, 10000 / size.Height)
    return (max(1, round(size.Width * scale)), max(1, round(size.Height * scale)))


def property_value(name, value):
    prop = PropertyValue()
    prop.Name = name
    prop.Value = value
    return prop


def validate(source):
    if len(source) > MAX_DOCUMENT or not source.startswith((b"R62EXT 1 T01DOC ", b"T01DOC ")):
        raise ValueError("Select a valid MoleculeStudio .r62doc document.")
    return source


def encode(source):
    compressed = zlib.compress(validate(source), level=6)
    if len(compressed) > MAX_EMBEDDED:
        raise ValueError("This drawing is too large to embed in the Office document.")
    return base64.b64encode(compressed).decode("ascii")


def decode(value):
    if len(value) > 4 * ((MAX_EMBEDDED + 2) // 3):
        raise ValueError("The embedded drawing exceeds the size limit.")
    payload = base64.b64decode(value, validate=True)
    reader = zlib.decompressobj()
    source = reader.decompress(payload, MAX_DOCUMENT + 1)
    if not reader.eof or reader.unused_data:
        raise ValueError("The embedded drawing data is incomplete or invalid.")
    return validate(source)


def file_data(path):
    with Path(path).open("rb") as stream:
        return validate(stream.read(MAX_DOCUMENT + 1))


def local_source(shape):
    try:
        attributes = shape.UserDefinedAttributes
        if attributes.hasByName(ATTRIBUTE):
            value = attributes.getByName(ATTRIBUTE)
            if value.Namespace == NAMESPACE:
                return value.Value
    except AttributeError:
        pass
    return None


def set_local_source(shape, value):
    attributes = shape.UserDefinedAttributes
    present = attributes.hasByName(ATTRIBUTE)
    if value is None:
        if present:
            attributes.removeByName(ATTRIBUTE)
        else:
            return
    elif present:
        data = AttributeData(NAMESPACE, "CDATA", value)
        attributes.replaceByName(ATTRIBUTE, data)
    else:
        attributes.insertByName(ATTRIBUTE, AttributeData(NAMESPACE, "CDATA", value))
    shape.UserDefinedAttributes = attributes


def drawing_key(shape):
    for field in ("Name", "Description"):
        match = DRAWING_MARKER.search(getattr(shape, field, "") or "")
        if match:
            return match[1]
    return None


def custom_source(model, key):
    properties = model.getDocumentProperties().getUserDefinedProperties()
    if properties.getPropertySetInfo().hasPropertyByName(key):
        value = properties.getPropertyValue(key)
        if isinstance(value, str):
            return value
    return None


@lru_cache(maxsize=2)
def office_module(name):
    path = Path(__file__).with_name(name + ".py")
    spec = importlib.util.spec_from_file_location("moleculestudio_" + name, path)
    codec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(codec)
    return codec


def storage_codec():
    return office_module("office_ole")


def ole_entry(model, shape):
    if not hasattr(shape, "supportsService"):
        return None
    if shape.supportsService("com.sun.star.text.TextEmbeddedObject"):
        key = shape.StreamName
    elif shape.supportsService("com.sun.star.drawing.OLE2Shape"):
        key = shape.PersistName
    else:
        return None
    storage = model.getDocumentStorage()
    if not key or not storage.hasByName(key) or not storage.isStreamElement(key):
        return None
    return storage, key


def is_molecule_stream(stream):
    _, header = stream.readBytes(None, 512)
    offset = office_module("writer_preview").root_class_offset(bytes(header), stream.getLength())
    if offset is None:
        return False
    stream.seek(offset)
    _, data = stream.readBytes(None, 16)
    return bytes(data) == storage_codec().CLASS.bytes_le


def ole_contents(storage, key):
    # The host owns the open stream. A clone can be read while editing continues.
    stream = storage.cloneStreamElement(key).getInputStream()
    try:
        length = stream.getLength()
        if not is_molecule_stream(stream):
            return None
        if length > 128 * 1024 * 1024:
            raise ValueError("The embedded Office object is too large to edit.")
        stream.seek(0)
        _, data = stream.readBytes(None, length)
        payload = bytes(data)
    finally:
        stream.closeInput()
    codec = storage_codec()
    root = codec.Gsf.InfileMSOle.new(codec.Gsf.InputMemory.new_clone(payload))
    content = root.child_by_name("CONTENTS")
    if not content or content.size > MAX_DOCUMENT:
        raise ValueError("Invalid embedded MoleculeStudio source.")
    native = bytes(content.read(content.size))
    original = root.child_by_name("MSSource")
    if original and 32 < original.size <= MAX_DOCUMENT + 32:
        if bytes(original.read(32)) == hashlib.sha256(native).digest():
            return validate(bytes(original.read(original.size - 32)))
    return validate(native)

def source(model, shape):
    recorded = local_source(shape)
    if recorded:
        return decode(recorded)
    key = drawing_key(shape)
    if key:
        stored = custom_source(model, key)
        return decode(stored) if stored else None
    entry = ole_entry(model, shape)
    return ole_contents(*entry) if entry else None


def set_custom_source(model, key, value):
    properties = model.getDocumentProperties().getUserDefinedProperties()
    present = properties.getPropertySetInfo().hasPropertyByName(key)
    if value is None:
        if present:
            properties.removeProperty(key)
    elif present:
        properties.setPropertyValue(key, value)
    else:
        removable = uno.getConstantByName("com.sun.star.beans.PropertyAttribute.REMOVABLE")
        properties.addProperty(key, removable, value)


def document_shapes(model):
    def members(collection):
        for index in range(collection.getCount()):
            shape = collection.getByIndex(index)
            yield shape
            if shape.supportsService("com.sun.star.drawing.GroupShape"):
                yield from members(shape)

    if model.supportsService("com.sun.star.text.TextDocument"):
        yield from members(model.getDrawPage())
    else:
        pages = model.getDrawPages()
        for index in range(pages.getCount()):
            yield from members(pages.getByIndex(index))


def stamped(text, key, fallback):
    prefix = DRAWING_MARKER.sub("", text).strip() or fallback
    return f"{prefix} [{key}]"


class DrawingSource:
    def __init__(self, model, shape, value):
        self.model = model
        self.shape = shape
        key = drawing_key(shape)
        if not key or any(other != shape and drawing_key(other) == key for other in document_shapes(model)):
            key = PROPERTY_PREFIX + uuid4().hex
        self.key = key
        self.before = (local_source(shape), custom_source(model, key), shape.Name, shape.Description)
        # OOXML discards UserDefinedAttributes. Keep a second copy in the
        # document and retain its identifier in the picture metadata.
        self.after = (value, value, stamped(shape.Name, key, "MoleculeStudio drawing"),
                      stamped(shape.Description, key, "Editable MoleculeStudio drawing"))

    def set_state(self, state):
        local, stored, name, description = state
        set_local_source(self.shape, local)
        set_custom_source(self.model, self.key, stored)
        if self.shape.Name != name:
            self.shape.Name = name
        if self.shape.Description != description:
            self.shape.Description = description

    def commit(self):
        try:
            self.set_state(self.after)
        except Exception:
            with suppress(Exception):
                self.set_state(self.before)
            raise


def is_drawing(model, shape):
    if local_source(shape):
        return True
    key = drawing_key(shape)
    if key:
        return bool(custom_source(model, key))
    entry = ole_entry(model, shape)
    if not entry:
        return False
    storage, key = entry
    stream = storage.cloneStreamElement(key).getInputStream()
    try:
        return is_molecule_stream(stream)
    finally:
        stream.closeInput()


def selected_shape(controller):
    selection = controller.getSelection()
    model = controller.getModel()
    if is_drawing(model, selection):
        return selection
    if hasattr(selection, "getCount") and selection.getCount() == 1:
        shape = selection.getByIndex(0)
        if is_drawing(model, shape):
            return shape
    raise ValueError("Select one image inserted by MoleculeStudio.")


@contextmanager
def undo_context(model, label):
    undo = model.getUndoManager() if hasattr(model, "getUndoManager") else None
    if undo:
        undo.enterUndoContext(label)
    try:
        yield
    finally:
        if undo:
            undo.leaveUndoContext()


def recovery_root():
    state = os.environ.get("XDG_STATE_HOME") or Path.home() / ".local/state"
    return Path(state) / "moleculestudio" / "office-recovery"


def recovery_target(model, shape):
    embedded = ole_entry(model, shape)
    return {"document": model.getURL(), "object": embedded[1] if embedded else drawing_key(shape)}


class Edit(unohelper.Base, XDocumentEventListener):
    def __init__(self, model, shape, original):
        self.model = model
        self.shape = shape
        self.original = original
        self.initial_digest = hashlib.sha256(original).digest()
        recovery = recovery_root()
        recovery.mkdir(mode=0o700, parents=True, exist_ok=True)
        title = Path(model.getTitle()).name or "Office"
        self.directory = Path(mkdtemp(prefix=title[:40] + "-", dir=recovery))
        self.path = self.directory / "Office drawing.r62doc"
        try:
            self.path.write_bytes(original)
            self.fingerprints = set()
            self.record_origin(original)
            model.addDocumentEventListener(self)
        except Exception:
            shutil.rmtree(self.directory)
            raise

        self.watching = False
        self.disposed = False

    def record_origin(self, data):
        self.fingerprints.add(hashlib.sha256(data).hexdigest())
        metadata = (recovery_target(self.model, self.shape) if self.shape is not None else
                    {"document": self.model.getURL(), "object": None})
        metadata["sources"] = sorted(self.fingerprints)
        staging = self.directory / "origin.tmp"
        staging.write_text(json.dumps(metadata), encoding="utf-8")
        staging.replace(self.directory / "origin.json")

    def documentEventOccured(self, event):
        if event.EventName == "OnSaveAsDone" and self.directory.is_dir():
            self.record_origin(self.original)

    def disposing(self, event):
        self.disposed = True

    def launch(self, callback=None):
        command = [EXECUTABLE, "--edit-office-object", str(self.path)]
        if callback is None:
            subprocess.Popen(command, start_new_session=True)
            return
        reader, writer = os.pipe()
        try:
            subprocess.Popen(command, pass_fds=(writer,), start_new_session=True,
                             env={**os.environ, "MOLECULESTUDIO_OFFICE_FD": str(writer)})
        except Exception:
            os.close(reader)
            raise
        finally:
            os.close(writer)

        def await_close():
            with os.fdopen(reader, "rb") as pipe:
                pipe.read()
            callback()

        threading.Thread(target=await_close, daemon=True).start()

    def close(self):
        if not self.disposed:
            self.model.removeDocumentEventListener(self)
        shutil.rmtree(self.directory)


class EditorClosed(unohelper.Base, XCallback):
    def __init__(self, handler, entry):
        self.handler = handler
        self.entry = entry

    def notify(self, unused):
        self.handler.editor_closed(self.entry)

class SlideDrawingInsertion(unohelper.Base, XUndoAction):
    def __init__(self, page, shape):
        self.page = page
        self.shape = shape

    @property
    def Title(self):
        return "Insert MoleculeStudio drawing"

    def undo(self):
        self.page.remove(self.shape)

    def redo(self):
        self.page.add(self.shape)


class ProtocolHandler(unohelper.Base, XInitialization, XDispatchProvider, XDispatch):
    edits = []

    def __init__(self, context):
        self.context = context
        self.frame = None

    def initialize(self, arguments):
        self.frame = arguments[0] if arguments else None

    def service(self, name):
        return self.context.ServiceManager.createInstanceWithContext(name, self.context)

    def queryDispatch(self, url, target, flags):
        if url.Protocol == PROTOCOL and url.Path in ("new", "insert", "insertFile", "edit", "apply", "refresh", "recover"):
            return self
        return None

    def queryDispatches(self, descriptors):
        return tuple(self.queryDispatch(item.FeatureURL, item.FrameName, item.SearchFlags) for item in descriptors)

    def addStatusListener(self, listener, url):
        event = uno.createUnoStruct("com.sun.star.frame.FeatureStateEvent")
        event.Source = self
        event.FeatureURL = url
        event.IsEnabled = True
        listener.statusChanged(event)

    def removeStatusListener(self, listener, url):
        pass

    def dispatch(self, url, arguments):
        try:
            controller = self.frame.getController()
            model = controller.getModel()
            if url.Path in ("insert", "insertFile"):
                source = self.clipboard_source() if url.Path == "insert" else None
                if source is None:
                    source = self.pick_document()
                if source:
                    self.insert(model, controller, source)
            elif url.Path == "new":
                self.new_drawing(model, controller)
            elif url.Path == "edit":
                self.edit(model, selected_shape(controller))
            elif url.Path == "apply":
                self.apply(model, selected_shape(controller))
            elif url.Path == "refresh":
                self.refresh_preview(model, selected_shape(controller))
            elif url.Path == "recover":
                self.recover(model, selected_shape(controller))
        except Exception as error:
            self.show_error(error)

    def show_error(self, error):
        box = self.service("com.sun.star.awt.Toolkit").createMessageBox(
            self.frame.getContainerWindow(), uno.Enum("com.sun.star.awt.MessageBoxType", "ERRORBOX"),
            1, "MoleculeStudio", str(error))
        box.execute()

    def clipboard_source(self):
        clipboard = self.service("com.sun.star.datatransfer.clipboard.SystemClipboard")
        data = clipboard.getContents() if clipboard else None
        if data:
            for flavor in data.getTransferDataFlavors():
                if flavor.MimeType.partition(";")[0].lower() == MIME:
                    source = data.getTransferData(flavor)
                    return validate(bytes(source.value if isinstance(source, uno.ByteSequence) else source))
        return None

    def pick_document(self):
        dialog = self.service("com.sun.star.ui.dialogs.FilePicker")
        try:
            dialog.setTitle("Insert a MoleculeStudio drawing")
            dialog.appendFilter("MoleculeStudio / ChemDraw (*.r62doc, *.cdxml, *.cdx)", "*.r62doc;*.cdxml;*.cdx")
            if dialog.execute() == 1:
                path = Path(uno.fileUrlToSystemPath(dialog.getSelectedFiles()[0]))
                if path.suffix.lower() not in (".cdxml", ".cdx"):
                    return file_data(path)
                if path.stat().st_size > MAX_DOCUMENT:
                    raise ValueError("This drawing is too large to embed in the Office document.")
                return self.import_drawing(path)
        finally:
            dialog.dispose()
        return None

    def graphic(self, path, folder, embedded=None, frame=None):
        svg = Path(folder) / "preview.svg"
        command = [EXECUTABLE, "--export-office-preview", str(path), str(svg)]
        if embedded is not None:
            command.append(str(embedded))
            if frame is not None:
                command.extend([str(frame.Width), str(frame.Height)])
        result = subprocess.run(command,
                                capture_output=True, text=True, timeout=30, check=False)
        if result.returncode:
            raise ValueError(result.stderr.strip() or "MoleculeStudio could not generate a preview.")
        graphic = self.service("com.sun.star.graphic.GraphicProvider").queryGraphic(
            (property_value("URL", svg.as_uri()),))
        if not graphic:
            raise ValueError("LibreOffice could not load the drawing preview.")
        size = graphic.Size100thMM
        if size.Width <= 0 or size.Height <= 0:
            raise ValueError("The drawing preview has invalid dimensions.")
        return graphic, size

    def writer_graphic(self, folder, picture, logical, fallback):
        # Writer preserves an imported metafile when saving the OLE replacement
        # image in DOCX. Its SVG replacement is rasterized by the DOCX filter.
        folder = Path(folder)
        svg = folder / "preview.svg"
        from xml.etree import ElementTree
        width, height = map(float, ElementTree.parse(svg).getroot().get("viewBox").split()[2:])
        preview = storage_codec().vector_preview(svg, folder, width, height, picture, logical,
                                                  office_module("office_emf").mixed_emf)
        if preview is None:
            return fallback
        return self.service("com.sun.star.graphic.GraphicProvider").queryGraphic(
            (property_value("URL", preview.as_uri()),)) or fallback

    @staticmethod
    def import_drawing(path):
        with TemporaryDirectory(prefix="moleculestudio-import-") as directory:
            native = Path(directory) / "drawing.r62doc"
            result = subprocess.run([EXECUTABLE, "--export-office-source", str(path), str(native)],
                                    capture_output=True, text=True, timeout=60, check=False)
            if result.returncode:
                raise ValueError(result.stderr.strip() or "MoleculeStudio could not open this drawing.")
            return file_data(native)

    def new_drawing(self, model, controller):
        if model.supportsService("com.sun.star.text.TextDocument"):
            view = controller.getViewCursor()
            anchor = view.getText().createTextCursorByRange(view.getStart())
        else:
            anchor = controller.getCurrentPage()
        with TemporaryDirectory(prefix="moleculestudio-new-") as directory:
            blank = Path(directory) / "new.cdxml"
            blank.write_text("<CDXML><page/></CDXML>", encoding="utf-8")
            original = self.import_drawing(blank)
        entry = Edit(model, None, original)
        entry.anchor = anchor
        self.edits.append(entry)
        try:
            self.watch_editor(entry)
        except Exception:
            self.edits.remove(entry)
            entry.close()
            raise

    def insert(self, model, controller, source, anchor=None):
        writer = model.supportsService("com.sun.star.text.TextDocument")
        service = "com.sun.star.text.TextEmbeddedObject" if writer else "com.sun.star.drawing.OLE2Shape"
        with TemporaryDirectory(prefix="moleculestudio-office-") as folder:
            path = Path(folder) / "source.r62doc"
            path.write_bytes(validate(source))
            embedded_path = Path(folder) / "embedded.r62doc"
            graphic, size = self.graphic(path, folder, embedded_path)
            compound = Path(folder) / "drawing.bin"
            extent = (size.Width, size.Height) if writer else presentation_extent(size)
            picture, logical = storage_codec().write_storage(embedded_path, Path(folder) / "preview.svg", compound, extent, source)
            if writer:
                graphic = self.writer_graphic(folder, picture, logical, graphic)
            payload = uno.ByteSequence(compound.read_bytes())

        page = None if writer else (controller.getCurrentPage() if anchor is None else anchor)

        with undo_context(model, "Insert MoleculeStudio drawing"):
            resolver = model.createInstance("com.sun.star.document.ImportEmbeddedObjectResolver")
            name = "ObjMolecule" + uuid4().hex
            try:
                stream = resolver.getByName(name)
                stream.writeBytes(payload)
                stream.closeOutput()
                url = resolver.resolveEmbeddedObjectURL(name)
                prefix = "vnd.sun.star.EmbeddedObject:"
                if not url.startswith(prefix) or len(url) <= len(prefix):
                    raise ValueError("LibreOffice could not create the embedded drawing.")
                key = url[len(prefix):]
                remove = None
                try:
                    shape = model.createInstance(service)
                    if writer:
                        shape.StreamName = key
                        shape.AnchorType = uno.Enum("com.sun.star.text.TextContentAnchorType", "AS_CHARACTER")
                        cursor = controller.getViewCursor() if anchor is None else anchor
                        text = cursor.getText()
                        text.insertTextContent(cursor, shape, False)
                        remove = text.removeTextContent
                    else:
                        page.add(shape)
                        remove = page.remove
                        shape.PersistName = key
                        shape.setPosition(Point(2000, 2000))
                    shape.Graphic = graphic
                    shape.setSize(size)
                    self.record_ole_class(model, key)
                except Exception:
                    if remove:
                        remove(shape)
                    storage = model.getDocumentStorage()
                    if storage.hasByName(key):
                        storage.removeElement(key)
                    raise
            finally:
                resolver.dispose()

            if not writer:
                model.getUndoManager().addUndoAction(SlideDrawingInsertion(page, shape))
        if writer or controller.getCurrentPage() == page:
            controller.select(shape)

    @staticmethod
    def record_ole_class(model, key):
        # Both OOXML filters take the OLE program identifier from this document property.
        sequence = "[]com.sun.star.beans.PropertyValue"
        properties = list(model.InteropGrabBag)
        objects = next((p for p in properties if p.Name == "EmbeddedObjects"), None)
        existing = tuple(objects.Value) if objects else ()
        program = property_value("ProgID", "MoleculeStudio.Document.1")
        entries = list(existing)
        for index, entry in enumerate(entries):
            if entry.Name == key:
                fields = tuple(entry.Value)
                if any(field.Name == "ProgID" and field.Value == program.Value for field in fields):
                    return
                fields = tuple(field for field in fields if field.Name != "ProgID") + (program,)
                entries[index] = property_value(key, uno.Any(sequence, fields))
                break
        else:
            entries.append(property_value(key, uno.Any(sequence, (program,))))
        recorded = property_value("EmbeddedObjects", uno.Any(sequence, tuple(entries)))
        if objects:
            properties[properties.index(objects)] = recorded
        else:
            properties.append(recorded)
        model.InteropGrabBag = tuple(properties)

    def pending(self, model, shape):
        return next((item for item in self.edits if item.model == model and item.shape == shape), None)

    def watch_editor(self, entry):
        events = self.service("com.sun.star.awt.AsyncCallback")
        closed = EditorClosed(self, entry)
        entry.watching = True
        entry.launch(lambda: events.addCallback(closed, None))

    def edit(self, model, shape):
        entry = self.pending(model, shape)
        if entry and entry.watching:
            entry.launch()
            return
        created = entry is None
        if created:
            entry = Edit(model, shape, source(model, shape))
            self.edits.append(entry)
        try:
            self.watch_editor(entry)
        except Exception:
            entry.watching = False
            if created:
                self.edits.remove(entry)
                entry.close()
            raise

    def editor_closed(self, entry):
        entry.watching = False
        if entry not in self.edits:
            return
        try:
            if entry.disposed:
                self.edits.remove(entry)
                if hashlib.sha256(file_data(entry.path)).digest() == entry.initial_digest:
                    entry.close()
                return
            if file_data(entry.path) == entry.original:
                self.edits.remove(entry)
                entry.close()
                return
            if entry.shape is None:
                controller = entry.model.getCurrentController()
                self.insert(entry.model, controller, file_data(entry.path), entry.anchor)
                self.edits.remove(entry)
                entry.close()
            else:
                self.apply(entry.model, entry.shape)
        except Exception as error:
            if entry.shape is None and entry in self.edits:
                self.edits.remove(entry)
                if not entry.disposed:
                    entry.model.removeDocumentEventListener(entry)
                self.show_error(f"{error}\nSaved drawing: {entry.path}")
            else:
                self.show_error(error)

    def apply_ole(self, model, shape, path, original_source, graphic, folder, storage, key):
        data = Path(folder) / "replacement.bin"
        old_size = shape.getSize()
        extent = (old_size.Width, old_size.Height)
        picture, logical = storage_codec().write_storage(path, Path(folder) / "preview.svg", data, extent, original_source)
        if model.supportsService("com.sun.star.text.TextDocument"):
            graphic = self.writer_graphic(folder, picture, logical, graphic)
        replacement = data.read_bytes()
        original = storage.cloneStreamElement(key)
        embedded = shape.EmbeddedObject
        scratch = self.service("com.sun.star.embed.StorageFactory").createInstance()
        reconnect = uno.getConstantByName("com.sun.star.embed.EntryInitModes.NO_INIT")

        def overwrite(content):
            stream = storage.openStreamElement(key, 6)
            try:
                output = stream.getOutputStream()
                output.truncate()
                output.writeBytes(uno.ByteSequence(content))
                output.closeOutput()
            finally:
                stream.dispose()

        embedded.storeAsEntry(scratch, "Backup", (), ())
        embedded.saveCompleted(True)
        try:
            overwrite(replacement)
            shape.Graphic = graphic
            shape.setSize(old_size)
            embedded.setPersistentEntry(storage, key, reconnect, (), ())
        except Exception:
            stream = original.getInputStream()
            _, old = stream.readBytes(None, stream.getLength())
            overwrite(bytes(old))
            embedded.setPersistentEntry(storage, key, reconnect, (), ())
            shape.setSize(old_size)
            raise
        finally:
            original.dispose()
        model.setModified(True)
    def recover(self, model, shape):
        if self.pending(model, shape):
            raise ValueError("Close the active MoleculeStudio editor before recovering another edit.")
        original = source(model, shape)
        if original is None:
            raise ValueError("Select one image inserted by MoleculeStudio.")
        root = recovery_root()
        if not root.is_dir():
            raise ValueError("No saved MoleculeStudio recovery drawings were found.")
        target = recovery_target(model, shape)
        fingerprint = hashlib.sha256(original).hexdigest()

        def matches(metadata):
            return (metadata.get("document") == target["document"] and
                    metadata.get("object") == target["object"] and fingerprint in metadata.get("sources", []))

        # Start the chooser at the most recent matching draft. The user can
        # also open a retained draft from an earlier extension version.
        directory = root
        latest = -1
        for manifest in root.glob("*/origin.json"):
            try:
                metadata = json.loads(manifest.read_text(encoding="utf-8"))
                modified = (manifest.parent / "Office drawing.r62doc").stat().st_mtime_ns
                if matches(metadata) and modified > latest:
                    directory, latest = manifest.parent, modified
            except (OSError, ValueError):
                continue
        dialog = self.service("com.sun.star.ui.dialogs.FilePicker")
        try:
            dialog.setTitle("Recover a saved MoleculeStudio edit")
            dialog.appendFilter("MoleculeStudio (*.r62doc)", "*.r62doc")
            dialog.setDisplayDirectory(uno.systemPathToFileUrl(str(directory)))
            if dialog.execute() != 1:
                return
            path = Path(uno.fileUrlToSystemPath(dialog.getSelectedFiles()[0]))
        finally:
            dialog.dispose()
        manifest = path.parent / "origin.json"
        if manifest.exists() and not matches(json.loads(manifest.read_text(encoding="utf-8"))):
            raise ValueError("This recovery file belongs to another document, drawing, or edit revision.")
        data = file_data(path)
        if source(model, shape) != original:
            raise ValueError("This Office drawing changed while the recovery file was being selected.")
        if data != original:
            self.replace_source(model, shape, data)

    def apply(self, model, shape):
        entry = self.pending(model, shape)
        if not entry:
            raise ValueError("Choose Edit in MoleculeStudio before applying changes.")
        if source(model, shape) != entry.original:
            raise ValueError("This Office image changed after you started editing it.")
        data = file_data(entry.path)
        self.replace_source(model, shape, data)
        if entry.watching:
            entry.original = data
            entry.record_origin(data)
        else:
            self.edits.remove(entry)
            entry.close()

    def refresh_preview(self, model, shape):
        data = source(model, shape)
        if data is None:
            raise ValueError("Select one image inserted by MoleculeStudio.")
        with TemporaryDirectory(prefix="moleculestudio-preview-") as directory:
            folder = Path(directory)
            snapshot = folder / "drawing.r62doc"
            snapshot.write_bytes(data)
            size = shape.getSize()
            native = ole_entry(model, shape)
            graphic, _ = self.graphic(snapshot, folder, folder / "embedded.r62doc" if native else None, size)
            if native and model.supportsService("com.sun.star.text.TextDocument"):
                from xml.etree import ElementTree
                svg = folder / "preview.svg"
                width, height = map(float, ElementTree.parse(svg).getroot().get("viewBox").split()[2:])
                picture, logical = storage_codec().wmf(svg, width, height)
                graphic = self.writer_graphic(folder, picture, logical, graphic)
            with undo_context(model, "Refresh MoleculeStudio drawing preview"):
                shape.Graphic = graphic
                placed = shape.getSize()
                if (placed.Width, placed.Height) != (size.Width, size.Height):
                    shape.setSize(size)
                model.setModified(True)

    def replace_source(self, model, shape, data):
        with TemporaryDirectory(prefix="moleculestudio-office-") as folder:
            snapshot = Path(folder) / "snapshot.r62doc"
            snapshot.write_bytes(data)
            native = ole_entry(model, shape)
            embedded_path = Path(folder) / "embedded.r62doc" if native else None
            graphic, size = self.graphic(snapshot, folder, embedded_path, shape.getSize() if native else None)
            if native:
                self.apply_ole(model, shape, embedded_path, data, graphic, folder, *native)
            else:
                value = encode(data)
                old_graphic = shape.Graphic
                old_size = shape.getSize()
                new_size = Size(old_size.Width, max(1, round(old_size.Width * size.Height / size.Width)))
                with undo_context(model, "Update MoleculeStudio drawing"):
                    try:
                        shape.Graphic = graphic
                        shape.setSize(new_size)
                        model.setModified(True)
                        DrawingSource(model, shape, value).commit()
                    except Exception:
                        with suppress(Exception):
                            shape.Graphic = old_graphic
                            shape.setSize(old_size)
                        raise


def slide_object_at_click(controller, event):
    model = controller.getModel()
    if not model.supportsService("com.sun.star.presentation.PresentationDocument"):
        return None
    size = event.Source.getOutputSize()
    if not (0 <= event.X < size.Width and 0 <= event.Y < size.Height):
        return None
    area = controller.VisibleArea
    x = area.X + event.X * area.Width / size.Width
    y = area.Y + event.Y * area.Height / size.Height
    page = controller.getCurrentPage()
    layers = model.getLayerManager()
    # Respect slide stacking order when a visible drawing overlaps another object.
    for index in reversed(range(page.Count)):
        shape = page.getByIndex(index)
        layer = layers.getLayerForShape(shape)
        if not layer.IsVisible or not getattr(shape, "Visible", True):
            continue
        bounds = shape.BoundRect
        if bounds.X <= x < bounds.X + bounds.Width and bounds.Y <= y < bounds.Y + bounds.Height:
            if layer.IsLocked or not ole_entry(model, shape):
                return None
            # Impress maps the unit square into slide coordinates for each shape.
            matrix = shape.Transformation
            a, b = matrix.Line1.Column1, matrix.Line1.Column2
            c, d = matrix.Line2.Column1, matrix.Line2.Column2
            determinant = a * d - b * c
            if not determinant:
                continue
            dx, dy = x - matrix.Line1.Column3, y - matrix.Line2.Column3
            u = (d * dx - b * dy) / determinant
            v = (a * dy - c * dx) / determinant
            if 0 <= u <= 1 and 0 <= v <= 1:
                return shape if is_drawing(model, shape) else None
    return None


class DrawingSaveListener(unohelper.Base, XDocumentEventListener):
    def documentEventOccured(self, event):
        if event.EventName not in ("OnSave", "OnSaveAs", "OnSaveTo"):
            return
        # Undo/redo can give an existing OLE stream a new storage name.
        # The OOXML exporter uses that name when looking up its program ID.
        model = event.Source
        if model.supportsService("com.sun.star.text.TextDocument"):
            shapes = model.EmbeddedObjects
        else:
            shapes = document_shapes(model)
        bag = next((p.Value for p in model.InteropGrabBag if p.Name == "EmbeddedObjects"), ())
        registered = {p.Name for p in bag if any(f.Name == "ProgID" and f.Value == "MoleculeStudio.Document.1"
                                                  for f in p.Value)}
        if not registered:
            return
        for shape in shapes:
            entry = ole_entry(model, shape)
            if entry:
                storage, key = entry
                if key in registered:
                    continue
                stream = storage.cloneStreamElement(key).getInputStream()
                try:
                    if is_molecule_stream(stream):
                        ProtocolHandler.record_ole_class(model, key)
                finally:
                    stream.closeInput()

    def disposing(self, event):
        pass


class DrawingMouseHandler(unohelper.Base, XMouseClickHandler):
    def __init__(self, context, controller):
        self.editor = ProtocolHandler(context)
        self.editor.initialize((controller.getFrame(),))
        self.consumed = False

    def mousePressed(self, event):
        self.consumed = False
        if event.ClickCount != 2 or event.Buttons != 1 or event.Modifiers:
            return False
        controller = self.editor.frame.getController()
        try:
            shape = selected_shape(controller)
        except (ValueError, AttributeError):
            shape = slide_object_at_click(controller, event)
            if shape is None:
                return False
            controller.select(shape)
        self.consumed = True
        try:
            self.editor.edit(controller.getModel(), shape)
        except Exception as error:
            self.editor.show_error(error)
        return True

    def mouseReleased(self, event):
        consumed = self.consumed
        self.consumed = False
        return consumed

    def disposing(self, event):
        self.editor.frame = None


class DrawingPaste(unohelper.Base, XDispatch):
    def __init__(self, editor, native):
        self.editor = editor
        self.native = native

    def addStatusListener(self, listener, url):
        self.native.addStatusListener(listener, url)

    def removeStatusListener(self, listener, url):
        self.native.removeStatusListener(listener, url)

    def dispatch(self, url, arguments):
        # Special paste modes belong to the host. For a normal paste, read
        # the current clipboard at invocation time, even for a cached dispatch.
        frame = self.editor.frame
        controller = frame.getController() if frame else None
        model = controller.getModel() if controller else None
        if not arguments and model and not model.isReadonly():
            try:
                source = self.editor.clipboard_source()
                if source is not None:
                    self.editor.insert(model, controller, source)
                    return
            except Exception as error:
                logging.getLogger(__name__).exception("Could not paste a MoleculeStudio drawing")
                self.editor.show_error(error)
                return
        self.native.dispatch(url, arguments)


class PasteInterceptor(unohelper.Base, XDispatchProviderInterceptor, XInterceptorInfo):
    def __init__(self, editor):
        self.editor = editor
        self.master = self.slave = None

    def getInterceptedURLs(self):
        return (".uno:Paste",)

    def getMasterDispatchProvider(self):
        return self.master

    def setMasterDispatchProvider(self, provider):
        self.master = provider

    def getSlaveDispatchProvider(self):
        return self.slave

    def setSlaveDispatchProvider(self, provider):
        self.slave = provider

    def queryDispatch(self, url, target, flags):
        native = self.slave.queryDispatch(url, target, flags) if self.slave else None
        if native and url.Complete == ".uno:Paste" and target in ("", "_self"):
            return DrawingPaste(self.editor, native)
        return native

    def queryDispatches(self, descriptors):
        return tuple(self.queryDispatch(d.FeatureURL, d.FrameName, d.SearchFlags) for d in descriptors)


class RegisterDrawingMouseHandler(unohelper.Base, XJob):
    def __init__(self, context):
        self.context = context

    def execute(self, arguments):
        environment = next((arg.Value for arg in arguments if arg.Name == "Environment"), ())
        model = next((arg.Value for arg in environment if arg.Name == "Model"), None)
        if model and any(model.supportsService(service) for service in (
            "com.sun.star.text.TextDocument", "com.sun.star.presentation.PresentationDocument"
        )):
            model.addDocumentEventListener(DrawingSaveListener())
            controller = model.getCurrentController()
            if controller and hasattr(controller, "addMouseClickHandler"):
                handler = DrawingMouseHandler(self.context, controller)
                controller.addMouseClickHandler(handler)
                controller.getFrame().registerDispatchProviderInterceptor(PasteInterceptor(handler.editor))


class PresentationFilter(unohelper.Base, XExporter, XImporter, XFilter, XInitialization):
    """Supply the OLE original size that Impress omits from PowerPoint slides."""

    def __init__(self, context):
        self.context = context
        self.native = None
        self.arguments = ()
        self.model = None
        self.cancelled = False

    def initialize(self, arguments):
        self.arguments = arguments

    def prepare(self):
        self.native = self.context.ServiceManager.createInstanceWithContext(
            "com.sun.star.comp.oox.ppt.PowerPointImport", self.context)
        if self.arguments and hasattr(self.native, "initialize"):
            self.native.initialize(self.arguments)
        self.cancelled = False

    def setSourceDocument(self, model):
        self.prepare()
        self.model = model
        self.native.setSourceDocument(model)

    def setTargetDocument(self, model):
        self.prepare()
        self.model = None
        self.native.setTargetDocument(model)

    def cancel(self):
        self.cancelled = True
        native = self.native
        if native is not None:
            native.cancel()

    def filter(self, properties):
        # Release the native importer while its document and drawing pool are live.
        try:
            return self.filter_document(properties)
        finally:
            self.native = None
            self.model = None

    def filter_document(self, properties):
        props = {p.Name: p.Value for p in properties}
        if self.model is None or "OutputStream" not in props or "EncryptionData" in props or "Password" in props:
            return self.native.filter(properties)
        objects = next((p.Value for p in getattr(self.model, "InteropGrabBag", ()) if p.Name == "EmbeddedObjects"), ())
        if not any(getattr(prop, "Name", None) == "ProgID" and prop.Value == "MoleculeStudio.Document.1"
                   for obj in objects if isinstance(obj.Value, (tuple, list)) for prop in obj.Value):
            return self.native.filter(properties)

        # Impress still owns the URL, file lock and final commit. Its normal exporter
        # writes to a private seekable stream before we supply bytes to that commit.
        private = self.context.ServiceManager.createInstanceWithContext("com.sun.star.io.TempFile", self.context)
        path = Path(uno.fileUrlToSystemPath(private.Uri))
        streams = {"StreamForOutput": private, "OutputStream": private.getOutputStream()}
        outgoing = tuple(PropertyValue(p.Name, p.Handle, streams[p.Name], p.State)
                         if p.Name in streams else p for p in properties)
        if not self.native.filter(outgoing) or self.cancelled:
            return False
        with TemporaryDirectory(prefix="moleculestudio-slides-") as folder:
            try:
                result = office_module("presentation_extents").presentation_extents(path, Path(folder), storage_codec())
            except Exception:
                # A failed thumbnail adjustment must not discard a successful native save.
                logging.getLogger(__name__).exception("Could not set PowerPoint OLE preview sizes")
                result = path
            if self.cancelled:
                return False
            output = props["OutputStream"]
            with result.open("rb") as source:
                if hasattr(output, "truncate"):
                    output.truncate()
                while chunk := source.read(1024 * 1024):
                    if self.cancelled:
                        return False
                    output.writeBytes(uno.ByteSequence(chunk))
            output.flush()
        return True

class WriterPdfFilter(unohelper.Base, XExporter, XFilter):
    """Render a Writer print copy when it contains a MoleculeStudio OLE frame."""

    def __init__(self, context):
        self.context = context
        self.native = context.ServiceManager.createInstanceWithContext(
            "com.sun.star.comp.PDF.PDFFilter", context)
        self.model = None
        self.cancelled = False

    def setSourceDocument(self, model):
        self.model = model
        self.native.setSourceDocument(model)

    def cancel(self):
        self.cancelled = True
        self.native.cancel()

    def has_molecule(self):
        for shape in self.model.EmbeddedObjects:
            entry = ole_entry(self.model, shape)
            if entry:
                storage, name = entry
                stream = storage.cloneStreamElement(name).getInputStream()
                try:
                    if is_molecule_stream(stream):
                        return True
                finally:
                    stream.closeInput()
        return False

    def filter(self, properties):
        props = {p.Name: p.Value for p in properties}
        options = {p.Name: p.Value for p in props.get("FilterData", ())}
        model = self.model
        if (not model.EmbeddedObjects.Count or options.get("IsAddStream") or
                "Selection" in options or "Selection" in props):
            return self.native.filter(properties)
        try:
            has_molecule = self.has_molecule()
        except Exception:
            logging.getLogger(__name__).exception("Could not identify embedded Writer objects")
            has_molecule = False
        if not has_molecule:
            return self.native.filter(properties)
        desktop = self.context.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", self.context)
        with TemporaryDirectory(prefix="moleculestudio-writer-") as directory:
            snapshot = Path(directory) / "document.odt"
            target = Path(directory) / "print.odt"
            copy = None
            try:
                try:
                    base = property_value("DocumentBaseURL", model.URL)
                    model.storeToURL(snapshot.as_uri(), (property_value("FilterName", "writer8"), base))
                    if office_module("writer_preview").print_copy(snapshot, target, storage_codec().CLASS.bytes_le):
                        copy = desktop.loadComponentFromURL(target.as_uri(), "_blank", 0, (
                            property_value("Hidden", True), property_value("ReadOnly", True),
                            property_value("MacroExecutionMode", NEVER_EXECUTE),
                            property_value("UpdateDocMode", NO_UPDATE), base))
                    if copy:
                        # The native PDF filter resolves relative file actions
                        # against the model URL, independently of DocumentBaseURL.
                        if model.URL and not copy.attachResource(model.URL, copy.Args):
                            raise RuntimeError("Could not retain the Writer document URL for PDF links")
                        self.native.setSourceDocument(copy)
                except Exception:
                    logging.getLogger(__name__).exception("Could not prepare Writer vector previews")
                    self.native.setSourceDocument(model)
                return not self.cancelled and self.native.filter(properties)
            finally:
                if copy:
                    self.native.setSourceDocument(model)
                    copy.close(True)


g_ImplementationHelper = unohelper.ImplementationHelper()
writer_pdf_service = "org.moleculestudio.office.WriterPdfFilter"
g_ImplementationHelper.addImplementation(WriterPdfFilter, writer_pdf_service, (writer_pdf_service,))
presentation_service = "org.moleculestudio.office.PresentationFilter"
g_ImplementationHelper.addImplementation(PresentationFilter, presentation_service, (presentation_service,))
g_ImplementationHelper.addImplementation(ProtocolHandler, IMPLEMENTATION, ("com.sun.star.frame.ProtocolHandler",))
g_ImplementationHelper.addImplementation(RegisterDrawingMouseHandler, "org.moleculestudio.office.RegisterDrawingMouseHandler", ("com.sun.star.task.Job",))
