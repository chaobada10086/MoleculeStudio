// =============================================================================
// re_round62_viewer/next/render/rasterize.cpp
//
// T03 - software rasterizer over render::Primitive (NOT over the GL mesh).
//
// Why this exists and is not a toy:
//   * The acceptance criteria require offscreen images and geometric bound
//     proof. A build machine may have no GPU and the GL path may be unavailable.
//   * It consumes the Primitive RECORDS through the same geometry kernel the GPU
//     path uses, so an agreement between the two is evidence about the kernel,
//     not about a driver.
//   * It implements fills with holes via the same contour + fill-rule input, so
//     'O'/'B'/'8' holes are verifiable pixel by pixel.
//
// It is NOT the production path and is not claimed to be fast.
// =============================================================================

#include "next/render/batcher.hpp"
#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace r62 {
namespace render {

namespace {

struct Target {
    int width, height, ss;
    std::vector<float> r, g, b, a;   // supersampled, premultiplied-free, linear-ish sRGB8/255
    void init(int w, int h, int supersample, uint32_t bg) {
        width = w; height = h; ss = supersample < 1 ? 1 : supersample;
        const size_t n = static_cast<size_t>(w) * ss * h * ss;
        r.assign(n, colorRed(bg) / 255.0f);
        g.assign(n, colorGreen(bg) / 255.0f);
        b.assign(n, colorBlue(bg) / 255.0f);
        a.assign(n, colorAlpha(bg) / 255.0f);
    }
    void blend(int x, int y, uint32_t rgba, float coverage) {
        if (x < 0 || y < 0 || x >= width * ss || y >= height * ss) return;
        if (coverage <= 0.0f) return;
        const size_t i = static_cast<size_t>(y) * width * ss + x;
        const float sr = colorRed(rgba) / 255.0f;
        const float sg = colorGreen(rgba) / 255.0f;
        const float sb = colorBlue(rgba) / 255.0f;
        const float sa = (colorAlpha(rgba) / 255.0f) * coverage;
        r[i] = r[i] * (1.0f - sa) + sr * sa;
        g[i] = g[i] * (1.0f - sa) + sg * sa;
        b[i] = b[i] * (1.0f - sa) + sb * sa;
        a[i] = a[i] * (1.0f - sa) + sa;
    }
};

// Device-space triangle with a flat colour, scanline filled with 4x vertical
// subsamples. Coverage-based, so a thin stroke still shows up at 1x.
struct DevTri { float x0, y0, x1, y1, x2, y2; uint32_t rgba; };

static void fillTriangle(Target* t, const DevTri& tri) {
    const float minX = std::min(tri.x0, std::min(tri.x1, tri.x2));
    const float maxX = std::max(tri.x0, std::max(tri.x1, tri.x2));
    const float minY = std::min(tri.y0, std::min(tri.y1, tri.y2));
    const float maxY = std::max(tri.y0, std::max(tri.y1, tri.y2));
    if (!(maxX >= minX) || !(maxY >= minY)) return;
    if (maxX < 0 || maxY < 0 || minX > t->width * t->ss || minY > t->height * t->ss) return;
    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int x1 = std::min(t->width * t->ss - 1, static_cast<int>(std::ceil(maxX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int y1 = std::min(t->height * t->ss - 1, static_cast<int>(std::ceil(maxY)));
    const int subs = 4;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int hits = 0;
            for (int sy = 0; sy < subs; ++sy) {
                for (int sx = 0; sx < subs; ++sx) {
                    const float px = x + (sx + 0.5f) / subs;
                    const float py = y + (sy + 0.5f) / subs;
                    const float d0 = (tri.x1 - tri.x0) * (py - tri.y0) - (tri.y1 - tri.y0) * (px - tri.x0);
                    const float d1 = (tri.x2 - tri.x1) * (py - tri.y1) - (tri.y2 - tri.y1) * (px - tri.x1);
                    const float d2 = (tri.x0 - tri.x2) * (py - tri.y2) - (tri.y0 - tri.y2) * (px - tri.x2);
                    const bool neg = (d0 < 0) || (d1 < 0) || (d2 < 0);
                    const bool pos = (d0 > 0) || (d1 > 0) || (d2 > 0);
                    if (!(neg && pos)) ++hits;
                }
            }
            if (hits) t->blend(x, y, tri.rgba, static_cast<float>(hits) / (subs * subs));
        }
    }
}

// A world mesh projected to device; extracted so both fill and stroke share it.
static void projectMesh(const geom::Mesh& world, const core::ViewTransform& view,
                        int ss, std::vector<float>* out) {
    out->clear();
    out->reserve(world.triangles.size());
    for (size_t i = 0; i + 1 < world.triangles.size(); i += 2) {
        const core::Vec2 d = view.toDevice(core::Vec2(world.triangles[i], world.triangles[i + 1]));
        out->push_back(static_cast<float>(d.x * ss));
        out->push_back(static_cast<float>(d.y * ss));
    }
}

static void rasterMesh(Target* t, const geom::Mesh& world, const core::ViewTransform& view,
                       uint32_t rgba, std::vector<float>* scratch) {
    projectMesh(world, view, t->ss, scratch);
    const std::vector<float>& v = *scratch;
    for (size_t i = 0; i + 5 < v.size(); i += 6) {
        DevTri tri;
        tri.x0 = v[i + 0]; tri.y0 = v[i + 1];
        tri.x1 = v[i + 2]; tri.y1 = v[i + 3];
        tri.x2 = v[i + 4]; tri.y2 = v[i + 5];
        tri.rgba = rgba;
        fillTriangle(t, tri);
    }
}

} // namespace

// Shared implementation. `measurer` may be null; when it is, text primitives
// whose glyphs were never laid out simply emit no geometry, which is visible as
// missing text rather than as a crash.
static core::Error rasterizeImpl(const Scene& content, const Scene* overlay, const View& view,
                                 const RasterOptions& options, geom::TextMeasurer* measurer,
                                 std::vector<uint8_t>* rgba) {
    if (!rgba) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    if (options.width <= 0 || options.height <= 0)
        return core::Error(core::ErrorCode::InvalidArgument, "bad raster size");
    const int ss = options.supersample < 1 ? 1 : (options.supersample > 4 ? 4 : options.supersample);

    Target target;
    target.init(options.width, options.height, ss, options.background);

    // The measurer is synthetic here: the rasterizer must not link GDI, and the
    // acceptance claim about text is about GEOMETRY TOPOLOGY and bounds, not about
    // which font file the machine happens to have. The GL path injects the real
    // outline measurer; both go through layoutText.
    // Build world meshes for content and overlay through the SAME cache class the
    // GPU backend uses, so the geometry is identical by construction. One sync for
    // the whole scene, then painter-order emission from the shared cache.
    Batcher batcher;
    BatchStats syncStats;
    batcher.sync(content, view, measurer, &syncStats);
    if (overlay) batcher.sync(*overlay, view, measurer, &syncStats);

    // Painter's order: (layer id, z, insertion index). Matches the GPU backend.
    struct Ordered { size_t index; const Primitive* prim; int layer; int z; };
    std::vector<Ordered> ordered;
    ordered.reserve(content.size() + (overlay ? overlay->size() : 0));
    for (size_t i = 0; i < content.primitives().size(); ++i) {
        Ordered o; o.index = i; o.prim = &content.primitives()[i];
        o.layer = static_cast<int>(content.primitives()[i].layer.value);
        o.z = content.primitives()[i].z;
        ordered.push_back(o);
    }
    if (overlay) for (size_t i = 0; i < overlay->primitives().size(); ++i) {
        Ordered o; o.index = content.primitives().size() + i;
        o.prim = &overlay->primitives()[i];
        o.layer = static_cast<int>(overlay->primitives()[i].layer.value);
        o.z = overlay->primitives()[i].z;
        ordered.push_back(o);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const Ordered& a, const Ordered& b) {
            if (a.layer != b.layer) return a.layer < b.layer;
            if (a.z != b.z) return a.z < b.z;
            return a.index < b.index;
        });

