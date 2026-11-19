#include "next/templates_queries/polymer.hpp"

#include "next/chemistry/element_data.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace r62 {
namespace templates_queries {

const char* const kPolymerParamsKey = "user.t13.polymer.params.1";

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string intText(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

std::string doubleText(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    if (s.find(".") != std::string::npos) {
        size_t last = s.find_last_not_of("0");
        if (last != std::string::npos && s[last] == '.') --last;
        s.erase(last + 1);
    }
    return s;
}

bool parseDouble(const std::string& s, double* out) {
    if (s.empty()) return false;
    const char* c = s.c_str();
    char* end = 0;
    const double v = std::strtod(c, &end);
    if (end == c || *end != 0) return false;
    if (!core::validCoordinate(v)) return false;
    *out = v;
    return true;
}

bool parseInt(const std::string& s, int* out) {
    if (s.empty()) return false;
    const char* c = s.c_str();
    char* end = 0;
    const long v = std::strtol(c, &end, 10);
    if (end == c || *end != 0) return false;
    if (v < -2147483647L || v > 2147483647L) return false;
    *out = static_cast<int>(v);
    return true;
}

// Join/split a list of ObjectIds into a stable text form.  Stable order matters:
// the encoder is compared byte-for-byte by a round-trip test.
std::string joinIds(const std::vector<core::ObjectId>& ids) {
    std::ostringstream o;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) o << ",";
        o << ids[i];
    }
    return o.str();
}

bool splitIds(const std::string& s, std::vector<core::ObjectId>* out) {
    out->clear();
    if (s.empty()) return true;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const std::string tok = (comma == std::string::npos)
            ? s.substr(start) : s.substr(start, comma - start);
        int v = 0;
        if (!parseInt(tok, &v) || v < 0) return false;
        out->push_back(static_cast<core::ObjectId>(v));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return true;
}

std::map<std::string, std::string> parseRecord(const std::string& text) {
    std::map<std::string, std::string> kv;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t semi = text.find(';', start);
        const std::string part = (semi == std::string::npos)
            ? text.substr(start) : text.substr(start, semi - start);
        if (!part.empty()) {
            const size_t eq = part.find('=');
            if (eq == std::string::npos) {
                kv["__malformed__"] = part;   // signals ok=false to the caller
            } else {
                kv[part.substr(0, eq)] = part.substr(eq + 1);
            }
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return kv;
}

} // namespace

// ---------------------------------------------------------------------------
// Usage names and predicates
// ---------------------------------------------------------------------------
const char* bracketUsageName(BracketUsage u) {
    switch (u) {
        case BracketUsage::Unspecified:          return "unspecified";
        case BracketUsage::SRU:                  return "sru";
        case BracketUsage::Monomer:              return "monomer";
        case BracketUsage::Mer:                  return "mer";
        case BracketUsage::Component:            return "component";
        case BracketUsage::CopolymerAlternating: return "copolymer-alternating";
        case BracketUsage::CopolymerRandom:      return "copolymer-random";
        case BracketUsage::CopolymerBlock:       return "copolymer-block";
        case BracketUsage::Crosslink:            return "crosslink";
        case BracketUsage::Graft:                return "graft";
        case BracketUsage::Modification:         return "modification";
        case BracketUsage::MixtureUnordered:     return "mixture-unordered";
        case BracketUsage::MixtureOrdered:       return "mixture-ordered";
        case BracketUsage::MultipleGroup:        return "multiple-group";
        case BracketUsage::Parameterized:        return "parameterized";
    }
    return "unspecified";
}

bool bracketUsageFromName(const std::string& n, BracketUsage* out) {
    for (int i = 0; i <= 14; ++i) {
        const BracketUsage u = static_cast<BracketUsage>(i);
        if (n == bracketUsageName(u)) { *out = u; return true; }
    }
    return false;
}

