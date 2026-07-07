#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "game/rules.h"

namespace game {

// Deterministic simulation state: a passability grid plus mobile units.
// Distances are in leptons (256 per cell, like the original); the sim is
// advanced by discrete ticks (15/s) and knows nothing about rendering.
class Sim {
public:
    static constexpr int kSize = 128;      // grid is kSize x kSize cells
    static constexpr int kLepton = 256;    // leptons per cell

    struct Unit {
        int id = 0;
        std::string type;   // "jeep", "e1", ...
        std::string house;
        bool infantry = false;
        int subcell = 0;    // infantry draw spot (0 center, 1-4 corners)
        int x = 0, y = 0;   // center position, world leptons
        int facing = 0;     // DirType: 0-255, 0 = north, 64 = east (clockwise)
        int health = 256;   // 0-256 display scale (Phase 4: cosmetic only)
        UnitStats stats;
        bool selected = false;

        // Movement state.
        std::vector<uint16_t> path; // remaining waypoint cells, front = next
        int destCell = -1;          // final destination, -1 = idle
        int blockedTicks = 0;       // consecutive ticks stuck behind someone
        int occCell = -1;           // cell this unit owns (non-infantry)
        int reservedCell = -1;      // next cell claimed while in transit

        int cell() const { return (y / kLepton) * kSize + (x / kLepton); }
        bool moving() const { return destCell >= 0; }
    };

    Sim() : land_(kSize * kSize, Land::Clear), blocked_(kSize * kSize, 0),
            occupant_(kSize * kSize, -1), explored_(kSize * kSize, 0) {}

    // --- setup ---
    void setLand(int cell, Land land) { land_[cell] = land; }
    void setBlocked(int cell) { blocked_[cell] = 1; }
    void setRules(const Rules* rules) { rules_ = rules; }
    // House whose units lift the shroud (empty = reveal everything).
    void setPlayerHouse(std::string house) { playerHouse_ = std::move(house); }
    // Adds a unit at a cell center and marks occupancy. Returns its id.
    int addUnit(Unit u, int cell);

    // --- queries ---
    const std::vector<Unit>& units() const { return units_; }
    std::vector<Unit>& units() { return units_; }
    Land landAt(int cell) const { return land_[cell]; }
    // A cell a unit of this class may stand in (terrain + static blockers).
    bool passable(int cell, SpeedClass cls) const;

    // --- shroud ---
    bool explored(int cell) const {
        return playerHouse_.empty() || explored_[cell];
    }
    // Marks cells within `radius` cells of `cell` explored (RA shroud never
    // regrows). Used for player structures at load; units reveal as they move.
    void reveal(int cell, int radius);

    // --- commands ---
    // Orders each unit to the closest free cell around destCell (first unit
    // gets destCell itself). Ids that are invalid are ignored.
    void orderMove(const std::vector<int>& ids, int destCell);

    // Advance one tick: rotation, movement, cell hand-over, stuck handling.
    void tick();

    // 8-directional A*. Returns waypoint cells excluding `from`; empty if
    // start == goal or nothing is reachable. If the goal is unreachable the
    // path leads to the reachable cell closest to it. Cells occupied by units
    // other than `selfId` are avoided (except the goal itself).
    std::vector<uint16_t> findPath(int from, int to, SpeedClass cls,
                                   int selfId = -1) const;

private:
    int moveCost(int cell, SpeedClass cls, bool diagonal) const;
    void tickUnit(Unit& u);

    std::vector<Land> land_;
    std::vector<uint8_t> blocked_;  // static: structures, terrain objects, walls
    std::vector<int> occupant_;     // unit id per cell, -1 free (vehicles/ships)
    std::vector<uint8_t> explored_; // player-house shroud state
    std::vector<Unit> units_;
    const Rules* rules_ = nullptr;
    std::string playerHouse_;
    int nextId_ = 0;
};

// DirType (0=N, 64=E, clockwise) for a world-space delta; dy grows south.
int directionTo(int dx, int dy);

} // namespace game
