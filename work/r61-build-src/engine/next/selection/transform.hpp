#ifndef R62_NEXT_SELECTION_TRANSFORM_HPP
#define R62_NEXT_SELECTION_TRANSFORM_HPP
// =============================================================================
// re_round62_viewer/next/selection/transform.hpp
//
// T05 - GEOMETRIC TRANSFORMS AND LAYOUT.
//
// THE ONE THING THAT MAKES THIS HARD
// -----------------------------------------------------------------------------
// The document does not store a per-object transform matrix. Geometry is stored
// as positions, and DIFFERENT RECORD TYPES carry geometry in different fields:
//   Atom       : position
//   Text       : position (+ size, which is optionally scaled - see below)
//   Path       : points[] (an arbitrary count)
//   Annotation : geometry[4] + scale + rotation
//   Table      : frame
//   Bond       : NOTHING of its own - it is derived from its two atoms
//   Group      : NOTHING of its own - it is derived from its members
//
// So a transform is a per-kind dispatcher, and the dispatcher must be COMPLETE.
// A transform that silently skips a Table is the classic "I rotated the
// molecule and the table stayed" defect, and it is invisible in a screenshot of
// a molecule. The tests count what each transform touched by kind.
//
// BONDS ARE DERIVED, AND THAT IS A FEATURE
// -----------------------------------------------------------------------------
// A bond's endpoints ARE its atoms' positions, so moving an atom moves its bonds
// for free and a bond can never become disconnected from its atoms. Selecting a
// bond and translating it therefore means "translate its two atoms" - NOT
// "translate the bond", which has nowhere to store the result. transformObjects
// does exactly that and reports which atoms were moved on the bond's behalf.
//
// TEXT SIZE AND GEOMETRIC SCALE: THE EXPLICIT RULE
// -----------------------------------------------------------------------------
// Does a caption's font size scale with a geometric scale? There is a real
// tradeoff and the task demands an EXPLICIT rule rather than a default nobody
// can point at. The rule implemented here is:
//
//   TextSizePolicy::PreservePointSize  (DEFAULT)
//     Geometry scales, the em size does NOT. A scale of 2 makes a caption's
//     anchor twice as far from the pivot but its glyphs stay 10pt. This is the
//     behaviour a chemist wants for "stretch my scheme wider": the labels stay
//     legible and comparable to other labels in the document.
//
//   TextSizePolicy::ScaleWithGeometry
//     The em size is multiplied by the uniform factor (the geometric mean of
//     sx and sy, so a pure single-axis stretch does not produce a non-integer
//     font size nobody asked for). Choose this for an "enlarge the whole figure"
//     operation.
//
//   TextSizePolicy::MatchUniformOnly
//     Scale the size only when sx == sy, i.e. a true uniform scale. A
//     single-axis stretch never changes point size. This is the middle ground
//     and the rule a "stretch horizontally" tool should use.
//
// A per-object size of 0 means "inherit the document default"; that is left at
// 0 under every policy, because writing a concrete number there would FREEZE
// the object against a later document-default change.
//
// NO TRANSFORM WRITES A BOND'S ORDER OR SEMANTICS. A transform moves geometry
// and optionally scales a text em. It never touches element, charge, bond order,
// display or stereo - which is what "align/distribute must not alter chemical
// references or silently change bond order" requires, structurally rather than
// by review.
// =============================================================================

#include "next/selection/bounds.hpp"
#include "next/selection/selection_set.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// Text size policy (see the header block above)
// ---------------------------------------------------------------------------
enum class TextSizePolicy : uint8_t {
    PreservePointSize = 0,
    MatchUniformOnly = 1,
    ScaleWithGeometry = 2
};

const char* textSizePolicyName(TextSizePolicy policy);

