#include "next/graphics/tables.hpp"

#include "next/core/commands.hpp"
#include "next/graphics/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace r62 {
namespace graphics {
namespace {

const double kMinCell = 2.0;   // [PROJ] a cell may not be dragged smaller than this

bool parseInt(const std::string& s, int* out) {
    if (s.empty()) return false;
    char* end = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != 0) return false;
    *out = static_cast<int>(v);
    return true;
}
bool parseDouble(const std::string& s, double* out) {
    if (s.empty()) return false;
    char* end = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end != 0 || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

void addInt(std::ostringstream& o, const char* k, int v) { o << k << "=" << v << ";"; }
void addDbl(std::ostringstream& o, const char* k, double v) {
    o.precision(17);
    o << k << "=" << v << ";";
}

} // namespace

// The parameter codec is public (tables.hpp) so that persistence and clipboard
// layers can move a table's authoritative record without linking to the model.
std::string encodeTableParams(const TableData& d) {
    std::ostringstream o;
    o.precision(17);
    addDbl(o, "fx0", d.grid.frame.min.x);
    addDbl(o, "fy0", d.grid.frame.min.y);
    addDbl(o, "fx1", d.grid.frame.max.x);
    addDbl(o, "fy1", d.grid.frame.max.y);
    addInt(o, "rows", static_cast<int>(d.grid.rows()));
    addInt(o, "cols", static_cast<int>(d.grid.columns()));
    for (size_t i = 0; i < d.grid.rowHeights.size(); ++i) {
        std::ostringstream k; k << "rh" << i;
        addDbl(o, k.str().c_str(), d.grid.rowHeights[i]);
    }
    for (size_t i = 0; i < d.grid.columnWidths.size(); ++i) {
        std::ostringstream k; k << "cw" << i;
        addDbl(o, k.str().c_str(), d.grid.columnWidths[i]);
    }
    addInt(o, "outer", static_cast<int>(d.grid.borders.outer));
    addInt(o, "inner", static_cast<int>(d.grid.borders.inner));
    addDbl(o, "bw", d.grid.borderWidth);
    addInt(o, "merged", static_cast<int>(d.grid.merged.size()));
    for (size_t i = 0; i < d.grid.merged.size(); ++i) {
        std::ostringstream k; k << "m" << i;
        std::ostringstream v;
        v << d.grid.merged[i].row << "," << d.grid.merged[i].column << ","
          << d.grid.merged[i].rowSpan << "," << d.grid.merged[i].columnSpan;
        o << k.str() << "=" << v.str() << ";";
    }
    // Cell kinds and embed refs. TEXT cells reference a Text object, which is
    // itself part of the core document, so only the kind and the object id are
    // recorded here; the string lives in the Text object.
    addInt(o, "cells", static_cast<int>(d.cells.size()));
    for (size_t i = 0; i < d.cells.size(); ++i) {
        const TableCell& c = d.cells[i];
        std::ostringstream k; k << "c" << i;
        std::ostringstream v;
        v << static_cast<int>(c.kind) << "," << static_cast<int>(c.embed.kind) << ","
          << c.embed.id << "," << c.text << "," << c.textOffset.x << "," << c.textOffset.y;
        o << k.str() << "=" << v.str() << ";";
    }
    // The title is escaped because it is the one free-text field in this record.
    if (!d.grid.title.empty()) {
        static const char* hex = "0123456789ABCDEF";
        std::string esc;
        for (size_t i = 0; i < d.grid.title.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(d.grid.title[i]);
            esc.push_back(hex[(ch >> 4) & 0xF]);
            esc.push_back(hex[ch & 0xF]);
        }
        o << "title=" << esc << ";";
    }
    return o.str();
}

// Decode. `ok` is false only for a MALFORMED record; a missing record is reported
// by the caller before this is reached.
TableData decodeTableParams(const std::string& text, bool* ok) {
    TableData d;
    bool good = true;
    int rows = -1, cols = -1, cells = -1, merged = 0;
    int outer = -1, inner = -1;
    std::vector<double> rh, cw;
    std::vector<TableCell> cellList;
    std::string titleHex;
    double fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0, bw = 0;
    bool haveFrame = false;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t semi = text.find(';', pos);
        const std::string tok = text.substr(pos, semi == std::string::npos ? std::string::npos
                                                                    : semi - pos);
        pos = (semi == std::string::npos) ? text.size() : semi + 1;
        if (tok.empty()) continue;
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) { good = false; break; }
        const std::string k = tok.substr(0, eq);
        const std::string v = tok.substr(eq + 1);
        double dv = 0.0;
        if (k == "fx0") { if (!parseDouble(v, &fx0)) { good = false; break; } }
        else if (k == "fy0") { if (!parseDouble(v, &fy0)) { good = false; break; } }
        else if (k == "fx1") { if (!parseDouble(v, &fx1)) { good = false; break; } }
        else if (k == "fy1") { if (!parseDouble(v, &fy1)) { good = false; break; } haveFrame = true; }
        else if (k == "rows") { if (!parseInt(v, &rows) || rows < 0 || rows > 10000) { good = false; break; } }
        else if (k == "cols") { if (!parseInt(v, &cols) || cols < 0 || cols > 10000) { good = false; break; } }
        else if (k == "outer") { if (!parseInt(v, &outer)) { good = false; break; } }
        else if (k == "inner") { if (!parseInt(v, &inner)) { good = false; break; } }
        else if (k == "bw") { if (!parseDouble(v, &bw)) { good = false; break; } }
        else if (k == "merged") { if (!parseInt(v, &merged) || merged < 0 || merged > 10000) { good = false; break; } }
        else if (k == "cells") { if (!parseInt(v, &cells) || cells < 0 || cells > 1000000) { good = false; break; } }
        else if (k.size() > 2 && k[0] == 'r' && k[1] == 'h') {
            int idx = -1;
            if (!parseInt(k.substr(2), &idx) || idx < 0) { good = false; break; }
            if (!parseDouble(v, &dv)) { good = false; break; }
            if (rh.size() <= static_cast<size_t>(idx)) rh.resize(static_cast<size_t>(idx) + 1, 0.0);
            rh[static_cast<size_t>(idx)] = dv;
        }
        else if (k.size() > 2 && k[0] == 'c' && k[1] == 'w') {
            int idx = -1;
            if (!parseInt(k.substr(2), &idx) || idx < 0) { good = false; break; }
            if (!parseDouble(v, &dv)) { good = false; break; }
            if (cw.size() <= static_cast<size_t>(idx)) cw.resize(static_cast<size_t>(idx) + 1, 0.0);
            cw[static_cast<size_t>(idx)] = dv;
        }
        else if (k[0] == 'm' && k.size() > 1 && k[1] >= '0' && k[1] <= '9') {
            // Reserved merged-region record; parsed so a v2 document round-trips
            // through a v1 reader without being destroyed.
            int idx = -1;
            if (!parseInt(k.substr(1), &idx) || idx < 0) { good = false; break; }
            MergedRegion m;
            if (std::sscanf(v.c_str(), "%u,%u,%u,%u", &m.row, &m.column, &m.rowSpan,
                            &m.columnSpan) != 4) { good = false; break; }
            if (d.grid.merged.size() <= static_cast<size_t>(idx))
                d.grid.merged.resize(static_cast<size_t>(idx) + 1);
            d.grid.merged[static_cast<size_t>(idx)] = m;
        }
        else if (k[0] == 'c' && k.size() > 1 && k[1] >= '0' && k[1] <= '9') {
            int idx = -1;
            if (!parseInt(k.substr(1), &idx) || idx < 0) { good = false; break; }
            unsigned int ck = 0, ek = 0, eid = 0, tid = 0;
            double ox = 0, oy = 0;
            char comma = 0;
            std::istringstream is(v);
            is >> ck >> comma >> ek >> comma >> eid >> comma >> tid >> comma >> ox >> comma >> oy;
            if (!is) { good = false; break; }
            TableCell tc;
            tc.kind = static_cast<CellKind>(ck);
            tc.embed = EmbedRef(static_cast<core::ObjectKind>(ek), eid);
            tc.text = tid;
            tc.textOffset = core::Vec2(ox, oy);
            if (cellList.size() <= static_cast<size_t>(idx))
                cellList.resize(static_cast<size_t>(idx) + 1);
            cellList[static_cast<size_t>(idx)] = tc;
        }
        else if (k == "title") { titleHex = v; }
        // Unknown keys are ignored on purpose.
    }
    if (!haveFrame || rows < 0 || cols < 0 || cells < 0) good = false;
    if (good) {
        if (rh.size() != static_cast<size_t>(rows) || cw.size() != static_cast<size_t>(cols))
            good = false;
        else if (cellList.size() != static_cast<size_t>(cells) ||
                 static_cast<size_t>(cells) != static_cast<size_t>(rows) * static_cast<size_t>(cols))
            good = false;
        else if (merged != static_cast<int>(d.grid.merged.size()))
            good = false;
    }
    if (!good) { if (ok) *ok = false; return TableData(); }
    d.grid.frame = core::Box(core::Vec2(fx0, fy0), core::Vec2(fx1, fy1));
    d.grid.rowHeights = rh;
    d.grid.columnWidths = cw;
    d.grid.borders.outer = static_cast<TableBorderStyle>(outer < 0 ? 1 : outer);
    d.grid.borders.inner = static_cast<TableBorderStyle>(inner < 0 ? 1 : inner);
    d.grid.borderWidth = bw;
    for (size_t i = 0; i < titleHex.size(); i += 2) {
        if (i + 1 >= titleHex.size()) { if (ok) *ok = false; return TableData(); }
        const std::string byte = titleHex.substr(i, 2);
        char* end = 0;
        const long val = std::strtol(byte.c_str(), &end, 16);
        if (!end || *end != 0) { if (ok) *ok = false; return TableData(); }
        d.grid.title.push_back(static_cast<char>(val));
    }
    d.cells = cellList;
    if (ok) *ok = true;
    return d;
}

