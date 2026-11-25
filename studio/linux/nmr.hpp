#pragma once
#include "i18n.hpp"

#include <gtk/gtk.h>
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace linux_nmr {
using linux_i18n::tr;
struct Peak {
    double ppm, area;
    std::string atoms, multiplicity, uncertainty;
};

struct Spectrum {
    std::string nucleus, formula, engine, solvent, warnings;
    std::vector<Peak> peaks;

    bool read(const std::string& result) {
        std::istringstream input(result);
        std::string line;
        if (!std::getline(input, line)) return false;
        if (line == "NMR1H\t1") nucleus = "¹H";
        else if (line == "NMR13C\t1") nucleus = "¹³C";
        else if (line == "NMR19F\t1") nucleus = "¹⁹F";
        else return false;
        while (std::getline(input, line)) {
            std::istringstream row(line);
            std::vector<std::string> cells;
            std::string value;
            while (std::getline(row, value, '\t')) cells.push_back(value);
            if (cells.size() < 2) continue;
            if (cells[0] == "formula") formula = cells[1];
            else if (cells[0] == "engine") engine = cells[1];
            else if (cells[0] == "solvent") solvent = cells[1];
            else if (cells[0] == "warning") warnings += cells[1] + "\n";
            else if (cells[0] == "peak" && cells.size() >= 6) {
                gchar *shiftEnd = nullptr, *areaEnd = nullptr;
                const double ppm = g_ascii_strtod(cells[1].c_str(), &shiftEnd);
                const double area = g_ascii_strtod(cells[2].c_str(), &areaEnd);
                if (shiftEnd == cells[1].c_str() || *shiftEnd || areaEnd == cells[2].c_str() || *areaEnd ||
                    !std::isfinite(ppm) || !std::isfinite(area) || area <= 0) return false;
                peaks.push_back({ppm, area, cells[3], cells[4], cells[5]});
            }
        }
        return !peaks.empty();
    }

    gboolean draw(GtkWidget* widget, cairo_t* cr) const {
        const double left = 36, right = gtk_widget_get_allocated_width(widget) - 20;
        const double top = 16, bottom = gtk_widget_get_allocated_height(widget) - 30;
        if (right <= left || bottom <= top) return FALSE;
        auto limits = std::minmax_element(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b){ return a.ppm < b.ppm; });
        const double padding = std::max(0.2, (limits.second->ppm - limits.first->ppm) * .09);
        const double low = limits.first->ppm - padding, high = limits.second->ppm + padding;
        double maxArea = 0;
        for (const Peak& peak : peaks) maxArea = std::max(maxArea, peak.area);
        const auto point = [&](double ppm){ return left + (high - ppm) * (right - left) / (high - low); };
        cairo_set_source_rgb(cr, .34, .39, .44);
        cairo_set_line_width(cr, 1);
        cairo_set_font_size(cr, 11);
        cairo_move_to(cr, left, bottom);
        cairo_line_to(cr, right, bottom);
        for (int i = 0; i <= 5; ++i) {
            const double ppm = high - i * (high - low) / 5;
            const double x = point(ppm);
            cairo_move_to(cr, x, bottom);
            cairo_line_to(cr, x, bottom + 4);
            char label[32];
            g_ascii_formatd(label, sizeof label, "%.2f", ppm);
            cairo_move_to(cr, x - 14, bottom + 18);
            cairo_show_text(cr, label);
        }
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, .06, .38, .46);
        cairo_set_line_width(cr, 2);
        for (const Peak& peak : peaks) {
            const double x = point(peak.ppm);
            cairo_move_to(cr, x, bottom);
            cairo_line_to(cr, x, bottom - peak.area * (bottom - top) / maxArea);
        }
        cairo_stroke(cr);
        return TRUE;
    }
};

