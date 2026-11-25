#pragma once

#include "i18n.hpp"
#include "next/core/contracts.hpp"
#include "worker.hpp"
#include <filesystem>
#include <sstream>

namespace linux_update {
namespace fs = std::filesystem;
using r62::core::Error;
using r62::core::ErrorCode;
using linux_i18n::tr;

inline fs::path chooseDestination(GtkWindow* owner, const std::string& name) {
    auto* dialog = gtk_file_chooser_dialog_new(tr("保存更新安装包"), owner, GTK_FILE_CHOOSER_ACTION_SAVE,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("保存"), GTK_RESPONSE_ACCEPT, nullptr);
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    if (const auto* downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD))
        gtk_file_chooser_set_current_folder(chooser, downloads);
    gtk_file_chooser_set_current_name(chooser, name.c_str());
    fs::path destination;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        auto* filename = gtk_file_chooser_get_filename(chooser);
        if (filename) destination = filename;
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
    return destination;
}

inline Error openPackage(GtkWindow* owner, const fs::path& destination) {
    GError* failure = nullptr;
    auto* url = g_filename_to_uri(destination.c_str(), nullptr, &failure);
    const bool opened = url && gtk_show_uri_on_window(owner, url, GDK_CURRENT_TIME, &failure);
    g_free(url);
    const std::string message = failure ? failure->message : tr("无法打开安装包");
    g_clear_error(&failure);
    return opened ? Error() : Error(ErrorCode::IoError, message);
}

inline Error run(GtkWindow* owner, const std::string& worker, std::string& status) {
    std::string error;
    bool cancelled = false;
    auto response = linux_worker::run(owner, "正在检查更新…", {"/usr/bin/python3", worker, "check"},
        "", error, 30, false, &cancelled);
    if (cancelled) return {};
    if (response.empty()) return {ErrorCode::IoError, error};
    std::istringstream result(response);
    std::string line;
    std::getline(result, line);
    std::istringstream columns(line);
    std::vector<std::string> fields;
    for (std::string field; std::getline(columns, field, '\t');) fields.push_back(std::move(field));
    if (fields.size() == 2 && fields[0] == "current") {
        status = std::string(tr("已安装版本满足当前发布要求")) + " · " + fields[1];
        return {};
    }
    if (fields.size() != 8 || fields[0] != "available") return {ErrorCode::ParseError, "无法读取更新检查结果"};

    auto* found = gtk_message_dialog_new(owner, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
        "%s %s", tr("有可用更新"), fields[2].c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(found), "%s %s\n%s", tr("已安装版本"),
        fields[1].c_str(), fields[4].c_str());
    gtk_dialog_add_button(GTK_DIALOG(found), tr("取消"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(found), tr("下载更新…"), GTK_RESPONSE_ACCEPT);
    const bool download = gtk_dialog_run(GTK_DIALOG(found)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(found);
    if (!download) return {};
    const auto destination = chooseDestination(owner, fields[4]);
    if (destination.empty()) return {};

    auto scratchPath = (destination.parent_path() / ".moleculestudio-update.XXXXXX").string();
    if (!g_mkdtemp(scratchPath.data())) return {ErrorCode::IoError, "无法创建更新暂存文件夹"};
    struct Scratch {
        fs::path path;
        ~Scratch() { std::error_code ignored; fs::remove_all(path, ignored); }
    } scratch{scratchPath};
    const auto staged = scratch.path / "package.deb";
    response = linux_worker::run(owner, "正在下载并校验更新…", {"/usr/bin/python3", worker, "download",
        fields[2], fields[3], fields[5], fields[6], fields[7], staged.string()}, "", error, 600, false, &cancelled);
    if (cancelled) return {};
    if (response.empty()) return {ErrorCode::IoError, error};
    std::error_code moveError;
    fs::rename(staged, destination, moveError);
    if (moveError) return {ErrorCode::IoError, moveError.message()};
    status = std::string(tr("已下载并校验更新")) + " · " + destination.string();

    auto* ready = gtk_message_dialog_new(owner, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
        "%s", tr("已下载并校验更新"));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(ready), "%s", destination.c_str());
    gtk_dialog_add_button(GTK_DIALOG(ready), tr("关闭"), GTK_RESPONSE_CLOSE);
    gtk_dialog_add_button(GTK_DIALOG(ready), tr("打开安装包"), GTK_RESPONSE_ACCEPT);
    const bool open = gtk_dialog_run(GTK_DIALOG(ready)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(ready);
    return open ? openPackage(owner, destination) : Error();
}
}
