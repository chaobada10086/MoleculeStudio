#ifndef R62_NEXT_ADVANCED_COMMON_HPP
#define R62_NEXT_ADVANCED_COMMON_HPP
// =============================================================================
// next/advanced/common.hpp  --  T17 shared vocabulary
//
// T17 owns the STRUCTURE CLEANUP, STRUCTURE PERSPECTIVE and FRAGMENT tools.
//
// The public document model is T01's (next/core). This header adds NOTHING to
// that model: no new slab, no new field on AtomRecord/BondRecord. Everything T17
// needs beyond the model is carried either
//
//   * in a PLAIN, COPYABLE SNAPSHOT (LayoutInput), or
//   * in the T01 EXTENSION BAG under the keys below.
//
// The snapshot exists for two reasons. First, it is what makes a layout usable
// on a worker thread: a core::Document belongs to one thread and T17 must not
// hand it to another one (next/core/THREADING.md). Second, it makes the layout
// math testable without constructing a Document.
//
// WHAT THIS FILE IS NOT
// -----------------------------------------------------------------------------
//   * It is not a second document model. LayoutInput is a read-only projection
//     of the model, never a place edits are kept.
//   * It does not decide chemistry. Valence, implicit hydrogen and mass come
//     from T06 (next/chemistry) wherever T17 needs them.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/core/contracts.hpp"
#include "next/core/document.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace advanced {

// ---------------------------------------------------------------------------
// Extension keys owned by T17. All live in the T01 extension bag, so they are
// part of the document digest, part of a transaction patch and part of the
// native .t01doc file (T01 serialization writes every object bag).
// ---------------------------------------------------------------------------
// "advanced.perspective.z"  : per-object out-of-plane depth, decimal double.
//                             A separate field, exactly as the task requires;
//                             the object's 2D position is NOT overwritten.
// "advanced.perspective.camera" : document-level camera of the perspective view.
//                             Written in the SAME transaction as the depths of a
//                             tilt, so undo restores the appearance as one step.
// "advanced.locked"         : "1" == a cleanup/arrange plan must not move this.
// "advanced.anchor"         : "1" == a user-fixed anchor; cleanup never moves it.
extern const char* const kDepthKey;
extern const char* const kCameraKey;
extern const char* const kLockKey;
extern const char* const kAnchorKey;

// ---------------------------------------------------------------------------
// Provenance. The task requires that a capability be labelled honestly.
// ---------------------------------------------------------------------------
enum class Provenance : uint8_t {
    NativeBacked = 0,   // actually executed through a verified native host port
    OwnImplemented = 1, // implemented here, fully deterministic, and tested
    Unsupported = 2     // deliberately not implemented; never silently faked
};
const char* provenanceName(Provenance p);

// ---------------------------------------------------------------------------
// LayoutInput - a plain read-only snapshot of everything a layout needs.
// ---------------------------------------------------------------------------
// No pointers, no document reference: safe to copy to a worker thread.
struct LayoutAtom {
    core::ObjectId id;
    core::Vec2     position;
    int            element;
    bool           locked;
    bool           anchor;
    bool           hasDepth;
    double         depth;
    LayoutAtom()
        : id(core::kInvalidObjectId), element(6), locked(false), anchor(false),
          hasDepth(false), depth(0.0) {}
};

struct LayoutBond {
    core::ObjectId id;
    core::ObjectId a;
    core::ObjectId b;
    uint8_t        order;       // core::BondOrder
    uint8_t        display;     // core::BondDisplay
    uint8_t        semantics;   // core::BondSemantics
    uint8_t        stereo;      // core::BondStereo
    bool           aromatic;
    LayoutBond()
        : id(core::kInvalidObjectId), a(core::kInvalidObjectId), b(core::kInvalidObjectId),
          order(1), display(0), semantics(0), stereo(0), aromatic(false) {}
};

struct LayoutText {
    core::ObjectId id;
    core::Vec2     position;
    double         size;        // 0 == document default
    std::string    text;
    bool           locked;
    LayoutText() : id(core::kInvalidObjectId), size(0.0), locked(false) {}
};

// An arrow annotation reduced to the only thing a layout needs: its segment.
// Reaction arrangement uses it to decide which side of the scheme a component
// belongs on. It is NOT a reaction model: the arrow slab has no reaction
// semantics and this module does not invent any.
struct LayoutArrow {
    core::ObjectId id;
    core::Vec2     a;
    core::Vec2     b;
    LayoutArrow() : id(core::kInvalidObjectId) {}
};