namespace {

// Keep TableRecord a valid, meaningful mirror of the grid.
void syncRecord(core::Document& document, core::ObjectId id, const TableData& d) {
    core::TableRecord* rec = document.tables().mutableGet(id);
    if (!rec) return;
    rec->frame = d.grid.frame;
    rec->rows = d.grid.rows();
    rec->columns = d.grid.columns();
    rec->rowHeight = d.grid.rowHeights.empty() ? 0.0 : d.grid.rowHeights[0];
    rec->cells.assign(static_cast<size_t>(d.grid.rows()) * d.grid.columns(),
                      core::kInvalidObjectId);
    const size_t n = std::min(rec->cells.size(), d.cells.size());
    for (size_t i = 0; i < n; ++i) {
        if (d.cells[i].kind == CellKind::Text) rec->cells[i] = d.cells[i].text;
    }
}

// Rebuild a grid from the core record alone: equal division inside `frame`.
TableData fromRecord(const core::TableRecord& rec) {
    TableData d;
    d.grid.frame = rec.frame;
    const uint32_t rows = rec.rows > 0 ? rec.rows : 1u;
    const uint32_t cols = rec.columns > 0 ? rec.columns : 1u;
    const double h = (rec.frame.max.y - rec.frame.min.y) / static_cast<double>(rows);
    const double w = (rec.frame.max.x - rec.frame.min.x) / static_cast<double>(cols);
    d.grid.rowHeights.assign(rows, h > 0.0 ? h : 12.0);
    d.grid.columnWidths.assign(cols, w > 0.0 ? w : 24.0);
    d.cells.resize(static_cast<size_t>(rows) * cols);
    const size_t n = std::min(d.cells.size(), rec.cells.size());
    for (size_t i = 0; i < n; ++i) {
        if (rec.cells[i] != core::kInvalidObjectId) {
            d.cells[i].kind = CellKind::Text;
            d.cells[i].text = rec.cells[i];
        }
    }
    return d;
}

// The grid lives in the extension bag, so the bag must be part of the change set
// or undo/redo cannot restore a resize. Document::noteTouchedExtensions is the
// public notice for that (see model.hpp for why setLineStyle() is NOT enough).
void notifyBagDirty(core::Document& document, core::ObjectId id) {
    document.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Table, id), false);
}

