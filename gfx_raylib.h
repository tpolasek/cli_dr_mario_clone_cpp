#pragma once

#include "gfx.h"

#include <raylib.h>

// Undef raylib color macros that conflict with our game color constants
#undef RED
#undef YELLOW
#undef BLUE

class GfxRaylib : public Gfx {
  public:
    GfxRaylib();
    ~GfxRaylib();

    void begin_frame(int screen_w, int screen_h) override;
    void end_frame() override;

    void clear(UIColor color) override;
    void draw_rect(int x, int y, int w, int h, UIColor color) override;
    void draw_rect_outline(int x, int y, int w, int h, UIColor color) override;
    void draw_line(int x1, int y1, int x2, int y2, UIColor color) override;
    void draw_circle(int cx, int cy, int radius, UIColor color) override;
    void draw_text(int x, int y, const char *text, int size,
                   UIColor color) override;
    int text_width(const char *text, int size) override;

    Tex load_texture(const char *path) override;
    void free_texture(Tex tex) override;
    void draw_texture(Tex tex, int x, int y, int w, int h) override;

  private:
    static Color to_raylib(UIColor c);
};
