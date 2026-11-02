// =============================================================================
// re_round62_viewer/next/helm/monomer_query.cpp
//
// T15 - MonomerLibrary behaviour: insertion rules, lookup, search, validation.
// Split out of monomer_library.cpp so the generated data table file stays a
// pure data file.
// =============================================================================

#include "next/helm/monomer.hpp"

#include <algorithm>
#include <cctype>

namespace r62 {
namespace helm {
namespace {

std::string lowered(const std::string& s) {
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// add()
// ---------------------------------------------------------------------------
// Every refusal here is a rule that, if skipped, would put a monomer into the
// library that the rest of the module cannot reason about.
bool MonomerLibrary::add(const MonomerDefinition& definition, std::string* reasonIfRefused) {
    if (reasonIfRefused) reasonIfRefused->clear();

    if (definition.symbol.empty()) {
        if (reasonIfRefused) *reasonIfRefused = "empty monomer symbol";
        return false;
    }
    if (definition.polymerType == PolymerType::Unknown) {
        if (reasonIfRefused) *reasonIfRefused = "unknown polymer type";
        return false;
    }
    if (definition.attachments.size() > static_cast<size_t>(kMaxAttachmentPoints)) {
        if (reasonIfRefused) *reasonIfRefused = "more attachment points than the model allows";
        return false;
    }
    for (size_t i = 0; i < definition.attachments.size(); ++i) {
        const std::string& label = definition.attachments[i].label;
        if (!isValidAttachmentLabel(label)) {
            if (reasonIfRefused) *reasonIfRefused = "invalid attachment label " + label;
            return false;
        }
        for (size_t k = i + 1; k < definition.attachments.size(); ++k) {
            if (definition.attachments[k].label == label) {
                if (reasonIfRefused) *reasonIfRefused = "duplicate attachment label " + label;
                return false;
            }
        }
    }
    // R5: provenance is mandatory.
    if (definition.license.empty()) {
        if (reasonIfRefused) *reasonIfRefused = "missing licence string";
        return false;
    }
    if (definition.source.empty()) {
        if (reasonIfRefused) *reasonIfRefused = "missing source string";
        return false;
    }

    const MonomerKey key(definition.polymerType, definition.symbol);
    std::map<MonomerKey, size_t>::iterator it = index_.find(key);
    if (it != index_.end()) {
        entries_[it->second] = definition;   // explicit replace
        return true;
    }
    index_[key] = entries_.size();
    entries_.push_back(definition);
    return true;
}

const MonomerDefinition* MonomerLibrary::find(PolymerType t, const std::string& symbol) const {
    std::map<MonomerKey, size_t>::const_iterator it = index_.find(MonomerKey(t, symbol));
    return it == index_.end() ? 0 : &entries_[it->second];
}

const MonomerDefinition* MonomerLibrary::findCaseTolerant(PolymerType t,
                                                         const std::string& symbol) const {
    const MonomerDefinition* exact = find(t, symbol);
    if (exact) return exact;
    const std::string wanted = lowered(symbol);
    const MonomerDefinition* unique = 0;
    int matches = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].polymerType != t) continue;
        if (lowered(entries_[i].symbol) != wanted) continue;
        ++matches;
        unique = &entries_[i];
        if (matches > 1) return 0;   // ambiguous: refuse rather than pick
    }
    return matches == 1 ? unique : 0;
}

std::vector<const MonomerDefinition*> MonomerLibrary::byPolymerType(PolymerType t) const {
    std::vector<const MonomerDefinition*> out;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].polymerType == t) out.push_back(&entries_[i]);
    return out;
}

std::vector<const MonomerDefinition*> MonomerLibrary::search(const std::string& query,
                                                             PolymerType filter) const {
    std::vector<const MonomerDefinition*> exact, symbolPrefix, namePrefix, rest;
    const std::string q = lowered(query);
    if (q.empty()) return byPolymerType(filter);

    for (size_t i = 0; i < entries_.size(); ++i) {
        const MonomerDefinition& e = entries_[i];
        if (filter != PolymerType::Unknown && e.polymerType != filter) continue;
        const std::string sym = lowered(e.symbol);
        const std::string nam = lowered(e.name);
        if (sym == q) { exact.push_back(&e); continue; }
        if (startsWith(sym, q)) { symbolPrefix.push_back(&e); continue; }
        if (startsWith(nam, q)) { namePrefix.push_back(&e); continue; }
        if (sym.find(q) != std::string::npos || nam.find(q) != std::string::npos)
            rest.push_back(&e);
    }
    std::vector<const MonomerDefinition*> out;
    out.reserve(exact.size() + symbolPrefix.size() + namePrefix.size() + rest.size());
    out.insert(out.end(), exact.begin(), exact.end());
    out.insert(out.end(), symbolPrefix.begin(), symbolPrefix.end());
    out.insert(out.end(), namePrefix.begin(), namePrefix.end());
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

std::vector<const MonomerDefinition*> MonomerLibrary::byNaturalAnalog(PolymerType t,
                                                                     const std::string& analog) const {
    std::vector<const MonomerDefinition*> out;
    if (analog.empty()) return out;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].polymerType == t && entries_[i].naturalAnalog == analog)
            out.push_back(&entries_[i]);
    return out;
}

size_t MonomerLibrary::validate(std::vector<LibraryProblem>* problems) const {
    size_t count = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const MonomerDefinition& e = entries_[i];
        if (e.license.empty()) {
            ++count;
            if (problems) problems->push_back(LibraryProblem(e.symbol, "missing licence"));
        }
        if (e.source.empty()) {
            ++count;
            if (problems) problems->push_back(LibraryProblem(e.symbol, "missing source"));
        }
        for (size_t a = 0; a < e.attachments.size(); ++a) {
            if (!isValidAttachmentLabel(e.attachments[a].label)) {
                ++count;
                if (problems) problems->push_back(
                    LibraryProblem(e.symbol, "invalid attachment label " + e.attachments[a].label));
            }
        }
        // A definition that claims chemistry yet states no structure is exactly
        // the silent-degradation case this module refuses to ship.
        if (e.chemistryDefined && e.smiles.empty()) {
            ++count;
            if (problems) problems->push_back(
                LibraryProblem(e.symbol, "claims chemistry definition but states no structure"));
        }
        if (e.isBlob() && e.chemistryDefined) {
            ++count;
            if (problems) problems->push_back(
                LibraryProblem(e.symbol, "BLOB monomer claims a chemistry definition"));
        }
        // The index must agree with the vector: a mismatch means add() is broken.
        const MonomerDefinition* found = find(e.polymerType, e.symbol);
        if (found != &e) {
            ++count;
            if (problems) problems->push_back(LibraryProblem(e.symbol, "index mismatch"));
        }
    }
    return count;
}

} // namespace helm
} // namespace r62
