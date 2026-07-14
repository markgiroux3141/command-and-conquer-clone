# Handoff — session 9 → session 10 (written 2026-07-14)

Session 9 turned the static HUD into a working, authentic one: fullscreen +
dynamic layout, a power bar, radar (shroud + faction medallion), functional
Sell/Repair, and real DOS sidebar art (medallion, strip slots, scroll arrows).

## Read first

1. `MILESTONES.md` — session-9 log entry (top of the log) + Phase 6/8 notes.
2. `README.md` / `play.bat` — how to run.
3. Source, only as needed:
   - `src/game_main.cpp` — the shell. The whole HUD is in the interactive render
     loop: layout is re-derived each frame from the live window size (top of the
     `while (!quit)` loop), then sidebar chrome (radar/medallion, power bar,
     buttons, strip slots, cameos, scroll arrows) and the top tab bar. Input for
     buttons/arrows/sell/repair is in the `SDL_MOUSEBUTTONDOWN LEFT` handler.
   - `src/game/sim.{h,cpp}` — new `structureAt` / `sellStructure` / `toggleRepair`
     + a repair pass in `tick()`.
   - `src/formats/fnt.{h,cpp}` + `render.{h,cpp}` (`drawText`, `blitIndexedScaled`).

## Current state (what compiles / plays)

- Builds clean (no warnings). Sim headless deterministic (scg01ea: Nod e1
  50→35, dies tick 197). `play.bat` runs GDI mission 1.
- **Display**: window is resizable; **F11 / Alt+Enter** toggle borderless
  fullscreen. Layout re-derives from the live window size each frame — a bigger
  window shows *more map*, the sidebar snaps to the right, camera clamps are
  guarded for window>map, and the tactical area is cleared black when larger
  than the map. **Screen-edge scroll** fires only inside the tactical viewport.
  KNOWN LIMITATION: fullscreen only *enlarges the window* (no scaling), so a
  small map looks tiny in a big black field — next-task #1 fixes this with a
  scaled logical-resolution render.
- **Building buildups**: placing a structure / deploying the MCV plays the
  20-frame `<type>make.shp` "rising" animation (~1.3 s) + a `constru2` sound,
  then swaps to the real building. Transient `Buildup` overlay in the shell; the
  sim structure is live immediately. Interactive only (headless is instant).
- **Sidebar** (all verified via `--ui-shot`):
  - Radar box: the faction **medallion** (GDI eagle / Nod viper — real
    `radar.gdi`/`radar.nod` art) until the house owns a Comm Center (`hq`/`eye`),
    then the live **minimap** (respects shroud: unexplored black, blips hidden).
  - **Vertical power bar** in the left gutter: green fill = output, yellow tick
    = demand, red on deficit.
  - **REPAIR | SELL | MAP** buttons: REPAIR/SELL toggle a click-mode (button
    highlights); then left-click one of your structures to repair (heals + drains
    credits over ticks) or sell (refunds half cost, removes it). ESC cancels.
    **MAP is still a stub.**
  - Cameo slots use the real `strip.shp` metallic texture; cameos (32×24 art)
    are 2×-scaled to fill, with green `$cost` labels. **Scroll arrows**
    (`stripup`/`stripdn`) appear at the bottom-right on overflow; click = 1 row.
  - Top tab bar: `OPTIONS | PWR n/n | $credits | SIDEBAR`.
- Still a sandbox: no win/lose, no AI/base-building, no mission triggers.

## Next tasks (suggested order) — user-requested, top priority

1. **Fullscreen should scale, not just enlarge.** Right now fullscreen only
   makes the window desktop-sized, so a small map shows a tiny image in a sea of
   black (see below). Switch to a **fixed internal render resolution scaled to
   the display**: render the whole frame into an offscreen surface at a logical
   size, upload to an `SDL_Texture`, and present via `SDL_Renderer` with
   `SDL_RenderSetLogicalSize` (letterbox auto-scale). Then convert mouse coords
   with `SDL_RenderWindowToLogical` so clicks/edge-scroll still line up. This is
   a contained render-pipeline refactor of the interactive loop (all the HUD
   drawing stays; only the final blit-to-window changes).
