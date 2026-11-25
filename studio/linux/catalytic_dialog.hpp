#pragma once

#include "catalytic_actions.hpp"
#include "canvas.hpp"
#include "i18n.hpp"

namespace linux_catalytic {
using namespace r62;
using namespace r62::core;
using integration::Session;
using studio::catalytic::CycleLayout;
using linux_i18n::tr;

class Editor {
    GtkWidget* dialog_ = nullptr;
    GtkWidget* grid_ = nullptr;
    GtkWidget* count_ = nullptr;
    GtkWidget* horizontal_ = nullptr;
    GtkWidget* vertical_ = nullptr;
    GtkWidget* step_ = nullptr;
    GtkWidget* element_ = nullptr;
    GtkWidget* oxidation_ = nullptr;
    GtkWidget* ligands_ = nullptr;
    GtkWidget* numbers_ = nullptr;
    GtkWidget* formatting_ = nullptr;
    GtkWidget* excited_ = nullptr;
    GtkWidget* collapsed_ = nullptr;
    GtkWidget* issue_ = nullptr;
    GtkWidget* preview_ = nullptr;
    GtkWidget* metalPreview_ = nullptr;
    guint timer_ = 0;
    int row_ = 0;
    size_t active_ = 0;
    bool adjusting_ = false;
    bool cycle_ = false;
    Session sample_;
    std::vector<ObjectId> sampleNodes_;
    std::unique_ptr<Session> previewSource_;
    std::unique_ptr<Session> metalPreviewSource_;
    std::vector<ObjectId> previewNodes_;

    GtkWidget* row(const char* caption, GtkWidget* widget) {
        auto* label = gtk_label_new(tr(caption));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
        atk_object_set_name(gtk_widget_get_accessible(widget), tr(caption));
        gtk_grid_attach(GTK_GRID(grid_), label, 0, row_, 1, 1);
        gtk_grid_attach(GTK_GRID(grid_), widget, 1, row_++, 1, 1);
        gtk_widget_set_hexpand(widget, TRUE);
        return widget;
    }

    GtkWidget* combo(const char* caption, std::initializer_list<const char*> choices) {
        auto* widget = gtk_combo_box_text_new();
        for (const auto* choice : choices) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), tr(choice));
        return row(caption, widget);
    }

    GtkWidget* spin(const char* caption, double initial, double lower, double upper, int decimals) {
        auto* widget = gtk_spin_button_new_with_range(lower, upper, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), initial);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(widget), decimals);
        return row(caption, widget);
    }

    GtkWidget* check(const char* caption) {
        auto* widget = gtk_check_button_new_with_label(tr(caption));
        gtk_grid_attach(GTK_GRID(grid_), widget, 0, row_++, 2, 1);
        return widget;
    }

    void loadMetal() {
        const auto& metal = metals.at(active_);
        gtk_entry_set_text(GTK_ENTRY(element_), metal.element.c_str());
        gtk_entry_set_text(GTK_ENTRY(oxidation_), metal.oxidation.c_str());
        gtk_entry_set_text(GTK_ENTRY(ligands_), metal.ligands.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(numbers_), std::clamp(metal.numerals, 0, 1));
        gtk_combo_box_set_active(GTK_COMBO_BOX(formatting_), std::clamp(metal.format, 0, 3));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(excited_), metal.excited);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(collapsed_), metal.collapsed);
    }

    void readMetal() {
        auto& metal = metals.at(active_);
        metal.element = gtk_entry_get_text(GTK_ENTRY(element_));
        metal.oxidation = gtk_entry_get_text(GTK_ENTRY(oxidation_));
        metal.ligands = gtk_entry_get_text(GTK_ENTRY(ligands_));
        metal.numerals = gtk_combo_box_get_active(GTK_COMBO_BOX(numbers_));
        metal.format = gtk_combo_box_get_active(GTK_COMBO_BOX(formatting_));
        metal.excited = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(excited_));
        metal.collapsed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(collapsed_));
    }

    void repopulate() {
        if (!cycle_) return;
        adjusting_ = true;
        if (sampleNodes_.empty()) {
            const auto count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(count_));
            metals.resize(count);
            active_ = std::min(active_, metals.size() - 1);
        }
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(step_));
        for (size_t index = 0; index < metals.size(); ++index) {
            const auto label = std::string(tr("步骤")) + ' ' + std::to_string(index + 1);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(step_), label.c_str());
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(step_), static_cast<int>(active_));
        loadMetal();
        adjusting_ = false;
    }

    bool rebuild() {
        auto candidate = std::make_unique<Session>();
        candidate->doc = sample_.doc;
        auto nodes = sampleNodes_;
        Error error;
        if (cycle_) {
            geometry.horizontal = gtk_spin_button_get_value(GTK_SPIN_BUTTON(horizontal_));
            geometry.vertical = gtk_spin_button_get_value(GTK_SPIN_BUTTON(vertical_));
            auto previewLayout = geometry;
            previewLayout.origin = Vec2();
            previewLayout.outline = {};
            error = studio::catalytic::layout(candidate->doc, previewLayout, nodes, metals, edited, nullptr);
            if (error.ok()) {
                auto metal = std::make_unique<Session>();
                try {
                    metals.at(active_).apply(metal->doc, kInvalidObjectId, Vec2());
                    metal->refresh();
                    metalPreviewSource_ = std::move(metal);
                } catch (const std::exception& problem) {
                    error = {ErrorCode::InvalidArgument, problem.what()};
                }
            }
        } else {
            try {
                metals.front().apply(candidate->doc, kInvalidObjectId, Vec2());
            } catch (const std::exception& problem) {
                error = {ErrorCode::InvalidArgument, problem.what()};
            }
        }
        gtk_label_set_text(GTK_LABEL(issue_), error.ok() ? "" : tr(error.message.c_str()));
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), GTK_RESPONSE_ACCEPT, error.ok());
        if (!error.ok()) return false;
        candidate->refresh();
        previewSource_ = std::move(candidate);
        previewNodes_ = std::move(nodes);
        gtk_widget_queue_draw(preview_);
        if (metalPreview_) gtk_widget_queue_draw(metalPreview_);
        return true;
    }

    void updatePreview() {
        if (timer_) g_source_remove(timer_);
        timer_ = g_timeout_add(120, +[](gpointer data) -> gboolean {
            auto& self = *static_cast<Editor*>(data);
            self.timer_ = 0;
            self.rebuild();
            return G_SOURCE_REMOVE;
        }, this);
    }

    void selectStep(size_t index) {
        if (index >= metals.size()) return;
        adjusting_ = true;
        active_ = index;
        gtk_combo_box_set_active(GTK_COMBO_BOX(step_), static_cast<int>(index));
        loadMetal();
        adjusting_ = false;
        gtk_widget_queue_draw(preview_);
        updatePreview();
    }

    static void paintPreview(GtkWidget* widget, cairo_t* cr, Session* source) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        if (!source) return;
        source->renderView.widthPx = gtk_widget_get_allocated_width(widget);
        source->renderView.heightPx = gtk_widget_get_allocated_height(widget);
        auto view = source->view();
        view.viewportCenter = Vec2(source->renderView.widthPx * .5, source->renderView.heightPx * .5);
        source->setView(view);
        source->fit();
        linux_canvas::paint(cr, source->displayScene(), nullptr, source->renderView, source->measurer.get());
    }

