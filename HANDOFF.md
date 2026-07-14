# Handoff — session 13 → Track B (Phase 7 polish) (written 2026-07-14)

Session 13 landed **Track A (Phase 8 game flow)**: a main menu + post-mission
screen wrapping the shell in a game-state machine, so the game no longer freezes
on the MISSION ACCOMPLISHED/FAILED banner. Two follow-up fixes rode along: the
menu/post buttons weren't clickable (hit-rects built after event polling), and
TD wall overlays now connect via neighbor adjacency. See the Phase 8 block +
session-13 log in `MILESTONES.md` for the full detail.

**The user's next priority is the AI (see "AI campaign fidelity gap" below)** —
they noticed Nod obliterates them in GDI mission 1, and it's a real fidelity
gap, not just tuning. It was deliberately deferred to a dedicated session.

## Read first

1. `MILESTONES.md` — Phase 8 "Main menu + game flow" is now ticked; session-13
   log is the delta.
2. `docs/original-source/06-houses-ai-missions.md` — the reference for Track B
   (reinforcements/`Do_Reinforcements` §7, TeamTypes/Teams §4, triggers §4,
   AI base-building §6).
3. `README.md` / `play.bat` — how to run.

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release --target game_exe`
  (the exe is `game.exe`; `game` is the static lib). Headless sim is unchanged
  and **deterministic** (verified byte-identical this session).
- **Game flow** (all in `src/game_main.cpp`): `main()` is now a state machine —
  **Menu → Mission → Post**. The interactive SDL window/renderer/mixer/fonts/
  cursor are created **once** in `main()` and shared via the new `Shell` struct.
  - `runScenario(argc, argv, mapPath, shell) -> Outcome` — the former `main()`
    body (setup + headless block + interactive loop), verbatim except it uses
    `shell`'s shared resources and returns `Outcome{Won,Lost,Quit,ToMenu}`.
    Headless (`--sim-ticks`/`--until-win`) branches and returns inside it as
    before.
  - `runMainMenu(shell, missions, sideName) -> int` — scrollable mission list
    (= mission select), a briefing panel (from `GENERAL/mission.ini`), NEW GAME
    (returns 0) + QUIT (returns -1).
  - `runPostMission(shell, outcome, hasNext, mission) -> PostChoice` — NEXT /
    RESTART / RETURN TO MENU (NEXT only when won and a next mission exists).
  - `MenuCanvas` — fixed-logical-res, letterbox-scaled present used by both menu
    screens (mirrors the in-mission present pipeline; keeps text legible).
  - `discoverCampaign(seedIni, side)` — finds `sc<g|b><NN>…ini` beside the seed
    map, sorted; `side` from `--house` (GoodGuy→'g', BadGuy→'b'). Empty → the
    seed map becomes a one-mission "campaign".
- **In-mission**: Esc → return to menu; the win/lose banner holds ~3.5s (or any
  key/click) then advances to the post screen (via `Outcome::Won/Lost`).
- `--no-menu` and `--ui-shot` skip the menu and drop into the seed mission
  (preserves the old direct-launch + all headless/`--ui-shot` verification).

## Where the game-flow code lives

All in `src/game_main.cpp`:
- New front-end block sits just before `} // namespace` (~line 405):
  `Outcome`/`Shell`/`Mission` types, `MenuCanvas`, `menuButton`,
  `discoverCampaign`/`briefingFor`/`wrapText`, `runMainMenu`, `runPostMission`.
- `runScenario` = the old `main()` body (starts ~line 415). Key edits vs. before:
  binds `mixer`/`hudFont` from `shell` at the top; interactive branch uses
  `shell.win/renderer/cursor` and `shell.frameSurf/frameTex/resIndex/fullscreen`
  (references) instead of creating them; frees only the per-mission `mapSurf` at
  the end; returns `result`.
- The new `main()` (bottom of the file): usage, headless shortcut, one-time SDL/
  window/renderer/mixer/font/cursor setup, campaign discovery, the state machine,
  teardown.

## NEXT SESSION — AI campaign fidelity (the user's ask)

**Problem the user hit:** in GDI mission 1 (scg01ea) Nod immediately rushes and
obliterates them. Root cause is a fidelity gap, not difficulty tuning: **the
original campaign enemy is scripted and restrained; our engine runs a full,
always-on skirmish AI over the top of it.**

What scg01ea actually scripts for Nod (`[Triggers]`/`[TeamTypes]`):
- `ATK2 = Time,Create Team,0,BadGuy,NOD1` — spawn team NOD1 early, once.
- `NOD1 = BadGuy,…,E1:2, 4, Move:0, Move:1, Move:2, Attack Units:6` — **2
  minigunners** that walk waypoints 0→1→2 and only *then* attack. A patrol, not
  a rush.
- `ATK4 = Bldgs Destr.,Reinforce.,0,BadGuy,NOD3` → `NOD3 = …,BGGY:1, Attack
  Units:5` — one buggy, and only after you start destroying Nod buildings.
- Win/Lose are `All Destr.` triggers; there's a `[Base]` prebuilt list at the end.

What our engine does wrong (all in `src/game_main.cpp` + `src/game/sim.cpp`):
1. `main()`/shell enables `sim.setAI(h)` on **every** non-player combatant by
   default and grants a **5000-credit stipend** (`--ai-credits`). So Nod
   base-builds a whole army the mission never intended.