2. **"New construction options" EVA.** Play EVA `newopt1` ("New construction
   options") when the buildable set grows — i.e. when a just-finished building
   unlocks new cameos. Track the previous `canProduce` type set each frame (or
   on the buildup-complete edge) and play the cue when a new type appears.
   NOTE: EVA speech is Covert-Ops-only (`covert_ops/AUD1/SPEECH`); confirm
   `newopt1.aud` is present (degrade silent if not).
3. **Proper mouse cursors.** TD `mouse.shp` isn't ShpD2 (we fall back to the OS
   cursor). Decode its real format and pick the frame by context: normal arrow,
   move (green), no-go (red), attack (crosshair/red), deploy (MCV), sell
   (wrench/$), repair, guard, and the 8 scroll arrows at screen edges. Wire it
   to the current mode/hover (placement, sell/repair mode, ordering units).
4. **Rest of UI wiring**: OPTIONS menu (pause/quit/volume), MAP button
   (jump/enlarge radar). Optionally swap the hand-drawn tab bar / power-bar for
   `tabs.shp` / `power.shp` art.
5. **Win/lose + a beatable mission** (Phase 7) + a minimal skirmish AI.
6. **Polish**: TD tiberium density frames, muzzle-flash anims, sound pan.

## Gotchas discovered this session (not in code comments)

- **`radar.gdi`/`radar.nod`** are 80×69 SHPs with **43 frames** (a power-up
  animation). Frame 0 is the faction medallion (radar-off state); the last
  frames go black (radar-on hole). Load by full path (`.gdi`/`.nod` extension —
  `ArtCache::shp` won't find them). They live in `covert_ops/CONQUER` = the TD
  `hiresDir`.
- **Radar building** = `hq` (Comm Center) or `eye` (Adv. Comm) per TD
  `HOUSE.CPP` `STRUCTF_RADAR|STRUCTF_EYE`. That's the logo↔minimap gate.
- **Layout is dynamic now**: `viewW`/`winW`/`winH` are *reassigned* each frame
  (the lambdas `entryPos`/`sidebarHit` capture them by ref — don't redeclare
  them inside the loop). `sideBot`, `btnRect[]`, `arrowUp/Dn` are recomputed at
  the loop top so the input handler and renderer agree.
- **Fullscreen path (window>map) isn't exercised by `--ui-shot`** (it uses the
  default window size). The black-clear + clamp guards cover it, but eyeball
  fullscreen once interactively.
- **Sell/Repair click-flow is verified-by-construction** — the sim methods are
  simple and mirror `killStructure`/credit code, but there's no headless
  trigger. Drive it interactively: SELL a powr (credits jump ~½ cost, building
  vanishes); damage a building then REPAIR (health climbs, credits tick down).
- `sellStructure` calls `killStructure`, which pushes a `StructDied` event — but
  it's called during input handling, so the next `tick()` clears it before
  `processEvents` sees it (no explosion for a sold building; fine).

## Verification recipe

```
# HUD screenshot (GDI base, no Comm Center → eagle medallion + strip slots):
build\Release\game.exe data\assets\tiberian_dawn\nod\GENERAL\scb03ea.ini ^
  data\assets\tiberian_dawn\nod --house GoodGuy --no-shroud --ui-shot out.bmp
# python -c "from PIL import Image; Image.open('out.bmp').save('out.png')"
# Interactive checks (no headless path): F11 fullscreen, screen-edge scroll,
# SELL/REPAIR a structure, scroll arrows on a base with >8 buildable rows.
```

## Context handoff protocol

At ~75% context: tick MILESTONES.md, add a session-log entry, rewrite this file
for the next session, commit and push.
