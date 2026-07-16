// Faithful StarCraft ISOM terrain auto-tiler — see sc_isom.h. Straight port of
// tools/sc_isom.py (which ports TheNitesWhoSay/IsomTerrain, MIT). Kept
// structurally identical to the Python so the two can be cross-checked.

#include "game/sc_isom.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace game {

using namespace isomlink;

namespace {

// Quadrants and sides used as plain indices below.
const int QUADS[4] = {TLq, TRq, BRq, BLq};
const int SIDES[4] = {SL, ST, SR, SB};

int oppositeQuadrant(int i) { return i ^ 2; }  // TL<->BR, TR<->BL

// ProjectedQuadrant.at(q) -> (firstSide, secondSide, firstEdgeFlags, secondEdge)
struct Proj { int first, second, ef1, ef2; };
const Proj PROJ[4] = {
    {SR, SB, 0x0, 0x2},  // TLq
    {SL, SB, 0x4, 0x6},  // TRq
    {SL, ST, 0x8, 0xA},  // BRq
    {ST, SR, 0xC, 0xE},  // BLq
};
constexpr int EDGE_MASK = 0xE;

// --- The 14 canonical shapes -------------------------------------------
struct ShapeQuad {
    int left = 0, top = 0, right = 0, bottom = 0, linkId = 0;
    bool stackTop = false;
};
struct Shape {
    ShapeQuad q[4];  // indexed TLq,TRq,BRq,BLq
};

// q(...) builder mirroring the Python keyword form.
ShapeQuad SQ(int left = 0, int top = 0, int right = 0, int bottom = 0,
             int linkId = 0, bool stackTop = false) {
    return {left, top, right, bottom, linkId, stackTop};
}
Shape MK(ShapeQuad tl = {}, ShapeQuad tr = {}, ShapeQuad br = {},
         ShapeQuad bl = {}) {
    Shape s;
    s.q[TLq] = tl; s.q[TRq] = tr; s.q[BRq] = br; s.q[BLq] = bl;
    return s;
}

const Shape SHAPES[14] = {
    // 0 edgeNorthWest
    MK(SQ(), SQ(0, 0, BR, BR, TRBL_NW, true), SQ(BR, BR, 0, 0),
       SQ(0, 0, BR, FR, TRBL_NW, true)),
    // 1 edgeNorthEast
    MK(SQ(BL, 0, 0, BL, TLBR_NE, true), SQ(), SQ(BL, 0, 0, FL, TLBR_NE, true),
       SQ(0, BL, BL, 0)),
    // 2 edgeSouthEast
    MK(SQ(0, 0, TL, TL), SQ(TL, FL, 0, 0, TRBL_SE), SQ(),
       SQ(TL, TL, 0, 0, TRBL_SE)),
    // 3 edgeSouthWest
    MK(SQ(0, FR, TR, 0, TLBR_SW), SQ(TR, 0, 0, TR), SQ(0, TR, TR, 0, TLBR_SW),
       SQ()),
    // 4 jutOutNorth
    MK(SQ(), SQ(), SQ(BL, 0, 0, BL, TLBR_NE, true),
       SQ(0, 0, BR, BR, TRBL_NW, true)),
    // 5 jutOutEast
    MK(SQ(BL, 0, 0, FL, TLBR_NE, true), SQ(), SQ(), SQ(TL, FL, 0, 0, TRBL_SE)),
    // 6 jutOutSouth
    MK(SQ(0, TR, TR, 0, TLBR_SW), SQ(TL, TL, 0, 0, TRBL_SE), SQ(), SQ()),
    // 7 jutOutWest
    MK(SQ(), SQ(0, 0, BR, FR, TRBL_NW, true), SQ(0, FR, TR, 0, TLBR_SW), SQ()),
    // 8 jutInEast
    MK(SQ(0, FR, TR, 0, TLBR_SW), SQ(RH, 0, 0, RH), SQ(RH, RH, 0, 0),
       SQ(0, 0, BR, FR, TRBL_NW)),
    // 9 jutInWest
    MK(SQ(0, 0, LH, LH), SQ(TL, FL, 0, 0, TRBL_SE), SQ(BL, 0, 0, FL, TLBR_NE),
       SQ(0, LH, LH, 0)),
    // 10 jutInNorth
    MK(SQ(BL, 0, 0, BL, TLBR_NE, true), SQ(0, 0, BR, BR, TRBL_NW, true),
       SQ(BR, BR, 0, 0), SQ(0, BL, BL, 0)),
    // 11 jutInSouth
    MK(SQ(0, 0, TL, TL), SQ(TR, 0, 0, TR), SQ(0, TR, TR, 0, TLBR_SW),
       SQ(TL, TL, 0, 0, TRBL_SE)),
    // 12 horizontal
    MK(SQ(0, TR, TR, 0, TLBR_SW), SQ(TL, TL, 0, 0, TRBL_SE),
       SQ(BL, 0, 0, BL, TLBR_NE), SQ(0, 0, BR, BR, TRBL_NW)),
    // 13 vertical
    MK(SQ(BL, 0, 0, FL, TLBR_NE), SQ(0, 0, BR, FR, TRBL_NW),
       SQ(0, FR, TR, 0, TLBR_SW), SQ(TL, FL, 0, 0, TRBL_SE)),
};
constexpr int NUM_SHAPES = 14;

// The four link fields of a shape/tile-group quadrant, by index l,t,r,b.
int shapeQuadEdge(const ShapeQuad& q, int k) {
    switch (k) { case 0: return q.left; case 1: return q.top;
                 case 2: return q.right; default: return q.bottom; }
}

// ShapeQuadrant.matches: hard links must equal; soft links match any soft.
bool quadMatches(const ShapeQuad& sq, const uint16_t links[4], bool noStack) {
    for (int k = 0; k < 4; ++k) {
        int lv = links[k], sv = shapeQuadEdge(sq, k);
        if (!(lv == sv || (lv <= SOFT && sv <= SOFT)))
            return false;
    }
    return noStack || !sq.stackTop;
}

// A group's links form a "shape quadrant" only when they mix hard and soft.
bool isShapeQuadrant(const uint16_t links[4]) {
    bool allHard = true, noHard = true;
    for (int k = 0; k < 4; ++k) {
        if (links[k] <= SOFT) allHard = false;
        else noHard = false;
    }
    return !allHard && !noHard;
}

// sl_edge_link: pick the shapelinks edge for a rect side's edge-flag bits.
int slEdgeLink(const ScIsom::ShapeLinks& sl, int isomValue) {
    switch (isomValue & EDGE_MASK) {
        case 0x0: return sl.q[TLq].right;
        case 0x2: return sl.q[TLq].bottom;
        case 0x4: return sl.q[TRq].left;
        case 0x6: return sl.q[TRq].bottom;
        case 0x8: return sl.q[BRq].left;
        case 0xA: return sl.q[BRq].top;
        case 0xC: return sl.q[BLq].top;
        default:  return sl.q[BLq].right;  // 0xE
    }
}

int slLinkId(const ScIsom::ShapeLinks& sl, int quadrant) {
    return sl.q[quadrant].linkId;
}

}  // namespace

