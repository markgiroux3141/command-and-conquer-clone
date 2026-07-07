# Handoff — session 3 → session 4 (2026-07-06)

Session 3 completed Phase 4's simulation core: `game.exe` (fixed 15/s tick
loop), rules.ini stat loading, land-type passability from TMP control maps,
A* pathfinding with cell-occupancy collision. **Shroud is the one Phase 4 box
left unticked.**

## Read first

1. `MILESTONES.md` — checkboxes + per-phase gotchas are current.
2. `README.md` — build commands, `game.exe` usage (incl. headless sim flags).
3. Only then source: `src/game/sim.{h,cpp}` + `src/game/rules.{h,cpp}` are the
   new sim core; `src/game_main.cpp` is the shell. Don't re-read
   `src/formats/` or the render/map/house code (all verified).

## Current state

- Everything compiles clean: `cmake --build build --config Release` (MSVC).
- `game.exe <map.ini> <data-root>` plays a scenario: select units, right-click
  move orders, units path around water/cliffs/structures, pivot at ROT, spread
  onto adjacent cells instead of stacking. Sim is deterministic, in leptons
  (256/cell), advanced only by `Sim::tick()` at 15/s.
- `mapview` unchanged (pure viewer). game_main.cpp duplicates mapview's
  ArtCache/terrain-bake code — consolidation into the game lib is a pending
  cleanup, do it when one of the two next changes there.
- Verified headlessly (see recipe) + interactive smoke test only. Nobody has
  played it with eyes-on yet — worth doing once before building more on top.

## Next task: finish Phase 4, then Phase 5 (combat & economy)

1. **Shroud**: per-house explored bitmap, reveal radius = Sight from
   UnitStats (already loaded), black/half-dark overlay at render. Original:
   MAP.CPP Sight_From.
2. Then Phase 5 (weapons/damage from rules.ini, death anims, harvester loop,
   power) — or Phase 9 map editor if variety appeals; Phase 4 unblocks it.

## Gotchas not in MILESTONES.md

- `game` CMake target name was taken by the engine lib → the exe target is
  `game_exe` with OUTPUT_NAME `game`.
- Sim facing is DirType (0=N, 64=E, clockwise, 0-255); `directionTo(dx,dy)`
  handles the +y-is-south flip. Draw-frame mapping stays `game::facingToFrame`
  (vehicles) / `(8-(facing>>5))&7` (infantry standing).
- Non-infantry occupancy: a moving unit owns `occCell` **and** reserves
  `path.front()` before entering (`occupant_` grid). If you add
  teleport/death, free both cells or units will path around ghosts.
- orderMove spirals per call; two separate orderMove calls can pick the same
  destination (occupancy isn't claimed until arrival) — the loser re-paths and
  parks adjacent, which looks fine, but a shared claim map would be cleaner.
- Infantry walk animations don't exist yet — they slide in their standing
  frame. Walk cycles are ~Phase 5 polish (frames after the 8 standing ones).
- rules.ini land percents parse via std::stoi("90%") → 90; Winged hardcoded
  100% everywhere.

## Verification recipe

Headless (was used this session):

```
build\Release\game.exe data\assets\red_alert\allied\MAIN\general\scg01ea.ini ^
    data\assets\red_alert\allied --sim-ticks 600 --move 0,55,75 --dump out.bmp
```

- unit 0 (Greece jeep at 63,50) must end at exactly `cell 55,75`, having
  crossed the cliff pass; convert BMP → PNG (PIL) and Read it to eyeball.
- `--move 0,50,79` (water) → settles at nearest land (48,77), sim goes idle.
- Three `--move i,55,75` orders → adjacent cells, never stacked.
- Interactive: select jeeps on scg01ea, right-click across the river — smooth
  pivot + path around water at ~1.5 cells/s.

## Context handoff protocol

At ~75% context: update MILESTONES.md checkboxes + session log, rewrite this
file for the next session, commit and push.
