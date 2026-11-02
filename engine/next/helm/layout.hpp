#ifndef R62_NEXT_HELM_LAYOUT_HPP
#define R62_NEXT_HELM_LAYOUT_HPP
// =============================================================================
// re_round62_viewer/next/helm/layout.hpp
//
// T15 - SEQUENCE LAYOUT. THIS FILE PRODUCES VIEW GEOMETRY ONLY.
//
// HARD RULE
// -----------------------------------------------------------------------------
// Lay out a model and the CHEMISTRY must be byte-identical afterwards. This is
// not a convention; the layout functions take a CONST SequenceModel, write into a
// separate SequenceLayout, and never touch connections. A test asserts
// model.chemistryDigest() is unchanged across a re-layout, and that the
// attachment serial does not move.
//
// WHERE THE RULES COME FROM
// -----------------------------------------------------------------------------
//  * A DNA/RNA residue is three monomers - ribose, phosphate, base - and on clean
//    up 'ribose and phosphate form the main sequence and the base is positioned
//    vertically under the ribose on the grid' (guide PDF page 194).
//  * 'A chem monomer attached to a DNA/RNA or peptide monomer is oriented
//    downwards orthogonal to the mainline sequence.' (page 194)
//  * A residue can be forced onto a new line with ALT+ENTER (page 191), which is
//    why MonomerLayout carries lineBreakBefore.
//  * When two DNA/RNA sequences are linked by hydrogen bonds, 'the sequences are
//    lined up with bases facing each other' (page 194). complementLayout()
//    implements exactly that, and NOTHING ELSE in this module moves a polymer
//    because of a hydrogen bond.
//  * A hairpin is 'a single stranded RNA or DNA sequence/fragment that forms a
//    duplex with itself via intramolecular base pairing', with 'a loop of
//    unpaired nucleotides on one end' (page 194).
//
// WHAT IS DELIBERATELY NOT HERE
// -----------------------------------------------------------------------------
//  * No chemistry. No bond creation, no attachment selection, no validation.
//  * No text measurement. Glyph metrics belong to T09 (next/text); this module
//    emits positions and lets the shared renderer place labels. Where a size is
//    needed (a marker radius) it is expressed in DOCUMENT UNITS as a parameter.
//  * No pixels and no DPI. Every number here is a document-space length, per
//    next/core/contracts.hpp.
// =============================================================================

#include "next/helm/sequence.hpp"

#include <vector>

namespace r62 {
namespace helm {

// ----------------------------------------------------------------------------
// Options
// ----------------------------------------------------------------------------
struct LayoutOptions {
    double residuePitch;   // document units between adjacent backbone residues
    double linePitch;      // document units between wrapped lines
    double branchDrop;     // document units a nucleobase sits below its sugar
    size_t residuesPerLine;   // 0 == do not wrap
    // Offset each successive polymer down by this much so two chains do not
    // overlap before complementLayout() is given a chance to pair them.
    double polymerRowGap;
    LayoutOptions()
        : residuePitch(14.4), linePitch(18.0), branchDrop(12.0),
          residuesPerLine(0), polymerRowGap(36.0) {}
};

// ----------------------------------------------------------------------------
// Layout entry points
// ----------------------------------------------------------------------------
// Lay out a whole model. The returned layout is a pure function of the model's
// monomer order and the options, so calling it twice gives the same answer.
SequenceLayout layoutSequence(const SequenceModel& model, const LayoutOptions& options);

// Lay out ONE polymer, appending into `layout`. Used by incremental editing so a
// local change does not recompute five thousand residues.
void layoutPolymer(const SequenceModel& model, size_t polymerIndex,
                   const LayoutOptions& options, const core::Vec2& origin,
                   SequenceLayout* layout);

// Lay out ONE polymer starting at `position`, writing only from that index on.
// This is the 'local edit does not re-place the whole chain' entry point: the
// caller passes the index of the first residue that actually moved.
void layoutPolymerFrom(const SequenceModel& model, size_t polymerIndex,
                       size_t fromPosition, const LayoutOptions& options,
                       const core::Vec2& origin, SequenceLayout* layout);

// ----------------------------------------------------------------------------
// Complementary strands (stage 2)
// ----------------------------------------------------------------------------
// Place `antisenseIndex` below `senseIndex` so that the hydrogen-bonded pairs
// face each other, as the guide describes. Residues are aligned per PAIR: the
// antisense residue is placed directly under its partner, and the antisense
// direction is reversed so the two chains read in opposite directions, which is
// what makes the bases face each other.
//
// REFUSES (returns a positioned error) when:
//   * either index is out of range or the two indices are equal;
//   * the two polymers are not both nucleic (the guide pairs DNA/RNA only);
//   * there is not exactly one pairing relationship, i.e. no hydrogen connection
//     joins them at all.
core::Error complementLayout(const SequenceModel& model, size_t senseIndex,
                            size_t antisenseIndex, const LayoutOptions& options,
                            SequenceLayout* layout, size_t* outPairsPlaced);

// ----------------------------------------------------------------------------
// Hairpin (stage 2)
// ----------------------------------------------------------------------------
// Detect an intramolecular hairpin: a single nucleic polymer whose hydrogen
// connections pair residues of ITSELF, leaving an unpaired loop at one end.
// Detection only - it reports which positions pair and which form the loop. The
// layout of a hairpin is produced by complementLayout-style folding in
// layoutHairpin().
struct HairpinDetection {
    bool     isHairpin;
    size_t   polymerIndex;
    size_t   pairCount;
    size_t   loopStart;     // first unpaired position of the loop
    size_t   loopEnd;       // one past the last unpaired loop position
    // The stem pairs, as (5' position, 3' position).
    std::vector<std::pair<size_t, size_t> > stemPairs;
    std::string reason;     // why it is NOT a hairpin, when it is not
    HairpinDetection() : isHairpin(false), polymerIndex(0), pairCount(0),
                         loopStart(0), loopEnd(0) {}
};

// Detect a hairpin on one polymer. Uses ONLY hydrogen connections that join the
// polymer to itself; a covalent cross-link is not a base pair.
HairpinDetection detectHairpin(const SequenceModel& model, size_t polymerIndex);

// Fold a detected hairpin so the stem duplex is drawn as two facing runs with the
// loop at the right-hand end (the guide: 'The loop will be towards right in a
// general HELM sequence', PDF page 195). Returns an error when the polymer is not
// a hairpin; the caller is expected to have called detectHairpin first.
core::Error layoutHairpin(const SequenceModel& model, const HairpinDetection& detection,
                          const LayoutOptions& options, SequenceLayout* layout);

// ----------------------------------------------------------------------------
// Bounds
// ----------------------------------------------------------------------------
// The document-space bounding box of every placed residue, expanded by `radius`
// so a caller can cover a label or a cartoon shape without measuring text here.
core::Box layoutBounds(const SequenceLayout& layout, double radius);

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_LAYOUT_HPP
