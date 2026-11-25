#pragma once

#include "table.hpp"
#include <array>
#include <cmath>
#include <memory>
#include <set>

namespace linux_table {

inline bool canvasTool(const Session& session) {
    const auto& tool = session.entries[session.activeIndex];
    return tool.command == "chooseTableTool" ||
        (tool.owner == "T05" && tool.command.find("Eraser") == std::string::npos);
}

inline ObjectId insert(GtkWindow* owner, Session& session, bool threeLines, Error& error) {
    const auto* title = threeLines ? "插入三线表" : "插入表格";
    auto* dialog = gtk_dialog_new_with_buttons(tr(title), owner, GTK_DIALOG_MODAL,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("插入"), GTK_RESPONSE_ACCEPT, nullptr);
    gtk_widget_set_name(dialog,"table-editor");
    gtk_window_set_default_size(GTK_WINDOW(dialog),340,-1);
    auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    gtk_container_set_border_width(GTK_CONTAINER(content),14);
    auto* dimensions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,14);
    gtk_box_set_homogeneous(GTK_BOX(dimensions),TRUE);
    gtk_box_pack_start(content,dimensions,FALSE,FALSE,0);
    auto spin = [&](const char* label, int min, int max) {
        auto* line = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        auto* field = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(min, max, 1));
        gtk_spin_button_set_value(field, 3);
        auto* caption = gtk_label_new_with_mnemonic(tr(label));
        gtk_label_set_mnemonic_widget(GTK_LABEL(caption), GTK_WIDGET(field));
        gtk_label_set_xalign(GTK_LABEL(caption),0);
        gtk_box_pack_start(GTK_BOX(line),caption,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(line),GTK_WIDGET(field),FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(dimensions),line,TRUE,TRUE,0);
        return field;
    };
    auto* rows = spin(threeLines ? "行数（2–100）" : "行数（1–100）", threeLines ? 2 : 1, 100);
    auto* columns = spin("列数（1–30）", 1, 30);
    gtk_widget_show_all(dialog);
    const bool confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    gtk_spin_button_update(rows);
    gtk_spin_button_update(columns);
    const auto rowCount = gtk_spin_button_get_value_as_int(rows);
    const auto colCount = gtk_spin_button_get_value_as_int(columns);
    gtk_widget_destroy(dialog);
    if (!confirmed) return kInvalidObjectId;

    TableGrid grid;
    grid.rowHeights.assign(rowCount, 24.);
    grid.columnWidths.assign(colCount, 64.);
    grid.borderWidth = .6;
    const auto center = session.view().center;
    const Vec2 halfSize(grid.totalWidth() * .5, grid.totalHeight() * .5);
    grid.frame = Box(center - halfSize, center + halfSize);
    auto id = std::make_shared<ObjectId>(kInvalidObjectId);
    error = session.submit(Command(title, kInvalidateStructure | kInvalidateGeometry,
        [grid, id, threeLines](CommandContext& ctx) {
            auto result = createTable(ctx.document, grid, id.get());
            if (!result.ok()) return CommandResult::fail(result.code, result.message);
            if (threeLines)
                ctx.document.extensions(ObjectRef(ObjectKind::Table, *id)).set("studio.table.style", "three");
            return CommandResult::success();
        }), title);
    return error.ok() ? *id : kInvalidObjectId;
}

class CanvasDrag {
    ObjectId id_ = kInvalidObjectId;
    DocumentId document_ = 0;
    uint64_t serial_ = 0;
    Vec2 press_, delta_;
    bool dragging_ = false;
    r62::render::Scene preview_;
    std::vector<size_t> slots_;
    std::vector<uint8_t> movingPrimitives_;
    uint64_t frame_ = 0;

    bool current(const Session& session) const {
        return pending() && document_ == session.doc.id() && serial_ == session.serial &&
            session.doc.tables().alive(id_);
    }

public:
    bool pending() const { return id_ != kInvalidObjectId; }
    const std::vector<uint8_t>& movingPrimitives() const { return movingPrimitives_; }

    const r62::render::Scene* preview(const Session& session) const {
        return dragging_ && current(session) ? &preview_ : nullptr;
    }

    void begin(Session& session, ObjectId id, Vec2 point) {
        cancel();
        session.cancel();
        session.setSelection({ObjectRef(ObjectKind::Table, id)});
        id_ = id;
        document_ = session.doc.id();
        serial_ = session.serial;
        press_ = point;
        session.status = "按住拖动整张表格 · 双击编辑单元格 · Esc 取消";
    }

    void cancel() {
        id_ = kInvalidObjectId;
        dragging_ = false;
        preview_.clear();
        slots_.clear();
        movingPrimitives_.clear();
        frame_ = 0;
    }

