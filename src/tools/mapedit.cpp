// mapedit — a level editor for the engine's custom-level format. Authors a map
// of a chosen size and theater and writes the .ini + .bin pair the game loads
// as a custom level (see MapFile::save / loadCustom).
//
//   mapedit <data-root> [--open level.ini] [--theater T] [--width W]
//           [--height H] [--out level.ini] [--shot out.bmp]
//
// <data-root> is a Tiberian Dawn asset root (e.g. data/assets/tiberian_dawn/gdi):
// theater art from <root>/<THEATER>/, unit/structure art from <root>/CONQUER/,
// palette + font from the theater dir / <root>/INSTALL/CCLOCAL.
//
// Custom levels are TD-flavored: TD template table, TD theaters, TD type lists,
// full 128-wide identity cell numbering. Grid is 128x128; the playable bounds
// (map.x/y/width/height) are outlined in the viewport.
//
// Controls: WASD/arrows/mouse-edge scroll · left-click paint selected palette
// item (terrain stamps its full footprint) · right-click erase cell · mouse
// wheel scroll palette · click a palette row to select · T/O switch category ·
// Ctrl+Z undo · Ctrl+Y redo · G toggle grid · Ctrl+S save · Esc quit.
//
// The SEA category (key 7) is a smart-coastline brush: left-drag paints a
// logical water field, right-drag erases it, and the correct shore/water tiles
// are auto-chosen per cell (marching squares over a dictionary harvested from
// the theater's shore art). [ and ] size the brush. The logical field is saved
// alongside the map in a .ini.mask sidecar so coastlines stay re-editable.

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/fnt.h"
#include "formats/palette.h"
#include "formats/sc_tileset.h"
#include "formats/shp.h"
#include "formats/tmp.h"
#include "game/house.h"
#include "game/map.h"
#include "game/render.h"
#include "game/rules.h"
#include "game/sc_isom.h"
#include "game/sc_render.h"
#include "game/td_template_table.h"

namespace {

constexpr int kTile = 24;    // on-screen cell size, pixels
constexpr int kPalW = 224;   // left palette panel width
constexpr int kBarH = 22;    // bottom status bar height
constexpr int kGrid = game::MapFile::kSize; // 128
constexpr int kCorners = kGrid + 1;         // coast water field is corner-based
constexpr int kPalTop = 150; // y where the palette list starts
constexpr int kPalRow = 22;  // palette row height
constexpr int kMaxUndo = 40; // document snapshots retained

// TD overlays offered in the Overlay palette: walls then tiberium.
const char* const kOverlayPalette[] = {
    "sbag", "cycl", "brik", "barb", "wood",
    "ti1",  "ti2",  "ti3",  "ti4",  "ti5",  "ti6",
    "ti7",  "ti8",  "ti9",  "ti10", "ti11", "ti12",
};

// TD type lists for the object palettes. Mirrors the kTd*Types tables in
// game_main.cpp; the REFACTOR_PLAN registry will eventually be the shared
// source (see docs/REFACTOR_PLAN.md #2).
const char* const kTdStructTypes[] = {
    "nuke", "nuk2", "proc", "silo", "pyle", "hand", "weap", "hq",  "fix",
    "hpad", "afld", "gun",  "gtwr", "atwr", "obli", "sam",  "tmpl", "eye",
    "brik", "sbag", "cycl", "fact",
};
const char* const kTdInfTypes[] = {"e1", "e2", "e3", "e4", "e5", "e6", "rmbo"};
const char* const kTdVehTypes[] = {
    "mcv",  "htnk", "mtnk", "ltnk", "ftnk", "stnk", "bggy",
    "jeep", "bike", "apc",  "arty", "msam", "harv", "mhq",
};

// Houses selectable for placed objects (cycled with H).
const char* const kHouses[] = {
    "GoodGuy", "BadGuy", "Neutral", "Special",
    "Multi1",  "Multi2", "Multi3",  "Multi4", "Multi5", "Multi6",
};

// Player-start waypoints offered in the Spawns palette (indices 0..7).
constexpr int kSpawnCount = 8;

// ---- CLI helpers ----
const char* strArg(int argc, char** argv, const char* name) {
    for (int i = 2; i < argc - 1; i++)
        if (std::strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return nullptr;
}
int intArg(int argc, char** argv, const char* name, int fallback) {
    const char* v = strArg(argc, argv, name);
    return v ? std::atoi(v) : fallback;
}
bool flagArg(int argc, char** argv, const char* name) {
    for (int i = 2; i < argc; i++)
        if (std::strcmp(argv[i], name) == 0)
            return true;
    return false;
}

std::string toUpper(std::string s) {
    for (auto& c : s)
        c = char(std::toupper((unsigned char)c));
    return s;
}

std::string theaterDirName(const std::string& theater) {
    if (theater == "DESERT")
        return "DESERT";
    if (theater == "WINTER")
        return "WINTER";
    return "TEMPERAT";
}

uint8_t theaterFlag(const std::string& theater) {
    if (theater == "DESERT")
        return game::kTdTheaterDesert;
    if (theater == "WINTER")
        return game::kTdTheaterWinter;
    return game::kTdTheaterTemperate;
}

// Caches theater TMP templates + conquer/theater SHP art (TD layout).
class ArtCache {
public:
    ArtCache(std::string theaterDir, std::string conquerDir, std::string ext)
        : theaterDir_(std::move(theaterDir)), conquerDir_(std::move(conquerDir)),
          ext_(std::move(ext)) {}

    const fmt::TmpFile* tmp(const std::string& name) {
        auto it = tmps_.find(name);
        if (it == tmps_.end()) {
            std::optional<fmt::TmpFile> v;
            std::string path = theaterDir_ + "/" + name + ext_;
            if (std::filesystem::exists(path)) {
                try {
                    v = fmt::TmpFile::load(path);
                } catch (const std::exception&) {
                }
            }
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
            if (std::filesystem::exists(path)) {
                try {
                    v = fmt::ShpFile::load(path);
                } catch (const std::exception&) {
                }
            }
            it = shps_.emplace(name, std::move(v)).first;
        }
        return it->second ? &*it->second : nullptr;
    }

private:
    std::string theaterDir_, conquerDir_, ext_;
    std::unordered_map<std::string, std::optional<fmt::TmpFile>> tmps_;
    std::unordered_map<std::string, std::optional<fmt::ShpFile>> shps_;
};

const char* templateArt(uint16_t id) {
    return id < game::kTdTemplateCount ? game::kTdTemplateTable[id].name : nullptr;
}

uint16_t findTemplateId(const char* name) {
    for (uint16_t id = 0; id < game::kTdTemplateCount; id++)
        if (game::kTdTemplateTable[id].name &&
            std::strcmp(game::kTdTemplateTable[id].name, name) == 0)
            return id;
    return 0xffff;
}

// ---- Smart coastline (auto-tiling) --------------------------------------
//
// The user paints a logical water field on the cell *corners*; each cell then
// looks at its 4 corners (a 4-bit marching-squares case) and is resolved to a
// concrete shore/water (template, icon). The 16-case dictionary is harvested
// automatically from the theater's shore art: we detect which palette indices
// are "water" (the ones unique to the fully-water w1/w2 tiles), then classify
// each shore icon's four corner quadrants as water/land to learn which case it
// depicts. No hand-authoring, no engine changes — cells still store the same
// (template, icon) the game already understands.

// bit0 = top-left, bit1 = top-right, bit2 = bottom-right, bit3 = bottom-left.
enum { kCornerTL = 1, kCornerTR = 2, kCornerBR = 4, kCornerBL = 8 };

struct CoastTile {
    uint16_t templateId = 0xffff;
    uint8_t icon = 0;
    bool valid = false;
};

struct CoastDict {
    std::array<CoastTile, 16> byCase{}; // case 1..14 (edges/corners)
    CoastTile deep;                     // case 15 (open water)
};

// Palette indices that appear in fully-water tiles but never in dry clear land.
std::array<bool, 256> waterIndices(ArtCache& art) {
    std::array<int, 256> inWater{}, inClear{};
    auto tally = [](std::array<int, 256>& hist, const fmt::TmpFile* t) {
        if (!t)
            return;
        for (const auto& tile : t->tiles)
            for (uint8_t px : tile)
                hist[px]++;
    };
    tally(inWater, art.tmp("w1"));
    tally(inWater, art.tmp("w2"));
    tally(inClear, art.tmp("clear1"));
    std::array<bool, 256> water{};
    for (int i = 0; i < 256; i++)
        water[i] = inWater[i] > 0 && inClear[i] == 0;
    return water;
}

// Fraction of pixels in one quadrant of a 24x24 tile that are water-colored.
float cornerWetness(const std::vector<uint8_t>& tile, int w, int h,
                    const std::array<bool, 256>& water, bool right, bool bottom) {
    int x0 = right ? w / 2 : 0, x1 = right ? w : w / 2;
    int y0 = bottom ? h / 2 : 0, y1 = bottom ? h : h / 2;
    int wet = 0, total = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++, total++)
            if (water[tile[y * w + x]])
                wet++;
    return total ? float(wet) / float(total) : 0.0f;
}

int popcount4(int c); // defined below

// Harvest the 16-case dictionary from the theater's shore/water templates.
CoastDict buildCoastDict(ArtCache& art, uint8_t flag) {
    CoastDict dict;
    auto water = waterIndices(art);

    // For each case, keep the most "decisive" matching icon (corners closest to
    // pure water or pure land — avoids picking a muddy transitional tile).
    std::array<float, 16> best{};
    best.fill(-1.0f);

    for (uint16_t id = 0; id < game::kTdTemplateCount; id++) {
        const auto& info = game::kTdTemplateTable[id];
        if (!(info.theaters & flag) || !info.name)
            continue;
        bool isShore = info.name[0] == 's' && info.name[1] == 'h';
        bool isWater = info.name[0] == 'w';
        if (!isShore && !isWater)
            continue;
        const fmt::TmpFile* t = art.tmp(info.name);
        if (!t || t->tiles.empty())
            continue;
        for (int icon = 0; icon < int(t->tiles.size()); icon++) {
            const auto& tile = t->tiles[icon];
            if (tile.empty())
                continue;
            float fTL = cornerWetness(tile, t->tileWidth, t->tileHeight, water, false, false);
            float fTR = cornerWetness(tile, t->tileWidth, t->tileHeight, water, true, false);
            float fBR = cornerWetness(tile, t->tileWidth, t->tileHeight, water, true, true);
            float fBL = cornerWetness(tile, t->tileWidth, t->tileHeight, water, false, true);
            int c = (fTL >= 0.5f ? kCornerTL : 0) | (fTR >= 0.5f ? kCornerTR : 0) |
                    (fBR >= 0.5f ? kCornerBR : 0) | (fBL >= 0.5f ? kCornerBL : 0);
            if (c == 0)
                continue; // pure land — not a coast tile

            // Prefer a consistent sandy-beach coast: bias toward icons whose dry
            // land is Beach and away from rocky/rough shore, so the auto-tiler
            // doesn't mix sand and rock along one shoreline.
            uint8_t landType = info.land;
            if (info.altIcons)
                for (const int8_t* p = info.altIcons; *p != -1; ++p)
                    if (*p == icon) {
                        landType = info.altLand;
                        break;
                    }
            float bias = 0.0f;
            if (landType == uint8_t(game::Land::Beach))
                bias += 2.0f;
            else if (landType == uint8_t(game::Land::Rock) ||
                     landType == uint8_t(game::Land::Rough))
                bias -= 2.0f;

            float score = std::abs(fTL - 0.5f) + std::abs(fTR - 0.5f) +
                          std::abs(fBR - 0.5f) + std::abs(fBL - 0.5f) + bias;
            if (c == 15) {
                if (score > best[15]) {
                    best[15] = score;
                    dict.deep = {id, uint8_t(icon), true};
                }
                continue;
            }
            if (score > best[c]) {
                best[c] = score;
                dict.byCase[c] = {id, uint8_t(icon), true};
            }
        }
    }
    if (!dict.deep.valid) {
        uint16_t w1 = findTemplateId("w1");
        if (w1 != 0xffff)
            dict.deep = {w1, 0, true};
    }
    auto nativeCases = dict.byCase; // snapshot so fallbacks don't chain
    int nativeCount = 0;
    for (int c = 1; c < 15; c++)
        nativeCount += dict.byCase[c].valid ? 1 : 0;

    // Some corner orientations simply don't exist as isolated tiles in TD's
    // shore art (e.g. temperate has no single TR/BR water-corner tile). Fill
    // those cases from the smallest "superset" case that does have a tile — a
    // missing convex corner borrows an adjacent edge, so the coast keeps a
    // continuous beach instead of an abrupt land/water step. Never picks a
    // tile that lacks one of the case's water corners, so orientation stays sane.
    for (int c = 1; c < 15; c++) {
        if (dict.byCase[c].valid)
            continue;
        CoastTile chosen;
        int bestExtra = 99;
        for (int s = 1; s < 15; s++) {
            if (!nativeCases[s].valid || (s & c) != c)
                continue; // s must contain all of c's water corners
            int extra = popcount4(s) - popcount4(c);
            if (extra < bestExtra) {
                bestExtra = extra;
                chosen = nativeCases[s];
            }
        }
        if (!chosen.valid && popcount4(c) >= 2)
            chosen = dict.deep; // concave with no match: round up to open water
        dict.byCase[c] = chosen;
    }

    // Coverage report: native tile per case (a * marks a fallback-filled case).
    std::printf("coast dict: ");
    for (int c = 1; c < 15; c++) {
        const auto& t = dict.byCase[c];
        if (t.valid)
            std::printf("[%X:%s#%d%s] ", c, templateArt(t.templateId), t.icon,
                        nativeCases[c].valid ? "" : "*");
        else
            std::printf("[%X:--] ", c);
    }
    std::printf("| deep:%s  (%d/14 native)\n",
                dict.deep.valid ? templateArt(dict.deep.templateId) : "none", nativeCount);
    return dict;
}

// A placed object resolved to art + draw position (world pixels).
struct DrawObject {
    const fmt::ShpFile* shp = nullptr;
    int frame = 0;
    int turretFrame = -1;
    int x = 0, y = 0;
    const game::RemapTable* remap = nullptr;
    int health = 256;
};

bool hasTurret(const std::string& type) {
    static const char* kTurreted[] = {"htnk", "mtnk", "ltnk", "ftnk", "stnk",
                                      "jeep", "bggy", "arty", "msam"};
    for (const char* t : kTurreted)
        if (type == t)
            return true;
    return false;
}

void drawObject(game::Canvas& c, const DrawObject& o, const fmt::Palette& pal,
                int offX, int offY) {
    if (!o.shp || o.frame >= int(o.shp->frames.size()))
        return;
    game::BlitOptions opts;
    opts.colorKey = true;
    opts.shadow = true;
    opts.remap = o.remap;
    blitIndexed(c, o.shp->frames[o.frame].data(), o.shp->width, o.shp->height,
                o.x - offX, o.y - offY, pal, opts);
    if (o.turretFrame >= 0 && o.turretFrame < int(o.shp->frames.size()))
        blitIndexed(c, o.shp->frames[o.turretFrame].data(), o.shp->width,
                    o.shp->height, o.x - offX, o.y - offY, pal, opts);
}

// ---- Palette ----
enum class Category { Terrain, Overlay, Structures, Units, Infantry, Spawns, Coast };
constexpr int kNumCat = 7;
const char* const kCatTab[kNumCat] = {"TER", "OVL", "STR", "VEH", "INF", "SPN", "SEA"};

struct PalEntry {
    int id = 0;        // terrain: templateId; overlay: index; spawn: waypoint idx
    std::string name;  // art base name / type code / "spawn N"
    int w = 1, h = 1;  // footprint in cells (terrain multi-cell templates)
};

bool isObjectCat(Category c) {
    return c == Category::Structures || c == Category::Units || c == Category::Infantry;
}

// ---- Editor document + state ----
struct Editor {
    game::MapFile map;
    std::string outPath;
    bool dirty = false;
    bool showGrid = true;

