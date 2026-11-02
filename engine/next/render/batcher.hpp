#ifndef R62_NEXT_RENDER_BATCHER_HPP
#define R62_NEXT_RENDER_BATCHER_HPP
// =============================================================================
// re_round62_viewer/next/render/batcher.hpp
//
// T03 - the incremental mesh cache and the batching/culling pipeline that BOTH
// backends share. Putting it here (rather than in backend_gl.cpp) is deliberate:
// the Null backend and the software rasterizer then exercise the SAME cache and
// the SAME culling decisions as the GPU path, so a cache or cull defect cannot
// hide behind 'the driver did it'.
//
// THREE SEPARATE INVALIDATIONS - the central design of this module:
//
//   1. OBJECT change  (a primitive's version changed)
//        -> rebuild that primitive's WORLD mesh only. Everything else is reused
//           by identity; the BVH is rebuilt only when the box set changed.
//   2. VIEW change    (pan / zoom / DPI / resize / clip)
//        -> re-project the cached world meshes into a device-space batch. No
//           curve flattening, no glyph sampling, no BVH rebuild.
//   3. NOTHING changed -> reuse the previous device batch verbatim.
//
// A drag preview is case 1 with the CONTENT scene byte-identical: the overlay
// scene changes, the content meshes are hits, so the base scene is not rebuilt.
// =============================================================================

#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"

#include <map>
#include <vector>

namespace r62 {
namespace render {

// Current-view independent cached geometry for one primitive.
struct CachedMesh {
    geom::Mesh fill;    // world triangles
    geom::Mesh stroke;  // world triangles for strokes (kept separate for colour)
    core::Box   worldBounds;
    uint64_t    version;
    uint64_t    signature;      // geometry/style signature, view-dependent only for curves
    uint64_t    sourceSerial;   // documents structure serial at build time
    bool        valid;
    CachedMesh() : version(0), signature(0), sourceSerial(0), valid(false) {}
};

// One device-space draw range inside a batch. All vertices are absolute device
// pixels, so a batch is submitted with an identity projection.
struct DrawRange {
    int      first;       // first vertex index
    int      count;       // vertex count
    size_t primitiveIndex = size_t(-1); // source contour for compatible rasterization
    uint32_t rgba;        // flat colour (per-vertex colour is only used by overlays)
    uint8_t  primKind;    // for stats
    bool     triangles;   // true == GL_TRIANGLES, false == GL_LINES
    DrawRange() : first(0), count(0), rgba(0xFFFFFFFFu), primKind(0), triangles(true) {}
};

struct Batch {
    std::vector<float>    triangleVertices;   // x,y device pairs, GL_TRIANGLES
    std::vector<float>    lineVertices;       // x,y device pairs, GL_LINES
    std::vector<DrawRange> triangleRanges;
    std::vector<DrawRange> lineRanges;
    size_t primitiveCount;
    Batch() : primitiveCount(0) {}
    void clear() {
        triangleVertices.clear(); lineVertices.clear();
        triangleRanges.clear(); lineRanges.clear(); primitiveCount = 0;
    }
    size_t triangleCount() const { return triangleVertices.size() / 6; }
    size_t lineCount() const { return lineVertices.size() / 4; }
};

// What the batcher did on the last build, so a caller can assert selectivity.
struct BatchStats {
    size_t cachedMeshes;        // entries in the identity cache
    size_t meshRebuilds;        // world meshes regenerated this build
    size_t meshReuses;          // world meshes served from cache
    size_t viewRebuilds;        // vertices re-projected this build
    size_t glyphSamples;
    size_t glyphReuses;
    size_t bvhRebuilds;
    size_t culled;
    size_t drawn;
    size_t triangles;
    size_t drawCalls;
    BatchStats() { reset(); }
    void reset() {
        cachedMeshes = meshRebuilds = meshReuses = viewRebuilds = 0;
        glyphSamples = glyphReuses = bvhRebuilds = culled = drawn = 0;
        triangles = drawCalls = 0;
    }
};

// A CullIndex implementation over the legacy-style BVH. Kept header-only so the
// batch pipeline pulls in no extra translation unit.
class SimpleCullIndex : public CullIndex {
public:
    SimpleCullIndex() : leafSize_(8) {}
    void rebuild(const std::vector<core::Box>& boxes);
    void query(const core::Box& viewport, std::vector<size_t>* out) const;
    size_t size() const { return boxes_.size(); }
    size_t nodeCount() const { return nodes_.size(); }
private:
    struct Node { core::Box box; size_t begin, end, left, right; bool leaf; };
    size_t leafSize_;
    std::vector<core::Box> boxes_;
    std::vector<size_t>    ids_;
    std::vector<Node>      nodes_;
    size_t buildNode(size_t begin, size_t end);
    void   queryNode(size_t at, const core::Box& v, std::vector<size_t>* out) const;
};

// Pure linear fill contours can be consumed directly by the CPU renderer.
inline bool directContourPrimitive(const Primitive& p){
    if(p.kind!=PrimKind::Polygon||!p.style.fill.enabled||p.style.stroke.enabled)return false;
    if(p.subpaths.empty())return false;
    if(p.style.fillRule!=FillRule::EvenOdd&&p.subpaths.size()!=1)return false;
    for(const auto& sub:p.subpaths)for(auto k:sub.kind)if(k!=0)return false;
    return true;
}

// The shared cache + batching engine.
//
// A SINGLE Batcher may serve several scenes (that is what the rasterizer does).
// Anonymous primitives - overlay marks with no document identity - are keyed by
// (scene slot, ordinal), NOT by ordinal alone, because ordinal 0 of one scene
// would otherwise collide with ordinal 0 of another and one scene's cached mesh
// would be served for the other's primitive. The scene slot is assigned per
// Scene object the first time the Batcher sees it and is stable thereafter.
class Batcher {
public:
    Batcher();

