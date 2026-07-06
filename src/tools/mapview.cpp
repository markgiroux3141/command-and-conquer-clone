// mapview — render a Red Alert map: terrain, overlays, terrain objects, and
// the pre-placed units/structures from the scenario.
//
//   mapview <map.ini> <data-root> [--dump out.bmp] [--scale N] [--full]
//
// <data-root> is a game data root like data/assets/red_alert/allied; theater
// art is read from <root>/MAIN/<theater>/, unit/structure art from
// <root>/MAIN/conquer/, palettes from <root>/INSTALL/REDALERT/local/.
//
// Interactive mode: WASD/arrow/mouse-edge scrolling, left-click or drag-box to
// select units (brackets + health bar), Esc quits. --dump renders headlessly.

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/palette.h"
#include "formats/shp.h"
#include "formats/shpd2.h"
#include "formats/tmp.h"
#include "game/house.h"
#include "game/map.h"
#include "game/render.h"
#include "game/template_table.h"

namespace {

constexpr int kTile = 24;

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

// Caches theater art (TMP templates + theater/conquer SHPs).
class ArtCache {
public:
    ArtCache(std::string theaterDir, std::string conquerDir, std::string hiresDir,
             std::string ext)
        : theaterDir_(std::move(theaterDir)), conquerDir_(std::move(conquerDir)),
          hiresDir_(std::move(hiresDir)), ext_(std::move(ext)) {}

    const fmt::TmpFile* tmp(const std::string& name) {
        auto it = tmps_.find(name);
        if (it == tmps_.end()) {
            std::optional<fmt::TmpFile> v;
            std::string path = theaterDir_ + "/" + name + ext_;
            if (std::filesystem::exists(path))
                v = fmt::TmpFile::load(path);
            it = tmps_.emplace(name, std::move(v)).first;
        }
        return it->second ? &*it->second : nullptr;
    }

