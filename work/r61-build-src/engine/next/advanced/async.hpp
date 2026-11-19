#ifndef R62_NEXT_ADVANCED_ASYNC_HPP
#define R62_NEXT_ADVANCED_ASYNC_HPP
// =============================================================================
// next/advanced/async.hpp  --  T17 versioned, cancellable layout job
//
// WHY THIS EXISTS
// -----------------------------------------------------------------------------
// A cleanup of a large, awkward structure can take longer than a UI frame. The
// rule this file enforces is the one in the task statement: a result computed
// from an OLD document state must NEVER be applied to a NEW one.
//
// The mechanism is a version ticket, not a timeout:
//
//   * start() snapshots the document into a plain LayoutInput and records the
//     source version (DocumentId, revision, structureSerial). The worker thread
//     ONLY ever sees the snapshot: a core::Document belongs to one thread, so
//     handing the live document to a worker would be a data race by design.
//   * tryTake() refuses the result unless the ticket equals the live document's
//     version AND the job was not cancelled. The refusal carries a reason.
//   * The plan itself is still validated again by applyCleanupCommand(), so a
//     caller that ignores tryTake() cannot land a stale plan either.
//
// This is deliberately not a general work-queue: one job, one document, one
// result. A bigger scheduler is not needed for one layout action.
// =============================================================================

#include "next/advanced/cleanup.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace r62 {
namespace advanced {

struct DocumentVersion {
    core::DocumentId id;
    uint64_t revision;
    uint64_t structureSerial;
    DocumentVersion() : id(0), revision(0), structureSerial(0) {}
    bool operator==(const DocumentVersion& o) const {
        return id == o.id && revision == o.revision &&
               structureSerial == o.structureSerial;
    }
    bool operator!=(const DocumentVersion& o) const { return !(*this == o); }
};

DocumentVersion captureVersion(const core::Document& doc);
bool            isCurrent(const core::Document& doc, const DocumentVersion& version);
std::string     versionMismatch(const core::Document& doc, const DocumentVersion& version);

template <class T>
struct VersionedResult {
    bool            hasValue;
    DocumentVersion version;
    T               value;
    std::string     note;
    VersionedResult() : hasValue(false) {}
};

class CleanupJob {
public:
    CleanupJob();
    ~CleanupJob();

    // Start a job from a snapshot. Replaces any previous job (joining it first).
    void start(const LayoutInput& input, const CleanupOptions& options);

    bool running() const;
    bool finished() const { return done_.load(); }
    bool cancelled() const { return cancelled_.load(); }
    // Non-blocking. The worker checks the flag before publishing; a cancelled
    // job therefore never produces a result even if the computation finished.
    void cancel();

    // Collect. True only when: finished, not cancelled, and the source version
    // equals the live document version. On false, `reason` explains
    // which of the three conditions failed.
    bool tryTake(const core::Document& doc, CleanupPlan* out, std::string* reason);

    // Drop result and join the worker. Safe to call repeatedly.
    void reset();

    const DocumentVersion& sourceVersion() const { return version_; }

private:
    CleanupJob(const CleanupJob&);
    CleanupJob& operator=(const CleanupJob&);
    void run(LayoutInput input, CleanupOptions options);

    std::thread           worker_;
    std::atomic<bool>     cancelFlag_;
    std::atomic<bool>     cancelled_;
    std::atomic<bool>     done_;
    std::mutex            mutex_;
    CleanupPlan           result_;
    core::Error           resultError_;
    bool                  resultOk_;
    DocumentVersion       version_;
};

// A plain synchronous reference implementation of the same version rule, used
// by tests and by callers that must not start a thread.
bool computeCleanupChecked(const core::Document& doc, const CleanupOptions& options,
                           CleanupPlan* out, std::string* reason);

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_ASYNC_HPP
