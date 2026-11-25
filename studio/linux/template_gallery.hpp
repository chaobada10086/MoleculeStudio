#pragma once

namespace linux_templates {

// The model owns the complete searchable catalogue. Only the visible page
// needs live drawing sessions, including when browsing custom structures.
class Gallery {
    static constexpr int columns = 4, capacity = 12;
    struct Card {
        Gallery* gallery = nullptr;
        GtkWidget *button = nullptr, *picture = nullptr, *caption = nullptr, *forms = nullptr;
        std::shared_ptr<Session> source;
        int index = -1;
    };
    std::array<Card, capacity> cards_;
    GtkDialog* dialog_;
    GtkTreeModel* model_;
    std::function<std::shared_ptr<Session>(GtkTreeIter&)> load_;
    GtkWidget *previous_, *next_, *counter_, *variants_;
    struct Family {
        std::vector<int> rows;
        std::vector<std::string> labels;
        int active = 0;
    };
    std::vector<Family> families_;
    int page_ = 0, selected_ = -1;
    bool updating_ = false;

    void select(int index) {
        if (selected_ != index || index < 0) {
            updating_ = true;
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(variants_));
            if (index >= 0) {
                const auto& family = families_[index];
                for (const auto& label : family.labels)
                    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(variants_), label.c_str());
                gtk_combo_box_set_active(GTK_COMBO_BOX(variants_), family.active);
            }
            gtk_widget_set_sensitive(variants_, index >= 0 && families_[index].rows.size() > 1);
            updating_ = false;
        }
        selected_ = index;
        updating_ = true;
        for (auto& card : cards_)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(card.button), card.index >= 0 && card.index == index);
        updating_ = false;
        gtk_dialog_set_response_sensitive(dialog_, GTK_RESPONSE_OK, index >= 0);
    }

    static gboolean draw(GtkWidget* area, cairo_t* cr, Card* card) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        if (!card->source) return TRUE;
        auto& src = *card->source;
        const auto bounds = src.scene.bounds();
        if (bounds.empty()) return TRUE;
        auto view = src.renderView;
        view.clipEnabled = false;
        view.transform.dpiScale = 1.;
        view.widthPx = gtk_widget_get_allocated_width(area);
        view.heightPx = gtk_widget_get_allocated_height(area);
        view.transform.viewportCenter = Vec2(view.widthPx * .5, view.heightPx * .5);
        view.transform.center = (bounds.min + bounds.max) * .5;
        view.transform.scale = std::min((view.widthPx - 12) / std::max(1., bounds.width()),
                                        (view.heightPx - 12) / std::max(1., bounds.height()));
        linux_canvas::paint(cr, src.scene, nullptr, view, src.measurer.get());
        return TRUE;
    }

