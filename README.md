# Command & Conquer Clone (personal project)

A from-scratch C++/SDL2 reimplementation targeting Red Alert first. See
[cnc-clone-asset-sources.md](cnc-clone-asset-sources.md) for the plan and
[ASSETS.md](ASSETS.md) for where the game data lives (all gitignored — personal use only).

## Build

Requires CMake + MSVC. SDL2 lives in `tools/SDL2` (gitignored); on a fresh
clone, fetch it first:

```
curl -L -o sdl2.zip https://github.com/libsdl-org/SDL/releases/download/release-2.32.8/SDL2-devel-2.32.8-VC.zip
tar -xf sdl2.zip -C tools && ren tools\SDL2-2.32.8 SDL2
```

Then:

```
cmake -S . -B build
cmake --build build --config Release
```

## License

Code is GPL v3 (see LICENSE): the format decoders are ports of OpenRA's GPL v3
implementations. Game assets are **not** included and are not redistributable —
see [ASSETS.md](ASSETS.md) and the licensing notes in
[cnc-clone-asset-sources.md](cnc-clone-asset-sources.md).

## Tools

### shpview — sprite viewer (first milestone)

```
build\Release\shpview.exe <file.shp> <file.pal> [--scale N] [--fps N]
```

Example — animate the Mammoth Tank:

```
build\Release\shpview.exe data\assets\red_alert\allied\MAIN\conquer\4tnk.shp ^
    data\assets\red_alert\allied\INSTALL\REDALERT\local\temperat.pal
```

Controls: left/right step frames, space pause, +/- zoom, Esc quit.
`--dump out.bmp [--frame N]` renders one frame to BMP headlessly instead.

### Other format tools

```
tmpview <file.tem> <file.pal> [--cols N] [--scale N] [--dump out.bmp]   terrain template grid
iniquery <file.ini> [section] [key]                                     inspect rules/missions
auddump <file.aud> <out.wav>                                            decode audio to WAV
```

Sample decoded WAVs (EVA, Tanya, Hell March) are in `data/wav-samples/`.

Notes: unit SHPs pack all rotations as frames (e.g. `4tnk.shp` = 32 hull facings
+ 32 turret facings). Bright green pixels are the shadow index (drawn translucent
by the real game); palette indices 80–95 are the house-color remap range. Both are
applied at render time, not decode time.

## Source layout

- `src/formats/` — Westwood file format decoders (PAL, SHP; next: TMP terrain, AUD audio, INI rules)
- `src/tools/` — asset viewers / debug utilities
- `tools/` — asset pipeline scripts (Python MIX extractor etc.)
- `reference/` — OpenRA + EA GPL sources (cloned, read-only reference)
