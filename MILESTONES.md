# Milestones

Working checklist for the Red Alert clone (C++ / SDL2, MSVC + CMake).
Tick items as they land. Keep this file updated every session — it doubles as
the anchor document for context handoffs (see bottom).

## Phase 0 — Asset acquisition ✅ 2026-07-06

- [x] Download freeware TD + RA discs (7 archives, `data/downloads/`)
- [x] Unpack ISOs → `data/extracted/` (`tools/extract-assets.ps1`)
- [x] Clone reference sources: OpenRA, EA TD/RA/Remaster (`reference/`)
- [x] Python MIX extractor incl. RA encrypted headers (`tools/mix_extract.py`)
- [x] Full recursive extraction: 112 MIXes → 11,911 named files in `data/assets/`

## Phase 1 — Format decoders (`src/formats/`)

- [x] PAL palettes (2026-07-06)
- [x] SHP sprites — LCW + XOR-delta, verified vs 4tnk.shp (2026-07-06)
- [x] `shpview` debug viewer with headless `--dump` (2026-07-06)
- [x] INI parser — verified vs rules.ini [4TNK], 246 sections (2026-07-06)
- [x] TMP terrain templates — verified vs rv01.tem river (2026-07-06)
- [x] AUD audio — both IMA (99) and WW ADPCM (1) paths verified; WAVs in
      `data/wav-samples/` for listening (2026-07-06)
- [x] FNT bitmap fonts (`src/formats/fnt.{h,cpp}`) — new-format (FontDataBlocks
      5) header + 4-bit packed glyphs; drives all HUD text via `game::drawText`
      (2026-07-13, session 8)
- [ ] Later: WSA/CPS (menus, briefings), VQA video (nice-to-have)

Gotchas learned: trees/terrain objects use theater extensions (.tem/.sno) but
are SHP-format, not TMP — sniff by magic, not extension. Only 10 of 1,637 AUDs
are format 1 (all in covert_ops). rules.ini lives at
`red_alert/*/INSTALL/REDALERT/local/rules.ini`.

## Phase 2 — Map rendering

- [x] Template ID→art table generated from RA source (`tools/gen_template_table.py`
      → `src/game/template_table.h`, 401 templates incl. FIXIT_ANTS) (2026-07-06)
- [x] Map INI loader (`src/game/map.cpp`: MapPack/OverlayPack base64 → chunked
      LCW → cells; LCW extracted to `src/formats/lcw.{h,cpp}`) (2026-07-06)
- [x] Theater tile rendering — all three theaters verified (snow scg01ea/scm01ea,
      temperate scg05ea, interior scg10eb) (2026-07-06)
- [x] Overlay layer: ore/gems (density frames per Tiberium_Adjust), walls
      (adjacency frames per Wall_Update), trees/terrain objects (2026-07-06)
- [x] Scrolling camera + edge/keyboard scroll (mapview windowed mode) (2026-07-06)
- [x] Render a real skirmish map end-to-end (scm01ea 94×94) (2026-07-06)

Gotchas learned: map template IDs 0 **and 255** mean clear (CELL.CPP
Recalc_Attributes special-cases 255; OpenRA's tileset makes 255 the clear
template) — without this, snow maps are full of "missing art". Clear cells use
icon `(x&3)|((y&3)<<2)` (Clear_Icon). Template art missing from MIXes: b4/b5/b6,
p15 (cut content), hill01 (ant missions only). SHP shadow index 4 is drawn as a
50% darken in mapview for now; real shadow tables are Phase 3.

## Phase 3 — Units on screen

- [x] Sprite renderer: shadow index translucency + house-color remap
      (`src/game/render.cpp` + `house.cpp`; remaps built from PALETTE.CPS via
      new CPS decoder, exactly like Init_Color_Remaps) (2026-07-06)
- [x] Facings (32-frame rotation, BodyShape table) + turret layering
      (1tnk–4tnk/jeep frames 32–63) (2026-07-06)
- [x] Selection: click, drag-box, selection brackets, health bar (verified
      headlessly via `mapview --select`; interactive path shares the code)
      (2026-07-06)
- [x] Cursor SHPs, basic mouse interaction (mouse.shp frame 0, OS cursor
      hidden; untested beyond code review — check interactively) (2026-07-06)

