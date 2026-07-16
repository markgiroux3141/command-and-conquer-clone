#!/usr/bin/env python3
"""Faithful StarCraft ISOM terrain port (validation prototype).

Ports the isometric terrain-brush algorithm from TheNitesWhoSay/IsomTerrain
(MIT) to Python so we can validate it before the C++ engine port. Lets you make
a blank map of a default terrain, paint terrain types with a diamond brush, and
compile the ISOM diamonds to concrete tile ids exactly as StarEdit does. Renders
the result via tools/sc_tiles.

Usage:  python tools/sc_isom.py            # runs the paint demo -> renders/sc/
"""
import os
import random
import struct
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sc_tiles import Tileset, OUTDIR
from PIL import Image

# ---- Link / LinkId constants (IsomApi.h) --------------------------------
NONE = 0
SOFT = 48                      # Link::SoftLinks
BL, TR, BR, TL, FR, FL, LH, RH = 49, 50, 51, 52, 53, 54, 55, 56
# LinkId special values (only match within same terrain type)
TRBL_NW, TRBL_SE, TLBR_NE, TLBR_SW = 255, 256, 257, 258
ONLY_SAME = 255                # LinkId::OnlyMatchSameType

# Quadrants
TLq, TRq, BRq, BLq = 0, 1, 2, 3
QUADS = (TLq, TRq, BRq, BLq)


def opposite_quadrant(i):
    return {TLq: BRq, TRq: BLq, BRq: TLq, BLq: TRq}[i]


# Sides
SL, ST, SR, SB = 0, 1, 2, 3
SIDES = (SL, ST, SR, SB)

# ProjectedQuadrant.at(q) -> (firstSide, secondSide, firstEdgeFlags, secondEdgeFlags)
PROJ = {
    TLq: (SR, SB, 0x0, 0x2),
    TRq: (SL, SB, 0x4, 0x6),
    BRq: (SL, ST, 0x8, 0xA),
    BLq: (ST, SR, 0xC, 0xE),
}
EDGE_MASK = 0xE


# ---- The 14 canonical shapes -------------------------------------------
# Each quadrant: (left, top, right, bottom, linkId, isStackTop). Unset dirs=NONE.
def q(left=NONE, top=NONE, right=NONE, bottom=NONE, linkId=NONE, stackTop=False):
    return {"left": left, "top": top, "right": right, "bottom": bottom,
            "linkId": linkId, "stackTop": stackTop}


def shape(tl=None, tr=None, br=None, bl=None):
    return {TLq: tl or q(), TRq: tr or q(), BRq: br or q(), BLq: bl or q()}


