# Handoff — session 5 → session 6 (written 2026-07-08)

Session 5 completed **all of Phase 6** (production): sidebar with real
cameos, build queues with drip-paid credits and prerequisite gating,
structure placement with adjacency, and MCV deploy. Phases 0-6 are done.

## Read first

1. `MILESTONES.md` — checkboxes + per-phase gotchas are current.
2. `README.md` — build commands, `game.exe` usage.
3. Only then source: `src/game/sim.{h,cpp}` (movement/combat/harvest/
   production), `src/game/rules.{h,cpp}`, `src/game_main.cpp` (shell:
   sidebar, placement, orders, effects). formats/render/map/house are all
   verified — don't re-read.

## Current state

- Compiles clean: `cmake --build build --config Release` (MSVC).
- Interactive: sidebar on the right (structures | units cameo columns,
  wheel-scrolls). Click a cameo to build (needs fact/barr|tent/weap);
  yellow progress bar on the cameo; buildings get a green border when
  ready — click the cameo again to enter placement (footprint ghost,
  green/red), right-click in the world cancels placement, right-click the
  cameo cancels production with refund. Units auto-spawn below their
  factory. Right-click an own selected MCV to deploy into a fact.
  `--credits N` (default 3000) seeds the player house.
- Sim owns all of it deterministically: one production slot per category
  (Building/Infantry/Vehicle) per house; ticks = Cost × BuildSpeed(0.8) ×
  900/1000 at full power; credits paid as progress advances (stalls when
  broke, cancel refunds); low power scales speed by powerFraction with a
  16/256 floor. Prereqs checked against owned structures (barr↔tent
  equivalent); weap's prereq is proc.
- Not yet played eyes-on beyond a one-frame `--ui-shot` screenshot —
  a real interactive playthrough (build a base, make tanks, fight) is the
  best first move next session.

## Next task: Phase 7 (AI & missions) or Phase 9 (map editor)

Phase 7 order suggestion: unit auto-acquire (fire at enemies in range
without orders — combat already supports targets, just needs a scan) →
skirmish AI (build order from the production API + attack waves) →
triggers/teamtypes for missions. Auto-acquire is also the quickest win for
making fights feel real.

## Gotchas not in MILESTONES.md

- `--build/--place/--deploy` (headless) retry every tick until accepted, so
  order flags freely: `--deploy 7 --build b,powr --place 35,89` works even
  though the fact doesn't exist at tick 0. `--place` args are consumed FIFO
  as buildings become ready. Unit indexes = initial unit list order.
- `Sim::deployMcv` and unit death **erase** from `units_` — any `Unit*`
  held across such calls dangles (copy house/cell first; two call sites in
  game_main already learned this).
- `startProduction` rejects when: slot busy, cost<=0, missing factory,
  missing prereq. It does NOT reserve credits — payment is per-progress in
  `tickProduction`.
- The sidebar build lists (`kStructTypes/kInfTypes/kVehTypes` in
  game_main.cpp) are hardcoded candidates filtered by rules Owner/TechLevel
  and icon art existence. Add types there if something seems missing.
- Placement validity = every footprint cell `Buildable=` land, unblocked,
  unoccupied + touching a friendly structure (Adjacent=1 for everything;
  real RA walls use Adjacent=8).
- fact footprint comes from fact.shp cell bounds (3×3 on snow); deploy
  places its top-left one cell up-left of the MCV's cell.
- `--ui-shot out.bmp` opens the real window for one frame and saves it —
  the only current way to screenshot the sidebar.
- Verification runs from session 4 (combat/harvest) now print an extra
  `house Greece credits 3000` line because of the --credits default; the
  determinism-hash recipe still works within a version.

## Verification recipe

Full production chain (scu04eb is a Soviet mission with a starting MCV,
unit index 7):

```
build\Release\game.exe data\assets\red_alert\allied\MAIN\general\scu04eb.ini ^
  data\assets\red_alert\allied --house USSR --no-shroud --sim-ticks 5000 ^
  --deploy 7 --credits 10000 --build b,powr --build b,barr --build b,proc ^
  --build b,weap --build i,e1 --build v,3tnk ^
  --place 35,89 --place 35,92 --place 29,89 --place 29,92
```

Expect: deploy tick 0; powr placed tick 216 (300×0.72); barr 433; proc
1874; weap 3315 (2000-cost = 1440 ticks each); "started building e1" right
after barr and a USSR e1 appears near 36,94; 3tnk starts after weap and
spawns near 30,94; final line `house USSR credits 4350 power 100/80`.
Combat/harvest recipes: see session-4 notes in MILESTONES + `--attack`,
`--harvest` flags (jeep kills e1 in 4 shots; harvest round trip = 700).

## Context handoff protocol

At ~75% context: update MILESTONES.md checkboxes + session log, rewrite this
file for the next session, commit and push.
