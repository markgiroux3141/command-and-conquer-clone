// shpview — render a Westwood SHP sprite with a PAL palette.
//
//   shpview <file.shp> <file.pal> [--scale N] [--fps N]
//   shpview <file.shp> <file.pal> --dump out.bmp [--frame N] [--scale N]
//
// Interactive: left/right step frames, space pauses, +/- zoom, Esc quits.
// --dump writes a single frame as BMP and exits (no window), which also gives
// us a headless way to verify the decoder.

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "formats/palette.h"
#include "formats/shp.h"

namespace {

SDL_Surface* renderFrame(const fmt::ShpFile& shp, const fmt::Palette& pal, int frame, int scale) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
        0, shp.width * scale, shp.height * scale, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf)
        throw std::runtime_error(SDL_GetError());

    const auto& pixels = shp.frames[frame];
    auto* out = static_cast<uint32_t*>(surf->pixels);
    int pitch = surf->pitch / 4;
    for (int y = 0; y < shp.height * scale; y++) {
        for (int x = 0; x < shp.width * scale; x++) {
            uint8_t idx = pixels[(y / scale) * shp.width + (x / scale)];
            uint32_t argb;
            if (idx == 0) {
                // transparent: checkerboard so sprite edges are obvious
                bool dark = ((x / 8) + (y / 8)) % 2 == 0;
                argb = dark ? 0xff303030 : 0xff3a3a3a;
            } else {
                auto c = pal.colors[idx];
                argb = 0xff000000 | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
            }
            out[y * pitch + x] = argb;
        }
    }
    return surf;
}

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
                     "usage: shpview <file.shp> <file.pal> [--scale N] [--fps N]\n"
                     "       shpview <file.shp> <file.pal> --dump out.bmp [--frame N] [--scale N]\n");
        return 2;
    }

    try {
        auto shp = fmt::ShpFile::load(argv[1]);
        auto pal = fmt::Palette::load(argv[2]);
        int scale = intArg(argc, argv, "--scale", 4);
        int fps = intArg(argc, argv, "--fps", 8);
        const char* dumpPath = strArg(argc, argv, "--dump");

        std::printf("%s: %zu frames, %dx%d\n", argv[1], shp.frames.size(), shp.width, shp.height);

        if (dumpPath) {
            int frame = intArg(argc, argv, "--frame", 0);
            if (frame < 0 || frame >= int(shp.frames.size()))
                throw std::runtime_error("--frame out of range");
            SDL_Surface* surf = renderFrame(shp, pal, frame, scale);
            if (SDL_SaveBMP(surf, dumpPath) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote frame %d to %s\n", frame, dumpPath);
            SDL_FreeSurface(surf);
            return 0;
        }

        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());

        SDL_Window* win = SDL_CreateWindow("shpview", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, shp.width * scale,
                                           shp.height * scale, 0);
        if (!win)
            throw std::runtime_error(SDL_GetError());

        int frame = 0;
        bool playing = true;
        bool quit = false;
        uint32_t lastStep = SDL_GetTicks();

        while (!quit) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT)
                    quit = true;
                else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: quit = true; break;
                    case SDLK_SPACE: playing = !playing; break;
                    case SDLK_RIGHT: frame = (frame + 1) % int(shp.frames.size()); playing = false; break;
                    case SDLK_LEFT:
                        frame = (frame + int(shp.frames.size()) - 1) % int(shp.frames.size());
                        playing = false;
                        break;
                    case SDLK_EQUALS:
                    case SDLK_PLUS:
                        scale = SDL_min(scale + 1, 16);
                        SDL_SetWindowSize(win, shp.width * scale, shp.height * scale);
                        break;
                    case SDLK_MINUS:
                        scale = SDL_max(scale - 1, 1);
                        SDL_SetWindowSize(win, shp.width * scale, shp.height * scale);
                        break;
                    }
                }
            }

            if (playing && SDL_GetTicks() - lastStep >= 1000u / fps) {
                frame = (frame + 1) % int(shp.frames.size());
                lastStep = SDL_GetTicks();
            }

            SDL_Surface* frameSurf = renderFrame(shp, pal, frame, scale);
            SDL_Surface* winSurf = SDL_GetWindowSurface(win);
            SDL_BlitSurface(frameSurf, nullptr, winSurf, nullptr);
            SDL_FreeSurface(frameSurf);
            SDL_UpdateWindowSurface(win);

            char title[256];
            std::snprintf(title, sizeof(title), "shpview — %s  frame %d/%zu  %dx",
                          argv[1], frame + 1, shp.frames.size(), scale);
            SDL_SetWindowTitle(win, title);
            SDL_Delay(10);
        }

        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
