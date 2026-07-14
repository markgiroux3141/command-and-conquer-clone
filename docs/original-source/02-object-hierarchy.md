# Original Source — Game Object Class Hierarchy

> Breadcrumbs into the original **C&C: Tiberian Dawn** source (EA GPL release),
> located at `reference/CnC_Tiberian_Dawn/` (gitignored; cloned locally — same as
> `D:\Command and Conquer Original source code\CnC_Tiberian_Dawn`). Files are
> UPPERCASE. All paths below are repo-relative.

The engine splits every game entity into **two parallel trees**:

- **Instance classes** (`*Class`): one C++ object per live entity; mutable
  per-object state (position, strength, mission, facing).
- **Type classes** (`*TypeClass`): one shared `const` object per *kind* of
  entity; immutable stats (cost, armor, max strength, art). Each instance points
  at its type via a `Class` member. Static tables of these live in the
  `*DATA.CPP` files.

This is the same "instance + type" pattern the clone uses (`rules.cpp` stat
tables ≈ the `*DATA.CPP` type tables; `sim.cpp` entities ≈ the instance classes).

---

## Instance-class inheritance chain

```
AbstractClass                                   ABSTRACT.H:49
  └─ ObjectClass                                OBJECT.H:61    (abstract; every on-map thing)
       ├─ MissionClass                          MISSION.H:48   (+ mission/order AI state machine)
       │    └─ RadioClass                        RADIO.H:55     (+ inter-object "radio" contact)
       │         └─ TechnoClass                  TECHNO.H:54    (owned/armed; +Flasher/Stage/Cargo/Door/Crew)
       │              ├─ FootClass               FOOT.H:54      (mobile: pathfinding + NavCom)
       │              │    ├─ DriveClass         DRIVE.H:47     (ground vehicle; holds UnitTypeClass*)
       │              │    │    └─ TurretClass   TURRET.H:43    (+ rotating turret facing)
       │              │    │         └─ TarComClass  TARCOM.H:49  (+ targeting/threat → fire)
       │              │    │              └─ UnitClass  UNIT.H:53   (concrete vehicle)
       │              │    ├─ InfantryClass      INFANTRY.H:52  (direct FootClass; no turret chain)
       │              │    └─ AircraftClass      AIRCRAFT.H:46  (also multiply-inherits FlyClass)
       │              └─ BuildingClass           BUILDING.H:58  (does NOT move)
       ├─ TerrainClass                           TERRAIN.H:49   (+ StageClass; trees/rocks)
       ├─ BulletClass                            BULLET.H:46    (+ FlyClass, FuseClass; projectiles)
       ├─ AnimClass                              ANIM.H:48      (+ private StageClass; explosions/fire)
       └─ OverlayClass / SmudgeClass / TemplateClass   (map dressing — see 03-map-and-terrain.md)
```