public:
    CycleLayout geometry;
    std::vector<CatalyticMetal> metals;
    std::set<size_t> edited;

    Error copySelection(Session& source, const std::vector<ObjectId>& nodes) {
        sample_.doc.setDefaultFont(source.doc.defaultFont());
        sample_.doc.setDefaultBondLength(source.doc.defaultBondLength());
        for (auto id : nodes) {
            std::vector<ObjectRef> made;
            const auto refs = studio::catalytic::component(source.doc, id);
            const auto error = studioio::append(source.doc, sample_.doc, refs, Vec2(), &made);
            if (!error.ok()) return error;
            const auto found = std::find(refs.begin(), refs.end(), ObjectRef(ObjectKind::Atom, id));
            const auto index = static_cast<size_t>(found - refs.begin());
            if (found == refs.end() || index >= made.size() || made[index].kind != ObjectKind::Atom)
                return {ErrorCode::InvalidArgument, "无法预览循环中间体"};
            sampleNodes_.push_back(made[index].id);
            const auto element = source.doc.atoms().alive(id)->element;
            metals.push_back(studio::metal_labels::find(elementSymbol(element)).valid() ?
                CatalyticMetal::read(source.doc, id) : CatalyticMetal());
        }
        return {};
    }

    bool show(GtkWindow* parent, bool cycle) {
        cycle_ = cycle;
        const auto* title = cycle ? "催化循环与金属配体" : "金属中心与配体";
        dialog_ = gtk_dialog_new_with_buttons(tr(title), parent, GTK_DIALOG_MODAL, tr("取消"), GTK_RESPONSE_CANCEL,
            tr("确定"), GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog_), GTK_RESPONSE_ACCEPT);
        auto* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        gtk_container_set_border_width(GTK_CONTAINER(body), 12);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog_))), body, TRUE, TRUE, 0);
        grid_ = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid_), 8);
        gtk_grid_set_column_spacing(GTK_GRID(grid_), 12);
        gtk_box_pack_start(GTK_BOX(body), grid_, FALSE, FALSE, 0);
        if (cycle_) {
            count_ = spin("步骤数", metals.empty() ? 4 : metals.size(), 3, 12, 0);
            horizontal_ = spin("水平半径（pt）", geometry.horizontal, 1, 2000, 2);
            vertical_ = spin("垂直半径（pt）", geometry.vertical, 1, 2000, 2);
            gtk_widget_set_sensitive(count_, sampleNodes_.empty());
            gtk_widget_set_sensitive(horizontal_, !geometry.outline.valid());
            gtk_widget_set_sensitive(vertical_, !geometry.outline.valid());
            step_ = row("当前中间体", gtk_combo_box_text_new());
        }
        element_ = row("金属元素", gtk_entry_new());
        oxidation_ = row("氧化态", gtk_entry_new());
        ligands_ = row("末端配体（英文逗号）", gtk_entry_new());
        gtk_entry_set_max_length(GTK_ENTRY(element_), 3);
        gtk_entry_set_max_length(GTK_ENTRY(oxidation_), 4);
        gtk_entry_set_max_length(GTK_ENTRY(ligands_), 240);
        numbers_ = combo("价态数字格式", {"罗马数字 II / III", "阿拉伯数字 2 / 3"});
        formatting_ = combo("价态显示位置", {"括号上标", "无括号上标", "同行括号", "同行无括号"});
        excited_ = check("激发态 *（独立上标）");
        collapsed_ = check("收起配合物");
        issue_ = gtk_label_new("");
        gtk_label_set_line_wrap(GTK_LABEL(issue_), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(issue_), 36);
        gtk_widget_set_halign(issue_, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid_), issue_, 0, row_, 2, 1);
        preview_ = gtk_drawing_area_new();
        atk_object_set_name(gtk_widget_get_accessible(preview_), tr("结构预览"));
        gtk_widget_set_size_request(preview_, 355, cycle_ ? 235 : 265);
        gtk_widget_add_events(preview_, GDK_BUTTON_PRESS_MASK);
        auto* previews = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_box_pack_start(GTK_BOX(body), previews, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(previews), preview_, TRUE, TRUE, 0);
        if (cycle_) {
            metalPreview_ = gtk_drawing_area_new();
            atk_object_set_name(gtk_widget_get_accessible(metalPreview_), tr("配合物预览"));
            gtk_widget_set_size_request(metalPreview_, 355, 145);
            gtk_box_pack_start(GTK_BOX(previews), metalPreview_, TRUE, TRUE, 0);
            g_signal_connect(metalPreview_, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, Editor* self) -> gboolean {
                paintPreview(widget, cr, self->metalPreviewSource_.get());
                return FALSE;
            }), this);
        }
        if (cycle_) repopulate();
        else loadMetal();

        auto connect = [&](GtkWidget* control, const char* signal) {
            g_signal_connect(control, signal, G_CALLBACK(+[](GtkWidget*, Editor* self) {
                if (self->adjusting_) return;
                self->readMetal();
                self->edited.insert(self->active_);
                self->updatePreview();
            }), this);
        };
        for (auto* entry : {element_, oxidation_, ligands_, numbers_, formatting_}) connect(entry, "changed");
        for (auto* toggle : {excited_, collapsed_}) connect(toggle, "toggled");
        if (cycle_) {
            g_signal_connect(count_, "value-changed", G_CALLBACK(+[](GtkWidget*, Editor* self) {
                self->repopulate(); self->updatePreview();
            }), this);
            for (auto* spin : {horizontal_, vertical_})
                g_signal_connect(spin, "value-changed", G_CALLBACK(+[](GtkWidget*, Editor* self) { self->updatePreview(); }), this);
            g_signal_connect(step_, "changed", G_CALLBACK(+[](GtkComboBox* widget, Editor* self) {
                if (!self->adjusting_) self->selectStep(static_cast<size_t>(gtk_combo_box_get_active(widget)));
            }), this);
        }
        g_signal_connect(preview_, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, Editor* self) -> gboolean {
            paintPreview(widget, cr, self->previewSource_.get());
            if (!self->previewSource_) return FALSE;
            auto& source = *self->previewSource_;
            if (self->active_ < self->previewNodes_.size()) {
                const auto at = source.view().toDevice(studio::catalytic::center(source.doc, self->previewNodes_[self->active_]));
                cairo_set_source_rgb(cr, .05, .45, .80);
                cairo_arc(cr, at.x, at.y, 13, 0, 6.283185307179586);
                cairo_set_line_width(cr, 1.);
                cairo_stroke(cr);
            }
            return FALSE;
        }), this);
        g_signal_connect(preview_, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, Editor* self) -> gboolean {
            if (event->button != 1 || !self->previewSource_) return FALSE;
            const auto& source = *self->previewSource_;
            double best = 28.;
            size_t closest = self->previewNodes_.size();
            for (size_t index = 0; index < self->previewNodes_.size(); ++index) {
                auto position = source.view().toDevice(studio::catalytic::center(source.doc, self->previewNodes_[index]));
                const auto distance = length(position - Vec2(event->x, event->y));
                if (distance < best) { closest = index; best = distance; }
            }
            if (closest < self->previewNodes_.size()) self->selectStep(closest);
            return TRUE;
        }), this);
        rebuild();
        gtk_widget_show_all(dialog_);
        bool accepted = false;
        while (gtk_dialog_run(GTK_DIALOG(dialog_)) == GTK_RESPONSE_ACCEPT) {
            if (cycle_)
                for (auto* spin : {count_, horizontal_, vertical_}) gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
            if (rebuild()) { accepted = true; break; }
        }
        if (timer_) { g_source_remove(timer_); timer_ = 0; }
        gtk_widget_destroy(dialog_);
        return accepted;
    }
};

