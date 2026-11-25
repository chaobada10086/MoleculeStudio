#pragma once

#include "canvas.hpp"
#include "i18n.hpp"
#include "interop.hpp"
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <set>
#include <tuple>

namespace linux_templates {
using namespace r62;
using namespace r62::core;
using r62::integration::Session;
using linux_i18n::tr;

struct Entry {
    std::string name, group, path;
};

inline std::string directory() {
    if (auto* path = g_getenv("MOLECULE_STUDIO_CUSTOM_TEMPLATE_DIR"))
        if (*path) return path;
    const auto* data = g_getenv("MOLECULE_STUDIO_DATA_DIR");
    return (data && *data ? std::string(data) :
        std::string(g_get_user_data_dir()) + "/moleculestudio-native") + "/custom_templates";
}

inline std::string trim(std::string text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}

inline std::string folded(const std::string& text) {
    auto* value = g_utf8_casefold(text.data(), text.size());
    std::string output = value;
    g_free(value);
    return output;
}

inline void require(const Error& result) {
    if (!result.ok()) throw std::runtime_error(result.message);
}

inline bool validLabel(const std::string& text) {
    return !trim(text).empty() && text.find('\0') == std::string::npos && validateUtf8(text).ok();
}

inline Entry read(const std::string& path, Document& doc) {
    std::string bytes;
    require(persistence::readFileBytes(path, &bytes));
    std::istringstream input(bytes);
    require(integration::loadSafeNative(doc, input));
    const auto& metadata = doc.documentExtensions();
    const auto* name = metadata.findValue("studio.template.name");
    const auto* group = metadata.findValue("studio.template.group");
    if (!name || !group || !validLabel(*name) || !validLabel(*group) || studioio::documentEmpty(doc))
        throw std::runtime_error("这不是有效的自定义模板文件");
    return {*name, *group, path};
}

inline std::vector<Entry> entries(const std::string& path) {
    GError* failure = nullptr;
    auto* handle = g_dir_open(path.c_str(), 0, &failure);
    if (!handle) {
        const bool missing = g_error_matches(failure, G_FILE_ERROR, G_FILE_ERROR_NOENT);
        const std::string message = failure->message;
        g_error_free(failure);
        if (missing) return {};
        throw std::runtime_error(message);
    }
    std::unique_ptr<GDir, decltype(&g_dir_close)> dir(handle, g_dir_close);
    std::vector<Entry> result;
    while (const auto* name = g_dir_read_name(dir.get())) {
        if (!g_str_has_suffix(name, ".t01doc")) continue;
        const auto file = path + "/" + name;
        if (!g_file_test(file.c_str(), G_FILE_TEST_IS_REGULAR)) continue;
        try {
            Document doc;
            result.push_back(read(file, doc));
        } catch (const std::runtime_error&) {
            // Keep the rest of the library available when an entry is damaged.
        }
    }
    std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) {
        return std::tie(a.group, a.name, a.path) < std::tie(b.group, b.name, b.path);
    });
    return result;
}

inline void write(const Document& doc, const std::string& path) {
    std::ostringstream output;
    require(integration::saveSafeNative(doc, output));
    persistence::AtomicWriteOptions options;
    options.verifyAfterWrite = true;
    require(persistence::writeFileAtomic(path, output.str(), options));
}

