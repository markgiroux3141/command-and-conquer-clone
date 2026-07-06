#pragma once
#include <cstdint>

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

// TECHNO.CPP BodyShape: maps facing (0-255, 0 = north, counter-clockwise)
// to the frame index within a 32-facing SHP.
int facingToFrame(int facing);

} // namespace game