// Move a cell's Text object so it sits at the cell centre plus its offset. The
// table owns the placement, so a resize never leaves a label behind.
void placeCellText(core::Document& document, const TableGrid& g, uint32_t row,
                   uint32_t column, const TableCell& cell) {
    if (cell.kind != CellKind::Text || cell.text == core::kInvalidObjectId) return;
    core::TextRecord* t = document.texts().mutableGet(cell.text);
    if (!t || !t->isAlive()) return;
    const core::Box b = g.cellBox(row, column);
    const double size=t->size>0?t->size:document.defaultTextSize();
    const auto plain=document.plainText(cell.text);
    const double height=size*(1+1.25*std::count(plain.begin(),plain.end(),'\n'));
    const auto valign=static_cast<const core::Document&>(document).extensions(core::ObjectRef(core::ObjectKind::Text,cell.text)).findValue("studio.cell.valign");
    const double cx=(t->format.align==0?b.min.x+4:t->format.align==2?b.max.x-4:(b.min.x+b.max.x)*.5)+cell.textOffset.x;
    const double cy=(valign&&*valign=="top"?b.min.y+3:valign&&*valign=="bottom"?b.max.y-height-3:(b.min.y+b.max.y-height)*.5)+size*.8+cell.textOffset.y;
    core::Error ignored = document.setTextPosition(cell.text, core::Vec2(cx, cy));
    (void)ignored;
}

void placeAllCellText(core::Document& document, const TableData& d) {
    for (uint32_t r = 0; r < d.grid.rows(); ++r) {
        for (uint32_t c = 0; c < d.grid.columns(); ++c) {
            const TableCell* cell = d.at(r, c);
            if (cell) placeCellText(document, d.grid, r, c, *cell);
        }
    }
}

core::Error commitTable(core::Document& document, core::ObjectId id, const TableData& d,
                        core::LineStyle style) {
    (void)style;   // the bag is authoritative; style is kept for call-site symmetry
    core::TableRecord* rec = document.tables().mutableGet(id);
    if (!rec || !rec->isAlive())
        return core::Error(core::ErrorCode::NotAlive, "Table is deleted");
    if (d.grid.rows() == 0 || d.grid.columns() == 0)
        return core::Error(core::ErrorCode::InvalidArgument, "A table needs at least one row and column");
    if (d.cells.size() != static_cast<size_t>(d.grid.rows()) * d.grid.columns())
        return core::Error(core::ErrorCode::InvalidArgument, "Cell array does not match the grid");
    for (size_t i = 0; i < d.grid.rowHeights.size(); ++i)
        if (!(d.grid.rowHeights[i] > 0.0))
            return core::Error(core::ErrorCode::InvalidGeometry, "Row height must be positive");
    for (size_t i = 0; i < d.grid.columnWidths.size(); ++i)
        if (!(d.grid.columnWidths[i] > 0.0))
            return core::Error(core::ErrorCode::InvalidGeometry, "Column width must be positive");
    for(size_t i=0;i<d.grid.merged.size();++i){const auto& m=d.grid.merged[i];
        if(!m.rowSpan||!m.columnSpan||m.row>=d.grid.rows()||m.column>=d.grid.columns()||m.rowSpan>d.grid.rows()-m.row||m.columnSpan>d.grid.columns()-m.column)
            return core::Error(core::ErrorCode::InvalidArgument,"无效的合并范围");
        for(size_t j=0;j<i;++j){const auto& n=d.grid.merged[j];if(m.row<n.row+n.rowSpan&&m.row+m.rowSpan>n.row&&m.column<n.column+n.columnSpan&&m.column+m.columnSpan>n.column)
            return core::Error(core::ErrorCode::InvalidArgument,"合并范围不能重叠");}
    }
    syncRecord(document, id, d);
    document.extensions(core::ObjectRef(core::ObjectKind::Table, id))
            .set(kTableParamsKey, encodeTableParams(d));
    placeAllCellText(document, d);
    document.noteTouchedTable(id);
    notifyBagDirty(document, id);
    document.bumpStructure();
    core::Error v = document.validate();
    return v;
}

} // namespace