// ======================= ScIsom (per-tileset tables) ====================

ScIsom::ScIsom(const fmt::ScTileset& tileset, const ScBrush& brush)
    : ts(tileset), brush_(brush), numTt_(static_cast<int>(brush.tt.size())) {
    buildTerrainTypeMap();
    buildHashMap();
    generateIsomLinks();
}

// populateTerrainTypeMap
void ScIsom::buildTerrainTypeMap() {
    int n = numTt_;
    const std::vector<int>& c = brush_.mapCompressed;
    ttMap.assign(size_t(n) * n, 0);
    std::vector<int> temp(size_t(n) * n, 0);
    size_t i = 0;
    while (c[i] != 0) {
        int row = c[i];
        ++i;
        int j = n * row;
        while (c[i] != 0) {
            temp[j] = c[i];
            ++i;
            ++j;
        }
        ++i;  // skip the 0 terminator
    }
    for (int src = n - 1; src >= 0; --src) {
        std::vector<int> rowData(n, 0);
        std::deque<int> stack{src};
        ttMap[size_t(n) * src + src] = src;
        while (!stack.empty()) {
            int destRow = stack.front();
            stack.pop_front();
            int start = src * n;
            int j = destRow * n;
            while (temp[j] != 0) {
                int tempPath = temp[j];
                if (ttMap[start + tempPath] == 0) {
                    int nextVal = rowData[destRow] == 0 ? tempPath : rowData[destRow];
                    stack.push_back(tempPath);
                    ttMap[start + tempPath] = nextVal;
                    rowData[tempPath] = nextVal;
                }
                ++j;
            }
        }
    }
}

