#include "next/templates_queries/catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace r62 {
namespace templates_queries {

// ---------------------------------------------------------------------------
// Group names
// ---------------------------------------------------------------------------
const char* templateGroupName(TemplateGroup g) {
    switch (g) {
        case TemplateGroup::Ring:            return "ring";
        case TemplateGroup::FunctionalGroup: return "functional";
        case TemplateGroup::Scaffold:        return "scaffold";
        case TemplateGroup::Labelled:        return "labelled";
        case TemplateGroup::Geometry:        return "geometry";
        case TemplateGroup::Unknown:         break;
    }
    return "unknown";
}

bool templateGroupFromName(const std::string& name, TemplateGroup* out) {
    if (name == "ring")       { *out = TemplateGroup::Ring; return true; }
    if (name == "functional") { *out = TemplateGroup::FunctionalGroup; return true; }
    if (name == "scaffold")   { *out = TemplateGroup::Scaffold; return true; }
    if (name == "labelled")   { *out = TemplateGroup::Labelled; return true; }
    if (name == "geometry")   { *out = TemplateGroup::Geometry; return true; }
    // "unknown" is deliberately NOT accepted from a file: an entry with no
    // group is a file defect, not a default.
    return false;
}

// ---------------------------------------------------------------------------
// Small parsers
// ---------------------------------------------------------------------------
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(const std::string& s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i)
        r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
    return r;
}

