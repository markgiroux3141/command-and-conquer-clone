// game — the playable shell (Phase 4): mapview's renderer plus a fixed-tick
// simulation (15 ticks/s like RA), rules.ini unit stats, and right-click move
// orders with A* pathfinding.
//
//   game <map.ini> <data-root> [--scale N] [--house H] [--no-shroud]
//        [--sim-ticks N] [--move i,cx,cy]... [--dump out.bmp]
//
// Interactive: WASD/arrows/mouse-edge scroll, left-click/drag select,
// right-click order selected units to move, Esc quits.
// Headless (--sim-ticks): applies --move orders (unit index, absolute cell
// coords on the 128x128 grid), advances the sim N ticks, prints unit cells
// before/after and optionally dumps the world to BMP.

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/fnt.h"
#include "formats/palette.h"
#include "formats/shp.h"
#include "formats/shpd2.h"
#include "formats/tmp.h"
#include "game/audio.h"
#include "game/house.h"
#include "game/map.h"
#include "game/render.h"
#include "game/rules.h"
#include "game/sim.h"
#include "game/td_template_table.h"
#include "game/template_table.h"

namespace {

constexpr int kTile = 24;
constexpr double kTickMs = 1000.0 / 15.0; // RA game speed: 15 ticks/s
constexpr int kCameoW = 64, kCameoH = 48; // sidebar icon SHP size
constexpr int kSidebarW = kCameoW * 2 + 12;
constexpr int kTopBar = 14;                  // OPTIONS | credits | SIDEBAR tab bar
constexpr int kRadarH = kSidebarW - 12;      // radar/logo box (near-square)
constexpr int kBtnH = 13;                    // REPAIR / SELL / MAP button row
constexpr int kSideTop = kTopBar + kRadarH + kBtnH + 6; // first cameo row y

// Westwood-style HUD palette (metallic gray with beige accents).
constexpr uint32_t kFace = 0xff54544c;  // panel face
constexpr uint32_t kLight = 0xff8c8c80; // raised highlight (top/left)
constexpr uint32_t kDark = 0xff23231e;  // raised shadow (bottom/right)
constexpr uint32_t kText = 0xffe8e8d0;  // off-white label text
constexpr uint32_t kGreen = 0xff38f838; // credits / radar readout green

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
    if (map.game == game::Game::TiberianDawn) {
        if (map.theater == "DESERT")
            return "DESERT";
        if (map.theater == "WINTER")
            return "WINTER";
        return "TEMPERAT";
    }
    if (map.theater == "SNOW")
        return "snow";
    if (map.theater == "INTERIOR")
        return "interior";
    return "temperat";
}

// Caches theater art (TMP templates + theater/conquer/hires SHPs).
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

// One drawable sprite this frame (structure or resolved sim unit).
struct DrawObject {
    const fmt::ShpFile* shp = nullptr;
    int frame = 0;
    int turretFrame = -1;
    int x = 0, y = 0; // top-left, world pixels
    const game::RemapTable* remap = nullptr;
    bool selectable = false;
    int health = 256;
    bool selected = false;
    int unitId = -1;   // sim unit id, -1 for structures
    int structId = -1; // sim structure id, -1 for units
};

// A playing effect animation (explosion, impact) in world pixels (center).
struct Anim {
    const fmt::ShpFile* shp = nullptr;
    int x = 0, y = 0;
    int frame = 0;
};

// Combat_Anim (COMBAT.CPP): warhead ExplosionSet + damage -> effect art.
std::string combatAnimName(int damage, const game::WarheadStats* wh, bool water) {
    static const char* kAp[] = {"veh-hit3", "veh-hit2", "frag1", "fball1"};
    static const char* kHe[] = {"veh-hit1", "veh-hit2", "art-exp1", "fball1"};
    static const char* kFire[] = {"napalm1", "napalm2", "napalm3"};
    static const char* kWater[] = {"h2o_exp3", "h2o_exp2", "h2o_exp1"};
    if (damage <= 0 || !wh)
        return "";
    auto pick = [&](const char* const* list, int n, int cap) {
        return list[(n - 1) * std::min(damage, cap) / cap];
    };
    switch (wh->explosion) {
    case 1: return "piff";
    case 2: return damage > 15 ? "piffpiff" : "piff";
    case 3: return water ? pick(kWater, 3, 150) : pick(kFire, 3, 150);
    case 4: return water ? pick(kWater, 3, 90) : pick(kAp, 4, 90);
    case 5: return water ? pick(kWater, 3, 130) : pick(kHe, 4, 130);
    case 6: return "atomsfx";
    default: return "";
    }
}

// One clickable sidebar cameo.
struct BuildEntry {
    std::string type;
    game::UnitKind kind;
    const fmt::ShpFile* icon = nullptr;
};

// Candidate build lists (the original defines type lists in code too).
const char* kStructTypes[] = {"powr", "apwr", "proc", "silo", "barr", "tent",
                              "weap", "dome", "fix",  "gun",  "agun", "pbox",
                              "hbox", "tsla", "ftur", "gap",  "atek", "stek",
                              "hpad", "afld", "kenn"};
const char* kInfTypes[] = {"e1", "e2", "e3", "e4", "e6", "dog", "medi",
                           "spy", "thf"};
const char* kVehTypes[] = {"harv", "mcv",  "jeep", "apc",  "1tnk", "2tnk",
                           "3tnk", "4tnk", "arty", "v2rl", "ftrk", "dtrk",
                           "mnly", "mgg",  "mrj",  "ttnk"};

// Tiberian Dawn build lists (type codes from the TD roster).
const char* kTdStructTypes[] = {"nuke", "nuk2", "proc", "silo", "pyle", "hand",
                                "weap", "hq",   "fix",  "hpad", "afld", "gun",
                                "gtwr", "atwr", "obli", "sam",  "tmpl"};
const char* kTdInfTypes[] = {"e1", "e2", "e3", "e4", "e5", "rmbo"};
const char* kTdVehTypes[] = {"mcv",  "htnk", "mtnk", "ltnk", "ftnk", "stnk",
                             "bggy", "jeep", "bike", "apc",  "arty", "msam",
                             "harv"};

bool isSovietHouse(const std::string& house) {
    return house == "USSR" || house == "Ukraine" || house == "BadGuy";
}

bool isShipType(const std::string& type) {
    static const std::set<std::string> kShips = {"ss", "dd", "ca", "pt", "lst",
                                                 "msub", "carr"};
    return kShips.count(type) > 0;
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
        int frac = std::clamp(o.health, 0, 256);
        uint32_t color = frac > 170 ? 0xff00c000 : frac > 85 ? 0xffe0e000 : 0xffc00000;
        game::drawRect(c, x, y - 5, w, 4, 0xff000000);
        game::fillRect(c, x + 1, y - 4, (w - 2) * frac / 256, 2, color);
    }
}

// A raised 3D panel: face fill with a light top/left and dark bottom/right edge
// (the Westwood beveled-metal look). `sunken` swaps the edges for a recess.
void bevelPanel(game::Canvas& c, int x, int y, int w, int h, uint32_t face,
                bool sunken = false) {
    uint32_t hi = sunken ? kDark : kLight, lo = sunken ? kLight : kDark;
    game::fillRect(c, x, y, w, h, face);
    game::fillRect(c, x, y, w, 1, hi);
    game::fillRect(c, x, y, 1, h, hi);
    game::fillRect(c, x, y + h - 1, w, 1, lo);
    game::fillRect(c, x + w - 1, y, 1, h, lo);
}

// Draw `text` centered horizontally in [x, x+w) at row y with font (if loaded).
void drawTextCentered(game::Canvas& c, const std::optional<fmt::FntFile>& font,
                      const std::string& text, int x, int w, int y, uint32_t argb,
                      int spacing = 1) {
    if (!font)
        return;
    int tw = game::textWidth(*font, text, spacing);
    game::drawText(c, *font, text, x + (w - tw) / 2, y, argb, spacing);
}

