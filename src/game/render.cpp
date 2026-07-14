#include "game/render.h"

#include <SDL.h>

namespace game {

Canvas Canvas::wrap(SDL_Surface* surf) {
    Canvas c;
    c.px = static_cast<uint32_t*>(surf->pixels);
    c.pitch = surf->pitch / 4;
    c.w = surf->w;
    c.h = surf->h;
    return c;
}

void blitIndexed(Canvas& c, const uint8_t* src, int sw, int sh, int dx, int dy,
                 const fmt::Palette& pal, const BlitOptions& opts) {
    for (int y = 0; y < sh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= c.h)
            continue;
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= c.w)
                continue;
            uint8_t idx = src[y * sw + x];
            if (opts.colorKey && idx == 0)
                continue;
            if (opts.shadow && idx == 4) {
                // Approximates the original shadow fading table by halving
                // the destination color.
                uint32_t p = c.px[ty * c.pitch + tx];
                c.px[ty * c.pitch + tx] = 0xff000000 | ((p >> 1) & 0x7f7f7f);
                continue;
            }
            if (opts.remap)
                idx = (*opts.remap)[idx];
            auto col = pal.colors[idx];
            c.px[ty * c.pitch + tx] =
                0xff000000 | (uint32_t(col.r) << 16) | (uint32_t(col.g) << 8) | col.b;
        }
    }
}

void blitIndexedScaled(Canvas& c, const uint8_t* src, int sw, int sh, int dx,
                       int dy, int dw, int dh, const fmt::Palette& pal,
                       const BlitOptions& opts) {
    if (dw <= 0 || dh <= 0)
        return;
    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= c.h)
            continue;
        int sy = y * sh / dh;
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= c.w)
                continue;
            uint8_t idx = src[sy * sw + (x * sw / dw)];
            if (opts.colorKey && idx == 0)
                continue;
            if (opts.remap)
                idx = (*opts.remap)[idx];
            auto col = pal.colors[idx];
            c.px[ty * c.pitch + tx] =
                0xff000000 | (uint32_t(col.r) << 16) | (uint32_t(col.g) << 8) | col.b;
        }
    }
}

void fillRect(Canvas& c, int dx, int dy, int w, int h, uint32_t argb) {
    for (int y = dy; y < dy + h; y++) {
        if (y < 0 || y >= c.h)
            continue;
        for (int x = dx; x < dx + w; x++) {
            if (x < 0 || x >= c.w)
                continue;
            c.px[y * c.pitch + x] = argb;
        }
    }
}

void drawRect(Canvas& c, int dx, int dy, int w, int h, uint32_t argb) {
    fillRect(c, dx, dy, w, 1, argb);
    fillRect(c, dx, dy + h - 1, w, 1, argb);
    fillRect(c, dx, dy, 1, h, argb);
    fillRect(c, dx + w - 1, dy, 1, h, argb);
}

// Scale an ARGB color's RGB channels by num/den (alpha forced opaque).
static inline uint32_t shade(uint32_t argb, int num, int den) {
    uint32_t r = ((argb >> 16) & 0xff) * num / den;
    uint32_t g = ((argb >> 8) & 0xff) * num / den;
    uint32_t b = (argb & 0xff) * num / den;
    return 0xff000000 | (r << 16) | (g << 8) | b;
}

int drawText(Canvas& c, const fmt::FntFile& font, const std::string& text, int x,
             int y, uint32_t argb, int spacing) {
    int cx = x;
    for (unsigned char ch : text) {
        const auto* g = font.glyph(ch);
        if (!g) {
            cx += font.maxWidth + spacing;
            continue;
        }
        for (int gy = 0; gy < g->height; gy++) {
            int ty = y + g->yOffset + gy;
            if (ty < 0 || ty >= c.h)
                continue;
            for (int gx = 0; gx < g->width; gx++) {
                // The glyph is 4-tone (a beveled/shaded letter): 0 transparent,
                // 1 the bright stroke, 2 the mid body, 3 the dark outline.
                // Rendering all three as one solid color fattens the outline
                // into the fill and letters read bloated/smushed — so keep the
                // shading by darkening 2 and 3 relative to the requested color.
                uint8_t v = g->pixels[gy * g->width + gx];
                if (v == 0)
                    continue; // transparent
                int tx = cx + gx;
                if (tx < 0 || tx >= c.w)
                    continue;
                c.px[ty * c.pitch + tx] =
                    v == 1 ? argb : v == 2 ? shade(argb, 2, 3) : shade(argb, 1, 3);
            }
        }
        cx += g->width + spacing;
    }
    return cx;
}

int textWidth(const fmt::FntFile& font, const std::string& text, int spacing) {
    int w = 0;
    for (unsigned char ch : text) {
        const auto* g = font.glyph(ch);
        w += (g ? g->width : font.maxWidth) + spacing;
    }
    return w > 0 ? w - spacing : 0;
}

int facingToFrame(int facing) {
    static const int kBodyShape[32] = {0,  31, 30, 29, 28, 27, 26, 25, 24, 23, 22,
                                       21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11,
                                       10, 9,  8,  7,  6,  5,  4,  3,  2,  1};
    return kBodyShape[(facing & 0xff) >> 3];
}

} // namespace game
