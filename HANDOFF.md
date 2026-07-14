# Handoff — session 15 → Track B continued (AI depth) (written 2026-07-14)

Session 15 landed **awake-AI structure** (Track B item #1): the AI that runs on
skirmish maps and on campaign houses woken by a Production/Autocreate trigger now
builds a parsed `[Base]` list in order and attacks on a difficulty-driven
`AlertTime` cadence, instead of a hardcoded tech-chain + fixed timer. Shipped,
verified, and (about to be) pushed to main.

## Read first

1. `MILESTONES.md` — Phase 7 "Awake-AI structure — [Base] + difficulty
   AlertTime" is the new ticked item; the session-15 log is the delta.
2. `docs/original-source/06-houses-ai-missions.md` — §6 `HouseClass::AI`
   (`AlertTime` cadence, `Suggest_New_Object` / `Base.Next_Buildable`), §5
   FactoryClass, §7 reinforcements (`Do_Reinforcements` — next task).
3. User memory `ai-campaign-fidelity-gap` — updated with this session's work.

## What session 15 changed (all deterministic, `tickCount_`-keyed)

Decisions confirmed with the user before building: default difficulty **Normal**,
a global **`--difficulty`** flag, **hybrid** base-building.

1. **`[Base]` parsed + threaded** (`map.{h,cpp}`): `parseScripting` reads the
   `[Base]` section (`Count=N`, then `NNN=TYPE,COORD` in index/build order). The
   COORD is a packed lepton coordinate — decoded with `Coord_Cell` (FUNCTION.H):
   `cell_x=(c>>8)&0xff`, `cell_y=(c>>24)&0xff`, engine cell `= cell_y*128+cell_x`
   (correct for both TD 64-wide and RA). Stored in `MapFile::base`
   (`{type, cell}` in build order). `game_main.cpp` hands it to the house
   *opposite* the player (`baseHouse`, per BASE.CPP) via `Sim::setBaseList`.
2. **Next_Buildable base-building** (`sim.{h,cpp}`): `Sim::aiNextBaseNode(house)`
   returns the first list node the house owns fewer of than the list requires up
   to that point (robust to exact placement). `tickAI` step 3 is now **hybrid**:
   if a `[Base]` list exists, build/rebuild it in order (placing each at its
   scripted cell when free, else near the base); with no list, fall back to the
   old MCV-deploy + power→proc→barracks→factory chain (our `scm*` skirmish maps
   ship `[Base] Count=0`, so they use the fallback and stay playable).
3. **Difficulty + AlertTime** (`sim.{h,cpp}`, `game_main.cpp`): `Sim::Difficulty
   {Easy,Normal,Hard}` + `setDifficulty`, set from `--difficulty` (default
   Normal). `Sim::alertTime(house)` replaces the fixed 450 attack timer with the
   per-tier range (Easy 16-40 min, Normal 5-20, Hard 4-10 — expressed in AI-think
   ticks, since `tickAI` runs ~1/s), drawn by a deterministic FNV hash of
   house+`tickCount_` (no RNG). The cooldown is **seeded once at wake** so the
   first wave waits out a full cadence rather than rushing. Per-difficulty
   starting stipend (Easy 3000 / Normal 5000 / Hard 8000; `--ai-credits` still
   overrides).

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release --target game_exe`.
- **Determinism preserved.** scg03ea `--sim-ticks 8000 --ai --difficulty hard`
  is byte-identical across two runs; scg01ea (no woken AI) is byte-identical to
  the *committed* baseline (proof: stashed my diff, rebuilt, same hash).
- **Baseline note:** the old handoff's scg01ea sha1 `9C500155…` is **stale** —
  the committed HEAD now hashes `09041a48f364d64c7163461c8116d4850429fe67`
  (drift predates this session; my diff does not touch the scg01ea path). Use
  `09041a48…` as the new reference, or just check "twice = identical".

## Verification recipe

```
cmake --build build --config Release --target game_exe

# Determinism (run twice, hash stdout — must match, not necessarily the old sha):
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg03ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy --no-shroud --sim-ticks 8000 ^
  --ai --difficulty hard

# Fidelity: on scg03ea the woken Nod (Production trigger @ tick 270) should keep
# all 15 prebuilt structures (intact [Base] => Next_Buildable builds nothing),
# train from its Hand of Nod, and launch a wave ~tick 6300 (hard). Grep the
# "after:" lines for BadGuy: structs unchanged, some units tagged (attacking).
# Try --difficulty normal to see the wave land much later (~tick 17550).
```

To re-run the throwaway lib test (it was removed): re-create
`src/tools/basetest.cpp` (a `game`-linked exe) covering `[Base]` parse,
`aiNextBaseNode` ordering, `alertTime` cadence-by-difficulty, and the no-`[Base]`
fallback — add a temp `basetest` target to `CMakeLists.txt`, build, run, remove.

## NEXT — Track B continued (as the user directs)

In `MILESTONES.md` Phase 7 "polish / known gaps", rough impact order:
1. **Reinforcements at the map edge/shore via waypoints** (`Do_Reinforcements`
   REINF.CPP:63). Today `spawnTeam` spawns near the base; the original enters
   from a waypoint / map edge (or by LST naval / A-plane) and drives on.
2. **RA-format campaign scripting** — RA's numeric trigger event/action format
   isn't matched by the TD-shaped string checks, so RA missions still run the
   old skirmish AI. Add an RA trigger parser path if RA fidelity matters.
3. **Broaden trigger coverage**: superweapons (Ion/Nuke/Airstrike), `Built It`,
   `Discovered`, `Credits`, cell triggers, trigger-chaining.
4. **AI depth**: *defensive*-structure building (gun/gtwr/obli/sam) beyond what
   `[Base]` scripts.

Later Phase 8: in-game OPTIONS menu (stub), save/load; more EVA cues.

## Gotchas / constraints (unchanged + new)

- **Determinism is sacred.** `alertTime` uses a deterministic FNV hash of
  house+`tickCount_` — no wall-clock / unseeded RNG. Re-verify byte-identical
  `--sim-ticks --ai` runs after any sim change.
- **`tickAI` runs ~once per 15 ticks** (once/sec), so `aiAttackCd_` counts
  *seconds* (AI-think units), not raw ticks — that's why `alertTime` ranges are
  in think-units. (The old `kAttackPeriod=450` comment claiming "~30s" was
  wrong; it was really 450 s.)
- **`[Base]` COORD is a lepton coordinate**, not a cell — decode it, don't feed
  it to `cellFn`. `Move to Cell` team steps still use the raw INI cell (unrelated
  TODO).
- The scg03ea `[Base]` is the enemy's *prebuilt* base, so on a normal run the AI
  rebuilds only what combat destroys. To see it build from scratch, use a map
  where the woken house has a ConYard but a gap in its base.
- **Do not commit the user's pending map-editor work.** The tree also carries
  unrelated uncommitted changes (`CMakeLists.txt`, `td_template_table.h`,
  `tools/gen_td_template_table.py`, untracked `src/tools/mapconv.cpp`,
  `mapedit.cpp`, `docs/REFACTOR_PLAN.md`) — the Phase 9 side-quest. Stage only
  the AI files (`src/game/map.{h,cpp}`, `src/game/sim.{h,cpp}`,
  `src/game_main.cpp`) + the two docs.
- Exe target is `game_exe` (`OUTPUT_NAME game`); `game` alone builds the lib.

## Context handoff protocol

At ~75% context: tick `MILESTONES.md`, add a session-log entry, rewrite this
file, commit and push.
