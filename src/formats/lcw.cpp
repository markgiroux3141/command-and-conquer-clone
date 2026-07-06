// LCW (format80) decoder, ported from OpenRA's LCWCompression.cs (GPL v3).
// Moved out of shp.cpp so the map loader can reuse it.

#include "formats/lcw.h"

#include <cstring>
#include <stdexcept>

namespace fmt {
namespace {

class Reader {
public:
    Reader(const std::vector<uint8_t>& data, size_t pos) : data_(data), pos_(pos) {}

    uint8_t byte() {
        if (pos_ >= data_.size())
            throw std::runtime_error("LCW: read past end of input");
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

} // namespace

void lcwDecode(const std::vector<uint8_t>& src, size_t srcOffset, std::vector<uint8_t>& dest) {
    Reader ctx(src, srcOffset);
    size_t destIndex = 0;

    auto checkSpace = [&](size_t count) {
        if (destIndex + count > dest.size())
            throw std::runtime_error("LCW: output overflow");
    };

    while (true) {
        uint8_t i = ctx.byte();
        if ((i & 0x80) == 0) {
            // copy `count` bytes from `rpos` bytes back in the output
            size_t count = ((i & 0x70) >> 4) + 3;
            size_t rpos = ((i & 0x0f) << 8) + ctx.byte();
            if (destIndex + count > dest.size())
                return; // matches OpenRA: final block may overrun, stop there
            if (rpos > destIndex)
                throw std::runtime_error("LCW: copy before start of output");
            for (size_t n = 0; n < count; n++, destIndex++)
                dest[destIndex] = dest[destIndex - rpos];
        } else if ((i & 0x40) == 0) {
            // literal run; count 0 terminates the stream
            size_t count = i & 0x3f;
            if (count == 0)
                return;
            checkSpace(count);
            for (size_t n = 0; n < count; n++)
                dest[destIndex++] = ctx.byte();
        } else {
            size_t count3 = i & 0x3f;
            if (count3 == 0x3e) {
                // RLE fill
                size_t count = ctx.word();
                uint8_t color = ctx.byte();
                checkSpace(count);
                std::memset(dest.data() + destIndex, color, count);
                destIndex += count;
            } else {
                // copy from an absolute position in the output
                size_t count = count3 == 0x3f ? ctx.word() : count3 + 3;
                size_t srcIndex = ctx.word();
                if (srcIndex >= destIndex)
                    throw std::runtime_error("LCW: forward reference");
                checkSpace(count);
                for (size_t n = 0; n < count; n++)
                    dest[destIndex++] = dest[srcIndex++];
            }
        }
    }
}

} // namespace fmt
