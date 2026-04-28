#pragma once

#include "gfx.h"
#include <string>
#include <unordered_map>

// ====================== SPRITE REGION ======================
// Defines a sub-rect within a sprite sheet texture.

struct SpriteRegion {
    int x, y, w, h;
};

// ====================== SPRITE SHEET ======================
// Loads a .def file + .png texture. Provides named sprite lookups.
// The .def format is a flat text file:
//   texture_name.png
//   { name x y w h 1 1 0 0 }
//   ...

class SpriteSheet {
  public:
    SpriteSheet() = default;

    // Load texture + parse .def. Returns true on success.
    bool load(Gfx &gfx, const char *png_path, const char *def_path);
    void free(Gfx &gfx);

    // Lookup a named sprite. Returns nullptr if not found.
    const SpriteRegion *get(const char *name) const;

    Gfx::Tex texture() const { return tex_; }
    bool is_loaded() const { return tex_ != Gfx::NO_TEX; }

  private:
    Gfx::Tex tex_ = Gfx::NO_TEX;
    std::unordered_map<std::string, SpriteRegion> regions_;
};