SHAPES = [
    # 0 edgeNorthWest
    shape(tr=q(right=BR, bottom=BR, linkId=TRBL_NW, stackTop=True),
          br=q(left=BR, top=BR),
          bl=q(right=BR, bottom=FR, linkId=TRBL_NW, stackTop=True)),
    # 1 edgeNorthEast
    shape(tl=q(left=BL, bottom=BL, linkId=TLBR_NE, stackTop=True),
          br=q(left=BL, bottom=FL, linkId=TLBR_NE, stackTop=True),
          bl=q(top=BL, right=BL)),
    # 2 edgeSouthEast
    shape(tl=q(right=TL, bottom=TL),
          tr=q(left=TL, top=FL, linkId=TRBL_SE),
          bl=q(left=TL, top=TL, linkId=TRBL_SE)),
    # 3 edgeSouthWest
    shape(tl=q(top=FR, right=TR, linkId=TLBR_SW),
          tr=q(left=TR, bottom=TR),
          br=q(top=TR, right=TR, linkId=TLBR_SW)),
    # 4 jutOutNorth
    shape(br=q(left=BL, bottom=BL, linkId=TLBR_NE, stackTop=True),
          bl=q(right=BR, bottom=BR, linkId=TRBL_NW, stackTop=True)),
    # 5 jutOutEast
    shape(tl=q(left=BL, bottom=FL, linkId=TLBR_NE, stackTop=True),
          bl=q(left=TL, top=FL, linkId=TRBL_SE)),
    # 6 jutOutSouth
    shape(tl=q(top=TR, right=TR, linkId=TLBR_SW),
          tr=q(left=TL, top=TL, linkId=TRBL_SE)),
    # 7 jutOutWest
    shape(tr=q(right=BR, bottom=FR, linkId=TRBL_NW, stackTop=True),
          br=q(top=FR, right=TR, linkId=TLBR_SW)),
    # 8 jutInEast
    shape(tl=q(top=FR, right=TR, linkId=TLBR_SW),
          tr=q(left=RH, bottom=RH),
          br=q(left=RH, top=RH),
          bl=q(right=BR, bottom=FR, linkId=TRBL_NW)),
    # 9 jutInWest
    shape(tl=q(right=LH, bottom=LH),
          tr=q(left=TL, top=FL, linkId=TRBL_SE),
          br=q(left=BL, bottom=FL, linkId=TLBR_NE),
          bl=q(top=LH, right=LH)),
    # 10 jutInNorth
    shape(tl=q(left=BL, bottom=BL, linkId=TLBR_NE, stackTop=True),
          tr=q(right=BR, bottom=BR, linkId=TRBL_NW, stackTop=True),
          br=q(left=BR, top=BR),
          bl=q(top=BL, right=BL)),
    # 11 jutInSouth
    shape(tl=q(right=TL, bottom=TL),
          tr=q(left=TR, bottom=TR),
          br=q(top=TR, right=TR, linkId=TLBR_SW),
          bl=q(left=TL, top=TL, linkId=TRBL_SE)),
    # 12 horizontal
    shape(tl=q(top=TR, right=TR, linkId=TLBR_SW),
          tr=q(left=TL, top=TL, linkId=TRBL_SE),
          br=q(left=BL, bottom=BL, linkId=TLBR_NE),
          bl=q(right=BR, bottom=BR, linkId=TRBL_NW)),
    # 13 vertical
    shape(tl=q(left=BL, bottom=FL, linkId=TLBR_NE),
          tr=q(right=BR, bottom=FR, linkId=TRBL_NW),
          br=q(top=FR, right=TR, linkId=TLBR_SW),
          bl=q(left=TL, top=FL, linkId=TRBL_SE)),
]
NUM_SHAPES = 14


def quad_matches(sq, links, noStackAbove):
    """ShapeQuadrant.matches: hard links must equal; soft links match any soft."""
    for k in ("left", "top", "right", "bottom"):
        lv, sv = links[k], sq[k]
        if not (lv == sv or (lv <= SOFT and sv <= SOFT)):
            return False
    return noStackAbove or not sq["stackTop"]


# ---- Badlands brush tables (IsomApi.h Brush::Badlands) -------------------
# terrainType index -> (isomValue, brushSortOrder, linkId, name)
BADLANDS_TT = {
    0: (10, -1, 0, ""), 1: (0, -1, 0, ""),
    2: (1, 0, 1, "Dirt"), 3: (2, 2, 2, "High Dirt"), 4: (9, 1, 4, "Mud"),
    5: (3, 3, 3, "Water"), 6: (4, 4, 5, "Grass"), 7: (7, 5, 6, "High Grass"),
    8: (0, -1, 0, ""), 9: (0, -1, 0, ""), 10: (0, -1, 0, ""),
    11: (0, -1, 0, ""), 12: (0, -1, 0, ""), 13: (0, -1, 0, ""),
    14: (5, 7, 9, "Asphalt"), 15: (6, 8, 10, "Rocky Ground"),
    16: (0, -1, 0, ""), 17: (0, -1, 0, ""), 18: (8, 6, 7, "Structure"),
    19: (0, -1, 0, ""), 20: (41, -1, 0, ""), 21: (69, -1, 0, ""),
    22: (111, -1, 0, ""), 23: (0, -1, 0, ""), 24: (0, -1, 0, ""),
    25: (0, -1, 0, ""), 26: (0, -1, 0, ""), 27: (83, -1, 0, ""),
    28: (55, -1, 0, ""), 29: (0, -1, 0, ""), 30: (0, -1, 0, ""),
    31: (97, -1, 0, ""), 32: (0, -1, 0, ""), 33: (0, -1, 0, ""),
    34: (13, -1, 0, ""), 35: (27, -1, 0, ""),
}
BADLANDS_MAP = [
    5, 35, 0,
    35, 5, 2, 20, 27, 28, 34, 22, 0,
    2, 34, 35, 20, 27, 28, 22, 0,
    34, 2, 3, 20, 21, 27, 28, 35, 22, 0,
    3, 34, 21, 0,
    6, 20, 0,
    20, 6, 2, 35, 34, 27, 28, 22, 0,
    14, 27, 31, 0,
    27, 14, 20, 2, 35, 34, 28, 22, 0,
    15, 28, 0,
    28, 15, 2, 34, 35, 20, 27, 22, 0,
    7, 21, 0,
    21, 7, 3, 34, 0,
    18, 31, 0,
    31, 18, 14, 0,
    4, 22, 0,
    22, 4, 2, 34, 35, 20, 27, 28, 0,
    0,
]