// ===========================================================================
// TableGrid
// ===========================================================================
double TableGrid::totalHeight() const {
    double h = 0.0;
    for (size_t i = 0; i < rowHeights.size(); ++i) h += rowHeights[i];
    return h;
}

double TableGrid::totalWidth() const {
    double w = 0.0;
    for (size_t i = 0; i < columnWidths.size(); ++i) w += columnWidths[i];
    return w;
}

core::Vec2 TableGrid::cellOrigin(uint32_t row, uint32_t column) const {
    double x = frame.min.x;
    double y = frame.min.y;
    for (uint32_t i = 0; i < column && i < columnWidths.size(); ++i) x += columnWidths[i];
    for (uint32_t i = 0; i < row && i < rowHeights.size(); ++i) y += rowHeights[i];
    return core::Vec2(x, y);
}

core::Box TableGrid::cellBox(uint32_t row, uint32_t column) const {
    const core::Vec2 o = cellOrigin(row, column);
    double w = column < columnWidths.size() ? columnWidths[column] : 24.0;
    double h = row < rowHeights.size() ? rowHeights[row] : 12.0;
    for(const auto& m:merged)if(row==m.row&&column==m.column){
        w=0;h=0;for(uint32_t c=column;c<std::min(columns(),column+m.columnSpan);++c)w+=columnWidths[c];
        for(uint32_t r=row;r<std::min(rows(),row+m.rowSpan);++r)h+=rowHeights[r];break;
    }
    return core::Box(o, core::Vec2(o.x + w, o.y + h));
}

bool TableGrid::cellAt(const core::Vec2& p, uint32_t* row, uint32_t* column) const {
    return cellAt(p, row, column, 0.0, 0, 0);
}

