# Original Source — Houses, AI, Missions, Scenarios, Triggers

> Breadcrumbs into `reference/CnC_Tiberian_Dawn/`. Files UPPERCASE; paths
> repo-relative. This is Phase 7 territory for the clone (skirmish AI + mission
> scripting — not yet built; see [MILESTONES.md](../../MILESTONES.md)).

## 1. Houses (players / factions)

- **`HousesType` enum** — [DEFINES.H:619-634](../../reference/CnC_Tiberian_Dawn/DEFINES.H#L619):
  `HOUSE_GOOD` (GDI), `HOUSE_BAD` (Nod), `HOUSE_NEUTRAL` (civilians), `HOUSE_JP`,
  `HOUSE_MULTI1..6`. Bit-flags `HOUSEF_*` DEFINES.H:638-640.
  > Clone convention (MILESTONES Phase 10): GoodGuy=GDI (gold), BadGuy=Nod (red),
  > Neutral=civilian.
- **`HouseTypeClass`** (static faction data) — [TYPE.H:140-215](../../reference/CnC_Tiberian_Dawn/TYPE.H#L140):
  `House`, `IniName` ("GoodGuy"/"BadGuy"/"Neutral"), `FullName`, radar
  `Color`/`BrightColor`, `RemapTable`/`RemapColor`. Instances in
  **[HDATA.CPP](../../reference/CnC_Tiberian_Dawn/HDATA.CPP)**: `HouseGood`/
  `HouseBad`/`HouseCivilian`/… at HDATA.CPP:53-181; `Pointers[]` HDATA.CPP:183;
  `From_Name` HDATA.CPP:262; `As_Reference` HDATA.CPP:315.
- **`HouseClass`** (live per-player instance) — [HOUSE.H:54-568](../../reference/CnC_Tiberian_Dawn/HOUSE.H#L54).
  One per active player. Back-pointer `HouseTypeClass const * const Class`
  (HOUSE.H:60); can impersonate another faction via `ActLike` (HOUSE.H:66).
  - **Flags:** `IsActive`/`IsHuman`/`IsAlerted`/`IsDefeated`/`IsToDie`/`IsToWin`/
    `IsToLose` (delayed via `BorrowedTime`) — HOUSE.H:72-149.
  - **Economy:** `Credits`, `Tiberium`, `Capacity`, `CreditsSpent`,
    `HarvestedCredits` — HOUSE.H:242-265.
  - **Power:** `Power`/`Drain` (HOUSE.H:318-319); `Power_Fraction()` HOUSE.CPP:4248.
    > Clone ports this as `Sim::power` + `powerFraction` (MILESTONES Phase 5).
  - **Ownership scan bitfields:** double-buffered "what do I own" masks
    `BScan/UScan/IScan/AScan` + `Active*`/`New*` (HOUSE.H:206-236), swapped each
    tick at the top of `AI` (HOUSE.CPP:774-789).
  - **Counts/limits:** `CurUnits`/`CurBuildings`/`MaxUnit`/`MaxBuilding`
    (HOUSE.H:249-257); factory counts + in-progress indices (HOUSE.H:307-335).
  - **Superweapons:** `SuperClass IonCannon, AirStrike, NukeStrike` (HOUSE.H:168-170).
  - **AI timers:** `AlertTime` (attack cadence), `TeamTime`, `TriggerTime`,
    `DamageTime` (HOUSE.H:191-197,547-557).
  - **Threat map:** `RegionClass Regions[MAP_TOTAL_REGIONS]` (HOUSE.H:475), fed by
    `Adjust_Threat()` HOUSE.CPP:2009.
  - **Allies:** `Allies` bitfield (HOUSE.H:531); `Make_Ally`/`Is_Ally` HOUSE.H:402-408.
  - **Key methods:** ctor HOUSE.CPP:301; `AI()` HOUSE.CPP:767 (§6);
    `Suggest_New_Object()` HOUSE.CPP:3166 (§6); `Can_Build()` overloads
    HOUSE.CPP:449/631/662/691/717; `Harvested` HOUSE.CPP:1458; `Spend_Money`
    HOUSE.CPP:1512; static `Read_INI` HOUSE.CPP:1648 (parses `[<HouseName>]`).

## 2. Missions — per-object order state machine

- **`MissionType` enum** — [DEFINES.H:451-479](../../reference/CnC_Tiberian_Dawn/DEFINES.H#L451):
  `MISSION_SLEEP, ATTACK, MOVE, RETREAT, GUARD, STICKY, ENTER, CAPTURE, HARVEST,
  GUARD_AREA, RETURN, STOP, AMBUSH, HUNT, TIMED_HUNT, UNLOAD, SABOTAGE,
  CONSTRUCTION, DECONSTRUCTION, REPAIR, RESCUE, MISSILE`.
- **`MissionClass`** — [MISSION.H:48-133](../../reference/CnC_Tiberian_Dawn/MISSION.H#L48),
  derives from `ObjectClass`. Holds `Mission`, `SuspendedMission`,
  `MissionQueue`, and a `Status` byte = sub-state within a mission
  (MISSION.H:56-66); a `Timer` throttles script processing (MISSION.H:127).
- **Dispatch:** `MissionClass::AI()` — [MISSION.CPP:188-278](../../reference/CnC_Tiberian_Dawn/MISSION.CPP#L188)
  — when `Timer` expires, `switch`es on `Mission` and calls the matching
  `Mission_*()` handler, using its return value as the next delay. **Base
  handlers just sleep 30s (MISSION.CPP:77-95); real behavior is overridden per
  object type** in UNIT.CPP / INFANTRY.CPP / AIRCRAFT.CPP / BUILDING.CPP /
  DRIVE.CPP / TECHNO.CPP.
- **Orders are queued, not immediate:** `Assign_Mission()` MISSION.CPP:334 sets
  `MissionQueue`; `Commence()` MISSION.CPP:300 promotes it to active and resets
  `Timer`/`Status` to 0 to force fresh state-machine entry. Default missions come
  from unit data tables (harvester default `MISSION_HARVEST` UDATA.CPP:686; most
  combat units `MISSION_GUARD`).
  > Clone parallel (MILESTONES Phase 10 session 7): auto-acquire + `autoTarget`
  > guarding ≈ `MISSION_GUARD`; player-ordered attack that chases ≈
  > `MISSION_ATTACK`. A move order canceling attack mirrors `Commence`.

## 3. Scenario loading (.INI → live map)

- **`Read_Scenario(char *root)`** — [SCENARIO.CPP:170-201](../../reference/CnC_Tiberian_Dawn/SCENARIO.CPP#L170):
  `Clear_Scenario()` → `Read_Scenario_Ini()` → `Fill_In_Data()`.
  - `Clear_Scenario()` SCENARIO.CPP:263 — resets end-game countdown, calls every
    subsystem `Init()` (Houses, TeamTypes, Teams, Triggers, all object types,
    Factory, Base).
  - `Fill_In_Data()` SCENARIO.CPP:219 — post-load fixups (buildable sidebar
    lists, redraw).
- **The parser: `Read_Scenario_Ini()`** — [INI.CPP:210](../../reference/CnC_Tiberian_Dawn/INI.CPP#L210).
  Loads the whole `<root>.INI` into a buffer, computes `ScenarioCRC`, reads
  `[Basic]` movie/theme/`BuildLevel` (INI.CPP:296-332), then parses sections **in
  strict dependency order**, each via that class's static `Read_INI`:
  1. `TeamTypeClass::Read_INI` — INI.CPP:338 (before triggers reference them)
  2. `HouseClass::Read_INI` — INI.CPP:345 (creates houses)
  3. Player assignment (`[Basic] Player`, `PlayerPtr`, carry-over credits) — INI.CPP:353-387
  4. `TriggerClass::Read_INI` — INI.CPP:393
  5. `Map.Read_INI` (dims/theater) INI.CPP:400; terrain INI.CPP:407-411
  6. Objects: `TerrainClass` :417, `UnitClass` :423, `InfantryClass` :429, `BuildingClass` :435
  7. **`Base.Read_INI`** (AI pre-built base) — INI.CPP:441
  8. `OverlayClass` :447, `SmudgeClass` :453, `[Briefing]` :459-499
  > The clone's TD map loader (`map.cpp loadTd`) handles the terrain/overlay/
  > object sections; the trigger/team/house scripting above is Phase 7 work.

## 4. Triggers & TeamTypes (map scripting)

### Triggers — `TriggerClass` ([TRIGGER.H:80-256](../../reference/CnC_Tiberian_Dawn/TRIGGER.H#L80))
An **event → action** pair scoped to a house/object/cell.
- **`EventType`** (the "when") TRIGGER.H:41-77: `EVENT_PLAYER_ENTERED`,
  `EVENT_DISCOVERED/ATTACKED/DESTROYED/ANY`, house events
  (`HOUSE_DISCOVERED`, `UNITS_DESTROYED`, `BUILDINGS_DESTROYED`, `ALL_DESTROYED`,
  `CREDITS`, `TIME`, `NBUILDINGS_DESTROYED`, `NOFACTORIES`, `EVAC_CIVILIAN`,
  `BUILD`).
- **`ActionType`** (the "what") TRIGGER.H:82-107: `ACTION_WIN`, `LOSE`,
  `BEGIN_PRODUCTION`, `CREATE_TEAM`, `DESTROY_TEAM`, `ALL_HUNT`,
  `REINFORCEMENTS`, `DZ`, `AIRSTRIKE`, `NUKE`, `ION`, `AUTOCREATE`, `WINLOSE`,
  `ALLOWWIN`.
- **Persistence:** `VOLATILE`/`SEMIPERSISTANT`/`PERSISTANT` (TRIGGER.H:109-113)
  controls self-delete after firing; `AttachCount` tracks references.
- **Firing:** three overloaded `Spring()` — object TRIGGER.CPP:361, cell
  TRIGGER.CPP:557, house TRIGGER.CPP:751. The house `Spring` (:751-916) matches
  event+house, handles credit/time/count thresholds, runs the action `switch`
  (:802-904), removes itself if `VOLATILE`. `Read_INI` TRIGGER.CPP:1029.
- Checked from `HouseClass::AI` (HOUSE.CPP:1241-1337, ~every 6 ticks).

### TeamTypes & Teams (AI attack waves)
- **`TeamMissionType`** — TEAMTYPE.H:47-64: `TMISSION_ATTACKBASE/ATTACKUNITS/
  ATTACKCIVILIANS/RAMPAGE/DEFENDBASE/MOVE/MOVECELL/RETREAT/GUARD/LOOP/
  ATTACKTARCOM/UNLOAD`.
- **`TeamTypeClass`** — [TEAMTYPE.H:84-257](../../reference/CnC_Tiberian_Dawn/TEAMTYPE.H#L84)
  — template for an AI squad: flags `IsRoundAbout`/`IsSuicide`/`IsAutocreate`/
  `IsPrebuilt`/`IsReinforcable` (:158-201), `RecruitPriority`, `InitNum`,
  `MaxAllowed`, owning `House`, scripted `MissionList[]` + `MissionCount`
  (:230-233), roster `Class[]`/`DesiredNum[]` (:243-248). `Create_One_Of()`
  (:125) instantiates a live team. Load `Read_INI` TEAMTYPE.CPP:196.
- **`TeamClass`** — [TEAM.H:52-247](../../reference/CnC_Tiberian_Dawn/TEAM.H#L52)
  — a live squad. Tracks `IsFullStrength`/`IsUnderStrength`/`IsReforming`,
  `Center`, `MissionTarget`/`Target`, member linked-list, `CurrentMission` index.
  `TeamClass::AI()` — TEAM.CPP:283 — recomputes strength (:306-352), recruits,
  advances `CurrentMission`, coordinates members via `Coordinate_Attack`
  (TEAM.CPP:1116)/`Coordinate_Move`/`Coordinate_Regroup`/`Coordinate_Unload`.

## 5. FactoryClass — production queue ([FACTORY.H:43-142](../../reference/CnC_Tiberian_Dawn/FACTORY.H#L43))

Represents **one in-progress production item** for a house (derives privately from
`StageClass`). State: `Object` (the `TechnoClass` created up-front, held in limbo,
FACTORY.H:128), `Balance`/`OriginalBalance` (installment cost, :120-121),
`IsSuspended` (:104). Production = `STEP_COUNT=108` steps (FACTORY.H:90).
- `Set()` FACTORY.CPP:352/403 — create object + init balance (does not start).
- `Start()` FACTORY.CPP:509 / `Suspend()` FACTORY.CPP:481; `Abandon()` (refund)
  FACTORY.CPP:556.
- `AI()` — FACTORY.CPP:233-302 — advances steps proportional to the house's count
  of like factories (:245-264), spends money incrementally via
  `House->Spend_Money`, and **steps backward to stall when funds are short**
  (:281-286). On completion suspends + pays remaining balance (:293-298).
- Poll: `Has_Completed` FACTORY.CPP:631, `Completed` (removes object)
  FACTORY.CPP:727.
> Clone parallel (MILESTONES Phase 6): drip-paid credits, stall when broke, power
> scaling — same model. The `*Factory` index fields on HouseClass drive it.

## 6. AI base-building & attack logic

### Pre-built base list — `BaseClass`/`BaseNodeClass` ([BASE.H:44-124](../../reference/CnC_Tiberian_Dawn/BASE.H#L44))
**Not** a class-hierarchy root — it's the AI's ordered list of buildings to
(re)construct. `BaseNodeClass = {StructType Type; COORDINATE Coord;}` (BASE.H:44);
`BaseClass` holds `DynamicVectorClass<BaseNodeClass> Nodes` + `House` (BASE.H:118).
One global `Base` (loaded at INI.CPP:441). `Next_Buildable()` — BASE.CPP:491 —
returns the first unbuilt node, or NULL when the base is complete.

### What the AI makes — `HouseClass::Suggest_New_Object` (HOUSE.CPP:3166-3392)
Dispatched by RTTI:
- **Units** (:3177-3279): prioritizes a replacement harvester if it has a
  refinery but none (:3186-3190); builds a demand table = 2× members still needed
  to fill all active + prebuilt teams (:3209-3242), subtracts units in play,
  randomly picks among most-needed affordable types. Gated by `CurUnits<MaxUnit`.
- **Infantry** (:3285-3376): same team-demand algorithm.
- **Buildings** (:3381-3389): just `Base.Next_Buildable()` — rebuilds the
  scripted base list in order. Gated by `CurBuildings<MaxBuilding`.

A `BuildingClass` acts as the placement driver: calls
`House->Suggest_New_Object(Class->ToBuild)` (BUILDING.CPP:1159) then
`Base.Next_Buildable(...)` (BUILDING.CPP:2169) for placement.

### Attack-wave generation — `HouseClass::AI` (HOUSE.CPP:767-1404)
Per-house think loop:
1. Swap ownership scan bitfields (:774-789).
2. Resolve delayed win/lose/die when `BorrowedTime` expires (:794-821).
3. Clamp `Power`/`Drain` (:829-832).
4. **Attack teams:** when `IsAlerted` and `AlertTime` expires, create up to
   `maxteams` squads via `Suggested_New_Team(true)` → `Create_One_Of()`; reset
   `AlertTime` by difficulty (Easy 16-40 min, Normal 5-20, Difficult 4-10)
   (:839-862).
5. Routine team building on `TeamTime` (:868-873).
6. Superweapon AI via `Special_Weapon_AI()` (:1070-1180; body HOUSE.CPP:2318).
7. **Trigger processing:** iterate `HouseTriggers[]` and `Spring()` house events
   (:1241-1337).
8. Radar on/off by power; sidebar recalc when `IsRecalcNeeded` (:1343-1403).

## 7. Reinforcements, superweapons, stats

- **Reinforcements:** `Do_Reinforcements(TeamTypeClass *)` — REINF.CPP:63 —
  spawns a team at the map edge/waypoint, orders `MISSION_GUARD` on arrival
  (:248,288). Fired by `ACTION_REINFORCEMENTS` triggers (TRIGGER.CPP:892).
- **Superweapons:** `SuperClass` — [SUPER.H:43-80](../../reference/CnC_Tiberian_Dawn/SUPER.H#L43)
  — recharge/availability for Ion Cannon, Air Strike, Nuke. `IsPresent`/
  `IsReady`/`IsSuspended`, `RechargeTime`, `Enable`/`Forced_Charge`/`AI`/
  `Is_Ready`. Each `HouseClass` owns three (HOUSE.H:168-170).
- **Stats:** STATS.CPP implements `UnitTrackerClass` (per-house built/destroyed/
  captured counters, HOUSE.H:280-301) for end-game scoring.
