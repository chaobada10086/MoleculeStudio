#ifndef R62_NEXT_CORE_COMMANDS_HPP
#define R62_NEXT_CORE_COMMANDS_HPP
// =============================================================================
// re_round62_viewer/next/core/commands.hpp
//
// T01 - Command / Transaction / ChangeSet.
//
// RULES (normative; the acceptance tests assert each one)
// -----------------------------------------------------------------------------
// R1  ONE user action == ONE atomic transaction == ONE undo step. A drag that
//     moves 5,000 atoms is one transaction, not 5,000.
// R2  A transaction that fails or is aborted leaves the document EXACTLY as it
//     was. Rollback is by inverse patch application, verified by a document
//     digest - not by trusting the command implementations.
// R3  Undo, Redo and Repeat are three distinct operations. Redo replays the
//     undone change; Repeat re-executes the last committed command as a NEW
//     history entry (ChemDraw Ctrl+Y semantics per SOURCES_AND_LIMITS.md).
// R4  Every committed transaction yields an INCREMENTAL patch: the records it
//     actually touched, stored as contiguous runs. No full-document snapshot is
//     ever stored in history. This is enforced by a byte-accounting test.
// R5  Deleting an object never renumbers or reuses its id.
//
// -----------------------------------------------------------------------------
// HOW THE PRIMITIVE SESSIONS WORK
// -----------------------------------------------------------------------------
// Transaction::begin() opens a "recording session" on the document: every mutator
// that is called afterwards appends touched ids to a dirty list instead of the
// command having to know what it changed. commit() diffs only those ids and
// stores the before/after records as runs. This means a command implementation
// cannot forget to record a change, which is the classic source of "undo leaves
// one field behind" bugs.
//
// Sessions are implemented by swapping function pointers on a per-document
// SessionHooks object, so the hot path costs one null check per mutator.
// =============================================================================

#include "document.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace core {

// ---------------------------------------------------------------------------
// Invalidation classes
// ---------------------------------------------------------------------------
// A command declares WHAT it touched so downstream caches can be selective.
// These are bit flags: a transform moves geometry AND selection; a style edit
// touches appearance only and must not rebuild a BVH.
enum InvalidationFlag : uint32_t {
    kInvalidateNone      = 0u,
    kInvalidateGeometry  = 1u << 0,   // positions/extents changed -> spatial index
    kInvalidateAppearance= 1u << 1,   // colors/labels/widths -> glyph+mesh cache
    kInvalidateSemantics = 1u << 2,   // element/order/charge -> glyph+topology
    kInvalidateSelection = 1u << 3,   // selection set changed
    kInvalidateView      = 1u << 4,   // view transform changed (never a doc edit)
    kInvalidateStructure = 1u << 5,   // objects added/removed -> records list
    kInvalidateHistory   = 1u << 6    // undo stack state changed
};

typedef uint32_t Invalidation;

// ---------------------------------------------------------------------------
// Document-level scalar change (see core::Defaults in contracts.hpp)
// ---------------------------------------------------------------------------
struct DefaultsPatch {
    bool     present;
    Defaults before;
    Defaults after;
    DefaultsPatch() : present(false) {}
};

// ---------------------------------------------------------------------------
// Patch primitives
// ---------------------------------------------------------------------------
// Contiguous run of records within one slab. offset is the slab index of the
// first record in the run.
template <typename T>
struct RecordRun {
    uint32_t offset;
    std::vector<T> records;
    RecordRun() : offset(0) {}
    size_t bytes() const { return records.size() * sizeof(T); }
};

struct ExtensionPatch {
    ObjectRef   ref;        // ObjectKind::Unknown + kInvalidObjectId == document bag
    bool        beforePresent;
    ExtensionBag before;
    bool        afterPresent;
    ExtensionBag after;
    ExtensionPatch() : beforePresent(false), afterPresent(false) {}
};

