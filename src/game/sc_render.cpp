// SC terrain -> 24px grid renderer. See sc_render.h.

#include "game/sc_render.h"

#include "game/sc_isom.h"

namespace game {

const uint8_t* ScTerrainRenderer::cellRgb(int megaId) {
    auto it = cache_.find(megaId);
    if (it != cache_.end()) return it->second.data();

    std::vector<uint8_t> mt;  // 32*32*3
    ts_.renderMegatile(megaId, mt);
    std::vector<uint8_t> cell(size_t(cellPx_) * cellPx_ * 3);
    const int M = fmt::ScTileset::MEGA;  // 32
    for (int y = 0; y < cellPx_; ++y) {
        int sy = y * M / cellPx_;
        for (int x = 0; x < cellPx_; ++x) {
            int sx = x * M / cellPx_;
            const uint8_t* s = mt.data() + (size_t(sy) * M + sx) * 3;
            uint8_t* d = cell.data() + (size_t(y) * cellPx_ + x) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    }
    return cache_.emplace(megaId, std::move(cell)).first->second.data();
}

void ScTerrainRenderer::draw(Canvas& c, const ScIsomMap& map, int dstX0, int dstY0) {
    const auto& tiles = map.tiles();
    int W = map.tileW(), H = map.tileH();
    for (int ty = 0; ty < H; ++ty) {
        for (int tx = 0; tx < W; ++tx) {
            uint16_t cell = tiles[size_t(ty) * W + tx];
            int group = cell / 16, sub = cell % 16;
            if (group == 0 || group >= ts_.numGroups()) continue;  // null: skip
            int mega = ts_.groups[group].megas[sub];
            const uint8_t* rgb = cellRgb(mega);
            int px0 = dstX0 + tx * cellPx_, py0 = dstY0 + ty * cellPx_;
            for (int y = 0; y < cellPx_; ++y) {
                int cy = py0 + y;
                if (cy < c.clipY0 || cy >= c.clipY1 || cy < 0 || cy >= c.h) continue;
                uint32_t* row = c.px + size_t(cy) * c.pitch;
                const uint8_t* src = rgb + size_t(y) * cellPx_ * 3;
                for (int x = 0; x < cellPx_; ++x) {
                    int cx = px0 + x;
                    if (cx < 0 || cx >= c.w) continue;
                    const uint8_t* s = src + x * 3;
                    row[cx] = 0xFF000000u | (uint32_t(s[0]) << 16) |
                              (uint32_t(s[1]) << 8) | uint32_t(s[2]);
                }
            }
        }
    }
}

}  // namespace game
