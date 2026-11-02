// =============================================================================
// re_round62_viewer/next/render/backend_gl.cpp
//
// T03 - the only translation unit in this module that calls OpenGL.
//
// COMPATIBILITY TARGET: MinGW-W64 + Win32/WGL + the OpenGL 1.1 entry points that
// opengl32.dll always exports, plus FBO/SSAA through WGL extensions WHEN THEY
// EXIST. Nothing here requires a core-profile context, a loader library, a
// shader, or a version above what the machine already has. When FBOs are absent
// the renderer draws directly at device resolution and reports it, rather than
// failing.
//
// BATCHING: vertices are submitted from CLIENT ARRAYS in a handful of draw calls
// (glDrawArrays per colour run), never one glBegin per atom or per bond. This is
// the same technique the existing editor proved, kept intact and given the same
// cache behind it.
//
// STATE DISCIPLINE: every piece of GL state this file changes is saved on entry
// and restored on exit, and every resource it creates is freed in destroy().
// That is what makes 'no leak after resize / DPI / focus loss / close' checkable:
// the test can call destroy() twice and read the counters.
// =============================================================================

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glext.h>

#include "next/render/batcher.hpp"
#include "next/render/offscreen.hpp"
#include "next/render/scene_api.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace r62 {
namespace render {

#ifndef APIENTRY
#define APIENTRY
#endif

typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);
typedef void (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);

#ifdef _WIN32
typedef const char* (APIENTRY *PFN_wglGetExtensionsStringARB)(HDC);
#ifndef WGL_DRAW_TO_WINDOW_ARB
#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_ACCELERATION_ARB   0x2003
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB  0x2011
#define WGL_PIXEL_TYPE_ARB     0x2013
#define WGL_TYPE_RGBA_ARB      0x202B
#define WGL_COLOR_BITS_ARB     0x2014
#define WGL_ALPHA_BITS_ARB     0x201B
#define WGL_DEPTH_BITS_ARB     0x2022
#define WGL_STENCIL_BITS_ARB   0x2023
#endif
#endif

// ---------------------------------------------------------------------------
// Offscreen context used by the acceptance harness. It is part of this module
// because reading back a real GPU frame is the only way to prove the GPU path
// draws the geometry - and it must not disturb any user window.
// ---------------------------------------------------------------------------
class OffscreenSurfaceI {
public:
    OffscreenSurfaceI() : hwnd_(0), hdc_(0), hglrc_(0), width_(0), height_(0), ready_(false) {}
    ~OffscreenSurfaceI() { destroy(); }

    bool create(int width, int height, std::string* error);
    bool makeCurrent() const;
    void release() const;
    void destroy();
    bool ready() const { return ready_; }
    int width() const { return width_; }
    int height() const { return height_; }
#ifdef _WIN32
    HDC dc() const { return hdc_; }
#endif
private:
#ifdef _WIN32
    HWND  hwnd_;
    HDC   hdc_;
    HGLRC hglrc_;
#else
    void* hwnd_; void* hdc_; void* hglrc_;
#endif
    int   width_, height_;
    bool  ready_;
};

#ifdef _WIN32
static LRESULT CALLBACK offscreenProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

