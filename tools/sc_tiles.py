#!/usr/bin/env python3
"""Decode a StarCraft tileset (cv5 / vx4|vx4ex / vr4 / wpe) and render megatiles.

StarCraft terrain is built from 8x8 "minitiles" (VR4) assembled 4x4 into 32x32
"megatiles" (VX4/VX4EX), grouped into terrain "tile groups" (CV5). Map cells
reference a 16-bit tile id = (group << 4) | subtile, so the megatile for a cell
is cv5[group].megatiles[subtile].

This tool is for eyeballing the raw art and validating the decode. Usage:
  python tools/sc_tiles.py badlands                 # dump first N megatiles
  python tools/sc_tiles.py jungle --groups 0-40     # dump cv5 groups (16 wide)
"""
import argparse
import os
import struct
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSDIR = os.path.join(ROOT, "data/assets/starcraft/tileset/TileSet")
OUTDIR = os.path.join(ROOT, "renders/sc")

CV5_ENTRY = 52          # bytes per cv5 tile group
CV5_MEGATILES = 16      # megatile variations per group (last 32 bytes = 16 u16)
MINI = 8                # minitile edge in px
MEGA = 32               # megatile edge in px


class Tileset:
    def __init__(self, name):
        self.name = name
        p = os.path.join(TSDIR, name)
        self.pal = self._wpe(p + ".wpe")
        self.vr4 = open(p + ".vr4", "rb").read()          # 64 B/minitile
        vx4ex = p + ".vx4ex"
        if os.path.exists(vx4ex):
            self.vx4, self.vx4_stride = open(vx4ex, "rb").read(), 4   # u32/ref
        else:
            self.vx4, self.vx4_stride = open(p + ".vx4", "rb").read(), 2  # u16/ref
        self.cv5 = open(p + ".cv5", "rb").read()
        self.n_groups = len(self.cv5) // CV5_ENTRY

    def _wpe(self, path):
        raw = open(path, "rb").read()
        return [tuple(raw[i * 4:i * 4 + 3]) for i in range(256)]  # (r,g,b), 4th unused

    def minitile(self, idx, hflip):
        """Return list of 64 (r,g,b), row-major, optionally horizontally flipped."""
        off = idx * 64
        px = self.vr4[off:off + 64]
        out = []
        for y in range(8):
            row = px[y * 8:y * 8 + 8]
            if hflip:
                row = row[::-1]
            out.extend(self.pal[b] for b in row)
        return out

    def megatile(self, mega_id):
        """Render megatile -> 32x32 PIL image."""
        img = Image.new("RGB", (MEGA, MEGA))
        base = mega_id * 16 * self.vx4_stride
        for i in range(16):
            o = base + i * self.vx4_stride
            ref = (struct.unpack_from("<I", self.vx4, o)[0] if self.vx4_stride == 4
                   else struct.unpack_from("<H", self.vx4, o)[0])
            mini_idx, hflip = ref >> 1, ref & 1
            mt = self.minitile(mini_idx, hflip)
            mx, my = (i % 4) * MINI, (i // 4) * MINI
            for y in range(8):
                for x in range(8):
                    img.putpixel((mx + x, my + y), mt[y * 8 + x])
        return img

    def group_megatiles(self, group):
        """The 16 VX4 megatile ids of a cv5 group."""
        base = group * CV5_ENTRY + 20  # first 20 bytes are metadata
        return list(struct.unpack_from("<16H", self.cv5, base))

    def group_meta(self, group):
        base = group * CV5_ENTRY
        # index, buildability/height, then 4 edge ids, then 4 unknowns (u16 each)
        vals = struct.unpack_from("<10H", self.cv5, base)
        return vals


def sheet(images, cols, pad=1, bg=(20, 20, 28)):
    if not images:
        return Image.new("RGB", (cols * MEGA, MEGA), bg)
    rows = (len(images) + cols - 1) // cols
    W = cols * (MEGA + pad) + pad
    H = rows * (MEGA + pad) + pad
    out = Image.new("RGB", (W, H), bg)
    for i, im in enumerate(images):
        x = pad + (i % cols) * (MEGA + pad)
        y = pad + (i // cols) * (MEGA + pad)
        out.paste(im, (x, y))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tileset")
    ap.add_argument("--count", type=int, default=512, help="megatiles to dump")
    ap.add_argument("--groups", help="dump cv5 groups a-b instead (16 megatiles/row)")
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    ts = Tileset(args.tileset)
    os.makedirs(OUTDIR, exist_ok=True)
    print(f"{ts.name}: {ts.n_groups} groups, "
          f"vx4 stride {ts.vx4_stride}, {len(ts.vr4)//64} minitiles")

    if args.groups:
        a, b = (int(x) for x in args.groups.split("-"))
        imgs = []
        for g in range(a, min(b + 1, ts.n_groups)):
            for m in ts.group_megatiles(g):
                imgs.append(ts.megatile(m))
        out = sheet(imgs, cols=16)
        tag = f"{ts.name}_groups{a}-{b}"
    else:
        imgs = [ts.megatile(m) for m in range(min(args.count, len(ts.vx4)//(16*ts.vx4_stride)))]
        out = sheet(imgs, cols=32)
        tag = f"{ts.name}_mega0-{len(imgs)}"

    if args.scale != 1:
        out = out.resize((out.width * args.scale, out.height * args.scale), Image.NEAREST)
    path = os.path.join(OUTDIR, tag + ".png")
    out.save(path)
    print("wrote", path, out.size)


if __name__ == "__main__":
    main()
