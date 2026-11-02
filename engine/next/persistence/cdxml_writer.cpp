#include "persistence.hpp"
#include "persistence_internal.hpp"

#include "third_party/tinyxml2/tinyxml2.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <locale>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace r62 {
namespace persistence {

using namespace cdxml;
using core::Error;
using core::ErrorCode;
using core::ObjectKind;
using core::ObjectRef;
using tinyxml2::XMLElement;

namespace {
// ---------------------------------------------------------------------------
// Writer state
// ---------------------------------------------------------------------------
struct Writer {
    const core::Document& document;
    CdxmlWriteOptions options;
    RefusalReport* refusals;
    CdxmlWriteFidelity* fidelity;
    std::ostringstream out;
    std::map<core::ObjectRef, int32_t> fileId;
    std::map<core::ObjectId, int32_t> atomFileId;
    SourceIdMap source;
    std::set<int32_t> usedFileIds;
    int32_t nextFileId;
    std::vector<std::string> fontNames;
    std::map<std::string, int32_t> fontIdByName;
    int32_t nextFontId;

    Writer(const core::Document& d, const CdxmlWriteOptions& o, RefusalReport* r,
           CdxmlWriteFidelity* f)
        : document(d), options(o), refusals(r), fidelity(f), nextFileId(3),
          nextFontId(3) {}

    void refuse(const char* element, const std::string& reason, int32_t sourceId,
                const core::ObjectRef& ref) {
        if (!refusals) return;
        Refusal item;
        item.path = "/CDXML";
        item.element = element ? element : "?";
        item.reason = reason;
        item.sourceId = sourceId;
        item.ref = ref;
        refusals->add(item);
    }

    int32_t reserveFileId() {
        while (usedFileIds.count(nextFileId)) ++nextFileId;
        const int32_t value = nextFileId++;
        usedFileIds.insert(value);
        return value;
    }

    int32_t fontFor(const std::string& name) {
        const std::string resolved = name.empty() ? std::string("Arial") : name;
        std::map<std::string, int32_t>::const_iterator it = fontIdByName.find(resolved);
        if (it != fontIdByName.end()) return it->second;
        const int32_t id = nextFontId++;
        fontNames.push_back(resolved);
        fontIdByName[resolved] = id;
        return id;
    }
};

// ---------------------------------------------------------------------------
// Id plan
// ---------------------------------------------------------------------------
// A patched export keeps the CDXML ids the objects originally had, because
// preserved raw regions and any external reference still point at them. An object
// with no source id (drawn in this session) gets a fresh one. A DUPLICATE source
// id - which real ChemDraw files do contain - is detected and, unless explicitly
// allowed, refused rather than silently merged.
Error planIds(Writer& writer) {
    const core::Document& document = writer.document;
    writer.source = SourceIdMap::fromDocument(document);

    const std::vector<int32_t>& collisions = writer.source.collisions();
    if (!collisions.empty() && !writer.options.allowDuplicateSourceIds) {
        std::ostringstream reason;
        reason << collisions.size()
               << " duplicate CDXML source id(s) present; renumbering them silently "
                  "would re-point every reference, so this export refuses instead";
        writer.refuse("CDXML", reason.str(), collisions[0], core::ObjectRef());
        return fail(reason.str(), ErrorCode::AlreadyExists);
    }

    // Reserve every surviving source id first, so a generated id cannot collide.
    const std::vector<core::AtomRecord>& atoms = document.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i)
        if (atoms[i].isAlive() && atoms[i].sourceId > 0)
            writer.usedFileIds.insert(atoms[i].sourceId);
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i)
        if (bonds[i].isAlive() && bonds[i].sourceId > 0)
            writer.usedFileIds.insert(bonds[i].sourceId);
    const std::vector<core::TextRecord>& texts = document.texts().records();
    for (size_t i = 0; i < texts.size(); ++i)
        if (texts[i].isAlive() && texts[i].sourceId > 0)
            writer.usedFileIds.insert(texts[i].sourceId);
    return Error::success();
}

void assignAtomIds(Writer& writer) {
    const std::vector<core::AtomRecord>& atoms = writer.document.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        if (atoms[i].sourceId > 0) writer.atomFileId[atoms[i].id] = atoms[i].sourceId;
        else writer.atomFileId[atoms[i].id] = writer.reserveFileId();
    }
}

// ---------------------------------------------------------------------------
// Escaping and attribute helpers
// ---------------------------------------------------------------------------
void attribute(std::ostringstream& out, const char* name, const std::string& value) {
    std::string escaped;
    escapeAttribute(value, &escaped);
    out << " " << name << "=\"" << escaped << "\"";
}

