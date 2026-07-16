# Handoff — session 17 → 18: StarCraft terrain in-engine (written 2026-07-16)

Session 17 **completed the C++ port** of the StarCraft-terrain editor pivot
(Phase 11). The tileset loader, the faithful ISOM auto-tiler, the `.scm`
parallel map format, the 24px renderer, and both a headless demo and an
interactive paint mode all exist in the engine and are verified. C&C
buildings/units/vehicles, the `TMP`/theater path, and all gameplay are
untouched — this is a parallel editor terrain system.

## Read first

1. User memory `starcraft-terrain-pivot` — full decision, SC tile format, ISOM
   algorithm, **and the s17 C++ port + junction-diagnosis notes**. Start here.
2. `MILESTONES.md` Phase 11 (now ✅) + the session-17 log entry.
3. The C++ source (all new this session):
   - `src/formats/sc_tileset.{h,cpp}` — cv5/vx4ex/vr4/wpe decode + megatile render.
   - `src/game/sc_isom.{h,cpp}` — faithful ISOM port + `.scm` save/load + `repairNullTiles`.
   - `src/game/sc_render.{h,cpp}` — SC megatile 32→24px sampling into a Canvas.
   - `src/tools/sctileview.cpp`, `scisomview.cpp` — headless verify tools.
   - `mapedit --sc-demo` / `--sc` blocks in `src/tools/mapedit.cpp`.
4. Python spec (still the reference for porting the *other* tilesets):
   `tools/sc_tiles.py`, `tools/sc_isom.py`, and MIT `IsomApi.h`
   (re-clone if the scratchpad copy is gone:
   `git clone --depth 1 https://github.com/TheNitesWhoSay/IsomTerrain`).

## What works now (C++, verified)

- `sctileview data/assets/starcraft/tileset/TileSet badlands --count 256 --out X.bmp`
  → **pixel-identical** to `sc_tiles.py` (ImageChops diff empty).
- `scisomview` → `renders/sc/cpp_isom_paint_demo.bmp`: cliffs/coast/field,
  isomLinks=125 & typed-groups=1425 (match Python). `--repair` clears junctions;
  `--grass X Y` moves the field (far apart → 0 nulls, proving the diagnosis).
- `mapedit <td-root> --sc-demo` → `renders/sc/cpp_mapedit_sc_demo.bmp`: SC terrain
  at 24px with **house-colored C&C units on top**; `.scm` round-trip lossless.
- `mapedit <td-root> --sc [--width W --height H] [--shot X.bmp] [--open-scm f]
  [--out-scm f]` — interactive diamond-brush paint (1-7 terrain, `[`/`]` brush,
  WASD scroll, Ctrl+S save). `--shot` paints a scripted stroke and exits (headless).

## Junction black tiles — RESOLVED (not a bug)

Two incompatible terrains one diamond apart → ISOM rect with no matching tile
group → hash miss → tile 0 (black). The MIT reference does the same; spacing
apart gives 0 nulls. Editor fix: opt-in `ScIsomMap::repairNullTiles()` (copies a
compiled vertical neighbor) run after `updateTiles`.

## NEXT — session 18 candidates (in rough priority)

1. **Other 7 tilesets' brush tables**: only `badlandsBrush()` is ported. Port
   `terrainTypeInfo` + `terrainTypeMap` for space/installation/ashworld/jungle/
   desert/arctic/twilight from `IsomApi.h` (jungle/desert/arctic/twilight share
   Jungle's map — see the Span aliases at IsomApi.h ~964-1072). Add a
   `--sc-tileset` smoke render for each.
2. **Wire `.scm` into a real flow**: either a game load path for SC-terrain maps,
   or an editor toggle to switch the *existing* C&C level between TMP and SC
   terrain. Decide with the user — the `.scm` currently only stores terrain, not
   C&C objects/spawns, so a combined map format may be wanted.
3. **Human smoke-test** live `--sc` mouse painting (only the scripted `--shot`
   path is auto-verified; the mouse→diamond mapping and drag-paint need eyes).
4. Consider the reference's extra stack-top walk in `updateTileFromIsom`
   (IsomApi.h 2205-2218) — our port simplified it; fine for the demos but may
   matter for tall multi-level cliffs.

## Gotchas / constraints

- **SC assets personal-use only** — `data/` and `renders/` are gitignored;
  never commit tiles/palettes/`.scm`/BMPs. (Memory `cnc-clone-personal-use-only`.)
- **Keep C&C intact** — SC terrain is a *parallel* system behind `--sc`/`--sc-demo`.
- SC tiles are 32px; grid/units stay 24px (sample down for display only).
- `.vx4ex` (u32 refs), not `.vx4`, in SC:R.
- After a *batch* of `place()` calls, either recompile per-place or call
  `setAllChanged()` before `updateTiles()` — each `place()` resets the changed
  area, so one trailing `updateTiles()` compiles only the last stroke.
- `ScIsomMap` subtile choice uses a per-map xorshift RNG, so compiled *tiles*
  differ run-to-run in the random variant (not the group). `.scm` stores the
  diamond grid (deterministic), so its round-trip is byte-lossless; compare
  group ids, not full cells, if diffing compiled output.
- Build: VS 2022 generator, `cmake --build build --target <t> --config Release`.
  New CMake targets: `sctileview`, `scisomview` (+ SC sources in the libs).

## Context handoff protocol

At ~75% context: tick `MILESTONES.md` (done), add a session-log entry (done),
rewrite this file (done), commit + push (pending user OK).
