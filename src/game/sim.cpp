// Simulation core: cell passability, A* pathfinding, per-tick movement.
//
// Movement model (simplified from DRIVE.CPP/FOOT.CPP): units travel between
// cell centers along their A* path at stats.speed leptons/tick scaled by the
// land percentage of the cell they are in. Vehicles pivot toward the next
// waypoint (ROT facing units per tick) and only translate once roughly
// aligned; infantry turn instantly. Non-infantry own their current cell and
// reserve the next one before entering it — a blocked reservation waits, then
// repaths around the obstacle, then gives up.

#include "game/sim.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace game {

namespace {
constexpr double kPi = 3.14159265358979323846;

// 8 neighbors: E, SE, S, SW, W, NW, N, NE.
constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};

int cellCenter(int c) { return c * Sim::kLepton + Sim::kLepton / 2; }
} // namespace

int directionTo(int dx, int dy) {
    if (dx == 0 && dy == 0)
        return 0;
    // Screen/map coordinates: +y is south, DirType 0 is north, clockwise.
    return int(std::lround(std::atan2(double(dx), double(-dy)) * 256.0 / (2 * kPi))) & 0xff;
}

int Sim::addUnit(Unit u, int cell) {
    u.id = nextId_++;
    u.x = cellCenter(cell % kSize);
    u.y = cellCenter(cell / kSize);
    if (u.hp < 0)
        u.hp = u.stats.strength;
    u.turretFacing = u.facing;
    if (u.infantry) {
        // Offset to the sub-cell spot so sim position matches the draw spot.
        static const int kSubX[5] = {0, -6, 6, -6, 6};
        static const int kSubY[5] = {0, -6, -6, 6, 6};
        int sub = std::clamp(u.subcell, 0, 4);
        u.x += kSubX[sub] * kLepton / 24;
        u.y += kSubY[sub] * kLepton / 24;
    } else {
        u.occCell = cell;
        occupant_[cell] = u.id;
    }
    if (!playerHouse_.empty() && u.house == playerHouse_)
        reveal(cell, u.stats.sight);
    units_.push_back(std::move(u));
    return units_.back().id;
}

int Sim::addStructure(Structure s) {
    s.id = nextStructId_++;
    if (s.hp <= 0)
        s.hp = s.stats.strength;
    int scx = s.cell % kSize, scy = s.cell / kSize;
    for (int by = 0; by < s.h; by++)
        for (int bx = 0; bx < s.w; bx++)
            if (scx + bx < kSize && scy + by < kSize)
                blocked_[(scy + by) * kSize + scx + bx] = 1;
    if (!playerHouse_.empty() && s.house == playerHouse_)
        reveal(s.cell, s.stats.sight);
    structures_.push_back(std::move(s));
    return structures_.back().id;
}

const Sim::Unit* Sim::findUnit(int id) const {
    for (const auto& u : units_)
        if (u.id == id)
            return &u;
    return nullptr;
}

const Sim::Structure* Sim::findStructure(int id) const {
    for (const auto& s : structures_)
        if (s.id == id)
            return &s;
    return nullptr;
}

Sim::Unit* Sim::mutableUnit(int id) {
    return const_cast<Unit*>(std::as_const(*this).findUnit(id));
}

Sim::Structure* Sim::mutableStructure(int id) {
    return const_cast<Structure*>(std::as_const(*this).findStructure(id));
}

bool Sim::targetPos(int targetUnit, int targetStruct, int& x, int& y) const {
    if (const Unit* u = findUnit(targetUnit)) {
        x = u->x;
        y = u->y;
        return true;
    }
    if (const Structure* s = findStructure(targetStruct)) {
        x = s->cx();
        y = s->cy();
        return true;
    }
    return false;
}

void Sim::reveal(int cell, int radius) {
    int cx = cell % kSize, cy = cell / kSize;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            // +radius rounds the circle like the original sight tables do.
            if (dx * dx + dy * dy > radius * radius + radius)
                continue;
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && x < kSize && y >= 0 && y < kSize)
                explored_[y * kSize + x] = 1;
        }
    }
}