Gotchas learned: infantry art lives in `INSTALL/REDALERT/hires/` (not
conquer). Civilians c3–c10 have no art — original uses C1's shapes with
per-type remap tables (we fall back to c1). War factory is two-part art
(weap.shp + weap2.shp roof). Shadow is still the 50% darken approximation,
not the original fading tables. mouse.shp is **Dune II-format** SHP
(per-frame sizes; decoder in `src/formats/shpd2.cpp`, verify with
`mapview --dump-cursor out.bmp`) — sniff SHP variants by header, extensions
lie.

## Phase 4 — Simulation core

- [x] Fixed-tick game loop (15/s), deterministic sim/render split — new
      `game.exe` (`src/game_main.cpp`); mapview stays a pure viewer (2026-07-06)
- [x] rules.ini → unit stat tables (`src/game/rules.{h,cpp}`: Strength/Speed/
      ROT/Sight, Tracked= → speed class, [Clear]/[Water]/... land percents)
      (2026-07-06)
- [x] Unit movement: land-type cell grid, 8-dir A*, per-tick rotation+motion,
      cell occupancy/reservation collision (`src/game/sim.{h,cpp}`; verified
      headlessly via `game --sim-ticks --move` on scg01ea) (2026-07-06)
- [x] Shroud: player-house explored bitmap, Sight-radius reveal from units
      (as they move) and structures (at load); unexplored cells drawn black,
      objects in them hidden. `--house H` picks the viewpoint (default
      Greece), `--no-shroud` disables. RA1-style: never regrows. (2026-07-07)

Gotchas learned: RA TMP stores a per-slot land-type byte (IControl_Type::
ColorMap at header offset 32; lookup table in CDATA.CPP Land_Type) — that plus
the rules.ini land sections gives passability with zero hardcoded terrain
tables. Speed= scales as `*256/100` → leptons/tick (TECHNO.CPP _Scale_To_256);
256 leptons per cell. Vehicles default WHEEL, `Tracked=yes` → TRACK
(UNIT.CPP). Approximations to revisit: structure/terrain-object footprints =
SHP cell bounds (real game uses BDATA/TDATA occupy lists), infantry don't
occupy cells (no infantry-vs-infantry collision), shadow still 50% darken.

## Phase 5 — Combat & economy

- [x] Weapons, projectiles, warheads, damage vs armor from rules.ini
      (`rules.{h,cpp}`: [weapon]/[warhead] sections, Verses vs Armor=,
      Modify_Damage port; sim: attack orders, chase into range, turret
      tracking, ROF cooldown, homing projectiles) (2026-07-07)
- [x] Death animations, explosion effects (sim emits Impact/UnitDied/
      StructDied events; shell maps them via Combat_Anim table to piff/
      veh-hit/art-exp/napalm/h2o_exp/fball1 SHPs; vehicle death = fball1.
      Infantry death sequences (InfDeath) still todo — they just vanish)
      (2026-07-07)
- [x] Harvester loop: ore → refinery → credits (ore bails = density frame+1,
      15 ticks/bail, BailCount cap, auto-shuttle to nearest friendly proc,
      GoldValue/GemValue credits per house; depleted cells rebaked to bare
      terrain; right-click ore = harvest, credits shown in title bar)
      (2026-07-07)
- [x] Power model (produced/drained per house from Power=, `Sim::power` +
      `powerFraction` = Power_Fraction port; the production-speed consumer
      lands with Phase 6 production) (2026-07-07)

Gotchas learned: Verses= order is none,wood,light,heavy,concrete (atoi stops
at '%'). Weapon Range= is fixed-point cells ("4.75" → 1216 leptons); weapon/
projectile Speed= scales *256/100 capped at 255 like unit speed; ROF= is
already in ticks. Modify_Damage falloff divisor is Spread*5 leptons (or 2 if
no spread; PIXEL_LEPTON_W==10) — direct hits pass distance 0. Explosion art
comes from the warhead ExplosionSet + damage via COMBAT.CPP Combat_Anim
(water cells swap in h2o_exp*). Structures are sim entities now (attackable,
footprint still SHP cell bounds). Not yet done: auto-acquire (units only
fire when ordered), defensive structures firing back, splash damage to
nearby objects, Burst=/secondary weapons, muzzle-flash Anim=, infantry
InfDeath sequences, ore re-adjacency on neighbor depletion (frames stay).
**Auto-acquire landed 2026-07-13** (Phase 10 session 7): idle armed units
scan for the nearest enemy in weapon range each tick and fire without orders
(`Sim::tickAutoAcquire`); a `Unit::autoTarget` flag holds guarding units at
their post (they drop the target when it leaves range instead of chasing,
unlike a player-ordered attack). A move order now cancels any attack.

