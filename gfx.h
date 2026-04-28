#pragma once

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>

// ====================== COLOR ======================
// Platform-agnostic color struct. All UI code uses this.
// Backends convert to their native format.

struct UIColor {
    uint8_t r, g, b, a;

    constexpr UIColor() : r(0), g(0), b(0), a(255) {}
    constexpr UIColor(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr UIColor rgb(uint8_t r, uint8_t g, uint8_t b) {
        return {r, g, b, 255};
    }
    static constexpr UIColor rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return {r, g, b, a};
    }

    bool operator==(const UIColor &o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const UIColor &o) const { return !(*this == o); }
};

// ====================== GFX INTERFACE ======================
// Abstract drawing backend. All game rendering goes through this.
// Implementations: GfxRaylib, (future) GfxTiny3D

class Gfx {
public:
    virtual ~Gfx() = default;

    // --- Frame lifecycle ---
    virtual void begin_frame(int screen_w, int screen_h) = 0;
    virtual void end_frame() = 0;

    // --- Primitives ---
    virtual void clear(UIColor color) = 0;
    virtual void draw_rect(int x, int y, int w, int h, UIColor color) = 0;
    virtual void draw_rect_outline(int x, int y, int w, int h, UIColor color) = 0;
    virtual void draw_line(int x1, int y1, int x2, int y2, UIColor color) = 0;
    virtual void draw_circle(int cx, int cy, int radius, UIColor color) = 0;
    virtual void draw_text(int x, int y, const char *text, int size,
                           UIColor color) = 0;
    virtual int text_width(const char *text, int size) = 0;

    // --- Texture (opaque handle) ---
    using Tex = void *;
    static constexpr Tex NO_TEX = nullptr;

    virtual Tex load_texture(const char *path) = 0;
    virtual void free_texture(Tex tex) = 0;
    // Draw full texture stretched to dest rect
    virtual void draw_texture(Tex tex, int x, int y, int w, int h) = 0;

    // Draw a sub-region of a texture (sprite sheet support)
    virtual void draw_texture_region(Tex tex,
                                     int src_x, int src_y, int src_w, int src_h,
                                     int dst_x, int dst_y, int dst_w, int dst_h) = 0;
};

// ====================== FORMAT HELPER ======================
// Portable string formatting (replaces raylib's TextFormat)

inline std::string fmt(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return std::string(buf);
}