bool parseDouble(const std::string& s, double* out) {
    if (s.empty()) return false;
    const char* c = s.c_str();
    char* end = 0;
    const double v = std::strtod(c, &end);
    if (end == c || *end != 0) return false;      // trailing junk is a refusal,
    if (!core::validCoordinate(v)) return false;  // not a silent truncation
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

// Bond order tokens.  Query orders are accepted because a QUERY template is a
// legitimate catalog entry: T01 declares them on the order axis, and Aromatic IS
// a declared BondOrder, so nothing here encodes aromatic as order == 4.
bool parseOrder(const std::string& s, core::BondOrder* out) {
    const std::string t = lower(s);
    if (t == "single" || t == "1")    { *out = core::BondOrder::Single; return true; }
    if (t == "double" || t == "2")    { *out = core::BondOrder::Double; return true; }
    if (t == "triple" || t == "3")    { *out = core::BondOrder::Triple; return true; }
    if (t == "quadruple" || t == "4") { *out = core::BondOrder::Quadruple; return true; }
    if (t == "aromatic")              { *out = core::BondOrder::Aromatic; return true; }
    if (t == "single-or-double" || t == "s/d")    { *out = core::BondOrder::SingleOrDouble; return true; }
    if (t == "single-or-aromatic" || t == "s/a")  { *out = core::BondOrder::SingleOrAromatic; return true; }
    if (t == "double-or-aromatic" || t == "d/a")  { *out = core::BondOrder::DoubleOrAromatic; return true; }
    if (t == "any")                   { *out = core::BondOrder::Any; return true; }
    return false;
}

const char* orderToken(core::BondOrder o) {
    switch (o) {
        case core::BondOrder::Single:           return "single";
        case core::BondOrder::Double:           return "double";
        case core::BondOrder::Triple:           return "triple";
        case core::BondOrder::Quadruple:        return "quadruple";
        case core::BondOrder::Aromatic:         return "aromatic";
        case core::BondOrder::SingleOrDouble:   return "single-or-double";
        case core::BondOrder::SingleOrAromatic: return "single-or-aromatic";
        case core::BondOrder::DoubleOrAromatic: return "double-or-aromatic";
        case core::BondOrder::Any:              return "any";
        case core::BondOrder::Unspecified:      break;
    }
    return "unspecified";
}

bool parseDisplay(const std::string& s, core::BondDisplay* out) {
    const std::string t = lower(s);
    if (t == "normal")       { *out = core::BondDisplay::Normal; return true; }
    if (t == "wedge")        { *out = core::BondDisplay::Wedge; return true; }
    if (t == "hash")         { *out = core::BondDisplay::Hash; return true; }
    if (t == "wedge-hash")   { *out = core::BondDisplay::WedgeHash; return true; }
    if (t == "hollow-wedge") { *out = core::BondDisplay::HollowWedge; return true; }
    if (t == "bold")         { *out = core::BondDisplay::Bold; return true; }
    if (t == "dashed")       { *out = core::BondDisplay::Dashed; return true; }
    if (t == "dotted")       { *out = core::BondDisplay::Dotted; return true; }
    if (t == "wavy")         { *out = core::BondDisplay::Wavy; return true; }
    return false;
}

const char* displayToken(core::BondDisplay d) {
    switch (d) {
        case core::BondDisplay::Normal:      return "normal";
        case core::BondDisplay::Wedge:       return "wedge";
        case core::BondDisplay::Hash:        return "hash";
        case core::BondDisplay::WedgeHash:   return "wedge-hash";
        case core::BondDisplay::HollowWedge: return "hollow-wedge";
        case core::BondDisplay::Bold:        return "bold";
        case core::BondDisplay::Dashed:      return "dashed";
        case core::BondDisplay::Dotted:      return "dotted";
        case core::BondDisplay::Wavy:        return "wavy";
    }
    return "normal";
}

bool parseSemantics(const std::string& s, core::BondSemantics* out) {
    const std::string t = lower(s);
    if (t == "normal")      { *out = core::BondSemantics::Normal; return true; }
    if (t == "aromatic")    { *out = core::BondSemantics::Aromatic; return true; }
    if (t == "dative")      { *out = core::BondSemantics::Dative; return true; }
    if (t == "hydrogen")    { *out = core::BondSemantics::Hydrogen; return true; }
    if (t == "query")       { *out = core::BondSemantics::Query; return true; }
    if (t == "unsaturated") { *out = core::BondSemantics::Unsaturated; return true; }
    return false;
}

const char* semanticsToken(core::BondSemantics s) {
    switch (s) {
        case core::BondSemantics::Normal:      return "normal";
        case core::BondSemantics::Aromatic:    return "aromatic";
        case core::BondSemantics::Dative:      return "dative";
        case core::BondSemantics::Hydrogen:    return "hydrogen";
        case core::BondSemantics::Query:       return "query";
        case core::BondSemantics::Unsaturated: return "unsaturated";
    }
    return "normal";
}

std::string fmtDouble(double v) {
    // 17 SIGNIFICANT DIGITS, not a fixed 4 decimal places.  A fixed precision is a
    // LOSSY encoder: the ring templates compute their vertex coordinates, so a value
    // such as -12.4707620100072 rounds to -12.4708 and the reloaded entry is no longer
    // equal to the shipped one.  17 significant digits is the shortest precision that
    // guarantees a double survives a text round trip (IEEE-754 binary64 needs at most
    // 17), so the documented format can express every built-in entry EXACTLY - which is
    // what the "round_trip_is_exact" acceptance check requires.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    std::string s(buf);
    // Trim trailing zeros in a fractional tail so the text stays readable and the
    // serialisation is idempotent: "14.4" stays "14.4", not "14.400000000000001".
    if (s.find('.') != std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos && s.find("inf") == std::string::npos &&
        s.find("nan") == std::string::npos) {
        size_t last = s.find_last_not_of('0');
        if (last != std::string::npos && s[last] == '.') --last;
        s.erase(last + 1);
    }
    if (s == "-0") s = "0";
    return s;
}


std::vector<std::string> split2(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

std::string intToString(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

CatalogIssue issue(int line, const std::string& id, const std::string& reason) {
    CatalogIssue i;
    i.line = line;
    i.templateId = id;
    i.reason = reason;
    return i;
}

// Validate one entry.  Returns false and fills reason when the entry cannot be
// used.  This is the single place the rules live, so the in-memory built-ins and
// a parsed file are held to exactly the same standard.
bool validateEntry(const CatalogEntry& e, std::string* reason) {
    if (e.templateId.empty()) { *reason = "entry has no template id"; return false; }
    if (e.name.empty())       { *reason = "entry has no name"; return false; }
    if (e.license.empty()) {
        *reason = "entry has no license: an untraceable asset may not be loaded";
        return false;
    }
    if (e.source.empty()) {
        *reason = "entry has no source/provenance string";
        return false;
    }
    if (e.group == TemplateGroup::Unknown) {
        *reason = "entry has no catalog group";
        return false;
    }
    if (e.atoms.empty()) { *reason = "entry has no atoms"; return false; }
    if (e.atoms.size() > 4096) {
        *reason = "entry has an implausible atom count";
        return false;
    }
    for (size_t i = 0; i < e.atoms.size(); ++i) {
        const CatalogAtom& a = e.atoms[i];
        if (a.element != 0 && !core::validElement(a.element)) {
            *reason = "atom element out of range 1..118 (0 means a labelled node)";
            return false;
        }
        if (a.element == 0 && a.labelText.empty()) {
            *reason = "atom with element 0 must carry label text";
            return false;
        }
        if (!core::validCharge(a.charge)) { *reason = "atom charge out of range"; return false; }
        if (!a.offset.finite()) { *reason = "atom offset is not finite"; return false; }
    }
    for (size_t i = 0; i < e.bonds.size(); ++i) {
        const CatalogBond& b = e.bonds[i];
        if (b.from < 0 || b.to < 0 ||
            static_cast<size_t>(b.from) >= e.atoms.size() ||
            static_cast<size_t>(b.to) >= e.atoms.size()) {
            *reason = "bond endpoint index out of range";
            return false;
        }
        if (b.from == b.to) { *reason = "bond is a self loop"; return false; }
        if (b.order == core::BondOrder::Unspecified) {
            *reason = "bond has Unspecified order; use a declared order";
            return false;
        }
        // T01 rule, enforced here so a template cannot introduce a state the
        // document model would reject at add time.
        if (b.order == core::BondOrder::Aromatic &&
            b.semantics != core::BondSemantics::Aromatic &&
            b.semantics != core::BondSemantics::Query) {
            *reason = "aromatic order requires aromatic or query semantics";
            return false;
        }
    }
    // A duplicate bond between the same unordered pair is a defect: it would make
    // Document::addBond return DuplicateBond at instantiation time, i.e. the
    // template would look fine and then fail halfway through a paste.
    for (size_t i = 0; i < e.bonds.size(); ++i) {
        for (size_t j = i + 1; j < e.bonds.size(); ++j) {
            const int a1 = e.bonds[i].from, b1 = e.bonds[i].to;
            const int a2 = e.bonds[j].from, b2 = e.bonds[j].to;
            if ((a1 == a2 && b1 == b2) || (a1 == b2 && b1 == a2)) {
                *reason = "duplicate bond between the same atom pair";
                return false;
            }
        }
    }
    if (e.anchors.empty()) {
        *reason = "entry has no anchor: a template must declare where it attaches";
        return false;
    }
    std::vector<int> ranks;
    for (size_t i = 0; i < e.anchors.size(); ++i) {
        if (e.anchors[i].atomIndex < 0 ||
            static_cast<size_t>(e.anchors[i].atomIndex) >= e.atoms.size()) {
            *reason = "anchor atom index out of range";
            return false;
        }
        if (e.anchors[i].rank < 1 || e.anchors[i].rank > 99) {
            *reason = "anchor rank must be 1..99";
            return false;
        }
        for (size_t k = 0; k < ranks.size(); ++k) {
            if (ranks[k] == e.anchors[i].rank) {
                *reason = "two anchors share a rank; attachment order undefined";
                return false;
            }
        }
        ranks.push_back(e.anchors[i].rank);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
core::Error TemplateRegistry::add(const CatalogEntry& entry) {
    std::string reason;
    if (!validateEntry(entry, &reason))
        return core::Error(core::ErrorCode::InvalidArgument, reason);
    if (find(entry.templateId) != 0)
        return core::Error(core::ErrorCode::AlreadyExists,
                           "template id already registered: " + entry.templateId);
    entries_.push_back(entry);
    return core::Error::success();
}

void TemplateRegistry::addQuiet(const CatalogEntry& entry) {
    // The built-in table is validated once by a test rather than at every call.
    entries_.push_back(entry);
}

const CatalogEntry* TemplateRegistry::find(const std::string& templateId) const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].templateId == templateId) return &entries_[i];
    return 0;
}

std::vector<std::string> TemplateRegistry::idsInGroup(TemplateGroup group) const {
    std::vector<std::string> out;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (group == TemplateGroup::Unknown || entries_[i].group == group)
            out.push_back(entries_[i].templateId);
    return out;
}

std::vector<std::string> TemplateRegistry::search(const std::string& needle) const {
    std::vector<std::string> out;
    const std::string n = lower(needle);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (n.empty() ||
            lower(entries_[i].templateId).find(n) != std::string::npos ||
            lower(entries_[i].name).find(n) != std::string::npos) {
            out.push_back(entries_[i].templateId);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
// Line oriented and strict: an unrecognised directive inside an entry is an
// ISSUE, not a silent ignore, because "I added a field and it had no effect" is
// exactly the failure this format exists to prevent.
CatalogLoadResult parseCatalog(const std::string& text, const std::string& origin) {
    CatalogLoadResult result;
    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    bool seenVersion = false;
    bool inEntry = false;
    CatalogEntry cur;
    int entryLine = 0;
    std::map<std::string, std::string> pendingMeta;

    const std::string where = origin.empty() ? std::string("<memory>") : origin;

    while (std::getline(in, raw)) {
        ++lineNo;
        std::string line = raw;
        if (!line.empty() && line[line.size() - 1] == 0x0D) line.erase(line.size() - 1);
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        std::vector<std::string> tok = split2(t);
        const std::string& head = tok[0];

        if (!inEntry && head == "version") {
            int v = 0;
            if (tok.size() != 2 || !parseInt(tok[1], &v)) {
                result.issues.push_back(issue(lineNo, "", where + ": malformed version line"));
                continue;
            }
            if (v != kCatalogFormatVersion) {
                result.issues.push_back(issue(lineNo, "",
                    where + ": catalog format version " + tok[1] +
                    " is not supported (this build reads version " +
                    intToString(kCatalogFormatVersion) + ")"));
                return result;   // refuse the whole file rather than misread it
            }
            seenVersion = true;
            continue;
        }

        if (!inEntry) {
            if (head == "entry") {
                if (tok.size() < 2 || tok[1].empty()) {
                    result.issues.push_back(issue(lineNo, "", where + ": entry with no id"));
                    continue;
                }
                if (!seenVersion) {
                    result.issues.push_back(issue(lineNo, tok[1],
                        where + ": entry appears before a version directive"));
                    continue;
                }
                inEntry = true;
                cur = CatalogEntry();
                cur.templateId = tok[1];
                entryLine = lineNo;
                pendingMeta.clear();
            } else {
                result.issues.push_back(issue(lineNo, "",
                    where + ": unexpected directive outside an entry: " + head));
            }
            continue;
        }

        // Inside an entry.
        if (head == "end") {
            std::string reason;
            if (!validateEntry(cur, &reason)) {
                result.issues.push_back(issue(entryLine, cur.templateId,
                    where + ": entry rejected: " + reason));
            } else {
                bool dup = false;
                for (size_t i = 0; i < result.entries.size(); ++i)
                    if (result.entries[i].templateId == cur.templateId) dup = true;
                if (dup) {
                    result.issues.push_back(issue(entryLine, cur.templateId,
                        where + ": duplicate template id in this file"));
                } else {
                    result.entries.push_back(cur);
                }
            }
            inEntry = false;
            continue;
        }

        if (head == "atom") {
            // atom <element> <x> <y> [key=value ...]
            if (tok.size() < 4) {
                result.issues.push_back(issue(lineNo, cur.templateId, "atom needs element x y"));
                continue;
            }
            CatalogAtom a;
            int el = 0;
            if (!parseInt(tok[1], &el)) {
                result.issues.push_back(issue(lineNo, cur.templateId, "atom element is not an integer"));
                continue;
            }
            if (!parseDouble(tok[2], &a.offset.x) || !parseDouble(tok[3], &a.offset.y)) {
                result.issues.push_back(issue(lineNo, cur.templateId, "atom coordinates are not numbers"));
                continue;
            }
            a.element = el;
            bool atomOk = true;
            for (size_t i = 4; i < tok.size(); ++i) {
                const std::string& kv = tok[i];
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) {
                    result.issues.push_back(issue(lineNo, cur.templateId, "atom attribute is not key=value: " + kv));
                    atomOk = false; break;
                }
                const std::string k = lower(kv.substr(0, eq));
                const std::string v = kv.substr(eq + 1);
                if (k == "label") {
                    a.labelText = v;
                } else if (k == "charge") {
                    if (!parseInt(v, &a.charge)) { atomOk = false; result.issues.push_back(issue(lineNo, cur.templateId, "atom charge is not an integer")); break; }
                } else if (k == "h") {
                    if (!parseInt(v, &a.hydrogenCount)) { atomOk = false; result.issues.push_back(issue(lineNo, cur.templateId, "atom h is not an integer")); break; }
                } else if (k == "aromatic") {
                    a.aromatic = (v == "1" || lower(v) == "true");
                } else {
                    atomOk = false;
                    result.issues.push_back(issue(lineNo, cur.templateId, "unknown atom attribute: " + k));
                    break;
                }
            }
            if (atomOk) cur.atoms.push_back(a);
            continue;
        }

        if (head == "bond") {
            // bond <from> <to> <order> [display=..] [semantics=..]
            if (tok.size() < 4) {
                result.issues.push_back(issue(lineNo, cur.templateId, "bond needs from to order"));
                continue;
            }
            CatalogBond b;
            if (!parseInt(tok[1], &b.from) || !parseInt(tok[2], &b.to)) {
                result.issues.push_back(issue(lineNo, cur.templateId, "bond endpoints are not integers"));
                continue;
            }
            if (!parseOrder(tok[3], &b.order)) {
                result.issues.push_back(issue(lineNo, cur.templateId, "unknown bond order: " + tok[3]));
                continue;
            }
            bool bondOk = true;
            for (size_t i = 4; i < tok.size(); ++i) {
                const std::string& kv = tok[i];
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) {
                    result.issues.push_back(issue(lineNo, cur.templateId, "bond attribute is not key=value: " + kv));
                    bondOk = false; break;
                }
                const std::string k = lower(kv.substr(0, eq));
                const std::string v = kv.substr(eq + 1);
                if (k == "display") {
                    if (!parseDisplay(v, &b.display)) { bondOk = false; result.issues.push_back(issue(lineNo, cur.templateId, "unknown bond display: " + v)); break; }
                } else if (k == "semantics") {
                    if (!parseSemantics(v, &b.semantics)) { bondOk = false; result.issues.push_back(issue(lineNo, cur.templateId, "unknown bond semantics: " + v)); break; }
                } else {
                    bondOk = false;
                    result.issues.push_back(issue(lineNo, cur.templateId, "unknown bond attribute: " + k));
                    break;
                }
            }
            if (bondOk) cur.bonds.push_back(b);
            continue;
        }

        if (head == "anchor") {
            if (tok.size() < 3) {
                result.issues.push_back(issue(lineNo, cur.templateId, "anchor needs atomIndex rank"));
                continue;
            }
            CatalogAnchor an;
            if (!parseInt(tok[1], &an.atomIndex) || !parseInt(tok[2], &an.rank)) {
                result.issues.push_back(issue(lineNo, cur.templateId, "anchor numbers are not integers"));
                continue;
            }
            cur.anchors.push_back(an);
            continue;
        }

        if (head == "name") {
            if (tok.size() < 2) { result.issues.push_back(issue(lineNo, cur.templateId, "name is empty")); continue; }
            cur.name = t.substr(t.find("name") + 4);
            cur.name = trim(cur.name);
            continue;
        }
        if (head == "group") {
            if (tok.size() < 2 || !templateGroupFromName(tok[1], &cur.group)) {
                result.issues.push_back(issue(lineNo, cur.templateId, "unknown or missing catalog group"));
            }
            continue;
        }
        if (head == "license") {
            cur.license = tok.size() >= 2 ? tok[1] : std::string();
            if (cur.license.empty()) result.issues.push_back(issue(lineNo, cur.templateId, "license is empty"));
            continue;
        }
        if (head == "source") {
            cur.source = tok.size() >= 2 ? t.substr(t.find("source") + 6) : std::string();
            cur.source = trim(cur.source);
            continue;
        }
        if (head == "meta") {
            if (tok.size() < 3) { result.issues.push_back(issue(lineNo, cur.templateId, "meta needs key value")); continue; }
            std::string key = tok[1];
            std::string val = trim(t.substr(t.find("meta") + 4));
            val = trim(val.substr(val.find(key) + key.size()));
            cur.meta[key] = val;
            continue;
        }

        result.issues.push_back(issue(lineNo, cur.templateId, "unknown directive inside entry: " + head));
    }

    if (inEntry) {
        result.issues.push_back(issue(entryLine, cur.templateId, "entry is not terminated by end"));
    }
    if (!seenVersion && result.entries.empty() && result.issues.empty()) {
        result.issues.push_back(issue(0, "", where + ": no version directive"));
    }
    return result;
}

CatalogLoadResult loadCatalogInto(TemplateRegistry& registry,
                                  const std::string& text,
                                  const std::string& origin) {
    CatalogLoadResult result = parseCatalog(text, origin);
    for (size_t i = 0; i < result.entries.size(); ++i) {
        core::Error e = registry.add(result.entries[i]);
        if (!e.ok()) {
            result.issues.push_back(issue(0, result.entries[i].templateId,
                origin + ": " + e.message));
        }
    }
    return result;
}

bool readCatalogFile(const std::string& path, std::string* out, std::string* error) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (!f.good() && !f.eof()) { if (error) *error = "read error on " + path; return false; }
    *out = ss.str();
    return true;
}

// ---------------------------------------------------------------------------
// Serialiser
// ---------------------------------------------------------------------------
std::string serializeEntry(const CatalogEntry& entry) {
    std::ostringstream o;
    o << "entry " << entry.templateId << "\n";
    o << "  name " << entry.name << "\n";
    o << "  group " << templateGroupName(entry.group) << "\n";
    o << "  license " << entry.license << "\n";
    o << "  source " << entry.source << "\n";
    for (std::map<std::string, std::string>::const_iterator it = entry.meta.begin();
         it != entry.meta.end(); ++it) {
        o << "  meta " << it->first << " " << it->second << "\n";
    }
    for (size_t i = 0; i < entry.atoms.size(); ++i) {
        const CatalogAtom& a = entry.atoms[i];
        o << "  atom " << a.element << " " << fmtDouble(a.offset.x) << " "
          << fmtDouble(a.offset.y);
        if (!a.labelText.empty()) o << " label=" << a.labelText;
        if (a.charge != 0)        o << " charge=" << a.charge;
        if (a.hydrogenCount != -1) o << " h=" << a.hydrogenCount;
        if (a.aromatic)           o << " aromatic=1";
        o << "\n";
    }
    for (size_t i = 0; i < entry.bonds.size(); ++i) {
        const CatalogBond& b = entry.bonds[i];
        o << "  bond " << b.from << " " << b.to << " " << orderToken(b.order);
        if (b.display != core::BondDisplay::Normal)
            o << " display=" << displayToken(b.display);
        if (b.semantics != core::BondSemantics::Normal)
            o << " semantics=" << semanticsToken(b.semantics);
        o << "\n";
    }
    for (size_t i = 0; i < entry.anchors.size(); ++i)
        o << "  anchor " << entry.anchors[i].atomIndex << " " << entry.anchors[i].rank << "\n";
    o << "end\n";
    return o.str();
}

std::string serializeCatalog(const std::vector<CatalogEntry>& entries) {
    std::ostringstream o;
    o << "# r62 templates_queries catalog, format version " << kCatalogFormatVersion << "\n";
    o << "version " << kCatalogFormatVersion << "\n\n";
    for (size_t i = 0; i < entries.size(); ++i) o << serializeEntry(entries[i]) << "\n";
    return o.str();
}

} // namespace templates_queries
} // namespace r62
