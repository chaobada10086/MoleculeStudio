#ifndef R62_NEXT_SELECTION_COMMANDS_HPP
#define R62_NEXT_SELECTION_COMMANDS_HPP
// =============================================================================
// re_round62_viewer/next/selection/commands.hpp
//
// T05 - THE TRANSACTION WRAPPER.
//
// WHY EVERY OPERATION ABOVE NEEDS THIS LAYER
// -----------------------------------------------------------------------------
// bounds/selection/transform/grouping/clipboard are PURE MUTATION PRIMITIVES
// with no undo of their own, exactly as T01's Document primitives are. T01's
// rule R1 says ONE user action == ONE atomic transaction == ONE undo step. A
// rotate that moves 5,000 atoms must be ONE history entry, and a "rotate twice"
// must be TWO entries that each undo cleanly.
//
// So this file is the ONLY place a T05 operation is turned into an undoable
// user action, and it is deliberately tiny: begin a transaction, call the
// primitive, commit. Everything hard (rollback on failure, incremental patches,
// the digest check) is T01's and is not reimplemented here.
//
// WHAT IS RECORDED, AND WHY
// -----------------------------------------------------------------------------
// SlabTransactionLedger counts, per committed action, the transaction's byte
// size and run count. That is the evidence for "the patch is incremental, not a
// snapshot": a 5,000-atom rotate must produce a run count far below its record
// count, and a test asserts exactly that.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/selection/clipboard.hpp"
#include "next/selection/grouping.hpp"
#include "next/selection/transform.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// Platform command ids for the T05 family
// ---------------------------------------------------------------------------
// T01 reserves ProjectBand = 23000..23999. T05 owns 23100..23199 so it can
// never collide with T04's tool band or T19's assembly ids.
namespace platform_command {
enum T05CommandId : uint32_t {
    kSelectAll        = 23100,
    kSelectInverse    = 23101,
    kSelectMolecule   = 23102,
    kDeleteSelection  = 23103,
    kCut              = 23104,
    kCopy             = 23105,
    kPaste            = 23106,
    kRepeat           = 23107,
    kGroup            = 23108,
    kUngroup          = 23109,
    kBringToFront     = 23110,
    kSendToBack       = 23111,
    kBringForward     = 23112,
    kSendBackward     = 23113,
    kAlignLeft        = 23114,
    kAlignHCenter     = 23115,
    kAlignRight       = 23116,
    kAlignTop         = 23117,
    kAlignVCenter     = 23118,
    kAlignBottom      = 23119,
    kDistributeH      = 23120,
    kDistributeV      = 23121,
    kCenterInPage     = 23122,
    kFlipHorizontal   = 23123,
    kFlipVertical     = 23124,
    kMergeSelection   = 23125,
    kNudgeShift       = 23126,
    kRotate90CW       = 23127,
    kRotate90CCW      = 23128
};
} // namespace platform_command

// ---------------------------------------------------------------------------
// ActionReport
// ---------------------------------------------------------------------------
// One uniform result for every wrapped action, so a host has ONE thing to
// inspect rather than eight different report structs.
struct ActionReport {
    core::Error  error;
    std::string  action;
    bool         committed;
    core::Invalidation invalidation;
    // History evidence from the committed transaction.
    uint64_t     patchBytes;
    size_t       patchRuns;
    uint64_t     recordsTouched;
    SelectionDelta selection;
    TransformReport transform;
    GroupReport  group;
    ZOrderReport zorder;
    CutReport    cut;
    PublishReport publish;
    PasteReport  paste;
    MergeReport  merge;
    std::string  detail;
    ActionReport()
        : committed(false), invalidation(core::kInvalidateNone), patchBytes(0),
          patchRuns(0), recordsTouched(0) {}
    bool ok() const { return error.ok() && committed; }
    std::string summary() const;
};

// ---------------------------------------------------------------------------
// SelectionCommands: the ONE entry point for a T05 user action
// ---------------------------------------------------------------------------
// It holds the CommandLog (the single write path) and the selection. The
// SELECTION IS NOT DOCUMENT STATE, so it is held here and mutated by the
// selection-only actions; a document action leaves it alone unless the action
// itself changes membership (delete, paste, ungroup).
class SelectionCommands {
public:
    SelectionCommands(core::CommandLog& log, SelectionSet& selection,
                      const selection::SelectionQuery* query)
        : log_(log), selection_(selection), query_(query), operations_(0),
          committed_(0), failed_(0), totalPatchBytes_(0) {}

