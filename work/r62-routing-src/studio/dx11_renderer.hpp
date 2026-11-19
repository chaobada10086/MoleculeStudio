#ifndef STUDIO_DX11_RENDERER_HPP
#define STUDIO_DX11_RENDERER_HPP
// Direct3D 11 rasterizing backend for Molecule Studio.
//
// The scene that reaches a backend is already fully tessellated: fills, strokes
// and glyph outlines arrive as device-space triangles.  This backend therefore
// only replaces the rasterizer (GDI+ software coverage in compatible_renderer.hpp)
// with the GPU, while deliberately keeping the exact same mesh cache, batching,
// painter order, background rectangles and embedded-metafile compositing.  That
// is what makes the DX11 image match the reference image instead of merely
// looking similar.
#include <windows.h>
#include <algorithm>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <gdiplus.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "next/render/batcher.hpp"
#include "embedded_images.hpp"

namespace dx11 {

struct GpuVertex { float x, y; uint32_t color; };
struct TexVertex { float x, y, u, v; };

class Renderer : public r62::render::Backend {
public:
    Renderer()
        : device_(nullptr), context_(nullptr), vs_(nullptr), ps_(nullptr), texVs_(nullptr),
          texPs_(nullptr), layout_(nullptr), texLayout_(nullptr), frameCb_(nullptr),
          raster_(nullptr), blendStraight_(nullptr), blendPremul_(nullptr), sampler_(nullptr),
          msaa_(nullptr), rtv_(nullptr), resolved_(nullptr), staging_(nullptr),
          width_(0), height_(0), sampleCount_(1), ready_(false),
          contentVb_(nullptr), contentVbCapacity_(0), contentVbCount_(0),
          overlayVb_(nullptr), overlayVbCapacity_(0), overlayVbCount_(0),
          memory_(nullptr), dib_(nullptr), oldDib_(nullptr), pixels_(nullptr),
          contentScene_(nullptr), overlayScene_(nullptr), contentKey_(0), contentCached_(false),
          contentCompactPending_(false),
          geometryKey_(0), geometryCached_(false),
          syncMicros_(0), batchMicros_(0), vertexMicros_(0),
          panCached_(false), panShiftX_(0), panShiftY_(0), lastViewValid_(false), panWhy_(0),
          lastBuildViewValid_(false), emfUse_(0) {}
    ~Renderer() { destroy(); }

    const char* name() const override { return "D3D11-MSAA"; }
    bool ready() const override { return ready_; }
    int samples() const { return sampleCount_; }
    uint64_t syncMicros() const { return syncMicros_; }
    uint64_t batchMicros() const { return batchMicros_; }
    uint64_t vertexMicros() const { return vertexMicros_; }
    // Phase timers: a whole-frame number cannot say whether a repaint is spent in
    // scene hashing, re-projection, rasterisation or the GPU->CPU readback.
    uint64_t hashMicros() const { return hashMicros_; }
    uint64_t foregroundMicros() const { return foregroundMicros_; }
    uint64_t resolveMicros() const { return resolveMicros_; }
    uint64_t readbackMicros() const { return readbackMicros_; }
    uint64_t presentMicros() const { return presentMicros_; }
    int samplesUsed() const { return sampleCount_; }
    // While a zoom gesture is running the previous device batch may be stretched
    // instead of re-tessellated; turning interactivity off asks for one exact
    // rebuild at the settled scale.
    void setInteractive(bool on) { if (interactive_ && !on) rebuildPending_ = true; interactive_ = on; }
    bool interactive() const { return interactive_; }
    // Same-binary A/B switch for the canvas benchmark: reproduces the previous
    // frame behaviour so before/after numbers never come from two different
    // builds of two different trees.
    static bool legacyPerf() {
        static const bool on = []() { const char* v = std::getenv("MOLECULE_STUDIO_PERF_LEGACY"); return v && v[0] == '1'; }();
        return on;
    }
    int panWhy() const { return panWhy_; }