inline Entry save(Document& doc, std::string group, std::string name, bool importing = false) {
    group = trim(group);
    name = trim(name);
    if (!validLabel(group) || !validLabel(name)) throw std::runtime_error("请输入分组和模板名称");
    if (studioio::documentEmpty(doc)) throw std::runtime_error("所选内容无法保存为模板");
    const auto folder = directory();
    if (g_mkdir_with_parents(folder.c_str(), 0700) != 0) throw std::runtime_error("无法创建模板目录");
    struct Lock {
        int fd;
        ~Lock() { if (fd >= 0) close(fd); }
    } lock{open((folder + "/.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600)};
    if (lock.fd < 0 || flock(lock.fd, LOCK_EX) != 0) throw std::runtime_error("无法锁定模板目录");
    const auto existing = entries(folder);
    const auto inUse = [&](const std::string& candidate) {
        const auto key = folded(candidate);
        return std::any_of(existing.begin(), existing.end(), [&](const Entry& entry) {
            return entry.group == group && folded(entry.name) == key;
        });
    };
    if (importing) {
        const auto original = name;
        for (unsigned suffix = 2; inUse(name); ++suffix)
            name = original + "（导入 " + std::to_string(suffix) + "）";
    } else if (inUse(name)) {
        throw std::runtime_error("该分组已有同名模板，请换一个名称");
    }

    auto* uuid = g_uuid_string_random();
    const auto path = folder + "/" + uuid + ".t01doc";
    g_free(uuid);
    doc.documentExtensions().set("studio.template.name", name);
    doc.documentExtensions().set("studio.template.group", group);
    write(doc, path);
    return {name, group, path};
}

inline GtkWidget* preview(std::shared_ptr<Session>& source) {
    auto* widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(widget, 230, 180);
    g_signal_connect(widget, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, gpointer data) -> gboolean {
        const auto& session = *static_cast<std::shared_ptr<Session>*>(data);
        if (!session) return FALSE;
        const auto bounds = session->scene.bounds();
        if (bounds.empty()) return FALSE;
        auto view = session->renderView;
        view.widthPx = gtk_widget_get_allocated_width(widget);
        view.heightPx = gtk_widget_get_allocated_height(widget);
        if (view.widthPx <= 20 || view.heightPx <= 20) return FALSE;
        view.transform.viewportCenter = Vec2(view.widthPx * .5, view.heightPx * .5);
        view.transform.center = (bounds.min + bounds.max) * .5;
        view.transform.scale = std::min((view.widthPx - 20) / std::max(1., bounds.width()),
                                        (view.heightPx - 20) / std::max(1., bounds.height()));
        linux_canvas::paint(cr, session->scene, nullptr, view, session->measurer.get());
        return TRUE;
    }), &source);
    return widget;
}

inline bool saveSelection(GtkWindow* owner, Session& current) {
    if (current.selected.empty()) throw std::runtime_error("请先框选要保存的结构");
    auto source = std::make_shared<Session>();
    require(studioio::clipboardSubset(current.doc, current.selected, source->doc));
    if (source->all().empty()) throw std::runtime_error("所选内容无法保存为模板");
    source->refresh();
    const auto saved = entries(directory());
    auto* dialog = gtk_dialog_new_with_buttons(tr("添加自定义模板"), owner, GTK_DIALOG_MODAL,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("保存模板"), GTK_RESPONSE_ACCEPT, nullptr);
    auto* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(body), 12);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), body);
    gtk_box_pack_start(GTK_BOX(body), preview(source), TRUE, TRUE, 0);
    auto* fields = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_box_pack_start(GTK_BOX(body), GTK_WIDGET(fields), TRUE, TRUE, 0);
    auto* groups = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new_with_entry());
    std::set<std::string> names;
    for (const auto& entry : saved) names.insert(entry.group);
    if (names.empty()) names.insert(tr("我的结构"));
    for (const auto& name : names) gtk_combo_box_text_append_text(groups, name.c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(groups), 0);
    auto* group = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(groups)));
    auto* name = GTK_ENTRY(gtk_entry_new());
    auto field = [&](const char* label, GtkWidget* widget, GtkEntry* entry) {
        auto* caption = gtk_label_new(tr(label));
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_label_set_mnemonic_widget(GTK_LABEL(caption), GTK_WIDGET(entry));
        gtk_entry_set_max_length(entry, 120);
        gtk_entry_set_activates_default(entry, TRUE);
        gtk_box_pack_start(fields, caption, FALSE, FALSE, 0);
        gtk_box_pack_start(fields, widget, FALSE, FALSE, 0);
    };
    field("分组（可输入新分组）", GTK_WIDGET(groups), group);
    field("模板名称", GTK_WIDGET(name), name);
    auto* status = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_line_wrap(status, TRUE);
    gtk_label_set_max_width_chars(status, 36);
    gtk_box_pack_start(fields, GTK_WIDGET(status), FALSE, FALSE, 0);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(GTK_WIDGET(name));
    bool accepted = false;
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        try {
            save(source->doc, gtk_entry_get_text(group), gtk_entry_get_text(name));
            accepted = true;
            break;
        } catch (const std::runtime_error& error) {
            gtk_label_set_text(status, linux_i18n::message(error.what()).c_str());
        }
    }
    gtk_widget_destroy(dialog);
    return accepted;
}

class Picker {
    std::vector<Entry> entries_;
    std::shared_ptr<Session> source_;
    std::string filter_;
    GtkWidget *dialog_, *list_, *preview_, *status_;
    GtkListStore* rows_;
    GtkTreeModelFilter* model_;

    int chosen() const {
        GtkTreeIter row;
        GtkTreeModel* model;
        if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list_)), &model, &row)) return -1;
        int index;
        gtk_tree_model_get(model, &row, 2, &index, -1);
        return index;
    }

    void load() {
        source_.reset();
        const int index = chosen();
        gtk_label_set_text(GTK_LABEL(status_), entries_.empty() ? tr("暂无自定义模板") : "");
        if (index >= 0) {
            try {
                auto source = std::make_shared<Session>();
                read(entries_[index].path, source->doc);
                source->refresh();
                source->fit();
                source_ = std::move(source);
            } catch (const std::runtime_error& error) {
                gtk_label_set_text(GTK_LABEL(status_), linux_i18n::message(error.what()).c_str());
            }
        }
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), GTK_RESPONSE_ACCEPT, bool(source_));
        gtk_widget_queue_draw(preview_);
    }

