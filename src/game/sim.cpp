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

// Two houses are hostile if they differ and neither is the civilian "Neutral"
// house. There is no alliance model yet, so any two named sides are enemies.
bool housesEnemy(const std::string& a, const std::string& b) {
    if (a == b || a == "Neutral" || b == "Neutral")
        return false;
    return true;
}

// The "barracks" prerequisite flag: RA's barr/tent and TD's pyle (GDI) / hand
// (Nod) all satisfy each other, like the original's shared BARRACKS bit.
bool isBarracks(const std::string& t) {
    return t == "barr" || t == "tent" || t == "pyle" || t == "hand";
}

// The vehicle factory: GDI/RA build from the Weapons Factory (weap); TD Nod
// builds vehicles from the Airstrip (afld). Either satisfies the requirement.
bool isWarFactory(const std::string& t) { return t == "weap" || t == "afld"; }
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
    if (!u.house.empty() && u.house != "Neutral")
        combatants_.insert(u.house);
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
    if (!s.house.empty() && s.house != "Neutral")
        combatants_.insert(s.house);
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

bool Sim::hasAssets(const std::string& house) const {
    for (const auto& s : structures_)
        if (s.house == house && s.hp > 0)
            return true;
    for (const auto& u : units_)
        if (u.house == house && u.hp > 0)
            return true;
    return false;
}

void Sim::evaluateDefeat() {
    for (const auto& h : combatants_) {
        if (defeated_.count(h) || hasAssets(h))
            continue;
        defeated_.insert(h);
        Event ev;
        ev.type = Event::HouseDefeated;
        ev.house = h;
        events_.push_back(ev);
    }
}

std::string Sim::winner() const {
    if (combatants_.size() < 2)
        return {};
    const std::string* alive = nullptr;
    for (const auto& h : combatants_) {
        if (defeated_.count(h))
            continue;
        if (alive)
            return {}; // more than one side still standing
        alive = &h;
    }
    return alive ? *alive : std::string{};
}

bool Sim::footprintOf(const std::string& type, int& w, int& h) const {
    auto it = footprint_.find(type);
    if (it == footprint_.end())
        return false;
    w = it->second.first;
    h = it->second.second;
    return true;
}

int Sim::aiFindBuildSpot(const std::string& house, int w, int h) const {
    // Anchor the search on the construction yard, else any owned structure.
    const Structure* anchor = nullptr;
    for (const auto& s : structures_) {
        if (s.house != house)
            continue;
        anchor = &s;
        if (s.type == "fact")
            break;
    }
    if (!anchor)
        return -1;
    int ax = anchor->cell % kSize, ay = anchor->cell / kSize;
    // Spiral outward; canPlace enforces "footprint free + adjacent to a
    // friendly structure", so the base stays contiguous.
    for (int r = 1; r < 16; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                int cx = ax + dx, cy = ay + dy;
                if (cx < 0 || cy < 0 || cx + w > kSize || cy + h > kSize)
                    continue;
                if (canPlace(house, cy * kSize + cx, w, h))
                    return cy * kSize + cx;
            }
    return -1;
}

void Sim::spawnTeam(const std::string& teamName) {
    auto it = teamTypes_.find(teamName);
    if (it == teamTypes_.end())
        return;
    const TeamTypeDef& tt = it->second;
    // Anchor: the team's construction yard / first structure, else first unit,
    // else the first valid waypoint (reinforcements with no base yet).
    int anchor = -1;
    for (const auto& s : structures_)
        if (s.house == tt.house) { anchor = s.cell; if (s.type == "fact") break; }
    if (anchor < 0)
        for (const auto& u : units_)
            if (u.house == tt.house) { anchor = u.cell(); break; }
    if (anchor < 0)
        for (int c : waypoints_)
            if (c >= 0) { anchor = c; break; }
    if (anchor < 0)
        return;
    // A scripted mission list makes this a coordinated team (drive it via
    // tickTeams); reserve its id up front so members can be tagged as spawned.
    int teamId = -1;
    if (!tt.script.empty())
        teamId = nextTeamId_++;
    int ax = anchor % kSize, ay = anchor / kSize;
    bool anyPlaced = false;
    for (const auto& m : tt.roster) {
        if (m.type == "lst" || m.type == "boat") // no naval spawn support yet
            continue;
        for (int n = 0; n < m.count; n++) {
            Unit u;
            u.type = m.type;
            u.house = tt.house;
            u.infantry = m.infantry;
            u.stats = rules_->unit(m.type, m.infantry ? UnitKind::Infantry
                                                      : UnitKind::Vehicle);
            u.turreted = hasTurretArt(m.type);
            u.harvester = m.type == "harv";
            u.facing = 128;
            u.teamId = teamId;
            // Spiral out from the anchor for a free, passable cell.
            bool placed = false;
            for (int r = 0; r < 12 && !placed; r++)
                for (int dy = -r; dy <= r && !placed; dy++)
                    for (int dx = -r; dx <= r && !placed; dx++) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r)
                            continue;
                        int cx = ax + dx, cy = ay + dy;
                        if (cx < 0 || cx >= kSize || cy < 0 || cy >= kSize)
                            continue;
                        int c = cy * kSize + cx;
                        if (!passable(c, u.stats.speedClass))
                            continue;
                        if (!u.infantry && occupant_[c] != -1)
                            continue;
                        addUnit(std::move(u), c);
                        placed = true;
                        anyPlaced = true;
                    }
        }
    }
    if (teamId >= 0 && anyPlaced)
        teams_.push_back({teamId, tt.house, tt.script, 0, 0});
}

