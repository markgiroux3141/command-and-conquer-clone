# Handoff — session 12 → Phase 7 polish / Phase 8 (written 2026-07-14)

Session 12 landed **all of Phase 7 (AI & missions)**: win/lose conditions, a
deterministic skirmish AI (base-building + attack waves), and mission scripting
(triggers/teamtypes/waypoints), with TD **GDI mission 1** as the worked example.
The engine is now an actual game: an enemy that builds and attacks, scripted
reinforcements/waves, and a win/lose outcome. See the Phase 7 block + session-12
log in `MILESTONES.md` for the full detail.

## Read first

1. `MILESTONES.md` — Phase 7 is now ticked; the session-12 log entry is the delta
   and lists the exact next-task candidates.
2. `docs/original-source/06-houses-ai-missions.md` — still the key reference
   (HouseClass AI, Mission_* state machine, triggers/teamtypes, reinforcements,
   superweapons). The clone implements a *simplified* subset of this.
3. `README.md` / `play.bat` — how to run.

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release`. Sim is headless and
  **deterministic** — verified again this session (AI + triggers byte-identical
  across runs). Don't break that (see Gotchas).
- **Win/lose**: `Sim` latches a house defeated at zero assets, `winner()`/
  `gameOver()`/`missionResult()` resolve the outcome; interactive shell shows a
  MISSION ACCOMPLISHED/FAILED banner + EVA and freezes orders.
- **Skirmish AI** (`Sim::setAI`/`tickAI`): deploys the MCV, builds the tech
  chain (power→refinery→barracks→factory), trains rifles + harvesters + tanks,
  and throws attack waves at the nearest enemy. On by default for enemy houses.
  Refineries grant a free harvester so the economy sustains (if ore is reachable).
- **Mission scripting**: `[Triggers]`/`[TeamTypes]`/`[Waypoints]` parse into
  `MapFile`; the sim's trigger evaluator handles Time / All Destr. / Bldgs Destr.
  → Win / Lose / Reinforce / Create Team / Production on the ~6s cadence.
- **Playtested**: GDI mission 1 plays and is winnable (MISSION ACCOMPLISHED
  confirmed). Camera now confines to the playable bounds (out-of-bounds cells
  marked impassable) so units/reinforcements can't wander off the scrollable map.

## Where the Phase-7 code lives

- `src/game/sim.{h,cpp}` — all the sim-side work:
  - Win/lose: `combatants_`/`defeated_`, `evaluateDefeat()`, `winner()`.
  - AI: `aiHouses_`, `tickAI()`, `aiFindBuildSpot()`, `footprint_`/`setFootprint`,
    `grantHarvester()`, attack cooldown `aiAttackCd_`.
  - Scripting: `TriggerDef`/`TeamTypeDef`/`MissionResult`, `triggers_`,
    `teamTypes_`, `waypoints_`, `tickTriggers()`/`runTriggerAction()`/
    `spawnTeam()`. `tick()` calls AI + triggers up top (deterministic, keyed off
    `tickCount_`).
- `src/game/map.{h,cpp}` — `parseScripting()` fills `MapFile::triggers/
  teamTypes/waypoints` (called from both RA `load` and TD `loadTd`).
- `src/game_main.cpp` — the shell bridges map→sim: registers building footprints
  (from the art), enables AI on enemy houses (`--no-ai`/`--ai`/`--ai-credits`),
  translates the scripting sections, reconciles AI-placed structures into the
  draw list (top of `buildDrawList`), latches the win/lose banner, and honors
  `--until-win` headless.

## Suggested next work (Phase 7 polish, then Phase 8)

1. **Coordinated TeamType missions.** Right now spawned team members just get
   folded into the AI's generic attack wave. The originals script per-team
   `Move:wpt`/`Attack`/`Guard` sequences (TEAM.CPP `Coordinate_*`). Retain the
   TeamType mission list (parsed-past today) and drive spawned teams along it.
2. **Real reinforcement entry.** Teams spawn near the house's base; the original
   enters from the map edge / a waypoint, and `LST:` is a naval landing (skipped
   today). Use `[Waypoints]` + map-edge pathing; add naval/air later.
3. **More trigger coverage.** Superweapons (Ion/Nuke/Airstrike), `Built It`,
   `Discovered`, `Credits`, cell triggers, trigger-destroys-trigger chaining.
4. **AI depth.** Defensive-structure building (gun/gtwr/obli/sam), difficulty
   (attack cadence + stipend), rebuild-destroyed-buildings, per-map `[Base]`
   prebuilt list (INI.CPP:441 / BASE.CPP).
5. **Phase 8**: main menu, in-game options, save/load; more EVA cues.

## Verification recipe

```
cmake --build build --config Release

# Win/lose + AI + triggers, headless (deterministic; enable AI with --ai):
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy --no-shroud --until-win --ai
#   Reinforcement check: --sim-ticks 300 --ai then count "after: unit .*GoodGuy"
#   (6 at t0 -> 9 at t300 -> 12 at t600 as GDI reinforcements arrive).
#   Base-build check: --house BadGuy (GoodGuy becomes AI, has the MCV) --sim-ticks 4000 --ai

# HUD screenshot (one frame): --ui-shot out.bmp  (then PIL-convert + Read the PNG)

# For sim-level checks that need a controlled setup (win-latch, trigger actions,
# AI-vs-AI to a winner) this session used throwaway exe targets wired into
# CMakeLists (added, built, run, then removed). Re-add the same way if needed —
# link `game SDL2::SDL2 SDL2::SDL2main`, use Sim + Rules::load("td_rules.ini"),
# setFootprint/setOre/addUnit/setAI directly. All were deterministic.
```

## Gotchas / constraints

- **Determinism is sacred.** AI + triggers are keyed off `tickCount_` only — no
  wall-clock, no unseeded RNG in `tick()`. The AI is fully deterministic (no RNG
  yet); if you add difficulty jitter, seed it and advance in tick order.
- AI think cadence = every 15 ticks (staggered per house). Trigger cadence =
  every 90 ticks (`TICKS_PER_MINUTE/10`) starting at tick 90; a Time trigger
  with `Data=N` fires after `N*90` ticks.
- **Footprints** for AI placement come from the art via `Sim::setFootprint`
  (shell registers `fact` + all struct types). A type with no footprint won't be
  built by the AI.
- House names: `GoodGuy`=GDI, `BadGuy`=Nod, `Neutral`=civilian (excluded from
  combatants). AI is off for `playerHouse_`.
- Economy needs reachable ore. scg01ea's GDI landing beach has none nearby, so
  an AI there builds a base but stalls at 0 credits — that's the map, not a bug.
  Synthetic tests with an ore patch confirm the full harvest→credits→army loop.

## Git / housekeeping

- **Session-12 Phase 7 work is committed & pushed to `main`** (win/lose, AI,
  mission scripting, bounds-confine camera fix). Working tree clean at handoff.
- Temp test files (wintest/aitest/scripttest) were removed; `CMakeLists.txt` is
  back to its original targets.
- **Never commit game assets** (`data/`, `renders/` are gitignored).
- Commit trailer in use: `Co-Authored-By: Claude ...`.

## Context handoff protocol

At ~75% context: tick `MILESTONES.md`, add a session-log entry, rewrite this
file, commit and push.
