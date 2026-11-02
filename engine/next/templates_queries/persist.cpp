#include "next/templates_queries/persist.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace r62 {
namespace templates_queries {

const char* const kKeyElementList    = "user.t13.query.elementlist";
const char* const kKeyAltGroupDef    = "user.t13.query.altgroup";
const char* const kKeyStereoFlagData = "user.t13.query.stereoflag";

namespace {

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

std::map<std::string, std::string> parseRecord(const std::string& text) {
    std::map<std::string, std::string> kv;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t semi = text.find(';', start);
        const std::string part = (semi == std::string::npos)
            ? text.substr(start) : text.substr(start, semi - start);
        if (!part.empty()) {
            const size_t eq = part.find('=');
            if (eq == std::string::npos) kv["__malformed__"] = part;
            else kv[part.substr(0, eq)] = part.substr(eq + 1);
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return kv;
}

std::string joinInts(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += intText(v[i]);
    }
    return s;
}

bool splitInts(const std::string& s, std::vector<int>* out) {
    out->clear();
    if (s.empty()) return true;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const std::string tok = (comma == std::string::npos)
            ? s.substr(start) : s.substr(start, comma - start);
        int v = 0;
        if (!parseInt(tok, &v)) return false;
        out->push_back(v);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return true;
}

std::string joinIds(const std::vector<core::ObjectId>& ids) {
    std::string s;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) s += ",";
        s += intText(static_cast<int>(ids[i]));
    }
    return s;
}

bool splitIds(const std::string& s, std::vector<core::ObjectId>* out) {
    std::vector<int> v;
    if (!splitInts(s, &v)) return false;
    out->clear();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] < 0) return false;
        out->push_back(static_cast<core::ObjectId>(v[i]));
    }
    return true;
}

std::string joinStrings(const std::vector<std::string>& v) {
    // Escape the separators so a fragment id containing a comma or a semicolon
    // cannot corrupt the record.  Template ids are dotted tokens today, but the
    // encoder must not depend on that.
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        for (size_t k = 0; k < v[i].size(); ++k) {
            const char ch = v[i][k];
            if (ch == '\\' || ch == ',' || ch == ';' || ch == '=') s += '\\';
            s += ch;
        }
    }
    return s;
}

bool splitStrings(const std::string& s, std::vector<std::string>* out) {
    out->clear();
    if (s.empty()) return true;
    std::string cur;
    bool escape = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (escape) { cur += ch; escape = false; continue; }
        if (ch == '\\') { escape = true; continue; }
        if (ch == ',') { out->push_back(cur); cur.clear(); continue; }
        cur += ch;
    }
    if (escape) return false;   // a trailing backslash is a malformed record
    out->push_back(cur);
    return true;
}

bool readExtension(const core::Document& document, const core::ObjectRef& ref,
                   const std::string& key, std::string* out) {
    const std::string* v = document.extensions(ref).findValue(key);
    if (!v) return false;
    *out = *v;
    return true;
}

core::Command writeExtension(const core::ObjectRef& ref, const std::string& key,
                             const std::string& value, const char* name) {
    return core::Command(name, core::kInvalidateSemantics | core::kInvalidateAppearance,
        [ref, key, value](core::CommandContext& ctx) -> core::CommandResult {
            ctx.document.extensions(ref).set(key, value);
            ctx.document.noteTouchedExtensions(ref, false);
            return core::CommandResult::success(core::kInvalidateSemantics |
                                                core::kInvalidateAppearance);
        });
}

} // namespace

// ---------------------------------------------------------------------------
// Element list codec
// ---------------------------------------------------------------------------
std::string encodeElementList(const ElementList& list) {
    std::string s = "v=1";
    s += list.negated ? ";neg=1" : ";neg=0";
    s += ";el=" + joinInts(list.elements);
    if (list.hasDeuterium) s += ";d=1";
    if (list.hasTritium) s += ";t=1";
    return s;
}

bool decodeElementList(const std::string& text, ElementList* out) {
    ElementList list;
    const std::map<std::string, std::string> kv = parseRecord(text);
    if (kv.find("__malformed__") != kv.end()) return false;
    std::map<std::string, std::string>::const_iterator it = kv.find("v");
    if (it == kv.end() || it->second != "1") return false;
    it = kv.find("neg");
    if (it == kv.end()) return false;
    list.negated = (it->second == "1");
    it = kv.find("el");
    if (it == kv.end() || !splitInts(it->second, &list.elements)) return false;
    for (size_t i = 0; i < list.elements.size(); ++i)
        if (!core::validElement(list.elements[i])) return false;
    it = kv.find("d");
    if (it != kv.end() && it->second == "1") list.hasDeuterium = true;
    it = kv.find("t");
    if (it != kv.end() && it->second == "1") list.hasTritium = true;
    if (list.empty()) return false;
    *out = list;
    return true;
}

