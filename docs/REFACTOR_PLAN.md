# Refactor Plan — clean, modular, scalable

Written 2026-07-14. **Not yet executed** — this is the plan for a future
*dedicated refactor pass* (feature work paused; tests as the safety net).
Scope chosen: **pragmatic decomposition** — break up the monolith, keep the
current single-binary SDL architecture (no engine/game rewrite, no ECS).

Line references point at the code as it stood when this was written; treat them
as approximate once work begins.

## Assessment: what's already good

Most of the codebase is in good shape — this scopes the work down to essentially
one file plus one cross-cutting theme.

- **`src/formats/`** — clean, single-responsibility format decoders (SHP, TMP,
  AUD, INI, LCW…). No changes needed.
- **The `game/` headers** — `sim.h`, `rules.h`, `map.h`, `audio.h` are
  well-designed: clear responsibilities, documented invariants, good
  encapsulation. Keep as the model for new modules.
- **`sim.cpp`** (~1426 lines) is large but *cohesive*: one class, well-named
  methods, a deterministic contract. A "split for navigability" candidate, not a
  structural problem.
- **`map.cpp` / `rules.cpp`** — tidy; already use templates (`parseObjects`,
  `parseScripting`) to share RA/TD logic.

## The problem

Almost entirely **one file**: `src/game_main.cpp` — ~2415 lines, a single
`main()` of ~2000 lines (`game_main.cpp:407`). It does everything: CLI parsing,
asset-path resolution, art caching, terrain baking, the map→sim bridge, the
headless test harness, window/render setup, the main loop, input handling, and
all HUD/sidebar/cursor drawing.

**Second theme (cross-cutting):** Red Alert vs Tiberian Dawn divergence is
handled by ~40 scattered `if (isTd)` branches, plus game *data* hardcoded into
the shell:
- type lists `game_main.cpp:210-227`
- `displayName` table `game_main.cpp:231-270`
- `infAnim` `game_main.cpp:898-907`
- `hasTurretArt` / `isShipType` / `isSovietHouse`, cursor tables, score lists.

Adding a third game — or even one unit — means editing the monolith in a dozen
places.

## Target module layout

```
src/
  game/
    ... (sim, map, rules, render, house, audio — mostly unchanged)
    profile.{h,cpp}       NEW  GameProfile: everything that differs RA <-> TD
    registry.{h,cpp}      NEW  type lists, display names, anim/turret data (data-driven)
    art_cache.{h,cpp}     from game_main.cpp:100-145
    world_loader.{h,cpp}  from game_main.cpp: terrain bake, overlays, map->sim bridge
  shell/
    app.{h,cpp}           NEW  owns Sim + loaded world; the run() entry
    headless.{h,cpp}      the --sim-ticks / --until-win harness (game_main.cpp:1109-1316)
    renderer.{h,cpp}      DrawObject build + drawObject + effects + shroud
    hud.{h,cpp}           sidebar, radar, power bar, top bar, tooltips, banner
    input.{h,cpp}         event->command translation, selection, cursor context
    cli.{h,cpp}           arg parsing (replaces intArg/strArg/flagArg)
  game_main.cpp           thin: parse args -> App::run()  (~50 lines)
```

## The refactors, by priority

### 1. Extract a `GameProfile` for RA-vs-TD divergence — *do this first*
One struct/interface capturing every RA↔TD difference, resolved once at startup,
so nothing downstream branches on `isTd`:
- asset dir layout (`game_main.cpp:432-451`), theater dir/ext/palette names
  (`game_main.cpp:85-98`, today split between the shell and `MapFile`),
- remap strategy (TD in-code band vs RA `PALETTE.CPS`) (`game_main.cpp:483-494`),
- template-table selection (`game_main.cpp:496-500`),
- build-list roster, side resolution, score list, cursor/font source dirs.

Turns `if (isTd) {A} else {B}` scattered across 2000 lines into
`profile.theaterDir()`, `profile.remapFor(house)`, etc. **Biggest single win for
"scalable."** Everything else leans on it.

### 2. Make the type system data-driven via a `registry`
Move `kStructTypes`/`kInfTypes`/`kVehTypes` (+TD variants), `displayName`,
`infAnim`, `hasTurretArt`, `isShipType` out of the shell into a per-game unit
registry — ideally seeded from the rules INI / a small data file rather than C++
arrays. Goal: adding a unit = a data entry, not a monolith edit. Complements #1.