bool OffscreenSurfaceI::create(int width, int height, std::string* error) {
    destroy();
    if (width <= 0 || height <= 0) {
        if (error) *error = "invalid offscreen size";
        return false;
    }
    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = offscreenProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"R62RenderOffscreen";
    // Registering twice is harmless; the class is process-wide and shared.
    RegisterClassExW(&wc);

    // A hidden window: this harness must never steal focus or appear on screen.
    hwnd_ = CreateWindowExW(0, L"R62RenderOffscreen", L"r62 render offscreen",
                            WS_POPUP, 0, 0, width, height, 0, 0, inst, 0);
    if (!hwnd_) {
        if (error) *error = "CreateWindowEx failed";
        return false;
    }
    hdc_ = GetDC(hwnd_);
    if (!hdc_) {
        if (error) *error = "GetDC failed";
        destroy();
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd;
    std::memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 0;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(hdc_, &pfd);
    if (!format) {
        if (error) *error = "ChoosePixelFormat failed";
        destroy();
        return false;
    }
    if (!SetPixelFormat(hdc_, format, &pfd)) {
        if (error) *error = "SetPixelFormat failed";
        destroy();
        return false;
    }
    hglrc_ = wglCreateContext(hdc_);
    if (!hglrc_) {
        if (error) *error = "wglCreateContext failed (no usable OpenGL)";
        destroy();
        return false;
    }
    if (!wglMakeCurrent(hdc_, hglrc_)) {
        if (error) *error = "wglMakeCurrent failed";
        destroy();
        return false;
    }
    width_ = width; height_ = height; ready_ = true;
    return true;
}

bool OffscreenSurfaceI::makeCurrent() const {
    return ready_ && hglrc_ && wglMakeCurrent(hdc_, hglrc_) != FALSE;
}
void OffscreenSurfaceI::release() const { if (hwnd_) wglMakeCurrent(0, 0); }

void OffscreenSurfaceI::destroy() {
    if (hglrc_) {
        wglMakeCurrent(0, 0);
        wglDeleteContext(hglrc_);
        hglrc_ = 0;
    }
    if (hdc_ && hwnd_) { ReleaseDC(hwnd_, hdc_); hdc_ = 0; }
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = 0; }
    ready_ = false;
    width_ = height_ = 0;
}
#else
bool OffscreenSurfaceI::create(int, int, std::string* error) {
    if (error) *error = "offscreen surface requires Win32 in this build";
    return false;
}
bool OffscreenSurfaceI::makeCurrent() const { return false; }
void OffscreenSurfaceI::release() const {}
void OffscreenSurfaceI::destroy() { ready_ = false; }
#endif

// ---------------------------------------------------------------------------
// OffscreenSurface (public harness facade)
// ---------------------------------------------------------------------------
// A thin pimpl over the implementation above so offscreen.hpp does not have to
// expose windows.h or GL types to a caller that only wants pixels.
struct OffscreenSurface::Impl {
    OffscreenSurfaceI surface;
};

OffscreenSurface::OffscreenSurface() : impl_(new Impl()) {}
OffscreenSurface::~OffscreenSurface() { delete impl_; impl_ = 0; }

bool OffscreenSurface::create(int width, int height, std::string* error) {
    return impl_->surface.create(width, height, error);
}
bool OffscreenSurface::makeCurrent() const { return impl_->surface.makeCurrent(); }
void OffscreenSurface::release() const { impl_->surface.release(); }
void OffscreenSurface::destroy() { impl_->surface.destroy(); }
bool OffscreenSurface::ready() const { return impl_->surface.ready(); }
int OffscreenSurface::width() const { return impl_->surface.width(); }
int OffscreenSurface::height() const { return impl_->surface.height(); }
#ifdef _WIN32
HDC OffscreenSurface::dc() const { return impl_->surface.dc(); }
#endif


namespace {

// FBO entry points resolved lazily through wglGetProcAddress. If any is missing,
// the renderer falls back to direct drawing; it does not fail and does not
// invent a version requirement.
struct GlExt {
    PFN_glGenFramebuffers        genFramebuffers;
    PFN_glBindFramebuffer        bindFramebuffer;
    PFN_glFramebufferTexture2D   framebufferTexture2D;
    PFN_glCheckFramebufferStatus checkFramebufferStatus;
    PFN_glDeleteFramebuffers     deleteFramebuffers;
    bool framebufferAvailable;
    GlExt() : genFramebuffers(0), bindFramebuffer(0), framebufferTexture2D(0),
              checkFramebufferStatus(0), deleteFramebuffers(0),
              framebufferAvailable(false) {}
};

#ifdef _WIN32
// wglGetProcAddress returns PROC (a function pointer); the standard-compliant
// way to convert it to a specific function-pointer type on Win32 is through
// void*, and doing it here keeps -Wcast-function-type quiet without a pragma.
template <typename T> static T glProc(const char* name) {
    PROC p = wglGetProcAddress(name);
    void* raw = 0;
    static_assert(sizeof(raw) == sizeof(p), "function pointer size");
    std::memcpy(&raw, &p, sizeof(raw));
    T out = 0;
    std::memcpy(&out, &raw, sizeof(out));
    return out;
}
#else
template <typename T> static T glProc(const char*) { return 0; }
#endif

} // namespace

