#ifndef R62_NEXT_CORE_DOCUMENT_HPP
#define R62_NEXT_CORE_DOCUMENT_HPP
// =============================================================================
// re_round62_viewer/next/core/document.hpp
//
// T01 - the shared document model.
//
// DESIGN NOTES THAT DOWNSTREAM MODULES DEPEND ON
// -----------------------------------------------------------------------------
// * Slab storage, not node-per-heap-object. Every object class has its own
//   std::vector slab. ObjectId IS the slab index. This keeps 50,000 atoms in a
//   handful of contiguous allocations, keeps the renderer's per-frame iteration
//   pointer-chase free, and makes "snapshot the whole document" cheap enough to
//   forbid instead of rely on.
// * Deletion is alive=false. Slots are never reused, never compacted, never
//   renumbered. A deleted id stays a valid-but-dead reference forever, so a
//   stale selection or a stale host binding resolves to "dead" rather than to
//   "some other object".
// * Duplicate-bond detection uses a sparse map. A dense N*N adjacency matrix was
//   rejected after measurement at 50,000 nodes (see RESULTS.md).
// * WITHIN a slab, records form a maximal contiguous run per modification, so a
//   ChangeSet is a list of (offset, vector<record>) spans - not a record list.
// * Every mutator is a primitive. Composite user actions belong in
//   commands.hpp, which wraps primitives in a Transaction. A mutator used
//   outside a transaction does NOT participate in undo.
// =============================================================================

#include "contracts.hpp"

