# Original Source — Application Lifecycle & Main Game Loop

> Breadcrumbs into `reference/CnC_Tiberian_Dawn/` (EA GPL release; same as
> `D:\Command and Conquer Original source code\CnC_Tiberian_Dawn`). Files are
> UPPERCASE; paths repo-relative. Every claim cites `FILE:line`.

## Control-flow summary (the one thing to remember)

```
WinMain                     STARTUP.CPP:120   ← program entry (Win32)
  └─ Main_Game(argc,argv)   STARTUP.CPP:552 → CONQUER.CPP:138
       ├─ Init_Game()       CONQUER.CPP:145 → INIT.CPP:110   (one-time init)
       └─ while Select_Game(fade)                CONQUER.CPP:157   (outer: menu → scenario)
            └─ for(;;) { Main_Loop() }           CONQUER.CPP:255,261  (per-scenario frame loop)
                 └─ Main_Loop()                  CONQUER.CPP:1458  (ONE game frame)
       └─ Uninit_Game()     CONQUER.CPP:404 → INIT.CPP:658
  └─ WM_DESTROY → Prog_End()  WINSTUB.CPP:268 → STARTUP.CPP:632   (shutdown)
```

The clone's equivalent is the fixed-tick loop in
[src/game_main.cpp](../../src/game_main.cpp) — this file explains what the
original loop does each frame so we can mirror ordering (input → logic → queue →
callback → win/lose → frame++).

## Program entry & shutdown — [STARTUP.CPP](../../reference/CnC_Tiberian_Dawn/STARTUP.CPP)

Owns the Win32 entry point and library bring-up/tear-down.

- `WinMain()` — **STARTUP.CPP:120**, the true entry point.
  - Single-instance guard (STARTUP.CPP:145), RAM check (STARTUP.CPP:161).
  - Manually tokenizes `command_line` into DOS-style `argc/argv` (STARTUP.CPP:209-231).
  - `Parse_Command_Line` (STARTUP.CPP:267); creates the 60 Hz `WinTimerClass` (STARTUP.CPP:270).
  - Reads `CONQUER.INI` via `Read_Private_Config_Struct`/`Read_Setup_Options` (STARTUP.CPP:345-350).
  - `Create_Main_Window` (STARTUP.CPP:354), `Audio_Init` (STARTUP.CPP:358),
    `Set_Video_Mode` 640×400/480 (STARTUP.CPP:367-388).
  - Sets up DirectDraw `VisiblePage`/`HiddenPage`, `SeenBuff`/`HidPage`
    viewports (STARTUP.CPP:392-464), creates `WWMouseClass` (STARTUP.CPP:488).
  - **Calls `Main_Game(argc, argv)` at STARTUP.CPP:552.**
  - On return: clears pages, `ReadyToQuit=1`, posts `WM_DESTROY`, pumps
    `Keyboard::Check()` until `ReadyToQuit != 1` (STARTUP.CPP:561-567).
- `Prog_End()` — **STARTUP.CPP:632**, graceful teardown (`Sound_End`, delete
  mouse/timer/palette). Called from the `WM_DESTROY` handler, not WinMain.
- `Read_Setup_Options()` STARTUP.CPP:727; `Delete_Swap_Files()` STARTUP.CPP:678;
  `Print_Error_End_Exit`/`Print_Error_Exit` STARTUP.CPP:690/700.

## Windows plumbing — [WINSTUB.CPP](../../reference/CnC_Tiberian_Dawn/WINSTUB.CPP)

- `Windows_Procedure()` — **WINSTUB.CPP:231**, the `WndProc`. Routes input to
  `Kbd.Message_Handler` (WINSTUB.CPP:262); `WM_DESTROY`→`Prog_End()`
  (WINSTUB.CPP:268) then surface release + `Reset_Video_Mode` + `PostQuitMessage`;
  `WM_ACTIVATEAPP` sets `GameInFocus` + `Focus_Loss()` (WINSTUB.CPP:305-309).
- `Create_Main_Window()` — WINSTUB.CPP:388 (registers the "Command & Conquer"
  window class).
- `Focus_Loss()` WINSTUB.CPP:143 / `Check_For_Focus_Loss()` WINSTUB.CPP:183 —
  pause/mute on alt-tab; the latter is called at the top of every `Main_Loop`.

## One-time initialization — [INIT.CPP](../../reference/CnC_Tiberian_Dawn/INIT.CPP)

