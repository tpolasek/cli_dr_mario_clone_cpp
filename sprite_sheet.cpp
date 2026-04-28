#include "sprite_sheet.h"

#include <cstdio>
#include <cstring>

// ====================== LOAD ======================

bool SpriteSheet::load(Gfx &gfx, const char *png_path, const char *def_path) {
    tex_ = gfx.load_texture(png_path);
    if (tex_ == Gfx::NO_TEX)
        return false;

    FILE *f = std::fopen(def_path, "r");
    if (!f) {
        gfx.free_texture(tex_);
        tex_ = Gfx::NO_TEX;
        return false;
    }

    // First line is the texture filename (already loaded, skip it)
    char line[256];
    if (!std::fgets(line, sizeof(line), f)) {
        std::fclose(f);
        gfx.free_texture(tex_);
        tex_ = Gfx::NO_TEX;
        return false;
    }

    // Parse sprite entries. Format is one field per line:
    //   {
    //     name
    //     x
    //     y
    //     w
    //     h
    //     1
    //     1
    //     0
    //     0
    //   }
    while (std::fgets(line, sizeof(line), f)) {
        // Look for opening brace line
        if (!std::strchr(line, '{'))
            continue;

        // Read the 9 subsequent lines: name, x, y, w, h, + 4 ignored
        char name[128] = {};
        int x = 0, y = 0, w = 0, h = 0;
        int ignored[4] = {};

        if (!std::fgets(name, sizeof(name), f))
            break;
        // Trim trailing whitespace/newline from name
        int len = (int)std::strlen(name);
        while (len > 0 && (name[len - 1] == '\n' || name[len - 1] == '\r' ||
                           name[len - 1] == '\t' || name[len - 1] == ' '))
            name[--len] = '\0';

        // Skip leading whitespace on name
        char *nm = name;
        while (*nm == '\t' || *nm == ' ')
            nm++;

        char val_line[64];
        if (!std::fgets(val_line, sizeof(val_line), f))
            break;
        x = std::atoi(val_line);

        if (!std::fgets(val_line, sizeof(val_line), f))
            break;
        y = std::atoi(val_line);

        if (!std::fgets(val_line, sizeof(val_line), f))
            break;
        w = std::atoi(val_line);

        if (!std::fgets(val_line, sizeof(val_line), f))
            break;
        h = std::atoi(val_line);

        // Read 4 more ignored fields + closing brace
        for (int i = 0; i < 5; i++)
            if (!std::fgets(val_line, sizeof(val_line), f))
                break;

        if (w <= 0 || h <= 0 || nm[0] == '\0')
            continue;

        SpriteRegion region;
        region.x = x;
        region.y = y;
        region.w = w;
        region.h = h;
        regions_[std::string(nm)] = region;
    }

    std::fclose(f);
    return !regions_.empty();
}

// ====================== FREE ======================

void SpriteSheet::free(Gfx &gfx) {
    if (tex_ != Gfx::NO_TEX) {
        gfx.free_texture(tex_);
        tex_ = Gfx::NO_TEX;
    }
    regions_.clear();
}

// ====================== GET ======================

const SpriteRegion *SpriteSheet::get(const char *name) const {
    auto it = regions_.find(std::string(name));
    if (it == regions_.end())
        return nullptr;
    return &it->second;
}
