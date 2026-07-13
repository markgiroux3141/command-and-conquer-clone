// Terrain template decoder for both Red Alert and Tiberian Dawn, ported from
// OpenRA's TmpRALoader.cs / TmpTDLoader.cs (GPL v3). The two games use
// different header layouts, distinguished by magic; both then share the same
// index-table + raw-pixel scheme (one byte per slot = image number or 0xFF).
//
// Red Alert header (little-endian):
//   0  u16 width   2  u16 height   16 u32 imgStart
//   20 u32 zero, 26 u16 magic 0x2c73
//   28 s32 indexEnd   36 s32 indexStart
//   32 u32 colorMap (land-type byte per slot, IControl_Type::ColorMap)
//
// Tiberian Dawn header (little-endian):
//   0  u16 width   2  u16 height   12 u32 imgStart
//   16 u32 zero, 20 u32 magic 0x0d1affff
//   24 s32 indexEnd   28 s32 indexStart   (no land-type map)

#include "formats/tmp.h"

#include <fstream>
#include <stdexcept>

namespace fmt {

TmpFile TmpFile::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open TMP: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    auto u16 = [&](size_t off) { return uint16_t(data.at(off) | (data.at(off + 1) << 8)); };
    auto u32 = [&](size_t off) {
        return uint32_t(u16(off)) | (uint32_t(u16(off + 2)) << 16);
    };

    if (data.size() < 40)
        throw std::runtime_error("TMP too short: " + path);

    bool isRa = u32(20) == 0 && u16(26) == 0x2c73;
    bool isTd = u32(16) == 0 && u32(20) == 0x0d1affff;
    if (!isRa && !isTd)
        throw std::runtime_error("not a TMP template (bad magic): " + path);

    TmpFile tmp;
    tmp.tileWidth = u16(0);
    tmp.tileHeight = u16(2);
    size_t tileSize = size_t(tmp.tileWidth) * tmp.tileHeight;
    uint32_t imgStart = isTd ? u32(12) : u32(16);
    uint32_t indexEnd = isTd ? u32(24) : u32(28);
    uint32_t indexStart = isTd ? u32(28) : u32(36);
    if (tileSize == 0 || indexStart > indexEnd || indexEnd > data.size())
        throw std::runtime_error("TMP header out of range: " + path);

    if (isRa) {
        uint32_t colorMap = u32(32);
        uint32_t slots = indexEnd - indexStart;
        if (colorMap && colorMap + slots <= data.size())
            tmp.landBytes.assign(data.begin() + colorMap, data.begin() + colorMap + slots);
    }

    for (uint32_t i = indexStart; i < indexEnd; i++) {
        uint8_t b = data[i];
        if (b == 0xFF) {
            tmp.tiles.emplace_back(); // empty slot
            continue;
        }
        size_t off = imgStart + size_t(b) * tileSize;
        if (off + tileSize > data.size())
            throw std::runtime_error("TMP tile out of range: " + path);
        tmp.tiles.emplace_back(data.begin() + off, data.begin() + off + tileSize);
    }
    return tmp;
}

} // namespace fmt
