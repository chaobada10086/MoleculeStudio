#ifndef R62_NEXT_SELECTION_CLIPBOARD_HPP
#define R62_NEXT_SELECTION_CLIPBOARD_HPP
// =============================================================================
// re_round62_viewer/next/selection/clipboard.hpp
//
// T05 - CUT / COPY / PASTE, CTRL-DRAG COPY, AND REPEAT.
//
// THE FOUR RULES THIS FILE EXISTS TO ENFORCE
// -----------------------------------------------------------------------------
// R1  A CUT DELETES NOTHING UNTIL THE CLIPBOARD HAS ACCEPTED THE PAYLOAD.
//     "Put it on the clipboard, then delete" is the only ordering that cannot
//     lose data. If publishing fails, the document is untouched - not partially
//     deleted, not "deleted but recoverable".
// R2  A FAILED PUBLISH OR READ CHANGES NOTHING. Not the selection, not the
//     document revision, not the clipboard's own previous content when that can
//     be preserved.
// R3  PASTE REMAPS EVERY INTERNAL REFERENCE. A copied ring's bond must point at
//     the COPY's atoms, never back at the original's. T02's selection package
//     guarantees this for the payload; this module's job is to make the failure
//     modes visible (a refused paste reports why) and to keep the whole paste in
//     ONE transaction.
// R4  CTRL-DRAG COPY KEEPS THE ORIGINAL. The dragged ghost is a PREVIEW; the
//     source objects are frozen and never mutated during the drag, and the
//     commit inserts copies while leaving the originals exactly where they were.
//
// PLATFORM BOUNDARY
// -----------------------------------------------------------------------------
// This module never calls a Win32 API directly in its core. It talks to an
// IClipboardTransport, which the platform layer implements with the real
// clipboard. That is what makes "a publish failure does not cut" testable
// without a window, and it is why the tests can inject a transport that always
// fails.
//
// FORMAT NAME
// -----------------------------------------------------------------------------
// T02 explicitly declines to pick a clipboard format name (RESEARCH_NOTES.md
// section 5 records that the "ChemDraw.CDFormat" value in this project's COM
// metadata is an INFERENCE, not a vendor declaration). So this module defines
// its OWN registered format name ("r62.selection.package.v1"), publishes a
// second plain-text form for human paste targets, and NEVER claims to be a
// ChemDraw binary format. A future verified format name is a one-line change to
// kSelectionClipboardFormatName.
// =============================================================================

#include "next/selection/grouping.hpp"
#include "next/selection/transform.hpp"

#include <map>
#include <string>
#include <vector>

// The T02 surface this module consumes. Included here rather than in a .cpp so
// a caller compiles against ONE header and the dependency is visible in the
// public interface.
#include "next/persistence/persistence.hpp"

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// Clipboard format identity
// ---------------------------------------------------------------------------
// NOT a claim about ChemDraw. See the header block.
extern const char* const kSelectionClipboardFormatName;   // "r62.selection.package.v1"
extern const char* const kSelectionClipboardTextName;     // "r62.selection.text.v1"
// The documented reason we do not use a ChemDraw-named format.
const char* clipboardFormatProvenance();

// ---------------------------------------------------------------------------
// IClipboardTransport: the platform seam
// ---------------------------------------------------------------------------
// Every method returns a core::Error. A platform that cannot answer MUST return
// an error rather than a plausible empty value, because an empty value is
// indistinguishable from "the clipboard is empty" and would silently turn a
// failed read into a successful empty paste.
class IClipboardTransport {
public:
    virtual ~IClipboardTransport() {}

    // Open/close the clipboard. A transport may implement these as no-ops; the
    // engine calls them symmetrically, so a real Win32 transport maps them to
    // OpenClipboard/CloseClipboard.
    virtual core::Error open() = 0;
    virtual void close() = 0;

    // Publish `bytes` under the registered format name. Returns an error when
    // the clipboard refuses (locked by another process, out of memory, a
    // non-Win32 platform). MUST NOT report success for a partial write.
    virtual core::Error publish(const std::string& formatName,
                                const std::string& bytes) = 0;

    // Read back. `present` distinguishes "the format is not offered" (OK with
    // present=false) from "reading it failed" (an error).
    virtual core::Error read(const std::string& formatName,
                             std::string* bytes, bool* present) = 0;

    // Whether a format is on the clipboard right now. Used to decide whether a
    // Paste menu item is enabled.
    virtual bool hasFormat(const std::string& formatName) = 0;

    // Human-readable transport name for status output.
    virtual const char* name() const = 0;
};