inline void populate(GtkBox* panel, Spectrum& spectrum) {
    const auto summary = spectrum.nucleus + "   " + spectrum.formula + "   " + spectrum.solvent + "   " + spectrum.engine;
    gtk_box_pack_start(panel, gtk_label_new(summary.c_str()), FALSE, FALSE, 6);
    auto* chart = gtk_drawing_area_new();
    gtk_widget_set_size_request(chart, 650, 180);
    g_signal_connect(chart, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, Spectrum* plot){
        return plot->draw(widget, cr);
    }), &spectrum);
    gtk_box_pack_start(panel, chart, FALSE, FALSE, 0);
    gtk_box_pack_start(panel, gtk_label_new(tr("化学位移（ppm）")), FALSE, FALSE, 0);
    auto* store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    for (const Peak& peak : spectrum.peaks) {
        char shift[32];
        g_ascii_formatd(shift, sizeof shift, "%.4f", peak.ppm);
        GtkTreeIter row;
        gtk_list_store_append(store, &row);
        gtk_list_store_set(store, &row, 0, shift, 1, peak.area, 2, peak.atoms.c_str(),
                           3, peak.multiplicity.c_str(), 4, peak.uncertainty.c_str(), -1);
    }
    auto* table = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    const char* columns[] = {"δ / ppm", tr("积分"), tr("原子编号"), tr("裂分"), tr("模型不确定性")};
    for (int i = 0; i < 5; ++i)
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(table), i, columns[i],
                                                    gtk_cell_renderer_text_new(), "text", i, nullptr);
    auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scroll, 650, 160);
    gtk_container_add(GTK_CONTAINER(scroll), table);
    gtk_box_pack_start(panel, scroll, TRUE, TRUE, 6);
    auto* note = gtk_label_new(spectrum.warnings.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(note), 70);
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_box_pack_start(panel, note, FALSE, FALSE, 6);
}

inline void copyPeaks(const Spectrum& spectrum) {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << "δ / ppm\t" << tr("积分") << "\t" << tr("原子编号") << "\t"
             << tr("裂分") << "\t" << tr("模型不确定性") << "\n";
        for (const Peak& peak : spectrum.peaks)
            text << peak.ppm << '\t' << peak.area << '\t' << peak.atoms << '\t'
                 << peak.multiplicity << '\t' << peak.uncertainty << '\n';
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.str().c_str(), -1);
}

inline void show(GtkWindow* owner, Spectrum& spectrum) {
    auto* dialog = gtk_dialog_new_with_buttons(tr("预测核磁"), owner, GTK_DIALOG_MODAL,
                                               tr("复制峰表"), 1, tr("关闭"), GTK_RESPONSE_CLOSE, nullptr);
    populate(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), spectrum);
    gtk_widget_show_all(dialog);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == 1) copyPeaks(spectrum);
    gtk_widget_destroy(dialog);
}

inline GtkWidget* solventChooser() {
    auto* solvents = gtk_combo_box_text_new();
    const char* keys[] = {"cdcl3","dmso-d6","cd3od","acetone-d6","cd2cl2","c6d6","cd3cn","d2o","thf-d8","toluene-d8","dmf-d7"};
    const char* names[] = {"CDCl₃","DMSO-d₆","CD₃OD","Acetone-d₆","CD₂Cl₂","C₆D₆","CD₃CN","D₂O","THF-d₈","Toluene-d₈","DMF-d₇"};
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(solvents), keys[i], names[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(solvents), 0);
    return solvents;
}

inline std::string options(GtkWindow* owner) {
    auto* dialog = gtk_dialog_new_with_buttons(tr("核磁预测条件"), owner, GTK_DIALOG_MODAL,
                                               tr("取消"), GTK_RESPONSE_CANCEL, tr("预测"), GTK_RESPONSE_OK, nullptr);
    auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    auto* nuclei = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nuclei), "nmr1h", "¹H");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nuclei), "nmr13c", "¹³C");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nuclei), "nmr19f", "¹⁹F");
    auto* solvents = solventChooser();
    gtk_combo_box_set_active(GTK_COMBO_BOX(nuclei), 0);
    gtk_box_pack_start(content, gtk_label_new(tr("核种")), FALSE, FALSE, 3);
    gtk_box_pack_start(content, nuclei, FALSE, FALSE, 3);
    gtk_box_pack_start(content, gtk_label_new(tr("溶剂")), FALSE, FALSE, 3);
    gtk_box_pack_start(content, solvents, FALSE, FALSE, 3);
    gtk_widget_show_all(dialog);
    std::string action;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        action = std::string(gtk_combo_box_get_active_id(GTK_COMBO_BOX(nuclei))) + ";solvent=" +
                 gtk_combo_box_get_active_id(GTK_COMBO_BOX(solvents));
    gtk_widget_destroy(dialog);
    return action;
}

}