    // SHP art: theater-extension file (overlays, trees), conquer .shp
    // (vehicles, structures, walls, cursors) or hires .shp (infantry).
    const fmt::ShpFile* shp(const std::string& name) {
        auto it = shps_.find(name);
        if (it == shps_.end()) {
            std::optional<fmt::ShpFile> v;
            std::string path = theaterDir_ + "/" + name + ext_;
            if (!std::filesystem::exists(path))
                path = conquerDir_ + "/" + name + ".shp";
            if (!std::filesystem::exists(path))
                path = hiresDir_ + "/" + name + ".shp";
            if (std::filesystem::exists(path)) {
                try {
                    v = fmt::ShpFile::load(path);
                } catch (const std::exception&) {
                    // wrong format; leave missing
                }
            }
            it = shps_.emplace(name, std::move(v)).first;
        }
        return it->second ? &*it->second : nullptr;
    }

private:
    std::string theaterDir_, conquerDir_, hiresDir_, ext_;
    std::unordered_map<std::string, std::optional<fmt::TmpFile>> tmps_;
    std::unordered_map<std::string, std::optional<fmt::ShpFile>> shps_;
};

// A placed object resolved to art + draw position (world pixels).
struct DrawObject {
    const fmt::ShpFile* shp = nullptr;
    int frame = 0;
    int turretFrame = -1; // -1 = no turret layer
    int x = 0, y = 0;     // top-left, world pixels
    const game::RemapTable* remap = nullptr;
    bool selectable = false;
    int health = 256; // 0-256
    bool selected = false;
};

// Vehicles whose SHP packs 32 turret frames after the 32 hull facings.
bool hasTurret(const std::string& type) {
    static const char* kTurreted[] = {"1tnk", "2tnk", "3tnk", "4tnk", "jeep"};
    for (const char* t : kTurreted)
        if (type == t)
            return true;
    return false;
}

void drawObject(game::Canvas& c, const DrawObject& o, const fmt::Palette& pal,
                int offX, int offY) {
    game::BlitOptions opts;
    opts.colorKey = true;
    opts.shadow = true;
    opts.remap = o.remap;
    const auto& frame = o.shp->frames[o.frame];
    blitIndexed(c, frame.data(), o.shp->width, o.shp->height, o.x - offX, o.y - offY,
                pal, opts);
    if (o.turretFrame >= 0 && o.turretFrame < int(o.shp->frames.size()))
        blitIndexed(c, o.shp->frames[o.turretFrame].data(), o.shp->width, o.shp->height,
                    o.x - offX, o.y - offY, pal, opts);

    if (o.selected) {
        int x = o.x - offX, y = o.y - offY, w = o.shp->width, h = o.shp->height;
        // Corner brackets.
        const uint32_t kWhite = 0xffffffff;
        int len = std::max(3, w / 5);
        game::fillRect(c, x, y, len, 1, kWhite);
        game::fillRect(c, x, y, 1, len, kWhite);
        game::fillRect(c, x + w - len, y, len, 1, kWhite);
        game::fillRect(c, x + w - 1, y, 1, len, kWhite);
        game::fillRect(c, x, y + h - 1, len, 1, kWhite);
        game::fillRect(c, x, y + h - len, 1, len, kWhite);
        game::fillRect(c, x + w - len, y + h - 1, len, 1, kWhite);
        game::fillRect(c, x + w - 1, y + h - len, 1, len, kWhite);
        // Health bar just above the sprite.
        int frac = std::clamp(o.health, 0, 256);
        uint32_t color = frac > 170 ? 0xff00c000 : frac > 85 ? 0xffe0e000 : 0xffc00000;
        game::drawRect(c, x, y - 5, w, 4, 0xff000000);
        game::fillRect(c, x + 1, y - 4, (w - 2) * frac / 256, 2, color);
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

        std::string localDir = root + "/INSTALL/REDALERT/local";
        auto pal = fmt::Palette::load(localDir + "/" + map.theaterPalette() + ".pal");
        auto remaps = game::buildRemaps(localDir + "/palette.cps");
        ArtCache art(root + "/MAIN/" + theaterDirName(map), root + "/MAIN/conquer",
                     root + "/INSTALL/REDALERT/hires", map.theaterExt());

        std::printf("%s: theater %s, bounds %d,%d %dx%d, %zu units, %zu infantry, "
                    "%zu structures\n",
                    argv[1], map.theater.c_str(), map.x, map.y, map.width, map.height,
                    map.units.size(), map.infantry.size(), map.structures.size());

        int cx0 = full ? 0 : map.x;
        int cy0 = full ? 0 : map.y;
        int cw = full ? game::MapFile::kSize : map.width;
        int ch = full ? game::MapFile::kSize : map.height;

        SDL_Surface* mapSurf = SDL_CreateRGBSurfaceWithFormat(0, cw * kTile, ch * kTile,
                                                              32, SDL_PIXELFORMAT_ARGB8888);
        if (!mapSurf)
            throw std::runtime_error(SDL_GetError());
        game::Canvas mc = game::Canvas::wrap(mapSurf);

        const fmt::TmpFile* clear = art.tmp("clear1");
        if (!clear)
            throw std::runtime_error("missing clear1 template");

        // ---- Terrain layer ----
        int missingTemplates = 0;
        for (int cy = 0; cy < ch; cy++) {
            for (int cx = 0; cx < cw; cx++) {
                int mapX = cx0 + cx, mapY = cy0 + cy;
                const auto& cell = map.cells[mapY * game::MapFile::kSize + mapX];
                int dx = cx * kTile, dy = cy * kTile;

                const fmt::TmpFile* t = nullptr;
                int icon = 0;
                if (cell.templateId != 0xffff && cell.templateId < game::kTemplateCount) {
                    t = art.tmp(game::kTemplateTable[cell.templateId].name);
                    icon = cell.icon;
                    if (!t) {
                        missingTemplates++;
                        game::fillRect(mc, dx, dy, kTile, kTile, 0xffff00ff);
                        continue;
                    }
                }
                if (t && (icon >= int(t->tiles.size()) || t->tiles[icon].empty()))
                    t = nullptr; // empty slot: falls back to clear
                if (!t) {
                    t = clear;
                    icon = (mapX & 3) | ((mapY & 3) << 2); // Clear_Icon()
                }
                blitIndexed(mc, t->tiles[icon].data(), t->tileWidth, t->tileHeight, dx, dy,
                            pal);
            }
        }
        if (missingTemplates)
            std::printf("note: %d cells reference missing template art (magenta)\n",
                        missingTemplates);

        // ---- Overlay layer ----
        // Frame selection per CELL.CPP: ore/gems by adjacent-resource count
        // (Tiberium_Adjust), walls by N/E/S/W same-wall bitmask (Wall_Update).
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
                frame = std::min(frame, int(s->frames.size()) - 1);
                game::BlitOptions opts;
                opts.colorKey = true;
                opts.shadow = true;
                blitIndexed(mc, s->frames[frame].data(), s->width, s->height, cx * kTile,
                            cy * kTile, pal, opts);
            }
        }

        // ---- Terrain objects (trees, mines) ----
        for (const auto& obj : map.terrain) {
            const fmt::ShpFile* s = art.shp(obj.name);
            if (!s || s->frames.empty())
                continue;
            game::BlitOptions opts;
            opts.colorKey = true;
            opts.shadow = true;
            blitIndexed(mc, s->frames[0].data(), s->width, s->height,
                        (obj.cell % game::MapFile::kSize - cx0) * kTile,
                        (obj.cell / game::MapFile::kSize - cy0) * kTile, pal, opts);
        }