const char* bracketUsageLabel(BracketUsage u) {
    switch (u) {
        case BracketUsage::Unspecified:          return "";
        case BracketUsage::SRU:                  return "n";
        case BracketUsage::Monomer:              return "mon";
        case BracketUsage::Mer:                  return "mer";
        case BracketUsage::Component:            return "c";
        case BracketUsage::CopolymerAlternating: return "alt";
        case BracketUsage::CopolymerRandom:      return "ran";
        case BracketUsage::CopolymerBlock:       return "blk";
        case BracketUsage::Crosslink:            return "xl";
        case BracketUsage::Graft:                return "grf";
        case BracketUsage::Modification:         return "mod";
        case BracketUsage::MixtureUnordered:     return "mix";
        case BracketUsage::MixtureOrdered:       return "f";
        case BracketUsage::MultipleGroup:        return "#";
        case BracketUsage::Parameterized:        return "#/# - #/MW";
    }
    return "";
}

bool bracketUsageIsStructureBased(BracketUsage u) {
    // Guide PDF 329: "You represent a structure-based polymer by enclosing a
    // portion of a structure in brackets with a bracket type of SRU (n)" and the
    // same page names Crosslink, Graft and Modification as structure-based.
    return u == BracketUsage::SRU || u == BracketUsage::Crosslink ||
           u == BracketUsage::Graft || u == BracketUsage::Modification;
}

bool bracketUsageIsSourceBased(BracketUsage u) {
    return u == BracketUsage::Monomer || u == BracketUsage::Mer ||
           u == BracketUsage::Component || u == BracketUsage::CopolymerAlternating ||
           u == BracketUsage::CopolymerRandom || u == BracketUsage::CopolymerBlock;
}

bool bracketUsageIsMixture(BracketUsage u) {
    return u == BracketUsage::MixtureUnordered || u == BracketUsage::MixtureOrdered;
}

bool bracketUsageNeedsRepeatCount(BracketUsage u) {
    return u == BracketUsage::MultipleGroup;
}

bool bracketUsageNeedsComponentOrder(BracketUsage u) {
    return u == BracketUsage::Component;
}

// ---------------------------------------------------------------------------
// Pattern / flip names
// ---------------------------------------------------------------------------
const char* repeatPatternName(RepeatPattern p) {
    switch (p) {
        case RepeatPattern::HeadToTail: return "head-to-tail";
        case RepeatPattern::HeadToHead: return "head-to-head";
        case RepeatPattern::Either:     return "either";
        case RepeatPattern::Undefined:  return "undefined";
    }
    return "undefined";
}

bool repeatPatternFromName(const std::string& n, RepeatPattern* out) {
    if (n == "head-to-tail") { *out = RepeatPattern::HeadToTail; return true; }
    if (n == "head-to-head") { *out = RepeatPattern::HeadToHead; return true; }
    if (n == "either")       { *out = RepeatPattern::Either;     return true; }
    if (n == "undefined")    { *out = RepeatPattern::Undefined;  return true; }
    return false;
}

const char* flipTypeName(FlipType f) {
    return f == FlipType::Flip ? "flip" : "no-flip";
}