// ---------------------------------------------------------------------------
// A plane transform expressed as a 2x3 matrix
// ---------------------------------------------------------------------------
//   x' = m00*x + m01*y + m02
//   y' = m10*x + m11*y + m12
// One representation for every operation below, because a rotation followed by
// a flip must equal a single matrix - otherwise "rotate twice then undo" can
// drift by a rounding step per call.
struct Affine2 {
    double m00, m01, m02;
    double m10, m11, m12;
    Affine2() : m00(1), m01(0), m02(0), m10(0), m11(1), m12(0) {}

    static Affine2 identity() { return Affine2(); }
    static Affine2 translation(const core::Vec2& delta);
    static Affine2 rotation(const core::Vec2& pivot, double degrees);
    // Scale about a pivot. sx/sy may be negative (that is what a flip is).
    static Affine2 scaling(const core::Vec2& pivot, double sx, double sy);
    static Affine2 flip(const core::Vec2& pivot, bool horizontal);
    // Composition: apply `first`, then `second`.
    static Affine2 compose(const Affine2& first, const Affine2& second);

    core::Vec2 apply(const core::Vec2& p) const;
    // Determinant. Negative means the matrix mirrors (a single flip).
    double determinant() const { return m00 * m11 - m01 * m10; }
    bool finite() const;
    // The uniform scale factor: the geometric mean of the two axis scales. Used
    // by TextSizePolicy::ScaleWithGeometry. Returns 0 when degenerate.
    double uniformFactor() const;
    bool isUniform() const;
    bool isTranslationOnly() const;
    std::string describe() const;
};

// ---------------------------------------------------------------------------
// TransformReport: what actually changed, BY KIND
// ---------------------------------------------------------------------------
struct TransformReport {
    uint64_t atomsMoved;
    uint64_t textsMoved;
    uint64_t pathsMoved;
    uint64_t annotationsMoved;
    uint64_t tablesMoved;
    uint64_t groupsTouched;         // groups whose members were transformed
    uint64_t bondEndpointsMoved;    // atoms moved ON BEHALF OF a selected bond
    uint64_t bondsUnaffected;       // bonds whose atoms were not selected
    uint64_t textSizesChanged;
    uint64_t refsSkippedDead;
    uint64_t refsSkippedUnsupported;
    // Layout bookkeeping for align/distribute. A "unit" is one independently
    // movable group of selected objects: a whole molecule/fragment, a group, or
    // a single non-chemical object. transformObjects() leaves these at zero.
    uint64_t objectsConsidered;
    uint64_t objectsMoved;
    TransformReport();
    void clear();
    uint64_t objectsChanged() const {
        return atomsMoved + textsMoved + pathsMoved + annotationsMoved + tablesMoved;
    }
    std::string summary() const;
};

// ---------------------------------------------------------------------------
// TransformOptions
// ---------------------------------------------------------------------------
struct TransformOptions {
    TextSizePolicy textSizePolicy;
    // Skip objects outside this document-space box (a partial-document edit).
    bool   useRegionFilter;
    core::Box region;
    // Move a bond's endpoints when the bond is selected but its atoms are not.
    // TRUE is the only useful setting for a UI: a bond with no stored geometry
    // cannot otherwise move. It is switchable so a test can prove the
    // alternative is a no-op rather than a silent half-move.
    bool   moveBondEndpoints;
    TransformOptions()
        : textSizePolicy(TextSizePolicy::PreservePointSize),
          useRegionFilter(false), region(), moveBondEndpoints(true) {}
};

// ---------------------------------------------------------------------------
// The transform entry point
// ---------------------------------------------------------------------------
// Apply `matrix` to every object in `refs`. BONDS ARE EXPANDED: a selected bond
// contributes its endpoint atoms (recorded in bondEndpointsMoved). A GROUP is
// expanded into its members (members are transformed, the group record itself
// has no geometry to change). OPTIONS.textSizePolicy decides the em-size rule.
TransformReport transformObjects(core::Document& document,
                                 const std::vector<core::ObjectRef>& refs,
                                 const Affine2& matrix,
                                 const TransformOptions& options);

