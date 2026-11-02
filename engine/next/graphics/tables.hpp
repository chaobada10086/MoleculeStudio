#ifndef R62_NEXT_GRAPHICS_TABLES_HPP
#define R62_NEXT_GRAPHICS_TABLES_HPP
// =============================================================================
// re_round62_viewer/next/graphics/tables.hpp
//
// T11 - THE EDITABLE TABLE OBJECT.
//
// -----------------------------------------------------------------------------
// WHAT T01 ALREADY GIVES US, AND WHAT IT DOES NOT
// -----------------------------------------------------------------------------
// core::TableRecord (next/core/document.hpp) is:
//
//     ObjectId id; Box frame; uint32 rows, columns;
//     std::vector<ObjectId> cells;   // row-major, kInvalidObjectId allowed
//     double rowHeight; int32 sourceId; uint8 alive;
//
// It is a REGULAR GRID: no column widths, no per-row heights, no borders, no
// cell content type, no merged regions. Those live in the object extension bag
// under "gfx.table.params.1", exactly as T10 stores the arrow parameters it needs
// beyond AnnotationRecord and for the same reasons (part of the digest, part of
// RawState, therefore part of undo and of save/reload).
//
// The core record stays VALID and MEANINGFUL at all times:
//   * rows/columns == the grid size;
//   * cells[] has exactly rows*columns entries, each the ObjectId of the
//     TextRole::TableCell Text object that holds that cell's text (or
//     kInvalidObjectId for an empty cell);
//   * rowHeight == the first row height (a conservative single-height mirror);
//   * frame == the whole table box.
// A consumer that knows nothing about T11 still finds a usable table.
//
// -----------------------------------------------------------------------------
// MERGED / SPANNING CELLS: NOT SUPPORTED IN V1, AND SAID SO OUT LOUD
// -----------------------------------------------------------------------------
// The task requires an explicit statement. The answer is:
//
//   **v1 does NOT support merged or spanning cells.**
//
// * merge_cells()/split_cell() exist, have a defined signature, and return
//   core::ErrorCode::Unsupported with a specific message. There is no code path
//   that silently produces a rectangular-only table while claiming a merge.
// * tableMergeSupport() reports the status programmatically so a UI can disable
//   the command instead of showing a broken one.
// * The PERSISTENCE FORMAT reserves a "merged" list in the record, so a v2 that
//   implements merging does not need a format version bump. The decoder reads it
//   and the encoder writes it; only the editing operation is missing.
//
// -----------------------------------------------------------------------------
// STRUCTURED CONTENT INSIDE A CELL: THE EMBED SEAM
// -----------------------------------------------------------------------------
// The task says: "structure content in a cell is admitted through an embedded
// object interface; a bitmap of a structure is NOT an editable molecule."
//
// So a table cell holds either
//   * TEXT       -> a core::Text object with role TextRole::TableCell, or
//   * EMBEDDED   -> an opaque EmbedRef { kind, id } owned by an injected
//                   ITableCellEmbedTarget (the T13 chemistry model plugs in here).
//
// This module never interprets the embedded object. It does not create atoms,
// does not read valences, and does not accept an image as a structure: the only
// way to put anything but text in a cell is through the injected target, which
// returns a REAL object identity.
// =============================================================================

#include "next/core/document.hpp"
#include "next/graphics/preset.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace graphics {

// ---------------------------------------------------------------------------
// Geometry of a regular grid
// ---------------------------------------------------------------------------
enum class TableBorderStyle : uint8_t {
    None = 0,
    Thin,
    Thick,
    Dashed
};

// Which edges of the whole table are drawn / which internal grid lines are drawn.
struct TableBorders {
    TableBorderStyle outer;
    TableBorderStyle inner;
    TableBorders() : outer(TableBorderStyle::Thin), inner(TableBorderStyle::Thin) {}
    bool operator==(const TableBorders& o) const {
        return outer == o.outer && inner == o.inner;
    }
    bool operator!=(const TableBorders& o) const { return !(*this == o); }
};

// A merged region. RESERVED for v2; the editor never creates one yet.
struct MergedRegion {
    uint32_t row, column, rowSpan, columnSpan;
    MergedRegion() : row(0), column(0), rowSpan(1), columnSpan(1) {}
};

