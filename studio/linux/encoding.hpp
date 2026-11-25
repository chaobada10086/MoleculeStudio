#pragma once
#include <iconv.h>
#include <cerrno>
#include <string>
#include <stdexcept>

namespace linux_encoding {
inline std::string codepage(unsigned code) {
    if (code == 65001) return "UTF-8";
    if (code == 10000) return "MACINTOSH";
    return "CP" + std::to_string(code);
}
inline std::string convert(const std::string& input, const std::string& from,
                           const std::string& to) {
    iconv_t handle = iconv_open(to.c_str(), from.c_str());
    if (handle == (iconv_t)-1) throw std::runtime_error("Unsupported encoding: " + from + " -> " + to);
    struct Close { iconv_t h; ~Close(){ iconv_close(h); } } close{handle};
    size_t remaining = input.size();
    char* cursor = const_cast<char*>(input.data());
    std::string result;
    while (remaining) {
        char buf[4096];
        char* out = buf;
        size_t space = sizeof(buf);
        size_t rc = iconv(handle, &cursor, &remaining, &out, &space);
        if (rc == size_t(-1) && errno != E2BIG)
            throw std::runtime_error("Invalid or unrepresentable text: " + from + " -> " + to);
        result.append(buf, out);
    }
    return result;
}
}