// Convenience wrappers. Each returns what it touched.
inline TransformReport translateObjects(core::Document& document,
                                        const std::vector<core::ObjectRef>& refs,
                                        const core::Vec2& delta,
                                        const TransformOptions& options = TransformOptions()) {
    return transformObjects(document, refs, Affine2::translation(delta), options);
}
TransformReport rotateObjects(core::Document& document,
                              const std::vector<core::ObjectRef>& refs,
                              const core::Vec2& pivot, double degrees,
                              const TransformOptions& options = TransformOptions());
// Uniform scale in both axes.
TransformReport scaleObjects(core::Document& document,
                             const std::vector<core::ObjectRef>& refs,
                             const core::Vec2& pivot, double factor,
                             const TransformOptions& options = TransformOptions());
// Independent axis scales == single-axis stretch when one factor is 1.
TransformReport stretchObjects(core::Document& document,
                               const std::vector<core::ObjectRef>& refs,
                               const core::Vec2& pivot, double sx, double sy,
                               const TransformOptions& options = TransformOptions());
TransformReport flipObjects(core::Document& document,
                            const std::vector<core::ObjectRef>& refs,
                            const core::Vec2& pivot, bool horizontal,
                            const TransformOptions& options = TransformOptions());

// ---------------------------------------------------------------------------
// Translation of exact positions (preview/commit agreement)
// ---------------------------------------------------------------------------
// A drag resolves positions ONCE (through snapping) and commits the resolved
// values. Re-deriving them from a delta would let a preview and its commit
// disagree by a snap step - a defect this project has already recorded. So the
// preview publishes absolute positions and this function writes exactly them.
struct AtomPlacement {
    core::ObjectId atom;
    core::Vec2     position;
    AtomPlacement() : atom(core::kInvalidObjectId), position() {}
    AtomPlacement(core::ObjectId a, const core::Vec2& p) : atom(a), position(p) {}
};
// Absolute placement for the atoms a drag moved, plus the BONDS whose other end
// did not move (so they stay attached - which they do automatically because a
// bond has no stored geometry; the parameter exists so the report can name them).
TransformReport placeAtoms(core::Document& document,
                           const std::vector<AtomPlacement>& placements,
                           TransformReport* report);

// ---------------------------------------------------------------------------
// ALIGNMENT
// ---------------------------------------------------------------------------
enum class AlignMode : uint8_t {
    Left = 0, HorizontalCenter, Right, Top, VerticalCenter, Bottom
};
const char* alignModeName(AlignMode mode);

// What the alignment is measured against. `SelectionBounds` is the T05 default;
// `KeyObject` and `Page` exist because a real align panel offers them, and the
// difference is user-visible. Only SelectionBounds is exercised by the tests.
enum class AlignReference : uint8_t { SelectionBounds = 0, Page = 1, KeyObject = 2 };

struct AlignOptions {
    AlignReference reference;
    // Page box used by AlignReference::Page. Ignored otherwise.
    core::Box page;
    // The object every other object aligns to (AlignReference::KeyObject).
    core::ObjectRef keyObject;
    TextSizePolicy textSizePolicy;
    // Measured ink box of a Text object. When null, a caption boxes as its
    // anchor point (the documented "we cannot measure" fallback). A host that
    // has a font engine supplies it so left/centre/right mean the same thing
    // for a caption as for the selection rectangle drawn around it.
    const TextBoundsProvider* textBounds;
    AlignOptions() : reference(AlignReference::SelectionBounds), page(),
                     keyObject(), textSizePolicy(TextSizePolicy::PreservePointSize),
                     textBounds(0) {}
};

