#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/sc_tileset.h"

// Faithful StarCraft ISOM (isometric) terrain auto-tiler. Straight C++ port of
// tools/sc_isom.py, which itself ports the MIT-licensed algorithm from
// TheNitesWhoSay/IsomTerrain (IsomApi.h). Given a tileset and its per-tileset
// brush tables, you build a blank map of a default terrain, paint terrain types
// with a diamond brush, and compile the ISOM diamond grid to concrete tile ids
// exactly as StarEdit does. C&C gameplay is untouched — this is a parallel
// editor terrain system.
//
// Terminology:
//  - "diamond" / ISOM cell: the isometric grid (isomW x isomH), valid when
//    (x+y) even. Painting sets diamond values; the compiler resolves them.
//  - "rect": one entry of the ISOM grid, four packed side values [L,T,R,B],
//    each = (isomValue << 4) | edgeFlags, with MODIFIED/VISITED bits.
//  - "tile": the final map cell = (tileGroup << 4) | subtile, tileW x tileH.

namespace game {

// --- Link / LinkId constants (IsomApi.h) --------------------------------
namespace isomlink {
constexpr int NONE = 0;
constexpr int SOFT = 48;  // Link::SoftLinks threshold (<= SOFT is a soft link)
constexpr int BL = 49, TR = 50, BR = 51, TL = 52, FR = 53, FL = 54, LH = 55, RH = 56;
// LinkId special values (only match within the same terrain type)
constexpr int TRBL_NW = 255, TRBL_SE = 256, TLBR_NE = 257, TLBR_SW = 258;
constexpr int ONLY_SAME = 255;  // LinkId::OnlyMatchSameType
}  // namespace isomlink

// Quadrants (also index the four diagonal neighbors: TL=UL, TR=UR, BR=LR, BL=LL)
enum Quad { TLq = 0, TRq = 1, BRq = 2, BLq = 3 };
// Rect sides
enum Side { SL = 0, ST = 1, SR = 2, SB = 3 };

// Per-tileset brush description: one TerrainTypeInfo per terrain-type index,
// plus the compressed terrain-type adjacency map (0-terminated rows, 0 ends).
struct ScTerrainType {
    int isomValue = 0;   // the ISOM value this terrain paints with
    int brushSort = -1;  // brush sort order (-1 = not a user brush)
    int linkId = 0;      // solid-brush link id
    std::string name;    // "" = not a real brush slot
};

struct ScBrush {
    std::vector<ScTerrainType> tt;     // indexed by terrainType
    std::vector<int> mapCompressed;    // terrainTypeMap compressed form
};

// Built-in brush tables (ported from IsomApi.h).
const ScBrush& badlandsBrush();

// Per-tileset ISOM tables (port of Terrain_::Tiles + loadIsom).
class ScIsom {
  public:
    ScIsom(const fmt::ScTileset& ts, const ScBrush& brush);

    int isomValueOf(int terrainType) const {
        return (terrainType >= 0 && terrainType < numTt_)
                   ? brush_.tt[terrainType].isomValue
                   : 0;
    }
    int numTt() const { return numTt_; }

    // One isomLink table entry: terrainType + four quadrants, each carrying its
    // two outer edge links plus a linkId.
    struct ShapeLinks {
        int terrainType = 0;
        struct Q {
            int left = 0, top = 0, right = 0, bottom = 0, linkId = 0;
        } q[4];
    };

    const fmt::ScTileset& ts;
    std::vector<ShapeLinks> isomLinks;
    std::unordered_map<uint64_t, std::vector<int>> hashToGroup;
    std::vector<int> ttMap;  // numTt*numTt

  private:
    const ScBrush& brush_;
    int numTt_;

    void buildTerrainTypeMap();
    void buildHashMap();
    void generateIsomLinks();
    void postProcessShapes(size_t start,
                           const std::array<int, 4>* shapeGroups,
                           int totalSolid);
    void fillOuter(size_t start, int linkId);
    void fillInner(size_t start, int linkId);
};

// The map + ISOM edit/compile (port of ScMap).
class ScIsomMap {
  public:
    ScIsomMap(const ScIsom& tiles, int tileW, int tileH);

    int tileW() const { return tileW_; }
    int tileH() const { return tileH_; }
    // Final compiled tiles: cell = (group << 4) | subtile. Row-major tileW*tileH.
    const std::vector<uint16_t>& tiles() const { return tiles_; }

