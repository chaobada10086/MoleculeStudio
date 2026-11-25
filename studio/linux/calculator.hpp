#pragma once

#include "calculator_model.hpp"
#include "cairo_scene.hpp"
#include "mass_dialog.hpp"

namespace linux_calculator {
using linux_i18n::tr;

inline GtkWidget* thumbnail(const yieldcalc::Row& row, int width, int height) {
    auto* picture = gtk_drawing_area_new();
    gtk_widget_set_size_request(picture, width, height);
    g_signal_connect(picture, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, const yieldcalc::Row* row) -> gboolean {
        if (row->bounds.empty() || !row->preview) return FALSE;
        auto view = row->preview->renderView;
        view.clipEnabled = false;
        view.widthPx = gtk_widget_get_allocated_width(widget);
        view.heightPx = gtk_widget_get_allocated_height(widget);
        view.transform.center = row->bounds.min + (row->bounds.max - row->bounds.min) * .5;
        view.transform.viewportCenter = r62::core::Vec2(view.widthPx * .5, view.heightPx * .5);
        view.transform.dpiScale = 1;
        view.transform.scale = std::min({2.5, (view.widthPx - 12) / std::max(1., row->bounds.width()),
            (view.heightPx - 12) / std::max(1., row->bounds.height())});
        if (view.transform.scale > 0)
            linux_cairo::draw(cr, row->preview->scene, view, row->preview->measurer.get());
        return FALSE;
    }), const_cast<yieldcalc::Row*>(&row));
    return picture;
}

inline GtkComboBox* combo(std::initializer_list<const char*> names, int selected) {
    auto* result = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    for (const auto* name : names) gtk_combo_box_text_append_text(result, tr(name));
    gtk_combo_box_set_active(GTK_COMBO_BOX(result), selected);
    return GTK_COMBO_BOX(result);
}

inline std::string value(GtkEntry* entry) { return gtk_entry_get_text(entry); }
inline void put(GtkEntry* entry, double n, int precision = 7) {
    gtk_entry_set_text(entry, yieldcalc::formatNumber(n, precision).c_str());
}

struct YieldPage {
    struct Controls {
        YieldPage* page = nullptr;
        size_t index = 0;
        GtkEntry *mass = nullptr, *amount = nullptr, *coefficient = nullptr;
        GtkComboBox *massUnit = nullptr, *amountUnit = nullptr, *role = nullptr;
        GtkToggleButton* include = nullptr;
        GtkWidget *result = nullptr, *amountLabel = nullptr;
        bool massSource = true;
        int automatic = 1;
    };

    r62::integration::Session& session;
    std::vector<yieldcalc::Row>& rows;
    std::vector<Controls> cards;
    std::vector<GtkEntry*> fields;
    GtkEntry *start = nullptr, *startCoefficient = nullptr, *active = nullptr;
    GtkComboBox *startUnit = nullptr, *annotationUnit = nullptr;
    GtkWidget *add = nullptr, *status = nullptr;
    double previousStartFactor = .001;
    bool quiet = false;

    explicit YieldPage(r62::integration::Session& source, std::vector<yieldcalc::Row>& list)
        : session(source), rows(list), cards(rows.size()) {}