    // Bring world meshes up to date. Only primitives whose identity/version/
    // content signature changed are rebuilt. Returns the number of rebuilds.
    size_t sync(const Scene& scene, const View& view, const geom::TextMeasurer* measurer,
                BatchStats* stats, bool directContours=false, bool updateIndex=true);
    // Same ordered keys and tessellation view; update only changed meshes.
    // Does not maintain a culling index. Caller must use uncropped batching.
    bool syncSparse(const Scene& scene,const View& view,const std::vector<uint8_t>& dirty,BatchStats* stats);

    // Re-project cached world meshes for `view` into `batch`. Does NOT touch
    // world geometry. `force` rebuilds the device batch even when the view is
    // unchanged (used by the backend when it knows the framebuffer changed).
    void buildBatch(const Scene& scene, const View& view, bool force, Batch* batch,
                    BatchStats* stats, bool directContours=false, bool cull=true,
                    const Batch* previous=nullptr, const std::vector<uint8_t>* dirty=nullptr);

    // Interactive-edit fast path: replace only dirty primitives' device ranges
    // in an existing batch. Returns false if a primitive changed its triangle
    // count (the caller then falls back to a full/incremental rebuild).
    bool updateBatchInPlace(const Scene& scene, const View& view,
                            const std::vector<uint8_t>& dirty, Batch* batch,
                            BatchStats* stats);

    // Rebuild the world meshes of only the primitives marked dirty. Used with the
    // structural fast path, where walking every primitive just to discover that
    // almost nothing changed costs more than the edit itself.
    bool syncDirty(const Scene& scene, const View& view,
                   const std::vector<uint8_t>& dirty, BatchStats* stats);

    // Structural-edit fast path with STABLE device vertices: unchanged
    // primitives keep their range offsets, so the vertex array is never shifted.
    // Changed/new primitives are appended at the end of the array and drawn at
    // their scene position by the backend. `appendedFrom` receives the first
    // vertex that is new, so only that tail has to reach the GPU.
    bool updateBatchStructural(const Scene& scene, const View& view,
                               const std::vector<uint8_t>& dirty,
                               const std::vector<uint64_t>& previousKeys,
                               Batch* batch, size_t* appendedFrom, BatchStats* stats);

    // Structural-edit fast path: the primitive count or painter order changed,
    // so ranges move, but unchanged primitives still project to the same device
    // vertices. Rebuild the batch by copying those vertices from `previous`
    // (located by the stable cache key of `previousKeys`) and re-projecting
    // only the primitives marked dirty. Returns false when the keys are not
    // unique or the previous batch cannot be indexed, so the caller falls back
    // to a full build. `firstChangedVertex` receives the first vertex offset
    // whose device value may differ from the previous batch, so the backend can
    // refresh only the tail of its vertex buffer.
    bool rebuildBatchReusing(const Scene& scene, const View& view,
                             const std::vector<uint8_t>& dirty,
                             const std::vector<uint64_t>& previousKeys,
                             const Batch& previous, Batch* batch,
                             size_t* firstChangedVertex, BatchStats* stats);

