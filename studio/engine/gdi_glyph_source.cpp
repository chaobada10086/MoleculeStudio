// =============================================================================
// re_round62_viewer/next/text/gdi_glyph_source.cpp
//
// T09 - the real outline provider.  Windows/GDI only; this is the ONLY file in
// this module that includes <windows.h>.
//
// WHY GetGlyphOutlineW + GGO_UNHINTED + GGO_NATIVE
// -----------------------------------------------------------------------------
//   * GGO_NATIVE returns the font's own quadratic splines (TTPOLYGONHEADER /
//     TTPOLYCURVE), so the outline is the real glyph, including the winding that
//     makes "O"/"B"/"8" holes work with the NonZero fill rule.  A bitmap or an
//     ANSI conversion could not express a hole at all.
//   * GGO_UNHINTED means the outline does NOT change with the device pixel
//     size, which is what lets ONE cached outline serve every zoom level.  A
//     hinted outline would have to be re-read after a zoom, and "zero glyph
//     resampling while dragging/zooming" would be false.
//   * the request is made at em size 2048 (the same 2048 units/em the legacy
//     viewer_text.hpp used, and the units a 16.16 FIXED maps to), so the
//     conversion is exact and the cache is resolution independent.
//
// FALLBACK CHAIN (an unknown font must have an EXPLAINABLE result)
//   1. the requested family, if it maps the code point
//   2. the CJK family (default "Microsoft YaHei"), for Chinese/Japanese/Korean
//   3. the default UI family (default "Segoe UI")
//   4. the tofu box with missing=true and a reason string
// Every step that changes the family is recorded in the fallback report so the
// caller can print "Verdana -> Segoe UI" instead of silently drawing Segoe.
//
// LICENSING: this file installs nothing and ships no font.  It reads the fonts
// the machine already has through the documented GDI API, exactly like the
// legacy viewer_text.hpp did.
// =============================================================================

#include "next/text/glyph_source.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace r62 {
namespace text {

namespace {

#ifdef _WIN32

const int kEmUnits = 2048;

inline double fixedToDouble(FIXED v) {
    return static_cast<double>(v.value) + static_cast<double>(v.fract) / 65536.0;
}

struct DeviceContext {
    HDC dc;
    HFONT font;
    HGDIOBJ old;
    std::string family;
    std::string key;
    bool bold, italic;
    bool kerningLoaded=false;std::map<uint32_t,int> kerning;std::map<uint64_t,double> pairCache;
    DeviceContext() : dc(0), font(0), old(0), bold(false), italic(false) {}
};

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), 0, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &out[0], n);
    return out;
}

// Flatten one quadratic B-spline span of a TTPOLYCURVE.  Same math as the legacy
// vector_text.hpp (subpixel approximation, count from the bend), but the result
// is cached forever because the outline is taken unhinted.
void flattenQuad(std::vector<std::vector<float> >* contour, float ax, float ay,
                 float bx, float by, float cx, float cy) {
    const double bend = std::sqrt((ax - 2 * bx + cx) * (ax - 2 * bx + cx) +
                                  (ay - 2 * by + cy) * (ay - 2 * by + cy));
    // Coordinates here are already em-normalized, not 2048-unit font points.
    // The old 0.60 tolerance reduced every quadratic to two segments (a faceted O).
    int steps = static_cast<int>(std::ceil(std::sqrt(bend / (4.0 / 16384.0))));
    if (steps < 2) steps = 2;
    if (steps > 64) steps = 64;
    for (int i = 1; i <= steps; ++i) {
        const double u = static_cast<double>(i) / steps;
        const double v = 1.0 - u;
        std::vector<float> p;
        p.push_back(static_cast<float>(v * v * ax + 2 * v * u * bx + u * u * cx));
        p.push_back(static_cast<float>(v * v * ay + 2 * v * u * by + u * u * cy));
        contour->push_back(p);
    }
}