bool Sim::passable(int cell, SpeedClass cls) const {
    if (cell < 0 || cell >= kSize * kSize)
        return false;
    if (blocked_[cell])
        return false;
    return rules_->landSpeed(land_[cell], cls) > 0;
}

int Sim::moveCost(int cell, SpeedClass cls, bool diagonal) const {
    int pct = rules_->landSpeed(land_[cell], cls);
    if (pct <= 0)
        return -1;
    return (diagonal ? 362 : 256) * 100 / pct;
}

std::vector<uint16_t> Sim::findPath(int from, int to, SpeedClass cls, int selfId) const {
    constexpr int kCells = kSize * kSize;
    if (from == to || from < 0 || from >= kCells || to < 0 || to >= kCells)
        return {};

    std::vector<int> gCost(kCells, INT32_MAX);
    std::vector<int32_t> parent(kCells, -1);
    // (f, cell) min-heap.
    using Node = std::pair<int, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> open;

    int tx = to % kSize, ty = to / kSize;
    auto heuristic = [&](int c) {
        int dx = std::abs(c % kSize - tx), dy = std::abs(c / kSize - ty);
        return 256 * std::max(dx, dy) + 106 * std::min(dx, dy); // octile
    };

    gCost[from] = 0;
    open.emplace(heuristic(from), from);
    int best = from, bestH = heuristic(from);

    while (!open.empty()) {
        auto [f, c] = open.top();
        open.pop();
        if (c == to)
            break;
        if (f - heuristic(c) > gCost[c])
            continue; // stale entry
        int cx = c % kSize, cy = c / kSize;
        for (int d = 0; d < 8; d++) {
            int nx = cx + kDx[d], ny = cy + kDy[d];
            if (nx < 0 || nx >= kSize || ny < 0 || ny >= kSize)
                continue;
            int n = ny * kSize + nx;
            bool diagonal = kDx[d] && kDy[d];
            if (diagonal) {
                // No corner cutting: both orthogonal neighbors must be open.
                if (!passable(cy * kSize + nx, cls) || !passable(ny * kSize + cx, cls))
                    continue;
            }
            if (!passable(n, cls))
                continue;
            if (n != to && occupant_[n] != -1 && occupant_[n] != selfId)
                continue;
            int step = moveCost(n, cls, diagonal);
            if (step < 0)
                continue;
            int g = gCost[c] + step;
            if (g >= gCost[n])
                continue;
            gCost[n] = g;
            parent[n] = c;
            int h = heuristic(n);
            open.emplace(g + h, n);
            if (h < bestH) {
                bestH = h;
                best = n;
            }
        }
    }

    int goal = gCost[to] != INT32_MAX ? to : best;
    if (goal == from)
        return {};
    std::vector<uint16_t> path;
    for (int c = goal; c != from; c = parent[c])
        path.push_back(uint16_t(c));
    std::reverse(path.begin(), path.end());
    return path;
}

void Sim::orderMove(const std::vector<int>& ids, int destCell) {
    std::vector<uint8_t> claimed(kSize * kSize, 0);
    for (int id : ids) {
        auto it = std::find_if(units_.begin(), units_.end(),
                               [&](const Unit& u) { return u.id == id; });
        if (it == units_.end())
            continue;
        Unit& u = *it;

        // Spiral out from the clicked cell to a free, unclaimed, passable one.
        int dest = -1;
        for (int r = 0; r < kSize && dest < 0; r++) {
            for (int dy = -r; dy <= r && dest < 0; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r)
                        continue;
                    int x = destCell % kSize + dx, y = destCell / kSize + dy;
                    if (x < 0 || x >= kSize || y < 0 || y >= kSize)
                        continue;
                    int c = y * kSize + x;
                    if (claimed[c] || !passable(c, u.stats.speedClass))
                        continue;
                    if (!u.infantry && occupant_[c] != -1 && occupant_[c] != u.id)
                        continue;
                    dest = c;
                    break;
                }
            }
        }
        if (dest < 0)
            continue;
        claimed[dest] = 1;

        // Drop any in-flight reservation before repathing.
        if (u.reservedCell >= 0 && occupant_[u.reservedCell] == u.id &&
            u.reservedCell != u.occCell)
            occupant_[u.reservedCell] = -1;
        u.reservedCell = -1;
        u.blockedTicks = 0;

        int from = u.infantry ? u.cell() : u.occCell;
        u.path = findPath(from, dest, u.stats.speedClass, u.id);
        u.destCell = u.path.empty() ? -1 : int(u.path.back());
    }
}

