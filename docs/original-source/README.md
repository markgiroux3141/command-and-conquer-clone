# Original Source — Navigation Guide

**Breadcrumbs into the original Command & Conquer source code.** When the user
says *"refer to the original source code,"* start here.

## What & where

The original **C&C: Tiberian Dawn** source (Westwood Studios; EA's 2020 GPL v3
release) lives locally at:

- In-repo (clickable, **gitignored** — cloned separately, not vendored):
  `reference/CnC_Tiberian_Dawn/`
- Also on disk at: `D:\Command and Conquer Original source code\CnC_Tiberian_Dawn`
  (same repo, `github.com/electronicarts/CnC_Tiberian_Dawn`).

It's ~156 `.CPP` + ~133 `.H` files in one **flat, UPPERCASE** directory. All the
paths in these docs are repo-relative to `reference/CnC_Tiberian_Dawn/` so links
resolve in the editor. Author of nearly all gameplay code: **Joe L. Bostic**.

**Companion:** `reference/CnC_Red_Alert/` is the Red Alert source — same engine
lineage, and crucially it **ships the low-level `WWFLAT32`/`WIN32LIB` library**
(SHP blitter, LCW codec, `.AUD` decoder, DirectDraw) that the TD drop omits. When
a TD breadcrumb says "this lives in WWLIB32, not in this drop," look in the RA
source. `reference/OpenRA/` and `reference/CnC_Remastered_Collection/` are further
cross-references (OpenRA is the clone's format-decoder reference).

> ⚠️ This is a **reference-only** GPL codebase for a personal-use clone. It does
> not compile as-is (needs Watcom C++, DirectX 5 SDK, and missing libraries).
> We read it to match original behavior — we don't build it. See
> [cnc-clone-asset-sources.md](../../cnc-clone-asset-sources.md) for licensing.

## The docs

| # | Doc | Covers |
|---|---|---|
| 01 | [App lifecycle & main loop](01-app-lifecycle-and-main-loop.md) | `WinMain`→`Main_Game`→`Main_Loop`, `Logic.AI()`, the event/command queue, per-frame ordering, globals |
| 02 | [Object class hierarchy](02-object-hierarchy.md) | `AbstractClass`→…→`UnitClass`/`BuildingClass`; the parallel `*TypeClass` tree; where the `*DATA.CPP` stat tables live; the `TARGET` handle |
| 03 | [Map, terrain & coordinates](03-map-terrain-coordinates.md) | COORDINATE/CELL/lepton system, the `Map` screen chain, `CellClass`, templates/terrain/overlay/smudge, tiberium growth |
| 04 | *(see 05)* | — |
| 05 | [Movement, pathfinding & combat](05-movement-pathfinding-combat.md) | `Find_Path` (a wall-follower, not A*), NavCom/DriveClass/FlyClass, facing, TarCom, `Modify_Damage` armor table, bullets |
| 06 | [Houses, AI, missions, triggers](06-houses-ai-missions.md) | `HouseClass` (player/economy/power/AI), the `Mission_*` state machine, scenario `.INI` loading, triggers/teamtypes, `FactoryClass`, base-building AI |
| 07 | [UI / HUD / input](07-ui-hud-input.md) | The single-inheritance `Map` chain (screen→…→mouse), sidebar/radar/power/tab, the `GadgetClass` widget family |
| 08 | [File formats & data](08-file-formats-and-data.md) | `.MIX` archives, `.SHP` shapes, master enums in `DEFINES.H`, the `CONST.CPP`/`*DATA.CPP` tables, object heaps, fixed-point + RNG, INI, `.AUD` |

*(There's no separate doc 04 — movement/pathfinding/combat were consolidated into 05.)*

## Naming conventions & traps

- **`FooClass`** = live instance (mutable per-object state). **`FooTypeClass`** =
  shared const stats for a *kind*. Instance holds `Class` → its type. All type
  classes are declared in one header, [TYPE.H](../../reference/CnC_Tiberian_Dawn/TYPE.H).
- **`*DATA.CPP` name traps:** `ADATA`=anims (not aircraft — that's `AADATA`);
  `BDATA`=buildings (bullets are `BBDATA`); `CDATA`=templates/cells (trees are
  `TDATA`). Full table in [02](02-object-hierarchy.md).
- **`TARGET`** is a packed integer handle (kind + index), *not* a pointer — decode
  with `As_Object`/`As_Cell`/`As_Coord` in `TARGET.CPP`.
- **`COORDINATE`** = lepton position (256 leptons/cell); **`CELL`** = tile index
  (`(Y<<6)|X` on the 64×64 grid). Conversion macros in `FUNCTION.H`.
- **`DirType`** = 256-step byte compass; **`FacingType`** = 8-step (path steps).
- No `GLOBALS.H` — globals are *defined* in `GLOBALS.CPP`, *declared* in `EXTERNS.H`.
- Nearly every `.CPP` starts with `#include "function.h"` — [FUNCTION.H](../../reference/CnC_Tiberian_Dawn/FUNCTION.H)
  is the master header (prototypes + class-hierarchy diagrams in comments).

## Fastest way in

1. **Behavior of the game loop / frame order** → [01](01-app-lifecycle-and-main-loop.md),
   then `CONQUER.CPP` `Main_Loop` (:1458) and `LOGIC.CPP` `AI` (:168).
2. **How an object type works** → [02](02-object-hierarchy.md) for the class, then
   its `.CPP` + its `*DATA.CPP` stat block.
3. **A specific rule/number** (damage, speed, cost) → [08](08-file-formats-and-data.md)
   §4 (`CONST.CPP` + `*DATA.CPP`) and the enum table for `DEFINES.H`.
4. **A gameplay mechanic** (pathfinding, harvesting, production, triggers) → the
   matching subsystem doc's "Quick file map" table at the bottom.

## Clone cross-references

These docs note where the clone already mirrors the original. Anchors:
[MILESTONES.md](../../MILESTONES.md) (phase log + gotchas),
[src/formats/](../../src/formats/) (format decoders),
[src/game/](../../src/game/) (sim/map/render/rules/house/audio),
[td_rules.ini](../../td_rules.ini) (our INI recreation of the `CONST.CPP`/`*DATA.CPP`
stat tables).
