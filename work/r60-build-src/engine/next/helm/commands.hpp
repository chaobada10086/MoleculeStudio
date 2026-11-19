#ifndef R62_NEXT_HELM_COMMANDS_HPP
#define R62_NEXT_HELM_COMMANDS_HPP
// =============================================================================
// re_round62_viewer/next/helm/commands.hpp
//
// T15 - SEQUENCE EDIT COMMANDS, ISSUED THROUGH T01.
//
// WHY COMMANDS AND NOT DIRECT MODEL EDITS
// -----------------------------------------------------------------------------
// T01 states the rule: 'ONE user action == ONE atomic transaction == ONE undo
// step', and history stores incremental patches, never snapshots. Every edit in
// this file therefore:
//
//   1. reads what it needs, validates, and REFUSES with a positioned diagnostic if
//      the edit would be invalid - before touching anything;
//   2. opens ONE core::Transaction or uses core::CommandLog::execute;
//   3. mutates through the sequence primitives and the document adapter;
//   4. commits, so undo restores the exact previous digest.
//
// A command that fails changes NOTHING: it validates first, so a failed replace
// cannot leave a half-edited sequence.
//
// HOW THE DOCUMENT AND THE SEQUENCE STAY IN STEP
// -----------------------------------------------------------------------------
// The command owns BOTH models. After mutating the sequence it re-maps into the
// document inside the SAME transaction. That re-map is what makes the T01 undo
// stack contain the real change, so Ctrl+Z restores the previous sequence state
// exactly as it restores geometry.
//
// HONEST LIMIT: re-mapping writes the whole sequence, so the incremental patch is
// proportional to the sequence size. That is acceptable for the thousands of
// monomers this module targets and is MEASURED in RESULTS.md rather than assumed.
// =============================================================================

#include "next/helm/document_map.hpp"
#include "next/helm/layout.hpp"
#include "next/helm/sequence.hpp"
#include "next/core/commands.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace helm {

// ----------------------------------------------------------------------------
// The session an editor holds
// ----------------------------------------------------------------------------
// One object owns a SequenceModel, its Document, a CommandLog and the layout
// options. Keeping them together is what prevents the classic 'the UI mutated the
// sequence but not the document' divergence.
class SequenceSession {
public:
    SequenceSession();

    SequenceModel& model() { return model_; }
    const SequenceModel& model() const { return model_; }
    core::Document& document() { return document_; }
    const core::Document& document() const { return document_; }
    core::CommandLog& log() { return log_; }
    const core::CommandLog& log() const { return log_; }

    const LayoutOptions& layoutOptions() const { return layout_; }
    void setLayoutOptions(const LayoutOptions& options) { layout_ = options; }

    // Replace the model (e.g. after a HELM or FASTA parse) and re-map. One
    // transaction, so it is one undo step.
    core::Error adopt(const SequenceModel& model, const std::string& label);

    // Recompute the layout and re-map. A LAYOUT-ONLY operation: the chemistry
    // digest must not change. Exposed so a test can assert exactly that.
    core::Error relayout(const std::string& label);

    // The document-space bounding box of the current layout.
    core::Box bounds(double radius) const;

private:
    SequenceModel model_;
    core::Document document_;
    core::CommandLog log_;
    LayoutOptions layout_;
};

// ----------------------------------------------------------------------------
// Edit results
// ----------------------------------------------------------------------------
struct EditResult {
    core::Error error;
    size_t affectedMonomer;
    size_t affectedConnection;
    size_t requiredConnections;   // for a replace: how many points the position uses
    std::string detail;
    EditResult() : affectedMonomer(static_cast<size_t>(-1)),
                   affectedConnection(static_cast<size_t>(-1)),
                   requiredConnections(0) {}
    bool ok() const { return error.ok(); }
};

// ----------------------------------------------------------------------------
// Stage 1 edits
// ----------------------------------------------------------------------------
// Insert a residue at `position` (position == liveCount appends). ONE undo step.
// Does NOT create a bond to a neighbour: the guide's own removal/insertion rule
// is stated in RESULTS.md and the chemistry is never invented.
EditResult insertResidue(SequenceSession& session, size_t polymerIndex, size_t position,
                         const std::string& symbol);

// Replace the residue at `position`. The instance ID and therefore every
// connection to it is PRESERVED. REFUSED, with the required connection count
// reported, when the replacement offers too few attachment points or lacks one
// that is in use - the guide rule is 'N connections require M with N <= M'.
EditResult replaceResidue(SequenceSession& session, size_t polymerIndex, size_t position,
                          const std::string& symbol);

// Delete the residue at `position`: the slot becomes dead, connections touching it
// are removed, and the neighbours are NOT joined.
EditResult deleteResidue(SequenceSession& session, size_t polymerIndex, size_t position);

// Reverse the reading direction. The chemistry is preserved: the same residues
// stay bonded through the same attachment points.
EditResult reverseSequence(SequenceSession& session, size_t polymerIndex);

// Switch one polymer between the labelled and cartoon display. A pure VIEW edit:
// the chemistry digest must not change.
EditResult setDisplayMode(SequenceSession& session, size_t polymerIndex,
                          SequenceDisplayMode mode);

// ----------------------------------------------------------------------------
// Stage 2 edits
// ----------------------------------------------------------------------------
// Connect two residues. `kind` is Covalent (a cross-link such as a disulfide) or
// Hydrogen (a base pair). REFUSED when the attachment point does not exist on the
// monomer or is already in use.
EditResult connectResidues(SequenceSession& session,
                           size_t fromPolymer, size_t fromPosition,
                           const std::string& fromAttachment,
                           size_t toPolymer, size_t toPosition,
                           const std::string& toAttachment,
                           ConnectionKind kind);

EditResult disconnectResidues(SequenceSession& session, ConnectionId id);

// Apply the complementary-strand layout. REJECTED, leaving everything untouched,
// when the two polymers are not both nucleic or no hydrogen bond joins them.
EditResult layoutComplementary(SequenceSession& session, size_t senseIndex,
                               size_t antisenseIndex);

// Detect and fold a hairpin. REJECTED when the polymer has no intramolecular base
// pairs, or when its pairs cross (a pseudoknot, which this stage does not fold).
EditResult layoutHairpin(SequenceSession& session, size_t polymerIndex);

// ----------------------------------------------------------------------------
// Undo / redo
// ----------------------------------------------------------------------------
// These go through the T01 log unchanged, which is the point of building the edits
// on top of it. After an undo the SEQUENCE model is rebuilt from the document, so
// the two cannot drift.
core::Error undo(SequenceSession& session);
core::Error redo(SequenceSession& session);

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_COMMANDS_HPP