// An in-process transport. Real clipboard semantics (the bytes survive the
// engine's lifetime), used by the tests and by a headless host.
class MemoryClipboardTransport : public IClipboardTransport {
public:
    MemoryClipboardTransport() : open_(false), publishes_(0), reads_(0) {}
    virtual core::Error open();
    virtual void close() { open_ = false; }
    virtual core::Error publish(const std::string& formatName, const std::string& bytes);
    virtual core::Error read(const std::string& formatName, std::string* bytes, bool* present);
    virtual bool hasFormat(const std::string& formatName);
    virtual const char* name() const { return "memory"; }

    // Test controls.
    void failNextPublish(const std::string& why) { failPublish_ = true; failWhy_ = why; }
    void failNextRead(const std::string& why) { failRead_ = true; failWhy_ = why; }
    void clearAll();
    size_t publishCount() const { return publishes_; }
    size_t readCount() const { return reads_; }
    const std::map<std::string, std::string>& contents() const { return slots_; }

private:
    std::map<std::string, std::string> slots_;
    bool open_;
    bool failPublish_;
    bool failRead_;
    std::string failWhy_;
    size_t publishes_;
    size_t reads_;
};

// ---------------------------------------------------------------------------
// Publish / read a selection package
// ---------------------------------------------------------------------------
struct PublishReport {
    bool     published;
    uint64_t bytes;
    uint64_t objectsEncoded;
    core::Error error;
    std::string why;
    PublishReport() : published(false), bytes(0), objectsEncoded(0) {}
};

// Encode the selection and publish it. THE DOCUMENT IS NOT TOUCHED. `refs` is
// closed over its dependencies by T02 before encoding, so a copied bond brings
// its atoms.
PublishReport publishSelection(const core::Document& document,
                              const std::vector<core::ObjectRef>& refs,
                              IClipboardTransport& transport,
                              bool alsoPublishText);

struct FetchReport {
    bool     present;
    uint64_t bytes;
    core::Error error;
    std::string why;
    FetchReport() : present(false), bytes(0) {}
};

// Read the package back WITHOUT touching a document. This is what a Paste menu
// enablement check and a paste preview both use.
FetchReport fetchSelection(IClipboardTransport& transport, std::string* package);

// ---------------------------------------------------------------------------
// Cut
// ---------------------------------------------------------------------------
// R1, literally: publish FIRST, and only delete when publish succeeded.
struct CutReport {
    PublishReport publish;
    bool     deleted;
    uint64_t deletedObjects;
    core::Error deleteError;
    std::string why;
    CutReport() : deleted(false), deletedObjects(0) {}
    bool ok() const { return publish.published && deleted; }
};

// The delete is performed by the CALLER's command layer through `deleteFn`, so
// this module never opens a transaction on its own. `deleteFn` returns the
// error from the transaction; a non-Ok result leaves the document as it was
// (the caller's transaction rolls back) and is reported.
//
// When publish fails, deleteFn is NEVER CALLED. That is asserted by the tests
// with a deleteFn that increments a counter.
typedef core::Error (*DeleteObjectsFn)(void* userData,
                                       const std::vector<core::ObjectRef>& refs);

CutReport cutSelection(core::Document& document,
                       const std::vector<core::ObjectRef>& refs,
                       IClipboardTransport& transport,
                       DeleteObjectsFn deleteFn, void* deleteUserData,
                       SelectionDelta* selectionDelta);

// ---------------------------------------------------------------------------
// Copy (no delete)
// ---------------------------------------------------------------------------
inline PublishReport copySelection(const core::Document& document,
                                   const std::vector<core::ObjectRef>& refs,
                                   IClipboardTransport& transport,
                                   bool alsoPublishText = true) {
    return publishSelection(document, refs, transport, alsoPublishText);
}

// ---------------------------------------------------------------------------
// Paste
// ---------------------------------------------------------------------------
struct PasteReport {
    bool     pasted;
    uint64_t objectsInserted;
    uint64_t bytesRead;
    core::Error error;
    std::string why;
    std::vector<core::ObjectRef> inserted;
    persistence::PasteMapping mapping;
    PasteReport() : pasted(false), objectsInserted(0), bytesRead(0) {}
};

// Paste the clipboard into `target`. Runs inside ONE core::Transaction owned by
// this call, so a failure leaves the target document byte-identical (asserted by
// comparing Document::digest() before and after). `translate` offsets the
// pasted objects so a paste at the pointer does not land on the original.
PasteReport pasteSelection(core::Document& target, IClipboardTransport& transport,
                           const core::Vec2& translate);

// Paste a package the caller already holds (a drag-copy preview holds one).
PasteReport pastePackage(core::Document& target, const std::string& package,
                         const core::Vec2& translate);

