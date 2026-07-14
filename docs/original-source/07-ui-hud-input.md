# Original Source — In-Game UI / HUD, Input & Menus

> Breadcrumbs into `reference/CnC_Tiberian_Dawn/`. Files UPPERCASE; paths
> repo-relative. This is what the clone's HUD in
> [src/game_main.cpp](../../src/game_main.cpp) + [src/game/render.cpp](../../src/game/render.cpp)
> reimplements (sidebar, radar, power bar, tab bar — MILESTONES Phases 6/8/9).

## 1. The tactical-screen class chain (the big one)

The in-game screen is **one linear single-inheritance chain**, NOT multiple
inheritance. Each HUD layer is a base class of the next; the single global object
`Map` at the bottom **is** the entire tactical UI (map + radar + power + sidebar +
tabs + tooltips + scrolling + mouse).

| Class | Declaration | Responsibility |
|---|---|---|
| `GScreenClass : VectorClass<CellClass>` | [GSCREEN.H:44](../../reference/CnC_Tiberian_Dawn/GSCREEN.H#L44) | Abstract full-screen game screen; owns cell vector + redraw loop |
| `MapClass : GScreenClass` | [MAP.H:46](../../reference/CnC_Tiberian_Dawn/MAP.H#L46) | Cell map / logic layer |
| `DisplayClass : MapClass` | [DISPLAY.H:61](../../reference/CnC_Tiberian_Dawn/DISPLAY.H#L61) | Tactical map rendering + tactical click gadget |
| `RadarClass : DisplayClass` | [RADAR.H:43](../../reference/CnC_Tiberian_Dawn/RADAR.H#L43) | Minimap / radar |
| `PowerClass : RadarClass` | [POWER.H:43](../../reference/CnC_Tiberian_Dawn/POWER.H#L43) | Power bar |
| `SidebarClass : PowerClass` | [SIDEBAR.H:45](../../reference/CnC_Tiberian_Dawn/SIDEBAR.H#L45) | Build sidebar + strips |
| `TabClass : SidebarClass` | [TAB.H:44](../../reference/CnC_Tiberian_Dawn/TAB.H#L44) | Top tab / credits bar |
| `HelpClass : TabClass` | [HELP.H:43](../../reference/CnC_Tiberian_Dawn/HELP.H#L43) | Tooltips / help overlay |
| `ScrollClass : HelpClass` | [SCROLL.H:44](../../reference/CnC_Tiberian_Dawn/SCROLL.H#L44) | Tactical-view scrolling |
| `MouseClass : ScrollClass` | [MOUSE.H:44](../../reference/CnC_Tiberian_Dawn/MOUSE.H#L44) | Mouse cursor + top-level input |

**The global object:** `MouseClass Map;` at
[GLOBALS.CPP:279](../../reference/CnC_Tiberian_Dawn/GLOBALS.CPP#L279) (declared
`extern MouseClass Map;` EXTERNS.H:123; in the editor build it's `MapEditClass
Map;`). So `Map.Radar_Cursor()`, `Map.Draw_It()` dispatch through the whole stack.

Redraw/input flow: each layer overrides `virtual void Draw_It(bool complete)` and
chains to its base (POWER.H:62, TAB.H:50, HELP.H:53). Top-level input enters at
`MouseClass`.

## 2. SidebarClass — build sidebar & strip buttons ([SIDEBAR.H](../../reference/CnC_Tiberian_Dawn/SIDEBAR.H))

- Class SIDEBAR.H:45. Two vertical strips: `COLUMNS=2` (SIDEBAR.H:106).
- **`StripClass : StageClass`** (SIDEBAR.H:140) — one build column. Instances
  `Column[COLUMNS]` (SIDEBAR.H:347).
  - Buildable icons: `struct BuildType` (SIDEBAR.H:305-309) in
    `Buildables[MAX_BUILDABLES]` (SIDEBAR.H:310).
  - Scroll buttons: `static ShapeButtonClass UpButton[COLUMNS]`/`DownButton[COLUMNS]` (SIDEBAR.H:337-338).
  - Per-icon click widgets: **`SelectClass : ControlClass`** (SIDEBAR.H:142),
    `SelectButton[COLUMNS][MAX_VISIBLE]` (SIDEBAR.H:339).
  - Production plumbing: `Abandon_Production` (SIDEBAR.H:165), `Activate`
    (SIDEBAR.H:174), `Factory_Link` (SIDEBAR.H:177).
- Sidebar API: `Abandon_Production` (SIDEBAR.H:123), `Factory_Link` (SIDEBAR.H:128),
  special command buttons `Activate_Repair`/`Activate_Upgrade`/`Activate_Demolish`
  (SIDEBAR.H:386-388), `Which_Column` maps RTTI→strip (SIDEBAR.H:390).
- Background/repair buttons: **`SBGadgetClass : GadgetClass`** (SIDEBAR.H:360).
> Clone parallel (MILESTONES Phase 6-9): two cameo columns, wheel scroll, click
> to build/place, right-click cancel, REPAIR|SELL|MAP row, scroll arrows
> (`stripup`/`stripdn`), `strip.shp` slot texture — all mirror this.

## 3. RadarClass — minimap ([RADAR.H](../../reference/CnC_Tiberian_Dawn/RADAR.H))

Class RADAR.H:43. `Radar_Cursor` (RADAR.H:86), `Coord_To_Radar_Pixel`
(RADAR.H:83), `Cell_On_Radar` (RADAR.H:88), `Click_In_Radar` (RADAR.H:75),
`Zoom_Mode` (RADAR.H:74), state `IsRadarActive` (RADAR.H:131). The clickable
surface is a gadget: `class TacticalClass : GadgetClass` (RADAR.H:150), instance
`static TacticalClass RadarButton` (RADAR.H:164).
> Clone (MILESTONES Phase 8-9): live minimap + house-colored blips + radar
> shroud + faction medallion (GDI eagle / Nod viper) before a Comm Center exists.

## 4. PowerClass — power bar ([POWER.H](../../reference/CnC_Tiberian_Dawn/POWER.H))

Class POWER.H:43. `Draw_It` (POWER.H:62), redraw `IsToRedraw` (POWER.H:73),
layout enum `PowerEnums` (POWER.H:94), bar-scaling `Power_Height(int)` (POWER.H:100).
> Clone (MILESTONES Phase 9): vertical power bar, green fill=output,
> yellow tick=demand, red on deficit.

## 5. TabClass — top tab / credits bar ([TAB.H](../../reference/CnC_Tiberian_Dawn/TAB.H))

Class TAB.H:44. `Draw_It` (TAB.H:50), `Draw_Credits_Tab` (TAB.H:53),
`Set_Active(int)` (TAB.H:76). Owns the animated credits readout `CreditClass
Credits` (TAB.H:64); `CreditClass` at [CREDITS.H:44](../../reference/CnC_Tiberian_Dawn/CREDITS.H#L44).
> Clone top bar (MILESTONES Phase 8): OPTIONS | PWR | `$credits` | SIDEBAR.

## 6. GadgetClass widget hierarchy ([GADGET.H](../../reference/CnC_Tiberian_Dawn/GADGET.H))

Base widget + core virtuals:
- **`GadgetClass : LinkClass`** (GADGET.H:99). `Input` (GADGET.H:122), `Draw_Me`
  (GADGET.H:147), `Action` (GADGET.H:174), `Clicked_On` (GADGET.H:231),
  `Flag_To_Redraw` (GADGET.H:137). Gadgets form an intrusive linked list
  processed each frame.
- **`ControlClass : GadgetClass`** (CONTROL.H:58) — adds an ID + peer-notification.

Derived widgets:

| Widget | Declaration | Use |
|---|---|---|
| `ToggleClass : ControlClass` | TOGGLE.H:49 | button base |
| `ShapeButtonClass : ToggleClass` | SHAPEBTN.H:43 | shape/icon buttons (sidebar Up/Down) |
| `TextButtonClass : ToggleClass` | TEXTBTN.H:44 | text buttons (dialogs) |
| `GaugeClass : ControlClass` | GAUGE.H:41 | gauges; `TriColorGaugeClass` GAUGE.H:94 |
| `SliderClass : GaugeClass` | SLIDER.H:58 | sliders |
| `ListClass : ControlClass` | LIST.H:59 | list boxes (COLRLIST, CHEKLIST) |

⚠️ **No separate `ButtonClass`** in TD — buttons descend from
`ControlClass → ToggleClass`. The clickable tactical-map area is itself a gadget:
`class TacticalClass : GadgetClass` (DISPLAY.H:273; `friend class TacticalClass`
DISPLAY.H:280 / MOUSE.H:75).

## 7. Mouse / cursor ([MOUSE.H](../../reference/CnC_Tiberian_Dawn/MOUSE.H))

`MouseClass : ScrollClass` (MOUSE.H:44) — bottom of the tactical chain, top of
input. Cursor control (virtual): `Override_Mouse_Shape` (MOUSE.H:56),
`Revert_Mouse_Shape` (MOUSE.H:57), `Get_Mouse_Shape` (MOUSE.H:58), `Mouse_Small`
(MOUSE.H:59). Cursor data: `static void const * MouseShapes` (MOUSE.H:106),
`CurrentMouseShape` (MOUSE.H:113).
> Clone gotcha (MILESTONES Phase 10): TD `mouse.shp` isn't ShpD2 format → OS
> cursor fallback; custom cursor still TODO.

## 8. Tactical-view scrolling ([SCROLL.H](../../reference/CnC_Tiberian_Dawn/SCROLL.H))

`ScrollClass : HelpClass` (SCROLL.H:44). Edge/auto scroll: `Set_Autoscroll(int)`
(SCROLL.H:74), `IsAutoScroll` (SCROLL.H:51).
> Clone (MILESTONES Phase 9): screen-edge scroll fires only inside the tactical
> viewport (never over the tab bar / sidebar).

## 9. Tooltips / help ([HELP.H](../../reference/CnC_Tiberian_Dawn/HELP.H))

`HelpClass : TabClass` (HELP.H:43). Tooltip + cost popup: `Help_Text(int text, x,
y, color, quick, cost)` (HELP.H:58), `Set_Text` (HELP.H:76). Timing
`CountDownTimer` (HELP.H:128).

## 10. Menus & dialogs (high level)

- `MENUS.CPP` — main/start menus (procedural, no header).
- Options: `OPTIONS.CPP/.H` (settings model), `GOPTIONS.CPP/.H` (in-game options
  dialog), `GAMEDLG.CPP/.H` (game-setup). These compose the widget family from §6
  (`TextButtonClass`, `SliderClass`, `ListClass`, gauges), not the tactical `Map`
  chain.
- List widgets for dialogs: `COLRLIST.CPP/.H`, `CHEKLIST.CPP/.H`, `MSGLIST.CPP/.H`.

## Quick navigation summary

- Whole in-game HUD = global `Map` (`MouseClass`, GLOBALS.CPP:279) walking the
  chain `GScreenClass → MapClass → DisplayClass → RadarClass → PowerClass →
  SidebarClass → TabClass → HelpClass → ScrollClass → MouseClass`.
- All clickable UI (sidebar icons, radar, dialog buttons, even the tactical map)
  are `GadgetClass` instances processed via `Input`/`Action`/`Draw_Me`.