// A very small contour area guard: a degenerate contour would make the
// triangulator produce nothing and hide a real glyph behind an invisible path.
float contourArea(const std::vector<std::vector<float> >& c) {
    if (c.size() < 3) return 0.0f;
    float a = 0.0f;
    for (size_t i = 0; i < c.size(); ++i) {
        const std::vector<float>& p = c[i];
        const std::vector<float>& q = c[(i + 1) % c.size()];
        a += static_cast<float>(p[0] * q[1] - q[0] * p[1]);
    }
    return a * 0.5f;
}

class GdiProvider : public GlyphProvider {
public:
    explicit GdiProvider(const std::string& defaultFamily, const std::string& cjkFamily)
        : defaultFamily_(defaultFamily.empty() ? std::string("Segoe UI") : defaultFamily),
          cjkFamily_(cjkFamily.empty() ? std::string("Microsoft YaHei") : cjkFamily),
          samples_(0), reuses_(0) {}

    ~GdiProvider() { releaseContexts(); }

    bool glyph(uint32_t codepoint, const FontSpec& font, Glyph* out) {
        if (!out) return false;
        Glyph& g = *out;
        g = Glyph();
        g.resolved = font;

        std::vector<std::string> chain;
        const std::string requested = font.family.empty() ? defaultFamily_ : font.family;
        chain.push_back(requested);
        // GDI does not apply font linking to GetGlyphOutline. Match Windows'
        // common CJK and mathematical fallbacks explicitly (e.g. literal ₂).
        if(codepoint>=0x2e80&&codepoint<=0x9fff&&requested!="SimSun")chain.push_back("SimSun");
        if(codepoint>=0x2000&&codepoint<=0x2bff){chain.push_back("Segoe UI Symbol");chain.push_back("Cambria Math");}
        if (cjkFamily_ != requested) chain.push_back(cjkFamily_);
        if (defaultFamily_ != requested && defaultFamily_ != cjkFamily_)
            chain.push_back(defaultFamily_);

        for (size_t i = 0; i < chain.size(); ++i) {
            const std::string& family = chain[i];
            DeviceContext* ctx = contextFor(family, font.bold, font.italic);
            if (!ctx) continue;
            const WORD index = glyphIndex(ctx, codepoint);
            if (index == 0xFFFFu) continue;
            if (!outline(ctx, index, &g)) continue;
            if (i > 0) {
                std::ostringstream oss;
                oss << requested << " -> " << family << " (fallback)";
                g.missingReason = oss.str();
                recordFallback(oss.str());
            }
            g.resolved.family = family;
            g.resolved.bold = font.bold;
            g.resolved.italic = font.italic;
            ++samples_;
            return true;
        }

        // No family had the glyph: a visible, EXPLAINED tofu box.
        makeTofu(&g);
        std::ostringstream oss;
        oss << "no font in the chain maps U+" << std::hex << codepoint
            << " (requested " << requested << ")";
        g.missing = true;
        g.missingReason = oss.str();
        recordFallback(g.missingReason);
        ++samples_;
        return true;
    }

    bool lineMetrics(const FontSpec& font, LineMetrics* out) {
        if (!out) return false;
        const std::string family = font.family.empty() ? defaultFamily_ : font.family;
        DeviceContext* ctx = contextFor(family, font.bold, font.italic);
        if (!ctx) return false;
        TEXTMETRICW tm;
        if (!GetTextMetricsW(ctx->dc, &tm)) return false;
        // Glyph outlines and advances use kEmUnits (the character em), not
        // tmHeight, which includes internal leading. Using tmHeight here shrinks
        // caption line spacing and disagrees with the native text editor.
        const double em = static_cast<double>(kEmUnits);
        if (em <= 0.0) return false;
        out->ascent = static_cast<double>(tm.tmAscent) / em;
        out->descent = static_cast<double>(tm.tmDescent) / em;
        out->lineGap = static_cast<double>(tm.tmExternalLeading) / em;
        out->em = 1.0;
        out->valid = true;
        return true;
    }

