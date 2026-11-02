// =============================================================================
// re_round62_viewer/next/text/text_edit.cpp
//
// T09 - text editing state machine.  See text_edit.hpp for the design notes.
// =============================================================================

#include "next/text/text_edit.hpp"

#include <algorithm>

namespace r62 {
namespace text {

namespace {

// The concatenation of every run's text.  The session works in UTF-16 offsets
// over this sequence, so an insert can never land inside a surrogate pair: the
// offset is converted to code points before any run is touched.
std::string joinRuns(const std::vector<core::TextRun>& runs) {
    std::string all;
    for (size_t i = 0; i < runs.size(); ++i) all += runs[i].utf8;
    return all;
}

} // namespace

TextEditSession::TextEditSession()
    : caret_(0), selAnchor_(0), active_(false), changed_(false),
      compositionOpen_(false), anchor_(0.0, 0.0) {}

void TextEditSession::begin(const std::vector<core::TextRun>& original) {
    original_ = original;
    if (original_.empty()) {
        core::TextRun empty;
        original_.push_back(empty);
    }
    runs_ = original_;
    caret_ = 0;
    selAnchor_ = 0;
    active_ = true;
    changed_ = false;
    composition_.clear();
    compositionOpen_ = false;
    undo_.clear();
}

size_t TextEditSession::totalUnits() const {
    size_t total = 0;
    for (size_t i = 0; i < runs_.size(); ++i)
        total += static_cast<size_t>(cpToU16(runs_[i].utf8, 0xFFFFFFFFu));
    return total;
}

void TextEditSession::pushUndo() {
    // A bounded stack: an editing session is short, and an unbounded one in a
    // long-lived session would be the same leak the document history avoids.
    if (undo_.size() >= 64) undo_.erase(undo_.begin());
    undo_.push_back(runs_);
}

bool TextEditSession::insertUtf16(const std::vector<uint16_t>& units,
                                  const EditStyle& style) {
    if (units.empty()) return false;
    pushUndo();
    const std::string inserted = utf16ToUtf8(units);
    const size_t low = selectionLow();
    const size_t high = selectionHigh();

    std::vector<core::TextRun> result;
    result.push_back(core::TextRun());
    result.clear();
    size_t consumed = 0;
    bool placed = false;
    for (size_t i = 0; i < runs_.size(); ++i) {
        const std::vector<uint16_t> runUnits = utf8ToUtf16(runs_[i].utf8);
        const size_t runLen = runUnits.size();
        const size_t begin = consumed;
        const size_t end = consumed + runLen;
        if (end <= low) {
            result.push_back(runs_[i]);
        } else if (begin >= high) {
            if (!placed) {
                core::TextRun nr;
                nr.utf8 = inserted;
                nr.flags = style.flags;
                nr.size = style.useCaretStyle ? 0.0 : style.size;
                nr.fontName = style.font;
                result.push_back(nr);
                placed = true;
            }
            result.push_back(runs_[i]);
        } else {
            // The insertion point is inside this run.
            const size_t top = std::min(low - begin, runLen);
            const size_t bottom = (high > begin) ? std::min(high - begin, runLen) : 0;
            const std::string head = utf16ToUtf8(std::vector<uint16_t>(
                runUnits.begin(), runUnits.begin() + static_cast<long>(top)));
            const std::string tail = utf16ToUtf8(std::vector<uint16_t>(
                runUnits.begin() + static_cast<long>(bottom), runUnits.end()));
            core::TextRun h = runs_[i];
            h.utf8 = head;
            if (!h.utf8.empty()) result.push_back(h);
            if (!placed) {
                core::TextRun nr;
                nr.utf8 = inserted;
                nr.flags = style.flags;
                nr.size = style.useCaretStyle ? runs_[i].size : style.size;
                nr.fontName = style.font.empty() ? runs_[i].fontName : style.font;
                result.push_back(nr);
                placed = true;
            }
            core::TextRun t = runs_[i];
            t.utf8 = tail;
            if (!t.utf8.empty()) result.push_back(t);
        }
        consumed = end;
    }
    if (!placed) {
        core::TextRun nr;
        nr.utf8 = inserted;
        nr.flags = style.flags;
        nr.size = style.size;
        nr.fontName = style.font;
        result.push_back(nr);
    }
    // Drop empty runs but always keep at least one, so run(0) exists as the
    // document contract requires.
    std::vector<core::TextRun> compact;
    for (size_t i = 0; i < result.size(); ++i)
        if (!result[i].utf8.empty()) compact.push_back(result[i]);
    if (compact.empty()) compact.push_back(core::TextRun());
    runs_ = compact;
    caret_ = low + units.size();
    selAnchor_ = caret_;
    changed_ = true;
    composition_.clear();
    compositionOpen_ = false;
    return true;
}

bool TextEditSession::insertUtf8(const std::string& utf8, const EditStyle& style) {
    return insertUtf16(utf8ToUtf16(utf8), style);
}

void TextEditSession::deleteRange(size_t low, size_t high) {
    if (low >= high) return;
    pushUndo();
    std::vector<core::TextRun> result;
    size_t consumed = 0;
    for (size_t i = 0; i < runs_.size(); ++i) {
        const std::vector<uint16_t> runUnits = utf8ToUtf16(runs_[i].utf8);
        const size_t runLen = runUnits.size();
        const size_t begin = consumed;
        const size_t end = consumed + runLen;
        if (end <= low || begin >= high) {
            result.push_back(runs_[i]);
        } else {
            const size_t top = (low > begin) ? std::min(low - begin, runLen) : 0;
            const size_t bottom = (high > begin) ? std::min(high - begin, runLen) : 0;
            const std::string head = utf16ToUtf8(std::vector<uint16_t>(
                runUnits.begin(), runUnits.begin() + static_cast<long>(top)));
            const std::string tail = utf16ToUtf8(std::vector<uint16_t>(
                runUnits.begin() + static_cast<long>(bottom), runUnits.end()));
            core::TextRun h = runs_[i];
            h.utf8 = head;
            if (!h.utf8.empty()) result.push_back(h);
            core::TextRun t = runs_[i];
            t.utf8 = tail;
            if (!t.utf8.empty()) result.push_back(t);
        }
        consumed = end;
    }
    std::vector<core::TextRun> compact;
    for (size_t i = 0; i < result.size(); ++i)
        if (!result[i].utf8.empty()) compact.push_back(result[i]);
    if (compact.empty()) compact.push_back(core::TextRun());
    runs_ = compact;
    caret_ = low;
    selAnchor_ = low;
    changed_ = true;
}

bool TextEditSession::backspace() {
    if (hasSelection()) { deleteRange(selectionLow(), selectionHigh()); return true; }
    if (caret_ == 0) return false;
    // Step back one CODE POINT, so a surrogate pair or a combining mark goes as
    // one unit from the user point of view.
    const std::vector<uint16_t> units = utf8ToUtf16(joinRuns(runs_));
    size_t step = 1;
    if (caret_ >= 2 && units[caret_ - 1] >= 0xDC00u && units[caret_ - 1] <= 0xDFFFu &&
        units[caret_ - 2] >= 0xD800u && units[caret_ - 2] <= 0xDBFFu)
        step = 2;
    deleteRange(caret_ - step, caret_);
    return true;
}

bool TextEditSession::deleteForward() {
    if (hasSelection()) { deleteRange(selectionLow(), selectionHigh()); return true; }
    const size_t total = totalUnits();
    if (caret_ >= total) return false;
    const std::vector<uint16_t> units = utf8ToUtf16(joinRuns(runs_));
    size_t step = 1;
    if (caret_ + 1 < units.size() && units[caret_] >= 0xD800u &&
        units[caret_] <= 0xDBFFu && units[caret_ + 1] >= 0xDC00u &&
        units[caret_ + 1] <= 0xDFFFu)
        step = 2;
    deleteRange(caret_, caret_ + step);
    return true;
}

bool TextEditSession::selectAll() {
    selAnchor_ = 0;
    caret_ = totalUnits();
    return true;
}

bool TextEditSession::setSelection(size_t anchorOffset, size_t caret) {
    const size_t total = totalUnits();
    selAnchor_ = std::min(anchorOffset, total);
    caret_ = std::min(caret, total);
    return true;
}

std::string TextEditSession::selectedText() const {
    const std::vector<uint16_t> units = utf8ToUtf16(joinRuns(runs_));
    const size_t low = std::min(selectionLow(), units.size());
    const size_t high = std::min(selectionHigh(), units.size());
    return utf16ToUtf8(std::vector<uint16_t>(units.begin() + static_cast<long>(low),
                                             units.begin() + static_cast<long>(high)));
}

std::string TextEditSession::plainText() const {
    return joinRuns(runs_);
}

bool TextEditSession::moveCaret(int deltaUnits, bool extendSelection) {
    const size_t total = totalUnits();
    long next = static_cast<long>(caret_) + deltaUnits;
    if (next < 0) next = 0;
    if (next > static_cast<long>(total)) next = static_cast<long>(total);
    caret_ = static_cast<size_t>(next);
    if (!extendSelection) selAnchor_ = caret_;
    return true;
}

bool TextEditSession::moveCaretToLineEdge(bool end, bool extendSelection) {
    const std::vector<uint16_t> units = utf8ToUtf16(joinRuns(runs_));
    if (end) {
        size_t at = caret_;
        while (at < units.size() && units[at] != 0x0Au) ++at;
        caret_ = at;
    } else {
        size_t at = caret_;
        while (at > 0 && units[at - 1] != 0x0Au) --at;
        caret_ = at;
    }
    if (!extendSelection) selAnchor_ = caret_;
    return true;
}

bool TextEditSession::moveCaretToStart(bool extendSelection) {
    caret_ = 0;
    if (!extendSelection) selAnchor_ = caret_;
    return true;
}

bool TextEditSession::moveCaretToEnd(bool extendSelection) {
    caret_ = totalUnits();
    if (!extendSelection) selAnchor_ = caret_;
    return true;
}

std::vector<core::TextRun> TextEditSession::measureRuns() const {
    std::vector<core::TextRun> out = runs_;
    if (compositionOpen_ && !composition_.empty()) {
        core::TextRun c;
        c.utf8 = composition_;
        c.flags = static_cast<uint16_t>(core::kUnderline);
        if (out.empty()) out.push_back(c);
        else out.push_back(c);
    }
    return out;
}

bool TextEditSession::caretRect(GlyphCache* cache, const LayoutOptions& options,
                                core::Box* out) const {
    if (!cache || !out) return false;
    RichLayout layout;
    const core::Error e = layoutRichText(measureRuns(), anchor_, options, cache, &layout);
    if (!e.ok()) return false;
    // The caret offset counts the composition as committed text, which is what
    // puts it after the underlined composition string.
    const uint32_t cp = u16ToCp(joinRuns(measureRuns()), static_cast<uint32_t>(caret_));
    const TextHit hit = caretAtCp(layout, cp, true);
    const double half = 0.5;
    double top = 0.0, bottom = 0.0;
    if (!layout.lines.empty()) {
        const CaretRect box = lineBox(layout, hit.lineIndex);
        top = box.y0;
        bottom = box.y1;
    }
    *out = core::Box(core::Vec2(anchor_.x + hit.x - half, anchor_.y + top),
                     core::Vec2(anchor_.x + hit.x + half, anchor_.y + bottom));
    return true;
}

bool TextEditSession::selectionRects(GlyphCache* cache, const LayoutOptions& options,
                                     std::vector<core::Box>* out) const {
    if (!cache || !out) return false;
    out->clear();
    if (!hasSelection()) return true;
    RichLayout layout;
    const core::Error e = layoutRichText(runs_, anchor_, options, cache, &layout);
    if (!e.ok()) return false;
    const std::string all = joinRuns(runs_);
    const uint32_t cpLow = u16ToCp(all, static_cast<uint32_t>(selectionLow()));
    const uint32_t cpHigh = u16ToCp(all, static_cast<uint32_t>(selectionHigh()));
    // Fully qualified: the member function has the same name as the free layout
    // helper, and the member would be found first.
    const std::vector<CaretRect> rects =
        ::r62::text::selectionRects(layout, cpLow, cpHigh);
    for (size_t i = 0; i < rects.size(); ++i) {
        out->push_back(core::Box(core::Vec2(anchor_.x + rects[i].x0, anchor_.y + rects[i].y0),
                                 core::Vec2(anchor_.x + rects[i].x1, anchor_.y + rects[i].y1)));
    }
    return true;
}

bool TextEditSession::moveCaretToPoint(const core::Vec2& documentPoint, GlyphCache* cache,
                                       const LayoutOptions& options,
                                       bool extendSelection) {
    if (!cache) return false;
    RichLayout layout;
    const core::Error e = layoutRichText(runs_, anchor_, options, cache, &layout);
    if (!e.ok()) return false;
    const LPoint p(documentPoint.x - anchor_.x, documentPoint.y - anchor_.y);
    const TextHit hit = hitTest(layout, p);
    caret_ = hit.u16Index;
    if (!extendSelection) selAnchor_ = caret_;
    return true;
}

void TextEditSession::compositionUpdate(const std::vector<uint16_t>& units) {
    composition_ = utf16ToUtf8(units);
    compositionOpen_ = !composition_.empty();
}

void TextEditSession::compositionUpdateUtf8(const std::string& utf8) {
    composition_ = utf8;
    compositionOpen_ = !composition_.empty();
}

bool TextEditSession::compositionCommit(const EditStyle& style) {
    if (!compositionOpen_) return false;
    const std::string text = composition_;
    composition_.clear();
    compositionOpen_ = false;
    if (text.empty()) return false;
    return insertUtf8(text, style);
}

bool TextEditSession::compositionCancel() {
    if (!compositionOpen_) return false;
    composition_.clear();
    compositionOpen_ = false;
    return true;
}

bool TextEditSession::undo() {
    if (undo_.empty()) return false;
    runs_ = undo_.back();
    undo_.pop_back();
    const size_t total = totalUnits();
    if (caret_ > total) caret_ = total;
    if (selAnchor_ > total) selAnchor_ = total;
    changed_ = true;
    return true;
}

bool TextEditSession::cancelAll() {
    const bool was = changed_;
    runs_ = original_;
    undo_.clear();
    caret_ = 0;
    selAnchor_ = 0;
    composition_.clear();
    compositionOpen_ = false;
    changed_ = false;
    return was;
}

} // namespace text
} // namespace r62