struct LayoutInput {
    std::vector<LayoutAtom> atoms;     // ascending id
    std::vector<LayoutBond> bonds;     // ascending id
    std::vector<LayoutText> texts;     // ascending id
    std::vector<LayoutArrow> arrows;   // ascending id
    double          defaultBondLength;
    double          defaultTextSize;
    core::DocumentId documentId;
    uint64_t        revision;
    uint64_t        structureSerial;
    LayoutInput()
        : defaultBondLength(14.4), defaultTextSize(10.0), documentId(0),
          revision(0), structureSerial(0) {}
};

// Read the snapshot. ObjectId order is ascending so every downstream result is
// reproducible without sorting by an unstable comparator.
core::Error captureLayoutInput(const core::Document& doc, LayoutInput* out);

// Index helpers over a snapshot.
const LayoutAtom* findAtom(const LayoutInput& in, core::ObjectId id);

// ---------------------------------------------------------------------------
// Lock / anchor / depth accessors (document side)
// ---------------------------------------------------------------------------
bool   isLocked(const core::Document& doc, const core::ObjectRef& ref);
void   setLockedQuiet(core::Document& doc, const core::ObjectRef& ref, bool locked);
bool   isAnchored(const core::Document& doc, core::ObjectId atom);

double readDepth(const core::Document& doc, const core::ObjectRef& ref);
void   writeDepthQuiet(core::Document& doc, const core::ObjectRef& ref, double depth);
void   clearDepthQuiet(core::Document& doc, const core::ObjectRef& ref);

// Tell the recording transaction about a bag write. T07 measured that
// Document::extensions() does NOT notify the session on its own; without this
// call an undo silently leaves the new value in place.
void   noteExtensionWrite(core::Document& doc, const core::ObjectRef& ref,
                          bool documentLevel);

// ---------------------------------------------------------------------------
// Connected components
// ---------------------------------------------------------------------------
struct Component {
    std::vector<core::ObjectId> atoms;   // ascending
    std::vector<core::ObjectId> bonds;   // ascending
    Component() {}
};

// Deterministic: components are ordered by their smallest atom id, and the
// atoms inside a component are ascending.
std::vector<Component> atomComponents(const LayoutInput& in);
std::vector<Component> atomComponents(const core::Document& doc);

core::Vec2 componentCentroid(const LayoutInput& in, const Component& c);
core::Box  componentBounds(const LayoutInput& in, const Component& c);

// ---------------------------------------------------------------------------
// Structure signature: a coordinate-independent invariant.
// ---------------------------------------------------------------------------
// Purpose: prove that a cleanup / perspective / fragment plan did or did not
// change the CHEMISTRY. Every field EXCEPT the coordinates contributes:
//
//   * atoms        : element, charge, radical, isotope, implicit H, node type,
//                    show-label, aromatic flag, arbitrary atom-label override
//   * bonds        : begin/end TOPOLOGY, order, display, semantics, stereo
//   * texts        : role, format, run content (not position)
//   * every other slab : object counts and their non-geometric fields
//   * extensions   : key/value/sourceId (T17 distinguishes its own keys)
//
// IDs are NOT hashed: cleanup must not depend on, or be validated by, the
// accident of which slot an atom occupies. The invariant is computed with a
// Weisfeiler-Lehman style refinement, so a multiset of atoms is not enough -
// the graph is actually used.
struct StructureSignature {
    uint64_t              hash;
    uint64_t              graphHash;      // topology + per-atom labels only
    uint64_t              attributeHash;  // labels/text/extension attributes
    size_t                atomCount;
    size_t                bondCount;
    size_t                textCount;
    size_t                componentCount;
    std::vector<uint64_t> componentHashes;  // one per component, sorted
    StructureSignature()
        : hash(0), graphHash(0), attributeHash(0), atomCount(0), bondCount(0),
          textCount(0), componentCount(0) {}
};

StructureSignature structureSignature(const core::Document& doc);
uint64_t           structureHash(const core::Document& doc);
std::string        describeSignature(const StructureSignature& sig);

// Convenience counts used by the coverage/demo output.
size_t aliveAtomCount(const core::Document& doc);
size_t aliveBondCount(const core::Document& doc);

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_COMMON_HPP
