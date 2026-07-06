// Dune II SHP decoder, ported from OpenRA's ShpD2Loader.cs and
// RLEZerosCompression.cs (GPL v3).
//
// Layout: u16 imageCount, then imageCount+1 offsets (u16 or u32 — detected
// from the high bytes of the first u32; the extra final offset points at end
// of file). Offsets are relative to +2. Each frame: u16 flags, u8 slices,
// u16 width, u8 height, u16 frameBytes, u16 dataSize, optional palette
// lookup table, then image data (LCW unless flag 2, then RLE-zeros).

#include "formats/shpd2.h"

#include "formats/lcw.h"

#include <fstream>
#include <stdexcept>

namespace fmt {
namespace {

// Format2: runs of zeros are encoded as 0x00 <count>; other bytes literal.
void rleZerosDecode(const std::vector<uint8_t>& src, std::vector<uint8_t>& dest) {
    size_t di = 0;
    for (size_t si = 0; si < src.size();) {
        uint8_t cmd = src[si++];
        if (cmd == 0) {
            if (si >= src.size())
                break;
            uint8_t count = src[si++];
            if (di + count > dest.size())
                throw std::runtime_error("SHP D2: RLE overflow");
            di += count; // dest is zero-initialized
        } else {
            if (di >= dest.size())
                throw std::runtime_error("SHP D2: RLE overflow");
            dest[di++] = cmd;
        }
    }
}

} // namespace

ShpD2File ShpD2File::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open SHP D2: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    auto u16 = [&](size_t off) {
        if (off + 2 > data.size())
            throw std::runtime_error("SHP D2 truncated: " + path);
        return uint16_t(data[off] | (data[off + 1] << 8));
    };
    auto u32 = [&](size_t off) {
        return uint32_t(u16(off)) | (uint32_t(u16(off + 2)) << 16);
    };

    size_t imageCount = u16(0);
    if (imageCount == 0)
        throw std::runtime_error("SHP D2 has no frames: " + path);

    // Two-byte offsets if the first u32's high bytes are in use.
    bool twoByteOffsets = (u32(2) & 0xff0000) != 0;
    std::vector<size_t> offsets(imageCount + 1);
    for (size_t i = 0; i < offsets.size(); i++)
        offsets[i] = (twoByteOffsets ? u32(2 + i * 2) & 0xffff : u32(2 + i * 4)) + 2;

    ShpD2File shp;
    shp.frames.reserve(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        size_t p = offsets[i];
        uint16_t flags = u16(p);
        // +2 skips the slice-count byte nobody needs.
        int width = u16(p + 3);
        int height = data.at(p + 5);
        size_t dataLeft = size_t(u16(p + 6)) - 10;
        size_t dataSize = u16(p + 8);
        p += 10;

        // Palette lookup table: explicit when flag 1, else identity with the
        // first shadow indices patched in.
        std::vector<uint8_t> table;
        if (flags & 1) {
            size_t n = (flags & 4) ? data.at(p++) : 16;
            if (flags & 4)
                dataLeft -= 1;
            table.assign(data.begin() + p, data.begin() + p + n);
            table.resize(256, 0);
            p += n;
            dataLeft -= n;
        } else {
            table.resize(256);
            for (int j = 0; j < 256; j++)
                table[j] = uint8_t(j);
            table[1] = 0x7f;
            table[2] = 0x7e;
            table[3] = 0x7d;
            table[4] = 0x7c;
        }

        if (p + dataLeft > data.size())
            throw std::runtime_error("SHP D2 frame truncated: " + path);
        std::vector<uint8_t> compressed(data.begin() + p, data.begin() + p + dataLeft);
        if ((flags & 2) == 0) {
            std::vector<uint8_t> lcwOut(dataSize, 0);
            lcwDecode(compressed, 0, lcwOut);
            compressed = std::move(lcwOut);
        }

        Frame frame;
        frame.width = width;
        frame.height = height;
        frame.pixels.assign(size_t(width) * height, 0);
        rleZerosDecode(compressed, frame.pixels);
        for (auto& px : frame.pixels)
            px = table[px];
        shp.frames.push_back(std::move(frame));
    }
    return shp;
}

} // namespace fmt
