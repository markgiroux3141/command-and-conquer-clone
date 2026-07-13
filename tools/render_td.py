#!/usr/bin/env python3
"""Render Tiberian Dawn missions to PNG images in renders/ for eyeballing.

Runs the built mapview on a curated set of TD scenarios (one per theater by
default), dumping each to a BMP then converting to PNG with ImageMagick. Output
lives in renders/ which is gitignored (the images are derived from copyrighted
game art -- personal use only, never commit).

Usage:
  python tools/render_td.py                      # default mission set
  python tools/render_td.py gdi/GENERAL/scg08ea  # specific scenario(s)
Options:
  --root DIR    game data root (default data/assets/tiberian_dawn/<disc>)
  --scale N     pixel scale passed to mapview (default 2)
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPVIEW = os.path.join(ROOT, "build/Release/mapview.exe")
ASSETS = os.path.join(ROOT, "data/assets/tiberian_dawn")
OUTDIR = os.path.join(ROOT, "renders")

# Default showcase set: (disc, scenario stem, output name). One per theater.
DEFAULT = [
    ("gdi", "GENERAL/scg01ea", "td_temperate_gdi-mission-1"),
    ("nod", "GENERAL/scb03ea", "td_desert_nod-base"),
    ("gdi", "GENERAL/scg08ea", "td_winter_front"),
]


def render(disc, stem, outname, scale):
    ini = os.path.join(ASSETS, disc, stem + ".ini")
    root = os.path.join(ASSETS, disc)
    if not os.path.exists(ini):
        print(f"  SKIP {outname}: no {ini}", file=sys.stderr)
        return False
    bmp = os.path.join(OUTDIR, outname + ".bmp")
    png = os.path.join(OUTDIR, outname + ".png")
    r = subprocess.run([MAPVIEW, ini, root, "--dump", bmp, "--scale", str(scale)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"  FAIL {outname}: {r.stderr.strip()}", file=sys.stderr)
        return False
    subprocess.run(["magick", bmp, png], check=True)
    os.remove(bmp)
    print(f"  {png}")
    return True


def main():
    args = sys.argv[1:]
    scale = 2
    if "--scale" in args:
        i = args.index("--scale")
        scale = int(args[i + 1])
        del args[i:i + 2]
    if "--root" in args:
        i = args.index("--root")
        del args[i:i + 2]  # (reserved; default disc dirs used below)

    os.makedirs(OUTDIR, exist_ok=True)
    if not os.path.exists(MAPVIEW):
        sys.exit(f"mapview not built: {MAPVIEW} (run cmake --build build --config Release)")

    jobs = DEFAULT
    if args:
        # Each positional arg is "<disc>/<path/stem>"; name it after the stem.
        jobs = []
        for a in args:
            disc, _, stem = a.replace("\\", "/").partition("/")
            jobs.append((disc, stem, os.path.basename(stem)))

    print(f"rendering {len(jobs)} mission(s) -> renders/")
    ok = sum(render(d, s, n, scale) for d, s, n in jobs)
    print(f"done: {ok}/{len(jobs)}")


if __name__ == "__main__":
    main()