#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace r62 {
namespace core {

// ---------------------------------------------------------------------------
// Record types
// ---------------------------------------------------------------------------

// Layout rule for every record: fields are ordered and padded so that NO byte of
// the object is left uninitialised. The document digest hashes the raw record
// bytes, so an uninitialised padding byte would make the digest depend on stack
// garbage - which is exactly what a "did the rollback restore the exact state?"
// check must not be allowed to be fooled by.
struct AtomRecord {
    ObjectId       id;
    Vec2           position;
    int16_t        element;     // 1..118
    uint8_t        alive;
    uint8_t        reserved0;
    AtomSemantics  semantics;
    int32_t        sourceId;    // CDXML id, 0 if not from CDXML
    uint32_t       reserved1;
    AtomRecord()
        : id(kInvalidObjectId), element(6), alive(1), reserved0(0),
          sourceId(0), reserved1(0) {}
    bool isAlive() const { return alive != 0; }
    double x() const { return position.x; }
    double y() const { return position.y; }
};

// A bond needs both semantics axes and an explicit "display vs order" pair.
struct BondRecord {
    ObjectId      id;
    ObjectId      beginAtom;
    ObjectId      endAtom;
    BondOrder     order;
    BondDisplay   display;
    BondSemantics semantics;
    BondStereo    stereo;
    int32_t       sourceId;
    uint8_t       alive;
    uint8_t       reserved0;
    uint8_t       reserved[2];
    BondRecord()
        : id(kInvalidObjectId), beginAtom(kInvalidObjectId), endAtom(kInvalidObjectId),
          order(BondOrder::Single), display(BondDisplay::Normal),
          semantics(BondSemantics::Normal), stereo(BondStereo::None),
          sourceId(0), alive(1), reserved0(0) {
        reserved[0] = reserved[1] = 0;
    }
    bool isAlive() const { return alive != 0; }
};

// Rich text. runs() always contains at least one run; run(0).utf8 is the plain
// text and equals plainText(). Keeping both makes the common case (one plain
// run) as cheap as the legacy editor::Text and keeps styled text possible.
struct TextRecord {
    ObjectId    id;
    Vec2        position;
    double      size;           // 0 == document default text size
    TextRole    role;
    uint8_t     alive;
    uint8_t     reserved[2];
    TextFormat  format;
    int32_t     sourceId;
    TextRecord()
        : id(kInvalidObjectId), size(0.0), role(TextRole::Caption), alive(1),
          sourceId(0) {
        reserved[0] = reserved[1] = 0;
    }
    bool isAlive() const { return alive != 0; }
    double x() const { return position.x; }
    double y() const { return position.y; }
};

// Free path: a polyline with optional quadratic control points. Arrowheads,
// glyph shapes and free curves all reduce to this. Not a full Bézier editor.
struct PathRecord {
    ObjectId    id;
    std::vector<Vec2> points;
    std::vector<uint8_t> pointKind;   // 0 = corner, 1 = quadratic control
    bool        closed;
    LineStyle   style;
    uint32_t    fillRGBA;
    uint8_t     fillEnabled;
    int32_t     sourceId;
    uint8_t     alive;
    uint8_t     reserved[2];
    PathRecord()
        : closed(false), fillRGBA(0), fillEnabled(0), sourceId(0), alive(1) {
        reserved[0] = reserved[1] = 0;
    }
    bool isAlive() const { return alive != 0; }
};

// Generic annotation drawn with a shared shape vocabulary: arrows (all 56
// presets), brackets and daggers (8), drawing shapes (32), experimental plates
// (2) and BioDraw presets (89). This is the "bounded number of contents, large
// number of presets" answer from the task statement: ONE slab, one preset id
// namespace, no per-preset struct.
struct AnnotationRecord {
    ObjectId   id;
    ObjectKind kind;            // Arrow / Bracket / BioShape / (Path fallback)
    ObjectId   anchors[4];      // optional attached object refs (kInvalidObjectId)
    Vec2       geometry[4];     // endpoints / control box, meaning per preset
    uint32_t   presetId;        // tool_registry preset index (stable, see registry)
    double     scale;
    double     rotation;        // degrees
    LineStyle  style;
    uint32_t   fillRGBA;
    int32_t    sourceId;
    uint8_t    alive;
    uint8_t    reserved[3];
    AnnotationRecord()
        : kind(ObjectKind::Unknown), presetId(0), scale(1.0), rotation(0.0),
          fillRGBA(0), sourceId(0), alive(1) {
        anchors[0] = anchors[1] = anchors[2] = anchors[3] = kInvalidObjectId;
        reserved[0] = reserved[1] = reserved[2] = 0;
    }
    bool isAlive() const { return alive != 0; }
};

// Table: rows x columns of cell references; each cell is a Text object.
struct TableRecord {
    ObjectId id;
    Box      frame;
    uint32_t rows, columns;
    std::vector<ObjectId> cells;   // row-major, kInvalidObjectId allowed
    double   rowHeight;
    int32_t  sourceId;
    uint8_t  alive;
    uint8_t  reserved[3];
    TableRecord()
        : rows(0), columns(0), rowHeight(0.0), sourceId(0), alive(1) {
        reserved[0] = reserved[1] = reserved[2] = 0;
    }
    bool isAlive() const { return alive != 0; }
};

// Grouping and z-order. Flat list, no nested group tree in v1: a group lists its
// members, zIndex orders draw calls inside the group's parent scope.
struct GroupRecord {
    ObjectId id;
    std::vector<ObjectRef> members;
    int32_t  zIndex;
    std::string label;      // e.g. "Synthesis fragment"
    int32_t  sourceId;
    uint8_t  alive;
    uint8_t  reserved[3];
    GroupRecord() : zIndex(0), sourceId(0), alive(1) {
        reserved[0] = reserved[1] = reserved[2] = 0;
    }
    bool isAlive() const { return alive != 0; }
};

// ---------------------------------------------------------------------------
// ObjectSlab<T>
// ---------------------------------------------------------------------------
// ObjectId == index. push() never reuses a slot. All operations stay O(1)
// amortised except the bounded dead-run scan in push().
template <typename T>
class ObjectSlab {
public:
    ObjectSlab() {}
    const std::vector<T>& records() const { return records_; }
    std::vector<T>& mutableRecords() { return records_; }
    size_t size() const { return records_.size(); }
    bool empty() const { return records_.empty(); }

    const T* get(ObjectId id) const {
        return id < records_.size() ? &records_[idx(id)] : 0;
    }
    const T* alive(ObjectId id) const {
        const T* r = get(id);
        return (r && r->isAlive()) ? r : 0;
    }
    bool exists(ObjectId id) const { return alive(id) != 0; }

    T* mutableGet(ObjectId id) {
        return id < records_.size() ? &records_[idx(id)] : 0;
    }

