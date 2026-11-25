#pragma once

#include "i18n.hpp"
#include "engine/table_layout.hpp"
#include "next/graphics/tables.hpp"
#include "next/integration/session.hpp"
#include "next/text/rich_text.hpp"
#include <gtk/gtk.h>
#include <algorithm>
#include <functional>
#include <numeric>

namespace linux_table {
using namespace r62::core;
using namespace r62::graphics;
using r62::integration::Session;
using linux_i18n::tr;

struct Cell {
    uint32_t row = 0;
    uint32_t column = 0;
};

struct Range {
    uint32_t top, left, bottom, right;

    bool includes(Cell cell) const {
        return cell.row >= top && cell.row <= bottom && cell.column >= left && cell.column <= right;
    }
};

inline Cell anchor(const TableGrid& grid, Cell position) {
    for (const auto& merged : grid.merged)
        if (position.row >= merged.row && position.row < merged.row + merged.rowSpan &&
                position.column >= merged.column && position.column < merged.column + merged.columnSpan)
            return {merged.row, merged.column};
    return position;
}

inline Range selection(const TableGrid& grid, Cell first, Cell last) {
    Range range{std::min(first.row, last.row), std::min(first.column, last.column),
        std::max(first.row, last.row), std::max(first.column, last.column)};
    bool expanded;
    do {
        expanded = false;
        for (const auto& cell : grid.merged) {
            const auto bottom = cell.row + cell.rowSpan - 1;
            const auto right = cell.column + cell.columnSpan - 1;
            if (range.top > bottom || range.bottom < cell.row || range.left > right || range.right < cell.column)
                continue;
            if (range.includes({cell.row, cell.column}) && range.includes({bottom, right})) continue;
            range = {std::min(range.top, cell.row), std::min(range.left, cell.column),
                std::max(range.bottom, bottom), std::max(range.right, right)};
            expanded = true;
        }
    } while (expanded);
    return range;
}

inline ObjectId at(const Document& document, Vec2 point, Cell& position) {
    const auto& tables = document.tables().records();
    for (auto item = tables.rbegin(); item != tables.rend(); ++item) {
        if (!item->isAlive()) continue;
        TableData data;
        if (readTable(document, item->id, &data, nullptr).ok() &&
                data.grid.cellAt(point, &position.row, &position.column)) {
            position = anchor(data.grid, position);
            return item->id;
        }
    }
    return kInvalidObjectId;
}

inline std::string copy(const Document& doc, const TableData& data, const Range& range) {
    std::string text;
    for (auto row = range.top; row <= range.bottom; ++row) {
        if (row != range.top) text += '\n';
        for (auto col = range.left; col <= range.right; ++col) {
            if (col != range.left) text += '\t';
            const auto* cell = data.at(row, col);
            if (cell->kind != CellKind::Text || coveredCell(data.grid, row, col)) continue;
            const auto value = doc.plainText(cell->text);
            const bool quote = value.find_first_of("\t\r\n\"") != std::string::npos;
            if (quote) text += '"';
            for (const auto ch : value) {
                if (ch == '"') text += '"';
                text += ch;
            }
            if (quote) text += '"';
        }
    }
    return text;
}

inline double textSize(const Document& doc, const TableCell* cell) {
    if (cell && cell->kind == CellKind::Text)
        if (const auto* text = doc.texts().alive(cell->text)) return text->size;
    return doc.defaultTextSize();
}

inline Error paste(Document& doc, ObjectId id, Cell start, const std::string& text) {
    if (text.size() > 100000) return {ErrorCode::InvalidArgument, "粘贴内容过长"};
    std::vector<std::vector<std::string>> rows(1, std::vector<std::string>(1));
    bool quoted = false;
    bool fieldStart = true;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto ch = text[i];
        auto& field = rows.back().back();
        if (quoted) {
            if (ch == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (ch == '"') quoted = false;
            else field += ch;
        } else if (fieldStart && ch == '"') {
            fieldStart = false;
            quoted = true;
        } else if (ch == '\t') {
            rows.back().emplace_back();
            fieldStart = true;
        } else if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            if (i + 1 < text.size()) rows.emplace_back(1);
            fieldStart = true;
        } else {
            field += ch;
            fieldStart = false;
        }
    }
    if (quoted) return {ErrorCode::InvalidArgument, "粘贴内容含未闭合的引号"};
    size_t columns = 0;
    for (const auto& row : rows) columns = std::max(columns, row.size());
    const auto requiredRows = start.row + rows.size();
    const auto requiredColumns = start.column + columns;
    if (requiredRows > 100 || requiredColumns > 30)
        return {ErrorCode::InvalidArgument, "粘贴范围最多 100 行、30 列"};
    TableData data;
    auto result = readTable(doc, id, &data, nullptr);
    if (!result.ok()) return result;
    for (const auto& merged : data.grid.merged)
        if (start.row < merged.row + merged.rowSpan && requiredRows > merged.row &&
                start.column < merged.column + merged.columnSpan && requiredColumns > merged.column)
            return {ErrorCode::InvalidArgument, "粘贴区域含合并单元格，请先拆分"};
    for (size_t row = 0; row < rows.size(); ++row) {
        for (size_t col = 0; col < rows[row].size(); ++col) {
            const auto* cell = data.at(start.row + row, start.column + col);
            if (cell && cell->kind == CellKind::Embedded)
                return {ErrorCode::InvalidArgument, "目标单元格含嵌入结构"};
        }
    }
    for (auto row = data.grid.rows(); row < requiredRows; ++row) {
        result = insertRow(doc, id, row, 24.);
        if (!result.ok()) return result;
    }
    for (auto col = data.grid.columns(); col < requiredColumns; ++col) {
        result = insertColumn(doc, id, col, 64.);
        if (!result.ok()) return result;
    }
    for (size_t row = 0; row < rows.size(); ++row) {
        for (size_t col = 0; col < rows[row].size(); ++col) {
            const auto r = start.row + row;
            const auto c = start.column + col;
            result = setCellText(doc, id, r, c, rows[row][col], textSize(doc, data.at(r, c)));
            if (!result.ok()) return result;
        }
    }
    return {};
}

