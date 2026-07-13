# Handoff — session 8 → session 9 (written 2026-07-13)

Session 8 delivered the **HUD fidelity** goal: an FNT font decoder plus a
full 1995-style HUD (metallic sidebar, radar minimap, tab bar, all text).
The Tiberian Dawn build now *looks* like the original. Sim is unchanged.

## Read first

1. `MILESTONES.md` — Phase 1 (FNT now ticked), Phase 6 (HUD-chrome note), and
   the session-8 log entry (top of the log).
2. `README.md` / `play.bat` — how to run.
3. Source, only as needed:
   - `src/formats/fnt.{h,cpp}` — the font decoder (new this session).
   - `src/game/render.{h,cpp}` — `drawText`/`textWidth` (new); Canvas/blit.
   - `src/game_main.cpp` — the shell. HUD lives in the render loop: sidebar
     chrome + radar + buttons + cameo strips, then the top tab bar; plus the
     placement label and selected-unit health %. Layout constants near the top
     (`kTopBar`/`kRadarH`/`kBtnH`/`kSideTop`) — change these and `entryPos`,
     `sidebarHit`, and the scroll clamp all follow.

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release` (MSVC, no warnings).
  `play.bat` runs GDI mission 1. Sim headless is unchanged/deterministic
  (scg01ea regression still: Nod e1 50→35, dies tick 197).
- **FNT decoder** works end-to-end: Westwood new-format fonts (FontDataBlocks
  5), 4-bit packed glyphs, per-char width/height(top-blank + data rows)/offset
  tables. `game::drawText(canvas, font, text, x, y, argb, spacing)` treats any
  non-transparent glyph pixel as the caller's color (game fonts are ~1-bit);
  `textWidth` mirrors it. Fonts load from `<root>/INSTALL/CCLOCAL/` (TD):
  `6point.fnt` (labels) + `8point.fnt` (credits). Optional — missing fonts just
  disable text, so headless/other-disc runs are unaffected.
- **HUD** (all verified via `--ui-shot` on scb03ea GoodGuy):
  - Top tab bar: `OPTIONS` button, `PWR <produced>/<drained>` (red when short),
    `$<credits>` (green, 8point), `SIDEBAR` tab.
  - Sidebar: beveled metallic panel (`bevelPanel` helper — raised/sunken),
    recessed 1px bezel per cameo, green `$<cost>` label bottom-right of each
    cameo, unaffordable cameos still darkened, building progress bar + ready
    border unchanged.
  - Radar/logo box: a **live minimap** — the baked world surface (`mc`, the
    Canvas over `mapSurf`) downscaled into the box, with 2px house-colored blips
    (friendly green / enemy red / neutral gray) and a faction-tinted frame
    (GDI gold `0xffd8b040`, Nod red `0xffd83030`).
  - REPAIR | SELL | MAP button row — **visual only** (no sell/repair sim yet).
  - Placement mode: `<NAME> $<cost>` label follows the cursor.
  - Selected units/structures: color-coded health `%` above the health bar.
- Still a sandbox: no win/lose, no AI/base-building, no mission triggers.

## Next tasks (suggested order)

1. **Wire the sidebar buttons** to sim: SELL (refund a structure, remove it),
   REPAIR (drip-heal a structure for credits), MAP (toggle a larger radar /
   jump-scroll). SELL/REPAIR need small `Sim` methods; MAP is pure UI.
2. **Win/lose + a beatable mission** (Phase 7): destroy-all-structures win /
   lose-when-you-have-none; then mission INI `[Triggers]`/`[TeamTypes]`, with a
   minimal skirmish AI (build order + attack waves).
3. **Polish**: radar shroud (dim unexplored cells in the minimap), cameo
   name/tooltip, TD tiberium density frames + real harvest density, muzzle-flash
   anims, sound pan by on-screen distance, more EVA cues.

## Gotchas discovered this session (not all in code comments)

- **FNT header** (little-endian): u16 fileLen; u8 compress(0); u8 dataBlocks(5);
  then u16 offsets at 4/6/8/10/12 = info/offset/width/data/height blocks.
  `maxHeight/maxWidth` = infoBlock[4]/[5]. **Char count = (widthBlockOff −
  offsetBlockOff)/2** (offset table is u16/char). Per char: offset table entry
  is an *absolute* file offset to the pixel data; width table is u8; height
  table is 2 bytes/char = {topBlank, dataRows}. Pixels are two-per-byte, **low
  nibble first**, `ceil(width/2)` bytes per row, `dataRows` rows. Value 0 =
  transparent, 1 = foreground (recolored), 2-15 = literal palette index (rare;
  our `drawText` treats all nonzero as the one caller color — fine for HUD).
- `6point.fnt` reports maxHeight/maxWidth 16 (generous cell), but real glyph
  data rows are ~7px — don't be alarmed; we only use per-glyph `yOffset`/`height`
  and `maxWidth` (fallback advance for missing glyphs), not maxHeight for layout.
- The minimap reuses `mc` (the persistent `Canvas` wrapping `mapSurf` from the
  bake step) — it stays valid the whole run because `mapSurf->pixels` is stable.
- Sim entities have **no `alive` flag**; dead ones are removed from the vectors.
  Blips guard on `hp > 0` anyway (harmless). `Unit::cell()` is a method;
  `Structure::cell` is a field (top-left) — watch the `()`.
- Cameos now start at `kSideTop` (below radar+buttons), not y=4. Any new sidebar
  hit-testing must use `kSideTop`, and the scroll clamp is
  `kSideTop + rows*(kCameoH+4) + 4 − winH`.
- **TD cameo `<type>icon.shp` art is 32×24** (not 64×48 like RA hires) — drawn
  with `blitIndexedScaled` to fill the 64×48 slot (2×). Don't `blitIndexed` them
  raw or they fill only the top-left quarter. Empty slots draw as recessed
  bezels so the sparse mission-start sidebar still looks framed.
- **Font naming is counter-intuitive**: `8point.fnt` maxHeight is 11 and
  `6point.fnt` is 16 — 8point is the *smaller* one and is the HUD default
  (`hudFont`). All chrome text is laid out from measured `textWidth` (spacing 0)
  so labels/buttons fit; don't hardcode box widths or you'll clip edge glyphs.

## Verification recipe

```
# HUD screenshot (populated GDI base → full sidebar):
build\Release\game.exe data\assets\tiberian_dawn\nod\GENERAL\scb03ea.ini ^
  data\assets\tiberian_dawn\nod --house GoodGuy --no-shroud --ui-shot out.bmp
# Then: python -c "from PIL import Image; Image.open('out.bmp').save('out.png')"
# Expect: OPTIONS/PWR/$credits/SIDEBAR tab bar, beveled sidebar, gold-framed
# radar minimap with green/red blips, REPAIR|SELL|MAP row, green $cost labels.
# Placement label + health % need an interactive session (click a cameo / units)
# — they share the same drawText path, so a screenshot of either confirms both.
```

## Context handoff protocol

At ~75% context: tick MILESTONES.md, add a session-log entry, rewrite this file
for the next session, commit and push.
