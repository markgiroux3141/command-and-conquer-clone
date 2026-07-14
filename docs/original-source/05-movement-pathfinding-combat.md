# Original Source — Movement, Pathfinding & Combat

> Breadcrumbs into `reference/CnC_Tiberian_Dawn/`. Files UPPERCASE; paths
> repo-relative. This is what the clone's [src/game/sim.cpp](../../src/game/sim.cpp)
> reimplements (movement, attack orders, projectiles, damage-vs-armor).

## The movement/combat class spine

```
AbstractClass → ObjectClass → MissionClass → RadioClass → TechnoClass
   → FootClass      (mobile: NavCom, Path[], pathfinding)          FOOT.H:54
      → DriveClass  (vehicle physics: tracks, Start_Of_Move)       DRIVE.H:47
         → TurretClass (independent rotating turret facing)        TURRET.H:43
            → TarComClass (target computer: acquire + fire)        TARCOM.H:49
               → UnitClass (concrete vehicle)                      UNIT.H:53
```
Side branches: `FlyClass` (free-flight physics mixin, FLY.H:51);
`AircraftClass : FootClass, FlyClass` (AIRCRAFT.H:46);
`BulletClass : ObjectClass, FlyClass` (BULLET.H:46). See
[02-object-hierarchy.md](02-object-hierarchy.md) for the full tree.

## 1. Pathfinding — `Find_Path` ([FINDPATH.CPP:529](../../reference/CnC_Tiberian_Dawn/FINDPATH.CPP#L529))

`PathType * FootClass::Find_Path(CELL dest, FacingType *final_moves, int maxlen,
MoveType threshhold)`.

**⚠️ It is NOT A\*.** It's a greedy **line-drawing + edge-following** (wall-follower
/ "bug") algorithm, documented at FINDPATH.CPP:611-628:
1. Walk a straight line toward `dest` (direction from `CELL_FACING`,
   FINDPATH.CPP:644; step via `Adjacent_Cell`, :645).
2. Test each candidate with `Passable_Cell(next, dir, threat, threshhold)`
   (:650) → movement cost (0 = impassable).
3. On an obstacle, trace **both** a left (CCW) and right (CW) contour around it
   and keep the shorter (`moves_left`/`moves_right`, :544-547).
4. Repeat from the emerging cell until `dest` reached or `maxlen` exhausted.

Anti-loop: `Register_Cell` (FINDPATH.CPP:329/424) maintains an `Overlap`
visited-bitfield; immediate reversals pop the last command (:446-450); loops
detected via `path->LastOverlap` (:457-461).

> The clone deliberately diverges here — MILESTONES Phase 4 uses **8-dir A\***
> in `sim.cpp`. The original's greedy wall-follower is cheaper but non-optimal;
> keep that in mind when matching original routing behavior.

**Path storage — `PathType`** (DEFINES.H:2384-2392): a path is a **list of
`FacingType` (8-way) steps**, not waypoints — `Start`, `Cost`, `Length`,
`FacingType *Command`, `Overlap` bitfield, `LastOverlap`. `MAX_MLIST_SIZE 300`.

**Following:** `FootClass::Basic_Path()` (FOOT.CPP:350) wraps `Find_Path`, resolves
an impassable target to a nearby enterable cell (:367-380), and copies facings
into the object's `FacingType Path[CONQUER_PATH_MAX]` (FOOT.H:177). The mover
consumes `Path[0]` step-by-step.

## 2. FootClass movement & the NavCom

- **NavCom** — a `TARGET` destination field: `TARGET NavCom; TARGET
  SuspendedNavCom;` (FOOT.H:142).
- `FootClass::Assign_Destination(TARGET)` — FOOT.CPP:1624 (`NavCom = target`).
- `Start_Driver(COORDINATE &headto)` — FOOT.CPP:785; `Stop_Driver` — FOOT.CPP:756.
- `Set_Speed(int 0-255)` — FOOT.CPP:276 (`Speed` field FOOT.H:126).

**Vehicle cell-to-cell (DriveClass):**
- `Start_Of_Move()` — DRIVE.CPP:826 — pulls `facing = Path[0]` (:836); no
  NavCom + empty path → stop/idle (:838-845).
- `While_Moving()` — DRIVE.CPP:585 — per-frame integrator. Accumulates
  `SpeedAccum + Fixed_To_Cardinal(Class->MaxSpeed, Speed)` (:605); at
  ≥`PIXEL_LEPTON_W` advances along a smooth-turn track, updates `Coord` via
  `Smooth_Turn` (:648), sets `PrimaryFacing` (:650).