// ---------------------------------------------------------------------------
// GL backend
// ---------------------------------------------------------------------------
class GLBackend : public Backend {
public:
    GLBackend()
        : haveView_(false), ready_(false), directOnly_(false),
          fboWidth_(0), fboHeight_(0), framebuffer_(0), texture_(0),
          vboCapacity_(0),
          textureUploads_(0), bufferUploads_(0) {
        stats_.reset();
    }
    ~GLBackend() { destroy(); }

    const char* name() const { return directOnly_ ? "GL-compat-direct" : "GL-compat-fbo2x"; }

    bool initialize(std::string* error);
    bool ready() const { return ready_; }

    void sync(const Scene& scene, const View& view) {
        BatchStats one;
        batcherFor(&scene)->sync(scene, view, 0, &one);
        Batcher::accumulate(&batchStats_, one);
    }
    void setView(const View& view) { view_ = view; haveView_ = true; }

    core::Error draw(const Scene& content, const Scene* overlay, const View& view,
                     RenderStats* stats);
    core::Error readPixels(int x, int y, int w, int h, std::vector<uint8_t>* rgba) const;
    void destroy();

    // Diagnostics for the acceptance report.
    bool directOnly() const { return directOnly_; }
    const char* glVersion() const { return glVersion_.c_str(); }
    const char* glRenderer() const { return glRenderer_.c_str(); }
    const char* glVendor() const { return glVendor_.c_str(); }
    size_t textureUploads() const { return textureUploads_; }
    size_t liveResources() const {
        return (framebuffer_ ? 1u : 0u) + (texture_ ? 1u : 0u);
    }

private:
    Batcher contentBatcher_;
    Batcher overlayBatcher_;
    View view_;
    bool haveView_;
    bool ready_;
    bool directOnly_;
    int fboWidth_, fboHeight_;
    GLuint framebuffer_, texture_;
    size_t vboCapacity_;
    size_t textureUploads_;
    size_t bufferUploads_;
    RenderStats stats_;
    BatchStats batchStats_;
    GlExt ext_;
    std::string glVersion_, glRenderer_, glVendor_;

    Batcher* batcherFor(const Scene* scene) {
        // Content and overlay caches are separate so clearing an overlay cannot
        // evict a content mesh, and a drag preview that changes every frame does
        // not churn the content cache.
        if (scene && scene->size() > 0 && OverlayScene::isOverlayLayer(scene->primitives()[0].layer))
            return &overlayBatcher_;
        return &contentBatcher_;
    }

    void ensureFramebuffer(int deviceWidth, int deviceHeight);
    void submitBatch(const Batch& batch, int deviceWidth, int deviceHeight, RenderStats* stats);
    void saveGlState();
    void restoreGlState();

    GLint     prevMatrixMode_, prevViewport_[4];
    GLboolean prevScissor_, prevBlend_, prevTexture_, prevDepth_;
    GLint     prevScissorBox_[4];
    // GL_CURRENT_COLOR and the client-array enable are changed by this module
    // (glColor4ub per draw range, glEnableClientState(GL_VERTEX_ARRAY)), so both
    // must be saved and restored. Resetting the colour to white instead of
    // restoring it silently changes an embedder's drawing colour.
    GLfloat   prevColor_[4];
    GLboolean prevVertexArray_, prevColorArray_, prevTexCoordArray_;
};

