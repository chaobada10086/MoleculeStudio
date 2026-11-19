// =============================================================================
// re_round62_viewer/next/render/demo_render.cpp
//
// T03 - runnable demo of the shared render kernel.
//
// MODES, because they answer different questions:
//
//   demo_render --offscreen <outDir>
//       Renders the demo scene through the SOFTWARE rasterizer over the shared
//       Primitive records and writes a PNG. No GPU, no window. Produces the
//       geometry evidence.
//
//   demo_render --gl
//       Opens a REAL Win32/WGL window, builds the scene, and draws it with the GL
//       backend, including a drag preview. Shows the pipeline end to end on this
//       machine's OpenGL.
//
//   demo_render --gl-offscreen
//       The same GL path on a HIDDEN window, printing statistics. For scripted
//       checks where no visible window may appear.
//
// The demo owns its own window and its own document. It does not touch, read or
// modify any other process, and it neither requires nor alters licence state.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/core/document.hpp"
#include "next/render/batcher.hpp"
#include "next/render/geometry.hpp"
#include "next/render/offscreen.hpp"
#include "next/render/scene_api.hpp"
#include "next/render/scene_builder.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace r62::render;
using r62::core::Vec2;
using r62::core::Box;

static const double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// A deterministic synthetic measurer so the demo needs no font file. The module
// takes the measurer by injection, so a real editor passes the GDI-outline one.
// ---------------------------------------------------------------------------
class DemoMeasurer : public geom::TextMeasurer {
public:
    DemoMeasurer() : reuses_(0) {}
    bool glyph(uint32_t cp, const std::string&, geom::GlyphMetrics* out) {
        if (!out) return false;
        if (cp == 0xFFFDu) return false;
        std::map<uint32_t, int>::iterator it = seen_.find(cp);
        if (it == seen_.end()) seen_[cp] = 1; else ++reuses_;
        const bool cjk = cp > 0x2E80u;
        out->advance = cjk ? 1.0 : 0.55;
        const double w = cjk ? 0.86 : 0.46;
        const double h = cjk ? 0.86 : 0.70;
        out->x0 = 0.07; out->y0 = 0.0; out->x1 = 0.07 + w; out->y1 = h;
        out->missing = false;
        out->triangles.clear();
        if (cp == 0x20u) return true;
        const float x0 = static_cast<float>(out->x0), y0 = 0.0f;
        const float x1 = static_cast<float>(out->x1), y1 = static_cast<float>(h);
        // A box with a clipped corner, so a transform error is visible.
        out->triangles.push_back(x0); out->triangles.push_back(y0);
        out->triangles.push_back(x1); out->triangles.push_back(y0);
        out->triangles.push_back(x1); out->triangles.push_back(y1);
        out->triangles.push_back(x0); out->triangles.push_back(y0);
        out->triangles.push_back(x1); out->triangles.push_back(y1);
        out->triangles.push_back(x0); out->triangles.push_back(static_cast<float>(h * 0.7));
        return true;
    }
    size_t sampleCount() const { return seen_.size(); }
    size_t reuseCount() const { return reuses_; }
private:
    std::map<uint32_t, int> seen_;
    size_t reuses_;
};

