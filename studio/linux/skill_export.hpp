#pragma once

#include "i18n.hpp"
#include "next/core/contracts.hpp"
#include <filesystem>

namespace linux_skill {
namespace fs = std::filesystem;
using r62::core::Error;
using r62::core::ErrorCode;

constexpr const char* name = "moleculestudio-draw";

inline Error exportTo(const fs::path& source, const fs::path& folder, bool replace) {
    try {
        if (!fs::is_regular_file(source / "SKILL.md"))
            return {ErrorCode::NotFound, "内置 AI 绘图 Skill 缺少 SKILL.md"};
        if (!fs::is_directory(folder)) return {ErrorCode::InvalidArgument, "目标文件夹无效"};
        const auto target = folder / name;
        const auto targetType = fs::symlink_status(target).type();
        const bool existing = targetType != fs::file_type::not_found;
        if (existing && (!replace || targetType != fs::file_type::directory))
            return {ErrorCode::AlreadyExists, "目标已存在，无法导出 AI 绘图 Skill"};

        auto scratchPath = (folder / ".moleculestudio-export.XXXXXX").string();
        if (!g_mkdtemp(scratchPath.data())) return {ErrorCode::IoError, "无法创建 Skill 导出目录"};
        struct Scratch {
            fs::path path;
            bool cleanup = true;
            ~Scratch() {
                std::error_code ignored;
                if (cleanup) fs::remove_all(path, ignored);
            }
        } scratch{scratchPath};
        const auto ready = scratch.path / name;
        const auto previous = scratch.path / "previous";
        fs::copy(source, ready, fs::copy_options::recursive | fs::copy_options::skip_symlinks);

        if (existing) fs::rename(target, previous);
        // GIO's no-overwrite move also catches a destination created after the
        // chooser confirmed the export. Both directories are on the same volume.
        auto* from = g_file_new_for_path(ready.c_str());
        auto* to = g_file_new_for_path(target.c_str());
        GError* failure = nullptr;
        const bool moved = g_file_move(from, to, G_FILE_COPY_NO_FALLBACK_FOR_MOVE, nullptr, nullptr, nullptr, &failure);
        g_object_unref(from);
        g_object_unref(to);
        if (moved) return {};
        const std::string message = failure ? failure->message : "无法写入目标文件夹";
        g_clear_error(&failure);
        if (existing) {
            std::error_code restore;
            fs::rename(previous, target, restore);
            if (restore) {
                scratch.cleanup = false;
                return {ErrorCode::IoError, message + " · " + linux_i18n::tr("原目录备份") + " " + previous.string()};
            }
        }
        return {ErrorCode::IoError, message};
    } catch (const fs::filesystem_error& error) {
        return {ErrorCode::IoError, error.what()};
    }
}

inline fs::path chooseFolder(GtkWindow* owner) {
    using linux_i18n::tr;
    auto* chooser = gtk_file_chooser_dialog_new(tr("另存 AI 绘图 Skill…"), owner, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("选择"), GTK_RESPONSE_ACCEPT, nullptr);
    fs::path folder;
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (filename) folder = filename;
        g_free(filename);
    }
    gtk_widget_destroy(chooser);
    return folder;
}

inline bool confirmReplacement(GtkWindow* owner, const fs::path& path) {
    using linux_i18n::tr;
    auto* dialog = gtk_message_dialog_new(owner, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "%s", tr("替换现有的 AI 绘图 Skill？"));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", path.c_str());
    gtk_dialog_add_button(GTK_DIALOG(dialog), tr("取消"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), tr("替换"), GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    const bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(dialog);
    return accepted;
}
}