void Sim::orderAttack(const std::vector<int>& ids, int targetUnit, int targetStruct) {
    for (int id : ids) {
        Unit* u = mutableUnit(id);
        if (!u || u->id == targetUnit)
            continue;
        u->targetUnit = targetUnit;
        u->targetStruct = targetStruct;
    }
}

void Sim::orderHarvest(const std::vector<int>& ids, int cell) {
    for (int id : ids) {
        Unit* u = mutableUnit(id);
        if (!u || !u->harvester)
            continue;
        u->targetUnit = u->targetStruct = -1;
        u->harvMode = Unit::Harv::ToOre;
        u->harvCell = cell;
        u->harvTimer = 0;
        // Drop any in-flight reservation and stop; tickHarvest paths next tick.
        if (u->reservedCell >= 0 && occupant_[u->reservedCell] == u->id &&
            u->reservedCell != u->occCell)
            occupant_[u->reservedCell] = -1;
        u->reservedCell = -1;
        u->blockedTicks = 0;
        u->path.clear();
        u->destCell = -1;
    }
}

void Sim::power(const std::string& house, int& produced, int& drained) const {
    produced = drained = 0;
    for (const auto& s : structures_) {
        if (s.house != house)
            continue;
        if (s.stats.power >= 0)
            produced += s.stats.power;
        else
            drained -= s.stats.power;
    }
}

int Sim::findOre(int from) const {
    int fx = from % kSize, fy = from / kSize;
    for (int r = 0; r < kSize; r++) {
        int best = -1, bestD = INT32_MAX;
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                int x = fx + dx, y = fy + dy;
                if (x < 0 || x >= kSize || y < 0 || y >= kSize)
                    continue;
                int c = y * kSize + x;
                if (oreBails_[c] > 0 && !blocked_[c] &&
                    dx * dx + dy * dy < bestD) {
                    best = c;
                    bestD = dx * dx + dy * dy;
                }
            }
        }
        if (best >= 0)
            return best;
    }
    return -1;
}

