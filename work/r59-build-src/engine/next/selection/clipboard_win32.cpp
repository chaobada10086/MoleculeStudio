#include "next/selection/clipboard_win32.hpp"

// The ONLY <windows.h> include in T05, kept in a platform file that the headless
// test executables never link.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace r62 {
namespace selection {

namespace {

// Global-memory allocation for a clipboard payload. GlobalAlloc with GMEM_MOVEABLE
// is what SetClipboardData requires for a non-standard format.
HGLOBAL allocateGlobal(const std::string& bytes) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes.size() ? bytes.size() : 1);
    if (!handle) return 0;
    void* target = GlobalLock(handle);
    if (!target) {
        GlobalFree(handle);
        return 0;
    }
    if (!bytes.empty()) memcpy(target, bytes.data(), bytes.size());
    GlobalUnlock(handle);
    return handle;
}

// UTF-8 -> UTF-16 for CF_UNICODETEXT. Done by hand rather than through
// MultiByteToWideChar(CP_ACP) so the payload is independent of the process code
// page - the same rule T02's Unicode path handling follows.
std::wstring utf8ToWide(const std::string& utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned int code = 0;
        size_t extra = 0;
        if (c < 0x80u) { code = c; extra = 0; }
        else if ((c & 0xE0u) == 0xC0u) { code = c & 0x1Fu; extra = 1; }
        else if ((c & 0xF0u) == 0xE0u) { code = c & 0x0Fu; extra = 2; }
        else if ((c & 0xF8u) == 0xF0u) { code = c & 0x07u; extra = 3; }
        else { ++i; continue; }   // invalid lead byte: skip, do not guess
        if (i + extra >= utf8.size()) break;
        for (size_t k = 1; k <= extra; ++k)
            code = (code << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
        i += extra + 1;
        if (code <= 0xFFFFu) {
            out.push_back(static_cast<wchar_t>(code));
        } else {
            // Surrogate pair. The clipboard wants UTF-16, so this is required
            // rather than optional for an emoji or a rare CJK ideograph.
            const unsigned int v = code - 0x10000u;
            out.push_back(static_cast<wchar_t>(0xD800u + (v >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
        }
    }
    return out;
}

UINT formatIdFor(const std::string& formatName, unsigned int registered) {
    if (formatName == kSelectionClipboardTextName) return CF_UNICODETEXT;
    if (formatName == kSelectionClipboardFormatName && registered != 0) return registered;
    return 0;
}

} // namespace

unsigned int registerSelectionClipboardFormat() {
    static unsigned int cached = 0;
    if (cached != 0) return cached;
    cached = RegisterClipboardFormatA(kSelectionClipboardFormatName);
    return cached;
}

Win32ClipboardTransport::Win32ClipboardTransport()
    : opened_(false), registered_(registerSelectionClipboardFormat()), lastError_(0) {}

Win32ClipboardTransport::~Win32ClipboardTransport() { close(); }

core::Error Win32ClipboardTransport::open() {
    if (opened_) return core::Error::success();
    // A fixed number of attempts, not an unbounded loop: another process can hold
    // the clipboard open and spinning forever would hang the UI thread.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(0)) {
            opened_ = true;
            return core::Error::success();
        }
        lastError_ = GetLastError();
        Sleep(1);
    }
    return core::Error(core::ErrorCode::Locked,
                       "OpenClipboard failed; another process holds the clipboard");
}

void Win32ClipboardTransport::close() {
    if (!opened_) return;
    CloseClipboard();
    opened_ = false;
}