- `Init_Game()` — **INIT.CPP:110**. Allocates all object heaps via `Set_Heap`
  (Units/Infantry/Buildings/Aircraft/Anims/Bullets/Triggers/Teams/Houses,
  INIT.CPP:121-149), clears waypoints (INIT.CPP:155), sets up keyboard, allocates
  shape staging buffer (`Set_Shape_Buffer`, INIT.CPP:174), registers `.MIX`
  archives (`CCLOCAL.MIX`, `UPDATE.MIX`…, INIT.CPP:187-193), loads fonts
  (INIT.CPP:203-217) and palettes (INIT.CPP:218-227). Returns `bool` success.
- `Uninit_Game()` — INIT.CPP:658, the matching teardown (CONQUER.CPP:404).

## Outer game loop & per-frame loop — [CONQUER.CPP](../../reference/CnC_Tiberian_Dawn/CONQUER.CPP)

The heart of the subsystem (the biggest gameplay file).

- `Main_Game()` — **CONQUER.CPP:138**. Top-level orchestrator.
  - `Init_Game()` once (CONQUER.CPP:145).
  - **Outer loop:** `while (Select_Game(fade))` (CONQUER.CPP:157) — returns to
    the menu between scenarios; sets `InMainLoop=true` (CONQUER.CPP:191).
  - **Inner per-frame loop:** `for(;;) { … if (Main_Loop()) break; }`
    (CONQUER.CPP:255-263).
  - Deferred modal dialogs handled *outside* the frame loop via `SpecialDialog`
    (CONQUER.CPP:270-302).
  - Scenario exit → fade, tear down modem/network (CONQUER.CPP:335-348),
    `Uninit_Game()` (CONQUER.CPP:404).
- `Main_Loop()` — **CONQUER.CPP:1458**, executes exactly ONE game frame. Order:
  1. `Check_For_Focus_Loss()` (1475), `Reallocate_Big_Shape_Buffer()` (1480).
  2. Set `FrameTimer` to `Options.GameSpeed` / multiplayer rate (1521-1526).
  3. Input + render: `Map.Input()` → `Keyboard_Process()` → `Map.Render()` (1535-1540).
  4. `Do_Record_Playback()` if recording (1549).
  5. `Map.Layer[LAYER_GROUND].Sort()` (1559) — outside redraw checks for net sync.
  6. **`Logic.AI()`** (1566) — all game-object simulation (see LOGIC.CPP below).
  7. **`Queue_AI()`** (1590) — dispatch queued player/network commands.
  8. **`Call_Back()`** (1599) — music + network servicing.
  9. Win/lose/restart → `Do_Win`/`Do_Lose`/`Do_Restart` (1614-1649; impls in
     [SCENARIO.CPP](../../reference/CnC_Tiberian_Dawn/SCENARIO.CPP):322/544).
  10. `Frame++` (1655), then `Sync_Delay()` (1683).
  11. Returns `!GameActive` — `true` ends the scenario (1685).
- `Sync_Delay()` — CONQUER.CPP:1412, burns the remaining frame-time budget while
  still cycling colors + servicing input/render/music (1419-1436).
- `Call_Back()` — CONQUER.CPP:1117, per-frame background work: `Theme.AI()` +
  `Speak_AI()` (1130-1132) and IPX/Internet packet servicing (1139+).
- `Color_Cycle()` CONQUER.CPP:1032; `Keyboard_Process()` CONQUER.CPP:428
  (key/hotkey → game actions).

## Simulation tick — [LOGIC.CPP](../../reference/CnC_Tiberian_Dawn/LOGIC.CPP) / LOGIC.H

`Logic` is a `LogicClass` (a layer/list of `ObjectClass*`), driven once per frame.

- `LogicClass::AI()` — **LOGIC.CPP:168**, the master per-frame sim pass: crate
  regen (177), all team AI (185), every sentient object's `AI()` with
  delete-safe indexing (194-207), house scan-bit rebuild (215-242), `Map.Logic()`
  (249), factory AI (256), house AI (265-270). **This is the ordering the clone's
  `Sim::tick` mirrors.**
- `LogicClass::Debug_Dump()` — LOGIC.CPP:66 (mono-monitor perf overlay, CHEAT_KEYS).

## Command queue / event system — [EVENT.H](../../reference/CnC_Tiberian_Dawn/EVENT.H), [QUEUE.CPP](../../reference/CnC_Tiberian_Dawn/QUEUE.CPP)

Deterministic-lockstep command layer. Player actions → `EventClass` on `OutList`
→ moved to `DoList` → executed on a scheduled frame so all machines stay in sync.