// The incremental change record. This is what history stores.
//
// Every slab carries TWO run lists over the SAME id ranges: "after" holds the
// records once the command has run, "before" holds them as they were just
// before it. Keeping both is what makes undo a data restore rather than a
// hand-written inverse, which is the classic place where one field silently
// fails to come back.
struct SlabPatch {
    // Slab length on this side of the change. Applying a patch resizes the slab
    // to this exact length before writing the runs, which is what removes a slot
    // that the other direction created. Without it, undoing "add an atom" would
    // leave a dead slot behind and the document would not be byte-identical to
    // its pre-command shape.
    uint32_t atomsSize, bondsSize, textsSize, pathsSize, tablesSize, groupsSize;
    uint32_t annotationSizes[5];
    SlabPatch()
        : atomsSize(0), bondsSize(0), textsSize(0), pathsSize(0),
          tablesSize(0), groupsSize(0) {
        for (int i = 0; i < 5; ++i) annotationSizes[i] = 0;
    }
    std::vector<RecordRun<AtomRecord> >       atomsBefore, atomsAfter;
    std::vector<RecordRun<BondRecord> >       bondsBefore, bondsAfter;
    std::vector<RecordRun<TextRecord> >       textsBefore, textsAfter;
    std::vector<RecordRun<PathRecord> >       pathsBefore, pathsAfter;
    std::vector<RecordRun<AnnotationRecord> > arrowsBefore, arrowsAfter;
    std::vector<RecordRun<AnnotationRecord> > bracketsBefore, bracketsAfter;
    std::vector<RecordRun<AnnotationRecord> > orbitalsBefore, orbitalsAfter;
    std::vector<RecordRun<AnnotationRecord> > symbolsBefore, symbolsAfter;
    std::vector<RecordRun<AnnotationRecord> > bioShapesBefore, bioShapesAfter;
    std::vector<RecordRun<TableRecord> >      tablesBefore, tablesAfter;
    std::vector<RecordRun<GroupRecord> >      groupsBefore, groupsAfter;
    std::vector<RecordRun<TextRun> >          textRunsBefore, textRunsAfter;
    size_t runCount() const;
    size_t recordCount() const;
};

struct ChangeSet {
    SlabPatch forward;   // before -> after
    SlabPatch reverse;   // after -> before
    std::vector<ExtensionPatch> extensions;
    // Document-level scalar defaults (bond length, line width, text size, font).
    DefaultsPatch defaults;
    bool empty() const;
    uint64_t byteSize() const;
    uint64_t recordCount() const;
    // Number of separate runs/patches. A 5,000-atom drag over contiguous slots
    // has a SMALL run count while recordCount() is large; that ratio is what
    // "incremental patch" means and what the acceptance test checks.
    size_t   runCount() const;
    // Apply this change set in the "before" or "after" direction. Writes through
    // the quiet mutators, so it neither records new history nor re-validates per
    // record; validate() runs once at the end.
    Error apply(Document& document, bool after);
};

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------
struct CommandContext {
    Document&      document;
    ViewTransform* view;      // may be null in headless use
    std::string    tag;       // debug label, e.g. "drag.atoms"
    explicit CommandContext(Document& d) : document(d), view(0) {}
};

struct CommandResult {
    Error        error;
    Invalidation invalidation;
    CommandResult() : invalidation(kInvalidateNone) {}
    // Named success() rather than ok(): a static factory and the ok() predicate
    // must not overload each other, or every call site becomes ambiguous.
    static CommandResult success(Invalidation inv = kInvalidateStructure | kInvalidateGeometry) {
        CommandResult r; r.invalidation = inv; return r;
    }
    static CommandResult noChange() {
        CommandResult r; r.invalidation = kInvalidateNone; return r;
    }
    static CommandResult fail(ErrorCode code, const std::string& message = std::string()) {
        CommandResult r; r.error = Error(code, message); return r;
    }
    bool ok() const { return error.ok(); }
};