enum Operation { RowAbove, RowBelow, DeleteRow, ColLeft, ColRight, DeleteCol, RowHeight,
                 ColWidth, Merge, Split, Grid, ThreeLines, Horizontal, Outer, NoBorder,
                 Clear, FontSize, Bold, AlignLeft, AlignCenter, AlignRight,
                 AlignTop, AlignMiddle, AlignBottom, EqualRows, EqualColumns, AutoFit, BorderWidth, DeleteTable };

inline bool values(GtkWindow* owner, const char* title, const char* first, double& x, double low, double high,
            const char* second = nullptr, double* y = nullptr, double maxY = 0) {
    auto* dialog = gtk_dialog_new_with_buttons(tr(title), owner, GTK_DIALOG_MODAL,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("应用"), GTK_RESPONSE_ACCEPT, nullptr);
    auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    auto add = [&](const char* label, double value, double min, double max) {
        auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        auto* spin = gtk_spin_button_new_with_range(min, max, second ? 1 : .5);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new(tr(label)), TRUE, TRUE, 6);
        gtk_box_pack_start(GTK_BOX(box), spin, TRUE, TRUE, 6);
        gtk_box_pack_start(content, box, TRUE, TRUE, 6);
        return GTK_SPIN_BUTTON(spin);
    };
    auto* a = add(first, x, low, high);
    auto* b = second ? add(second, *y, 1, maxY) : nullptr;
    gtk_widget_show_all(dialog);
    const bool ok = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    if (ok) {
        gtk_spin_button_update(a);
        x = gtk_spin_button_get_value(a);
        if (b) {
            gtk_spin_button_update(b);
            *y = gtk_spin_button_get_value(b);
        }
    }
    gtk_widget_destroy(dialog);
    return ok;
}

