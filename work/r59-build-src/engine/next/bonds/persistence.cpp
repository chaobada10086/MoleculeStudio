// =============================================================================
// re_round62_viewer/next/bonds/persistence.cpp
//
// T07 - sidecar read/write and the bond-state digest. See persistence.hpp.
//
// The sidecar format is deliberately boring: a version header, then one record
// per line with fixed fields. It is easy to diff, easy to hand-inspect, and it
// cannot silently change meaning because an unknown version is REFUSED.
// =============================================================================

#include "next/bonds/persistence.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace r62 {
namespace bonds {

namespace {

uint64_t fnv1a(uint64_t h, const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(s[i]));
        h *= 1099511628211ull;
    }
    return h;
}
uint64_t fnv1a(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xFFull;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

BondSideFile captureSide(const core::Document& document) {
    BondSideFile file;
    const core::ObjectSlab<core::BondRecord>& bonds = document.bonds();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord* b = bonds.get(static_cast<core::ObjectId>(i));
        if (!b || !b->isAlive()) continue;
        const core::ExtensionBag& bag =
            document.extensions(core::ObjectRef(core::ObjectKind::Bond, b->id));
        const DoubleSide s = sideOf(bag);
        if (s == DoubleSide::Symmetric) continue;   // the default: not recorded
        BondSideRecord rec;
        rec.sourceId = b->sourceId;
        rec.slot = b->id;
        rec.side = s;
        file.records.push_back(rec);
    }
    return file;
}

core::Error writeSideFile(const BondSideFile& file, const std::string& path) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return core::Error(core::ErrorCode::IoError, "cannot open " + path);
    out << "R62BONDS " << file.formatVersion << "\n";
    for (size_t i = 0; i < file.records.size(); ++i) {
        const BondSideRecord& r = file.records[i];
        out << "side " << r.sourceId << " " << r.slot << " "
            << doubleSideName(r.side) << "\n";
    }
    out.flush();
    if (!out) return core::Error(core::ErrorCode::IoError, "write failed for " + path);
    return core::Error::success();
}

core::Error readSideFile(const std::string& path, BondSideFile* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return core::Error(core::ErrorCode::IoError, "cannot open " + path);
    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (!in) return core::Error(core::ErrorCode::ParseError, "missing header in " + path);
    if (magic != "R62BONDS")
        return core::Error(core::ErrorCode::ParseError, "not an R62BONDS file: " + path);
    if (version != 1)
        // An unknown version is REFUSED, never guessed at. Guessing is how a
        // format silently changes meaning between releases.
        return core::Error(core::ErrorCode::Unsupported,
                           "unsupported R62BONDS format version");
    BondSideFile file;
    file.formatVersion = version;
    std::string tag;
    while (in >> tag) {
        if (tag != "side")
            return core::Error(core::ErrorCode::ParseError, "unknown record tag: " + tag);
        BondSideRecord rec;
        std::string sideName;
        in >> rec.sourceId >> rec.slot >> sideName;
        if (!in) return core::Error(core::ErrorCode::ParseError, "truncated side record");
        if (sideName == "left") rec.side = DoubleSide::Left;
        else if (sideName == "right") rec.side = DoubleSide::Right;
        else if (sideName == "centered") rec.side = DoubleSide::Centered;
        else if (sideName == "symmetric") rec.side = DoubleSide::Symmetric;
        else return core::Error(core::ErrorCode::ParseError, "unknown side token: " + sideName);
        file.records.push_back(rec);
    }
    *out = file;
    return core::Error::success();
}

core::Error applySide(const BondSideFile& file, core::Document& document,
                      size_t* matched, size_t* unmatched) {
    size_t hit = 0, miss = 0;
    for (size_t i = 0; i < file.records.size(); ++i) {
        const BondSideRecord& r = file.records[i];
        core::ObjectId target = core::kInvalidObjectId;
        // Prefer the source id when it is meaningful: it is the identity that
        // survives a CDXML export/import cycle. Fall back to the slot id.
        if (r.sourceId != 0) {
            const core::ObjectSlab<core::BondRecord>& bonds = document.bonds();
            for (size_t k = 0; k < bonds.size(); ++k) {
                const core::BondRecord* b = bonds.get(static_cast<core::ObjectId>(k));
                if (b && b->isAlive() && b->sourceId == r.sourceId) {
                    target = b->id;
                    break;
                }
            }
        }
        if (target == core::kInvalidObjectId && document.bonds().exists(r.slot))
            target = r.slot;
        if (target == core::kInvalidObjectId) { ++miss; continue; }
        core::ExtensionBag& bag = document.extensions(
            core::ObjectRef(core::ObjectKind::Bond, target));
        bag.set(kSideExtensionKey, doubleSideName(r.side));
        ++hit;
    }
    if (matched) *matched = hit;
    if (unmatched) *unmatched = miss;
    return core::Error::success();
}

uint64_t bondStateDigest(const core::Document& document) {
    uint64_t h = 14695981039346656037ull;
    const core::ObjectSlab<core::BondRecord>& bonds = document.bonds();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord* b = bonds.get(static_cast<core::ObjectId>(i));
        if (!b || !b->isAlive()) continue;
        h = fnv1a(h, static_cast<uint64_t>(b->id));
        h = fnv1a(h, static_cast<uint64_t>(b->beginAtom));
        h = fnv1a(h, static_cast<uint64_t>(b->endAtom));
        h = fnv1a(h, static_cast<uint64_t>(b->order));
        h = fnv1a(h, static_cast<uint64_t>(b->display));
        h = fnv1a(h, static_cast<uint64_t>(b->semantics));
        h = fnv1a(h, static_cast<uint64_t>(b->stereo));
        const core::ExtensionBag& bag =
            document.extensions(core::ObjectRef(core::ObjectKind::Bond, b->id));
        h = fnv1a(h, std::string(doubleSideName(sideOf(bag))));
    }
    return h;
}

std::string describeBondState(const core::Document& document) {
    std::ostringstream os;
    const core::ObjectSlab<core::BondRecord>& bonds = document.bonds();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord* b = bonds.get(static_cast<core::ObjectId>(i));
        if (!b || !b->isAlive()) continue;
        const core::ExtensionBag& bag =
            document.extensions(core::ObjectRef(core::ObjectKind::Bond, b->id));
        os << "bond#" << b->id << " " << b->beginAtom << "->" << b->endAtom
           << " order=" << static_cast<int>(b->order)
           << " display=" << static_cast<int>(b->display)
           << " semantics=" << static_cast<int>(b->semantics)
           << " stereo=" << static_cast<int>(b->stereo)
           << " side=" << doubleSideName(sideOf(bag)) << "\n";
    }
    return os.str();
}

} // namespace bonds
} // namespace r62