// ---------------------------------------------------------------------------
// Stereo flag codec
// ---------------------------------------------------------------------------
std::string encodeStereoFlag(const StereoFlag& flag) {
    std::string s = "v=1";
    s += ";kind=";
    s += stereoFlagName(flag.kind);
    s += ";x=" + doubleText(flag.position.x);
    s += ";y=" + doubleText(flag.position.y);
    s += ";atoms=" + joinIds(flag.atoms);
    if (flag.textSize > 0.0) s += ";size=" + doubleText(flag.textSize);
    return s;
}

bool decodeStereoFlag(const std::string& text, StereoFlag* out) {
    StereoFlag flag;
    const std::map<std::string, std::string> kv = parseRecord(text);
    if (kv.find("__malformed__") != kv.end()) return false;
    std::map<std::string, std::string>::const_iterator it = kv.find("v");
    if (it == kv.end() || it->second != "1") return false;
    it = kv.find("kind");
    if (it == kv.end() || !stereoFlagFromName(it->second, &flag.kind)) return false;
    it = kv.find("x");
    if (it == kv.end() || !parseDouble(it->second, &flag.position.x)) return false;
    it = kv.find("y");
    if (it == kv.end() || !parseDouble(it->second, &flag.position.y)) return false;
    it = kv.find("atoms");
    if (it != kv.end() && !splitIds(it->second, &flag.atoms)) return false;
    it = kv.find("size");
    if (it != kv.end() && !parseDouble(it->second, &flag.textSize)) return false;
    *out = flag;
    return true;
}

// ---------------------------------------------------------------------------
// Alternative group codec
// ---------------------------------------------------------------------------
// Fragments are encoded as "templateId|rank:atomIndex,rank:atomIndex".  Rank
// ORDER matters (guide p.336-337), so the attachment list is written in rank order
// and restored in the same order rather than sorted.
std::string encodeAlternativeGroup(const AlternativeGroup& group) {
    std::string s = "v=1";
    s += group.anonymous ? ";anon=1" : ";anon=0";
    if (!group.name.empty()) s += ";name=" + group.name;
    std::vector<std::string> frags;
    for (size_t f = 0; f < group.fragments.size(); ++f) {
        std::string one = group.fragments[f].templateId + "|";
        // Sort attachments by rank so the encoded form is canonical.
        std::vector<std::pair<int, int> > pairs;
        for (size_t a = 0; a < group.fragments[f].attachments.size(); ++a)
            pairs.push_back(std::make_pair(group.fragments[f].attachments[a].rank,
                                            group.fragments[f].attachments[a].atomIndex));
        for (size_t i = 1; i < pairs.size(); ++i) {
            const std::pair<int, int> key = pairs[i];
            size_t j = i;
            while (j > 0 && pairs[j - 1].first > key.first) { pairs[j] = pairs[j - 1]; --j; }
            pairs[j] = key;
        }
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (i) one += ",";
            one += intText(pairs[i].first) + ":" + intText(pairs[i].second);
        }
        frags.push_back(one);
    }
    s += ";frag=" + joinStrings(frags);
    return s;
}

