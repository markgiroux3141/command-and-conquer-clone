#!/usr/bin/env python3
"""Build a browsable index.html for the terrain renders in renders/levels/.

Groups each level by faction (GDI/NOD/other) and theater, showing the plain and
grid renders side by side. Open renders/levels/index.html in a browser.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "data/assets/tiberian_dawn")
OUTDIR = os.path.join(ROOT, "renders/levels")

FACTION = {"scg": "GDI", "scb": "NOD", "scj": "NOD (jungle)", "scm": "Multiplayer"}


def theater_of(stem):
    for disc in ("gdi", "nod"):
        ini = os.path.join(ASSETS, disc, "GENERAL", stem + ".ini")
        if os.path.exists(ini):
            with open(ini, "r", errors="ignore") as f:
                m = re.search(r"^Theater=(\w+)", f.read(), re.M | re.I)
                return m.group(1).upper() if m else "?"
    return "?"


def main():
    stems = sorted(
        os.path.splitext(f)[0]
        for f in os.listdir(OUTDIR)
        if f.endswith(".png") and not f.endswith("_grid.png")
    )
    rows = []
    for s in stems:
        fac = FACTION.get(s[:3].lower(), "Other")
        rows.append((fac, theater_of(s), s))

    order = ["GDI", "NOD", "NOD (jungle)", "Multiplayer", "Other"]
    rows.sort(key=lambda r: (order.index(r[0]) if r[0] in order else 9, r[2]))

    html = [
        "<!doctype html><meta charset=utf-8><title>TD level terrain renders</title>",
        "<style>",
        "body{background:#111;color:#ddd;font:14px system-ui,sans-serif;margin:24px}",
        "h1{color:#fc0}h2{color:#8cf;border-bottom:1px solid #333;margin-top:36px}",
        ".lvl{margin:22px 0}.lvl h3{margin:0 0 6px;color:#fff;font-size:15px}",
        ".lvl .meta{color:#89a;font-weight:normal;font-size:12px;margin-left:8px}",
        ".pair{display:flex;gap:12px;flex-wrap:wrap}",
        ".pair figure{margin:0}.pair figcaption{color:#789;font-size:11px;margin:2px}",
        "img{max-width:640px;width:100%;height:auto;border:1px solid #333;",
        "image-rendering:pixelated;background:#000}",
        "a{color:#8cf}",
        "</style>",
        "<h1>Tiberian Dawn — level terrain renders</h1>",
        f"<p>{len(stems)} maps. Terrain only (units/structures omitted). "
        "Left: plain · Right: cell grid with coordinates.</p>",
    ]
    cur = None
    for fac, th, s in rows:
        if fac != cur:
            html.append(f"<h2>{fac}</h2>")
            cur = fac
        html.append(
            f"<div class=lvl><h3>{s}<span class=meta>{th}</span></h3><div class=pair>"
            f"<figure><a href='{s}.png'><img src='{s}.png' loading=lazy></a>"
            f"<figcaption>plain</figcaption></figure>"
            f"<figure><a href='{s}_grid.png'><img src='{s}_grid.png' loading=lazy></a>"
            f"<figcaption>grid</figcaption></figure>"
            f"</div></div>"
        )
    with open(os.path.join(OUTDIR, "index.html"), "w", encoding="utf-8") as f:
        f.write("\n".join(html))
    print("wrote", os.path.join(OUTDIR, "index.html"), f"({len(stems)} maps)")


if __name__ == "__main__":
    main()
