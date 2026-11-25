#pragma once
#include "next/selection/clipboard.hpp"
namespace r62 { namespace selection {
constexpr const char* kDocumentClipboardFormat = "application/x-moleculestudio-document-selection";
constexpr const char* kPngClipboardFormat = "image/png";
constexpr const char* kCdxClipboardFormat = "chemical/x-cdx";
constexpr const char* kCdxmlClipboardFormat = "chemical/x-cdxml";
constexpr const char* kCdxmlVendorClipboardFormat = "application/vnd.chemdraw+xml";

class GtkClipboardTransport final : public IClipboardTransport {
    std::map<std::string,std::string> formats;
    core::Error replaceFormats(std::map<std::string,std::string> next);
public:
    core::Error open() override;
    void close() override { formats.clear(); }
    core::Error publish(const std::string&,const std::string&) override;
    core::Error publishAll(const std::map<std::string,std::string>&);
    core::Error read(const std::string&,std::string*,bool*) override;
    std::string readText();
    bool hasFormat(const std::string&) override;
    const char* name() const override { return "GTK"; }
};
}}