    // Offsets added by the caller to shift a whole scene (used by nothing today;
    // kept so a future multi-viewport shell does not need a second Batcher).
    void setOriginPx(double x, double y) { originX_ = x; originY_ = y; }

    // The index of the most recently synced scene, for a single-scene caller.
    // Diagnostics: which slot a scene maps to, so a test can prove the content
    // and overlay caches really are separate.
    uint32_t slotOf(const Scene& scene) const { return slotFor(scene); }
    size_t sceneSlotCount() const { return sceneSlots_.size(); }
    size_t sceneCount() const { return scenes_.size(); }

    const SimpleCullIndex* cullIndex(uint32_t slot = 0) const {
        std::map<uint32_t, SceneCache>::const_iterator it = scenes_.find(slot);
        return it == scenes_.end() ? 0 : &it->second.index;
    }
    size_t cachedCount() const {
        size_t n = 0;
        for (std::map<uint32_t, SceneCache>::const_iterator it = scenes_.begin();
             it != scenes_.end(); ++it) n += it->second.meshes.size();
        return n;
    }
    // Add one stats record into an accumulator. A caller that syncs several
    // scenes in one frame (content + overlay) uses this to report the FRAME total
    // rather than only the last sync's numbers. Per-call values in `from` are
    // unchanged, so a single-sync caller can read them directly.
    static void accumulate(BatchStats* into, const BatchStats& from);

    // Drop cached meshes for objects no longer present. Called by sync().
    void prune(const Scene& scene);
    void clear();

private:
    struct Key {
        int      kind;
        uint32_t sceneSlot; // distinguishes scenes sharing one Batcher
        uint32_t objectKind;
        uint32_t objectId;
        int64_t  ordinal;   // overlay primitives have no document id
        bool operator<(const Key& o) const {
            if (kind != o.kind) return kind < o.kind;
            if (sceneSlot != o.sceneSlot) return sceneSlot < o.sceneSlot;
            if (objectKind != o.objectKind) return objectKind < o.objectKind;
            if (objectId != o.objectId) return objectId < o.objectId;
            return ordinal < o.ordinal;
        }
    };
    Key keyFor(const Primitive& p, uint32_t sceneSlot, double viewScale) const;

    // Stable per-Scene slot. Const-correctness is preserved by keying on the
    // address, which is stable for the lifetime of the Scene.
    uint32_t slotFor(const Scene& scene) const {
        std::map<const Scene*, uint32_t>::iterator it = sceneSlots_.find(&scene);
        if (it != sceneSlots_.end()) return it->second;
        const uint32_t slot = static_cast<uint32_t>(sceneSlots_.size());
        sceneSlots_[&scene] = slot;
        return slot;
    }

    mutable std::map<const Scene*, uint32_t> sceneSlots_;

    // The culling index is PER SCENE. One Batcher may serve a content scene and
    // an overlay scene in the same frame, and a single shared BVH would be
    // replaced by the second sync() - leaving the first scene's buildBatch()
    // querying the other scene's boxes. That is a silent, total loss of drawing,
    // so the index is keyed by scene slot like the mesh cache.
    // One entry PER SCENE. `meshes` holds that scene's world geometry, `index`
    // its BVH, `boxes` the world boxes the BVH was built from. Keeping the mesh
    // cache per scene is what lets a content scene and an overlay scene share one
    // Batcher without either evicting the other - a single global cache made the
    // second sync() drop the first scene's meshes, which silently emptied the
    // base batch every frame.
    struct SceneCache {
        std::map<Key, CachedMesh> meshes;
        SimpleCullIndex           index;
        std::vector<core::Box>    boxes;
        bool built;bool directContours;
        SceneCache() : built(false),directContours(false) {}
    };

    std::map<uint32_t, SceneCache> scenes_;
    uint64_t                  indexSignature_;
    double                    originX_, originY_;
    uint64_t                  lastViewHash_;
    bool                      haveView_;

    SceneCache& cacheFor(uint32_t slot) { return scenes_[slot]; }

    static uint64_t viewHash(const View& view, size_t primCount);
    static void buildPrimitiveMeshes(const Primitive& prim, const View& view,
                                     const geom::TextMeasurer* measurer, CachedMesh* out,
                                     size_t* glyphSamples, size_t* glyphReuses);
    static void appendDevice(const geom::Mesh& world, const core::ViewTransform& t,
                             double ox, double oy, bool lines, Batch* batch, uint32_t rgba,
                             uint8_t primKind);
};

} // namespace render
} // namespace r62

#endif // R62_NEXT_RENDER_BATCHER_HPP