void attributeNumber(std::ostringstream& out, const char* name, double value) {
    out << " " << name << "=\"" << numberToText(value) << "\"";
}
void attributeInteger(std::ostringstream& out, const char* name, long long value) {
    out << " " << name << "=\"" << value << "\"";
}

// CDXML display spellings, the INVERSE of the reader table in cdxml_util.cpp.
// Both directions live in this module and the test suite asserts them against
// each other, because a one-sided table is how a round trip changes meaning.
const char* displayToCdxml(core::BondDisplay display) {
    switch (display) {
    case core::BondDisplay::Normal: return "Solid";
    case core::BondDisplay::Wedge: return "WedgeBegin";
    case core::BondDisplay::Hash: return "WedgedHashBegin";
    case core::BondDisplay::WedgeHash: return "WedgedHashBegin";
    case core::BondDisplay::HollowWedge: return "HollowWedgeBegin";
    case core::BondDisplay::Bold: return "Bold";
    case core::BondDisplay::Dashed: return "Dash";
    case core::BondDisplay::Dotted: return "Dot";
    case core::BondDisplay::Wavy: return "Wavy";
    }
    return "Solid";
}

const char* orderToCdxml(core::BondOrder order, core::BondSemantics semantics) {
    if (semantics == core::BondSemantics::Aromatic) return "1.5";
    switch (order) {
    case core::BondOrder::Single: return "1";
    case core::BondOrder::Double: return "2";
    case core::BondOrder::Triple: return "3";
    case core::BondOrder::Quadruple: return "4";
    case core::BondOrder::Aromatic: return "1.5";
    case core::BondOrder::SingleOrDouble: return "SingleOrDouble";
    case core::BondOrder::SingleOrAromatic: return "SingleOrAromatic";
    case core::BondOrder::DoubleOrAromatic: return "DoubleOrAromatic";
    case core::BondOrder::Any: return "Any";
    case core::BondOrder::Unspecified: return "Unspecified";
    }
    return "1";
}


const char* kindKeyName(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Atom: return "Atom";
    case ObjectKind::Bond: return "Bond";
    case ObjectKind::Text: return "Text";
    case ObjectKind::Path: return "Path";
    case ObjectKind::Arrow: return "Arrow";
    case ObjectKind::Bracket: return "Bracket";
    case ObjectKind::Table: return "Table";
    case ObjectKind::Orbital: return "Orbital";
    case ObjectKind::Symbol: return "Symbol";
    case ObjectKind::BioShape: return "BioShape";
    case ObjectKind::Group: return "Group";
    default: return "Unknown";
    }
}

std::string sourceKey(ObjectKind kind, core::ObjectId id) {
    std::ostringstream key;
    key << kExtensionPrefix << "src." << kindKeyName(kind) << "." << id;
    return key.str();
}

// ---------------------------------------------------------------------------
// Signature agreement
// ---------------------------------------------------------------------------
// An object may only be re-emitted from its preserved source bytes if the
// document has not changed since it was read. Otherwise the stale bytes would
// OVERWRITE the user edit - the exact opposite of the goal. The signature is
// recomputed from the current model state and compared with what the reader
// recorded. Any mismatch means "regenerate from the model".
//
// The signature covers every field the writer can emit, in the SAME order and
// formatting the writer uses, so a signature that matches implies the generated
// bytes equal the preserved bytes for the promoted part.
// Signatures live in cdxml_encoders.cpp so the reader and the writer cannot drift apart.

} // namespace


