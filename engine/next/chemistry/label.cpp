// =============================================================================
// re_round62_viewer/next/chemistry/label.cpp
//
// T06 - typed atom label implementation.
//
// The single most important property of this file: there is no code path that
// turns an unrecognised string into carbon.  If text is not an element, not an
// injected abbreviation, not an R group and not a query token, it becomes
// LabelKind::Arbitrary and the text is kept byte-for-byte.
// =============================================================================

#include "next/chemistry/label.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace r62 {
namespace chemistry {
namespace {

bool isDigit(char c) { return c >= 48 && c <= 57; }

std::string intToStr(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

// R / R1 / R2 ...   Also accepts a bare "R".
bool parseRGroup(const std::string& s, int* index) {
    if (s.empty() || s[0] != 82) return false;
    if (s.size() == 1) { if (index) *index = 0; return true; }
    for (size_t i = 1; i < s.size(); ++i)
        if (!isDigit(s[i])) return false;
    if (index) *index = std::atoi(s.c_str() + 1);
    return true;
}

// Query tokens the official Query toolbar exposes as label-ish atoms.  Only the
// ones that are actually ATOM LABELS are here; free-site and stereo markers are
// NOT atoms and must not become one by accident.
const char* kQueryTokens[] = { "A", "Q", "X", "M", "L", "Xm", "Ak", "R#" };
const int kQueryTokenCount = static_cast<int>(sizeof(kQueryTokens) / sizeof(char*));

bool isQueryToken(const std::string& s) {
    for (int i = 0; i < kQueryTokenCount; ++i)
        if (s == kQueryTokens[i]) return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
const char* labelKindName(LabelKind kind) {
    switch (kind) {
        case LabelKind::None:         return "none";
        case LabelKind::Element:      return "element";
        case LabelKind::Abbreviation: return "abbreviation";
        case LabelKind::RGroup:       return "rgroup";
        case LabelKind::Query:        return "query";
        case LabelKind::Arbitrary:    return "arbitrary";
    }
    return "?";
}

bool labelKindFromName(const std::string& name, LabelKind* out) {
    static const struct TagRow { const char* n; LabelKind k; } table[] = {
        { "none",         LabelKind::None },
        { "element",      LabelKind::Element },
        { "abbreviation", LabelKind::Abbreviation },
        { "rgroup",       LabelKind::RGroup },
        { "query",        LabelKind::Query },
        { "arbitrary",    LabelKind::Arbitrary }
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (name == table[i].n) { if (out) *out = table[i].k; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Element symbol test
// ---------------------------------------------------------------------------
bool isElementSymbol(const std::string& text, int* number) {
    const ElementInfo* e = elementInfoBySymbol(text.c_str(), static_cast<int>(text.size()));
    if (!e) return false;
    if (number) *number = static_cast<int>(e->number);
    return true;
}

// ---------------------------------------------------------------------------
// Markup stripping
// ---------------------------------------------------------------------------
// Removes tags of the form <name> and </name>.  A lone less-than sign that opens
// no tag within the next 12 characters is kept literally, because a user may
// legitimately type one in a formula.
std::string stripTags(const std::string& utf8, size_t* tagsRemoved) {
    size_t removed = 0;
    std::string out;
    out.reserve(utf8.size());
    const char LT = static_cast<char>(60);
    const char GT = static_cast<char>(62);
    const char SLASH = static_cast<char>(47);
    const char DASH = static_cast<char>(45);
    for (size_t i = 0; i < utf8.size();) {
        if (utf8[i] != LT) { out += utf8[i++]; continue; }
        const size_t close = utf8.find(GT, i + 1);
        if (close == std::string::npos || close - i > 12) { out += utf8[i++]; continue; }
        bool looksLikeTag = true;
        for (size_t k = i + 1; k < close; ++k) {
            const char c = utf8[k];
            if (c == SLASH || c == DASH) continue;
            if ((c >= 97 && c <= 122) || (c >= 65 && c <= 90) || (c >= 48 && c <= 57)) continue;
            looksLikeTag = false;
            break;
        }
        if (!looksLikeTag) { out += utf8[i++]; continue; }
        ++removed;
        i = close + 1;
    }
    if (tagsRemoved) *tagsRemoved = removed;
    return out;
}

// ---------------------------------------------------------------------------
// Interpretation
// ---------------------------------------------------------------------------
LabelInterpretation interpretLabel(const std::string& text,
                                   const std::vector<AbbreviationEntry>& abbreviations) {
    LabelInterpretation r;

    // 1. empty -> no override.
    if (text.empty()) { r.label.kind = LabelKind::None; return r; }

    // Work on the markup-free form; a label that was bold Ph is still Ph.
    size_t tags = 0;
    const std::string plain = stripTags(text, &tags);
    if (tags > 0) { r.warnings += tags; r.note = "label markup flattened"; }
    if (plain.empty()) { r.label.kind = LabelKind::None; return r; }

    // 2/3. Element symbol vs abbreviation, INCLUDING the genuine collisions.
    //
    // "Ac" is actinium (Z=89) AND the acetyl abbreviation the ChemDraw atom-hotspot
    // table binds to Shift+a.  "NO2" is not an element; "Me" is not.  The collision
    // set is small and real, so it is handled EXPLICITLY rather than by ordering:
    //
    //   RULE: when a string is BOTH an element symbol and a known abbreviation, the
    //   ABBREVIATION wins, because the atom-hotspot table is what a user typing on
    //   an atom means, and because an abbreviation label is DISPLAY-ONLY and can be
    //   expanded back.  The element reading is not lost: it remains reachable through
    //   setElementCommand(atom, Z), and a warning records the collision so a caller
    //   never has to guess which one it got.
    int element = 0;
    const bool isElement = isElementSymbol(plain, &element);
    const AbbreviationEntry* abbrev = 0;
    for (size_t i = 0; i < abbreviations.size(); ++i)
        if (abbreviations[i].text == plain) { abbrev = &abbreviations[i]; break; }

    if (abbrev && isElement) {
        r.label.kind = LabelKind::Abbreviation;
        r.label.text = plain;
        r.label.element = abbrev->element;
        r.templateId = abbrev->templateId;
        r.expansion = abbrev->expansion;
        r.element = abbrev->element;
        r.warnings += 1;
        r.note = "text is both an element symbol and an abbreviation; "
                 "the abbreviation was chosen (use an element edit for Z="
                 + intToStr(element) + ")";
        return r;
    }
    if (isElement) {
        r.label.kind = LabelKind::Element;
        r.label.element = element;
        r.label.text = plain;
        r.element = element;
        return r;
    }
    if (abbrev) {
        r.label.kind = LabelKind::Abbreviation;
        r.label.text = plain;
        r.label.element = abbrev->element;
        r.templateId = abbrev->templateId;
        r.expansion = abbrev->expansion;
        r.element = abbrev->element;
        return r;
    }

    // 4. R group.
    int rIndex = 0;
    if (parseRGroup(plain, &rIndex)) {
        r.label.kind = LabelKind::RGroup;
        r.label.text = plain;
        r.label.rGroupIndex = rIndex;
        r.label.nodeType = static_cast<uint8_t>(core::NodeType::RGroup);
        return r;
    }

    // 5. query token.
    if (isQueryToken(plain)) {
        r.label.kind = LabelKind::Query;
        r.label.text = plain;
        r.label.nodeType = static_cast<uint8_t>(core::NodeType::Query);
        return r;
    }

    // 6. PRESERVE.  This is the branch that matters: the text survives and the
    //    atom keeps its element field.  We do NOT set kind = Element and we do NOT
    //    default the element to carbon.
    r.label.kind = LabelKind::Arbitrary;
    r.label.text = plain;
    r.warnings += 1;
    r.note = r.note.empty() ? "unrecognised label preserved verbatim"
                            : r.note + "; unrecognised label preserved verbatim";
    return r;
}

LabelInterpretation interpretLabel(const std::string& text) {
    static const std::vector<AbbreviationEntry> kNone;
    return interpretLabel(text, kNone);
}

// ---------------------------------------------------------------------------
// Display text
// ---------------------------------------------------------------------------
// The element symbol is the anchor token in T09; everything else is affixed.  The
// isotope goes BEFORE the symbol and the hydrogen count and charge AFTER it, which
// is the order the ChemDraw user guide documents for a formula-style label.
std::string formatLabelText(const AtomLabel& label, bool includeHydrogens) {
    std::string core;
    switch (label.kind) {
        case LabelKind::None:
        case LabelKind::Element:
            if (label.element >= 1 && label.element <= 118)
                core = elementInfo(label.element).symbol;
            else
                core = label.text;
            if (label.isotope > 0) core = isotopePrefix(label.isotope) + core;
            if (includeHydrogens && label.hydrogenCount > 0)
                core += "H" + (label.hydrogenCount > 1 ? intToStr(label.hydrogenCount) : std::string());
            core += chargeSuffix(label.charge);
            return core;
        case LabelKind::Abbreviation:
        case LabelKind::RGroup:
        case LabelKind::Query:
        case LabelKind::Arbitrary:
            // The text IS the display.  Charge is appended because a charged
            // abbreviation (an anion of a group) is meaningful and T09 draws it.
            return label.text + chargeSuffix(label.charge);
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// Persistence encoding
// ---------------------------------------------------------------------------
namespace {

const char kEsc = 92;   // backslash
const char kSep = 59;   // semicolon
const char kEq  = 61;   // equals sign
const char kNL  = 10;
const char kCR  = 13;

std::string escapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == kEsc || c == kSep || c == kEq || c == kNL || c == kCR) out += kEsc;
        out += c;
    }
    return out;
}

std::string unescapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == kEsc && i + 1 < s.size()) { out += s[++i]; continue; }
        out += s[i];
    }
    return out;
}

} // namespace

std::string encodeLabel(const AtomLabel& label) {
    std::string out;
    out += "k="; out += labelKindName(label.kind); out += kSep;
    out += "t="; out += escapeField(label.text); out += kSep;
    out += "e="; out += intToStr(label.element); out += kSep;
    out += "c="; out += intToStr(label.charge); out += kSep;
    out += "i="; out += intToStr(label.isotope); out += kSep;
    out += "h="; out += intToStr(label.hydrogenCount); out += kSep;
    out += "r="; out += intToStr(static_cast<int>(label.radical)); out += kSep;
    out += "g="; out += intToStr(label.rGroupIndex); out += kSep;
    out += "n="; out += intToStr(static_cast<int>(label.nodeType)); out += kSep;
    out += "s="; out += (label.showLabel ? "1" : "0"); out += kSep;
    return out;
}

core::Error decodeLabel(const std::string& encoded, AtomLabel* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null label output");
    AtomLabel label;
    bool sawKind = false;
    size_t i = 0;
    while (i < encoded.size()) {
        if (encoded[i] == kSep || encoded[i] == 32) { ++i; continue; }
        const size_t eq = encoded.find(kEq, i);
        if (eq == std::string::npos)
            return core::Error(core::ErrorCode::ParseError, "label field without value");
        if (eq == i) return core::Error(core::ErrorCode::ParseError, "empty label key");
        const std::string key = encoded.substr(i, eq - i);
        // Find the field terminator, SKIPPING escaped separators.  A raw find()
        // would stop at the ';' inside an escaped value and truncate the field,
        // which is exactly the bug the round-trip test catches.
        size_t semi = eq + 1;
        while (semi < encoded.size()) {
            if (encoded[semi] == kEsc) { semi += 2; continue; }
            if (encoded[semi] == kSep) break;
            ++semi;
        }
        if (semi > encoded.size()) semi = encoded.size();
        const std::string raw = encoded.substr(eq + 1,
            (semi >= encoded.size() ? encoded.size() : semi) - (eq + 1));
        if (key == "k") {
            if (!labelKindFromName(raw, &label.kind))
                return core::Error(core::ErrorCode::ParseError, "unknown label kind tag");
            sawKind = true;
        } else if (key == "t") {
            label.text = unescapeField(raw);
        } else if (key == "e") {
            label.element = std::atoi(raw.c_str());
        } else if (key == "c") {
            label.charge = std::atoi(raw.c_str());
        } else if (key == "i") {
            label.isotope = std::atoi(raw.c_str());
        } else if (key == "h") {
            label.hydrogenCount = std::atoi(raw.c_str());
        } else if (key == "r") {
            label.radical = static_cast<uint8_t>(std::atoi(raw.c_str()));
        } else if (key == "g") {
            label.rGroupIndex = std::atoi(raw.c_str());
        } else if (key == "n") {
            label.nodeType = static_cast<uint8_t>(std::atoi(raw.c_str()));
        } else if (key == "s") {
            label.showLabel = (raw != "0");
        }
        // Unknown keys are ignored so an older reader tolerates a newer writer
        // adding a field; the KIND is the one field that is mandatory, because
        // defaulting it would change what the atom IS.
        if (semi == std::string::npos) break;
        i = semi + 1;
    }
    if (!sawKind) return core::Error(core::ErrorCode::ParseError, "label has no kind tag");
    if (label.charge < -15 || label.charge > 15)
        return core::Error(core::ErrorCode::OutOfRange, "label charge out of range");
    if (label.isotope < 0 || label.isotope > 400)
        return core::Error(core::ErrorCode::OutOfRange, "label isotope out of range");
    *out = label;
    return core::Error();
}

} // namespace chemistry
} // namespace r62
