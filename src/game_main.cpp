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

#include "formats/palette.h"
#include "formats/shp.h"
#include "formats/shpd2.h"
#include "formats/tmp.h"
#include "game/house.h"
#include "game/map.h"
#include "game/render.h"
#include "game/rules.h"
#include "game/sim.h"
#include "game/template_table.h"

namespace {

constexpr int kTile = 24;
constexpr double kTickMs = 1000.0 / 15.0; // RA game speed: 15 ticks/s

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

// Vehicles whose SHP packs 32 turret frames after the 32 hull facings.
bool hasTurret(const std::string& type) {
    static const char* kTurreted[] = {"1tnk", "2tnk", "3tnk", "4tnk", "jeep"};
    for (const char* t : kTurreted)
        if (type == t)
            return true;
    return false;
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: game <map.ini> <data-root> [--scale N] [--house H] [--no-shroud]\n"
                     "            [--sim-ticks N] [--move i,cx,cy]... [--attack i,j]...\n"
                     "            [--attack-struct i,sid]... [--dump out.bmp]\n"
                     "  data-root example: data/assets/red_alert/allied\n");
        return 2;
    }
    try {
        auto map = game::MapFile::load(argv[1]);
        std::string root = argv[2];
        const char* dumpPath = strArg(argc, argv, "--dump");
        int scale = intArg(argc, argv, "--scale", 1);
        int simTicks = intArg(argc, argv, "--sim-ticks", -1);

        std::string localDir = root + "/INSTALL/REDALERT/local";
        auto pal = fmt::Palette::load(localDir + "/" + map.theaterPalette() + ".pal");
        auto remaps = game::buildRemaps(localDir + "/palette.cps");
        ArtCache art(root + "/MAIN/" + theaterDirName(map), root + "/MAIN/conquer",
                     root + "/INSTALL/REDALERT/hires", map.theaterExt());
        auto rules = game::Rules::load(localDir + "/rules.ini");

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
        std::string playerHouse = strArg(argc, argv, "--house")
                                      ? strArg(argc, argv, "--house") : "Greece";
        if (!flagArg(argc, argv, "--no-shroud"))
            sim.setPlayerHouse(playerHouse);

        // Bakes one cell's base terrain into the world surface (also used to
        // redraw a cell after its ore is harvested away).
        auto bakeTerrainCell = [&](int mapX, int mapY) {
            const auto& cell = map.cells[mapY * kSize + mapX];
            int dx = (mapX - cx0) * kTile, dy = (mapY - cy0) * kTile;

            const fmt::TmpFile* t = nullptr;
            int icon = 0;
            if (cell.templateId != 0xffff && cell.templateId < game::kTemplateCount) {
                t = art.tmp(game::kTemplateTable[cell.templateId].name);
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
            // Land type from the template's control map (Land_Type()).
            if (t != clear && icon < int(t->landBytes.size()))
                sim.setLand(mapY * kSize + mapX,
                            game::landFromControl(t->landBytes[icon]));
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
        for (int cy = 0; cy < ch; cy++) {
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
        for (const auto& s : map.structures) {
            const fmt::ShpFile* shp = art.shp(s.type);
            if (!shp || shp->frames.empty()) {
                std::printf("note: missing structure art: %s\n", s.type.c_str());
                continue;
            }
            game::Sim::Structure st;
            st.type = s.type;
            st.house = s.house;
            st.cell = s.cell;
            // Footprint = SHP cell bounds (approximation, see MILESTONES).
            st.w = (shp->width + kTile - 1) / kTile;
            st.h = (shp->height + kTile - 1) / kTile;
            st.stats = rules.unit(s.type, game::UnitKind::Structure);
            st.hp = std::max(1, st.stats.strength * s.health / 256);
            int sid = sim.addStructure(std::move(st));

            DrawObject o;
            o.shp = shp;
            int scx = s.cell % kSize, scy = s.cell / kSize;
            o.x = (scx - cx0) * kTile;
            o.y = (scy - cy0) * kTile;
            o.remap = &remaps[size_t(game::houseColor(s.house))];
            o.selectable = true;
            o.structId = sid;
            structures.push_back(o);
            if (const fmt::ShpFile* top = art.shp(s.type + "2")) {
                DrawObject o2 = o;
                o2.shp = top;
                o2.selectable = false;
                structures.push_back(o2);
            } // addStructure blocked the footprint and revealed shroud
        }

        // ---- Sim units from the scenario ----
        for (const auto& u : map.units) {
            game::Sim::Unit su;
            su.type = u.type;
            su.house = u.house;
            su.facing = u.facing;
            su.turreted = hasTurret(u.type);
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
            o.remap = &remaps[size_t(game::houseColor(u.house))];
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
            for (auto [id, dest] : orders)
                sim.orderMove({id}, dest);
            for (int t = 0; t < simTicks; t++) {
                sim.tick();
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
        int winW = std::min(mapSurf->w, 1280), winH = std::min(mapSurf->h, 800);
        SDL_Window* win = SDL_CreateWindow("game", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, winW, winH, 0);
        if (!win)
            throw std::runtime_error(SDL_GetError());
        SDL_ShowCursor(SDL_DISABLE);
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
        double tickAcc = 0;
        bool quit = false;
        while (!quit) {
            int mx, my;
            uint32_t mstate = SDL_GetMouseState(&mx, &my);
            auto objects = buildDrawList();

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
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                    int wx = e.button.x + int(camX), wy = e.button.y + int(camY);
                    int cellX = cx0 + wx / kTile, cellY = cy0 + wy / kTile;
                    if (cellX >= 0 && cellX < kSize && cellY >= 0 && cellY < kSize) {
                        std::vector<int> ids;
                        for (const auto& u : sim.units())
                            if (u.selected)
                                ids.push_back(u.id);
                        // Right-click on an enemy = attack, otherwise move.
                        int tu = -1, ts = -1;
                        for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                            if (wx < it->x || wx >= it->x + it->shp->width ||
                                wy < it->y || wy >= it->y + it->shp->height)
                                continue;
                            if (it->unitId >= 0) {
                                const auto* u = sim.findUnit(it->unitId);
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
                        if (ids.empty())
                            ; // nothing selected
                        else if (tu >= 0 || ts >= 0)
                            sim.orderAttack(ids, tu, ts);
                        else if (sim.oreAt(cell) > 0) {
                            // Harvesters gather; anyone else just drives there.
                            std::vector<int> harv, rest;
                            for (int id : ids)
                                (sim.findUnit(id)->harvester ? harv : rest)
                                    .push_back(id);
                            if (!harv.empty())
                                sim.orderHarvest(harv, cell);
                            if (!rest.empty())
                                sim.orderMove(rest, cell);
                        } else
                            sim.orderMove(ids, cell);
                    }
                }
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
            objects = buildDrawList(); // positions may have ticked above
            for (const auto& o : objects)
                drawObject(wc, o, pal, int(camX), int(camY));
            drawEffects(wc, int(camX), int(camY));
            drawShroud(wc, int(camX), int(camY));

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
