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

- [ ] Sprite renderer: shadow index translucency + house-color remap (indices 80–95)
- [ ] Facings (32-frame rotation) + turret layering
- [ ] Selection: click, drag-box, selection brackets, health bar
- [ ] Cursor SHPs, basic mouse interaction

## Phase 4 — Simulation core

- [ ] Fixed-tick game loop, deterministic sim/render split
- [ ] rules.ini → unit/structure stat tables
- [ ] Unit movement: cell grid, A* pathfinding, collision
- [ ] Fog of war / shroud

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
  expected RA look (shorelines/cliffs/bridges/ore all coherent). Next: Phase 3
  (units on screen — sprite renderer with shadow tables + house-color remap).