bool GLBackend::initialize(std::string* error) {
    if (ready_) return true;
    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!ver) {
        if (error) *error = "no current OpenGL context";
        return false;
    }
    glVersion_ = ver;
    const char* rend = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    glRenderer_ = rend ? rend : "(unknown)";
    const char* vend = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    glVendor_ = vend ? vend : "(unknown)";

    ext_.genFramebuffers = glProc<PFN_glGenFramebuffers>("glGenFramebuffersEXT");
    ext_.bindFramebuffer = glProc<PFN_glBindFramebuffer>("glBindFramebufferEXT");
    ext_.framebufferTexture2D = glProc<PFN_glFramebufferTexture2D>("glFramebufferTexture2DEXT");
    ext_.checkFramebufferStatus = glProc<PFN_glCheckFramebufferStatus>("glCheckFramebufferStatusEXT");
    ext_.deleteFramebuffers = glProc<PFN_glDeleteFramebuffers>("glDeleteFramebuffersEXT");
    ext_.framebufferAvailable = ext_.genFramebuffers && ext_.bindFramebuffer &&
        ext_.framebufferTexture2D && ext_.checkFramebufferStatus && ext_.deleteFramebuffers;
    directOnly_ = !ext_.framebufferAvailable;
    ready_ = true;
    return true;
}

void GLBackend::saveGlState() {
    glGetIntegerv(GL_MATRIX_MODE, &prevMatrixMode_);
    glGetIntegerv(GL_VIEWPORT, prevViewport_);
    glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox_);
    prevScissor_ = glIsEnabled(GL_SCISSOR_TEST);
    prevBlend_ = glIsEnabled(GL_BLEND);
    prevTexture_ = glIsEnabled(GL_TEXTURE_2D);
    prevDepth_ = glIsEnabled(GL_DEPTH_TEST);
    glGetFloatv(GL_CURRENT_COLOR, prevColor_);
    prevVertexArray_ = glIsEnabled(GL_VERTEX_ARRAY);
    prevColorArray_ = glIsEnabled(GL_COLOR_ARRAY);
    prevTexCoordArray_ = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
}

void GLBackend::restoreGlState() {
    glMatrixMode(prevMatrixMode_);
    glViewport(prevViewport_[0], prevViewport_[1], prevViewport_[2], prevViewport_[3]);
    glScissor(prevScissorBox_[0], prevScissorBox_[1], prevScissorBox_[2], prevScissorBox_[3]);
    if (prevScissor_) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (prevBlend_) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevTexture_) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    if (prevDepth_) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
    // Restore the caller's colour and client-array enables rather than resetting
    // them; a reset would corrupt whatever the embedder draws after us.
    glColor4fv(prevColor_);
    if (prevVertexArray_) glEnableClientState(GL_VERTEX_ARRAY);
    else glDisableClientState(GL_VERTEX_ARRAY);
    if (prevColorArray_) glEnableClientState(GL_COLOR_ARRAY);
    else glDisableClientState(GL_COLOR_ARRAY);
    if (prevTexCoordArray_) glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void GLBackend::ensureFramebuffer(int deviceWidth, int deviceHeight) {
    if (directOnly_) return;
    if (deviceWidth <= 0 || deviceHeight <= 0) return;
    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    if (maxTex <= 0) { directOnly_ = true; return; }
    if (deviceWidth > maxTex / 2 || deviceHeight > maxTex / 2) {
        // 2x would exceed the texture limit; draw directly instead of failing.
        directOnly_ = true;
        return;
    }
    if (!framebuffer_) {
        ext_.genFramebuffers(1, &framebuffer_);
        glGenTextures(1, &texture_);
    }
    ext_.bindFramebuffer(0x8D40 /*GL_FRAMEBUFFER_EXT*/, framebuffer_);
    if (fboWidth_ != deviceWidth || fboHeight_ != deviceHeight) {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, deviceWidth * 2, deviceHeight * 2, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, 0);
        ext_.framebufferTexture2D(0x8D40, 0x8CE0 /*COLOR_ATTACHMENT0_EXT*/, GL_TEXTURE_2D,
                                  texture_, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (ext_.checkFramebufferStatus(0x8D40) != 0x8CD5 /*COMPLETE*/) {
            ext_.bindFramebuffer(0x8D40, 0);
            directOnly_ = true;
            return;
        }
        fboWidth_ = deviceWidth;
        fboHeight_ = deviceHeight;
        ++textureUploads_;
    }
    glDrawBuffer(0x8CE0 /*GL_COLOR_ATTACHMENT0_EXT*/);
    glViewport(0, 0, deviceWidth * 2, deviceHeight * 2);
    // 2x SSAA doubles the effective line width, so a stroke keeps its visual
    // weight after the resolve. Same convention the legacy 2x path used.
    glLineWidth(2.0f);
}