    double kern(uint32_t left, uint32_t right, const FontSpec& font) {
        const std::string family = font.family.empty() ? defaultFamily_ : font.family;
        DeviceContext* ctx = contextFor(family, font.bold, font.italic);
        if (!ctx) return 0.0;
        const uint64_t pair=(uint64_t(left)<<32)|right;
        auto cached=ctx->pairCache.find(pair);if(cached!=ctx->pairCache.end())return cached->second;
        const WORD a=glyphIndex(ctx,left),b=glyphIndex(ctx,right);
        double result=0;
        if(a!=0xFFFFu&&b!=0xFFFFu){
            if(!ctx->kerningLoaded){
                const DWORD needed=GetKerningPairsW(ctx->dc,0,nullptr);
                if(needed){std::vector<KERNINGPAIR> pairs(needed);const DWORD got=GetKerningPairsW(ctx->dc,needed,pairs.data());
                    for(DWORD i=0;i<got;++i)ctx->kerning.emplace((uint32_t(pairs[i].wFirst)<<16)|pairs[i].wSecond,pairs[i].iKernAmount);
                }
                ctx->kerningLoaded=true;
            }
            auto it=ctx->kerning.find((uint32_t(a)<<16)|b);if(it!=ctx->kerning.end())result=-double(it->second)/kEmUnits;
        }
        // Bound uncommon pairs; the per-font table remains reusable.
        if(ctx->pairCache.size()>=8192)ctx->pairCache.clear();ctx->pairCache.emplace(pair,result);return result;
    }

    size_t sampleCount() const { return samples_; }
    size_t reuseCount() const { return reuses_; }
    void resetCounters() { samples_ = 0; reuses_ = 0; }
    const char* providerName() const { return "GDI/GetGlyphOutlineW"; }
    void clear() { releaseContexts(); }

    const std::vector<std::string>& fallbackLog() const { return fallbackLog_; }

private:
    // Device contexts are owned by this provider; every path that drops them
    // goes through here so a DC/HFONT pair is never leaked (the legacy editor
    // leaked one HFONT per distinct label font, which is what this avoids).
    void releaseContexts() {
        for (size_t i = 0; i < contextKeys_.size(); ++i) {
            std::map<std::string, DeviceContext*>::iterator it =
                contexts_.find(contextKeys_[i]);
            if (it != contexts_.end()) { destroyContext(it->second); contexts_.erase(it); }
        }
        contexts_.clear();
        contextKeys_.clear();
    }

    void recordFallback(const std::string& s) {
        for (size_t i = 0; i < fallbackLog_.size(); ++i) if (fallbackLog_[i] == s) return;
        if (fallbackLog_.size() < 64) fallbackLog_.push_back(s);
    }

    DeviceContext* contextFor(const std::string& family, bool bold, bool italic) {
        std::ostringstream key;
        key << family << '|' << (bold ? 'B' : '-') << (italic ? 'I' : '-');
        std::map<std::string, DeviceContext*>::iterator it = contexts_.find(key.str());
        if (it != contexts_.end()) return it->second;

        DeviceContext ctx;
        ctx.family = family;
        ctx.bold = bold;
        ctx.italic = italic;
        ctx.dc = CreateCompatibleDC(0);
        if (!ctx.dc) return 0;
        const std::wstring wide = widen(family);
        ctx.font = CreateFontW(-kEmUnits, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                               italic ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, DEFAULT_PITCH,
                               wide.empty() ? 0 : wide.c_str());
        if (!ctx.font) { DeleteDC(ctx.dc); return 0; }
        ctx.old = SelectObject(ctx.dc, ctx.font);
        if (!ctx.old || ctx.old == HGDI_ERROR) {
            DeleteObject(ctx.font);
            DeleteDC(ctx.dc);
            return 0;
        }
        DeviceContext* stored = new DeviceContext(ctx);
        contexts_[key.str()] = stored;
        contextKeys_.push_back(key.str());
        return stored;
    }

    static void destroyContext(DeviceContext* c) {
        if (!c) return;
        if (c->dc) {
            if (c->old) SelectObject(c->dc, c->old);
            if (c->font) DeleteObject(c->font);
            DeleteDC(c->dc);
        }
        delete c;
    }