    bool initialize(std::string* why) override {
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                       D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_10_0;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const char* warpEnv = std::getenv("MOLECULE_STUDIO_DX11_WARP");
        bool forceWarp = warpEnv && warpEnv[0] == '1';
        HRESULT hr = E_FAIL;
        if (!forceWarp) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, 4, D3D11_SDK_VERSION, &device_, &got, &context_);
        }
        if (FAILED(hr)) {
            releaseDevice();
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                   levels, 4, D3D11_SDK_VERSION, &device_, &got, &context_);
        }
        if (FAILED(hr) || !device_ || !context_) {
            if (why) *why = "D3D11CreateDevice failed (0x" + hex(hr) + ")";
            return false;
        }
        featureLevel_ = got;

        // Resolve the effective sample count now so diagnostics written before
        // the first paint (renderer-status.txt) report the real value.
        sampleCount_ = 1;
        {
            int maxSamples = 8;
            if (const char* msaaEnv = std::getenv("MOLECULE_STUDIO_DX11_MSAA")) {
                int v = std::atoi(msaaEnv);
                if (v >= 1 && v <= 8) maxSamples = v;
            }
            for (int s = maxSamples; s >= 1; --s) {
                if (s > 1 && s != 2 && s != 4 && s != 8) continue;
                UINT q = 0;
                if (s == 1 || (SUCCEEDED(device_->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, s, &q)) && q > 0)) {
                    sampleCount_ = s; break;
                }
            }
        }

        static const char* vsSrc =
            "cbuffer Frame : register(b0) { float2 viewport; float2 offset; float zoom; float3 padding; };\n"
            "struct VSIn { float2 pos : POSITION; float4 color : COLOR; };\n"
            "struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };\n"
            "VSOut VSMain(VSIn i) { VSOut o; o.pos = float4((i.pos.x * zoom + offset.x) * 2.0 / viewport.x - 1.0,"
            " 1.0 - (i.pos.y * zoom + offset.y) * 2.0 / viewport.y, 0.0, 1.0); o.color = i.color; return o; }\n"
            "float4 PSMain(VSOut i) : SV_Target { return i.color; }\n";
        static const char* texVsSrc =
            "cbuffer Frame : register(b0) { float2 viewport; float2 offset; float zoom; float3 padding; };\n"
            "struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VSOut VSMain(VSIn i) { VSOut o; o.pos = float4((i.pos.x * zoom + offset.x) * 2.0 / viewport.x - 1.0,"
            " 1.0 - (i.pos.y * zoom + offset.y) * 2.0 / viewport.y, 0.0, 1.0); o.uv = i.uv; return o; }\n";
        static const char* texPsSrc =
            "Texture2D emfTexture : register(t0);\n"
            "SamplerState emfSampler : register(s0);\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "float4 PSMain(VSOut i) : SV_Target { return emfTexture.Sample(emfSampler, i.uv); }\n";

        ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr;
        ID3DBlob* texVsBlob = nullptr; ID3DBlob* texPsBlob = nullptr;
        if (!compile(vsSrc, "VSMain", "vs_5_0", &vsBlob, why) ||
            !compile(texVsSrc, "VSMain", "vs_5_0", &texVsBlob, why) ||
            !compile("struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };"
                     "float4 PSMain(VSOut i) : SV_Target { return i.color; }\n",
                     "PSMain", "ps_5_0", &psBlob, why) ||
            !compile(texPsSrc, "PSMain", "ps_5_0", &texPsBlob, why)) {
            if (vsBlob) vsBlob->Release(); if (psBlob) psBlob->Release();
            if (texVsBlob) texVsBlob->Release(); if (texPsBlob) texPsBlob->Release();
            releaseDevice();
            return false;
        }
        hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
        if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);
        if (SUCCEEDED(hr)) hr = device_->CreateVertexShader(texVsBlob->GetBufferPointer(), texVsBlob->GetBufferSize(), nullptr, &texVs_);
        if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(texPsBlob->GetBufferPointer(), texPsBlob->GetBufferSize(), nullptr, &texPs_);
        if (SUCCEEDED(hr)) {
            D3D11_INPUT_ELEMENT_DESC elems[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
            };
            hr = device_->CreateInputLayout(elems, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout_);
            D3D11_INPUT_ELEMENT_DESC telems[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
            };
            if (SUCCEEDED(hr)) hr = device_->CreateInputLayout(telems, 2, texVsBlob->GetBufferPointer(), texVsBlob->GetBufferSize(), &texLayout_);
        }
        vsBlob->Release(); psBlob->Release(); texVsBlob->Release(); texPsBlob->Release();
        if (FAILED(hr)) { if (why) *why = "D3D11 shader/input layout creation failed (0x" + hex(hr) + ")"; releaseDevice(); return false; }

        D3D11_BUFFER_DESC cb = {};
        cb.ByteWidth = 32; cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device_->CreateBuffer(&cb, nullptr, &frameCb_);
        if (SUCCEEDED(hr)) {
            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.FrontCounterClockwise = FALSE;
            rd.DepthClipEnable = FALSE; rd.ScissorEnable = TRUE; rd.MultisampleEnable = TRUE;
            rd.AntialiasedLineEnable = FALSE;
            hr = device_->CreateRasterizerState(&rd, &raster_);
        }
        if (SUCCEEDED(hr)) {
            D3D11_BLEND_DESC bd = {};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            hr = device_->CreateBlendState(&bd, &blendStraight_);
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            if (SUCCEEDED(hr)) hr = device_->CreateBlendState(&bd, &blendPremul_);
        }
        if (SUCCEEDED(hr)) {
            D3D11_SAMPLER_DESC sd = {};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            hr = device_->CreateSamplerState(&sd, &sampler_);
        }
        if (FAILED(hr)) { if (why) *why = "D3D11 state creation failed (0x" + hex(hr) + ")"; releaseDevice(); return false; }
        ready_ = true;
        return true;
    }

    struct RotationTarget {uint64_t key;int mode;r62::core::Vec2 anchor;RotationTarget(uint64_t k,int m,r62::core::Vec2 a):key(k),mode(m),anchor(a){}};
    void setRotationPreview(const r62::render::Scene* source,const std::vector<RotationTarget>& targets,const double* matrix,uint64_t token){
        if(source!=rotationSource_||token!=rotationToken_){clearRotationMetadata();rotationTargets_.clear();for(const auto& target:targets)rotationTargets_.emplace(target.key,target);}
        rotationSource_=source;rotationToken_=token;if(matrix)std::copy(matrix,matrix+6,rotationMatrix_);
    }
    bool rotationPreviewApplied()const{return rotationApplied_;}
    void setInsertionPreview(bool enabled){
        insertionPreview_=enabled;
        if(!enabled){insertionRasterValid_=false;if(insertionRaster_){insertionRaster_->Release();insertionRaster_=nullptr;}}
    }
    size_t insertionRasterHits()const{return insertionRasterHits_;}
    int insertionRasterMissWhy()const{return insertionRasterMissWhy_;}
    void setCopyPreview(const r62::render::Scene* scene,r62::core::Vec2 offset,uint64_t token){
        if(copyScene_!=scene||copyToken_!=token){copyBatcher_.clear();copyGeneration_=copyViewKey_=0;copyTriangles_=0;copyVbCount_=0;
            if(!scene&&copyVb_){copyVb_->Release();copyVb_=nullptr;copyVbCapacity_=0;}}
        copyScene_=scene;copyToken_=token;copyOffset_=offset;
    }
    void sync(const r62::render::Scene&, const r62::render::View&) override {}
    void setView(const r62::render::View&) override {}
    // Only the canonical document scene has a revision supplied by Session.
    // Transient editing/drag scenes continue through full content validation.
    void setPanOverlay(const r62::render::Scene* scene,uint64_t token){
        if(panOverlayScene_!=scene||panOverlayToken_!=token||!scene)overlayPanValid_=false;
        panOverlayScene_=scene;panOverlayToken_=token;
    }
    void setSceneRevision(const r62::render::Scene* scene,uint64_t revision){
        if(revisionScene_!=scene||revision_!=revision){summaryValid_=false;revisionScene_=scene;revision_=revision;}
    }

    r62::core::Error draw(const r62::render::Scene& scene, const r62::render::Scene* overlay,
                          const r62::render::View& view, r62::render::RenderStats* stats) override {
        using namespace r62::render; using r62::core::Vec2;
        if (!ready_) return r62::core::Error(r62::core::ErrorCode::NotImplemented, "D3D11 backend not initialized");
        if (stats) *stats = RenderStats();
        if (!std::isfinite(view.widthPx) || !std::isfinite(view.heightPx) ||
            view.widthPx <= 0 || view.heightPx <= 0 || view.widthPx > 16384 || view.heightPx > 16384)
            return r62::core::Error(r62::core::ErrorCode::InvalidArgument, "invalid viewport");
        const int w = int(view.widthPx + 0.5), h = int(view.heightPx + 0.5);
        if (!ensureSize(w, h)) return r62::core::Error(r62::core::ErrorCode::IoError, "cannot allocate D3D11 render target");

        if (contentScene_ != &scene) {
            // dragScene / editingScene are copies of the same document.  Keep
            // the mesh cache when the first content primitive still matches;
            // clearing here rebuilt every mesh on the first drag frame (~700 ms).
            bool keepCache = false;
            if (!foreground_.primitives().empty()) {
                for (const auto& p : scene.primitives()) {
                    if (p.layer.value == 0 && p.kind == PrimKind::Rect) continue;
                    if (p.text.compare(0, 10, "CDXML-EMF:") == 0) continue;
                    const auto& previous = foreground_.primitives()[0];
                    keepCache = p.kind == previous.kind && p.source.valid() == previous.source.valid() &&
                                (!p.source.valid() || (p.source.id == previous.source.id && p.source.kind == previous.source.kind));
                    break;
                }
            }
            if (!keepCache) {
                content_.clear(); foreground_.clear();
                contentCached_ = false; contentKey_ = 0; contentVbCount_ = 0; contentCompactPending_ = false;
                geometryCached_ = false; geometryKey_ = 0; visibleContent_.clear();
            }
            // dragScene / editingScene are copies of the same document; keeping
            // the geometry and mesh caches lets the normal per-primitive key
            // comparison update only what really changed.  Clearing them here
            // re-projected the whole 12.7 M triangle scene on drag release.
            contentScene_ = &scene;
        }
        if (overlayScene_ != overlay) { overlay_.clear(); overlayScene_ = overlay; }

        static LONGLONG qpf = 0; if (!qpf) QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&qpf));
        auto tick = [&]() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart * 1000000 / qpf; };
        LONGLONG t0 = tick();

        // Compose the same "foreground" scene the reference backend rasterizes:
        // opaque backdrop rectangles and CDXML metafiles are composited outside
        // the triangle list, exactly as compatible_renderer.hpp does.
        uint64_t key = 1469598103934665603ull;
        // Word-at-a-time mixing. The content key is recomputed for every
        // primitive on every frame; a byte loop over ~90k primitive records cost
        // several milliseconds of otherwise idle CPU per repaint. Reading 8 bytes
        // at a time keeps "any byte difference changes the key".
        const bool legacy = legacyPerf();
        auto hash = [&](const void* ptr, size_t length) {
            const unsigned char* bytes = static_cast<const unsigned char*>(ptr);
            if (legacy) {
                for (size_t i = 0; i < length; ++i) { key ^= bytes[i]; key *= 1099511628211ull; }
                return;
            }
            size_t i = 0;
            for (; i + 8 <= length; i += 8) {
                uint64_t w; std::memcpy(&w, bytes + i, 8);
                key ^= w; key *= 1099511628211ull; key = (key << 29) | (key >> 35);
            }
            for (; i < length; ++i) { key ^= bytes[i]; key *= 1099511628211ull; }
        };
        for (double d : { view.transform.center.x, view.transform.center.y,
                          view.transform.viewportCenter.x, view.transform.viewportCenter.y,
                          view.transform.scale, view.transform.dpiScale,
                          view.widthPx, view.heightPx, view.defaultStrokeWidth })
            hash(&d, sizeof(d));
        hash(&view.clipEnabled, sizeof(view.clipEnabled));
        hash(&view.clipMin, sizeof(view.clipMin));
        hash(&view.clipMax, sizeof(view.clipMax));
        const uint64_t viewKey = key;
        key = 1469598103934665603ull;
        // Only PrimKind::Curve meshes are generated at a view-dependent tolerance
        // (batcher primitiveSignature).  Every other primitive's world mesh - bonds,
        // glyph outlines, filled paths - is scale independent, so re-projecting the
        // batch for a new zoom level reproduces exactly what scaling the existing
        // device vertices already draws.  Knowing this lets a gesture end without a
        // 340 ms full re-projection.
        bool hasCurves = false;
        const bool reuseSummary = !std::getenv("MOLECULE_STUDIO_PAN_LEGACY") && summaryValid_ && revisionScene_==&scene && summaryGeneration_==scene.generation();
        if(reuseSummary){key=summaryKey_;hasCurves=summaryCurves_;}
        else {
        backdropIndices_.clear();metafileIndices_.clear();
        for (size_t pi=0;pi<scene.size();++pi) {
            const auto& p=scene.primitives()[pi];
            if(p.layer.value==0&&p.kind==PrimKind::Rect)backdropIndices_.push_back(pi);
            if(p.kind==PrimKind::Rect&&p.text.compare(0,10,"CDXML-EMF:")==0)metafileIndices_.push_back(pi);
            if (p.layer.value == 0 && p.kind == PrimKind::Rect) continue;
            if (p.text.compare(0, 10, "CDXML-EMF:") == 0) continue;
            if (p.kind == PrimKind::Curve) hasCurves = true;
            hash(&p.version, sizeof(p.version)); hash(&p.kind, sizeof(p.kind));
            hash(&p.layer.value, sizeof(p.layer.value)); hash(&p.z, sizeof(p.z));
            hash(&p.style.fill.rgba, sizeof(p.style.fill.rgba));
            hash(&p.style.fill.enabled, sizeof(p.style.fill.enabled));
            hash(&p.style.stroke.rgba, sizeof(p.style.stroke.rgba));
            hash(&p.style.stroke.enabled, sizeof(p.style.stroke.enabled));
            hash(&p.style.strokeWidth, sizeof(p.style.strokeWidth));
            if (!p.version) {
                for (auto v : p.points) hash(&v, sizeof(v));
                for (const auto& sub : p.subpaths) for (auto v : sub.points) hash(&v, sizeof(v));
            }
        }
        }
        summaryValid_=revisionScene_==&scene;
        summaryKey_=key;summaryCurves_=hasCurves;summaryGeneration_=scene.generation();
        const uint64_t geometryKey = key;
        const uint64_t contentHash = key ^ (viewKey * 1099511628211ull);
        contentHasCurves_ = hasCurves;
        hashMicros_ = uint64_t(tick() - t0);

        BatchStats bs;
        // A pure pan keeps every device-space triangle valid; only a uniform
        // translation is needed.  Reusing the vertex buffer turns a 3165-atom
        // pan from a full re-projection into a constant-buffer update.
        bool scaleEq = lastViewValid_ && view.transform.scale == lastView_.transform.scale;
        bool dpiEq = lastViewValid_ && view.transform.dpiScale == lastView_.transform.dpiScale;
        bool vcEq = lastViewValid_ && view.transform.viewportCenter.x == lastView_.transform.viewportCenter.x && view.transform.viewportCenter.y == lastView_.transform.viewportCenter.y;
        bool whEq = lastViewValid_ && view.widthPx == lastView_.widthPx && view.heightPx == lastView_.heightPx;
        bool clipEq = lastViewValid_ && view.clipEnabled == lastView_.clipEnabled && view.clipMin.x == lastView_.clipMin.x && view.clipMin.y == lastView_.clipMin.y && view.clipMax.x == lastView_.clipMax.x && view.clipMax.y == lastView_.clipMax.y;
        bool centerDiff = lastViewValid_ && (view.transform.center.x != lastView_.transform.center.x || view.transform.center.y != lastView_.transform.center.y);
        panWhy_ = (contentCached_ ? 1 : 0) | (geometryCached_ ? 2 : 0) | (scaleEq ? 4 : 0) | (dpiEq ? 8 : 0) | (vcEq ? 16 : 0) | (whEq ? 32 : 0) | (clipEq ? 64 : 0) | (centerDiff ? 128 : 0) | (geometryKey == geometryKey_ ? 256 : 0);
        const char* noPanEnv = std::getenv("MOLECULE_STUDIO_DX11_NO_PAN");
        // Reuse uncropped vector vertices for affine view changes. Build at
        // twice the current scale so curved paths retain their flattening
        // precision throughout the reuse interval (0.25..1 of build scale).
        syncMicros_ = batchMicros_ = vertexMicros_ = 0;
        const double currentScale = view.transform.scale * view.transform.dpiScale;
        const double builtScale = lastBuildViewValid_ ? lastBuildView_.transform.scale * lastBuildView_.transform.dpiScale : 0;
        const double ratio = builtScale > 0 ? currentScale / builtScale : 0;
        // A settled gesture never keeps a stretched frame: the exact rebuild runs
        // before anything else can be shown.
        // The settle rebuild only exists to regenerate view-dependent meshes.
        // A scene made of bonds, glyph outlines and filled paths has none, so the
        // stretched batch is already the exact image and must not be rebuilt.
        if (rebuildPending_) { rebuildPending_ = false; if (ratio > 1.15 && contentHasCurves_) contentCached_ = false; }
        // Down-scaling a device batch is always safe - its curves were flattened
        // finer than one device pixel at the build scale - so a zoom-out gesture
        // needs no rebuild at all. Up-scaling costs glyph/flattening quality and is
        // limited to the gesture, then repaid by the settle rebuild.
        const double reuseLow = legacy ? .25 : .02;
        // Curve-free scenes (bonds, glyph outlines, filled paths) are completely
        // scale independent, so their cached device batch can serve any zoom and no
        // rebuild is ever needed; view-dependent curve meshes keep the tighter
        // interactive window and the settle rebuild.
        const double reuseHigh = legacy ? 1.0
                            : (contentHasCurves_ ? (interactive_ ? 2.5 : 1.0) : 8.0);
        bool purePan = !(noPanEnv && noPanEnv[0] == '1') && lastBuildViewValid_ &&
            contentCached_ && geometryCached_ && geometryKey == geometryKey_ &&
            ratio >= reuseLow && ratio <= reuseHigh &&
            view.defaultStrokeWidth == lastBuildView_.defaultStrokeWidth;
        if (purePan) contentKey_ = contentHash;

        auto stamp = [&](const Primitive& p) -> uint64_t {
            uint64_t h = p.version;
            auto mix = [&](const void* data, size_t size) {
                const unsigned char* b = static_cast<const unsigned char*>(data);
                if (legacy) {
                    for (size_t k = 0; k < size; ++k) { h ^= b[k]; h *= 1099511628211ull; }
                    return;
                }
                size_t k = 0;
                for (; k + 8 <= size; k += 8) {
                    uint64_t w; std::memcpy(&w, b + k, 8);
                    h ^= w; h *= 1099511628211ull; h = (h << 29) | (h >> 35);
                }
                for (; k < size; ++k) { h ^= b[k]; h *= 1099511628211ull; }
            };
            mix(&p.kind, sizeof(p.kind));
            mix(&p.cacheKey, sizeof(p.cacheKey));
            mix(&p.layer.value, sizeof(p.layer.value));
            mix(&p.z, sizeof(p.z));
            if (!p.version) {
                size_t points = p.points.size();
                mix(&points, sizeof(points));
                mix(&p.bounds.min.x, sizeof(double)); mix(&p.bounds.min.y, sizeof(double));
                mix(&p.bounds.max.x, sizeof(double)); mix(&p.bounds.max.y, sizeof(double));
            }
            mix(&p.style.fill.rgba, sizeof(p.style.fill.rgba));
            mix(&p.style.fill.enabled, sizeof(p.style.fill.enabled));
            mix(&p.style.stroke.rgba, sizeof(p.style.stroke.rgba));
            mix(&p.style.stroke.enabled, sizeof(p.style.stroke.enabled));
            mix(&p.style.strokeWidth, sizeof(p.style.strokeWidth));
            return h;
        };

        // Rebuild the foreground scene only when document geometry changes.  A
        // pan/zoom keeps the same primitives and only re-projects them, exactly
        // like the GDI+ backend's geometryValid_/vectorKey_ split.
        if (!purePan) {
            if (!geometryCached_ || geometryKey != geometryKey_) {
                std::vector<int> map;
                map.reserve(scene.primitives().size());
                for (size_t i = 0; i < scene.primitives().size(); ++i) {
                    const auto& p = scene.primitives()[i];
                    if (p.layer.value == 0 && p.kind == PrimKind::Rect) continue;
                    if (p.text.compare(0, 10, "CDXML-EMF:") != 0) map.push_back(int(i));
                }
                bool sameMap = map == contentMap_ && map.size() == foreground_.size();
                if (sameMap && !map.empty()) {
                    for (size_t i = 0; i < map.size(); ++i) {
                        const auto& p = scene.primitives()[size_t(map[i])];
                        const auto& kept = foreground_.primitives()[i];
                        if (!legacy && p.version && p.version == kept.version && p.cacheKey == kept.cacheKey) continue;
                        if (stamp(p) != stamp(kept)) {
                            foreground_.mutablePrimitives()[i] = p;
                        }
                    }
                }
                if (!sameMap) {
                    // Reuse the existing element storage: a structural edit changes
                    // the map by one entry, and reallocating 90k primitives here cost
                    // ~20 ms per edit on a large page.
                    auto& dst = foreground_.mutablePrimitives();
                    dst.resize(map.size());
                    for (size_t i = 0; i < map.size(); ++i)
                        dst[i] = scene.primitives()[size_t(map[i])];
                    foreground_.touch();
                }
                contentMap_.swap(map);
                geometryKey_ = geometryKey; geometryCached_ = true;
                contentCached_ = false;
                foregroundMicros_ = uint64_t(tick() - t0) - hashMicros_;
            }
            if (!contentCached_ || contentHash != contentKey_) {
                View buildView = view; buildView.transform.scale *= 2;
                // The batch contains uncropped geometry. Edits can keep its
                // coordinate system even after a pan or a small zoom.
                // Keep appending into the existing build space for exactly the range
                // of views the cached batch is still valid for.  Using a narrower
                // window here made the first geometry change after a zoom-out pad a
                // full 300 MB re-projection instead of an incremental update.
                if (lastBuildViewValid_ && ratio >= reuseLow && ratio <= reuseHigh &&
                    view.defaultStrokeWidth == lastBuildView_.defaultStrokeWidth)
                    buildView = lastBuildView_;
                LONGLONG tSync = tick();
                bool sameBuildView = lastBuildViewValid_ &&
                    buildView.transform.center.x == lastBuildView_.transform.center.x &&
                    buildView.transform.center.y == lastBuildView_.transform.center.y &&
                    buildView.transform.scale == lastBuildView_.transform.scale &&
                    buildView.transform.dpiScale == lastBuildView_.transform.dpiScale &&
                    buildView.transform.viewportCenter.x == lastBuildView_.transform.viewportCenter.x &&
                    buildView.transform.viewportCenter.y == lastBuildView_.transform.viewportCenter.y &&
                    buildView.widthPx == lastBuildView_.widthPx && buildView.heightPx == lastBuildView_.heightPx &&
                    buildView.clipEnabled == lastBuildView_.clipEnabled &&
                    buildView.clipMin.x == lastBuildView_.clipMin.x && buildView.clipMin.y == lastBuildView_.clipMin.y &&
                    buildView.clipMax.x == lastBuildView_.clipMax.x && buildView.clipMax.y == lastBuildView_.clipMax.y &&
                    buildView.defaultStrokeWidth == lastBuildView_.defaultStrokeWidth;
                const size_t oldCount = contentStamps_.size();
                bool orderSame = oldCount == foreground_.size() && contentKeys_.size() == oldCount;
                for (size_t i = 0; orderSame && i < oldCount; ++i)
                    if (foreground_.primitives()[i].cacheKey != contentKeys_[i]) orderSame = false;
                bool incremental = sameBuildView && !contentBatch_.triangleVertices.empty() &&
                                   foreground_.size() > 0 && oldCount > 0;
                const char* noIncremental = std::getenv("MOLECULE_STUDIO_DX11_NO_INCREMENTAL");
                if (noIncremental && noIncremental[0] == '1') incremental = false;
                size_t dirtyCount = 0;
                if (incremental) {
                    contentDirty_.assign(foreground_.size(), 0);
                    if (orderSame) {
                        for (size_t i = 0; i < oldCount; ++i) {
                            const auto& p = foreground_.primitives()[i];
                            // A primitive whose content-hash version is unchanged cannot
                            // have changed at all, so the full stamp can be skipped: an
                            // interactive drag frame scans ~90k 8-byte compares instead
                            // of ~90k full hashes.
                            if (!legacy && p.version && p.version == contentVersions_[i]) continue;
                            if (stamp(p) != contentStamps_[i]) {
                                contentDirty_[i] = 1; ++dirtyCount;
                            }
                        }
                        // Nothing moved: the cached batch is already correct.
                        if (!dirtyCount) incremental = false;
                    } else {
                        // A structural edit (insert/delete/reorder) shifts indices.
                        // Match primitives by their stable cache key instead of by
                        // position, so only the objects that really changed are
                        // re-projected; everything else keeps its device vertices.
                        std::unordered_map<uint64_t, size_t> oldByKey;
                        oldByKey.reserve(oldCount * 2);
                        bool unique = true;
                        for (size_t i = 0; i < oldCount && unique; ++i) {
                            if (!contentKeys_[i] || !oldByKey.emplace(contentKeys_[i], i).second)
                                unique = false;
                        }
                        if (!unique) incremental = false;
                        else {
                            for (size_t i = 0; i < foreground_.size(); ++i) {
                                const auto& p = foreground_.primitives()[i];
                                std::unordered_map<uint64_t, size_t>::const_iterator it =
                                    p.cacheKey ? oldByKey.find(p.cacheKey) : oldByKey.end();
                                if (!legacy && it != oldByKey.end() && p.version && p.version == contentVersions_[it->second]) continue;
                                if (it == oldByKey.end() || stamp(p) != contentStamps_[it->second]) {
                                    contentDirty_[i] = 1; ++dirtyCount;
                                }
                            }
                        }
                    }
                }
                // Only the dirty meshes are rebuilt. Walking all 90k primitives to
                // discover that almost nothing changed cost ~55 ms on a large page.
                bool synced = incremental && content_.syncDirty(foreground_, buildView, contentDirty_, &bs);
                if (!synced) content_.sync(foreground_, buildView, nullptr, &bs, false, false);
                LONGLONG tBatch = tick();
                bool vbRangesUpdated = false;
                bool inPlace = incremental && orderSame && content_.updateBatchInPlace(foreground_, buildView, contentDirty_, &contentBatch_, &bs);
                if (inPlace) vbRangesUpdated = updateVertexBufferRanges(contentDirty_);
                if (!inPlace) {
                    bool updated = false;
                    if (incremental && !contentCompactPending_) {
                        // Structural fast path: unchanged primitives keep their device
                        // vertices; only changed/new primitives are appended. Ranges are
                        // reordered instead of the 300 MB vertex array being shifted.
                        const size_t size = contentBatch_.triangleVertices.size();
                        // An edit appends only a few dozen/hundred vertices. Grow with
                        // plenty of headroom when the slack is nearly gone, so the first
                        // edit after opening a large file never reallocates the arena.
                        const size_t need = size + 65536;
                        if (contentBatch_.triangleVertices.capacity() < need) {
                            const size_t grow = size + size / 2 + 16384;
                            if (std::getenv("MOLECULE_STUDIO_PERF_TRACE"))
                                std::fprintf(stderr, "[arena] realloc %.1fMB -> %.1fMB\n",
                                             double(size * sizeof(float)) / 1048576.0, double(grow * sizeof(float)) / 1048576.0);
                            contentBatch_.triangleVertices.reserve(grow);
                        }
                        size_t appendedFrom = size / 2;
                        updated = content_.updateBatchStructural(foreground_, buildView, contentDirty_, contentKeys_,
                                                                 &contentBatch_, &appendedFrom, &bs);
                        if (updated) {
                            appendContentVerticesFrom(appendedFrom);
                            vbRangesUpdated = true;
                            size_t live = 0;
                            for (const auto& r : contentBatch_.triangleRanges) live += size_t(r.count);
                            const size_t total = contentBatch_.triangleVertices.size() / 2;
                            // Bound the append-only arena: when most of it is garbage a
                            // later edit compacts it by doing one full rebuild.
                            if (live * 2 < total) contentCompactPending_ = true;
                        }
                    }
                    if (!updated) {
                        contentCompactPending_ = false;
                        contentBatch_.clear();
                        if (!synced) content_.sync(foreground_, buildView, nullptr, &bs, false, false);
                        // No culling: a later pan may reveal primitives outside this
                        // viewport, and the cached device batch is translated instead of
                        // rebuilt.  The GPU scissor still clips to the real viewport.
                        content_.buildBatch(foreground_, buildView, false, &contentBatch_, &bs, false, false);
                    }
                }
                LONGLONG tVb = tick();
                if (!vbRangesUpdated) {
                    buildVertexBuffer(contentBatch_, true);
                    // Leave headroom so the first structural edit after a full build
                    // does not have to reallocate the whole device-vertex arena.
                    const size_t size = contentBatch_.triangleVertices.size();
                    if (size && contentBatch_.triangleVertices.capacity() < size + size / 8 + 4096)
                        contentBatch_.triangleVertices.reserve(size + size / 2 + 16384);
                }
                syncMicros_ = uint64_t(tBatch - tSync);
                batchMicros_ = uint64_t(tVb - tBatch);
                vertexMicros_ = uint64_t(tick() - tVb);
                contentKey_ = contentHash; contentCached_ = true;
                contentStamps_.resize(foreground_.size());
                contentKeys_.resize(foreground_.size());
                contentVersions_.resize(foreground_.size());
                for (size_t i = 0; i < foreground_.size(); ++i) {
                    contentStamps_[i] = stamp(foreground_.primitives()[i]);
                    contentKeys_[i] = foreground_.primitives()[i].cacheKey;
                    contentVersions_[i] = foreground_.primitives()[i].version;
                }
                lastBuildView_ = buildView; lastBuildViewValid_ = true;
                panShiftX_ = panShiftY_ = 0;
            }
        }
        const double contentZoom = currentScale / (lastBuildView_.transform.scale * lastBuildView_.transform.dpiScale);
        panShiftX_ = view.transform.viewportCenter.x - lastBuildView_.transform.viewportCenter.x * contentZoom +
            (lastBuildView_.transform.center.x - view.transform.center.x) * currentScale;
        panShiftY_ = view.transform.viewportCenter.y - lastBuildView_.transform.viewportCenter.y * contentZoom +
            (lastBuildView_.transform.center.y - view.transform.center.y) * currentScale;
        panCached_ = (panShiftX_ != 0 || panShiftY_ != 0 || contentZoom != 1);

        BatchStats os;
        Batch overlayBatch;
        const bool stableOverlay=overlay&&overlay==panOverlayScene_;
        const bool reuseOverlay=stableOverlay&&overlayPanValid_&&
            view.transform.scale==overlayPanView_.transform.scale&&view.transform.dpiScale==overlayPanView_.transform.dpiScale;
        if (overlay&&!reuseOverlay) {
            overlay_.sync(*overlay, view, nullptr, &os, false);
            // Panning must retain offscreen handles so newly exposed edges draw.
            overlay_.buildBatch(*overlay, view, false, &overlayBatch, &os, false,!stableOverlay);
            buildVertexBuffer(overlayBatch, false);
            overlayPanView_=view;overlayPanValid_=stableOverlay;
        } else if(!overlay) {
            overlayVbCount_ = 0;overlayPanValid_=false;
        }
        BatchStats cs;
        if(copyScene_&&(copyGeneration_!=copyScene_->generation()||copyViewKey_!=viewKey||!copyVb_)){
            Batch copy;copyBatcher_.sync(*copyScene_,view,nullptr,&cs,false,false);
            copyBatcher_.buildBatch(*copyScene_,view,false,&copy,&cs,false,false);
            buildVertexBuffer(copy,false,true);copyTriangles_=copy.triangleCount();
            copyGeneration_=copyScene_->generation();copyViewKey_=viewKey;
        }
        LONGLONG tBatch = tick();

        context_->OMSetRenderTargets(1, &rtv_, nullptr);
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f };
        context_->RSSetViewports(1, &vp);
        D3D11_RECT rect = { 0, 0, w, h };
        if (view.clipEnabled) {
            LONG x0 = LONG(std::floor(view.clipMin.x)), y0 = LONG(std::floor(view.clipMin.y));
            LONG x1 = LONG(std::ceil(view.clipMax.x)), y1 = LONG(std::ceil(view.clipMax.y));
            if (x1 < x0) std::swap(x0, x1); if (y1 < y0) std::swap(y0, y1);
            rect.left = std::max<LONG>(0, std::min<LONG>(w, x0));
            rect.top = std::max<LONG>(0, std::min<LONG>(h, y0));
            rect.right = std::max<LONG>(0, std::min<LONG>(w, x1));
            rect.bottom = std::max<LONG>(0, std::min<LONG>(h, y1));
        }
        D3D11_RECT sr = rect;
        context_->RSSetScissorRects(1, &sr);
        context_->RSSetState(raster_);
        const float clear[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        // Copying a full multisample surface costs more than drawing a tiny
        // document. Reserve the raster cache for geometry-heavy backgrounds.
        const bool cacheRaster=insertionPreview_&&contentBatch_.triangleCount()>200000&&rotationSource_!=&scene;
        const bool reuseRaster=cacheRaster&&insertionRaster_&&insertionRasterValid_&&insertionRasterScene_==&scene&&insertionRasterGeneration_==scene.generation()&&insertionRasterRevision_==revision_&&insertionRasterView_==viewKey&&insertionRasterContent_==contentHash;
        insertionRasterMissWhy_=(!cacheRaster?128:0)|(!insertionRaster_?1:0)|(!insertionRasterValid_?2:0)|(insertionRasterScene_!=&scene?4:0)|(insertionRasterGeneration_!=scene.generation()?8:0)|(insertionRasterRevision_!=revision_?16:0)|(insertionRasterView_!=viewKey?32:0)|(insertionRasterContent_!=contentHash?64:0);
        if(reuseRaster){context_->OMSetRenderTargets(0,nullptr,nullptr);context_->CopyResource(msaa_,insertionRaster_);context_->OMSetRenderTargets(1,&rtv_,nullptr);++insertionRasterHits_;}
        else context_->ClearRenderTargetView(rtv_, clear);
        context_->OMSetBlendState(blendStraight_, nullptr, 0xffffffff);

        // The reference uses GDI+ PixelOffsetModeHalf, so device-space vertices
        // already use the same pixel-centre convention as the D3D11 viewport.
        updateFrameConstants(float(w), float(h), 0.0f, 0.0f);
        context_->VSSetConstantBuffers(0, 1, &frameCb_);
        bindColorPipeline();

        if(!reuseRaster){
        // 1. backdrop / page rectangles (layer 0 Rects, exactly like the reference)
        {
            std::vector<GpuVertex> verts;
            for (auto pi : backdropIndices_) {
                const auto& p=scene.primitives()[pi];
                if (p.layer.value != 0 || p.kind != PrimKind::Rect || p.points.size() < 2) continue;
                auto project = [&](Vec2 x) { return (x - view.transform.center) * (view.transform.scale * view.transform.dpiScale) + view.transform.viewportCenter; };
                Vec2 a = project(p.points[0]), b = project(p.points[1]);
                uint32_t c = gpuColor(p.style.fill.rgba);
                pushQuad(verts, float(a.x), float(a.y), float(b.x), float(b.y), c);
            }
            if (!verts.empty()) drawVertices(verts);
        }

        // 2. embedded metafiles behind the vector content
        drawMetafiles(scene, view, -1);
        // 3. vector content (translated by the pan fast path)
        if (contentVbCount_) {
            updateFrameConstants(float(w), float(h), panCached_ ? float(panShiftX_) : 0.0f, panCached_ ? float(panShiftY_) : 0.0f, float(contentZoom));
            rotationApplied_=rotationSource_==&scene&&prepareRotation();
            if(rotationApplied_){context_->IASetInputLayout(rotationLayout_);context_->VSSetShader(rotationVs_,nullptr,0);context_->VSSetConstantBuffers(1,1,&rotationCb_);}
            drawContent(float(contentZoom), float(panShiftX_), float(panShiftY_), rect);
            if(rotationApplied_){context_->IASetInputLayout(layout_);context_->VSSetShader(vs_,nullptr,0);ID3D11Buffer* empty=nullptr;UINT stride=0,offset=0;context_->IASetVertexBuffers(1,1,&empty,&stride,&offset);}
            updateFrameConstants(float(w), float(h), 0.0f, 0.0f);
        }
        // 4. embedded metafiles in front of the vector content
        drawMetafiles(scene, view, 1);
        if(cacheRaster){
            if(!insertionRaster_){D3D11_TEXTURE2D_DESC desc={};msaa_->GetDesc(&desc);device_->CreateTexture2D(&desc,nullptr,&insertionRaster_);}
            if(insertionRaster_){context_->OMSetRenderTargets(0,nullptr,nullptr);context_->CopyResource(insertionRaster_,msaa_);context_->OMSetRenderTargets(1,&rtv_,nullptr);insertionRasterValid_=true;insertionRasterScene_=&scene;insertionRasterGeneration_=scene.generation();insertionRasterRevision_=revision_;insertionRasterView_=viewKey;insertionRasterContent_=contentHash;}
        }else insertionRasterValid_=false;
        }
        // 5. selection / hotspot overlay, drawn last
        if (overlayVbCount_) {
            if(stableOverlay){
                const double dx=view.transform.viewportCenter.x-overlayPanView_.transform.viewportCenter.x+(overlayPanView_.transform.center.x-view.transform.center.x)*currentScale;
                const double dy=view.transform.viewportCenter.y-overlayPanView_.transform.viewportCenter.y+(overlayPanView_.transform.center.y-view.transform.center.y)*currentScale;
                updateFrameConstants(float(w),float(h),float(dx),float(dy));
            }
            drawBuffer(overlayVb_, overlayVbCapacity_, overlayVbCount_ / 3);
            updateFrameConstants(float(w),float(h),0,0);
        }
        if(copyScene_&&copyVbCount_){
            updateFrameConstants(float(w),float(h),float(copyOffset_.x*currentScale),float(copyOffset_.y*currentScale));
            drawBuffer(copyVb_,copyVbCapacity_,copyVbCount_/3);updateFrameConstants(float(w),float(h),0,0);
        }

        LONGLONG tDraw = tick();
        context_->ResolveSubresource(resolved_, 0, msaa_, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
        context_->CopyResource(staging_, resolved_);
        LONGLONG tResolved = tick();
        readbackToDib();
        LONGLONG tEnd = tick();
        resolveMicros_ = uint64_t(tResolved - tDraw);
        readbackMicros_ = uint64_t(tEnd - tResolved);

        lastView_ = view; lastViewValid_ = true;
        if (stats) {
            stats->buildMicros = uint64_t(tBatch - t0);
            stats->drawMicros = uint64_t(tEnd - tBatch);
            stats->primitivesTotal = scene.size() + (overlay ? overlay->size() : 0);
            stats->primitivesCulled = bs.culled + os.culled;
            stats->primitivesDrawn = bs.drawn + os.drawn;
            stats->meshRebuilds = bs.meshRebuilds + os.meshRebuilds + cs.meshRebuilds;
            stats->viewRebuilds = bs.viewRebuilds + os.viewRebuilds;
            stats->bvhRebuilds = bs.bvhRebuilds + os.bvhRebuilds;
            stats->triangles = contentBatch_.triangleCount() + (overlayVbCount_/3) + (copyScene_?copyTriangles_:0);
            stats->drawCalls = 1;
        }
        return r62::core::Error();
    }

    bool composite(Gdiplus::Graphics& graphics,const Gdiplus::RectF& rect){
        if(!pixels_||width_<=0||height_<=0)return false;
        Gdiplus::Bitmap bitmap(width_,height_,width_*4,PixelFormat32bppRGB,static_cast<BYTE*>(pixels_));
        return graphics.DrawImage(&bitmap,rect)==Gdiplus::Ok;
    }
    // Reuse the last completed frame while the native window border is moving.
    // The final WM_EXITSIZEMOVE renders sharp geometry at the exact new viewport.
    bool presentPreview(HDC target,int w,int h) {
        if(!memory_||width_<=0||height_<=0||w<=0||h<=0)return false;
        int old=SetStretchBltMode(target,COLORONCOLOR);
        bool ok=StretchBlt(target,0,0,w,h,memory_,0,0,width_,height_,SRCCOPY)!=FALSE;
        SetStretchBltMode(target,old);return ok;
    }
    bool present(HDC target) {
        if (!memory_ || width_ <= 0 || height_ <= 0) return false;
        static LONGLONG qpf = 0; if (!qpf) QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&qpf));
        LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
        bool ok = BitBlt(target, 0, 0, width_, height_, memory_, 0, 0, SRCCOPY) != FALSE;
        QueryPerformanceCounter(&b);
        presentMicros_ = uint64_t((b.QuadPart - a.QuadPart) * 1000000 / qpf);
        return ok;
    }

    r62::core::Error readPixels(int x, int y, int w, int h, std::vector<uint8_t>* rgba) const override {
        using namespace r62::core;
        if (!rgba || !pixels_ || x < 0 || y < 0 || w <= 0 || h <= 0 ||
            int64_t(x) + w > width_ || int64_t(y) + h > height_)
            return Error(ErrorCode::InvalidArgument, "invalid readback rectangle");
        GdiFlush();
        rgba->resize(size_t(w) * h * 4);
        for (int row = 0; row < h; ++row) {
            const BYTE* src = static_cast<const BYTE*>(pixels_) + (size_t(height_ - 1 - y - row) * width_ + x) * 4;
            BYTE* dst = rgba->data() + size_t(row) * w * 4;
            for (int col = 0; col < w; ++col) {
                dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
                src += 4; dst += 4;
            }
        }
        return Error();
    }

    void destroy() override {
        ready_ = false;
        releaseTextures();
        releaseDevice();
        releasePresent();
        content_.clear(); overlay_.clear(); foreground_.clear();
        contentBatch_.clear(); visibleContent_.clear();
        emfCache_.clear();
        contentScene_ = overlayScene_ = nullptr;overlayPanValid_=false;panOverlayScene_=nullptr;summaryValid_=false;revisionScene_=nullptr;
        contentCached_ = false; contentKey_ = 0;
    }