bool TableGrid::cellAt(const core::Vec2& p, uint32_t* row, uint32_t* column,
                       double tolerance, int* verticalBorderIndex,
                       int* horizontalBorderIndex) const {
    if (verticalBorderIndex) *verticalBorderIndex = -1;
    if (horizontalBorderIndex) *horizontalBorderIndex = -1;
    const core::Box outer(frame.min,
                          core::Vec2(frame.min.x + totalWidth(), frame.min.y + totalHeight()));
    const core::Box test = inflateBox(outer, tolerance);
    if (!pointInBox(test, p)) return false;

    double x = frame.min.x;
    uint32_t c = 0;
    for (; c < columnWidths.size(); ++c) {
        if (p.x < x + columnWidths[c] || c + 1 == columnWidths.size()) break;
        x += columnWidths[c];
    }
    double y = frame.min.y;
    uint32_t r = 0;
    for (; r < rowHeights.size(); ++r) {
        if (p.y < y + rowHeights[r] || r + 1 == rowHeights.size()) break;
        y += rowHeights[r];
    }
    if (c >= columnWidths.size()) c = columnWidths.empty() ? 0u : static_cast<uint32_t>(columnWidths.size() - 1);
    if (r >= rowHeights.size()) r = rowHeights.empty() ? 0u : static_cast<uint32_t>(rowHeights.size() - 1);
    for(const auto& m:merged)if(r>=m.row&&r<m.row+m.rowSpan&&c>=m.column&&c<m.column+m.columnSpan){r=m.row;c=m.column;break;}
    if (row) *row = r;
    if (column) *column = c;

    if (verticalBorderIndex && tolerance > 0.0) {
        double bx = frame.min.x;
        for (size_t i = 0; i + 1 < columnWidths.size(); ++i) {
            bx += columnWidths[i];
            if (std::fabs(p.x - bx) <= tolerance) {
                *verticalBorderIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (horizontalBorderIndex && tolerance > 0.0) {
        double by = frame.min.y;
        for (size_t i = 0; i + 1 < rowHeights.size(); ++i) {
            by += rowHeights[i];
            if (std::fabs(p.y - by) <= tolerance) {
                *horizontalBorderIndex = static_cast<int>(i);
                break;
            }
        }
    }
    return true;
}

// ===========================================================================
// TableData
// ===========================================================================
TableCell* TableData::at(uint32_t row, uint32_t column) {
    if (!indexValid(row, column)) return 0;
    return &cells[index(row, column)];
}

const TableCell* TableData::at(uint32_t row, uint32_t column) const {
    if (!indexValid(row, column)) return 0;
    return &cells[index(row, column)];
}

void TableCellRange::normalise() {
    if (row0 > row1) std::swap(row0, row1);
    if (column0 > column1) std::swap(column0, column1);
}

bool TableCellRange::contains(uint32_t row, uint32_t column) const {
    return row >= row0 && row <= row1 && column >= column0 && column <= column1;
}

// ===========================================================================
// Embed target
// ===========================================================================
const char* NullTableCellEmbedTarget::name() const { return "none"; }
bool NullTableCellEmbedTarget::supported() const { return false; }

core::Error NullTableCellEmbedTarget::createEmbedded(core::Document&, const core::Box&,
                                                     EmbedRef*) {
    return core::Error(core::ErrorCode::Unsupported,
                       "No structured-content target is installed; a table cell can hold text only."
                       " A bitmap of a structure is not accepted as an editable molecule.");
}

core::Error NullTableCellEmbedTarget::placeEmbedded(core::Document&, const EmbedRef&,
                                                    const core::Box&) {
    return core::Error(core::ErrorCode::Unsupported, "No structured-content target is installed");
}

core::Error NullTableCellEmbedTarget::describeEmbedded(const core::Document&, const EmbedRef&,
                                                       std::string*) const {
    return core::Error(core::ErrorCode::Unsupported, "No structured-content target is installed");
}

// ===========================================================================
// Reads / writes
// ===========================================================================
core::Error readTable(const core::Document& document, core::ObjectId id,
                      TableData* out, bool* usedExtension) {
    if (usedExtension) *usedExtension = false;
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    const core::TableRecord* rec = document.tables().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "No live table with that id");
    const core::ExtensionBag& bag = document.extensions(core::ObjectRef(core::ObjectKind::Table, id));
    const std::string* text = bag.findValue(kTableParamsKey);
    if (text) {
        bool ok = false;
        const TableData decoded = decodeTableParams(*text, &ok);
        if (!ok)
            return core::Error(core::ErrorCode::ParseError,
                               "The table parameter record is malformed; refusing to guess the grid");
        *out = decoded;
        // Native selection transforms update the core frame. Reconcile the
        // authoritative grid so moving/scaling a table keeps its borders and
        // subsequent edits at the same coordinates as the cell text.
        if(core::length(out->grid.frame.min-rec->frame.min)>1e-8||core::length(out->grid.frame.max-rec->frame.max)>1e-8){
            double oldWidth=out->grid.totalWidth(),oldHeight=out->grid.totalHeight();
            double sx=oldWidth>0?(rec->frame.max.x-rec->frame.min.x)/oldWidth:1;
            double sy=oldHeight>0?(rec->frame.max.y-rec->frame.min.y)/oldHeight:1;
            if(sx>0&&sy>0&&std::isfinite(sx)&&std::isfinite(sy)){
                for(auto& width:out->grid.columnWidths)width*=sx;for(auto& height:out->grid.rowHeights)height*=sy;
                for(auto& cell:out->cells){cell.textOffset.x*=sx;cell.textOffset.y*=sy;}out->grid.frame=rec->frame;
            }
        }
        if (usedExtension) *usedExtension = true;
        return core::Error::success();
    }
    *out = fromRecord(*rec);
    return core::Error::success();
}

core::Error createTable(core::Document& document, const TableGrid& grid, core::ObjectId* outId) {
    if (grid.rows() == 0 || grid.columns() == 0)
        return core::Error(core::ErrorCode::InvalidArgument, "A table needs at least one row and column");
    TableData d;
    d.grid = grid;
    for (size_t i = 0; i < d.grid.rowHeights.size(); ++i)
        if (!(d.grid.rowHeights[i] > 0.0))
            return core::Error(core::ErrorCode::InvalidGeometry, "Row height must be positive");
    for (size_t i = 0; i < d.grid.columnWidths.size(); ++i)
        if (!(d.grid.columnWidths[i] > 0.0))
            return core::Error(core::ErrorCode::InvalidGeometry, "Column width must be positive");
    d.grid.frame = core::Box(grid.frame.min,
                             core::Vec2(grid.frame.min.x + d.grid.totalWidth(),
                                        grid.frame.min.y + d.grid.totalHeight()));
    d.cells.resize(static_cast<size_t>(d.grid.rows()) * d.grid.columns());

    core::TableRecord rec;
    rec.frame = d.grid.frame;
    rec.rows = d.grid.rows();
    rec.columns = d.grid.columns();
    rec.rowHeight = d.grid.rowHeights[0];
    rec.cells.assign(d.cells.size(), core::kInvalidObjectId);
    core::ObjectId id = core::kInvalidObjectId;
    core::Error e = document.addTable(rec, &id);
    if (!e.ok()) return e;
    document.extensions(core::ObjectRef(core::ObjectKind::Table, id))
            .set(kTableParamsKey, encodeTableParams(d));
    core::LineStyle style;
    notifyBagDirty(document, id);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeTable(core::Document& document, core::ObjectId id, const TableData& data) {
    core::LineStyle style;
    return commitTable(document, id, data, style);
}

// ===========================================================================
// Row / column editing
// ===========================================================================
namespace {

// Insert an element into a vector at `at` (clamped).
template <typename T>
void insertAt(std::vector<T>& v, size_t at, const T& value) {
    if (at > v.size()) at = v.size();
    v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), value);
}

void reindexAfterRowInsert(TableData* d, uint32_t beforeRow, double height) {
    // Size the destination from the OLD row count + 1. Reading d->grid.rows()
    // after the insert would be too late, and sizing from the old count without
    // the +1 makes cells.size() != rows*columns, which commitTable() correctly
    // refuses. This was a real defect: every insertRow returned
    // "Cell array does not match the grid".
    const uint32_t rows = d->grid.rows();
    const uint32_t cols = d->grid.columns();
    std::vector<TableCell> fresh(static_cast<size_t>(rows + 1u) * cols);
    for (uint32_t r = 0; r < rows; ++r) {
        const uint32_t dst = (r < beforeRow) ? r : r + 1u;
        for (uint32_t c = 0; c < cols; ++c)
            fresh[static_cast<size_t>(dst) * cols + c] = *d->at(r, c);
    }
    d->cells.swap(fresh);
    insertAt(d->grid.rowHeights, beforeRow, height);
}

void reindexAfterColumnInsert(TableData* d, uint32_t beforeColumn, double width) {
    const uint32_t rows = d->grid.rows();
    const uint32_t oldCols = d->grid.columns();
    std::vector<TableCell> fresh(static_cast<size_t>(rows) * (oldCols + 1u));
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < oldCols; ++c) {
            const uint32_t dst = (c < beforeColumn) ? c : c + 1u;
            fresh[static_cast<size_t>(r) * (oldCols + 1u) + dst] = *d->at(r, c);
        }
    }
    d->cells.swap(fresh);
    insertAt(d->grid.columnWidths, beforeColumn, width);
}

} // namespace

