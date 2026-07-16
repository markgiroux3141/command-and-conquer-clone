#pragma once
#include <cstdint>
#include <string>
#include <vector>

// StarCraft tileset decoder (cv5 / vx4|vx4ex / vr4 / wpe). Straight port of
// tools/sc_tiles.py's Tileset. SC terrain is built from 8x8 "minitiles" (VR4)
// assembled 4x4 into 32x32 "megatiles" (VX4/VX4EX), grouped into terrain "tile
// groups" (CV5). A map cell references a 16-bit tile id = (group << 4) | subtile,
// so the megatile for a cell is cv5[group].megaTileIndex[subtile].
//
// SC:R ships extended vx4ex (u32 minitile refs, 4 B) rather than legacy vx4
// (u16, 2 B); the loader auto-detects by file presence. SC assets are
// personal-use only and gitignored under data/ — never committed.

namespace fmt {

struct ScTileset {
    struct Color { uint8_t r, g, b; };

    // A cv5 tile group: 52 bytes = 20 B metadata + 16 u16 megatile ids.
    struct TileGroup {
        uint16_t terrainType = 0;
        uint8_t build = 0;
        uint8_t height = 0;
        uint16_t links[4] = {0, 0, 0, 0};  // left, top, right, bottom
        uint16_t stack[4] = {0, 0, 0, 0};  // stackConnections L,T,R,B
        uint16_t megas[16] = {0};          // megatile id per subtile
    };

    static constexpr int MINI = 8;   // minitile edge (px)
    static constexpr int MEGA = 32;  // megatile edge (px)

    std::string name;
    std::vector<Color> palette;      // 256 entries (from wpe)
    std::vector<uint8_t> vr4;        // 64 B per minitile
    std::vector<uint8_t> vx4;        // vx4ex(4)/vx4(2) minitile refs per megatile
    int vx4Stride = 4;               // 4 for vx4ex, 2 for legacy vx4
    std::vector<TileGroup> groups;   // parsed cv5

    static ScTileset load(const std::string& dir, const std::string& name);

    int numGroups() const { return static_cast<int>(groups.size()); }
    int numMegatiles() const {
        return static_cast<int>(vx4.size()) / (16 * vx4Stride);
    }
    int numMinitiles() const { return static_cast<int>(vr4.size()) / 64; }

    // Render megatile mega_id into a 32*32 RGB buffer (row-major, 3 B/px).
    // Appends into `out` starting at offset (out must have room), or use the
    // convenience overload returning a fresh 32*32*3 buffer.
    void renderMegatile(int megaId, std::vector<uint8_t>& out32x32rgb) const;
    std::vector<uint8_t> renderMegatile(int megaId) const;

  private:
    // Write an 8x8 minitile (optionally hflipped) into an RGB buffer at (px,py)
    // with the given row stride (bytes).
    void blitMinitile(int idx, bool hflip, uint8_t* dst, int dstStrideBytes,
                      int px, int py) const;
};

}  // namespace fmt