## Phase 6 — Production

- [x] Sidebar UI (two cameo columns: structures | units, `<type>icon.shp`
      from hires; wheel scroll, click to build, click-when-ready to place,
      right-click cancels+refunds; progress bar + ready border on the cameo)
      (2026-07-08)
- [x] Build queues, prerequisites tree, cost/time (one item per category —
      Building/Infantry/Vehicle; ticks = Cost × BuildSpeed×900/1000 at full
      power; credits drip-paid with progress, stall when broke; Prerequisite=
      checked against owned structures, barr↔tent interchangeable; producing
      factory fact/barr|tent/weap is an implicit prereq; low power slows via
      powerFraction with a 16/256 floor) (2026-07-08)
- [x] Structure placement (footprint buildable+free per land Buildable=,
      must touch friendly structure) + MCV deploy (right-click own selected
      MCV → fact, footprint one cell up-left) (2026-07-08)

Gotchas learned: cameo art is `<type>icon.shp` (64×48) in **hires**, game
palette. weap's Prerequisite is proc (not just fact) — the tech chain is
fact→powr→barr/proc→weap. Sidebar candidate type lists are hardcoded (the
original defines type lists in code too). Headless `--build/--place/--deploy`
retry every tick, so chains like deploy→powr→barr resolve naturally; unit
indexes in flags refer to the initial unit list.
**Dynamic sidebar landed 2026-07-13 (session 7):** the sidebar now shows only
cameos whose prerequisites are met right now (rebuilt each frame from
`Sim::canProduce`, the shared gate `startProduction` also uses), and darkens
available-but-unaffordable cameos — like the original, not the whole tech tree
at once. Fixed TD production factories in the process: infantry spawn from
`pyle`/`hand` (not `barr`/`tent`) and vehicles from `weap` **or** `afld` (TD
Nod builds vehicles at the Airstrip) — `isBarracks`/`isWarFactory` groups in
`prereqsMet` + `spawnProduced`. Verified via `--ui-shot` (GoodGuy base in
scb03ea shows structures + infantry, no vehicles without a war factory) and
headless (e1 builds from pyle).
Not yet done: batch/queued items per category, ship production (syrd/spen),
buildup anims (`<type>make.shp`), bib drawing under buildings, per-cell
placement validity coloring, silo storage caps, selling/repair.
**HUD chrome landed 2026-07-13 (session 8):** the FNT decoder (Phase 1) now
drives all sidebar/HUD text. The sidebar is a beveled metallic panel with
recessed cameo bezels, per-cameo green `$cost` labels, a live radar minimap
(downscaled baked world + house-colored unit/structure blips, faction-tinted
frame — GDI gold / Nod red), and a REPAIR|SELL|MAP button row (visual only —
sell/repair sim is still TODO). Top bar = OPTIONS | PWR produced/drained |
`$credits` (green) | SIDEBAR. Placement shows a "`<NAME> $cost`" cursor label;
selected units/structures show a color-coded health %. Layout constants
(`kTopBar`/`kRadarH`/`kBtnH`/`kSideTop`) keep `entryPos`/`sidebarHit`/scroll in
sync. Fonts load from `<root>/INSTALL/CCLOCAL/{6point,8point}.fnt` (optional —
HUD degrades to no-text if absent).

## Phase 7 — AI & missions

- [ ] Skirmish AI: base building + attack waves (reference: original TeamTypes)
- [ ] Mission scripting: triggers, teamtypes, reinforcements
- [ ] Playable campaign mission 1 (Allied)

## Phase 8 — Audio & polish

- [x] **SFX wired to sim events** (2026-07-13, session 7): new SDL mixer
      (`src/game/audio.{h,cpp}`) decodes AUDs → device-rate mono s16, resamples,
      and mixes N one-shot effects + one music track on the audio callback
      thread (device-lock guarded; silent + no-op if the device won't open, so
      headless runs are unaffected). A new sim `Event::Fire` (weapon report) +
      `Impact`/`UnitDied`/`StructDied` drive SFX in `processEvents`. Fire sounds
      are data-driven: `Report=` per weapon in `td_rules.ini` → `WeaponStats`.