    GtkEntry* entry(const std::string& name) {
        auto* item = GTK_ENTRY(gtk_entry_new());
        gtk_entry_set_max_length(item, 80);
        gtk_entry_set_width_chars(item, 11);
        gtk_entry_set_activates_default(item, FALSE);
        atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(item)), name.c_str());
        fields.push_back(item);
        g_signal_connect(item, "focus-in-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, YieldPage* page) -> gboolean {
            page->active = GTK_ENTRY(widget);
            return FALSE;
        }), this);
        return item;
    }

    double startFactor() const {
        return yieldcalc::amountFactor(gtk_combo_box_get_active(startUnit) + 1);
    }

    void sync(Controls& card, bool both = false) {
        auto& row = rows[card.index];
        const auto old = quiet;
        quiet = true;
        if (row.valid && (card.massSource || both) && !row.amountDisplay)
            card.automatic = yieldcalc::amountUnit(row.mol);
        const int unit = row.amountDisplay ? row.amountDisplay : card.automatic;
        const char* units[] = {"", "mmol", "mol", "µmol"};
        const auto title = linux_i18n::message("物质的量") + " (" + units[unit] + ")";
        gtk_label_set_text(GTK_LABEL(card.amountLabel), title.c_str());
        if (row.valid) {
            if (both || !card.massSource) put(card.mass, row.grams / yieldcalc::massFactor(row.massUnit));
            if (both || card.massSource) put(card.amount, row.mol / yieldcalc::amountFactor(unit));
        } else {
            gtk_entry_set_text(card.massSource ? card.amount : card.mass, "");
        }
        quiet = old;
    }

    void recalculate(Controls& card, bool useMass) {
        if (quiet) return;
        auto& row = rows[card.index];
        card.massSource = useMass;
        const auto input = value(useMass ? card.mass : card.amount);
        row.valid = false;
        row.problem.clear();
        double n = 0;
        if (!input.empty()) {
            if (!row.formula.valid) row.problem = "分子量不可用";
            else if (!yieldcalc::number(input, n)) row.problem = "请输入非负数值";
            else {
                const double factor = useMass ? yieldcalc::massFactor(row.massUnit) :
                    yieldcalc::amountFactor(row.amountDisplay ? row.amountDisplay : card.automatic);
                row.valid = yieldcalc::convert(n * factor, row.formula.averageMass, useMass, row.grams, row.mol);
                if (!row.valid) row.problem = "数值超出范围";
            }
        }
        sync(card);
        refresh();
    }

    void refresh() {
        if (quiet) return;
        double initial = 0, coefficient = 1;
        const bool haveInitial = yieldcalc::number(value(start), initial, true) &&
            yieldcalc::number(value(startCoefficient), coefficient, true);
        initial *= startFactor();
        bool ready = false;
        for (auto& card : cards) {
            auto& row = rows[card.index];
            row.role = gtk_combo_box_get_active(card.role);
            row.include = gtk_toggle_button_get_active(card.include);
            const bool validCoefficient = yieldcalc::number(value(card.coefficient), row.coefficient, true);
            if (!validCoefficient) row.coefficient = 0;
            row.initialMol = haveInitial ? initial : 0;
            row.initialCoefficient = coefficient;
            row.yieldValid = row.valid && row.role == 2 && validCoefficient && haveInitial &&
                yieldcalc::percent(row.mol, initial, row.coefficient, coefficient, row.yield);
            std::string message;
            if (!row.problem.empty()) message = linux_i18n::message(row.problem);
            else if (!validCoefficient) message = tr("计量系数须大于 0");
            else if (row.yieldValid) message = "Yield = " + yieldcalc::formatNumber(row.yield, 5) + " %";
            else if (row.valid) message = tr(row.role == 2 ? "填写起始物用量后显示产率" : "质量与物质的量已换算");
            gtk_label_set_text(GTK_LABEL(card.result), message.c_str());
            ready |= row.include && row.valid && validCoefficient;
        }
        gtk_widget_set_sensitive(add, ready);
        gtk_label_set_text(GTK_LABEL(status), tr(haveInitial ? "产率按填写的起始物用量及计量系数计算" :
            "填写起始物用量后显示产率"));
    }

    void key(const std::string& key) {
        if (!active) {
            gtk_widget_grab_focus(GTK_WIDGET(start));
            active = start;
        }
        auto* field = GTK_EDITABLE(active);
        if (key == "下一项") {
            const auto found = std::find(fields.begin(), fields.end(), active);
            const size_t next = (size_t(found - fields.begin()) + 1) % fields.size();
            gtk_widget_grab_focus(GTK_WIDGET(fields[next]));
            gtk_editable_select_region(GTK_EDITABLE(fields[next]), 0, -1);
            return;
        }
        if (key == "清空") { gtk_entry_set_text(active, ""); return; }
        gint first, last;
        const bool selected = gtk_editable_get_selection_bounds(field, &first, &last);
        int caret = gtk_editable_get_position(field);
        if (key == "⌫") {
            if (selected) gtk_editable_delete_selection(field);
            else if (caret > 0) gtk_editable_delete_text(field, caret - 1, caret);
            return;
        }
        if (key == "±") {
            auto text = value(active);
            const auto exponent = text.find_last_of("eE");
            const size_t sign = exponent != std::string::npos && size_t(caret) > exponent ? exponent + 1 : 0;
            if (sign < text.size() && text[sign] == '-') text.erase(sign, 1);
            else if (sign < text.size() && text[sign] == '+') text[sign] = '-';
            else text.insert(sign, "-");
            gtk_entry_set_text(active, text.c_str());
            gtk_editable_set_position(field, -1);
            return;
        }
        if (selected) {
            gtk_editable_delete_selection(field);
            caret = std::min(first, last);
        }
        gtk_editable_insert_text(field, key.c_str(), key.size(), &caret);
        gtk_editable_set_position(field, caret);
    }

    GtkWidget* keypad() {
        auto* keys = GTK_GRID(gtk_grid_new());
        gtk_grid_set_row_spacing(keys, 4);
        gtk_grid_set_column_spacing(keys, 4);
        const char* definitions[] = {"7", "8", "9", "⌫", "4", "5", "6", "清空",
                                     "1", "2", "3", "±", "0", ".", "e", "下一项"};
        for (int i = 0; i < 16; ++i) {
            auto* button = gtk_button_new_with_label(tr(definitions[i]));
            gtk_widget_set_focus_on_click(button, FALSE);
            g_object_set_data(G_OBJECT(button), "calculator-key", const_cast<char*>(definitions[i]));
            gtk_widget_set_size_request(button, 51, 43);
            gtk_grid_attach(keys, button, i % 4, i / 4, 1, 1);
            g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* button, YieldPage* page) {
                page->key(static_cast<const char*>(g_object_get_data(G_OBJECT(button), "calculator-key")));
            }), this);
        }
        return GTK_WIDGET(keys);
    }

    GtkWidget* createCard(Controls& card, size_t index) {
        auto& row = rows[index];
        card.page = this;
        card.index = index;
        auto* group = GTK_GRID(gtk_grid_new());
        gtk_container_set_border_width(GTK_CONTAINER(group), 7);
        gtk_grid_set_row_spacing(group, 5);
        gtk_grid_set_column_spacing(group, 5);
        const std::string name = linux_i18n::message("化合物") + " " + std::to_string(index + 1);
        card.include = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label(name.c_str()));
        gtk_toggle_button_set_active(card.include, row.include);
        gtk_grid_attach(group, GTK_WIDGET(card.include), 0, 0, 2, 1);
        gtk_grid_attach(group, thumbnail(row, 168, 86), 0, 1, 2, 1);
        const auto formula = row.formula.plain.empty() ? tr("分子量不可用") : row.formula.plain;
        auto* summary = gtk_label_new((formula + (row.formula.valid ?
            " · " + yieldcalc::formatNumber(row.formula.averageMass) + " g/mol" : "")).c_str());
        gtk_grid_attach(group, summary, 0, 2, 2, 1);
        auto field = [&](GtkEntry*& field, GtkComboBox*& choice, const char* title, int y,
                         std::initializer_list<const char*> units, int mode) {
            auto* caption = gtk_label_new(tr(title));
            gtk_label_set_xalign(GTK_LABEL(caption), 0);
            gtk_grid_attach(group, caption, 0, y, 2, 1);
            field = entry(name + " " + tr(title));
            choice = combo(units, mode);
            const auto unitName = name + " " + tr(title) + " " + tr("单位");
            atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(choice)), unitName.c_str());
            gtk_grid_attach(group, GTK_WIDGET(field), 0, y + 1, 1, 1);
            gtk_grid_attach(group, GTK_WIDGET(choice), 1, y + 1, 1, 1);
            return caption;
        };
        field(card.mass, card.massUnit, "质量", 3, {"g", "mg", "µg", "kg"}, row.massUnit);
        card.amountLabel = field(card.amount, card.amountUnit, "物质的量", 5, {"自动", "mmol", "mol", "µmol"}, row.amountDisplay);
        card.role = combo({"反应物", "试剂", "产物"}, row.role);
        const auto roleName = name + " " + tr("角色");
        atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(card.role)), roleName.c_str());
        gtk_grid_attach(group, GTK_WIDGET(card.role), 0, 7, 2, 1);
        gtk_grid_attach(group, gtk_label_new(tr("计量系数")), 0, 8, 1, 1);
        card.coefficient = entry(name + " " + tr("计量系数"));
        put(card.coefficient, row.coefficient);
        gtk_grid_attach(group, GTK_WIDGET(card.coefficient), 1, 8, 1, 1);
        card.result = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(card.result), 0);
        gtk_label_set_line_wrap(GTK_LABEL(card.result), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(card.result), 26);
        gtk_grid_attach(group, card.result, 0, 9, 2, 1);
        sync(card, true);
        for (auto* input : {card.mass, card.amount})
            g_signal_connect(input, "changed", G_CALLBACK(+[](GtkEntry* input, Controls* card) {
                card->page->recalculate(*card, input == card->mass);
            }), &card);
        for (auto* unit : {card.massUnit, card.amountUnit})
            g_signal_connect(unit, "changed", G_CALLBACK(+[](GtkComboBox*, Controls* card) {
                auto& page = *card->page;
                auto& row = page.rows[card->index];
                row.massUnit = gtk_combo_box_get_active(card->massUnit);
                row.amountDisplay = gtk_combo_box_get_active(card->amountUnit);
                page.sync(*card, true);
                page.refresh();
            }), &card);
        for (auto* widget : {GTK_WIDGET(card.coefficient), GTK_WIDGET(card.role), GTK_WIDGET(card.include)})
            g_signal_connect_swapped(widget, widget == GTK_WIDGET(card.include) ? "toggled" : "changed",
                G_CALLBACK(+[](YieldPage* page) { page->refresh(); }), this);
        auto* frame = gtk_frame_new(nullptr);
        gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(group));
        return frame;
    }

    GtkWidget* build() {
        auto* box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
        gtk_container_set_border_width(GTK_CONTAINER(box), 10);
        auto* top = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7));
        start = entry(tr("起始物物质的量"));
        startUnit = combo({"mmol", "mol", "µmol"}, 0);
        atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(startUnit)), tr("起始物用量单位"));
        startCoefficient = entry(tr("起始物计量系数"));
        put(startCoefficient, 1);
        for (const auto& row : rows) if (row.initialMol > 0) {
            put(start, row.initialMol / startFactor());
            put(startCoefficient, row.initialCoefficient);
            break;
        }
        for (auto* item : {gtk_label_new(tr("起始物物质的量")), GTK_WIDGET(start), GTK_WIDGET(startUnit),
                           gtk_label_new(tr("计量系数")), GTK_WIDGET(startCoefficient)})
            gtk_box_pack_start(top, item, FALSE, FALSE, 0);
        gtk_box_pack_start(box, GTK_WIDGET(top), FALSE, FALSE, 0);
        auto* body = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
        auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        auto* compounds = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
        for (size_t index = 0; index < rows.size(); ++index)
            gtk_box_pack_start(compounds, createCard(cards[index], index), FALSE, FALSE, 0);
        if (rows.empty())
            gtk_box_pack_start(compounds, gtk_label_new(tr("请先在画布中选中一个分子，或框选整条反应式。")), TRUE, TRUE, 5);
        gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(compounds));
        gtk_box_pack_start(body, scroll, TRUE, TRUE, 0);
        auto* keys = keypad();
        gtk_widget_set_valign(keys, GTK_ALIGN_START);
        gtk_box_pack_start(body, keys, FALSE, FALSE, 0);
        gtk_box_pack_start(box, GTK_WIDGET(body), TRUE, TRUE, 0);
        status = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(status), 0);
        gtk_box_pack_start(box, status, FALSE, FALSE, 0);
        auto* actions = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7));
        annotationUnit = combo({"自动", "g", "mg", "µg"}, 0);
        atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(annotationUnit)), tr("标注质量单位"));
        add = gtk_button_new_with_label(tr("插入质量和产率"));
        for (auto* item : {gtk_label_new(tr("标注质量单位")), GTK_WIDGET(annotationUnit), add})
            gtk_box_pack_start(actions, item, FALSE, FALSE, 0);
        gtk_box_pack_start(box, GTK_WIDGET(actions), FALSE, FALSE, 0);
        for (auto* field : {start, startCoefficient})
            g_signal_connect_swapped(field, "changed", G_CALLBACK(+[](YieldPage* page) { page->refresh(); }), this);
        g_signal_connect(startUnit, "changed", G_CALLBACK(+[](GtkComboBox*, YieldPage* page) {
            double input;
            const double next = page->startFactor();
            if (yieldcalc::number(value(page->start), input)) {
                page->quiet = true;
                put(page->start, input * page->previousStartFactor / next, 14);
                page->quiet = false;
            }
            page->previousStartFactor = next;
            page->refresh();
        }), this);
        g_signal_connect(add, "clicked", G_CALLBACK(+[](GtkButton*, YieldPage* page) {
            page->refresh();
            const auto result = yieldcalc::insert(page->session, page->rows, gtk_combo_box_get_active(page->annotationUnit));
            gtk_label_set_text(GTK_LABEL(page->status), result.ok() ? tr("已插入结构下方 · 可继续修改后更新 · Ctrl+Z 可撤销") :
                linux_i18n::message(result.message).c_str());
        }), this);
        refresh();
        return GTK_WIDGET(box);
    }
};