void Sim::runTriggerAction(const TriggerDef& d) {
    if (d.action == "Win")
        missionResult_ = MissionResult::Won;
    else if (d.action == "Lose")
        missionResult_ = MissionResult::Lost;
    else if (d.action == "Reinforce." || d.action == "Create Team")
        spawnTeam(d.team);
    else if (d.action == "Production" || d.action == "Autocreate")
        setAI(d.house, true); // wake the house's base-building / attack AI
    // Other actions (superweapons, DZ, trigger chaining) are not modeled yet.
}

void Sim::tickTriggers() {
    // Every TICKS_PER_MINUTE/10 ticks (~6s), starting at tick 90 (not 0), so a
    // Time trigger with Data=N fires after N*90 ticks like the original.
    if (triggers_.empty() || tickCount_ == 0 || tickCount_ % 90 != 0)
        return;
    bool anyDead = false;
    for (auto& ts : triggers_) {
        if (ts.dead)
            continue;
        const TriggerDef& d = ts.def;
        bool fire = false;
        if (d.event == "Time") {
            if (--ts.counter <= 0) {
                fire = true;
                ts.counter = d.data; // re-arm (matters only for persistent ones)
            }
        } else if (d.event == "All Destr.") {
            fire = houseDefeated(d.house);
        } else if (d.event == "Bldgs Destr." || d.event == "No Factories") {
            bool any = false;
            for (const auto& s : structures_)
                if (s.house == d.house) { any = true; break; }
            fire = !any && combatants_.count(d.house) != 0;
        }
        if (!fire)
            continue;
        runTriggerAction(d);
        if (!d.persist) {
            ts.dead = true;
            anyDead = true;
        }
    }
    if (anyDead)
        triggers_.erase(std::remove_if(triggers_.begin(), triggers_.end(),
                                       [](const TriggerState& t) { return t.dead; }),
                        triggers_.end());
}