private:
    const r62::render::Scene* panOverlayScene_=nullptr;
    uint64_t panOverlayToken_=0;
    bool overlayPanValid_=false;
    r62::render::View overlayPanView_;
    const r62::render::Scene* revisionScene_=nullptr;
    bool insertionPreview_=false,insertionRasterValid_=false;
    int insertionRasterMissWhy_=0;
    ID3D11Texture2D* insertionRaster_=nullptr;const r62::render::Scene* insertionRasterScene_=nullptr;
    uint64_t insertionRasterGeneration_=0,insertionRasterRevision_=0,insertionRasterView_=0,insertionRasterContent_=0;size_t insertionRasterHits_=0;
    uint64_t revision_=0,summaryGeneration_=0,summaryKey_=0;
    bool summaryValid_=false,summaryCurves_=false;
    std::vector<size_t> backdropIndices_,metafileIndices_;
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;
    D3D_FEATURE_LEVEL featureLevel_;
    ID3D11VertexShader* vs_;
    ID3D11PixelShader* ps_;
    ID3D11VertexShader* texVs_;
    ID3D11PixelShader* texPs_;
    ID3D11InputLayout* layout_;
    ID3D11InputLayout* texLayout_;
    ID3D11Buffer* frameCb_;
    ID3D11RasterizerState* raster_;
    ID3D11BlendState* blendStraight_;
    ID3D11BlendState* blendPremul_;
    ID3D11SamplerState* sampler_;
    ID3D11Texture2D* msaa_;
    ID3D11RenderTargetView* rtv_;
    ID3D11Texture2D* resolved_;
    ID3D11Texture2D* staging_;
    int width_, height_, sampleCount_;
    bool ready_;
    ID3D11Buffer* contentVb_;
    UINT contentVbCapacity_;
    UINT contentVbCount_;
    ID3D11Buffer* overlayVb_;
    UINT overlayVbCapacity_;
    UINT overlayVbCount_;
    const r62::render::Scene* rotationSource_=nullptr;uint64_t rotationToken_=0,rotationGeometryKey_=0;bool rotationApplied_=false;
    double rotationMatrix_[6]={1,0,0,0,1,0},rotationDevice_[6]={1,0,0,0,1,0};
    std::map<uint64_t,RotationTarget> rotationTargets_;
    struct RotationMeta {float x=0,y=0,mode=0;};
    std::vector<ID3D11Buffer*> rotationMetaBuffers_;std::vector<float> rotationRadii_;
    ID3D11VertexShader* rotationVs_=nullptr;ID3D11InputLayout* rotationLayout_=nullptr;ID3D11Buffer* rotationCb_=nullptr;
    const r62::render::Scene* copyScene_=nullptr;uint64_t copyToken_=0,copyGeneration_=0,copyViewKey_=0;size_t copyTriangles_=0;
    r62::core::Vec2 copyOffset_;r62::render::Batcher copyBatcher_;
    ID3D11Buffer* copyVb_=nullptr;UINT copyVbCapacity_=0,copyVbCount_=0;
    HDC memory_;
    HBITMAP dib_;
    HGDIOBJ oldDib_;
    void* pixels_;
    r62::render::Batcher content_, overlay_;
    r62::render::Scene foreground_;
    r62::render::Batch contentBatch_;
    std::vector<r62::render::Primitive> visibleContent_;
    const r62::render::Scene* contentScene_;
    const r62::render::Scene* overlayScene_;
    uint64_t contentKey_;
    bool contentCached_;
    bool contentCompactPending_;
    uint64_t geometryKey_;
    bool geometryCached_;
    uint64_t syncMicros_, batchMicros_, vertexMicros_;
    uint64_t resolveMicros_=0, readbackMicros_=0, presentMicros_=0, hashMicros_=0, foregroundMicros_=0;
    bool interactive_=false, rebuildPending_=false, contentHasCurves_=false;
    bool panCached_;
    double panShiftX_, panShiftY_;
    r62::render::View lastView_;
    bool lastViewValid_;
    int panWhy_;
    r62::render::View lastBuildView_;
    bool lastBuildViewValid_;
    std::vector<uint64_t> contentStamps_;
    std::vector<uint64_t> contentKeys_;
    std::vector<uint64_t> contentVersions_;
    std::vector<uint8_t> contentDirty_;
    std::vector<int> contentMap_;
    struct EmfEntry { ID3D11Texture2D* texture; ID3D11ShaderResourceView* view; uint64_t used; };
    uint64_t emfUse_;
    struct EmfKey {
        Gdiplus::Metafile* metafile; int w, h;
        bool operator<(const EmfKey& o) const {
            if (metafile != o.metafile) return metafile < o.metafile;
            if (w != o.w) return w < o.w;
            return h < o.h;
        }
    };
    std::map<EmfKey, EmfEntry> emfCache_;

    static std::string hex(HRESULT hr) {
        char b[32]; std::snprintf(b, sizeof(b), "%08lx", (unsigned long)hr); return b;
    }
    bool compile(const char* source, const char* entry, const char* target, ID3DBlob** out, std::string* why) {
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(source, std::strlen(source), "molecule_studio_dx11", nullptr, nullptr,
                                entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
        if (FAILED(hr)) {
            if (why) {
                *why = "D3DCompile(" + std::string(target) + ") failed (0x" + hex(hr) + ")";
                if (errors) *why += ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            }
            if (errors) errors->Release();
            return false;
        }
        if (errors) errors->Release();
        return true;
    }
    void releasePresent() {
        GdiFlush();
        if (memory_ && oldDib_) SelectObject(memory_, oldDib_);
        if (dib_) DeleteObject(dib_);
        if (memory_) DeleteDC(memory_);
        memory_ = nullptr; dib_ = nullptr; oldDib_ = nullptr; pixels_ = nullptr;
    }
    void releaseTextures() {
        if(insertionRaster_){insertionRaster_->Release();insertionRaster_=nullptr;}insertionRasterValid_=false;
        if (staging_) { staging_->Release(); staging_ = nullptr; }
        if (resolved_) { resolved_->Release(); resolved_ = nullptr; }
        if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
        if (msaa_) { msaa_->Release(); msaa_ = nullptr; }
    }
    void releaseDevice() {
        clearContentChunks();
        for (auto& it : emfCache_) {
            if (it.second.view) it.second.view->Release();
            if (it.second.texture) it.second.texture->Release();
        }
        emfCache_.clear();
        if (contentVb_) { contentVb_->Release(); contentVb_ = nullptr; }
        if (overlayVb_) { overlayVb_->Release(); overlayVb_ = nullptr; }
        clearRotationMetadata();rotationSource_=nullptr;if(rotationVs_){rotationVs_->Release();rotationVs_=nullptr;}if(rotationLayout_){rotationLayout_->Release();rotationLayout_=nullptr;}if(rotationCb_){rotationCb_->Release();rotationCb_=nullptr;}
        if(copyVb_){copyVb_->Release();copyVb_=nullptr;}copyVbCapacity_=copyVbCount_=0;copyScene_=nullptr;copyBatcher_.clear();
        if (sampler_) { sampler_->Release(); sampler_ = nullptr; }
        if (blendPremul_) { blendPremul_->Release(); blendPremul_ = nullptr; }
        if (blendStraight_) { blendStraight_->Release(); blendStraight_ = nullptr; }
        if (raster_) { raster_->Release(); raster_ = nullptr; }
        if (frameCb_) { frameCb_->Release(); frameCb_ = nullptr; }
        if (layout_) { layout_->Release(); layout_ = nullptr; }
        if (texLayout_) { texLayout_->Release(); texLayout_ = nullptr; }
        if (texPs_) { texPs_->Release(); texPs_ = nullptr; }
        if (texVs_) { texVs_->Release(); texVs_ = nullptr; }
        if (ps_) { ps_->Release(); ps_ = nullptr; }
        if (vs_) { vs_->Release(); vs_ = nullptr; }
        if (context_) { context_->Release(); context_ = nullptr; }
        if (device_) { device_->Release(); device_ = nullptr; }
    }
    bool ensureSize(int w, int h) {
        if (w == width_ && h == height_ && msaa_ && staging_ && memory_) return true;
        releaseTextures();
        releasePresent();
        width_ = height_ = 0;
        DXGI_FORMAT fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
        sampleCount_ = 1;
        int maxSamples = 8;
        if (const char* msaaEnv = std::getenv("MOLECULE_STUDIO_DX11_MSAA")) {
            int v = std::atoi(msaaEnv);
            if (v >= 1 && v <= 8) maxSamples = v;
        }
        for (int s = maxSamples; s >= 1; --s) {
            if (s > 1 && s != 2 && s != 4 && s != 8) continue;
            UINT q = 0;
            if (s == 1 || (SUCCEEDED(device_->CheckMultisampleQualityLevels(fmt, s, &q)) && q > 0)) { sampleCount_ = s; break; }
        }
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = UINT(w); d.Height = UINT(h); d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt;
        d.SampleDesc.Count = UINT(sampleCount_); d.SampleDesc.Quality = 0;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &msaa_))) { releaseTextures(); return false; }
        if (FAILED(device_->CreateRenderTargetView(msaa_, nullptr, &rtv_))) { releaseTextures(); return false; }
        d.SampleDesc.Count = 1; d.BindFlags = 0;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &resolved_))) { releaseTextures(); return false; }
        d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &staging_))) { releaseTextures(); return false; }
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        memory_ = CreateCompatibleDC(nullptr);
        if (!memory_) { releaseTextures(); return false; }
        dib_ = CreateDIBSection(memory_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
        if (!dib_) { releaseTextures(); releasePresent(); return false; }
        oldDib_ = SelectObject(memory_, dib_);
        width_ = w; height_ = h;
        return true;
    }
    void updateFrameConstants(float w, float h, float ox, float oy, float zoom = 1.0f) {
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(context_->Map(frameCb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            float* v = static_cast<float*>(m.pData);
            v[0] = w; v[1] = h; v[2] = ox; v[3] = oy; v[4] = zoom; v[5] = v[6] = v[7] = 0;
            context_->Unmap(frameCb_, 0);
        }
    }
    void bindColorPipeline() {
        context_->IASetInputLayout(layout_);
        context_->VSSetShader(vs_, nullptr, 0);
        context_->PSSetShader(ps_, nullptr, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    static void pushQuad(std::vector<GpuVertex>& out, float x0, float y0, float x1, float y1, uint32_t rgba) {
        float ax = std::min(x0, x1), bx = std::max(x0, x1);
        float ay = std::min(y0, y1), by = std::max(y0, y1);
        GpuVertex a = { ax, ay, rgba }, b = { bx, ay, rgba }, c = { bx, by, rgba }, d = { ax, by, rgba };
        out.push_back(a); out.push_back(b); out.push_back(c);
        out.push_back(a); out.push_back(c); out.push_back(d);
    }
    static uint32_t gpuColor(uint32_t rgba) {
        rgba = r62::render::resolvePaint(rgba);
        uint32_t r = (rgba >> 24) & 0xff, g = (rgba >> 16) & 0xff, b = (rgba >> 8) & 0xff, a = rgba & 0xff;
        return r | (g << 8) | (b << 16) | (a << 24);
    }
    bool ensureContentChunks(size_t required) {
        while(contentChunks_.size()>required){contentChunks_.back().buffer->Release();contentChunks_.pop_back();}
        while(contentChunks_.size()<required){
            ContentChunk chunk;D3D11_BUFFER_DESC bd={};bd.ByteWidth=49152*sizeof(GpuVertex);
            bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            if(FAILED(device_->CreateBuffer(&bd,nullptr,&chunk.buffer))){clearContentChunks();contentVbCount_=0;return false;}
            contentChunks_.push_back(chunk);
        }
        return true;
    }
    // Refresh the device chunks from firstVertex on. Vertices before firstVertex
    // kept their index and content, so a structural edit uploads only the tail it
    // appended instead of the whole document.
    void appendContentVerticesFrom(size_t firstVertex) {
        const auto& batch=contentBatch_;
        const size_t total=batch.triangleVertices.size()/2;
        const size_t required=(total+49151)/49152;
        if(!ensureContentChunks(required)){contentVbCount_=0;return;}
        if(!required){contentVbCount_=0;return;}
        const size_t start=std::min(firstVertex/49152,required);
        // After an append-only edit the range list is in painter order, not in
        // vertex order. Bucket ranges by chunk so every chunk is filled once.
        const auto& ranges=batch.triangleRanges;
        std::vector<std::vector<int>> buckets(required-start);
        for(size_t r=0;r<ranges.size();++r){
            const auto& rg=ranges[r];
            if(rg.count<=0||rg.first<0)continue;
            const size_t a=size_t(rg.first),b=a+size_t(rg.count);
            if(b>total)continue;
            size_t ca=a/49152,cb=(b-1)/49152;
            if(cb>=required)cb=required-1;
            if(ca<start)ca=start;
            for(size_t c=ca;c<=cb;++c)buckets[c-start].push_back(int(r));
        }
        std::vector<GpuVertex> upload(49152);
        for(size_t index=start;index<required;++index){
            auto& chunk=contentChunks_[index];
            chunk.first=UINT(index*49152);
            const size_t begin=index*49152,end=std::min(begin+49152,total);
            chunk.count=UINT(end-begin);
            // The arena keeps garbage vertices from replaced primitives. Zero the
            // uncovered slots first (degenerate triangles draw nothing) instead of
            // uploading stale device data.
            size_t covered=0;
            for(int r:buckets[index-start]){
                const auto& rg=ranges[size_t(r)];
                const size_t a=std::max(begin,size_t(rg.first));
                const size_t b=std::min(end,size_t(rg.first)+size_t(rg.count));
                covered+=b-a;
            }
            if(covered!=chunk.count)std::fill(upload.begin(),upload.begin()+chunk.count,GpuVertex{0.0f,0.0f,0xFFFFFFFFu});
            for(int r:buckets[index-start]){
                const auto& rg=ranges[size_t(r)];
                const size_t a=std::max(begin,size_t(rg.first));
                const size_t b=std::min(end,size_t(rg.first)+size_t(rg.count));
                const uint32_t color=gpuColor(rg.rgba);
                for(size_t k=a;k<b;++k)
                    upload[k-begin]=GpuVertex{batch.triangleVertices[k*2],batch.triangleVertices[k*2+1],color};
            }
            D3D11_BOX box={0,0,0,UINT(chunk.count*sizeof(GpuVertex)),1,1};
            context_->UpdateSubresource(chunk.buffer,0,&box,upload.data(),0,0);
            chunk.bounds=vertexBounds(upload.data(),chunk.count);
        }
        contentVbCount_=UINT(total);
        if (std::getenv("MOLECULE_STUDIO_PERF_TRACE"))
            std::fprintf(stderr, "[chunks] start=%zu required=%zu of=%zu\n", start, required, total);
    }
    void buildVertexBuffer(const r62::render::Batch& batch, bool content, bool copy=false) {
        if(content){
            // Content always lives in contentBatch_; this path is the full rebuild.
            appendContentVerticesFrom(0);
            return;
        }
        const size_t sourceVerts = batch.triangleVertices.size() / 2;
        if (!sourceVerts) {
            if (content) contentVbCount_ = 0; else if(copy)copyVbCount_=0;else overlayVbCount_ = 0;
            return;
        }
        ID3D11Buffer** target = content ? &contentVb_ : copy ? &copyVb_ : &overlayVb_;
        UINT* capacity = content ? &contentVbCapacity_ : copy ? &copyVbCapacity_ : &overlayVbCapacity_;
        UINT* count = content ? &contentVbCount_ : copy ? &copyVbCount_ : &overlayVbCount_;
        const size_t bytes = sourceVerts * sizeof(GpuVertex);
        if (bytes > *capacity) {
            if (*target) { (*target)->Release(); *target = nullptr; }
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = UINT(bytes);
            bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(&bd, nullptr, target))) { *capacity = 0; *count = 0; return; }
            *capacity = bd.ByteWidth;
        }
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (FAILED(context_->Map(*target, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { *count = 0; return; }
        GpuVertex* dst = static_cast<GpuVertex*>(m.pData);
        size_t written = 0;
        for (const auto& r : batch.triangleRanges) {
            if (r.count <= 0 || r.first < 0) continue;
            if (size_t(r.first) + size_t(r.count) > sourceVerts) continue;
            const uint32_t c = gpuColor(r.rgba);
            const float* src = &batch.triangleVertices[size_t(r.first) * 2];
            for (int i = 0; i < r.count; ++i) {
                dst[written].x = src[i * 2];
                dst[written].y = src[i * 2 + 1];
                dst[written].color = c;
                ++written;
            }
        }
        context_->Unmap(*target, 0);
        *count = UINT(written);
    }
    bool updateVertexBufferRanges(const std::vector<uint8_t>& dirty) {
        if (contentChunks_.empty() || !contentVbCount_) return false;
        // Only small touched GPU-local buffers are updated; the driver never
        // needs to synchronize or replace the entire document's vertex buffer.
        std::vector<GpuVertex> upload;
        bool any = false;
        std::vector<uint8_t> changedChunks(contentChunks_.size(),0);
        for (const auto& r : contentBatch_.triangleRanges) {
            if (r.count <= 0 || r.first < 0) continue;
            if (size_t(r.primitiveIndex) >= dirty.size() || !dirty[size_t(r.primitiveIndex)]) continue;
            if (size_t(r.first) + size_t(r.count) > contentVbCount_) continue;
            upload.resize(size_t(r.count));
            GpuVertex* dst = upload.data();
            const float* src = &contentBatch_.triangleVertices[size_t(r.first) * 2];
            const uint32_t color = gpuColor(r.rgba);
            for (int k = 0; k < r.count; ++k) {
                dst[k].x = src[k * 2];
                dst[k].y = src[k * 2 + 1];
                dst[k].color = color;
            }
            any = true;
            for(size_t start=size_t(r.first),end=start+size_t(r.count);start<end;){
                size_t index=start/49152;auto& chunk=contentChunks_[index];
                size_t stop=std::min(end,size_t(chunk.first)+chunk.count);
                D3D11_BOX box={UINT((start-chunk.first)*sizeof(GpuVertex)),0,0,UINT((stop-chunk.first)*sizeof(GpuVertex)),1,1};
                context_->UpdateSubresource(chunk.buffer,0,&box,upload.data()+start-size_t(r.first),0,0);
                changedChunks[index]=1;start=stop;
            }
        }
        for(size_t i=0;i<changedChunks.size();++i)if(changedChunks[i]){
            auto& chunk=contentChunks_[i];chunk.bounds=r62::core::Box();
            for(size_t k=chunk.first;k<size_t(chunk.first)+chunk.count;++k)
                chunk.bounds.add(r62::core::Vec2(contentBatch_.triangleVertices[k*2],contentBatch_.triangleVertices[k*2+1]));
        }
        return any;
    }
    struct ContentChunk {UINT first=0,count=0;r62::core::Box bounds;ID3D11Buffer* buffer=nullptr;};
    std::vector<ContentChunk> contentChunks_;
    void clearContentChunks(){for(auto& chunk:contentChunks_)if(chunk.buffer)chunk.buffer->Release();contentChunks_.clear();}
    static r62::core::Box vertexBounds(const GpuVertex* vertices,size_t count){
        r62::core::Box bounds;
        for(size_t i=0;i<count;++i)bounds.add(r62::core::Vec2(vertices[i].x,vertices[i].y));
        return bounds;
    }
    void clearRotationMetadata(){for(auto buffer:rotationMetaBuffers_)if(buffer)buffer->Release();rotationMetaBuffers_.clear();rotationRadii_.clear();rotationGeometryKey_=0;rotationApplied_=false;}
    bool ensureRotationShader(){
        if(rotationVs_&&rotationLayout_&&rotationCb_)return true;
        const char* shader=
            "cbuffer Frame:register(b0){float2 viewport;float2 offset;float zoom;float3 pad;};"
            "cbuffer Rotation:register(b1){float4 row0;float4 row1;float4 upright;};"
            "struct In{float2 pos:POSITION;float4 color:COLOR;float3 data:TEXCOORD0;};"
            "struct Out{float4 pos:SV_Position;float4 color:COLOR;};"
            "float2 rotatePoint(float2 p){return float2(dot(row0.xy,p)+row0.z,dot(row1.xy,p)+row1.z);}"
            "Out main(In i){Out o;float2 p=i.pos;float2 a=i.data.xy;"
            "if(i.data.z>0.5){if(i.data.z<1.5)p=rotatePoint(p);else if(i.data.z<2.5)p+=rotatePoint(a)-a;"
            "else {float2 d=p-a;p=rotatePoint(a)+float2(dot(upright.xy,d),dot(upright.zw,d));}}"
            "o.pos=float4((p.x*zoom+offset.x)*2/viewport.x-1,1-(p.y*zoom+offset.y)*2/viewport.y,0,1);o.color=i.color;return o;}";
        ID3DBlob* blob=nullptr;std::string why;if(!compile(shader,"main","vs_5_0",&blob,&why))return false;
        HRESULT hr=device_->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&rotationVs_);
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,1,0,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(SUCCEEDED(hr))hr=device_->CreateInputLayout(elements,3,blob->GetBufferPointer(),blob->GetBufferSize(),&rotationLayout_);blob->Release();
        D3D11_BUFFER_DESC desc={};desc.ByteWidth=48;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(SUCCEEDED(hr))hr=device_->CreateBuffer(&desc,nullptr,&rotationCb_);
        if(FAILED(hr)){if(rotationVs_){rotationVs_->Release();rotationVs_=nullptr;}if(rotationLayout_){rotationLayout_->Release();rotationLayout_=nullptr;}if(rotationCb_){rotationCb_->Release();rotationCb_=nullptr;}}return SUCCEEDED(hr);
    }
    bool prepareRotation(){
        if(!ensureRotationShader()||!lastBuildViewValid_)return false;
        uint64_t metadataKey=geometryKey_;auto mix=[&](double value){const unsigned char* bytes=reinterpret_cast<const unsigned char*>(&value);for(size_t i=0;i<sizeof(value);++i){metadataKey^=bytes[i];metadataKey*=1099511628211ull;}};
        const auto& build=lastBuildView_;for(double value:{build.transform.center.x,build.transform.center.y,build.transform.scale,build.transform.dpiScale,build.transform.viewportCenter.x,build.transform.viewportCenter.y,build.widthPx,build.heightPx})mix(value);
        if(rotationGeometryKey_!=metadataKey||rotationMetaBuffers_.size()!=contentChunks_.size()){
            clearRotationMetadata();
            struct Range {size_t first,count;RotationMeta meta;float radius;};std::vector<Range> ranges;
            const auto& v=lastBuildView_;double scale=v.transform.scale*v.transform.dpiScale;
            for(const auto& r:contentBatch_.triangleRanges){if(r.count<=0||r.first<0||r.primitiveIndex<0||size_t(r.primitiveIndex)>=foreground_.size())continue;
                const auto& primitive=foreground_.primitives()[size_t(r.primitiveIndex)];auto found=rotationTargets_.find(primitive.cacheKey);if(found==rotationTargets_.end())continue;
                auto point=v.transform.toDevice(found->second.anchor);RotationMeta meta;meta.x=float(point.x);meta.y=float(point.y);meta.mode=float(found->second.mode);float radius=0;
                if(found->second.mode>=2&&!primitive.bounds.empty())for(double x:{primitive.bounds.min.x,primitive.bounds.max.x})for(double y:{primitive.bounds.min.y,primitive.bounds.max.y})radius=std::max(radius,float(r62::core::length(r62::core::Vec2(x,y)-found->second.anchor)*scale));
                ranges.push_back({size_t(r.first),size_t(r.count),meta,radius});
            }
            std::sort(ranges.begin(),ranges.end(),[](const Range& a,const Range& b){return a.first<b.first;});size_t cursor=0;
            for(const auto& chunk:contentChunks_){std::vector<RotationMeta> data(chunk.count);float radius=0;size_t end=size_t(chunk.first)+chunk.count;
                while(cursor<ranges.size()&&ranges[cursor].first+ranges[cursor].count<=chunk.first)++cursor;
                for(size_t i=cursor;i<ranges.size()&&ranges[i].first<end;++i){const auto& range=ranges[i];size_t begin=std::max(size_t(chunk.first),range.first),stop=std::min(end,range.first+range.count);if(stop>begin){std::fill(data.begin()+begin-chunk.first,data.begin()+stop-chunk.first,range.meta);radius=std::max(radius,range.radius);}}
                D3D11_BUFFER_DESC desc={};desc.ByteWidth=UINT(data.size()*sizeof(RotationMeta));desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA input={};input.pSysMem=data.data();ID3D11Buffer* buffer=nullptr;
                if(FAILED(device_->CreateBuffer(&desc,&input,&buffer))){clearRotationMetadata();return false;}rotationMetaBuffers_.push_back(buffer);rotationRadii_.push_back(radius);
            }rotationGeometryKey_=metadataKey;
        }
        const auto& v=lastBuildView_.transform;double scale=v.scale*v.dpiScale;const double* m=rotationMatrix_;
        rotationDevice_[0]=m[0];rotationDevice_[1]=m[1];rotationDevice_[3]=m[3];rotationDevice_[4]=m[4];
        rotationDevice_[2]=scale*(m[0]*v.center.x+m[1]*v.center.y+m[2]-v.center.x)+v.viewportCenter.x-m[0]*v.viewportCenter.x-m[1]*v.viewportCenter.y;
        rotationDevice_[5]=scale*(m[3]*v.center.x+m[4]*v.center.y+m[5]-v.center.y)+v.viewportCenter.y-m[3]*v.viewportCenter.x-m[4]*v.viewportCenter.y;
        double norm=std::hypot(m[0],m[3]);if(norm<1e-12)return false;
        float data[12]={float(rotationDevice_[0]),float(rotationDevice_[1]),float(rotationDevice_[2]),0,float(rotationDevice_[3]),float(rotationDevice_[4]),float(rotationDevice_[5]),0,float(m[0]/norm),float(m[1]/norm),float(m[3]/norm),float(m[4]/norm)};
        D3D11_MAPPED_SUBRESOURCE mapped={};if(FAILED(context_->Map(rotationCb_,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;std::memcpy(mapped.pData,data,sizeof(data));context_->Unmap(rotationCb_,0);return true;
    }
    void drawContent(float zoom,float dx,float dy,const D3D11_RECT& clip){
        UINT stride=sizeof(GpuVertex),offset=0;
        // Draw the ranges in painter order, merging neighbours that happen to be
        // contiguous in the vertex array. After an append-only structural edit the
        // array is not in scene order any more, so several runs may be needed; the
        // merged run is still rasterized in exactly the same triangle order.
        auto drawRun=[&](size_t first,size_t count){
            size_t end=first+count;
            while(first<end){
                const size_t ci=first/49152;
                if(ci>=contentChunks_.size())break;
                const auto& chunk=contentChunks_[ci];
                const size_t cs=size_t(chunk.first),ce=cs+size_t(chunk.count);
                if(ce<=first){first=ce;continue;}
                const size_t stop=std::min(end,ce);
                auto b=chunk.bounds;
                if(rotationApplied_){auto original=b;const double* m=rotationDevice_;for(double x:{original.min.x,original.max.x})for(double y:{original.min.y,original.max.y})b.add(r62::core::Vec2(m[0]*x+m[1]*y+m[2],m[3]*x+m[4]*y+m[5]));double radius=rotationRadii_[ci]*2+2;b.min=b.min-r62::core::Vec2(radius,radius);b.max=b.max+r62::core::Vec2(radius,radius);}
                const bool visible=b.max.x*zoom+dx>=clip.left-1 && b.min.x*zoom+dx<=clip.right+1 &&
                                   b.max.y*zoom+dy>=clip.top-1 && b.min.y*zoom+dy<=clip.bottom+1;
                if(visible){
                    context_->IASetVertexBuffers(0,1,&chunk.buffer,&stride,&offset);
                    if(rotationApplied_){UINT metaStride=sizeof(RotationMeta);context_->IASetVertexBuffers(1,1,&rotationMetaBuffers_[ci],&metaStride,&offset);}
                    context_->Draw(UINT(stop-first),UINT(first-cs));
                }
                first=stop;
            }
        };
        size_t runFirst=0,runCount=0;
        for(const auto& r:contentBatch_.triangleRanges){
            if(r.count<=0||r.first<0)continue;
            if(runCount&&size_t(r.first)==runFirst+runCount)runCount+=size_t(r.count);
            else{
                if(runCount)drawRun(runFirst,runCount);
                runFirst=size_t(r.first);runCount=size_t(r.count);
            }
        }
        if(runCount)drawRun(runFirst,runCount);
    }
    void drawVertices(std::vector<GpuVertex>& verts) {
        if (verts.empty()) return;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = UINT(verts.size() * sizeof(GpuVertex)); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* vb = nullptr;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, &vb))) return;
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(context_->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            std::memcpy(m.pData, verts.data(), verts.size() * sizeof(GpuVertex));
            context_->Unmap(vb, 0);
        }
        UINT stride = sizeof(GpuVertex), offset = 0;
        context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context_->Draw(UINT(verts.size()), 0);
        vb->Release();
    }
    void drawBuffer(ID3D11Buffer* vb, UINT capacity, UINT triangles) {
        if (!vb || !triangles) return;
        UINT stride = sizeof(GpuVertex), offset = 0;
        context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context_->Draw(triangles * 3, 0);
    }
    void drawMetafiles(const r62::render::Scene& scene, const r62::render::View& view, int pass) {
        using namespace r62::render; using r62::core::Vec2;
        for (auto pi : metafileIndices_) {
            const auto& p=scene.primitives()[pi];
            if (p.kind != PrimKind::Rect || p.text.compare(0, 10, "CDXML-EMF:") != 0 || p.points.size() < 4) continue;
            if ((pass < 0 && p.z >= 0) || (pass > 0 && p.z < 0)) continue;
            Gdiplus::PointF pts[3];
            const int indices[3] = { 0, 2, 3 };
            for (int i = 0; i < 3; ++i) {
                Vec2 v = p.points[indices[i]];
                v = (v - view.transform.center) * (view.transform.scale * view.transform.dpiScale) + view.transform.viewportCenter;
                pts[i] = Gdiplus::PointF(float(v.x), float(v.y));
            }
            const float fourthX=pts[1].X+pts[2].X-pts[0].X, fourthY=pts[1].Y+pts[2].Y-pts[0].Y;
            float minX=fourthX,maxX=fourthX,minY=fourthY,maxY=fourthY;
            for(const auto& pt:pts){minX=std::min(minX,pt.X);maxX=std::max(maxX,pt.X);minY=std::min(minY,pt.Y);maxY=std::max(maxY,pt.Y);}
            if(maxX < -2 || maxY < -2 || minX > view.widthPx+2 || minY > view.heightPx+2)continue;
            int w = int(std::ceil(std::hypot(pts[1].X - pts[0].X, pts[1].Y - pts[0].Y)));
            int h = int(std::ceil(std::hypot(pts[2].X - pts[0].X, pts[2].Y - pts[0].Y)));
            if (w < 1 || h < 1) continue;
            Gdiplus::Metafile* mf = embedded::metafile(p.text.substr(10));
            if (!mf) continue;
            // Interactive metafiles are cached as textures.  Very large on-screen
            // sizes are downsampled to a bounded texture and stretched, rather than
            // dropped; ordinary zoom levels stay pixel-exact.
            int tw = w, th = h;
            if (tw > 4096 || th > 4096) {
                double s = std::min(4096.0 / tw, 4096.0 / th);
                tw = std::max(1, int(tw * s)); th = std::max(1, int(th * s));
            }
            // Reuse a slightly higher-resolution texture across wheel steps.
            // The old exact-pixel key plus clear-all eviction thrashed with
            // 32 embedded objects alternating between two zoom levels.
            auto bucket=[](int n){int b=16;while(b<n&&b<4096)b*=2;return b;};
            tw=bucket(tw);th=bucket(th);
            ID3D11ShaderResourceView* srv = metafileTexture(mf, tw, th);
            if (!srv) continue;
            TexVertex q[6];
            float brx = pts[1].X + pts[2].X - pts[0].X;
            float bry = pts[1].Y + pts[2].Y - pts[0].Y;
            TexVertex a = { pts[0].X, pts[0].Y, 0.f, 0.f };
            TexVertex b = { pts[1].X, pts[1].Y, 1.f, 0.f };
            TexVertex c = { brx, bry, 1.f, 1.f };
            TexVertex d = { pts[2].X, pts[2].Y, 0.f, 1.f };
            q[0] = a; q[1] = b; q[2] = c; q[3] = a; q[4] = c; q[5] = d;
            drawTextured(q, 6, srv);
        }
    }
    ID3D11ShaderResourceView* metafileTexture(Gdiplus::Metafile* mf, int w, int h) {
        EmfKey key = { mf, w, h };
        auto found = emfCache_.find(key);
        if (found != emfCache_.end()) {found->second.used=++emfUse_;return found->second.view;}
        size_t bytes=0;for(const auto& it:emfCache_)bytes+=size_t(it.first.w)*it.first.h*4;
        const size_t cost=size_t(w)*h*4;
        while (!emfCache_.empty() && (emfCache_.size() >= 96 || bytes+cost > 128u*1024u*1024u)) {
            auto oldest=emfCache_.begin();for(auto it=emfCache_.begin();it!=emfCache_.end();++it)if(it->second.used<oldest->second.used)oldest=it;
            bytes-=size_t(oldest->first.w)*oldest->first.h*4;
            if(oldest->second.view)oldest->second.view->Release();if(oldest->second.texture)oldest->second.texture->Release();
            emfCache_.erase(oldest);
        }
        Gdiplus::Bitmap bmp(w, h, PixelFormat32bppPARGB);
        {
            Gdiplus::Graphics g(&bmp);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.DrawImage(mf, Gdiplus::Rect(0, 0, w, h));
        }
        Gdiplus::Rect lockRect(0, 0, w, h);
        Gdiplus::BitmapData data = {};
        if (bmp.LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok) return nullptr;
        std::vector<uint8_t> packed(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = static_cast<const uint8_t*>(data.Scan0) + ptrdiff_t(y) * data.Stride;
            std::memcpy(&packed[size_t(y) * w * 4], src, size_t(w) * 4);
        }
        bmp.UnlockBits(&data);
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = UINT(w); d.Height = UINT(h); d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = packed.data(); sd.SysMemPitch = UINT(w * 4);
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device_->CreateTexture2D(&d, &sd, &tex))) return nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
        svd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        svd.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(device_->CreateShaderResourceView(tex, &svd, &srv))) { tex->Release(); return nullptr; }
        EmfEntry e = { tex, srv, ++emfUse_ };
        emfCache_[key] = e;
        return srv;
    }
    void drawTextured(const TexVertex* verts, UINT count, ID3D11ShaderResourceView* srv) {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = count * sizeof(TexVertex); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* vb = nullptr;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, &vb))) return;
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(context_->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            std::memcpy(m.pData, verts, count * sizeof(TexVertex));
            context_->Unmap(vb, 0);
        }
        context_->IASetInputLayout(texLayout_);
        context_->VSSetShader(texVs_, nullptr, 0);
        context_->PSSetShader(texPs_, nullptr, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(TexVertex), offset = 0;
        context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context_->PSSetShaderResources(0, 1, &srv);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->OMSetBlendState(blendPremul_, nullptr, 0xffffffff);
        context_->Draw(count, 0);
        ID3D11ShaderResourceView* none = nullptr;
        context_->PSSetShaderResources(0, 1, &none);
        context_->OMSetBlendState(blendStraight_, nullptr, 0xffffffff);
        bindColorPipeline();
        vb->Release();
    }
    void readbackToDib() {
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (FAILED(context_->Map(staging_, 0, D3D11_MAP_READ, 0, &m))) return;
        uint8_t* dst = static_cast<uint8_t*>(pixels_);
        for (int y = 0; y < height_; ++y)
            std::memcpy(dst + size_t(y) * width_ * 4, static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch, size_t(width_) * 4);
        context_->Unmap(staging_, 0);
    }
};

} // namespace dx11
#endif