// loadIsom hash map (hashToTileGroup)
void ScIsom::buildHashMap() {
    const auto& groups = ts.groups;
    for (size_t i = 0; i < groups.size(); i += 2) {
        const auto& gl = groups[i].links;  // [L,T,R,B]
        uint64_t l = gl[0], t = gl[1], r = gl[2], b = gl[3];
        uint64_t h = (((l << 6 | t) << 6 | r) << 6 | b) << 6;
        if (l >= 48 || t >= 48 || r >= 48 || b >= 48)
            h |= groups[i].terrainType;
        hashToGroup[h].push_back(static_cast<int>(i));
    }
}

// generateIsomLinks
void ScIsom::generateIsomLinks() {
    const auto& groups = ts.groups;
    int total = std::min<int>(1024, static_cast<int>(groups.size()));
    std::vector<std::vector<int>> byType(numTt_);
    for (int i = 0; i < total; i += 2) {
        int tt = groups[i].terrainType;
        if (tt > 0 && tt < numTt_)
            byType[tt].push_back(i);
    }

    // solid brushes (index 1..numTt/2) and other brushes (rest), each sorted by
    // isomValue.
    struct SolidE { int idx, isom, linkId; };
    struct OtherE { int idx, isom; };
    std::vector<SolidE> solid;
    std::vector<OtherE> other;
    int i = 1;
    for (; i <= numTt_ / 2; ++i)
        if (brush_.tt[i].isomValue != 0)
            solid.push_back({i, brush_.tt[i].isomValue, brush_.tt[i].linkId});
    for (; i < numTt_; ++i)
        if (brush_.tt[i].isomValue != 0)
            other.push_back({i, brush_.tt[i].isomValue});
    std::sort(solid.begin(), solid.end(),
              [](const SolidE& a, const SolidE& b) { return a.isom < b.isom; });
    std::sort(other.begin(), other.end(),
              [](const OtherE& a, const OtherE& b) { return a.isom < b.isom; });

    auto& links = isomLinks;
    for (const auto& e : solid) {
        while (static_cast<int>(links.size()) < e.isom)
            links.emplace_back();
        if (byType[e.idx].empty()) { links.emplace_back(); continue; }
        const auto& g = groups[byType[e.idx][0]].links;  // [L,T,R,B]
        ShapeLinks sl;
        sl.terrainType = e.idx;
        sl.q[TLq] = {0, 0, g[2], g[3], e.linkId};  // right,bottom
        sl.q[TRq] = {g[0], 0, 0, g[3], e.linkId};  // left,bottom
        sl.q[BRq] = {g[0], g[1], 0, 0, e.linkId};  // left,top
        sl.q[BLq] = {0, g[1], g[2], 0, e.linkId};  // top,right
        links.push_back(sl);
    }

    int totalSolid = static_cast<int>(links.size());
    if (!other.empty())
        while (static_cast<int>(links.size()) < other[0].isom)
            links.emplace_back();

    for (const auto& e : other) {
        size_t start = links.size();
        for (int s = 0; s < NUM_SHAPES; ++s) {
            ShapeLinks sl;
            sl.terrainType = e.idx;
            links.push_back(sl);
        }
        // shapeGroups[si][quad] = source group index (-1 = none)
        std::vector<std::array<int, 4>> shapeGroups(
            NUM_SHAPES, std::array<int, 4>{-1, -1, -1, -1});
        for (int gi : byType[e.idx]) {
            const auto& g = groups[gi];
            if (!isShapeQuadrant(g.links)) continue;
            bool noStack = (g.stack[ST] == 0);  // stack.top
            for (int si = 0; si < NUM_SHAPES; ++si) {
                const Shape& sh = SHAPES[si];
                ShapeLinks& sl = links[start + si];
                if (quadMatches(sh.q[TLq], g.links, noStack)) {
                    sl.q[TLq].right = g.links[2];
                    sl.q[TLq].bottom = g.links[3];
                    shapeGroups[si][TLq] = gi;
                }
                if (quadMatches(sh.q[TRq], g.links, noStack)) {
                    sl.q[TRq].left = g.links[0];
                    sl.q[TRq].bottom = g.links[3];
                    shapeGroups[si][TRq] = gi;
                }
                if (quadMatches(sh.q[BRq], g.links, noStack)) {
                    sl.q[BRq].left = g.links[0];
                    sl.q[BRq].top = g.links[1];
                    shapeGroups[si][BRq] = gi;
                }
                if (quadMatches(sh.q[BLq], g.links, noStack)) {
                    sl.q[BLq].top = g.links[1];
                    sl.q[BLq].right = g.links[2];
                    shapeGroups[si][BLq] = gi;
                }
            }
        }
        postProcessShapes(start, shapeGroups.data(), totalSolid);
    }
}