void Sim::tickAI(const std::string& house) {
    const bool nod = house == "BadGuy"; // side-specific building/unit choices

    // 1. Deploy the MCV into a construction yard if we don't have one yet.
    bool hasFact = false;
    for (const auto& s : structures_)
        if (s.house == house && s.type == "fact")
            hasFact = true;
    if (!hasFact) {
        int w = 0, h = 0;
        if (footprintOf("fact", w, h))
            for (const auto& u : units_)
                if (u.house == house && u.type == "mcv" && !u.moving()) {
                    deployMcv(u.id, w, h); // erases the MCV; stop touching units_
                    hasFact = true;
                    break;
                }
    }

    // 2. Tally what the base already has.
    bool hasPower = false, hasProc = false, hasBarr = false, hasWar = false;
    hasFact = false;
    for (const auto& s : structures_) {
        if (s.house != house)
            continue;
        if (s.type == "fact") hasFact = true;
        else if (s.type == "nuke" || s.type == "nuk2") hasPower = true;
        else if (s.type == "proc") hasProc = true;
        else if (isBarracks(s.type)) hasBarr = true;
        else if (isWarFactory(s.type)) hasWar = true;
    }

    // 3. Buildings: place a finished one, else queue the next in the tech chain.
    Production& bslot = prod_[house].slot[size_t(ProdCat::Building)];
    if (bslot.active() && bslot.ready) {
        int w = 0, h = 0;
        if (footprintOf(bslot.type, w, h)) {
            int cell = aiFindBuildSpot(house, w, h);
            if (cell >= 0)
                placeBuilding(house, cell, w, h);
        }
    } else if (hasFact && !bslot.active()) {
        int produced = 0, drained = 0;
        power(house, produced, drained);
        std::string want;
        if (!hasPower || produced < drained + 20)
            want = "nuke"; // power plant (keep a comfortable surplus)
        else if (!hasProc)
            want = "proc"; // refinery (needs a power plant)
        else if (!hasBarr)
            want = nod ? "hand" : "pyle";
        else if (!hasWar)
            want = nod ? "afld" : "weap";
        if (!want.empty())
            startProduction(house, want, UnitKind::Structure);
    }

    // 4. Infantry: keep a small rifle squad coming while we have a barracks.
    Production& islot = prod_[house].slot[size_t(ProdCat::Infantry)];
    if (hasBarr && !islot.active()) {
        int inf = 0;
        for (const auto& u : units_)
            if (u.house == house && u.infantry)
                inf++;
        if (inf < 8)
            startProduction(house, "e1", UnitKind::Infantry);
    }

    // 5. Vehicles: a harvester or two first (economy), then a tank line.
    Production& vslot = prod_[house].slot[size_t(ProdCat::Vehicle)];
    if (hasWar && !vslot.active()) {
        int harv = 0, tanks = 0;
        for (const auto& u : units_)
            if (u.house == house) {
                if (u.harvester) harv++;
                else if (!u.infantry && u.type != "mcv") tanks++;
            }
        if (hasProc && harv < 2)
            startProduction(house, "harv", UnitKind::Vehicle);
        else if (tanks < 6)
            startProduction(house, nod ? "ltnk" : "mtnk", UnitKind::Vehicle);
    }

    // 6. Idle harvesters go find ore.
    for (auto& u : units_)
        if (u.house == house && u.harvester &&
            u.harvMode == Unit::Harv::None && !u.moving() && u.path.empty()) {
            int ore = findOre(u.cell());
            if (ore >= 0)
                orderHarvest({u.id}, ore);
        }

    // 7. Attack waves: once we've massed enough armed units, send them all at
    // the nearest enemy structure (else nearest enemy unit) on a cooldown.
    int& cd = aiAttackCd_[house];
    if (cd > 0)
        cd--;
    std::vector<int> force;
    for (const auto& u : units_) {
        if (u.house != house || u.harvester || u.type == "mcv" || !u.stats.primary)
            continue;
        force.push_back(u.id);
    }
    constexpr int kAttackThreshold = 5;
    constexpr int kAttackPeriod = 450; // ~30s between waves at 15 ticks/s
    if (cd == 0 && int(force.size()) >= kAttackThreshold) {
        long long bestD = -1;
        int targUnit = -1, targStruct = -1;
        int ax = 0, ay = 0;
        for (const auto& s : structures_)
            if (s.house == house) { ax = s.cx(); ay = s.cy(); break; }
        for (const auto& s : structures_) {
            if (!housesEnemy(house, s.house))
                continue;
            long long dx = s.cx() - ax, dy = s.cy() - ay, d = dx * dx + dy * dy;
            if (bestD < 0 || d < bestD) { bestD = d; targStruct = s.id; targUnit = -1; }
        }
        if (targStruct < 0)
            for (const auto& u : units_) {
                if (!housesEnemy(house, u.house))
                    continue;
                long long dx = u.x - ax, dy = u.y - ay, d = dx * dx + dy * dy;
                if (bestD < 0 || d < bestD) { bestD = d; targUnit = u.id; targStruct = -1; }
            }
        if (targUnit >= 0 || targStruct >= 0) {
            orderAttack(force, targUnit, targStruct);
            cd = kAttackPeriod;
        }
    }
}

void Sim::findNearestEnemy(const std::string& house, int fx, int fy,
                           bool preferStruct, int& tu, int& ts) const {
    tu = ts = -1;
    auto nearestStruct = [&](long long& bestD) {
        int best = -1;
        for (const auto& s : structures_) {
            if (s.hp <= 0 || !housesEnemy(house, s.house))
                continue;
            long long dx = s.cx() - fx, dy = s.cy() - fy, d = dx * dx + dy * dy;
            if (best < 0 || d < bestD) { bestD = d; best = s.id; }
        }
        return best;
    };
    auto nearestUnit = [&](long long& bestD) {
        int best = -1;
        for (const auto& u : units_) {
            if (u.hp <= 0 || !housesEnemy(house, u.house))
                continue;
            long long dx = u.x - fx, dy = u.y - fy, d = dx * dx + dy * dy;
            if (best < 0 || d < bestD) { bestD = d; best = u.id; }
        }
        return best;
    };
    long long bestD = -1;
    if (preferStruct) {
        ts = nearestStruct(bestD);
        if (ts < 0) { bestD = -1; tu = nearestUnit(bestD); }
    } else {
        tu = nearestUnit(bestD);
        if (tu < 0) { bestD = -1; ts = nearestStruct(bestD); }
    }
}