    // The batcher keys meshes by (kind, ObjectRef, ordinal). Walk the ordered list
    // and pull each primitive's world mesh by rebuilding a single-primitive scene
    // ONLY for overlay primitives without a document identity, which is the same
    // identity rule the cache uses.
    // The batch holds DEVICE pixels; the target grid is device * ss. Scaling here,
    // once, is what puts the geometry on the supersampled grid. Omitting it draws
    // the shape at 1/ss size in the top-left corner - the failure mode that made a
    // blank-looking image when the object was far from the origin.
    const float grid = static_cast<float>(ss);
    Batcher perPrim;
    for (size_t i = 0; i < ordered.size(); ++i) {
        const Primitive& p = *ordered[i].prim;
        Batch mesh;
        Scene one;
        one.reserve(1);
        one.add(p);
        perPrim.clear();
        perPrim.sync(one, view, measurer, 0);
        perPrim.buildBatch(one, view, true, &mesh, 0);
        for (size_t k = 0; k < mesh.triangleRanges.size(); ++k) {
            const DrawRange& range = mesh.triangleRanges[k];
            // The batcher always fills in a real colour. The fallback is opaque
            // black on purpose: falling back to `p.style.fill.rgba` would be
            // 0x00000000 for a stroke-only primitive and would draw the geometry
            // fully transparent - a silent 'nothing rendered' failure.
            const uint32_t colour = resolvePaint(range.rgba);
            // `range.count` counts VERTICES (three per triangle), and each vertex
            // is two floats. Advancing by 3 vertices per triangle is what makes a
            // two-triangle range emit both triangles; stepping by 6 emitted only the
            // first one, which is why a stroked line drew as a wedge.
            for (int v = 0; v + 2 < range.count; v += 3) {
                const size_t base = static_cast<size_t>(range.first + v) * 2;
                DevTri tri;
                tri.x0 = mesh.triangleVertices[base + 0] * grid;
                tri.y0 = mesh.triangleVertices[base + 1] * grid;
                tri.x1 = mesh.triangleVertices[base + 2] * grid;
                tri.y1 = mesh.triangleVertices[base + 3] * grid;
                tri.x2 = mesh.triangleVertices[base + 4] * grid;
                tri.y2 = mesh.triangleVertices[base + 5] * grid;
                tri.rgba = colour;
                fillTriangle(&target, tri);
            }
        }
        // `mesh.lineRanges` is always empty for this module: every stroke is
        // expanded into triangles by the geometry kernel, which is precisely why
        // no glLineWidth dependency exists. Nothing is skipped here.
    }
    (void)rasterMesh;

