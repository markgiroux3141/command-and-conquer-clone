#pragma once
#include <cstdint>

#include <string>

#include "formats/fnt.h"
#include "formats/palette.h"
#include "game/house.h"

struct SDL_Surface;

namespace game {

// A 32-bit ARGB target wrapping an SDL surface's pixels.
struct Canvas {
    uint32_t* px = nullptr;
    int pitch = 0; // in pixels, not bytes
    int w = 0, h = 0;

    static Canvas wrap(SDL_Surface* surf);
};

struct BlitOptions {
    bool colorKey = false;            // index 0 transparent (SHP sprites)
    bool shadow = false;              // index 4 darkens the destination
    const RemapTable* remap = nullptr; // house-color remap, applied pre-palette
};

// Blit a w*h block of 8-bit palette indices to (dx,dy), clipped.
void blitIndexed(Canvas& c, const uint8_t* src, int sw, int sh, int dx, int dy,
                 const fmt::Palette& pal, const BlitOptions& opts = {});

void fillRect(Canvas& c, int dx, int dy, int w, int h, uint32_t argb);
void drawRect(Canvas& c, int dx, int dy, int w, int h, uint32_t argb); // 1px outline

// Draw a string with a FNT font at (x,y). Every non-transparent glyph pixel
// becomes `argb` (the game fonts are effectively 1-bit); `spacing` extra pixels
// are inserted between glyphs (FontXSpacing). Returns the x past the last glyph.
int drawText(Canvas& c, const fmt::FntFile& font, const std::string& text, int x,
             int y, uint32_t argb, int spacing = 1);

// Pixel width the string would occupy with drawText (same spacing rule).
int textWidth(const fmt::FntFile& font, const std::string& text, int spacing = 1);

// TECHNO.CPP BodyShape: maps facing (0-255, 0 = north, counter-clockwise)
// to the frame index within a 32-facing SHP.
int facingToFrame(int facing);

} // namespace game