- `EventClass` — EVENT.H:47. `EventType` enum (ALLY, MEGAMISSION, PLACE, PRODUCE,
  SELL, FRAMEINFO, TIMING…) EVENT.H:54-90; per-type `Data` union EVENT.H:121-199;
  scheduled `Frame:27` + originator `ID:4` bitfields EVENT.H:99-104.
- `EventClass::Execute()` — EVENT.CPP:400 (big dispatch switch on `Type`).
- `Queue_AI()` — QUEUE.CPP:321, per-frame dispatcher → `Queue_Playback` /
  `Queue_AI_Normal` / `Queue_AI_Multiplayer` by `GameToPlay` (327-345).
- `Queue_AI_Normal()` — QUEUE.CPP:374, single-player: drains `OutList`→`DoList`
  (380-386), `Execute_DoList` (400), `Clean_DoList` (408).

## Screen/render base — [GSCREEN.CPP](../../reference/CnC_Tiberian_Dawn/GSCREEN.CPP) / GSCREEN.H

`GScreenClass` is the pure-virtual base of the whole map/screen hierarchy; the
global `Map` is the concrete instance driven by `Main_Loop` (see
[07-ui-hud-input.md](07-ui-hud-input.md) for the full chain).

- `GScreenClass::Input()` GSCREEN.CPP:276 — gathers keyboard/mouse for the frame.
- `GScreenClass::Render()` GSCREEN.CPP:390 — if dirty, set logic page to
  `HidPage`, call `Draw_It()` (407), draw buttons + messages, `Blit_Display()`.
- `GScreenClass::Blit_Display()` GSCREEN.CPP:494 — `HidPage`→`SeenBuff` page flip.

## Music — [THEME.CPP](../../reference/CnC_Tiberian_Dawn/THEME.CPP) / THEME.H

- `ThemeClass` THEME.H:41 — background-score manager; `_themes[]` table THEME.H:61.
- `ThemeClass::AI()` THEME.CPP:190 — per-frame score maintenance (driven from
  `Call_Back`); `Queue_Song()` THEME.CPP:283, `Play_Song()` THEME.CPP:310.

## Globals & shared headers

- **[GLOBALS.CPP](../../reference/CnC_Tiberian_Dawn/GLOBALS.CPP)** (33 KB) —
  *definitions* of all game-wide globals (`Map`, `Logic`, `Scen`, `Options`,
  `GameToPlay`, `Frame`, `PlayerPtr`, object heaps, palettes, `OutList`/`DoList`,
  timers). **There is no `GLOBALS.H`** — the matching `extern`s live in EXTERNS.H.
- **[EXTERNS.H](../../reference/CnC_Tiberian_Dawn/EXTERNS.H)** — `extern`
  declarations for all globals + object-class header includes. `Map` is declared
  `extern MouseClass Map;` (EXTERNS.H:123).
- **[FUNCTION.H](../../reference/CnC_Tiberian_Dawn/FUNCTION.H)** (36 KB) — the
  master project header: pulls in every subsystem header + free-function
  prototypes. Contains the full class-hierarchy diagrams in comments
  (FUNCTION.H:41-114). Nearly every `.CPP` starts with `#include "function.h"`.
- **[DEFINES.H](../../reference/CnC_Tiberian_Dawn/DEFINES.H)** (90 KB) — the big
  enum/constant header (RTTI/House/Mission/heap-max enums used throughout). See
  [08-file-formats-and-data.md](08-file-formats-and-data.md) for the master enum list.
- **COMPAT.H / WATCOM.H** — portability shims for the Watcom/DOS→Win32 build.

## Gotchas for future devs

- The per-frame throttle is split between `FrameTimer.Set()` in `Main_Loop`
  (CONQUER.CPP:1521) and the spin in `Sync_Delay()` (CONQUER.CPP:1419) — both keep
  input/render/music alive while waiting.
- Modal dialogs are **deferred**: `Main_Loop` only sets `SpecialDialog`; the
  dialog runs in `Main_Game` after the frame (CONQUER.CPP:270), because dialogs
  re-enter `Main_Loop` to keep the game running underneath.
- Determinism: object sorting (1559) and `OutList`→`DoList`→scheduled-frame
  execution (QUEUE.CPP:380-408) are deliberately outside redraw guards so all
  machines simulate identically.
- Shutdown is indirect: `Main_Game` returns to `WinMain`, which posts
  `WM_DESTROY`; the WndProc is what actually calls `Prog_End()`.