void ScIsom::postProcessShapes(size_t start, const std::array<int, 4>* sg,
                               int totalSolid) {
    auto& links = isomLinks;
    auto S = [&](int i) -> ShapeLinks& { return links[start + i]; };
    const auto& g = ts.groups;

    // populateJutInEastWest
    if (S(8).q[TRq].left == NONE && sg[1][BLq] != -1) {
        S(8).q[TRq].left = g[sg[1][BLq]].links[0];
        S(8).q[TRq].bottom = g[sg[1][BLq]].links[3];
        if (sg[2][TLq] != -1) {
            S(8).q[BRq].left = g[sg[2][TLq]].links[0];
            S(8).q[BRq].top = g[sg[2][TLq]].links[1];
        }
    }
    if (S(9).q[TLq].right == NONE && sg[0][BRq] != -1) {
        S(9).q[TLq].right = g[sg[0][BRq]].links[2];
        S(9).q[TLq].bottom = g[sg[0][BRq]].links[3];
        if (sg[3][TRq] != -1) {
            S(9).q[BLq].top = g[sg[3][TRq]].links[1];
            S(9).q[BLq].right = g[sg[3][TRq]].links[2];
        }
    }
    // populateEmptyQuadrantLinks
    S(0).q[TLq].right = S(0).q[TRq].left;
    S(0).q[TLq].bottom = S(0).q[BLq].top;
    S(1).q[TRq].left = S(1).q[TLq].right;
    S(1).q[TRq].bottom = S(1).q[BRq].top;
    S(2).q[BRq].left = S(2).q[BLq].right;
    S(2).q[BRq].top = S(2).q[TRq].bottom;
    S(3).q[BLq].top = S(3).q[TLq].bottom;
    S(3).q[BLq].right = S(3).q[BRq].left;
    S(4).q[TLq].bottom = S(4).q[BLq].top;
    S(4).q[TLq].right = S(4).q[TLq].bottom;
    S(4).q[TRq].bottom = S(4).q[BRq].top;
    S(4).q[TRq].left = S(4).q[TRq].bottom;
    {
        int fill = S(5).q[TLq].right;
        S(5).q[TRq].left = fill;
        S(5).q[TRq].bottom = fill;
        S(5).q[BRq].left = fill;
        S(5).q[BRq].top = fill;
    }
    S(6).q[BRq].top = S(6).q[TRq].bottom;
    S(6).q[BRq].left = S(6).q[BRq].top;
    S(6).q[BLq].top = S(6).q[TLq].bottom;
    S(6).q[BLq].right = S(6).q[BLq].top;
    {
        int fill = S(7).q[TRq].left;
        S(7).q[TLq].right = fill;
        S(7).q[TLq].bottom = fill;
        S(7).q[BLq].right = fill;
        S(7).q[BLq].top = fill;
    }
    // populateHardcodedLinkIds
    for (int si = 0; si < NUM_SHAPES; ++si)
        for (int qq : QUADS)
            if (SHAPES[si].q[qq].linkId >= ONLY_SAME)
                S(si).q[qq].linkId = SHAPES[si].q[qq].linkId;
    // populateLinkIdsToSolidBrushes
    bool haveOuter = sg[0][TRq] != -1, haveInner = sg[0][BRq] != -1;
    int outerRef = haveOuter ? g[sg[0][TRq]].links[0] : 0;  // .left
    int innerRef = haveInner ? g[sg[0][BRq]].links[2] : 0;  // .right
    for (int bi = 0; bi < totalSolid; ++bi) {
        int brushLink = links[bi].q[TLq].right;
        int brushLinkId = links[bi].q[TLq].linkId;
        if (haveOuter && brushLink == outerRef) fillOuter(start, brushLinkId);
        if (haveInner && brushLink == innerRef) fillInner(start, brushLinkId);
    }
}

void ScIsom::fillOuter(size_t start, int linkId) {
    auto& links = isomLinks;
    auto S = [&](int i) -> ShapeLinks& { return links[start + i]; };
    S(0).q[TLq].linkId = linkId;
    S(1).q[TRq].linkId = linkId;
    S(2).q[BRq].linkId = linkId;
    S(3).q[BLq].linkId = linkId;
    S(4).q[TLq].linkId = linkId;
    S(4).q[TRq].linkId = linkId;
    S(5).q[TRq].linkId = linkId;
    S(5).q[BRq].linkId = linkId;
    S(7).q[TLq].linkId = linkId;
    S(7).q[BLq].linkId = linkId;
    S(6).q[BRq].linkId = linkId;
    S(6).q[BLq].linkId = linkId;
}