    // Append an uninitialised-as-far-as-id-goes record. Caller fills fields
    // except id, which is assigned here. Returns the new id.
    ObjectId push(T value) {
        const size_t n = records_.size();

        // Reproduce v1's dead-run reuse behaviour exactly (one probe only) so
        // that migration from CDXEDIT 1 reproduces the same ids.
        if (n > 0 && !records_[n - 1].isAlive()) {
            records_[n - 1] = value;
            records_[n - 1].id = id_of(n - 1);
            return id_of(n - 1);
        }

        // The id that this push WILL produce if no dead slot is consumed.
        const size_t nominalEnd = records_.size() + deadRun_ + 1;
        if (lastReusedId_ != kInvalidObjectId && lastReusedId_ + 1 < nominalEnd) {
            const size_t slot = idx(static_cast<ObjectId>(lastReusedId_ + 1));
            if (slot < records_.size() && !records_[slot].isAlive()) {
                records_[slot] = value;
                records_[slot].id = id_of(slot);
                lastReusedId_ = records_[slot].id;
                deadRun_ -= 1;
                return records_[slot].id;
            }
        }

        value.id = id_of(n);
        records_.push_back(value);
        deadRun_ = 0;
        lastReusedId_ = kInvalidObjectId;
        return value.id;
    }

    // Mark dead. Does not shrink, does not renumber. Records the dead run so a
    // later push can -- as v1 did -- regenerate the same id.
    bool kill(ObjectId id) {
        T* r = mutableGet(id);
        if (!r || !r->isAlive()) return false;
        r->alive = 0;
        if (r == &records_.back()) {
            deadRun_ = 1;
            size_t i = records_.size();
            while (i > 0 && !records_[i - 1].isAlive()) { ++deadRun_; --i; }
            deadRun_ -= 1;
            lastReusedId_ = id - 1 >= static_cast<ObjectId>(deadRun_) ? id : kInvalidObjectId;
            if (deadRun_ == 0) lastReusedId_ = kInvalidObjectId;
        } else if (!r->isAlive()) {
            lastReusedId_ = kInvalidObjectId;
        }
        return true;
    }

    void clear() {
        records_.clear();
        deadRun_ = 0;
        lastReusedId_ = kInvalidObjectId;
    }

    // Rebuild the reuse bookkeeping after restore()/deserialize().
    void rebuildBookkeeping() {
        deadRun_ = 0;
        lastReusedId_ = kInvalidObjectId;
        size_t i = records_.size();
        while (i > 0 && !records_[i - 1].isAlive()) { ++deadRun_; --i; }
        if (deadRun_ > 0) lastReusedId_ = records_[records_.size() - 1].id;
        else lastReusedId_ = kInvalidObjectId;
    }

    // Hand the slab over directly; caller must then call rebuildBookkeeping().
    void adoptRecords(std::vector<T> records) {
        records_.swap(records);
        rebuildBookkeeping();
    }

private:
    static ObjectId id_of(size_t index) { return static_cast<ObjectId>(index); }
    static size_t idx(ObjectId id) { return static_cast<size_t>(id); }

    std::vector<T> records_;
    size_t   deadRun_ = 0;                  // trailing dead records
    ObjectId lastReusedId_ = kInvalidObjectId;
};

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------
class Document {
public:
    Document();
    explicit Document(DocumentId id);

    DocumentId id() const { return documentId_; }
    uint64_t   revision() const { return revision_; }
    // Monotonic counter of structural mutations; the renderer uses it to decide
    // whether a cached mesh is stale without diffing anything.
    uint64_t   structureSerial() const { return structureSerial_; }
    void       bumpStructure() { ++structureSerial_; ++revision_; }

    // ---- document-level defaults (the "root" attributes of a CDXML page) ----
    double defaultBondLength() const { return defaultBondLength_; }
    void   setDefaultBondLength(double v) { defaultBondLength_ = v; bumpStructure(); }
    double defaultLineWidth() const { return defaultLineWidth_; }
    void   setDefaultLineWidth(double v) { defaultLineWidth_ = v; bumpStructure(); }
    double defaultTextSize() const { return defaultTextSize_; }
    void   setDefaultTextSize(double v) { defaultTextSize_ = v; bumpStructure(); }
    const std::string& defaultFont() const { return defaultFont_; }
    void   setDefaultFont(const std::string& v) { defaultFont_ = v; bumpStructure(); }
    const std::string& name() const { return name_; }
    void   setName(const std::string& v) { name_ = v; }
    const std::string& sourcePath() const { return sourcePath_; }
    void   setSourcePath(const std::string& v) { sourcePath_ = v; }

