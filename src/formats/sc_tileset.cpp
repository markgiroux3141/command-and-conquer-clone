// StarCraft tileset decoder — see sc_tileset.h. Port of tools/sc_tiles.py.

#include "formats/sc_tileset.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fmt {

namespace {

std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open: " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

uint16_t u16(const std::vector<uint8_t>& d, size_t off) {
    return uint16_t(d.at(off) | (d.at(off + 1) << 8));
}

}  // namespace

ScTileset ScTileset::load(const std::string& dir, const std::string& name) {
    namespace fs = std::filesystem;
    ScTileset ts;
    ts.name = name;
    std::string base = (fs::path(dir) / name).string();

    // WPE: 256 * 4 B (r,g,b,pad).
    auto wpe = readAll(base + ".wpe");
    if (wpe.size() < 256 * 4)
        throw std::runtime_error("wpe too short: " + base + ".wpe");
    ts.palette.resize(256);
    for (int i = 0; i < 256; ++i)
        ts.palette[i] = {wpe[i * 4], wpe[i * 4 + 1], wpe[i * 4 + 2]};

    // VR4: 64 B per minitile.
    ts.vr4 = readAll(base + ".vr4");

    // VX4EX (u32 refs) preferred; fall back to legacy VX4 (u16 refs).
    if (fs::exists(base + ".vx4ex")) {
        ts.vx4 = readAll(base + ".vx4ex");
        ts.vx4Stride = 4;
    } else {
        ts.vx4 = readAll(base + ".vx4");
        ts.vx4Stride = 2;
    }

    // CV5: 52 B tile groups.
    auto cv5 = readAll(base + ".cv5");
    size_t n = cv5.size() / 52;
    ts.groups.resize(n);
    for (size_t i = 0; i < n; ++i) {
        size_t o = i * 52;
        TileGroup& g = ts.groups[i];
        g.terrainType = u16(cv5, o + 0);
        g.build = cv5.at(o + 2);
        g.height = cv5.at(o + 3);
        for (int k = 0; k < 4; ++k) g.links[k] = u16(cv5, o + 4 + k * 2);
        for (int k = 0; k < 4; ++k) g.stack[k] = u16(cv5, o + 12 + k * 2);
        for (int k = 0; k < 16; ++k) g.megas[k] = u16(cv5, o + 20 + k * 2);
    }
    return ts;
}

void ScTileset::blitMinitile(int idx, bool hflip, uint8_t* dst,
                             int dstStrideBytes, int px, int py) const {
    size_t off = size_t(idx) * 64;
    if (off + 64 > vr4.size())
        return;
    for (int y = 0; y < 8; ++y) {
        uint8_t* row = dst + (py + y) * dstStrideBytes + px * 3;
        for (int x = 0; x < 8; ++x) {
            int sx = hflip ? (7 - x) : x;
            uint8_t idx8 = vr4[off + y * 8 + sx];
            const Color& c = palette[idx8];
            row[x * 3 + 0] = c.r;
            row[x * 3 + 1] = c.g;
            row[x * 3 + 2] = c.b;
        }
    }
}

void ScTileset::renderMegatile(int megaId, std::vector<uint8_t>& out) const {
    out.assign(MEGA * MEGA * 3, 0);
    size_t base = size_t(megaId) * 16 * vx4Stride;
    for (int i = 0; i < 16; ++i) {
        size_t o = base + size_t(i) * vx4Stride;
        uint32_t ref;
        if (vx4Stride == 4) {
            if (o + 4 > vx4.size()) return;
            ref = uint32_t(vx4[o]) | (uint32_t(vx4[o + 1]) << 8) |
                  (uint32_t(vx4[o + 2]) << 16) | (uint32_t(vx4[o + 3]) << 24);
        } else {
            if (o + 2 > vx4.size()) return;
            ref = uint32_t(vx4[o]) | (uint32_t(vx4[o + 1]) << 8);
        }
        int miniIdx = ref >> 1;
        bool hflip = ref & 1;
        int mx = (i % 4) * MINI, my = (i / 4) * MINI;
        blitMinitile(miniIdx, hflip, out.data(), MEGA * 3, mx, my);
    }
}

std::vector<uint8_t> ScTileset::renderMegatile(int megaId) const {
    std::vector<uint8_t> out;
    renderMegatile(megaId, out);
    return out;
}

}  // namespace fmt