void ScIsom::fillInner(size_t start, int linkId) {
    auto& links = isomLinks;
    auto S = [&](int i) -> ShapeLinks& { return links[start + i]; };
    S(0).q[BRq].linkId = linkId;
    S(1).q[BLq].linkId = linkId;
    S(2).q[TLq].linkId = linkId;
    S(3).q[TRq].linkId = linkId;
    S(8).q[TRq].linkId = linkId;
    S(8).q[BRq].linkId = linkId;
    S(9).q[TLq].linkId = linkId;
    S(9).q[BLq].linkId = linkId;
    S(10).q[BRq].linkId = linkId;
    S(10).q[BLq].linkId = linkId;
    S(11).q[TLq].linkId = linkId;
    S(11).q[TRq].linkId = linkId;
}

// ======================= ScIsomMap (map + compile) ======================

ScIsomMap::ScIsomMap(const ScIsom& tiles, int tileW, int tileH)
    : T_(tiles), tileW_(tileW), tileH_(tileH),
      isomW_(tileW / 2 + 1), isomH_(tileH + 1) {
    isom_.assign(size_t(isomW_) * isomH_, Rect{});
    tiles_.assign(size_t(tileW_) * tileH_, 0);
    resetChanged();
}

void ScIsomMap::neighbor(int x, int y, int i, int& nx, int& ny) {
    switch (i) {  // UL, UR, LR, LL
        case 0: nx = x - 1; ny = y - 1; break;
        case 1: nx = x + 1; ny = y - 1; break;
        case 2: nx = x + 1; ny = y + 1; break;
        default: nx = x - 1; ny = y + 1; break;
    }
}

void ScIsomMap::rectCoords(int x, int y, int quad, int& rx, int& ry) {
    switch (quad) {
        case TLq: rx = x - 1; ry = y - 1; break;
        case TRq: rx = x;     ry = y - 1; break;
        case BRq: rx = x;     ry = y;     break;
        default:  rx = x - 1; ry = y;     break;  // BLq
    }
}

void ScIsomMap::resetChanged() {
    changed_[0] = isomW_; changed_[1] = isomH_; changed_[2] = 0; changed_[3] = 0;
}

void ScIsomMap::expand(int x, int y) {
    changed_[0] = std::min(changed_[0], x);
    changed_[1] = std::min(changed_[1], y);
    changed_[2] = std::max(changed_[2], x);
    changed_[3] = std::max(changed_[3], y);
}

void ScIsomMap::setAllChanged() {
    changed_[0] = 0; changed_[1] = 0; changed_[2] = isomW_ - 1; changed_[3] = isomH_ - 1;
}

void ScIsomMap::fill(int terrainType) {
    uint16_t iv = uint16_t((T_.isomValueOf(terrainType) << 4) | MODIFIED);
    for (auto& r : isom_) { r.s[0] = r.s[1] = r.s[2] = r.s[3] = iv; }
    setAllChanged();
    updateTiles();
}

void ScIsomMap::setIsomValue(int rx, int ry, int quad, int isomValue) {
    if (!inBounds(rx, ry)) return;
    const Proj& p = PROJ[quad];
    Rect& r = rect(rx, ry);
    r.s[p.first] = uint16_t((isomValue << 4) | p.ef1);
    r.s[p.second] = uint16_t((isomValue << 4) | p.ef2);
    r.s[p.first] |= MODIFIED;
    r.s[p.second] |= MODIFIED;
    expand(rx, ry);
}

void ScIsomMap::setDiamond(int dx, int dy, int isomValue) {
    for (int qq : QUADS) {
        int rx, ry;
        rectCoords(dx, dy, qq, rx, ry);
        setIsomValue(rx, ry, qq, isomValue);
    }
}

bool ScIsomMap::place(int dx, int dy, int terrainType, int brushExtent) {
    int iv = T_.isomValueOf(terrainType);
    if (iv == 0 || !diamondValid(dx, dy) ||
        iv >= static_cast<int>(T_.isomLinks.size()) ||
        T_.isomLinks[iv].terrainType == 0)
        return false;
    int bmin = -(brushExtent / 2);
    int bmax = bmin + brushExtent;
    if (brushExtent % 2 == 0) { bmin += 1; bmax += 1; }
    resetChanged();
    std::deque<std::pair<int, int>> todo;
    for (int ox = bmin; ox < bmax; ++ox) {
        for (int oy = bmin; oy < bmax; ++oy) {
            int bx = dx + ox - oy;
            int by = dy + ox + oy;
            if (inBounds(bx, by)) {
                setDiamond(bx, by, iv);
                if (ox == bmin || ox == bmax - 1 || oy == bmin || oy == bmax - 1) {
                    for (int i = 0; i < 4; ++i) {
                        int nx, ny;
                        neighbor(bx, by, i, nx, ny);
                        if (diamondNeedsUpdate(nx, ny)) todo.push_back({nx, ny});
                    }
                }
            }
        }
    }
    radialUpdate(todo);
    return true;
}

