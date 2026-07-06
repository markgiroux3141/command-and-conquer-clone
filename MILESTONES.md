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

- [ ] Map INI/MPR loader (RA maps: [Map] section, packed MapPack/OverlayPack base64+LCW)
- [ ] Theater tile rendering (temperate first), full map draw
- [ ] Overlay layer: ore/gems, walls, trees (terrain objects)
- [ ] Scrolling camera + edge/keyboard scroll
- [ ] Render a real skirmish map end-to-end

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
