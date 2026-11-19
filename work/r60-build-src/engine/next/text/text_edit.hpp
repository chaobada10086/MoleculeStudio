#ifndef R62_NEXT_TEXT_TEXT_EDIT_HPP
#define R62_NEXT_TEXT_TEXT_EDIT_HPP
// =============================================================================
// re_round62_viewer/next/text/text_edit.hpp
//
// T09 - THE TEXT EDITING MODEL (caret, selection, clipboard, composition).
//
// WHY THIS IS NOT A WINDOW CONTROL
// -----------------------------------------------------------------------------
// A platform edit control would own the caret, the selection, the undo stack and
// the IME composition, and none of that can be tested without a window station,
// nor can it report a caret position in DOCUMENT space, which is what the
// renderer needs to draw the caret on a zoomed canvas.  This class owns the
// editing STATE and the mapping from caret offsets to document geometry; the
// platform layer (T04/T19) owns the window, the keystrokes and the IME.
//
// OFFSETS ARE UTF-16 CODE UNITS, deliberately:
//   * WM_CHAR, WM_IME_COMPOSITION, the clipboard formats and the Windows caret
//     all use UTF-16, so converting once at the boundary is what stops a caret
//     from landing inside a surrogate pair;
//   * rich_text.hpp provides cpToU16/u16ToCp so the code point view used by the
//     layout is always reachable.
//
// WHAT "INPUT METHOD MUST NOT FIRE A HOTKEY" MEANS HERE
// -----------------------------------------------------------------------------
// The session reports its FocusState.  While focus is Composition or TextEntry
// the platform layer MUST NOT run a tool hotkey; T04 owns that decision, and
// this class exposes the state instead of assuming it.  Escape during a
// composition cancels ONLY the composition; Escape with an active edit cancels
// the whole edit and restores the original runs, which is what makes "IM
// finish/cancel does not execute a tool" observable.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/text/rich_text.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace text {

// What the platform layer must consult before dispatching a hotkey.
enum class FocusState : uint8_t {
    Inactive = 0,     // no text edit; hotkeys are free to run
    TextEntry,        // an edit session is active; characters are text
    Composition       // an IME composition is open; EVERY key belongs to the IME
};

struct EditStyle {
    uint16_t    flags;    // core::TextFlags applied to inserted text
    double      size;     // 0 == inherit from the run at the caret
    std::string font;
    bool        useCaretStyle;
    EditStyle() : flags(0), size(0.0), useCaretStyle(true) {}
};

class TextEditSession {
public:
    // `original` is not modified; commit() returns the new run vector so the
    // caller can put it behind one core::Transaction (one undo step).
    TextEditSession();

    void begin(const std::vector<core::TextRun>& original);

    // A visual anchor: the document position a caret at u16 offset 0 sits at.
    // The platform layer supplies it so the geometry helpers can map offsets to
    // document points without this class knowing about a view.
    void setAnchor(const core::Vec2& anchor) { anchor_ = anchor; }
    core::Vec2 anchor() const { return anchor_; }

    // ---- state ----------------------------------------------------------
    FocusState focus() const { return compositionOpen_ ? FocusState::Composition
                                                        : FocusState::TextEntry; }
    bool active() const { return active_; }
    bool changed() const { return changed_; }
    const std::vector<core::TextRun>& runs() const { return runs_; }
    const std::vector<core::TextRun>& originalRuns() const { return original_; }
    size_t caret() const { return caret_; }
    size_t anchorOffset() const { return selAnchor_; }
    bool hasSelection() const { return caret_ != selAnchor_; }
    size_t selectionLow() const { return caret_ < selAnchor_ ? caret_ : selAnchor_; }
    size_t selectionHigh() const { return caret_ < selAnchor_ ? selAnchor_ : caret_; }
    const std::string& composition() const { return composition_; }
    bool compositionOpen() const { return compositionOpen_; }
    size_t undoCount() const { return undo_.size(); }

    // ---- editing --------------------------------------------------------
    bool insertUtf16(const std::vector<uint16_t>& units, const EditStyle& style);
    bool insertUtf8(const std::string& utf8, const EditStyle& style);
    bool backspace();
    bool deleteForward();
    bool selectAll();
    bool setSelection(size_t anchorOffset, size_t caret);
    // Clipboard text is plain here; a rich clipboard round-trip is T02/T05 work.
    std::string selectedText() const;
    // The visible text of every run joined, which is what a caller compares when it
    // wants "did the edit produce the right string" rather than "which run moved".
    std::string plainText() const;

    // ---- caret movement -------------------------------------------------
    bool moveCaret(int deltaUnits, bool extendSelection);
    bool moveCaretToLineEdge(bool end, bool extendSelection);
    bool moveCaretToStart(bool extendSelection);
    bool moveCaretToEnd(bool extendSelection);
    // Set the caret from a document point, using the layout of the current runs.
    bool moveCaretToPoint(const core::Vec2& documentPoint, GlyphCache* cache,
                          const LayoutOptions& options, bool extendSelection);

    // ---- composition (IME) ----------------------------------------------
    // Windows sends a whole composition string on each update; the session
    // REPLACES the previous composition instead of appending, which is what
    // makes a recomposition correct.
    void compositionUpdate(const std::vector<uint16_t>& units);
    void compositionUpdateUtf8(const std::string& utf8);
    bool compositionCommit(const EditStyle& style);
    bool compositionCancel();

    // ---- undo / cancel ---------------------------------------------------
    bool undo();
    // Restore the runs the session started with.  Returns true when anything
    // had changed.
    bool cancelAll();

    // The value to hand to core::Document::setRuns().  Only valid after a change.
    const std::vector<core::TextRun>& commit() const { return runs_; }

    // ---- geometry -------------------------------------------------------
    // caret rectangle in DOCUMENT space, using the current runs and the anchor.
    // `compositionExtra` is appended to the text for measurement so the caret
    // sits after the composition string, exactly where the user sees it.
    bool caretRect(GlyphCache* cache, const LayoutOptions& options,
                   core::Box* out) const;
    bool selectionRects(GlyphCache* cache, const LayoutOptions& options,
                        std::vector<core::Box>* out) const;
    // Total UTF-16 length of the runs.
    size_t totalUnits() const;

private:
    void pushUndo();
    size_t offsetToRun(size_t u16, uint32_t* cpInRun) const;
    void deleteRange(size_t low, size_t high);
    std::vector<core::TextRun> measureRuns() const;

    std::vector<core::TextRun> original_;
    std::vector<core::TextRun> runs_;
    std::vector<std::vector<core::TextRun> > undo_;
    size_t   caret_;
    size_t   selAnchor_;
    bool     active_;
    bool     changed_;
    std::string composition_;
    bool     compositionOpen_;
    core::Vec2 anchor_;
};

} // namespace text
} // namespace r62

#endif // R62_NEXT_TEXT_TEXT_EDIT_HPP