- `Per_Cell_Process(bool center)` — DRIVE.CPP:772 — once per cell entered (map
  reveal, crush infantry via `Overrun_Square` DRIVE.CPP:228, consume next
  `Path[]` step); chains to `FootClass::Per_Cell_Process` (FOOT.CPP:1359).
- `DriveClass::AI()` — DRIVE.CPP:1222; mission-level `FootClass::Mission_Move()`
  — FOOT.CPP:552.

**Aircraft/free flight (FlyClass):**
- `FlyClass::Physics(COORDINATE &coord, DirType facing)` — FLY.CPP:62 — vector
  move via `Coord_Move` (:79); `IMPACT_EDGE` off-map (:98-101). No cell-track
  constraint.

## 3. Facing / rotation

- **`DirType`** — DEFINES.H:2037-2050 — a **single byte (0–255) compass** (256
  steps): `DIR_N=0, DIR_NE=32, DIR_E=64, … DIR_NW=224`; `operator+` wraps mod 256.
  `FacingType` is the coarser 8-way enum for path steps.
- **`FacingClass`** — FACING.H:47 — holds `CurrentFacing`/`DesiredFacing`
  (FACING.H:74-75). `Set_Desired` (FACING.CPP:86), `Is_Rotating()` (FACING.H:67),
  `Difference()` shortest signed delta (FACING.H:69).
- `Rotation_Adjust(int rate)` — FACING.CPP:142 — the stepper: snaps if
  `ABS(diff)<rate` else rotates `rate` toward desired; returns true when it
  crosses into a new 1/32 sprite zone (`Facing_To_32`, :180) so the object
  redraws. Units carry `PrimaryFacing` (body) + `SecondaryFacing` (turret).

## 4. Targeting — the TARGET encoded handle

**`TARGET` is a packed integer, not a pointer** (TARGET.H:63-85): low 12 bits
(`TARGET_MANTISSA=12`) = index in the per-type array; high bits = `KindType`
(`KIND_UNIT`/`KIND_INFANTRY`/`KIND_BUILDING`/`KIND_CELL`/…, TARGET.H:46-60).
This survives networking + object deletion safely (a stale index resolves to
NULL). See also [02-object-hierarchy.md](02-object-hierarchy.md) §TARGET.

**Decode → pointers** (all in [TARGET.CPP](../../reference/CnC_Tiberian_Dawn/TARGET.CPP)):
- `As_Object(TARGET)` — TARGET.CPP:226 — central switch mapping `KindType`→global
  heap (`Units.Raw_Ptr`, `Infantry.Raw_Ptr`, …) by `Target_Value` (:231-262).
- `As_Techno` :199, `As_Unit` :296, `As_Building` :347, `As_Cell` :399,
  `As_Coord` :421.

**TarCom (target computer):**
- `TARGET TarCom` on TechnoClass — TECHNO.H:164 (+ `SuspendedTarCom` :165).
- `TechnoClass::Assign_Target(TARGET)` — TECHNO.CPP:1887.
- `TarComClass::AI()` — TARCOM.CPP:99 — each frame picks primary/secondary
  weapon, `Can_Fire` (:112-119), on `FIRE_OK` → `Fire_At` + weapon sound
  (:131-133); on `FIRE_FACING` rotates toward target (:136-150); keeps
  `SecondaryFacing` aimed (:159-164).
- `FootClass::Greatest_Threat(...)` — FOOT.CPP:52 — selects the best enemy.

## 5. Combat resolution — damage, warheads, armor

**Firing → bullet:** `TechnoClass::Fire_At(TARGET, int which)` — TECHNO.CPP:1966.
Picks the `WeaponTypeClass` (:1974), computes fire direction (:2004-2011),
`new BulletClass(weapon->Fires)` (:2018), sets `bullet->Strength =
weapon->Attack` and `bullet->Payback = this` (:2021-2022), `Unlimbo` launches
(:2031), sets rearm `Arm` (:2045), decrements `Ammo` (:2076-2078).
- `Can_Fire(TARGET, which)` — TECHNO.CPP:1775 → `FireErrorType`
  (`FIRE_ILLEGAL/CANT/REARM/RANGE/AMMO/OK`); checks cloak, weapon present,
  anti-air, `Arm` timer, `In_Range`, `Ammo`.
- Turret overrides: `TurretClass::Fire_At` TURRET.CPP:251, `Can_Fire`
  TURRET.CPP:293.

**Applying damage — `Take_Damage` chain**
(`ResultType Take_Damage(int &damage, int distance, WarheadType, TechnoClass
*source)`):
- `ObjectClass::Take_Damage` — OBJECT.CPP:1274 — the real math: skips if
  `IsImmune` (:1279); `Modify_Damage(...)` (:1286); result tiers
  `RESULT_LIGHT/HALF/MAJOR/DESTROYED` by strength thresholds (:1292-1334);
  clamps a lethal blow to 1 HP unless it kills (:1298-1311); on 0 →
  `Record_The_Kill` + `Detach_All` (:1322-1326); springs `EVENT_ATTACKED`
  triggers (:1339-1340).