void ScIsomMap::loadNeighbors(int dx, int dy, NInfo nb[4], int& maxMod) const {
    const auto& il = T_.isomLinks;
    maxMod = 0;
    for (int i = 0; i < 4; ++i) {
        int nx, ny;
        neighbor(dx, dy, i, nx, ny);
        NInfo info;
        if (inBounds(nx, ny)) {
            int iv = centralIsom(nx, ny);
            info.modified = centralModified(nx, ny);
            info.isomValue = iv;
            if (iv < static_cast<int>(il.size())) {
                info.linkId = slLinkId(il[iv], oppositeQuadrant(i));
                if (info.modified && il[iv].terrainType > maxMod)
                    maxMod = il[iv].terrainType;
            }
        }
        nb[i] = info;
    }
}

int ScIsomMap::countMatches(const ScIsom::ShapeLinks& sl, const NInfo nb[4]) const {
    int tt = sl.terrainType;
    int total = 0;
    const auto& il = T_.isomLinks;
    for (int qq : QUADS) {
        const NInfo& n = nb[qq];
        int nTT = (n.isomValue < static_cast<int>(il.size()))
                      ? il[n.isomValue].terrainType : 0;
        int nLink = n.linkId;
        int qLink = slLinkId(sl, qq);
        if (nLink == qLink && (qLink < ONLY_SAME || tt == nTT))
            ++total;
        else if (n.modified)
            return 0;
    }
    return total;
}

void ScIsomMap::searchBest(int startTT, const NInfo nb[4], int& bestVal,
                           int& bestCount) const {
    const auto& il = T_.isomLinks;
    bool searchUntilHigher = (startTT == T_.numTt() / 2 + 1);
    bool searchUntilEnd = (startTT == 0);
    int iv = T_.isomValueOf(startTT);
    while (iv < static_cast<int>(il.size())) {
        int tt = il[iv].terrainType;
        if (!searchUntilEnd && tt != startTT &&
            (!searchUntilHigher || tt > startTT))
            break;
        int mc = countMatches(il[iv], nb);
        if (mc > bestCount) { bestVal = iv; bestCount = mc; }
        ++iv;
    }
}

int ScIsomMap::bestMatch(int dx, int dy) const {
    NInfo nb[4];
    int maxMod;
    loadNeighbors(dx, dy, nb, maxMod);
    int bestVal = 0, bestCount = 0;
    int prev = centralIsom(dx, dy);
    const auto& il = T_.isomLinks;
    if (prev < static_cast<int>(il.size())) {
        int prevTT = il[prev].terrainType;
        int mapped = T_.ttMap[size_t(maxMod) * T_.numTt() + prevTT];
        searchBest(mapped, nb, bestVal, bestCount);
    }
    searchBest(maxMod, nb, bestVal, bestCount);
    searchBest(T_.numTt() / 2 + 1, nb, bestVal, bestCount);
    if (bestVal == prev) return -1;
    return bestVal;
}

void ScIsomMap::radialUpdate(std::deque<std::pair<int, int>>& todo) {
    while (!todo.empty()) {
        auto [dx, dy] = todo.front();
        todo.pop_front();
        if (diamondNeedsUpdate(dx, dy) && !(rect(dx, dy).s[SR] & VISITED)) {
            rect(dx, dy).s[SR] |= VISITED;
            expand(dx, dy);
            int bm = bestMatch(dx, dy);
            if (bm != -1) {
                if (bm != 0) setDiamond(dx, dy, bm);
                for (int i = 0; i < 4; ++i) {
                    int nx, ny;
                    neighbor(dx, dy, i, nx, ny);
                    if (diamondNeedsUpdate(nx, ny)) todo.push_back({nx, ny});
                }
            }
        }
    }
}

uint64_t ScIsomMap::hashDiamond(int dx, int dy) const {
    const Rect& r = rect(dx, dy);
    const auto& il = T_.isomLinks;
    uint64_t h = 0;
    int lastTT = 0;
    for (int side : SIDES) {
        int iv = r.s[side] & CLEAR_ALL;
        const ScIsom::ShapeLinks& sl = il[iv >> 4];
        int edge = slEdgeLink(sl, iv);
        h = (h | uint64_t(edge)) << 6;
        if (sl.terrainType != 0 && edge > SOFT) lastTT = sl.terrainType;
    }
    return h | uint64_t(lastTT);
}