void Sim::tickHarvest(Unit& u) {
    switch (u.harvMode) {
    case Unit::Harv::None:
        return;
    case Unit::Harv::ToOre: {
        if (!u.path.empty())
            return;
        int c = u.cell();
        if (oreBails_[c] > 0) {
            u.harvMode = Unit::Harv::Harvest;
            u.harvTimer = 0;
            return;
        }
        int target = findOre(u.harvCell >= 0 ? u.harvCell : c);
        if (target < 0) {
            u.harvMode = Unit::Harv::None; // map is picked clean
            return;
        }
        u.path = findPath(u.occCell, target, u.stats.speedClass, u.id);
        u.destCell = u.path.empty() ? -1 : int(u.path.back());
        if (u.path.empty())
            u.harvMode = Unit::Harv::None;
        return;
    }
    case Unit::Harv::Harvest: {
        int c = u.cell();
        if (oreBails_[c] == 0) {
            u.harvMode = Unit::Harv::ToOre;
            return;
        }
        if (++u.harvTimer < 15) // ~1s per bail
            return;
        u.harvTimer = 0;
        u.harvCell = c;
        if (oreGem_[c])
            u.gemBails++;
        else
            u.bails++;
        if (--oreBails_[c] == 0) {
            // Approximation: ore sat on clear ground (we lost the original
            // land type when the overlay was applied).
            land_[c] = Land::Clear;
            oreGem_[c] = 0;
            Event ev;
            ev.type = Event::OreDepleted;
            ev.cell = c;
            ev.x = (c % kSize) * kLepton + kLepton / 2;
            ev.y = (c / kSize) * kLepton + kLepton / 2;
            events_.push_back(ev);
        }
        if (u.bails + u.gemBails >= rules_->bailCount())
            u.harvMode = Unit::Harv::ToRef;
        else if (oreBails_[c] == 0)
            u.harvMode = Unit::Harv::ToOre;
        return;
    }
    case Unit::Harv::ToRef: {
        if (!u.path.empty())
            return;
        const Structure* refinery = nullptr;
        int bestD = INT32_MAX;
        for (const auto& s : structures_) {
            if (s.type != "proc" || s.house != u.house)
                continue;
            int dx = s.cx() - u.x, dy = s.cy() - u.y;
            if (dx * dx + dy * dy < bestD) {
                bestD = dx * dx + dy * dy;
                refinery = &s;
            }
        }
        if (!refinery) {
            u.harvMode = Unit::Harv::None; // nowhere to unload; stay loaded
            return;
        }
        // Docked = standing in a cell adjacent to the footprint.
        int cx = u.cell() % kSize, cy = u.cell() / kSize;
        int sx = refinery->cell % kSize, sy = refinery->cell / kSize;
        if (cx >= sx - 1 && cx <= sx + refinery->w && cy >= sy - 1 &&
            cy <= sy + refinery->h) {
            u.harvMode = Unit::Harv::Unload;
            u.harvTimer = 0;
            return;
        }
        // Path at the footprint; A* falls back to the closest reachable
        // cell, which is one adjacent to the building.
        int goal = (sy + refinery->h / 2) * kSize + sx + refinery->w / 2;
        u.path = findPath(u.occCell, goal, u.stats.speedClass, u.id);
        u.destCell = u.path.empty() ? -1 : int(u.path.back());
        if (u.path.empty())
            u.harvMode = Unit::Harv::None;
        return;
    }
    case Unit::Harv::Unload: {
        if (++u.harvTimer < 30) // ~2s to unload
            return;
        credits_[u.house] +=
            u.bails * rules_->goldValue() + u.gemBails * rules_->gemValue();
        u.bails = u.gemBails = 0;
        u.harvTimer = 0;
        u.harvMode = Unit::Harv::ToOre; // head back for more
        return;
    }
    }
}

namespace {
Sim::ProdCat catOf(UnitKind kind) {
    switch (kind) {
    case UnitKind::Structure: return Sim::ProdCat::Building;
    case UnitKind::Infantry:  return Sim::ProdCat::Infantry;
    default:                  return Sim::ProdCat::Vehicle;
    }
}
} // namespace