core::Error Win32ClipboardTransport::publish(const std::string& formatName,
                                             const std::string& bytes) {
    if (!opened_)
        return core::Error(core::ErrorCode::Locked, "clipboard is not open");
    const UINT format = formatIdFor(formatName, registered_);
    if (format == 0)
        return core::Error(core::ErrorCode::Unsupported,
                           "no clipboard format registered for " + formatName);
    if (bytes.empty())
        return core::Error(core::ErrorCode::InvalidArgument,
                           "refused to publish an empty payload");

    if (format == CF_UNICODETEXT) {
        const std::wstring wide = utf8ToWide(bytes);
        const size_t byteCount = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, byteCount);
        if (!handle) {
            lastError_ = GetLastError();
            return core::Error(core::ErrorCode::IoError, "GlobalAlloc failed");
        }
        void* target = GlobalLock(handle);
        if (!target) {
            GlobalFree(handle);
            lastError_ = GetLastError();
            return core::Error(core::ErrorCode::IoError, "GlobalLock failed");
        }
        memcpy(target, wide.c_str(), byteCount);
        GlobalUnlock(handle);
        if (!SetClipboardData(CF_UNICODETEXT, handle)) {
            lastError_ = GetLastError();
            GlobalFree(handle);
            return core::Error(core::ErrorCode::IoError, "SetClipboardData(CF_UNICODETEXT) failed");
        }
        return core::Error::success();
    }

    HGLOBAL handle = allocateGlobal(bytes);
    if (!handle) {
        lastError_ = GetLastError();
        return core::Error(core::ErrorCode::IoError, "clipboard allocation failed");
    }
    // EmptyClipboard is called ONLY for the primary format, so publishing the
    // package first and the text afterwards leaves BOTH on the clipboard.
    if (!SetClipboardData(format, handle)) {
        lastError_ = GetLastError();
        GlobalFree(handle);   // ownership stays with us when the call fails
        return core::Error(core::ErrorCode::IoError, "SetClipboardData failed");
    }
    return core::Error::success();
}

core::Error Win32ClipboardTransport::read(const std::string& formatName,
                                          std::string* bytes, bool* present) {
    if (!opened_)
        return core::Error(core::ErrorCode::Locked, "clipboard is not open");
    const UINT format = formatIdFor(formatName, registered_);
    if (format == 0) {
        if (present) *present = false;
        return core::Error::success();
    }
    if (!IsClipboardFormatAvailable(format)) {
        if (present) *present = false;
        if (bytes) bytes->clear();
        return core::Error::success();
    }
    HANDLE handle = GetClipboardData(format);
    if (!handle) {
        lastError_ = GetLastError();
        return core::Error(core::ErrorCode::IoError, "GetClipboardData failed");
    }
    if (format == CF_UNICODETEXT) {
        const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(handle));
        if (!wide) {
            lastError_ = GetLastError();
            return core::Error(core::ErrorCode::IoError, "GlobalLock failed");
        }
        // UTF-16 -> UTF-8. An unpaired surrogate is replaced with U+FFFD rather
        // than produced as an invalid sequence.
        std::string utf8;
        for (size_t i = 0; wide[i] != 0; ++i) {
            unsigned int code = static_cast<unsigned int>(wide[i]);
            if (code >= 0xD800u && code <= 0xDBFFu && wide[i + 1] != 0) {
                const unsigned int low = static_cast<unsigned int>(wide[i + 1]);
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                    ++i;
                }
            }
            if (code <= 0x7Fu) utf8.push_back(static_cast<char>(code));
            else if (code <= 0x7FFu) {
                utf8.push_back(static_cast<char>(0xC0u | (code >> 6)));
                utf8.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
            } else if (code <= 0xFFFFu) {
                utf8.push_back(static_cast<char>(0xE0u | (code >> 12)));
                utf8.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
                utf8.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
            } else {
                utf8.push_back(static_cast<char>(0xF0u | (code >> 18)));
                utf8.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3Fu)));
                utf8.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
                utf8.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
            }
        }
        GlobalUnlock(handle);
        if (present) *present = true;
        if (bytes) *bytes = utf8;
        return core::Error::success();
    }

    const SIZE_T size = GlobalSize(handle);
    const void* source = GlobalLock(handle);
    if (!source) {
        lastError_ = GetLastError();
        return core::Error(core::ErrorCode::IoError, "GlobalLock failed");
    }
    if (bytes) bytes->assign(static_cast<const char*>(source), static_cast<size_t>(size));
    GlobalUnlock(handle);
    if (present) *present = true;
    return core::Error::success();
}

bool Win32ClipboardTransport::hasFormat(const std::string& formatName) {
    const UINT format = formatIdFor(formatName, registered_);
    if (format == 0) return false;
    return IsClipboardFormatAvailable(format) != 0;
}

} // namespace selection
} // namespace r62
