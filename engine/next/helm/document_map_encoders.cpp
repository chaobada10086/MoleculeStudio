// =============================================================================
// re_round62_viewer/next/helm/document_map.cpp
//
// T15 - SequenceModel <-> r62::core::Document. The id contract and the encoding
// are documented in document_map.hpp.
//
// The encoders are written so that a malformed record is DETECTED. A decoder
// returns false and a reason rather than producing a half-configured monomer,
// because a silently half-read sequence is worse than a refused load.
// =============================================================================

#include "next/helm/document_map.hpp"

#include <cstdio>
#include <map>
#include <sstream>

namespace r62 {
namespace helm {

const char* const kKeyDocumentHeader   = "user.t15.sequence.header";
const char* const kKeyMonomerRecord    = "user.t15.monomer.record";
const char* const kKeyConnectionRecord = "user.t15.connection.record";
const char* const kKeyPolymerRecord    = "user.t15.polymer.record";

MapReport::MapReport()
    : atomsCreated(0), bondsCreated(0), polymersCreated(0), unknownMonomers(0),
      blobMonomers(0), hydrogenBonds(0) {}

namespace {

// Hex encoding of a possibly-empty UTF-8 string. Reused from the approach the T01
// native format uses so a record can never contain a delimiter or a newline.
std::string hexEncode(const std::string& value) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() * 2);
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        out.push_back(digits[(c >> 4) & 0xF]);
        out.push_back(digits[c & 0xF]);
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hexDecode(const std::string& value, std::string* out) {
    if (value.size() % 2 != 0) return false;
    out->clear();
    out->reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int hi = hexDigit(value[i]);
        const int lo = hexDigit(value[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out->push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}


bool parseU64(const std::string& text, unsigned long long* out) {
    if (text.empty()) return false;
    for (size_t i = 0; i < text.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    *out = std::strtoull(text.c_str(), 0, 10);
    return true;
}

// Split 'k=v;k=v' into a map. A field without '=' is a malformed record.
bool parseFields(const std::string& text, std::map<std::string, std::string>* out) {
    size_t cursor = 0;
    while (cursor <= text.size()) {
        const size_t semi = text.find(';', cursor);
        const bool last = (semi == std::string::npos);
        const std::string field = last ? text.substr(cursor) : text.substr(cursor, semi - cursor);
        if (!field.empty()) {
            const size_t eq = field.find('=');
            if (eq == std::string::npos) return false;
            (*out)[field.substr(0, eq)] = field.substr(eq + 1);
        }
        if (last) break;
        cursor = semi + 1;
    }
    return true;
}

std::string fieldOf(const std::map<std::string, std::string>& fields, const std::string& key) {
    std::map<std::string, std::string>::const_iterator it = fields.find(key);
    return it == fields.end() ? std::string() : it->second;
}

// The atom LABEL a monomer maps to. This is the string T09 will measure and draw.

// The element a monomer node is stored with. It is the document's placeholder for
// "a labelled node", NOT a claim about the monomer's chemistry: the chemical
// identity is in the extension record and, when known, in the SMILES. Carbon is
// used because it is the neutral default the T01 model already uses for a node.

} // namespace

// ----------------------------------------------------------------------------
// Monomer record
// ----------------------------------------------------------------------------
std::string encodeMonomerRecord(const MonomerInstance& instance) {
    // The 1-argument form writes NO polymer order or position. It exists so a caller
    // that only wants the definition (a test, a diff) can encode one, and a reader
    // treats the absent field as "unknown" rather than as zero.
    return encodeMonomerRecord(instance, kInvalidOrder, kInvalidOrder);
}

std::string encodeMonomerRecord(const MonomerInstance& instance, size_t polymerOrder,
                                size_t position) {
    const MonomerDefinition& d = instance.definition;
    std::ostringstream out;
    out << "v=" << kDocumentMapVersion;
    if (polymerOrder != kInvalidOrder) out << ";p=" << polymerOrder;
    if (position != kInvalidOrder) out << ";i=" << position;
    out << ";pv=" << instance.dataVersion;
    out << ";t=" << polymerTypeName(d.polymerType);
    out << ";r=" << monomerRoleName(d.role);
    out << ";s=" << hexEncode(d.symbol);
    out << ";a=" << hexEncode(d.naturalAnalog);
    out << ";n=" << hexEncode(d.name);
    out << ";c=" << instance.copyIndex;
    out << ";st=" << hexEncode(instance.sourceText);
    out << ";k=" << (d.chemistryDefined ? 1 : 0);
    out << ";sm=" << hexEncode(d.smiles);
    out << ";li=" << hexEncode(d.license);
    out << ";so=" << hexEncode(d.source);
    out << ";at=";
    for (size_t i = 0; i < d.attachments.size(); ++i) {
        if (i != 0) out << '|';
        out << hexEncode(d.attachments[i].label) << ','
            << hexEncode(d.attachments[i].capGroupName) << ','
            << hexEncode(d.attachments[i].capGroupSmiles);
    }
    return out.str();
}

bool decodeMonomerRecord(const std::string& text, MonomerInstance* out, std::string* reason) {
    return decodeMonomerRecord(text, out, 0, 0, reason);
}

bool decodeMonomerRecord(const std::string& text, MonomerInstance* out,
                         size_t* polymerOrder, size_t* position, std::string* reason) {
    if (polymerOrder) *polymerOrder = kInvalidOrder;
    if (position) *position = kInvalidOrder;
    if (!out) return false;
    std::map<std::string, std::string> fields;
    if (!parseFields(text, &fields)) {
        if (reason) *reason = "malformed monomer record: a field has no '='";
        return false;
    }
    unsigned long long version = 0;
    if (!parseU64(fieldOf(fields, "v"), &version)) {
        if (reason) *reason = "malformed monomer record: missing or invalid version";
        return false;
    }
    if (version > kDocumentMapVersion) {
        if (reason) *reason = "monomer record version is newer than this build can read";
        return false;
    }
    unsigned long long orderField = 0;
    if (polymerOrder && parseU64(fieldOf(fields, "p"), &orderField))
        *polymerOrder = static_cast<size_t>(orderField);
    if (position && parseU64(fieldOf(fields, "i"), &orderField))
        *position = static_cast<size_t>(orderField);
    MonomerInstance instance;
    unsigned long long v = 0;
    if (parseU64(fieldOf(fields, "pv"), &v)) instance.dataVersion = static_cast<int>(v);
    PolymerType type = PolymerType::Unknown;
    if (!polymerTypeFromToken(fieldOf(fields, "t"), &type)) {
        // polymerTypeName writes the lowercase stable token, not the HELM token.
        if (!polymerTypeFromName(fieldOf(fields, "t"), &type)) {
            if (reason) *reason = "malformed monomer record: unknown polymer type token";
            return false;
        }
    }
    MonomerRole role = MonomerRole::Unknown;
    if (!monomerRoleFromName(fieldOf(fields, "r"), &role)) {
        if (reason) *reason = "malformed monomer record: unknown monomer role";
        return false;
    }
    MonomerDefinition definition;
    definition.polymerType = type;
    definition.role = role;
    if (!hexDecode(fieldOf(fields, "s"), &definition.symbol)) {
        if (reason) *reason = "malformed monomer record: symbol is not hex";
        return false;
    }
    if (!hexDecode(fieldOf(fields, "a"), &definition.naturalAnalog)) {
        if (reason) *reason = "malformed monomer record: natural analog is not hex";
        return false;
    }
    if (!hexDecode(fieldOf(fields, "n"), &definition.name)) {
        if (reason) *reason = "malformed monomer record: name is not hex";
        return false;
    }
    if (!hexDecode(fieldOf(fields, "st"), &instance.sourceText)) {
        if (reason) *reason = "malformed monomer record: source text is not hex";
        return false;
    }
    if (!hexDecode(fieldOf(fields, "sm"), &definition.smiles)) {
        if (reason) *reason = "malformed monomer record: smiles is not hex";
        return false;
    }
    if (!hexDecode(fieldOf(fields, "li"), &definition.license)) {
        if (reason) *reason = "malformed monomer record: licence is not hex";
        return false;
    }
    if (!hexDecode(fieldOf(fields, "so"), &definition.source)) {
        if (reason) *reason = "malformed monomer record: source is not hex";
        return false;
    }
    // chemistryDefined is read from the record, but it is CROSS-CHECKED against the
    // structure. A record that claims chemistry without a structure is refused, so
    // a hand-edited or truncated file cannot make this module assert a structure it
    // does not have.
    unsigned long long known = 0;
    parseU64(fieldOf(fields, "k"), &known);
    definition.chemistryDefined = (known != 0);
    if (definition.chemistryDefined && definition.smiles.empty()) {
        if (reason) *reason = "monomer record claims chemistry but states no structure";
        return false;
    }
    if (definition.polymerType == PolymerType::Blob && definition.chemistryDefined) {
        if (reason) *reason = "monomer record claims a BLOB has a chemistry definition";
        return false;
    }
    definition.natural = !definition.naturalAnalog.empty();
    // Attachment points.
    const std::string attachments = fieldOf(fields, "at");
    if (!attachments.empty()) {
        size_t cursor = 0;
        while (cursor <= attachments.size()) {
            const size_t bar = attachments.find('|', cursor);
            const bool last = (bar == std::string::npos);
            const std::string triple = last ? attachments.substr(cursor)
                                            : attachments.substr(cursor, bar - cursor);
            if (!triple.empty()) {
                const size_t c1 = triple.find(',');
                const size_t c2 = c1 == std::string::npos ? std::string::npos
                                                           : triple.find(',', c1 + 1);
                if (c1 == std::string::npos || c2 == std::string::npos) {
                    if (reason) *reason = "malformed monomer record: attachment triple";
                    return false;
                }
                AttachmentPoint point;
                if (!hexDecode(triple.substr(0, c1), &point.label) ||
                    !hexDecode(triple.substr(c1 + 1, c2 - c1 - 1), &point.capGroupName) ||
                    !hexDecode(triple.substr(c2 + 1), &point.capGroupSmiles)) {
                    if (reason) *reason = "malformed monomer record: attachment is not hex";
                    return false;
                }
                if (!isValidAttachmentLabel(point.label)) {
                    if (reason) *reason = "malformed monomer record: invalid attachment label";
                    return false;
                }
                definition.attachments.push_back(point);
            }
            if (last) break;
            cursor = bar + 1;
        }
    }
    unsigned long long copyIndex = 0;
    if (parseU64(fieldOf(fields, "c"), &copyIndex))
        instance.copyIndex = static_cast<int>(copyIndex);
    instance.definition = definition;
    *out = instance;
    return true;
}

// ----------------------------------------------------------------------------
// Connection record
// ----------------------------------------------------------------------------
std::string encodeConnectionRecord(const SequenceConnection& connection) {
    std::ostringstream out;
    out << "v=" << kDocumentMapVersion;
    out << ";id=" << connection.id;
    out << ";k=" << connectionKindName(connection.kind);
    out << ";fp=" << connection.from.polymer;
    out << ";fi=" << connection.from.position;
    out << ";fa=" << hexEncode(connection.from.attachment);
    out << ";tp=" << connection.to.polymer;
    out << ";ti=" << connection.to.position;
    out << ";ta=" << hexEncode(connection.to.attachment);
    out << ";an=" << hexEncode(connection.annotation);
    return out.str();
}

bool decodeConnectionRecord(const std::string& text, SequenceConnection* out, std::string* reason) {
    if (!out) return false;
    std::map<std::string, std::string> fields;
    if (!parseFields(text, &fields)) {
        if (reason) *reason = "malformed connection record: a field has no '='";
        return false;
    }
    unsigned long long version = 0;
    if (!parseU64(fieldOf(fields, "v"), &version) || version > kDocumentMapVersion) {
        if (reason) *reason = "connection record version is missing or newer than this build";
        return false;
    }
    SequenceConnection connection;
    unsigned long long value = 0;
    if (!parseU64(fieldOf(fields, "id"), &value)) {
        if (reason) *reason = "malformed connection record: missing id";
        return false;
    }
    connection.id = static_cast<ConnectionId>(value);
    const std::string kind = fieldOf(fields, "k");
    if (kind == "hydrogen") connection.kind = ConnectionKind::Hydrogen;
    else if (kind == "generic") connection.kind = ConnectionKind::Generic;
    else if (kind == "covalent") connection.kind = ConnectionKind::Covalent;
    else {
        if (reason) *reason = "malformed connection record: unknown connection kind";
        return false;
    }
    if (!parseU64(fieldOf(fields, "fp"), &value)) {
        if (reason) *reason = "malformed connection record: missing source polymer";
        return false;
    }
    connection.from.polymer = static_cast<size_t>(value);
    if (!parseU64(fieldOf(fields, "fi"), &value)) {
        if (reason) *reason = "malformed connection record: missing source position";
        return false;
    }
    connection.from.position = static_cast<size_t>(value);
    if (!parseU64(fieldOf(fields, "tp"), &value)) {
        if (reason) *reason = "malformed connection record: missing target polymer";
        return false;
    }
    connection.to.polymer = static_cast<size_t>(value);
    if (!parseU64(fieldOf(fields, "ti"), &value)) {
        if (reason) *reason = "malformed connection record: missing target position";
        return false;
    }
    connection.to.position = static_cast<size_t>(value);
    if (!hexDecode(fieldOf(fields, "fa"), &connection.from.attachment) ||
        !hexDecode(fieldOf(fields, "ta"), &connection.to.attachment) ||
        !hexDecode(fieldOf(fields, "an"), &connection.annotation)) {
        if (reason) *reason = "malformed connection record: a text field is not hex";
        return false;
    }
    if (connection.kind != ConnectionKind::Generic) {
        if (!isValidAttachmentLabel(connection.from.attachment) ||
            !isValidAttachmentLabel(connection.to.attachment)) {
            if (reason) *reason = "malformed connection record: invalid attachment label";
            return false;
        }
    }
    connection.alive = true;
    *out = connection;
    return true;
}

// ----------------------------------------------------------------------------
// Polymer record
// ----------------------------------------------------------------------------
std::string encodePolymerRecord(const SequencePolymer& polymer) {
    return encodePolymerRecord(polymer, kInvalidOrder);
}

std::string encodePolymerRecord(const SequencePolymer& polymer, size_t polymerOrder) {
    std::ostringstream out;
    out << "v=" << kDocumentMapVersion;
    if (polymerOrder != kInvalidOrder) out << ";o=" << polymerOrder;
    out << ";id=" << hexEncode(polymer.polymerId);
    out << ";t=" << polymerTypeName(polymer.type);
    out << ";d=" << sequenceDisplayModeName(polymer.display);
    out << ";st=" << hexEncode(polymer.strandType);
    return out.str();
}

bool decodePolymerRecord(const std::string& text, SequencePolymer* out, std::string* reason) {
    return decodePolymerRecord(text, out, 0, reason);
}

bool decodePolymerRecord(const std::string& text, SequencePolymer* out, size_t* polymerOrder,
                         std::string* reason) {
    if (polymerOrder) *polymerOrder = kInvalidOrder;
    if (!out) return false;
    std::map<std::string, std::string> fields;
    if (!parseFields(text, &fields)) {
        if (reason) *reason = "malformed polymer record: a field has no '='";
        return false;
    }
    unsigned long long version = 0;
    if (!parseU64(fieldOf(fields, "v"), &version) || version > kDocumentMapVersion) {
        if (reason) *reason = "polymer record version is missing or newer than this build";
        return false;
    }
    unsigned long long orderField = 0;
    if (polymerOrder && parseU64(fieldOf(fields, "o"), &orderField))
        *polymerOrder = static_cast<size_t>(orderField);
    SequencePolymer polymer;
    if (!hexDecode(fieldOf(fields, "id"), &polymer.polymerId)) {
        if (reason) *reason = "malformed polymer record: id is not hex";
        return false;
    }
    PolymerType type = PolymerType::Unknown;
    if (!polymerTypeFromName(fieldOf(fields, "t"), &type)) {
        if (reason) *reason = "malformed polymer record: unknown polymer type";
        return false;
    }
    polymer.type = type;
    SequenceDisplayMode display = SequenceDisplayMode::Structure;
    if (!sequenceDisplayModeFromName(fieldOf(fields, "d"), &display)) {
        if (reason) *reason = "malformed polymer record: unknown display mode";
        return false;
    }
    polymer.display = display;
    if (!hexDecode(fieldOf(fields, "st"), &polymer.strandType)) {
        if (reason) *reason = "malformed polymer record: strand type is not hex";
        return false;
    }
    *out = polymer;
    return true;
}

} // namespace helm
} // namespace r62