// The authoritative grid: fixed row heights and column widths in DOCUMENT units,
// measured from the table frame's min corner. Keeping explicit positions instead
// of "equal division" is what makes a drag of an internal border exact and makes
// reload reproduce the same table.
struct TableGrid {
    core::Box                 frame;
    std::vector<double>       rowHeights;     // size == rows, each > 0
    std::vector<double>       columnWidths;   // size == columns, each > 0
    TableBorders              borders;
    std::vector<MergedRegion> merged;         // reserved; always empty in v1
    std::string               title;
    double                    borderWidth;    // document units, 0 == default
    TableGrid() : borderWidth(0.0) {}
    uint32_t rows() const { return static_cast<uint32_t>(rowHeights.size()); }
    uint32_t columns() const { return static_cast<uint32_t>(columnWidths.size()); }
    double totalHeight() const;
    double totalWidth() const;
    // Top-left corner of cell (r,c) in document space. Out-of-range indices are
    // clamped, never UB.
    core::Vec2 cellOrigin(uint32_t row, uint32_t column) const;
    core::Box  cellBox(uint32_t row, uint32_t column) const;
    // Which cell contains a document point? Returns false for a point outside the
    // frame. `onVerticalBorder` / `onHorizontalBorder` report proximity to an
    // internal border, which is what the resize gesture needs.
    bool cellAt(const core::Vec2& point, uint32_t* row, uint32_t* column) const;
    bool cellAt(const core::Vec2& point, uint32_t* row, uint32_t* column,
                double tolerance, int* verticalBorderIndex, int* horizontalBorderIndex) const;
};

// ---------------------------------------------------------------------------
// Cell content
// ---------------------------------------------------------------------------
// An opaque reference to an object owned by the injector. `id` is a
// core::ObjectId when the embed is a document object; an injector that keeps its
// content elsewhere uses its own numbering and says so through its kind.
struct EmbedRef {
    core::ObjectKind kind;
    core::ObjectId   id;
    EmbedRef() : kind(core::ObjectKind::Unknown), id(core::kInvalidObjectId) {}
    EmbedRef(core::ObjectKind k, core::ObjectId i) : kind(k), id(i) {}
    bool valid() const { return kind != core::ObjectKind::Unknown && id != core::kInvalidObjectId; }
    bool operator==(const EmbedRef& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const EmbedRef& o) const { return !(*this == o); }
};

enum class CellKind : uint8_t {
    Empty = 0,
    Text,
    Embedded
};

// The embed seam. Implemented by whatever owns structured content; the table
// module only distinguishes "text" from "an object somebody else owns".
//
// CONTRACT: create() must return a ref to a REAL object. An implementation that
// wants to place a picture must return ErrorCode::Unsupported, because a bitmap
// is not an editable molecule. That is enforced here, not only documented.
class ITableCellEmbedTarget {
public:
    virtual ~ITableCellEmbedTarget() {}
    // Human name for status output and tests ("none", "document", "t13").
    virtual const char* name() const = 0;
    // True when this target can put a structured object in a cell at all.
    virtual bool supported() const = 0;
    // Create a structured object centred in `box`. On success *out receives its
    // identity. On failure the document must be unchanged.
    virtual core::Error createEmbedded(core::Document& document, const core::Box& box,
                                       EmbedRef* out) = 0;
    // Report the current placement box of an embedded object, so a table resize
    // can decide whether it needs rescaling. Not owning the object, the target
    // also performs the move.
    virtual core::Error placeEmbedded(core::Document& document, const EmbedRef& ref,
                                      const core::Box& box) = 0;
    virtual core::Error describeEmbedded(const core::Document& document, const EmbedRef& ref,
                                         std::string* out) const = 0;
};

// The "no structured content available" target. Honest and usable: a table with
// text cells works with it, and every attempt to insert a structure reports
// Unsupported instead of quietly inserting an image.
class NullTableCellEmbedTarget : public ITableCellEmbedTarget {
public:
    const char* name() const;
    bool supported() const;
    core::Error createEmbedded(core::Document&, const core::Box&, EmbedRef*);
    core::Error placeEmbedded(core::Document&, const EmbedRef&, const core::Box&);
    core::Error describeEmbedded(const core::Document&, const EmbedRef&, std::string*) const;
};

// ---------------------------------------------------------------------------
// Table data: grid + cells, the object T11 reads and writes
// ---------------------------------------------------------------------------
struct TableCell {
    CellKind    kind;
    EmbedRef    embed;      // valid when kind == Embedded
    core::ObjectId text;    // valid when kind == Text
    core::Vec2  textOffset;// offset from the cell centre, document units
    TableCell() : kind(CellKind::Empty), text(core::kInvalidObjectId), textOffset(0, 0) {}
};

struct TableData {
    TableGrid              grid;
    std::vector<TableCell> cells;   // row-major, size == rows*columns
    TableData() {}
    bool indexValid(uint32_t row, uint32_t column) const {
        return row < grid.rows() && column < grid.columns();
    }
    size_t index(uint32_t row, uint32_t column) const {
        return static_cast<size_t>(row) * grid.columns() + column;
    }
    TableCell* at(uint32_t row, uint32_t column);
    const TableCell* at(uint32_t row, uint32_t column) const;
};

// ---------------------------------------------------------------------------
// Parameter record codec (versioned text, stored in the object's extension bag)
// ---------------------------------------------------------------------------
// Public on purpose: a persistence or clipboard layer needs to carry a table's
// parameters without linking against the model, and a reader needs to tell a
// MALFORMED record (ok = false) apart from a MISSING one (no record at all).
// encodeTableParams and decodeTableParams are exact inverses for every field a
// v1 table has; see decodeTableParams for the required-key rules.
std::string encodeTableParams(const TableData& data);
TableData   decodeTableParams(const std::string& text, bool* ok);