- [x] **Music jukebox** (scores): the render loop starts the next SCORES track
      when the current one ends (playlist aoi/ccthang/ind/ind2/fwp/heavyg).
- [x] **EVA + unit acknowledgements** (2026-07-13, session 7): a single speech
      channel (newest-wins, no overlap) plays unit voice responses on
      select/move/attack (`.v00-.v03` variation files cycled for variety:
      report1/vehic1, movout1, affirm1) and EVA computer lines on build events
      (bldging1 on start, constru1/unitredy on the ready edge, cancel1, deploy1).
      EVA speech ships only on the Covert Ops disc (`covert_ops/AUD1/SPEECH`,
      like the cameos). Verified: voice + EVA AUDs decode through the shared
      `fmt::AudFile` path; interactive launch stable. **Phase 8 audio complete.**
- [ ] Sound fade/pan by on-screen distance (Sound_Effect pans by cell); more
      EVA cues (low power, base under attack, insufficient funds, new options).
- [ ] Main menu, in-game options, save/load

## Phase 9 — Map editor (side quest; can start any time after Phase 4)

Maps are pure data (template/icon grid + overlay grid + object lists), so an
editor is mapview plus mouse input and a writer. Custom maps already load —
skirmish maps are just INI files in `general/`.

- [ ] LCW encoder + MapPack/OverlayPack writer (naive literal-run encoding is
      valid LCW — ~20 lines; round-trip test: load → save → load → compare)
- [ ] Template stamp palette UI (browse/place templates, eyedropper from map)
- [ ] Overlay + terrain-object placement (ore brush, walls, trees)
- [ ] Playable-bounds editing, [Waypoints] (start positions), map INI metadata
- [ ] Smart terrain brushes (auto-pick shore/cliff transition pieces) — v2

Out of scope for now: multiplayer, VQA-driven campaign FMVs.

## Phase 10 — Tiberian Dawn pivot 🚧 (started 2026-07-13)

Goal: retarget the engine from Red Alert to the original 1995 **Tiberian Dawn**
aesthetic (per user: "use this version instead"). Full pivot — RA-specific paths
may bit-rot. TD assets were already extracted in Phase 0 (`data/assets/
tiberian_dawn/{gdi,nod,covert_ops}`). Formats mostly shared with RA; the
divergences are the work.

- [x] TD template table generator (`tools/gen_td_template_table.py` →
      `src/game/td_template_table.h`, 216 templates from TD DEFINES.H/CDATA.CPP,
      cross-checked vs OpenRA cnc tilesets) (2026-07-13)
- [x] TD `.bin` map loader (`map.cpp` `loadTd`): 64×64 grid, 2 bytes/cell
      (template,icon), template 0xff=clear; [OVERLAY]/[TERRAIN]/object sections;
      loaded into top-left of the 128-grid so sim/render stay unchanged.
      Auto-detected in `MapFile::load` (no [MapPack] + sibling .bin) (2026-07-13)
- [x] TD TMP decoder branch (`tmp.cpp`): magic 0x0d1affff, imgStart@12,
      indexEnd@24, indexStart@28, no land-type map (2026-07-13)
- [x] TD theater plumbing: theaters DESERT/TEMPERATE/WINTER, exts .des/.tem/.win,
      dirs TEMPERAT/DESERT/WINTER, palette per theater; `mapview` game-aware data
      layout (root/<THEATER>, root/CONQUER) + template table + TD overlay pass
      (2026-07-13)
- [x] **Verified: full missions render in all three theaters** — scg01ea
      (temperate landing beach), scb03ea (Nod desert base: Temple/Hand/refinery/
      airstrip/obelisks + village), scg08ea (winter, frozen rivers + bridges).
      100% template/art pass, zero missing art (2026-07-13)
