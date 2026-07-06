# Handoff — session 1 → session 2 (2026-07-06)

## Read first

1. `MILESTONES.md` — checklist + session log. Phase 0 and Phase 1 are done and
   verified; **Phase 2 (map rendering) is next and not yet started.**
2. `README.md` — build commands and tool usage.
3. `ASSETS.md` — where every asset lives.

Don't re-read the OpenRA decoder sources already ported (SHP/PAL/TMP/AUD/INI —
all in `src/formats/`, all verified working).

## Current state

- Everything compiles clean: `cmake --build build --config Release` (MSVC).
- Working decoders + debug tools: `shpview`, `tmpview`, `iniquery`, `auddump`
  (all support headless verification, see README).
- No engine/game code exists yet — only format decoders and viewers.

## Next task: Phase 2 — map rendering

Suggested order:
1. **Template list**: RA maps reference terrain templates by numeric ID. The
   ID→filename+shape mapping lives in the original source
   (`reference/CnC_Red_Alert/CODE/TDATA.CPP`, `TemplateTypeClass`) — generate a
   table from it (a Python script writing a C++ header is fine).
2. **Map loader**: RA maps = INI files (missions: `data/assets/red_alert/*/
   INSTALL/REDALERT/general/sc*.ini`; multiplayer maps `scm*.ini` — check
   `general/` and expansion MAINs). The `[MapPack]`/`[OverlayPack]` sections
   are base64 across numbered keys → concatenate → 3 LCW-compressed chunks
   (format80, same decoder as SHP — reuse `lcwDecode`, it lives in shp.cpp;
   consider extracting it to `src/formats/lcw.{h,cpp}`). 128×128 cells:
   u16 template ID + u8 icon index per cell. OpenRA's
   `ImportLegacyMapCommand.cs` + `MapImporterLegacyUtils` (search reference/OpenRA
   for "MapPack") is the working spec.
3. **Renderer**: new `mapview` tool — render full map to a surface, WASD/edge
   scroll, `--dump` for verification. Theater from map INI `[Map] Theater=`.

## Gotchas not in the docs

- Terrain objects (trees `t01`–`t17`, mines) have theater extensions
  (.tem/.sno/.int) but are **SHP format**, not TMP. `[TERRAIN]` section of maps
  places them.
- `tmpview`/`shpview` verification loop: `--dump x.bmp`, convert BMP→PNG via
  PowerShell System.Drawing, then view the PNG (Read tool renders PNGs).
- Empty TMP template slots (0xFF index) are normal, render as gaps.
- INI comments use `;` and can be mid-line; sections/keys are case-insensitive.
- CMake regenerates fine; SDL2 is vendored at `tools/SDL2` (gitignored — fresh
  clones must re-download, see README).

## Verification recipe for Phase 2

Render a known mission map (e.g. Allied mission 1 `scg01ea.ini`) with
`mapview --dump`, view the PNG, compare against the same map opened in OpenRA
or a YouTube playthrough — the shoreline/river shapes make errors obvious.

## Context handoff protocol

At ~75% context: update MILESTONES.md checkboxes + session log, rewrite this
file for the next session, commit and push.
