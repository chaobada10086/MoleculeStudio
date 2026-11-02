#ifndef R62_NEXT_SELECTION_CLIPBOARD_WIN32_HPP
#define R62_NEXT_SELECTION_CLIPBOARD_WIN32_HPP
// =============================================================================
// re_round62_viewer/next/selection/clipboard_win32.hpp
//
// T05 - THE REAL WIN32 CLIPBOARD TRANSPORT.
//
// WHAT THIS FILE IS
// -----------------------------------------------------------------------------
// IClipboardTransport implemented over OpenClipboard / SetClipboardData /
// GetClipboardData with a REGISTERED format. It is the ONLY file in T05 that
// includes <windows.h>, and it is compiled ONLY into the Win32 demo, never into
// the headless test EXEs. That separation is what lets the cut/paste rules be
// tested without a window (see clipboard.hpp).
//
// IT OWNS NO POLICY. It does not decide when to cut, what to publish, or
// whether a failure matters. It reports failure and nothing else.
//
// WHY A REGISTERED FORMAT AND NOT CF_TEXT OR A CHEMDRAW NAME
// -----------------------------------------------------------------------------
//  * A registered format ("r62.selection.package.v1") carries a byte payload
//    that CF_TEXT would corrupt at the first zero byte.
//  * No ChemDraw clipboard format NAME is published anywhere this project has
//    verified, so claiming one would be a fabricated compatibility claim. See
//    clipboardFormatProvenance().
// The text companion format IS CF_UNICODETEXT (a real, documented Win32 format)
// so a paste into Notepad shows a human-readable line rather than nothing.
// =============================================================================

#include "next/selection/clipboard.hpp"

namespace r62 {
namespace selection {

// Register the module's clipboard format. Idempotent; returns 0 on failure.
// Must be called once before the transport is used.
unsigned int registerSelectionClipboardFormat();

// The transport. Owns no window: OpenClipboard(0) associates the clipboard with
// the calling task, which is what a headless demo wants.
class Win32ClipboardTransport : public IClipboardTransport {
public:
    Win32ClipboardTransport();
    virtual ~Win32ClipboardTransport();

    virtual core::Error open();
    virtual void close();
    virtual core::Error publish(const std::string& formatName, const std::string& bytes);
    virtual core::Error read(const std::string& formatName, std::string* bytes, bool* present);
    virtual bool hasFormat(const std::string& formatName);
    virtual const char* name() const { return "win32"; }

    // Standard Win32 format id for the selection package, or 0 when unregistered.
    unsigned int registeredFormat() const { return registered_; }
    // Last Win32 error code observed, for the status line.
    unsigned long lastError() const { return lastError_; }

private:
    bool     opened_;
    unsigned int registered_;
    unsigned long lastError_;
};

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_CLIPBOARD_WIN32_HPP
