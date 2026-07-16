// sctileview — decode a StarCraft tileset and dump a megatile sheet BMP.
// Mirrors tools/sc_tiles.py's default megatile dump so the C++ decode can be
// compared pixel-for-pixel against the Python reference.
//
//   sctileview <tileset-dir> <name> [--count N] [--cols N] [--out sheet.bmp]
//   sctileview data/assets/starcraft/tileset/TileSet badlands --count 256
//
// SC assets are personal-use only (gitignored under data/).

#include "formats/sc_tileset.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Minimal 24-bit BMP writer. `rgb` is row-major top-to-bottom, 3 B/px.
void writeBmp(const std::string& path, int w, int h,
              const std::vector<uint8_t>& rgb) {
    int rowBytes = (w * 3 + 3) & ~3;  // padded to 4-byte boundary
    uint32_t imgSize = uint32_t(rowBytes) * h;
    uint32_t fileSize = 54 + imgSize;
    std::vector<uint8_t> hdr(54, 0);
    hdr[0] = 'B'; hdr[1] = 'M';
    auto put32 = [&](int o, uint32_t v) {
        hdr[o] = v & 0xFF; hdr[o + 1] = (v >> 8) & 0xFF;
        hdr[o + 2] = (v >> 16) & 0xFF; hdr[o + 3] = (v >> 24) & 0xFF;
    };
    auto put16 = [&](int o, uint16_t v) {
        hdr[o] = v & 0xFF; hdr[o + 1] = (v >> 8) & 0xFF;
    };
    put32(2, fileSize);
    put32(10, 54);       // pixel data offset
    put32(14, 40);       // DIB header size
    put32(18, uint32_t(w));
    put32(22, uint32_t(h));
    put16(26, 1);        // planes
    put16(28, 24);       // bpp
    put32(34, imgSize);

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write: " + path);
    f.write(reinterpret_cast<const char*>(hdr.data()), 54);
    std::vector<uint8_t> row(rowBytes, 0);
    // BMP rows are bottom-to-top; source is top-to-bottom.
    for (int y = h - 1; y >= 0; --y) {
        const uint8_t* src = rgb.data() + size_t(y) * w * 3;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 3 + 2];  // B
            row[x * 3 + 1] = src[x * 3 + 1];  // G
            row[x * 3 + 2] = src[x * 3 + 0];  // R
        }
        f.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: sctileview <tileset-dir> <name> [--count N] [--cols N] "
            "[--out sheet.bmp]\n");
        return 2;
    }
    std::string dir = argv[1], name = argv[2];
    int count = 256, cols = 32;
    std::string out = "sctile_sheet.bmp";
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
        else if (a == "--cols" && i + 1 < argc) cols = std::atoi(argv[++i]);
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
    }

    try {
        auto ts = fmt::ScTileset::load(dir, name);
        std::printf("%s: %d groups, vx4 stride %d, %d minitiles, %d megatiles\n",
                    ts.name.c_str(), ts.numGroups(), ts.vx4Stride,
                    ts.numMinitiles(), ts.numMegatiles());

        int n = count < ts.numMegatiles() ? count : ts.numMegatiles();
        const int M = fmt::ScTileset::MEGA, pad = 1;
        int rows = (n + cols - 1) / cols;
        int W = cols * (M + pad) + pad;
        int H = rows * (M + pad) + pad;
        std::vector<uint8_t> sheet(size_t(W) * H * 3, 0);
        for (size_t i = 0; i < sheet.size(); i += 3) {  // bg (20,20,28)
            sheet[i] = 20; sheet[i + 1] = 20; sheet[i + 2] = 28;
        }
        std::vector<uint8_t> mt;
        for (int i = 0; i < n; ++i) {
            ts.renderMegatile(i, mt);
            int cx = pad + (i % cols) * (M + pad);
            int cy = pad + (i / cols) * (M + pad);
            for (int y = 0; y < M; ++y) {
                uint8_t* dst = sheet.data() + (size_t(cy + y) * W + cx) * 3;
                std::memcpy(dst, mt.data() + size_t(y) * M * 3, M * 3);
            }
        }
        writeBmp(out, W, H, sheet);
        std::printf("wrote %s (%dx%d, %d megatiles)\n", out.c_str(), W, H, n);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
