# Handoff — session 16 → 17: StarCraft-terrain C++ port (written 2026-07-15)

Session 16 pivoted the **map editor's terrain** from C&C's hand-authored art to
**StarCraft tiles + StarCraft's faithful ISOM auto-tiling**. The risky research
is done and **validated in Python**; session 17 ports it to C++ in the engine.
C&C buildings/units/vehicles and all gameplay are untouched — this is a parallel
terrain system for the editor.

## Read first

1. User memory `starcraft-terrain-pivot` — the full decision, SC tile format
   (`cv5/vx4ex/vr4/vf4/wpe`), and the ISOM algorithm/pipeline notes. **Start here.**
2. `MILESTONES.md` Phase 11 — the checklist (what's done / next).
3. `tools/sc_tiles.py` (tile decoder) and `tools/sc_isom.py` (the ISOM port to
   translate to C++). These two ARE the spec — the C++ is a straight port.
4. Reference: MIT `TheNitesWhoSay/IsomTerrain`, file `IsomTerrain/IsomApi.h`
   (re-clone to a scratch dir if needed: `git clone --depth 1
   https://github.com/TheNitesWhoSay/IsomTerrain`). All brush tables + algorithm
   live in that one header; `sc_isom.py` already ports badlands.

## What works now (Python, validated)

- `python tools/sc_tiles.py badlands --count 256` → `renders/sc/*.png` (megatiles).
- `python tools/sc_isom.py` → `renders/sc/isom_paint_demo.png`: a blank dirt map
  with a painted high-dirt **plateau (cliffs)**, a **water lake (coastline)**, and
  a **grass field** — all auto-tiled exactly like StarEdit. This is the proof the
  approach works. Read that PNG first to see the target quality.

## SC tile format (confirmed by file sizes)

Files in `data/assets/starcraft/tileset/TileSet/<name>.{cv5,vx4ex,vr4,vf4,wpe}`
(gitignored). VR4 = 8×8 minitile, 64 B. VX4EX = megatile = 16 minitile refs, each
`u32` (bit0 = hflip, index = ref>>1), 64 B. CV5 = 52-B tile groups: `u16
terrainType, u8 build, u8 height, u16 links[L,T,R,B], u16 stackConnections[L,T,R,B],
u16 megaTileIndex[16]`. Map cell = `(group<<4)|subtile`; megatile =
`cv5[group].megaTileIndex[subtile]`. WPE = 256×4 B RGB(pad) palette. Each cv5
group is one terrain role with up to 16 random variants.

## NEXT — session 17 tasks, in order

1. **C++ SC tileset loader** (`src/formats/sc_tileset.{h,cpp}` or similar): parse
   cv5/vx4ex/vr4/wpe; expose "render megatile N → 32×32 indexed/RGB". Verify by
   dumping a tilesheet BMP and comparing to `sc_tiles.py`'s output.
2. **C++ ISOM module** (`src/game/sc_isom.{h,cpp}`): straight port of
   `tools/sc_isom.py` (which itself faithfully ports `IsomApi.h`). Keep the same
   structure: TileGroup, the 14 Shapes, Link/LinkId, per-tileset TerrainTypeInfo
   + terrainTypeMap (port badlands first, then the rest from IsomApi.h), then
   `loadIsom` (hashToGroup + generateIsomLinks + terrainTypeMap), `place`
   (brush→setDiamond→radial), `updateTiles` (diamond hash→group + stack + random
   subtile). Validate by reproducing the same paint demo headlessly.
3. **Fix junction hash-misses**: in the Python demo, painting two features
   directly adjacent leaves black tiles at incompatible-terrain corners (e.g.
   high-dirt touching water). Add test cases; likely a `generateIsomLinks` /
   hash edge case (or genuinely-invalid SC adjacency the brush should prevent).
   Chase this during/after the C++ port with more paint configurations.
4. **New parallel map format + editor paint mode**: a map that stores the ISOM
   diamond grid + tileset id (not C&C template/icon). In `mapedit`, add a mode
   that paints terrain types with a diamond brush, live-resolves via the ISOM
   module, and renders SC megatiles (sampled 32→24 px) with C&C units on top.
   Keep the C&C `TMP` path and all existing categories intact.

## Verification recipe

- Python target to match: `python tools/sc_isom.py` then Read
  `renders/sc/isom_paint_demo.png` (cliffs/coast/fields, minor black junction).
- For the C++ loader/ISOM: add a headless `--render`-style BMP dump (as mapedit
  already has for C&C terrain) and Read the PNG; it should match the Python demo.

## Gotchas / constraints

- **SC assets are personal-use only** — `data/` (and `renders/`) are gitignored;
  never commit tiles/palettes. (Memory `cnc-clone-personal-use-only`.)
- **Keep C&C intact.** The SC terrain is a *second* map format behind a flag;
  don't disturb the `TMP`/theater path, object categories, or gameplay.
- SC tiles are 32px; our cell/grid/units are 24px. Sample SC megatiles down to
  24 for display; keep the logical grid + pathfinding at 24 (don't move units).
- `.vx4ex` (not `.vx4`) in SC:R — 4 bytes per minitile ref (u32), not 2.
- Small elevated brushes collapse to background (SC min feature size); the paint
  demo uses brushExtent ≥ ~7. Recompile the whole map (`set_all_changed` +
  `updateTiles`) after a batch of paints, or call updateTiles per paint.
- The coast auto-tiler from earlier this session lives in `mapedit.cpp` (SEA
  category) — superseded but left in place; don't confuse it with the SC work.

## Context handoff protocol

At ~75% context: tick `MILESTONES.md` (Phase 11), add a session-log entry,
rewrite this file, commit and push.
