#include "next/advanced/async.hpp"

#include <cstdio>

namespace r62 {
namespace advanced {

DocumentVersion captureVersion(const core::Document& doc) {
    DocumentVersion v;
    v.id = doc.id();
    v.revision = doc.revision();
    v.structureSerial = doc.structureSerial();
    return v;
}

bool isCurrent(const core::Document& doc, const DocumentVersion& version) {
    return doc.id() == version.id && doc.revision() == version.revision &&
           doc.structureSerial() == version.structureSerial;
}

std::string versionMismatch(const core::Document& doc, const DocumentVersion& version) {
    if (doc.id() != version.id) return "document switched";
    if (doc.revision() != version.revision ||
        doc.structureSerial() != version.structureSerial)
        return "document changed since the layout started";
    return "";
}

CleanupJob::CleanupJob()
    : cancelFlag_(false), cancelled_(false), done_(false), resultOk_(false) {}

CleanupJob::~CleanupJob() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

void CleanupJob::start(const LayoutInput& input, const CleanupOptions& options) {
    reset();
    version_.id = input.documentId;
    version_.revision = input.revision;
    version_.structureSerial = input.structureSerial;
    cancelFlag_.store(false);
    cancelled_.store(false);
    done_.store(false);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        result_ = CleanupPlan();
        resultError_ = core::Error();
        resultOk_ = false;
    }
    worker_ = std::thread(&CleanupJob::run, this, input, options);
}

void CleanupJob::run(LayoutInput input, CleanupOptions options) {
    CleanupPlan plan;
    const core::Error e = planCleanup(input, options, &plan);
    if (cancelFlag_.load()) {
        // A cancelled job NEVER publishes, even if it computed successfully.
        done_.store(true);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        result_ = plan;
        resultError_ = e;
        resultOk_ = e.ok() && plan.valid;
    }
    done_.store(true);
}

bool CleanupJob::running() const {
    return worker_.joinable() && !done_.load();
}

void CleanupJob::cancel() {
    cancelFlag_.store(true);
    cancelled_.store(true);
}

bool CleanupJob::tryTake(const core::Document& doc, CleanupPlan* out,
                         std::string* reason) {
    if (!done_.load()) {
        if (reason) *reason = "layout is still running";
        return false;
    }
    if (cancelled_.load()) {
        if (reason) *reason = "layout was cancelled";
        return false;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    if (!resultOk_) {
        if (reason) *reason = resultError_.message.empty()
                                  ? std::string("layout produced no result")
                                  : resultError_.message;
        return false;
    }
    if (!isCurrent(doc, version_)) {
        if (reason) *reason = versionMismatch(doc, version_);
        return false;
    }
    if (out) *out = result_;
    return true;
}

void CleanupJob::reset() {
    cancel();
    if (worker_.joinable()) worker_.join();
    done_.store(false);
    cancelled_.store(false);
    cancelFlag_.store(false);
    std::lock_guard<std::mutex> guard(mutex_);
    result_ = CleanupPlan();
    resultError_ = core::Error();
    resultOk_ = false;
}

bool computeCleanupChecked(const core::Document& doc, const CleanupOptions& options,
                           CleanupPlan* out, std::string* reason) {
    LayoutInput input;
    core::Error e = captureLayoutInput(doc, &input);
    if (!e.ok()) {
        if (reason) *reason = e.message;
        return false;
    }
    const DocumentVersion version = captureVersion(doc);
    CleanupPlan plan;
    e = planCleanup(input, options, &plan);
    if (!e.ok()) {
        if (reason) *reason = e.message;
        return false;
    }
    if (!isCurrent(doc, version)) {
        if (reason) *reason = versionMismatch(doc, version);
        return false;
    }
    if (out) *out = plan;
    return true;
}

} // namespace advanced
} // namespace r62
