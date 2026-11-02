#include "engine/atom_runs.hpp"
#include "persistence_internal.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace r62 {
namespace persistence {
namespace cdxml {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
// CDXML colour spellings observed in real documents and in the vendor data
// type description. The palette below is deliberately the SMALL, documented
// set; anything else is preserved as raw text by the caller rather than
// approximated to black, because "we did not understand the colour" and
// "the colour is black" must not become the same document.
namespace {

struct NamedColor {
    const char* name;
    uint32_t    rgba;
};

const NamedColor kNamedColors[] = {
    { "black",        0x000000FFu },
    { "white",        0xFFFFFFFFu },
    { "red",          0xFF0000FFu },
    { "green",        0x00A000FFu },
    { "blue",         0x0000FFFFu },
    { "cyan",         0x00FFFFFFu },
    { "magenta",      0xFF00FFFFu },
    { "yellow",       0xFFFF00FFu },
    { "darkgray",     0x404040FFu },
    { "darkgrey",     0x404040FFu },
    { "lightgray",    0xC0C0C0FFu },
    { "lightgrey",    0xC0C0C0FFu },
    { "gray",         0x808080FFu },
    { "grey",         0x808080FFu },
    { "none",         0x00000000u },
    { "",             0u }
};

bool parseSignedInteger(const std::string& text, long long* out) {
    if (text.empty()) return false;
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    long long value = 0;
    if (!(in >> value)) return false;
    in >> std::ws;
    if (!in.eof()) return false;
    *out = value;
    return true;
}

} // namespace

// CDX stores colour as a signed 32-bit integer whose bytes are, from the
// least significant one: red, green, blue, alpha. So 0x00BBGGRR on the wire.
// A CDXML document may write either that integer or a name.
bool colorFromCdxml(const char* text, uint32_t* rgba) {
    if (!text || !*text) { *rgba = 0x000000FFu; return true; }
    const std::string raw(text);
    const std::string v = lower(raw);

    for (int i = 0; kNamedColors[i].rgba != 0u || kNamedColors[i].name[0] == 0; ++i) {
        if (v == kNamedColors[i].name) { *rgba = kNamedColors[i].rgba; return true; }
        if (kNamedColors[i].name[0] == 0) break;
    }

    long long value = 0;
    if (parseSignedInteger(raw, &value)) {
        const uint32_t bits = static_cast<uint32_t>(value);
        const uint32_t red   = bits & 0xFFu;
        const uint32_t green = (bits >> 8) & 0xFFu;
        const uint32_t blue  = (bits >> 16) & 0xFFu;
        uint32_t alpha = (bits >> 24) & 0xFFu;
        if (alpha == 0) alpha = 0xFFu;   // "no alpha written" means opaque
        *rgba = (red << 24) | (green << 16) | (blue << 8) | alpha;
        return true;
    }
    if (raw.size() == 7 && raw[0] == 35) {   // #RRGGBB
        char* end = 0;
        const unsigned long parsed = std::strtoul(raw.c_str() + 1, &end, 16);
        if (end && *end == 0) {
            *rgba = (static_cast<uint32_t>(parsed) << 8) | 0xFFu;
            return true;
        }
    }
    return false;
}

std::string colorToCdxml(uint32_t rgba) {
    const uint32_t red   = (rgba >> 24) & 0xFFu;
    const uint32_t green = (rgba >> 16) & 0xFFu;
    const uint32_t blue  = (rgba >> 8) & 0xFFu;
    const uint32_t alpha = rgba & 0xFFu;
    const uint32_t bits = red | (green << 8) | (blue << 16) | (alpha << 24);
    // Written as an unsigned decimal CDX colour value, the spelling a real
    // document uses, so a reader that only understands integers still works.
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << bits;
    return out.str();
}

// ---------------------------------------------------------------------------
// Escaping
// ---------------------------------------------------------------------------
void escapeAttribute(const std::string& value, std::string* out) {
    out->clear();
    out->reserve(value.size() + 8);
    for (size_t i = 0; i < value.size(); ++i) {
        switch (value[i]) {
        case 38: out->append("&amp;"); break;
        case 60: out->append("&lt;"); break;
        case 62: out->append("&gt;"); break;
        case 34: out->append("&quot;"); break;
        case 39: out->append("&apos;"); break;
        default: out->push_back(value[i]); break;
        }
    }
}

void escapeContent(const std::string& value, std::string* out) {
    out->clear();
    out->reserve(value.size() + 8);
    for (size_t i = 0; i < value.size(); ++i) {
        switch (value[i]) {
        case 38: out->append("&amp;"); break;
        case 60: out->append("&lt;"); break;
        case 62: out->append("&gt;"); break;
        case 13: out->append("&#13;"); break;
        default: out->push_back(value[i]); break;
        }
    }
}

// ---------------------------------------------------------------------------
// Doubles
// ---------------------------------------------------------------------------
std::string numberToText(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(17);
    out << value;
    return out.str();
}

} // namespace cdxml

// ---------------------------------------------------------------------------
// Edit-detection signatures
// ---------------------------------------------------------------------------
// See persistence_internal.hpp. The formatting here MUST match what the writer
// emits; the acceptance test asserts that a load/write/reload with no edit
// produces byte-identical output, which is what makes a drift visible.
static void appendNumber(std::string* out, double value) {
    *out += cdxml::numberToText(value);
}

static void appendAtomSignature(const core::Document& document, const core::AtomRecord& atom,
                         std::string* out) {
    out->append("n");
    out->push_back('\x1f');
    appendNumber(out, atom.x());
    out->push_back(' ');
    appendNumber(out, atom.y());
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(atom.element)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(atom.semantics.charge)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(atom.semantics.isotope)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(atom.semantics.implicitHydrogens)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(atom.semantics.radical)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(atom.semantics.nodeType)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(atom.semantics.showLabel ? 1 : 0)));
    out->push_back('\x1f');
    out->append(document.atomLabel(atom.id));
    out->push_back('\x1f');
    const std::vector<core::TextRun>& runs = studioatom::runs(document,atom.id);
    for (size_t i = 0; i < runs.size(); ++i) {
        out->append(runs[i].utf8);
        out->push_back('\x1e');
        out->append(cdxml::numberToText(static_cast<double>(runs[i].flags)));
        out->push_back('\x1e');
        out->append(cdxml::numberToText(runs[i].size));
        out->push_back('\x1e');
        out->append(runs[i].fontName);
        out->push_back('\x1d');
    }
}

