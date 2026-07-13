# Handoff — session 7 → session 8 (written 2026-07-13)

Session 7 closed the three top gameplay gaps on the Tiberian Dawn build:
**enemy auto-acquire (return fire)**, **TD passability**, and **audio (SFX +
music jukebox)**. All build clean and the headless sim is unchanged/deterministic.

## Read first

1. `MILESTONES.md` — Phase 8 (audio) + Phase 10 (TD) checklists and the
   session-7 log entry (top of the log).
2. `README.md` / `play.bat` — how to run.
3. Then source, only as needed for the next task:
   - `src/game/sim.{h,cpp}` — movement/combat/harvest/production. New this
     session: `tickAutoAcquire`, `Unit::autoTarget`, `Event::Fire`.
   - `src/game/audio.{h,cpp}` — the SDL mixer (new).
   - `src/game_main.cpp` — the shell. Audio is wired here (`mixer`, the SFX
     switch in `processEvents`, the jukebox in the render loop). TD support is
     still the `isTd` branch through setup/bake/build-lists.

## Current state (what compiles / plays)

- Builds clean: `cmake --build build --config Release` (MSVC). `play.bat` runs
  GDI mission 1 by default (auto-detects disc from map name).
- **Auto-acquire**: idle armed units fire at the nearest enemy in weapon range
  without orders (both RA + TD; `housesEnemy` = different house, neither
  Neutral). Guarding units hold their post; a *player*-ordered attack still
  chases; a move order cancels the attack.
- **TD passability**: ground units no longer cross water/rock — they clamp to
  the shore. Per-cell land comes from the template table (`land` + per-icon
  `altLand`/`altIcons`), baked in `bakeTerrainCell`.
- **Audio (Phase 8 complete)**: combat SFX (weapon fire via per-weapon
  `Report=`, impacts, deaths, building crumble) + a SCORES music jukebox + EVA
  computer lines and unit voice acknowledgements (select/move/attack/build).
  Silent+no-op if no audio device.
- Still a sandbox: no win/lose, no AI/base-building, no mission triggers.
  Gunboat is immobile (naval needs a contiguous water region).

## Next tasks (suggested order)

1. **Win/lose + a beatable mission** (Phase 7 start): simplest is
   destroy-all-structures / lose-when-you-have-none; then read the mission INI
   `[Triggers]`/`[TeamTypes]` for real objectives. Pairs well with a minimal
   skirmish AI (build order + attack waves) so the placed-but-passive enemy
   actually does something beyond return fire.
2. **Polish**: TD tiberium density/adjacency frames (currently frame 0 only) +
   real harvest density; gunboat/naval water pathing; muzzle-flash anims; sound
   fade/pan by on-screen distance; more EVA cues (low power, base under attack).

## Gotchas discovered this session (not all in code comments)

- **Auto-acquire is range-gated, not chase**: `autoTarget` units drop the
  target the instant it leaves weapon range (see the `dist > w->range` branch in
  `tickCombat`) so idle armies don't wander. Player orders set `autoTarget=false`
  → normal chase. A unit *moving* under orders does NOT auto-fire (correct: plain
  move ≠ attack-move) — this is why a unit run through a gauntlet dies without
  shooting back until it goes idle.
- **TD land = template table, not the TMP** (RA reads the TMP control map; TD
  TMPs have none). Generator parses `TemplateTypeClass` ctor arg 5 (`Land`) and
  arg 8 (`AltLand`) + the `_slope*` `AltIcons` exception lists; TD `LandType`
  0-6 == `game::Land` 0-6 (Tiberium→Ore). CELL.CPP `Land_Type()` = "icon in
  AltIcons → AltLand, else Land". Overlays (tiberium/walls) override land after
  the terrain bake, so ordering is fine.
- **Audio device format**: opened 22050 Hz mono S16; the device may pick another
  rate (`have.freq`) so everything resamples to `rate_`. AUD format 1 = 8-bit
  unsigned (→ `(s-128)<<8`), format 99 = 16-bit. Mixing is guarded by
  `SDL_LockAudioDevice`; cache entries are never freed so `Voice` pcm pointers
  stay valid. Music plays at 3/8 volume under SFX.
- **`Event::Fire` is new** — any code iterating `sim.events()` must handle it
  (the shell `continue`s after playing the report; it has no explosion anim).
- **Some SFX aren't on the base GDI disc** (toss/hvygun10/turrfir5/gun5); the
  `Report=` values were swapped for present equivalents (bazook1/tnkfire6/
  tnkfire3/gun8). Nod disc may differ — missing files just play silent.
- SFX/music dirs are `<root>/SOUNDS` and `<root>/SCORES` (TD layout). RA would
  need different paths (degrades silent).
- **EVA speech is Covert-Ops-only** — `<root>/../covert_ops/AUD1/SPEECH/*.aud`
  (base gdi/nod discs omit it, same as the cameos). Unit voice responses ARE on
  the base disc but use the `.v00-.v03` extension, not `.aud` (loaded via
  `AudioMixer::playVoice`). EVA + unit voices share ONE speech channel
  (`speech_`, newest-wins) so lines never overlap; SFX use the many-voice pool.
  New EVA/voice cues go in the interactive command handlers in `game_main.cpp`
  (the `mixer.playEva`/`playVoice` calls) — headless never opens the device.

## Verification recipe

```
# Auto-acquire (headless, both directions): move a GDI e1 to its post in range
# of Nod e1 with NO attack order; it auto-fires (Nod e1 50->35) and the idle Nod
# mob returns fire and kills it:
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy --no-shroud --sim-ticks 240 --move 2,57,48
# Expect: "unit 6 ... hp 35/50" and unit 2 died ~tick 197.

# Passability: order the (weaponless) MCV onto the boat's water cell; it stops
# at the shore instead of driving onto water:
build\Release\game.exe data\assets\tiberian_dawn\gdi\GENERAL\scg01ea.ini ^
  data\assets\tiberian_dawn\gdi --house GoodGuy --no-shroud --sim-ticks 300 --move 0,53,59
# Expect: "unit 0 mcv ... cell 51,57" (clamped to land), not 53,59.

# Audio: run interactively (play) and LISTEN — tank/gun fire, explosions on
# death, and background music from SCORES. Headless is always silent.
```

## Context handoff protocol

At ~75% context: tick MILESTONES.md, add a session-log entry, rewrite this file
for the next session, commit and push.