// Align every independent unit in `refs` to the reference edge.
//
// THE UNIT RULE (this is what keeps a molecule from collapsing)
// -----------------------------------------------------------------------------
// A selection of atoms and bonds is NOT a bag of independently alignable
// points. If every atom were aligned on its own, a benzene ring selected as a
// whole would collapse onto a vertical line - the defect this rule exists to
// prevent. So the selection is first split into LAYOUT UNITS:
//   * a connected fragment whose atoms are ALL selected is one unit (it moves
//     by one delta and keeps its internal geometry);
//   * a group is one unit (all members move by one delta);
//   * a selected bond is one unit (its two endpoint atoms move together);
//   * a text/path/table/annotation is one unit;
//   * only a PARTIAL selection of a fragment (a strict subset of its atoms) is
//     split into individual atom units, which is the deliberate way to align
//     substituents inside one molecule.
// Units that share an atom are merged, so a bond plus one of its atoms never
// moves the atom twice. `objectsConsidered` / `objectsMoved` report the units.
//
// Align never changes bond order or chemistry. Reference frames:
//   SelectionBounds - the union of the units (the default);
//   Page            - the page box;
//   KeyObject       - the unit containing the key object, which stays fixed.
TransformReport alignObjects(core::Document& document,
                             const std::vector<core::ObjectRef>& refs,
                             AlignMode mode, const AlignOptions& options,
                             TransformReport* report);

// ---------------------------------------------------------------------------
// DISTRIBUTION
// ---------------------------------------------------------------------------
enum class DistributeMode : uint8_t {
    HorizontalCenters = 0,   // equal spacing between object centres
    VerticalCenters,
    HorizontalGaps,          // equal GAPS between adjacent bounding boxes
    VerticalGaps
};
const char* distributeModeName(DistributeMode mode);

// Requires at least three objects with a measurable extent; fewer than three is
// reported as a no-op with a reason in DistributeReport.why, not as success.
struct DistributeReport {
    TransformReport transform;
    uint64_t objectsConsidered;
    uint64_t objectsMoved;
    bool     applied;
    std::string why;    // non-empty when applied == false
    DistributeReport() : objectsConsidered(0), objectsMoved(0), applied(false) {}
};
struct DistributeOptions {
    // Same contract as AlignOptions::textBounds: a caption is distributed by
    // its measured ink box instead of its anchor point.
    const TextBoundsProvider* textBounds;
    DistributeOptions() : textBounds(0) {}
};
DistributeReport distributeObjects(core::Document& document,
                                   const std::vector<core::ObjectRef>& refs,
                                   DistributeMode mode);
DistributeReport distributeObjects(core::Document& document,
                                   const std::vector<core::ObjectRef>& refs,
                                   DistributeMode mode,
                                   const DistributeOptions& options);

// ---------------------------------------------------------------------------
// PAGE / SELECTION CENTRING
// ---------------------------------------------------------------------------
// Move the whole selection so its bounds centre coincides with the page centre
// (or with an explicit target point). This is a TRANSLATION of every object by
// one delta, so nothing is deformed and no bond order is involved.
TransformReport centerSelection(core::Document& document,
                                const std::vector<core::ObjectRef>& refs,
                                const core::Box& page);

// ---------------------------------------------------------------------------
// Keyboard nudge
// ---------------------------------------------------------------------------
// [G124] Shift+arrow moves the selection 10pt. A plain arrow is a SMALLER step
// and the guide does not state its size, so it is a PROJECT value exposed as a
// parameter rather than asserted as ChemDraw's. The nudge is a translation of
// OBJECTS, not of a caret: an atom is an object, and a nudge over a selection
// of 5,000 atoms is one transaction.
struct NudgeProfile {
    double stepShift;       // [G124]: 10 document units
    double stepPlain;       // PROJECT: 1 document unit
    NudgeProfile() : stepShift(10.0), stepPlain(1.0) {}
};
struct NudgeVector {
    int dx;   // -1, 0, +1
    int dy;
    NudgeVector() : dx(0), dy(0) {}
    NudgeVector(int x, int y) : dx(x), dy(y) {}
    bool zero() const { return dx == 0 && dy == 0; }
};
core::Vec2 nudgeDelta(const NudgeVector& direction, bool shift,
                      const NudgeProfile& profile = NudgeProfile());

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_TRANSFORM_HPP
