#include "next/selection/commands.hpp"

#include <algorithm>
#include <memory>
#include <sstream>

namespace r62 {
namespace selection {

using core::Error;
using core::ErrorCode;
using core::ObjectRef;
using core::ObjectKind;

namespace {

// Delete through the CALLER's transaction. Used as the DeleteObjectsFn handed to
// cutSelection, so a cut's delete is one undoable action driven by the log this
// SelectionCommands owns.
struct DeleteBridge {
    core::CommandLog* log;
    core::Document*   document;
    uint64_t          patchBytes;
    size_t            patchRuns;
    bool              used;
    DeleteBridge() : log(0), document(0), patchBytes(0), patchRuns(0), used(false) {}
};

Error deleteBridgeFn(void* userData, const std::vector<ObjectRef>& refs) {
    DeleteBridge* bridge = static_cast<DeleteBridge*>(userData);
    if (!bridge || !bridge->log || !bridge->document)
        return Error(ErrorCode::InvalidArgument, "delete bridge is not wired");
    bridge->used = true;
    const core::Command command = core::commands::deleteObjects(refs);
    const Error executed = bridge->log->execute(*bridge->document, command);
    if (!executed.ok()) return executed;
    const core::ChangeSet& changes = bridge->log->lastChangeSet();
    bridge->patchBytes = changes.byteSize();
    bridge->patchRuns = changes.runCount();
    return Error::success();
}

} // namespace

std::string ActionReport::summary() const {
    std::ostringstream out;
    out << action << (committed ? " committed" : " not-committed");
    if (!error.ok()) out << " error=" << core::errorName(error.code) << " (" << error.message << ")";
    if (committed) out << " patchBytes=" << patchBytes << " runs=" << patchRuns
                       << " records=" << recordsTouched;
    if (!detail.empty()) out << " | " << detail;
    return out.str();
}

// ---------------------------------------------------------------------------
// run(): the single place a T05 edit becomes a history entry
// ---------------------------------------------------------------------------
ActionReport SelectionCommands::run(core::Document& document, const core::Command& command,
                                    const std::string& actionName) {
    ActionReport report;
    report.action = actionName;
    ++operations_;
    if (!command.valid()) {
        report.error = Error(ErrorCode::NotImplemented, "command is empty");
        ++failed_;
        return report;
    }
    const Error executed = log_.execute(document, command);
    if (!executed.ok()) {
        // T01 guarantees the document is unchanged on failure. The report says
        // so explicitly rather than leaving the caller to assume it.
        report.error = executed;
        report.detail = "document unchanged (transaction refused)";
        ++failed_;
        return report;
    }
    const core::ChangeSet& changes = log_.lastChangeSet();
    report.patchBytes = changes.byteSize();
    report.patchRuns = changes.runCount();
    report.recordsTouched = changes.recordCount();
    report.invalidation = command.invalidation();
    report.committed = true;
    ++committed_;
    totalPatchBytes_ += report.patchBytes;
    return report;
}

ActionReport SelectionCommands::selectionOnly(const std::string& actionName,
                                              const SelectionDelta& delta,
                                              uint64_t scanned) {
    ActionReport report;
    report.action = actionName;
    report.selection = delta;
    report.committed = false;          // deliberately NOT a history entry
    report.invalidation = core::kInvalidateSelection;
    report.recordsTouched = 0;
    ++operations_;
    ++selectionOnly_;
    if (scanned) report.detail = "scan=" + std::to_string(scanned);
    return report;
}

void SelectionCommands::resetCounters() {
    operations_ = 0; committed_ = 0; failed_ = 0; totalPatchBytes_ = 0; selectionOnly_ = 0;
}

// ---------------------------------------------------------------------------
// Selection-only actions
// ---------------------------------------------------------------------------
ActionReport SelectionCommands::selectAll(core::Document& document) {
    SelectionDelta delta;
    if (!query_) {
        ActionReport report;
        report.action = "select.all";
        report.error = Error(ErrorCode::InvalidArgument, "no query bound");
        ++failed_;
        return report;
    }
    selection_ = selection::selectAll(document, *query_, selection_, &delta);
    return selectionOnly("select.all", delta, delta.scanned);
}

ActionReport SelectionCommands::selectInverse(core::Document& document) {
    SelectionDelta delta;
    if (!query_) {
        ActionReport report;
        report.action = "select.inverse";
        report.error = Error(ErrorCode::InvalidArgument, "no query bound");
        ++failed_;
        return report;
    }
    selection_ = selection::selectInverse(document, *query_, selection_, &delta);
    return selectionOnly("select.inverse", delta, delta.scanned);
}

ActionReport SelectionCommands::selectAt(core::Document& document, const ObjectRef& ref,
                                         bool additive, bool toggle) {
    SelectionDelta delta;
    selection_ = selection::selectOne(document, selection_, ref, additive, toggle, &delta);
    return selectionOnly("select.at", delta, delta.scanned);
}

ActionReport SelectionCommands::selectMolecule(core::Document& document, const ObjectRef& seed,
                                               bool additive) {
    SelectionDelta delta;
    selection_ = selection::selectMolecule(document, selection_, seed, additive, &delta);
    return selectionOnly("select.molecule", delta, delta.scanned);
}

ActionReport SelectionCommands::selectGroup(core::Document& document, core::ObjectId groupId,
                                            bool additive) {
    SelectionDelta delta;
    selection_ = selection::selectGroup(document, selection_, groupId, additive, &delta);
    return selectionOnly("select.group", delta, delta.scanned);
}

ActionReport SelectionCommands::selectMarquee(core::Document& document,
                                              const MarqueeGesture& gesture,
                                              const MarqueeOptions& options, bool additive) {
    SelectionDelta delta;
    if (!query_) {
        ActionReport report;
        report.action = "select.marquee";
        report.error = Error(ErrorCode::InvalidArgument, "no query bound");
        ++failed_;
        return report;
    }
    selection_ = selection::selectMarquee(document, selection_, *query_, gesture, options, additive, &delta);
    return selectionOnly("select.marquee", delta, delta.scanned);
}

// ---------------------------------------------------------------------------
// Document edits
// ---------------------------------------------------------------------------
ActionReport SelectionCommands::deleteSelection(core::Document& document) {
    const std::vector<ObjectRef> refs = selection_.refs();
    if (refs.empty()) {
        ActionReport report;
        report.action = "edit.delete";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    // Groups are split out: T01's removeObject returns NotImplemented for the
    // Group class, so a delete that included a group would fail the WHOLE
    // transaction and delete nothing. The selection module owns group removal,
    // so it removes the group records itself inside the SAME transaction.
    std::vector<ObjectRef> nonGroups;
    std::vector<core::ObjectId> groupIds;
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].kind == ObjectKind::Group) groupIds.push_back(refs[i].id);
        else nonGroups.push_back(refs[i]);
    }
    // Members of a deleted group are NOT deleted: ChemDraw's Delete on a group
    // removes the group and its contents only when the content was part of the
    // selection. Deleting a group deletes the GROUP; the members stay alive and
    // become unowned, which is what makes "undo restores the full hierarchy"
    // meaningful.
    ActionReport report = run(document,
        core::Command("selection.delete",
                      core::kInvalidateStructure | core::kInvalidateGeometry |
                      core::kInvalidateSemantics | core::kInvalidateSelection,
            [nonGroups, groupIds](core::CommandContext& ctx) -> core::CommandResult {
                for (size_t i = 0; i < groupIds.size(); ++i) {
                    core::GroupRecord* record = ctx.document.groups().mutableGet(groupIds[i]);
                    if (!record || !record->isAlive()) continue;
                    ctx.document.noteTouchedGroup(groupIds[i]);
                    ctx.document.groups().kill(groupIds[i]);
                    ctx.document.noteTouchedStructure();
                    ctx.document.bumpStructure();
                }
                if (!nonGroups.empty()) {
                    // Reuse T01's delete so the cascade and the invariants stay
                    // T01's, not a reimplementation.
                    core::CommandContext inner(ctx.document);
                    const core::CommandResult deleted =
                        core::commands::deleteObjects(nonGroups).execute(inner);
                    if (!deleted.ok())
                        return core::CommandResult::fail(deleted.error.code, deleted.error.message);
                }
                return core::CommandResult::success(
                    core::kInvalidateStructure | core::kInvalidateGeometry |
                    core::kInvalidateSemantics | core::kInvalidateSelection);
            }),
        "edit.delete");
    if (report.committed) {
        // The deleted objects' slots are now dead. Pruning is what stops a
        // selection from outliving its objects - and the delta reports the drop
        // so the overlay removes exactly those highlights.
        SelectionSet before(selection_);
        const size_t dropped = selection_.pruneDead(document);
        (void)dropped;
        report.selection = diffSelection(before, selection_, refs.size());
    }
    return report;
}