    // Resolve the supersampled grid down to RGBA8, bottom-up rows.
    rgba->assign(static_cast<size_t>(options.width) * options.height * 4, 255u);
    const float inv = 1.0f / (ss * ss);
    for (int y = 0; y < options.height; ++y) {
        for (int x = 0; x < options.width; ++x) {
            float sr = 0, sg = 0, sb = 0, sa = 0;
            for (int sy = 0; sy < ss; ++sy) {
                for (int sx = 0; sx < ss; ++sx) {
                    const int px = x * ss + sx;
                    const int py = y * ss + sy;
                    const size_t i = static_cast<size_t>(py) * options.width * ss + px;
                    sr += target.r[i]; sg += target.g[i];
                    sb += target.b[i]; sa += target.a[i];
                }
            }
            sr *= inv; sg *= inv; sb *= inv; sa *= inv;
            // The raster target stores rows top-down; the output is bottom-up.
            const int outY = options.height - 1 - y;
            const size_t o = (static_cast<size_t>(outY) * options.width + x) * 4;
            (*rgba)[o + 0] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, sr * 255.0f + 0.5f)));
            (*rgba)[o + 1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, sg * 255.0f + 0.5f)));
            (*rgba)[o + 2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, sb * 255.0f + 0.5f)));
            (*rgba)[o + 3] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, sa * 255.0f + 0.5f)));
        }
    }
    return core::Error();
}