inline Error edit(GtkWindow* parent, Session& session, bool clockwise, ObjectRef outline = {}) {
    auto selection = studio::catalytic::selected(session);
    if (!selection.nodes.empty() && (selection.nodes.size() < 3 || selection.nodes.size() > 12))
        return {ErrorCode::InvalidArgument, "请选择 3–12 个独立中间体，或清空选区创建循环骨架"};
    Editor editor;
    editor.geometry.clockwise = clockwise;
    editor.geometry.origin = session.view().center;
    if (!selection.nodes.empty()) {
        Vec2 center;
        for (const auto id : selection.nodes) center = center + studio::catalytic::center(session.doc, id);
        editor.geometry.origin = center * (1. / selection.nodes.size());
        selection.nodes = studio::catalytic::order(session.doc, std::move(selection.nodes), editor.geometry.origin);
    }
    graphics::ShapeParams shape;
    if (studio::catalytic::ellipse(session.doc, outline, shape)) {
        editor.geometry.outline = outline;
        editor.geometry.fixedSize = true;
        editor.geometry.origin = shape.frame.centre;
        editor.geometry.horizontal = length(shape.frame.axisU);
        editor.geometry.vertical = length(shape.frame.axisV);
        editor.geometry.rotation = std::atan2(shape.frame.axisU.y, shape.frame.axisU.x);
    }
    auto result = editor.copySelection(session, selection.nodes);
    if (!result.ok() || !editor.show(parent, true)) return result;
    auto made = std::make_shared<std::vector<ObjectRef>>();
    result = session.submit(Command("催化循环排版", kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
        [=, nodes = selection.nodes, geometry = editor.geometry, metals = editor.metals, edited = editor.edited]
        (CommandContext& context) {
            auto actual = nodes;
            made->clear();
            const auto applied = studio::catalytic::layout(context.document, geometry, actual, metals, edited, made.get());
            return applied.ok() ? CommandResult::success() : CommandResult::fail(applied.code, applied.message);
        }), "催化循环排版");
    if (result.ok()) {
        session.activateCommand("chooseSelectionTool_Marquee");
        session.setSelection(*made);
        session.status = "已生成循环路径，可分别移动中间体或添加试剂与产物";
    }
    return result;
}

