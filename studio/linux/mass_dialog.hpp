#pragma once

#include "i18n.hpp"
#include "../mass_calculator.hpp"
#include <cstring>

namespace linux_mass {
using linux_i18n::tr;

inline std::string number(double value, int precision = 4) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

inline GtkWidget* metric(GtkGrid* grid, int row, const char* title, const std::string& value,
                         const std::string& unit, bool copyable) {
    auto* label = gtk_label_new(tr(title));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(grid, label, 0, row, 1, 1);
    auto* field = gtk_label_new((value + unit).c_str());
    gtk_label_set_xalign(GTK_LABEL(field), 0);
    gtk_label_set_selectable(GTK_LABEL(field), TRUE);
    gtk_widget_set_hexpand(field, TRUE);
    gtk_grid_attach(grid, field, 1, row, 1, 1);
    if (copyable) {
        auto* button = gtk_button_new_with_label(tr("复制"));
        atk_object_set_name(gtk_widget_get_accessible(button), tr(title));
        gtk_widget_set_tooltip_text(button, tr(title));
        gtk_grid_attach(grid, button, 2, row, 1, 1);
        return button;
    }
    return nullptr;
}

inline void copyText(const std::string& text) {
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
}

struct Adducts {
    studio::Formula formula;
    GtkComboBox* polarity = nullptr;
    GtkToggleButton* showAll = nullptr;
    GtkListStore* rows = nullptr;
    GtkTreeSelection* selection = nullptr;
    GtkWidget* result = nullptr;
    GtkWidget* copy = nullptr;
    bool expanded[2] = {};
    bool switching = false;

    int mode() const { return gtk_combo_box_get_active(polarity) == 1 ? 1 : 0; }

    std::string selected() const {
        GtkTreeModel* model = nullptr;
        GtkTreeIter row;
        if (!formula.exactValid || !gtk_tree_selection_get_selected(selection, &model, &row)) return {};
        int index = -1;
        gtk_tree_model_get(model, &row, 4, &index, -1);
        return number(studio::mass::mz(formula, studio::mass::adducts()[index]));
    }

    void update() const {
        const auto value = selected();
        gtk_label_set_text(GTK_LABEL(result), value.empty() ? "—" : value.c_str());
        gtk_widget_set_sensitive(copy, !value.empty());
    }