// A command is a value-typed, copyable description of an edit. It is replayed
// verbatim by Repeat, so it must not capture pointers into the document.
class Command {
public:
    typedef std::function<CommandResult(CommandContext&)> Fn;
    Command() : name_("empty"), invalidation_(kInvalidateNone) {}
    Command(const std::string& name, Invalidation inv, const Fn& fn)
        : name_(name), invalidation_(inv), fn_(fn) {}
    const std::string& name() const { return name_; }
    Invalidation invalidation() const { return invalidation_; }
    CommandResult execute(CommandContext& ctx) const {
        if (!fn_) return CommandResult::fail(ErrorCode::NotImplemented, "empty command");
        return fn_(ctx);
    }
    bool valid() const { return static_cast<bool>(fn_); }
private:
    std::string  name_;
    Invalidation invalidation_;
    Fn           fn_;
};

// ---------------------------------------------------------------------------
// Patch recording on the Document
// ---------------------------------------------------------------------------
// Transaction::begin() installs a DocumentSession on the document. Every
// mutator then appends the touched id to a dirty list, so a command
// implementation CANNOT forget to record a change. commit() diffs only the
// dirty ids and turns the differences into contiguous runs.
//
// Cost on the hot path: one pointer compare per mutator. The dirty vectors are
// sorted+deduplicated once at commit, so a drag that touches 5,000 atoms in
// arbitrary order still produces runs in ascending order.

// Slab tag used to bucket a dirty id. ObjectKind is reused so the bucket also
// carries the semantic kind of the record.
struct DirtyId {
    ObjectKind kind;
    ObjectId   id;
    DirtyId() : kind(ObjectKind::Unknown), id(kInvalidObjectId) {}
    DirtyId(ObjectKind k, ObjectId i) : kind(k), id(i) {}
    bool operator<(const DirtyId& o) const {
        return kind != o.kind ? kind < o.kind : id < o.id;
    }
    bool operator==(const DirtyId& o) const { return kind == o.kind && id == o.id; }
};

class DocumentSession {
public:
    DocumentSession();
    ~DocumentSession();
    DocumentSession(Document& document, const std::string& tag);
    DocumentSession(DocumentSession&& other);
    DocumentSession& operator=(DocumentSession&& other);

    // Install on a document / remove. Idempotent and safe with no document.
    Error attach(Document& document, const std::string& tag);
    void  detach();

    bool attached() const { return document_ != 0; }
    const std::string& tag() const { return tag_; }

    // Mutation notices. No-ops when nothing is attached.
    void noteAtom(ObjectId id)  { if (document_) dirty_.push_back(DirtyId(ObjectKind::Atom, id)); }
    void noteBond(ObjectId id)  { if (document_) dirty_.push_back(DirtyId(ObjectKind::Bond, id)); }
    void noteText(ObjectId id)  { if (document_) dirty_.push_back(DirtyId(ObjectKind::Text, id)); }
    void notePath(ObjectId id)  { if (document_) dirty_.push_back(DirtyId(ObjectKind::Path, id)); }
    void noteAnnotation(ObjectKind kind, ObjectId id) {
        if (document_) dirty_.push_back(DirtyId(kind, id));
    }
    void noteTable(ObjectId id) { if (document_) dirty_.push_back(DirtyId(ObjectKind::Table, id)); }
    void noteGroup(ObjectId id) { if (document_) dirty_.push_back(DirtyId(ObjectKind::Group, id)); }
    void noteDefaults()         { if (document_) defaultsDirty_ = true; }
    void noteExtensions(const ObjectRef& ref, bool documentLevel) {
        if (!document_) return;
        extensionsDirty_ = true;
        extensionRefs_.push_back(documentLevel ? ObjectRef() : ref);
    }
    void noteStructure()        { if (document_) structureDirty_ = true; }

    const std::vector<DirtyId>& dirty() const { return dirty_; }
    bool defaultsDirty() const { return defaultsDirty_; }
    bool extensionsDirty() const { return extensionsDirty_; }
    bool structureDirty() const { return structureDirty_; }
    const std::vector<ObjectRef>& extensionRefs() const { return extensionRefs_; }

    // The raw state captured at attach(). Used for rollback (R2) so rollback
    // does not depend on the command's own inverse logic.
    const Document::RawState& baseline() const { return baseline_; }

    // Build a ChangeSet from the dirty ids, comparing baseline against current.
    // Sets maxRunLength to the longest contiguous run produced.
    void buildChangeSet(ChangeSet* out, size_t* maxRunLength) const;