// ---------------------------------------------------------------------------
// Reads / writes
// ---------------------------------------------------------------------------
core::Error readTable(const core::Document& document, core::ObjectId id,
                      TableData* out, bool* usedExtension);
// Create a table. `outCells` (optional) receives the created Text ids in
// row-major order. Every cell starts Empty.
core::Error createTable(core::Document& document, const TableGrid& grid,
                        core::ObjectId* outId);
core::Error writeTable(core::Document& document, core::ObjectId id,
                       const TableData& data);

// ---------------------------------------------------------------------------
// Row / column editing (primitives - wrap in ONE core::Command)
// ---------------------------------------------------------------------------
core::Error insertRow(core::Document& document, core::ObjectId id, uint32_t beforeRow,
                      double height);
core::Error insertColumn(core::Document& document, core::ObjectId id, uint32_t beforeColumn,
                         double width);
core::Error deleteRow(core::Document& document, core::ObjectId id, uint32_t row);
core::Error deleteColumn(core::Document& document, core::ObjectId id, uint32_t column);
// Apply a DELTA to one row height / column width. Deltas, not absolutes: this is
// what a drag produces, and it keeps the operation exact across a zoom change.
core::Error resizeRow(core::Document& document, core::ObjectId id, uint32_t row,
                      double delta, double minimum);
core::Error resizeColumn(core::Document& document, core::ObjectId id, uint32_t column,
                         double delta, double minimum);
core::Error setTableBorders(core::Document& document, core::ObjectId id,
                            const TableBorders& borders);
core::Error setTableTitle(core::Document& document, core::ObjectId id,
                          const std::string& title);
core::Error setTableBorderWidth(core::Document& document, core::ObjectId id, double width);

// Merging: defined, and deliberately not implemented. See the header comment.
core::Error mergeCells(core::Document& document, core::ObjectId id, uint32_t row,
                       uint32_t column, uint32_t rowSpan, uint32_t columnSpan);
core::Error splitCell(core::Document& document, core::ObjectId id, uint32_t row,
                      uint32_t column);
enum class MergeSupport : uint8_t { Unsupported = 0, Supported };
MergeSupport tableMergeSupport();
const char*  tableMergeSupportText();

// ---------------------------------------------------------------------------
// Cell content
// ---------------------------------------------------------------------------
// Set the text of a cell. Creates the TextRole::TableCell Text object on demand,
// reuses the existing one otherwise, and destroys it when the text becomes empty.
core::Error setCellText(core::Document& document, core::ObjectId id, uint32_t row,
                        uint32_t column, const std::string& utf8, double textSize);
// Read a cell's text. An Empty or Embedded cell returns an empty string with a
// successful error, and *kind reports which it was - the caller must be able to
// tell "empty" from "holds a structure".
core::Error cellText(const core::Document& document, core::ObjectId id, uint32_t row,
                     uint32_t column, std::string* out, CellKind* kind);
core::Error setCellEmbedded(core::Document& document, core::ObjectId id, uint32_t row,
                            uint32_t column, const EmbedRef& ref);
core::Error clearCell(core::Document& document, core::ObjectId id, uint32_t row,
                      uint32_t column);

// ---------------------------------------------------------------------------
// Selection inside a table
// ---------------------------------------------------------------------------
// Selection is VIEW STATE, exactly like the document selection, so it is not
// stored in the document and not restored by undo. This struct is a value the
// editor owns.
struct TableCellRange {
    core::ObjectId table;
    uint32_t row0, column0, row1, column1;   // inclusive, normalised
    TableCellRange() : table(core::kInvalidObjectId), row0(0), column0(0), row1(0), column1(0) {}
    TableCellRange(core::ObjectId t, uint32_t r0, uint32_t c0, uint32_t r1, uint32_t c1)
        : table(t), row0(r0), column0(c0), row1(r1), column1(c1) {
        normalise();
    }
    void normalise();
    bool contains(uint32_t row, uint32_t column) const;
    uint32_t rowCount() const { return row1 - row0 + 1; }
    uint32_t columnCount() const { return column1 - column0 + 1; }
    bool valid() const { return table != core::kInvalidObjectId; }
};

// ---------------------------------------------------------------------------
// Geometry-derived helpers used by hit testing and by the render bridge
// ---------------------------------------------------------------------------
// Distance test against the whole table: the table is a FILLED region (the cell
// interiors are part of it), so a click anywhere inside selects it; the outer
// frame band and the internal grid lines add the usual tolerance band.
bool tableHitTest(const core::Document& document, core::ObjectId id, const core::Vec2& point,
                  double tolerance);
core::Box tableBounds(const core::Document& document, core::ObjectId id,
                      double labelTextSize);

} // namespace graphics
} // namespace r62

#endif // R62_NEXT_GRAPHICS_TABLES_HPP
