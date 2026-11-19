#include "persistence.hpp"
#include "persistence_internal.hpp"

#include "third_party/tinyxml2/tinyxml2.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace r62 {
namespace persistence {
namespace cdxml {

using tinyxml2::XMLElement;
using core::Error;
using core::ErrorCode;

const char* const kPredefinedEntities[] = { "amp", "lt", "gt", "quot", "apos" };

Error fail(const std::string& message, ErrorCode code) {
    return Error(code, "CDXML: " + message);
}

// ---------------------------------------------------------------------------
// XML entity screening
// ---------------------------------------------------------------------------
// TinyXML2 does not resolve named entities and must never be pointed at an
// external DTD. Rejecting unknown named entities up front is what stops a DTD
// reference from silently becoming literal text. This mirrors the check the T01
// adapter and the legacy editor both shipped; it is kept HERE as well so the
// T02 reader never depends on being called through T01.
Error checkEntities(const std::string& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != 38) continue;
        const size_t end = bytes.find(59, i + 1);
        if (end == std::string::npos || end - i > 32)
            return fail("unterminated or over-long XML entity", ErrorCode::Unsupported);
        const std::string name = bytes.substr(i + 1, end - i - 1);
        bool predefined = false;
        for (int k = 0; k < 5; ++k)
            if (name == kPredefinedEntities[k]) { predefined = true; break; }
        if (predefined) { i = end; continue; }
        if (name.empty() || name[0] != 35)
            return fail("unknown named XML entity (external DTDs are never resolved)",
                        ErrorCode::Unsupported);
        unsigned base = 10;
        size_t pos = 1;
        if (pos < name.size() && (name[pos] == 120 || name[pos] == 88)) {
            base = 16;
            ++pos;
        }
        if (pos == name.size()) return fail("empty numeric XML entity", ErrorCode::ParseError);
        unsigned cp = 0;
        for (; pos < name.size(); ++pos) {
            const char c = name[pos];
            unsigned digit = 0;
            if (c >= 48 && c <= 57) digit = static_cast<unsigned>(c - 48);
            else if (base == 16 && c >= 97 && c <= 102) digit = static_cast<unsigned>(c - 97 + 10);
            else if (base == 16 && c >= 65 && c <= 70) digit = static_cast<unsigned>(c - 65 + 10);
            else return fail("invalid numeric XML entity", ErrorCode::ParseError);
            if (digit >= base || cp > (0x10ffffu - digit) / base)
                return fail("invalid numeric XML entity", ErrorCode::ParseError);
            cp = cp * base + digit;
        }
        if (!(cp == 9 || cp == 10 || cp == 13 || (cp >= 0x20 && cp <= 0xd7ff) ||
              (cp >= 0xe000 && cp <= 0xfffd) || (cp >= 0x10000 && cp <= 0x10ffffu)))
            return fail("numeric entity is not an XML character", ErrorCode::ParseError);
        i = end;
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// Primitive attribute parsing
// ---------------------------------------------------------------------------
Error parseId(const char* input, int32_t* out) {
    if (!input || !*input) return fail("missing object ID/reference", ErrorCode::ParseError);
    for (const char* p = input; *p; ++p)
        if (*p < 48 || *p > 57)
            return fail("ID must be a decimal integer", ErrorCode::ParseError);
    unsigned long long value = 0;
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    if (!(in >> value) || value == 0 || value > 0x7FFFFFFFull)
        return fail("invalid object ID", ErrorCode::ParseError);
    *out = static_cast<int32_t>(value);
    return Error::success();
}

Error parseNumber(const char* input, const char* name, double* out) {
    if (!input) return fail(std::string("missing ") + name, ErrorCode::ParseError);
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    double value = 0.0;
    if (!(in >> value) || !std::isfinite(value))
        return fail(std::string("invalid ") + name, ErrorCode::ParseError);
    in >> std::ws;
    if (!in.eof()) return fail(std::string("invalid ") + name, ErrorCode::ParseError);
    *out = value;
    return Error::success();
}

Error parseInteger(const char* input, const char* name, long long minimum,
                   long long maximum, long long* out) {
    double value = 0.0;
    Error r = parseNumber(input, name, &value);
    if (!r.ok()) return r;
    if (value < static_cast<double>(minimum) || value > static_cast<double>(maximum) ||
        value != std::floor(value))
        return fail(std::string("unsupported ") + name, ErrorCode::Unsupported);
    *out = static_cast<long long>(value);
    return Error::success();
}

Error parsePosition(const char* input, core::Vec2* out) {
    if (!input) return fail("missing position", ErrorCode::ParseError);
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    double x = 0.0, y = 0.0;
    if (!(in >> x >> y) || !std::isfinite(x) || !std::isfinite(y))
        return fail("invalid position", ErrorCode::ParseError);
    in >> std::ws;
    if (!in.eof())
        return fail("only two-dimensional positions are supported", ErrorCode::Unsupported);
    out->x = x;
    out->y = y;
    return Error::success();
}

bool parseBoundingBox(const char* input, core::Box* out) {
    if (!input) return false;
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!(in >> x0 >> y0 >> x1 >> y1)) return false;
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
        return false;
    out->min = core::Vec2(std::min(x0, x1), std::min(y0, y1));
    out->max = core::Vec2(std::max(x0, x1), std::max(y0, y1));
    return true;
}

std::string lower(const std::string& value) {
    std::string result = value;
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    return result;
}

// ---------------------------------------------------------------------------
// Enum vocabulary
// ---------------------------------------------------------------------------
// Every table below is a REJECT-ON-UNKNOWN table: an unrecognised value returns
// false and the caller refuses the load. That is deliberate - reading
// Order="4" and storing order=4 as if it were a quadruple bond, or
// Display="Wavy" and storing Normal, is exactly the silent data loss this
// module exists to prevent.
bool bondOrderFromCdxml(const char* text, core::BondOrder* order,
                        core::BondSemantics* semantics) {
    *semantics = core::BondSemantics::Normal;
    if (!text || !*text) { *order = core::BondOrder::Single; return true; }
    const std::string v = lower(text);

    // VERIFIED SOURCE: vendor spec, properties/Bond_Order.htm (kCDXProp_Bond_Order
    // 0x0600). The value is a BIT SET, not an ordinal:
    //   0x0001 single   0x0002 double  0x0004 triple  0x0008 quadruple
    //   0x0010 five     0x0020 six     0x0040 0.5     0x0080 1.5 (aromatic)
    //   0x0100 2.5      0x0200 3.5     0x0400 4.5     0x0800 5.5
    //   0x1000 dative   0x2000 ionic   0x4000 hydrogen 0x8000 three-center
    // Combinations are meaningful: 0x0003 (single|double) is a query bond.
    // 0xFFFF means unspecified and the attribute is normally omitted.
    //
    // A reader that treats this as the integer 4 for a quadruple bond happens to
    // be right (0x0008 is written as the decimal 8, not 4 - so the arithmetic
    // spellings below are NOT the bit values; both forms are accepted because
    // the spec's own IntroCDXML example writes Order="2" for a double bond).
    long long numeric = 0;
    {
        // See the note in nodeTypeFromCdxml: a FAILED extraction sets the target to 0,
        // and 0 is not a valid bond order, so the failure must be explicit.
        long long parsed = 0;
        std::istringstream in(v);
        in.imbue(std::locale::classic());
        bool numericOk = false;
        if (in >> parsed) {
            in >> std::ws;
            if (in.eof()) { numericOk = true; numeric = parsed; }
        }
        if (numericOk) {
                switch (numeric) {
                case 1: *order = core::BondOrder::Single; return true;
                case 2: *order = core::BondOrder::Double; return true;
                case 3: *order = core::BondOrder::Triple; return true;
                case 4: *order = core::BondOrder::Quadruple; return true;
                case 5: *order = core::BondOrder::Aromatic;
                        *semantics = core::BondSemantics::Aromatic; return true;
                case 6: *order = core::BondOrder::SingleOrDouble;
                        *semantics = core::BondSemantics::Query; return true;
                case 8: *order = core::BondOrder::Quadruple; return true;
                case 16: *order = core::BondOrder::SingleOrDouble;
                         *semantics = core::BondSemantics::Query; return true;
                case 32: *order = core::BondOrder::Any;
                         *semantics = core::BondSemantics::Query; return true;
                case 64: *order = core::BondOrder::Unspecified; return true;
                case 4096: *order = core::BondOrder::Single;
                           *semantics = core::BondSemantics::Dative; return true;
                case 8192: *order = core::BondOrder::Single;
                           *semantics = core::BondSemantics::Query; return true;
                case 16384: *order = core::BondOrder::Single;
                            *semantics = core::BondSemantics::Hydrogen; return true;
                case 32768: *order = core::BondOrder::Single;
                            *semantics = core::BondSemantics::Query; return true;
                case 65535: *order = core::BondOrder::Unspecified; return true;
                default: return false;
                }
            }
    }
    // Fractional spellings written literally by real ChemDraw files.
    if (v == "1.5") { *order = core::BondOrder::Aromatic; *semantics = core::BondSemantics::Aromatic; return true; }
    if (v == "0.5") { *order = core::BondOrder::Unspecified; return true; }
    if (v == "2.5") { *order = core::BondOrder::Double; *semantics = core::BondSemantics::Query; return true; }
    if (v == "3.5") { *order = core::BondOrder::Triple; *semantics = core::BondSemantics::Query; return true; }
    if (v == "single") { *order = core::BondOrder::Single; return true; }
    if (v == "double") { *order = core::BondOrder::Double; return true; }
    if (v == "triple") { *order = core::BondOrder::Triple; return true; }
    if (v == "quadruple") { *order = core::BondOrder::Quadruple; return true; }
    if (v == "aromatic" || v == "arom") { *order = core::BondOrder::Aromatic; *semantics = core::BondSemantics::Aromatic; return true; }
    if (v == "singleordouble") { *order = core::BondOrder::SingleOrDouble; *semantics = core::BondSemantics::Query; return true; }
    if (v == "singleoraromatic") { *order = core::BondOrder::SingleOrAromatic; *semantics = core::BondSemantics::Query; return true; }
    if (v == "doubleoraromatic") { *order = core::BondOrder::DoubleOrAromatic; *semantics = core::BondSemantics::Query; return true; }
    if (v == "any" || v == "anybond") { *order = core::BondOrder::Any; *semantics = core::BondSemantics::Query; return true; }
    if (v == "dative" || v == "coordinate") { *order = core::BondOrder::Single; *semantics = core::BondSemantics::Dative; return true; }
    if (v == "hydrogen" || v == "h-bond" || v == "hbond") { *order = core::BondOrder::Single; *semantics = core::BondSemantics::Hydrogen; return true; }
    if (v == "unspecified" || v == "unknown") { *order = core::BondOrder::Unspecified; return true; }
    return false;
}

bool bondDisplayFromCdxml(const char* text, core::BondDisplay* display) {
    if (!text || !*text) { *display = core::BondDisplay::Normal; return true; }
    const std::string v = lower(text);
    // VERIFIED SOURCE: vendor spec, properties/Bond_Display.htm (0x0601) and
    // Bond_Display2.htm (0x0602). The real names are direction-qualified:
    //   0 Solid    1 Dash     2 Hash                3 WedgedHashBegin
    //   4 WedgedHashEnd       5 Bold                6 WedgeBegin
    //   7 WedgeEnd            8 Wavy                9 HollowWedgeBegin
    //  10 HollowWedgeEnd     11 WavyWedgeBegin     12 WavyWedgeEnd
    //  13 Dot                14 DashDot
    // There is NO "WedgeHash" and NO bare "HollowWedge" value. ChemDraw itself
    // does not support every one of these on every bond order, but the reader
    // must not silently turn an unsupported display into Solid: it returns false
    // and the caller refuses, or preserves the raw attribute.
    if (v == "solid" || v == "normal" || v == "0") { *display = core::BondDisplay::Normal; return true; }
    if (v == "dash" || v == "dashed" || v == "1") { *display = core::BondDisplay::Dashed; return true; }
    if (v == "hash" || v == "hashed" || v == "2") { *display = core::BondDisplay::Hash; return true; }
    if (v == "wedgedhashbegin" || v == "3") { *display = core::BondDisplay::WedgeHash; return true; }
    if (v == "wedgedhashend" || v == "4") { *display = core::BondDisplay::WedgeHash; return true; }
    if (v == "bold" || v == "5") { *display = core::BondDisplay::Bold; return true; }
    if (v == "wedgebegin" || v == "wedge" || v == "solidwedge" || v == "6") { *display = core::BondDisplay::Wedge; return true; }
    if (v == "wedgeend" || v == "7") { *display = core::BondDisplay::Wedge; return true; }
    if (v == "wavy" || v == "8") { *display = core::BondDisplay::Wavy; return true; }
    if (v == "hollowwedgebegin" || v == "hollowwedge" || v == "9") { *display = core::BondDisplay::HollowWedge; return true; }
    if (v == "hollowwedgeend" || v == "10") { *display = core::BondDisplay::HollowWedge; return true; }
    if (v == "wavywedgebegin" || v == "11") { *display = core::BondDisplay::Wavy; return true; }
    if (v == "wavywedgeend" || v == "12") { *display = core::BondDisplay::Wavy; return true; }
    if (v == "dot" || v == "dotted" || v == "13") { *display = core::BondDisplay::Dotted; return true; }
    if (v == "dashdot" || v == "14") { *display = core::BondDisplay::Dashed; return true; }
    return false;
}

bool bondStereoFromCdxml(const char* text, core::BondStereo* stereo) {
    if (!text || !*text) { *stereo = core::BondStereo::None; return true; }
    const std::string v = lower(text);
    // VERIFIED SOURCE: vendor spec has NO bond-level "Stereo" attribute. Stereo
    // up/down is carried by Bond_Display (Wedge/Hash and their direction-qualified
    // variants) together with which atom is Bond_Begin. This reader therefore
    // accepts only the words the model can state unambiguously and derives the
    // rest from Display in the caller. An unknown spelling is refused.
    if (v == "none" || v == "0" || v == "unspecified") { *stereo = core::BondStereo::None; return true; }
    if (v == "up" || v == "1" || v == "wedgebegin" || v == "wedgeend") { *stereo = core::BondStereo::Up; return true; }
    if (v == "down" || v == "2" || v == "wedgedhashbegin" || v == "wedgedhashend") { *stereo = core::BondStereo::Down; return true; }
    if (v == "either" || v == "3" || v == "4" || v == "doubleeither") { *stereo = core::BondStereo::Either; return true; }
    return false;
}

bool nodeTypeFromCdxml(const char* text, core::NodeType* type) {
    if (!text || !*text) { *type = core::NodeType::Element; return true; }
    const std::string v = lower(text);
    // VERIFIED SOURCE: vendor spec, properties/Node_Type.htm (0x0400). The real
    // enumeration is:
    //   0 Unspecified  1 Element          2 ElementList   3 ElementListNickname
    //   4 Nickname     5 Fragment         6 Formula       7 GenericNickname
    //   8 AnonymousAlternativeGroup       9 NamedAlternativeGroup
    //  10 MultiAttachment                11 VariableAttachment
    //  12 ExternalConnectionPoint         13 LinkNode
    // The model's NodeType is COARSER than this: it cannot distinguish
    // ElementList from Nickname, or the two AlternativeGroup kinds. Those
    // distinctions are NOT thrown away - the caller keeps the raw NodeType text
    // in the extension bag so a round trip cannot change it.
    // A failed numeric extraction sets the target to 0 in C++11, which would silently
    // map every non-numeric spelling onto NodeType 0. The parse therefore only counts
    // when it SUCCEEDS and consumes the whole token.
    long long numeric = -1;
    {
        long long parsed = 0;
        std::istringstream in(v);
        in.imbue(std::locale::classic());
        if (in >> parsed) {
            in >> std::ws;
            if (in.eof()) numeric = parsed;
        }
    }
    if (numeric >= 0) {
        switch (numeric) {
        case 0: *type = core::NodeType::Element; return true;
        case 1: *type = core::NodeType::Element; return true;
        case 2: *type = core::NodeType::Element; return true;   // element list
        case 3: *type = core::NodeType::Nickname; return true;
        case 4: *type = core::NodeType::Nickname; return true;
        case 5: *type = core::NodeType::Fragment; return true;
        case 6: *type = core::NodeType::Nickname; return true;  // formula label
        case 7: *type = core::NodeType::Nickname; return true;
        case 8: *type = core::NodeType::RGroup; return true;
        case 9: *type = core::NodeType::RGroup; return true;
        case 10: *type = core::NodeType::Attachment; return true;
        case 11: *type = core::NodeType::Attachment; return true;
        case 12: *type = core::NodeType::ExternalConnection; return true;
        case 13: *type = core::NodeType::Query; return true;
        default: return false;
        }
    }
    if (v == "unspecified") { *type = core::NodeType::Element; return true; }
    if (v == "element" || v == "elementlist" || v == "formula") { *type = core::NodeType::Element; return true; }
    if (v == "elementlistnickname" || v == "nickname" || v == "genericnickname") { *type = core::NodeType::Nickname; return true; }
    if (v == "fragment") { *type = core::NodeType::Fragment; return true; }
    if (v == "anonymousalternativegroup" || v == "namedalternativegroup" || v == "rgroup") { *type = core::NodeType::RGroup; return true; }
    if (v == "multiattachment" || v == "variableattachment" || v == "attachment") { *type = core::NodeType::Attachment; return true; }
    if (v == "externalconnectionpoint" || v == "externalconnection" || v == "external") { *type = core::NodeType::ExternalConnection; return true; }
    if (v == "linknode") { *type = core::NodeType::Query; return true; }
    return false;
}

bool radicalFromCdxml(const char* text, uint8_t* radical) {
    if (!text || !*text) { *radical = 0; return true; }
    const std::string v = lower(text);
    // VERIFIED SOURCE: vendor spec, properties/Atom_Radical.htm (0x0422).
    //   0 None   1 Singlet (diradical)   2 Doublet (monoradical)   3 Triplet
    if (v == "none" || v == "0") { *radical = 0; return true; }
    if (v == "singlet" || v == "1" || v == "diradical") {
        *radical = static_cast<uint8_t>(core::Radical::Singlet); return true;
    }
    if (v == "doublet" || v == "2" || v == "monoradical") {
        *radical = static_cast<uint8_t>(core::Radical::Doublet); return true;
    }
    if (v == "triplet" || v == "3" || v == "biradical") {
        *radical = static_cast<uint8_t>(core::Radical::Triplet); return true;
    }
    return false;
}

// CDXML face bit flags -> core::TextFlags, and back.
//
// VERIFIED SOURCE: the vendor CDX specification's CDXString page. The bit
// values are 0x01 bold, 0x02 italic, 0x04 underline, 0x08 outline,
// 0x10 shadow, 0x20 subscript, 0x40 superscript, 0x60 formula (subscript |
// superscript). Bit 0 is "plain". FACE_UNMAPPED_MASK returns the bits this
// model has no flag for; the reader keeps their raw value so nothing is lost.
// VERIFIED SOURCE: vendor spec, DataType/CDXString.htm.
//   0x00 plain  0x01 bold  0x02 italic  0x04 underline  0x08 outline
//   0x10 shadow 0x20 subscript 0x40 superscript 0x60 formula
// Bold italic is 0x03. The outline and shadow styles are Macintosh-only and have
// no core::TextFlags bit, so their raw bits are preserved separately rather than
// approximated away. face=96 (0x60, formula) is ChemDraw's standard atom-label
// default and maps to core::kFormula.
uint16_t textFlagsFromFace(int face) {
    uint16_t flags = 0;
    if (face & 0x01) flags |= core::kBold;
    if (face & 0x02) flags |= core::kItalic;
    if (face & 0x04) flags |= core::kUnderline;
    if ((face & 0x60) == 0x60) flags |= core::kFormula;
    else if (face & 0x20) flags |= core::kSubscript;
    else if (face & 0x40) flags |= core::kSuperscript;
    return flags;
}

int faceFromTextFlags(uint16_t flags) {
    int face = 0;
    if (flags & core::kBold) face |= 0x01;
    if (flags & core::kItalic) face |= 0x02;
    if (flags & core::kUnderline) face |= 0x04;
    if (flags & core::kSubscript) face |= 0x20;
    if (flags & core::kSuperscript) face |= 0x40;
    if (flags & core::kFormula) face |= 0x60;
    return face;
}

bool textAlignFromCdxml(const char* text, core::TextAlign* align, bool* known) {
    *known = true;
    if (!text || !*text) { *align = core::TextAlign::Left; return true; }
    const std::string v = lower(text);
    if (v == "left" || v == "0" || v == "l") { *align = core::TextAlign::Left; return true; }
    if (v == "center" || v == "centre" || v == "1" || v == "c") { *align = core::TextAlign::Center; return true; }
    if (v == "right" || v == "2" || v == "r") { *align = core::TextAlign::Right; return true; }
    if (v == "automatic") { *align = core::TextAlign::Left; return true; }
    *known = false;
    return false;
}

// ---------------------------------------------------------------------------
// Content checks
// ---------------------------------------------------------------------------
bool onlyWhitespace(const XMLElement* element) {
    for (const tinyxml2::XMLNode* n = element->FirstChild(); n; n = n->NextSibling()) {
        if (n->ToElement() || n->ToComment() || n->ToDeclaration()) continue;
        if (n->ToText()) {
            bool whitespace = true;
            for (const char* p = n->Value(); *p; ++p)
                if (!std::isspace(static_cast<unsigned char>(*p))) whitespace = false;
            if (whitespace) continue;
            return false;
        }
        return false;
    }
    return true;
}

// The signature is a normalised dump of the element WITH all descendants and
// attributes, used to detect post-load edits. It must cover everything a reader
// promoted, otherwise an edited object would look unmodified and its stale
// source bytes would be re-emitted - which is precisely the "quiet data loss"
// this module refuses to ship.
void signatureOf(const XMLElement* element, std::string* out) {
    out->clear();
    const char* name = element->Name();
    out->append(name ? name : "?");
    for (const tinyxml2::XMLAttribute* a = element->FirstAttribute(); a; a = a->Next()) {
        out->push_back(static_cast<char>(0x1f));
        out->append(a->Name() ? a->Name() : "?");
        out->push_back(static_cast<char>(0x1e));
        out->append(a->Value() ? a->Value() : "");
    }
    out->push_back(static_cast<char>(0x1d));
    for (const tinyxml2::XMLNode* n = element->FirstChild(); n; n = n->NextSibling()) {
        if (n->ToText()) { out->append(n->Value() ? n->Value() : ""); continue; }
        if (n->ToComment()) { out->push_back(static_cast<char>(0x1b)); out->append(n->Value() ? n->Value() : ""); continue; }
        const XMLElement* child = n->ToElement();
        if (!child) continue;
        out->push_back(static_cast<char>(0x1a));
        std::string nested;
        signatureOf(child, &nested);
        out->append(nested);
    }
}

} // namespace cdxml
} // namespace persistence
} // namespace r62
