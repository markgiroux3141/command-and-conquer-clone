// mapview — render a Red Alert map (terrain + overlay + terrain objects).
//
//   mapview <map.ini> <data-root> [--dump out.bmp] [--scale N] [--full]
//
// <data-root> is a game data root like data/assets/red_alert/allied; theater
// art is read from <root>/MAIN/<theater>/ and the palette from
// <root>/INSTALL/REDALERT/local/<theater>.pal.
//
// By default only the playable bounds ([Map] X/Y/Width/Height) are rendered;
// --full renders the whole 128x128 grid. Without --dump, opens a window with
// WASD/arrow + mouse edge scrolling.
//
// Missing template art renders magenta; empty template slots fall back to the
// clear tile like the original engine.

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "formats/palette.h"
#include "formats/shp.h"
#include "formats/tmp.h"
#include "game/map.h"
#include "game/template_table.h"

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

bool flagArg(int argc, char** argv, const char* name) {
    for (int i = 3; i < argc; i++)
        if (std::strcmp(argv[i], name) == 0)
            return true;
    return false;
}

std::string theaterDirName(const game::MapFile& map) {
    if (map.theater == "SNOW")
        return "snow";
    if (map.theater == "INTERIOR")
        return "interior";
    return "temperat";
}

// Caches theater art; art names are template/overlay/terrain base names.
class ArtCache {
public:
    ArtCache(std::string theaterDir, std::string ext)
        : dir_(std::move(theaterDir)), ext_(std::move(ext)) {}

    const fmt::TmpFile* tmp(const std::string& name) {
        auto it = tmps_.find(name);
        if (it == tmps_.end()) {
            std::optional<fmt::TmpFile> v;
            std::string path = dir_ + "/" + name + ext_;
            if (std::filesystem::exists(path))
                v = fmt::TmpFile::load(path);
            it = tmps_.emplace(name, std::move(v)).first;
        }
        return it->second ? &*it->second : nullptr;
    }

    // Overlay/terrain art: theater-extension file that is SHP format inside.
    const fmt::ShpFile* shp(const std::string& name) {
        auto it = shps_.find(name);
        if (it == shps_.end()) {
            std::optional<fmt::ShpFile> v;
            std::string path = dir_ + "/" + name + ext_;
            if (!std::filesystem::exists(path)) // walls etc. live in conquer as .shp
                path = dir_ + "/../conquer/" + name + ".shp";
            if (std::filesystem::exists(path)) {
                try {
                    v = fmt::ShpFile::load(path);
                } catch (const std::exception&) {
                    // not SHP after all; leave missing
                }
            }
            it = shps_.emplace(name, std::move(v)).first;
        }
        return it->second ? &*it->second : nullptr;
    }

private:
    std::string dir_, ext_;
    std::unordered_map<std::string, std::optional<fmt::TmpFile>> tmps_;
    std::unordered_map<std::string, std::optional<fmt::ShpFile>> shps_;
};

struct Canvas {
    SDL_Surface* surf = nullptr;
    uint32_t* px = nullptr;
    int pitch = 0; // in pixels
    int w = 0, h = 0;
};