- [x] TD house-color remap (`house.cpp` `tdRemap`/`tdHouseIndex`): TD art carries
      the player color in a placeholder band at palette indices 176-191; each
      house remaps that band to the exact targets from CONST.CPP
      (RemapYellow/Red/BlueGreen/Orange/Green/Blue). GoodGuy=gold, BadGuy=red
      (source default is Blue — we use Red per the GDI-gold/Nod-red convention),
      multi houses = distinct colors. Wired into `mapview` remapFor (cached by
      house). Verified: scb03ea shows gold/red/blue unit groups (2026-07-13)
- [x] Local render tool `tools/render_td.py` → `renders/*.png` (gitignored;
      derived from copyrighted art). Default set = one mission per theater
      (2026-07-13)
- [x] TD unit/structure stats: authored `td_rules.ini` (committed) in the RA
      rules.ini format the loader already reads — land movement sections,
      warheads (SA/HE/AP/Fire/Laser), weapons, and the core TD roster
      (vehicles/infantry/structures) with classic TD stats. game.exe loads it
      for TD (`--rules` overrides) (2026-07-13)
- [x] **`game.exe` TD support + PLAYABLE** (2026-07-13): mirrored the mapview
      pivot (TD data layout, td template table, TD overlay pass, tdRemap,
      td_rules.ini, TD build lists, GoodGuy/BadGuy side). Sidebar cameos come
      from the Covert Ops CONQUER (`root/../covert_ops/CONQUER` — base discs omit
      `<type>icon.shp`). Verified headless on scg01ea: MCV pathfinds/moves, GDI
      e1 chases + shoots Nod e1 (50→35hp, M16 SA), gun turrets are attackable
      sim structures; --ui-shot shows the desert Nod base + populated sidebar.
- [x] **TD passability** (2026-07-13, session 7): the template table now carries
      per-template `land` + a per-icon `altLand`/`altIcons` exception list
      (`gen_td_template_table.py` parses ctor args 5/8 + the `_slope*` lists;
      TD `LandType` 0-6 == `game::Land` 0-6, Tiberium→Ore). `bakeTerrainCell`
      sets per-cell land from it for TD (mirrors RA's control-map path;
      CELL.CPP `Land_Type()` alt-icon logic). Verified: MCV/units ordered onto
      water stop at the shore (clamp to nearest land), cross-map land paths
      still work. (Gunboat still immobile — naval needs a contiguous water
      region; its start cell sits in a tiny inlet. Pre-existing, not regressed.)
- [ ] Polish/known gaps (not blocking play):
      - TD tiberium overlay draws frame 0 only (no density/adjacency frames);
        harvest uses a flat BailCount.
      - td_rules.ini is a compact hand-port; refine values / add missing
        types (aircraft, gunboat, more structures) as needed.
      - TD production build-chain not yet play-tested (sidebar populates; sim
        production is the shared RA path).
      - TD `mouse.shp` isn't ShpD2 format → OS cursor fallback. Structures show
        little house color (TD art is mostly pre-colored; only the 176-191 band
        remaps — expected).
      - mapview still uses `root/CONQUER` only (no covert_ops icon fallback);
        fine since it doesn't draw cameos.