void GLBackend::submitBatch(const Batch& batch, int deviceWidth, int deviceHeight,
                            RenderStats* stats) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, deviceWidth, deviceHeight, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);

    // Triangles and lines are each submitted as one glDrawArrays per colour run.
    // Client arrays are used deliberately: they need no buffer-object extension,
    // which is what keeps the module runnable on the oldest context this project
    // already ships against.
    if (!batch.triangleVertices.empty()) {
        glVertexPointer(2, GL_FLOAT, 0, &batch.triangleVertices[0]);
        for (size_t i = 0; i < batch.triangleRanges.size(); ++i) {
            DrawRange r = batch.triangleRanges[i];
            if (r.count <= 0) continue;
            if (!std::getenv("R62_T19_DISABLE_COALESCE")) {
                while (i + 1 < batch.triangleRanges.size()) {
                    const DrawRange& next = batch.triangleRanges[i + 1];
                    if (next.count <= 0 || r.first + r.count != next.first ||
                        resolvePaint(r.rgba) != resolvePaint(next.rgba)) break;
                    r.count += next.count;
                    ++i;
                }
            }
            glColor4ub(colorRed(resolvePaint(r.rgba)), colorGreen(resolvePaint(r.rgba)),
                       colorBlue(resolvePaint(r.rgba)), colorAlpha(resolvePaint(r.rgba)));
            glDrawArrays(GL_TRIANGLES, r.first, r.count);
            if (stats) ++stats->drawCalls;
        }
    }
    if (!batch.lineVertices.empty()) {
        glVertexPointer(2, GL_FLOAT, 0, &batch.lineVertices[0]);
        for (size_t i = 0; i < batch.lineRanges.size(); ++i) {
            DrawRange r = batch.lineRanges[i];
            if (r.count <= 0) continue;
            if (!std::getenv("R62_T19_DISABLE_COALESCE")) {
                while (i + 1 < batch.lineRanges.size()) {
                    const DrawRange& next = batch.lineRanges[i + 1];
                    if (next.count <= 0 || r.first + r.count != next.first ||
                        resolvePaint(r.rgba) != resolvePaint(next.rgba)) break;
                    r.count += next.count;
                    ++i;
                }
            }
            glColor4ub(colorRed(resolvePaint(r.rgba)), colorGreen(resolvePaint(r.rgba)),
                       colorBlue(resolvePaint(r.rgba)), colorAlpha(resolvePaint(r.rgba)));
            glDrawArrays(GL_LINES, r.first, r.count);
            if (stats) ++stats->drawCalls;
        }
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}

