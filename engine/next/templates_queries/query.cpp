#include "next/templates_queries/query.hpp"

#include "next/chemistry/element_data.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace r62 {
namespace templates_queries {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(s[i]);
    }
    out.push_back(cur);
    return out;
}

std::string intText(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

// Extension keys.  Everything T13 persists goes under a "user.t13." prefix so it
// is provably OURS and does not collide with a CDXML attribute namespace.  Note
// isPersistentExtensionKey() only blesses "cdxml."; T02's native writer persists
// extension entries by key verbatim, so a user.* key IS saved by the native format
// and is NOT claimed to survive a CDXML round trip.  That distinction is stated in
// RESULTS.md rather than glossed over.
const char* const kKeyAtomMap        = "user.t13.query.atommap";
const char* const kKeyFreeSite       = "user.t13.query.freesite";
const char* const kKeyElementList    = "user.t13.query.elementlist";
const char* const kKeyStereoFlag     = "user.t13.query.stereoflag";
const char* const kKeyAltGroupDef    = "user.t13.query.altgroup";

// A command that writes one extension value on one object, recording the prior
// value so undo restores it exactly.  Building this once and reusing it is what
// keeps every query tool a single undo step without six near-identical bodies.
core::Command setObjectExtension(const core::ObjectRef& ref,
                                 const std::string& key,
                                 const std::string& value);

core::Command setObjectExtension(const core::ObjectRef& ref,
                                 const std::string& key,
                                 const std::string& value) {
    return core::Command("t13.setExtension", core::kInvalidateSemantics |
                                              core::kInvalidateAppearance,
        [ref, key, value](core::CommandContext& ctx) -> core::CommandResult {
            ctx.document.extensions(ref).set(key, value);
            ctx.document.noteTouchedExtensions(ref, false);
            return core::CommandResult::success(core::kInvalidateSemantics |
                                                core::kInvalidateAppearance);
        });
}

} // namespace

// ---------------------------------------------------------------------------
// The six commands
// ---------------------------------------------------------------------------
const char* queryToolCommand(QueryTool t) {
    switch (t) {
        case QueryTool::ReactionAtomAtomMap:     return "chooseQueryTool_ReactionAtomAtomMap";
        case QueryTool::AlternativeGroup:        return "chooseQueryTool_AlternativeGroup";
        case QueryTool::FreeSite:                return "chooseQueryTool_FreeSite";
        case QueryTool::AbsoluteStereochemistry: return "chooseQueryTool_AbsoluteStereochemistry";
        case QueryTool::RacemicStereochemistry:  return "chooseQueryTool_RacemicStereochemistry";
        case QueryTool::RelativeStereochemistry: return "chooseQueryTool_RelativeStereochemistry";
        case QueryTool::None:                    break;
    }
    return "";
}

bool queryToolFromCommand(const std::string& command, QueryTool* out) {
    for (int i = 1; i <= 6; ++i) {
        const QueryTool t = static_cast<QueryTool>(i);
        if (command == queryToolCommand(t)) { *out = t; return true; }
    }
    return false;
}

const char* queryToolName(QueryTool t) {
    switch (t) {
        case QueryTool::ReactionAtomAtomMap:     return "reaction-atom-atom-map";
        case QueryTool::AlternativeGroup:        return "alternative-group";
        case QueryTool::FreeSite:                return "free-site";
        case QueryTool::AbsoluteStereochemistry: return "absolute-stereochemistry";
        case QueryTool::RacemicStereochemistry:  return "racemic-stereochemistry";
        case QueryTool::RelativeStereochemistry: return "relative-stereochemistry";
        case QueryTool::None:                    break;
    }
    return "none";
}

// ---------------------------------------------------------------------------
// 1. Atom-atom mapping
// ---------------------------------------------------------------------------
core::Error setAtomMapNumber(const std::vector<core::ObjectId>& atoms,
                             int mapNumber, std::vector<core::Command>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (atoms.empty()) return core::Error(core::ErrorCode::InvalidArgument, "no atoms");
    if (mapNumber < 0 || mapNumber > kMaxAtomMapNumber)
        return core::Error(core::ErrorCode::OutOfRange,
            "map number must be 0.." + intText(kMaxAtomMapNumber) +
            " (0 clears; ChemDraw mappings are positive integers)");
    out->clear();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (atoms[i] == core::kInvalidObjectId)
            return core::Error(core::ErrorCode::InvalidArgument, "invalid atom id");
        out->push_back(setObjectExtension(core::ObjectRef(core::ObjectKind::Atom, atoms[i]),
                                          kKeyAtomMap, intText(mapNumber)));
    }
    return core::Error::success();
}

