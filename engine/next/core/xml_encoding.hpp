#pragma once

#include <cctype>
#include <string>

namespace r62 { namespace core {
inline std::size_t xmlEncodingValue(const std::string& declaration) {
    const auto name = declaration.find("encoding");
    if (name == std::string::npos) return name;
    const auto equal = declaration.find_first_not_of(" \t\r\n", name + 8);
    if (equal == std::string::npos || declaration[equal] != '=') return std::string::npos;
    const auto quote = declaration.find_first_not_of(" \t\r\n", equal + 1);
    if (quote == std::string::npos || (declaration[quote] != '\'' && declaration[quote] != '"'))
        return std::string::npos;
    return quote + 1;
}

inline bool declaresUtf8Xml(std::string declaration) {
    for (char& c : declaration) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (declaration.find("encoding") == std::string::npos) return true;
    const auto value = xmlEncodingValue(declaration);
    if (value == std::string::npos) return false;
    const auto end = declaration.find(declaration[value - 1], value);
    if (end == std::string::npos) return false;
    const auto encoding = declaration.substr(value, end - value);
    return encoding == "utf-8" || encoding == "utf8";
}

// The Windows release requires the hyphenated spelling when opening CDXML.
inline void canonicalizeXmlUtf8Alias(std::string& header) {
    const auto start = header.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3u : 0u;
    if (header.compare(start, 5, "<?xml") != 0) return;
    const auto end = header.find("?>", start + 5);
    if (end == std::string::npos) return;
    auto declaration = header.substr(start, end - start);
    for (char& c : declaration) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto value = xmlEncodingValue(declaration);
    if (value != std::string::npos && value + 4 < declaration.size() &&
        declaration.compare(value, 4, "utf8") == 0 && declaration[value + 4] == declaration[value - 1])
        header.replace(start + value, 4, "UTF-8");
}
} }
