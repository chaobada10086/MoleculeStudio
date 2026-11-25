#pragma once

#include "canvas.hpp"
#include "i18n.hpp"
#include "nicknames.hpp"

namespace linux_groups {
using linux_i18n::tr;
using r62::integration::Session;
using namespace r62::core;

inline void complete(GtkEntry* entry) {
    customgroups::load();
    if (!chemcompletion::enabled() || customgroups::entries().empty()) return;
    auto* names = gtk_list_store_new(1, G_TYPE_STRING);
    for (const auto& group : customgroups::entries()) {
        GtkTreeIter row;
        gtk_list_store_append(names, &row);
        gtk_list_store_set(names, &row, 0, group.name.c_str(), -1);
    }
    auto* completion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(names));
    gtk_entry_completion_set_text_column(completion, 0);
    gtk_entry_completion_set_minimum_key_length(completion, 1);
    gtk_entry_set_completion(entry, completion);
    g_object_unref(completion);
    g_object_unref(names);
}

class Manager {
    enum Response { SaveGroup = 1, InsertGroup, RemoveGroup };
    Session& session_;
    GtkWidget *dialog_, *name_, *list_, *preview_, *status_;
    GtkListStore* rows_;
    std::unique_ptr<Session> structure_;

    std::string selected() const {
        GtkTreeModel* model;
        GtkTreeIter row;
        if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list_)), &model, &row))
            return {};
        gchar* name = nullptr;
        gtk_tree_model_get(model, &row, 0, &name, -1);
        std::string result = name ? name : "";
        g_free(name);
        return result;
    }

    void message(const std::string& status) const {
        gtk_label_set_text(GTK_LABEL(status_), linux_i18n::message(status).c_str());
    }

    void showSelection() {
        structure_.reset();
        const auto selection = selected();
        const auto* entry = customgroups::find(selection);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), InsertGroup, FALSE);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), RemoveGroup, entry != nullptr);
        message(customgroups::readError());
        if (entry) {
            auto structure = std::make_unique<Session>();
            const auto result = customgroups::preview(*entry, structure->doc);
            if (result.ok()) {
                structure->refresh();
                structure_ = std::move(structure);
                gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), InsertGroup, TRUE);
            } else message(result.message);
        }
        gtk_widget_queue_draw(preview_);
    }

    void refresh(const std::string& selection = {}) {
        gtk_list_store_clear(rows_);
        GtkTreeIter selectedRow;
        bool found = false;
        for (const auto& item : customgroups::entries()) {
            GtkTreeIter row;
            gtk_list_store_append(rows_, &row);
            gtk_list_store_set(rows_, &row, 0, item.name.c_str(), -1);
            if (item.name == selection) {
                selectedRow = row;
                found = true;
            }
        }
        if (!found) found = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(rows_), &selectedRow);
        if (found) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(list_)), &selectedRow);
        else showSelection();
    }

    void draw(cairo_t* cr) const {
        if (!structure_) return;
        auto view = structure_->renderView;
        view.widthPx = gtk_widget_get_allocated_width(preview_);
        view.heightPx = gtk_widget_get_allocated_height(preview_);
        auto bounds = structure_->scene.bounds();
        if (bounds.empty() || view.widthPx <= 20 || view.heightPx <= 20) return;
        view.transform.viewportCenter = Vec2(view.widthPx * .5, view.heightPx * .5);
        view.transform.center = (bounds.min + bounds.max) * .5;
        view.transform.scale = std::min({2.5, (view.widthPx - 20) / std::max(1., bounds.width()),
                                                  (view.heightPx - 20) / std::max(1., bounds.height())});
        linux_canvas::paint(cr, structure_->scene, nullptr, view, structure_->measurer.get());
    }

    bool confirmRemoval(const std::string& name) const {
        auto* confirmation = gtk_message_dialog_new(GTK_WINDOW(dialog_), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE, "%s", tr("从库中移除此官能团？"));
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirmation), "%s", name.c_str());
        gtk_dialog_add_buttons(GTK_DIALOG(confirmation), tr("取消"), GTK_RESPONSE_CANCEL,
            tr("移除"), GTK_RESPONSE_ACCEPT, nullptr);
        const auto response = gtk_dialog_run(GTK_DIALOG(confirmation));
        gtk_widget_destroy(confirmation);
        return response == GTK_RESPONSE_ACCEPT;
    }