core::Error clearAtomMap(const std::vector<core::ObjectId>& atoms,
                         std::vector<core::Command>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (atoms.empty()) return core::Error(core::ErrorCode::InvalidArgument, "no atoms");
    out->clear();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (atoms[i] == core::kInvalidObjectId)
            return core::Error(core::ErrorCode::InvalidArgument, "invalid atom id");
        out->push_back(setObjectExtension(core::ObjectRef(core::ObjectKind::Atom, atoms[i]),
                                          kKeyAtomMap, std::string("0")));
    }
    return core::Error::success();
}

std::vector<int> usedAtomMapNumbers(const core::Document& document) {
    std::vector<int> used;
    const core::ObjectSlab<core::AtomRecord>& slab = document.atoms();
    const std::vector<core::AtomRecord>& recs = slab.records();
    for (size_t i = 0; i < recs.size(); ++i) {
        if (!recs[i].isAlive()) continue;
        const std::string* v = document.extensions(
            core::ObjectRef(core::ObjectKind::Atom, recs[i].id)).findValue(kKeyAtomMap);
        if (!v) continue;
        const int n = std::atoi(v->c_str());
        if (n <= 0) continue;
        bool dup = false;
        for (size_t k = 0; k < used.size(); ++k) if (used[k] == n) dup = true;
        if (!dup) used.push_back(n);
    }
    // Insertion sort: the list is tiny and this keeps determinism independent of
    // the map implementation.
    for (size_t i = 1; i < used.size(); ++i) {
        const int key = used[i];
        size_t j = i;
        while (j > 0 && used[j - 1] > key) { used[j] = used[j - 1]; --j; }
        used[j] = key;
    }
    return used;
}

core::ObjectId atomWithMapNumber(const core::Document& document, int mapNumber,
                                 int* count) {
    if (count) *count = 0;
    if (mapNumber <= 0) return core::kInvalidObjectId;
    core::ObjectId first = core::kInvalidObjectId;
    const core::ObjectSlab<core::AtomRecord>& slab = document.atoms();
    const std::vector<core::AtomRecord>& recs = slab.records();
    for (size_t i = 0; i < recs.size(); ++i) {
        if (!recs[i].isAlive()) continue;
        const std::string* v = document.extensions(
            core::ObjectRef(core::ObjectKind::Atom, recs[i].id)).findValue(kKeyAtomMap);
        if (!v) continue;
        if (std::atoi(v->c_str()) != mapNumber) continue;
        if (first == core::kInvalidObjectId) first = recs[i].id;
        if (count) *count = *count + 1;
    }
    return first;
}

// ---------------------------------------------------------------------------
// 2. Free sites
// ---------------------------------------------------------------------------
std::string freeSiteLabel(int count) {
    if (count < 0) count = 0;
    if (count > kMaxFreeSites) count = kMaxFreeSites;
    return "*" + intText(count);
}

bool parseFreeSiteLabel(const std::string& text, int* out) {
    std::string t = trim(text);
    if (t.empty()) return false;
    // Accept "*n", "n", and a trailing "*" as the guide draws the marker.
    if (t[0] == '*') t = t.substr(1);
    if (!t.empty() && t[t.size() - 1] == '*') t.erase(t.size() - 1);
    if (t.empty()) return false;
    for (size_t i = 0; i < t.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
    const long v = std::atol(t.c_str());
    if (v < 0 || v > kMaxFreeSites) return false;   // guide lists *0 .. *15 only
    *out = static_cast<int>(v);
    return true;
}

core::Error setFreeSiteCount(const std::vector<core::ObjectId>& atoms, int count,
                             std::vector<core::Command>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (atoms.empty()) return core::Error(core::ErrorCode::InvalidArgument, "no atoms");
    if (count < 0 || count > kMaxFreeSites)
        return core::Error(core::ErrorCode::OutOfRange,
            "free site count must be 0.." + intText(kMaxFreeSites) +
            " (the guide lists *0 through *15)");
    out->clear();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (atoms[i] == core::kInvalidObjectId)
            return core::Error(core::ErrorCode::InvalidArgument, "invalid atom id");
        out->push_back(setObjectExtension(core::ObjectRef(core::ObjectKind::Atom, atoms[i]),
                                          kKeyFreeSite, intText(count)));
    }
    return core::Error::success();
}

