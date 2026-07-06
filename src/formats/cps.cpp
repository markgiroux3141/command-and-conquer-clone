// CPS image decoder. Layout: u16 fileSize-2, u16 compression (4 = LCW),
// u32 uncompressedSize (64000), u16 paletteSize (0 or 768), then the palette
// bytes if present, then the compressed image data.

#include "formats/cps.h"

#include "formats/lcw.h"

#include <fstream>
#include <stdexcept>

namespace fmt {

CpsFile CpsFile::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open CPS: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 10)
        throw std::runtime_error("CPS too short: " + path);

    auto u16 = [&](size_t off) { return uint16_t(data[off] | (data[off + 1] << 8)); };
    uint16_t compression = u16(2);
    uint32_t imageSize = uint32_t(u16(4)) | (uint32_t(u16(6)) << 16);
    uint16_t paletteSize = u16(8);

    if (compression != 4)
        throw std::runtime_error("CPS: unsupported compression: " + path);
    if (imageSize != kWidth * kHeight)
        throw std::runtime_error("CPS: unexpected image size: " + path);
    if (data.size() < 10 + paletteSize)
        throw std::runtime_error("CPS: palette truncated: " + path);

    CpsFile cps;
    if (paletteSize == 768) {
        cps.hasPalette = true;
        auto expand = [](uint8_t v) { return uint8_t((v << 2) | (v >> 4)); };
        for (int i = 0; i < 256; i++)
            cps.palette.colors[i] = {expand(data[10 + i * 3]), expand(data[10 + i * 3 + 1]),
                                     expand(data[10 + i * 3 + 2])};
    } else if (paletteSize != 0) {
        throw std::runtime_error("CPS: odd palette size: " + path);
    }

    cps.pixels.assign(imageSize, 0);
    lcwDecode(data, 10 + paletteSize, cps.pixels);
    return cps;
}

} // namespace fmt