    // Move the baseline to the current document state. Called after a
    // successful commit; clears the dirty lists.
    void resetBaseline();

    Document* document() const { return document_; }

private:
    DocumentSession(const DocumentSession&);
    DocumentSession& operator=(const DocumentSession&);

    Document*               document_;
    std::string             tag_;
    std::vector<DirtyId>    dirty_;
    std::vector<ObjectRef>  extensionRefs_;
    bool                    defaultsDirty_;
    bool                    extensionsDirty_;
    bool                    structureDirty_;
    Document::RawState      baseline_;
};

// Document-side accessors installed by DocumentSession. Each returns 0 when no
// session is attached, which is the normal (fast) case outside a transaction.
DocumentSession* documentSession(const Document* document);

// ---------------------------------------------------------------------------
// Transaction
// ---------------------------------------------------------------------------
// RAII: destroying an un-committed transaction rolls back and leaves the
// document untouched.
class Transaction {
public:
    Transaction();
    Transaction(Document& document, const std::string& label);
    ~Transaction();
    Transaction(Transaction&& other);
    Transaction& operator=(Transaction&& other);

    // Begin/commit/abort. begin() on an already-open transaction fails.
    Error begin(Document& document, const std::string& label);
    Error commit();          // records history entry, returns Ok/RolledBack
    void  abort();           // rollback, no history entry

    bool open() const { return open_; }
    bool rolledBack() const { return rolledBack_; }
    const std::string& label() const { return label_; }
    // Last error raised inside the transaction; commit() refuses when set and a
    // command declared the failure fatal.
    Error error() const { return error_; }
    void  setError(const Error& e) { error_ = e; }
    Invalidation invalidation() const { return invalidation_; }
    void  addInvalidation(Invalidation inv) { invalidation_ |= inv; }
    // The patch produced by the most recent commit().
    const ChangeSet& changeSet() const { return changeSet_; }
    size_t historyIndexAfterCommit() const { return committedIndex_; }

private:
    Transaction(const Transaction&);
    Transaction& operator=(const Transaction&);
    friend class CommandLog;

    Document*       document_;
    DocumentSession session_;
    bool            open_;
    bool            rolledBack_;
    std::string     label_;
    Error           error_;
    Invalidation    invalidation_;
    ChangeSet       changeSet_;
    size_t          committedIndex_;
};

// ---------------------------------------------------------------------------
// CommandLog
// ---------------------------------------------------------------------------
// Holds the undo/redo stacks, the repeat command, and the byte budget.
class CommandLog {
public:
    enum { kDefaultMaxEntries = 128 };
    enum { kDefaultMaxBytes = 64u * 1024u * 1024u };

    CommandLog();
    ~CommandLog();

    void clear();
    void setLimits(size_t maxEntries, uint64_t maxBytes) {
        maxEntries_ = maxEntries; maxBytes_ = maxBytes; trim();
    }
    size_t   entries() const { return entries_.size(); }
    size_t   cursor() const { return cursor_; }        // number of applied entries
    bool     canUndo() const { return cursor_ > 0; }
    bool     canRedo() const { return cursor_ < entries_.size(); }
    bool     canRepeat() const { return repeat_.valid(); }
    uint64_t byteSize() const { return byteSize_; }
    // Total bytes of full-document snapshots stored by history. The design
    // forbids this from being non-zero; it exists so a test can assert it.
    uint64_t snapshotBytes() const { return snapshotBytes_; }

    const std::string& undoLabel() const;
    const std::string& redoLabel() const;

    // Execute one user action: open a transaction, run the command, commit.
    // On failure the document is unchanged and NOTHING is pushed on the stack.
    Error execute(Document& document, const Command& command,
                  CommandContext* externalContext = 0);

    Error undo(Document& document);
    Error redo(Document& document);
    // ChemDraw Ctrl+Y: re-run the last committed command as a new entry.
    Error repeat(Document& document, CommandContext* externalContext = 0);