- `TechnoClass::Take_Damage` — TECHNO.CPP:2581 — on destroy sends
  `RADIO_OVER_OUT` + `Stun()`; else flags `IsTickedOff` (retaliation).
- Also overridden per type (INFANTRY.H:169, UNIT.H:134, BUILDING.H:241,
  AIRCRAFT.H:143, TERRAIN.H:111).

**Warhead-vs-armor math:** `Modify_Damage(int damage, WarheadType, ArmorType,
int distance)` — **[COMBAT.CPP:72](../../reference/CnC_Tiberian_Dawn/COMBAT.CPP#L72)**:
- `whead = &Warheads[warhead]` (:80).
- `damage = Fixed_To_Cardinal(damage, whead->Modifier[armor])` (:82) — the
  per-armor % table.
- Falloff: `distance >>= whead->SpreadFactor; distance = Bound(distance,0,16);
  damage >>= distance;` (:89-91).

> The clone ports this as `Modify_Damage` (MILESTONES Phase 5): `Verses=` order
> is none,wood,light,heavy,concrete; falloff divisor `Spread*5` leptons. The
> original's `Modifier[armor]` table + `SpreadFactor` are the source of truth.

**Splash:** `Explosion_Damage(COORDINATE, strength, source, WarheadType)` —
COMBAT.CPP:132 — gathers objects in the impact cell + 8 neighbors (:159-176) and
`Take_Damage`s each with distance-scaled strength.

**Data tables:**
- **Weapons:** `Weapons[WEAPON_COUNT]` — [CONST.CPP:76-102](../../reference/CnC_Tiberian_Dawn/CONST.CPP#L76). Row = `{Fires (BulletType), Attack, ROF, Range (leptons), Sound, Anim}`. Struct TYPE.H:53-95. Enum `WeaponType` DEFINES.H:1739.
- **Warheads:** `Warheads[WARHEAD_COUNT]` — CONST.CPP:111-124. Row = `{SpreadFactor, IsWallDestroyer, IsWoodDestroyer, IsTiberiumDestroyer, Modifier[ARMOR_COUNT]}`. `Modifier` is the 5-column armor table `{none, wood, aluminum, steel, concrete}` in fixed-point (0xFF=100%). Struct TYPE.H:102-133. Enum `WarheadType` DEFINES.H:1714-1732.
- **Armor:** `enum ArmorType` DEFINES.H:1778-1786 (`ARMOR_NONE/WOOD/ALUMINUM/STEEL/CONCRETE`).
- Externs at EXTERNS.H:341-342.

Example (CONST.CPP:112): `WARHEAD_SA = {2,false,false,false,{0xFF,0x80,0x90,0x40,
0x40}}` → small arms 100% vs unarmored infantry, ~25% vs steel — the classic
AP-vs-armor / HE-vs-infantry rock-paper-scissors. **This single table is the
entire combat balance.**

## 6. Bullet / projectile lifecycle

Bullets live in the global `Bullets` heap; `BulletTypeClass` static data
(`IsHoming`, `IsArcing`, `IsDropping`, `IsAntiAircraft`, `Warhead`, `ROT`,
`Explosion`) in **[BBDATA.CPP](../../reference/CnC_Tiberian_Dawn/BBDATA.CPP)**
drives behavior. In [BULLET.CPP](../../reference/CnC_Tiberian_Dawn/BULLET.CPP):
1. **Create:** `new BulletClass(weapon->Fires)` from `Fire_At`; ctor BULLET.CPP:194 (pool `new`/`delete` :139/:164).
2. **Launch:** `Unlimbo(COORDINATE, DirType)` — BULLET.CPP:634 (facing/altitude/fuse).
3. **Update:** `BulletClass::AI()` — BULLET.CPP:310 — arcing/dropping gravity
   (:321-340), homing re-aim every other frame (:347-348), rotation (:367-369),
   `Physics(coord, PrimaryFacing)` (:371). Off-map → delete (:378-383).
4. **Detonate:** on fuse/target (:416,437-439) → `Explosion_Damage(Coord,
   Strength, Payback, Class->Warhead)` (:445), spawn explosion anim (:467-468),
   `delete this` (:470). SAM/AA apply damage directly (:446-457).
5. **Cleanup:** `Detach(TARGET, bool)` — BULLET.CPP:601 — clears `TarCom`/`Payback`
   if the referenced object dies mid-flight.
