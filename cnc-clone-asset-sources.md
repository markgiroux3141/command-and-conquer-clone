# Command & Conquer — Asset & Source Reference (for a personal-use clone)

> **Purpose of this doc:** context pack to drop into a Claude Code session. It catalogs where the assets, source code, file formats, and reference engines live for the classic *Command & Conquer* games, so you can build a personal-use clone. Scope is the classic 2D Westwood titles (Tiberian Dawn / Red Alert 1) unless noted.

---

## 0. TL;DR / recommended stack

- **For classic 2D assets:** download the official **freeware** Tiberian Dawn + Red Alert releases.
- **For HD assets:** extract from the **Remastered Collection** (paid, ~$20) if you own it.
- **For game logic:** read the **EA GPL source** and/or the **OpenRA** engine.
- **To unpack the files:** use **XCC Utilities** (classic MIX/SHP/AUD/VQA) or **Bibber's C&C Asset Extractor** (later W3D-engine games).
- **Fastest path to a playable clone:** build on or heavily study **OpenRA** (C#, cross-platform, GPL3).

---

## 1. Licensing model — read this first

C&C has **two separate licensing tracks**. Do not conflate them:

| Thing | License | What it means for you |
|---|---|---|
| **Source code** (Tiberian Dawn, Red Alert, Renegade, Generals/ZH, Remaster) | GPL v3 | Free to run/study/modify. **Code only** — not the art/audio. |
| **Game data** (art, audio, music, FMV) | Copyrighted | "Freeware" releases are free to *download and play*, **not** to redistribute. Fine for personal use. |
| **Trademarks** ("Command & Conquer", faction names, logos) | Reserved by EA | GPL release grants **no** trademark/publicity rights. Don't ship publicly under the C&C name. |

**Bottom line for a personal clone:** all of the below is usable for a project you keep to yourself. The moment you distribute, the asset copyright and trademark restrictions become the constraint — plan to swap in your own art/names before any public release.

---

## 2. Asset sources (ordered: grab-directly → extract-yourself)

### 2.1 Full games as freeware — original classic assets
EA released the classic games free between 2007–2010. The official pages closed in 2011; community mirrors host them with modern-OS install instructions.
- **CNCNZ freeware hub:** https://cncnz.com/features/freeware-classic-command-conquer-games/
- **C&C Wiki download page:** https://cnc.fandom.com/wiki/Command_and_Conquer_Wiki:Tiberian_Dawn_and_Red_Alert_1_downloads
- Covers: Tiberian Dawn + Covert Ops, Red Alert + expansions (and Tiberian Sun).
- Contains: sprite art, audio, music, FMV. **Cleanest legitimate source for the classic 2D asset set.**

### 2.2 Remastered HD assets (4K) — highest quality
Part of the **paid** Remastered Collection (~$20). Assets redone in 4K by Lemon Sky Studios; soundtrack remastered by Frank Klepacki; EVA lines re-recorded.
- **Steam:** https://store.steampowered.com/app/1213210/Command__Conquer_Remastered_Collection/
- Not freeware — you must own it. If you do, the HD art/audio can be extracted for personal use.
- OpenRA's **Tiberian Dawn HD** mod already supports switching between remastered and classic assets, i.e. there's a proven pipeline for feeding these into a clone engine.

### 2.3 CnCNet — community distributions & later titles
- https://cncnet.org/download — playable classic/community builds and mods (useful as reference, not primarily an asset source).

---

## 3. Source code (GPL v3)

Official EA repositories (code only; binaries still need original game data to run):
- **EA GitHub org:** https://github.com/electronicarts
- Tiberian Dawn: https://github.com/electronicarts/CnC_Tiberian_Dawn
- Also released: Red Alert, Renegade, Generals + Zero Hour (same org; announced Feb 2025).
- Remaster source (C# + original engine) released 2020, also GPL v3.
- Original code is C++, Assembly, and C; historic build tooling references Watcom C/C++ and Borland Turbo Assembler. Treat it as **reference**, not something to compile as-is on a modern toolchain.

**Announcement (context/terms):** https://www.ea.com/games/command-and-conquer/command-and-conquer-remastered/news/steam-workshop-support

---

## 4. File formats you'll be reading

Classic Westwood engine packs everything into archives:

| Format | Contents | Notes |
|---|---|---|
| `.MIX` | Archive container | Top-level bundle; unpack first |
| `.SHP` | Sprite sheets | Units/structures/animations; paired with a palette |
| `.PAL` | Palette | 256-color palettes; needed to render SHP correctly |
| `.AUD` | Audio | SFX and voice lines |
| `.VQA` | Video | FMV cutscenes (Westwood codec) |
| `.INI` | Rules/config | Unit stats, mission scripting, map data (great for logic reverse-engineering) |

Later W3D-engine games (Tiberium Wars → Tiberian Twilight) use `.W3D` models, streamed textures, and `.BIG`/stream archives instead.

**Format documentation:** the ModdingWiki (shikadi.net) has detailed write-ups of many of these formats. OpenRA's source is also a de-facto spec for how they're parsed.

---

## 5. Extraction tools

- **XCC Utilities** — the long-standing toolset for classic C&C formats (Dune 2000, Tiberian Dawn, Red Alert 1/2, Tiberian Sun, Renegade, Generals). Unpacks MIX, converts SHP/AUD/VQA/PAL. Search "XCC Utilities" via the C&C community (CNCNZ / ppmforums / CnCNet).
- **Bibber's C&C Asset Extractor** — for the later W3D-engine titles; extracts models, textures, sounds/music.
  - https://bibber.eu/downloads/cnc-asset-extractor/
- **OpenRA** — not a standalone extractor, but its asset-loading code reads the original formats directly and is the best working reference.

---

## 6. Reference engines / prior art

- **OpenRA** — open-source RTS engine (C#, SDL, OpenGL; Windows/Linux/BSD/macOS), GPL3. Reimplements Tiberian Dawn, Red Alert, Dune 2000. Ships a Mod SDK + modding guide.
  - Site: https://www.openra.net/
  - Code: https://github.com/openra/openra
  - **This is your best starting point** — either build on it or study its architecture (asset loading, trait system, YAML rules, Lua mission scripting).

---

## 7. Suggested build approach

1. **Decide the target:** classic pixel look (freeware assets) vs HD look (Remaster assets you own).
2. **Acquire assets** per §2. Keep them in a `data/` dir outside version control.
3. **Unpack** MIX archives with XCC Utilities → extract SHP/PAL/AUD/VQA + the INI rules files.
4. **Study the rules:** the `.INI` files define unit stats, costs, build trees, and mission logic — the fastest way to understand game balance without reading C++.
5. **Pick your engine path:**
   - *Fastest:* fork/mod **OpenRA** and treat this as a mod project.
   - *From scratch:* pick a framework (e.g. a 2D engine or your language of choice), write a MIX/SHP/PAL loader first, get a single unit rendering, then build out the tile/terrain renderer.
6. **Reference the GPL source** for exact behavior (pathfinding, targeting, harvesting, tiberium spread) when your reimplementation needs to match the original.
7. **Before any public release:** replace copyrighted art/audio with your own and drop all C&C trademarks/names.

---

## 8. Quick link index

- Freeware games: https://cncnz.com/features/freeware-classic-command-conquer-games/
- C&C Wiki downloads: https://cnc.fandom.com/wiki/Command_and_Conquer_Wiki:Tiberian_Dawn_and_Red_Alert_1_downloads
- Remastered Collection: https://store.steampowered.com/app/1213210/Command__Conquer_Remastered_Collection/
- EA source (org): https://github.com/electronicarts
- EA source (Tiberian Dawn): https://github.com/electronicarts/CnC_Tiberian_Dawn
- OpenRA site: https://www.openra.net/
- OpenRA code: https://github.com/openra/openra
- CnCNet: https://cncnet.org/download
- Bibber Asset Extractor: https://bibber.eu/downloads/cnc-asset-extractor/

---

*Notes: verify each link at build time — community hosting and repos move. Licensing summary here is a practical orientation, not legal advice; the constraints that matter (asset copyright, trademarks) only bite on distribution, not on a private personal build.*