bool Sim::prereqsMet(const std::string& house, const std::string& prereq) const {
    const char* p = prereq.c_str();
    while (*p) {
        const char* comma = std::strchr(p, ',');
        std::string need = comma ? std::string(p, comma - p) : std::string(p);
        bool found = false;
        for (const auto& s : structures_) {
            if (s.house != house)
                continue;
            if (s.type == need ||
                // Barracks flag: barr and tent are interchangeable.
                ((need == "barr" || need == "tent") &&
                 (s.type == "barr" || s.type == "tent"))) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
        if (!comma)
            break;
        p = comma + 1;
    }
    return true;
}

bool Sim::startProduction(const std::string& house, const std::string& type,
                          UnitKind kind) {
    Production& slot = prod_[house].slot[size_t(catOf(kind))];
    if (slot.active())
        return false;
    const UnitStats& stats = rules_->unit(type, kind);
    if (stats.cost <= 0)
        return false;
    // The producing factory itself is an implicit prerequisite.
    std::string factory = kind == UnitKind::Structure ? "fact"
                          : kind == UnitKind::Infantry ? "barr" : "weap";
    if (!prereqsMet(house, factory) || !prereqsMet(house, stats.prereq))
        return false;
    slot.type = type;
    slot.kind = kind;
    slot.cost = stats.cost;
    slot.ticksTotal = rules_->buildTicks(stats.cost);
    slot.progress = slot.paid = slot.powAcc = 0;
    slot.ready = false;
    return true;
}

const Sim::Production* Sim::production(const std::string& house, ProdCat cat) const {
    auto it = prod_.find(house);
    if (it == prod_.end())
        return nullptr;
    const Production& p = it->second.slot[size_t(cat)];
    return p.active() ? &p : nullptr;
}

void Sim::cancelProduction(const std::string& house, ProdCat cat) {
    auto it = prod_.find(house);
    if (it == prod_.end())
        return;
    Production& p = it->second.slot[size_t(cat)];
    if (!p.active())
        return;
    credits_[house] += p.paid;
    p = Production{};
}

bool Sim::canPlace(const std::string& house, int cell, int w, int h) const {
    int cx = cell % kSize, cy = cell / kSize;
    if (cx < 0 || cy < 0 || cx + w > kSize || cy + h > kSize)
        return false;
    for (int by = 0; by < h; by++)
        for (int bx = 0; bx < w; bx++) {
            int c = (cy + by) * kSize + cx + bx;
            if (blocked_[c] || occupant_[c] != -1 ||
                !rules_->landBuildable(land_[c]))
                return false;
        }
    // Must touch a friendly structure (Adjacent=1 approximation): footprints
    // [cx, cx+w) and [sx-1, sx+s.w+1) must overlap, same for rows.
    for (const auto& s : structures_) {
        if (s.house != house)
            continue;
        int sx = s.cell % kSize, sy = s.cell / kSize;
        if (cx <= sx + s.w && cx + w >= sx && cy <= sy + s.h && cy + h >= sy)
            return true;
    }
    return false;
}

int Sim::placeBuilding(const std::string& house, int cell, int w, int h) {
    Production& slot = prod_[house].slot[size_t(ProdCat::Building)];
    if (!slot.active() || !slot.ready || !canPlace(house, cell, w, h))
        return -1;
    Structure st;
    st.type = slot.type;
    st.house = house;
    st.cell = cell;
    st.w = w;
    st.h = h;
    st.stats = rules_->unit(slot.type, UnitKind::Structure);
    st.hp = st.stats.strength;
    slot = Production{};
    return addStructure(std::move(st));
}

int Sim::deployMcv(int unitId, int w, int h) {
    Unit* u = mutableUnit(unitId);
    if (!u || u->type != "mcv" || u->moving())
        return -1;
    int cx = u->cell() % kSize - 1, cy = u->cell() / kSize - 1;
    if (cx < 0 || cy < 0 || cx + w > kSize || cy + h > kSize)
        return -1;
    for (int by = 0; by < h; by++)
        for (int bx = 0; bx < w; bx++) {
            int c = (cy + by) * kSize + cx + bx;
            if (blocked_[c] ||
                (occupant_[c] != -1 && occupant_[c] != u->id) ||
                !rules_->landBuildable(land_[c]))
                return -1;
        }
    // Consume the MCV silently (no death event/explosion).
    std::string house = u->house;
    int id = u->id;
    if (u->occCell >= 0 && occupant_[u->occCell] == u->id)
        occupant_[u->occCell] = -1;
    if (u->reservedCell >= 0 && occupant_[u->reservedCell] == u->id)
        occupant_[u->reservedCell] = -1;
    units_.erase(std::remove_if(units_.begin(), units_.end(),
                                [&](const Unit& v) { return v.id == id; }),
                 units_.end());
    Structure st;
    st.type = "fact";
    st.house = std::move(house);
    st.cell = cy * kSize + cx;
    st.w = w;
    st.h = h;
    st.stats = rules_->unit("fact", UnitKind::Structure);
    st.hp = st.stats.strength;
    return addStructure(std::move(st));
}

bool Sim::spawnProduced(const std::string& house, const Production& p) {
    // Find the producing factory.
    const Structure* fac = nullptr;
    for (const auto& s : structures_) {
        if (s.house != house)
            continue;
        if (p.kind == UnitKind::Infantry ? (s.type == "barr" || s.type == "tent")
                                         : s.type == "weap") {
            fac = &s;
            break;
        }
    }
    if (!fac)
        return false;
    Unit u;
    u.type = p.type;
    u.house = house;
    u.infantry = p.kind == UnitKind::Infantry;
    u.stats = rules_->unit(p.type, p.kind);
    u.turreted = hasTurretArt(p.type);
    u.harvester = p.type == "harv";
    u.facing = 128; // exit facing south
    // Spiral for a free cell around the factory footprint.
    int fx = fac->cell % kSize + fac->w / 2, fy = fac->cell / kSize + fac->h;
    for (int r = 0; r < 6; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                int x = fx + dx, y = fy + dy;
                if (x < 0 || x >= kSize || y < 0 || y >= kSize)
                    continue;
                int c = y * kSize + x;
                if (!passable(c, u.stats.speedClass))
                    continue;
                if (!u.infantry && occupant_[c] != -1)
                    continue;
                addUnit(std::move(u), c);
                return true;
            }
        }
    }
    return false;
}