# ---- cv5 tile groups ----------------------------------------------------
class TileGroup:
    __slots__ = ("terrainType", "build", "height", "links", "stack", "megas")

    def __init__(self, raw):
        v = struct.unpack_from("<H BB HHHH HHHH 16H", raw)
        self.terrainType = v[0]
        self.build, self.height = v[1], v[2]
        self.links = {"left": v[3], "top": v[4], "right": v[5], "bottom": v[6]}
        self.stack = {"left": v[7], "top": v[8], "right": v[9], "bottom": v[10]}
        self.megas = v[11:27]


def load_tile_groups(cv5_bytes):
    n = len(cv5_bytes) // 52
    return [TileGroup(cv5_bytes[i * 52:(i + 1) * 52]) for i in range(n)]


# ---- ShapeLinks (an isomLink table entry) -------------------------------
def shapelinks(terrainType=0):
    # each quadrant carries only its two outer edges + a linkId
    return {
        "terrainType": terrainType,
        TLq: {"right": NONE, "bottom": NONE, "linkId": NONE},
        TRq: {"left": NONE, "bottom": NONE, "linkId": NONE},
        BRq: {"left": NONE, "top": NONE, "linkId": NONE},
        BLq: {"top": NONE, "right": NONE, "linkId": NONE},
    }


def sl_edge_link(sl, isomValue):
    return {
        0x0: sl[TLq]["right"], 0x2: sl[TLq]["bottom"],
        0x4: sl[TRq]["left"], 0x6: sl[TRq]["bottom"],
        0x8: sl[BRq]["left"], 0xA: sl[BRq]["top"],
        0xC: sl[BLq]["top"], 0xE: sl[BLq]["right"],
    }[isomValue & EDGE_MASK]


def sl_link_id(sl, quadrant):
    return sl[quadrant]["linkId"]