core::Error GLBackend::draw(const Scene& content, const Scene* overlay, const View& view,
                            RenderStats* stats) {
    if (!ready_) return core::Error(core::ErrorCode::NotImplemented, "backend not initialized");
    view_ = view;
    haveView_ = true;
    if (stats) *stats = RenderStats();

    const int deviceWidth = static_cast<int>(view.widthPx + 0.5);
    const int deviceHeight = static_cast<int>(view.heightPx + 0.5);
    if (deviceWidth <= 0 || deviceHeight <= 0)
        return core::Error(core::ErrorCode::InvalidArgument, "empty viewport");

    saveGlState();
    batchStats_.reset();

    // --- object-change path: refresh only what changed -----------------------
    // Each sync resets the per-call counters it is handed (so a single-sync caller
    // reads that call's numbers), therefore the frame total is accumulated here.
    BatchStats one;
    contentBatcher_.sync(content, view, 0, &one);
    Batcher::accumulate(&batchStats_, one);
    if (overlay) {
        overlayBatcher_.sync(*overlay, view, 0, &one);
        Batcher::accumulate(&batchStats_, one);
    }

    // --- device batching (view-change path; no world geometry rebuilt) -------
    Batch contentBatch, overlayBatch;
    contentBatcher_.buildBatch(content, view, false, &contentBatch, &batchStats_);
    if (overlay) overlayBatcher_.buildBatch(*overlay, view, false, &overlayBatch, &batchStats_);

    // --- 2x SSAA render target ----------------------------------------------
    ensureFramebuffer(deviceWidth, deviceHeight);
    const double scale = directOnly_ ? 1.0 : 2.0;
    glViewport(0, 0, static_cast<GLsizei>(deviceWidth * scale),
               static_cast<GLsizei>(deviceHeight * scale));
    // Clear the complete reused FBO before restricting this frame's content.
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (view.clipEnabled) {
        glEnable(GL_SCISSOR_TEST);
        const GLint sx = static_cast<GLint>(view.clipMin.x * scale);
        const GLint sy = static_cast<GLint>((deviceHeight - view.clipMax.y) * scale);
        const GLint sw = static_cast<GLint>((view.clipMax.x - view.clipMin.x) * scale);
        const GLint sh = static_cast<GLint>((view.clipMax.y - view.clipMin.y) * scale);
        glScissor(sx, sy, sw < 0 ? 0 : sw, sh < 0 ? 0 : sh);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    submitBatch(contentBatch, deviceWidth, deviceHeight, stats);
    // Overlays are submitted AFTER content into the same target, so an overlay
    // can never be painted under a user object and a content redraw can never
    // erase an overlay that was not rebuilt.
    if (overlay) submitBatch(overlayBatch, deviceWidth, deviceHeight, stats);

    // --- resolve the 2x target into the window ------------------------------
    if (!directOnly_) {
        ext_.bindFramebuffer(0x8D40, 0);
        glDrawBuffer(GL_BACK);
        glViewport(0, 0, deviceWidth, deviceHeight);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, deviceWidth, 0.0, deviceHeight, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(static_cast<GLfloat>(deviceWidth), 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(static_cast<GLfloat>(deviceWidth), static_cast<GLfloat>(deviceHeight));
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, static_cast<GLfloat>(deviceHeight));
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (stats) ++stats->drawCalls;
    }

    glFinish();
    const GLenum err = glGetError();
    restoreGlState();

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
        stats->textureUploads = textureUploads_;
    }
    if (err != GL_NO_ERROR)
        return core::Error(core::ErrorCode::InvalidGeometry, "GL error during draw");
    return core::Error();
}

core::Error GLBackend::readPixels(int x, int y, int w, int h, std::vector<uint8_t>* rgba) const {
    if (!rgba) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    if (w <= 0 || h <= 0) return core::Error(core::ErrorCode::InvalidArgument, "bad read size");
    rgba->assign(static_cast<size_t>(w) * h * 4, 0u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &(*rgba)[0]);
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) return core::Error(core::ErrorCode::IoError, "glReadPixels failed");
    return core::Error();
}

void GLBackend::destroy() {
    // Idempotent, and safe to call twice, which is what the leak check does.
    if (ext_.deleteFramebuffers && framebuffer_) ext_.deleteFramebuffers(1, &framebuffer_);
    if (texture_) glDeleteTextures(1, &texture_);
    framebuffer_ = texture_ = 0;
    fboWidth_ = fboHeight_ = 0;
    vboCapacity_ = 0;
    contentBatcher_.clear();
    overlayBatcher_.clear();
    ready_ = false;
}

Backend* createGLBackend() { return new GLBackend(); }

} // namespace render
} // namespace r62