    // Diagnostics used by the acceptance tests and by the coverage report.
    const Command* repeatCommand() const { return repeat_.valid() ? &repeat_ : 0; }
    uint64_t totalRecordBytes() const { return byteSize_; }
    size_t   maxRunLength() const { return maxRunLength_; }
    // The change set of the most recently committed entry. Empty when the log
    // is empty. Exposed for tests; production code should not need it.
    const ChangeSet& lastChangeSet() const;
    // Longest run over every entry currently in the log.
    size_t   longestRunOverall() const;

private:
    struct Entry {
        ChangeSet     changes;
        std::string   label;
        Invalidation  invalidation;
        uint64_t      bytes;
        uint64_t      revisionBefore;
        uint64_t      revisionAfter;
        Entry() : invalidation(kInvalidateNone), bytes(0),
                  revisionBefore(0), revisionAfter(0) {}
    };
    friend class Transaction;
    Error push(Transaction& tx, const Command& command);
    void  trim();
    Error applyEntry(Document& document, const Entry& entry, bool after, Invalidation* inv);

    std::vector<Entry> entries_;
    size_t   cursor_;
    Command  repeat_;
    size_t   maxEntries_;
    uint64_t maxBytes_;
    uint64_t byteSize_;
    uint64_t snapshotBytes_;
    size_t   maxRunLength_;
    std::string emptyLabel_;
};

// ---------------------------------------------------------------------------
// Built-in commands (geometry / style / semantics / selection / view)
// ---------------------------------------------------------------------------
// These are provided so that every module issues edits through the SAME entry
// point (CommandLog::execute) rather than mutating Document directly. Each is a
// factory returning a copyable Command value.
namespace commands {

// --- object creation / removal -------------------------------------------
Command addAtom(const Vec2& position, int element, ObjectId* outId);
Command deleteObjects(const std::vector<ObjectRef>& refs);
Command addBond(ObjectId begin, ObjectId end, BondOrder order, ObjectId* outId);
Command addText(const Vec2& position, double size, const std::string& utf8, ObjectId* outId);

// --- transforms -----------------------------------------------------------
// One drag == one command. The delta is applied to every listed object.
Command translateObjects(const std::vector<ObjectRef>& refs, const Vec2& delta);
// Absolute placement, used when committing a preview whose endpoints were
// resolved by the snap module (preview and commit share the resolution).
Command setAtomPositions(const std::vector<ObjectId>& atoms,
                         const std::vector<Vec2>& positions);
Command rotateObjects(const std::vector<ObjectRef>& refs, const Vec2& pivot,
                      double degrees);

// --- semantics / style ----------------------------------------------------
Command setElements(const std::vector<ObjectId>& atoms, int element);
Command setAtomSemantics(const std::vector<ObjectId>& atoms, const AtomSemantics& s);
Command setBondOrders(const std::vector<ObjectId>& bonds, BondOrder order);
Command setBondDisplay(const std::vector<ObjectId>& bonds, BondDisplay display);
Command setBondSemantics(const std::vector<ObjectId>& bonds, BondSemantics semantics);
Command setBondStereo(const std::vector<ObjectId>& bonds, BondStereo stereo);
Command setTextPlain(ObjectId text, const std::string& utf8);
Command setTextRuns(ObjectId text, const std::vector<TextRun>& runs);
Command setLineStyle(const std::vector<ObjectRef>& refs, const LineStyle& style);

// --- style options (the 275-entry family: same algorithm, different preset) --
Command setStylePreset(const std::vector<ObjectRef>& refs, const std::string& optionId);

// --- extension payload ----------------------------------------------------
Command setDocumentExtension(const std::string& key, const std::string& value);
Command setObjectExtension(const ObjectRef& ref, const std::string& key, const std::string& value);

} // namespace commands

// ---------------------------------------------------------------------------
// ChangeSet helpers implemented here for reuse by T02 (persistence)
// ---------------------------------------------------------------------------
namespace detail {
template <typename T>
void appendRun(std::vector<RecordRun<T> >& runs, const std::vector<T>& records,
               ObjectId id, bool before);
} // namespace detail

} // namespace core
} // namespace r62

#endif // R62_NEXT_CORE_COMMANDS_HPP