void Sim::tickProduction() {
    for (auto& [house, hp] : prod_) {
        for (auto& slot : hp.slot) {
            if (!slot.active())
                continue;
            if (slot.ready) {
                // Finished units wait here until the factory has room;
                // buildings wait for the player to place them.
                if (slot.kind != UnitKind::Structure && spawnProduced(house, slot))
                    slot = Production{};
                continue;
            }
            // Low power slows production but never stops it entirely.
            slot.powAcc = std::min(slot.powAcc + std::max(16, powerFraction(house)), 512);
            while (slot.powAcc >= 256 && slot.progress < slot.ticksTotal) {
                // Pay for this tick of progress before advancing.
                int nextPaid = slot.cost * (slot.progress + 1) / slot.ticksTotal;
                int due = nextPaid - slot.paid;
                auto it = credits_.find(house);
                int have = it == credits_.end() ? 0 : it->second;
                if (have < due) {
                    slot.powAcc = 0; // broke: production stalls
                    break;
                }
                credits_[house] = have - due;
                slot.paid = nextPaid;
                slot.progress++;
                slot.powAcc -= 256;
            }
            if (slot.progress >= slot.ticksTotal)
                slot.ready = true;
        }
    }
}

void Sim::tick() {
    events_.clear();
    tickProduction();
    for (auto& u : units_) {
        bool wasMoving = u.moving();
        tickUnit(u);
        if (wasMoving && !playerHouse_.empty() && u.house == playerHouse_)
            reveal(u.cell(), u.stats.sight);
    }
    tickProjectiles();
    units_.erase(std::remove_if(units_.begin(), units_.end(),
                                [](const Unit& u) { return u.hp <= 0; }),
                 units_.end());
    structures_.erase(std::remove_if(structures_.begin(), structures_.end(),
                                     [](const Structure& s) { return s.hp <= 0; }),
                      structures_.end());
}