// ---------------------------------------------------------------------------
// The demo scene: a hexagon with an inner ring, both text kinds, a filled ring,
// a dashed leader, a wavy bond, a curve and an arrow.
// ---------------------------------------------------------------------------
static Scene buildDemoScene(DemoMeasurer* measurer) {
    Scene s;
    LayerId content(static_cast<uint32_t>(layers::kContent));
    const double cx = 70.0, cy = 60.0, r = 26.0;
    Vec2 v[6];
    for (int i = 0; i < 6; ++i) {
        const double a = kPi * 2.0 * i / 6.0;
        v[i] = Vec2(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    for (int i = 0; i < 6; ++i) {
        Primitive p;
        p.kind = PrimKind::Polyline;
        p.layer = content;
        p.points.push_back(v[i]);
        p.points.push_back(v[(i + 1) % 6]);
        p.style.strokeWidth = 1.4;
        p.style.stroke.rgba = makeColor(0x10, 0x10, 0x10, 0xFF);
        p.style.cap = CapStyle::Round;
        p.bounds = geom::computeBounds(p, 1.4);
        s.add(p);
    }
    for (int i = 0; i < 3; ++i) {
        const int a = i * 2;
        const int b = (i * 2 + 1) % 6;
        const Vec2 pa(v[a].x * 0.78 + cx * 0.22, v[a].y * 0.78 + cy * 0.22);
        const Vec2 pb(v[b].x * 0.78 + cx * 0.22, v[b].y * 0.78 + cy * 0.22);
        Primitive p;
        p.kind = PrimKind::Segment;
        p.layer = content;
        p.points.push_back(pa);
        p.points.push_back(pb);
        p.style.strokeWidth = 1.4;
        p.bounds = geom::computeBounds(p, 1.4);
        s.add(p);
    }
    {
        Primitive p;
        p.kind = PrimKind::Text;
        p.layer = content;
        p.text = "C6H6";
        p.textSize = 14.0;
        const Vec2 baseline(cx - 18.0, cy + 8.0);
        p.points.push_back(baseline);
        p.style.fill = Paint(makeColor(0x10, 0x10, 0x10, 0xFF), true);
        p.style.stroke.enabled = false;
        geom::Mesh m; Box ink; double adv = 0.0;
        geom::layoutText(p.text, baseline, p.textSize, std::string(), measurer, &m, &ink, &adv);
        p.bounds = ink;
        s.add(p);
    }
    {
        Primitive p;
        p.kind = PrimKind::Text;
        p.layer = content;
        p.text = "\xE8\x8B\xAF\xE7\x8E\xAF";
        p.textSize = 13.0;
        const Vec2 baseline(cx - 16.0, cy + 128.0);
        p.points.push_back(baseline);
        p.style.fill = Paint(makeColor(0x20, 0x20, 0x80, 0xFF), true);
        p.style.stroke.enabled = false;
        geom::Mesh m; Box ink; double adv = 0.0;
        geom::layoutText(p.text, baseline, p.textSize, std::string(), measurer, &m, &ink, &adv);
        p.bounds = ink;
        s.add(p);
    }
    {
        Primitive p;
        p.kind = PrimKind::Marker;
        p.layer = content;
        p.points.push_back(Vec2(205, 65));
        p.marker = MarkerShape::Ring;
        p.size = 52.0;
        p.style.fill = Paint(makeColor(0x20, 0xA0, 0x40, 0xFF), true);
        p.bounds = geom::computeBounds(p, 1.0);
        s.add(p);
    }
    {
        Primitive p;
        p.kind = PrimKind::Segment;
        p.layer = content;
        p.points.push_back(Vec2(150, 130));
        p.points.push_back(Vec2(262, 130));
        p.style.strokeWidth = 1.6;
        p.style.dash = DashPattern(3.0, 2.0);
        p.bounds = geom::computeBounds(p, 1.6);
        s.add(p);
    }
    {
        Primitive p;
        p.kind = PrimKind::Segment;
        p.layer = content;
        p.points.push_back(Vec2(150, 152));
        p.points.push_back(Vec2(262, 152));
        p.style.strokeWidth = 1.6;
        p.style.wavyAmplitude = 2.2;
        p.style.wavyWavelength = 12.0;
        p.style.stroke.rgba = makeColor(0xC0, 0x20, 0x20, 0xFF);
        p.bounds = geom::computeBounds(p, 1.6);
        s.add(p);
    }
    {
        Primitive p;
        p.kind = PrimKind::Curve;
        p.layer = content;
        p.points.push_back(Vec2(165, 190));
        p.points.push_back(Vec2(210, 240));
        p.points.push_back(Vec2(255, 190));
        p.pointKind.push_back(static_cast<uint8_t>(PointKind::Corner));
        p.pointKind.push_back(static_cast<uint8_t>(PointKind::QuadControl));
        p.pointKind.push_back(static_cast<uint8_t>(PointKind::Corner));
        p.style.strokeWidth = 1.8;
        p.bounds = geom::computeBounds(p, 1.8);
        s.add(p);
    }
    {
        Primitive shaft;
        shaft.kind = PrimKind::Segment;
        shaft.layer = content;
        shaft.points.push_back(Vec2(40, 220));
        shaft.points.push_back(Vec2(140, 220));
        shaft.style.strokeWidth = 2.0;
        shaft.bounds = geom::computeBounds(shaft, 2.0);
        s.add(shaft);
        Primitive head;
        head.kind = PrimKind::Arrowhead;
        head.layer = content;
        head.points.push_back(Vec2(120, 220));
        head.points.push_back(Vec2(140, 220));
        head.arrow = ArrowShape::Triangle;
        head.size = 12.0;
        head.style.fill = Paint(makeColor(0x10, 0x10, 0x10, 0xFF), true);
        head.bounds = geom::computeBounds(head, 1.0);
        s.add(head);
    }
    return s;
}

static View demoView(double w, double h) {
    View view;
    view.widthPx = w;
    view.heightPx = h;
    view.transform.scale = 1.0;
    view.transform.dpiScale = 1.0;
    view.transform.center = Vec2(150.0, 130.0);
    view.transform.viewportCenter = Vec2(w * 0.5, h * 0.5);
    view.defaultStrokeWidth = 1.0;
    view.defaultTextSize = 13.0;
    return view;
}

// ---------------------------------------------------------------------------
// Offscreen software render
// ---------------------------------------------------------------------------
static int runOffscreen(const std::string& outDir) {
    DemoMeasurer measurer;
    Scene scene = buildDemoScene(&measurer);
    const int W = 320, H = 280;
    View view = demoView(W, H);
    RasterOptions opt;
    opt.width = W;
    opt.height = H;
    opt.supersample = 2;
    opt.background = 0xFFFFFFFFu;
    std::vector<uint8_t> rgba;
    r62::core::Error e = rasterizeWithText(scene, 0, view, opt, &measurer, &rgba);
    if (!e.ok()) {
        std::printf("demo: rasterize failed: %s\n", e.message.c_str());
        return 1;
    }
    const std::string path = outDir + "/demo_scene_320x280_2x.png";
    e = writePng(path, W, H, rgba);
    if (!e.ok()) {
        std::printf("demo: writePng failed: %s\n", e.message.c_str());
        return 1;
    }
    std::printf("demo: wrote %s\n", path.c_str());
    std::printf("demo: primitives=%zu glyphsSampled=%zu\n", scene.size(), measurer.sampleCount());
    return 0;
}

#ifdef _WIN32

#include <windows.h>
#include <GL/gl.h>

#define R62_GET_X(l) (static_cast<short>(LOWORD(l)))
#define R62_GET_Y(l) (static_cast<short>(HIWORD(l)))

static const wchar_t* kClassName = L"R62RenderDemo";
static bool g_running = true;
static bool g_dragging = false;
static double g_dragDx = 0.0, g_dragDy = 0.0;

struct DemoState {
    Scene scene;
    SceneBuilder builder;
    r62::core::Document doc;
    Backend* backend;
    View view;
    DemoMeasurer measurer;
    bool showOverlay;
    int width, height;
    DemoState() : backend(0), showOverlay(true), width(720), height(560) {}
};

static LRESULT CALLBACK demoProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    DemoState* st = reinterpret_cast<DemoState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
        case WM_CREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
                reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
            return 0;
        case WM_SIZE:
            if (st) {
                st->width = LOWORD(l);
                st->height = HIWORD(l);
                if (st->width < 1) st->width = 1;
                if (st->height < 1) st->height = 1;
            }
            return 0;
        case WM_MOUSEMOVE:
            if (st) {
                // A drag publishes an overlay preview ONLY. The content scene and its
                // caches are untouched, which is the property the perf harness
                // measures frame by frame.
                g_dragDx = g_dragging ? (R62_GET_X(l) - st->width / 2) * 0.1 : 0.0;
                g_dragDy = g_dragging ? (R62_GET_Y(l) - st->height / 2) * 0.1 : 0.0;
                InvalidateRect(h, 0, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            g_dragging = true;
            SetCapture(h);
            return 0;
        case WM_LBUTTONUP:
            g_dragging = false;
            g_dragDx = g_dragDy = 0.0;
            ReleaseCapture();
            InvalidateRect(h, 0, FALSE);
            return 0;
        case WM_KEYDOWN:
            if (st && w == 'O') { st->showOverlay = !st->showOverlay; InvalidateRect(h, 0, FALSE); }
            if (w == VK_ESCAPE) { g_running = false; DestroyWindow(h); }
            return 0;
        case WM_CLOSE:
            g_running = false;
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(h, m, w, l);
}

static int runWindowed() {
    DemoState state;
    state.scene = buildDemoScene(&state.measurer);
    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = demoProc;
    wc.hInstance = GetModuleHandleW(0);
    // IDC_ARROW is MAKEINTRESOURCE(32512); the ANSI headers type that as char*, so
    // the documented way to hand a system resource id to a W entry point is this cast.
    wc.hCursor = LoadCursorW(0, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) { std::printf("demo: RegisterClass failed\n"); return 1; }
    RECT rc = { 0, 0, state.width, state.height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, kClassName,
        L"T03 shared render kernel demo - drag to preview, O toggles the overlay, Esc quits",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, 0, 0, wc.hInstance, &state);
    if (!hwnd) { std::printf("demo: CreateWindow failed\n"); return 1; }
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    std::memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int fmt = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, fmt, &pfd);
    HGLRC glrc = wglCreateContext(dc);
    if (!glrc || !wglMakeCurrent(dc, glrc)) {
        std::printf("demo: cannot create an OpenGL context\n");
        return 1;
    }
    std::string err;
    state.backend = createGLBackend();
    state.backend->initialize(&err);
    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::printf("demo: GL backend=%s version=%s\n", state.backend->name(), ver ? ver : "?");
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    size_t frames = 0;
    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;
        state.view = demoView(state.width, state.height);
        OverlayScene overlay;
        if (state.showOverlay) {
            state.builder.buildMarquee(Vec2(150.0 + g_dragDx, 20.0 + g_dragDy),
                                       Vec2(272.0 + g_dragDx, 110.0 + g_dragDy),
                                       state.view.transform, &overlay);
        }
        RenderStats stats;
        const r62::core::Error e = state.backend->draw(state.scene, &overlay.scene(),
                                                       state.view, &stats);
        if (!e.ok()) { std::printf("demo: draw failed: %s\n", e.message.c_str()); break; }
        SwapBuffers(dc);
        ++frames;
        if (frames == 1) {
            std::printf("demo: first frame prims=%zu drawn=%zu calls=%zu tris=%zu\n",
                        stats.primitivesTotal, stats.primitivesDrawn, stats.drawCalls,
                        stats.triangles);
        }
        Sleep(8);
    }
    std::printf("demo: rendered %zu frames, exiting cleanly\n", frames);
    state.backend->destroy();
    delete state.backend;
    wglMakeCurrent(0, 0);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, dc);
    return 0;
}

