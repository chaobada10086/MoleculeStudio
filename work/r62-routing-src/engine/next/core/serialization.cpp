#include "serialization.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

namespace r62 {
namespace core {

namespace {

// ---------------------------------------------------------------------------
// Hex encoding. Used for every string so a record can never contain a newline,
// a space or a NUL, and so the format stays byte-safe for arbitrary UTF-8.
// ---------------------------------------------------------------------------
const char* const kHexDigits = "0123456789abcdef";

inline int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parser for one line. Small and allocation-light; every failure produces an
// Error instead of an exception so the public API never throws for bad input.
class Line {
public:
    Line() : ok_(true) {}
    explicit Line(const std::string& text) : stream_(text), ok_(true) {
        stream_.imbue(std::locale::classic());
    }
    bool ok() const { return ok_; }
    void fail() { ok_ = false; }

    std::string word() {
        std::string value;
        if (!(stream_ >> value)) ok_ = false;
        return value;
    }
    long long integer() {
        long long value = 0;
        if (!(stream_ >> value)) ok_ = false;
        return value;
    }
    unsigned long long unsignedInteger() {
        unsigned long long value = 0;
        if (!(stream_ >> value)) ok_ = false;
        return value;
    }
    double number() {
        double value = 0.0;
        if (!(stream_ >> value)) ok_ = false;
        return value;
    }
    bool boolean() { return integer() != 0; }
    bool atEnd() {
        stream_ >> std::ws;
        return stream_.eof();
    }
    const std::string& remainder() {
        stream_ >> std::ws;
        remainder_.clear();
        std::getline(stream_, remainder_);
        return remainder_;
    }
private:
    std::istringstream stream_;
    bool               ok_;
    std::string        remainder_;
};

Error parseError(const std::string& message) {
    return Error(ErrorCode::ParseError, message);
}

void writeString(std::ostream& out, const std::string& value) {
    if (value.empty()) { out << "-"; return; }
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        out << kHexDigits[c >> 4] << kHexDigits[c & 15];
    }
}

Error readString(Line& line, std::string* out) {
    const std::string token = line.word();
    if (!line.ok()) return parseError("Truncated string field");
    return hexDecode(token, out);
}

// Ordinal used for the annotation slabs so one record type covers all five.
ObjectKind annotationKind(int ordinal) {
    switch (ordinal) {
    case 0: return ObjectKind::Arrow;
    case 1: return ObjectKind::Bracket;
    case 2: return ObjectKind::Orbital;
    case 3: return ObjectKind::Symbol;
    case 4: return ObjectKind::BioShape;
    default: return ObjectKind::Unknown;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Hex helpers (public)
// ---------------------------------------------------------------------------
std::string hexEncode(const std::string& value) {
    if (value.empty()) return "-";
    std::string result;
    result.reserve(value.size() * 2);
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        result += kHexDigits[c >> 4];
        result += kHexDigits[c & 15];
    }
    return result;
}

Error hexDecode(const std::string& value, std::string* out) {
    out->clear();
    if (value == "-") return Error::success();
    if (value.size() % 2 != 0) return parseError("Hex field has an odd length");
    out->reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int hi = hexValue(value[i]);
        const int lo = hexValue(value[i + 1]);
        if (hi < 0 || lo < 0) return parseError("Hex field contains a non-hex character");
        out->push_back(static_cast<char>((hi << 4) | lo));
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------
Error saveDocument(const Document& document, std::ostream& output,
                   const SaveOptions& options) {
    Error valid = document.validate();
    if (!valid.ok()) return valid;

    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    output << kNativeMagic << ' ' << kNativeFormatVersion << ' '
           << document.id() << ' ' << document.revision() << ' '
           << document.structureSerial() << '\n';
    output << "D " << document.defaultBondLength() << ' '
           << document.defaultLineWidth() << ' '
           << document.defaultTextSize() << ' ';
    writeString(output, document.defaultFont());
    output << ' ';
    writeString(output, document.name());
    output << ' ';
    writeString(output, document.sourcePath());
    output << '\n';

    // --- atoms -------------------------------------------------------------
    {
        const std::vector<AtomRecord>& records = document.atoms().records();
        for (size_t i = 0; i < records.size(); ++i) {
            const AtomRecord& a = records[i];
            if (!a.isAlive() && !options.includeDeadSlots) continue;
            output << "A " << a.id << ' ' << a.x() << ' ' << a.y() << ' '
                   << a.element << ' ' << static_cast<int>(a.alive) << ' '
                   << static_cast<int>(a.semantics.charge) << ' '
                   << static_cast<int>(a.semantics.radical) << ' '
                   << a.semantics.isotope << ' '
                   << static_cast<int>(a.semantics.implicitHydrogens) << ' '
                   << static_cast<int>(a.semantics.nodeType) << ' '
                   << static_cast<int>(a.semantics.showLabel) << ' '
                   << static_cast<int>(a.semantics.aromatic) << ' '
                   << a.sourceId << ' ';
            writeString(output, document.atomLabel(a.id));
            output << '\n';
        }
    }

    // --- bonds -------------------------------------------------------------
    {
        const std::vector<BondRecord>& records = document.bonds().records();
        for (size_t i = 0; i < records.size(); ++i) {
            const BondRecord& b = records[i];
            if (!b.isAlive() && !options.includeDeadSlots) continue;
            output << "B " << b.id << ' ' << b.beginAtom << ' ' << b.endAtom << ' '
                   << static_cast<int>(b.order) << ' '
                   << static_cast<int>(b.display) << ' '
                   << static_cast<int>(b.semantics) << ' '
                   << static_cast<int>(b.stereo) << ' '
                   << static_cast<int>(b.alive) << ' ' << b.sourceId << '\n';
        }
    }

    // --- texts (plain text lives in the runs table) -------------------------
    {
        const std::vector<TextRecord>& records = document.texts().records();
        for (size_t i = 0; i < records.size(); ++i) {
            const TextRecord& t = records[i];
            if (!t.isAlive() && !options.includeDeadSlots) continue;
            output << "T " << t.id << ' ' << t.x() << ' ' << t.y() << ' '
                   << t.size << ' ' << static_cast<int>(t.role) << ' '
                   << static_cast<int>(t.alive) << ' ' << t.sourceId << ' '
                   << t.format.flags << ' ' << static_cast<int>(t.format.align) << ' '
                   << t.format.colorRGBA << ' ' << t.format.size << ' ';
            writeString(output, t.format.fontName);
            output << '\n';
        }
    }

    // --- styled text runs ---------------------------------------------------
    {
        const std::vector<TextRecord>& records = document.texts().records();
        for (size_t i = 0; i < records.size(); ++i) {
            const TextRecord& t = records[i];
            if (!t.isAlive() && !options.includeDeadSlots) continue;
            const std::vector<TextRun>& runs = document.runs(t.id);
            output << "R " << t.id << ' ' << runs.size();
            for (size_t k = 0; k < runs.size(); ++k) {
                output << ' ' << runs[k].flags << ' ' << runs[k].size << ' ';
                writeString(output, runs[k].fontName);
                output << ' ';
                writeString(output, runs[k].utf8);
            }
            output << '\n';
        }
    }

    // --- paths ---------------------------------------------------------------
    {
        const std::vector<PathRecord>& records = document.paths().records();
        for (size_t i = 0; i < records.size(); ++i) {
            const PathRecord& p = records[i];
            if (!p.isAlive() && !options.includeDeadSlots) continue;
            output << "P " << p.id << ' ' << static_cast<int>(p.alive) << ' '
                   << static_cast<int>(p.closed) << ' '
                   << static_cast<int>(p.fillEnabled) << ' '
                   << p.fillRGBA << ' ' << p.sourceId << ' '
                   << p.style.width << ' ' << p.style.colorRGBA << ' '
                   << static_cast<int>(p.style.dash) << ' '
                   << p.points.size();
            for (size_t k = 0; k < p.points.size(); ++k) {
                const unsigned kind = k < p.pointKind.size() ? p.pointKind[k] : 0u;
                output << ' ' << p.points[k].x << ' ' << p.points[k].y << ' ' << kind;
            }
            output << '\n';
        }
    }

    // --- annotations ----------------------------------------------------------
    {
        for (int ordinal = 0; ordinal < 5; ++ordinal) {
            const ObjectSlab<AnnotationRecord>& slab =
                ordinal == 0 ? document.arrows() :
                ordinal == 1 ? document.brackets() :
                ordinal == 2 ? document.orbitals() :
                ordinal == 3 ? document.symbols() : document.bioShapes();
            const std::vector<AnnotationRecord>& records = slab.records();
            for (size_t i = 0; i < records.size(); ++i) {
                const AnnotationRecord& a = records[i];
                if (!a.isAlive() && !options.includeDeadSlots) continue;
                output << "N " << ordinal << ' ' << a.id << ' '
                       << static_cast<int>(a.alive) << ' ' << a.presetId << ' '
                       << a.scale << ' ' << a.rotation << ' ' << a.fillRGBA << ' '
                       << a.sourceId << ' ' << a.style.width << ' '
                       << a.style.colorRGBA << ' ' << static_cast<int>(a.style.dash);
                for (int k = 0; k < 4; ++k) output << ' ' << a.anchors[k];
                for (int k = 0; k < 4; ++k)
                    output << ' ' << a.geometry[k].x << ' ' << a.geometry[k].y;
                output << '\n';
            }
        }
    }

    // --- groups ---------------------------------------------------------------
    {
        const std::vector<GroupRecord>& records = document.groups().records();
        for (size_t i = 0; i < records.size(); ++i) {
            const GroupRecord& g = records[i];
            if (!g.isAlive() && !options.includeDeadSlots) continue;
            output << "G " << g.id << ' ' << static_cast<int>(g.alive) << ' '
                   << g.zIndex << ' ' << g.sourceId << ' '
                   << g.members.size() << ' ';
            writeString(output, g.label);
            for (size_t k = 0; k < g.members.size(); ++k)
                output << ' ' << static_cast<int>(g.members[k].kind) << ' '
                       << g.members[k].id;
            output << '\n';
        }
    }

    // --- extensions -----------------------------------------------------------
    if (options.includeExtensionPayload) {
        const std::vector<ExtensionEntry>& documentEntries =
            document.documentExtensions().entries();
        for (size_t i = 0; i < documentEntries.size(); ++i) {
            output << "X doc 0 0 " << documentEntries[i].sourceId << ' ';
            writeString(output, documentEntries[i].key);
            output << ' ';
            writeString(output, documentEntries[i].value);
            output << '\n';
        }
        const std::map<ObjectRef, ExtensionBag>& bags = document.allObjectExtensions();
        for (std::map<ObjectRef, ExtensionBag>::const_iterator it = bags.begin();
             it != bags.end(); ++it) {
            const std::vector<ExtensionEntry>& entries = it->second.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                output << "X obj " << static_cast<int>(it->first.kind) << ' '
                       << it->first.id << ' ' << entries[i].sourceId << ' ';
                writeString(output, entries[i].key);
                output << ' ';
                writeString(output, entries[i].value);
                output << '\n';
            }
        }
    }

    output << "E\n";
    if (!output) return Error(ErrorCode::IoError, "Cannot write the document stream");
    return Error::success();
}

std::string serializeToString(const Document& document, const SaveOptions& options) {
    std::ostringstream out;
    Error r = saveDocument(document, out, options);
    if (!r.ok()) return std::string();
    return out.str();
}

Error saveDocument(const Document& document, const std::string& path,
                   const SaveOptions& options) {
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output) return Error(ErrorCode::IoError, "Cannot open the output file: " + path);
    return saveDocument(document, output, options);
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
Error loadDocument(Document& document, std::istream& input, LoadReport* report) {
    input.imbue(std::locale::classic());
    std::string header;
    if (!std::getline(input, header)) return Error(ErrorCode::Truncated, "Empty document");
    if (!header.empty() && header[header.size() - 1] == '\r') header.erase(header.size() - 1);

    // Match the FULL magic string, not a prefix slice: comparing the first six
    // characters of "CDXEDIT 1" against the seven-character magic silently fails
    // and the file is then reported as an unknown format.
    const size_t legacyMagicLength = std::strlen(kLegacyMagic);
    if (header.size() >= legacyMagicLength &&
        header.compare(0, legacyMagicLength, kLegacyMagic) == 0) {
        // A stream cannot be rewound, so the remaining lines are appended to the
        // header we already consumed and handed to the legacy reader as one
        // buffer. The path overload avoids this copy entirely.
        std::string rest;
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            rest += line;
            rest += '\n';
        }
        std::istringstream combined(header + "\n" + rest);
        return loadLegacyCdxedit(document, combined, report);
    }

    Line headerLine(header);
    const std::string magic = headerLine.word();
    if (magic != kNativeMagic)
        return parseError("Unknown document magic: " + magic);
    const long long version = headerLine.integer();
    if (!headerLine.ok()) return parseError("Malformed document header");
    if (version > kNativeFormatVersion)
        return Error(ErrorCode::Unsupported,
                     "Document format version is newer than this build supports");
    if (version < 1) return parseError("Document format version must be >= 1");
    const unsigned long long documentId = headerLine.unsignedInteger();
    headerLine.integer();  // revision is informational on load
    headerLine.integer();  // structure serial is informational on load
    if (!headerLine.ok()) return parseError("Malformed document header");

    Document::RawState state;
    std::map<ObjectRef, ExtensionBag> extensions;
    ExtensionBag documentExtensions;
    std::map< ObjectId, std::vector<TextRun> > runs;
    std::map< ObjectId, std::string > labels;
    bool sawDefaults = false;
    bool sawTerminator = false;
    uint64_t extensionCount = 0;
    uint64_t atomCount = 0, bondCount = 0, textCount = 0;

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        Line fields(line);
        const std::string kind = fields.word();
        if (!fields.ok()) return parseError("Malformed record");

        if (kind == "D") {
            state.documentDefaults.bondLength = fields.number();
            state.documentDefaults.lineWidth = fields.number();
            state.documentDefaults.textSize = fields.number();
            Error r = readString(fields, &state.documentDefaults.font);
            if (!r.ok()) return r;
            std::string name;
            r = readString(fields, &name);
            if (!r.ok()) return r;
            std::string sourcePath;
            r = readString(fields, &sourcePath);
            if (!r.ok()) return r;
            document.setName(name);
            document.setSourcePath(sourcePath);
            sawDefaults = true;
        } else if (kind == "A") {
            AtomRecord a;
            const unsigned long long id = fields.unsignedInteger();
            a.id = static_cast<ObjectId>(id);
            a.position.x = fields.number();
            a.position.y = fields.number();
            a.element = static_cast<int16_t>(fields.integer());
            a.alive = static_cast<uint8_t>(fields.integer() != 0);
            a.semantics.charge = static_cast<int8_t>(fields.integer());
            a.semantics.radical = static_cast<uint8_t>(fields.integer());
            a.semantics.isotope = static_cast<int16_t>(fields.integer());
            a.semantics.implicitHydrogens = static_cast<int8_t>(fields.integer());
            a.semantics.nodeType = static_cast<uint8_t>(fields.integer());
            a.semantics.showLabel = fields.integer() != 0;
            a.semantics.aromatic = fields.integer() != 0;
            a.sourceId = static_cast<int32_t>(fields.integer());
            std::string label;
            Error r = readString(fields, &label);
            if (!r.ok()) return r;
            if (!fields.ok()) return parseError("Malformed atom record");
            if (a.id >= state.atoms.size()) state.atoms.resize(a.id + 1);
            state.atoms[a.id] = a;
            state.atoms[a.id].id = a.id;
            if (!label.empty()) labels[a.id] = label;
            ++atomCount;
        } else if (kind == "B") {
            BondRecord b;
            b.id = static_cast<ObjectId>(fields.unsignedInteger());
            b.beginAtom = static_cast<ObjectId>(fields.unsignedInteger());
            b.endAtom = static_cast<ObjectId>(fields.unsignedInteger());
            b.order = static_cast<BondOrder>(fields.integer());
            b.display = static_cast<BondDisplay>(fields.integer());
            b.semantics = static_cast<BondSemantics>(fields.integer());
            b.stereo = static_cast<BondStereo>(fields.integer());
            b.alive = static_cast<uint8_t>(fields.integer() != 0);
            b.sourceId = static_cast<int32_t>(fields.integer());
            if (!fields.ok()) return parseError("Malformed bond record");
            if (b.id >= state.bonds.size()) state.bonds.resize(b.id + 1);
            state.bonds[b.id] = b;
            state.bonds[b.id].id = b.id;
            ++bondCount;
        } else if (kind == "T") {
            TextRecord t;
            t.id = static_cast<ObjectId>(fields.unsignedInteger());
            t.position.x = fields.number();
            t.position.y = fields.number();
            t.size = fields.number();
            t.role = static_cast<TextRole>(fields.integer());
            t.alive = static_cast<uint8_t>(fields.integer() != 0);
            t.sourceId = static_cast<int32_t>(fields.integer());
            t.format.flags = static_cast<uint16_t>(fields.integer());
            t.format.align = static_cast<uint8_t>(fields.integer());
            t.format.colorRGBA = static_cast<uint32_t>(fields.unsignedInteger());
            t.format.size = fields.number();
            Error r = readString(fields, &t.format.fontName);
            if (!r.ok()) return r;
            if (!fields.ok()) return parseError("Malformed text record");
            if (t.id >= state.texts.size()) state.texts.resize(t.id + 1);
            state.texts[t.id] = t;
            state.texts[t.id].id = t.id;
            ++textCount;
        } else if (kind == "R") {
            const ObjectId id = static_cast<ObjectId>(fields.unsignedInteger());
            const long long count = fields.integer();
            if (!fields.ok() || count < 0 || count > 4096)
                return parseError("Malformed text run record");
            std::vector<TextRun> runList;
            runList.reserve(static_cast<size_t>(count));
            for (long long k = 0; k < count; ++k) {
                TextRun run;
                run.flags = static_cast<uint16_t>(fields.integer());
                run.size = fields.number();
                Error r = readString(fields, &run.fontName);
                if (!r.ok()) return r;
                r = readString(fields, &run.utf8);
                if (!r.ok()) return r;
                runList.push_back(run);
            }
            if (!fields.ok()) return parseError("Malformed text run record");
            if (runList.empty()) runList.resize(1);
            runs[id] = runList;
        } else if (kind == "P") {
            PathRecord p;
            p.id = static_cast<ObjectId>(fields.unsignedInteger());
            p.alive = static_cast<uint8_t>(fields.integer() != 0);
            p.closed = fields.integer() != 0;
            p.fillEnabled = static_cast<uint8_t>(fields.integer());
            p.fillRGBA = static_cast<uint32_t>(fields.unsignedInteger());
            p.sourceId = static_cast<int32_t>(fields.integer());
            p.style.width = fields.number();
            p.style.colorRGBA = static_cast<uint32_t>(fields.unsignedInteger());
            p.style.dash = static_cast<uint8_t>(fields.integer());
            const long long count = fields.integer();
            if (!fields.ok() || count < 0 || count > 1000000)
                return parseError("Malformed path record");
            for (long long k = 0; k < count; ++k) {
                Vec2 point;
                point.x = fields.number();
                point.y = fields.number();
                const unsigned kindValue = static_cast<unsigned>(fields.integer());
                p.points.push_back(point);
                p.pointKind.push_back(static_cast<uint8_t>(kindValue));
            }
            if (!fields.ok()) return parseError("Malformed path record");
            if (p.id >= state.paths.size()) state.paths.resize(p.id + 1);
            state.paths[p.id] = p;
            state.paths[p.id].id = p.id;
        } else if (kind == "N") {
            const int ordinal = static_cast<int>(fields.integer());
            const ObjectKind objectKind = annotationKind(ordinal);
            if (objectKind == ObjectKind::Unknown)
                return parseError("Unknown annotation slab ordinal");
            AnnotationRecord a;
            a.kind = objectKind;
            a.id = static_cast<ObjectId>(fields.unsignedInteger());
            a.alive = static_cast<uint8_t>(fields.integer() != 0);
            a.presetId = static_cast<uint32_t>(fields.unsignedInteger());
            a.scale = fields.number();
            a.rotation = fields.number();
            a.fillRGBA = static_cast<uint32_t>(fields.unsignedInteger());
            a.sourceId = static_cast<int32_t>(fields.integer());
            a.style.width = fields.number();
            a.style.colorRGBA = static_cast<uint32_t>(fields.unsignedInteger());
            a.style.dash = static_cast<uint8_t>(fields.integer());
            for (int k = 0; k < 4; ++k)
                a.anchors[k] = static_cast<ObjectId>(fields.unsignedInteger());
            for (int k = 0; k < 4; ++k) {
                a.geometry[k].x = fields.number();
                a.geometry[k].y = fields.number();
            }
            if (!fields.ok()) return parseError("Malformed annotation record");
            std::vector<AnnotationRecord>& slab = state.annotations[ordinal];
            if (a.id >= slab.size()) slab.resize(a.id + 1);
            slab[a.id] = a;
            slab[a.id].id = a.id;
        } else if (kind == "G") {
            GroupRecord g;
            g.id = static_cast<ObjectId>(fields.unsignedInteger());
            g.alive = static_cast<uint8_t>(fields.integer() != 0);
            g.zIndex = static_cast<int32_t>(fields.integer());
            g.sourceId = static_cast<int32_t>(fields.integer());
            const long long memberCount = fields.integer();
            Error r = readString(fields, &g.label);
            if (!r.ok()) return r;
            if (!fields.ok() || memberCount < 0 || memberCount > 1000000)
                return parseError("Malformed group record");
            for (long long k = 0; k < memberCount; ++k) {
                const int memberKind = static_cast<int>(fields.integer());
                const ObjectId memberId = static_cast<ObjectId>(fields.unsignedInteger());
                g.members.push_back(ObjectRef(static_cast<ObjectKind>(memberKind), memberId));
            }
            if (!fields.ok()) return parseError("Malformed group record");
            if (g.id >= state.groups.size()) state.groups.resize(g.id + 1);
            state.groups[g.id] = g;
            state.groups[g.id].id = g.id;
        } else if (kind == "X") {
            const std::string scope = fields.word();
            const int objectKind = static_cast<int>(fields.integer());
            const ObjectId objectId = static_cast<ObjectId>(fields.unsignedInteger());
            const int32_t sourceId = static_cast<int32_t>(fields.integer());
            std::string key, value;
            Error r = readString(fields, &key);
            if (!r.ok()) return r;
            r = readString(fields, &value);
            if (!r.ok()) return r;
            if (!fields.ok()) return parseError("Malformed extension record");
            if (scope == "doc") {
                documentExtensions.set(key, value, sourceId);
            } else if (scope == "obj") {
                const ObjectKind k = static_cast<ObjectKind>(objectKind);
                if (k == ObjectKind::Unknown) return parseError("Unknown extension object kind");
                extensions[ObjectRef(k, objectId)].set(key, value, sourceId);
            } else {
                return parseError("Unknown extension scope: " + scope);
            }
            ++extensionCount;
        } else if (kind == "E") {
            sawTerminator = true;
            break;
        } else {
            return parseError("Unknown record type: " + kind);
        }
    }

