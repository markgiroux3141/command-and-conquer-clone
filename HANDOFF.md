# Handoff — session 10 → session 11 (written 2026-07-14)

Session 10 was a feedback-driven polish pass on the TD shell: native-resolution
rendering, the original left-click control scheme, crisp HUD text, contextual
mouse cursors, and several UI fixes. All interactive bits were confirmed by the
user.

## Read first

1. `MILESTONES.md` — session-10 log entry (top of the log) + Phase 10 notes.
2. `README.md` / `play.bat` — how to run.
3. `docs/original-source/README.md` — breadcrumbs into the original C&C source
   (`reference/CnC_Tiberian_Dawn/`); doc 07 covers the UI/input we mirror.
4. Source, only as needed:
   - `src/game_main.cpp` — the shell. Interactive render loop: logical frame
     sizing (top of `while (!quit)`), `toLogical` mouse mapping, the input
     handler (left = command, right = deselect), contextual cursor pick + draw,
     sidebar chrome, present step.
   - `src/game/render.cpp` — `drawText` 4-tone glyph shading.
   - `src/formats/shpd2.cpp` — `mouse.shp` decoder (no shadow remap).

## Current state (what compiles / plays)

- Builds clean (`cmake --build build --config Release`). Sim headless
  deterministic. `play.bat` runs GDI mission 1.
- **Render**: fixed internal resolution (height presets 360→900, width follows
  display aspect), scaled to fill the display via `SDL_Renderer` + a letterbox
  rect. Consistent zoom windowed or fullscreen; fullscreen scales the native
  ~640×360 frame up (matches the original's field of view). **`+`/`-`** cycle
  resolution (default 360 = native). **F11 / Alt+Enter** = borderless fullscreen.
  Mouse coords are DPI-correct (`toLogical` measures output/window ratio).
- **Controls (original C&C scheme)**: **left-click commands** — select a unit,
  move to terrain, attack an enemy, deploy a selected MCV, place a building;
  **left-drag** = selection box. **Right-click deselects** (and cancels
  placement / sell-repair mode / sidebar production). The contextual cursor
  shows what a left-click will do.
- **Cursors**: TD `mouse.shp` loads from `INSTALL/CCLOCAL`; frames per
  `MOUSE.CPP`. Contextual: arrow, 8 scroll arrows (+ dimmed "can't scroll"),
  move (green), no-move (red), attack (red), deploy (green), select brackets,
  sell/repair (+ their no-* variants), animated, correct hotspots. RA still uses
  the plain arrow (different frame layout — a TODO).
- **HUD**: crisp 4-tone FNT text; sidebar cameo **hover tooltip** (name +
  `$cost`, no per-cell price); **SIDEBAR tab** slides the sidebar in/out
  (`bleep2`); sidebar **scroll arrows** on cameo overflow; **"new construction
  options"** EVA when buildables grow; tight infantry **selection boxes** +
  health bars (no `100%` text).
- **Edge-scroll** fires at the true window edges (incl. over sidebar/tab bar).
- Still a sandbox: no win/lose, no AI/base-building, no mission triggers.

## Next tasks (suggested — pick one)

1. **Rest of UI wiring**: OPTIONS menu (pause/quit/volume), MAP button
   (jump/enlarge radar). Optionally swap the hand-drawn tab bar / power bar for
   `tabs.shp` / `power.shp` art.
2. **RA contextual cursors**: port RA's `MouseControl[]` frame map (its
   `mouse.shp` is a different 222-frame layout) so RA gets the same cursors.
3. **Win/lose + a beatable mission** (Phase 7) + a minimal skirmish AI.
4. **Polish**: TD tiberium density frames, muzzle-flash anims, sound pan;
   refine `td_rules.ini` values; group-select hotkeys / control groups.

## Gotchas discovered this session (not in code comments)

- **`mouse.shp` path**: TD ships it in `INSTALL/CCLOCAL` (covert-ops:
  `INSTALL/LOCAL`), **not** `CONQUER`. It *is* ShpD2 format (byte-identical
  frame data to RA's). The old "isn't ShpD2" note was wrong — it was just the
  wrong path.
- **Cursor colors** come straight from the palette: move/deploy green = indices
  3/4, attack red = index 6, no-move red = 121-127. The ShpD2 decoder's Dune II
  shadow remap (indices 1-4 → 0x7c-0x7f) corrupted the greens; removed it (the
  only consumer is `mouse.shp`, which uses 1-4 as literal colors).
- **FNT glyphs are 4-tone** (0 transparent, 1 stroke, 2 body, 3 outline) — don't
  paint them all one color or letters bloat and merge.
- **`mapview` has its own stale `drawObject`** (duplicate of game_main's) — its
  selection boxes are the *old* full-frame style. Verify selection UI with
  `game --select --dump`, not mapview.
- **Zoom = fixed logical resolution**, NOT a divisor of the display. `logicalH`
  is a preset; `logicalW = lh * outW/outH`. Present scales it to fill the output.
- Original control scheme is authoritative: `DISPLAY.CPP`
  `TacticalClass::Action` — `LEFTRELEASE`→`Mouse_Left_Release` (command),
  `RIGHTPRESS`→`Mouse_Right_Press` (cancel/deselect).

## Verification recipe

```
# HUD screenshot (saves the logical frame — its size shows the render res):
build\Release\game.exe data\assets\tiberian_dawn\nod\GENERAL\scb03ea.ini ^
  data\assets\tiberian_dawn\nod --house GoodGuy --no-shroud --ui-shot out.bmp
# Selection UI (tight infantry boxes + health bars):
build\Release\game.exe <map.ini> <root> --no-shroud --sim-ticks 0 --select --dump sel.bmp
# Cursor frame grid (verify mouse.shp decode + frame map):
build\Release\mapview.exe <map.ini> <root> --dump-cursor cur.bmp ^
  --cursor-path <root>\INSTALL\CCLOCAL\mouse.shp
# python -c "from PIL import Image; Image.open('out.bmp').save('out.png')"
# Interactive only: left-click commands, right-click deselect, SIDEBAR toggle,
# +/- zoom, F11 fullscreen, screen-edge scroll (all four edges).
```

## Context handoff protocol

At ~75% context: tick MILESTONES.md, add a session-log entry, rewrite this file
for the next session, commit and push.
