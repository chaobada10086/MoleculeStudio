#ifndef R62_NEXT_RENDER_OFFSCREEN_HPP
#define R62_NEXT_RENDER_OFFSCREEN_HPP
// =============================================================================
// re_round62_viewer/next/render/offscreen.hpp
//
// T03 - a HIDDEN Win32/WGL surface for reading back real GPU frames.
//
// Why this belongs to the render module: the only way to prove the GPU path draws
// the geometry is to read pixels from a real GPU-rendered frame. Doing that in a
// USER WINDOW is unacceptable - it would disturb a session that may hold unsaved
// work. This surface creates its own hidden popup window with its own pixel format
// and context, draws, reads back, and destroys everything. It never touches another
// process, never takes focus, and never appears on screen.
//
// It is a TEST/HARNESS facility. Nothing in the production draw path depends on it;
// an embedder supplies its own context via Backend::initialize().
// =============================================================================

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace r62 {
namespace render {

class OffscreenSurface {
public:
    OffscreenSurface();
    ~OffscreenSurface();

    // Creates a hidden window of the given size and makes its context current.
    // Returns false with a reason when no usable OpenGL is present; a caller must
    // treat that as 'skip the GPU evidence', not as a failure of the renderer.
    bool create(int width, int height, std::string* error);
    bool makeCurrent() const;
    void release() const;
    void destroy();
    bool ready() const;
    int width() const;
    int height() const;
#ifdef _WIN32
    HDC dc() const;
#endif
private:
    struct Impl;
    Impl* impl_;
    OffscreenSurface(const OffscreenSurface&);
    OffscreenSurface& operator=(const OffscreenSurface&);
};

} // namespace render
} // namespace r62

#endif // R62_NEXT_RENDER_OFFSCREEN_HPP