bool Sim::tickCombat(Unit& u) {
    if (u.cooldown > 0)
        u.cooldown--;
    if (!u.hasTarget()) {
        // Idle turret eases back to face forward.
        int diff = ((u.facing - u.turretFacing + 128) & 0xff) - 128;
        u.turretFacing =
            (u.turretFacing + std::clamp(diff, -u.stats.rot, u.stats.rot)) & 0xff;
        return false;
    }
    const WeaponStats* w = u.stats.primary;
    int tx = 0, ty = 0;
    if (!w || !targetPos(u.targetUnit, u.targetStruct, tx, ty)) {
        u.targetUnit = u.targetStruct = -1; // target gone (or we can't shoot)
        return false;
    }
    int dx = tx - u.x, dy = ty - u.y;
    double dist = std::sqrt(double(dx) * dx + double(dy) * dy);
    int desired = directionTo(dx, dy);

    if (dist > w->range) {
        // Chase: (re)path when we have no destination or the target strayed
        // more than a couple of cells from where we were headed.
        int tcell = (ty / kLepton) * kSize + (tx / kLepton);
        bool needPath = u.destCell < 0;
        if (!needPath) {
            int dcx = u.destCell % kSize - tcell % kSize;
            int dcy = u.destCell / kSize - tcell / kSize;
            needPath = dcx * dcx + dcy * dcy > 4;
        }
        if (needPath) {
            int from = u.infantry ? u.cell() : u.occCell;
            u.path = findPath(from, tcell, u.stats.speedClass, u.id);
            u.destCell = u.path.empty() ? -1 : int(u.path.back());
            if (u.path.empty()) {
                u.targetUnit = u.targetStruct = -1; // unreachable: give up
                return false;
            }
        }
        if (u.turreted) { // track while driving
            int diff = ((desired - u.turretFacing + 128) & 0xff) - 128;
            u.turretFacing =
                (u.turretFacing + std::clamp(diff, -u.stats.rot, u.stats.rot)) & 0xff;
        }
        return false;
    }

    // In range. Vehicles mid-way into a reserved cell finish that move first.
    if (!u.infantry && u.reservedCell >= 0)
        return false;
    u.path.clear();
    u.destCell = -1;

    // Aim with the turret if we have one, else pivot the body.
    int aim = u.turreted ? u.turretFacing : u.facing;
    int diff = ((desired - aim + 128) & 0xff) - 128;
    int step = std::clamp(diff, -u.stats.rot, u.stats.rot);
    if (u.turreted)
        u.turretFacing = (u.turretFacing + step) & 0xff;
    else
        u.facing = (u.facing + step) & 0xff;
    if (std::abs(diff - step) > 8)
        return true; // still traversing onto the target

    if (u.cooldown == 0) {
        Projectile p;
        p.x = u.x;
        p.y = u.y;
        p.tx = tx;
        p.ty = ty;
        p.targetUnit = u.targetUnit;
        p.targetStruct = u.targetStruct;
        p.weapon = w;
        p.facing = desired;
        projectiles_.push_back(p);
        u.cooldown = w->rof;
    }
    return true;
}

void Sim::tickProjectiles() {
    size_t count = projectiles_.size(); // impacts may add none; safe bound
    for (size_t i = 0; i < count; i++) {
        Projectile& p = projectiles_[i];
        int tx, ty;
        if (targetPos(p.targetUnit, p.targetStruct, tx, ty)) {
            p.tx = tx; // home on the live target
            p.ty = ty;
        }
        int dx = p.tx - p.x, dy = p.ty - p.y;
        double dist = std::sqrt(double(dx) * dx + double(dy) * dy);
        if (dist <= p.weapon->speed) {
            p.x = p.tx;
            p.y = p.ty;
            impact(p);
            p.weapon = nullptr; // tombstone
        } else {
            p.facing = directionTo(dx, dy);
            p.x += int(std::lround(dx * p.weapon->speed / dist));
            p.y += int(std::lround(dy * p.weapon->speed / dist));
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
                                      [](const Projectile& p) { return !p.weapon; }),
                       projectiles_.end());
}

void Sim::impact(const Projectile& p) {
    const WarheadStats* wh = p.weapon->warhead;
    Event ev;
    ev.type = Event::Impact;
    ev.x = p.x;
    ev.y = p.y;
    ev.warhead = wh;
    int dealt = 0;
    if (Unit* t = mutableUnit(p.targetUnit)) {
        dealt = rules_->modifyDamage(p.weapon->damage, wh, t->stats.armor, 0);
        t->hp -= dealt;
        ev.infantry = t->infantry;
        if (t->hp <= 0)
            killUnit(*t);
    } else if (Structure* s = mutableStructure(p.targetStruct)) {
        dealt = rules_->modifyDamage(p.weapon->damage, wh, s->stats.armor, 0);
        s->hp -= dealt;
        if (s->hp <= 0)
            killStructure(*s);
    } else {
        dealt = p.weapon->damage; // target died mid-flight: ground impact
    }
    ev.damage = dealt;
    events_.push_back(ev);
}

