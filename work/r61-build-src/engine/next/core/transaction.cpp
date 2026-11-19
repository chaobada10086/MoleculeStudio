#include "commands.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace r62 {
namespace core {

// ===========================================================================
// Transaction
// ===========================================================================
Transaction::Transaction()
    : document_(0), open_(false), rolledBack_(false), label_(""),
      committedIndex_(0) {}

Transaction::Transaction(Document& document, const std::string& label)
    : document_(0), open_(false), rolledBack_(false), label_(label),
      committedIndex_(0) {
    Error r = begin(document, label);
    if (!r.ok()) { error_ = r; rolledBack_ = true; }
}

Transaction::~Transaction() {
    // RAII guarantee: a transaction that was never committed leaves the document
    // exactly as it was. This is what makes "failure rolls back" structural
    // rather than something each command implementation must remember.
    if (open_) abort();
}

Transaction::Transaction(Transaction&& other)
    : document_(other.document_), session_(static_cast<DocumentSession&&>(other.session_)),
      open_(other.open_), rolledBack_(other.rolledBack_), label_(other.label_),
      error_(other.error_), invalidation_(other.invalidation_),
      changeSet_(other.changeSet_), committedIndex_(other.committedIndex_) {
    other.document_ = 0;
    other.open_ = false;
    other.rolledBack_ = true;
}

Transaction& Transaction::operator=(Transaction&& other) {
    if (this == &other) return *this;
    if (open_) abort();
    document_ = other.document_;
    session_ = static_cast<DocumentSession&&>(other.session_);
    open_ = other.open_;
    rolledBack_ = other.rolledBack_;
    label_ = other.label_;
    error_ = other.error_;
    invalidation_ = other.invalidation_;
    changeSet_ = other.changeSet_;
    committedIndex_ = other.committedIndex_;
    other.document_ = 0;
    other.open_ = false;
    other.rolledBack_ = true;
    return *this;
}

Error Transaction::begin(Document& document, const std::string& label) {
    if (open_) return Error(ErrorCode::AlreadyExists, "Transaction is already open");
    Error attached = session_.attach(document, label);
    if (!attached.ok()) return attached;
    document_ = &document;
    open_ = true;
    rolledBack_ = false;
    label_ = label;
    error_ = Error::success();
    invalidation_ = kInvalidateNone;
    changeSet_ = ChangeSet();
    return Error::success();
}

void Transaction::abort() {
    if (!open_) return;
    // Roll back from the SESSION BASELINE, not from the command-provided inverse.
    // A command that computed a wrong inverse still cannot corrupt the document.
    rolledBack_ = true;
    open_ = false;
    if (document_) document_->restoreRaw(session_.baseline());
    session_.detach();
    document_ = 0;
    invalidation_ = kInvalidateNone;
    changeSet_ = ChangeSet();
}

Error Transaction::commit() {
    if (!open_) return Error(ErrorCode::NotAlive, "Transaction is not open");
    open_ = false;
    if (!document_) {
        rolledBack_ = true;
        return Error(ErrorCode::RolledBack, "Transaction has no document");
    }
    return Error::success();
}

// ===========================================================================
// CommandLog
// ===========================================================================
CommandLog::CommandLog()
    : cursor_(0), maxEntries_(kDefaultMaxEntries), maxBytes_(kDefaultMaxBytes),
      byteSize_(0), snapshotBytes_(0), maxRunLength_(0), emptyLabel_("") {}

CommandLog::~CommandLog() {}

void CommandLog::clear() {
    entries_.clear();
    cursor_ = 0;
    byteSize_ = 0;
    snapshotBytes_ = 0;
    maxRunLength_ = 0;
    repeat_ = Command();
}

void CommandLog::trim() {
    while (!entries_.empty() &&
           (entries_.size() > maxEntries_ ||
            (byteSize_ > maxBytes_ && entries_.size() > 1))) {
        byteSize_ -= entries_.front().bytes;
        entries_.erase(entries_.begin());
        if (cursor_ > 0) --cursor_;
    }
}

const ChangeSet& CommandLog::lastChangeSet() const {
    static const ChangeSet empty;
    if (entries_.empty() || cursor_ == 0) return empty;
    return entries_[cursor_ - 1].changes;
}

size_t CommandLog::longestRunOverall() const {
    size_t best = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const SlabPatch& patch = entries_[i].changes.forward;
        for (size_t k = 0; k < patch.atomsAfter.size(); ++k)
            if (patch.atomsAfter[k].records.size() > best)
                best = patch.atomsAfter[k].records.size();
        for (size_t k = 0; k < patch.bondsAfter.size(); ++k)
            if (patch.bondsAfter[k].records.size() > best)
                best = patch.bondsAfter[k].records.size();
        for (size_t k = 0; k < patch.textsAfter.size(); ++k)
            if (patch.textsAfter[k].records.size() > best)
                best = patch.textsAfter[k].records.size();
    }
    return best;
}