// ---------------------------------------------------------------------------
// Ctrl-drag copy
// ---------------------------------------------------------------------------
// The drag machinery for a copy. It holds the FROZEN source geometry so nothing
// writes the model during the drag, and it holds the encoded package so the
// commit does not have to re-encode (and so a preview and its commit paste the
// SAME bytes - the preview/commit agreement rule).
//
// Lifecycle:
//   begin()   -> encode the source, freeze its geometry, produce a ghost
//   update()  -> move the ghost; NEVER touches the document
//   commit()  -> paste the frozen package at the final offset, one transaction
//   cancel()  -> drop everything; the document was never touched
class CopyDrag {
public:
    CopyDrag();
    ~CopyDrag();

    // Start a copy-drag. `constrain` selects the Ctrl+Shift axis lock [G82-85].
    // Returns false and leaves `why` set when there is nothing to copy or the
    // package could not be encoded. NO DOCUMENT WRITE HAPPENS HERE.
    bool begin(const core::Document& document,
               const std::vector<core::ObjectRef>& refs,
               const core::Vec2& origin,
               bool constrainAxis);

    bool active() const { return active_; }
    bool constrained() const { return constrained_; }
    const core::Vec2& origin() const { return origin_; }
    const core::Vec2& offset() const { return offset_; }
    core::Vec2 pointer() const { return origin_ + offset_; }
    const std::string& package() const { return package_; }
    const std::vector<core::ObjectRef>& sources() const { return sources_; }
    // The frozen source bounds at begin(). The preview draws the GHOST at
    // frozenBounds translated by offset(), so the original is visibly unmoved.
    const core::Box& frozenBounds() const { return frozenBounds_; }
    uint64_t sourceCount() const { return static_cast<uint64_t>(sources_.size()); }
    uint64_t packageBytes() const { return static_cast<uint64_t>(package_.size()); }
    const std::string& why() const { return why_; }

    // Move the ghost. `constrainAxis` (Ctrl+Shift) locks the motion to the
    // dominant axis. Returns the new offset. DOES NOT WRITE THE DOCUMENT.
    core::Vec2 update(const core::Vec2& pointer, bool constrainAxis);

    // Commit: paste the frozen package at the current offset inside one
    // transaction. On failure the target is unchanged and the drag stays active
    // so the user can retry or cancel.
    PasteReport commit(core::Document& target);

    // Abandon. The document was never touched, so this only forgets state.
    void cancel();

private:
    bool           active_;
    bool           constrained_;
    core::Vec2     origin_;
    core::Vec2     offset_;
    std::string    package_;
    std::vector<core::ObjectRef> sources_;
    core::Box      frozenBounds_;
    std::string    why_;
};

// ---------------------------------------------------------------------------
// Repeat (Ctrl+Y) [G81]
// ---------------------------------------------------------------------------
// ChemDraw's Ctrl+Y REPEATS the last action as a NEW history entry; it is not
// redo. T01's CommandLog already implements that (repeat()), so this module does
// NOT reimplement it. What belongs here is the DECISION of whether the last
// action is repeatable and the honest report when it is not.
struct RepeatReport {
    bool repeatable;
    std::string label;
    std::string why;
    RepeatReport() : repeatable(false) {}
};
RepeatReport repeatStatus(const core::CommandLog& log);

// ---------------------------------------------------------------------------
// Merge [G124-126]
// ---------------------------------------------------------------------------
// Ctrl+J merges the selected objects. It is an EXPLICIT command, deliberately
// separate from snapping (which joins as you draw) and from grouping (which
// changes ownership, not chemistry). This module performs the SAFE part: it
// merges atoms that pass mergeCompatible and reports, per pair, why a pair was
// refused. It never merges two atoms just because they are close.
struct MergeReport {
    uint64_t mergedPairs;
    uint64_t refusedPairs;
    uint64_t bondsRewired;
    uint64_t duplicateBondsDropped;
    // A bond between the two merged atoms becomes a self-bond and is removed.
    // Counted separately from a duplicate so the two causes stay distinct.
    uint64_t selfBondsDropped;
    std::vector<std::string> refusals;
    core::Error error;
    std::string summary() const;
    MergeReport() : mergedPairs(0), refusedPairs(0), bondsRewired(0),
                    duplicateBondsDropped(0), selfBondsDropped(0) {}
};

// Merge the selected atoms. The merge policy: for each pair of selected atoms
// within `maxDistance` that is COMPATIBLE, the higher-id atom is removed and
// its bonds are rewired to the lower-id atom. A bond that would become a
// self-bond is dropped; a bond that would duplicate an existing one is dropped
// and counted. The WHOLE merge is the caller's transaction.
MergeReport mergeSelection(core::Document& document,
                           const std::vector<core::ObjectRef>& refs,
                           double maxDistance);

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_CLIPBOARD_HPP
