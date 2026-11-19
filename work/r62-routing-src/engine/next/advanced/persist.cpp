#include "next/advanced/persist.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace r62 {
namespace advanced {

namespace {

template <typename T>
core::ObjectId findBySourceId(const std::vector<T>& records, int32_t sourceId) {
    if (sourceId == 0) return core::kInvalidObjectId;
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId == sourceId)
            return records[i].id;
    return core::kInvalidObjectId;
}

int kindCode(core::ObjectKind kind) { return static_cast<int>(kind); }

bool depthKeyPresent(const core::Document& doc, const core::ObjectRef& ref) {
    return doc.extensions(ref).findValue(kDepthKey) != 0;
}

bool lockKeyPresent(const core::Document& doc, const core::ObjectRef& ref) {
    const std::string* v = doc.extensions(ref).findValue(kLockKey);
    return v && *v == "1";
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

} // namespace

AdvancedSideFile captureSide(const core::Document& doc) {
    AdvancedSideFile file;
    file.camera = readCamera(doc);
    file.hasCamera = doc.documentExtensions().findValue(kCameraKey) != 0;

    const std::vector<core::AtomRecord>& atoms = doc.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        core::ObjectRef ref(core::ObjectKind::Atom, atoms[i].id);
        const bool hasDepth = depthKeyPresent(doc, ref);
        const bool hasLock = lockKeyPresent(doc, ref);
        if (!hasDepth && !hasLock) continue;
        AdvancedRecord r;
        r.kind = core::ObjectKind::Atom;
        r.sourceId = atoms[i].sourceId;
        r.slot = atoms[i].id;
        r.hasDepth = hasDepth;
        r.depth = readObjectDepth(doc, ref);
        r.hasLock = hasLock;
        r.locked = hasLock;
        file.records.push_back(r);
    }
    const std::vector<core::BondRecord>& bonds = doc.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        if (!bonds[i].isAlive()) continue;
        core::ObjectRef ref(core::ObjectKind::Bond, bonds[i].id);
        const bool hasLock = lockKeyPresent(doc, ref);
        if (!hasLock) continue;
        AdvancedRecord r;
        r.kind = core::ObjectKind::Bond;
        r.sourceId = bonds[i].sourceId;
        r.slot = bonds[i].id;
        r.hasLock = true;
        r.locked = true;
        file.records.push_back(r);
    }
    const std::vector<core::TextRecord>& texts = doc.texts().records();
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        core::ObjectRef ref(core::ObjectKind::Text, texts[i].id);
        const bool hasDepth = depthKeyPresent(doc, ref);
        const bool hasLock = lockKeyPresent(doc, ref);
        if (!hasDepth && !hasLock) continue;
        AdvancedRecord r;
        r.kind = core::ObjectKind::Text;
        r.sourceId = texts[i].sourceId;
        r.slot = texts[i].id;
        r.hasDepth = hasDepth;
        r.depth = readObjectDepth(doc, ref);
        r.hasLock = hasLock;
        r.locked = hasLock;
        file.records.push_back(r);
    }
    return file;
}

core::Error writeSideFile(const AdvancedSideFile& file, const std::string& path) {
    const std::string temp = path + ".tmp";
    std::ofstream out(temp.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return core::Error(core::ErrorCode::IoError, "cannot open sidecar for write");
    out << "r62advanced " << file.formatVersion << "\n";
    if (file.hasCamera) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "camera %d %.6f %.6f %.6f %.6f %.6f\n",
                      file.camera.enabled ? 1 : 0, file.camera.yawDeg, file.camera.pitchDeg,
                      file.camera.depth, file.camera.pivot.x, file.camera.pivot.y);
        out << buf;
    }
    for (size_t i = 0; i < file.records.size(); ++i) {
        const AdvancedRecord& r = file.records[i];
        char buf[256];
        std::snprintf(buf, sizeof(buf), "record %d %d %u %d %.6f %d %d\n",
                      kindCode(r.kind), r.sourceId, r.slot,
                      r.hasDepth ? 1 : 0, r.depth, r.hasLock ? 1 : 0,
                      r.locked ? 1 : 0);
        out << buf;
    }
    out.flush();
    if (!out) {
        out.close();
        return core::Error(core::ErrorCode::IoError, "sidecar write failed");
    }
    out.close();
    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0)
        return core::Error(core::ErrorCode::IoError, "sidecar rename failed");
    return core::Error::success();
}