namespace {

// ---------------------------------------------------------------------------
// Preserved-attribute replay
// ---------------------------------------------------------------------------
// When an object is REGENERATED from the model, every attribute the reader could
// not promote must still be written. They live in the extension bag under
// pst.attr.<Name>; this replays them. The order the reader saw them in is kept so
// a second read sees the same file, which is what makes the byte comparison in
// the round-trip test meaningful.
void replayPreservedAttributes(std::ostringstream& out, const core::Document& document,
                               const ObjectRef& ref) {
    if (!ref.valid()) return;
    const core::ExtensionBag& bag = document.extensions(ref);
    const std::vector<core::ExtensionEntry>& entries = bag.entries();
    // Replay in the order recorded by the reader when it is available.
    std::ostringstream orderKeyText;
    orderKeyText << kExtensionPrefix << "attr.order." << kindKeyName(ref.kind) << "." << ref.id;
    const std::string orderKeyValue = orderKeyText.str();
    const std::string* order = bag.findValue(orderKeyValue);
    std::vector<std::string> names;
    if (order && !order->empty()) {
        size_t start = 0;
        for (;;) {
            const size_t comma = order->find(44, start);
            names.push_back(order->substr(start, comma == std::string::npos
                                                  ? std::string::npos : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    std::set<std::string> written;
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string key = std::string(kExtensionPrefix) + "attr." + names[i];
        const std::string* value = bag.findValue(key);
        if (!value) continue;
        attribute(out, names[i].c_str(), *value);
        written.insert(names[i]);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& key = entries[i].key;
        if (key.compare(0, 9, "pst.attr.") != 0) continue;
        // The ORDER BOOKKEEPING key lives in the same namespace and must never be
        // written back as if it were an attribute of the element.
        if (key.compare(0, 10, "pst.attr.o") == 0 && key.find("attr.order.") != std::string::npos)
            continue;
        const std::string name = key.substr(9);
        if (written.count(name)) continue;
        attribute(out, name.c_str(), entries[i].value);
        written.insert(name);
    }
}

// ---------------------------------------------------------------------------
// Atom emission
// ---------------------------------------------------------------------------
// The atom label is written as a <t> CHILD whose text is the label, which is how
// real CDXML stores an abbreviation. A plain element node with no label override
// stays self-closing, which is what the simple fixtures look like and what keeps
// the byte comparison meaningful.
void emitAtom(Writer& writer, const core::AtomRecord& atom) {
    const core::Document& document = writer.document;
    const std::map<core::ObjectId, int32_t>::const_iterator idIt = writer.atomFileId.find(atom.id);
    if (idIt == writer.atomFileId.end()) return;
    std::ostringstream& out = writer.out;
    out << "<n";
    attributeInteger(out, "id", idIt->second);
    out << " p=\"" << numberToText(atom.x()) << ' ' << numberToText(atom.y()) << "\"";
    if (atom.element != 6 || atom.semantics.nodeType != static_cast<uint8_t>(core::NodeType::Element))
        attributeInteger(out, "Element", atom.element);
    if (atom.semantics.charge != 0)
        attributeInteger(out, "Charge", atom.semantics.charge);
    if (atom.semantics.isotope != 0)
        attributeInteger(out, "Isotope", atom.semantics.isotope);
    if (atom.semantics.implicitHydrogens >= 0)
        attributeInteger(out, "NumHydrogens", atom.semantics.implicitHydrogens);
    if (atom.semantics.radical != 0)
        attributeInteger(out, "Radical", atom.semantics.radical);
    if (atom.semantics.nodeType != static_cast<uint8_t>(core::NodeType::Element)) {
        static const char* const kNodeNames[] = { "Element", "Nickname", "Fragment",
                                                  "RGroup", "Query", "Polymer",
                                                  "Attachment", "ExternalConnectionPoint" };
        const size_t index = atom.semantics.nodeType < 8 ? atom.semantics.nodeType : 0;
        attribute(out, "NodeType", kNodeNames[index]);
    }
    if (atom.semantics.showLabel) attribute(out, "LabelDisplay", "Shown");
    replayPreservedAttributes(out, document, ObjectRef(ObjectKind::Atom, atom.id));

    const std::string& label = document.atomLabel(atom.id);
    const std::vector<core::TextRun>& runs = document.runs(atom.id);
    if (label.empty() && runs.empty()) { out << "/>"; return; }
    out << "><t>";
    if (runs.empty()) {
        // A label override with no explicit run still has to be written, or the
        // abbreviation would silently disappear from the exported file.
        std::string escaped;
        escapeContent(label, &escaped);
        out << "<s font=\"" << writer.fontFor(document.defaultFont()) << "\" size=\""
            << numberToText(document.defaultTextSize()) << "\" face=\"96\">"
            << escaped << "</s>";
        out << "</t></n>";
        return;
    }
    for (size_t i = 0; i < runs.size(); ++i) {
        std::string escaped;
        escapeContent(runs[i].utf8, &escaped);
        out << "<s font=\"" << writer.fontFor(runs[i].fontName) << "\" size=\""
            << numberToText(runs[i].size > 0.0 ? runs[i].size : document.defaultTextSize())
            << "\"";
        const int face = faceFromTextFlags(runs[i].flags);
        if (face != 0) out << " face=\"" << face << "\"";
        out << ">" << escaped << "</s>";
    }
    out << "</t></n>";
}

void emitBond(Writer& writer, const core::BondRecord& bond) {
    const std::map<core::ObjectId, int32_t>::const_iterator beginIt =
        writer.atomFileId.find(bond.beginAtom);
    const std::map<core::ObjectId, int32_t>::const_iterator endIt =
        writer.atomFileId.find(bond.endAtom);
    if (beginIt == writer.atomFileId.end() || endIt == writer.atomFileId.end()) {
        writer.refuse("b", "bond endpoint does not resolve to an emitted atom",
                      bond.sourceId, ObjectRef(ObjectKind::Bond, bond.id));
        return;
    }
    std::ostringstream& out = writer.out;
    out << "<b id=\"" << (bond.sourceId > 0 ? bond.sourceId : writer.reserveFileId())
        << "\" B=\"" << beginIt->second << "\" E=\"" << endIt->second << "\"";
    out << " Order=\"" << orderToCdxml(bond.order, bond.semantics) << "\"";
    if (bond.display != core::BondDisplay::Normal)
        attribute(out, "Display", displayToCdxml(bond.display));
    replayPreservedAttributes(out, writer.document, ObjectRef(ObjectKind::Bond, bond.id));
    out << "/>";
}

void emitText(Writer& writer, const core::TextRecord& text) {
    const core::Document& document = writer.document;
    const std::vector<core::TextRun>& runs = document.runs(text.id);
    std::ostringstream& out = writer.out;
    out << "<t id=\"" << (text.sourceId > 0 ? text.sourceId : writer.reserveFileId())
        << "\" p=\"" << numberToText(text.x()) << ' ' << numberToText(text.y()) << "\"";
    if (text.format.align == static_cast<uint8_t>(core::TextAlign::Center))
        attribute(out, "Justification", "Center");
    else if (text.format.align == static_cast<uint8_t>(core::TextAlign::Right))
        attribute(out, "Justification", "Right");
    replayPreservedAttributes(out, document, ObjectRef(ObjectKind::Text, text.id));
    out << ">";
    const double defaultSize = text.size > 0.0 ? text.size : document.defaultTextSize();
    if (runs.empty()) {
        out << "<s font=\"" << writer.fontFor(text.format.fontName) << "\" size=\""
            << numberToText(defaultSize) << "\"/>";
    } else {
        for (size_t i = 0; i < runs.size(); ++i) {
            std::string escaped;
            escapeContent(runs[i].utf8, &escaped);
            out << "<s font=\"" << writer.fontFor(runs[i].fontName) << "\" size=\""
                << numberToText(runs[i].size > 0.0 ? runs[i].size : defaultSize) << "\"";
            const int face = faceFromTextFlags(runs[i].flags);
            if (face != 0) out << " face=\"" << face << "\"";
            out << ">" << escaped << "</s>";
        }
    }
    out << "</t>";
}

void emitPath(Writer& writer, const core::PathRecord& path) {
    std::ostringstream& out = writer.out;
    out << "<graphic";
    attributeInteger(out, "id", path.sourceId > 0 ? path.sourceId : writer.reserveFileId());
    attribute(out, "GraphicType", path.closed ? "Rectangle" : "Line");
    if (path.points.size() >= 2) {
        out << " BoundingBox=\"" << numberToText(path.points.front().x) << ' '
            << numberToText(path.points.front().y) << ' '
            << numberToText(path.points.back().x) << ' '
            << numberToText(path.points.back().y) << "\"";
    }
    if (path.style.width > 0.0) attributeNumber(out, "LineWidth", path.style.width);
    replayPreservedAttributes(out, writer.document, ObjectRef(ObjectKind::Path, path.id));
    out << "/>";
}

void emitAnnotation(Writer& writer, const core::AnnotationRecord& annotation) {
    std::ostringstream& out = writer.out;
    out << "<graphic";
    attributeInteger(out, "id",
                     annotation.sourceId > 0 ? annotation.sourceId : writer.reserveFileId());
    switch (annotation.kind) {
    case ObjectKind::Arrow: attribute(out, "GraphicType", "Line"); attribute(out, "ArrowType", "FullHead"); break;
    case ObjectKind::Bracket: attribute(out, "GraphicType", "Bracket"); break;
    case ObjectKind::Orbital: attribute(out, "GraphicType", "Orbital"); break;
    case ObjectKind::Symbol: attribute(out, "GraphicType", "Symbol"); break;
    case ObjectKind::BioShape: attribute(out, "GraphicType", "Undefined"); break;
    default: attribute(out, "GraphicType", "Undefined"); break;
    }
    const core::Vec2 a = annotation.geometry[0];
    const core::Vec2 b = annotation.geometry[2];
    out << " BoundingBox=\"" << numberToText(a.x) << ' ' << numberToText(a.y) << ' '
        << numberToText(b.x) << ' ' << numberToText(b.y) << "\"";
    if (annotation.style.width > 0.0) attributeNumber(out, "LineWidth", annotation.style.width);
    replayPreservedAttributes(out, writer.document,
                              ObjectRef(annotation.kind, annotation.id));
    out << "/>";
}

// Raw regions are source fragments no object models (an unrecognised <graphic>, an
// unknown sibling element). They are written back VERBATIM, in the source's own
// ORDER relative to the modelled objects, because the bytes came from the source
// file: re-emitting them cannot invent or lose anything, and moving them would
// reorder the user's document.
struct RawRegionEntry {
    long long order;
    std::string bytes;
    bool operator<(const RawRegionEntry& other) const { return order < other.order; }
};
void collectRawRegions(const core::Document& document,
                       std::vector<RawRegionEntry>* out) {
    const core::ExtensionBag& bag = document.documentExtensions();
    const std::vector<core::ExtensionEntry>& entries = bag.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& key = entries[i].key;
        if (key.compare(0, 8, "pst.raw.") != 0) continue;
        if (key.compare(0, 18, "pst.raw.order.") == 0) continue;   // bookkeeping, not a region
        RawRegionEntry region;
        const std::string sourceId = key.substr(8);
        const std::string* recorded =
            bag.findValue(std::string(kExtensionPrefix) + "raw.order." + sourceId);
        region.order = 1000000000LL;
        if (recorded && !recorded->empty()) {
            std::istringstream in(*recorded);
            in.imbue(std::locale::classic());
            in >> region.order;
        }
        region.bytes = entries[i].value;
        out->push_back(region);
    }
}

void emitRawRegionsInRange(Writer& writer, long long from, long long to) {
    std::vector<RawRegionEntry> regions;
    collectRawRegions(writer.document, &regions);
    std::stable_sort(regions.begin(), regions.end());
    for (size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].order < from || regions[i].order >= to) continue;
        writer.out << regions[i].bytes;
        if (writer.fidelity) ++writer.fidelity->rawFragmentsEmitted;
    }
}

} // namespace


