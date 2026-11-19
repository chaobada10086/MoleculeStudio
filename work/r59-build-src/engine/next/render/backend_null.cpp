// =============================================================================
// re_round62_viewer/next/render/backend_null.cpp
//
// T03 - Null backend. Runs the FULL cache, BVH and batching pipeline with no GL
// call at all. Its purpose is to make the cache and culling behaviour testable on
// a machine with no GPU and on a CI box with no window station, and to give the
// performance harness a baseline that isolates CPU work from driver time.
//
// It is not a stub: it produces the same RenderStats a real backend does, minus
// the GL counters. A test asserting 'a pan rebuilds no geometry' runs against this
// backend and the claim therefore does not depend on a driver being present.
// =============================================================================

#include "next/render/batcher.hpp"
#include "next/render/scene_api.hpp"

namespace r62 {
namespace render {

class NullBackend : public Backend {
public:
    NullBackend() : ready_(false) {}
    const char* name() const { return "null"; }
    bool initialize(std::string* error) {
        (void)error;
        ready_ = true;
        return true;
    }
    bool ready() const { return ready_; }

    void sync(const Scene& scene, const View& view) {
        BatchStats one;
        batcherFor(&scene)->sync(scene, view, 0, &one);
        Batcher::accumulate(&batchStats_, one);
    }
    void setView(const View& view) { view_ = view; }

    core::Error draw(const Scene& content, const Scene* overlay, const View& view,
                     RenderStats* stats) {
        if (!ready_) return core::Error(core::ErrorCode::NotImplemented, "not initialized");
        view_ = view;
        batchStats_.reset();
        if (stats) *stats = RenderStats();

        BatchStats one;
        contentBatcher_.sync(content, view, 0, &one);
        Batcher::accumulate(&batchStats_, one);
        if (overlay) {
            overlayBatcher_.sync(*overlay, view, 0, &one);
            Batcher::accumulate(&batchStats_, one);
        }

        Batch contentBatch, overlayBatch;
        contentBatcher_.buildBatch(content, view, false, &contentBatch, &batchStats_);
        if (overlay) overlayBatcher_.buildBatch(*overlay, view, false, &overlayBatch, &batchStats_);

        lastTriangles_ = contentBatch.triangleVertices;
        lastLines_ = contentBatch.lineVertices;
        lastTriangleRanges_ = contentBatch.triangleRanges;
        lastLineRanges_ = contentBatch.lineRanges;

        if (stats) {
            stats->primitivesTotal = content.size() + (overlay ? overlay->size() : 0);
            stats->primitivesCulled = batchStats_.culled;
            stats->primitivesDrawn = batchStats_.drawn;
            stats->meshRebuilds = batchStats_.meshRebuilds;
            stats->viewRebuilds = batchStats_.viewRebuilds;
            stats->glyphSamples = batchStats_.glyphSamples;
            stats->glyphReuses = batchStats_.glyphReuses;
            stats->bvhRebuilds = batchStats_.bvhRebuilds;
            stats->triangles = contentBatch.triangleCount() + overlayBatch.triangleCount();
            stats->lines = contentBatch.lineCount() + overlayBatch.lineCount();
            stats->drawCalls = contentBatch.triangleRanges.size() + contentBatch.lineRanges.size();
        }
        return core::Error();
    }

    core::Error readPixels(int, int, int, int, std::vector<uint8_t>* rgba) const {
        if (rgba) rgba->clear();
        return core::Error(core::ErrorCode::NotImplemented, "null backend has no pixels");
    }

    void destroy() {
        contentBatcher_.clear();
        overlayBatcher_.clear();
        ready_ = false;
    }

    // Exposed so a test can assert the batch it produced, not just the stats.
    const std::vector<float>& lastTriangles() const { return lastTriangles_; }
    const std::vector<float>& lastLines() const { return lastLines_; }

private:
    Batcher* batcherFor(const Scene* scene) {
        if (scene && scene->size() > 0 && OverlayScene::isOverlayLayer(scene->primitives()[0].layer))
            return &overlayBatcher_;
        return &contentBatcher_;
    }
    Batcher contentBatcher_;
    Batcher overlayBatcher_;
    BatchStats batchStats_;
    View view_;
    bool ready_;
    std::vector<float> lastTriangles_, lastLines_;
    std::vector<DrawRange> lastTriangleRanges_, lastLineRanges_;
};

Backend* createNullBackend() { return new NullBackend(); }

} // namespace render
} // namespace r62