static int runGLHidden() {
    DemoMeasurer measurer;
    Scene scene = buildDemoScene(&measurer);
    OffscreenSurface surface;
    std::string err;
    if (!surface.create(320, 280, &err)) {
        std::printf("demo: no GPU path available: %s\n", err.c_str());
        return 0;
    }
    Backend* backend = createGLBackend();
    if (!backend->initialize(&err)) {
        std::printf("demo: backend init failed: %s\n", err.c_str());
        delete backend;
        return 1;
    }
    View view = demoView(320, 280);
    RenderStats stats;
    const r62::core::Error e = backend->draw(scene, 0, view, &stats);
    std::printf("demo[gl-hidden]: backend=%s draw=%s prims=%zu drawn=%zu calls=%zu tris=%zu\n",
                backend->name(), e.ok() ? "ok" : e.message.c_str(),
                stats.primitivesTotal, stats.primitivesDrawn, stats.drawCalls, stats.triangles);
    // Prove the cache held across a second frame.
    RenderStats s2;
    backend->draw(scene, 0, view, &s2);
    std::printf("demo[gl-hidden]: second frame meshRebuilds=%zu (expect 0)\n", s2.meshRebuilds);
    backend->destroy();
    delete backend;
    surface.destroy();
    return e.ok() ? 0 : 1;
}

#endif // _WIN32

int main(int argc, char** argv) {
    std::string mode = "--offscreen";
    std::string outDir = ".";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gl") == 0) mode = "--gl";
        else if (std::strcmp(argv[i], "--gl-offscreen") == 0) mode = "--gl-offscreen";
        else if (std::strcmp(argv[i], "--offscreen") == 0) mode = "--offscreen";
        else outDir = argv[i];
    }
    if (mode == "--offscreen") return runOffscreen(outDir);
#ifdef _WIN32
    if (mode == "--gl-offscreen") return runGLHidden();
    if (mode == "--gl") return runWindowed();
#else
    if (mode == "--gl" || mode == "--gl-offscreen") {
        std::printf("demo: GL modes require Win32 in this build\n");
        return 0;
    }
#endif
    std::printf("usage: demo_render [--offscreen <outDir> | --gl | --gl-offscreen]\n");
    return 2;
}