void blitIndexed(Canvas& c, const uint8_t* src, int sw, int sh, int dx, int dy,
                 const fmt::Palette& pal, bool colorKey) {
    for (int y = 0; y < sh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= c.h)
            continue;
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= c.w)
                continue;
            uint8_t idx = src[y * sw + x];
            if (colorKey && idx == 0)
                continue; // SHP transparency
            if (colorKey && idx == 4) {
                // SHP shadow index: darken what's underneath (real shadow
                // tables come with the Phase 3 sprite renderer).
                uint32_t p = c.px[ty * c.pitch + tx];
                c.px[ty * c.pitch + tx] = 0xff000000 | ((p >> 1) & 0x7f7f7f);
                continue;
            }
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: mapview <map.ini> <data-root> [--dump out.bmp] [--scale N] [--full]\n"
                     "  data-root example: data/assets/red_alert/allied\n");
        return 2;
    }
    try {
        auto map = game::MapFile::load(argv[1]);
        std::string root = argv[2];
        const char* dumpPath = strArg(argc, argv, "--dump");
        int scale = intArg(argc, argv, "--scale", 1);
        bool full = flagArg(argc, argv, "--full");

        std::string theaterDir = root + "/MAIN/" + theaterDirName(map);
        std::string palPath =
            root + "/INSTALL/REDALERT/local/" + map.theaterPalette() + ".pal";
        auto pal = fmt::Palette::load(palPath);
        ArtCache art(theaterDir, map.theaterExt());

        std::printf("%s: theater %s, bounds %d,%d %dx%d\n", argv[1], map.theater.c_str(),
                    map.x, map.y, map.width, map.height);

        const int kTile = 24;
        int cx0 = full ? 0 : map.x;
        int cy0 = full ? 0 : map.y;
        int cw = full ? game::MapFile::kSize : map.width;
        int ch = full ? game::MapFile::kSize : map.height;

        Canvas c;
        c.w = cw * kTile;
        c.h = ch * kTile;
        c.surf = SDL_CreateRGBSurfaceWithFormat(0, c.w, c.h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!c.surf)
            throw std::runtime_error(SDL_GetError());
        c.px = static_cast<uint32_t*>(c.surf->pixels);
        c.pitch = c.surf->pitch / 4;

        const fmt::TmpFile* clear = art.tmp("clear1");
        if (!clear)
            throw std::runtime_error("missing clear1 template in " + theaterDir);

        int missingTemplates = 0;
        std::unordered_map<uint16_t, int> missingIds;

        // Terrain layer.
        for (int cy = 0; cy < ch; cy++) {
            for (int cx = 0; cx < cw; cx++) {
                int mapX = cx0 + cx, mapY = cy0 + cy;
                const auto& cell = map.cells[mapY * game::MapFile::kSize + mapX];
                int dx = cx * kTile, dy = cy * kTile;

                // CellClass::Clear_Icon(): pseudo-random variation from position.
                int clearIcon = (mapX & 3) | ((mapY & 3) << 2);

                const fmt::TmpFile* t = nullptr;
                int icon = 0;
                if (cell.templateId != 0xffff && cell.templateId < game::kTemplateCount) {
                    t = art.tmp(game::kTemplateTable[cell.templateId].name);
                    icon = cell.icon;
                    if (!t) {
                        missingTemplates++;
                        missingIds[cell.templateId]++;
                    }
                }
                if (t && (icon >= int(t->tiles.size()) || t->tiles[icon].empty())) {
                    t = nullptr; // empty slot: engine falls back to clear
                }
                if (!t) {
                    if (cell.templateId != 0xffff && cell.templateId < game::kTemplateCount &&
                        !art.tmp(game::kTemplateTable[cell.templateId].name)) {
                        fillRect(c, dx, dy, kTile, kTile, 0xffff00ff); // missing art
                        continue;
                    }
                    t = clear;
                    icon = clearIcon;
                }
                blitIndexed(c, t->tiles[icon].data(), t->tileWidth, t->tileHeight, dx, dy,
                            pal, false);
            }
        }

        // Overlay layer (ore, gems, walls, crates). Frame selection follows
        // CELL.CPP: ore/gems by adjacent-resource count (Tiberium_Adjust),
        // walls by N/E/S/W same-wall bitmask (Wall_Update).
        auto overlayAt = [&](int mx, int my) -> int {
            if (mx < 0 || mx >= game::MapFile::kSize || my < 0 || my >= game::MapFile::kSize)
                return 0xff;
            return map.cells[my * game::MapFile::kSize + mx].overlay;
        };
        auto isResource = [](int o) { return o >= 5 && o <= 12; }; // gold01..gem04
        auto isWall = [](int o) {
            return o == 0 || o == 1 || o == 2 || o == 3 || o == 4 || o == 23 || o == 24;
        };
        for (int cy = 0; cy < ch; cy++) {
            for (int cx = 0; cx < cw; cx++) {
                int mapX = cx0 + cx, mapY = cy0 + cy;
                int o = overlayAt(mapX, mapY);
                if (o == 0xff || o >= 25)
                    continue;
                const fmt::ShpFile* s = art.shp(game::kOverlayNames[o]);
                if (!s || s->frames.empty())
                    continue;

                int frame = 0;
                if (isResource(o)) {
                    static const int kAdjGold[9] = {0, 1, 3, 4, 6, 7, 8, 10, 11};
                    static const int kAdjGem[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
                    int count = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            if ((dx || dy) && isResource(overlayAt(mapX + dx, mapY + dy)))
                                count++;
                    frame = o <= 8 ? kAdjGold[count] : kAdjGem[count];
                } else if (isWall(o)) {
                    if (overlayAt(mapX, mapY - 1) == o)
                        frame |= 1;
                    if (overlayAt(mapX + 1, mapY) == o)
                        frame |= 2;
                    if (overlayAt(mapX, mapY + 1) == o)
                        frame |= 4;
                    if (overlayAt(mapX - 1, mapY) == o)
                        frame |= 8;
                }
                if (frame >= int(s->frames.size()))
                    frame = int(s->frames.size()) - 1;
                blitIndexed(c, s->frames[frame].data(), s->width, s->height, cx * kTile,
                            cy * kTile, pal, true);
            }
        }

        // Terrain objects (trees, mines): SHP anchored at the cell's top-left.
        for (const auto& obj : map.terrain) {
            int mapX = obj.cell % game::MapFile::kSize;
            int mapY = obj.cell / game::MapFile::kSize;
            const fmt::ShpFile* s = art.shp(obj.name);
            if (!s || s->frames.empty())
                continue;
            blitIndexed(c, s->frames[0].data(), s->width, s->height, (mapX - cx0) * kTile,
                        (mapY - cy0) * kTile, pal, true);
        }

        if (missingTemplates) {
            std::printf("note: %d cells reference missing template art (magenta)\n",
                        missingTemplates);
            for (const auto& [id, count] : missingIds)
                std::printf("  id %u (%s): %d cells\n", id,
                            id < game::kTemplateCount ? game::kTemplateTable[id].name : "?",
                            count);
        }

        // Optional integer upscale.
        SDL_Surface* outSurf = c.surf;
        if (scale > 1) {
            outSurf = SDL_CreateRGBSurfaceWithFormat(0, c.w * scale, c.h * scale, 32,
                                                     SDL_PIXELFORMAT_ARGB8888);
            if (!outSurf)
                throw std::runtime_error(SDL_GetError());
            SDL_BlitScaled(c.surf, nullptr, outSurf, nullptr);
        }

        if (dumpPath) {
            if (SDL_SaveBMP(outSurf, dumpPath) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s (%dx%d)\n", dumpPath, outSurf->w, outSurf->h);
            return 0;
        }

        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());
        int winW = std::min(outSurf->w, 1280), winH = std::min(outSurf->h, 800);
        SDL_Window* win = SDL_CreateWindow("mapview", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, winW, winH, 0);
        if (!win)
            throw std::runtime_error(SDL_GetError());

        float camX = 0, camY = 0;
        const float kSpeed = 480.0f; // px/s
        const int kEdge = 16;        // edge-scroll margin
        uint32_t last = SDL_GetTicks();
        bool quit = false;
        while (!quit) {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                if (e.type == SDL_QUIT ||
                    (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                    quit = true;

            uint32_t now = SDL_GetTicks();
            float dt = float(now - last) / 1000.0f;
            last = now;

            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            float dx = 0, dy = 0;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] || mx < kEdge)
                dx -= 1;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] || mx >= winW - kEdge)
                dx += 1;
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] || my < kEdge)
                dy -= 1;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] || my >= winH - kEdge)
                dy += 1;
            camX = std::max(0.0f, std::min(camX + dx * kSpeed * dt, float(outSurf->w - winW)));
            camY = std::max(0.0f, std::min(camY + dy * kSpeed * dt, float(outSurf->h - winH)));

            SDL_Rect src{int(camX), int(camY), winW, winH};
            SDL_BlitSurface(outSurf, &src, SDL_GetWindowSurface(win), nullptr);
            SDL_UpdateWindowSurface(win);
            SDL_Delay(16);
        }
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