void Sim::tickStandingOrders() {
    if (tickCount_ % 15 != 0) // coordinate on the ~1s cadence
        return;
    for (auto& u : units_) {
        // Skip units the player commands, the skirmish AI runs, team members
        // (driven by tickTeams), harvesters, and anything already busy.
        if (u.hp <= 0 || u.teamId >= 0 || u.harvester)
            continue;
        if (u.house == playerHouse_ || aiHouses_.count(u.house))
            continue;
        if (u.hasTarget() || u.moving() || !u.path.empty())
            continue;
        if (u.order == Unit::Order::Hunt) {
            int tu = -1, ts = -1;
            findNearestEnemy(u.house, u.x, u.y, /*preferStruct=*/false, tu, ts);
            if (tu >= 0 || ts >= 0)
                orderAttack({u.id}, tu, ts);
        } else if (u.order == Unit::Order::AreaGuard && u.orderCell >= 0) {
            // Return to post if pulled away (auto-acquire keeps us defending;
            // this just re-centers a unit that chased or was shoved off).
            int dx = std::abs(u.cell() % kSize - u.orderCell % kSize);
            int dy = std::abs(u.cell() / kSize - u.orderCell / kSize);
            if (std::max(dx, dy) > 3)
                orderMove({u.id}, u.orderCell);
        }
        // Guard: hold; tickAutoAcquire already engages anything in range.
    }
}

void Sim::tickTeams() {
    if (teams_.empty())
        return;
    // Drop teams whose members are all dead.
    teams_.erase(std::remove_if(teams_.begin(), teams_.end(),
                    [&](const Team& t) {
                        for (const auto& u : units_)
                            if (u.teamId == t.id && u.hp > 0)
                                return false;
                        return true;
                    }),
                 teams_.end());
    if (tickCount_ % 15 != 0) // coordinate on the ~1s cadence
        return;
    for (auto& team : teams_) {
        if (team.step >= int(team.script.size()))
            continue; // script finished: members hold / auto-acquire
        std::vector<int> members;
        for (const auto& u : units_)
            if (u.teamId == team.id && u.hp > 0)
                members.push_back(u.id);
        if (members.empty())
            continue;
        const TeamTypeDef::Step& st = team.script[team.step];
        team.stepTicks++;
        const std::string& m = st.mission;

        if (m == "Move" || m == "Move to Cell") {
            int target = -1;
            if (m == "Move") {
                if (st.arg >= 0 && st.arg < int(waypoints_.size()))
                    target = waypoints_[st.arg];
            } else if (st.arg >= 0 && st.arg < kSize * kSize) {
                target = st.arg;
            }
            if (target < 0) { team.step++; team.stepTicks = 0; continue; }
            bool allArrived = true;
            for (int id : members) {
                const Unit* u = findUnit(id);
                int dx = std::abs(u->cell() % kSize - target % kSize);
                int dy = std::abs(u->cell() / kSize - target / kSize);
                if (std::max(dx, dy) > 2) { allArrived = false; break; }
            }
            if (allArrived) { team.step++; team.stepTicks = 0; continue; }
            std::vector<int> idle; // (re)issue movement for stopped members
            for (int id : members) {
                const Unit* u = findUnit(id);
                if (!u->moving() && u->path.empty())
                    idle.push_back(id);
            }
            if (!idle.empty())
                orderMove(idle, target);
            if (team.stepTicks > 40) { team.step++; team.stepTicks = 0; } // stuck
        } else if (m.rfind("Attack", 0) == 0 || m == "Rampage") {
            bool preferStruct = (m == "Attack Base");
            for (int id : members) {
                Unit* u = mutableUnit(id);
                if (!u || u->hasTarget() || u->moving() || !u->path.empty())
                    continue;
                int tu = -1, ts = -1;
                findNearestEnemy(team.house, u->x, u->y, preferStruct, tu, ts);
                if (tu >= 0 || ts >= 0)
                    orderAttack({id}, tu, ts);
            }
            // Terminal: hold on this step, re-engaging as targets fall.
        } else if (m == "Loop") {
            team.step = 0;
            team.stepTicks = 0;
        } else {
            // Guard / Guard Area / Defend Base / Retreat / Unload / unknown:
            // hold for the scripted duration (arg ~seconds) then advance;
            // members auto-acquire meanwhile.
            if (team.stepTicks > std::max(1, st.arg)) {
                team.step++;
                team.stepTicks = 0;
            }
        }
    }
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

        // A move order cancels any attack (ordered or auto-acquired).
        u.targetUnit = u.targetStruct = -1;
        u.autoTarget = false;

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
        u->autoTarget = false; // explicit player order: chase, don't disengage
    }
}