int ScIsomMap::randomSubtile(int group) {
    const auto& groups = T_.ts.groups;
    if (group >= 0 && group < static_cast<int>(groups.size())) {
        const uint16_t* megs = groups[group].megas;
        int common = 0;
        while (common < 16 && megs[common] != 0) ++common;
        int rare = 0;
        while (common + rare + 1 < 16 && megs[common + rare + 1] != 0) ++rare;
        // xorshift32 PRNG (deterministic per map instance).
        auto next = [&]() {
            uint32_t x = rngState_;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            return rngState_ = x;
        };
        if (rare && (next() % 20) == 0)
            return (common + 1 + int(next() % rare)) & 15;
        if (common)
            return int(next() % common) & 15;
    }
    return 0;
}

void ScIsomMap::updateTile(int dx, int dy) {
    if (dx + 1 >= isomW_ || dy + 1 >= isomH_) return;
    int lx = 2 * dx, rx = 2 * dx + 1;
    const auto& groups = T_.ts.groups;
    int ng = static_cast<int>(groups.size());
    uint64_t h = hashDiamond(dx, dy);
    auto it = T_.hashToGroup.find(h);
    if (it == T_.hashToGroup.end() || it->second.empty()) {
        setTile(lx, dy, 0);
        setTile(rx, dy, 0);
        return;
    }
    const std::vector<int>& potential = it->second;
    int dest = potential[0];
    if (dy > 0) {
        int above = getTile(lx, dy - 1) / 16;
        if (above < ng) {
            int bottomC = groups[above].stack[SB];
            for (int p : potential) {
                if (groups[p].stack[ST] == bottomC) { dest = p; break; }
            }
        }
    }
    int sub = randomSubtile(dest) % 16;
    setTile(lx, dy, uint16_t(16 * dest + sub));
    setTile(rx, dy, uint16_t(16 * (dest + 1) + sub));
    // stack propagation downward (cliffs)
    for (int y = dy + 1; y < tileH_; ++y) {
        int tg = getTile(lx, y - 1) / 16;
        int ntg = getTile(lx, y) / 16;
        if (tg >= ng || ntg >= ng || groups[tg].stack[SB] == 0 ||
            groups[ntg].stack[ST] == 0)
            break;
        int bottomC = groups[tg].stack[SB];
        int lg = getTile(lx, y) / 16;
        int rg = getTile(rx, y) / 16;
        if (bottomC != groups[ntg].stack[ST]) {
            uint64_t h2 = hashDiamond(dx, y);
            auto it2 = T_.hashToGroup.find(h2);
            if (it2 != T_.hashToGroup.end()) {
                for (int p : it2->second) {
                    if (groups[p].stack[ST] == bottomC) { lg = p; rg = p + 1; break; }
                }
            }
        }
        setTile(lx, y, uint16_t(16 * lg + sub));
        setTile(rx, y, uint16_t(16 * rg + sub));
    }
}

// --- .scm serialization ---------------------------------------------------
namespace {
void putU16(std::ofstream& f, uint16_t v) {
    char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
    f.write(b, 2);
}
uint16_t getU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
}  // namespace

bool ScIsomMap::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write("SCM1", 4);
    putU16(f, uint16_t(tileW_));
    putU16(f, uint16_t(tileH_));
    putU16(f, uint16_t(tilesetName.size()));
    f.write(tilesetName.data(), std::streamsize(tilesetName.size()));
    for (const Rect& r : isom_)
        for (int s = 0; s < 4; ++s)
            putU16(f, uint16_t(r.s[s] & CLEAR_ALL));  // strip editor flags
    return bool(f);
}

bool ScIsomMap::peek(const std::string& path, std::string& tileset, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    f.read(magic, 4);
    if (!f || std::memcmp(magic, "SCM1", 4) != 0) return false;
    uint8_t hdr[6];
    f.read(reinterpret_cast<char*>(hdr), 6);
    if (!f) return false;
    w = getU16(hdr);
    h = getU16(hdr + 2);
    int nameLen = getU16(hdr + 4);
    std::string name(nameLen, '\0');
    f.read(name.data(), nameLen);
    if (!f) return false;
    tileset = name;
    return true;
}