inline Error formatCells(Document& doc, TableData& data, const Range& range, Operation op, double size) {
    for (auto row = range.top; row <= range.bottom; ++row) {
        for (auto col = range.left; col <= range.right; ++col) {
            auto* cell = data.at(row, col);
            if (cell->kind != CellKind::Text || coveredCell(data.grid, row, col)) continue;
            const auto id = cell->text;
            const auto* text = doc.texts().alive(id);
            if (!text) continue;
            auto format = text->format;
            if (op >= AlignTop && op <= AlignBottom) {
                const auto value = op == AlignTop ? "top" : op == AlignBottom ? "bottom" : "middle";
                const ObjectRef ref(ObjectKind::Text, id);
                doc.noteTouchedExtensions(ref, false);
                doc.extensions(ref).set("studio.cell.valign", value);
                cell->textOffset.y = 0;
                continue;
            }
            if (op >= AlignLeft && op <= AlignRight) {
                format.align = uint8_t(op - AlignLeft);
                cell->textOffset.x = 0;
            } else {
                auto runs = doc.runs(id);
                if (op == FontSize) {
                    format.size = size;
                    for (auto& run : runs) run.size = size;
                    auto result = doc.setTextSize(id, size);
                    if (!result.ok()) return result;
                } else {
                    const bool bold = std::all_of(runs.begin(), runs.end(), [&](const TextRun& run) {
                        return ((run.flags | format.flags) & kBold) != 0;
                    });
                    if (bold) format.flags &= ~kBold;
                    else format.flags |= kBold;
                    for (auto& run : runs) {
                        if (bold) run.flags &= ~kBold;
                        else run.flags |= kBold;
                    }
                }
                if (!runs.empty()) {
                    auto result = doc.setTextRuns(id, runs);
                    if (!result.ok()) return result;
                }
            }
            auto result = doc.setTextFormat(id, format);
            if (!result.ok()) return result;
        }
    }
    return {};
}

inline Error autoFit(Document& doc, TableData& data, r62::text::GlyphCache& glyphs) {
    auto& grid = data.grid;
    std::fill(grid.columnWidths.begin(), grid.columnWidths.end(), 36.);
    std::fill(grid.rowHeights.begin(), grid.rowHeights.end(), 22.);
    for (uint32_t row = 0; row < grid.rows(); ++row) {
        for (uint32_t col = 0; col < grid.columns(); ++col) {
            const auto* cell = data.at(row, col);
            if (cell->kind != CellKind::Text || coveredCell(grid, row, col)) continue;
            const auto* text = doc.texts().alive(cell->text);
            if (!text) continue;
            r62::text::LayoutOptions options;
            options.triangulateGlyphs = false;
            options.defaultSize = text->size > 0 ? text->size : doc.defaultTextSize();
            options.defaultFont = text->format.fontName.empty() ? doc.defaultFont() : text->format.fontName;
            options.defaultFlags = text->format.flags;
            r62::text::RichLayout measured;
            auto result = r62::text::layoutRichText(doc.runs(cell->text), Vec2(), options, &glyphs, &measured);
            if (!result.ok()) return result;
            const auto area = selection(grid, {row, col}, {row, col});
            const auto width = std::min(400., measured.metrics.width + 10.);
            const auto height = measured.metrics.height + 8.;
            for (auto c = area.left; c <= area.right; ++c)
                grid.columnWidths[c] = std::max(grid.columnWidths[c], width / (area.right - area.left + 1));
            for (auto r = area.top; r <= area.bottom; ++r)
                grid.rowHeights[r] = std::max(grid.rowHeights[r], height / (area.bottom - area.top + 1));
        }
    }
    return {};
}

inline Cell bounded(const TableGrid& grid, Cell position) {
    return anchor(grid, {std::min(position.row, grid.rows()-1), std::min(position.column, grid.columns()-1)});
}