2. `Create Team`/`Reinforce.` spawns the roster near the base and **folds it into
   the generic "attack nearest enemy with everything" wave** — NOD1's
   `Move→Move→Move→Attack` script is ignored (known gap; TeamType mission list is
   parsed-past — see `map.h` TeamType comment and `sim.cpp` `spawnTeam`).

Fixes, in impact order (this is the whole session):
1. **Don't run the aggressive skirmish AI on trigger-scripted campaign houses.**
   The original only base-builds when there's a `[Base]` list + a Production/
   Autocreate trigger, and only attacks via *alerted* TeamTypes. Simplest first
   cut: if the map has Win/Lose triggers (a scripted mission), *don't* auto-enable
   `setAI` on scripted houses — let triggers/teams drive them. Keep the full
   skirmish AI only for trigger-less skirmish maps. (Decision point: confirm this
   gating with the user.)
2. **Run coordinated TeamType missions** — drive spawned teams along their parsed
   `Move:wpt / Attack / Guard / Guard Area / Loop` script (TEAM.CPP
   `Coordinate_*`) instead of the generic wave. Needs the TeamType mission list
   retained through `map.cpp` → `sim` (today `map.h TeamType` drops it; re-add a
   `missions` vector and thread it into `spawnTeam`).
3. **Difficulty tiers + AlertTime cadence + stipend** (Easy/Normal/Hard attack
   intervals per §6), and gate base-building on `[Base]` (INI.CPP:441 / BASE.CPP).

Reference: `docs/original-source/06-houses-ai-missions.md` §4 (TeamTypes/Teams,
`Coordinate_*`), §6 (HouseClass::AI, alerted attack cadence), §7 (reinforcements).
There is also a **user memory** `ai-campaign-fidelity-gap` capturing this.

## Other Track B items (after the AI, or as the user directs)

1. **Reinforcements at the map edge/shore via waypoints** (LST naval landing is
   skipped; teams spawn mid-base). `Do_Reinforcements` REINF.CPP:63 — enter from
   a waypoint / map edge and drive on.
2. **Broaden trigger coverage**: superweapons (Ion/Nuke/Airstrike), `Built It`,
   `Discovered`, `Credits`, cell triggers, trigger-chaining.
3. **AI depth**: defensive structures (gun/gtwr/obli/sam), building rebuild.

Later Phase 8: in-game OPTIONS menu (the tab is a stub), save/load; more EVA cues.

## Verification recipe

```
cmake --build build --config Release --target game_exe

# Determinism (must stay byte-identical):
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy --no-shroud --sim-ticks 600 --ai
#   run twice, sha1sum the stdout -> identical.

# Interactive game flow: just launch (menu comes up first).
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy
#   Menu -> click a mission (or NEW GAME) -> play -> win/lose banner -> Post
#   screen -> Restart / Next / Menu. Esc mid-mission returns to the menu.

# One-frame mission screenshot (skips the menu): --ui-shot out.bmp
```

Menu/post screens have no headless trigger. This session verified them with
**throwaway env-gated screenshot hooks** (`CNC_MENU_SHOT` / `CNC_POST_SHOT` that
saved one `MenuCanvas` frame to BMP) — both rendered correctly, then the hooks
were removed. Re-add the same way (a `save()` on `MenuCanvas` + an env check
after `present()`) if you need to eyeball a menu change.

## Gotchas / constraints

- **Determinism is sacred.** The headless sim (`Sim::tick`) is untouched; the
  game-flow work is all front-end. Keep it that way — re-verify byte-identical
  runs after any sim change.
- **Never commit game assets** (`data/`, `renders/` are gitignored).
- The exe target is `game_exe` (OUTPUT_NAME `game`); `game` alone builds the lib.
- `runScenario` still reads per-mission options from `argc/argv` (`--house`,
  `--credits`, `--tech`, `--no-shroud`, `--no-ai`, `--ai-credits`, `--rules`,
  `--scale`), so they apply to every mission in a campaign run.
- Campaign "next mission" just advances the sorted index — it steps through
  branch variants (scg04ea/wa/wb) linearly. Fine for now; real branching is a
  later concern.
- scg01ea AI-vs-AI (`--until-win` with the player as AI) prints "no winner": the
  GDI beach has no reachable ore so the AI stalls at 0 credits — the map, not a
  bug (documented since session 12).

## Git / housekeeping

- **All session-13 work is committed & pushed to `main`** — three commits:
  (1) Phase 8 game-flow state machine, (2) fix unclickable menu/post hit-rects,
  (3) TD wall neighbor-adjacency frames. Plus MILESTONES/HANDOFF doc updates.
- The AI fidelity gap was **deferred** (no code change) — captured here + in the
  `ai-campaign-fidelity-gap` user memory. Working tree is clean at handoff.
- `docs/REFACTOR_PLAN.md` is untracked (predates this session) — left for the
  user to commit if wanted; it documents a *future* decomposition pass and is
  unrelated to this work.
- Commit trailer in use: `Co-Authored-By: Claude ...`.

## Context handoff protocol

At ~75% context: tick `MILESTONES.md`, add a session-log entry, rewrite this
file, commit and push.
