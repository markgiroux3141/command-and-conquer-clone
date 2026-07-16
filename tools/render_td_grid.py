#!/usr/bin/env python3
"""Batch-render every Tiberian Dawn campaign map's TERRAIN to PNGs for study.

For each scenario in gdi/GENERAL and nod/GENERAL this runs the built mapedit in
its headless --render mode (terrain + overlays + trees/rocks, cropped to
content, units/structures omitted so coastlines are visible) and produces two
images per map:

  renders/levels/<stem>.png        plain terrain
  renders/levels/<stem>_grid.png   with a cell grid + coordinate labels

Output lives in renders/ which is gitignored (derived from copyrighted game art,
personal use only, never commit).

Usage:
  python tools/render_td_grid.py            # all campaign maps
  python tools/render_td_grid.py scg01ea    # only matching stems (substring)
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPEDIT = os.path.join(ROOT, "build/Release/mapedit.exe")
ASSETS = os.path.join(ROOT, "data/assets/tiberian_dawn")
OUTDIR = os.path.join(ROOT, "renders/levels")


def collect():
    """stem -> (data_root, ini_path); gdi wins when a stem exists on both discs."""
    maps = {}
    for disc in ("nod", "gdi"):  # gdi last so it takes precedence
        gen = os.path.join(ASSETS, disc, "GENERAL")
        if not os.path.isdir(gen):
            continue
        for f in os.listdir(gen):
            if f.lower().endswith(".ini") and f.lower().startswith("sc"):
                stem = os.path.splitext(f)[0]
                maps[stem] = (os.path.join(ASSETS, disc), os.path.join(gen, f))
    return dict(sorted(maps.items()))


def render(stem, root, ini, grid):
    out_png = os.path.join(OUTDIR, stem + ("_grid" if grid else "") + ".png")
    out_bmp = out_png[:-4] + ".bmp"
    cmd = [MAPEDIT, root, "--open", ini, "--render", out_bmp]
    if grid:
        cmd.append("--grid")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out_bmp):
        print(f"  FAIL {stem}{'(grid)' if grid else ''}: {r.stderr.strip()}",
              file=sys.stderr)
        return False
    subprocess.run(["magick", out_bmp, out_png], check=True)
    os.remove(out_bmp)
    return True


def main():
    filt = sys.argv[1:]
    os.makedirs(OUTDIR, exist_ok=True)
    maps = collect()
    if filt:
        maps = {s: v for s, v in maps.items() if any(f in s for f in filt)}
    print(f"rendering {len(maps)} maps -> {OUTDIR}")
    ok = 0
    for stem, (root, ini) in maps.items():
        a = render(stem, root, ini, grid=False)
        b = render(stem, root, ini, grid=True)
        if a and b:
            ok += 1
            print(f"  {stem}")
    print(f"done: {ok}/{len(maps)} maps ({ok * 2} images)")


if __name__ == "__main__":
    main()
