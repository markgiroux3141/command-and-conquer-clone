#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
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
        bool turreted = false; // art has a turret; aim with turretFacing
        int subcell = 0;    // infantry draw spot (0 center, 1-4 corners)
        int x = 0, y = 0;   // center position, world leptons
        int facing = 0;     // DirType: 0-255, 0 = north, 64 = east (clockwise)
        int turretFacing = 0;
        int hp = -1;        // hit points; addUnit defaults to stats.strength
        UnitStats stats;
        bool selected = false;

        // Movement state.
        std::vector<uint16_t> path; // remaining waypoint cells, front = next
        int destCell = -1;          // final destination, -1 = idle
        int blockedTicks = 0;       // consecutive ticks stuck behind someone
        int occCell = -1;           // cell this unit owns (non-infantry)
        int reservedCell = -1;      // next cell claimed while in transit

        // Combat state.
        int targetUnit = -1;   // unit id being attacked, -1 none
        int targetStruct = -1; // structure id being attacked, -1 none
        int cooldown = 0;      // ticks until the weapon may fire again
        bool autoTarget = false; // target was auto-acquired (guard), not ordered

        // Harvester state.
        enum class Harv { None, ToOre, Harvest, ToRef, Unload };
        bool harvester = false;
        Harv harvMode = Harv::None;
        int harvCell = -1;     // where to look for ore (locality anchor)
        int harvTimer = 0;
        int bails = 0, gemBails = 0;

        int cell() const { return (y / kLepton) * kSize + (x / kLepton); }
        bool moving() const { return destCell >= 0; }
        bool hasTarget() const { return targetUnit >= 0 || targetStruct >= 0; }
        // 0-256 health fraction for UI bars.
        int healthFrac() const {
            return std::clamp(hp * 256 / std::max(1, stats.strength), 0, 256);
        }
    };

    // A static, attackable building. Footprint cells are blocked while alive.
    struct Structure {
        int id = 0;
        std::string type;
        std::string house;
        int cell = 0;       // top-left footprint cell
        int w = 1, h = 1;   // footprint, cells
        int hp = 1;
        UnitStats stats;
        // Center in world leptons.
        int cx() const { return (cell % kSize) * kLepton + w * kLepton / 2; }
        int cy() const { return (cell / kSize) * kLepton + h * kLepton / 2; }
    };

    // A shot in flight. Homes on its target's current position.
    struct Projectile {
        int x = 0, y = 0;      // leptons
        int tx = 0, ty = 0;    // last known target position
        int targetUnit = -1, targetStruct = -1;
        const WeaponStats* weapon = nullptr;
        int facing = 0;        // for directional bullet art
    };

    // One sidebar strip each: construction yard, barracks/tent, war factory.
    enum class ProdCat : uint8_t { Building, Infantry, Vehicle, Count };

    // One in-progress item (per house, per category). Credits are paid as
    // progress advances (like the original), so cancelling refunds `paid`.
    struct Production {
        std::string type;   // empty = slot idle
        UnitKind kind = UnitKind::Vehicle;
        int cost = 0;
        int ticksTotal = 1;
        int progress = 0;   // 0..ticksTotal
        int paid = 0;       // credits taken so far
        int powAcc = 0;     // power-fraction accumulator (256 = 1 tick)
        bool ready = false; // finished; buildings await placement
        bool active() const { return !type.empty(); }
        int frac256() const { return progress * 256 / std::max(1, ticksTotal); }
    };

    // Things that happened during the last tick(); the shell turns these
    // into animations/sound. Cleared at the start of every tick.
    struct Event {
        enum Type { Impact, UnitDied, StructDied, OreDepleted, Fire } type;
        int x = 0, y = 0;              // world leptons
        int cell = -1;                 // OreDepleted: cell to redraw
        int damage = 0;                // damage dealt (anim size selection)
        const WarheadStats* warhead = nullptr; // Impact only
        const WeaponStats* weapon = nullptr;   // Fire only (for the report SFX)
        bool infantry = false;         // target was infantry
    };

    Sim() : land_(kSize * kSize, Land::Clear), blocked_(kSize * kSize, 0),
            occupant_(kSize * kSize, -1), explored_(kSize * kSize, 0),
            oreBails_(kSize * kSize, 0), oreGem_(kSize * kSize, 0) {}

    // --- setup ---
    void setLand(int cell, Land land) { land_[cell] = land; }
    void setBlocked(int cell) { blocked_[cell] = 1; }
    void setOre(int cell, int bails, bool gem) {
        oreBails_[cell] = uint8_t(std::clamp(bails, 0, 255));
        oreGem_[cell] = gem;
    }
    void setRules(const Rules* rules) { rules_ = rules; }
    // House whose units lift the shroud (empty = reveal everything).
    void setPlayerHouse(std::string house) { playerHouse_ = std::move(house); }
    // Adds a unit at a cell center and marks occupancy. Returns its id.
    int addUnit(Unit u, int cell);
    // Adds a structure and blocks its footprint. Returns its id.
    int addStructure(Structure s);

    // --- queries ---
    const std::vector<Unit>& units() const { return units_; }
    std::vector<Unit>& units() { return units_; }
    const std::vector<Structure>& structures() const { return structures_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<Event>& events() const { return events_; }
    const Unit* findUnit(int id) const;
    const Structure* findStructure(int id) const;
    Land landAt(int cell) const { return land_[cell]; }
    int oreAt(int cell) const { return oreBails_[cell]; }
    int credits(const std::string& house) const {
        auto it = credits_.find(house);
        return it == credits_.end() ? 0 : it->second;
    }
    // Power produced (+ terms) and drained (- terms) by a house's structures.
    void power(const std::string& house, int& produced, int& drained) const;
    // Power_Fraction (HOUSE.CPP): produced/drained clamped to [0,256].
    // Production speed in Phase 6 scales by this; 256 = full power.
    int powerFraction(const std::string& house) const {
        int produced = 0, drained = 0;
        power(house, produced, drained);
        if (drained <= 0 || produced >= drained)
            return 256;
        return produced * 256 / drained;
    }
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

    // Orders units to attack a unit or structure (one of the ids, the other
    // -1). Units chase into weapon range, then fire on their ROF cooldown.
    void orderAttack(const std::vector<int>& ids, int targetUnit, int targetStruct);

    // Orders harvesters to gather ore around `cell`, then shuttle it to the
    // nearest friendly refinery ("proc") indefinitely. Non-harvesters ignore.
    void orderHarvest(const std::vector<int>& ids, int cell);

    // --- production ---
    void setCredits(const std::string& house, int amount) {
        credits_[house] = amount;
    }
    // Starts building `type` in its category slot. Fails (false) if the slot
    // is busy, the type has no cost, prerequisites aren't met, or the house
    // lacks the producing factory (fact/barr|tent/weap).
    bool startProduction(const std::string& house, const std::string& type,
                         UnitKind kind);
    const Production* production(const std::string& house, ProdCat cat) const;
    // Refunds what has been paid and clears the slot.
    void cancelProduction(const std::string& house, ProdCat cat);
    // All Prerequisite= structures present? (barr and tent count as each
    // other, like the original's shared BARRACKS flag.)
    bool prereqsMet(const std::string& house, const std::string& prereq) const;
    // Every footprint cell buildable + free, and touching (Chebyshev 1) a
    // friendly structure.
    bool canPlace(const std::string& house, int cell, int w, int h) const;
    // Places the ready Building-slot structure; returns its id or -1.
    int placeBuilding(const std::string& house, int cell, int w, int h);
    // Turns an MCV into a construction yard ("fact", w x h footprint whose
    // top-left is one cell up-left of the MCV). Returns struct id or -1.
    int deployMcv(int unitId, int w, int h);

    // Advance one tick: rotation, movement, cell hand-over, stuck handling,
    // combat (aim/fire), projectile flight, damage and deaths.
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
    // Idle armed unit locks onto the nearest enemy in weapon range (guard /
    // return-fire). No-op if the unit already has a target or is busy.
    void tickAutoAcquire(Unit& u);
    // Aim (body or turret) and fire at the target; true if u should not move.
    bool tickCombat(Unit& u);
    // Harvester gather/shuttle state machine; issues paths via the normal
    // movement machinery.
    void tickHarvest(Unit& u);
    // Nearest cell with ore, spiraling out from `from`; -1 if none.
    int findOre(int from) const;
    // Advance all houses' production slots (drip pay + power scaling) and
    // spawn finished units at their factory.
    void tickProduction();
    // Spawn a finished unit next to its factory; false if no room yet.
    bool spawnProduced(const std::string& house, const Production& p);
    void tickProjectiles();
    // Applies weapon damage to whichever target the projectile chased.
    void impact(const Projectile& p);
    void killUnit(Unit& u);
    void killStructure(Structure& s);
    Unit* mutableUnit(int id);
    Structure* mutableStructure(int id);
    // Target center position; false if the target no longer exists.
    bool targetPos(int targetUnit, int targetStruct, int& x, int& y) const;

    std::vector<Land> land_;
    std::vector<uint8_t> blocked_;  // static: structures, terrain objects, walls
    std::vector<int> occupant_;     // unit id per cell, -1 free (vehicles/ships)
    std::vector<uint8_t> explored_; // player-house shroud state
    std::vector<uint8_t> oreBails_; // harvestable bails per cell
    std::vector<uint8_t> oreGem_;   // 1 = the cell's ore is gems
    std::unordered_map<std::string, int> credits_;
    struct HouseProd {
        Production slot[size_t(ProdCat::Count)];
    };
    std::unordered_map<std::string, HouseProd> prod_;
    std::vector<Unit> units_;
    std::vector<Structure> structures_;
    std::vector<Projectile> projectiles_;
    std::vector<Event> events_;
    const Rules* rules_ = nullptr;
    std::string playerHouse_;
    int nextId_ = 0;
    int nextStructId_ = 0;
};

// DirType (0=N, 64=E, clockwise) for a world-space delta; dy grows south.
int directionTo(int dx, int dy);

} // namespace game