    float camX = 0, camY = 0; // baked-surface pixels

    Category cat = Category::Terrain;
    std::vector<PalEntry> palettes[kNumCat];
    int selIdx[kNumCat] = {0};
    int palScroll = 0;  // first visible palette row
    int houseIdx = 0;   // index into kHouses for placed objects
    bool objectsDirty = false; // rebuild the DrawObject list
    std::string saveNote;      // shown in the status bar after a save

    // Smart-coastline state. `water` is the logical field on cell corners
    // ((kGrid+1)^2, 1=water); `coastManaged` marks cells the coast resolver owns
    // so removing water can revert only them back to clear.
    std::vector<uint8_t> water;
    std::vector<uint8_t> coastManaged;
    int brush = 2; // coast brush radius, in corners
    CoastDict coast;

    // Undo/redo bundle the map with the coast mask so they never desync.
    struct Snapshot {
        game::MapFile map;
        std::vector<uint8_t> water, coastManaged;
    };
    std::vector<Snapshot> undo, redo;

    std::vector<PalEntry>& pal() { return palettes[int(cat)]; }
    int& sel() { return selIdx[int(cat)]; }
    std::string house() const { return kHouses[houseIdx]; }
    std::vector<PalEntry>& terrainPal() { return palettes[int(Category::Terrain)]; }