    // Resolve a code point to a glyph index.  Uses the UNICODE path for BMP and
    // the surrogate pair path for supplementary planes, so an emoji or a CJK
    // extension character is not silently turned into tofu.
    static WORD glyphIndex(DeviceContext* ctx, uint32_t cp) {
        wchar_t buf[2];
        int count = 1;
        if (cp <= 0xFFFFu) {
            buf[0] = static_cast<wchar_t>(cp);
        } else {
            const uint32_t v = cp - 0x10000u;
            buf[0] = static_cast<wchar_t>(0xD800u + (v >> 10));
            buf[1] = static_cast<wchar_t>(0xDC00u + (v & 0x3FFu));
            count = 2;
        }
        WORD indices[2] = { 0xFFFFu, 0xFFFFu };
        const DWORD r = GetGlyphIndicesW(ctx->dc, buf, count, indices, GGI_MARK_NONEXISTING_GLYPHS);
        if (r == GDI_ERROR) return 0xFFFFu;
        return indices[0];
    }

    static bool outline(DeviceContext* ctx, WORD index, Glyph* out) {
        MAT2 matrix;
        std::memset(&matrix, 0, sizeof(matrix));
        matrix.eM11.value = 1;
        matrix.eM22.value = 1;
        GLYPHMETRICS metrics;
        std::memset(&metrics, 0, sizeof(metrics));
        const UINT flags = GGO_NATIVE | GGO_GLYPH_INDEX | GGO_UNHINTED;
        const DWORD bytes = GetGlyphOutlineW(ctx->dc, index, flags, &metrics, 0, 0, &matrix);
        if (bytes == GDI_ERROR) return false;
        out->advance = static_cast<double>(metrics.gmCellIncX) / kEmUnits;
        if (bytes == 0) {
            out->blank = true;
            return true;
        }
        std::vector<unsigned char> buffer(bytes);
        if (GetGlyphOutlineW(ctx->dc, index, flags, &metrics, bytes, &buffer[0], &matrix) == GDI_ERROR)
            return false;

        size_t offset = 0;
        while (offset < bytes) {
            if (bytes - offset < sizeof(TTPOLYGONHEADER)) return false;
            const TTPOLYGONHEADER* header =
                reinterpret_cast<const TTPOLYGONHEADER*>(&buffer[offset]);
            if (header->dwType != TT_POLYGON_TYPE || header->cb < sizeof(TTPOLYGONHEADER) ||
                header->cb > bytes - offset)
                return false;
            std::vector<std::vector<float> > contour;
            {
                std::vector<float> p;
                p.push_back(static_cast<float>(fixedToDouble(header->pfxStart.x) / kEmUnits));
                p.push_back(static_cast<float>(fixedToDouble(header->pfxStart.y) / kEmUnits));
                contour.push_back(p);
            }
            size_t at = offset + sizeof(TTPOLYGONHEADER);
            const size_t end = offset + header->cb;
            while (at + sizeof(WORD) * 2 <= end) {
                const TTPOLYCURVE* curve = reinterpret_cast<const TTPOLYCURVE*>(&buffer[at]);
                const size_t length = sizeof(WORD) * 2 + sizeof(POINTFX) * curve->cpfx;
                if (!curve->cpfx || length > end - at) return false;
                if (curve->wType == TT_PRIM_LINE) {
                    for (unsigned i = 0; i < curve->cpfx; ++i) {
                        std::vector<float> p;
                        p.push_back(static_cast<float>(fixedToDouble(curve->apfx[i].x) / kEmUnits));
                        p.push_back(static_cast<float>(fixedToDouble(curve->apfx[i].y) / kEmUnits));
                        contour.push_back(p);
                    }
                } else if (curve->wType == TT_PRIM_QSPLINE && curve->cpfx >= 2) {
                    for (unsigned i = 0; i + 1 < curve->cpfx; ++i) {
                        const float bx = static_cast<float>(fixedToDouble(curve->apfx[i].x) / kEmUnits);
                        const float by = static_cast<float>(fixedToDouble(curve->apfx[i].y) / kEmUnits);
                        const float nx = static_cast<float>(fixedToDouble(curve->apfx[i + 1].x) / kEmUnits);
                        const float ny = static_cast<float>(fixedToDouble(curve->apfx[i + 1].y) / kEmUnits);
                        float tx = nx, ty = ny;
                        if (i + 2 < curve->cpfx) {
                            tx = 0.5f * (bx + nx);
                            ty = 0.5f * (by + ny);
                        }
                        const std::vector<float>& back = contour.back();
                        flattenQuad(&contour, back[0], back[1], bx, by, tx, ty);
                    }
                } else {
                    return false;   // TT_PRIM_CSPLINE: not produced by GGO_NATIVE
                }
                at += length;
            }
            offset = end;
            // Drop a duplicated closing point, which several fonts emit.
            if (contour.size() >= 2) {
                const std::vector<float>& a = contour.front();
                const std::vector<float>& b = contour.back();
                if (a[0] == b[0] && a[1] == b[1]) contour.pop_back();
            }
            if (contour.size() >= 3 && std::fabs(contourArea(contour)) > 1e-7f)
                out->contours.push_back(contour);
        }
        if (out->contours.empty()) {
            out->blank = true;
            return true;
        }
        // Ink box from the contours (the GLYPHMETRICS box is in device pixels at
        // the DC's font size and would be a second, disagreeing source).
        float x0 = out->contours[0][0][0], x1 = x0;
        float y0 = out->contours[0][0][1], y1 = y0;
        for (size_t c = 0; c < out->contours.size(); ++c)
            for (size_t p = 0; p < out->contours[c].size(); ++p) {
                const float x = out->contours[c][p][0];
                const float y = out->contours[c][p][1];
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        out->x0 = x0; out->y0 = y0; out->x1 = x1; out->y1 = y1;
        out->hasInk = true;
        return true;
    }

    static void makeTofu(Glyph* out) {
        const float boxes[2][4] = {
            { 0.06f, 0.00f, 0.74f, 0.72f },
            { 0.14f, 0.08f, 0.66f, 0.64f }
        };
        for (int b = 0; b < 2; ++b) {
            std::vector<std::vector<float> > c;
            const float xs[4] = { boxes[b][0], boxes[b][2], boxes[b][2], boxes[b][0] };
            const float ys[4] = { boxes[b][1], boxes[b][1], boxes[b][3], boxes[b][3] };
            for (int i = 0; i < 4; ++i) {
                const int k = (b == 0) ? i : (3 - i);
                std::vector<float> p;
                p.push_back(xs[k]);
                p.push_back(ys[k]);
                c.push_back(p);
            }
            out->contours.push_back(c);
        }
        out->advance = 0.8;
        out->x0 = 0.06; out->y0 = 0.0; out->x1 = 0.74; out->y1 = 0.72;
        out->hasInk = true;
    }

    std::string defaultFamily_;
    std::string cjkFamily_;
    std::map<std::string, DeviceContext*> contexts_;
    std::vector<std::string> contextKeys_;
    std::vector<std::string> fallbackLog_;
    size_t samples_, reuses_;
};

GdiProvider* g_lastProvider = 0;

#endif // _WIN32

} // namespace

bool gdiGlyphProviderAvailable() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

GlyphProvider* createGdiGlyphProvider(const std::string& defaultFamily,
                                      const std::string& cjkFamily) {
#ifdef _WIN32
    GdiProvider* p = new GdiProvider(defaultFamily, cjkFamily);
    g_lastProvider = p;
    return p;
#else
    (void)defaultFamily;
    (void)cjkFamily;
    return 0;
#endif
}

void gdiFallbackReport(std::vector<std::string>* out) {
    if (!out) return;
    out->clear();
#ifdef _WIN32
    if (g_lastProvider) *out = g_lastProvider->fallbackLog();
#endif
}

} // namespace text
} // namespace r62