void Sim::killUnit(Unit& u) {
    u.hp = 0; // purged at the end of tick()
    if (u.occCell >= 0 && occupant_[u.occCell] == u.id)
        occupant_[u.occCell] = -1;
    if (u.reservedCell >= 0 && occupant_[u.reservedCell] == u.id)
        occupant_[u.reservedCell] = -1;
    u.occCell = u.reservedCell = -1;
    u.path.clear();
    u.destCell = -1;
    Event ev;
    ev.type = Event::UnitDied;
    ev.x = u.x;
    ev.y = u.y;
    ev.infantry = u.infantry;
    events_.push_back(ev);
}

void Sim::killStructure(Structure& s) {
    s.hp = 0; // purged at the end of tick()
    int scx = s.cell % kSize, scy = s.cell / kSize;
    for (int by = 0; by < s.h; by++)
        for (int bx = 0; bx < s.w; bx++)
            if (scx + bx < kSize && scy + by < kSize)
                blocked_[(scy + by) * kSize + scx + bx] = 0;
    Event ev;
    ev.type = Event::StructDied;
    ev.x = s.cx();
    ev.y = s.cy();
    events_.push_back(ev);
}

void Sim::tickUnit(Unit& u) {
    if (tickCombat(u))
        return; // standing and fighting
    if (u.harvester)
        tickHarvest(u); // may issue a path handled below
    if (u.path.empty()) {
        u.destCell = -1;
        return;
    }
    int next = u.path.front();
    int tx = cellCenter(next % kSize), ty = cellCenter(next / kSize);
    if (u.infantry) {
        // Keep the sub-cell spot while walking so squads don't stack.
        static const int kSubX[5] = {0, -6, 6, -6, 6};
        static const int kSubY[5] = {0, -6, -6, 6, 6};
        int sub = std::clamp(u.subcell, 0, 4);
        tx += kSubX[sub] * kLepton / 24;
        ty += kSubY[sub] * kLepton / 24;
    }

    // Rotate toward the waypoint.
    int desired = directionTo(tx - u.x, ty - u.y);
    int diff = ((desired - u.facing + 128) & 0xff) - 128;
    int step = std::clamp(diff, -u.stats.rot, u.stats.rot);
    u.facing = (u.facing + step) & 0xff;
    diff -= step;
    if (!u.infantry && std::abs(diff) > 32)
        return; // still pivoting in place

    // Reserve the next cell before entering it (vehicles/ships only).
    if (!u.infantry && next != u.occCell) {
        if (occupant_[next] != -1 && occupant_[next] != u.id) {
            u.blockedTicks++;
            if (u.blockedTicks == 8) {
                // Path around whoever is parked there.
                auto alt = findPath(u.occCell, u.destCell, u.stats.speedClass, u.id);
                if (!alt.empty()) {
                    u.path = std::move(alt);
                    return;
                }
            }
            if (u.blockedTicks > 45) { // ~3s: give up
                u.path.clear();
                u.destCell = -1;
                u.blockedTicks = 0;
            }
            return;
        }
        occupant_[next] = u.id;
        u.reservedCell = next;
        u.blockedTicks = 0;
    }

    // Translate toward the waypoint center.
    int pct = rules_->landSpeed(land_[u.cell()], u.stats.speedClass);
    int speed = u.stats.speed * (pct > 0 ? pct : 100) / 100;
    if (speed <= 0)
        speed = 1;
    int dx = tx - u.x, dy = ty - u.y;
    double dist = std::sqrt(double(dx) * dx + double(dy) * dy);
    if (dist <= speed) {
        u.x = tx;
        u.y = ty;
        if (!u.infantry) {
            if (u.occCell >= 0 && occupant_[u.occCell] == u.id && u.occCell != next)
                occupant_[u.occCell] = -1;
            u.occCell = next;
            u.reservedCell = -1;
        }
        u.path.erase(u.path.begin());
        if (u.path.empty())
            u.destCell = -1;
    } else {
        u.x += int(std::lround(dx * speed / dist));
        u.y += int(std::lround(dy * speed / dist));
    }
}

} // namespace game
