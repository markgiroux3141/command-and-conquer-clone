# Handoff — session 11 → Phase 7 (written 2026-07-14)

Session 11 was another feedback-driven polish pass on the TD shell (sidebar
build clock, placement hatch, infantry walk/fire animations synced to the
weapon sound, per-column sidebar scroll, and several HUD fixes — all confirmed
by the user). The engine is now a **solid, playable sandbox**. The next big step
is **Phase 7 — AI & missions**, which is what turns it into an actual game.

## Read first

1. `MILESTONES.md` — the roadmap. Phase 7 is the block near the bottom; the
   session-11 log entry (top of the log) + Phase 10 bullet list is the delta.
2. `docs/original-source/06-houses-ai-missions.md` — **the key reference for
   Phase 7.** Covers `HouseClass` (economy/power/AI state), the `Mission_*`
   state machine, scenario `.INI` loading, triggers/teamtypes, `FactoryClass`,
   and the base-building AI. Points into `reference/CnC_Tiberian_Dawn/`
   (`HOUSE.CPP`, `TEAM.CPP`, `TEAMTYPE.CPP`, `TRIGGER.CPP`, the `MISSION*.CPP`).
3. `README.md` / `play.bat` — how to run.
4. Source, only as needed (below).

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release`. Sim is headless and
  **deterministic** (don't break that — see below). `play.bat` runs TD mission 1.
- **Playable sandbox**: build from the sidebar (prereqs/power/credits enforced),
  place structures / deploy MCV, move + attack, harvest ore → credits, combat
  with armor/warheads/projectiles, shroud. Auto-acquire works (idle armed units
  fire on the nearest enemy in range; guarding units hold their post).
- **HUD**: native-res render + letterbox scale, contextual `mouse.shp` cursors,
  dynamic sidebar (cameos gated by prereqs, per-column scroll+arrows, build
  clock, READY text), radar medallion→minimap, power bar, sell/repair, buildup
  animations, EVA + unit-voice audio, music jukebox.
- **What's missing (the Phase 7 gap): no win/lose, no enemy AI (it only
  return-fires — never builds/expands/attacks), no mission scripting.** It's a
  sandbox with a passive enemy.

## Where the code lives (Phase 7 hook points)

- `src/game/sim.{h,cpp}` — the deterministic sim. `Sim::tick()` is the per-tick
  entry; it already loops units (`tickUnit`/`tickAutoAcquire`/`tickHarvest`),
  production (`tickProduction`), and projectiles. **Houses are just string keys**
  (`"GoodGuy"`=GDI, `"BadGuy"`=Nod, `"Neutral"`=civilian) with a `credits_` map —
  there is **no `HouseClass` struct and no game-over state yet**. Phase 7 adds
  both.
- `src/game_main.cpp` — the shell / render loop / input. `processEvents()` maps
  sim `Event`s to SFX/EVA. A win/lose banner + end-of-game handling goes here.
- `src/game/rules.{h,cpp}` + `td_rules.ini` — unit/structure/weapon stats.
- `src/game/map.cpp` — scenario `.bin`/`.ini` loader (`loadTd`). Triggers/
  teamtypes/waypoints would be parsed here from the `.ini` sections.

## Suggested Phase 7 order (smallest → biggest)

1. **Win/lose conditions** (start here — small, high-impact). Add game-over
   detection to the sim: a house is defeated when it has no structures and no
   MCV/units left (TD `HouseClass::MPlayer_Defeated` / short-game logic). Expose
   `Sim::winner()` / a defeated set; `game_main.cpp` shows a "MISSION
   ACCOMPLISHED / FAILED" banner and stops issuing orders. Add a headless flag
   (e.g. `--until-win`) so it's testable without the window.
2. **Minimal skirmish AI** (the meat). Give each non-player house a `tickAI`
   (rate-limited, not every tick): keep an MCV deployed, build power → refinery
   → barracks/factory when it can afford it (reuse `startProduction`/
   `placeBuilding`), train a few units, and send an attack group at the player's
   base when it has N units. Reference: `HOUSE.CPP` `AI()` + TeamTypes, but a
   simplified state machine is fine to start. Keep all randomness seeded so the
   sim stays deterministic.
3. **Mission scripting** (biggest). Parse `[Triggers]`/`[TeamTypes]`/`[CellTriggers]`
   from the scenario `.ini` (see doc 06 + `TRIGGER.CPP`/`TEAMTYPE.CPP`); run a
   trigger evaluator in the sim (events → actions: reinforce, win, lose, produce).
   Then wire up **playable TD GDI mission 1**.

## Gotchas / constraints

- **Determinism is sacred.** The headless sim must stay reproducible (identical
  run → identical hashes; this is how combat/economy were verified). Don't call
  `SDL_GetTicks()`/wall-clock or unseeded RNG inside `sim.tick()` or anything it
  calls. AI randomness must come from a seeded generator advanced in tick order.
  (Render-only animation in `game_main.cpp` may use `SDL_GetTicks()` — e.g. the
  infantry walk cycle does — because it never feeds back into the sim.)
- **House names**: `GoodGuy`=GDI, `BadGuy`=Nod, `Neutral`=civilian.
- **TD production factories**: infantry ← `pyle`/`hand`, vehicles ← `weap` **or**
  `afld` (Nod builds vehicles at the Airstrip). Prereq chain: fact→powr→
  barr/proc→weap. `startProduction`/`canProduce` already encode this — the AI
  should go through them, not bypass them.
- **Placement**: `Sim::canPlace(house, cell, w, h)` (footprint buildable+free and
  adjacent to a friendly structure); `Sim::cellBuildable(cell)` for a single
  cell. The AI needs a "find a buildable spot near my base" helper.
- Infantry animation frame layout (if you touch it): STAND {0,1,1}, WALK
  {16,6,6}, FIRE {64,N,N} per weapon, civilians walk at frame 56 — from
  `IDATA.CPP` DoControls. Frame = `base + facing*cycle + (stage % cycle)`.

## Verification recipe

```
# Build:
cmake --build build --config Release

# HUD screenshot (one frame, then quits) — sanity that render/sidebar still work:
build\Release\game.exe data\assets\tiberian_dawn\nod\GENERAL\scb03ea.ini ^
  data\assets\tiberian_dawn\nod --house GoodGuy --no-shroud --ui-shot out.bmp
python -c "from PIL import Image; Image.open('out.bmp').save('out.png')"  # then Read the PNG

# Headless sim (deterministic) — use for win/lose + AI logic without the window:
build\Release\game.exe <map.ini> <root> --house GoodGuy --no-shroud --sim-ticks N ...
#   existing debug flags: --build b|i|v,type  --place x,y  --deploy idx
#   --move / --attack / --harvest / --select --dump sel.bmp  (see game_main.cpp arg parsing)
# For Phase 7, add a headless win-condition flag and assert the winner.

# Interactive: play.bat (or the game.exe line above without --ui-shot).
```

## Git / housekeeping

- Session-11 work is committed & pushed to `main`. Working tree clean at handoff.
- **Never commit game assets** (`data/`, `renders/` are gitignored — trademarks
  stay out of any release; this is a personal-use clone). Only source + docs.
- Commit message trailer in use: `Co-Authored-By: Claude ...`.

## Context handoff protocol

At ~75% context: tick `MILESTONES.md`, add a session-log entry, rewrite this
file for the next session, commit and push.
