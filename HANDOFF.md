# Handoff — session 3 → session 4 (written 2026-07-06/07)

Session 3 completed **all of Phase 4**: `game.exe` (fixed 15/s tick loop),
rules.ini stat loading, land-type passability from TMP control maps, A*
pathfinding with cell-occupancy collision, and shroud.

## Read first

1. `MILESTONES.md` — checkboxes + per-phase gotchas are current.
2. `README.md` — build commands, `game.exe` usage (incl. headless sim flags).
3. Only then source: `src/game/sim.{h,cpp}` + `src/game/rules.{h,cpp}` are the
   sim core; `src/game_main.cpp` is the shell. Don't re-read `src/formats/`
   or the render/map/house code (all verified).

## Current state

- Everything compiles clean: `cmake --build build --config Release` (MSVC).
- `game.exe <map.ini> <data-root>` plays a scenario: select units, right-click
  move orders, units path around water/cliffs/structures, pivot at ROT, spread
  onto adjacent cells instead of stacking; shroud reveals with unit Sight as
  they move (`--house`, `--no-shroud`). Sim is deterministic, in leptons
  (256/cell), advanced only by `Sim::tick()` at 15/s.
- `mapview` unchanged (pure viewer). game_main.cpp duplicates mapview's
  ArtCache/terrain-bake code — consolidate into the game lib next time either
  copy needs changing.
- Verified headlessly (see recipe) + interactive smoke test only. Nobody has
  played it with eyes-on yet — worth doing once before building more on top.

## Next task: Phase 5 (combat & economy) — or Phase 9 (map editor)

Phase 5 suggested order:
1. Weapons/projectiles/warheads from rules.ini ([M60mg], [105mm]... Damage/
   ROF/Range/Speed; warhead Verses= vs armor classes). Reference: RULES.CPP,
   COMBAT.CPP Modify_Damage.
2. Attack orders: right-click enemy → chase into Range, turret tracks target,
   fire on ROF cooldown, apply damage; health bars already render.
3. Death: unit removal + explosion anim SHPs (fire/explosion art in conquer,
   e.g. veh-hit1, fball1); an Anim layer in the sim.
4. Harvester loop + credits, then power. Sidebar/production is Phase 6.

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
  frame (walk cycles are the frames after the 8 standing ones). They also
  don't occupy cells (no infantry collision).
- Shroud reveal only triggers while a unit `moving()` (plus spawn/structure
  load) — a future Chrono teleport or reinforcement drop must call
  `Sim::reveal` itself. Unexplored = flat black; the original's soft shroud
  edges (shadow.shp) are still todo, as are the real fading tables (shadow
  index 4 is a 50% darken).
- rules.ini land percents parse via std::stoi("90%") → 90; Winged hardcoded
  100% everywhere.

## Verification recipe

Headless (used this session):

```
build\Release\game.exe data\assets\red_alert\allied\MAIN\general\scg01ea.ini ^
    data\assets\red_alert\allied --sim-ticks 600 --move 0,55,75 --dump out.bmp
```

- unit 0 (Greece jeep at 63,50) must end at exactly `cell 55,75`, having
  crossed the cliff pass; convert BMP → PNG (PIL) and Read it to eyeball.
- `--move 0,50,79` (water) → settles at nearest land (48,77), sim goes idle.
- Three `--move i,55,75` orders → adjacent cells, never stacked.
- Shroud: `--sim-ticks 0 --dump` shows only sight circles around the Greece
  start force; after the 55,75 trip the jeep's route is a revealed corridor
  through the (previously hidden) Soviet base. `--no-shroud` shows all.
- Interactive: select jeeps on scg01ea, right-click across the map — smooth
  pivot + path around water at ~1.5 cells/s, shroud lifting as they go.

## Context handoff protocol

At ~75% context: update MILESTONES.md checkboxes + session log, rewrite this
file for the next session, commit and push.