        // ---- Resolve scenario objects to draw list ----
        std::vector<DrawObject> objects;
        auto cellPx = [&](int cell) {
            return std::pair<int, int>{(cell % game::MapFile::kSize - cx0) * kTile,
                                       (cell / game::MapFile::kSize - cy0) * kTile};
        };
        for (const auto& s : map.structures) {
            const fmt::ShpFile* shp = art.shp(s.type);
            if (!shp || shp->frames.empty()) {
                std::printf("note: missing structure art: %s\n", s.type.c_str());
                continue;
            }
            DrawObject o;
            o.shp = shp;
            o.frame = 0;
            auto [px, py] = cellPx(s.cell);
            o.x = px;
            o.y = py;
            o.remap = &remaps[size_t(game::houseColor(s.house))];
            o.selectable = true;
            o.health = s.health;
            objects.push_back(o);
            // Two-part buildings (war factory): the roof lives in a second
            // SHP drawn over the base.
            if (const fmt::ShpFile* top = art.shp(s.type + "2")) {
                DrawObject o2 = o;
                o2.shp = top;
                o2.selectable = false;
                objects.push_back(o2);
            }
        }
        for (const auto& u : map.units) {
            const fmt::ShpFile* shp = art.shp(u.type);
            if (!shp || shp->frames.empty()) {
                std::printf("note: missing unit art: %s\n", u.type.c_str());
                continue;
            }
            DrawObject o;
            o.shp = shp;
            o.frame = game::facingToFrame(u.facing) % int(shp->frames.size());
            if (hasTurret(u.type) && shp->frames.size() >= 64)
                o.turretFrame = 32 + game::facingToFrame(u.facing);
            auto [px, py] = cellPx(u.cell);
            o.x = px + kTile / 2 - shp->width / 2; // centered in cell
            o.y = py + kTile / 2 - shp->height / 2;
            o.remap = &remaps[size_t(game::houseColor(u.house))];
            o.selectable = true;
            o.health = u.health;
            objects.push_back(o);
        }
        for (const auto& inf : map.infantry) {
            const fmt::ShpFile* shp = art.shp(inf.type);
            // RA ships no art for civilians c3-c10; the original draws them
            // with C1's shapes and a per-type color remap.
            if ((!shp || shp->frames.empty()) && inf.type[0] == 'c' &&
                std::isdigit((unsigned char)inf.type[1]))
                shp = art.shp("c1");
            if (!shp || shp->frames.empty()) {
                std::printf("note: missing infantry art: %s\n", inf.type.c_str());
                continue;
            }
            // Sub-cell spots: center, UL, UR, LL, LR.
            static const int kSubX[5] = {12, 6, 18, 6, 18};
            static const int kSubY[5] = {12, 6, 6, 18, 18};
            int sub = std::clamp(inf.subcell, 0, 4);
            DrawObject o;
            o.shp = shp;
            // Standing frames: one per 8 facings at the start of the SHP.
            o.frame = (8 - ((inf.facing & 0xff) >> 5)) & 7;
            auto [px, py] = cellPx(inf.cell);
            o.x = px + kSubX[sub] - shp->width / 2;
            o.y = py + kSubY[sub] - shp->height / 2;
            o.remap = &remaps[size_t(game::houseColor(inf.house))];
            o.selectable = true;
            o.health = inf.health;
            objects.push_back(o);
        }
        // Painter's order: draw top rows first.
        std::stable_sort(objects.begin(), objects.end(),
                         [](const DrawObject& a, const DrawObject& b) { return a.y < b.y; });

        // Debug: dump the first cursor frames to verify the D2 SHP decoder.
        if (const char* cursorDump = strArg(argc, argv, "--dump-cursor")) {
            auto cur = fmt::ShpD2File::load(root + "/INSTALL/REDALERT/hires/mouse.shp");
            int n = std::min<int>(20, int(cur.frames.size()));
            SDL_Surface* cs = SDL_CreateRGBSurfaceWithFormat(0, n * 48, 48, 32,
                                                             SDL_PIXELFORMAT_ARGB8888);
            game::Canvas cc = game::Canvas::wrap(cs);
            game::fillRect(cc, 0, 0, cs->w, cs->h, 0xff604060);
            game::BlitOptions copts;
            copts.colorKey = true;
            for (int i = 0; i < n; i++)
                blitIndexed(cc, cur.frames[i].pixels.data(), cur.frames[i].width,
                            cur.frames[i].height, i * 48, 0, pal, copts);
            if (SDL_SaveBMP(cs, cursorDump) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s (%zu cursor frames total)\n", cursorDump,
                        cur.frames.size());
            return 0;
        }