namespace {


// ---------------------------------------------------------------------------
// Edit detection
// ---------------------------------------------------------------------------
// "Was this object edited since it was read?" is answered by comparing a signature
// computed from the CURRENT model state with the one the reader stored. Comparing a
// signature rather than trusting a dirty flag is deliberate: a dirty flag can be
// set by a transaction that changed nothing, and cleared by a rollback, while a
// signature cannot disagree with the data.
std::string storedSignature(const core::Document& document, const ObjectRef& ref) {
    std::ostringstream key;
    key << kExtensionPrefix << "sign." << kindKeyName(ref.kind) << "." << ref.id;
    const std::string* value = document.extensions(ref).findValue(key.str());
    return value ? *value : std::string();
}

bool objectUnchanged(const core::Document& document, const ObjectRef& ref,
                     const std::string& current) {
    const std::string stored = storedSignature(document, ref);
    return !stored.empty() && stored == current;
}

// Source bytes the reader preserved for one object.
const std::string* preservedSource(const core::Document& document, const ObjectRef& ref) {
    return document.extensions(ref).findValue(sourceKey(ref.kind, ref.id));
}

// ---------------------------------------------------------------------------
// Document assembly
// ---------------------------------------------------------------------------
// Re-emit an object from its preserved source bytes when, and only when, the
// model still matches what was read. Returns true when the object was written
// from its source. The SOURCE bytes carry the object's original id and every
// attribute, including the ones this writer cannot promote, which is exactly how
// the unknown-attribute preservation survives into the output.
bool emitPreservedIfUnchanged(Writer& writer, const ObjectRef& ref,
                              const std::string& currentSignature) {
    if (writer.options.mode != CdxmlExportMode::Lossless) return false;
    const std::string* bytes = preservedSource(writer.document, ref);
    if (!bytes || bytes->empty()) return false;
    if (!objectUnchanged(writer.document, ref, currentSignature)) return false;
    writer.out << *bytes;
    if (writer.fidelity) ++writer.fidelity->objectsFromSource;
    return true;
}

Error writeDocument(Writer& writer) {
    const core::Document& document = writer.document;
    std::ostringstream& out = writer.out;

    // Font table. Fonts are numbered by first use, and the mapping is written into
    // the document bag as pst.font.<n> = "<id>\x1f<name>" so a re-read assigns the
    // SAME numbers and byte identity is reproducible rather than accidental.
    std::vector<core::ObjectRef> objectOrder;

    assignAtomIds(writer);

    const std::vector<core::AtomRecord>& atoms = document.atoms().records();
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    const std::vector<core::TextRecord>& texts = document.texts().records();
    const std::vector<core::PathRecord>& paths = document.paths().records();

    for (size_t i = 0; i < atoms.size(); ++i) if (atoms[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Atom, atoms[i].id));
    for (size_t i = 0; i < bonds.size(); ++i) if (bonds[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Bond, bonds[i].id));
    for (size_t i = 0; i < texts.size(); ++i) if (texts[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Text, texts[i].id));
    for (size_t i = 0; i < paths.size(); ++i) if (paths[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Path, paths[i].id));

    const core::ObjectSlab<core::AnnotationRecord>* const slabs[5] = {
        &document.arrows(), &document.brackets(), &document.orbitals(),
        &document.symbols(), &document.bioShapes()
    };
    const ObjectKind slabKinds[5] = { ObjectKind::Arrow, ObjectKind::Bracket,
                                      ObjectKind::Orbital, ObjectKind::Symbol,
                                      ObjectKind::BioShape };
    for (int s = 0; s < 5; ++s) {
        const std::vector<core::AnnotationRecord>& records = slabs[s]->records();
        for (size_t i = 0; i < records.size(); ++i)
            if (records[i].isAlive())
                objectOrder.push_back(ObjectRef(slabKinds[s], records[i].id));
    }

    // Pre-register every font so the table is complete before the header is
    // printed, and so a run never references a font that has no entry.
    writer.fontFor(document.defaultFont());
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        const std::vector<core::TextRun>& runs = document.runs(texts[i].id);
        for (size_t k = 0; k < runs.size(); ++k) writer.fontFor(runs[k].fontName);
    }
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        const std::vector<core::TextRun>& runs = document.runs(atoms[i].id);
        for (size_t k = 0; k < runs.size(); ++k) writer.fontFor(runs[k].fontName);
    }

    // ---- header ---------------------------------------------------------
    // In Lossless mode the ENTIRE prologue (XML declaration, DOCTYPE, root open tag
    // with every attribute, font table, and the source's own whitespace) is written
    // back verbatim. Regenerating it cannot reproduce attribute spelling such as
    // LineWidth="0.6" vs "0.59999999999999998", the choice of which defaults the
    // source bothered to write, or the font table layout - all of which are part of
    // the file the user has. Byte identity is only claimed in this mode.
    const std::string* prologue =
        document.documentExtensions().findValue(std::string(kExtensionPrefix) + "source.prologue");
    if (writer.options.mode == CdxmlExportMode::Lossless && prologue && !prologue->empty()) {
        // The prologue is emitted VERBATIM, with no added separator: its final bytes
        // are the container open tag, and the first object's preserved slice already
        // begins with the whitespace that followed it.
        out << *prologue;
    } else {
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<CDXML";
        if (!document.name().empty()) attribute(out, "Name", document.name());
        attributeNumber(out, "BondLength", document.defaultBondLength());
        if (document.defaultLineWidth() > 0.0)
            attributeNumber(out, "LineWidth", document.defaultLineWidth());
        if (document.defaultTextSize() > 0.0)
            attributeNumber(out, "CaptionSize", document.defaultTextSize());
        if (!document.defaultFont().empty())
            attribute(out, "CaptionFont", document.defaultFont());
        {
            const core::ExtensionBag& bag = document.documentExtensions();
            const std::vector<core::ExtensionEntry>& entries = bag.entries();
            std::set<std::string> written;
            for (size_t i = 0; i < entries.size(); ++i) {
                const std::string& key = entries[i].key;
                if (key.compare(0, 9, "pst.attr.") != 0) continue;
                const std::string name = key.substr(9);
                if (name == "Name" || name == "BondLength" || name == "LineWidth" ||
                    name == "CaptionSize" || name == "CaptionFont" || name == "LabelSize" ||
                    name == "LabelFont") continue;
                if (written.count(name)) continue;
                attribute(out, name.c_str(), entries[i].value);
                if (writer.fidelity) ++writer.fidelity->unknownAttributesEmitted;
                written.insert(name);
            }
        }
        out << ">\n";
        if (!writer.fontNames.empty()) {
            out << "<fonttable>";
            for (size_t i = 0; i < writer.fontNames.size(); ++i) {
                std::string escaped;
                escapeAttribute(writer.fontNames[i], &escaped);
                out << "<font id=\"" << writer.fontIdByName[writer.fontNames[i]]
                    << "\" charset=\"utf-8\" name=\"" << escaped << "\"/>";
            }
            out << "</fonttable>\n";
        }
    }

    // Re-emit in the SOURCE's order when the reader recorded one, so a file whose
    // nodes and bonds are interleaved comes back interleaved. Objects drawn in this
    // session have no recorded order and sort after every loaded object, in id order.
    {
        std::vector<std::pair<long long, core::ObjectRef> > keyed;
        keyed.reserve(objectOrder.size());
        for (size_t i = 0; i < objectOrder.size(); ++i) {
            const std::string* recorded =
                document.extensions(objectOrder[i]).findValue(
                    std::string(kExtensionPrefix) + "source.order");
            long long position = 0;
            if (recorded && !recorded->empty()) {
                std::istringstream in(*recorded);
                in.imbue(std::locale::classic());
                if (!(in >> position)) position = 0;
            } else {
                position = 1000000000LL + static_cast<long long>(objectOrder[i].id);
            }
            keyed.push_back(std::make_pair(position, objectOrder[i]));
        }
        std::stable_sort(keyed.begin(), keyed.end());
        objectOrder.clear();
        for (size_t i = 0; i < keyed.size(); ++i) objectOrder.push_back(keyed[i].second);
    }

    // ---- pages and fragments --------------------------------------------
    // A container is opened only when the document actually has more than one
    // page or more than one fragment; otherwise the output keeps the flat, single
    // page/fragment shape the simple fixtures use, which is what makes the
    // no-edit byte comparison exact for the common case.
    // Containers come from the source when available; otherwise a fresh page and
    // fragment are opened. The container ids are NOT taken from the object id pool,
    // because they are container identity, not object identity.
    if (!(writer.options.mode == CdxmlExportMode::Lossless && prologue && !prologue->empty())) {
        out << "<page id=\"" << writer.reserveFileId() << "\">\n";
        out << "<fragment id=\"" << writer.reserveFileId() << "\">\n";
    }
    long long rawCursor = 0;
    for (size_t i = 0; i < objectOrder.size(); ++i) {
        const ObjectRef& ref = objectOrder[i];
        bool preservedEmitted = false;
        // Emit every preserved raw region that comes BEFORE this object in the source,
        // so an unknown <graphic> stays between the same neighbours it had.
        {
            const std::string* recorded =
                document.extensions(ref).findValue(std::string(kExtensionPrefix) + "source.order");
            long long position = 1000000000LL;
            if (recorded && !recorded->empty()) {
                std::istringstream in(*recorded);
                in.imbue(std::locale::classic());
                in >> position;
            }
            emitRawRegionsInRange(writer, rawCursor, position);
            rawCursor = position;
        }
        switch (ref.kind) {
        case ObjectKind::Atom: {
            const core::AtomRecord* atom = document.atoms().get(ref.id);
            if (!atom) break;
            if (emitPreservedIfUnchanged(writer, ref, atomSignature(document, *atom))) { preservedEmitted = true; break; }
            emitAtom(writer, *atom);
            break;
        }
        case ObjectKind::Bond: {
            const core::BondRecord* bond = document.bonds().get(ref.id);
            if (!bond) break;
            if (emitPreservedIfUnchanged(writer, ref, bondSignature(*bond))) { preservedEmitted = true; break; }
            emitBond(writer, *bond);
            break;
        }
        case ObjectKind::Text: {
            const core::TextRecord* text = document.texts().get(ref.id);
            if (!text) break;
            if (emitPreservedIfUnchanged(writer, ref, textSignature(document, *text))) { preservedEmitted = true; break; }
            emitText(writer, *text);
            break;
        }
        case ObjectKind::Path: {
            const core::PathRecord* path = document.paths().get(ref.id);
            if (!path) break;
            if (emitPreservedIfUnchanged(writer, ref, pathSignature(document, *path))) {
                preservedEmitted = true;
                break;
            }
            emitPath(writer, *path);
            break;
        }
        case ObjectKind::Arrow: {
            const core::AnnotationRecord* a = document.arrows().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::Bracket: {
            const core::AnnotationRecord* a = document.brackets().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::Orbital: {
            const core::AnnotationRecord* a = document.orbitals().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::Symbol: {
            const core::AnnotationRecord* a = document.symbols().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::BioShape: {
            const core::AnnotationRecord* a = document.bioShapes().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        default:
            // A slab the model can hold but this writer cannot lay out. That is a
            // LOSS, and it is refused rather than silently omitted.
            if (writer.options.failOnLossyEntities)
                return fail(std::string("cannot write object of kind ") +
                            core::kindName(ref.kind), ErrorCode::NotImplemented);
            if (writer.refusals) writer.refuse(core::kindName(ref.kind),
                                               "object kind is not writable", 0, ref);
            break;
        }
        // A PRESERVED region already contains the whitespace that followed the
        // previous object, so no separator is added; a REGENERATED object gets one
        // because its generator does not emit trailing whitespace.
        if (!preservedEmitted) out << "\n";
    }
    // Any raw region recorded AFTER the last modelled object (for example an unknown
    // element that came last in the source) still has to be written. Regions that sit
    // between modelled objects were already emitted by the interleaving above, so this
    // final flush only covers the tail - running it twice would duplicate them.
    emitRawRegionsInRange(writer, rawCursor, 1000000000LL);
    // The epilogue is the source's own closing bytes when available, so the trailing
    // whitespace is the file's, not the generator's. Regenerating it is what turns a
    // 2-byte difference into a "not identical" verdict.
    const std::string* epilogue =
        document.documentExtensions().findValue(std::string(kExtensionPrefix) + "source.epilogue");
    if (writer.options.mode == CdxmlExportMode::Lossless && epilogue && !epilogue->empty())
        out << *epilogue;
    else
        out << "\n</fragment>\n</page>\n</CDXML>\n";
    return Error::success();
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------
core::Error writeCdxml(const core::Document& document, std::ostream& output,
                       RefusalReport* refusals, CdxmlWriteFidelity* fidelity,
                       const CdxmlWriteOptions& options) {
    RefusalReport localRefusals;
    CdxmlWriteFidelity localFidelity;
    Writer writer(document, options, refusals ? refusals : &localRefusals,
                  fidelity ? fidelity : &localFidelity);

    Error valid = document.validate();
    if (!valid.ok()) return valid;
    Error planned = planIds(writer);
    if (!planned.ok()) return planned;
    Error written = writeDocument(writer);
    if (!written.ok()) return written;

    const std::string text = writer.out.str();
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) return Error(ErrorCode::IoError, "cannot write CDXML output");
    return Error::success();
}

std::string writeCdxmlToString(const core::Document& document, RefusalReport* refusals,
                               CdxmlWriteFidelity* fidelity,
                               const CdxmlWriteOptions& options) {
    std::ostringstream out;
    Error r = writeCdxml(document, out, refusals, fidelity, options);
    if (!r.ok()) return std::string();
    return out.str();
}

core::Error writeCdxml(const core::Document& document, const std::string& path,
                       RefusalReport* refusals, CdxmlWriteFidelity* fidelity,
                       const CdxmlWriteOptions& options) {
    // The destination is never the file the content came from, and the replace is
    // atomic: a failure at any point leaves the previous file intact.
    std::string bytes = writeCdxmlToString(document, refusals, fidelity, options);
    if (bytes.empty()) return Error(ErrorCode::IoError, "CDXML serialization produced no bytes");
    AtomicWriteOptions atomic;
    atomic.sourcePath = document.sourcePath();
    atomic.verifyAfterWrite = true;
    return writeFileAtomic(path, bytes, atomic, 0);
}

core::Error planCdxmlWrite(const core::Document& document, CdxmlFidelityPlan* plan,
                           const CdxmlWriteOptions& options) {
    if (!plan) return Error(ErrorCode::InvalidArgument, "null fidelity plan");
    *plan = CdxmlFidelityPlan();
    CdxmlWriteFidelity fidelity;
    RefusalReport refusals;
    std::ostringstream sink;
    Error r = writeCdxml(document, sink, &refusals, &fidelity, options);
    if (!r.ok()) {
        plan->objectsRefused = refusals.size() ? refusals.size() : 1;
        for (size_t i = 0; i < refusals.items.size(); ++i)
            plan->losses.push_back(refusals.items[i].summaryLine());
        if (refusals.items.empty()) plan->losses.push_back(r.message);
        return r;
    }
    plan->objectsPreserved = fidelity.objectsFromSource;
    plan->objectsRegenerated = fidelity.objectsRegenerated;
    plan->objectsRefused = refusals.size();
    for (size_t i = 0; i < refusals.items.size(); ++i)
        plan->losses.push_back(refusals.items[i].summaryLine());
    return Error::success();
}

} // namespace persistence
} // namespace r62