bool ScIsomMap::load(const std::string& path) {
    std::string ts;
    int w, h;
    if (!peek(path, ts, w, h) || w != tileW_ || h != tileH_) return false;
    std::ifstream f(path, std::ios::binary);
    f.seekg(4 + 6 + std::streamoff(ts.size()));
    std::vector<uint8_t> buf(size_t(isomW_) * isomH_ * 4 * 2);
    f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()));
    if (!f) return false;
    tilesetName = ts;
    const uint8_t* p = buf.data();
    for (Rect& r : isom_)
        for (int s = 0; s < 4; ++s, p += 2)
            r.s[s] = getU16(p);
    setAllChanged();
    updateTiles();
    return true;
}

int ScIsomMap::nullTileCount() const {
    int n = 0;
    for (uint16_t cell : tiles_)
        if (cell / 16 == 0) ++n;
    return n;
}

void ScIsomMap::repairNullTiles() {
    // A null pair is (lx=2*dx, rx=2*dx+1) both group 0. Copy the compiled tile
    // pair from the nearest non-null row above or below in the same column,
    // which is the surrounding background terrain. Two passes (down then up) so
    // a band thicker than one row still fills from both sides.
    for (int pass = 0; pass < 2; ++pass) {
        bool topDown = (pass == 0);
        for (int col = 0; col < tileW_; col += 2) {
            int yStart = topDown ? 1 : tileH_ - 2;
            int yEnd = topDown ? tileH_ : -1;
            int step = topDown ? 1 : -1;
            for (int y = yStart; y != yEnd; y += step) {
                if (getTile(col, y) / 16 != 0) continue;  // not null
                int src = y - step;                       // the neighbor we came from
                if (src < 0 || src >= tileH_) continue;
                uint16_t l = getTile(col, src), r = getTile(col + 1, src);
                if (l / 16 == 0) continue;                // neighbor also null
                setTile(col, y, l);
                setTile(col + 1, y, r);
            }
        }
    }
}

void ScIsomMap::updateTiles() {
    int* c = changed_;
    for (int y = c[1]; y <= c[3]; ++y) {
        for (int x = c[0]; x <= c[2]; ++x) {
            Rect& r = rect(x, y);
            if ((r.s[0] | r.s[2]) & MODIFIED) updateTile(x, y);
            r.s[0] &= CLEAR_ALL;
            r.s[1] &= CLEAR_ALL;
            r.s[2] &= CLEAR_ALL;
            r.s[3] &= CLEAR_ALL;
        }
    }
    resetChanged();
}

// ======================= built-in brush tables ==========================

const ScBrush& badlandsBrush() {
    static ScBrush b = [] {
        ScBrush br;
        br.tt.resize(36);
        auto T = [&](int i, int isom, int sort, int link, const char* name) {
            br.tt[i] = {isom, sort, link, name};
        };
        // (index -> isomValue, brushSort, linkId, name) from IsomApi.h Badlands
        T(0, 10, -1, 0, "");   T(1, 0, -1, 0, "");
        T(2, 1, 0, 1, "Dirt"); T(3, 2, 2, 2, "High Dirt");
        T(4, 9, 1, 4, "Mud");  T(5, 3, 3, 3, "Water");
        T(6, 4, 4, 5, "Grass"); T(7, 7, 5, 6, "High Grass");
        T(8, 0, -1, 0, ""); T(9, 0, -1, 0, ""); T(10, 0, -1, 0, "");
        T(11, 0, -1, 0, ""); T(12, 0, -1, 0, ""); T(13, 0, -1, 0, "");
        T(14, 5, 7, 9, "Asphalt"); T(15, 6, 8, 10, "Rocky Ground");
        T(16, 0, -1, 0, ""); T(17, 0, -1, 0, ""); T(18, 8, 6, 7, "Structure");
        T(19, 0, -1, 0, ""); T(20, 41, -1, 0, ""); T(21, 69, -1, 0, "");
        T(22, 111, -1, 0, ""); T(23, 0, -1, 0, ""); T(24, 0, -1, 0, "");
        T(25, 0, -1, 0, ""); T(26, 0, -1, 0, ""); T(27, 83, -1, 0, "");
        T(28, 55, -1, 0, ""); T(29, 0, -1, 0, ""); T(30, 0, -1, 0, "");
        T(31, 97, -1, 0, ""); T(32, 0, -1, 0, ""); T(33, 0, -1, 0, "");
        T(34, 13, -1, 0, ""); T(35, 27, -1, 0, "");
        br.mapCompressed = {
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
        };
        return br;
    }();
    return b;
}

}  // namespace game