inline Error applyOperation(GtkWindow* owner, Session& session, ObjectId id, Cell first, Cell last,
                            Operation op, const std::string& title) {
    TableData data;
    auto read = readTable(session.doc, id, &data, nullptr);
    if (!read.ok()) return read;
    const auto cell = bounded(data.grid, last);
    const auto range = selection(data.grid, bounded(data.grid, first), cell);
    const auto height = data.grid.rowHeights[range.top], width = data.grid.columnWidths[range.left];
    const bool multiple = range.top != range.bottom || range.left != range.right;
    double x = 1, y = 1;
    if (op == RowHeight) {
        x = height;
        if (!values(owner, "行高", "行高（pt）", x, 12, 240)) return {};
    } else if (op == ColWidth) {
        x = width;
        if (!values(owner, "列宽", "列宽（pt）", x, 12, 400)) return {};
    } else if (op == Merge) {
        x = range.bottom - range.top + 1;
        y = range.right - range.left + 1;
        if (!multiple && !values(owner, "合并单元格", "行数", x, 1, data.grid.rows() - range.top,
                "列数", &y, data.grid.columns() - range.left)) return {};
    } else if (op == FontSize) {
        x = textSize(session.doc, data.at(cell.row, cell.column));
        if (!values(owner, "字号", "字号（pt）", x, 4, 144)) return {};
    } else if (op == BorderWidth) {
        x = data.grid.borderWidth > 0 ? data.grid.borderWidth : .6;
        if (!values(owner, "边框线宽", "线宽（pt）", x, .1, 12)) return {};
    }
    auto* glyphs = &session.glyphs;
    auto change = [=](Document& doc) -> Error {
        Error result;
        switch (op) {
        case Clear:
        case DeleteTable: {
            TableData current;
            result = readTable(doc, id, &current, nullptr);
            if (!result.ok()) return result;
            if (op == DeleteTable) {
                for (const auto& member : current.cells) {
                    if (member.kind != CellKind::Text || !doc.texts().alive(member.text)) continue;
                    result = doc.removeObject({ObjectKind::Text, member.text});
                    if (!result.ok()) return result;
                }
                return doc.removeObject({ObjectKind::Table, id});
            }
            for (auto r = range.top; r <= range.bottom; ++r)
                for (auto c = range.left; c <= range.right; ++c) {
                    const auto kind = current.at(r,c)->kind;
                    if (kind == CellKind::Embedded) return {ErrorCode::InvalidArgument, "目标单元格含嵌入结构"};
                    if (kind != CellKind::Text || coveredCell(current.grid,r,c)) continue;
                    result = clearCell(doc,id,r,c);
                    if (!result.ok()) return result;
                }
            return {};
        }
        case RowAbove: return insertRow(doc, id, range.top, height);
        case RowBelow: return insertRow(doc, id, range.bottom + 1, height);
        case DeleteRow:
            for (int row = int(range.bottom); row >= int(range.top); --row) {
                result = deleteRow(doc, id, uint32_t(row));
                if (!result.ok()) return result;
            }
            return {};
        case ColLeft: return insertColumn(doc, id, range.left, width);
        case ColRight: return insertColumn(doc, id, range.right + 1, width);
        case DeleteCol:
            for (int col = int(range.right); col >= int(range.left); --col) {
                result = deleteColumn(doc, id, uint32_t(col));
                if (!result.ok()) return result;
            }
            return {};
        case Merge: return mergeCells(doc, id, range.top, range.left, uint32_t(x), uint32_t(y));
        case Split: return splitCell(doc, id, cell.row, cell.column);
        case BorderWidth: return setTableBorderWidth(doc, id, x);
        default: break;
        }
        TableData data;
        result = readTable(doc, id, &data, nullptr);
        if (!result.ok()) return result;
        if (op == AutoFit || op == RowHeight || op == ColWidth || op == EqualRows || op == EqualColumns) {
            if (op == AutoFit) {
                result = autoFit(doc, data, *glyphs);
                if (!result.ok()) return result;
            } else {
                const bool byRow = op == RowHeight || op == EqualRows;
                auto& lengths = byRow ? data.grid.rowHeights : data.grid.columnWidths;
                const auto from = lengths.begin() + (byRow ? range.top : range.left);
                const auto to = lengths.begin() + (byRow ? range.bottom : range.right) + 1;
                const auto length = op == EqualRows || op == EqualColumns ?
                    std::accumulate(from, to, 0.) / (to - from) : x;
                std::fill(from, to, length);
            }
            data.grid.frame.max = data.grid.frame.min + Vec2(data.grid.totalWidth(), data.grid.totalHeight());
            return writeTable(doc, id, data);
        }
        if (op >= FontSize && op <= AlignBottom) {
            result = formatCells(doc, data, range, op, x);
            if (!result.ok()) return result;
            return writeTable(doc, id, data);
        }
        if (op == ThreeLines && data.grid.rows() < 2)
            return {ErrorCode::InvalidArgument, "三线表需要至少两行：表头和数据行"};
        const char* style = op == ThreeLines ? "three" : op == Horizontal ? "horizontal" :
            op == Outer ? "outer" : op == NoBorder ? "none" : "grid";
        const ObjectRef ref(ObjectKind::Table, id);
        doc.noteTouchedExtensions(ref, false);
        doc.extensions(ref).set("studio.table.style", style);
        data.grid.borders.outer = op == NoBorder ? TableBorderStyle::None : TableBorderStyle::Thin;
        data.grid.borders.inner = op == NoBorder || op == Outer ? TableBorderStyle::None : TableBorderStyle::Thin;
        if (op == ThreeLines) {
            for (uint32_t col = 0; col < data.grid.columns(); ++col) {
                const auto* header = data.at(0, col);
                if (header->kind != CellKind::Text) continue;
                auto format = doc.texts().get(header->text)->format;
                format.flags |= kBold;
                result = doc.setTextFormat(header->text, format);
                if (!result.ok()) return result;
            }
        }
        return writeTable(doc, id, data);
    };
    return session.submit(Command(title, kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
        [change](CommandContext& ctx) {
            auto result = change(ctx.document);
            return result.ok() ? CommandResult::success() : CommandResult::fail(result.code, result.message);
        }), title);
}