bool flipTypeFromName(const std::string& n, FlipType* out) {
    if (n == "flip")    { *out = FlipType::Flip;   return true; }
    if (n == "no-flip") { *out = FlipType::NoFlip; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
core::Error validatePolymerBracket(const core::Document& document,
                                  const PolymerBracket& bracket) {
    if (bracketUsageNeedsRepeatCount(bracket.usage)) {
        if (bracket.repeatCount < 1)
            return core::Error(core::ErrorCode::InvalidArgument,
                "Multiple Group requires a repeat count >= 1 "
                "(the guide: the repeat count must be a positive integer)");
        if (bracket.repeatCount > kMaxRepeatCount)
            return core::Error(core::ErrorCode::OutOfRange,
                "repeat count above " + intText(kMaxRepeatCount) +
                "; use an SRU or parameterized label for a distribution");
    }
    if (bracketUsageNeedsComponentOrder(bracket.usage) && bracket.componentOrder < 0)
        return core::Error(core::ErrorCode::InvalidArgument,
                           "Component order must be a non-negative integer");
    if (bracket.componentOrder < 0)
        return core::Error(core::ErrorCode::InvalidArgument, "component order must be >= 0");

    if (bracket.usage == BracketUsage::Parameterized) {
        if (!bracket.weight.specified && bracket.label.empty())
            return core::Error(core::ErrorCode::InvalidArgument,
                "parameterized brackets need either a molecular weight or a label, "
                "otherwise they carry no parameter");
    }

    if (bracket.flip == FlipType::Flip && bracket.crossingBonds.size() != 2)
        return core::Error(core::ErrorCode::InvalidArgument,
            "Flip Type applies only to brackets with exactly two crossing bonds "
            "(the guide restricts it to ladder polymers)");

    if (bracketUsageIsStructureBased(bracket.usage) && bracket.crossingBonds.empty())
        return core::Error(core::ErrorCode::InvalidSemantics,
            "a structure-based usage needs at least one crossing bond: the crossing "
            "bonds are what match up across the brackets");

    if (bracket.weight.specified) {
        if (bracket.weight.mn < 0.0 || bracket.weight.mw < 0.0)
            return core::Error(core::ErrorCode::InvalidArgument, "molecular weight must be >= 0");
        if (!core::validCoordinate(bracket.weight.mn) ||
            !core::validCoordinate(bracket.weight.mw))
            return core::Error(core::ErrorCode::InvalidArgument, "molecular weight is not finite");
        if (bracket.usage != BracketUsage::MultipleGroup &&
            bracket.usage != BracketUsage::Parameterized)
            return core::Error(core::ErrorCode::InvalidSemantics,
                "the guide attaches molecular-weight display to multiple-group and "
                "parameterized brackets, not to this usage");
    }

    for (size_t i = 0; i < bracket.crossingBonds.size(); ++i) {
        const core::ObjectId b = bracket.crossingBonds[i];
        if (!document.bonds().exists(b))
            return core::Error(core::ErrorCode::NotFound, "crossing bond does not exist");
        for (size_t k = i + 1; k < bracket.crossingBonds.size(); ++k) {
            if (bracket.crossingBonds[k] == b)
                return core::Error(core::ErrorCode::AlreadyExists, "duplicate crossing bond");
        }
    }
    for (size_t i = 0; i < bracket.enclosedAtoms.size(); ++i) {
        if (!document.atoms().exists(bracket.enclosedAtoms[i]))
            return core::Error(core::ErrorCode::NotFound, "enclosed atom does not exist");
    }
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Label text
// ---------------------------------------------------------------------------
std::string polymerLabelText(const PolymerBracket& bracket) {
    // An explicit label always wins: the guide lets the user edit the SRU label to
    // any text and the component label to "c1", "c2", ....
    if (!bracket.label.empty()) return bracket.label;
    switch (bracket.usage) {
        case BracketUsage::Unspecified:          return "";
        case BracketUsage::MultipleGroup:        return intText(bracket.repeatCount);
        case BracketUsage::Parameterized:
            return "MW " + doubleText(bracket.weight.mn > 0.0 ? bracket.weight.mn
                                                              : bracket.weight.mw);
        case BracketUsage::Component:
            // "displayed as the letter c followed by an integer (or c alone if an
            //  order of zero is specified)" - guide p.329.
            return bracket.componentOrder == 0 ? std::string("c")
                                               : std::string("c") + intText(bracket.componentOrder);
        default:
            return bracketUsageLabel(bracket.usage);
    }
}

// ---------------------------------------------------------------------------
// Link nodes
// ---------------------------------------------------------------------------
// Guide p.333 grammar: optional bracket/paren/brace, optional head, "low-high", then
// the optional closing bracket.  The hyphen is REQUIRED: "[CH2]3-7", not "[CH2]3".
bool parseLinkNodeLabel(const std::string& text, LinkNode* out, std::string* reason) {
    std::string t = trim(text);
    if (t.empty()) { if (reason) *reason = "empty link node label"; return false; }

    // The guide's form is "[CH2]3-7" (head bracketed, range OUTSIDE the brackets) or
    // the bare "O1-3".  There is therefore no single "outer wrapper" to strip: the
    // bracket belongs to the HEAD.  First split off the head bracket if present, then
    // handle an optional wrapper around the whole token for the no-head case ("[4-4]").
    std::string headFromBrackets;
    bool haveHeadBrackets = false;
    if (t[0] == '[' || t[0] == '(' || t[0] == '{') {
        const char open  = t[0];
        const char close = (open == '[') ? ']' : ((open == '(') ? ')' : '}');
        const size_t end = t.find(close);
        if (end == std::string::npos) {
            if (reason) *reason = "unbalanced brackets in link node label";
            return false;
        }
        // Only treat this as the HEAD bracket when something (the range) follows the
        // closing bracket.  "[4-4]" is a wrapper around a headless range, not a head.
        const std::string after = trim(t.substr(end + 1));
        if (!after.empty()) {
            headFromBrackets = t.substr(1, end - 1);
            haveHeadBrackets = true;
            t = after;
        }
    } else if (t[t.size() - 1] == ']' || t[t.size() - 1] == ')' || t[t.size() - 1] == '}') {
        if (reason) *reason = "link node label has a closing bracket with no opening one";
        return false;
    }

    // Find the LAST hyphen that separates the range.  Taking the LAST one (rather
    // than the first) is required because a head like "CH2" ends in a DIGIT: with the
    // first hyphen, "[CH2]3-7" has head "CH" and range "2]3-7".  The guide's grammar
    // is "the LAST part of the label must be number-hyphen-number", so the range is
    // anchored at the END of the token, not at its first digit.
    const size_t dash = t.rfind('-');
    if (dash == std::string::npos) {
        if (reason) *reason = "link node label has no low-high range (the hyphen is required)";
        return false;
    }
    std::string lowText  = trim(t.substr(0, dash));
    std::string highText = trim(t.substr(dash + 1));

    // The high bound is the ENTIRE tail after the hyphen and must be all digits.
    if (highText.empty()) {
        if (reason) *reason = "link node range has no upper number";
        return false;
    }
    for (size_t i = 0; i < highText.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(highText[i]))) {
            if (reason) *reason = "link node upper bound is not an integer";
            return false;
        }
    }

    // The low bound is the run of digits at the END of lowText; the head is what
    // precedes that run.  This is what makes "CH2" a head and "3" the low bound.
    size_t start = lowText.size();
    while (start > 0 && std::isdigit(static_cast<unsigned char>(lowText[start - 1]))) --start;
    if (start == lowText.size()) {
        if (reason) *reason = "link node range has no lower number";
        return false;
    }
    std::string head = trim(lowText.substr(0, start));
    lowText = lowText.substr(start);

    // A bracketed head ("[CH2]3-7") wins over anything derived from the digits, and a
    // head found BOTH ways must agree or the label is ambiguous.
    if (haveHeadBrackets) {
        const std::string bracketed = trim(headFromBrackets);
        if (!head.empty() && head != bracketed) {
            if (reason) *reason = "link node label has a bracketed head that disagrees with its text head";
            return false;
        }
        head = bracketed;
    }

    int low = 0, high = 0;
    if (!parseInt(lowText, &low)) {
        if (reason) *reason = "link node lower bound is not an integer";
        return false;
    }
    if (!parseInt(highText, &high)) {
        if (reason) *reason = "link node upper bound is not an integer";
        return false;
    }
    if (low < 1) {
        if (reason) *reason = "link node lower bound must be >= 1";
        return false;
    }
    if (high < low) {
        if (reason) *reason = "link node upper bound is below its lower bound";
        return false;
    }
    if (high > kMaxRepeatCount) {
        if (reason) *reason = "link node range exceeds " + intText(kMaxRepeatCount);
        return false;
    }

    // Guide: "It must consist of a generic name, or no more than one element symbol."
    // A head that is neither is refused rather than stored, because it would render
    // as something the model cannot justify.
    if (!head.empty()) {
        const bool isElement = (core::elementNumber(head) > 0);
        // A "generic name" is text that is not a run of digits and contains no
        // whitespace (a nickname such as "CH2" or "R").  We accept a short
        // alphanumeric token and refuse anything with spaces or punctuation, which is
        // the checkable part of the guide rule.
        bool alnum = true;
        for (size_t i = 0; i < head.size(); ++i) {
            const char ch = head[i];
            if (!std::isalnum(static_cast<unsigned char>(ch))) { alnum = false; break; }
        }
        if (!isElement && !alnum) {
            if (reason) *reason = "link node head is neither an element symbol nor a generic name";
            return false;
        }
    }

    LinkNode node;
    node.atom = core::kInvalidObjectId;
    node.low = low;
    node.high = high;
    node.head = head;
    *out = node;
    return true;
}

std::string formatLinkNodeLabel(const LinkNode& node) {
    // The guide writes "[CH2]3-7": the head is bracketed and the range is OUTSIDE the
    // brackets, with the numbers and hyphen subscripted.  A bare range ("O1-3") has no
    // brackets, and the guide says "Brackets can be omitted, if not required for
    // clarity, for example with O1-3".  Reproducing that rule is what makes the text a
    // faithful round trip instead of a re-spelling.
    std::string body;
    if (!node.head.empty()) body += "[" + node.head + "]";
    body += intText(node.low) + "-" + intText(node.high);
    return body;
}

int connectingBondCount(const core::Document& document, core::ObjectId atom) {
    if (!document.atoms().exists(atom)) return -1;
    const std::vector<core::ObjectId>& inc = document.incidentBonds(atom);
    int n = 0;
    for (size_t i = 0; i < inc.size(); ++i)
        if (document.bonds().exists(inc[i])) ++n;
    return n;
}

core::Error validateLinkNode(const core::Document& document, const LinkNode& node) {
    if (!document.atoms().exists(node.atom))
        return core::Error(core::ErrorCode::NotFound, "link node atom does not exist");
    const int n = connectingBondCount(document, node.atom);
    if (n != 2)
        return core::Error(core::ErrorCode::InvalidSemantics,
            "a link node must have exactly two connecting bonds (the guide states this); "
            "this atom has " + intText(n));
    if (node.low < 1)
        return core::Error(core::ErrorCode::InvalidArgument, "link node low bound must be >= 1");
    if (node.high < node.low)
        return core::Error(core::ErrorCode::InvalidArgument, "link node high bound is below low");
    if (node.high > kMaxRepeatCount)
        return core::Error(core::ErrorCode::OutOfRange, "link node range exceeds the module bound");
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
std::string encodePolymerBracket(const PolymerBracket& b) {
    std::ostringstream o;
    o << "v=1";
    o << ";usage=" << bracketUsageName(b.usage);
    o << ";count=" << b.repeatCount;
    o << ";order=" << b.componentOrder;
    o << ";pattern=" << repeatPatternName(b.pattern);
    o << ";flip=" << flipTypeName(b.flip);
    if (!b.label.empty()) o << ";label=" << b.label;
    if (!b.crossingBonds.empty()) o << ";cross=" << joinIds(b.crossingBonds);
    if (!b.enclosedAtoms.empty()) o << ";atoms=" << joinIds(b.enclosedAtoms);
    if (b.weight.specified) {
        o << ";mw=1";
        o << ";mn=" << doubleText(b.weight.mn);
        o << ";mwp=" << doubleText(b.weight.mw);
    }
    return o.str();
}

PolymerBracket decodePolymerBracket(const std::string& text, bool* ok) {
    PolymerBracket b;
    if (ok) *ok = false;
    if (text.empty()) return b;

    const std::map<std::string, std::string> kv = parseRecord(text);
    if (kv.find("__malformed__") != kv.end()) return b;   // refuse, do not guess

    std::map<std::string, std::string>::const_iterator it = kv.find("v");
    if (it == kv.end() || it->second != "1") return b;    // unknown version refused

    it = kv.find("usage");
    if (it == kv.end() || !bracketUsageFromName(it->second, &b.usage)) return b;

    it = kv.find("count");
    if (it != kv.end() && !parseInt(it->second, &b.repeatCount)) return b;
    it = kv.find("order");
    if (it != kv.end() && !parseInt(it->second, &b.componentOrder)) return b;
    it = kv.find("pattern");
    if (it != kv.end() && !repeatPatternFromName(it->second, &b.pattern)) return b;
    it = kv.find("flip");
    if (it != kv.end() && !flipTypeFromName(it->second, &b.flip)) return b;

    it = kv.find("label");
    if (it != kv.end()) b.label = it->second;
    it = kv.find("cross");
    if (it != kv.end() && !splitIds(it->second, &b.crossingBonds)) return b;
    it = kv.find("atoms");
    if (it != kv.end() && !splitIds(it->second, &b.enclosedAtoms)) return b;

    it = kv.find("mw");
    if (it != kv.end() && it->second == "1") {
        b.weight.specified = true;
        it = kv.find("mn");
        if (it != kv.end() && !parseDouble(it->second, &b.weight.mn)) return b;
        it = kv.find("mwp");
        if (it != kv.end() && !parseDouble(it->second, &b.weight.mw)) return b;
    }

    if (ok) *ok = true;
    return b;
}

// ---------------------------------------------------------------------------
// Repeat-unit molecular weight
// ---------------------------------------------------------------------------
// "Average repeat-unit weight" means: sum over the enclosed atoms of the element's
// standard atomic weight, PLUS the implicit hydrogens the valence rule assigns.
// The hydrogens matter: -(CH2-CH2)- weighs 28 per unit, not 24, and omitting them
// is the difference between a plausible number and a wrong one.
//
// The function returns 0 when it cannot compute honestly (no atoms, or an atom
// whose element is unknown), and the caller must treat 0 as "not computed".
double repeatUnitWeight(const core::Document& document,
                        const std::vector<core::ObjectId>& enclosedAtoms) {
    if (enclosedAtoms.empty()) return 0.0;
    double total = 0.0;
    bool any = false;
    for (size_t i = 0; i < enclosedAtoms.size(); ++i) {
        const core::AtomRecord* a = document.atoms().alive(enclosedAtoms[i]);
        if (!a) continue;
        const chemistry::ElementInfo& info = chemistry::elementInfo(a->element);
        if (info.number == 0 || info.weight <= 0.0) return 0.0;   // unknown element
        total += info.weight;
        any = true;

        // Implicit hydrogens: use the explicit count when the model carries one,
        // otherwise derive it from the bond-order sum via the shared valence rule.
        int h = a->semantics.implicitHydrogens;
        if (h < 0) {
            int bondSum = 0;
            const std::vector<core::ObjectId>& inc = document.incidentBonds(a->id);
            for (size_t k = 0; k < inc.size(); ++k) {
                const core::BondRecord* br = document.bonds().alive(inc[k]);
                if (!br) continue;
                if (br->order == core::BondOrder::Single) bondSum += 1;
                else if (br->order == core::BondOrder::Double) bondSum += 2;
                else if (br->order == core::BondOrder::Triple) bondSum += 3;
                else if (br->order == core::BondOrder::Aromatic) bondSum += 1;
                else bondSum += 1;   // query/any: one, the conservative floor
            }
            const int val = chemistry::conventionalValenceFor(a->element, bondSum);
            h = (val >= 0 && val > bondSum) ? (val - bondSum) : 0;
        }
        if (h > 0) {
            const chemistry::ElementInfo& hy = chemistry::elementInfo(1);
            total += hy.weight * static_cast<double>(h);
        }
    }
    if (!any) return 0.0;
    return total;
}

} // namespace templates_queries
} // namespace r62