void Sim::tickAutoAcquire(Unit& u) {
    // Only truly idle armed combatants scan: no target, no orders, not a
    // harvester. This keeps guarding units at their post and lets units that
    // just finished a move (or whose ordered target died) engage on their own.
    if (u.hasTarget() || u.moving() || !u.path.empty() || u.harvester)
        return;
    const WeaponStats* w = u.stats.primary;
    if (!w || w->range <= 0)
        return;

    long long bestD = (long long)w->range * w->range; // inclusive of edge
    int bestUnit = -1, bestStruct = -1;
    for (const auto& t : units_) {
        if (t.id == u.id || t.hp <= 0 || !housesEnemy(u.house, t.house))
            continue;
        long long dx = t.x - u.x, dy = t.y - u.y;
        long long d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            bestUnit = t.id;
            bestStruct = -1;
        }
    }
    for (const auto& s : structures_) {
        if (s.hp <= 0 || !housesEnemy(u.house, s.house))
            continue;
        long long dx = s.cx() - u.x, dy = s.cy() - u.y;
        long long d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            bestStruct = s.id;
            bestUnit = -1;
        }
    }
    if (bestUnit >= 0 || bestStruct >= 0) {
        u.targetUnit = bestUnit;
        u.targetStruct = bestStruct;
        u.autoTarget = true;
    }
}