Gotchas learned: TD template IDs are single bytes (RA=u16); 0xff=clear (TD has
no icon-index quirk like RA's 0/255). TD maps are 64-wide — object/overlay cell
numbers use that width, remapped to the engine's 128 grid on load. TD theater
dir is truncated `TEMPERAT` but palette base is `temperat`. TD TMP has no land
control map, so sim passability must come from the template table, not the tile.
House names: GoodGuy=GDI, BadGuy=Nod, Neutral=civilian.

---

## Context handoff protocol

We work until the session's context is nearly full, then hand off to a fresh
session. **Claude: track this and warn unprompted when usage feels ~75%+.**

When handing off, write `HANDOFF.md` containing:

1. **Read first:** `MILESTONES.md` (this file), `README.md`, `ASSETS.md` — then
   only the source files relevant to the current task.
2. **Current task:** what's in progress, exact state (what compiles, what's
   verified, what's broken), and the very next action.
3. **Gotchas discovered this session** that aren't yet written into code
   comments or docs (e.g. format quirks, path oddities).
4. **Verification recipe:** how to prove the current feature works
   (usually: build + `shpview --dump`-style headless render + Read the PNG).

Keep handoffs short — the docs carry the durable knowledge; the handoff only
carries the delta. Update this file's checkboxes *before* writing a handoff.

### Session log

- **2026-07-06 (session 1):** Phase 0 complete. Phase 1 complete (PAL, SHP,
  INI, TMP, AUD + shpview/tmpview/iniquery/auddump tools, all verified).
  Context ~70% at end of session; next session starts Phase 2 (map rendering).
  Handoff written (`HANDOFF.md`), repo pushed to GitHub
  (markgiroux3141/command-and-conquer-clone).
- **2026-07-06 (session 2):** Phase 2 complete. Template table generator,
  LCW extracted to own module, map loader (game lib), mapview tool with
  terrain/overlay/terrain-object layers + scrolling. Verified by rendering
  missions in all three theaters and a 94×94 multiplayer map; renders match
  expected RA look (shorelines/cliffs/bridges/ore all coherent). Phase 9
  (map editor) added to plan. **Phase 3 also complete** (same session): CPS
  decoder, house remaps from PALETTE.CPS, render module, map object parsing,
  mapview draws structures/units/infantry with facings + turrets + selection
  UI + cursor. Verified on scg01ea (Soviet base red, Greece jeeps blue,
  war factory roof, brackets/health bars via --select). Next: Phase 4
  (simulation core — game loop, rules.ini stats, movement/pathfinding).
- **2026-07-06 (session 3):** Phase 4 mostly complete (shroud remains).
  TMP decoder now reads the land-type control map; new rules loader
  (stats + land speed percents), sim core (A*, occupancy, per-tick
  rotation/movement in leptons), and `game.exe` with a fixed 15/s tick loop,
  right-click move orders and headless `--sim-ticks/--move` flags. Verified
  on scg01ea: jeep paths around cliffs/water to the exact ordered cell,
  water destinations clamp to nearest reachable land, three jeeps ordered to
  one cell settle on adjacent cells, infantry walk with sub-cell offsets;
  interactive window smoke-tested. Shroud landed same session (dated
  2026-07-07): sight circles at start, corridor revealed along a jeep's
  route, hidden Soviet base until scouted — **Phase 4 complete.** Next:
  Phase 5 (combat & economy) or Phase 9 (map editor).
- **2026-07-07 (session 4): Phase 5 complete.** Weapons/warheads/armor from
  rules.ini, attack orders (chase, turret tracking, ROF, homing projectiles,
  Modify_Damage port), sim events → explosion/death anims, attackable sim
  structures, harvester loop with per-house credits and ore depletion
  (rebaked cells), power model + powerFraction. Verified headlessly on
  scg01ea (`--attack`, `--attack-struct`, `--harvest`): jeep kills e1 in 4
  shots, tesla takes exactly 3/shot (Verses 25% vs heavy), grenadier kills
  jeep → fball1 renders, harvester round trip = 700 credits (28×25),
  determinism confirmed (identical run hashes). Movement regressions pass
  (55,75 exact; water order settles 48,77). Next: Phase 6 (production) or
  Phase 9 (map editor); infantry death anims + auto-acquire are known gaps.
- **2026-07-13 (session 6): Phase 10 (Tiberian Dawn pivot) — PLAYABLE.**
  User wants the original 1995 TD aesthetic instead of RA ("use this version
  instead"). TD assets were already extracted (Phase 0). Built: TD template
  table generator (`gen_td_template_table.py` → `td_template_table.h`), TD `.bin`
  map loader (`map.cpp loadTd`, 64×64 into the 128-grid), TD TMP decoder branch
  (`tmp.cpp`, magic 0x0d1affff), theater plumbing (desert/temperate/winter),
  TD house-color remap from CONST.CPP bands (`house.cpp tdRemap`, GDI gold/Nod
  red), authored `td_rules.ini` (TD ships none), and full `game.exe` TD support
  (data layout, td template table, TD overlay, build lists, Covert-Ops cameo
  fallback). `tools/render_td.py` writes gitignored `renders/*.png`. Verified:
  all 3 theaters render; scg01ea plays headless (MCV pathfinds, GDI e1 shoots
  Nod e1 50→35hp); interactive window + populated sidebar via --ui-shot; the
  user confirmed it runs. `play.bat` now launches TD (auto-detects disc).
  Sandbox-level: no enemy AI/return-fire, no win/lose, no audio, passability
  defaults to Clear. Next: enemy auto-acquire, TD passability, audio (Phase 8).
- **2026-07-13 (session 7): auto-acquire + TD passability + audio.** Three
  gameplay gaps closed. (1) **Auto-acquire/return-fire** (`Sim::tickAutoAcquire`,
  `Unit::autoTarget`): idle armed units lock onto the nearest enemy in weapon
  range and fire without orders; guarding units hold their post (drop the target
  when it leaves range) while player-ordered attacks still chase; a move order
  cancels the attack. Verified headless both ways (idle Nod mob kills a moving
  GDI e1; a GDI e1 that goes idle in range shoots Nod e1 50→35). (2) **TD
  passability**: the template table now carries per-template `land` + per-icon
  `altLand`/`altIcons` (generator parses ctor args 5/8 + `_slope*` lists);
  `bakeTerrainCell` sets per-cell land for TD. Verified: MCV/units ordered onto
  water clamp to the shore; land paths still cross the map. (3) **Audio (Phase 8
  complete)**: SDL mixer + combat SFX + score jukebox, plus EVA computer lines
  and unit voice acknowledgements on select/move/attack/build (see Phase 8).
  Built clean, headless sim unchanged/deterministic, interactive smoke-launch
  runs without crashing, all AUD types decode — by-ear check pending on a real
  run. Then, on user request ("get the game looking like the original"),
  reworked the **sidebar to be dynamic**: shows only cameos whose prerequisites
  are currently met (via new `Sim::canProduce`) and darkens unaffordable ones,
  instead of listing the whole tech tree at once. Fixed TD production factories
  found along the way (infantry ← pyle/hand, vehicles ← weap/afld). Verified
  with `--ui-shot`. Remaining for full UI fidelity: sidebar frame art + an FNT
  font for text. Known gaps: gunboat immobile (naval), no win/lose/AI (Phase 7).
- **2026-07-13 (session 8): HUD fidelity — "look like the original."** Built
  the **FNT font decoder** (`src/formats/fnt.{h,cpp}`, the Phase 1 linchpin):
  Westwood new-format fonts (header block offsets per TXTPRNT.ASM, 4-bit packed
  glyphs, per-char width/height/offset tables), plus `game::drawText`/`textWidth`
  in `render.{h,cpp}` (non-transparent glyph pixel → caller color; the game
  fonts are effectively 1-bit). Verified end-to-end by rendering — text is crisp
  in every HUD element. Then rebuilt the HUD to match the 1995 game: beveled
  metallic sidebar with recessed cameo bezels + green `$cost` labels, a **live
  radar minimap** (downscaled baked world + house-colored blips, faction-tinted
  frame), a REPAIR|SELL|MAP button row (visual), and a top tab bar
  (OPTIONS | PWR | `$credits` | SIDEBAR). Placement shows a `<NAME> $cost` cursor
  label; selected objects show a health %. New layout constants keep the cameo
  hit-testing/scroll in sync with the shifted-down strips. Fonts load from
  `<root>/INSTALL/CCLOCAL`. Built clean (no warnings), sim unchanged/deterministic
  (scg01ea regression: Nod e1 50→35, died tick 197). Verified the sidebar/tab/
  radar/cost via `--ui-shot` on scb03ea GoodGuy; placement label + health % are
  verified-by-construction (share the tested `drawText`, no headless trigger).
  Remaining UI polish: functional sell/repair, real radar shroud, cameo name
  tooltips.
- **2026-07-08 (session 5): Phase 6 complete.** Production stats in rules
  (Cost/TechLevel/Owner/Prerequisite/BuildSpeed/land Buildable=), sim
  production slots with drip payment + power scaling, prereq tree,
  unit spawn at factory, canPlace/placeBuilding adjacency, deployMcv;
  shell sidebar (cameo strips, progress bars, place-on-ready, cancel),
  placement ghost, right-click MCV deploy, `--credits/--tech/--build/
  --place/--deploy/--ui-shot` flags. Verified headlessly on scu04eb
  (USSR): MCV→fact at tick 0, powr done tick 216 (=300×0.72 exactly),
  full chain fact→powr→barr→proc→weap gates correctly (weap needs proc),
  e1 and 3tnk spawn below their factories, credits 10000→4350 exact,
  power 100/80; sidebar screenshot eyeballed (correct Soviet cameo set).
  Next: Phase 7 (AI & missions) or Phase 9 (map editor).
