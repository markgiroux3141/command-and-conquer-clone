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
    units_.push_back(std::move(u));
    return units_.back().id;
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

void Sim::tick() {
    for (auto& u : units_)
        tickUnit(u);
}

void Sim::tickUnit(Unit& u) {
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