struct MassPage {
    const std::vector<yieldcalc::Row>& rows;
    GtkComboBox* choice = nullptr;
    GtkBox* content = nullptr;
    GtkWidget* current = nullptr;

    void update() {
        if (current) gtk_widget_destroy(current);
        const int index = gtk_combo_box_get_active(choice);
        const auto* row = index >= 0 && size_t(index) < rows.size() ? &rows[index] : nullptr;
        auto* body = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
        gtk_box_pack_start(body, linux_mass::panel(row ? row->formula : studio::Formula()), TRUE, TRUE, 0);
        if (row) gtk_box_pack_start(body, thumbnail(*row, 186, 130), FALSE, FALSE, 4);
        current = GTK_WIDGET(body);
        gtk_box_pack_start(content, current, TRUE, TRUE, 0);
        gtk_widget_show_all(current);
    }

    GtkWidget* build() {
        content = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
        auto* items = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto label = std::to_string(i + 1) + " · " + rows[i].formula.plain;
            gtk_combo_box_text_append_text(items, label.c_str());
        }
        choice = GTK_COMBO_BOX(items);
        if (!rows.empty()) gtk_combo_box_set_active(choice, 0);
        gtk_box_pack_start(content, GTK_WIDGET(choice), FALSE, FALSE, 0);
        g_signal_connect_swapped(choice, "changed", G_CALLBACK(+[](MassPage* page) { page->update(); }), this);
        update();
        return GTK_WIDGET(content);
    }
};

inline void show(GtkWindow* owner, r62::integration::Session& session) {
    auto rows = yieldcalc::collect(session, session.selected);
    YieldPage yield(session, rows);
    MassPage mass{rows};
    auto* dialog = gtk_dialog_new_with_buttons(tr("计算器"), owner, GTK_DIALOG_MODAL, tr("关闭"), GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 880, 490);
    auto* tabs = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_append_page(tabs, yield.build(), gtk_label_new(tr("产率计算器")));
    gtk_notebook_append_page(tabs, mass.build(), gtk_label_new(tr("质谱计算器")));
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), GTK_WIDGET(tabs));
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(GTK_WIDGET(yield.start));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
}