    // ---- slabs ----
    ObjectSlab<AtomRecord>&       atoms()       { return atoms_; }
    ObjectSlab<BondRecord>&       bonds()       { return bonds_; }
    ObjectSlab<TextRecord>&       texts()       { return texts_; }
    ObjectSlab<PathRecord>&       paths()       { return paths_; }
    ObjectSlab<AnnotationRecord>& arrows()      { return annotations_[0]; }
    ObjectSlab<AnnotationRecord>& brackets()    { return annotations_[1]; }
    ObjectSlab<AnnotationRecord>& orbitals()    { return annotations_[2]; }
    ObjectSlab<AnnotationRecord>& symbols()     { return annotations_[3]; }
    ObjectSlab<AnnotationRecord>& bioShapes()   { return annotations_[4]; }
    ObjectSlab<TableRecord>&      tables()      { return tables_; }
    ObjectSlab<GroupRecord>&      groups()      { return groups_; }

    const ObjectSlab<AtomRecord>&       atoms() const { return atoms_; }
    const ObjectSlab<BondRecord>&       bonds() const { return bonds_; }
    const ObjectSlab<TextRecord>&       texts() const { return texts_; }
    const ObjectSlab<PathRecord>&       paths() const { return paths_; }
    const ObjectSlab<AnnotationRecord>& arrows() const { return annotations_[0]; }
    const ObjectSlab<AnnotationRecord>& brackets() const { return annotations_[1]; }
    const ObjectSlab<AnnotationRecord>& orbitals() const { return annotations_[2]; }
    const ObjectSlab<AnnotationRecord>& symbols() const { return annotations_[3]; }
    const ObjectSlab<AnnotationRecord>& bioShapes() const { return annotations_[4]; }
    const ObjectSlab<TableRecord>&      tables() const { return tables_; }
    const ObjectSlab<GroupRecord>&      groups() const { return groups_; }

    // Generic slab access by Slab enum. Returns 0 for an out-of-range slab.
    const void* slabData(Slab slab) const;
    size_t      slabSize(Slab slab) const;

    // ---- extensions ----
    const ExtensionBag& documentExtensions() const { return documentExtensions_; }
    ExtensionBag&       documentExtensions() { return documentExtensions_; }
    const ExtensionBag& extensions(const ObjectRef& ref) const;
    ExtensionBag&       extensions(const ObjectRef& ref);
    size_t              extensionObjectCount() const { return objectExtensions_.size(); }
    const std::map<ObjectRef, ExtensionBag>& allObjectExtensions() const { return objectExtensions_; }
    std::map<ObjectRef, ExtensionBag>& mutableObjectExtensions() { return objectExtensions_; }

    // ---- adjacency ----
    // Atom -> incident bond ids. Maintained on addBond/removeBond. Bond ids are
    // appended in creation order, bonds that were later deleted are skipped by
    // callers via BondRecord::isAlive().
    const std::vector<ObjectId>& incidentBonds(ObjectId atom) const {
        static const std::vector<ObjectId> empty;
        return atom < adjacency_.size() ? adjacency_[atom] : empty;
    }
    // Atom -> neighbour atom ids (each neighbour appears once per connecting bond).
    std::vector<ObjectId> neighbours(ObjectId atom) const;
    // Bond lookup between two atoms, kInvalidObjectId when absent.
    ObjectId bondBetween(ObjectId a, ObjectId b) const;
    // Number of alive objects of every slab; used by tests and status reporting.
    uint64_t aliveObjectCount() const;
    uint64_t totalSlotCount() const;

    // ---- mutation primitives (no undo on their own) ----
    Error addAtom(const Vec2& position, int element, ObjectId* out);
    Error addAtomRecord(const AtomRecord& record, ObjectId* out);

    Error addBond(ObjectId begin, ObjectId end, ObjectId* out);
    Error addBondRecord(const BondRecord& record, ObjectId* out);

