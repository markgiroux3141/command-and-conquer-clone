#pragma once
#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
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

        // Scripted standing order (scenario maps; ignored for the player house
        // and for houses under the skirmish AI). Guard/AreaGuard hold position
        // and return fire; Hunt actively seeks the nearest enemy. From the INI
        // per-unit mission (Guard/Area Guard/Hunt/...). See Sim::tickStandingOrders.
        enum class Order { Guard, AreaGuard, Hunt };
        Order order = Order::Guard;
        int orderCell = -1;    // AreaGuard: home cell to leash back to
        int teamId = -1;       // spawned-TeamType membership (-1 = none)

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
        bool repairing = false; // sidebar Repair toggled on (heals, drains credits)
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

    // Scenario scripting (Phase 7). A trigger is an event → action pair; a
    // TeamType is a named squad roster a trigger can spawn. The shell translates
    // the parsed map sections into these and hands them to the sim.
    enum class MissionResult { None, Won, Lost };
    // AI aggression tier (DifficultyClass). Sets the attack-wave cadence
    // (HouseClass::AI AlertTime, §6) and the starting credit stipend.
    enum class Difficulty { Easy, Normal, Hard };
    struct TriggerDef {
        std::string name, event, action, house, team;
        int data = 0;       // "Time": fires after data trigger-checks (×90 ticks)
        bool persist = false; // false = fire once then remove (volatile)
    };
    struct TeamTypeDef {
        std::string name, house;
        struct Member { std::string type; int count = 0; bool infantry = false; };
        std::vector<Member> roster;
        // Scripted mission list (TEAMTYPE.CPP). Replayed by tickTeams to drive
        // spawned squads: e.g. Move:0, Move:1, Move:2, Attack Units (a patrol
        // that walks waypoints then attacks). Empty = spawn and hold.
        struct Step { std::string mission; int arg = 0; };
        std::vector<Step> script;
    };

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
        enum Type { Impact, UnitDied, StructDied, OreDepleted, Fire,
                    HouseDefeated } type;
        int x = 0, y = 0;              // world leptons
        int cell = -1;                 // OreDepleted: cell to redraw
        int damage = 0;                // damage dealt (anim size selection)
        const WarheadStats* warhead = nullptr; // Impact only
        const WeaponStats* weapon = nullptr;   // Fire only (for the report SFX)
        bool infantry = false;         // target was infantry
        std::string house;             // HouseDefeated: the house that lost
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

    // --- win / lose ---
    // Combatant houses (every non-Neutral house that has ever owned an asset).
    const std::set<std::string>& combatants() const { return combatants_; }
    // Houses that have been eliminated (no structures and no units left). Once
    // latched a house stays defeated. Emits an Event::HouseDefeated the tick it
    // falls (TD HouseClass::MPlayer_Defeated / IsDefeated).
    const std::set<std::string>& defeatedHouses() const { return defeated_; }
    bool houseDefeated(const std::string& house) const {
        return defeated_.count(house) != 0;
    }
    // The sole surviving combatant once every other combatant is defeated, else
    // empty. Requires at least two combatants (a lone side never "wins").
    std::string winner() const;
    bool gameOver() const { return !winner().empty(); }

    // --- skirmish AI ---
    // Turn a simple base-building + attack AI on/off for a house. AI houses
    // "think" on a fixed cadence (driven off the tick counter, so the sim stays
    // deterministic) and reuse the ordinary production/placement/order paths:
    // deploy the MCV, build power → refinery → barracks → factory, train a few
    // units + harvesters, then throw attack waves at the nearest enemy.
    void setAI(const std::string& house, bool on = true) {
        if (on)
            aiHouses_.insert(house);
        else
            aiHouses_.erase(house);
    }
    bool aiControlled(const std::string& house) const {
        return aiHouses_.count(house) != 0;
    }
    // Register a structure type's footprint (cells). The AI needs these to place
    // buildings; the shell derives them from the art. A type with no registered
    // footprint simply won't be built by the AI.
    void setFootprint(const std::string& type, int w, int h) {
        footprint_[type] = {w, h};
    }
    // AI aggression tier. Applies to every AI house (like a global skirmish
    // setting); the original tracks it per-house, but one dial is enough here.
    void setDifficulty(Difficulty d) { difficulty_ = d; }
    Difficulty difficulty() const { return difficulty_; }
    // A pre-built base node the awake AI rebuilds in order (Next_Buildable).
    struct BaseNodeDef { std::string type; int cell = 0; };
    // Give a house its ordered [Base] list. When present, tickAI builds/rebuilds
    // exactly this list (in order) instead of the hardcoded tech-chain fallback.
    void setBaseList(const std::string& house, std::vector<BaseNodeDef> nodes) {
        baseList_[house] = std::move(nodes);
    }

    // --- scenario scripting ---
    void addTrigger(const TriggerDef& t) { triggers_.push_back({t, t.data, false}); }
    void addTeamType(const TeamTypeDef& t) { teamTypes_[t.name] = t; }
    void setWaypoint(int idx, int cell) {
        if (idx < 0)
            return;
        if (idx >= int(waypoints_.size()))
            waypoints_.resize(idx + 1, -1);
        waypoints_[idx] = cell;
    }
    // Won/Lost once a Win/Lose trigger has fired (from the scenario's intended
    // player's perspective); None while the mission is still in progress or the
    // map has no such triggers.
    MissionResult missionResult() const { return missionResult_; }

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
    // Could this house begin `type` right now? True iff it costs something,
    // the house owns the producing factory (construction yard / barracks /
    // war factory) and every Prerequisite= structure. Ignores cost-on-hand
    // and slot state, so it also answers "should the sidebar show this cameo".
    bool canProduce(const std::string& house, const std::string& type,
                    UnitKind kind) const;
    const Production* production(const std::string& house, ProdCat cat) const;
    // Refunds what has been paid and clears the slot.
    void cancelProduction(const std::string& house, ProdCat cat);
    // All Prerequisite= structures present? (barr and tent count as each
    // other, like the original's shared BARRACKS flag.)
    bool prereqsMet(const std::string& house, const std::string& prereq) const;
    // A single cell: in-bounds, unblocked, unoccupied, buildable terrain.
    // Used by the placement cursor to shade each footprint cell (the original
    // draws TRANS.ICN frame 0 on clear cells, frame 2 on blocked ones).
    bool cellBuildable(int cell) const;
    // Every footprint cell buildable + free, and touching (Chebyshev 1) a
    // friendly structure.
    bool canPlace(const std::string& house, int cell, int w, int h) const;
    // Places the ready Building-slot structure; returns its id or -1.
    int placeBuilding(const std::string& house, int cell, int w, int h);
    // Turns an MCV into a construction yard ("fact", w x h footprint whose
    // top-left is one cell up-left of the MCV). Returns struct id or -1.
    int deployMcv(int unitId, int w, int h);

    // --- sidebar Repair / Sell ---
    // Structure id whose footprint covers `cell`, or -1.
    int structureAt(int cell) const;
    // Refund half the build cost, remove the structure (emits StructDied).
    // Returns the credits refunded, or 0 if id is invalid.
    int sellStructure(int id);
    // Toggle the repair flag on a structure; returns the new state.
    bool toggleRepair(int id);

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
    // A refinery arrives with a free harvester (like the original), spawned
    // beside `procCell` and sent to gather. No-op if there's no room.
    void grantHarvester(const std::string& house, int procCell, int w, int h);
    void tickProjectiles();
    // Applies weapon damage to whichever target the projectile chased.
    void impact(const Projectile& p);
    void killUnit(Unit& u);
    void killStructure(Structure& s);
    // Does this house still own any live unit or structure?
    bool hasAssets(const std::string& house) const;
    // Latch newly-eliminated combatants and emit HouseDefeated events.
    void evaluateDefeat();
    // Evaluate scenario triggers on the ~6s cadence; run fired actions.
    void tickTriggers();
    void runTriggerAction(const TriggerDef& d);
    // Spawn a TeamType's roster for its house near its base (or a waypoint).
    // If the TeamType has a scripted mission list, registers a live Team so
    // tickTeams drives the members along it.
    void spawnTeam(const std::string& teamName);
    // Drive scripted per-unit orders (Hunt / Area Guard) for units that aren't
    // player-owned, skirmish-AI-owned, or team members. On the ~1s AI cadence.
    void tickStandingOrders();
    // Advance every live Team along its scripted mission list (Move / Attack /
    // Guard / Loop). On the ~1s AI cadence; deterministic (tickCount_-keyed).
    void tickTeams();
    // One AI "think" for a house (see setAI). Called on cadence from tick().
    void tickAI(const std::string& house);
    // Ticks (AI-think units, ~1/s) until the next attack wave for `house`, drawn
    // deterministically from the difficulty's AlertTime range (§6) — keyed off
    // tickCount_ + house so it varies yet stays byte-identical across runs.
    int alertTime(const std::string& house) const;
    // The next unbuilt [Base] node for `house` (Next_Buildable): the first node
    // whose type the house owns fewer of than the list requires up to that
    // point. Returns nullptr when the base is complete or the house has none.
    const BaseNodeDef* aiNextBaseNode(const std::string& house) const;
    // A buildable footprint spot for `house` near its base, or -1.
    int aiFindBuildSpot(const std::string& house, int w, int h) const;
    bool footprintOf(const std::string& type, int& w, int& h) const;
    Unit* mutableUnit(int id);
    Structure* mutableStructure(int id);
    // Target center position; false if the target no longer exists.
    bool targetPos(int targetUnit, int targetStruct, int& x, int& y) const;
    // Nearest enemy of `house` to world point (fx,fy). Sets tu/ts to the chosen
    // unit/structure id (the other -1), or both -1 if none. preferStruct picks
    // the nearest structure when any exists, else the nearest unit.
    void findNearestEnemy(const std::string& house, int fx, int fy,
                          bool preferStruct, int& tu, int& ts) const;

    std::vector<Land> land_;
    std::vector<uint8_t> blocked_;  // static: structures, terrain objects, walls
    std::vector<int> occupant_;     // unit id per cell, -1 free (vehicles/ships)
    std::vector<uint8_t> explored_; // player-house shroud state
    std::vector<uint8_t> oreBails_; // harvestable bails per cell
    std::vector<uint8_t> oreGem_;   // 1 = the cell's ore is gems
    std::unordered_map<std::string, int> credits_;
    std::set<std::string> combatants_; // non-Neutral houses that owned an asset
    std::set<std::string> defeated_;   // combatants eliminated (latched)
    std::set<std::string> aiHouses_;   // houses run by the skirmish AI
    std::unordered_map<std::string, std::pair<int, int>> footprint_; // type -> w,h
    std::unordered_map<std::string, int> aiAttackCd_; // per-house attack cooldown
    std::set<std::string> aiSeeded_;   // houses whose first AlertTime is set
    std::unordered_map<std::string, std::vector<BaseNodeDef>> baseList_; // [Base]
    Difficulty difficulty_ = Difficulty::Normal;
    int tickCount_ = 0;                // ticks elapsed (AI cadence / determinism)
    struct TriggerState { TriggerDef def; int counter; bool dead; };
    std::vector<TriggerState> triggers_;
    std::unordered_map<std::string, TeamTypeDef> teamTypes_;
    // A live squad spawned from a TeamTypeDef with a scripted mission list.
    struct Team {
        int id;
        std::string house;
        std::vector<TeamTypeDef::Step> script;
        int step = 0;      // index into script; >= size() means finished
        int stepTicks = 0; // cadence ticks spent on the current step (timeouts)
    };
    std::vector<Team> teams_;
    int nextTeamId_ = 0;
    std::vector<int> waypoints_;       // waypoint index -> cell (-1 = unset)
    MissionResult missionResult_ = MissionResult::None;
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