class Editor {
    enum Response { Edit = 1, Paste, Apply, Copy, Undo, Redo, ClearSelection };
    Session& session_;
    ObjectId id_;
    Cell first_, cell_;
    Range range_{};
    GtkWidget *dialog_, *tree_, *operation_, *status_, *location_;
    TableData data_;
    bool refreshing_ = false;
    bool dragging_ = false;
    bool changed_ = false;
    std::function<void()> redraw_;

    void status(const std::string& text) const {
        gtk_label_set_text(GTK_LABEL(status_), linux_i18n::message(text).c_str());
    }

    bool mutate(const char* name, std::function<Error(Document&)> edit) {
        auto result = session_.submit(Command(name, kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
            [edit](CommandContext& ctx) {
                auto result = edit(ctx.document);
                return result.ok() ? CommandResult::success() : CommandResult::fail(result.code, result.message);
            }), name);
        status(result.message);
        if (!result.ok()) return false;
        changed_ = true;
        redraw_();
        return true;
    }

    void select(Cell position, bool extend = false) {
        cell_ = anchor(data_.grid, position);
        if (!extend) first_ = cell_;
        range_ = selection(data_.grid, first_, cell_);
        auto span = [](uint32_t first, uint32_t last) {
            auto text = std::to_string(first + 1);
            if (first != last) text += "–" + std::to_string(last + 1);
            return text;
        };
        gtk_label_set_text(GTK_LABEL(location_), (std::string(tr("行")) + " " + span(range_.top, range_.bottom) +
            "    " + tr("列") + " " + span(range_.left, range_.right)).c_str());
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_), Edit,
            data_.at(cell_.row, cell_.column)->kind != CellKind::Embedded);
        gtk_widget_queue_draw(tree_);
    }

    Cell position(GtkTreePath* path, GtkTreeViewColumn* column) const {
        uint32_t col = 0;
        for (uint32_t i = 1; i <= data_.grid.columns(); ++i) {
            if (gtk_tree_view_get_column(GTK_TREE_VIEW(tree_), int(i)) != column) continue;
            col = i - 1;
            break;
        }
        return {uint32_t(gtk_tree_path_get_indices(path)[0]), col};
    }

    void placeCursor(Cell cell, bool extend) {
        const auto previous = refreshing_;
        refreshing_ = true;
        select(cell, extend);
        auto* tree = GTK_TREE_VIEW(tree_);
        auto* column = gtk_tree_view_get_column(tree, int(cell_.column + 1));
        auto* path = gtk_tree_path_new_from_indices(int(cell_.row), -1);
        gtk_tree_view_set_cursor(tree, path, column, FALSE);
        gtk_tree_view_scroll_to_cell(tree, path, column, FALSE, 0, 0);
        gtk_tree_path_free(path);
        refreshing_ = previous;
    }

    bool pickCell(int x, int y, bool extend) {
        auto* tree = GTK_TREE_VIEW(tree_);
        GtkTreePath* path = nullptr;
        GtkTreeViewColumn* column = nullptr;
        if (!gtk_tree_view_get_path_at_pos(tree, x, y, &path, &column, nullptr, nullptr)) return false;
        const auto cell = position(path, column);
        gtk_tree_path_free(path);
        gtk_widget_grab_focus(tree_);
        placeCursor(cell, extend);
        return true;
    }

    bool handleKey(const GdkEventKey& event) {
        if (event.state & GDK_MOD1_MASK) return false;
        const bool extend = (event.state & GDK_SHIFT_MASK) != 0;
        auto response = [this](Response value) {
            gtk_dialog_response(GTK_DIALOG(dialog_), value);
            return true;
        };
        if (event.state & GDK_CONTROL_MASK) {
            switch (gdk_keyval_to_lower(event.keyval)) {
            case GDK_KEY_a:
                first_ = {};
                placeCursor({data_.grid.rows() - 1, data_.grid.columns() - 1}, true);
                return true;
            case GDK_KEY_c: return response(Copy);
            case GDK_KEY_v: return response(Paste);
            case GDK_KEY_z: return response(extend ? Redo : Undo);
            case GDK_KEY_y: return response(Redo);
            default: return false;
            }
        }
        const auto selected = selection(data_.grid, cell_, cell_);
        int row = int(cell_.row);
        int col = int(cell_.column);
        switch (event.keyval) {
        case GDK_KEY_Left: col = int(selected.left) - 1; break;
        case GDK_KEY_Right: col = int(selected.right) + 1; break;
        case GDK_KEY_Up: row = int(selected.top) - 1; break;
        case GDK_KEY_Down: row = int(selected.bottom) + 1; break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter: return response(Edit);
        case GDK_KEY_Delete: return response(ClearSelection);
        default: return false;
        }
        placeCursor({uint32_t(std::clamp(row, 0, int(data_.grid.rows()) - 1)),
                     uint32_t(std::clamp(col, 0, int(data_.grid.columns()) - 1))}, extend);
        return true;
    }

    bool refresh() {
        auto result = readTable(session_.doc, id_, &data_, nullptr);
        if (!result.ok()) {
            status(result.message);
            return false;
        }
        refreshing_ = true;
        auto* tree = GTK_TREE_VIEW(tree_);
        while (auto* column = gtk_tree_view_get_column(tree, 0)) gtk_tree_view_remove_column(tree, column);
        const auto count = data_.grid.columns();
        std::vector<GType> types(count + 1, G_TYPE_STRING);
        auto* list = gtk_list_store_newv(int(types.size()), types.data());
        gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list));
        for (uint32_t col = 0; col <= count; ++col) {
            auto* renderer = gtk_cell_renderer_text_new();
            g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", col ? 13 : 4, nullptr);
            auto* column = gtk_tree_view_column_new_with_attributes(col ? std::to_string(col).c_str() : tr("行"),
                renderer, "text", int(col), nullptr);
            gtk_tree_view_column_set_expand(column,col!=0);
            gtk_tree_view_column_set_min_width(column,col ? 84 : 40);
            g_object_set(renderer,"xpad",8,"ypad",6,nullptr);
            if (col) {
                g_object_set_data(G_OBJECT(column), "table-column-number", GUINT_TO_POINTER(col));
                gtk_tree_view_column_set_cell_data_func(column, renderer,
                    +[](GtkTreeViewColumn* col, GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* row,
                        gpointer data) {
                        const auto& editor = *static_cast<Editor*>(data);
                        auto* path = gtk_tree_model_get_path(model, row);
                        const auto index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(col), "table-column-number"));
                        const auto selected = editor.range_.includes({uint32_t(gtk_tree_path_get_indices(path)[0]),
                            index - 1});
                        gtk_tree_path_free(path);
                        GdkRGBA bg{.81,.92,.96,1.}, fg{.08,.22,.31,1.};
                        g_object_set(renderer, "cell-background-rgba", &bg, "foreground-rgba", &fg,
                            "cell-background-set", selected, "foreground-set", selected, nullptr);
                    }, this, nullptr);
            }
            gtk_tree_view_append_column(tree, column);
        }
        for (uint32_t row = 0; row < data_.grid.rows(); ++row) {
            GtkTreeIter iter;
            gtk_list_store_append(list, &iter);
            gtk_list_store_set(list, &iter, 0, std::to_string(row + 1).c_str(), -1);
            for (uint32_t col = 0; col < count; ++col) {
                auto position = anchor(data_.grid, {row, col});
                std::string text;
                if (position.row == row && position.column == col) {
                    const auto* cell = data_.at(row, col);
                    if (cell->kind == CellKind::Embedded) text = tr("嵌入结构");
                    else if (cell->kind == CellKind::Text) text = session_.doc.plainText(cell->text);
                }
                gtk_list_store_set(list, &iter, int(col + 1), text.c_str(), -1);
            }
        }
        g_object_unref(list);
        auto clamp = [&](Cell cell) {
            return Cell{std::min(cell.row, data_.grid.rows() - 1), std::min(cell.column, count - 1)};
        };
        first_ = clamp(first_);
        placeCursor(clamp(cell_), true);
        refreshing_ = false;
        return true;
    }

    void editCell() {
        std::string before;
        CellKind kind;
        auto result = cellText(session_.doc, id_, cell_.row, cell_.column, &before, &kind);
        if (!result.ok() || kind == CellKind::Embedded) return;
        auto* dialog = gtk_dialog_new_with_buttons(tr("编辑单元格"), GTK_WINDOW(dialog_), GTK_DIALOG_MODAL,
            tr("取消"), GTK_RESPONSE_CANCEL, tr("应用"), GTK_RESPONSE_ACCEPT, nullptr);
        auto* view = gtk_text_view_new();
        auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        gtk_text_buffer_set_text(buffer, before.c_str(), -1);
        auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(scroll, 400, 160);
        gtk_container_add(GTK_CONTAINER(scroll), view);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll);
        gtk_widget_show_all(dialog);
        gtk_widget_grab_focus(view);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            GtkTextIter begin, end;
            gtk_text_buffer_get_bounds(buffer, &begin, &end);
            auto* value = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
            const std::string text = value;
            g_free(value);
            const auto position = cell_;
            const auto id = id_;
            const auto size = textSize(session_.doc, data_.at(position.row, position.column));
            if (text != before && mutate("编辑单元格", [id, position, text, size](Document& doc) {
                return setCellText(doc, id, position.row, position.column, text, size);
            })) refresh();
        }
        gtk_widget_destroy(dialog);
    }

    void apply(Operation op) {
        const auto before = session_.serial;
        auto result = applyOperation(GTK_WINDOW(dialog_), session_, id_, first_, cell_, op, "表格编辑");
        status(result.message);
        if (result.ok() && session_.serial != before) {
            changed_ = true;
            refresh();
            redraw_();
        }
    }