    Snapshot current() const { return {map, water, coastManaged}; }
    void restore(Snapshot& s) {
        map = std::move(s.map);
        water = std::move(s.water);
        coastManaged = std::move(s.coastManaged);
        dirty = true;
    }
    void snapshot() {
        undo.push_back(current());
        if (int(undo.size()) > kMaxUndo)
            undo.erase(undo.begin());
        redo.clear();
    }
    void doUndo() {
        if (undo.empty())
            return;
        redo.push_back(current());
        restore(undo.back());
        undo.pop_back();
    }
    void doRedo() {
        if (redo.empty())
            return;
        undo.push_back(current());
        restore(redo.back());
        redo.pop_back();
    }
};

game::MapFile blankLevel(const std::string& theater, int w, int h) {
    game::MapFile m;
    m.game = game::Game::TiberianDawn;
    m.theater = theater;
    m.cells.assign(size_t(kGrid) * kGrid, {});
    w = std::clamp(w, 16, kGrid - 4);
    h = std::clamp(h, 16, kGrid - 4);
    m.x = (kGrid - w) / 2;
    m.y = (kGrid - h) / 2;
    m.width = w;
    m.height = h;
    return m;
}

// Templates available in this theater whose art loads, for the Terrain palette.
std::vector<PalEntry> buildTerrainPalette(ArtCache& art, uint8_t flag) {
    std::vector<PalEntry> out;
    for (uint16_t id = 0; id < game::kTdTemplateCount; id++) {
        const auto& t = game::kTdTemplateTable[id];
        if (!(t.theaters & flag) || !t.name || !t.name[0])
            continue;
        const fmt::TmpFile* tmp = art.tmp(t.name);
        if (!tmp || tmp->tiles.empty())
            continue;
        out.push_back({id, t.name, std::max<int>(1, t.width), std::max<int>(1, t.height)});
    }
    return out;
}

std::vector<PalEntry> buildOverlayPalette(ArtCache& art) {
    std::vector<PalEntry> out;
    for (int i = 0; i < int(std::size(kOverlayPalette)); i++) {
        const fmt::ShpFile* s = art.shp(kOverlayPalette[i]);
        if (!s || s->frames.empty())
            continue;
        out.push_back({i, kOverlayPalette[i], 1, 1});
    }
    return out;
}

// Object types whose SHP art loads, for a Structures/Units/Infantry palette.
std::vector<PalEntry> buildObjectPalette(ArtCache& art, const char* const* types,
                                         int n) {
    std::vector<PalEntry> out;
    for (int i = 0; i < n; i++) {
        const fmt::ShpFile* s = art.shp(types[i]);
        if (!s || s->frames.empty())
            continue;
        out.push_back({i, types[i], 1, 1});
    }
    return out;
}

std::vector<PalEntry> buildSpawnPalette() {
    std::vector<PalEntry> out;
    for (int i = 0; i < kSpawnCount; i++)
        out.push_back({i, "spawn " + std::to_string(i), 1, 1});
    return out;
}

// First non-empty tile index of a template (for previews / fallback).
int firstTile(const fmt::TmpFile& t) {
    for (int i = 0; i < int(t.tiles.size()); i++)
        if (!t.tiles[i].empty())
            return i;
    return 0;
}

// Bake terrain + TD overlays + terrain objects into `mc` (kGrid*kTile square).
void bakeTerrain(game::Canvas& mc, const game::MapFile& map, ArtCache& art,
                 const fmt::Palette& pal) {
    const fmt::TmpFile* clear = art.tmp("clear1");
    for (int cy = 0; cy < kGrid; cy++) {
        for (int cx = 0; cx < kGrid; cx++) {
            const auto& cell = map.cells[cy * kGrid + cx];
            int dx = cx * kTile, dy = cy * kTile;
            const fmt::TmpFile* t = nullptr;
            int icon = 0;
            const char* tname =
                cell.templateId != 0xffff ? templateArt(cell.templateId) : nullptr;
            if (tname && tname[0]) {
                t = art.tmp(tname);
                icon = cell.icon;
                if (!t) {
                    game::fillRect(mc, dx, dy, kTile, kTile, 0xffff00ff);
                    continue;
                }
            }
            if (t && (icon >= int(t->tiles.size()) || t->tiles[icon].empty()))
                t = nullptr;
            if (!t) {
                t = clear;
                icon = (cx & 3) | ((cy & 3) << 2); // Clear_Icon()
            }
            if (t && icon < int(t->tiles.size()) && !t->tiles[icon].empty())
                blitIndexed(mc, t->tiles[icon].data(), t->tileWidth, t->tileHeight,
                            dx, dy, pal);
            else
                game::fillRect(mc, dx, dy, kTile, kTile, 0xff204020);
        }
    }
    // TD overlay list (frame 0) then terrain objects (trees/rocks).
    game::BlitOptions opts;
    opts.colorKey = true;
    opts.shadow = true;
    for (const auto& ov : map.tdOverlay) {
        const fmt::ShpFile* s = art.shp(ov.name);
        if (!s || s->frames.empty())
            continue;
        blitIndexed(mc, s->frames[0].data(), s->width, s->height,
                    (ov.cell % kGrid) * kTile, (ov.cell / kGrid) * kTile, pal, opts);
    }
    for (const auto& obj : map.terrain) {
        const fmt::ShpFile* s = art.shp(obj.name);
        if (!s || s->frames.empty())
            continue;
        blitIndexed(mc, s->frames[0].data(), s->width, s->height,
                    (obj.cell % kGrid) * kTile, (obj.cell / kGrid) * kTile, pal, opts);
    }
}

std::vector<DrawObject> resolveObjects(
    const game::MapFile& map, ArtCache& art,
    const std::function<const game::RemapTable*(const std::string&)>& remapFor) {
    std::vector<DrawObject> out;
    auto cellPx = [](int cell) {
        return std::pair<int, int>{(cell % kGrid) * kTile, (cell / kGrid) * kTile};
    };
    for (const auto& s : map.structures) {
        const fmt::ShpFile* shp = art.shp(s.type);
        if (!shp || shp->frames.empty())
            continue;
        DrawObject o;
        o.shp = shp;
        auto [px, py] = cellPx(s.cell);
        o.x = px;
        o.y = py;
        o.remap = remapFor(s.house);
        o.health = s.health;
        out.push_back(o);
        if (const fmt::ShpFile* top = art.shp(s.type + "2")) {
            DrawObject o2 = o;
            o2.shp = top;
            out.push_back(o2);
        }
    }
    for (const auto& u : map.units) {
        const fmt::ShpFile* shp = art.shp(u.type);
        if (!shp || shp->frames.empty())
            continue;
        DrawObject o;
        o.shp = shp;
        o.frame = game::facingToFrame(u.facing) % int(shp->frames.size());
        if (hasTurret(u.type) && shp->frames.size() >= 64)
            o.turretFrame = 32 + game::facingToFrame(u.facing);
        auto [px, py] = cellPx(u.cell);
        o.x = px + kTile / 2 - shp->width / 2;
        o.y = py + kTile / 2 - shp->height / 2;
        o.remap = remapFor(u.house);
        o.health = u.health;
        out.push_back(o);
    }
    for (const auto& inf : map.infantry) {
        const fmt::ShpFile* shp = art.shp(inf.type);
        if (!shp || shp->frames.empty())
            continue;
        static const int kSubX[5] = {12, 6, 18, 6, 18};
        static const int kSubY[5] = {12, 6, 6, 18, 18};
        int sub = std::clamp(inf.subcell, 0, 4);
        DrawObject o;
        o.shp = shp;
        o.frame = (8 - ((inf.facing & 0xff) >> 5)) & 7;
        auto [px, py] = cellPx(inf.cell);
        o.x = px + kSubX[sub] - shp->width / 2;
        o.y = py + kSubY[sub] - shp->height / 2;
        o.remap = remapFor(inf.house);
        o.health = inf.health;
        out.push_back(o);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const DrawObject& a, const DrawObject& b) { return a.y < b.y; });
    return out;
}

// Non-blocking playability checks; returned strings are shown on save.
std::vector<std::string> validate(const game::MapFile& m) {
    std::vector<std::string> warn;
    if (m.width < 8 || m.height < 8)
        warn.push_back("playable bounds are very small");
    if (m.x + m.width > kGrid || m.y + m.height > kGrid)
        warn.push_back("playable bounds exceed the 128x128 grid");
    if (m.structures.empty() && m.units.empty() && m.infantry.empty())
        warn.push_back("no objects placed (map has no units/buildings)");
    int spawns = 0;
    for (int c : m.waypoints)
        if (c >= 0)
            spawns++;
    if (spawns == 0)
        warn.push_back("no spawn waypoints set (needed for player starts)");
    // Objects outside the playable rectangle will be unreachable/odd.
    auto outside = [&](int cell) {
        int cx = cell % kGrid, cy = cell / kGrid;
        return cx < m.x || cx >= m.x + m.width || cy < m.y || cy >= m.y + m.height;
    };
    int stray = 0;
    for (const auto* v : {&m.structures, &m.units, &m.infantry})
        for (const auto& o : *v)
            if (outside(o.cell))
                stray++;
    if (stray)
        warn.push_back(std::to_string(stray) + " object(s) outside playable bounds");
    return warn;
}

// ---- Editing operations ----
const PalEntry* selEntry(Editor& ed) {
    auto& list = ed.pal();
    if (list.empty())
        return nullptr;
    return &list[std::clamp(ed.sel(), 0, int(list.size()) - 1)];
}

// Editor-only water mask, stored next to the .ini so coastlines stay editable.
// The .bin the game loads already holds the resolved tiles; this just preserves
// the logical field the resolver was driven from.
std::string maskPath(const std::string& iniPath) { return iniPath + ".mask"; }

void saveMask(const Editor& ed) {
    bool any = false;
    for (uint8_t w : ed.water)
        if (w) {
            any = true;
            break;
        }
    std::string path = maskPath(ed.outPath);
    if (!any) {
        std::remove(path.c_str()); // don't leave a stale mask for a drained map
        return;
    }
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        uint32_t dim = kCorners;
        std::fwrite("CMSK", 1, 4, f);
        std::fwrite(&dim, sizeof dim, 1, f);
        std::fwrite(ed.water.data(), 1, ed.water.size(), f);
        std::fclose(f);
    }
}

int coastCaseAt(const Editor& ed, int cx, int cy); // defined below

// Load the sidecar mask (if any) and mark the cells it owns. Cells were already
// restored from the .bin, so we only rebuild the logical field + managed flags.
void loadMask(Editor& ed, const std::string& iniPath) {
    FILE* f = std::fopen(maskPath(iniPath).c_str(), "rb");
    if (!f)
        return;
    char magic[4];
    uint32_t dim = 0;
    if (std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "CMSK", 4) == 0 &&
        std::fread(&dim, sizeof dim, 1, f) == 1 && dim == uint32_t(kCorners)) {
        std::fread(ed.water.data(), 1, ed.water.size(), f);
        for (int cy = 0; cy < kGrid; cy++)
            for (int cx = 0; cx < kGrid; cx++)
                if (coastCaseAt(ed, cx, cy))
                    ed.coastManaged[cy * kGrid + cx] = 1;
    }
    std::fclose(f);
}

// Save + validate, reporting warnings to the console and the status bar.
void saveMap(Editor& ed) {
    ed.map.save(ed.outPath);
    saveMask(ed);
    ed.dirty = false;
    auto warns = validate(ed.map);
    std::printf("saved %s (+ .bin)\n", ed.outPath.c_str());
    for (const auto& w : warns)
        std::printf("  warning: %s\n", w.c_str());
    ed.saveNote = warns.empty() ? "saved OK"
                                : "saved, " + std::to_string(warns.size()) + " warning(s)";
}

