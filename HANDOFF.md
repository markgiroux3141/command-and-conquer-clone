# Handoff — session 6 → session 7 (written 2026-07-13)

Session 6 pivoted the engine from Red Alert to the original 1995 **Tiberian
Dawn** aesthetic (user: "use this version instead") and got it **playable**.
Phase 10 is well underway; see MILESTONES.md "Phase 10" for the full checklist.

## Read first

1. `MILESTONES.md` — Phase 10 section (done items + the "Polish/known gaps" list
   is the backlog) and the session-6 log entry.
2. `README.md` / `play.bat` — how to run.
3. Then source, only as needed for the next task:
   - `src/game_main.cpp` — the shell; TD support is an `isTd` branch through the
     setup (data layout, `templateArt`, `remapFor`), the terrain/overlay bake,
     build lists, and sides. This is where most TD gameplay work lands.
   - `src/game/sim.{h,cpp}` — movement/combat/harvest/production (shared RA+TD).
   - `src/game/map.cpp` (`loadTd`), `src/game/house.cpp` (`tdRemap`),
     `src/formats/tmp.cpp` (TD branch) — verified, don't re-read unless touching.

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release` (MSVC). Targets:
  `game_exe`→game.exe, `mapview`, `shpview`, etc.
- **TD is playable**: `play.bat` (auto-detects disc from map name: scg*→gdi,
  scb*→nod, scm*→either). Default `play` = GDI mission 1. Select (click/drag),
  move/attack (right-click), build from the sidebar, deploy MCV, harvest.
- Renders correct in all three theaters; GDI gold / Nod red house colors.
- `tools/render_td.py` → `renders/*.png` (gitignored) for eyeballing maps.
- **It's a sandbox**: enemies are placed but passive (no auto-acquire / no AI),
  no win/lose, no triggers, no audio. Passability defaults to Clear (units cross
  water/rock). `td_rules.ini` is a compact hand-port of TD stats.

## Next tasks (suggested order — highest gameplay payoff first)

1. **Enemy return-fire (auto-acquire)** — units fire at in-range enemies without
   orders. Combat already supports targets (`sim.orderAttack`); add a per-tick
   scan for the nearest enemy in weapon range when a unit is idle. This is the
   single biggest "feels like a game" win and is the top gap. Applies to RA too.
2. **TD passability** — stop the water/rock-walking. Emit the template `Land=`
   into `td_template_table.h` (extend `gen_td_template_table.py`: the 5th ctor
   arg in CDATA.CPP is `LAND_*`; TD `LandType` order == our `game::Land`), then
   in `game_main.cpp bakeTerrainCell` set per-cell land from the template land
   for TD (mirror the RA `landBytes`/`landFromControl` path). mapview too if you
   want the debug view accurate.
3. **Audio (Phase 8)** — assets + decoder exist (`src/formats/aud.cpp`, TD
   `SCORES/`=music, `SOUNDS/`=SFX+EVA). Wire SDL audio: AUD→PCM, play SFX on sim
   events (fire/impact/death/build), EVA lines, and a music jukebox from SCORES.
4. **Play-test + refine TD production** (sidebar populates; the build chain uses
   the shared RA production path — verify prereqs/costs feel right, tune
   `td_rules.ini`). Then skirmish AI and mission triggers for a beatable mission.

## Gotchas discovered this session (not all in code comments)

- TD template IDs are single **bytes** (RA=u16); `0xff`=clear. TD `.bin` is
  64×64, 2 bytes/cell (template,icon), interleaved; loaded into the top-left of
  the 128-grid so sim/render stay 128-wide. Object/overlay cell numbers are
  64-wide, remapped on load (`map.cpp loadTd`).
- TD theater **dir** is truncated `TEMPERAT` but the **palette** base is
  `temperat`; theater art exts are `.des/.tem/.win`.
- TD **TMP** has no land-control map (RA does) — that's why passability needs
  the template `Land=` instead (task 2).
- TD **cameos** (`<type>icon.shp`) ship only on the **Covert Ops** disc, not the
  base gdi/nod CONQUER. game.exe uses `root/../covert_ops/CONQUER` as the
  fallback art dir for TD (`hiresDir`). mapview does NOT (doesn't draw cameos).
- TD house remap: art carries player color in palette band **176–191**; each
  house remaps it to a CONST.CPP band. GDI=RemapYellow (identity/gold), Nod
  (BadGuy) we map to **RemapRed** (source default is actually Blue — we chose
  red per the GDI-gold/Nod-red convention; one-line switch in `house.cpp`).
  Most TD building art is pre-colored, so house color is a small accent — normal.
- TD `mouse.shp` is NOT ShpD2 format → cursor load throws, falls back to OS
  cursor (harmless). A TD-cursor decoder is a nice-to-have.
- Houses: GoodGuy=GDI, BadGuy=Nod, Neutral=civilian. Side for owner-filtering:
  GoodGuy→gdi, BadGuy→nod (`game_main.cpp`).

## Verification recipe

```
# Headless movement + combat (should path the MCV and damage the Nod e1):
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy --no-shroud --sim-ticks 400 ^
  --attack 2,6 --dump out.bmp --scale 2
# Expect: unit 2 (GDI e1) ends "(attacking)" near unit 6; unit 6 hp 50 -> ~35.

# Visual set (writes renders/*.png):  python tools/render_td.py
# Interactive:  play                (or:  play scb03ea BadGuy 5000)
```

## Context handoff protocol

At ~75% context: tick MILESTONES.md, add a session-log entry, rewrite this file
for the next session, commit and push.