### AbstractClass — [ABSTRACT.H:49](../../reference/CnC_Tiberian_Dawn/ABSTRACT.H#L49)
Root of everything. Minimal common denominator: a world `COORDINATE Coord`
([ABSTRACT.H:57](../../reference/CnC_Tiberian_Dawn/ABSTRACT.H#L57)) and an
`IsActive` flag ([ABSTRACT.H:64](../../reference/CnC_Tiberian_Dawn/ABSTRACT.H#L64)).
Key virtuals: `Center_Coord`/`Target_Coord` (ABSTRACT.H:80-81),
`Direction`/`Distance` helpers (ABSTRACT.H:87-94), `Can_Enter_Cell`
(ABSTRACT.H:99), `AI` (ABSTRACT.H:104).

### ObjectClass — [OBJECT.H:61](../../reference/CnC_Tiberian_Dawn/OBJECT.H#L61)
Any object that can physically exist on the map. Adds placement/selection/limbo
state (`IsDown`, `IsSelected`, `IsInLimbo` — OBJECT.H:68-108), cell-list `Next`
pointer (OBJECT.H:115), `Trigger` (OBJECT.H:121), `short Strength` (OBJECT.H:126).
Pure virtuals `Class_Of` (OBJECT.H:151) and `Draw_It` (OBJECT.H:189) force every
concrete leaf to bind a type-class and rendering. Also: `What_Am_I` RTTI
(OBJECT.H:133), `Limbo`/`Unlimbo` (OBJECT.H:172-173), `Take_Damage` (OBJECT.H:213),
`As_Target` (OBJECT.H:214), `Receive_Message` (OBJECT.H:225), `Mark` (OBJECT.H:192).

### MissionClass — [MISSION.H:48](../../reference/CnC_Tiberian_Dawn/MISSION.H#L48)
Order/mission tracking on every object. Holds `MissionType Mission`,
`SuspendedMission`, `MissionQueue`, `char Status` (MISSION.H:56-66) and a thread
`Timer` (MISSION.H:127). `Assign_Mission` (MISSION.H:82), `AI` (MISSION.H:84),
and the `Mission_*` state handlers — `Mission_Attack`, `Mission_Guard`,
`Mission_Harvest`, `Mission_Hunt`, … (MISSION.H:90-107);
`Mission_Name`/`Mission_From_Name` (MISSION.H:110-111). **This is the per-unit AI
state machine** — see also [06-houses-ai-missions.md](06-houses-ai-missions.md).

### RadioClass — [RADIO.H:55](../../reference/CnC_Tiberian_Dawn/RADIO.H#L55)
Point-to-point "radio" contact when one object commands/coordinates another
(transport ↔ passenger, harvester ↔ refinery). Holds `RadioClass * Radio` and
`LastMessage` (RADIO.H:61-69). `In_Radio_Contact` (RADIO.H:88),
`Contact_With_Whom` (RADIO.H:90), `Receive_Message`/`Transmit_Message`
(RADIO.H:93-95).

### TechnoClass — [TECHNO.H:54](../../reference/CnC_Tiberian_Dawn/TECHNO.H#L54)
Multiple inheritance: `public RadioClass, FlasherClass, StageClass, CargoClass,
DoorClass, CrewClass`. Common base for all owned, money-costing, crewed/armed
objects — **buildings and units**. Where ownership, combat, and weapons state
live: `HouseClass * House` (TECHNO.H:152), `TARGET TarCom` (TECHNO.H:164),
`FacingClass PrimaryFacing` (TECHNO.H:170), `Ammo`/`Arm`/`PurchasePrice`
(TECHNO.H:176-190). Type accessor `Techno_Type_Class()` downcasts `Class_Of()`
to `TechnoTypeClass const *` (TECHNO.H:207). Combat: `Fire_At` (TECHNO.H:253),
`Can_Fire` (TECHNO.H:248), `Take_Damage` (TECHNO.H:256), `Greatest_Threat`
(TECHNO.H:249), `Captured` (TECHNO.H:255), `Do_Cloak`/`Do_Uncloak`
(TECHNO.H:292-293).

### FootClass — [FOOT.H:54](../../reference/CnC_Tiberian_Dawn/FOOT.H#L54)
Everything that moves (units, infantry, aircraft — "everything except
buildings"). Adds pathfinding + movement: `TARGET NavCom`/`SuspendedNavCom`
(FOOT.H:142-143), `ArchiveTarget` (FOOT.H:135), the `FacingType
Path[CONQUER_PATH_MAX]` path list (FOOT.H:177), team linkage `Team`/`Member`/
`Group` (FOOT.H:151-167). `Basic_Path` (FOOT.H:204), private `Find_Path`
(FOOT.H:294), `Start_Driver`/`Stop_Driver` (FOOT.H:221-222), `Assign_Destination`
(FOOT.H:223), `Approach_Target` (FOOT.H:275). See
[05-movement-pathfinding-combat.md](05-movement-pathfinding-combat.md).

---

## Unit branch (the deep mixin chain)

Vehicles are built up through mixins, each adding one capability:

| Class | File:line | Adds |
|---|---|---|
| `DriveClass` | [DRIVE.H:47](../../reference/CnC_Tiberian_Dawn/DRIVE.H#L47) | ground driving; **holds** `UnitTypeClass const * const Class` (DRIVE.H:53), harvest state `Tiberium`/`IsHarvesting` (DRIVE.H:59-66) |
| `TurretClass` | [TURRET.H:43](../../reference/CnC_Tiberian_Dawn/TURRET.H#L43) | independent turret `FacingClass SecondaryFacing` (TURRET.H:57), `Reload` timer (TURRET.H:51) |
| `TarComClass` | [TARCOM.H:49](../../reference/CnC_Tiberian_Dawn/TARCOM.H#L49) | targeting-computer/combat logic (threat → launch) |
| `UnitClass` | [UNIT.H:53](../../reference/CnC_Tiberian_Dawn/UNIT.H#L53) | concrete vehicle: `Flagged` (CTF, UNIT.H:60), custom `new`/`delete` (UNIT.H:65-66), `operator UnitType` (UNIT.H:69) |

`UnitClass` notables: `What_Am_I`→`RTTI_UNIT` (UNIT.H:71), `Try_To_Deploy`/
`Goto_Tiberium`/`Harvesting`/`Flag_Attach` (UNIT.H:79-87), `Mission_Harvest`/
`Mission_Unload` (UNIT.H:152-154), INI persistence `Read_INI`/`Write_INI`,
`INI_Name`="UNITS" (UNIT.H:179-181).

## Infantry branch

`InfantryClass` — [INFANTRY.H:52](../../reference/CnC_Tiberian_Dawn/INFANTRY.H#L52),
`public FootClass` directly (no turret chain). Holds `InfantryTypeClass const *
const Class` (INFANTRY.H:55), `DoType Doing` animation sequence (INFANTRY.H:64),
`Fear` + prone/boxing/technician flags (INFANTRY.H:78-106). `Do_Action`
(INFANTRY.H:215), `Fire_At` (INFANTRY.H:168), occupancy-bit helpers
(INFANTRY.H:175-178), `HumanShape[32]` facing table (INFANTRY.H:232), INI
name "INFANTRY" (INFANTRY.H:206).

## Aircraft branch

`AircraftClass` — [AIRCRAFT.H:46](../../reference/CnC_Tiberian_Dawn/AIRCRAFT.H#L46),
`public FootClass, public FlyClass`. Holds `AircraftTypeClass const * const
Class` (AIRCRAFT.H:52), `What_Am_I`→`RTTI_AIRCRAFT` (AIRCRAFT.H:61),
`FLIGHT_LEVEL=24` (AIRCRAFT.H:64). Flight missions `Mission_Retreat`/
`Mission_Unload` (AIRCRAFT.H:66-73).

## Building branch

`BuildingClass` — [BUILDING.H:58](../../reference/CnC_Tiberian_Dawn/BUILDING.H#L58),
`public TechnoClass` (sibling to the whole mobile subtree — buildings don't move).
Holds `BuildingTypeClass const * const Class` (BUILDING.H:61), `FactoryClass *
Factory` for production (BUILDING.H:68), `HousesType ActLike` (BUILDING.H:75),
repair/commence flags (BUILDING.H:81-89).

## Non-Techno leaf objects (derive straight from ObjectClass)

- **TerrainClass** — [TERRAIN.H:49](../../reference/CnC_Tiberian_Dawn/TERRAIN.H#L49),
  `public ObjectClass, public StageClass`. Trees/rocks. `TerrainTypeClass const *
  const Class` (TERRAIN.H:52), `Class_Of` inline (TERRAIN.H:75),
  `What_Am_I`→`RTTI_TERRAIN` (TERRAIN.H:63).
- **BulletClass** — [BULLET.H:46](../../reference/CnC_Tiberian_Dawn/BULLET.H#L46),
  `public ObjectClass, FlyClass, FuseClass`. Projectiles. `BulletTypeClass const *
  const Class` (BULLET.H:56), `TechnoClass * Payback` = who fired it (BULLET.H:63),
  custom `new`/`delete` (BULLET.H:73-74).
- **AnimClass** — [ANIM.H:48](../../reference/CnC_Tiberian_Dawn/ANIM.H#L48),
  `public ObjectClass, private StageClass`. Explosions/fire. `AnimTypeClass`
  pointer (ANIM.H:56), `Attach_To` another object (ANIM.H:64), `Class_Of` inline
  (ANIM.H:73).

---

## The parallel *TypeClass hierarchy — all in TYPE.H

```
AbstractTypeClass                     TYPE.H:223
  └─ ObjectTypeClass                  TYPE.H:263   (abstract)
       ├─ TechnoTypeClass             TYPE.H:386
       │    ├─ BuildingTypeClass      TYPE.H:573
       │    ├─ UnitTypeClass          TYPE.H:837
       │    ├─ InfantryTypeClass      TYPE.H:1053
       │    └─ AircraftTypeClass      TYPE.H:1678
       ├─ BulletTypeClass             TYPE.H:1168
       ├─ TerrainTypeClass            TYPE.H:1333
       ├─ TemplateTypeClass           TYPE.H:1430
       ├─ AnimTypeClass               TYPE.H:1501
       ├─ OverlayTypeClass            TYPE.H:1786
       └─ SmudgeTypeClass             TYPE.H:1906
```

Standalone in TYPE.H (not in the Object tree): `WeaponTypeClass` (TYPE.H:53),
`WarheadTypeClass` (TYPE.H:102), `HouseTypeClass` (TYPE.H:140). `TeamTypeClass`
is separate — [TEAMTYPE.H:84](../../reference/CnC_Tiberian_Dawn/TEAMTYPE.H#L84).

- **AbstractTypeClass** (TYPE.H:223): base of all type classes. `char
  IniName[9]` (scenario id) + `int Name` (language-text id) (TYPE.H:233-241);
  `What_Am_I` (TYPE.H:245), `Full_Name` (TYPE.H:248).
- **ObjectTypeClass** (TYPE.H:263): shared immutable traits — `IsCrushable`,
  `IsSelectable`, `IsLegalTarget`, `IsSentient` (TYPE.H:271-318), `ArmorType
  Armor`, `MaxStrength` (TYPE.H:324-329), art `ImageData`/`RadarIcon`
  (TYPE.H:336-341). Pure virtuals `Create_And_Place` (TYPE.H:361) and
  `Create_One_Of` (TYPE.H:364) make instances.
- **TechnoTypeClass** (TYPE.H:386): combat/economy stats shared by units,
  infantry, aircraft, buildings (turret flag, leader/scanner flags, cost/build
  data).

### Where the static data tables live (the *DATA.CPP files)

Each concrete `*TypeClass` defines a fixed array of `const` instances (one per
enum value), a `static Pointers[]` lookup, and an `As_Reference(enum)` accessor:

| Type class | Data file | `Pointers[]` | `As_Reference` |
|---|---|---|---|
| `UnitTypeClass` | [UDATA.CPP](../../reference/CnC_Tiberian_Dawn/UDATA.CPP) | UDATA.CPP:1301 | UDATA.CPP:1797 |
| `InfantryTypeClass` | [IDATA.CPP](../../reference/CnC_Tiberian_Dawn/IDATA.CPP) | IDATA.CPP:1582 | — |
| `AircraftTypeClass` | [AADATA.CPP](../../reference/CnC_Tiberian_Dawn/AADATA.CPP) | AADATA.CPP:289 | — |
| `BuildingTypeClass` | [BDATA.CPP](../../reference/CnC_Tiberian_Dawn/BDATA.CPP) | BDATA.CPP:3857 | BDATA.CPP:4578 |
| `AnimTypeClass` | [ADATA.CPP](../../reference/CnC_Tiberian_Dawn/ADATA.CPP) | ADATA.CPP:2251 | — |
| `BulletTypeClass` | [BBDATA.CPP](../../reference/CnC_Tiberian_Dawn/BBDATA.CPP) | BBDATA.CPP:488 | — |
| `TerrainTypeClass` | [TDATA.CPP](../../reference/CnC_Tiberian_Dawn/TDATA.CPP) | TDATA.CPP:755 | — |
| `TemplateTypeClass` | [CDATA.CPP](../../reference/CnC_Tiberian_Dawn/CDATA.CPP) | CDATA.CPP:2255 | — |
| `OverlayTypeClass` | [ODATA.CPP](../../reference/CnC_Tiberian_Dawn/ODATA.CPP) | ODATA.CPP:567 | — |
| `SmudgeTypeClass` | [SDATA.CPP](../../reference/CnC_Tiberian_Dawn/SDATA.CPP) | SDATA.CPP:185 | — |
| `HouseTypeClass` | [HDATA.CPP](../../reference/CnC_Tiberian_Dawn/HDATA.CPP) | HDATA.CPP:183 | HDATA.CPP:315 |

⚠️ **Naming trap:** `ADATA.CPP` is **anim** data (not aircraft — that's
`AADATA.CPP`); `BDATA.CPP` is **buildings** (bullets are `BBDATA.CPP`). Example
instance defs: `UnitMTank` at UDATA.CPP:285, `UnitHarvester` at UDATA.CPP:631.

### Instance ↔ type relationship (the key pattern)

Each concrete instance stores `SomethingTypeClass const * const Class` and
implements `operator EnumType()` via `Class->Type`:

- `UnitClass`→`UnitTypeClass` (pointer inherited from `DriveClass::Class`,
  DRIVE.H:53; `operator UnitType` UNIT.H:69)
- `InfantryClass`→`InfantryTypeClass` (INFANTRY.H:55-56)
- `AircraftClass`→`AircraftTypeClass` (AIRCRAFT.H:52-57)
- `BuildingClass`→`BuildingTypeClass` (BUILDING.H:61-62)
- `TerrainClass`→`TerrainTypeClass` (TERRAIN.H:52-53)
- `BulletClass`→`BulletTypeClass` (BULLET.H:56-57)
- `AnimClass`→`AnimTypeClass` (ANIM.H:56)

`ObjectClass::Class_Of()` (pure virtual, OBJECT.H:151) is overridden in each leaf
to return `*Class`; `TechnoClass::Techno_Type_Class()` (TECHNO.H:207) downcasts
so shared combat code reads stats without knowing the exact leaf.
`virtual RTTIType What_Am_I()` (OBJECT.H:133) is the engine's hand-rolled RTTI
(`RTTI_UNIT`, `RTTI_AIRCRAFT`, `RTTI_TERRAIN`, `RTTI_BULLET`, …).

---

## TARGET — the object handle abstraction ([TARGET.H](../../reference/CnC_Tiberian_Dawn/TARGET.H))

A `TARGET` is **not** a class in the live build (`class TargetClass` at
TARGET.H:101 is inside `#ifdef NEVER`, i.e. dead). In practice it's an **encoded
integer handle**: a `KindType` tag (TARGET.H:46-60: `KIND_UNIT`, `KIND_BUILDING`,
`KIND_CELL`, …) packed above a 12-bit value (`TARGET_MANTISSA`, TARGET.H:63).
Inline helpers pack/unpack: `Build_Target` (TARGET.H:84), `As_Target(CELL)`
(TARGET.H:85), `Target_Kind`/`Target_Value` (TARGET.H:68-69), and per-kind
predicates (TARGET.H:71-82). This lets `TarCom`/`NavCom`/`ArchiveTarget`
reference any object *or* a cell through one save-game-safe integer instead of a
raw pointer; `ObjectClass::As_Target()` (OBJECT.H:214) produces one.