// Right-align `text` so it ends at xRight.
void drawTextRight(game::Canvas& c, const std::optional<fmt::FntFile>& font,
                   const std::string& text, int xRight, int y, uint32_t argb) {
    if (!font)
        return;
    game::drawText(c, *font, text, xRight - game::textWidth(*font, text), y, argb);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: game <map.ini> <data-root> [--scale N] [--house H] [--no-shroud]\n"
                     "            [--credits N] [--tech N] [--sim-ticks N] [--dump out.bmp]\n"
                     "            [--move i,cx,cy]... [--attack i,j]... [--attack-struct i,sid]...\n"
                     "            [--harvest i,cx,cy]... [--build b|i|v,type]... [--place cx,cy]\n"
                     "            [--deploy i]\n"
                     "  data-root example: data/assets/red_alert/allied\n");
        return 2;
    }
    try {
        auto map = game::MapFile::load(argv[1]);
        std::string root = argv[2];
        const char* dumpPath = strArg(argc, argv, "--dump");
        int scale = intArg(argc, argv, "--scale", 1);
        int simTicks = intArg(argc, argv, "--sim-ticks", -1);

        bool isTd = map.game == game::Game::TiberianDawn;
        std::string theaterDir, conquerDir, hiresDir, palPath, rulesPath;
        std::array<game::RemapTable, size_t(game::PlayerColor::Count)> remaps{};
        bool haveRemaps = false;
        if (isTd) {
            theaterDir = root + "/" + theaterDirName(map);
            conquerDir = root + "/CONQUER";
            // Sidebar cameos (<type>icon.shp) ship only on the Covert Ops disc,
            // not the base GDI/Nod CONQUER; use it as the fallback art dir.
            hiresDir = root + "/../covert_ops/CONQUER";
            palPath = theaterDir + "/" + map.theaterPalette() + ".pal";
            // TD has no rules.ini; use our authored stats (override with --rules).
            rulesPath = strArg(argc, argv, "--rules") ? strArg(argc, argv, "--rules")
                                                      : "td_rules.ini";
        } else {
            std::string localDir = root + "/INSTALL/REDALERT/local";
            theaterDir = root + "/MAIN/" + theaterDirName(map);
            conquerDir = root + "/MAIN/conquer";
            hiresDir = root + "/INSTALL/REDALERT/hires";
            palPath = localDir + "/" + map.theaterPalette() + ".pal";
            rulesPath = localDir + "/rules.ini";
            remaps = game::buildRemaps(localDir + "/palette.cps");
            haveRemaps = true;
        }
        auto pal = fmt::Palette::load(palPath);
        ArtCache art(theaterDir, conquerDir, hiresDir, map.theaterExt());
        auto rules = game::Rules::load(rulesPath);

        // HUD fonts (Westwood .FNT). Optional: absent → HUD renders without text.
        std::string fontDir =
            isTd ? root + "/INSTALL/CCLOCAL" : root + "/INSTALL/REDALERT/local";
        auto loadFont = [](const std::string& p) -> std::optional<fmt::FntFile> {
            try {
                return fmt::FntFile::load(p);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        };
        std::optional<fmt::FntFile> font6 = loadFont(fontDir + "/6point.fnt");
        std::optional<fmt::FntFile> font8 = loadFont(fontDir + "/8point.fnt");
        // 8point is the compact, legible HUD face; 6point (larger) is a fallback.
        const std::optional<fmt::FntFile>& hudFont = font8.has_value() ? font8 : font6;
        if (!hudFont)
            std::printf("note: no HUD font at %s (text disabled)\n", fontDir.c_str());

        // Sound mixer: opened only for the interactive window (init() below);
        // stays silent in headless runs, so processEvents()'s SFX are no-ops.
        game::AudioMixer mixer;
        mixer.setSoundDir(root + "/SOUNDS");
        mixer.setMusicDir(root + "/SCORES");
        // EVA speech ships only on the Covert Ops disc (like the cameos).
        mixer.setEvaDir(root + "/../covert_ops/AUD1/SPEECH");

        // House->remap: TD builds its remap in code (placeholder band ->
        // per-house block); RA uses PALETTE.CPS. Cached by house name.
        std::unordered_map<std::string, game::RemapTable> tdRemaps;
        auto remapFor = [&](const std::string& house) -> const game::RemapTable* {
            if (isTd) {
                auto it = tdRemaps.find(house);
                if (it == tdRemaps.end())
                    it = tdRemaps.emplace(house, game::tdRemap(house)).first;
                return &it->second;
            }
            if (!haveRemaps)
                return nullptr;
            return &remaps[size_t(game::houseColor(house))];
        };
        // Template id -> art base name, per game's template table.
        auto templateArt = [&](uint16_t id) -> const char* {
            if (isTd)
                return id < game::kTdTemplateCount ? game::kTdTemplateTable[id].name : nullptr;
            return id < game::kTemplateCount ? game::kTemplateTable[id].name : nullptr;
        };

        std::printf("%s: theater %s, bounds %d,%d %dx%d, %zu units, %zu infantry, "
                    "%zu structures\n",
                    argv[1], map.theater.c_str(), map.x, map.y, map.width, map.height,
                    map.units.size(), map.infantry.size(), map.structures.size());

        const int cx0 = map.x, cy0 = map.y, cw = map.width, ch = map.height;
        constexpr int kSize = game::MapFile::kSize;

        // ---- Bake static world surface (terrain + overlays + terrain objects) ----
        SDL_Surface* mapSurf = SDL_CreateRGBSurfaceWithFormat(0, cw * kTile, ch * kTile,
                                                              32, SDL_PIXELFORMAT_ARGB8888);
        if (!mapSurf)
            throw std::runtime_error(SDL_GetError());
        game::Canvas mc = game::Canvas::wrap(mapSurf);

        const fmt::TmpFile* clear = art.tmp("clear1");
        if (!clear)
            throw std::runtime_error("missing clear1 template");

        game::Sim sim;
        sim.setRules(&rules);
        // Shroud is drawn from this house's point of view.
        std::string playerHouse = strArg(argc, argv, "--house") ? strArg(argc, argv, "--house")
                                  : isTd                        ? "GoodGuy"
                                                                : "Greece";
        if (!flagArg(argc, argv, "--no-shroud"))
            sim.setPlayerHouse(playerHouse);

        // Bakes one cell's base terrain into the world surface (also used to
        // redraw a cell after its ore is harvested away).
        auto bakeTerrainCell = [&](int mapX, int mapY) {
            const auto& cell = map.cells[mapY * kSize + mapX];
            int dx = (mapX - cx0) * kTile, dy = (mapY - cy0) * kTile;

            const fmt::TmpFile* t = nullptr;
            int icon = 0;
            const char* tname =
                cell.templateId != 0xffff ? templateArt(cell.templateId) : nullptr;
            if (tname && tname[0]) {
                t = art.tmp(tname);
                icon = cell.icon;
                if (!t) {
                    game::fillRect(mc, dx, dy, kTile, kTile, 0xffff00ff);
                    return;
                }
            }
            if (t && (icon >= int(t->tiles.size()) || t->tiles[icon].empty()))
                t = nullptr;
            if (!t) {
                t = clear;
                icon = (mapX & 3) | ((mapY & 3) << 2); // Clear_Icon()
            }
            blitIndexed(mc, t->tiles[icon].data(), t->tileWidth, t->tileHeight, dx, dy,
                        pal);
            // Land type: RA reads the TMP control map; TD TMPs carry none, so
            // TD looks up the template table (default land, with per-icon
            // exceptions for shore/slope transitions -- CELL.CPP Land_Type()).
            if (t == clear)
                return; // clear-fallback cell stays the default Land::Clear
            if (isTd) {
                if (cell.templateId < game::kTdTemplateCount) {
                    const auto& info = game::kTdTemplateTable[cell.templateId];
                    game::Land land = game::Land(info.land);
                    if (info.altIcons)
                        for (const int8_t* p = info.altIcons; *p != -1; ++p)
                            if (*p == icon) {
                                land = game::Land(info.altLand);
                                break;
                            }
                    sim.setLand(mapY * kSize + mapX, land);
                }
            } else if (icon < int(t->landBytes.size())) {
                sim.setLand(mapY * kSize + mapX,
                            game::landFromControl(t->landBytes[icon]));
            }
        };
        for (int cy = 0; cy < ch; cy++)
            for (int cx = 0; cx < cw; cx++)
                bakeTerrainCell(cx0 + cx, cy0 + cy);

        // ---- Overlay layer (+ land effects: ore, walls) ----
        auto overlayAt = [&](int mx, int my) -> int {
            if (mx < 0 || mx >= kSize || my < 0 || my >= kSize)
                return 0xff;
            return map.cells[my * kSize + mx].overlay;
        };
        auto isResource = [](int o) { return o >= 5 && o <= 12; }; // gold01..gem04
        auto isWall = [](int o) {
            return o == 0 || o == 1 || o == 2 || o == 3 || o == 4 || o == 23 || o == 24;
        };
        for (int cy = 0; !isTd && cy < ch; cy++) {
            for (int cx = 0; cx < cw; cx++) {
                int mapX = cx0 + cx, mapY = cy0 + cy;
                int o = overlayAt(mapX, mapY);
                if (o == 0xff || o >= 25)
                    continue;
                if (isResource(o))
                    sim.setLand(mapY * kSize + mapX, game::Land::Ore);
                else if (isWall(o))
                    sim.setLand(mapY * kSize + mapX, game::Land::Wall);
                const fmt::ShpFile* s = art.shp(game::kOverlayNames[o]);
                if (!s || s->frames.empty())
                    continue;

                int frame = 0;
                if (isResource(o)) {
                    static const int kAdjGold[9] = {0, 1, 3, 4, 6, 7, 8, 10, 11};
                    static const int kAdjGem[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
                    int count = 0;
                    for (int dy2 = -1; dy2 <= 1; dy2++)
                        for (int dx2 = -1; dx2 <= 1; dx2++)
                            if ((dx2 || dy2) && isResource(overlayAt(mapX + dx2, mapY + dy2)))
                                count++;
                    frame = o <= 8 ? kAdjGold[count] : kAdjGem[count];
                    // Displayed density doubles as the harvestable bail count.
                    sim.setOre(mapY * kSize + mapX, frame + 1, o >= 9);
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

        // ---- Overlay layer (Tiberian Dawn): tiberium (ti1-12), walls, crates ----
        if (isTd) {
            for (const auto& ov : map.tdOverlay) {
                int ocx = ov.cell % kSize, ocy = ov.cell / kSize;
                if (ocx < cx0 || ocx >= cx0 + cw || ocy < cy0 || ocy >= cy0 + ch)
                    continue;
                bool tib = ov.name.size() >= 2 && ov.name[0] == 't' && ov.name[1] == 'i';
                bool wall = ov.name == "sbag" || ov.name == "cycl" || ov.name == "brik" ||
                            ov.name == "barb" || ov.name == "wood";
                if (tib) {
                    sim.setLand(ov.cell, game::Land::Ore);
                    sim.setOre(ov.cell, rules.bailCount(), false);
                } else if (wall) {
                    sim.setLand(ov.cell, game::Land::Wall);
                    sim.setBlocked(ov.cell);
                }
                const fmt::ShpFile* s = art.shp(ov.name);
                if (!s || s->frames.empty())
                    continue;
                game::BlitOptions opts;
                opts.colorKey = true;
                opts.shadow = true;
                blitIndexed(mc, s->frames[0].data(), s->width, s->height,
                            (ocx - cx0) * kTile, (ocy - cy0) * kTile, pal, opts);
            }
        }

        // ---- Terrain objects (trees, mines): draw + block footprint ----
        // Approximation: blocks the SHP's cell bounds (the original uses
        // per-type occupy lists from TDATA.CPP).
        for (const auto& obj : map.terrain) {
            const fmt::ShpFile* s = art.shp(obj.name);
            if (!s || s->frames.empty())
                continue;
            game::BlitOptions opts;
            opts.colorKey = true;
            opts.shadow = true;
            int ocx = obj.cell % kSize, ocy = obj.cell / kSize;
            blitIndexed(mc, s->frames[0].data(), s->width, s->height,
                        (ocx - cx0) * kTile, (ocy - cy0) * kTile, pal, opts);
            for (int by = 0; by < (s->height + kTile - 1) / kTile; by++)
                for (int bx = 0; bx < (s->width + kTile - 1) / kTile; bx++)
                    if (ocx + bx < kSize && ocy + by < kSize)
                        sim.setBlocked((ocy + by) * kSize + ocx + bx);
        }

        // ---- Structures: sim entities (attackable) + static drawables ----
        std::vector<DrawObject> structures;
        // Footprint = SHP cell bounds (approximation, see MILESTONES).
        auto structFootprint = [&](const std::string& type, int& w, int& h) {
            const fmt::ShpFile* shp = art.shp(type);
            if (!shp || shp->frames.empty())
                return false;
            w = (shp->width + kTile - 1) / kTile;
            h = (shp->height + kTile - 1) / kTile;
            return true;
        };
        // Drawables for an already-registered sim structure (art + roof).
        auto addStructDrawable = [&](const std::string& type, const std::string& house,
                                     int cell, int sid) {
            const fmt::ShpFile* shp = art.shp(type);
            DrawObject o;
            o.shp = shp;
            o.x = (cell % kSize - cx0) * kTile;
            o.y = (cell / kSize - cy0) * kTile;
            o.remap = remapFor(house);
            o.selectable = true;
            o.structId = sid;
            structures.push_back(o);
            if (const fmt::ShpFile* top = art.shp(type + "2")) {
                DrawObject o2 = o;
                o2.shp = top;
                o2.selectable = false;
                structures.push_back(o2);
            }
        };
        for (const auto& s : map.structures) {
            int w = 0, h = 0;
            if (!structFootprint(s.type, w, h)) {
                std::printf("note: missing structure art: %s\n", s.type.c_str());
                continue;
            }
            game::Sim::Structure st;
            st.type = s.type;
            st.house = s.house;
            st.cell = s.cell;
            st.w = w;
            st.h = h;
            st.stats = rules.unit(s.type, game::UnitKind::Structure);
            st.hp = std::max(1, st.stats.strength * s.health / 256);
            int sid = sim.addStructure(std::move(st));
            addStructDrawable(s.type, s.house, s.cell, sid);
            // addStructure blocked the footprint and revealed shroud
        }

        // ---- Production setup: starting credits + sidebar build lists ----
        sim.setCredits(playerHouse, intArg(argc, argv, "--credits", 3000));
        int techLevel = intArg(argc, argv, "--tech", 16);
        std::string side = isTd ? (playerHouse == "BadGuy" ? "nod" : "gdi")
                           : isSovietHouse(playerHouse) ? "soviet"
                                                        : "allies";
        std::vector<BuildEntry> buildStructs, buildUnits;
        auto addBuildable = [&](const char* type, game::UnitKind kind) {
            const auto& st = rules.unit(type, kind);
            if (st.cost <= 0 || st.techLevel < 0 || st.techLevel > techLevel)
                return;
            if (st.owner.find(side) == std::string::npos)
                return;
            const fmt::ShpFile* icon = art.shp(std::string(type) + "icon");
            if (!icon || icon->frames.empty())
                return;
            (kind == game::UnitKind::Structure ? buildStructs : buildUnits)
                .push_back({type, kind, icon});
        };
        for (const char* t : kStructTypes)
            if (!isTd) addBuildable(t, game::UnitKind::Structure);
        for (const char* t : kInfTypes)
            if (!isTd) addBuildable(t, game::UnitKind::Infantry);
        for (const char* t : kVehTypes)
            if (!isTd) addBuildable(t, game::UnitKind::Vehicle);
        if (isTd) {
            for (const char* t : kTdStructTypes)
                addBuildable(t, game::UnitKind::Structure);
            for (const char* t : kTdInfTypes)
                addBuildable(t, game::UnitKind::Infantry);
            for (const char* t : kTdVehTypes)
                addBuildable(t, game::UnitKind::Vehicle);
        }

        // ---- Sim units from the scenario ----
        for (const auto& u : map.units) {
            game::Sim::Unit su;
            su.type = u.type;
            su.house = u.house;
            su.facing = u.facing;
            su.turreted = game::hasTurretArt(u.type);
            su.harvester = u.type == "harv";
            su.stats = rules.unit(u.type, isShipType(u.type) ? game::UnitKind::Ship
                                                             : game::UnitKind::Vehicle);
            su.hp = std::max(1, su.stats.strength * u.health / 256);
            sim.addUnit(std::move(su), u.cell);
        }
        for (const auto& inf : map.infantry) {
            game::Sim::Unit su;
            su.type = inf.type;
            su.house = inf.house;
            su.infantry = true;
            su.subcell = inf.subcell;
            su.facing = inf.facing;
            su.stats = rules.unit(inf.type, game::UnitKind::Infantry);
            su.hp = std::max(1, su.stats.strength * inf.health / 256);
            sim.addUnit(std::move(su), inf.cell);
        }

        // Resolve a sim unit to its drawable for the current frame.
        auto unitDrawObject = [&](const game::Sim::Unit& u) -> std::optional<DrawObject> {
            const fmt::ShpFile* shp = art.shp(u.type);
            if ((!shp || shp->frames.empty()) && u.infantry && u.type.size() >= 2 &&
                u.type[0] == 'c' && std::isdigit((unsigned char)u.type[1]))
                shp = art.shp("c1"); // civilians c3-c10 share c1's art
            if (!shp || shp->frames.empty())
                return std::nullopt;
            DrawObject o;
            o.shp = shp;
            if (u.infantry) {
                o.frame = (8 - ((u.facing & 0xff) >> 5)) & 7;
            } else {
                o.frame = game::facingToFrame(u.facing) % int(shp->frames.size());
                if (u.turreted && shp->frames.size() >= 64)
                    o.turretFrame = 32 + game::facingToFrame(u.turretFacing);
            }
            o.x = u.x * kTile / game::Sim::kLepton - cx0 * kTile - shp->width / 2;
            o.y = u.y * kTile / game::Sim::kLepton - cy0 * kTile - shp->height / 2;
            o.remap = remapFor(u.house);
            o.selectable = true;
            o.health = u.healthFrac();
            o.selected = u.selected;
            o.unitId = u.id;
            return o;
        };

        // Anything whose center sits in an unexplored cell is hidden.
        auto objectVisible = [&](const DrawObject& o) {
            int cx = cx0 + (o.x + o.shp->width / 2) / kTile;
            int cy = cy0 + (o.y + o.shp->height / 2) / kTile;
            if (cx < 0 || cx >= kSize || cy < 0 || cy >= kSize)
                return false;
            return sim.explored(cy * kSize + cx);
        };

        auto buildDrawList = [&]() {
            std::vector<DrawObject> objects;
            for (auto s : structures) {
                const auto* live = sim.findStructure(s.structId);
                if (!live || !objectVisible(s))
                    continue;
                s.health = std::clamp(live->hp * 256 / std::max(1, live->stats.strength),
                                      0, 256);
                objects.push_back(s);
            }
            for (const auto& u : sim.units())
                if (auto o = unitDrawObject(u))
                    if (objectVisible(*o))
                        objects.push_back(*o);
            std::stable_sort(objects.begin(), objects.end(),
                             [](const DrawObject& a, const DrawObject& b) {
                                 return a.y < b.y;
                             });
            return objects;
        };

        // ---- Effects: projectiles in flight + explosion/impact anims ----
        std::vector<Anim> anims;
        // Turn last tick's sim events into effect anims; call once per tick.
        auto processEvents = [&]() {
            for (auto& a : anims)
                a.frame++;
            anims.erase(std::remove_if(anims.begin(), anims.end(),
                                       [](const Anim& a) {
                                           return a.frame >= int(a.shp->frames.size());
                                       }),
                        anims.end());
            for (const auto& ev : sim.events()) {
                // Sound (no-op unless the interactive mixer is open).
                switch (ev.type) {
                case game::Sim::Event::Fire:
                    mixer.playSound(ev.weapon ? ev.weapon->report : "");
                    continue; // muzzle report only; no explosion anim
                case game::Sim::Event::Impact:
                    if (ev.damage >= 20) // skip machine-gun pitter-patter
                        mixer.playSound("xplos");
                    break;
                case game::Sim::Event::UnitDied:
                    mixer.playSound(ev.infantry ? "nuyell1" : "xplobig4");
                    break;
                case game::Sim::Event::StructDied:
                    mixer.playSound("crumble");
                    break;
                default:
                    break;
                }
                std::string name;
                if (ev.type == game::Sim::Event::OreDepleted) {
                    bakeTerrainCell(ev.cell % kSize, ev.cell / kSize);
                    continue;
                }
                if (ev.type == game::Sim::Event::Impact) {
                    int c = (ev.y / game::Sim::kLepton) * kSize + ev.x / game::Sim::kLepton;
                    bool water = sim.landAt(c) == game::Land::Water ||
                                 sim.landAt(c) == game::Land::River;
                    name = combatAnimName(ev.damage, ev.warhead, water);
                } else if (ev.type == game::Sim::Event::UnitDied) {
                    // Approximation: vehicles fireball; infantry just vanish
                    // (real per-type InfDeath sequences are still todo).
                    name = ev.infantry ? "" : "fball1";
                } else { // StructDied
                    name = "fball1";
                }
                if (name.empty())
                    continue;
                const fmt::ShpFile* shp = art.shp(name);
                if (!shp || shp->frames.empty())
                    continue;
                anims.push_back({shp, ev.x * kTile / game::Sim::kLepton - cx0 * kTile,
                                 ev.y * kTile / game::Sim::kLepton - cy0 * kTile, 0});
            }
        };
        auto drawEffects = [&](game::Canvas& c, int offX, int offY) {
            game::BlitOptions opts;
            opts.colorKey = true;
            opts.shadow = true;
            for (const auto& p : sim.projectiles()) {
                if (p.weapon->projectileImage.empty())
                    continue;
                const fmt::ShpFile* shp = art.shp(p.weapon->projectileImage);
                if (!shp || shp->frames.empty())
                    continue;
                int frame = shp->frames.size() >= 32
                                ? game::facingToFrame(p.facing) % int(shp->frames.size())
                                : 0;
                blitIndexed(c, shp->frames[frame].data(), shp->width, shp->height,
                            p.x * kTile / game::Sim::kLepton - cx0 * kTile -
                                shp->width / 2 - offX,
                            p.y * kTile / game::Sim::kLepton - cy0 * kTile -
                                shp->height / 2 - offY,
                            pal, opts);
            }
            for (const auto& a : anims)
                blitIndexed(c, a.shp->frames[a.frame].data(), a.shp->width,
                            a.shp->height, a.x - a.shp->width / 2 - offX,
                            a.y - a.shp->height / 2 - offY, pal, opts);
        };

        // Black out unexplored cells of the visible window (world-pixel view
        // rect at offX/offY). Drawn over objects, under the cursor.
        auto drawShroud = [&](game::Canvas& c, int offX, int offY) {
            int c0 = std::max(0, offX / kTile), r0 = std::max(0, offY / kTile);
            int c1 = std::min(cw - 1, (offX + c.w) / kTile);
            int r1 = std::min(ch - 1, (offY + c.h) / kTile);
            for (int cy = r0; cy <= r1; cy++)
                for (int cx = c0; cx <= c1; cx++)
                    if (!sim.explored((cy0 + cy) * kSize + cx0 + cx))
                        game::fillRect(c, cx * kTile - offX, cy * kTile - offY,
                                       kTile, kTile, 0xff000000);
        };

        // ---- Headless sim run ----
        if (simTicks >= 0) {
            auto printUnits = [&](const char* tag) {
                for (const auto& u : sim.units())
                    std::printf("%s unit %d %-5s %-8s cell %d,%d facing %d hp %d/%d%s%s\n",
                                tag, u.id, u.type.c_str(), u.house.c_str(),
                                u.x / game::Sim::kLepton, u.y / game::Sim::kLepton,
                                u.facing, u.hp, u.stats.strength,
                                u.moving() ? " (moving)" : "",
                                u.hasTarget() ? " (attacking)" : "");
                for (const auto& s : sim.structures())
                    std::printf("%s struct %d %-5s %-8s cell %d,%d hp %d/%d\n", tag,
                                s.id, s.type.c_str(), s.house.c_str(), s.cell % kSize,
                                s.cell / kSize, s.hp, s.stats.strength);
            };
            printUnits("before:");
            std::vector<std::pair<int, int>> orders; // unit id -> dest cell
            for (int i = 3; i < argc - 1; i++) {
                int a = -1, b = -1, c = -1;
                if (std::strcmp(argv[i], "--move") == 0) {
                    if (std::sscanf(argv[i + 1], "%d,%d,%d", &a, &b, &c) == 3 &&
                        a >= 0 && a < int(sim.units().size()))
                        orders.emplace_back(sim.units()[a].id, c * kSize + b);
                    else
                        std::fprintf(stderr, "bad --move '%s' (want i,cx,cy)\n",
                                     argv[i + 1]);
                } else if (std::strcmp(argv[i], "--attack") == 0) {
                    if (std::sscanf(argv[i + 1], "%d,%d", &a, &b) == 2 && a >= 0 &&
                        a < int(sim.units().size()) && b >= 0 &&
                        b < int(sim.units().size()))
                        sim.orderAttack({sim.units()[a].id}, sim.units()[b].id, -1);
                    else
                        std::fprintf(stderr, "bad --attack '%s' (want i,j)\n",
                                     argv[i + 1]);
                } else if (std::strcmp(argv[i], "--attack-struct") == 0) {
                    if (std::sscanf(argv[i + 1], "%d,%d", &a, &b) == 2 && a >= 0 &&
                        a < int(sim.units().size()) && sim.findStructure(b))
                        sim.orderAttack({sim.units()[a].id}, -1, b);
                    else
                        std::fprintf(stderr, "bad --attack-struct '%s' (want i,structId)\n",
                                     argv[i + 1]);
                } else if (std::strcmp(argv[i], "--harvest") == 0) {
                    if (std::sscanf(argv[i + 1], "%d,%d,%d", &a, &b, &c) == 3 &&
                        a >= 0 && a < int(sim.units().size()))
                        sim.orderHarvest({sim.units()[a].id}, c * kSize + b);
                    else
                        std::fprintf(stderr, "bad --harvest '%s' (want i,cx,cy)\n",
                                     argv[i + 1]);
                }
            }
            // Builds retry every tick until the prerequisites exist (e.g. a
            // powr queued behind an MCV deploy).
            std::vector<std::pair<std::string, game::UnitKind>> pendingBuilds;
            for (int i = 3; i < argc - 1; i++) {
                if (std::strcmp(argv[i], "--build") != 0)
                    continue;
                char cat = 0;
                char type[16] = {};
                if (std::sscanf(argv[i + 1], "%c,%15s", &cat, type) == 2)
                    pendingBuilds.emplace_back(type,
                                               cat == 'b' ? game::UnitKind::Structure
                                               : cat == 'i' ? game::UnitKind::Infantry
                                                            : game::UnitKind::Vehicle);
                else
                    std::fprintf(stderr, "bad --build '%s' (want b|i|v,type)\n",
                                 argv[i + 1]);
            }
            // Placement spots, consumed FIFO as buildings become ready.
            std::vector<std::pair<int, int>> placeQueue;
            for (int i = 3; i < argc - 1; i++) {
                int px = -1, py = -1;
                if (std::strcmp(argv[i], "--place") == 0 &&
                    std::sscanf(argv[i + 1], "%d,%d", &px, &py) == 2)
                    placeQueue.emplace_back(px, py);
            }
            // Deployed as soon as the (possibly still-in-production) MCV
            // exists and has room; -1 = no deploy requested.
            int deployId = -1;
            if (const char* p = strArg(argc, argv, "--deploy")) {
                int idx = std::atoi(p);
                if (idx >= 0 && idx < int(sim.units().size()))
                    deployId = sim.units()[idx].id;
            }
            for (auto [id, dest] : orders)
                sim.orderMove({id}, dest);
            for (int t = 0; t < simTicks; t++) {
                sim.tick();
                if (deployId >= 0) {
                    if (const auto* mcv = sim.findUnit(deployId)) {
                        int w = 0, h = 0, cell = mcv->cell();
                        std::string house = mcv->house; // deployMcv erases the unit
                        if (structFootprint("fact", w, h)) {
                            int sid = sim.deployMcv(deployId, w, h);
                            if (sid >= 0) {
                                addStructDrawable("fact", house, cell - 1 - kSize, sid);
                                std::printf("tick %d: deployed MCV -> fact\n", t);
                                deployId = -1;
                            }
                        }
                    } else
                        deployId = -1; // died
                }
                for (auto it = pendingBuilds.begin(); it != pendingBuilds.end();) {
                    if (sim.startProduction(playerHouse, it->first, it->second)) {
                        std::printf("tick %d: started building %s\n", t,
                                    it->first.c_str());
                        it = pendingBuilds.erase(it);
                    } else
                        ++it;
                }
                // Place a finished building as soon as it's ready.
                if (!placeQueue.empty()) {
                    const auto* b = sim.production(playerHouse,
                                                   game::Sim::ProdCat::Building);
                    if (b && b->ready) {
                        auto [px, py] = placeQueue.front();
                        std::string type = b->type;
                        int w = 0, h = 0;
                        structFootprint(type, w, h);
                        int cell = py * kSize + px;
                        int sid = sim.placeBuilding(playerHouse, cell, w, h);
                        std::printf("tick %d: place %s at %d,%d: %s\n", t,
                                    type.c_str(), px, py, sid >= 0 ? "ok" : "FAILED");
                        if (sid >= 0) {
                            addStructDrawable(type, playerHouse, cell, sid);
                            placeQueue.erase(placeQueue.begin());
                        }
                    }
                }
                for (const auto& ev : sim.events()) {
                    if (ev.type == game::Sim::Event::UnitDied ||
                        ev.type == game::Sim::Event::StructDied)
                        std::printf("tick %d: %s died at %d,%d\n", t,
                                    ev.type == game::Sim::Event::UnitDied
                                        ? (ev.infantry ? "infantry" : "unit")
                                        : "structure",
                                    ev.x / game::Sim::kLepton,
                                    ev.y / game::Sim::kLepton);
                    else if (ev.type == game::Sim::Event::OreDepleted)
                        std::printf("tick %d: ore depleted at %d,%d\n", t,
                                    ev.cell % kSize, ev.cell / kSize);
                }
                processEvents();
            }
            printUnits("after: ");
            static const char* kCatName[] = {"building", "infantry", "vehicle"};
            for (int c = 0; c < 3; c++)
                if (const auto* p = sim.production(playerHouse, game::Sim::ProdCat(c)))
                    std::printf("production %s: %s %d/%d%s paid %d\n", kCatName[c],
                                p->type.c_str(), p->progress, p->ticksTotal,
                                p->ready ? " READY" : "", p->paid);
            std::set<std::string> houses;
            for (const auto& s : sim.structures())
                houses.insert(s.house);
            for (const auto& u : sim.units())
                houses.insert(u.house);
            for (const auto& h : houses) {
                int produced = 0, drained = 0;
                sim.power(h, produced, drained);
                if (sim.credits(h) || produced || drained)
                    std::printf("house %-8s credits %d power %d/%d\n", h.c_str(),
                                sim.credits(h), produced, drained);
            }
            if (dumpPath) {
                for (const auto& o : buildDrawList())
                    drawObject(mc, o, pal, 0, 0);
                drawEffects(mc, 0, 0);
                drawShroud(mc, 0, 0);
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
            }
            return 0;
        }

        // ---- Interactive: fixed-tick sim + free-running render ----
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());
        int viewW = std::min(mapSurf->w, 1280 - kSidebarW);
        int winW = viewW + kSidebarW, winH = std::min(mapSurf->h, 800);
        SDL_Window* win = SDL_CreateWindow("game", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, winW, winH, 0);
        if (!win)
            throw std::runtime_error(SDL_GetError());
        SDL_ShowCursor(SDL_DISABLE);
        std::optional<fmt::ShpD2File> cursor;
        try {
            cursor = fmt::ShpD2File::load((isTd ? conquerDir : hiresDir) + "/mouse.shp");
        } catch (const std::exception& ex) {
            std::printf("note: no cursor art (%s), using OS cursor\n", ex.what());
            SDL_ShowCursor(SDL_ENABLE);
        }

        // Audio: open the device and set up a simple score jukebox (each track
        // plays once; the render loop starts the next when one finishes).
        if (!mixer.init())
            std::printf("note: audio unavailable, running silent\n");
        static const char* kScores[] = {"aoi", "ccthang", "ind",
                                        "ind2", "fwp", "heavyg"};
        int musicIdx = 0;
        // Tracks per-category production "ready" so EVA announces the edge once.
        bool wasReady[int(game::Sim::ProdCat::Count)] = {};

        float camX = 0, camY = 0;
        const float kSpeed = 480.0f;
        const int kEdge = 16;
        bool dragging = false;
        int dragX0 = 0, dragY0 = 0;
        uint32_t last = SDL_GetTicks();
        double tickAcc = 0;
        bool quit = false;

        // ---- Sidebar layout + placement mode state ----
        std::string placingType; // building awaiting placement (ghost follows mouse)
        int placeW = 0, placeH = 0;
        int sideScroll = 0;
        // Cameos actually on the sidebar: the candidates whose prerequisites are
        // met right now (rebuilt each frame, like the original's StripClass).
        // Pointers into the stable buildStructs/buildUnits candidate lists.
        std::vector<const BuildEntry*> visStructs, visUnits;
        auto entryPos = [&](int col, int idx, int& x, int& y) {
            x = viewW + 4 + col * (kCameoW + 4);
            y = kSideTop + idx * (kCameoH + 4) - sideScroll;
        };
        // Cameo under the mouse; null if none.
        auto sidebarHit = [&](int hx, int hy) -> const BuildEntry* {
            if (hx < viewW + 4)
                return nullptr;
            int col = (hx - viewW - 4) / (kCameoW + 4);
            if (col > 1 || hx >= viewW + 4 + col * (kCameoW + 4) + kCameoW)
                return nullptr;
            const auto& list = col == 0 ? visStructs : visUnits;
            int idx = (hy - kSideTop + sideScroll) / (kCameoH + 4);
            if (idx < 0 || idx >= int(list.size()))
                return nullptr;
            int ex, ey;
            entryPos(col, idx, ex, ey);
            if (hy < ey || hy >= ey + kCameoH)
                return nullptr;
            return list[idx];
        };
        auto prodCatOf = [](game::UnitKind kind) {
            return kind == game::UnitKind::Structure ? game::Sim::ProdCat::Building
                   : kind == game::UnitKind::Infantry ? game::Sim::ProdCat::Infantry
                                                      : game::Sim::ProdCat::Vehicle;
        };

        while (!quit) {
            // Advance the jukebox when the current track ends.
            if (mixer.enabled() && !mixer.musicPlaying())
                mixer.playMusic(kScores[musicIdx++ % (int(sizeof(kScores) / sizeof(kScores[0])))]);
            int mx, my;
            uint32_t mstate = SDL_GetMouseState(&mx, &my);
            auto objects = buildDrawList();

            // Only show cameos whose prerequisites are currently met (the
            // sidebar reflects your base, not the whole tech tree at once).
            visStructs.clear();
            visUnits.clear();
            for (const auto& en : buildStructs)
                if (sim.canProduce(playerHouse, en.type, en.kind))
                    visStructs.push_back(&en);
            for (const auto& en : buildUnits)
                if (sim.canProduce(playerHouse, en.type, en.kind))
                    visUnits.push_back(&en);

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT)
                    quit = true;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                    if (!placingType.empty())
                        placingType.clear(); // keep the building ready
                    else
                        quit = true;
                }
                if (e.type == SDL_MOUSEWHEEL && mx >= viewW) {
                    int rows = int(std::max(visStructs.size(), visUnits.size()));
                    int maxScroll =
                        std::max(0, kSideTop + rows * (kCameoH + 4) + 4 - winH);
                    sideScroll = std::clamp(sideScroll - e.wheel.y * (kCameoH + 4),
                                            0, maxScroll);
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    if (e.button.x >= viewW) {
                        // Sidebar: click a cameo to build / place when ready.
                        if (const BuildEntry* en = sidebarHit(e.button.x, e.button.y)) {
                            auto cat = prodCatOf(en->kind);
                            const auto* p = sim.production(playerHouse, cat);
                            if (en->kind == game::UnitKind::Structure && p &&
                                p->ready && p->type == en->type) {
                                if (structFootprint(en->type, placeW, placeH))
                                    placingType = en->type;
                            } else if (!p) {
                                if (!sim.startProduction(playerHouse, en->type,
                                                         en->kind))
                                    std::printf("can't build %s (prerequisites?)\n",
                                                en->type.c_str());
                                else
                                    mixer.playEva("bldging1"); // "building"
                            }
                        }
                    } else if (!placingType.empty()) {
                        // Place the finished building at the cursor cell.
                        int cellX = cx0 + (e.button.x + int(camX)) / kTile;
                        int cellY = cy0 + (e.button.y + int(camY)) / kTile;
                        int cell = cellY * kSize + cellX;
                        int sid = sim.placeBuilding(playerHouse, cell, placeW, placeH);
                        if (sid >= 0) {
                            addStructDrawable(placingType, playerHouse, cell, sid);
                            placingType.clear();
                        }
                    } else {
                        dragging = true;
                        dragX0 = e.button.x;
                        dragY0 = e.button.y;
                    }
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                    dragging) {
                    dragging = false;
                    int x0 = std::min(dragX0, e.button.x) + int(camX);
                    int y0 = std::min(dragY0, e.button.y) + int(camY);
                    int x1 = std::max(dragX0, e.button.x) + int(camX);
                    int y1 = std::max(dragY0, e.button.y) + int(camY);
                    bool box = (x1 - x0) > 4 || (y1 - y0) > 4;
                    for (auto& s : structures)
                        s.selected = false;
                    for (auto& u : sim.units())
                        u.selected = false;
                    auto selectHit = [&](const DrawObject& o) {
                        if (o.unitId >= 0) {
                            for (auto& u : sim.units())
                                if (u.id == o.unitId)
                                    u.selected = true;
                        } else {
                            for (auto& s : structures)
                                if (s.shp == o.shp && s.x == o.x && s.y == o.y)
                                    s.selected = s.selectable;
                        }
                    };
                    if (box) {
                        for (const auto& o : objects)
                            if (o.selectable && o.x < x1 && o.x + o.shp->width > x0 &&
                                o.y < y1 && o.y + o.shp->height > y0)
                                selectHit(o);
                    } else {
                        for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                            if (it->selectable && x1 >= it->x &&
                                x1 < it->x + it->shp->width && y1 >= it->y &&
                                y1 < it->y + it->shp->height) {
                                selectHit(*it);
                                break;
                            }
                        }
                    }
                    // Acknowledge a fresh selection with a unit response.
                    int selCount = 0;
                    bool selInf = false;
                    for (const auto& su : sim.units())
                        if (su.selected) {
                            if (!selCount)
                                selInf = su.infantry;
                            selCount++;
                        }
                    if (selCount)
                        mixer.playVoice(selInf ? "report1" : "vehic1", selInf ? 4 : 2);
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT &&
                    e.button.x >= viewW) {
                    // Sidebar: right-click cancels that cameo's production.
                    if (const BuildEntry* en = sidebarHit(e.button.x, e.button.y)) {
                        auto cat = prodCatOf(en->kind);
                        const auto* p = sim.production(playerHouse, cat);
                        if (p && p->type == en->type) {
                            sim.cancelProduction(playerHouse, cat);
                            mixer.playEva("cancel1"); // "canceled"
                            if (placingType == en->type)
                                placingType.clear();
                        }
                    }
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT &&
                    e.button.x < viewW && !placingType.empty()) {
                    placingType.clear(); // abort placement, keep it ready
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT &&
                    e.button.x < viewW && placingType.empty()) {
                    int wx = e.button.x + int(camX), wy = e.button.y + int(camY);
                    int cellX = cx0 + wx / kTile, cellY = cy0 + wy / kTile;
                    if (cellX >= 0 && cellX < kSize && cellY >= 0 && cellY < kSize) {
                        std::vector<int> ids;
                        for (const auto& u : sim.units())
                            if (u.selected)
                                ids.push_back(u.id);
                        // Right-click on an enemy = attack; on an own selected
                        // MCV = deploy; otherwise move (or harvest ore).
                        int tu = -1, ts = -1;
                        bool deployed = false;
                        for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                            if (wx < it->x || wx >= it->x + it->shp->width ||
                                wy < it->y || wy >= it->y + it->shp->height)
                                continue;
                            if (it->unitId >= 0) {
                                const auto* u = sim.findUnit(it->unitId);
                                if (u && u->house == playerHouse && u->selected &&
                                    u->type == "mcv") {
                                    int fw = 0, fh = 0;
                                    int mcvCell = u->cell();
                                    if (structFootprint("fact", fw, fh)) {
                                        int sid = sim.deployMcv(u->id, fw, fh);
                                        if (sid >= 0)
                                            addStructDrawable("fact", playerHouse,
                                                              mcvCell - 1 - kSize, sid);
                                        else
                                            mixer.playEva("deploy1"); // can't deploy here
                                    }
                                    deployed = true;
                                    break;
                                }
                                if (u && u->house != playerHouse) {
                                    tu = u->id;
                                    break;
                                }
                            } else if (it->structId >= 0 && it->selectable) {
                                const auto* s = sim.findStructure(it->structId);
                                if (s && s->house != playerHouse) {
                                    ts = s->id;
                                    break;
                                }
                            }
                        }
                        int cell = cellY * kSize + cellX;
                        if (ids.empty() || deployed)
                            ; // nothing selected (or the click deployed an MCV)
                        else if (tu >= 0 || ts >= 0) {
                            sim.orderAttack(ids, tu, ts);
                            mixer.playVoice("affirm1", 4); // "affirmative"
                        } else if (sim.oreAt(cell) > 0) {
                            // Harvesters gather; anyone else just drives there.
                            std::vector<int> harv, rest;
                            for (int id : ids)
                                (sim.findUnit(id)->harvester ? harv : rest)
                                    .push_back(id);
                            if (!harv.empty())
                                sim.orderHarvest(harv, cell);
                            if (!rest.empty())
                                sim.orderMove(rest, cell);
                            mixer.playVoice("movout1", 4); // "movin' out"
                        } else {
                            sim.orderMove(ids, cell);
                            mixer.playVoice("movout1", 4); // "movin' out"
                        }
                    }
                }
            }

            // EVA: announce production completion on the ready edge (player house).
            for (int c = 0; c < int(game::Sim::ProdCat::Count); c++) {
                const auto* p = sim.production(playerHouse, game::Sim::ProdCat(c));
                bool ready = p && p->ready;
                if (ready && !wasReady[c])
                    mixer.playEva(c == int(game::Sim::ProdCat::Building) ? "constru1"
                                                                        : "unitredy");
                wasReady[c] = ready;
            }

            // No in-game font yet: surface credits/power in the title bar.
            static uint32_t lastTitle = 0;
            if (SDL_GetTicks() - lastTitle > 500) {
                lastTitle = SDL_GetTicks();
                int produced = 0, drained = 0;
                sim.power(playerHouse, produced, drained);
                char title[128];
                std::snprintf(title, sizeof(title),
                              "game — %s  credits: %d  power: %d/%d", playerHouse.c_str(),
                              sim.credits(playerHouse), produced, drained);
                SDL_SetWindowTitle(win, title);
            }

            uint32_t now = SDL_GetTicks();
            float dt = float(now - last) / 1000.0f;
            tickAcc += now - last;
            last = now;
            if (tickAcc > 10 * kTickMs)
                tickAcc = kTickMs; // don't spiral after a stall
            while (tickAcc >= kTickMs) {
                sim.tick();
                processEvents();
                tickAcc -= kTickMs;
            }

            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            float dx = 0, dy = 0;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] || mx < kEdge)
                dx -= 1;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ||
                (mx < viewW && mx >= viewW - kEdge))
                dx += 1;
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] || my < kEdge)
                dy -= 1;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] || my >= winH - kEdge)
                dy += 1;
            camX = std::clamp(camX + dx * kSpeed * dt, 0.0f, float(mapSurf->w - viewW));
            camY = std::clamp(camY + dy * kSpeed * dt, 0.0f, float(mapSurf->h - winH));

            SDL_Surface* wsurf = SDL_GetWindowSurface(win);
            SDL_Rect src{int(camX), int(camY), viewW, winH};
            SDL_Rect dst{0, 0, viewW, winH};
            SDL_BlitSurface(mapSurf, &src, wsurf, &dst);

            game::Canvas wc = game::Canvas::wrap(wsurf);
            objects = buildDrawList(); // positions may have ticked above
            for (const auto& o : objects)
                drawObject(wc, o, pal, int(camX), int(camY));
            // Numeric health % above selected units/structures.
            if (hudFont)
                for (const auto& o : objects)
                    if (o.selected && o.selectable) {
                        int pct = std::clamp(o.health * 100 / 256, 0, 100);
                        std::string s = std::to_string(pct) + "%";
                        uint32_t hc = pct > 66 ? 0xff30ff30
                                      : pct > 33 ? 0xffe0e030
                                                 : 0xffff3030;
                        int tx = o.x - int(camX) + o.shp->width / 2 -
                                 game::textWidth(*hudFont, s) / 2;
                        int ty = o.y - int(camY) - 13;
                        game::drawText(wc, *hudFont, s, tx + 1, ty + 1, 0xff000000);
                        game::drawText(wc, *hudFont, s, tx, ty, hc);
                    }
            drawEffects(wc, int(camX), int(camY));
            drawShroud(wc, int(camX), int(camY));

            // Placement ghost: footprint outline at the cursor cell.
            if (!placingType.empty() && mx < viewW) {
                int cellX = cx0 + (mx + int(camX)) / kTile;
                int cellY = cy0 + (my + int(camY)) / kTile;
                bool ok = sim.canPlace(playerHouse, cellY * kSize + cellX,
                                       placeW, placeH);
                uint32_t col = ok ? 0xff00ff00 : 0xffff0000;
                for (int by = 0; by < placeH; by++)
                    for (int bx = 0; bx < placeW; bx++)
                        game::drawRect(wc,
                                       (cellX + bx - cx0) * kTile - int(camX),
                                       (cellY + by - cy0) * kTile - int(camY),
                                       kTile, kTile, col);
                // "<NAME> $<cost>" label following the cursor.
                if (hudFont) {
                    std::string lbl = placingType;
                    for (auto& ch : lbl)
                        ch = char(std::toupper((unsigned char)ch));
                    int cost = rules.unit(placingType, game::UnitKind::Structure).cost;
                    lbl += " $" + std::to_string(cost);
                    game::drawText(wc, *hudFont, lbl, mx + 13, my + 1, 0xff000000);
                    game::drawText(wc, *hudFont, lbl, mx + 12, my, kText);
                }
            }

            // ---- Sidebar chrome (metallic panel + radar + buttons) ----
            uint32_t faction = playerHouse == "BadGuy" ? 0xffd83030   // Nod red
                                                       : 0xffd8b040;  // GDI gold
            bevelPanel(wc, viewW, kTopBar, kSidebarW, winH - kTopBar, kFace);

            // Radar/logo box: a live minimap of the baked world with house blips
            // and a faction-colored inner frame (the original's radar surround).
            int rx = viewW + 4, ry = kTopBar + 4;
            int rw = kSidebarW - 8, rh = kRadarH - 4;
            bevelPanel(wc, rx, ry, rw, rh, 0xff101008, /*sunken=*/true);
            int irx = rx + 2, iry = ry + 2, irw = rw - 4, irh = rh - 4;
            for (int py = 0; py < irh; py++)
                for (int px = 0; px < irw; px++) {
                    int sx = px * mapSurf->w / irw, sy = py * mapSurf->h / irh;
                    wc.px[(iry + py) * wc.pitch + (irx + px)] =
                        mc.px[sy * mc.pitch + sx];
                }
            auto blip = [&](int cell, const std::string& house) {
                int bx = irx + (cell % kSize - cx0) * irw / cw;
                int by = iry + (cell / kSize - cy0) * irh / ch;
                uint32_t col = house == "Neutral" ? 0xffb0b0b0
                               : house == playerHouse ? 0xff30ff30
                                                      : 0xffff3030;
                game::fillRect(wc, bx, by, 2, 2, col);
            };
            for (const auto& s : sim.structures())
                if (s.hp > 0)
                    blip(s.cell, s.house);
            for (const auto& u : sim.units())
                if (u.hp > 0)
                    blip(u.cell(), u.house);
            game::drawRect(wc, irx - 1, iry - 1, irw + 2, irh + 2, faction);

            // REPAIR | SELL | MAP button row (visual for now). Buttons are sized
            // to their labels so the text never overflows the narrow sidebar.
            int byRow = kTopBar + kRadarH + 2;
            const char* btns[] = {"REPAIR", "SELL", "MAP"};
            int tw[3], natural = 0;
            for (int b = 0; b < 3; b++) {
                tw[b] = hudFont ? game::textWidth(*hudFont, btns[b], 0) : 24;
                natural += tw[b] + 6; // 3px padding each side
            }
            int gap = std::max(1, (kSidebarW - 8 - natural) / 2);
            int bx = viewW + 4;
            for (int b = 0; b < 3; b++) {
                int w = tw[b] + 6;
                bevelPanel(wc, bx, byRow, w, kBtnH, kFace);
                drawTextCentered(wc, hudFont, btns[b], bx, w, byRow + 2, kText, 0);
                bx += w + gap;
            }

            // Halve a region's brightness (like the SHP shadow table) to mark
            // an available-but-unaffordable cameo.
            auto darken = [&](int x0, int y0, int w, int h) {
                for (int yy = std::max(0, y0); yy < std::min(winH, y0 + h); yy++)
                    for (int xx = std::max(0, x0); xx < std::min(wc.w, x0 + w); xx++) {
                        uint32_t px = wc.px[yy * wc.pitch + xx];
                        wc.px[yy * wc.pitch + xx] = 0xff000000 | ((px >> 1) & 0x7f7f7f);
                    }
            };
            auto drawStrip = [&](const std::vector<const BuildEntry*>& list, int col) {
                for (int i = 0; i < int(list.size()); i++) {
                    int ex, ey;
                    entryPos(col, i, ex, ey);
                    if (ey + kCameoH < kSideTop - kCameoH || ey >= winH)
                        continue;
                    const auto& en = *list[i];
                    // TD cameos are 32x24 — scale to fill the 64x48 slot.
                    blitIndexedScaled(wc, en.icon->frames[0].data(), en.icon->width,
                                      en.icon->height, ex, ey, kCameoW, kCameoH, pal);
                    const auto* p = sim.production(playerHouse, prodCatOf(en.kind));
                    bool building = p && p->type == en.type;
                    // Darken cameos you can't yet afford (unless already building).
                    if (!building &&
                        sim.credits(playerHouse) < rules.unit(en.type, en.kind).cost)
                        darken(ex, ey, kCameoW, kCameoH);
                    // Cost label (bottom-right of the cameo) with a 1px shadow.
                    int cost = rules.unit(en.type, en.kind).cost;
                    if (hudFont && cost > 0) {
                        std::string s = "$" + std::to_string(cost);
                        int tx = ex + kCameoW - game::textWidth(*hudFont, s) - 2;
                        int ty = ey + kCameoH - hudFont->maxHeight;
                        game::drawText(wc, *hudFont, s, tx + 1, ty + 1, 0xff000000);
                        game::drawText(wc, *hudFont, s, tx, ty, kGreen);
                    }
                    // Raised bezel around the filled slot.
                    game::drawRect(wc, ex - 1, ey - 1, kCameoW + 2, kCameoH + 2, kDark);
                    if (building) {
                        game::fillRect(wc, ex, ey + kCameoH - 4,
                                       kCameoW * p->frac256() / 256, 3, 0xffe8e800);
                        if (p->ready)
                            game::drawRect(wc, ex, ey, kCameoW, kCameoH,
                                           placingType.empty() ? 0xff00ff00
                                                               : 0xffffffff);
                    }
                }
            };
            // Empty recessed cameo slots so a sparse sidebar still reads as a
            // framed grid (the original's fixed slot background).
            for (int col = 0; col < 2; col++)
                for (int ey = kSideTop; ey + kCameoH <= winH; ey += kCameoH + 4) {
                    int ex = viewW + 4 + col * (kCameoW + 4);
                    bevelPanel(wc, ex, ey, kCameoW, kCameoH, 0xff1a1a16, /*sunken=*/true);
                }
            drawStrip(visStructs, 0);
            drawStrip(visUnits, 1);

            // ---- Top tab bar: OPTIONS | power/credits | SIDEBAR ----
            bevelPanel(wc, 0, 0, winW, kTopBar, kFace);
            int tY = 2;
            if (hudFont) {
                int wOpt = game::textWidth(*hudFont, "OPTIONS") + 10;
                bevelPanel(wc, 1, 1, wOpt, kTopBar - 2, kFace);
                drawTextCentered(wc, hudFont, "OPTIONS", 1, wOpt, tY, kText);
                int wSide = game::textWidth(*hudFont, "SIDEBAR") + 10;
                bevelPanel(wc, winW - 1 - wSide, 1, wSide, kTopBar - 2, kFace);
                drawTextCentered(wc, hudFont, "SIDEBAR", winW - 1 - wSide, wSide, tY, kText);

                int produced = 0, drained = 0;
                sim.power(playerHouse, produced, drained);
                std::string cr = "$" + std::to_string(sim.credits(playerHouse));
                int crRight = viewW - 6;
                drawTextRight(wc, hudFont, cr, crRight, tY, kGreen);
                std::string pw = std::to_string(produced) + "/" + std::to_string(drained);
                uint32_t pc = produced >= drained ? kText : 0xffff5050;
                int pwRight = crRight - game::textWidth(*hudFont, cr) - 12;
                drawTextRight(wc, hudFont, pw, pwRight, tY, pc);
                drawTextRight(wc, hudFont, "PWR", pwRight - game::textWidth(*hudFont, pw) - 4,
                              tY, kText);
            }

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
            // One-frame screenshot of the real UI (sidebar included).
            if (const char* shot = strArg(argc, argv, "--ui-shot")) {
                SDL_SaveBMP(wsurf, shot);
                std::printf("wrote %s\n", shot);
                quit = true;
            }
            SDL_Delay(8);
        }
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