inline Error metal(GtkWindow* parent, Session& session) {
    const auto selection = studioio::expanded(session.doc, session.selected);
    auto selected = kInvalidObjectId;
    for (auto ref : selection) {
        const auto* atom = ref.kind == ObjectKind::Atom ? session.doc.atoms().alive(ref.id) : nullptr;
        if (!atom || !studio::metal_labels::find(elementSymbol(atom->element)).valid()) continue;
        if (selected != kInvalidObjectId) return {ErrorCode::InvalidArgument, "请只选择一个金属中心"};
        selected = ref.id;
    }
    if (!selection.empty() && selected == kInvalidObjectId)
        return {ErrorCode::InvalidArgument, "请选择一个金属原子，或清空选区插入新配合物"};
    Editor editor;
    editor.metals.push_back(CatalyticMetal::read(session.doc, selected));
    if (!editor.show(parent, false)) return {};
    const auto position = session.view().center;
    auto made = std::make_shared<ObjectId>(selected);
    const auto result = session.submit(Command("编辑金属配体", kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
        [selected, position, config = editor.metals.front(), made](CommandContext& context) {
            try {
                *made = config.apply(context.document, selected, position);
                return CommandResult::success();
            } catch (const std::exception& problem) {
                return CommandResult::fail(ErrorCode::InvalidArgument, problem.what());
            }
        }), "金属配体");
    if (result.ok()) {
        session.activateCommand("chooseSelectionTool_Marquee");
        session.setSelection({ObjectRef(ObjectKind::Atom, *made)});
        session.status = "已更新金属、配体与显示格式";
    }
    return result;
}
}
