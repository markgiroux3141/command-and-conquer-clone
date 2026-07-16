// scisomview — reproduce the ISOM paint demo headlessly and dump a BMP.
// Mirrors tools/sc_isom.py's main(): a blank dirt map with a high-dirt plateau
// (cliff), a water lake (coastline), and a grass field, all auto-tiled. Used to
// validate the C++ ISOM port against renders/sc/isom_paint_demo.png.
//
//   scisomview [tileset-dir] [--out demo.bmp] [--size N]

#include "formats/sc_tileset.h"
#include "game/sc_isom.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void writeBmp(const std::string& path, int w, int h,
              const std::vector<uint8_t>& rgb) {
    int rowBytes = (w * 3 + 3) & ~3;
    uint32_t imgSize = uint32_t(rowBytes) * h;
    std::vector<uint8_t> hdr(54, 0);
    hdr[0] = 'B'; hdr[1] = 'M';
    auto put32 = [&](int o, uint32_t v) {
        hdr[o] = v & 0xFF; hdr[o + 1] = (v >> 8) & 0xFF;
        hdr[o + 2] = (v >> 16) & 0xFF; hdr[o + 3] = (v >> 24) & 0xFF;
    };
    auto put16 = [&](int o, uint16_t v) { hdr[o] = v & 0xFF; hdr[o + 1] = (v >> 8) & 0xFF; };
    put32(2, 54 + imgSize); put32(10, 54); put32(14, 40);
    put32(18, uint32_t(w)); put32(22, uint32_t(h));
    put16(26, 1); put16(28, 24); put32(34, imgSize);
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write: " + path);
    f.write(reinterpret_cast<const char*>(hdr.data()), 54);
    std::vector<uint8_t> row(rowBytes, 0);
    for (int y = h - 1; y >= 0; --y) {
        const uint8_t* src = rgb.data() + size_t(y) * w * 3;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 3 + 2];
            row[x * 3 + 1] = src[x * 3 + 1];
            row[x * 3 + 2] = src[x * 3 + 0];
        }
        f.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = "data/assets/starcraft/tileset/TileSet";
    std::string out = "renders/sc/cpp_isom_paint_demo.bmp";
    int size = 48;
    bool repair = false;   // --repair: run the null-junction quality pass
    int grassX = 20, grassY = 24;  // --grass X Y: move the grass field
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--size" && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (a == "--repair") repair = true;
        else if (a == "--grass" && i + 2 < argc) {
            grassX = std::atoi(argv[++i]); grassY = std::atoi(argv[++i]);
        } else if (a[0] != '-') dir = a;
    }

    try {
        auto ts = fmt::ScTileset::load(dir, "badlands");
        game::ScIsom tiles(ts, game::badlandsBrush());
        int typed = 0;
        for (const auto& g : ts.groups) if (g.terrainType > 0) ++typed;
        std::printf("isomLinks: %zu entries, %d typed groups\n",
                    tiles.isomLinks.size(), typed);

        int W = size, H = size;
        game::ScIsomMap m(tiles, W, H);
        m.fill(2);                       // Dirt background
        m.place(12, 12, 3, 8);           // high dirt plateau (cliff)
        m.place(12, 36, 5, 8);           // water lake (coastline)
        m.place(grassX, grassY, 6, 7);   // grass field
        m.setAllChanged();
        m.updateTiles();
        int nulls = m.nullTileCount();
        std::printf("null (group-0) tiles after compile: %d\n", nulls);
        if (repair) {
            m.repairNullTiles();
            std::printf("null tiles after repair pass: %d\n", m.nullTileCount());
        }

        // Render compiled tiles.
        const auto& tv = m.tiles();
        std::vector<uint8_t> img(size_t(W) * 32 * H * 32 * 3, 0);
        int imgW = W * 32;
        std::vector<uint8_t> mt;
        for (int ty = 0; ty < H; ++ty) {
            for (int tx = 0; tx < W; ++tx) {
                uint16_t cell = tv[size_t(ty) * W + tx];
                int group = cell / 16, sub = cell % 16;
                if (group >= ts.numGroups()) continue;
                int mega = ts.groups[group].megas[sub];
                ts.renderMegatile(mega, mt);
                for (int y = 0; y < 32; ++y) {
                    uint8_t* dst = img.data() +
                        (size_t(ty * 32 + y) * imgW + tx * 32) * 3;
                    std::memcpy(dst, mt.data() + size_t(y) * 32 * 3, 32 * 3);
                }
            }
        }
        writeBmp(out, imgW, H * 32, img);
        std::printf("wrote %s (%dx%d)\n", out.c_str(), imgW, H * 32);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