public:
    Gallery(GtkDialog* dialog, GtkBox* parent, GtkTreeModel* model,
            std::function<std::shared_ptr<Session>(GtkTreeIter&)> load)
        : dialog_(dialog), model_(model), load_(std::move(load)) {
        auto* grid = gtk_grid_new();
        gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
        gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
        gtk_box_pack_start(parent, grid, TRUE, TRUE, 0);
        for (int i = 0; i < capacity; ++i) {
            auto& card = cards_[i];
            card.gallery = this;
            card.button = gtk_toggle_button_new();
            auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
            card.picture = gtk_drawing_area_new();
            gtk_widget_set_size_request(card.picture, 118, 72);
            card.caption = gtk_label_new("");
            gtk_label_set_ellipsize(GTK_LABEL(card.caption), PANGO_ELLIPSIZE_END);
            gtk_box_pack_start(GTK_BOX(box), card.picture, TRUE, TRUE, 0);
            gtk_box_pack_end(GTK_BOX(box), card.caption, FALSE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(card.button), box);
            auto* slot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            card.forms = gtk_combo_box_text_new();
            gtk_widget_set_no_show_all(card.forms,TRUE);
            atk_object_set_name(gtk_widget_get_accessible(card.forms),linux_i18n::tr("结构变体"));
            gtk_box_pack_end(GTK_BOX(slot),card.forms,FALSE,FALSE,0);
            g_signal_connect(card.forms,"changed",G_CALLBACK(+[](GtkComboBox* combo,Card* c){
                const int form=gtk_combo_box_get_active(combo);
                if (c->gallery->updating_ || form<0 || c->index<0) return;
                c->gallery->select(c->index);
                gtk_combo_box_set_active(GTK_COMBO_BOX(c->gallery->variants_),form);
            }),&card);
            gtk_box_pack_start(GTK_BOX(slot), card.button, TRUE, TRUE, 0);
            gtk_grid_attach(GTK_GRID(grid), slot, i % columns, i / columns, 1, 1);
            g_signal_connect(card.picture, "draw", G_CALLBACK(draw), &card);
            g_signal_connect(card.button, "clicked", G_CALLBACK(+[](GtkButton*, Card* c) {
                if (!c->gallery->updating_) c->gallery->select(c->index);
            }), &card);
            g_signal_connect(card.button, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* e, Card* c)->gboolean {
                if (e->button != 1 || e->type != GDK_2BUTTON_PRESS) return FALSE;
                c->gallery->select(c->index);
                gtk_dialog_response(c->gallery->dialog_, GTK_RESPONSE_OK);
                return TRUE;
            }), &card);
        }
        gtk_widget_add_events(grid, GDK_SCROLL_MASK);
        g_signal_connect(grid, "scroll-event", G_CALLBACK(+[](GtkWidget*, GdkEventScroll* event, Gallery* gallery)->gboolean {
            if (event->direction != GDK_SCROLL_UP && event->direction != GDK_SCROLL_DOWN) return FALSE;
            gallery->showPage(gallery->page_ + (event->direction == GDK_SCROLL_DOWN ? 1 : -1));
            return TRUE;
        }), this);
        g_signal_connect(dialog_, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, Gallery* gallery)->gboolean {
            if (event->keyval == GDK_KEY_Page_Up || event->keyval == GDK_KEY_Page_Down) {
                gallery->showPage(gallery->page_ + (event->keyval == GDK_KEY_Page_Down ? 1 : -1));
                return TRUE;
            }
            if ((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) && gallery->selected_ >= 0) {
                auto* focus = gtk_window_get_focus(GTK_WINDOW(gallery->dialog_));
                if (GTK_IS_ENTRY(focus) || GTK_IS_TOGGLE_BUTTON(focus)) {
                    gtk_dialog_response(gallery->dialog_, GTK_RESPONSE_OK);
                    return TRUE;
                }
            }
            return FALSE;
        }), this);
        auto* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_end(parent, bar, FALSE, FALSE, 2);
        variants_ = gtk_combo_box_text_new();
        gtk_box_pack_start(GTK_BOX(bar), variants_, FALSE, FALSE, 0);
        atk_object_set_name(gtk_widget_get_accessible(variants_), linux_i18n::tr("结构变体"));
        g_signal_connect(variants_, "changed", G_CALLBACK(+[](GtkComboBox* combo, Gallery* gallery) {
            if (gallery->updating_ || gallery->selected_ < 0) return;
            const int variant = gtk_combo_box_get_active(combo);
            if (variant < 0) return;
            auto& family = gallery->families_[gallery->selected_];
            family.active = variant;
            auto& card = gallery->cards_[gallery->selected_ % capacity];
            gallery->updating_=true;
            gtk_combo_box_set_active(GTK_COMBO_BOX(card.forms),variant);
            gallery->updating_=false;
            GtkTreeIter iter;
            if (gallery->selected(iter)) card.source = gallery->load_(iter);
            gtk_widget_queue_draw(card.picture);
        }), this);
        previous_ = gtk_button_new_with_label(linux_i18n::tr("上一页"));
        next_ = gtk_button_new_with_label(linux_i18n::tr("下一页"));
        counter_ = gtk_label_new("");
        gtk_box_pack_end(GTK_BOX(bar), next_, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(bar), counter_, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(bar), previous_, FALSE, FALSE, 0);
        for (auto* button : {previous_, next_})
            g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* b, Gallery* gallery) {
                gallery->showPage(gallery->page_ + (GTK_WIDGET(b) == gallery->next_ ? 1 : -1));
            }), this);
    }

    void refilter() {
        families_.clear();
        std::map<int, size_t> positions;
        GtkTreeIter iter;
        int row = 0;
        for (bool valid = gtk_tree_model_get_iter_first(model_, &iter); valid;
             valid = gtk_tree_model_iter_next(model_, &iter), ++row) {
            int id;
            gchar* label = nullptr;
            gtk_tree_model_get(model_, &iter, 7, &id, 8, &label, -1);
            auto [pos, inserted] = positions.emplace(id, families_.size());
            if (inserted) families_.emplace_back();
            auto& family = families_[pos->second];
            family.rows.push_back(row);
            family.labels.emplace_back(label);
            g_free(label);
        }
        selected_ = -1;
        showPage(0);
    }

    void showPage(int page) {
        const int count = int(families_.size());
        const int pages = std::max(1, (count + capacity - 1) / capacity);
        page_ = std::clamp(page, 0, pages - 1);
        for (int i = 0; i < capacity; ++i) {
            auto& card = cards_[i];
            card.source.reset();
            GtkTreeIter iter;
            card.index = page_ * capacity + i;
            if (card.index >= count) {
                card.index = -1;
                gtk_widget_hide(card.button);
                gtk_widget_hide(card.forms);
                continue;
            }
            const auto& family = families_[card.index];
            updating_=true;
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(card.forms));
            for (const auto& name:family.labels) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(card.forms),name.c_str());
            gtk_combo_box_set_active(GTK_COMBO_BOX(card.forms),family.active);
            gtk_widget_set_visible(card.forms,family.rows.size()>1);
            updating_=false;
            gtk_tree_model_iter_nth_child(model_, &iter, nullptr, family.rows[family.active]);
            gchar *caption = nullptr, *details = nullptr;
            gtk_tree_model_get(model_, &iter, 0, &details, 6, &caption, -1);
            gtk_label_set_text(GTK_LABEL(card.caption), caption);
            atk_object_set_name(gtk_widget_get_accessible(card.button), caption);
            gtk_widget_set_tooltip_text(card.button, details);
            g_free(caption);
            g_free(details);
            card.source = load_(iter);
            gtk_widget_show_all(card.button);
            gtk_widget_queue_draw(card.picture);
        }
        gtk_label_set_text(GTK_LABEL(counter_), (std::to_string(page_ + 1) + " / " + std::to_string(pages)).c_str());
        gtk_widget_set_sensitive(previous_, page_ > 0);
        gtk_widget_set_sensitive(next_, page_ + 1 < pages);
        select(count ? page_ * capacity : -1);
    }

    bool selected(GtkTreeIter& iter) const {
        if (selected_ < 0) return false;
        const auto& family = families_[selected_];
        return gtk_tree_model_iter_nth_child(model_, &iter, nullptr, family.rows[family.active]);
    }
};
}
