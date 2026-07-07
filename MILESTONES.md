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
- [ ] Later: WSA/CPS (menus, briefings), FNT fonts, VQA video (nice-to-have)

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
- [ ] Fog of war / shroud

Gotchas learned: RA TMP stores a per-slot land-type byte (IControl_Type::
ColorMap at header offset 32; lookup table in CDATA.CPP Land_Type) — that plus
the rules.ini land sections gives passability with zero hardcoded terrain
tables. Speed= scales as `*256/100` → leptons/tick (TECHNO.CPP _Scale_To_256);
256 leptons per cell. Vehicles default WHEEL, `Tracked=yes` → TRACK
(UNIT.CPP). Approximations to revisit: structure/terrain-object footprints =
SHP cell bounds (real game uses BDATA/TDATA occupy lists), infantry don't
occupy cells (no infantry-vs-infantry collision), shadow still 50% darken.

## Phase 5 — Combat & economy

- [ ] Weapons, projectiles, warheads, damage vs armor from rules.ini
- [ ] Death animations, explosion effects
- [ ] Harvester loop: ore → refinery → credits
- [ ] Power model (production speed penalty)

## Phase 6 — Production

- [ ] Sidebar UI (build strip, tabs, cameos)
- [ ] Build queues, prerequisites tree, cost/time
- [ ] Structure placement (adjacency rules) + MCV deploy

## Phase 7 — AI & missions

- [ ] Skirmish AI: base building + attack waves (reference: original TeamTypes)
- [ ] Mission scripting: triggers, teamtypes, reinforcements
- [ ] Playable campaign mission 1 (Allied)

## Phase 8 — Audio & polish

- [ ] SFX + EVA voice playback wired to events
- [ ] Music jukebox (scores)
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

Out of scope for now: multiplayer, VQA-driven campaign FMVs, TD support (later).

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
  interactive window smoke-tested.
