# Handoff — session 14 → Track B continued (AI depth) (written 2026-07-14)

Session 14 landed **AI campaign fidelity** (Track B). GDI mission 1 no longer
rushes the player: the scripted Nod garrison holds position and only the scripted
2-minigunner `NOD1` patrol advances, matching the original. See the Phase 7
"AI campaign fidelity" checklist + session-14 log in `MILESTONES.md` for detail.

## Read first

1. `MILESTONES.md` — Phase 7 "AI campaign fidelity" is now ticked; session-14
   log is the delta. The Phase 7 "polish / known gaps" list is the Track B TODO.
2. `docs/original-source/06-houses-ai-missions.md` — §4 TeamTypes/Teams
   (`Coordinate_*`), §6 `HouseClass::AI` (alerted attack cadence / `AlertTime`,
   `[Base]` base-building), §7 reinforcements (`Do_Reinforcements`).
3. User memory `ai-campaign-fidelity-gap` — updated to reflect the fix.

## What session 14 changed (the AI fidelity work)

The problem was a **fidelity gap**: the original campaign enemy is scripted and
restrained, but our engine ran a full always-on skirmish AI (+5000cr stipend)
over the top of it, and the map loader dropped the two INI facts that encode the
restraint. Fixed in three parts (all deterministic, `tickCount_`-keyed):

1. **Gating** (`src/game_main.cpp`, ~line 1300): a map with any `Win`/`Lose`
   trigger is `scripted`; on those, the blanket `sim.setAI(h)` + stipend loop is
   skipped (`if (aiEnabled && !scripted)`). Enemies are driven by their unit
   orders + team scripts + triggers instead — exactly like `HouseClass::AI` with
   an un-alerted house. A `Production`/`Autocreate` trigger still calls
   `setAI(house)` via `runTriggerAction`, so a mission that *intends* an active
   AI still gets one. Trigger-less skirmish maps are unchanged (full AI on).
2. **Per-unit INI missions** honored:
   - `map.{h,cpp}`: `Object::mission` retained (field 5 of `[UNITS]`/`[INFANTRY]`;
     `parseObjects` gained a `hasMission` param — false for `[STRUCTURES]`, whose
     field 5 is a trigger).
   - `sim.h`: `Unit::Order {Guard, AreaGuard, Hunt}` + `orderCell`.
     `game_main.cpp` `orderFor()` maps the string → order.
   - `sim.cpp` `tickStandingOrders()`: `Hunt` seeks the nearest enemy;
     `AreaGuard` leashes back to spawn if pulled >3 cells; `Guard` just holds
     (auto-acquire already engages anything in range).
3. **Coordinated TeamType missions**:
   - `map.{h,cpp}`: `TeamType::missions` (`Mission:arg` list) parsed after the
     class roster and retained.
   - `sim.h`: `TeamTypeDef::Step` + a private `Team {id,house,script,step,...}`
     + `teams_`. `spawnTeam` reserves a team id, tags each spawned member's
     `teamId`, and pushes a `Team` when the script is non-empty.
   - `sim.cpp` `tickTeams()`: replays `Move`/`Move to Cell` (advance when all
     members within 2 cells; 40-tick stuck timeout), `Attack*`/`Rampage`
     (re-engage nearest enemy; `Attack Base` prefers structures), `Guard`/etc.
     (timed hold), `Loop` (restart). Runs on the ~1s cadence.

Both new passes skip player-owned and skirmish-AI-owned houses and team members,
so they only affect scripted enemies. `findNearestEnemy()` is a shared helper.

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release --target game_exe`
  (exe is `game.exe`; `game` = static lib).
- **Determinism preserved** — `--sim-ticks 900 --ai` on scg01ea is byte-identical
  across two runs (sha1 `9C500155166F5AB6C63F823BFD766576C2513748` this session).
- Verified scg01ea before/after via `--sim-ticks` probes; `--ui-shot` + the
  interactive setup path load without crashing. Game-flow (Track A, session 13)
  is untouched and still works.

## Verification recipe

```
cmake --build build --config Release --target game_exe

# Determinism (must stay byte-identical): run twice, hash stdout.
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy --no-shroud --sim-ticks 900 --ai

# Fidelity probe: dump BadGuy positions at tick 3000 — the 10 pre-placed E1
# should hold their spawn cells; only NOD1 (2 E1) advances + dies attacking.
#   (grep the "after:" lines for BadGuy; compare to the "before:" lines.)

# Interactive: just launch (menu first). Play GDI mission 1 and confirm Nod
# holds / sends a small patrol, not a base-built army.
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy
```

To eyeball a scripted enemy in motion, `--ui-shot out.bmp` renders one frame
(shrouded at frame 0 — pass `--no-shroud` to see the whole map).

## NEXT — Track B continued (as the user directs)

In rough impact order (all in `MILESTONES.md` Phase 7 "polish / known gaps"):
1. **Difficulty tiers + `AlertTime` cadence + `[Base]` gating** for *awake* AI
   (§6). Session 14 gated the AI off for scripted houses rather than tuning it;
   the next step is making the awake AI (skirmish maps + trigger-alerted campaign
   houses) build only from a parsed `[Base]` list and attack on an
   Easy/Normal/Hard `AlertTime` cadence. `[Base]` isn't parsed yet (INI.CPP:441).
2. **Reinforcements at the map edge/shore via waypoints** (LST naval landing is
   skipped; `spawnTeam` still spawns near the base). `Do_Reinforcements`
   REINF.CPP:63 — enter from a waypoint / map edge and drive on.
3. **RA-format campaign scripting** — RA triggers use a numeric event/action
   format that the current TD-shaped string checks (`Win`/`Lose`/`Create Team`/
   `Reinforce.`) don't match, so RA missions still run the old skirmish AI. If RA
   campaign fidelity matters, add an RA trigger parser path.
4. **Broaden trigger coverage**: superweapons (Ion/Nuke/Airstrike), `Built It`,
   `Discovered`, `Credits`, cell triggers, trigger-chaining.
5. **AI depth**: defensive structures (gun/gtwr/obli/sam), building rebuild.

Later Phase 8: in-game OPTIONS menu (stub), save/load; more EVA cues.

## Gotchas / constraints (unchanged)

- **Determinism is sacred.** `Sim::tick` and the new `tickStandingOrders`/
  `tickTeams` key off `tickCount_` only — no wall-clock / unseeded RNG. Re-verify
  byte-identical `--sim-ticks --ai` runs after any sim change.
- **Never commit game assets** (`data/`, `renders/` are gitignored).
- The exe target is `game_exe` (OUTPUT_NAME `game`); `game` alone builds the lib.
- `parseObjects` now takes a `hasMission` flag — pass `true` for units/infantry,
  `false` for structures (their field 5 is a trigger, not a mission).
- `Move to Cell` team steps use the raw INI cell arg without the TD 64→128
  remap (unused by scg01ea; fix if a mission needs it).
- `docs/REFACTOR_PLAN.md` is still untracked (a *future* decomposition plan,
  unrelated to this work) — left for the user to commit if wanted.

## Context handoff protocol

At ~75% context: tick `MILESTONES.md`, add a session-log entry, rewrite this
file, commit and push.