public:
    Editor(GtkWindow* owner, Session& session, ObjectId id, Cell position, std::function<void()> redraw, const Cell* first = nullptr)
        : session_(session), id_(id), first_(first ? *first : position), cell_(position), redraw_(std::move(redraw)) {
        dialog_ = gtk_dialog_new_with_buttons(tr("表格编辑"), owner, GTK_DIALOG_MODAL,
            tr("关闭"), GTK_RESPONSE_CLOSE, tr("编辑单元格"), Edit,
            tr("复制所选单元格"), Copy, tr("粘贴表格内容"), Paste, nullptr);
        gtk_widget_set_name(dialog_,"table-editor");
        gtk_window_set_default_size(GTK_WINDOW(dialog_), 660, 340);
        auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog_)));
        gtk_container_set_border_width(GTK_CONTAINER(content), 10);
        auto* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        location_ = gtk_label_new("");
        operation_ = gtk_combo_box_text_new();
        for (const auto* name : {"上方插入行", "下方插入行", "删除行", "左侧插入列", "右侧插入列", "删除列",
                "行高", "列宽", "合并单元格", "拆分单元格", "全框线", "三线表", "横线", "仅外框", "无边框",
                "清空单元格", "字号", "加粗", "文字左对齐", "文字居中", "文字右对齐",
                "顶端对齐", "垂直居中", "底端对齐", "等行高", "等列宽", "自动调整表格", "边框线宽"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(operation_), tr(name));
        gtk_combo_box_set_active(GTK_COMBO_BOX(operation_), 0);
        auto* applyButton = gtk_button_new_with_label(tr("应用"));
        gtk_box_pack_start(GTK_BOX(head), location_, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(head), applyButton, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(head), operation_, FALSE, FALSE, 0);
        gtk_box_pack_start(content, head, FALSE, FALSE, 4);
        tree_ = gtk_tree_view_new();
        gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_)), GTK_SELECTION_NONE);
        gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tree_), GTK_TREE_VIEW_GRID_LINES_BOTH);
        gtk_widget_add_events(tree_, GDK_BUTTON1_MOTION_MASK | GDK_BUTTON_RELEASE_MASK);
        auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(scroll), tree_);
        gtk_box_pack_start(content, scroll, TRUE, TRUE, 4);
        status_ = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(status_), 0);
        gtk_box_pack_start(content, status_, FALSE, FALSE, 4);
        g_signal_connect(applyButton, "clicked", G_CALLBACK(+[](GtkButton*, Editor* editor) {
            gtk_dialog_response(GTK_DIALOG(editor->dialog_), Apply);
        }), this);
        g_signal_connect(tree_, "cursor-changed", G_CALLBACK(+[](GtkTreeView* tree, Editor* editor) {
            if (editor->refreshing_) return;
            GtkTreePath* path = nullptr;
            GtkTreeViewColumn* column = nullptr;
            gtk_tree_view_get_cursor(tree, &path, &column);
            if (!path) return;
            editor->select(editor->position(path, column));
            gtk_tree_path_free(path);
        }), this);
        g_signal_connect(tree_, "button-press-event", G_CALLBACK(+[](GtkTreeView* tree, GdkEventButton* event, Editor* editor)->gboolean {
            if (event->button != 1 || (event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) ||
                    event->window != gtk_tree_view_get_bin_window(tree)) return FALSE;
            if (!editor->pickCell(int(event->x), int(event->y), event->state & GDK_SHIFT_MASK)) return FALSE;
            editor->dragging_ = event->type == GDK_BUTTON_PRESS;
            if (event->type == GDK_2BUTTON_PRESS) gtk_dialog_response(GTK_DIALOG(editor->dialog_), Edit);
            return TRUE;
        }), this);
        g_signal_connect(tree_, "motion-notify-event", G_CALLBACK(+[](GtkTreeView* tree, GdkEventMotion* event, Editor* editor)->gboolean {
            if (!editor->dragging_ || !(event->state & GDK_BUTTON1_MASK) ||
                    event->window != gtk_tree_view_get_bin_window(tree)) return FALSE;
            return editor->pickCell(int(event->x), int(event->y), true);
        }), this);
        g_signal_connect(tree_, "button-release-event", G_CALLBACK(+[](GtkTreeView*, GdkEventButton* event, Editor* editor)->gboolean {
            if (event->button == 1) editor->dragging_ = false;
            return FALSE;
        }), this);
        g_signal_connect(tree_, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, Editor* editor)->gboolean {
            return editor->handleKey(*event);
        }), this);
        g_signal_connect(tree_, "row-activated", G_CALLBACK(+[](GtkTreeView*, GtkTreePath* path, GtkTreeViewColumn* column, Editor* editor) {
            editor->select(editor->position(path, column));
            gtk_dialog_response(GTK_DIALOG(editor->dialog_), Edit);
        }), this);
    }
    ~Editor() {
        g_signal_handlers_disconnect_by_data(tree_, this);
        gtk_widget_destroy(dialog_);
    }
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    Cell firstCell() const { return first_; }
    Cell lastCell() const { return cell_; }
    bool run() {
        if (!refresh()) return false;
        gtk_widget_show_all(dialog_);
        gtk_widget_grab_focus(tree_);
        while (true) {
            const auto response = gtk_dialog_run(GTK_DIALOG(dialog_));
            if (response == Edit) editCell();
            else if (response == Apply) apply(Operation(gtk_combo_box_get_active(GTK_COMBO_BOX(operation_))));
            else if (response == ClearSelection) apply(Clear);
            else if (response == Undo || response == Redo) {
                const auto result = session_.undo(response == Redo);
                status(result.message);
                if (result.ok()) {
                    changed_ = true;
                    redraw_();
                    if (!refresh()) return changed_;
                }
            } else if (response == Copy) {
                const auto text = copy(session_.doc, data_, range_);
                gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.data(), int(text.size()));
            }
            else if (response == Paste) {
                auto* value = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
                if (!value) { status("剪贴板没有文本"); continue; }
                const std::string text = value;
                g_free(value);
                const auto position = cell_;
                const auto id = id_;
                if (!text.empty() && mutate("粘贴表格内容", [id, position, text](Document& doc) {
                    return paste(doc, id, position, text);
                })) refresh();
            } else return changed_;
        }
    }
};
}