    // --- .scm parallel map format ------------------------------------------
    // The SC editor map is the ISOM diamond grid + a tileset name; the tiles are
    // recompiled from it. This is entirely separate from the C&C TMP/theater map
    // (game::MapFile) — a parallel format behind the editor's SC mode.
    std::string tilesetName;  // set by the caller; persisted in the .scm
    // Serialize the diamond grid to `path` (magic "SCM1", dims, tileset name,
    // then isomW*isomH rects). Returns false on I/O error.
    bool save(const std::string& path) const;
    // Load a .scm into this map. Dimensions must match those this map was built
    // with (peek() first to size the tileset/map); recompiles tiles. Returns
    // false on I/O error or dimension mismatch.
    bool load(const std::string& path);
    // Read just the header so the caller can load the right tileset and size the
    // map before load(). Returns false if `path` is not a valid .scm.
    static bool peek(const std::string& path, std::string& tileset, int& w, int& h);

    void fill(int terrainType);                    // blank map of one terrain
    bool place(int dx, int dy, int terrainType, int brushExtent);  // paint
    void setAllChanged();
    void updateTiles();                            // compile ISOM -> tiles

    // Count compiled cells that resolved to the null tile (group 0). Nonzero
    // means two incompatible terrains were painted too close for the required
    // transition to fit — a StarEdit-faithful "null" junction (see .cpp notes).
    int nullTileCount() const;
    // Editor quality pass: replace null (group-0) cells by copying a compiled
    // vertical neighbor, so incompatible-adjacency junctions blend into the
    // surrounding terrain instead of showing black. Faithful ISOM data is
    // untouched — this only rewrites the final tile ids. Call after updateTiles.
    void repairNullTiles();

  private:
    static constexpr uint16_t MODIFIED = 0x0001;
    static constexpr uint16_t VISITED = 0x8000;
    static constexpr uint16_t CLEAR_ALL = 0x7FFE;

    struct Rect { uint16_t s[4] = {0, 0, 0, 0}; };  // [L,T,R,B]

    const ScIsom& T_;
    int tileW_, tileH_, isomW_, isomH_;
    std::vector<Rect> isom_;
    std::vector<uint16_t> tiles_;
    int changed_[4];  // left, top, right, bottom
    uint32_t rngState_ = 0x12345678u;

    Rect& rect(int x, int y) { return isom_[size_t(y) * isomW_ + x]; }
    const Rect& rect(int x, int y) const { return isom_[size_t(y) * isomW_ + x]; }
    bool inBounds(int x, int y) const {
        return x >= 0 && x < isomW_ && y >= 0 && y < isomH_;
    }
    static bool diamondValid(int x, int y) { return ((x + y) & 1) == 0; }
    static void neighbor(int x, int y, int i, int& nx, int& ny);
    static void rectCoords(int x, int y, int quad, int& rx, int& ry);

    void resetChanged();
    void expand(int x, int y);
    int centralIsom(int x, int y) const { return rect(x, y).s[0] >> 4; }
    bool centralModified(int x, int y) const { return rect(x, y).s[0] & MODIFIED; }
    bool diamondNeedsUpdate(int x, int y) const {
        return inBounds(x, y) && !centralModified(x, y) && centralIsom(x, y) != 0;
    }

    void setIsomValue(int rx, int ry, int quad, int isomValue);
    void setDiamond(int dx, int dy, int isomValue);

    struct NInfo { int linkId = isomlink::NONE, isomValue = 0; bool modified = false; };
    void loadNeighbors(int dx, int dy, NInfo nb[4], int& maxMod) const;
    int countMatches(const ScIsom::ShapeLinks& sl, const NInfo nb[4]) const;
    void searchBest(int startTT, const NInfo nb[4], int& bestVal, int& bestCount) const;
    int bestMatch(int dx, int dy) const;  // -1 = no change
    void radialUpdate(std::deque<std::pair<int, int>>& todo);

    uint64_t hashDiamond(int dx, int dy) const;
    int randomSubtile(int group);
    void updateTile(int dx, int dy);
    void setTile(int tx, int ty, uint16_t tv) { tiles_[size_t(ty) * tileW_ + tx] = tv; }
    uint16_t getTile(int tx, int ty) const { return tiles_[size_t(ty) * tileW_ + tx]; }
};

}  // namespace game