public:
    Manager(GtkWindow* owner, Session& session) : session_(session) {
        dialog_ = gtk_dialog_new_with_buttons(tr("自定义官能团"), owner, GTK_DIALOG_MODAL,
            tr("关闭"), GTK_RESPONSE_CLOSE, tr("移除"), RemoveGroup,
            tr("从选区保存"), SaveGroup, tr("插入缩写"), InsertGroup, nullptr);
        gtk_window_set_default_size(GTK_WINDOW(dialog_), 650, 410);
        auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog_)));
        gtk_container_set_border_width(GTK_CONTAINER(content), 12);
        auto* panels = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(content, panels, TRUE, TRUE, 4);
        rows_ = gtk_list_store_new(1, G_TYPE_STRING);
        list_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(rows_));
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_), -1,
            tr("缩写"), gtk_cell_renderer_text_new(), "text", 0, nullptr);
        auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(scroll, 180, 200);
        gtk_container_add(GTK_CONTAINER(scroll), list_);
        gtk_box_pack_start(GTK_BOX(panels), scroll, FALSE, TRUE, 0);

        auto* border = gtk_frame_new(tr("结构式预览"));
        preview_ = gtk_drawing_area_new();
        gtk_widget_set_size_request(preview_, 275, 200);
        gtk_container_add(GTK_CONTAINER(border), preview_);
        gtk_box_pack_start(GTK_BOX(panels), border, TRUE, TRUE, 0);

        auto* form = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        auto* label = gtk_label_new_with_mnemonic(tr("官能团缩写"));
        name_ = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(name_), 24);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), name_);
        gtk_box_pack_start(GTK_BOX(form), label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(form), name_, TRUE, TRUE, 0);
        gtk_box_pack_start(content, form, FALSE, FALSE, 4);
        auto* hint = gtk_label_new(tr("框选完整官能团，并以一个末端 R 标记连接点。"));
        gtk_label_set_xalign(GTK_LABEL(hint), 0);
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_box_pack_start(content, hint, FALSE, FALSE, 2);
        status_ = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(status_), 0);
        gtk_label_set_line_wrap(GTK_LABEL(status_), TRUE);
        gtk_box_pack_start(content, status_, FALSE, FALSE, 4);
        g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list_)), "changed",
            G_CALLBACK(+[](GtkTreeSelection*, Manager* manager) { manager->showSelection(); }), this);
        g_signal_connect(preview_, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, Manager* manager)->gboolean {
            manager->draw(cr);
            return FALSE;
        }), this);
        g_signal_connect(name_, "activate", G_CALLBACK(+[](GtkEntry*, GtkDialog* dialog) {
            auto* button = gtk_dialog_get_widget_for_response(dialog, SaveGroup);
            if (button && gtk_widget_get_sensitive(button))
                gtk_dialog_response(dialog, SaveGroup);
        }), dialog_);
        customgroups::load();
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), SaveGroup, customgroups::readError().empty());
        gtk_widget_set_sensitive(name_, customgroups::readError().empty());
        refresh();
    }

    ~Manager() {
        g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(list_)), this);
        gtk_widget_destroy(dialog_);
        g_object_unref(rows_);
    }
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    std::string run() {
        gtk_widget_show_all(dialog_);
        while (true) {
            const auto response = gtk_dialog_run(GTK_DIALOG(dialog_));
            const auto selection = selected();
            try {
                if (response == SaveGroup) {
                    const std::string name = gtk_entry_get_text(GTK_ENTRY(name_));
                    customgroups::saveSelection(session_, name);
                    gtk_entry_set_text(GTK_ENTRY(name_), "");
                    refresh(name);
                    message("已保存官能团，可输入缩写使用");
                } else if (response == RemoveGroup) {
                    if (selection.empty() || !confirmRemoval(selection)) continue;
                    customgroups::remove(selection);
                    ++chemcompletion::state().revision;
                    refresh();
                    message("已从官能团库移除");
                } else if (response == InsertGroup) {
                    if (structure_ && !selection.empty()) return selection;
                } else return {};
            } catch (const std::exception& error) {
                message(error.what());
            }
        }
    }
};
}