void Sim::orderHarvest(const std::vector<int>& ids, int cell) {
    for (int id : ids) {
        Unit* u = mutableUnit(id);
        if (!u || !u->harvester)
            continue;
        u->targetUnit = u->targetStruct = -1;
        u->autoTarget = false;
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
                // Barracks/war-factory flags: the interchangeable buildings
                // (barr/tent/pyle/hand and weap/afld) satisfy each other.
                (isBarracks(need) && isBarracks(s.type)) ||
                (isWarFactory(need) && isWarFactory(s.type))) {
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

bool Sim::canProduce(const std::string& house, const std::string& type,
                     UnitKind kind) const {
    if (!rules_ || rules_->unit(type, kind).cost <= 0)
        return false;
    // The producing factory itself is an implicit prerequisite.
    std::string factory = kind == UnitKind::Structure ? "fact"
                          : kind == UnitKind::Infantry ? "barr" : "weap";
    return prereqsMet(house, factory) &&
           prereqsMet(house, rules_->unit(type, kind).prereq);
}

bool Sim::startProduction(const std::string& house, const std::string& type,
                          UnitKind kind) {
    Production& slot = prod_[house].slot[size_t(catOf(kind))];
    if (slot.active() || !canProduce(house, type, kind))
        return false;
    const UnitStats& stats = rules_->unit(type, kind);
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

bool Sim::cellBuildable(int cell) const {
    int cx = cell % kSize, cy = cell / kSize;
    if (cx < 0 || cy < 0 || cx >= kSize || cy >= kSize)
        return false;
    return !blocked_[cell] && occupant_[cell] == -1 &&
           rules_->landBuildable(land_[cell]);
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
    bool isRefinery = st.type == "proc";
    slot = Production{};
    int id = addStructure(std::move(st));
    if (id >= 0 && isRefinery)
        grantHarvester(house, cell, w, h); // refineries ship with a harvester
    return id;
}

void Sim::grantHarvester(const std::string& house, int procCell, int w, int h) {
    if (!rules_ || rules_->unit("harv", UnitKind::Vehicle).cost <= 0)
        return;
    Unit u;
    u.type = "harv";
    u.house = house;
    u.stats = rules_->unit("harv", UnitKind::Vehicle);
    u.harvester = true;
    u.facing = 128;
    int px = procCell % kSize, py = procCell / kSize;
    // Spiral out from just below the footprint for a free, passable cell.
    for (int r = 0; r < 8; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                int x = px + w / 2 + dx, y = py + h + dy;
                if (x < 0 || x >= kSize || y < 0 || y >= kSize)
                    continue;
                int c = y * kSize + x;
                if (!passable(c, u.stats.speedClass) || occupant_[c] != -1)
                    continue;
                int id = addUnit(std::move(u), c);
                int ore = findOre(c);
                if (ore >= 0)
                    orderHarvest({id}, ore);
                return;
            }
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

int Sim::structureAt(int cell) const {
    int cx = cell % kSize, cy = cell / kSize;
    for (const auto& s : structures_) {
        if (s.hp <= 0)
            continue;
        int sx = s.cell % kSize, sy = s.cell / kSize;
        if (cx >= sx && cx < sx + s.w && cy >= sy && cy < sy + s.h)
            return s.id;
    }
    return -1;
}

int Sim::sellStructure(int id) {
    for (auto& s : structures_) {
        if (s.id != id || s.hp <= 0)
            continue;
        int refund = s.stats.cost / 2;
        credits_[s.house] += refund;
        killStructure(s); // unblocks footprint, hp=0 (purged), StructDied event
        return refund;
    }
    return 0;
}

bool Sim::toggleRepair(int id) {
    for (auto& s : structures_) {
        if (s.id == id && s.hp > 0) {
            s.repairing = !s.repairing;
            return s.repairing;
        }
    }
    return false;
}

bool Sim::spawnProduced(const std::string& house, const Production& p) {
    // Find the producing factory.
    const Structure* fac = nullptr;
    for (const auto& s : structures_) {
        if (s.house != house)
            continue;
        if (p.kind == UnitKind::Infantry ? isBarracks(s.type)
                                         : isWarFactory(s.type)) {
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
    // Skirmish AI thinks ~once a second, staggered per house so several AIs
    // don't all fire on the same tick. Runs before movement/production so its
    // orders take effect this tick. Deterministic: keyed off tickCount_ only.
    if (!aiHouses_.empty()) {
        int stagger = 0;
        for (const auto& h : aiHouses_) {
            if ((tickCount_ + stagger) % 15 == 0 && !defeated_.count(h))
                tickAI(h);
            stagger += 5;
        }
    }
    tickTriggers();
    // Scripted enemy behaviour on scenario maps: per-unit standing orders
    // (Hunt / Area Guard) and coordinated team scripts. Skips player and
    // skirmish-AI houses, so trigger-less skirmish maps are unaffected.
    tickStandingOrders();
    tickTeams();
    tickCount_++;
    tickProduction();
    for (auto& u : units_) {
        bool wasMoving = u.moving();
        tickUnit(u);
        if (wasMoving && !playerHouse_.empty() && u.house == playerHouse_)
            reveal(u.cell(), u.stats.sight);
    }
    tickProjectiles();
    // Sidebar Repair: heal flagged structures, draining credits (stalls if broke).
    for (auto& s : structures_) {
        if (!s.repairing || s.hp <= 0)
            continue;
        if (s.hp >= s.stats.strength) {
            s.repairing = false;
            continue;
        }
        int step = std::max(1, s.stats.strength / 150);
        int due = std::max(1, s.stats.cost * step / std::max(1, s.stats.strength * 2));
        if (credits_[s.house] < due)
            continue; // stalled until funds arrive
        credits_[s.house] -= due;
        s.hp = std::min(s.stats.strength, s.hp + step);
        if (s.hp >= s.stats.strength)
            s.repairing = false;
    }
    units_.erase(std::remove_if(units_.begin(), units_.end(),
                                [](const Unit& u) { return u.hp <= 0; }),
                 units_.end());
    structures_.erase(std::remove_if(structures_.begin(), structures_.end(),
                                     [](const Structure& s) { return s.hp <= 0; }),
                      structures_.end());
    evaluateDefeat();
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
        // Auto-acquired (guard) targets are not chased: a unit holds its
        // post and simply drops the target once it leaves weapon range,
        // re-scanning next tick for anything still nearby.
        if (u.autoTarget) {
            u.targetUnit = u.targetStruct = -1;
            return false;
        }
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
        Event ev;
        ev.type = Event::Fire;
        ev.x = u.x;
        ev.y = u.y;
        ev.weapon = w;
        events_.push_back(ev);
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
    tickAutoAcquire(u);
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
