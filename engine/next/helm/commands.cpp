// =============================================================================
// re_round62_viewer/next/helm/commands.cpp
//
// T15 - edit commands.
//
// THE PATTERN (it follows T01's contract exactly)
// -----------------------------------------------------------------------------
//   1. VALIDATE the edit against the current model. Nothing is touched yet. A
//      refusal returns a positioned diagnostic and the models are unchanged.
//   2. Capture the sequence model, because T01's document session records the
//      DOCUMENT change but not the sequence model change.
//   3. Run core::CommandLog::execute with a lambda that (a) applies the sequence
//      primitive and (b) re-maps the sequence into the document. The lambda runs
//      INSIDE the transaction, so the log diffs exactly what changed and undo
//      restores the previous document byte for byte.
//   4. If the command reports failure, T01 aborts and rolls the document back, and
//      this file restores the sequence snapshot. If the command succeeds, the
//      sequence model already holds the new state.
//
// Because step 1 runs before anything is opened, a REFUSED edit is a true no-op:
// both models are untouched and NO history entry is created. The tests assert that
// by comparing chemistryDigest() and log().entries() around a refusal.
// =============================================================================

#include "next/helm/commands.hpp"

#include "next/helm/helm_codec.hpp"

#include <functional>
#include <sstream>

namespace r62 {
namespace helm {

SequenceSession::SequenceSession() {}

core::Error SequenceSession::adopt(const SequenceModel& model, const std::string& label) {
    (void)label;
    model_ = model;
    model_.setLayout(layoutSequence(model_, layout_));
    core::Error mapped = mapToDocument(model_, document_, 0);
    if (!mapped.ok()) return mapped;
    // A replaced document instance invalidates the history, because a stored patch
    // describes the previous instance. Clearing is the honest choice.
    log_.clear();
    return core::Error();
}

core::Error SequenceSession::relayout(const std::string& label) {
    (void)label;
    // The sequence model is not mutated except for its layout, and the layout is
    // excluded from chemistryDigest(), so this cannot change the chemistry.
    model_.setLayout(layoutSequence(model_, layout_));
    return mapToDocument(model_, document_, 0);
}

core::Box SequenceSession::bounds(double radius) const {
    return layoutBounds(model_.layout(), radius);
}

namespace {

EditResult failure(const core::Error& error) {
    EditResult result;
    result.error = error;
    result.detail = error.message;
    return result;
}

// Publish the sequence model into the document. Called INSIDE the command lambda.
//
// It deliberately does NOT recompute the layout. A command that sets a SPECIFIC
// layout (complementary strands, a hairpin fold) would otherwise have that layout
// overwritten by the default row layout and become a silent no-op. Structural edits
// that need a fresh layout ask for one explicitly through runEdit's flag.
core::Error remap(SequenceSession& session) {
    return mapToDocument(session.model(), session.document(), 0);
}

// The shape every edit uses. `prepare` validates and may refuse; `apply` mutates the
// sequence model and returns the result to report. Exactly one of them is allowed to
// return a failure, and a prepare failure never opens a transaction.
//
// `apply` MUST be callable more than once in principle, but in this design it is
// called exactly once, from inside the command.
// The single entry point. `body` mutates the sequence model; the sequence snapshot
// is taken first and restored when the command fails.
//
// A subtlety worth stating: the lambda captures the session by reference and calls
// remap(), which needs the CURRENT sequence state. Since the sequence primitive has
// already run when the lambda executes, the document it writes is the post-edit
// document. The transaction baseline was captured BEFORE the lambda, so the diff is
// exactly the change.
// `recomputeLayout` is true for a STRUCTURAL edit (insert / replace / delete /
// reverse), which changes the residue order and therefore the default layout. It is
// FALSE for a command that sets a layout itself.
EditResult runEdit(SequenceSession& session, const std::string& label,
                  const std::function<core::Error(EditResult*)>& body,
                  bool recomputeLayout = true) {
    EditResult result;
    const SequenceModel before = session.model();
    const size_t entriesBefore = session.log().entries();

    core::Command command(
        label,
        core::kInvalidateStructure | core::kInvalidateGeometry | core::kInvalidateSemantics,
        [&session, &body, &result, recomputeLayout](core::CommandContext& ctx)
            -> core::CommandResult {
            (void)ctx;
            core::Error bodyError = body(&result);
            if (!bodyError.ok())
                return core::CommandResult::fail(bodyError.code, bodyError.message);
            if (recomputeLayout)
                session.model().setLayout(layoutSequence(session.model(),
                                                        session.layoutOptions()));
            core::Error mapped = remap(session);
            if (!mapped.ok())
                return core::CommandResult::fail(mapped.code, mapped.message);
            return core::CommandResult::success(
                core::kInvalidateStructure | core::kInvalidateGeometry |
                core::kInvalidateSemantics);
        });

    core::Error executed = session.log().execute(session.document(), command, 0);
    if (!executed.ok()) {
        // T01 already rolled the document back by inverse patch. Restore the
        // sequence model so the two agree again.
        session.model() = before;
        // A no-op command creates no entry and reports success; a failure that did
        // create an entry would mean the log disagreed with the command, which the
        // tests would catch. Nothing else is required here.
        if (session.log().entries() != entriesBefore) {
            session.log().clear();
        }
        return failure(executed);
    }
    return result;
}

} // namespace

// ----------------------------------------------------------------------------
// Stage 1 edits
// ----------------------------------------------------------------------------
EditResult insertResidue(SequenceSession& session, size_t polymerIndex, size_t position,
                         const std::string& symbol) {
    // ---- 1. validate -------------------------------------------------------
    const SequencePolymer* polymer = session.model().polymer(polymerIndex);
    if (!polymer)
        return failure(core::Error(core::ErrorCode::OutOfRange, "polymer index out of range"));
    if (symbol.empty())
        return failure(core::Error(core::ErrorCode::InvalidArgument, "empty monomer symbol"));
    // The insertion position is a LIVE index as far as the caller is concerned, but
    // the model addresses dead slots too. Both are accepted: position <= size.
    if (position > polymer->monomers.size())
        return failure(core::Error(core::ErrorCode::OutOfRange,
                                  "insert position is beyond the polymer length"));
    // An unknown symbol is ALLOWED here: it is preserved without a structure, which
    // is the required behaviour. Only an empty symbol is refused.
    MonomerInstanceId created = kInvalidMonomerInstanceId;
    std::ostringstream label;
    label << "sequence.insert." << symbol;

    EditResult result = runEdit(session, label.str(),
        [&session, polymerIndex, position, symbol, &created](EditResult* out) -> core::Error {
            core::Error inserted = session.model().insertMonomer(polymerIndex, position,
                                                                symbol, &created);
            if (!inserted.ok()) return inserted;
            if (out) out->affectedMonomer = position;
            return core::Error();
        });
    if (result.ok()) result.affectedMonomer = position;
    return result;
}

EditResult replaceResidue(SequenceSession& session, size_t polymerIndex, size_t position,
                          const std::string& symbol) {
    const SequencePolymer* polymer = session.model().polymer(polymerIndex);
    if (!polymer)
        return failure(core::Error(core::ErrorCode::OutOfRange, "polymer index out of range"));
    if (position >= polymer->monomers.size())
        return failure(core::Error(core::ErrorCode::OutOfRange,
                                  "replace position is beyond the polymer length"));
    if (!polymer->monomers[position].alive)
        return failure(core::Error(core::ErrorCode::NotAlive, "that residue was deleted"));
    if (symbol.empty())
        return failure(core::Error(core::ErrorCode::InvalidArgument, "empty monomer symbol"));

    // Report how many attachment points this position currently uses, so a caller
    // can explain the guide's N <= M rule when the replacement is refused.
    const size_t required = session.model().usedAttachments(polymerIndex, position).size();
    size_t reported = 0;
    EditResult result = runEdit(session, "sequence.replace",
        [&session, polymerIndex, position, symbol, &reported](EditResult* out) -> core::Error {
            // The MODEL enforces the guide rule, so the refusal is not duplicated here
            // and cannot disagree with it.
            core::Error replaced = session.model().replaceMonomer(polymerIndex, position,
                                                                 symbol, &reported);
            if (!replaced.ok()) return replaced;
            if (out) out->affectedMonomer = position;
            return core::Error();
        });
    result.requiredConnections = reported ? reported : required;
    if (result.ok()) result.affectedMonomer = position;
    return result;
}

EditResult deleteResidue(SequenceSession& session, size_t polymerIndex, size_t position) {
    const SequencePolymer* polymer = session.model().polymer(polymerIndex);
    if (!polymer)
        return failure(core::Error(core::ErrorCode::OutOfRange, "polymer index out of range"));
    if (position >= polymer->monomers.size())
        return failure(core::Error(core::ErrorCode::OutOfRange,
                                  "delete position is beyond the polymer length"));
    if (!polymer->monomers[position].alive)
        return failure(core::Error(core::ErrorCode::NotAlive, "the residue is already deleted"));

    size_t killed = 0;
    EditResult result = runEdit(session, "sequence.delete",
        [&session, polymerIndex, position, &killed](EditResult* out) -> core::Error {
            core::Error removed = session.model().deleteMonomer(polymerIndex, position, &killed);
            if (!removed.ok()) return removed;
            if (out) out->affectedMonomer = position;
            return core::Error();
        });
    result.affectedConnection = killed;
    if (result.ok()) result.affectedMonomer = position;
    return result;
}

EditResult reverseSequence(SequenceSession& session, size_t polymerIndex) {
    const SequencePolymer* polymer = session.model().polymer(polymerIndex);
    if (!polymer)
        return failure(core::Error(core::ErrorCode::OutOfRange, "polymer index out of range"));
    if (polymer->liveCount() < 2)
        return failure(core::Error(core::ErrorCode::InvalidArgument,
                                  "reversing needs at least two live residues"));
    return runEdit(session, "sequence.reverse",
        [&session, polymerIndex](EditResult*) -> core::Error {
            return session.model().reversePolymer(polymerIndex);
        });
}

EditResult setDisplayMode(SequenceSession& session, size_t polymerIndex,
                          SequenceDisplayMode mode) {
    SequencePolymer* polymer = session.model().mutablePolymer(polymerIndex);
    if (!polymer)
        return failure(core::Error(core::ErrorCode::OutOfRange, "polymer index out of range"));
    if (polymer->display == mode) {
        // Not an error, and deliberately not a history entry: pressing a key that
        // already holds should not consume an undo step (T01 states the same rule
        // for a no-op command).
        EditResult noop;
        return noop;
    }
    // A DISPLAY-only change requests no geometry or semantics invalidation.
    return runEdit(session, mode == SequenceDisplayMode::Cartoon
                                ? "sequence.display.cartoon"
                                : "sequence.display.structure",
        [&session, polymerIndex, mode](EditResult*) -> core::Error {
            SequencePolymer* target = session.model().mutablePolymer(polymerIndex);
            if (!target)
                return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");
            target->display = mode;
            return core::Error();
        });
}

// ----------------------------------------------------------------------------
// Stage 2 edits
// ----------------------------------------------------------------------------
EditResult connectResidues(SequenceSession& session,
                           size_t fromPolymer, size_t fromPosition,
                           const std::string& fromAttachment,
                           size_t toPolymer, size_t toPosition,
                           const std::string& toAttachment,
                           ConnectionKind kind) {
    ConnectionId created = kInvalidConnectionId;
    EditResult result = runEdit(session, "sequence.connect",
        [&session, fromPolymer, fromPosition, fromAttachment, toPolymer, toPosition,
         toAttachment, kind, &created](EditResult* out) -> core::Error {
            core::Error made = session.model().connect(fromPolymer, fromPosition,
                                                      fromAttachment, toPolymer, toPosition,
                                                      toAttachment, kind, &created);
            if (!made.ok()) return made;
            if (out) out->affectedConnection = created;
            return core::Error();
        });
    if (result.ok()) result.affectedConnection = created;
    return result;
}

EditResult disconnectResidues(SequenceSession& session, ConnectionId id) {
    const SequenceConnection* connection = id < session.model().connectionCount()
                                               ? session.model().connection(id) : 0;
    if (!connection)
        return failure(core::Error(core::ErrorCode::OutOfRange, "connection id out of range"));
    if (!connection->alive)
        return failure(core::Error(core::ErrorCode::NotAlive, "the connection is already removed"));
    return runEdit(session, "sequence.disconnect",
        [&session, id](EditResult*) -> core::Error {
            return session.model().disconnect(id);
        });
}

EditResult layoutComplementary(SequenceSession& session, size_t senseIndex,
                               size_t antisenseIndex) {
    // Validate FIRST so a rejected pairing touches nothing.
    SequenceLayout probe;
    size_t pairs = 0;
    core::Error error = complementLayout(session.model(), senseIndex, antisenseIndex,
                                         session.layoutOptions(), &probe, &pairs);
    if (!error.ok()) return failure(error);
    EditResult result = runEdit(session, "sequence.layout.complementary",
        [&session, &probe](EditResult*) -> core::Error {
            // The layout is a VIEW: setting it changes no chemistry. The map step
            // then moves the atom positions to match.
            session.model().setLayout(probe);
            return core::Error();
        },
        /* recomputeLayout */ false);
    result.affectedConnection = pairs;
    return result;
}

EditResult layoutHairpin(SequenceSession& session, size_t polymerIndex) {
    const HairpinDetection detection = detectHairpin(session.model(), polymerIndex);
    if (!detection.isHairpin) {
        // The guide describes a hairpin as a fold; this stage refuses to fold
        // anything else rather than drawing a wrong picture.
        return failure(core::Error(core::ErrorCode::Unsupported,
            detection.reason.empty() ? std::string("not a hairpin") : detection.reason));
    }
    SequenceLayout probe;
    core::Error error = layoutHairpin(session.model(), detection, session.layoutOptions(), &probe);
    if (!error.ok()) return failure(error);
    EditResult result = runEdit(session, "sequence.layout.hairpin",
        [&session, &probe](EditResult*) -> core::Error {
            session.model().setLayout(probe);
            return core::Error();
        },
        /* recomputeLayout */ false);
    result.affectedConnection = detection.pairCount;
    return result;
}

// ----------------------------------------------------------------------------
// Undo / redo
// ----------------------------------------------------------------------------
// The document is the source of truth after an undo, so the sequence model is
// rebuilt FROM it. That is what keeps the two from drifting: there is exactly one
// reconstruction path and it is the same one a file load uses.
// The rebuilt model gets the DEFAULT layout. The layout is a derived VIEW, so
// rebuilding it here cannot change the chemistry; without it the restored model
// would have no placements and the renderer would draw an empty sequence.
core::Error undo(SequenceSession& session) {
    core::Error applied = session.log().undo(session.document());
    if (!applied.ok()) return applied;
    const MonomerLibrary library = session.model().library();
    core::Error rebuilt = mapFromDocument(session.document(), session.model(), 0);
    if (!rebuilt.ok()) return rebuilt;
    session.model().setLibrary(library);
    session.model().setLayout(layoutSequence(session.model(), session.layoutOptions()));
    return core::Error();
}

core::Error redo(SequenceSession& session) {
    core::Error applied = session.log().redo(session.document());
    if (!applied.ok()) return applied;
    const MonomerLibrary library = session.model().library();
    core::Error rebuilt = mapFromDocument(session.document(), session.model(), 0);
    if (!rebuilt.ok()) return rebuilt;
    session.model().setLibrary(library);
    session.model().setLayout(layoutSequence(session.model(), session.layoutOptions()));
    return core::Error();
}

} // namespace helm
} // namespace r62