        // ---- Headless dump: bake objects into the map surface ----
        if (dumpPath) {
            if (flagArg(argc, argv, "--select")) // debug: show selection UI
                for (auto& o : objects)
                    o.selected = o.selectable;
            for (const auto& o : objects)
                drawObject(mc, o, pal, 0, 0);

            SDL_Surface* outSurf = mapSurf;
            if (scale > 1) {
                outSurf = SDL_CreateRGBSurfaceWithFormat(0, mapSurf->w * scale,
                                                         mapSurf->h * scale, 32,
                                                         SDL_PIXELFORMAT_ARGB8888);
                if (!outSurf)
                    throw std::runtime_error(SDL_GetError());
                SDL_BlitScaled(mapSurf, nullptr, outSurf, nullptr);
            }
            if (SDL_SaveBMP(outSurf, dumpPath) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s (%dx%d)\n", dumpPath, outSurf->w, outSurf->h);
            return 0;
        }

        // ---- Interactive mode ----
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());
        int winW = std::min(mapSurf->w, 1280), winH = std::min(mapSurf->h, 800);
        SDL_Window* win = SDL_CreateWindow("mapview", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, winW, winH, 0);
        if (!win)
            throw std::runtime_error(SDL_GetError());
        SDL_ShowCursor(SDL_DISABLE);
        // mouse.shp is Dune II-format SHP with per-frame sizes; frame 0 is
        // the standard arrow.
        std::optional<fmt::ShpD2File> cursor;
        try {
            cursor = fmt::ShpD2File::load(root + "/INSTALL/REDALERT/hires/mouse.shp");
        } catch (const std::exception& ex) {
            std::printf("note: no cursor art (%s), using OS cursor\n", ex.what());
            SDL_ShowCursor(SDL_ENABLE);
        }

        float camX = 0, camY = 0;
        const float kSpeed = 480.0f;
        const int kEdge = 16;
        bool dragging = false;
        int dragX0 = 0, dragY0 = 0;
        uint32_t last = SDL_GetTicks();
        bool quit = false;
        while (!quit) {
            int mx, my;
            uint32_t mstate = SDL_GetMouseState(&mx, &my);

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT ||
                    (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                    quit = true;
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    dragging = true;
                    dragX0 = e.button.x;
                    dragY0 = e.button.y;
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                    dragging) {
                    dragging = false;
                    int x0 = std::min(dragX0, e.button.x) + int(camX);
                    int y0 = std::min(dragY0, e.button.y) + int(camY);
                    int x1 = std::max(dragX0, e.button.x) + int(camX);
                    int y1 = std::max(dragY0, e.button.y) + int(camY);
                    bool box = (x1 - x0) > 4 || (y1 - y0) > 4;
                    for (auto& o : objects)
                        o.selected = false;
                    if (box) {
                        for (auto& o : objects)
                            if (o.selectable && o.x < x1 && o.x + o.shp->width > x0 &&
                                o.y < y1 && o.y + o.shp->height > y0)
                                o.selected = true;
                    } else {
                        // Topmost object under the click.
                        for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                            if (it->selectable && x1 >= it->x && x1 < it->x + it->shp->width &&
                                y1 >= it->y && y1 < it->y + it->shp->height) {
                                it->selected = true;
                                break;
                            }
                        }
                    }
                }
            }

            uint32_t now = SDL_GetTicks();
            float dt = float(now - last) / 1000.0f;
            last = now;

            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            float dx = 0, dy = 0;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] || mx < kEdge)
                dx -= 1;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] || mx >= winW - kEdge)
                dx += 1;
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] || my < kEdge)
                dy -= 1;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] || my >= winH - kEdge)
                dy += 1;
            camX = std::clamp(camX + dx * kSpeed * dt, 0.0f, float(mapSurf->w - winW));
            camY = std::clamp(camY + dy * kSpeed * dt, 0.0f, float(mapSurf->h - winH));

            SDL_Surface* wsurf = SDL_GetWindowSurface(win);
            SDL_Rect src{int(camX), int(camY), winW, winH};
            SDL_BlitSurface(mapSurf, &src, wsurf, nullptr);

            game::Canvas wc = game::Canvas::wrap(wsurf);
            for (const auto& o : objects)
                drawObject(wc, o, pal, int(camX), int(camY));

            if (dragging && (mstate & SDL_BUTTON_LMASK))
                game::drawRect(wc, std::min(dragX0, mx), std::min(dragY0, my),
                               std::abs(mx - dragX0) + 1, std::abs(my - dragY0) + 1,
                               0xffffffff);

            if (cursor && !cursor->frames.empty()) {
                const auto& cf = cursor->frames[0];
                game::BlitOptions copts;
                copts.colorKey = true;
                blitIndexed(wc, cf.pixels.data(), cf.width, cf.height, mx, my, pal, copts);
            }

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