    void update(Session& session, Vec2 point, bool constrain) {
        if (!current(session)) {
            cancel();
            return;
        }
        auto delta = point - press_;
        if (!dragging_ && length(delta) < session.view().deviceRadiusToDocument(3)) return;
        if (constrain) {
            if (std::abs(delta.x) >= std::abs(delta.y)) delta.y = 0;
            else delta.x = 0;
        }
        if (!dragging_) {
            const auto& source = session.scene.primitives();
            const auto* table = session.doc.tables().alive(id_);
            std::set<ObjectId> texts(table->cells.begin(), table->cells.end());
            preview_ = session.gpuScene;
            for (size_t i = 0; i < source.size(); ++i) {
                const auto ref = source[i].source;
                if ((ref.kind == ObjectKind::Table && ref.id == id_) ||
                        (ref.kind == ObjectKind::Text && texts.count(ref.id))) slots_.push_back(i);
            }
            dragging_ = true;
            session.status = "移动整表 · 松开鼠标放置，Shift 限定方向，Esc 取消";
            if (preview_.size() >= source.size() && preview_.size() >= 200 && slots_.size() < preview_.size()/2) {
                movingPrimitives_.assign(preview_.size(), 0);
                for (auto index : slots_) movingPrimitives_[preview_.size()-source.size()+index] = 1;
            }
        } else if (delta == delta_) return;
        delta_ = delta;
        // The rendered scene has the page backdrop before its editable primitives.
        if (preview_.size() < session.scene.size()) return;
        const auto offset = preview_.size() - session.scene.size();
        ++frame_;
        for (size_t index : slots_) {
            auto& primitive = preview_.mutablePrimitives()[offset + index];
            primitive = session.gpuScene.primitives()[offset + index];
            for (auto& point : primitive.points) point = point + delta;
            for (auto& path : primitive.subpaths)
                for (auto& point : path.points) point = point + delta;
            primitive.bounds.min = primitive.bounds.min + delta;
            primitive.bounds.max = primitive.bounds.max + delta;
            primitive.version ^= frame_;
        }
        preview_.touch();
    }

    Error finish(Session& session, Vec2 point, bool constrain) {
        update(session, point, constrain);
        const auto id = id_;
        const auto delta = delta_;
        const bool moved = dragging_ && length(delta) > 1e-8;
        cancel();
        if (!moved) return {};
        auto result = session.move({ObjectRef(ObjectKind::Table, id)}, delta);
        if (result.ok()) session.status = "整张表格已移动 · Ctrl+Z 撤销";
        return result;
    }
};

enum class EdgeAction { AppendRow, RemoveRow, AppendColumn, RemoveColumn };

struct EdgeControl {
    Box frame;
    const char* label;
    bool enabled;
};

struct Controls {
    ObjectId table = kInvalidObjectId;
    std::array<EdgeControl, 4> buttons{};

    int at(Vec2 point) const {
        if (table == kInvalidObjectId) return -1;
        for (size_t i = 0; i < buttons.size(); ++i) {
            const auto& rect = buttons[i].frame;
            if (point.x >= rect.min.x && point.x <= rect.max.x &&
                    point.y >= rect.min.y && point.y <= rect.max.y) return int(i);
        }
        return -1;
    }
};

inline Controls controls(const Session& session) {
    if (!canvasTool(session) || session.selected.size() != 1 || session.selected.front().kind != ObjectKind::Table)
        return {};
    const auto id = session.selected.front().id;
    const auto* table = session.doc.tables().alive(id);
    if (!table || table->rows == 0 || table->columns == 0) return {};
    const auto view = session.view();
    const auto first = view.toDevice(table->frame.min);
    const auto last = view.toDevice(table->frame.max);
    if (last.x - first.x < 8 || last.y - first.y < 8) return {};
    constexpr double side = 18, gap = 3, inset = 2;
    const auto box = [side](double x, double y) {
        return Box(Vec2(x, y), Vec2(x + side, y + side));
    };
    return {id, {{{box(last.x - 2 * side - gap, last.y + inset), "在表格底部添加一行", table->rows < 100},
        {box(last.x - side, last.y + inset), "删除表格最后一行", table->rows > 1},
        {box(last.x + inset, last.y - 2 * side - gap), "在表格右侧添加一列", table->columns < 30},
        {box(last.x + inset, last.y - side), "删除表格最后一列", table->columns > 1}}}};
}

inline Error applyControl(Session& session, const Controls& layout, int index) {
    const auto& control = layout.buttons[index];
    if (!control.enabled) return {};
    const auto id = layout.table;
    const auto action = static_cast<EdgeAction>(index);
    return session.submit(Command(control.label, kInvalidateStructure | kInvalidateGeometry,
        [id, action](CommandContext& ctx) {
            TableData data;
            auto result = readTable(ctx.document, id, &data, nullptr);
            if (result.ok()) {
                const auto rows = data.grid.rows(), cols = data.grid.columns();
                switch (action) {
                case EdgeAction::AppendRow: result = insertRow(ctx.document, id, rows, data.grid.rowHeights.back()); break;
                case EdgeAction::RemoveRow: result = deleteRow(ctx.document, id, rows - 1); break;
                case EdgeAction::AppendColumn: result = insertColumn(ctx.document, id, cols, data.grid.columnWidths.back()); break;
                case EdgeAction::RemoveColumn: result = deleteColumn(ctx.document, id, cols - 1); break;
                }
            }
            return result.ok() ? CommandResult::success() : CommandResult::fail(result.code, result.message);
        }), control.label);
}

inline void drawControls(cairo_t* cr, const Controls& layout, int hovered) {
    if (layout.table == kInvalidObjectId) return;
    cairo_save(cr);
    for (size_t i = 0; i < layout.buttons.size(); ++i) {
        const auto& button = layout.buttons[i];
        const auto& bounds = button.frame;
        const auto center = (bounds.min + bounds.max) * .5;
        const bool focused = button.enabled && int(i) == hovered;
        cairo_rectangle(cr, bounds.min.x + .5, bounds.min.y + .5, bounds.width() - 1, bounds.height() - 1);
        if (focused) cairo_set_source_rgb(cr, .86, .95, .94);
        else cairo_set_source_rgba(cr, 1., 1., 1., button.enabled ? .96 : .78);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, .12, .34, .34, button.enabled ? .8 : .34);
        cairo_set_line_width(cr, 1.);
        cairo_stroke(cr);
        cairo_set_line_width(cr, 1.6);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, center.x - 4, center.y);
        cairo_line_to(cr, center.x + 4, center.y);
        if (i % 2 == 0) {
            cairo_move_to(cr, center.x, center.y - 4);
            cairo_line_to(cr, center.x, center.y + 4);
        }
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

}