// Stamp the selected terrain template's full footprint at (cx,cy).
void paintTerrain(Editor& ed, int cx, int cy) {
    const PalEntry* e = selEntry(ed);
    if (!e)
        return;
    for (int dy = 0; dy < e->h; dy++) {
        for (int dx = 0; dx < e->w; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= kGrid || y < 0 || y >= kGrid)
                continue;
            auto& cell = ed.map.cells[y * kGrid + x];
            cell.templateId = uint16_t(e->id);
            cell.icon = uint8_t(dy * e->w + dx);
        }
    }
    ed.dirty = true;
}

void paintOverlay(Editor& ed, int cx, int cy) {
    const PalEntry* e = selEntry(ed);
    if (!e || cx < 0 || cx >= kGrid || cy < 0 || cy >= kGrid)
        return;
    int cell = cy * kGrid + cx;
    for (auto& ov : ed.map.tdOverlay)
        if (ov.cell == cell) {
            ov.name = e->name;
            ed.dirty = true;
            return;
        }
    ed.map.tdOverlay.push_back({cell, e->name});
    ed.dirty = true;
}

// ---- Smart coastline painting ----
int coastCaseAt(const Editor& ed, int cx, int cy) {
    auto w = [&](int x, int y) { return ed.water[y * kCorners + x] ? 1 : 0; };
    return (w(cx, cy) ? kCornerTL : 0) | (w(cx + 1, cy) ? kCornerTR : 0) |
           (w(cx + 1, cy + 1) ? kCornerBR : 0) | (w(cx, cy + 1) ? kCornerBL : 0);
}

int popcount4(int c) { return (c & 1) + ((c >> 1) & 1) + ((c >> 2) & 1) + ((c >> 3) & 1); }

// Re-resolve every cell from the water mask. Cheap enough (128^2) to run on
// each edit. Only cells the tool owns are reverted when their water is removed;
// hand-painted terrain elsewhere is untouched.
void resolveCoast(Editor& ed) {
    auto setCell = [&](int cell, uint16_t tid, uint8_t icon) {
        ed.map.cells[cell].templateId = tid;
        ed.map.cells[cell].icon = icon;
        ed.coastManaged[cell] = 1;
    };
    auto clearCell = [&](int cell) {
        if (ed.coastManaged[cell]) {
            ed.map.cells[cell].templateId = 0xffff;
            ed.map.cells[cell].icon = 0;
            ed.coastManaged[cell] = 0;
        }
    };
    for (int cy = 0; cy < kGrid; cy++) {
        for (int cx = 0; cx < kGrid; cx++) {
            int cell = cy * kGrid + cx;
            int c = coastCaseAt(ed, cx, cy);
            if (c == 0) {
                clearCell(cell);
                continue;
            }
            CoastTile t = c == 15 ? ed.coast.deep : ed.coast.byCase[c];
            if (t.valid) {
                setCell(cell, t.templateId, t.icon);
            } else if (popcount4(c) >= 3 && ed.coast.deep.valid) {
                // No native tile for this concave corner: round up to open water
                // (loses a tiny land nub) rather than stamp a wrong-facing tile.
                setCell(cell, ed.coast.deep.templateId, ed.coast.deep.icon);
            } else {
                // Sparse water with no tile: round down to land.
                clearCell(cell);
            }
        }
    }
    ed.dirty = true;
}

// Paint (or erase) water in a circular brush around a corner, then re-resolve.
void paintWater(Editor& ed, int cornerX, int cornerY, bool set) {
    int r = ed.brush;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r)
                continue;
            int x = cornerX + dx, y = cornerY + dy;
            if (x < 0 || x >= kCorners || y < 0 || y >= kCorners)
                continue;
            ed.water[y * kCorners + x] = set ? 1 : 0;
        }
    resolveCoast(ed);
}

// Place a structure/unit/infantry of the selected type + current house.
void placeObject(Editor& ed, int cx, int cy) {
    const PalEntry* e = selEntry(ed);
    if (!e || cx < 0 || cx >= kGrid || cy < 0 || cy >= kGrid)
        return;
    game::MapFile::Object o;
    o.house = ed.house();
    o.type = e->name;
    o.health = 256;
    o.cell = cy * kGrid + cx;
    o.facing = 0;
    if (ed.cat == Category::Structures)
        ed.map.structures.push_back(o);
    else if (ed.cat == Category::Units)
        ed.map.units.push_back(o);
    else {
        o.subcell = 0; // center
        ed.map.infantry.push_back(o);
    }
    ed.dirty = true;
    ed.objectsDirty = true;
}

// Set the selected player-start waypoint to (cx,cy).
void placeSpawn(Editor& ed, int cx, int cy) {
    const PalEntry* e = selEntry(ed);
    if (!e || cx < 0 || cx >= kGrid || cy < 0 || cy >= kGrid)
        return;
    int idx = e->id;
    if (idx >= int(ed.map.waypoints.size()))
        ed.map.waypoints.resize(idx + 1, -1);
    ed.map.waypoints[idx] = cy * kGrid + cx;
    ed.dirty = true;
}

// Remove the topmost object at (cx,cy). Returns true if one was removed.
bool deleteObjectAt(Editor& ed, int cx, int cy) {
    int cell = cy * kGrid + cx;
    for (auto* vec : {&ed.map.infantry, &ed.map.units, &ed.map.structures}) {
        for (auto it = vec->begin(); it != vec->end(); ++it)
            if (it->cell == cell) {
                vec->erase(it);
                ed.dirty = true;
                ed.objectsDirty = true;
                return true;
            }
    }
    return false;
}

// Right-click erase, dispatched by category.
void eraseAt(Editor& ed, int cx, int cy) {
    if (cx < 0 || cx >= kGrid || cy < 0 || cy >= kGrid)
        return;
    int cell = cy * kGrid + cx;
    if (isObjectCat(ed.cat)) {
        deleteObjectAt(ed, cx, cy);
        return;
    }
    if (ed.cat == Category::Spawns) {
        for (auto& w : ed.map.waypoints)
            if (w == cell)
                w = -1;
        ed.dirty = true;
        return;
    }
    // Terrain / Overlay: clear the cell and drop any overlay there.
    auto& c = ed.map.cells[cell];
    c.templateId = 0xffff;
    c.icon = 0;
    auto& ov = ed.map.tdOverlay;
    ov.erase(std::remove_if(ov.begin(), ov.end(),
                            [&](const game::MapFile::TerrainObject& o) { return o.cell == cell; }),
             ov.end());
    ed.dirty = true;
}

// Draw a scaled preview of a palette entry into a small box at (x,y,box).
void drawPalPreview(game::Canvas& wc, const PalEntry& e, Category cat, ArtCache& art,
                    const fmt::Palette& pal, int x, int y, int box) {
    if (cat == Category::Terrain) {
        const fmt::TmpFile* t = art.tmp(e.name);
        if (t && !t->tiles.empty()) {
            int idx = firstTile(*t);
            blitIndexedScaled(wc, t->tiles[idx].data(), t->tileWidth, t->tileHeight, x,
                              y, box, box, pal);
        }
    } else {
        const fmt::ShpFile* s = art.shp(e.name);
        if (s && !s->frames.empty()) {
            game::BlitOptions opts;
            opts.colorKey = true;
            blitIndexedScaled(wc, s->frames[0].data(), s->width, s->height, x, y, box,
                              box, pal, opts);
        }
    }
}

