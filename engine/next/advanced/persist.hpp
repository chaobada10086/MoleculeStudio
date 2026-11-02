#ifndef R62_NEXT_ADVANCED_PERSIST_HPP
#define R62_NEXT_ADVANCED_PERSIST_HPP
// =============================================================================
// next/advanced/persist.hpp  --  T17 sidecar for perspective and locks
//
// The T01 native format already persists the whole object extension bag, so a
// .t01doc round trip keeps advanced.perspective.z, the camera and the lock
// flags without any help from this file (that fact is asserted by a test, not
// assumed). This sidecar covers the OTHER paths: CDXML import, template paste
// and host hand-off, where a document can be rebuilt from chemistry and lose
// module-private extension keys.
//
// It is deliberately a plain text format, one record per line, so a reviewer can
// diff it. Matching prefers the source object id (stable inside one source
// file) and falls back to the slot only when the source id is absent.
// =============================================================================

#include "next/advanced/perspective.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace advanced {

enum { kAdvancedSideFormatVersion = 1 };

struct AdvancedRecord {
    core::ObjectKind kind;
    int32_t          sourceId;
    uint32_t         slot;
    bool             hasDepth;
    double           depth;
    bool             hasLock;
    bool             locked;
    AdvancedRecord()
        : kind(core::ObjectKind::Unknown), sourceId(0), slot(0),
          hasDepth(false), depth(0.0), hasLock(false), locked(false) {}
};

struct AdvancedSideFile {
    int                    formatVersion;
    bool                   hasCamera;
    PerspectiveCamera      camera;
    std::vector<AdvancedRecord> records;
    AdvancedSideFile() : formatVersion(kAdvancedSideFormatVersion), hasCamera(false) {}
};

AdvancedSideFile captureSide(const core::Document& doc);
core::Error      writeSideFile(const AdvancedSideFile& file, const std::string& path);
core::Error      readSideFile(const std::string& path, AdvancedSideFile* out);

// Apply depth and lock records. matched/unmatched count objects that were found
// / not found. Writing goes through the extension bag and notifies the recording
// session, so this participates in undo like any other command body.
core::Error applySide(const AdvancedSideFile& file, core::Document& doc,
                      size_t* matched, size_t* unmatched);

// Digest of exactly the perspective state: camera + every depth. Two documents
// with the same geometry but different tilt differ in this value.
uint64_t perspectiveStateDigest(const core::Document& doc);
std::string describePerspectiveState(const core::Document& doc);

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_PERSIST_HPP