class Tiles:
    """Per-tileset ISOM tables (port of IsomApi Terrain_::Tiles + loadIsom)."""

    def __init__(self, tile_groups, tt_info, tt_map_compressed):
        self.groups = tile_groups
        self.tt = tt_info                          # index -> (isom,bsort,linkId,name)
        self.num_tt = len(tt_info)
        self.isomLinks = []                        # list[shapelinks]
        self.hashToGroup = {}
        self._build_terrain_type_map(tt_map_compressed)
        self._build_hash_map()
        self._generate_isom_links()

    def isom_value_of(self, terrainType):
        return self.tt[terrainType][0] if terrainType < self.num_tt else 0

    # populateTerrainTypeMap
    def _build_terrain_type_map(self, compressed):
        n = self.num_tt
        self.ttMap = [0] * (n * n)
        temp = [0] * (n * n)
        i = 0
        while compressed[i] != 0:
            row = compressed[i]
            i += 1
            j = n * row
            while compressed[i] != 0:
                temp[j] = compressed[i]
                i += 1
                j += 1
            i += 1  # skip the 0 terminator
        for src in range(n - 1, -1, -1):
            rowData = [0] * n
            stack = deque([src])
            self.ttMap[n * src + src] = src
            while stack:
                destRow = stack.popleft()
                start = src * n
                j = destRow * n
                while temp[j] != 0:
                    tempPath = temp[j]
                    if self.ttMap[start + tempPath] == 0:
                        nextVal = tempPath if rowData[destRow] == 0 else rowData[destRow]
                        stack.append(tempPath)
                        self.ttMap[start + tempPath] = nextVal
                        rowData[tempPath] = nextVal
                    j += 1

    # loadIsom hash map (hashToTileGroup)
    def _build_hash_map(self):
        for i in range(0, len(self.groups), 2):
            gl = self.groups[i].links
            l, t, r, b = gl["left"], gl["top"], gl["right"], gl["bottom"]
            h = (((l << 6 | t) << 6 | r) << 6 | b) << 6
            if l >= 48 or t >= 48 or r >= 48 or b >= 48:
                h |= self.groups[i].terrainType
            self.hashToGroup.setdefault(h, []).append(i)

    # generateIsomLinks
    def _generate_isom_links(self):
        total = min(1024, len(self.groups))
        by_type = [[] for _ in range(self.num_tt)]
        for i in range(0, total, 2):
            tt = self.groups[i].terrainType
            if tt > 0:
                by_type[tt].append(i)

        solid, other = [], []
        i = 1
        while i <= self.num_tt // 2:
            if self.tt[i][0] != 0:
                solid.append((i, self.tt[i][0], self.tt[i][2]))  # idx,isom,linkId
            i += 1
        while i < self.num_tt:
            if self.tt[i][0] != 0:
                other.append((i, self.tt[i][0]))
            i += 1
        solid.sort(key=lambda e: e[1])
        other.sort(key=lambda e: e[1])

        links = self.isomLinks
        for idx, isom, linkId in solid:
            while len(links) < isom:
                links.append(shapelinks())
            g = self.groups[by_type[idx][0]].links
            sl = shapelinks(idx)
            sl[TLq] = {"right": g["right"], "bottom": g["bottom"], "linkId": linkId}
            sl[TRq] = {"left": g["left"], "bottom": g["bottom"], "linkId": linkId}
            sl[BRq] = {"left": g["left"], "top": g["top"], "linkId": linkId}
            sl[BLq] = {"top": g["top"], "right": g["right"], "linkId": linkId}
            links.append(sl)

        total_solid = len(links)
        while len(links) < other[0][1]:
            links.append(shapelinks())

        for idx, isom in other:
            start = len(links)
            for _ in range(NUM_SHAPES):
                links.append(shapelinks(idx))
            group_idxs = by_type[idx]
            shapeGroups = [dict(tl=None, tr=None, br=None, bl=None)
                           for _ in range(NUM_SHAPES)]
            for gi in group_idxs:
                g = self.groups[gi]
                if not _is_shape_quadrant(g.links):
                    continue
                noStack = (g.stack["top"] == 0)
                for si in range(NUM_SHAPES):
                    sh = SHAPES[si]
                    sl = links[start + si]
                    if quad_matches(sh[TLq], g.links, noStack):
                        sl[TLq]["right"] = g.links["right"]
                        sl[TLq]["bottom"] = g.links["bottom"]
                        shapeGroups[si]["tl"] = gi
                    if quad_matches(sh[TRq], g.links, noStack):
                        sl[TRq]["left"] = g.links["left"]
                        sl[TRq]["bottom"] = g.links["bottom"]
                        shapeGroups[si]["tr"] = gi
                    if quad_matches(sh[BRq], g.links, noStack):
                        sl[BRq]["left"] = g.links["left"]
                        sl[BRq]["top"] = g.links["top"]
                        shapeGroups[si]["br"] = gi
                    if quad_matches(sh[BLq], g.links, noStack):
                        sl[BLq]["top"] = g.links["top"]
                        sl[BLq]["right"] = g.links["right"]
                        shapeGroups[si]["bl"] = gi
            self._post_process_shapes(links, start, shapeGroups, total_solid)

    def _post_process_shapes(self, links, start, sg, total_solid):
        def S(i):
            return links[start + i]
        g = self.groups
        # populateJutInEastWest
        if S(8)[TRq]["left"] == NONE and sg[1]["bl"] is not None:
            S(8)[TRq]["left"] = g[sg[1]["bl"]].links["left"]
            S(8)[TRq]["bottom"] = g[sg[1]["bl"]].links["bottom"]
            if sg[2]["tl"] is not None:
                S(8)[BRq]["left"] = g[sg[2]["tl"]].links["left"]
                S(8)[BRq]["top"] = g[sg[2]["tl"]].links["top"]
        if S(9)[TLq]["right"] == NONE and sg[0]["br"] is not None:
            S(9)[TLq]["right"] = g[sg[0]["br"]].links["right"]
            S(9)[TLq]["bottom"] = g[sg[0]["br"]].links["bottom"]
            if sg[3]["tr"] is not None:
                S(9)[BLq]["top"] = g[sg[3]["tr"]].links["top"]
                S(9)[BLq]["right"] = g[sg[3]["tr"]].links["right"]
        # populateEmptyQuadrantLinks
        S(0)[TLq]["right"] = S(0)[TRq]["left"]
        S(0)[TLq]["bottom"] = S(0)[BLq]["top"]
        S(1)[TRq]["left"] = S(1)[TLq]["right"]
        S(1)[TRq]["bottom"] = S(1)[BRq]["top"]
        S(2)[BRq]["left"] = S(2)[BLq]["right"]
        S(2)[BRq]["top"] = S(2)[TRq]["bottom"]
        S(3)[BLq]["top"] = S(3)[TLq]["bottom"]
        S(3)[BLq]["right"] = S(3)[BRq]["left"]
        S(4)[TLq]["bottom"] = S(4)[BLq]["top"]
        S(4)[TLq]["right"] = S(4)[TLq]["bottom"]
        S(4)[TRq]["bottom"] = S(4)[BRq]["top"]
        S(4)[TRq]["left"] = S(4)[TRq]["bottom"]
        fill = S(5)[TLq]["right"]
        S(5)[TRq]["left"] = fill
        S(5)[TRq]["bottom"] = fill
        S(5)[BRq]["left"] = fill
        S(5)[BRq]["top"] = fill
        S(6)[BRq]["top"] = S(6)[TRq]["bottom"]
        S(6)[BRq]["left"] = S(6)[BRq]["top"]
        S(6)[BLq]["top"] = S(6)[TLq]["bottom"]
        S(6)[BLq]["right"] = S(6)[BLq]["top"]
        fill = S(7)[TRq]["left"]
        S(7)[TLq]["right"] = fill
        S(7)[TLq]["bottom"] = fill
        S(7)[BLq]["right"] = fill
        S(7)[BLq]["top"] = fill
        # populateHardcodedLinkIds
        for si in range(NUM_SHAPES):
            for qq in QUADS:
                if SHAPES[si][qq]["linkId"] >= ONLY_SAME:
                    S(si)[qq]["linkId"] = SHAPES[si][qq]["linkId"]
        # populateLinkIdsToSolidBrushes
        outer_ref = self.groups[sg[0]["tr"]].links["left"] if sg[0]["tr"] is not None else None
        inner_ref = self.groups[sg[0]["br"]].links["right"] if sg[0]["br"] is not None else None
        for bi in range(total_solid):
            brushLink = links[bi][TLq]["right"]
            brushLinkId = links[bi][TLq]["linkId"]
            if outer_ref is not None and brushLink == outer_ref:
                self._fill_outer(links, start, brushLinkId)
            if inner_ref is not None and brushLink == inner_ref:
                self._fill_inner(links, start, brushLinkId)

    def _fill_outer(self, links, start, linkId):
        def S(i):
            return links[start + i]
        S(0)[TLq]["linkId"] = linkId
        S(1)[TRq]["linkId"] = linkId
        S(2)[BRq]["linkId"] = linkId
        S(3)[BLq]["linkId"] = linkId
        S(4)[TLq]["linkId"] = linkId
        S(4)[TRq]["linkId"] = linkId
        S(5)[TRq]["linkId"] = linkId
        S(5)[BRq]["linkId"] = linkId
        S(7)[TLq]["linkId"] = linkId
        S(7)[BLq]["linkId"] = linkId
        S(6)[BRq]["linkId"] = linkId
        S(6)[BLq]["linkId"] = linkId

    def _fill_inner(self, links, start, linkId):
        def S(i):
            return links[start + i]
        S(0)[BRq]["linkId"] = linkId
        S(1)[BLq]["linkId"] = linkId
        S(2)[TLq]["linkId"] = linkId
        S(3)[TRq]["linkId"] = linkId
        S(8)[TRq]["linkId"] = linkId
        S(8)[BRq]["linkId"] = linkId
        S(9)[TLq]["linkId"] = linkId
        S(9)[BLq]["linkId"] = linkId
        S(10)[BRq]["linkId"] = linkId
        S(10)[BLq]["linkId"] = linkId
        S(11)[TLq]["linkId"] = linkId
        S(11)[TRq]["linkId"] = linkId


