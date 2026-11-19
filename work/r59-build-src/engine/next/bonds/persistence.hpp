#ifndef R62_NEXT_BONDS_PERSISTENCE_HPP
#define R62_NEXT_BONDS_PERSISTENCE_HPP
// =============================================================================
// re_round62_viewer/next/bonds/persistence.hpp
//
// T07 - persistence of the bond state that the CORE MODEL DOES NOT OWN, plus the
// verification that the state it DOES own survives a save/load.
//
// WHAT THE CORE ALREADY PERSISTS (verified, not assumed)
//   core::BondRecord has order / display / semantics / stereo, and
//   next/core/serialization.cpp writes all four (lines 189-192) and reads them
//   back (lines 446-449). Native .t01doc round-trips them WITHOUT help from T07.
//
// WHAT T07 MUST CARRY ITSELF
//   The double-bond SIDE placement. The T01 model has no side field on the order
//   axis, and core/INTERFACES.md section 6 forbids smuggling it into order. It is
//   therefore stored in the bond EXTENSION BAG under a user.bonds.* key, which
//   core::isPersistentExtensionKey() deliberately reports as NOT persistent
//   (extension bags are T02 business, and T02 correctly refuses to invent a
//   writer for keys it does not know).
//
//   So T07 provides its own sidecar: a tiny, versioned, append-only text file.
//   It is NOT a replacement for the T02 document format and does NOT claim to be
//   one. It records exactly the delta T07 owns, so the pair
//   (T02 .t01doc + T07 .r62bonds) is lossless for everything T07 created.
// =============================================================================

#include "next/bonds/edit.hpp"
#include "next/core/document.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace bonds {

// ---------------------------------------------------------------------------
// Sidecar record
// ---------------------------------------------------------------------------
struct BondSideRecord {
    int32_t    sourceId;    // the CDXML id, stable across a T02 round trip
    uint32_t   slot;        // internal slot id, used when sourceId == 0
    DoubleSide side;
    BondSideRecord() : sourceId(0), slot(0), side(DoubleSide::Symmetric) {}
};

struct BondSideFile {
    int                         formatVersion;
    std::vector<BondSideRecord> records;
    BondSideFile() : formatVersion(1) {}
};

// ---------------------------------------------------------------------------
// Capture / restore
// ---------------------------------------------------------------------------
// Capture the side placement of every alive bond that has one. A record carries
// BOTH the source id and the internal slot, so a match can prefer the id that
// survives the caller transformation.
BondSideFile captureSide(const core::Document& document);

// Write the sidecar. Text, UTF-8, one record per line, stable field order.
core::Error writeSideFile(const BondSideFile& file, const std::string& path);

// Read the sidecar. Refuses an unknown format version rather than guessing.
core::Error readSideFile(const std::string& path, BondSideFile* out);

// Apply a sidecar to a document. Returns the number of bonds it matched. A
// record that matches nothing is NOT an error (the bond may have been deleted
// between save and load) but it is counted so the caller can report it.
core::Error applySide(const BondSideFile& file, core::Document& document,
                      size_t* matched, size_t* unmatched);

// ---------------------------------------------------------------------------
// Round-trip verification helper used by the acceptance test
// ---------------------------------------------------------------------------
// A digest of the FOUR CORE AXES plus the side for every alive bond, in slot
// order. Equal before and after a save/load means the whole bond state survived.
// The side is read from the extension bag, so this also proves the bag survived.
uint64_t bondStateDigest(const core::Document& document);

// Human-readable rendering of one bond state, for the test log and RESULTS.md.
std::string describeBondState(const core::Document& document);

} // namespace bonds
} // namespace r62

#endif // R62_NEXT_BONDS_PERSISTENCE_HPP