int stepFreeSite(int current, bool decrease) {
    int next = decrease ? current - 1 : current + 1;
    if (next < 0) next = 0;
    if (next > kMaxFreeSites) next = kMaxFreeSites;
    // Guide: "Continue to click the atom to increase the free site number."  At the
    // ceiling the value SATURATES rather than wrapping, because wrapping *15 -> *0
    // would silently turn a strict query into an unrestricted one.
    return next;
}

// ---------------------------------------------------------------------------
// 3. Element lists
// ---------------------------------------------------------------------------
// Two-pass: strip the optional brackets and an optional NOT, then split on commas.
// A token that is not a known element is a REFUSAL with the token quoted, because
// the guide restricts the list to atomic symbols plus D and T, and silently
// dropping an unknown token would change the query meaning.
bool parseElementList(const std::string& text, ElementList* out, std::string* reason) {
    ElementList list;
    std::string t = trim(text);
    if (t.empty()) { if (reason) *reason = "empty element list"; return false; }

    const bool bracketed = (t[0] == '[' || t[0] == '(' || t[0] == '{');
    if (bracketed) {
        const char close = (t[0] == '[') ? ']' : ((t[0] == '(') ? ')' : '}');
        if (t[t.size() - 1] != close) {
            if (reason) *reason = "unbalanced brackets in element list";
            return false;
        }
        t = trim(t.substr(1, t.size() - 2));
    }

    // NOT prefix: the guide requires all-caps NOT followed by a space, or a minus.
    if (t.compare(0, 4, "NOT ") == 0) {
        list.negated = true;
        t = trim(t.substr(4));
    } else if (!t.empty() && t[0] == '-') {
        list.negated = true;
        t = trim(t.substr(1));
    } else if (t.compare(0, 3, "not") == 0) {
        // Lower-case "not" is NOT accepted: the guide says all-caps.  Accepting it
        // would make the parser more permissive than the documented syntax and a
        // user could not tell which spelling a file used.
        if (reason) *reason = "the NOT keyword must be all upper case";
        return false;
    }

    if (t.empty()) { if (reason) *reason = "element list has no elements"; return false; }

    const std::vector<std::string> items = splitOn(t, ',');
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string sym = trim(items[i]);
        if (sym.empty()) {
            if (reason) *reason = "element list has an empty item (stray comma)";
            return false;
        }
        if (sym == "D") {
            if (list.hasDeuterium) { if (reason) *reason = "duplicate D"; return false; }
            list.hasDeuterium = true;
            continue;
        }
        if (sym == "T") {
            if (list.hasTritium) { if (reason) *reason = "duplicate T"; return false; }
            list.hasTritium = true;
            continue;
        }
        const int z = core::elementNumber(sym);
        if (z <= 0) {
            if (reason) *reason = "not an atomic element symbol: " + sym;
            return false;
        }
        for (size_t k = 0; k < list.elements.size(); ++k) {
            if (list.elements[k] == z) {
                if (reason) *reason = "duplicate element in list: " + sym;
                return false;
            }
        }
        list.elements.push_back(z);
    }

    if (list.empty()) { if (reason) *reason = "element list has no elements"; return false; }
    *out = list;
    return true;
}

std::string formatElementList(const ElementList& list) {
    std::string body;
    for (size_t i = 0; i < list.elements.size(); ++i) {
        if (!body.empty()) body += ", ";
        body += core::elementSymbol(list.elements[i]);
    }
    if (list.hasDeuterium) { if (!body.empty()) body += ", "; body += "D"; }
    if (list.hasTritium)   { if (!body.empty()) body += ", "; body += "T"; }
    if (list.negated) return "[NOT " + body + "]";
    return "[" + body + "]";
}

