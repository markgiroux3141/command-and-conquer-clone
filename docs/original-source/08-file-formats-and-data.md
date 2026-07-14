# Original Source — File I/O, Asset Formats, Rules Data & Containers

> Breadcrumbs into `reference/CnC_Tiberian_Dawn/`. Files UPPERCASE; paths
> repo-relative. The clone's [src/formats/](../../src/formats/) decoders
> (mix/shp/cps/aud/ini/tmp/pal/fnt) are modern reimplementations of the readers
> described here (with OpenRA cross-checks — see [README.md](../../README.md)).
>
> ⚠️ The low-level Westwood library (**WWLIB32 / WWFLAT32**) — the actual SHP
> blitter, `.AUD` ADPCM decoder, LCW codec, DirectDraw layer — is **NOT in this
> GPL drop**. Only the game-side wrappers are present. For those low-level
> routines, the **Red Alert** source under `reference/CnC_Red_Alert/` *does*
> include `WWFLAT32`/`WIN32LIB` and is the companion reference.

## 1. File & archive system (.MIX)

File classes form a chain, each adding capability:
`FileClass` (abstract, WW lib) → `RawFileClass` → `CDFileClass` → `CCFileClass`.

- **`RawFileClass`** — [RAWFILE.H:119](../../reference/CnC_Tiberian_Dawn/RAWFILE.H#L119).
  Wraps raw DOS handles; declares `Open/Read/Seek/Size/Write/Close/Error`
  (RAWFILE.H:135-148).
- **`CDFileClass`** — [CDFILE.H:54](../../reference/CnC_Tiberian_Dawn/CDFILE.H#L54).
  Multi-directory / CD-ROM search; static drive list `SearchDriveType * First`
  (CDFILE.H:95); `Set_Search_Drives`/`Add_Search_Drive` (CDFILE.H:68-72).
- **`CCFileClass`** — [CCFILE.H:52](../../reference/CnC_Tiberian_Dawn/CCFILE.H#L52).
  The class the game uses; understands files inside .MIX archives. State
  `FromDisk`, `Pointer` (RAM-resident), `Start` (embedded offset), `Length`
  (CCFILE.H:79-107).

### .MIX archive format — `MixFileClass` ([MIXFILE.H:44](../../reference/CnC_Tiberian_Dawn/MIXFILE.H#L44))
`class MixFileClass : public LinkClass` — mixfiles kept in an intrusive linked
list via static `First` (MIXFILE.H:84).

**On-disk layout** (from the ctor + structs):
- **`FileHeader`** (MIXFILE.H:74-77): `short count` + `long size`.
- `count` × **`SubBlock`** (MIXFILE.H:60-68): `long CRC`, `long Offset`, `long
  Size`. Sub-blocks sorted by CRC → lookup is a **binary search**.
- Then the raw data section.

**Load/lookup:**
- Ctor `MixFileClass::MixFileClass` — MIXFILE.CPP:171 — opens via `CCFileClass`,
  reads header, allocates `SubBlock` index, links into the global list. Data
  uncached (`Data = 0`).
- `Cache()` MIXFILE.CPP:329 loads the data section to RAM; `Free()` MIXFILE.CPP:384.
- **`Offset()`** — MIXFILE.CPP:466 — the heart: `Calculate_CRC` of the
  upper-cased name (:475), `bsearch` each mixfile's index (:490). Returns whether
  in RAM (`realptr`) or a disk offset.
- `Retrieve(filename)` MIXFILE.CPP:246 — direct RAM pointer if cached.

`CCFileClass::Open` (CCFILE.CPP:414) calls `MixFileClass::Offset` (:436) to
resolve loose vs in-RAM vs on-disk-embedded; `Read` (CCFILE.CPP:204) has three
paths (Mem_Copy / seek+delegate / plain `CDFileClass::Read`).
> Clone: `tools/mix_extract.py` + `src/formats/` MIX reader (MILESTONES Phase 0)
> implement this, incl. RA encrypted headers (a TD-vs-RA divergence).

## 2. Shape (.SHP) format & sprite drawing

- **[KEYFRAME.CPP](../../reference/CnC_Tiberian_Dawn/KEYFRAME.CPP)** decodes SHP
  frames. Header `KeyFrameHeaderType` (KEYFRAME.CPP:49-57): `frames, x, y, width,
  height, largest_frame_size, flags`.
- **`Build_Frame(dataptr, framenumber, buffptr)`** — KEYFRAME.CPP:215 —
  reconstructs one frame: keyframes are LCW-compressed (`LCW_Uncompress`,
  :347), delta frames are `Apply_XOR_Delta` (:387). Frame-type bits
  `KF_KEYFRAME`/`KF_DELTA` from the top byte of the offset (:337-350).
- Optional decoded-shape cache ("Big Shape Buffer", `UseBigShapeBuffer`) trades
  RAM for speed (KEYFRAME.CPP:246-484).
- Accessors: `Get_Build_Frame_Count` (:506), `_Width` (:548), `_Height` (:572),
  `Get_Build_Frame_Palette` (:581).
- **Drawing:** game-side handler `CC_Draw_Shape` —
  [CONQUER.CPP:2697](../../reference/CnC_Tiberian_Dawn/CONQUER.CPP#L2697) — calls
  `Build_Frame` then blits. Options use the `ShapeFlags_Type` bitmask
  (`SHAPE_NORMAL/CENTER/WIN_REL/FADING/GHOST/PREDATOR`; `operator|` in
  JSHELL.H:118-120). ⚠️ The enum itself + the blitter live in WWLIB32 (not in
  this drop; see RA source).
> Clone: `src/formats/shp.cpp` (LCW + XOR-delta, verified vs 4tnk.shp) +
> `shpd2.cpp` (Dune II-format variant for `mouse.shp`) — MILESTONES Phase 1/3.

## 3. Master game-object enums — [DEFINES.H](../../reference/CnC_Tiberian_Dawn/DEFINES.H)

`DEFINES.H` (90 KB) holds essentially every gameplay enum:

| Enum | Location |
|---|---|
| `HousesType` | DEFINES.H:619 (–634) |
| `MissionType` | DEFINES.H:451 (–479) |
| `StructType` (buildings) | DEFINES.H:772 (–851) |
| `InfantryType` | DEFINES.H:933 (–959) |
| `UnitType` (vehicles) | DEFINES.H:968 (–995) |
| `AircraftType` | DEFINES.H:1027 (–1037) |
| `WarheadType` | DEFINES.H:1714 (–1731) |
| `WeaponType` | DEFINES.H:1739 (–1769) |
| `ArmorType` | DEFINES.H:1778 (–1786) |

Neighbors: `RTTIType` (:182), `BulletType` (:739), `OverlayType` (:889),
`TemplateType` (:1054), `TerrainType` (:1294), `SmudgeType` (:1341), `AnimType`
(:1370), `DoType` (:1480), `FacingType` (:1991), `DirType` (:2037), `SpeedType`
(:2081), `LandType` (:1891), `TheaterType` (:1907), `VocType`/`VoxType` (sound,
:2100/:2239), `ThemeType` (music, :325). `PathType` struct (:2384).

## 4. Big static data tables

### [CONST.CPP](../../reference/CnC_Tiberian_Dawn/CONST.CPP) — global rule tables
- **`Weapons[WEAPON_COUNT]`** (CONST.CPP:76) — `WeaponTypeClass`: bullet type,
  damage, ROF, range (leptons), fire sound, muzzle anim.
- **`Warheads[WARHEAD_COUNT]`** (CONST.CPP:111) — `WarheadTypeClass`: spread,
  wall/wood/tiberium destroyer flags, and the **armor-vs-warhead damage table**
  `{none, wood, aluminum, steel, concrete}` (CONST.CPP:112-123). **This is the
  C&C damage model** — see [05-movement-pathfinding-combat.md](05-movement-pathfinding-combat.md) §5.
- Geometry: `StoppingCoordAbs` (:62), `Pixel2Lepton` (:132), `AdjacentCell`/
  `AdjacentCoord` (:144/155), `Facing8/16/32` tables (:173/185/201).
- **`Ground[LAND_COUNT]`** (CONST.CPP:261) — per-terrain movement speeds.
- `Theaters[THEATER_COUNT]` (:282) and house color remaps
  `RemapYellow/Red/BlueGreen/Orange/Green/Blue/None` (:294-408).
> Clone: `td_rules.ini` + `src/game/rules.cpp` recreate these tables in INI form;
> `house.cpp tdRemap` uses the CONST.CPP remap bands (MILESTONES Phase 10).

### `*DATA.CPP` — one file per object category
Each defines `static const *TypeClass` instances (the stat blocks) for every
object + a `Prep_For_Add` for the editor. Full list in
[02-object-hierarchy.md](02-object-hierarchy.md):
`IDATA` (infantry), `UDATA` (units), `BDATA` (buildings, 225 KB), `ADATA` (anims),
`AADATA` (aircraft), `TDATA` (terrain/trees), `ODATA` (overlays), `SDATA`
(smudges), `BBDATA` (bullets), `CDATA` (templates, 67 KB), `HDATA` (houses).

## 5. Low-level containers & object pools

### Vectors — [VECTOR.H](../../reference/CnC_Tiberian_Dawn/VECTOR.H)
- **`VectorClass<T>`** (VECTOR.H:81) — resizable typed array; `Resize/Clear/ID/
  operator[]`.
- **`DynamicVectorClass<T>`** (VECTOR.H:118) — adds `Add`/`Delete` with an
  active-count packed at the front.
- `BooleanVectorClass` — bit-vector for heap free-flags (HEAP.H:100).

### Fixed-size object pools — [HEAP.H](../../reference/CnC_Tiberian_Dawn/HEAP.H) / HEAP.CPP
- **`FixedHeapClass`** (HEAP.H:50) — block allocator: fixed `Size` blocks,
  `TotalCount`/`ActiveCount`, `Buffer`, `BooleanVectorClass FreeFlag`.
- **`FixedIHeapClass`** (HEAP.H:139) — adds `DynamicVectorClass<void*>
  ActivePointers` (HEAP.H:159) for cheap iteration of active objects.
- **`TFixedIHeapClass<T>`** (HEAP.H:169) — typed; adds save/load + pointer
  (de)serialization: `Save` HEAP.CPP:408, `Load` :458, `Code_Pointers` :523,
  `Decode_Pointers` :546; `Ptr(index)`/`Raw_Ptr(index)` (HEAP.H:184-185).

### The global game-object pools — [GLOBALS.CPP:63-77](../../reference/CnC_Tiberian_Dawn/GLOBALS.CPP#L63)
Every live entity lives in one of these `TFixedIHeapClass` instances:
```
Units, Factories, Terrains, Templates, Smudges, Overlays, Infantry,
Bullets, Buildings, Anims, Aircraft, Triggers, TeamTypes, Teams, Houses
```
Combined with the per-class overloaded `new`/`delete`, this is how all in-game
objects are allocated. `TARGET` handles index into these (see
[05-movement-pathfinding-combat.md](05-movement-pathfinding-combat.md) §4).

## 6. Fixed-point math & RNG

### Fixed-point
No `FIXED.CPP/.H` in this drop. Fractions are 8-bit fixed-point where 256 (0x100)
== 1.0, via two inline-asm helpers in
[JSHELL.H](../../reference/CnC_Tiberian_Dawn/JSHELL.H):
- `Fixed_To_Cardinal(base, fixed)` — JSHELL.H:205 — `base * fixed / 256`.
- `Cardinal_To_Fixed(base, cardinal)` — JSHELL.H:218 — `cardinal * 256 / base`.
Coordinate/lepton math in COORD.CPP + COORDA.ASM.

### RNG — [RAND.CPP](../../reference/CnC_Tiberian_Dawn/RAND.CPP)
- **`Sim_Random()`** — RAND.CPP:58 — **deterministic** (network-sync) 0-255
  generator: indexes a fixed 256-entry permutation `_randvals[]` via a wrapping
  `unsigned char SimRandIndex`. Every value appears once per cycle.
- **`Sim_IRandom(min, max)`** — RAND.CPP:107 — scales via `Fixed_To_Cardinal`.
> Determinism matters for the clone too — MILESTONES notes identical run hashes.

## 7. INI / rules reading

- **[PROFILE.CPP](../../reference/CnC_Tiberian_Dawn/PROFILE.CPP)** — the actual
  INI parser: `WWGetPrivateProfileInt` (:120), `WWGetPrivateProfileString`,
  `WWWritePrivateProfile*` (:36-39), `Read_Private_Config_Struct` (:54).
- **[INI.CPP](../../reference/CnC_Tiberian_Dawn/INI.CPP)** — not a generic class:
  the scenario/rules loader (`Read_Scenario_Ini` :210) that consumes the
  `WWGetPrivateProfile*` API. See [06-houses-ai-missions.md](06-houses-ai-missions.md) §3.
> Clone: `src/formats/ini.cpp` (verified vs rules.ini, 246 sections) — Phase 1.

## 8. Audio (.AUD)

- **[AUDIO.CPP](../../reference/CnC_Tiberian_Dawn/AUDIO.CPP)** drives sound/speech
  via the WW sample API: `Is_Sample_Playing`/`Stop_Sample_Playing`
  (:477/516/540), keyed off `VocType`/`VoxType` (DEFINES.H:2100/2239). ⚠️ The
  `.AUD` (Westwood ADPCM) decoder + mixer are in WWLIB32, not this drop.
> Clone: `src/formats/aud.cpp` (both IMA + WW ADPCM paths) + `src/game/audio.cpp`
> SDL mixer — MILESTONES Phase 1/8.

## Quick file index

| Concern | Files |
|---|---|
| File chain | `RAWFILE`, `CDFILE`, `CCFILE` (.H/.CPP) |
| Archive | `MIXFILE.H/.CPP` |
| Shapes | `KEYFRAME.CPP`, `CC_Draw_Shape` in CONQUER.CPP:2697, flags in JSHELL.H |
| Enums | `DEFINES.H` |
| Rule tables | `CONST.CPP`, `*DATA.CPP`, `CDATA.CPP` |
| Containers/pools | `VECTOR.H`, `HEAP.H/.CPP`, global pools GLOBALS.CPP:63-77, `ALLOC.CPP` |
| Math/RNG | fixed-point JSHELL.H:205-219, `RAND.CPP`, `COORD.CPP` |
| INI | `PROFILE.CPP` (parser), `INI.CPP` (scenario loader) |
| Audio | `AUDIO.CPP` / `AUDIO.H` |
