# Handoff — session 2 → session 3 (2026-07-06)

## Read first

1. `MILESTONES.md` — Phase 0–2 done and verified; **Phase 3 (units on screen)
   is next and not yet started.**
2. `README.md` — build commands and tool usage (incl. new `mapview`).
3. `ASSETS.md` — where every asset lives.

Don't re-read decoder sources (`src/formats/` — PAL/SHP/TMP/AUD/INI/LCW, all
verified) or the map loader (`src/game/map.cpp`, verified in 3 theaters).

## Current state

- Everything compiles clean: `cmake --build build --config Release` (MSVC).
- `mapview <map.ini> <data-root> [--dump out.bmp] [--full]` renders any RA map
  (terrain + overlays + terrain objects) and scrolls (WASD/edges) in a window.
- `src/game/` is the new engine library (currently: map loader + generated
  `template_table.h`; regenerate with `python tools/gen_template_table.py`).
- No simulation/game loop exists yet.

## Next task: Phase 3 — units on screen

Suggested order:
1. **Sprite renderer** (`src/game/` renderer module, not a throwaway tool):
   palettized blit with per-pixel effects — shadow index 4 via the theater
   shadow table (mapview currently fakes it with 50% darken; the real fading
   tables can be computed like OpenRA does or read from the palette), and
   house-color remap of palette indices 80–95.
2. **Facings**: unit SHPs pack 32 hull facings (+32 turret frames for tanks,
   e.g. `4tnk.shp`). Map a 0–255 facing to frame index; render turret as second
   layer at same cell.
3. **Selection UI**: click hit-test, drag box, corner brackets, health bar.
   Cursor SHPs are in `data/assets/red_alert/*/MAIN/conquer/mouse.shp`.
4. Extend mapview (or start `game.exe`) to place a few units from the map INI
   `[UNITS]`/`[STRUCTURES]` sections — format: see original `INI.CPP` or any
   mission file (`iniquery` helps).

## Gotchas not in the docs

- Map template IDs **0 and 255 both mean clear** (CELL.CPP Recalc_Attributes);
  handled in `map.cpp`. Clear icon = `(x&3)|((y&3)<<2)`.
- Overlay frame selection: ore = `_adj[]` table by 8-neighbor resource count,
  gems capped at frame 2, walls = N|E<<1|S<<2|W<<3 bitmask of same-type
  neighbors (see mapview.cpp, ported from CELL.CPP).
- b4/b5/b6/p15 template art doesn't exist in any MIX (cut content); hill01 only
  in expansion MIXes. Loader tolerates missing art.
- BMP→PNG for viewing: PowerShell System.Drawing snippet in README workflow;
  Read tool renders PNGs. `*.bmp` is gitignored.

## Verification recipe for Phase 3

Render a mission map with a known starting unit layout (scg01ea has Allied
units at bottom) via the extended viewer `--dump`, view the PNG: units should
sit on correct cells, facings visibly distinct, house colors correct (Allies
blue/gold, Soviets red), shadows translucent not green.

## Context handoff protocol

At ~75% context: update MILESTONES.md checkboxes + session log, rewrite this
file for the next session, commit and push.