ActionReport SelectionCommands::translateSelection(core::Document& document,
                                                   const core::Vec2& delta,
                                                   const TransformOptions& options) {
    if (selection_.empty()) {
        ActionReport report;
        report.action = "transform.translate";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    const std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report =
        run(document, makeSelectionCommand("selection.translate",
                                           core::kInvalidateGeometry | core::kInvalidateSelection,
                                           refs, Affine2::translation(delta), options),
            "transform.translate");
    return report;
}

ActionReport SelectionCommands::rotateSelection(core::Document& document, const core::Vec2& pivot,
                                                double degrees, const TransformOptions& options) {
    if (selection_.empty()) {
        ActionReport report;
        report.action = "transform.rotate";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    const std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report =
        run(document, makeSelectionCommand("selection.rotate",
                                           core::kInvalidateGeometry | core::kInvalidateSelection,
                                           refs, Affine2::rotation(pivot, degrees), options),
            "transform.rotate");
    return report;
}

ActionReport SelectionCommands::scaleSelection(core::Document& document, const core::Vec2& pivot,
                                               double factor, const TransformOptions& options) {
    if (selection_.empty()) {
        ActionReport report;
        report.action = "transform.scale";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    const std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report =
        run(document, makeSelectionCommand("selection.scale",
                                           core::kInvalidateGeometry | core::kInvalidateSelection,
                                           refs, Affine2::scaling(pivot, factor, factor), options),
            "transform.scale");
    return report;
}

ActionReport SelectionCommands::stretchSelection(core::Document& document, const core::Vec2& pivot,
                                                 double sx, double sy,
                                                 const TransformOptions& options) {
    if (selection_.empty()) {
        ActionReport report;
        report.action = "transform.stretch";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    const std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report =
        run(document, makeSelectionCommand("selection.stretch",
                                           core::kInvalidateGeometry | core::kInvalidateSelection,
                                           refs, Affine2::scaling(pivot, sx, sy), options),
            "transform.stretch");
    return report;
}

ActionReport SelectionCommands::flipSelection(core::Document& document, const core::Vec2& pivot,
                                              bool horizontal, const TransformOptions& options) {
    if (selection_.empty()) {
        ActionReport report;
        report.action = "transform.flip";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    const std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report =
        run(document, makeSelectionCommand("selection.flip",
                                           core::kInvalidateGeometry | core::kInvalidateSelection,
                                           refs, Affine2::flip(pivot, horizontal), options),
            "transform.flip");
    return report;
}

ActionReport SelectionCommands::alignSelection(core::Document& document, AlignMode mode,
                                               const AlignOptions& options) {
    if (selection_.size() < 2) {
        ActionReport report;
        report.action = "layout.align";
        report.error = Error(ErrorCode::InvalidArgument,
                             "align needs at least two selected objects");
        ++failed_;
        return report;
    }
    std::vector<ObjectRef> refs = selection_.refs();
    const core::Box page = options.page;
    ActionReport report = run(document,
        core::Command("selection.align",
                      core::kInvalidateGeometry | core::kInvalidateSelection,
            [refs, mode, options](core::CommandContext& ctx) -> core::CommandResult {
                TransformReport moved;
                const Error error =
                    Error::success();
                (void)error;
                selection::alignObjects(ctx.document, refs, mode, options, &moved);
                if (moved.objectsChanged() == 0 && moved.atomsMoved == 0)
                    return core::CommandResult::success(core::kInvalidateSelection);
                return core::CommandResult::success(
                    core::kInvalidateGeometry | core::kInvalidateSelection);
            }),
        "layout.align");
    (void)page;
    return report;
}

ActionReport SelectionCommands::distributeSelection(core::Document& document, DistributeMode mode) {
    if (selection_.size() < 3) {
        ActionReport report;
        report.action = "layout.distribute";
        report.error = Error(ErrorCode::InvalidArgument,
                             "distribute needs at least three selected objects");
        ++failed_;
        return report;
    }
    std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report = run(document,
        core::Command("selection.distribute",
                      core::kInvalidateGeometry | core::kInvalidateSelection,
            [refs, mode](core::CommandContext& ctx) -> core::CommandResult {
                const DistributeReport distributed =
                    selection::distributeObjects(ctx.document, refs, mode);
                if (!distributed.applied)
                    return core::CommandResult::fail(ErrorCode::InvalidArgument,
                                                     distributed.why);
                return core::CommandResult::success(
                    core::kInvalidateGeometry | core::kInvalidateSelection);
            }),
        "layout.distribute");
    return report;
}

ActionReport SelectionCommands::centerInPage(core::Document& document, const core::Box& page) {
    if (selection_.empty()) {
        ActionReport report;
        report.action = "layout.center";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report = run(document,
        core::Command("selection.center",
                      core::kInvalidateGeometry | core::kInvalidateSelection,
            [refs, page](core::CommandContext& ctx) -> core::CommandResult {
                if (page.empty())
                    return core::CommandResult::fail(ErrorCode::InvalidArgument,
                                                     "page box is empty");
                selection::centerSelection(ctx.document, refs, page);
                return core::CommandResult::success(
                    core::kInvalidateGeometry | core::kInvalidateSelection);
            }),
        "layout.center");
    return report;
}

ActionReport SelectionCommands::nudgeSelection(core::Document& document,
                                               const NudgeVector& direction, bool shift,
                                               const NudgeProfile& profile) {
    if (direction.zero()) {
        ActionReport report;
        report.action = "transform.nudge";
        report.error = Error(ErrorCode::InvalidArgument, "zero nudge direction");
        ++failed_;
        return report;
    }
    const core::Vec2 delta = selection::nudgeDelta(direction, shift, profile);
    ActionReport report = translateSelection(document, delta);
    report.action = "transform.nudge";
    if (report.committed)
        report.detail = "step=" + std::to_string(shift ? profile.stepShift : profile.stepPlain);
    return report;
}

// ---------------------------------------------------------------------------
// Grouping
// ---------------------------------------------------------------------------
ActionReport SelectionCommands::groupSelection(core::Document& document,
                                               const std::string& label) {
    if (selection_.size() < 2) {
        ActionReport report;
        report.action = "group.create";
        report.error = Error(ErrorCode::InvalidArgument,
                             "a group needs at least two selected objects");
        ++failed_;
        return report;
    }
    std::vector<ObjectRef> refs = selection_.refs();
    std::shared_ptr<core::ObjectId> created(new core::ObjectId(core::kInvalidObjectId));
    ActionReport report = run(document,
        core::Command("selection.group",
                      core::kInvalidateStructure | core::kInvalidateSelection,
            [refs, label, created](core::CommandContext& ctx) -> core::CommandResult {
                GroupReport report;
                const Error error = selection::createGroup(ctx.document, refs, label, &report);
                if (!error.ok()) return core::CommandResult::fail(error.code, error.message);
                *created = report.group;
                return core::CommandResult::success(
                    core::kInvalidateStructure | core::kInvalidateSelection);
            }),
        "group.create");
    if (report.committed && *created != core::kInvalidObjectId) {
        // A group is one object from now on: keep the group, not its members,
        // selected so the next click/drag/ungroup acts on the whole group.
        selection_.clear();
        selection_.add(core::ObjectRef(ObjectKind::Group, *created), SelectionReason::Direct);
    }
    return report;
}

ActionReport SelectionCommands::ungroupSelection(core::Document& document, bool recursive) {
    // Ungroup operates on the GROUPS in the selection, and - when the selection
    // has none - on the groups that OWN the selected objects. Both are what a
    // user means by "ungroup this".
    std::vector<core::ObjectId> groups;
    for (size_t i = 0; i < selection_.size(); ++i) {
        if (selection_.refs()[i].kind == ObjectKind::Group) {
            groups.push_back(selection_.refs()[i].id);
        } else {
            const std::vector<core::ObjectId> owners =
                groupsContaining(document, selection_.refs()[i]);
            for (size_t g = 0; g < owners.size(); ++g) groups.push_back(owners[g]);
        }
    }
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    if (groups.empty()) {
        ActionReport report;
        report.action = "group.ungroup";
        report.error = Error(ErrorCode::InvalidArgument, "no group in or around the selection");
        ++failed_;
        return report;
    }
    std::shared_ptr<std::vector<ObjectRef> > freedRefs(new std::vector<ObjectRef>());
    ActionReport report = run(document,
        core::Command("selection.ungroup",
                      core::kInvalidateStructure | core::kInvalidateSelection,
            [groups, recursive, freedRefs](core::CommandContext& ctx) -> core::CommandResult {
                GroupReport ungrouped;
                const SelectionSet freed =
                    selection::ungroup(ctx.document, groups, recursive, &ungrouped);
                // The freed members become the selection, so an ungroup leaves
                // the user holding exactly what the group released.
                *freedRefs = freed.refs();
                return core::CommandResult::success(
                    core::kInvalidateStructure | core::kInvalidateSelection);
            }),
        "group.ungroup");
    if (report.committed) {
        // Reflect the new selection outside the transaction: selection is NOT
        // document state, so it cannot be captured by the patch.
        SelectionSet before(selection_);
        SelectionSet after;
        for (size_t i = 0; i < freedRefs->size(); ++i)
            after.add((*freedRefs)[i], SelectionReason::Direct);
        after.pruneDead(document);
        selection_ = after;
        report.selection = diffSelection(before, selection_, before.size());
    }
    return report;
}

ActionReport SelectionCommands::reorderSelection(core::Document& document, ZOrderCommand command) {
    if (selection_.empty()) {
        ActionReport report;
        report.action = "layout.zorder";
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report = run(document,
        core::Command("selection.zorder",
                      core::kInvalidateStructure | core::kInvalidateSelection,
            [refs, command](core::CommandContext& ctx) -> core::CommandResult {
                const ZOrderReport reordered =
                    selection::reorderObjects(ctx.document, refs, command);
                if (reordered.unsupported > 0 && reordered.groupsReordered == 0 &&
                    reordered.membersReordered == 0)
                    return core::CommandResult::fail(
                        ErrorCode::Unsupported,
                        reordered.detail.empty() ? "no z-order available" : reordered.detail);
                return core::CommandResult::success(
                    core::kInvalidateStructure | core::kInvalidateSelection);
            }),
        "layout.zorder");
    return report;
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------
ActionReport SelectionCommands::copy(core::Document& document, IClipboardTransport& transport) {
    ActionReport report;
    report.action = "edit.copy";
    ++operations_;
    report.publish = selection::publishSelection(document, selection_.refs(), transport, true);
    if (!report.publish.published) {
        report.error = report.publish.error;
        report.detail = report.publish.why + " | document untouched";
        ++failed_;
        return report;
    }
    report.committed = false;   // a copy is NOT a document edit and NOT undoable
    report.invalidation = core::kInvalidateNone;
    report.detail = "published " + std::to_string(report.publish.bytes) + " bytes";
    return report;
}

ActionReport SelectionCommands::cut(core::Document& document, IClipboardTransport& transport) {
    ActionReport report;
    report.action = "edit.cut";
    ++operations_;
    const std::vector<ObjectRef> refs = selection_.refs();
    if (refs.empty()) {
        report.error = Error(ErrorCode::InvalidArgument, "nothing selected");
        ++failed_;
        return report;
    }
    DeleteBridge bridge;
    bridge.log = &log_;
    bridge.document = &document;
    report.cut = selection::cutSelection(document, refs, transport, &deleteBridgeFn, &bridge, 0);
    if (!report.cut.publish.published) {
        report.error = report.cut.publish.error;
        report.detail = "publish failed; source untouched; deleteFn called=" +
                        std::string(bridge.used ? "yes" : "no");
        ++failed_;
        return report;
    }
    if (!report.cut.deleted) {
        report.error = report.cut.deleteError;
        report.detail = report.cut.why;
        ++failed_;
        return report;
    }
    report.publish = report.cut.publish;
    report.committed = true;
    report.patchBytes = bridge.patchBytes;
    report.patchRuns = bridge.patchRuns;
    report.invalidation = core::kInvalidateStructure | core::kInvalidateSelection;
    ++committed_;
    totalPatchBytes_ += report.patchBytes;
    // Prune the selection: the cut objects are dead now.
    SelectionSet before(selection_);
    selection_.pruneDead(document);
    report.selection = diffSelection(before, selection_, refs.size());
    report.detail = "cut " + std::to_string(report.cut.deletedObjects) + " objects";
    return report;
}

ActionReport SelectionCommands::paste(core::Document& document, IClipboardTransport& transport,
                                      const core::Vec2& translate) {
    ActionReport report;
    report.action = "edit.paste";
    ++operations_;
    report.paste = selection::pasteSelection(document, transport, translate);
    if (!report.paste.pasted) {
        report.error = report.paste.error;
        report.detail = report.paste.why + " | document unchanged";
        ++failed_;
        return report;
    }
    report.committed = true;
    ++committed_;
    report.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry |
                          core::kInvalidateSelection;
    // The pasted objects become the selection, which is what every editor does
    // and what makes "paste then drag" work.
    SelectionSet before(selection_);
    selection_.setRefs(report.paste.inserted, SelectionReason::Direct);
    report.selection = diffSelection(before, selection_, report.paste.objectsInserted);
    report.detail = "pasted " + std::to_string(report.paste.objectsInserted) + " objects";
    return report;
}

ActionReport SelectionCommands::merge(core::Document& document, double maxDistance) {
    if (selection_.size() < 2) {
        ActionReport report;
        report.action = "edit.merge";
        report.error = Error(ErrorCode::InvalidArgument, "need at least two selected atoms");
        ++failed_;
        return report;
    }
    std::vector<ObjectRef> refs = selection_.refs();
    ActionReport report = run(document,
        core::Command("selection.merge",
                      core::kInvalidateStructure | core::kInvalidateGeometry |
                      core::kInvalidateSemantics | core::kInvalidateSelection,
            [refs, maxDistance](core::CommandContext& ctx) -> core::CommandResult {
                const MergeReport merged = selection::mergeSelection(ctx.document, refs, maxDistance);
                if (!merged.error.ok())
                    return core::CommandResult::fail(merged.error.code, merged.error.message);
                if (merged.mergedPairs == 0 && merged.refusedPairs == 0)
                    return core::CommandResult::noChange();
                return core::CommandResult::success(
                    core::kInvalidateStructure | core::kInvalidateGeometry |
                    core::kInvalidateSemantics | core::kInvalidateSelection);
            }),
        "edit.merge");
    if (report.committed) selection_.pruneDead(document);
    return report;
}

// ---------------------------------------------------------------------------
// makeSelectionCommand
// ---------------------------------------------------------------------------
core::Command makeSelectionCommand(const std::string& name, core::Invalidation invalidation,
                                   const std::vector<ObjectRef>& refs,
                                   const Affine2& matrix,
                                   const TransformOptions& options) {
    // Everything is captured BY VALUE so Ctrl+Y can re-run it later.
    std::vector<ObjectRef> captured(refs);
    const Affine2 capturedMatrix(matrix);
    const TransformOptions capturedOptions(options);
    return core::Command(name, invalidation,
        [captured, capturedMatrix, capturedOptions](core::CommandContext& ctx)
                -> core::CommandResult {
            const TransformReport moved =
                selection::transformObjects(ctx.document, captured, capturedMatrix, capturedOptions);
            if (moved.objectsChanged() == 0 && moved.bondsUnaffected == 0)
                return core::CommandResult::noChange();
            return core::CommandResult::success(
                core::kInvalidateGeometry | core::kInvalidateSelection);
        });
}

// ---------------------------------------------------------------------------
// TransactionLedger
// ---------------------------------------------------------------------------
void TransactionLedger::observe(const core::Transaction& transaction) {
    const core::ChangeSet& changes = transaction.changeSet();
    if (changes.empty()) return;
    ++actions;
    bytes += changes.byteSize();
    records += changes.recordCount();
    const size_t longest = changes.runCount() ? changes.runCount() : 0;
    if (longest > longestRun) longestRun = longest;
    // snapshots stays 0 by construction: T01 stores no full-document state.
}

void TransactionLedger::observe(const core::CommandLog& log) {
    ++actions;
    bytes += log.totalRecordBytes();
    if (log.snapshotBytes() != 0) ++snapshots;
    const size_t longest = log.longestRunOverall();
    if (longest > longestRun) longestRun = longest;
}

void TransactionLedger::clear() {
    actions = 0; bytes = 0; records = 0; longestRun = 0; snapshots = 0;
}

std::string TransactionLedger::summary() const {
    std::ostringstream out;
    out << "actions=" << actions << " bytes=" << bytes << " records=" << records
        << " longestRun=" << longestRun << " snapshots=" << snapshots;
    return out.str();
}

} // namespace selection
} // namespace r62