### 3. Decompose `main()` into `App` + modules
Sequence so each step compiles and the game still runs:
1. **`cli`** — replace the three `argc/argv` scanners (`game_main.cpp:64-83`)
   with one parsed options struct.
2. **`ArtCache`** — lift verbatim (`game_main.cpp:100-145`); already self-contained.
3. **`WorldLoader`** — terrain bake (`:543-591`), overlay/wall/ore setup
   (`:604-703`), structure/unit loading (`:758-834`), footprint + scripting
   bridge (`:836-891`). Produces a populated `Sim` + a `DrawObject` list.
4. **`headless`** — the whole `--sim-ticks` block (`:1109-1316`).
5. **`renderer`** — `DrawObject`, `drawObject` (`:326-373`), `buildDrawList`
   (`:969-1001`), effects/shroud.
6. **`hud`** — sidebar strips, radar, power bar, top bar, tooltips, win banner
   (`:2019-2366`).
7. **`input`** — the event switch (`:1578-1859`) and cursor-context logic
   (`:2272-2353`).

### 4. Unify the headless and interactive command paths
The headless harness re-implements move/attack/harvest/build/place/deploy
(`:1125-1241`) that the interactive loop also implements (`:1628-1832`). Route
both through one command layer (`issueMove`, `issueAttack`, `deploy`,
`startBuild`, `place`…). Payoff: headless mode becomes a genuine scripting/test
API over the same code the UI drives — which is exactly what verification already
uses (see `HANDOFF.md`).

### 5. Split `sim.cpp` internally — *lower priority (navigability, not structure)*
Only if it keeps growing. Natural seams by section: `sim_movement.cpp`
(pathfinding/`orderMove`/`tickUnit`), `sim_combat.cpp`
(`tickCombat`/`tickProjectiles`/`impact`/`kill*`), `sim_economy.cpp`
(harvest/production), `sim_ai.cpp` (`tickAI`/build-spot), `sim_scripting.cpp`
(triggers/teams). Keep one `Sim` class; split the translation units only.
**Do not** touch the deterministic tick ordering — documented invariant
(`HANDOFF.md`: "Determinism is sacred").

### 6. Introduce a real test harness
No automated tests today; verification is manual CLI probes (`HANDOFF.md`). Once
#4 lands, the deterministic sim + unified command layer make golden-output tests
cheap: script orders, run N ticks, assert on unit/credit/winner state. Lock in
determinism *before* the big extraction so refactors are provably
behavior-preserving.

## Phasing (dedicated pass)

| Phase | Work | Risk | Why here |
|---|---|---|---|
| **0** | Determinism/golden-output test (a few scenarios) | none | Safety net before moving code |
| **1** | `cli` + `ArtCache` extraction | low | Warm-up; isolated pieces |
| **2** | `GameProfile` (#1) | med | Unblocks removing `isTd` everywhere |
| **3** | `registry` (#2) | med | Depends on the profile |
| **4** | `WorldLoader` + `headless` | med | Biggest line-count removal from `main()` |
| **5** | `renderer` + `hud` + `input` + `App` | high | Interactive core; do last, tests green |
| **6** | Unify command paths (#4); optional `sim.cpp` split (#5) | med | Cleanup once modules exist |

Net: `game_main.cpp` goes from ~2400 lines to ~50; `main()` becomes "parse CLI,
build `GameProfile`, load world, `App::run()`". Each game-specific difference
lives in one place, and adding content becomes data, not code.

## Guardrails during the pass

- **Determinism is sacred.** AI + triggers key off `tickCount_` only; no
  wall-clock, no unseeded RNG in `tick()`. Verify byte-identical headless runs
  before and after each phase.
- **Behavior-preserving only.** This pass moves code; it does not change gameplay,
  rendering output, or the CLI surface. Any behavior change is a separate commit.
- **CMake:** each new module is added to the `game` static lib (or a new `shell`
  lib) as it's extracted; keep the tool executables (`shpview`, `mapview`, …)
  linking cleanly at every step.
- Commit per phase (or per module within a phase) so regressions bisect cleanly.