// Draw one editor frame to `dst`. Terrain must already be baked into `mapSurf`.
void renderEditor(SDL_Surface* dst, Editor& ed, SDL_Surface* mapSurf, int gridPx,
                  const std::vector<DrawObject>& objects, const fmt::Palette& pal,
                  const std::optional<fmt::FntFile>& font, ArtCache& art,
                  int hoverCellX, int hoverCellY, bool inView, int mouseX = -1,
                  int mouseY = -1) {
    int winW = dst->w, winH = dst->h;
    int vx = kPalW, vy = 0, vw = winW - kPalW, vh = winH - kBarH;
    game::Canvas wc = game::Canvas::wrap(dst);
    game::fillRect(wc, 0, 0, winW, winH, 0xff101418);

    // Map viewport: blit visible baked terrain.
    SDL_Rect srcR{int(ed.camX), int(ed.camY), std::min(vw, gridPx - int(ed.camX)),
                  std::min(vh, gridPx - int(ed.camY))};
    SDL_Rect dstR{vx, vy, srcR.w, srcR.h};
    SDL_BlitSurface(mapSurf, &srcR, dst, &dstR);

    wc.clipY0 = vy;
    wc.clipY1 = vy + vh;
    int offX = int(ed.camX) - vx, offY = int(ed.camY) - vy;
    for (const auto& o : objects)
        drawObject(wc, o, pal, offX, offY);
    wc.clipY0 = 0;
    wc.clipY1 = 1 << 30;

    if (ed.showGrid) {
        for (int gx = 0; gx <= kGrid; gx++) {
            int sx = vx + gx * kTile - int(ed.camX);
            if (sx >= vx && sx < vx + vw)
                game::fillRect(wc, sx, vy, 1, vh, 0x22ffffff);
        }
        for (int gy = 0; gy <= kGrid; gy++) {
            int sy = vy + gy * kTile - int(ed.camY);
            if (sy >= vy && sy < vy + vh)
                game::fillRect(wc, vx, sy, vw, 1, 0x22ffffff);
        }
    }

    // Playable-bounds outline.
    {
        int bx = vx + ed.map.x * kTile - int(ed.camX);
        int by = vy + ed.map.y * kTile - int(ed.camY);
        game::drawRect(wc, bx, by, ed.map.width * kTile, ed.map.height * kTile, 0xffffcc00);
    }

    // Waypoint/spawn markers.
    if (font)
        for (size_t i = 0; i < ed.map.waypoints.size() && i < size_t(kSpawnCount); i++) {
            int cell = ed.map.waypoints[i];
            if (cell < 0)
                continue;
            int wxp = vx + (cell % kGrid) * kTile - int(ed.camX);
            int wyp = vy + (cell / kGrid) * kTile - int(ed.camY);
            game::fillRect(wc, wxp + 2, wyp + 2, kTile - 4, kTile - 4, 0x9900aaff);
            game::drawRect(wc, wxp + 2, wyp + 2, kTile - 4, kTile - 4, 0xff00ccff);
            game::drawText(wc, *font, std::to_string(i), wxp + 8, wyp + 6, 0xffffffff);
        }

    // Hover highlight: the selected template's footprint (terrain) or one cell.
    if (inView && hoverCellX >= 0 && hoverCellX < kGrid && hoverCellY >= 0 &&
        hoverCellY < kGrid) {
        int bw = 1, bh = 1;
        if (ed.cat == Category::Terrain) {
            const PalEntry* e = selEntry(ed);
            if (e) {
                bw = e->w;
                bh = e->h;
            }
        }
        int hx = vx + hoverCellX * kTile - int(ed.camX);
        int hy = vy + hoverCellY * kTile - int(ed.camY);
        game::drawRect(wc, hx, hy, kTile * bw, kTile * bh, 0xff00ff88);
    }

    // ---- Coast tool: water-mask overlay + brush cursor ----
    if (ed.cat == Category::Coast) {
        wc.clipY0 = vy;
        wc.clipY1 = vy + vh;
        // Water corners as small cyan diamonds over the baked art.
        if (!ed.water.empty())
            for (int cy = 0; cy <= kGrid; cy++)
                for (int cx = 0; cx <= kGrid; cx++) {
                    if (!ed.water[cy * kCorners + cx])
                        continue;
                    int sx = vx + cx * kTile - int(ed.camX);
                    int sy = vy + cy * kTile - int(ed.camY);
                    if (sx < vx - 4 || sx > vx + vw || sy < vy - 4 || sy > vy + vh)
                        continue;
                    game::fillRect(wc, sx - 2, sy - 2, 5, 5, 0xaa33ccff);
                }
        // Brush ring centered on the corner under the mouse.
        if (inView && mouseX >= 0) {
            int px = int(ed.camX) + (mouseX - vx), py = int(ed.camY) + (mouseY - vy);
            int cornerX = (px + kTile / 2) / kTile, cornerY = (py + kTile / 2) / kTile;
            int sx = vx + cornerX * kTile - int(ed.camX);
            int sy = vy + cornerY * kTile - int(ed.camY);
            int r = (ed.brush + 1) * kTile;
            game::drawRect(wc, sx - r, sy - r, 2 * r, 2 * r, 0xff00ff88);
        }
        wc.clipY0 = 0;
        wc.clipY1 = 1 << 30;
    }

    // ---- Palette panel ----
    game::fillRect(wc, 0, 0, kPalW, winH, 0xff181c22);
    game::fillRect(wc, kPalW - 1, 0, 1, winH, 0xff000000);
    uint32_t kText = 0xffd8e0e8, kDim = 0xff8090a0;
    if (font) {
        game::drawText(wc, *font, "MAPEDIT", 10, 8, 0xffffcc00);
        game::drawText(wc, *font, "theater: " + ed.map.theater, 10, 26, kText);
        char sz[64];
        std::snprintf(sz, sizeof sz, "size: %dx%d", ed.map.width, ed.map.height);
        game::drawText(wc, *font, sz, 10, 40, kText);

        // Category tabs (keys 1-6). Rendered as a row of short labels.
        int tx = 8;
        for (int c = 0; c < kNumCat; c++) {
            int w = game::textWidth(*font, kCatTab[c]) + 8;
            bool on = int(ed.cat) == c;
            game::fillRect(wc, tx, 58, w, 16, on ? 0xff3a6b8a : 0xff232a33);
            game::drawText(wc, *font, kCatTab[c], tx + 4, 60, on ? 0xffffffff : kDim);
            tx += w + 2;
        }

        // House selector (only meaningful for object categories).
        if (isObjectCat(ed.cat) || ed.cat == Category::Spawns)
            game::drawText(wc, *font, "house: " + ed.house() + "  [H]", 10, 80,
                           0xffffcc00);
        if (ed.cat == Category::Coast) {
            char b[64];
            std::snprintf(b, sizeof b, "brush: %d   [ / ] size", ed.brush);
            game::drawText(wc, *font, b, 10, 80, 0xffffcc00);
            game::drawText(wc, *font, "L water   R land", 10, 100, kText);
            game::drawText(wc, *font, "paint the sea; shore", 10, 118, kDim);
            game::drawText(wc, *font, "tiles are auto-chosen", 10, 131, kDim);
        }
    }

    // Palette list (only visible rows).
    auto& list = ed.pal();
    int rowsVisible = (winH - kPalTop - 96) / kPalRow;
    for (int r = 0; r < rowsVisible; r++) {
        int i = ed.palScroll + r;
        if (i < 0 || i >= int(list.size()))
            break;
        int y = kPalTop + r * kPalRow;
        bool selected = i == ed.sel();
        if (selected)
            game::fillRect(wc, 4, y - 2, kPalW - 8, kPalRow, 0xff2f5b7a);
        if (ed.cat != Category::Spawns)
            drawPalPreview(wc, list[i], ed.cat, art, pal, 8, y - 1, 18);
        if (font) {
            std::string nm = list[i].name;
            if (ed.cat == Category::Terrain && (list[i].w > 1 || list[i].h > 1)) {
                char sfx[16];
                std::snprintf(sfx, sizeof sfx, " %dx%d", list[i].w, list[i].h);
                nm += sfx;
            }
            game::drawText(wc, *font, nm, 32, y + 2, selected ? 0xffffffff : kText);
        }
    }

    // Controls hint (bottom of panel).
    if (font) {
        int hy = winH - 80;
        game::drawText(wc, *font, "1-7 category  H house", 10, hy, kDim);
        game::drawText(wc, *font, "L place  R erase", 10, hy + 13, kDim);
        game::drawText(wc, *font, "Ctrl+Z/Y undo/redo", 10, hy + 26, kDim);
        game::drawText(wc, *font, "G grid  Ctrl+S save", 10, hy + 39, kDim);
        game::drawText(wc, *font, "Esc quit", 10, hy + 52, kDim);
    }

    // ---- Status bar ----
    game::fillRect(wc, kPalW, winH - kBarH, winW - kPalW, kBarH, 0xff20262e);
    if (font) {
        const char* selName = "-";
        if (!ed.pal().empty())
            selName = ed.pal()[std::clamp(ed.sel(), 0, int(ed.pal().size()) - 1)].name.c_str();
        char st[260];
        std::snprintf(st, sizeof st, "cell %d,%d  sel:%s  %s%s  out:%s",
                      hoverCellX < 0 ? 0 : hoverCellX, hoverCellY < 0 ? 0 : hoverCellY,
                      selName, ed.dirty ? "*unsaved " : "",
                      ed.saveNote.empty() ? "" : (ed.saveNote + "  ").c_str(),
                      ed.outPath.c_str());
        game::drawText(wc, *font, st, kPalW + 8, winH - kBarH + 5, 0xffb0c0d0);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: mapedit <data-root> [--open level.ini] [--theater T] "
                     "[--width W] [--height H] [--out level.ini] [--shot out.bmp]\n"
                     "  data-root example: data/assets/tiberian_dawn/gdi\n");
        return 2;
    }
    try {
        std::string root = argv[1];
        const char* openPath = strArg(argc, argv, "--open");
        const char* outArg = strArg(argc, argv, "--out");
        std::string theater = toUpper(strArg(argc, argv, "--theater")
                                          ? strArg(argc, argv, "--theater")
                                          : "TEMPERATE");
        int newW = intArg(argc, argv, "--width", 64);
        int newH = intArg(argc, argv, "--height", 64);

        Editor ed;
        if (openPath) {
            ed.map = game::MapFile::load(openPath);
            ed.outPath = outArg ? outArg : openPath;
            theater = ed.map.theater;
        } else {
            ed.map = blankLevel(theater, newW, newH);
            ed.outPath = outArg ? outArg : "custom_level.ini";
        }

        // ---- Art + palette (TD layout) ----
        std::string theaterDir = root + "/" + theaterDirName(ed.map.theater);
        std::string conquerDir = root + "/CONQUER";
        std::string ext = ed.map.theaterExt();
        std::string palPath = theaterDir + "/" + ed.map.theaterPalette() + ".pal";
        auto pal = fmt::Palette::load(palPath);
        ArtCache art(theaterDir, conquerDir, ext);

        ed.palettes[int(Category::Terrain)] = buildTerrainPalette(art, theaterFlag(ed.map.theater));
        ed.palettes[int(Category::Overlay)] = buildOverlayPalette(art);
        ed.palettes[int(Category::Structures)] =
            buildObjectPalette(art, kTdStructTypes, int(std::size(kTdStructTypes)));
        ed.palettes[int(Category::Units)] =
            buildObjectPalette(art, kTdVehTypes, int(std::size(kTdVehTypes)));
        ed.palettes[int(Category::Infantry)] =
            buildObjectPalette(art, kTdInfTypes, int(std::size(kTdInfTypes)));
        ed.palettes[int(Category::Spawns)] = buildSpawnPalette();

        // Smart-coastline: logical water field + auto-harvested tile dictionary.
        ed.water.assign(size_t(kCorners) * kCorners, 0);
        ed.coastManaged.assign(size_t(kGrid) * kGrid, 0);
        ed.coast = buildCoastDict(art, theaterFlag(ed.map.theater));
        if (openPath)
            loadMask(ed, openPath);

        std::unordered_map<std::string, game::RemapTable> tdRemaps;
        auto remapFor = [&](const std::string& house) -> const game::RemapTable* {
            auto it = tdRemaps.find(house);
            if (it == tdRemaps.end())
                it = tdRemaps.emplace(house, game::tdRemap(house)).first;
            return &it->second;
        };

        std::string fontDir = root + "/INSTALL/CCLOCAL";
        std::optional<fmt::FntFile> font;
        try {
            font = fmt::FntFile::load(fontDir + "/8point.fnt");
        } catch (const std::exception&) {
            try {
                font = fmt::FntFile::load(fontDir + "/6point.fnt");
            } catch (const std::exception&) {
            }
        }

        // ---- Window ----
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());
        int winW = 1280, winH = 800;
        SDL_Window* win = SDL_CreateWindow("mapedit", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, winW, winH,
                                           SDL_WINDOW_RESIZABLE);
        if (!win)
            throw std::runtime_error(SDL_GetError());

        const int gridPx = kGrid * kTile;
        SDL_Surface* mapSurf = SDL_CreateRGBSurfaceWithFormat(0, gridPx, gridPx, 32,
                                                              SDL_PIXELFORMAT_ARGB8888);
        if (!mapSurf)
            throw std::runtime_error(SDL_GetError());
        game::Canvas mc = game::Canvas::wrap(mapSurf);

        bool terrainDirty = true;
        auto objects = resolveObjects(ed.map, art, remapFor);

        ed.camX = float(std::max(0, (ed.map.x + ed.map.width / 2) * kTile - (winW - kPalW) / 2));
        ed.camY = float(std::max(0, (ed.map.y + ed.map.height / 2) * kTile - (winH - kBarH) / 2));

        // Scripted edits for headless verification of the paint/place ops:
        // multi-cell terrain, a wall run, a building, a tank, infantry, spawns.
        if (flagArg(argc, argv, "--demo")) {
            auto& terr = ed.palettes[int(Category::Terrain)];
            auto& ovl = ed.palettes[int(Category::Overlay)];
            auto pick = [](std::vector<PalEntry>& list, const char* name) {
                for (int i = 0; i < int(list.size()); i++)
                    if (list[i].name == name)
                        return i;
                return 0;
            };
            int mc0 = ed.map.x + 2, mr0 = ed.map.y + 2;
            ed.cat = Category::Terrain;
            for (int i = 0; i < int(terr.size()); i++)
                if (terr[i].w >= 2 && terr[i].h >= 2) {
                    ed.selIdx[int(Category::Terrain)] = i;
                    break;
                }
            paintTerrain(ed, mc0, mr0);
            paintTerrain(ed, mc0 + 3, mr0 + 1);
            ed.cat = Category::Overlay;
            ed.selIdx[int(Category::Overlay)] = pick(ovl, "sbag");
            for (int x = 0; x < 6; x++)
                paintOverlay(ed, ed.map.x + x, ed.map.y + 6);
            ed.cat = Category::Structures;
            ed.selIdx[int(Category::Structures)] = pick(ed.pal(), "nuke");
            placeObject(ed, ed.map.x + 8, ed.map.y + 8);
            ed.cat = Category::Units;
            ed.selIdx[int(Category::Units)] = pick(ed.pal(), "htnk");
            placeObject(ed, ed.map.x + 11, ed.map.y + 8);
            ed.cat = Category::Infantry;
            ed.selIdx[int(Category::Infantry)] = pick(ed.pal(), "e1");
            placeObject(ed, ed.map.x + 8, ed.map.y + 11);
            ed.cat = Category::Spawns;
            ed.selIdx[int(Category::Spawns)] = 0;
            placeSpawn(ed, ed.map.x + 1, ed.map.y + 1);
            ed.selIdx[int(Category::Spawns)] = 1;
            placeSpawn(ed, ed.map.x + ed.map.width - 2, ed.map.y + ed.map.height - 2);
            objects = resolveObjects(ed.map, art, remapFor);
            if (outArg)
                saveMap(ed);
        }

        // Smart-coastline demo: paint a logical water field (a lake + a wavy
        // diagonal sea) and let the resolver choose the shore tiles.
        if (flagArg(argc, argv, "--coast-demo")) {
            int x0 = ed.map.x, y0 = ed.map.y, w = ed.map.width, h = ed.map.height;
            auto setW = [&](int cx, int cy) {
                if (cx >= 0 && cx < kCorners && cy >= 0 && cy < kCorners)
                    ed.water[cy * kCorners + cx] = 1;
            };
            int lcx = x0 + w / 3, lcy = y0 + h / 2, lr = std::min(w, h) / 6;
            for (int cy = 0; cy <= kGrid; cy++)
                for (int cx = 0; cx <= kGrid; cx++) {
                    int ddx = cx - lcx, ddy = cy - lcy;
                    if (ddx * ddx + ddy * ddy <= lr * lr)
                        setW(cx, cy); // isolated circular lake
                    int wave = ((cx / 3) % 2) * 2;
                    if (cx > x0 + w / 2 && cy > y0 + h / 2 &&
                        cx + cy > x0 + y0 + w + h - h / 3 + wave)
                        setW(cx, cy); // wavy diagonal sea in the SE corner
                }
            resolveCoast(ed);
            objects = resolveObjects(ed.map, art, remapFor);
            if (outArg)
                saveMap(ed);
        }

        // ---- StarCraft-terrain demo (parallel format) ----
        // Headless proof of the SC pivot: build an ISOM map, paint a plateau +
        // lake + field, compile & repair, render SC megatiles sampled to our
        // 24px cell, draw C&C units on top, round-trip the .scm, dump a BMP.
        // Entirely separate from the C&C TMP path above.
        if (flagArg(argc, argv, "--sc-demo")) {
            std::string scDir = strArg(argc, argv, "--sc-tiles")
                                    ? strArg(argc, argv, "--sc-tiles")
                                    : "data/assets/starcraft/tileset/TileSet";
            std::string scName = strArg(argc, argv, "--sc-tileset")
                                     ? strArg(argc, argv, "--sc-tileset")
                                     : "badlands";
            auto scTs = fmt::ScTileset::load(scDir, scName);
            game::ScIsom scTiles(scTs, game::badlandsBrush());
            const int scW = 48, scH = 40;  // cells (24px), independent of C&C grid
            game::ScIsomMap scMap(scTiles, scW, scH);
            scMap.tilesetName = scName;
            scMap.fill(2);                 // Dirt background
            scMap.place(10, 10, 3, 8);     // high-dirt plateau (cliff)
            scMap.place(10, 30, 5, 8);     // water lake (coastline)
            scMap.place(16, 20, 6, 7);     // grass field
            scMap.setAllChanged();
            scMap.updateTiles();
            scMap.repairNullTiles();

            // Round-trip the parallel map format: save, peek+load into a fresh
            // map, re-save, and byte-compare. This verifies the serialized ISOM
            // diamond grid is lossless (compiled tiles differ only by the random
            // subtile variant, which is not part of the format).
            const char* scmPath = "renders/sc/cpp_mapedit_demo.scm";
            const char* scmPath2 = "renders/sc/cpp_mapedit_demo_rt.scm";
            scMap.save(scmPath);
            std::string pkTs; int pkW = 0, pkH = 0;
            bool okPeek = game::ScIsomMap::peek(scmPath, pkTs, pkW, pkH);
            game::ScIsomMap reload(scTiles, pkW, pkH);
            reload.load(scmPath);
            reload.save(scmPath2);
            auto readFile = [](const char* p) {
                std::ifstream f(p, std::ios::binary);
                return std::vector<char>((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
            };
            bool ok = okPeek && readFile(scmPath) == readFile(scmPath2);
            std::printf(".scm round-trip: tileset=%s %dx%d lossless=%s\n",
                        pkTs.c_str(), pkW, pkH, ok ? "yes" : "NO");

            // Render SC terrain at 24px, then draw C&C units on top.
            SDL_FillRect(mapSurf, nullptr, 0xFF201822);
            game::ScTerrainRenderer scRender(scTs, kTile);
            scRender.draw(mc, scMap, 0, 0);
            auto drawUnit = [&](const char* type, int cx, int cy, const char* house,
                                int facing) {
                const fmt::ShpFile* shp = art.shp(type);
                if (!shp || shp->frames.empty()) return;
                DrawObject o;
                o.shp = shp;
                o.remap = remapFor(house);
                o.frame = game::facingToFrame(facing) % int(shp->frames.size());
                if (hasTurret(type))
                    o.turretFrame = 32 + game::facingToFrame(facing);
                o.x = cx * kTile + kTile / 2 - shp->width / 2;
                o.y = cy * kTile + kTile / 2 - shp->height / 2;
                drawObject(mc, o, pal, 0, 0);
            };
            drawUnit("htnk", 8, 8, "BadGuy", 64);
            drawUnit("mtnk", 20, 14, "GoodGuy", 160);
            drawUnit("jeep", 14, 24, "GoodGuy", 32);
            drawUnit("e1", 11, 12, "GoodGuy", 0);

            const char* scShot = strArg(argc, argv, "--shot")
                                     ? strArg(argc, argv, "--shot")
                                     : "renders/sc/cpp_mapedit_sc_demo.bmp";
            int ow = scW * kTile, oh = scH * kTile;
            SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32,
                                                              SDL_PIXELFORMAT_ARGB8888);
            SDL_Rect src{0, 0, ow, oh}, dst{0, 0, ow, oh};
            SDL_BlitSurface(mapSurf, &src, out, &dst);
            if (SDL_SaveBMP(out, scShot) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s (%dx%d)\n", scShot, ow, oh);
            SDL_FreeSurface(out);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 0;
        }

        // ---- Interactive StarCraft-terrain paint mode (parallel editor) ----
        // A second, self-contained editor loop for the SC ISOM terrain, entered
        // with --sc. Left-drag paints the selected terrain type with a diamond
        // brush; the ISOM module live-resolves cliffs/coasts; SC megatiles render
        // at our 24px cell. Keeps the C&C TMP editor (below) fully intact.
        // With --shot it paints a scripted stroke, screenshots, and exits so the
        // paint->compile->render path is verifiable headlessly.
        if (flagArg(argc, argv, "--sc")) {
            std::string scDir = strArg(argc, argv, "--sc-tiles")
                                    ? strArg(argc, argv, "--sc-tiles")
                                    : "data/assets/starcraft/tileset/TileSet";
            std::string scName = strArg(argc, argv, "--sc-tileset")
                                     ? strArg(argc, argv, "--sc-tileset")
                                     : "badlands";
            auto scTs = fmt::ScTileset::load(scDir, scName);
            game::ScIsom scTiles(scTs, game::badlandsBrush());
            int scW = intArg(argc, argv, "--width", 64) & ~1;   // keep even
            int scH = intArg(argc, argv, "--height", 64);
            const char* scmOpen = strArg(argc, argv, "--open-scm");
            std::string scmOut = strArg(argc, argv, "--out-scm")
                                     ? strArg(argc, argv, "--out-scm")
                                     : "data/custom/sc_level.scm";
            if (scmOpen) {
                std::string ts; int w, h;
                if (game::ScIsomMap::peek(scmOpen, ts, w, h)) { scW = w; scH = h; }
            }
            game::ScIsomMap scMap(scTiles, scW, scH);
            scMap.tilesetName = scName;
            if (scmOpen && scMap.load(scmOpen)) { /* loaded */ }
            else scMap.fill(2);  // Dirt

            // Terrain-type brush palette (index -> ISOM terrainType, label).
            struct ScBrushEntry { int tt; const char* name; };
            const ScBrushEntry scBrushes[] = {
                {2, "Dirt"}, {3, "High Dirt"}, {4, "Mud"},   {5, "Water"},
                {6, "Grass"}, {7, "High Grass"}, {18, "Structure"},
            };
            int scSel = 1;         // default High Dirt (shows cliffs)
            int scExtent = 8;      // diamond brush extent
            game::ScTerrainRenderer scRender(scTs, kTile);

            auto recompile = [&]() { scMap.updateTiles(); scMap.repairNullTiles(); };
            // Map a canvas pixel (map space) to a valid ISOM diamond and paint.
            auto paintAt = [&](int mapPxX, int mapPxY) {
                int tx = mapPxX / kTile, ty = mapPxY / kTile;
                int dx = tx / 2, dy = ty;
                if (((dx + dy) & 1) != 0) ++dy;  // snap to a valid diamond
                if (scMap.place(dx, dy, scBrushes[scSel].tt, scExtent)) {
                    recompile();
                    return true;
                }
                return false;
            };

            float scCamX = 0, scCamY = 0;
            const int scHudW = 150;  // left HUD strip
            bool scDirty = true, scPainting = false;
            auto redraw = [&]() {
                SDL_FillRect(mapSurf, nullptr, 0xFF201822);
                scRender.draw(mc, scMap, 0, 0);
                scDirty = false;
            };
            auto present = [&](SDL_Surface* wsurf) {
                SDL_FillRect(wsurf, nullptr, 0xFF101014);
                int vx = scHudW, vw = winW - scHudW, vh = winH;
                int mapPx = 0;  // clamp camera
                (void)mapPx;
                scCamX = std::clamp(scCamX, 0.f, float(std::max(0, scW * kTile - vw)));
                scCamY = std::clamp(scCamY, 0.f, float(std::max(0, scH * kTile - vh)));
                SDL_Rect srcR{int(scCamX), int(scCamY),
                              std::min(vw, scW * kTile - int(scCamX)),
                              std::min(vh, scH * kTile - int(scCamY))};
                SDL_Rect dstR{vx, 0, srcR.w, srcR.h};
                SDL_BlitSurface(mapSurf, &srcR, wsurf, &dstR);
                game::Canvas wc = game::Canvas::wrap(wsurf);
                if (font) {
                    int ty = 8;
                    game::drawText(wc, *font, "SC TERRAIN", 8, ty, 0xFFFFFFFF);
                    ty += 16;
                    for (int i = 0; i < int(std::size(scBrushes)); ++i) {
                        uint32_t col = (i == scSel) ? 0xFFFFE060 : 0xFFB0B0B0;
                        game::drawText(wc, *font, scBrushes[i].name, 12, ty, col);
                        ty += 14;
                    }
                    ty += 8;
                    game::drawText(wc, *font, "brush " + std::to_string(scExtent),
                                   8, ty, 0xFFB0B0B0);
                    ty += 14;
                    game::drawText(wc, *font, "1-7 type [ ] size", 8, ty, 0xFF808080);
                    ty += 14;
                    game::drawText(wc, *font, "Ctrl+S save  Esc quit", 8, ty, 0xFF808080);
                }
            };

            // Scripted stroke for headless verification.
            if (const char* shot = strArg(argc, argv, "--shot")) {
                // Each place() resets the changed area, so recompile per stroke
                // (exactly as the interactive paint path does).
                scMap.place(scW / 3, scH / 3, 3, 10); recompile();      // plateau
                scMap.place(scW / 3, 2 * scH / 3, 5, 10); recompile();  // lake
                scMap.place(2 * scW / 3, scH / 2, 6, 8); recompile();   // grass
                redraw();
                SDL_Surface* wsurf = SDL_GetWindowSurface(win);
                present(wsurf);
                if (SDL_SaveBMP(wsurf, shot) != 0)
                    throw std::runtime_error(SDL_GetError());
                std::printf("wrote %s (scripted --sc stroke)\n", shot);
                SDL_DestroyWindow(win);
                SDL_Quit();
                return 0;
            }

            bool scRun = true;
            while (scRun) {
                SDL_GetWindowSize(win, &winW, &winH);
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) scRun = false;
                    else if (e.type == SDL_KEYDOWN) {
                        SDL_Keycode k = e.key.keysym.sym;
                        bool ctrl = SDL_GetModState() & KMOD_CTRL;
                        if (k == SDLK_ESCAPE) scRun = false;
                        else if (k >= SDLK_1 && k <= SDLK_7)
                            scSel = std::min<int>(k - SDLK_1, int(std::size(scBrushes)) - 1);
                        else if (k == SDLK_LEFTBRACKET) scExtent = std::max(3, scExtent - 1);
                        else if (k == SDLK_RIGHTBRACKET) scExtent = std::min(16, scExtent + 1);
                        else if (k == SDLK_s && ctrl) {
                            scMap.save(scmOut);
                            std::printf("saved %s\n", scmOut.c_str());
                        }
                    } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                               e.button.button == SDL_BUTTON_LEFT && mx >= scHudW) {
                        scPainting = true;
                        if (paintAt(int(scCamX) + mx - scHudW, int(scCamY) + my))
                            scDirty = true;
                    } else if (e.type == SDL_MOUSEBUTTONUP &&
                               e.button.button == SDL_BUTTON_LEFT) {
                        scPainting = false;
                    } else if (e.type == SDL_MOUSEMOTION && scPainting && mx >= scHudW) {
                        if (paintAt(int(scCamX) + mx - scHudW, int(scCamY) + my))
                            scDirty = true;
                    }
                }
                const Uint8* keys = SDL_GetKeyboardState(nullptr);
                float sp = 12;
                if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) scCamX -= sp;
                if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) scCamX += sp;
                if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) scCamY -= sp;
                if (keys[SDL_SCANCODE_S] && !(SDL_GetModState() & KMOD_CTRL)) scCamY += sp;
                if (keys[SDL_SCANCODE_DOWN]) scCamY += sp;
                if (scDirty) redraw();
                SDL_Surface* wsurf = SDL_GetWindowSurface(win);
                present(wsurf);
                SDL_UpdateWindowSurface(win);
                SDL_Delay(16);
            }
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 0;
        }

        // Study render: dump the whole map's terrain (no editor UI, cropped to
        // content) to a BMP and exit. --grid overlays cell lines + coordinates.
        // Units/structures are omitted so coastlines aren't occluded.
        if (const char* renderPath = strArg(argc, argv, "--render")) {
            bakeTerrain(mc, ed.map, art, pal);
            int minx = kGrid, miny = kGrid, maxx = -1, maxy = -1;
            auto ext = [&](int cell) {
                int cx = cell % kGrid, cy = cell / kGrid;
                minx = std::min(minx, cx), miny = std::min(miny, cy);
                maxx = std::max(maxx, cx), maxy = std::max(maxy, cy);
            };
            for (int i = 0; i < kGrid * kGrid; i++)
                if (ed.map.cells[i].templateId != 0xffff)
                    ext(i);
            for (const auto& o : ed.map.tdOverlay)
                ext(o.cell);
            for (const auto& o : ed.map.terrain)
                ext(o.cell);
            // Always include the playable bounds so context isn't cropped away.
            ext(ed.map.y * kGrid + ed.map.x);
            ext((ed.map.y + ed.map.height - 1) * kGrid + ed.map.x + ed.map.width - 1);
            if (maxx < minx)
                minx = miny = 0, maxx = maxy = kGrid - 1;
            minx = std::max(0, minx - 1), miny = std::max(0, miny - 1);
            maxx = std::min(kGrid - 1, maxx + 1), maxy = std::min(kGrid - 1, maxy + 1);
            int cw = maxx - minx + 1, chh = maxy - miny + 1;
            int outW = cw * kTile, outH = chh * kTile;
            SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, outW, outH, 32,
                                                              SDL_PIXELFORMAT_ARGB8888);
            SDL_Rect src{minx * kTile, miny * kTile, outW, outH}, dst{0, 0, outW, outH};
            SDL_BlitSurface(mapSurf, &src, out, &dst);
            game::Canvas oc = game::Canvas::wrap(out);
            if (flagArg(argc, argv, "--grid")) {
                for (int gx = 0; gx <= cw; gx++)
                    game::fillRect(oc, gx * kTile, 0, 1, outH,
                                   (minx + gx) % 8 == 0 ? 0x88ffee00 : 0x33ffffff);
                for (int gy = 0; gy <= chh; gy++)
                    game::fillRect(oc, 0, gy * kTile, outW, 1,
                                   (miny + gy) % 8 == 0 ? 0x88ffee00 : 0x33ffffff);
                if (font) {
                    for (int gx = 0; gx < cw; gx++)
                        if ((minx + gx) % 8 == 0)
                            game::drawText(oc, *font, std::to_string(minx + gx),
                                           gx * kTile + 2, 1, 0xffffee00);
                    for (int gy = 0; gy < chh; gy++)
                        if ((miny + gy) % 8 == 0)
                            game::drawText(oc, *font, std::to_string(miny + gy), 1,
                                           gy * kTile + 1, 0xffffee00);
                }
            }
            if (SDL_SaveBMP(out, renderPath) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s (%dx%d) cells %d,%d..%d,%d\n", renderPath, outW, outH,
                        minx, miny, maxx, maxy);
            SDL_FreeSurface(out);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 0;
        }

        // Headless one-frame dump (verification): render to a BMP and exit.
        if (const char* shotPath = strArg(argc, argv, "--shot")) {
            if (outArg && !flagArg(argc, argv, "--demo") &&
                !flagArg(argc, argv, "--coast-demo"))
                saveMap(ed);
            bakeTerrain(mc, ed.map, art, pal);
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, winW, winH, 32,
                                                               SDL_PIXELFORMAT_ARGB8888);
            renderEditor(shot, ed, mapSurf, gridPx, objects, pal, font, art, -1, -1, false);
            if (SDL_SaveBMP(shot, shotPath) != 0)
                throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s (%dx%d)\n", shotPath, winW, winH);
            SDL_FreeSurface(shot);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 0;
        }

        auto viewRect = [&](int& vx, int& vy, int& vw, int& vh) {
            vx = kPalW;
            vy = 0;
            vw = winW - kPalW;
            vh = winH - kBarH;
        };

        bool painting = false, erasing = false;
        uint32_t last = SDL_GetTicks();
        bool quit = false;
        while (!quit) {
            SDL_GetWindowSize(win, &winW, &winH);
            int vx, vy, vw, vh;
            viewRect(vx, vy, vw, vh);

            int mx, my;
            SDL_GetMouseState(&mx, &my);
            bool inView = mx >= vx && mx < vx + vw && my >= vy && my < vy + vh;
            int hoverCellX = -1, hoverCellY = -1;
            if (inView) {
                hoverCellX = (int(ed.camX) + (mx - vx)) / kTile;
                hoverCellY = (int(ed.camY) + (my - vy)) / kTile;
            }

            auto applyPaint = [&](bool erase) {
                if (!inView)
                    return;
                if (ed.cat == Category::Coast) {
                    int px = int(ed.camX) + (mx - vx), py = int(ed.camY) + (my - vy);
                    paintWater(ed, (px + kTile / 2) / kTile, (py + kTile / 2) / kTile, !erase);
                    terrainDirty = true;
                    return;
                }
                if (hoverCellX < 0 || hoverCellY < 0)
                    return;
                if (erase) {
                    eraseAt(ed, hoverCellX, hoverCellY);
                } else {
                    switch (ed.cat) {
                    case Category::Terrain: paintTerrain(ed, hoverCellX, hoverCellY); break;
                    case Category::Overlay: paintOverlay(ed, hoverCellX, hoverCellY); break;
                    case Category::Structures:
                    case Category::Units:
                    case Category::Infantry: placeObject(ed, hoverCellX, hoverCellY); break;
                    case Category::Spawns: placeSpawn(ed, hoverCellX, hoverCellY); break;
                    }
                }
                terrainDirty = true;
            };
            // Terrain/overlay paint continuously on drag; objects/spawns act
            // only on the initial click (drag must not spam placements).
            bool dragPaints = ed.cat == Category::Terrain ||
                              ed.cat == Category::Overlay || ed.cat == Category::Coast;

            // Palette hit-test: which row is at panel y.
            auto palRowAt = [&](int py) {
                if (py < kPalTop)
                    return -1;
                int r = (py - kPalTop) / kPalRow;
                int i = ed.palScroll + r;
                return (r >= 0 && i >= 0 && i < int(ed.pal().size())) ? i : -1;
            };

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT)
                    quit = true;
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    bool ctrl = SDL_GetModState() & KMOD_CTRL;
                    if (k == SDLK_ESCAPE)
                        quit = true;
                    else if (k == SDLK_g)
                        ed.showGrid = !ed.showGrid;
                    else if (k >= SDLK_1 && k <= SDLK_7)
                        ed.cat = Category(k - SDLK_1), ed.palScroll = 0;
                    else if (k == SDLK_LEFTBRACKET)
                        ed.brush = std::max(0, ed.brush - 1);
                    else if (k == SDLK_RIGHTBRACKET)
                        ed.brush = std::min(24, ed.brush + 1);
                    else if (k == SDLK_t)
                        ed.cat = Category::Terrain, ed.palScroll = 0;
                    else if (k == SDLK_o && !ctrl)
                        ed.cat = Category::Overlay, ed.palScroll = 0;
                    else if (k == SDLK_h)
                        ed.houseIdx = (ed.houseIdx + 1) % int(std::size(kHouses));
                    else if (k == SDLK_z && ctrl)
                        ed.doUndo(), terrainDirty = true, ed.objectsDirty = true;
                    else if (k == SDLK_y && ctrl)
                        ed.doRedo(), terrainDirty = true, ed.objectsDirty = true;
                    else if (k == SDLK_s && ctrl)
                        saveMap(ed);
                }
                if (e.type == SDL_MOUSEWHEEL) {
                    int maxScroll = std::max(0, int(ed.pal().size()) - 1);
                    ed.palScroll = std::clamp(ed.palScroll - e.wheel.y, 0, maxScroll);
                }
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    if (e.button.x < kPalW) {
                        // Panel click: category tab row (y 58-74) or a palette row.
                        if (e.button.y >= 58 && e.button.y < 74) {
                            int tx = 8;
                            for (int c = 0; c < kNumCat && font; c++) {
                                int w = game::textWidth(*font, kCatTab[c]) + 8;
                                if (e.button.x >= tx && e.button.x < tx + w) {
                                    ed.cat = Category(c);
                                    ed.palScroll = 0;
                                    break;
                                }
                                tx += w + 2;
                            }
                        } else {
                            int i = palRowAt(e.button.y);
                            if (i >= 0)
                                ed.sel() = i;
                        }
                    } else if (e.button.button == SDL_BUTTON_LEFT) {
                        ed.snapshot();
                        painting = true;
                        applyPaint(false);
                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                        ed.snapshot();
                        erasing = true;
                        applyPaint(true);
                    }
                }
                if (e.type == SDL_MOUSEBUTTONUP) {
                    if (e.button.button == SDL_BUTTON_LEFT)
                        painting = false;
                    if (e.button.button == SDL_BUTTON_RIGHT)
                        erasing = false;
                }
                if (e.type == SDL_MOUSEMOTION && (painting || erasing) && dragPaints)
                    applyPaint(erasing);
            }

            if (ed.objectsDirty) {
                objects = resolveObjects(ed.map, art, remapFor);
                ed.objectsDirty = false;
            }

            uint32_t now = SDL_GetTicks();
            float dt = float(now - last) / 1000.0f;
            last = now;

            // ---- Scroll (only when not actively painting) ----
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            bool ctrl = SDL_GetModState() & KMOD_CTRL;
            const float kSpeed = 640.0f;
            const int kEdge = 12;
            float dx = 0, dy = 0;
            if (!ctrl) {
                if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] || (inView && mx < vx + kEdge))
                    dx -= 1;
                if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] || (inView && mx >= vx + vw - kEdge))
                    dx += 1;
                if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] || (inView && my < vy + kEdge))
                    dy -= 1;
                if (keys[SDL_SCANCODE_DOWN] || (inView && my >= vy + vh - kEdge))
                    dy += 1;
            }
            ed.camX = std::clamp(ed.camX + dx * kSpeed * dt, 0.0f, float(std::max(0, gridPx - vw)));
            ed.camY = std::clamp(ed.camY + dy * kSpeed * dt, 0.0f, float(std::max(0, gridPx - vh)));

            if (terrainDirty) {
                bakeTerrain(mc, ed.map, art, pal);
                terrainDirty = false;
            }

            SDL_Surface* wsurf = SDL_GetWindowSurface(win);
            renderEditor(wsurf, ed, mapSurf, gridPx, objects, pal, font, art, hoverCellX,
                         hoverCellY, inView, mx, my);
            SDL_UpdateWindowSurface(win);
            SDL_Delay(16);
        }
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mapedit error: %s\n", e.what());
        return 1;
    }
}