    if (!sawDefaults) return parseError("Document has no D (defaults) record");
    if (!sawTerminator) return Error(ErrorCode::Truncated, "Document has no E terminator");

    state.extensions = extensions;
    state.documentExtensions = documentExtensions;
    state.runs = runs;
    Error restored = document.restoreRaw(state);
    if (!restored.ok()) return restored;
    for (std::map< ObjectId, std::string >::const_iterator it = labels.begin();
         it != labels.end(); ++it)
        document.setAtomLabelQuiet(it->first, it->second);
    (void)documentId;

    if (report) {
        report->formatVersion = static_cast<int>(version);
        report->migratedFromLegacy = false;
        report->atomsRead = atomCount;
        report->bondsRead = bondCount;
        report->textsRead = textCount;
        report->extensionsRead = extensionCount;
        report->sourceMagic = kNativeMagic;
    }
    return Error::success();
}

Error loadDocument(Document& document, const std::string& path, LoadReport* report) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) return Error(ErrorCode::IoError, "Cannot open the document: " + path);
    return loadDocument(document, input, report);
}

// ---------------------------------------------------------------------------
// Format detection
// ---------------------------------------------------------------------------
Error detectFormat(const std::string& bytes, LoadReport* report) {
    const size_t legacyLength = std::strlen(kLegacyMagic);
    const size_t nativeLength = std::strlen(kNativeMagic);
    if (bytes.size() >= legacyLength && bytes.compare(0, legacyLength, kLegacyMagic) == 0) {
        if (report) report->sourceMagic = kLegacyMagic;
        return Error::success();
    }
    if (bytes.size() >= nativeLength && bytes.compare(0, nativeLength, kNativeMagic) == 0) {
        size_t space = bytes.find(' ');
        if (space == std::string::npos)
            return parseError("Native document header has no version field");
        size_t end = bytes.find_first_of(" \r\n", space + 1);
        const std::string version = bytes.substr(space + 1, end == std::string::npos
                                                 ? std::string::npos : end - space - 1);
        std::istringstream in(version);
        in.imbue(std::locale::classic());
        int value = 0;
        if (!(in >> value)) return parseError("Native document version is not a number");
        if (value > kNativeFormatVersion)
            return Error(ErrorCode::Unsupported, "Document is newer than this build");
        if (report) {
            report->sourceMagic = kNativeMagic;
            report->formatVersion = value;
        }
        return Error::success();
    }
    return parseError("Unrecognised document format");
}