public:
    Picker(GtkWindow* owner, bool exporting) : entries_(entries(directory())) {
        dialog_ = gtk_dialog_new_with_buttons(tr(exporting ? "导出自定义模板" : "我的模板"), owner, GTK_DIALOG_MODAL,
            tr("取消"), GTK_RESPONSE_CANCEL, tr(exporting ? "选择并导出" : "放置"), GTK_RESPONSE_ACCEPT, nullptr);
        gtk_window_set_default_size(GTK_WINDOW(dialog_), 640, 380);
        auto* body = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog_)));
        gtk_container_set_border_width(GTK_CONTAINER(body), 12);
        auto* search = gtk_search_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(search), tr("搜索模板名称或分组…"));
        gtk_box_pack_start(body, search, FALSE, FALSE, 4);
        auto* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(body, content, TRUE, TRUE, 4);
        rows_ = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);
        for (size_t i = 0; i < entries_.size(); ++i) {
            const auto& item = entries_[i];
            const auto searchText = folded(item.name + "\n" + item.group);
            GtkTreeIter row;
            gtk_list_store_insert_with_values(rows_, &row, -1, 0, item.name.c_str(),
                1, item.group.c_str(), 2, int(i), 3, searchText.c_str(), -1);
        }
        model_ = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(rows_), nullptr));
        gtk_tree_model_filter_set_visible_func(model_, +[](GtkTreeModel* model, GtkTreeIter* row, gpointer data) -> gboolean {
            const auto& filter = static_cast<Picker*>(data)->filter_;
            if (filter.empty()) return TRUE;
            gchar* value = nullptr;
            gtk_tree_model_get(model, row, 3, &value, -1);
            const bool show = value && std::string(value).find(filter) != std::string::npos;
            g_free(value);
            return show;
        }, this, nullptr);
        list_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model_));
        for (int column = 0; column < 2; ++column)
            gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_), -1,
                tr(column == 0 ? "模板名称" : "分组"), gtk_cell_renderer_text_new(), "text", column, nullptr);
        auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(scroll), list_);
        gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
        preview_ = preview(source_);
        gtk_box_pack_start(GTK_BOX(content), preview_, TRUE, TRUE, 0);
        status_ = gtk_label_new(entries_.empty() ? tr("暂无自定义模板") : "");
        gtk_box_pack_start(body, status_, FALSE, FALSE, 2);
        auto* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list_));
        g_signal_connect(selection, "changed", G_CALLBACK(+[](GtkTreeSelection*, Picker* self) { self->load(); }), this);
        g_signal_connect(search, "changed", G_CALLBACK(+[](GtkEditable* field, Picker* self) {
            self->filter_ = folded(gtk_entry_get_text(GTK_ENTRY(field)));
            gtk_tree_model_filter_refilter(self->model_);
        }), this);
        g_signal_connect(list_, "row-activated", G_CALLBACK(+[](GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, Picker* self) {
            if (self->source_) gtk_dialog_response(GTK_DIALOG(self->dialog_), GTK_RESPONSE_ACCEPT);
        }), this);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), GTK_RESPONSE_ACCEPT, FALSE);
        GtkTreeIter first;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model_), &first)) gtk_tree_selection_select_iter(selection, &first);
    }

    ~Picker() {
        gtk_widget_destroy(dialog_);
        g_object_unref(model_);
        g_object_unref(rows_);
    }
    Picker(const Picker&) = delete;
    Picker& operator=(const Picker&) = delete;

    std::shared_ptr<Session> run(std::string& name) {
        gtk_widget_show_all(dialog_);
        if (gtk_dialog_run(GTK_DIALOG(dialog_)) != GTK_RESPONSE_ACCEPT || !source_) return {};
        const auto index = chosen();
        if (index < 0) return {};
        name = entries_[index].name;
        return source_;
    }
};

inline std::string chooseFile(GtkWindow* owner, bool importing, std::string name = {}) {
    auto* dialog = gtk_file_chooser_dialog_new(tr(importing ? "导入自定义模板" : "导出自定义模板"), owner,
        importing ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE,
        tr("取消"), GTK_RESPONSE_CANCEL, tr(importing ? "导入" : "导出"), GTK_RESPONSE_ACCEPT, nullptr);
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, tr("MoleculeStudio 自定义模板"));
    gtk_file_filter_add_pattern(filter, "*.[mM][sS][tT][eE][mM][pP][lL][aA][tT][eE]");
    gtk_file_filter_add_pattern(filter, "*.[tT]01[dD][oO][cC]");
    gtk_file_chooser_add_filter(chooser, filter);
    auto* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, tr("所有文件"));
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(chooser, all);
    if (!importing) {
        for (auto& c : name)
            if (std::string("\\/:*?\"<>|").find(c) != std::string::npos) c = '_';
        gtk_file_chooser_set_current_name(chooser, (name + ".mstemplate").c_str());
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    }
    std::string path;
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* value = gtk_file_chooser_get_filename(chooser);
        if (!value) continue;
        std::filesystem::path target(value);
        g_free(value);
        if (!importing && !target.has_extension()) {
            target += ".mstemplate";
            if (g_file_test(target.c_str(), G_FILE_TEST_EXISTS)) {
                gtk_file_chooser_set_current_folder(chooser, target.parent_path().c_str());
                gtk_file_chooser_set_current_name(chooser, target.filename().c_str());
                continue;
            }
        }
        path = target.string();
        break;
    }
    gtk_widget_destroy(dialog);
    return path;
}

}