// ---------------------------------------------------------------------------
// 4. Alternative groups
// ---------------------------------------------------------------------------
core::Error validateAlternativeGroup(const TemplateRegistry& registry,
                                    const AlternativeGroup& group) {
    if (!group.anonymous && group.name.empty())
        return core::Error(core::ErrorCode::InvalidArgument,
                           "a named alternative group needs a name (for example R1)");
    if (group.anonymous && !group.name.empty())
        return core::Error(core::ErrorCode::InvalidArgument,
                           "an anonymous alternative group must not carry a name");
    if (group.fragments.empty())
        return core::Error(core::ErrorCode::InvalidArgument,
                           "an alternative group needs at least one fragment");

    std::vector<int> referenceRanks;
    for (size_t f = 0; f < group.fragments.size(); ++f) {
        const AltFragment& frag = group.fragments[f];
        if (registry.find(frag.templateId) == 0)
            return core::Error(core::ErrorCode::NotFound,
                               "fragment references an unknown template: " + frag.templateId);
        if (frag.attachments.empty())
            return core::Error(core::ErrorCode::InvalidArgument,
                               "fragment " + frag.templateId + " has no attachment point");
        std::vector<int> ranks;
        for (size_t a = 0; a < frag.attachments.size(); ++a) {
            if (frag.attachments[a].rank < 1)
                return core::Error(core::ErrorCode::InvalidArgument,
                                   "attachment rank must be >= 1");
            for (size_t k = 0; k < ranks.size(); ++k) {
                if (ranks[k] == frag.attachments[a].rank)
                    return core::Error(core::ErrorCode::AlreadyExists,
                        "fragment " + frag.templateId + " has two attachments with rank " +
                        intText(frag.attachments[a].rank) +
                        "; attachment order would be undefined");
            }
            ranks.push_back(frag.attachments[a].rank);
        }
        // ChemDraw cross-links fragments BY RANK ("The attachment point number
        // matches that found in the definition", guide p.335).  Fragments whose
        // rank sets differ cannot be substituted for each other.
        if (f == 0) {
            referenceRanks = ranks;
        } else {
            if (ranks.size() != referenceRanks.size())
                return core::Error(core::ErrorCode::InvalidSemantics,
                    "fragment " + frag.templateId +
                    " has a different number of attachment points than fragment 0");
            for (size_t k = 0; k < ranks.size(); ++k) {
                bool found = false;
                for (size_t m = 0; m < referenceRanks.size(); ++m)
                    if (referenceRanks[m] == ranks[k]) found = true;
                if (!found)
                    return core::Error(core::ErrorCode::InvalidSemantics,
                        "fragment " + frag.templateId + " is missing attachment rank " +
                        intText(ranks[k]));
            }
        }
    }
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// 5. Stereochemistry flags
// ---------------------------------------------------------------------------
const char* stereoFlagName(StereoFlagKind k) {
    switch (k) {
        case StereoFlagKind::Absolute: return "absolute";
        case StereoFlagKind::Racemic:  return "racemic";
        case StereoFlagKind::Relative: return "relative";
        case StereoFlagKind::None:     break;
    }
    return "none";
}

bool stereoFlagFromName(const std::string& name, StereoFlagKind* out) {
    if (name == "absolute") { *out = StereoFlagKind::Absolute; return true; }
    if (name == "racemic")  { *out = StereoFlagKind::Racemic;  return true; }
    if (name == "relative") { *out = StereoFlagKind::Relative; return true; }
    return false;
}

QueryTool stereoFlagTool(StereoFlagKind k) {
    switch (k) {
        case StereoFlagKind::Absolute: return QueryTool::AbsoluteStereochemistry;
        case StereoFlagKind::Racemic:  return QueryTool::RacemicStereochemistry;
        case StereoFlagKind::Relative: return QueryTool::RelativeStereochemistry;
        case StereoFlagKind::None:     break;
    }
    return QueryTool::None;
}

core::Error validateStereoFlag(const core::Document& document,
                               const StereoFlag& flag) {
    if (flag.kind == StereoFlagKind::None)
        return core::Error(core::ErrorCode::InvalidArgument, "stereo flag has no kind");
    if (!flag.position.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "stereo flag position is not finite");
    if (flag.atoms.empty())
        return core::Error(core::ErrorCode::InvalidArgument,
                           "a stereo flag must be assigned to at least one atom");
    for (size_t i = 0; i < flag.atoms.size(); ++i) {
        if (!document.atoms().exists(flag.atoms[i]))
            return core::Error(core::ErrorCode::NotFound, "stereo flag references a dead atom");
    }
    return core::Error::success();
}

} // namespace templates_queries
} // namespace r62
