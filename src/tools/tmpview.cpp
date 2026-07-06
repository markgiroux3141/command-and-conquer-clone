// tmpview — render all tiles of an RA terrain template in a grid.
//   tmpview <file.tem> <file.pal> [--scale N] [--cols N] [--dump out.bmp]
// Empty template slots render as magenta.

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "formats/palette.h"
#include "formats/tmp.h"

namespace {

int intArg(int argc, char** argv, const char* name, int fallback) {
    for (int i = 3; i < argc - 1; i++)
        if (std::strcmp(argv[i], name) == 0)
            return std::atoi(argv[i + 1]);
    return fallback;
}

const char* strArg(int argc, char** argv, const char* name) {
    for (int i = 3; i < argc - 1; i++)
        if (std::strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tmpview <file.tem> <file.pal> [--scale N] [--cols N] [--dump out.bmp]\n");
        return 2;
    }
    try {
        auto tmp = fmt::TmpFile::load(argv[1]);
        auto pal = fmt::Palette::load(argv[2]);
        int scale = intArg(argc, argv, "--scale", 3);
        int cols = intArg(argc, argv, "--cols", 0);
        const char* dumpPath = strArg(argc, argv, "--dump");

        int n = int(tmp.tiles.size());
        std::printf("%s: %d tiles of %dx%d\n", argv[1], n, tmp.tileWidth, tmp.tileHeight);
        if (cols <= 0)
            cols = n; // default: single row, shows template layout runs left-to-right
        int rows = (n + cols - 1) / cols;

        int tw = tmp.tileWidth * scale, th = tmp.tileHeight * scale;
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, cols * tw, rows * th, 32,
                                                           SDL_PIXELFORMAT_ARGB8888);
        if (!surf)
            throw std::runtime_error(SDL_GetError());
        auto* out = static_cast<uint32_t*>(surf->pixels);
        int pitch = surf->pitch / 4;

        for (int t = 0; t < n; t++) {
            int ox = (t % cols) * tw, oy = (t / cols) * th;
            const auto& px = tmp.tiles[t];
            for (int y = 0; y < th; y++) {
                for (int x = 0; x < tw; x++) {
                    uint32_t argb = 0xffff00ff; // magenta = empty slot
                    if (!px.empty()) {
                        auto c = pal.colors[px[(y / scale) * tmp.tileWidth + (x / scale)]];
                        argb = 0xff000000 | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
                    }
                    out[(oy + y) * pitch + ox + x] = argb;
                }
            }
        }

        if (dumpPath) {
            if (SDL_SaveBMP(surf, dumpPath) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s\n", dumpPath);
            SDL_FreeSurface(surf);
            return 0;
        }

        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());
        SDL_Window* win = SDL_CreateWindow("tmpview", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, surf->w, surf->h, 0);
        if (!win)
            throw std::runtime_error(SDL_GetError());
        bool quit = false;
        while (!quit) {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                if (e.type == SDL_QUIT ||
                    (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                    quit = true;
            SDL_BlitSurface(surf, nullptr, SDL_GetWindowSurface(win), nullptr);
            SDL_UpdateWindowSurface(win);
            SDL_Delay(30);
        }
        SDL_FreeSurface(surf);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
