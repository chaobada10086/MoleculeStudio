#include "clipboard.hpp"
#include <gtk/gtk.h>
#include <limits>
#include <memory>

namespace r62 { namespace selection {
namespace {
using Payload = std::map<std::string,std::string>;
const char* mime(const std::string& format){
    if (format == kDocumentClipboardFormat || format == kPngClipboardFormat ||
            format == kCdxClipboardFormat || format == kCdxmlClipboardFormat ||
            format == kCdxmlVendorClipboardFormat) return format.c_str();
    return format == kSelectionClipboardTextName ? "text/plain;charset=utf-8" : "application/x-moleculestudio-selection";
}
void appendFormat(Payload& payload, const std::string& format, const std::string& bytes) {
    payload[mime(format)] = bytes;
    if (format == kSelectionClipboardTextName) payload["UTF8_STRING"] = bytes;
}
GtkClipboard* systemClipboard(){return gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);}
void provide(GtkClipboard*,GtkSelectionData* data,guint,gpointer owner){
    gchar* target=gdk_atom_name(gtk_selection_data_get_target(data));
    auto& payload=*static_cast<Payload*>(owner);
    auto entry=payload.find(target);
    g_free(target);
    if(entry!=payload.end())
        gtk_selection_data_set(data,gtk_selection_data_get_target(data),8,
            reinterpret_cast<const guchar*>(entry->second.data()),static_cast<gint>(entry->second.size()));
}
void release(GtkClipboard*,gpointer owner){delete static_cast<Payload*>(owner);}
}
core::Error GtkClipboardTransport::open(){
    formats.clear();
    if(!gdk_display_get_default())return {core::ErrorCode::IoError,"无法连接桌面剪贴板"};
    return {};
}
core::Error GtkClipboardTransport::publish(const std::string& format,const std::string& bytes){
    auto next = formats;
    appendFormat(next, format, bytes);
    return replaceFormats(std::move(next));
}
core::Error GtkClipboardTransport::publishAll(const std::map<std::string,std::string>& entries) {
    Payload next;
    for (const auto& entry : entries) appendFormat(next, entry.first, entry.second);
    return replaceFormats(std::move(next));
}
core::Error GtkClipboardTransport::replaceFormats(std::map<std::string,std::string> next) {
    if (next.empty()) return {core::ErrorCode::InvalidArgument, "没有可复制的数据"};
    for (const auto& entry : next)
        if (entry.second.size() > std::numeric_limits<gint>::max())
            return {core::ErrorCode::InvalidArgument, "剪贴板数据过大"};
    auto payload=std::make_unique<Payload>(next);
    std::vector<GtkTargetEntry> targets;
    for(const auto& item:*payload)targets.push_back({const_cast<char*>(item.first.c_str()),0,0});
    if(!gtk_clipboard_set_with_data(systemClipboard(),targets.data(),targets.size(),provide,release,payload.get()))
        return {core::ErrorCode::IoError,"系统拒绝复制"};
    payload.release();
    formats = std::move(next);
    gtk_clipboard_set_can_store(systemClipboard(),nullptr,0);
    return {};
}
bool GtkClipboardTransport::hasFormat(const std::string& format){
    return gtk_clipboard_wait_is_target_available(systemClipboard(),gdk_atom_intern(mime(format),FALSE));
}
core::Error GtkClipboardTransport::read(const std::string& format,std::string* bytes,bool* present){
    bytes->clear();
    *present=hasFormat(format);
    if(!*present)return {};
    auto* data=gtk_clipboard_wait_for_contents(systemClipboard(),gdk_atom_intern(mime(format),FALSE));
    if(!data)return {core::ErrorCode::IoError,"无法读取系统剪贴板"};
    int size=gtk_selection_data_get_length(data);
    if(size>0)bytes->assign(reinterpret_cast<const char*>(gtk_selection_data_get_data(data)),size);
    gtk_selection_data_free(data);
    return size<0 ? core::Error(core::ErrorCode::IoError,"剪贴板内容已失效") : core::Error();
}
std::string GtkClipboardTransport::readText() {
    const std::unique_ptr<gchar, decltype(&g_free)> text(gtk_clipboard_wait_for_text(systemClipboard()), g_free);
    return text ? std::string(text.get()) : std::string();
}
}}