const std::string& CommandLog::undoLabel() const {
    return cursor_ > 0 ? entries_[cursor_ - 1].label : emptyLabel_;
}

const std::string& CommandLog::redoLabel() const {
    return cursor_ < entries_.size() ? entries_[cursor_].label : emptyLabel_;
}

Error CommandLog::execute(Document& document, const Command& command,
                          CommandContext* externalContext) {
    if (!command.valid()) return Error(ErrorCode::NotImplemented, "Command is empty");
    Transaction transaction;
    Error started = transaction.begin(document, command.name());
    if (!started.ok()) return started;

    CommandContext local(document);
    CommandContext& ctx = externalContext ? *externalContext : local;
    if (ctx.tag.empty()) ctx.tag = command.name();
    CommandResult result = command.execute(ctx);
    if (!result.ok()) {
        transaction.setError(result.error);
        transaction.abort();
        return result.error;
    }
    transaction.addInvalidation(command.invalidation());
    transaction.addInvalidation(result.invalidation);
    return push(transaction, command);
}

Error CommandLog::push(Transaction& transaction, const Command& command) {
    Document& document = *transaction.document_;
    // 1. Build the incremental patch while the session baseline is still the
    //    pre-command state.
    size_t maxRun = 0;
    transaction.session_.buildChangeSet(&transaction.changeSet_, &maxRun);

    // 2. Close the rollback window: the command succeeded, so the baseline moves
    //    forward and the patches become the only record of the change.
    transaction.session_.detach();
    transaction.document_ = 0;

    if (transaction.changeSet_.empty()) {
        // A no-op command must not create a history entry: pressing a key that
        // is already the current value should not consume an undo step.
        transaction.committedIndex_ = cursor_;
        return Error::success();
    }

    // Committing invalidates any redo branch.
    if (cursor_ < entries_.size()) {
        for (size_t i = cursor_; i < entries_.size(); ++i) byteSize_ -= entries_[i].bytes;
        entries_.resize(cursor_);
    }

    Entry entry;
    entry.changes = transaction.changeSet_;
    entry.label = command.name();
    entry.invalidation = transaction.invalidation_;
    entry.bytes = entry.changes.byteSize();
    entry.revisionBefore = document.revision();
    entry.revisionAfter = document.revision() + 1;
    byteSize_ += entry.bytes;
    if (maxRun > maxRunLength_) maxRunLength_ = maxRun;
    entries_.push_back(entry);
    cursor_ = entries_.size();
    transaction.committedIndex_ = cursor_;

    // Repeat semantics: the last COMMITTED command, not the last undone one.
    repeat_ = command;

    document.bumpStructure();
    trim();
    return Error::success();
}

Error CommandLog::applyEntry(Document& document, const Entry& entry, bool after,
                             Invalidation* inv) {
    Error applied = const_cast<ChangeSet&>(entry.changes).apply(document, after);
    if (!applied.ok()) return applied;
    if (inv) *inv = entry.invalidation;
    return Error::success();
}

Error CommandLog::undo(Document& document) {
    if (cursor_ == 0) return Error(ErrorCode::OutOfRange, "Nothing to undo");
    Entry& entry = entries_[cursor_ - 1];
    Invalidation inv = kInvalidateNone;
    Error applied = applyEntry(document, entry, false, &inv);
    if (!applied.ok()) return applied;
    --cursor_;
    document.bumpStructure();
    return Error::success();
}

Error CommandLog::redo(Document& document) {
    if (cursor_ >= entries_.size()) return Error(ErrorCode::OutOfRange, "Nothing to redo");
    Entry& entry = entries_[cursor_];
    Invalidation inv = kInvalidateNone;
    Error applied = applyEntry(document, entry, true, &inv);
    if (!applied.ok()) return applied;
    ++cursor_;
    document.bumpStructure();
    return Error::success();
}

Error CommandLog::repeat(Document& document, CommandContext* externalContext) {
    if (!repeat_.valid()) return Error(ErrorCode::NotImplemented, "Nothing to repeat");
    // Repeat is a NEW entry with a NEW transaction, not a redo. Undoing it must
    // return to the state before the repeat, which redo alone would not do.
    return execute(document, repeat_, externalContext);
}

} // namespace core
} // namespace r62