    void populate() {
        const int mode = this->mode();
        const auto& adducts = studio::mass::adducts();
        gtk_list_store_clear(rows);
        bool hasPreferred = false;
        GtkTreeIter preferred;
        for (size_t i = 0; i < adducts.size(); ++i) {
            const auto& adduct = adducts[i];
            if ((adduct.charge < 0) != (mode == 1) || (!adduct.common && !expanded[mode])) continue;
            const auto charge = std::string(adduct.charge < 0 ? "−" : "+") +
                (std::abs(adduct.charge) > 1 ? std::to_string(std::abs(adduct.charge)) : "");
            const auto mz = formula.exactValid ? number(studio::mass::mz(formula, adduct)) : "—";
            GtkTreeIter item;
            gtk_list_store_append(rows, &item);
            gtk_list_store_set(rows, &item, 0, adduct.name, 1, charge.c_str(), 2, mz.c_str(),
                3, tr(adduct.charge < 0 ? "负离子" : "正离子"), 4, int(i), -1);
            if (!hasPreferred || std::strcmp(adduct.name, "[M+H]+") == 0) {
                preferred = item;
                hasPreferred = true;
            }
        }
        if (hasPreferred) gtk_tree_selection_select_iter(selection, &preferred);
        update();
    }
};

inline GtkWidget* panel(const studio::Formula& formula) {
    auto* content = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    if (!formula.valid) {
        auto* message = gtk_label_new(tr("无法计算分子式"));
        gtk_box_pack_start(content, message, FALSE, FALSE, 0);
        if (!formula.note.empty()) {
            auto* details = gtk_label_new(formula.note.c_str());
            gtk_label_set_line_wrap(GTK_LABEL(details), TRUE);
            gtk_box_pack_start(content, details, FALSE, FALSE, 0);
        }
        return GTK_WIDGET(content);
    }

    auto* summary = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(summary, 4);
    gtk_grid_set_column_spacing(summary, 14);
    metric(summary, 0, "分子式", formula.plain, "", false);
    auto* average = metric(summary, 1, "平均分子量", number(formula.averageMass), " g/mol", true);
    auto* exact = metric(summary, 2, "单同位素质量", formula.exactValid ? number(formula.exactMass) : "—",
        formula.exactValid ? " u" : "", true);
    metric(summary, 3, "DBE", number(formula.dbe, 1), "", false);
    gtk_box_pack_start(content, GTK_WIDGET(summary), FALSE, FALSE, 0);
    if (!formula.note.empty()) {
        auto* note = gtk_label_new(formula.note.c_str());
        gtk_label_set_xalign(GTK_LABEL(note), 0);
        gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
        gtk_box_pack_start(content, note, FALSE, FALSE, 0);
    }

    auto& state = *new Adducts{formula};
    g_object_set_data_full(G_OBJECT(content), "adduct-state", &state, +[](gpointer data) {
        delete static_cast<Adducts*>(data);
    });
    auto* controls = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    auto* selector = gtk_combo_box_text_new();
    state.polarity = GTK_COMBO_BOX(selector);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(selector), tr("正离子"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(selector), tr("负离子"));
    gtk_combo_box_set_active(state.polarity, 0);
    gtk_box_pack_start(controls, selector, FALSE, FALSE, 0);
    auto* all = gtk_check_button_new_with_label(tr("显示全部"));
    state.showAll = GTK_TOGGLE_BUTTON(all);
    gtk_box_pack_start(controls, all, FALSE, FALSE, 0);
    gtk_box_pack_start(content, GTK_WIDGET(controls), FALSE, FALSE, 0);

    state.rows = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    auto* view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(state.rows)));
    g_object_unref(state.rows);
    const char* columns[] = {"加合物", "电荷", "精确 m/z", "模式"};
    for (int i = 0; i < 4; ++i) {
        auto* column = gtk_tree_view_column_new_with_attributes(tr(columns[i]), gtk_cell_renderer_text_new(),
            "text", i, nullptr);
        gtk_tree_view_column_set_expand(column, i == 0 || i == 2);
        gtk_tree_view_append_column(view, column);
    }
    state.selection = gtk_tree_view_get_selection(view);
    auto* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroller), GTK_WIDGET(view));
    gtk_box_pack_start(content, scroller, TRUE, TRUE, 0);

    auto* footer = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10));
    auto* text = gtk_label_new(tr("所选加合物精确 m/z"));
    state.result = gtk_label_new("—");
    gtk_label_set_selectable(GTK_LABEL(state.result), TRUE);
    state.copy = gtk_button_new_with_label(tr("复制"));
    atk_object_set_name(gtk_widget_get_accessible(state.copy), tr("所选加合物精确 m/z"));
    for (auto* widget : {text, state.result, state.copy}) gtk_box_pack_start(footer, widget, FALSE, FALSE, 0);
    gtk_box_pack_start(content, GTK_WIDGET(footer), FALSE, FALSE, 0);
    gtk_widget_set_sensitive(exact, formula.exactValid);
    g_signal_connect(average, "clicked", G_CALLBACK(+[](GtkButton*, Adducts* state) {
        copyText(number(state->formula.averageMass));
    }), &state);
    g_signal_connect(exact, "clicked", G_CALLBACK(+[](GtkButton*, Adducts* state) {
        copyText(number(state->formula.exactMass));
    }), &state);
    g_signal_connect(state.copy, "clicked", G_CALLBACK(+[](GtkButton*, Adducts* state) {
        const auto value = state->selected();
        if (!value.empty()) copyText(value);
    }), &state);
    g_signal_connect(state.selection, "changed", G_CALLBACK(+[](GtkTreeSelection*, Adducts* state) {
        state->update();
    }), &state);
    g_signal_connect(selector, "changed", G_CALLBACK(+[](GtkComboBox*, Adducts* state) {
        state->switching = true;
        gtk_toggle_button_set_active(state->showAll, state->expanded[state->mode()]);
        state->switching = false;
        state->populate();
    }), &state);
    g_signal_connect(all, "toggled", G_CALLBACK(+[](GtkToggleButton* button, Adducts* state) {
        if (state->switching) return;
        state->expanded[state->mode()] = gtk_toggle_button_get_active(button);
        state->populate();
    }), &state);
    g_signal_connect(content, "destroy", G_CALLBACK(+[](GtkWidget*, Adducts* state) {
        g_signal_handlers_disconnect_by_data(state->selection, state);
    }), &state);
    state.populate();
    return GTK_WIDGET(content);
}

inline void show(GtkWindow* owner, const studio::Formula& formula) {
    auto* dialog = gtk_dialog_new_with_buttons(tr("计算器"), owner, GTK_DIALOG_MODAL,
        tr("关闭"), GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 570, 420);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), panel(formula));
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
}