    // --- selection only: NO transaction, NO history entry -------------------
    // Changing what is highlighted is not an edit. Wrapping it in a transaction
    // would put "select" entries on the undo stack, which is the behaviour the
    // task explicitly rejects.
    ActionReport selectAll(core::Document& document);
    ActionReport selectInverse(core::Document& document);
    ActionReport selectAt(core::Document& document, const core::ObjectRef& ref,
                          bool additive, bool toggle);
    ActionReport selectMolecule(core::Document& document, const core::ObjectRef& seed,
                                bool additive);
    ActionReport selectGroup(core::Document& document, core::ObjectId groupId, bool additive);
    ActionReport selectMarquee(core::Document& document, const MarqueeGesture& gesture,
                               const MarqueeOptions& options, bool additive);

    // --- document edits: ONE transaction each -------------------------------
    ActionReport deleteSelection(core::Document& document);
    ActionReport translateSelection(core::Document& document, const core::Vec2& delta,
                                    const TransformOptions& options = TransformOptions());
    ActionReport rotateSelection(core::Document& document, const core::Vec2& pivot,
                                 double degrees,
                                 const TransformOptions& options = TransformOptions());
    ActionReport scaleSelection(core::Document& document, const core::Vec2& pivot,
                                double factor,
                                const TransformOptions& options = TransformOptions());
    ActionReport stretchSelection(core::Document& document, const core::Vec2& pivot,
                                  double sx, double sy,
                                  const TransformOptions& options = TransformOptions());
    ActionReport flipSelection(core::Document& document, const core::Vec2& pivot,
                               bool horizontal,
                               const TransformOptions& options = TransformOptions());
    ActionReport alignSelection(core::Document& document, AlignMode mode,
                                const AlignOptions& options);
    ActionReport distributeSelection(core::Document& document, DistributeMode mode);
    ActionReport centerInPage(core::Document& document, const core::Box& page);
    ActionReport nudgeSelection(core::Document& document, const NudgeVector& direction,
                                bool shift, const NudgeProfile& profile = NudgeProfile());

    ActionReport groupSelection(core::Document& document, const std::string& label);
    ActionReport ungroupSelection(core::Document& document, bool recursive);
    ActionReport reorderSelection(core::Document& document, ZOrderCommand command);

    ActionReport cut(core::Document& document, IClipboardTransport& transport);
    ActionReport copy(core::Document& document, IClipboardTransport& transport);
    ActionReport paste(core::Document& document, IClipboardTransport& transport,
                       const core::Vec2& translate);
    ActionReport merge(core::Document& document, double maxDistance);

    // --- selection state ---------------------------------------------------
    SelectionSet& selection() { return selection_; }
    const SelectionSet& selection() const { return selection_; }
    void setQuery(const selection::SelectionQuery* query) { query_ = query; }
    const selection::SelectionQuery* query() const { return query_; }

    // --- counters (prove acts vs. non-acts) --------------------------------
    uint64_t operations() const { return operations_; }
    uint64_t committed() const { return committed_; }
    uint64_t failed() const { return failed_; }
    uint64_t totalPatchBytes() const { return totalPatchBytes_; }
    uint64_t selectionOnlyActions() const { return selectionOnly_; }
    void resetCounters();

private:
    // Run one core::Command through the log and fill the report's history
    // fields. This is the single place a T05 edit becomes a history entry.
    ActionReport run(core::Document& document, const core::Command& command,
                     const std::string& actionName);
    // Selection-only bookkeeping.
    ActionReport selectionOnly(const std::string& actionName, const SelectionDelta& delta,
                               uint64_t scanned);

    core::CommandLog& log_;
    SelectionSet& selection_;
    const selection::SelectionQuery* query_;
    uint64_t operations_;
    uint64_t committed_;
    uint64_t failed_;
    uint64_t totalPatchBytes_;
    uint64_t selectionOnly_ = 0;
};

// ---------------------------------------------------------------------------
// A core::Command adapter for the T05 primitives
// ---------------------------------------------------------------------------
// Needed because core::Command::Fn takes a CommandContext (document + view) and
// a T05 operation needs its own arguments. The capture is BY VALUE so a Repeat
// (Ctrl+Y) re-runs the SAME operation later without the caller keeping state.
core::Command makeSelectionCommand(const std::string& name, core::Invalidation invalidation,
                                   const std::vector<core::ObjectRef>& refs,
                                   const Affine2& matrix,
                                   const TransformOptions& options);

// ---------------------------------------------------------------------------
// TransactionLedger: history evidence
// ---------------------------------------------------------------------------
// Accumulates the committed transactions' patch statistics so the report can
// state "N actions, M bytes of incremental patch, longest run R" instead of
// asserting that history is incremental.
struct TransactionLedger {
    uint64_t actions;
    uint64_t bytes;
    uint64_t records;
    size_t   longestRun;
    uint64_t snapshots;    // MUST stay 0
    TransactionLedger() : actions(0), bytes(0), records(0), longestRun(0), snapshots(0) {}
    void observe(const core::Transaction& transaction);
    void observe(const core::CommandLog& log);
    void clear();
    std::string summary() const;
};

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_COMMANDS_HPP
