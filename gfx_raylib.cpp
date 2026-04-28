#include "gfx_raylib.h"

#include <cstdlib>

// ====================== HELPERS ======================

Color GfxRaylib::to_raylib(UIColor c) {
    return {c.r, c.g, c.b, c.a};
}

// ====================== LIFECYCLE ======================

GfxRaylib::GfxRaylib() {}

GfxRaylib::~GfxRaylib() {}

void GfxRaylib::begin_frame(int /*screen_w*/, int /*screen_h*/) {
    BeginDrawing();
}

void GfxRaylib::end_frame() { EndDrawing(); }

// ====================== PRIMITIVES ======================

void GfxRaylib::clear(UIColor color) {
    ClearBackground(to_raylib(color));
}

void GfxRaylib::draw_rect(int x, int y, int w, int h, UIColor color) {
    DrawRectangle(x, y, w, h, to_raylib(color));
}

void GfxRaylib::draw_rect_outline(int x, int y, int w, int h, UIColor color) {
    DrawRectangleLines(x, y, w, h, to_raylib(color));
}

void GfxRaylib::draw_line(int x1, int y1, int x2, int y2, UIColor color) {
    DrawLine(x1, y1, x2, y2, to_raylib(color));
}

void GfxRaylib::draw_circle(int cx, int cy, int radius, UIColor color) {
    DrawCircle(cx, cy, radius, to_raylib(color));
}

void GfxRaylib::draw_text(int x, int y, const char *text, int size,
                          UIColor color) {
    DrawText(text, x, y, size, to_raylib(color));
}

int GfxRaylib::text_width(const char *text, int size) {
    return MeasureText(text, size);
}

// ====================== TEXTURE ======================

Gfx::Tex GfxRaylib::load_texture(const char *path) {
    Texture2D *tex = (Texture2D *)malloc(sizeof(Texture2D));
    *tex = LoadTexture(path);
    return static_cast<Tex>(tex);
}

void GfxRaylib::free_texture(Tex tex) {
    if (tex) {
        Texture2D *t = static_cast<Texture2D *>(tex);
        UnloadTexture(*t);
        free(t);
    }
}

void GfxRaylib::draw_texture(Tex tex, int x, int y, int w, int h) {
    if (!tex)
        return;
    Texture2D *t = static_cast<Texture2D *>(tex);
    DrawTexturePro(*t,
                   (Rectangle){0, 0, (float)t->width, (float)t->height},
                   (Rectangle){(float)x, (float)y, (float)w, (float)h},
                   (Vector2){0, 0}, 0, WHITE);
}