static void appendBondSignature(const core::BondRecord& bond, std::string* out) {
    out->append("b");
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(bond.beginAtom)));
    out->push_back(' ');
    out->append(cdxml::numberToText(static_cast<double>(bond.endAtom)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(bond.order)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(bond.display)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(bond.semantics)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(bond.stereo)));
}

static void appendTextSignature(const core::Document& document, const core::TextRecord& text,
                         std::string* out) {
    out->append("t");
    out->push_back('\x1f');
    appendNumber(out, text.x());
    out->push_back(' ');
    appendNumber(out, text.y());
    out->push_back('\x1f');
    appendNumber(out, text.size);
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(text.format.align)));
    out->push_back('\x1f');
    out->append(cdxml::numberToText(static_cast<double>(text.format.colorRGBA)));
    out->push_back('\x1f');
    const std::vector<core::TextRun>& runs = document.runs(text.id);
    for (size_t i = 0; i < runs.size(); ++i) {
        out->append(runs[i].utf8);
        out->push_back('\x1e');
        out->append(cdxml::numberToText(static_cast<double>(runs[i].flags)));
        out->push_back('\x1e');
        out->append(cdxml::numberToText(runs[i].size));
        out->push_back('\x1e');
        out->append(runs[i].fontName);
        out->push_back('\x1d');
    }
    // The wrap width changes the rendered geometry and is written as an attribute,
    // so an edited caption must stop matching its preserved source bytes.
    double width = 0.0;
    const std::string* storedWidth =
        document.extensions(core::ObjectRef(core::ObjectKind::Text, text.id)).findValue("studio.text.width");
    if (storedWidth) width = std::atof(storedWidth->c_str());
    out->push_back('\x1f');
    appendNumber(out, width);
}

// The writer freezes the signature of every object it EMITS. A later write of the
// same document with no edit produces the same signature, which is what makes the
// patch decision reproducible rather than dependent on a session variable.
std::string atomSignature(const core::Document& document, const core::AtomRecord& atom) {
    std::string out;
    appendAtomSignature(document, atom, &out);
    return out;
}
std::string bondSignature(const core::BondRecord& bond) {
    std::string out;
    appendBondSignature(bond, &out);
    return out;
}
// A path or annotation signature covers exactly the fields the writer can emit for
// that object, so an edited shape is regenerated while an untouched one is re-emitted.
std::string pathSignature(const core::Document& document, const core::PathRecord& path) {
    std::ostringstream out;
    out << "path";
    for (size_t i = 0; i < path.points.size(); ++i)
        out << ' ' << cdxml::numberToText(path.points[i].x) << ' '
            << cdxml::numberToText(path.points[i].y);
    out << '|' << (path.closed ? 1 : 0) << '|' << cdxml::numberToText(path.style.width)
        << '|' << path.style.colorRGBA;
    (void)document;
    return out.str();
}

std::string annotationSignature(const core::AnnotationRecord& annotation) {
    std::ostringstream out;
    out << "ann" << static_cast<int>(annotation.kind);
    for (int i = 0; i < 4; ++i)
        out << ' ' << cdxml::numberToText(annotation.geometry[i].x) << ' '
            << cdxml::numberToText(annotation.geometry[i].y);
    out << '|' << cdxml::numberToText(annotation.style.width) << '|'
        << annotation.style.colorRGBA << '|' << cdxml::numberToText(annotation.rotation);
    return out.str();
}

std::string textSignature(const core::Document& document, const core::TextRecord& text) {
    std::string out;
    appendTextSignature(document, text, &out);
    return out;
}

// ---------------------------------------------------------------------------
// Extension keys this module owns
// ---------------------------------------------------------------------------
// One place, so a reader and a writer cannot disagree about where a signature or
// a preserved attribute lives.
std::string signatureKeyName(core::ObjectKind kind) {
    switch (kind) {
    case core::ObjectKind::Atom: return "Atom";
    case core::ObjectKind::Bond: return "Bond";
    case core::ObjectKind::Text: return "Text";
    case core::ObjectKind::Path: return "Path";
    case core::ObjectKind::Arrow: return "Arrow";
    case core::ObjectKind::Bracket: return "Bracket";
    case core::ObjectKind::Table: return "Table";
    case core::ObjectKind::Orbital: return "Orbital";
    case core::ObjectKind::Symbol: return "Symbol";
    case core::ObjectKind::BioShape: return "BioShape";
    case core::ObjectKind::Group: return "Group";
    default: return "Unknown";
    }
}

std::string signatureKey(core::ObjectKind kind, core::ObjectId id) {
    std::ostringstream key;
    key << kExtensionPrefix << "sign." << signatureKeyName(kind) << "." << id;
    return key.str();
}

} // namespace persistence
} // namespace r62