core::Error readSideFile(const std::string& path, AdvancedSideFile* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null sidecar");
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return core::Error(core::ErrorCode::IoError, "cannot open sidecar");
    *out = AdvancedSideFile();
    std::string line;
    bool header = false;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string keyword;
        ss >> keyword;
        if (keyword == "r62advanced") {
            int version = 0;
            ss >> version;
            if (version != kAdvancedSideFormatVersion)
                return core::Error(core::ErrorCode::ParseError, "unsupported sidecar version");
            header = true;
        } else if (keyword == "camera") {
            int enabled = 0;
            double yaw = 0, pitch = 0, depth = 0, px = 0, py = 0;
            if (!(ss >> enabled >> yaw >> pitch >> depth >> px >> py))
                return core::Error(core::ErrorCode::ParseError, "bad camera line");
            if (enabled != 0 && enabled != 1)
                return core::Error(core::ErrorCode::ParseError, "bad camera enabled flag");
            out->hasCamera = true;
            out->camera.enabled = enabled == 1;
            out->camera.yawDeg = yaw;
            out->camera.pitchDeg = pitch;
            out->camera.depth = depth;
            out->camera.pivot = core::Vec2(px, py);
        } else if (keyword == "record") {
            int kind = 0, sid = 0, hasDepth = 0, hasLock = 0, locked = 0;
            unsigned slot = 0;
            double depth = 0;
            if (!(ss >> kind >> sid >> slot >> hasDepth >> depth >> hasLock >> locked))
                return core::Error(core::ErrorCode::ParseError, "bad record line");
            if (hasDepth != 0 && hasDepth != 1)
                return core::Error(core::ErrorCode::ParseError, "bad hasDepth flag");
            if (hasLock != 0 && hasLock != 1)
                return core::Error(core::ErrorCode::ParseError, "bad hasLock flag");
            AdvancedRecord r;
            r.kind = static_cast<core::ObjectKind>(kind);
            r.sourceId = sid;
            r.slot = slot;
            r.hasDepth = hasDepth == 1;
            r.depth = depth;
            r.hasLock = hasLock == 1;
            r.locked = locked == 1;
            out->records.push_back(r);
        } else {
            return core::Error(core::ErrorCode::ParseError,
                               "unknown sidecar keyword at line " + std::to_string(lineNumber));
        }
    }
    if (!header)
        return core::Error(core::ErrorCode::ParseError, "missing sidecar header");
    return core::Error::success();
}

core::Error applySide(const AdvancedSideFile& file, core::Document& doc,
                      size_t* matched, size_t* unmatched) {
    size_t ok = 0, miss = 0;
    if (file.hasCamera) {
        doc.documentExtensions().set(kCameraKey, encodeCamera(file.camera));
        noteExtensionWrite(doc, core::ObjectRef(), true);
    }
    for (size_t i = 0; i < file.records.size(); ++i) {
        const AdvancedRecord& r = file.records[i];
        core::ObjectRef ref(r.kind, core::kInvalidObjectId);
        switch (r.kind) {
            case core::ObjectKind::Atom: {
                core::ObjectId id = findBySourceId(doc.atoms().records(), r.sourceId);
                if (id == core::kInvalidObjectId && doc.atoms().alive(r.slot))
                    id = r.slot;
                ref = core::ObjectRef(core::ObjectKind::Atom, id);
                break;
            }
            case core::ObjectKind::Bond: {
                core::ObjectId id = findBySourceId(doc.bonds().records(), r.sourceId);
                if (id == core::kInvalidObjectId && doc.bonds().alive(r.slot))
                    id = r.slot;
                ref = core::ObjectRef(core::ObjectKind::Bond, id);
                break;
            }
            case core::ObjectKind::Text: {
                core::ObjectId id = findBySourceId(doc.texts().records(), r.sourceId);
                if (id == core::kInvalidObjectId && doc.texts().alive(r.slot))
                    id = r.slot;
                ref = core::ObjectRef(core::ObjectKind::Text, id);
                break;
            }
            default:
                ref = core::ObjectRef(r.kind, core::kInvalidObjectId);
                break;
        }
        if (ref.id == core::kInvalidObjectId) { ++miss; continue; }
        if (r.hasDepth) {
            if (r.depth == 0.0) clearDepthQuiet(doc, ref);
            else writeDepthQuiet(doc, ref, r.depth);
        }
        if (r.hasLock) setLockedQuiet(doc, ref, r.locked);
        ++ok;
    }
    if (matched) *matched = ok;
    if (unmatched) *unmatched = miss;
    return core::Error::success();
}

uint64_t perspectiveStateDigest(const core::Document& doc) {
    uint64_t h = 1469598103934665603ULL;
    const std::string camera = encodeCamera(readCamera(doc));
    for (size_t i = 0; i < camera.size(); ++i) {
        h ^= static_cast<unsigned char>(camera[i]);
        h *= 1099511628211ULL;
    }
    std::vector<DepthSample> depths = readDepths(doc);
    for (size_t i = 0; i < depths.size(); ++i) {
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(depths[i].ref.kind));
        h *= 1099511628211ULL;
        h ^= depths[i].ref.id;
        h *= 1099511628211ULL;
        uint64_t bits = 0;
        double z = depths[i].z;
        std::memcpy(&bits, &z, sizeof(bits));
        h ^= bits;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string describePerspectiveState(const core::Document& doc) {
    const PerspectiveCamera c = readCamera(doc);
    const std::vector<DepthSample> depths = readDepths(doc);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "camera[enabled=%d yaw=%.3f pitch=%.3f depth=%.3f pivot=(%.3f,%.3f)] "
                  "depths=%llu digest=%016llx",
                  c.enabled ? 1 : 0, c.yawDeg, c.pitchDeg, c.depth, c.pivot.x, c.pivot.y,
                  static_cast<unsigned long long>(depths.size()),
                  static_cast<unsigned long long>(perspectiveStateDigest(doc)));
    return std::string(buf);
}

} // namespace advanced
} // namespace r62