bool decodeAlternativeGroup(const std::string& text, AlternativeGroup* out) {
    AlternativeGroup group;
    const std::map<std::string, std::string> kv = parseRecord(text);
    if (kv.find("__malformed__") != kv.end()) return false;
    std::map<std::string, std::string>::const_iterator it = kv.find("v");
    if (it == kv.end() || it->second != "1") return false;
    it = kv.find("anon");
    if (it == kv.end()) return false;
    group.anonymous = (it->second == "1");
    it = kv.find("name");
    if (it != kv.end()) group.name = it->second;
    it = kv.find("frag");
    if (it == kv.end()) return false;
    std::vector<std::string> frags;
    if (!splitStrings(it->second, &frags)) return false;
    for (size_t f = 0; f < frags.size(); ++f) {
        const size_t bar = frags[f].find('|');
        if (bar == std::string::npos) return false;
        AltFragment frag;
        frag.templateId = frags[f].substr(0, bar);
        if (frag.templateId.empty()) return false;
        const std::string rest = frags[f].substr(bar + 1);
        if (!rest.empty()) {
            size_t start = 0;
            while (start <= rest.size()) {
                const size_t comma = rest.find(',', start);
                const std::string tok = (comma == std::string::npos)
                    ? rest.substr(start) : rest.substr(start, comma - start);
                const size_t colon = tok.find(':');
                if (colon == std::string::npos) return false;
                AltAttachment att;
                if (!parseInt(tok.substr(0, colon), &att.rank)) return false;
                if (!parseInt(tok.substr(colon + 1), &att.atomIndex)) return false;
                frag.attachments.push_back(att);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        group.fragments.push_back(frag);
    }
    *out = group;
    return true;
}

// ---------------------------------------------------------------------------
// Readers
// ---------------------------------------------------------------------------
bool readFreeSiteCount(const core::Document& document, core::ObjectId atom, int* out) {
    std::string v;
    if (!readExtension(document, core::ObjectRef(core::ObjectKind::Atom, atom),
                       "user.t13.query.freesite", &v)) return false;
    int n = 0;
    if (!parseInt(v, &n)) return false;
    if (n < 0 || n > kMaxFreeSites) return false;
    *out = n;
    return true;
}

bool readAtomMapNumber(const core::Document& document, core::ObjectId atom, int* out) {
    std::string v;
    if (!readExtension(document, core::ObjectRef(core::ObjectKind::Atom, atom),
                       "user.t13.query.atommap", &v)) return false;
    int n = 0;
    if (!parseInt(v, &n)) return false;
    if (n < 0 || n > kMaxAtomMapNumber) return false;
    *out = n;
    return true;
}

bool readElementList(const core::Document& document, core::ObjectId atom,
                     ElementList* out) {
    std::string v;
    if (!readExtension(document, core::ObjectRef(core::ObjectKind::Atom, atom),
                       kKeyElementList, &v)) return false;
    return decodeElementList(v, out);
}

bool readPolymerBracket(const core::Document& document, core::ObjectId bracket,
                        PolymerBracket* out) {
    std::string v;
    if (!readExtension(document, core::ObjectRef(core::ObjectKind::Bracket, bracket),
                       kPolymerParamsKey, &v)) return false;
    bool ok = false;
    const PolymerBracket b = decodePolymerBracket(v, &ok);
    if (!ok) return false;
    *out = b;
    return true;
}

bool readStereoFlag(const core::Document& document, core::ObjectRef holder,
                    StereoFlag* out) {
    std::string v;
    if (!readExtension(document, holder, kKeyStereoFlagData, &v)) return false;
    return decodeStereoFlag(v, out);
}

bool readAlternativeGroup(const core::Document& document, core::ObjectRef holder,
                          AlternativeGroup* out) {
    std::string v;
    if (!readExtension(document, holder, kKeyAltGroupDef, &v)) return false;
    return decodeAlternativeGroup(v, out);
}

// ---------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------
core::Error writeElementListCommand(core::ObjectId atom, const ElementList& list,
                                   core::Command* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (atom == core::kInvalidObjectId)
        return core::Error(core::ErrorCode::InvalidArgument, "invalid atom id");
    if (list.empty())
        return core::Error(core::ErrorCode::InvalidArgument,
            "an element list must name at least one element; "
            "use a clear command to remove one");
    *out = writeExtension(core::ObjectRef(core::ObjectKind::Atom, atom),
                          kKeyElementList, encodeElementList(list),
                          "t13.writeElementList");
    return core::Error::success();
}

core::Error writePolymerBracketCommand(const core::Document& document,
                                       core::ObjectId bracket,
                                       const PolymerBracket& params,
                                       core::Command* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (!document.brackets().exists(bracket))
        return core::Error(core::ErrorCode::NotFound,
                           "bracket object does not exist in this document");
    core::Error e = validatePolymerBracket(document, params);
    if (!e.ok()) return e;   // never store an invalid polymer
    *out = writeExtension(core::ObjectRef(core::ObjectKind::Bracket, bracket),
                          kPolymerParamsKey, encodePolymerBracket(params),
                          "t13.writePolymerBracket");
    return core::Error::success();
}

core::Error writeStereoFlagCommand(core::ObjectRef holder, const StereoFlag& flag,
                                   core::Command* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (!holder.valid()) return core::Error(core::ErrorCode::InvalidArgument, "invalid holder");
    if (flag.kind == StereoFlagKind::None)
        return core::Error(core::ErrorCode::InvalidArgument, "stereo flag has no kind");
    *out = writeExtension(holder, kKeyStereoFlagData, encodeStereoFlag(flag),
                          "t13.writeStereoFlag");
    return core::Error::success();
}

core::Error writeAlternativeGroupCommand(core::ObjectRef holder,
                                         const AlternativeGroup& group,
                                         core::Command* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (!holder.valid()) return core::Error(core::ErrorCode::InvalidArgument, "invalid holder");
    if (group.fragments.empty())
        return core::Error(core::ErrorCode::InvalidArgument,
                           "an alternative group needs at least one fragment");
    *out = writeExtension(holder, kKeyAltGroupDef, encodeAlternativeGroup(group),
                          "t13.writeAlternativeGroup");
    return core::Error::success();
}

} // namespace templates_queries
} // namespace r62
