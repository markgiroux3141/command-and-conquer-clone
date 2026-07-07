// RA terrain template decoder, ported from OpenRA's TmpRALoader.cs (GPL v3).
//
// Header (little-endian):
//   0  u16 tile width (24)      2  u16 tile height (24)
//   4  ...                     16  u32 imgStart (pixel data)
//   20 u32 zero, 24 u16 ?, 26 u16 magic 0x2c73
//   28 s32 indexEnd            36  s32 indexStart
//   32 u32 colorMap (land-type control byte per slot, IControl_Type::ColorMap)
// Index: one byte per template slot, image number or 0xFF for empty.
// Pixels: raw, imgStart + imageNumber * w * h.

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
    if (u32(20) != 0 || u16(26) != 0x2c73)
        throw std::runtime_error("not an RA TMP (bad magic): " + path);

    TmpFile tmp;
    tmp.tileWidth = u16(0);
    tmp.tileHeight = u16(2);
    size_t tileSize = size_t(tmp.tileWidth) * tmp.tileHeight;
    uint32_t imgStart = u32(16);
    uint32_t indexEnd = u32(28);
    uint32_t indexStart = u32(36);
    if (tileSize == 0 || indexStart > indexEnd || indexEnd > data.size())
        throw std::runtime_error("TMP header out of range: " + path);

    uint32_t colorMap = u32(32);
    uint32_t slots = indexEnd - indexStart;
    if (colorMap && colorMap + slots <= data.size())
        tmp.landBytes.assign(data.begin() + colorMap, data.begin() + colorMap + slots);

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