core::Error insertRow(core::Document& document, core::ObjectId id, uint32_t beforeRow,
                      double height) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (beforeRow > d.grid.rows())
        return core::Error(core::ErrorCode::OutOfRange, "Row index out of range");
    if (!(height > 0.0)) height = d.grid.rowHeights.empty() ? 12.0 : d.grid.rowHeights[0];
    reindexAfterRowInsert(&d, beforeRow, height);
    for(auto& m:d.grid.merged){if(beforeRow<=m.row)++m.row;else if(beforeRow<m.row+m.rowSpan)++m.rowSpan;}
    d.grid.frame.max.y = d.grid.frame.min.y + d.grid.totalHeight();
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error insertColumn(core::Document& document, core::ObjectId id, uint32_t beforeColumn,
                         double width) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (beforeColumn > d.grid.columns())
        return core::Error(core::ErrorCode::OutOfRange, "Column index out of range");
    if (!(width > 0.0)) width = d.grid.columnWidths.empty() ? 24.0 : d.grid.columnWidths[0];
    reindexAfterColumnInsert(&d, beforeColumn, width);
    for(auto& m:d.grid.merged){if(beforeColumn<=m.column)++m.column;else if(beforeColumn<m.column+m.columnSpan)++m.columnSpan;}
    d.grid.frame.max.x = d.grid.frame.min.x + d.grid.totalWidth();
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error deleteRow(core::Document& document, core::ObjectId id, uint32_t row) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (row >= d.grid.rows())
        return core::Error(core::ErrorCode::OutOfRange, "Row index out of range");
    if (d.grid.rows() <= 1u)
        return core::Error(core::ErrorCode::InvalidArgument,
                           "A table must keep at least one row; delete the table instead");
    // Destroy any cell content the removed row owned. A Text object is a real
    // document object, so deleting its row must delete it, not orphan it.
    for (uint32_t c = 0; c < d.grid.columns(); ++c) {
        const TableCell* cell = d.at(row, c);
        if (cell && cell->kind == CellKind::Text && cell->text != core::kInvalidObjectId) {
            core::Error ignored = document.removeObject(core::ObjectRef(core::ObjectKind::Text, cell->text));
            (void)ignored;
        }
    }
    std::vector<double> rh;
    std::vector<TableCell> fresh;
    for (uint32_t r = 0; r < d.grid.rows(); ++r) {
        if (r == row) continue;
        rh.push_back(d.grid.rowHeights[r]);
        for (uint32_t c = 0; c < d.grid.columns(); ++c) fresh.push_back(*d.at(r, c));
    }
    for(auto& m:d.grid.merged){if(row<m.row)--m.row;else if(row<m.row+m.rowSpan)--m.rowSpan;}
    d.grid.merged.erase(std::remove_if(d.grid.merged.begin(),d.grid.merged.end(),[](const MergedRegion& m){return !m.rowSpan||!m.columnSpan||(m.rowSpan==1&&m.columnSpan==1);}),d.grid.merged.end());
    d.grid.rowHeights.swap(rh);
    d.cells.swap(fresh);
    d.grid.frame.max.y = d.grid.frame.min.y + d.grid.totalHeight();
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error deleteColumn(core::Document& document, core::ObjectId id, uint32_t column) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (column >= d.grid.columns())
        return core::Error(core::ErrorCode::OutOfRange, "Column index out of range");
    if (d.grid.columns() <= 1u)
        return core::Error(core::ErrorCode::InvalidArgument,
                           "A table must keep at least one column; delete the table instead");
    for (uint32_t r = 0; r < d.grid.rows(); ++r) {
        const TableCell* cell = d.at(r, column);
        if (cell && cell->kind == CellKind::Text && cell->text != core::kInvalidObjectId) {
            core::Error ignored = document.removeObject(core::ObjectRef(core::ObjectKind::Text, cell->text));
            (void)ignored;
        }
    }
    std::vector<double> cw;
    std::vector<TableCell> fresh;
    for (uint32_t c = 0; c < d.grid.columns(); ++c) {
        if (c == column) continue;
        cw.push_back(d.grid.columnWidths[c]);
    }
    for (uint32_t r = 0; r < d.grid.rows(); ++r)
        for (uint32_t c = 0; c < d.grid.columns(); ++c) {
            if (c == column) continue;
            fresh.push_back(*d.at(r, c));
        }
    for(auto& m:d.grid.merged){if(column<m.column)--m.column;else if(column<m.column+m.columnSpan)--m.columnSpan;}
    d.grid.merged.erase(std::remove_if(d.grid.merged.begin(),d.grid.merged.end(),[](const MergedRegion& m){return !m.rowSpan||!m.columnSpan||(m.rowSpan==1&&m.columnSpan==1);}),d.grid.merged.end());
    d.grid.columnWidths.swap(cw);
    d.cells.swap(fresh);
    d.grid.frame.max.x = d.grid.frame.min.x + d.grid.totalWidth();
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error resizeRow(core::Document& document, core::ObjectId id, uint32_t row,
                      double delta, double minimum) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (row >= d.grid.rows())
        return core::Error(core::ErrorCode::OutOfRange, "Row index out of range");
    if (!std::isfinite(delta))
        return core::Error(core::ErrorCode::InvalidArgument, "Delta must be finite");
    const double minH = minimum > 0.0 ? minimum : kMinCell;
    d.grid.rowHeights[row] = std::max(minH, d.grid.rowHeights[row] + delta);
    d.grid.frame.max.y = d.grid.frame.min.y + d.grid.totalHeight();
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error resizeColumn(core::Document& document, core::ObjectId id, uint32_t column,
                         double delta, double minimum) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (column >= d.grid.columns())
        return core::Error(core::ErrorCode::OutOfRange, "Column index out of range");
    if (!std::isfinite(delta))
        return core::Error(core::ErrorCode::InvalidArgument, "Delta must be finite");
    const double minW = minimum > 0.0 ? minimum : kMinCell;
    d.grid.columnWidths[column] = std::max(minW, d.grid.columnWidths[column] + delta);
    d.grid.frame.max.x = d.grid.frame.min.x + d.grid.totalWidth();
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error setTableBorders(core::Document& document, core::ObjectId id,
                            const TableBorders& borders) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    d.grid.borders = borders;
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error setTableTitle(core::Document& document, core::ObjectId id,
                          const std::string& title) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    // The title is hex-escaped into the record, so it may contain anything a
    // text object may contain; validate it as UTF-8 text anyway.
    core::Error te = core::validateText(title, true);
    if (!te.ok()) return te;
    d.grid.title = title;
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error setTableBorderWidth(core::Document& document, core::ObjectId id, double width) {
    if (!(width >= 0.0) || !std::isfinite(width))
        return core::Error(core::ErrorCode::InvalidArgument, "Border width must be finite and >= 0");
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    d.grid.borderWidth = width;
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error mergeCells(core::Document& document, core::ObjectId id, uint32_t row,
                       uint32_t column, uint32_t rowSpan, uint32_t columnSpan) {
    TableData d;bool used=false;auto e=readTable(document,id,&d,&used);if(!e.ok())return e;
    if(!rowSpan||!columnSpan||row>=d.grid.rows()||column>=d.grid.columns()||rowSpan>d.grid.rows()-row||columnSpan>d.grid.columns()-column)
        return core::Error(core::ErrorCode::OutOfRange,"合并范围超出表格");
    if(rowSpan==1&&columnSpan==1)return core::Error(core::ErrorCode::InvalidArgument,"请选择至少两个单元格");
    for(const auto& m:d.grid.merged)if(row<m.row+m.rowSpan&&row+rowSpan>m.row&&column<m.column+m.columnSpan&&column+columnSpan>m.column)
        return core::Error(core::ErrorCode::InvalidArgument,"请先拆分范围内已有的合并单元格");
    std::string text;
    for(uint32_t r=row;r<row+rowSpan;++r)for(uint32_t c=column;c<column+columnSpan;++c){
        auto cell=d.at(r,c);if(cell->kind==CellKind::Embedded)return core::Error(core::ErrorCode::Unsupported,"含嵌入结构的单元格不能合并");
        if(cell->kind==CellKind::Text){auto value=document.plainText(cell->text);if(!value.empty()){if(!text.empty())text+="\n";text+=value;}}
    }
    for(uint32_t r=row;r<row+rowSpan;++r)for(uint32_t c=column;c<column+columnSpan;++c)if(r!=row||c!=column){e=clearCell(document,id,r,c);if(!e.ok())return e;}
    e=setCellText(document,id,row,column,text,0);if(!e.ok())return e;
    e=readTable(document,id,&d,&used);if(!e.ok())return e;
    MergedRegion m;m.row=row;m.column=column;m.rowSpan=rowSpan;m.columnSpan=columnSpan;d.grid.merged.push_back(m);
    return writeTable(document,id,d);
}

core::Error splitCell(core::Document& document, core::ObjectId id, uint32_t row, uint32_t column) {
    TableData d;bool used=false;auto e=readTable(document,id,&d,&used);if(!e.ok())return e;
    auto& regions=d.grid.merged;auto old=regions.size();
    regions.erase(std::remove_if(regions.begin(),regions.end(),[row,column](const MergedRegion& m){return row>=m.row&&row<m.row+m.rowSpan&&column>=m.column&&column<m.column+m.columnSpan;}),regions.end());
    if(old==regions.size())return core::Error(core::ErrorCode::InvalidArgument,"当前单元格未合并");
    return writeTable(document,id,d);
}
MergeSupport tableMergeSupport(){return MergeSupport::Supported;}
const char* tableMergeSupportText(){return "支持矩形合并；拆分后文字保留在左上单元格";}

// ===========================================================================
// Cell content
// ===========================================================================
namespace {

core::Error setCellTextImpl(core::Document& document, core::ObjectId id, uint32_t row,
                            uint32_t column, const std::string& utf8, double textSize) {
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (!d.indexValid(row, column))
        return core::Error(core::ErrorCode::OutOfRange, "Cell index out of range");
    TableCell& cell = d.cells[d.index(row, column)];

    if (utf8.empty()) {
        if (cell.kind == CellKind::Text && cell.text != core::kInvalidObjectId) {
            core::Error ignored = document.removeObject(core::ObjectRef(core::ObjectKind::Text, cell.text));
            (void)ignored;
        }
        cell = TableCell();
        core::LineStyle style;
        return commitTable(document, id, d, style);
    }

    core::Error te = core::validateText(utf8, false);
    if (!te.ok()) return te;

    // An Embedded cell being overwritten with text: the embedded object belongs
    // to the injector, so dropping the reference is the table owner's business.
    if (cell.kind == CellKind::Embedded) cell = TableCell();

    const core::Box box = d.grid.cellBox(row, column);
    const core::Vec2 centre((box.min.x + box.max.x) * 0.5, (box.min.y + box.max.y) * 0.5);
    if (cell.kind == CellKind::Text && cell.text != core::kInvalidObjectId) {
        core::TextRecord* t = document.texts().mutableGet(cell.text);
        if (t && t->isAlive()) {
            core::Error se = document.setTextPlain(cell.text, utf8);
            if (!se.ok()) return se;
            if (textSize > 0.0) {
                core::Error sz = document.setTextSize(cell.text, textSize);
                if (!sz.ok()) return sz;
            }
        } else {
            cell.kind = CellKind::Empty;
            cell.text = core::kInvalidObjectId;
        }
    }
    if (cell.kind != CellKind::Text || cell.text == core::kInvalidObjectId) {
        core::ObjectId tid = core::kInvalidObjectId;
        core::Error ce = document.addText(centre, textSize, utf8, &tid);
        if (!ce.ok()) return ce;
        core::TextRecord* t = document.texts().mutableGet(tid);
        if (t) { t->role = core::TextRole::TableCell; t->format.align = static_cast<uint8_t>(core::TextAlign::Center); }
        cell.kind = CellKind::Text;
        cell.text = tid;
    }
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

} // namespace

core::Error setCellText(core::Document& document, core::ObjectId id, uint32_t row,
                        uint32_t column, const std::string& utf8, double textSize) {
    return setCellTextImpl(document, id, row, column, utf8, textSize);
}

core::Error cellText(const core::Document& document, core::ObjectId id, uint32_t row,
                     uint32_t column, std::string* out, CellKind* kind) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (!d.indexValid(row, column))
        return core::Error(core::ErrorCode::OutOfRange, "Cell index out of range");
    const TableCell& cell = d.cells[d.index(row, column)];
    if (kind) *kind = cell.kind;
    out->clear();
    if (cell.kind == CellKind::Text && cell.text != core::kInvalidObjectId)
        *out = document.plainText(cell.text);
    return core::Error::success();
}

core::Error setCellEmbedded(core::Document& document, core::ObjectId id, uint32_t row,
                            uint32_t column, const EmbedRef& ref) {
    if (!ref.valid())
        return core::Error(core::ErrorCode::InvalidArgument, "Embedded reference is not valid");
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    if (!d.indexValid(row, column))
        return core::Error(core::ErrorCode::OutOfRange, "Cell index out of range");
    TableCell& cell = d.cells[d.index(row, column)];
    if (cell.kind == CellKind::Text && cell.text != core::kInvalidObjectId) {
        core::Error ignored = document.removeObject(core::ObjectRef(core::ObjectKind::Text, cell.text));
        (void)ignored;
    }
    cell = TableCell();
    cell.kind = CellKind::Embedded;
    cell.embed = ref;
    core::LineStyle style;
    return commitTable(document, id, d, style);
}

core::Error clearCell(core::Document& document, core::ObjectId id, uint32_t row,
                      uint32_t column) {
    return setCellText(document, id, row, column, std::string(), 0.0);
}

// ===========================================================================
// Hit test and bounds
// ===========================================================================
bool tableHitTest(const core::Document& document, core::ObjectId id, const core::Vec2& point,
                  double tolerance) {
    TableData d;
    bool used = false;
    if (!readTable(document, id, &d, &used).ok()) return false;
    if (tolerance < 0.0) tolerance = 0.0;
    // A table's cell interiors ARE part of it (unlike a hollow shape), so an
    // inside click selects it and the grid lines add the tolerance band.
    uint32_t r = 0, c = 0;
    if (d.grid.cellAt(point, &r, &c, tolerance, 0, 0)) return true;
    return false;
}

core::Box tableBounds(const core::Document& document, core::ObjectId id,
                      double labelTextSize) {
    TableData d;
    bool used = false;
    if (!readTable(document, id, &d, &used).ok()) return core::Box();
    const double bw = d.grid.borderWidth > 0.0 ? d.grid.borderWidth : 0.6;
    core::Box b = d.grid.frame;
    b = inflateBox(b, bw * 0.5);
    // Cell text and embedded boxes are inside the frame by construction; a cell
    // text offset may push a label out by up to the offset plus half a line.
    for (size_t i = 0; i < d.cells.size(); ++i) {
        const TableCell& cell = d.cells[i];
        if (cell.kind != CellKind::Text) continue;
        const core::Vec2 off = cell.textOffset;
        const double pad = std::max(labelTextSize, 1.0) +
                           std::sqrt(off.x * off.x + off.y * off.y);
        const core::Box cellBox = d.grid.cellBox(static_cast<uint32_t>(i / d.grid.columns()),
                                                 static_cast<uint32_t>(i % d.grid.columns()));
        const core::Box tb(core::Vec2(cellBox.min.x + off.x - pad, cellBox.min.y + off.y - pad),
                           core::Vec2(cellBox.max.x + off.x + pad, cellBox.max.y + off.y + pad));
        b.add(tb.min);
        b.add(tb.max);
    }
    return b;
}

} // namespace graphics
} // namespace r62