    Error addText(const Vec2& position, double size, const std::string& utf8, ObjectId* out);
    Error addTextRecord(const TextRecord& record, ObjectId* out);

    Error addPath(const PathRecord& record, ObjectId* out);
    Error addAnnotation(ObjectKind kind, const AnnotationRecord& record, ObjectId* out);
    Error addTable(const TableRecord& record, ObjectId* out);
    Error addGroup(const GroupRecord& record, ObjectId* out);

    Error removeAtom(ObjectId id);              // cascades to incident bonds
    Error removeBond(ObjectId id);
    Error removeObject(const ObjectRef& ref);   // dispatch by kind

    Error setAtomPosition(ObjectId id, const Vec2& p);
    Error setElement(ObjectId id, int element);
    Error setAtomSemantics(ObjectId id, const AtomSemantics& s);
    Error setAtomLabel(ObjectId id, const std::string& utf8);   // label override

    Error setBondEndpoints(ObjectId id, ObjectId begin, ObjectId end);
    Error setBondOrder(ObjectId id, BondOrder order);
    Error setBondDisplay(ObjectId id, BondDisplay display);
    Error setBondSemantics(ObjectId id, BondSemantics semantics);
    Error setBondStereo(ObjectId id, BondStereo stereo);

    Error setTextPosition(ObjectId id, const Vec2& p);
    Error setTextPlain(ObjectId id, const std::string& utf8);
    Error setTextSize(ObjectId id, double size);
    Error setTextFormat(ObjectId id, const TextFormat& format);
    Error setTextRuns(ObjectId id, const std::vector<TextRun>& runs);

    // Text runs are kept in a side table keyed by id so TextRecord stays POD and
    // cheap to copy. run(0) is the plain text and always exists.
    const std::vector<TextRun>& runs(ObjectId id) const;
    std::string plainText(ObjectId id) const;
    Error       setRuns(ObjectId id, const std::vector<TextRun>& runs);

    // Atom label override (an abbreviation such as Ph). Empty means "draw the
    // element symbol". Stored in a side table so AtomRecord stays POD.
    const std::string& atomLabel(ObjectId id) const;
    void setAtomLabelQuiet(ObjectId id, const std::string& utf8);

    Error setLineStyle(ObjectRef ref, const LineStyle& style);
    Error setAnnotationGeometry(ObjectId id, const Vec2* points, size_t count);

    // Whole-record replacement used by the change-set writer. Returns the record
    // that was present before (so the caller can build the inverse), or an error
    // if the ref is unknown.
    // "Quiet" variants used by ChangeSet::apply and by the rollback path. They
    // skip the touch notices (the change set IS the record of what happened) and
    // skip the per-mutation validate, because the whole document is validated
    // once at the end of the application.
    void  setRunsQuiet(ObjectId id, const std::vector<TextRun>& runs);
    void  setDefaultsQuiet(const Defaults& defaults);

    Error readAtom(ObjectId id, AtomRecord* out) const;
    Error readBond(ObjectId id, BondRecord* out) const;
    Error readText(ObjectId id, TextRecord* out) const;
    Error writeAtom(const AtomRecord& record);
    Error writeBond(const BondRecord& record);
    Error writeText(const TextRecord& record);

    // ---- validation used by restore()/deserialize() and by the command layer --
    Error validate() const;
    Error validateBondReference(const BondRecord& bond) const;

    // Restore from a previously captured raw state. Used by transactions.
    // Not a public snapshot API: a raw state is only ever produced by
    // captureAll() while a transaction is open.
    struct RawState {
        std::vector<AtomRecord> atoms;
        std::vector<BondRecord> bonds;
        std::vector<TextRecord> texts;
        std::vector<PathRecord> paths;
        std::vector<AnnotationRecord> annotations[5];
        std::vector<TableRecord> tables;
        std::vector<GroupRecord> groups;
        std::map<ObjectRef, ExtensionBag> extensions;
        ExtensionBag documentExtensions;
        std::map<ObjectId, std::vector<TextRun> > runs;
        // Atom label overrides. These live in a side table rather than in
        // AtomRecord, but they ARE document state and are part of the digest, so
        // a raw state that omitted them would silently fail to roll back. Any
        // future side table must be added here at the same time it is added to
        // digest().
        std::map<ObjectId, std::string> atomLabels;
        // Document scalars captured at the same instant, so a rollback restores
        // them along with the records.
        Defaults documentDefaults;
    };
    void captureAll(RawState* out) const;
    Error restoreRaw(const RawState& state);
    void  rebuildDerived();

