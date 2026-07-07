# Handoff — session 2 → session 3 (2026-07-06)

Session 2 completed Phases 2 AND 3 (map rendering + units on screen), plus a
late fix: mouse.shp is Dune II-format SHP → new decoder src/formats/shpd2.cpp.

## Read first

1. `MILESTONES.md` — Phase 0–3 done and verified; **Phase 4 (simulation core)
   is next and not yet started.** Phase 9 (map editor) queued as a side quest.
2. `README.md` — build commands and tool usage (incl. `mapview`).
3. `ASSETS.md` — where every asset lives.

Don't re-read `src/formats/` (PAL/SHP/TMP/AUD/INI/LCW/CPS decoders, verified)
or `src/game/` (map loader, house remaps, render helpers — all verified via
mapview renders in three theaters).

## Current state

- Everything compiles clean: `cmake --build build --config Release` (MSVC).
- `mapview <map.ini> <data-root>` is the de-facto game shell: renders any RA
  map with terrain/overlays/terrain-objects plus scenario structures, units
  and infantry (house colors, facings, turrets), interactive scrolling,
  click/drag-box selection with brackets + health bars, SHP cursor.
- No simulation exists: nothing moves, no game loop, no rules.ini stats.

## Next task: Phase 4 — simulation core

Suggested order:
1. **Fixed-tick loop**: split mapview's interactive mode into sim tick
   (default 15/s like RA) + render; probably time to promote it to `game.exe`
   and keep mapview as pure viewer.
2. **rules.ini stats**: load `red_alert/*/INSTALL/REDALERT/local/rules.ini`
   ([JEEP] Speed=, Strength=...) into unit type table. INI parser handles it
   already (verified in Phase 1).
3. **Movement**: cell occupancy grid from map + structures; A* over it;
   right-click move orders for selected units; smooth per-tick interpolation
   cell→cell; rotate facing toward heading (TD/RA turn logic: step facing by
   ROT per tick).
4. **Shroud** can wait until the end of the phase.

## Gotchas not in the docs

- Infantry art is in `INSTALL/REDALERT/hires/`, not conquer. Civilians c3–c10
  have no art; fall back to c1 (mapview does).
- War factory = weap.shp + weap2.shp roof overlay (mapview handles `<type>2`).
- Structures like BARL (explosive barrels) appear in [STRUCTURES]; their
  selection boxes look oversized because SHP frames have empty margins —
  cosmetic, fix someday with trimmed bounds.
- Facing 0 = north, increases clockwise, 32 per compass step;
  frame = BodyShape[facing>>3] (`game::facingToFrame`). Infantry standing
  frame = `(8 - (facing>>5)) & 7`.
- House→color: HDATA.CPP mapping in `game/house.cpp` (Greece=LtBlue, USSR=Red,
  England=Green...). Remaps built from PALETTE.CPS row 0 → row pcolor.
- Shadow index 4 = 50% darken approximation (not original fading tables).

## Verification recipe for Phase 4

Interactive: select a jeep on scg01ea, right-click across the map — it should
path around water/cliffs, rotate smoothly, and arrive. Headless: add a
`--sim-ticks N --move unit,cell` style debug flag and dump before/after BMPs;
compare positions.

## Context handoff protocol

At ~75% context: update MILESTONES.md checkboxes + session log, rewrite this
file for the next session, commit and push.