def _is_shape_quadrant(links):
    vals = [links["left"], links["top"], links["right"], links["bottom"]]
    all_hard = all(v > SOFT for v in vals)
    no_hard = all(v <= SOFT for v in vals)
    return not all_hard and not no_hard


# ---- IsomRect / editor flags -------------------------------------------
MODIFIED = 0x0001
VISITED = 0x8000
CLEAR_ALL = 0x7FFE


# ---- ScMap: the map + ISOM edit/compile ---------------------------------
class ScMap:
    def __init__(self, tiles_data, tileW, tileH):
        self.T = tiles_data
        self.tileW, self.tileH = tileW, tileH
        self.isomW = tileW // 2 + 1
        self.isomH = tileH + 1
        # each isom rect = [left, top, right, bottom]
        self.isom = [[0, 0, 0, 0] for _ in range(self.isomW * self.isomH)]
        self.tiles = [0] * (tileW * tileH)
        self.changed = None
        self._reset_changed()

    # --- helpers ---
    def _rect(self, x, y):
        return self.isom[y * self.isomW + x]//1 and self.isom[y * self.isomW + x]

    def rect(self, x, y):
        return self.isom[y * self.isomW + x]

    def in_bounds(self, x, y):
        return 0 <= x < self.isomW and 0 <= y < self.isomH

    def _reset_changed(self):
        self.changed = [self.isomW, self.isomH, 0, 0]  # left, top, right, bottom

    def _expand(self, x, y):
        c = self.changed
        c[0] = min(c[0], x)
        c[1] = min(c[1], y)
        c[2] = max(c[2], x)
        c[3] = max(c[3], y)

    def set_all_changed(self):
        self.changed = [0, 0, self.isomW - 1, self.isomH - 1]

    # --- blank map fill ---
    def fill(self, terrainType):
        iv = (self.T.isom_value_of(terrainType) << 4) | MODIFIED
        for r in self.isom:
            r[0] = r[1] = r[2] = r[3] = iv
        self.set_all_changed()
        self.update_tiles()

    # --- ISOM diamond geometry ---
    @staticmethod
    def diamond_valid(x, y):
        return (x + y) % 2 == 0

    @staticmethod
    def neighbor(x, y, i):  # UL, UR, LR, LL
        return [(x - 1, y - 1), (x + 1, y - 1), (x + 1, y + 1), (x - 1, y + 1)][i]

    @staticmethod
    def rect_coords(x, y, quadrant):
        return {TLq: (x - 1, y - 1), TRq: (x, y - 1),
                BRq: (x, y), BLq: (x - 1, y)}[quadrant]

    def central_isom(self, x, y):
        return self.rect(x, y)[0] >> 4

    def central_modified(self, x, y):
        return bool(self.rect(x, y)[0] & MODIFIED)

    def diamond_needs_update(self, x, y):
        return (self.in_bounds(x, y) and not self.central_modified(x, y)
                and self.central_isom(x, y) != 0)

    # --- placing terrain ---
    def set_isom_value(self, rx, ry, quadrant, isomValue):
        if not self.in_bounds(rx, ry):
            return
        first, second, ef1, ef2 = PROJ[quadrant]
        r = self.rect(rx, ry)
        r[first] = (isomValue << 4) | ef1
        r[second] = (isomValue << 4) | ef2
        r[first] |= MODIFIED
        r[second] |= MODIFIED
        self._expand(rx, ry)

    def set_diamond(self, dx, dy, isomValue):
        for qq in QUADS:
            rx, ry = self.rect_coords(dx, dy, qq)
            self.set_isom_value(rx, ry, qq, isomValue)

    def place(self, dx, dy, terrainType, brushExtent):
        iv = self.T.isom_value_of(terrainType)
        if iv == 0 or not self.diamond_valid(dx, dy) or iv >= len(self.T.isomLinks) \
                or self.T.isomLinks[iv]["terrainType"] == 0:
            return False
        bmin = -(brushExtent // 2)
        bmax = bmin + brushExtent
        if brushExtent % 2 == 0:
            bmin += 1
            bmax += 1
        self._reset_changed()
        todo = deque()
        for ox in range(bmin, bmax):
            for oy in range(bmin, bmax):
                bx = dx + ox - oy
                by = dy + ox + oy
                if self.in_bounds(bx, by):
                    self.set_diamond(bx, by, iv)
                    if ox in (bmin, bmax - 1) or oy in (bmin, bmax - 1):
                        for i in range(4):
                            nx, ny = self.neighbor(bx, by, i)
                            if self.diamond_needs_update(nx, ny):
                                todo.append((nx, ny))
        self._radial_update(todo)
        return True

    # --- radial resolve (findBestMatch) ---
    def _load_neighbors(self, dx, dy):
        il = self.T.isomLinks
        nb = {}
        maxMod = 0
        for i in range(4):
            nx, ny = self.neighbor(dx, dy, i)
            info = {"linkId": NONE, "isomValue": 0, "modified": False}
            if self.in_bounds(nx, ny):
                iv = self.central_isom(nx, ny)
                info["modified"] = self.central_modified(nx, ny)
                info["isomValue"] = iv
                if iv < len(il):
                    info["linkId"] = sl_link_id(il[iv], opposite_quadrant(i))
                    if info["modified"] and il[iv]["terrainType"] > maxMod:
                        maxMod = il[iv]["terrainType"]
            nb[i] = info
        return nb, maxMod

    def _count_matches(self, sl, nb):
        tt = sl["terrainType"]
        total = 0
        il = self.T.isomLinks
        for qq in QUADS:  # quadrant index maps to neighbor index (TL=UL etc.)
            n = nb[qq]
            neighborShape = il[n["isomValue"]]
            nTT = neighborShape["terrainType"]
            nLink = n["linkId"]
            qLink = sl_link_id(sl, qq)
            if nLink == qLink and (qLink < ONLY_SAME or tt == nTT):
                total += 1
            elif n["modified"]:
                return 0
        return total

    def _search_best(self, startTT, nb, best):
        il = self.T.isomLinks
        searchUntilHigher = (startTT == self.T.num_tt // 2 + 1)
        searchUntilEnd = (startTT == 0)
        iv = self.T.isom_value_of(startTT)
        while iv < len(il):
            tt = il[iv]["terrainType"]
            if not searchUntilEnd and tt != startTT and (not searchUntilHigher or tt > startTT):
                break
            mc = self._count_matches(il[iv], nb)
            if mc > best["count"]:
                best["value"], best["count"] = iv, mc
            iv += 1

    def _best_match(self, dx, dy):
        nb, maxMod = self._load_neighbors(dx, dy)
        best = {"value": 0, "count": 0}
        prev = self.central_isom(dx, dy)
        il = self.T.isomLinks
        if prev < len(il):
            prevTT = il[prev]["terrainType"]
            mapped = self.T.ttMap[maxMod * self.T.num_tt + prevTT]
            self._search_best(mapped, nb, best)
        self._search_best(maxMod, nb, best)
        self._search_best(self.T.num_tt // 2 + 1, nb, best)
        if best["value"] == prev:
            return None
        return best["value"]

    def _radial_update(self, todo):
        while todo:
            dx, dy = todo.popleft()
            if self.diamond_needs_update(dx, dy) and not (self.rect(dx, dy)[2] & VISITED):
                self.rect(dx, dy)[2] |= VISITED
                self._expand(dx, dy)
                bm = self._best_match(dx, dy)
                if bm is not None:
                    if bm != 0:
                        self.set_diamond(dx, dy, bm)
                    for i in range(4):
                        nx, ny = self.neighbor(dx, dy, i)
                        if self.diamond_needs_update(nx, ny):
                            todo.append((nx, ny))

    # --- compile ISOM -> tiles ---
    def _hash(self, dx, dy):
        r = self.rect(dx, dy)
        il = self.T.isomLinks
        h = 0
        lastTT = 0
        for side in SIDES:
            iv = r[side] & CLEAR_ALL
            sl = il[iv >> 4]
            edge = sl_edge_link(sl, iv)
            h = (h | edge) << 6
            if sl["terrainType"] != 0 and edge > SOFT:
                lastTT = sl["terrainType"]
        return h | lastTT

    def _get_tile_group(self, tv):
        return tv // 16

    def _random_subtile(self, group):
        megs = self.T.groups[group].megas if group < len(self.T.groups) else None
        if megs:
            common = 0
            while common < 16 and megs[common] != 0:
                common += 1
            rare = 0
            while common + rare + 1 < 16 and megs[common + rare + 1] != 0:
                rare += 1
            if rare and random.randint(0, 19) == 0:
                return 16 * group + common + 1 + random.randrange(rare)
            if common:
                return 16 * group + random.randrange(common)
        return 16 * group

    def update_tiles(self):
        c = self.changed
        for y in range(c[1], c[3] + 1):
            for x in range(c[0], c[2] + 1):
                r = self.rect(x, y)
                if (r[0] | r[2]) & MODIFIED:
                    self._update_tile(x, y)
                r[0] &= CLEAR_ALL
                r[1] &= CLEAR_ALL
                r[2] &= CLEAR_ALL
                r[3] &= CLEAR_ALL
        self._reset_changed()

    def _set_tile(self, tx, ty, tv):
        self.tiles[ty * self.tileW + tx] = tv

    def _get_tile(self, tx, ty):
        return self.tiles[ty * self.tileW + tx]

    def _update_tile(self, dx, dy):
        if dx + 1 >= self.isomW or dy + 1 >= self.isomH:
            return
        lx, rx = 2 * dx, 2 * dx + 1
        groups = self.T.groups
        h = self._hash(dx, dy)
        potential = self.T.hashToGroup.get(h)
        if not potential:
            self._set_tile(lx, dy, 0)
            self._set_tile(rx, dy, 0)
            return
        dest = potential[0]
        if dy > 0:
            above = self._get_tile_group(self._get_tile(lx, dy - 1))
            if above < len(groups):
                bottomC = groups[above].stack["bottom"]
                for p in potential:
                    if groups[p].stack["top"] == bottomC:
                        dest = p
                        break
        sub = self._random_subtile(dest) % 16
        self._set_tile(lx, dy, 16 * dest + sub)
        self._set_tile(rx, dy, 16 * (dest + 1) + sub)
        # stack propagation downward (cliffs) -- simplified per reference
        for y in range(dy + 1, self.tileH):
            tg = self._get_tile_group(self._get_tile(lx, y - 1))
            ntg = self._get_tile_group(self._get_tile(lx, y))
            if tg >= len(groups) or ntg >= len(groups) or \
                    groups[tg].stack["bottom"] == 0 or groups[ntg].stack["top"] == 0:
                break
            bottomC = groups[tg].stack["bottom"]
            lg = self._get_tile_group(self._get_tile(lx, y))
            rg = self._get_tile_group(self._get_tile(rx, y))
            if bottomC != groups[ntg].stack["top"]:
                h2 = self._hash(dx, y)
                pot2 = self.T.hashToGroup.get(h2)
                if pot2:
                    for p in pot2:
                        if groups[p].stack["top"] == bottomC:
                            lg, rg = p, p + 1
                            break
            self._set_tile(lx, y, 16 * lg + sub)
            self._set_tile(rx, y, 16 * rg + sub)


# ---- render the compiled tile map --------------------------------------
def render_map(scmap, tileset, out_name):
    W, H = scmap.tileW, scmap.tileH
    img = Image.new("RGB", (W * 32, H * 32), (0, 0, 0))
    for ty in range(H):
        for tx in range(W):
            tv = scmap.tiles[ty * W + tx]
            group, sub = tv // 16, tv % 16
            if group >= len(scmap.T.groups):
                continue
            mega = scmap.T.groups[group].megas[sub]
            img.paste(tileset.megatile(mega), (tx * 32, ty * 32))
    os.makedirs(OUTDIR, exist_ok=True)
    path = os.path.join(OUTDIR, out_name)
    img.save(path)
    print("wrote", path, img.size)


def main():
    random.seed(1)
    ts = Tileset("badlands")
    groups = load_tile_groups(ts.cv5)
    tiles = Tiles(groups, BADLANDS_TT, BADLANDS_MAP)
    print(f"isomLinks: {len(tiles.isomLinks)} entries, "
          f"{len([g for g in groups if g.terrainType>0])} typed groups")

    W = H = 48
    m = ScMap(tiles, W, H)
    m.fill(2)  # Dirt background

    # Paint a lake (Water=5), a grass field (Grass=6), and a high-dirt plateau
    # (HighDirt=3, i.e. a CLIFF) to exercise edges, corners and elevation.
    D = BADLANDS_TT
    def paint(tt, dx, dy, ext):
        m.place(dx, dy, tt, ext)
    paint(3, 12, 12, 8)    # high dirt plateau (cliff), surrounded by dirt
    paint(5, 12, 36, 8)    # water lake, surrounded by dirt
    paint(6, 20, 24, 7)    # grass field, surrounded by dirt
    m.set_all_changed()    # recompile the whole map from final ISOM state
    m.update_tiles()
    render_map(m, ts, "isom_paint_demo.png")


if __name__ == "__main__":
    main()
