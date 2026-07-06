// SHP (TD/RA) sprite decoder, ported from OpenRA's ShpTDSprite.cs,
// LCWCompression.cs and XORDeltaCompression.cs (GPL v3).
//
// File layout:
//   u16 imageCount, u16 x, u16 y, u16 width, u16 height, u32 largestFrame
//   imageCount+2 image headers of 8 bytes:
//     u32 lo24=fileOffset hi8=format (0x80 LCW, 0x40 XOR vs referenced frame,
//                                     0x20 XOR vs previous frame)
//     u16 refOffset, u16 refFormat
//   ...frame data (offsets are absolute within the file)

#include "formats/shp.h"

#include "formats/lcw.h"

#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace fmt {
namespace {

class Reader {
public:
    Reader(const std::vector<uint8_t>& data, size_t pos) : data_(data), pos_(pos) {}

    uint8_t byte() {
        if (pos_ >= data_.size())
            throw std::runtime_error("SHP: read past end of file");
        return data_[pos_++];
    }

    uint16_t word() {
        uint16_t lo = byte();
        return uint16_t(lo | (byte() << 8));
    }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_;
};

// Format40: XOR patch applied over a base frame already present in dest.
void xorDeltaDecode(const std::vector<uint8_t>& src, size_t srcOffset, std::vector<uint8_t>& dest) {
    Reader ctx(src, srcOffset);
    size_t destIndex = 0;

    auto checkSpace = [&](size_t count) {
        if (destIndex + count > dest.size())
            throw std::runtime_error("SHP: XOR delta output overflow");
    };

    while (true) {
        uint8_t i = ctx.byte();
        if ((i & 0x80) == 0) {
            size_t count = i & 0x7f;
            if (count == 0) {
                // XOR the next byte's value `count` times
                count = ctx.byte();
                uint8_t value = ctx.byte();
                checkSpace(count);
                for (size_t n = 0; n < count; n++)
                    dest[destIndex++] ^= value;
            } else {
                // XOR `count` literal bytes
                checkSpace(count);
                for (size_t n = 0; n < count; n++)
                    dest[destIndex++] ^= ctx.byte();
            }
        } else {
            size_t count = i & 0x7f;
            if (count == 0) {
                count = ctx.word();
                if (count == 0)
                    return;
                if ((count & 0x8000) == 0) {
                    destIndex += count & 0x7fff; // long skip
                } else if ((count & 0x4000) == 0) {
                    count &= 0x3fff; // long literal XOR run
                    checkSpace(count);
                    for (size_t n = 0; n < count; n++)
                        dest[destIndex++] ^= ctx.byte();
                } else {
                    count &= 0x3fff; // long single-value XOR run
                    uint8_t value = ctx.byte();
                    checkSpace(count);
                    for (size_t n = 0; n < count; n++)
                        dest[destIndex++] ^= value;
                }
            } else {
                destIndex += count; // short skip
            }
        }
    }
}

struct ImageHeader {
    uint32_t fileOffset = 0;
    uint8_t format = 0;
    uint16_t refOffset = 0;
    int refIndex = -1; // resolved reference frame
    bool decoded = false;
};

} // namespace

ShpFile ShpFile::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open SHP: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    auto u16 = [&](size_t off) { return uint16_t(data.at(off) | (data.at(off + 1) << 8)); };
    auto u32 = [&](size_t off) {
        return uint32_t(u16(off)) | (uint32_t(u16(off + 2)) << 16);
    };

    if (data.size() < 14)
        throw std::runtime_error("SHP too short: " + path);

    ShpFile shp;
    size_t imageCount = u16(0);
    shp.width = u16(6);
    shp.height = u16(8);
    size_t frameSize = size_t(shp.width) * shp.height;
    if (imageCount == 0 || frameSize == 0)
        throw std::runtime_error("SHP has no frames: " + path);
    if (data.size() < 14 + (imageCount + 2) * 8)
        throw std::runtime_error("SHP header truncated: " + path);

    std::vector<ImageHeader> headers(imageCount);
    std::unordered_map<uint32_t, int> byOffset;
    for (size_t i = 0; i < imageCount; i++) {
        size_t off = 14 + i * 8;
        uint32_t v = u32(off);
        headers[i].fileOffset = v & 0xffffff;
        headers[i].format = uint8_t(v >> 24);
        headers[i].refOffset = u16(off + 4);
        byOffset[headers[i].fileOffset] = int(i);
    }

    for (size_t i = 0; i < imageCount; i++) {
        auto& h = headers[i];
        if (h.format == 0x20) {
            h.refIndex = int(i) - 1;
        } else if (h.format == 0x40) {
            auto it = byOffset.find(h.refOffset);
            if (it == byOffset.end())
                throw std::runtime_error("SHP: XOR reference points nowhere: " + path);
            h.refIndex = it->second;
        }
    }

    shp.frames.assign(imageCount, {});

    // Frames may reference other frames; decode on demand, depth-limited.
    auto decode = [&](auto&& self, size_t i, int depth) -> void {
        auto& h = headers[i];
        if (h.decoded)
            return;
        if (depth > int(imageCount))
            throw std::runtime_error("SHP: reference loop: " + path);

        auto& out = shp.frames[i];
        switch (h.format) {
        case 0x80:
            out.assign(frameSize, 0);
            lcwDecode(data, h.fileOffset, out);
            break;
        case 0x40:
        case 0x20:
            if (h.refIndex < 0)
                throw std::runtime_error("SHP: frame 0 cannot reference previous: " + path);
            self(self, size_t(h.refIndex), depth + 1);
            out = shp.frames[h.refIndex]; // copy base, then XOR-patch
            xorDeltaDecode(data, h.fileOffset, out);
            break;
        default:
            throw std::runtime_error("SHP: unknown frame format: " + path);
        }
        h.decoded = true;
    };

    for (size_t i = 0; i < imageCount; i++)
        decode(decode, i, 0);

    return shp;
}

} // namespace fmt