core::Error rasterizeWithText(const Scene& content, const Scene* overlay, const View& view,
                              const RasterOptions& options, geom::TextMeasurer* measurer,
                              std::vector<uint8_t>* rgba) {
    return rasterizeImpl(content, overlay, view, options, measurer, rgba);
}

core::Error rasterize(const Scene& content, const Scene* overlay, const View& view,
                      const RasterOptions& options, std::vector<uint8_t>* rgba) {
    return rasterizeImpl(content, overlay, view, options, 0, rgba);
}

// ---------------------------------------------------------------------------
// PNG writer
// ---------------------------------------------------------------------------
namespace {
void putBigEndian(std::vector<uint8_t>* out, uint32_t v) {
    out->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(v & 0xFF));
}

uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void chunk(std::vector<uint8_t>* out, const char tag[4], const std::vector<uint8_t>& body) {
    putBigEndian(out, static_cast<uint32_t>(body.size()));
    const size_t start = out->size();
    out->push_back(static_cast<uint8_t>(tag[0]));
    out->push_back(static_cast<uint8_t>(tag[1]));
    out->push_back(static_cast<uint8_t>(tag[2]));
    out->push_back(static_cast<uint8_t>(tag[3]));
    out->insert(out->end(), body.begin(), body.end());
    const uint32_t crc = crc32(&(*out)[start], out->size() - start);
    putBigEndian(out, crc);
}
} // namespace

core::Error writePng(const std::string& path, int width, int height,
                     const std::vector<uint8_t>& rgbaBottomUp) {
    if (width <= 0 || height <= 0) return core::Error(core::ErrorCode::InvalidArgument, "bad size");
    if (rgbaBottomUp.size() < static_cast<size_t>(width) * height * 4)
        return core::Error(core::ErrorCode::InvalidArgument, "short buffer");

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);   // filter: None
        // PNG rows are top-down; our buffer is bottom-up.
        const int srcY = height - 1 - y;
        const size_t base = static_cast<size_t>(srcY) * width * 4;
        for (int x = 0; x < width; ++x) {
            raw.push_back(rgbaBottomUp[base + x * 4 + 0]);
            raw.push_back(rgbaBottomUp[base + x * 4 + 1]);
            raw.push_back(rgbaBottomUp[base + x * 4 + 2]);
            raw.push_back(rgbaBottomUp[base + x * 4 + 3]);
        }
    }

    // zlib stream with stored (uncompressed) deflate blocks: no dependency, and
    // the file is small enough for evidence screenshots.
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t at = 0;
    while (at < raw.size()) {
        const size_t take = std::min<size_t>(65535, raw.size() - at);
        const bool last = (at + take) >= raw.size();
        z.push_back(last ? 1 : 0);
        const uint16_t len = static_cast<uint16_t>(take);
        z.push_back(static_cast<uint8_t>(len & 0xFF));
        z.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        const uint16_t nlen = static_cast<uint16_t>(~len);
        z.push_back(static_cast<uint8_t>(nlen & 0xFF));
        z.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                 raw.begin() + static_cast<std::ptrdiff_t>(at + take));
        at += take;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    const uint32_t adler = (b << 16) | a;
    putBigEndian(&z, adler);

    std::vector<uint8_t> out;
    const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    out.insert(out.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    putBigEndian(&ihdr, static_cast<uint32_t>(width));
    putBigEndian(&ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(6);   // colour type: RGBA
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(&out, "IHDR", ihdr);
    chunk(&out, "IDAT", z);
    chunk(&out, "IEND", std::vector<uint8_t>());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return core::Error(core::ErrorCode::IoError, "cannot open " + path);
    const size_t wrote = std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (wrote != out.size()) return core::Error(core::ErrorCode::IoError, "short write");
    return core::Error();
}

} // namespace render
} // namespace r62
