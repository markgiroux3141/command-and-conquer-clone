#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "formats/sc_tileset.h"
#include "game/render.h"

namespace game {

class ScIsomMap;

// Renders SC (StarCraft) terrain onto the engine's 24px square grid so it drops
// straight into the existing top-down renderer/editor with C&C units drawn on
// top. SC megatiles are 32px and get nearest-sampled down to `cellPx` (24). The
// logical grid, pathfinding, and unit placement stay at 24px — only the terrain
// art comes from StarCraft. Rendered megatiles are cached by id, so repainting
// the whole map each edit stays cheap.
class ScTerrainRenderer {
  public:
    explicit ScTerrainRenderer(const fmt::ScTileset& ts, int cellPx = 24)
        : ts_(ts), cellPx_(cellPx) {}

    // Blit the whole compiled map into `c`, cell (tx,ty) landing at canvas
    // (dstX0 + tx*cellPx, dstY0 + ty*cellPx). Cells whose group is 0 (null) are
    // left untouched (transparent) so a background shows through.
    void draw(Canvas& c, const ScIsomMap& map, int dstX0, int dstY0);

  private:
    const uint8_t* cellRgb(int megaId);  // cellPx*cellPx*3, sampled & cached

    const fmt::ScTileset& ts_;
    int cellPx_;
    std::unordered_map<int, std::vector<uint8_t>> cache_;
};

}  // namespace game