// ---------------------------------------------------------------------------
// Legacy CDXEDIT 1 migration
// ---------------------------------------------------------------------------
Error loadLegacyCdxedit(Document& document, std::istream& input, LoadReport* report) {
    input.imbue(std::locale::classic());
    std::string header;
    if (!std::getline(input, header))
        return Error(ErrorCode::Truncated, "Empty legacy document");
    if (!header.empty() && header[header.size() - 1] == '\r') header.erase(header.size() - 1);
    if (header != "CDXEDIT 1")
        return Error(ErrorCode::Unsupported,
                     "Only legacy format version 1 is supported, got: " + header);

    Document::RawState state;
    ExtensionBag preservedUnknowns;
    bool sawWidth = false;
    uint64_t atomCount = 0, bondCount = 0, textCount = 0;

    // The legacy writer emitted A records for ONE atom, B for ONE bond and T for
    // ONE text, each assigned the next id via push_back. We append in the same
    // order through the same id-allocation rule, which is why the ids survive.
    std::vector<AtomRecord> atoms;
    std::vector<BondRecord> bonds;
    std::vector<TextRecord> texts;
    std::map< ObjectId, std::vector<TextRun> > runs;
    size_t nextAtomId = 0, nextBondId = 0, nextTextId = 0;

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        Line fields(line);
        const std::string kind = fields.word();
        if (!fields.ok()) continue;
        if (kind == "W") {
            state.documentDefaults.lineWidth = fields.number();
            sawWidth = true;
        } else if (kind == "A") {
            const unsigned long long fileId = fields.unsignedInteger();
            AtomRecord a;
            a.position.x = fields.number();
            a.position.y = fields.number();
            a.element = static_cast<int16_t>(fields.integer());
            const long long alive = fields.integer();
            a.alive = static_cast<uint8_t>(alive != 0);
            a.sourceId = 0;
            if (!fields.ok()) return parseError("Malformed legacy A record");
            // Legacy ids were dense and equal to the slot index; a file that
            // disagrees is rejected rather than silently renumbered, because the
            // whole point of migration is that ids survive.
            if (fileId != nextAtomId)
                return Error(ErrorCode::Unsupported,
                             "Legacy atom ids are not dense in file order");
            a.id = static_cast<ObjectId>(fileId);
            if (a.id < atoms.size() && !atoms[a.id].isAlive() && a.isAlive()) {
                // trailing-dead-run reuse, reproduced exactly
                atoms[a.id] = a;
            } else {
                atoms.push_back(a);
            }
            ++nextAtomId;
            ++atomCount;
        } else if (kind == "B") {
            const unsigned long long fileId = fields.unsignedInteger();
            BondRecord b;
            b.beginAtom = static_cast<ObjectId>(fields.unsignedInteger());
            b.endAtom = static_cast<ObjectId>(fields.unsignedInteger());
            b.order = static_cast<BondOrder>(fields.integer());
            const long long alive = fields.integer();
            b.alive = static_cast<uint8_t>(alive != 0);
            b.display = BondDisplay::Normal;
            b.semantics = BondSemantics::Normal;
            b.stereo = BondStereo::None;
            b.sourceId = 0;
            if (!fields.ok()) return parseError("Malformed legacy B record");
            if (fileId != nextBondId)
                return Error(ErrorCode::Unsupported, "Legacy bond ids are not dense in file order");
            b.id = static_cast<ObjectId>(fileId);
            if (b.id < bonds.size() && !bonds[b.id].isAlive() && b.isAlive()) {
                bonds[b.id] = b;
            } else {
                bonds.push_back(b);
            }
            ++nextBondId;
            ++bondCount;
        } else if (kind == "T") {
            const unsigned long long fileId = fields.unsignedInteger();
            TextRecord t;
            t.position.x = fields.number();
            t.position.y = fields.number();
            t.size = fields.number();
            const long long alive = fields.integer();
            t.alive = static_cast<uint8_t>(alive != 0);
            t.role = TextRole::Caption;
            t.sourceId = 0;
            std::string encoded = fields.word();
            std::string utf8;
            Error decoded = hexDecode(encoded, &utf8);
            if (!decoded.ok()) return decoded;
            if (!fields.ok()) return parseError("Malformed legacy T record");
            Error validText = validateUtf8(utf8);
            if (!validText.ok()) return validText;
            if (fileId != nextTextId)
                return Error(ErrorCode::Unsupported, "Legacy text ids are not dense in file order");
            t.id = static_cast<ObjectId>(fileId);
            if (t.id < texts.size() && !texts[t.id].isAlive() && t.isAlive()) {
                texts[t.id] = t;
            } else {
                texts.push_back(t);
            }
            std::vector<TextRun> runList(1);
            runList[0].utf8 = utf8;
            runs[t.id] = runList;
            ++nextTextId;
            ++textCount;
        } else {
            // Unknown legacy record: preserved, never silently dropped. This is
            // the extension container doing the job it exists for.
            preservedUnknowns.set("legacy.record." + kind, line);
        }
    }

    if (!sawWidth) return parseError("Legacy document has no W (line width) record");
    state.atoms = atoms;
    state.bonds = bonds;
    state.texts = texts;
    state.runs = runs;
    state.documentExtensions = preservedUnknowns;
    Error restored = document.restoreRaw(state);
    if (!restored.ok()) return restored;
    document.setName("(migrated from CDXEDIT 1)");

    if (report) {
        report->formatVersion = kFormatVersionLegacy1;
        report->migratedFromLegacy = true;
        report->atomsRead = atomCount;
        report->bondsRead = bondCount;
        report->textsRead = textCount;
        report->extensionsRead = preservedUnknowns.size();
        report->sourceMagic = kLegacyMagic;
    }
    return Error::success();
}

Error loadLegacyCdxedit(Document& document, const std::string& path, LoadReport* report) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) return Error(ErrorCode::IoError, "Cannot open the legacy document: " + path);
    return loadLegacyCdxedit(document, input, report);
}

} // namespace core
} // namespace r62
