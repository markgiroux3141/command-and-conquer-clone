# Handoff — session 4 → session 5 (written 2026-07-07)

Session 4 completed **all of Phase 5**: weapons/warheads/damage from
rules.ini, attack orders with turret tracking and homing projectiles,
explosion/death anims via a sim event stream, attackable sim structures,
the harvester→refinery→credits loop, and the power model.

## Read first

1. `MILESTONES.md` — checkboxes + per-phase gotchas are current.
2. `README.md` — build commands, `game.exe` usage.
3. Only then source: `src/game/sim.{h,cpp}` (movement + combat + harvest),
   `src/game/rules.{h,cpp}` (stats/weapons/warheads/economy), and
   `src/game_main.cpp` (shell: draw lists, events→anims, orders). The
   formats/render/map/house code is all verified — don't re-read.

## Current state

- Compiles clean: `cmake --build build --config Release` (MSVC).
- Combat: right-click an enemy unit/structure → selected units chase into
  Range, aim (turret decoupled from hull on 1tnk/2tnk/3tnk/4tnk/jeep), fire
  on ROF, damage = Damage × Verses[Armor] (COMBAT.CPP Modify_Damage port,
  MinDamage/MaxDamage clamps). Deaths free occupancy, fire events; the
  shell plays Combat_Anim-selected SHPs (piff/veh-hit/art-exp/napalm/
  h2o_exp) and fball1 for vehicle/structure deaths.
- Economy: right-click ore with a harvester → gathers 1 bail/15 ticks up to
  BailCount (28), auto-shuttles to nearest friendly `proc`, unloads after 30
  ticks → credits[house] += bails×GoldValue (gems ×GemValue). Depleted cells
  rebake to bare terrain. Credits + power in the window title (no FNT font
  renderer yet).
- Power: `Sim::power(house)` sums structure Power= (+/-);
  `Sim::powerFraction` is the Power_Fraction port for Phase 6 production.
- Sim stays deterministic (verified: identical stdout hashes on repeated
  1400-tick combat+harvest runs). All verification headless; interactive
  play still only smoke-tested — worth a real playthrough.

## Next task: Phase 6 (production) or Phase 9 (map editor)

Phase 6 suggested order: sidebar cameo strip (SHP cameos exist in conquer,
e.g. 1tnkicon.shp) → build queue with cost/time (BuildSpeed, scaled by
powerFraction) → prerequisites (Prerequisite= in rules.ini) → structure
placement (adjacency + passability) + MCV deploy. Spending hits the credits
map that harvesting already fills.

## Gotchas not in MILESTONES.md

- Sim units die by erase-remove at end of `Sim::tick()`; anything holding a
  `Unit*`/`Structure*` across a tick must re-look-up by id (`findUnit`).
  Projectiles already handle their target dying mid-flight (they fly to the
  last known spot and detonate on the ground).
- `orderAttack` targets are (unitId, structId) with the unused one -1.
  Headless flags: `--attack i,j` `--attack-struct i,structId`
  `--harvest i,cx,cy` (unit *indices* into the initial list, struct *ids*).
- Chase repaths when the target strays >2 cells from the stored destCell;
  if A* returns empty the unit drops the target (prevents repath-spin on
  unreachable targets).
- In-range vehicles finish entering a reserved cell before stopping to
  fire (occupancy stays consistent); infantry stop instantly.
- The e2 grenade rules section is [Grenade] with Image=BOMB — projectile
  art draws by facing only if the SHP has ≥32 frames, else frame 0.
- Harvester "dock" = any cell adjacent to the proc footprint (real RA uses
  a fixed dock cell + docking animation — revisit in polish).
- bakeTerrainCell rebakes terrain only: a depleted ore cell under a tree
  shadow would lose the shadow pixels (rare; ignore until it shows).
- Water-destination regression (`--move 0,50,79` → 48,77) needs ~1200
  ticks, not 600 — the handoff recipe under-ran it this session and it
  looked like a regression for a while.

## Verification recipe

```
build\Release\game.exe data\assets\red_alert\allied\MAIN\general\scg01ea.ini ^
    data\assets\red_alert\allied --sim-ticks 1400 --no-shroud ^
    --attack 0,13 --harvest 1,76,48
```

- unit 13 (e1 at 62,55) dies ~tick 85 (4 × 15 dmg from jeep M60mg).
- "ore depleted" lines appear (~ticks 440-750), then
  `house USSR credits 700 power 700/690` (28 bails × GoldValue 25).
- `--attack-struct 3,0` → tesla hp falls 400→334 over 600 ticks (3/shot =
  15 × 25% SA-vs-heavy).
- Repeat a run → identical stdout (determinism).
- Visual: `--dump out.bmp` right after a death tick shows fball1; the
  harvested field shows bare snow where ore was.
- Interactive: select jeeps, right-click Soviet infantry → chase/kill;
  right-click ore with the harvester; watch title-bar credits climb.

## Context handoff protocol

At ~75% context: update MILESTONES.md checkboxes + session log, rewrite this
file for the next session, commit and push.