    // Compact statistics for status output and tests.
    struct Stats {
        uint64_t atoms, bonds, texts, paths, annotations, tables, groups;
        uint64_t aliveAtoms, aliveBonds, aliveTexts;
        uint64_t deadSlots;
        uint64_t extensionEntries;
        Stats() : atoms(0), bonds(0), texts(0), paths(0), annotations(0),
                  tables(0), groups(0), aliveAtoms(0), aliveBonds(0), aliveTexts(0),
                  deadSlots(0), extensionEntries(0) {}
    };
    Stats stats() const;

    // Document identity: a fresh id is generated per construction/clone/load.
    static DocumentId nextDocumentId();

    // ---- patch-recording session (see commands.hpp) ------------------------
    // A session is installed by Transaction::begin() and removed on
    // commit()/abort(). Outside a transaction these return 0 and every
    // noteTouched* call is a single predictable branch.
    void             setSession(class DocumentSession* session) { session_ = session; }
    class DocumentSession* session() const { return session_; }
    void noteTouchedAtom(ObjectId id);
    void noteTouchedBond(ObjectId id);
    void noteTouchedText(ObjectId id);
    void noteTouchedPath(ObjectId id);
    void noteTouchedAnnotation(ObjectKind kind, ObjectId id);
    void noteTouchedTable(ObjectId id);
    void noteTouchedGroup(ObjectId id);
    void noteTouchedDefaults();
    void noteTouchedExtensions(const ObjectRef& ref, bool documentLevel);
    void noteTouchedStructure();
    // True while a mutator's changes are being recorded.
    bool recording() const { return session_ != 0; }

    // Stable digest of the whole logical document (records + extensions +
    // defaults). Rollback verification compares digests, so a rollback bug
    // cannot hide behind identical sizes.
    uint64_t digest() const;
    // Digest of a captured raw state. Used to prove that rollback restored the
    // exact pre-command state. Because it hashes the same fields as digest(),
    // a temporary document built from the raw state has an equal digest.
    static uint64_t digestOf(const RawState& state);

private:
    friend class ChangeSet;
    friend class Transaction;
    friend class DocumentSession;

    ObjectSlab<AnnotationRecord>& annotationSlab(ObjectKind kind);

    DocumentId documentId_;
    uint64_t   revision_;
    uint64_t   structureSerial_;

    double      defaultBondLength_;
    double      defaultLineWidth_;
    double      defaultTextSize_;
    std::string defaultFont_;
    std::string name_;
    std::string sourcePath_;

    ObjectSlab<AtomRecord>  atoms_;
    ObjectSlab<BondRecord>  bonds_;
    ObjectSlab<TextRecord>  texts_;
    ObjectSlab<PathRecord>  paths_;
    ObjectSlab<AnnotationRecord> annotations_[5]; // Arrow, Bracket, Orbital, Symbol, BioShape
    ObjectSlab<TableRecord> tables_;
    ObjectSlab<GroupRecord> groups_;

    std::vector< std::vector<ObjectId> > adjacency_;
    // Sparse unordered pair -> bond id. Deliberately not a dense matrix: at
    // 50,000 atoms a dense matrix costs 2.5e9 bytes (measured, RESULTS.md).
    std::map< std::pair<ObjectId, ObjectId>, ObjectId > bondIndex_;

    std::map<ObjectRef, ExtensionBag> objectExtensions_;
    ExtensionBag documentExtensions_;
    std::map< ObjectId, std::vector<TextRun> > runs_;
    std::map< ObjectId, std::string > atomLabels_;
    class DocumentSession* session_;
};

// Convenience: canonical symmetry key for a bond's atom pair.
inline std::pair<ObjectId, ObjectId> bondKey(ObjectId a, ObjectId b) {
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

} // namespace core
} // namespace r62

#endif // R62_NEXT_CORE_DOCUMENT_HPP