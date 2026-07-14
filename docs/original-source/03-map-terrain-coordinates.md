# Original Source — Map, Cells, Terrain & Coordinates

> Breadcrumbs into `reference/CnC_Tiberian_Dawn/`. Files UPPERCASE; paths
> repo-relative. This is the subsystem the clone's
> [src/game/map.cpp](../../src/game/map.cpp) and `template_table.h` reimplement.

## 1. The coordinate system

Two fundamental integer types ([DEFINES.H:1593-1594](../../reference/CnC_Tiberian_Dawn/DEFINES.H#L1593)):

- `typedef unsigned long COORDINATE;` — fine position at **lepton** resolution.
- `typedef signed short CELL;` — coarse position at **cell** (tile) resolution.

**Units:**
- A cell is **24 pixels** square (`ICON_PIXEL_W/H = 24`, DISPLAY.H:45-46) and
  **256 leptons** square (`ICON_LEPTON_W/H = 256`, DISPLAY.H:47-48). So
  **1 lepton = 1/256 of a cell**.
- A COORDINATE packs two 16-bit sub-values into a 32-bit word: **high word = Y,
  low word = X**, each in leptons. Within each 16-bit half, the high byte is the
  cell index and the low byte is the 0-255 lepton offset inside that cell.

> The clone uses **256 leptons/cell** too (see MILESTONES Phase 4 gotchas:
> "256 leptons per cell", `Speed=` → leptons/tick). This matches the original.

**Map grid:** 64×64 cells (`MAP_CELL_W = MAP_CELL_H = 64`, `MAP_CELL_TOTAL =
4096`; DEFINES.H:245-253). Width is a power of two, so `CELL = (Y<<6)|X`.
(TD maps are 64-wide; the clone loads them into the top-left of its 128 grid —
MILESTONES Phase 10.)

**Conversion macros/inlines** (all in [FUNCTION.H](../../reference/CnC_Tiberian_Dawn/FUNCTION.H); mirrored in REAL.H):

| Macro | Meaning | Line |
|---|---|---|
| `XY_Coord(x,y)` | build COORDINATE from lepton X,Y | FUNCTION.H:709 |
| `Coord_X` / `Coord_Y` | extract lepton X/Y | FUNCTION.H:710-711 |
| `Coord_XLepton`/`Coord_YLepton` | 0-255 offset within cell | FUNCTION.H:715-716 |
| `Coord_XCell`/`Coord_YCell` | cell-index byte of each axis | FUNCTION.H:198-199 |
| `Cell_X(cell)`=`cell&0x3F`, `Cell_Y(cell)`=`cell>>6` | | FUNCTION.H:712-713 |
| `XY_Cell(x,y)`=`(y<<6)\|x` | build CELL | FUNCTION.H:708 |
| `Cell_Coord(cell)` | CELL→COORDINATE, centered at offset 0x80 | FUNCTION.H:722 |
| `Coord_Cell(coord)` | COORDINATE→CELL (inline asm) | FUNCTION.H:201-209 |
| `Cell_To_Lepton(c)`=`c<<8`, `Lepton_To_Cell(l)`=`(l+0x80)>>8` | | FUNCTION.H:706-707 |
| `Coord_Snap` | snap to cell center (0x80) | FUNCTION.H:720 |

**[COORD.CPP](../../reference/CnC_Tiberian_Dawn/COORD.CPP) runtime helpers:**
- `Coord_Move(start, dir, distance)` — move along a DirType via sin/cos tables (COORD.CPP:174).
- `Coord_Scatter` — random nearby coordinate (COORD.CPP:389).
- `Coord_Spillage_List` — cells an object overlaps, keyed off sub-cell position (COORD.CPP:74).
- `COORDA.ASM` holds asm `Cardinal_To_Fixed`/`Fixed_To_Cardinal` (the C versions
  in COORD.CPP:316/349 are `#ifdef OBSOLETE`).

## 2. The screen/map class hierarchy

A single deep single-inheritance chain builds the whole tactical display. The
base **is** the cell array itself (`VectorClass<CellClass>`), so any class in the
chain indexes cells via `(*this)[cell]`.

```
VectorClass<CellClass>                 (VECTOR.H)
  └─ GScreenClass        GSCREEN.H:44   — root screen/redraw framework, owns cell vector
     └─ MapClass         MAP.H:46       — cell grid, dimensions, tiberium Logic()
        └─ DisplayClass  DISPLAY.H:61   — tactical view, layers, coord<->pixel, cursor
           └─ RadarClass RADAR.H:43     — radar/minimap
              └─ PowerClass    POWER.H:43
                 └─ SidebarClass SIDEBAR.H:45
                    └─ TabClass      TAB.H:44
                       └─ HelpClass  HELP.H:43
                          └─ ScrollClass SCROLL.H:44
                             └─ MouseClass MOUSE.H:44   ← global `Map` is this
                                └─ MapEditClass MAPEDIT.H:177 (editor build only)
```

The global instance `Map` is a `MouseClass` (see
[07-ui-hud-input.md](07-ui-hud-input.md) for the UI layers).

**Key MapClass members** ([MAP.H](../../reference/CnC_Tiberian_Dawn/MAP.H)):
`MapCellX/Y/Width/Height` — active sub-rectangle of the 64×64 grid (MAP.H:99-102);
`TotalValue` — total harvestable tiberium (MAP.H:107); `Logic()` — tiberium
growth/spread (MAP.H:80, §5); queries `In_Radar`/`Cell_Region`/`Cell_Threat`/
`Sight_From`/`Place_Down`/`Pick_Up`/`Overpass` (MAP.H:63-78); cell buffer
`Alloc_Cells`/`Free_Cells`/`Init_Cells` (MAP.H:55-57).

**Key DisplayClass members** ([DISPLAY.H](../../reference/CnC_Tiberian_Dawn/DISPLAY.H)):
`TacticalCoord` — top-left of visible view (DISPLAY.H:75); `DesiredTacticalCoord`
for scrolling (DISPLAY.H:224); `static LayerClass Layer[LAYER_COUNT]` — display
sort layers (DISPLAY.H:88); `Click_Cell_Calc(x,y)` pixel→CELL (DISPLAY.H:149);
`Pixel_To_Coord`/`Coord_To_Pixel` (DISPLAY.H:171-172).

**LayerClass** ([LAYER.H:45](../../reference/CnC_Tiberian_Dawn/LAYER.H#L45)):
`class LayerClass : public DynamicVectorClass<ObjectClass *>` — a depth-sorted
render list. `Submit`/`Sort`/`Sorted_Add` (LAYER.H:50-52). Layers:
`LAYER_GROUND` (units/buildings), `LAYER_TOP` (aircraft/bullets) (DEFINES.H:722-730).

## 3. CellClass — per-cell data ([CELL.H:49](../../reference/CnC_Tiberian_Dawn/CELL.H#L49))

Every one of the 4096 cells is a `CellClass`:

- **State bitflags** (CELL.H:57-103): `IsPlot` (radar dirty), `IsCursorHere`,
  `IsMapped` (revealed vs shroud), `IsVisible`, `IsTrigger`, `IsWaypoint`,
  `IsRadarCursor`, `IsFlagged`.
- **Terrain layers:**
  - `TemplateType TType; unsigned char TIcon;` — base terrain template + icon
    (lowest render layer) (CELL.H:110-111).
  - `OverlayType Overlay; unsigned char OverlayData;` — tiberium/walls/concrete/
    roads. `OverlayData` doubles as tiberium growth stage / wall damage (CELL.H:118-119).
  - `SmudgeType Smudge; unsigned char SmudgeData;` — stains/craters/scorch (CELL.H:125-126).
  - `LandType Land` (private, CELL.H:250) — computed; via `Land_Type()` (CELL.H:193).
- **Ownership:** `HousesType Owner` (walls/smudges), `HousesType InfType`
  (infantry occupant) (CELL.H:134,140).
- **Occupancy:** `ObjectClass *OccupierPtr` (object in cell) + `Overlapper[3]`
  (objects overlapping from adjacent cells) (CELL.H:146-147); `union Flag` with
  `Occupy` bitfield — sub-cell slots `Center/NW/NE/SW/SE`, plus `Vehicle`/
  `Monolith`/`Building` (CELL.H:157-169); `Spot_Index`/`Is_Spot_Free`/
  `Closest_Free_Spot` (CELL.H:181-184).
- **Key methods:** `Cell_Coord()` (CELL.H:190), `Cell_Number()` (CELL.H:192),
  `Adjacent_Cell(face)` (CELL.H:188-189), `Cell_Object/Techno/Unit/Infantry/
  Terrain/Building` accessors (CELL.H:187,194-199), `Occupy_Down/Up` &
  `Overlap_Down/Up` (CELL.H:208-211), `Draw_It` (CELL.H:227), and tiberium/wall
  maintenance `Tiberium_Adjust`/`Reduce_Tiberium`/`Wall_Update`/
  `Recalc_Attributes` (CELL.H:234-239). Impls in CELL.CPP.

> ⚠️ Clone gotcha (MILESTONES Phase 2): template IDs **0 and 255** mean clear —
> `CELL.CPP Recalc_Attributes` special-cases 255. TD uses `0xff`=clear.

## 4. Terrain content types & their data tables

Each cell "content" system has a *type class* in
[TYPE.H](../../reference/CnC_Tiberian_Dawn/TYPE.H) (all derive from
`ObjectTypeClass`, TYPE.H:263), with concrete instances in a `*DATA.CPP` file
retrieved via `As_Reference(type)`:

| System | Type class (TYPE.H) | Data file | Runtime class | What it is |
|---|---|---|---|---|
| **Template** | `TemplateTypeClass` (1430) | [CDATA.CPP](../../reference/CnC_Tiberian_Dawn/CDATA.CPP) (84+) | TEMPLATE.* | Base ground tiles: clear, roads, shores, cliffs, bridges. Sets cell base `Land`. `Width`/`Height` in icons, `AltLand`/`AltIcons` (TYPE.H:1436-1462). |
| **Terrain** | `TerrainTypeClass` (1333) | [TDATA.CPP](../../reference/CnC_Tiberian_Dawn/TDATA.CPP) (81+) | TERRAIN.* | Standalone objects: trees, rocks, blossom trees. `IsDestroyable`, `IsTiberiumSpawn` (blossom trees seed tiberium) (TYPE.H:1345-1369). |
| **Overlay** | `OverlayTypeClass` (1786) | [ODATA.CPP](../../reference/CnC_Tiberian_Dawn/ODATA.CPP) (54+) | OVERLAY.* | Flat "carpet": tiberium fields, walls, concrete, roads. `IsWall`, `IsTiberium`, `DamageLevels` (TYPE.H:1802-1845). |
| **Smudge** | `SmudgeTypeClass` (1906) | [SDATA.CPP](../../reference/CnC_Tiberian_Dawn/SDATA.CPP) | SMUDGE.* | Cosmetic stains: craters, scorch, building bibs. |

⚠️ **Naming quirk:** *templates* live in `CDATA.CPP` (C = Cell/template icon set),
*terrain* objects (trees) in `TDATA.CPP`. `TEMPLATE.*`/`TERRAIN.*` hold the
*runtime* object classes; `CDATA/TDATA/ODATA/SDATA.CPP` hold the *data tables*.
`OverlayClass` and `SmudgeClass` are transient — they stamp themselves into the
cell then limbo/destroy.

## 5. Tiberium — representation & growth

**Representation:** Tiberium is an **Overlay**. Twelve enums
`OVERLAY_TIBERIUM1..12` (DEFINES.H:897-908) are the visual variants; their
`OverlayTypeClass` set `Land = LAND_TIBERIUM`, `IsTiberium = 1` (TYPE.H:1840;
instances ODATA.CPP:173+). A cell's **density stage lives in
`CellClass::OverlayData`** (0-11). Value per step is
`UnitTypeClass::TIBERIUM_STEP = 25` credits (TYPE.H:841).

**Smoothing/value:** `CellClass::Tiberium_Adjust(pregame)` (CELL.CPP:1781) counts
tiberium in the 8 neighbors, maps to `OverlayData` via `_adj[9]`, returns cell
value `(OverlayData+1)*TIBERIUM_STEP` (CELL.CPP:1812). Map `TotalValue` summed at
MAP.CPP:602.

**Harvesting:** `CellClass::Reduce_Tiberium(levels)` decrements `OverlayData`,
clearing the overlay at zero (CELL.CPP:1391-1403).

**Growth & spread** — `MapClass::Logic()` (MAP.CPP:788), gated by
`Special.IsTGrowth`/`IsTSpread` (MAP.CPP:793):
- Incrementally scans the 4096 cells (30/call, alternating direction; MAP.CPP:799-834).
- **Growth:** cells with `Land_Type()==LAND_TIBERIUM` and `OverlayData<11` → on
  pass completion random ones get `OverlayData++` up to 11 (MAP.CPP:845-852).
- **Spread:** heavy tiberium (`OverlayData>6`) or a blossom tree
  (`IsTiberiumSpawn`, weighted 3×) → a random empty/clear/non-bridge adjacent
  cell gets a new tiberium overlay with `OverlayData=1` (`new OverlayClass(...)`
  at MAP.CPP:883-884).
- `Special.IsTFast` doubles tries per pass (MAP.CPP:837-838).

Blossom trees are the seed source, flagged by `TerrainTypeClass::IsTiberiumSpawn`
(TYPE.H:1357).

> Clone status (MILESTONES Phase 10): TD tiberium overlay currently draws frame 0
> only (no density/adjacency frames); harvest uses a flat BailCount. The original
> density model above is the reference for finishing that.
