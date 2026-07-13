// FNT (Westwood bitmap font) decoder. Header layout, per TXTPRNT.ASM:
//   u16 fileLength; u8 compress(0); u8 dataBlocks(5);
//   u16 infoBlockOff; u16 offsetBlockOff; u16 widthBlockOff;
//   u16 dataBlockOff; u16 heightBlockOff;
// infoBlock+4 = maxHeight, infoBlock+5 = maxWidth.
// offsetBlock[c] (u16) = absolute file offset to glyph c's packed 4-bit pixels.
// widthBlock[c]  (u8)  = glyph pixel width.
// heightBlock[c] = { u8 topBlank, u8 dataRows }.
// Pixels are two per byte (low nibble first), ceil(width/2) bytes per row.

#include "formats/fnt.h"

#include <fstream>
#include <stdexcept>

namespace fmt {
namespace {

uint16_t rdU16(const std::vector<uint8_t>& d, size_t off) {
    if (off + 1 >= d.size())
        throw std::runtime_error("FNT: read past end of file");
    return uint16_t(d[off] | (d[off + 1] << 8));
}

} // namespace

FntFile FntFile::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("FNT: cannot open " + path);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    if (d.size() < 14)
        throw std::runtime_error("FNT: file too small: " + path);
    if (d[2] != 0)
        throw std::runtime_error("FNT: compressed fonts unsupported: " + path);

    const size_t infoOff = rdU16(d, 4);
    const size_t offsetOff = rdU16(d, 6);
    const size_t widthOff = rdU16(d, 8);
    const size_t heightOff = rdU16(d, 12);
    if (offsetOff >= widthOff || widthOff > d.size())
        throw std::runtime_error("FNT: bad block offsets: " + path);

    FntFile fnt;
    fnt.maxHeight = d.at(infoOff + 4);
    fnt.maxWidth = d.at(infoOff + 5);

    const int numChars = int((widthOff - offsetOff) / 2);
    fnt.glyphs.resize(numChars);
    for (int c = 0; c < numChars; c++) {
        Glyph& g = fnt.glyphs[c];
        g.width = d.at(widthOff + c);
        g.yOffset = d.at(heightOff + c * 2);
        g.height = d.at(heightOff + c * 2 + 1);
        size_t src = rdU16(d, offsetOff + c * 2);
        g.pixels.assign(size_t(g.width) * g.height, 0);
        for (int row = 0; row < g.height; row++) {
            for (int col = 0; col < g.width; col += 2) {
                if (src >= d.size())
                    throw std::runtime_error("FNT: glyph data past end: " + path);
                uint8_t b = d[src++];
                g.pixels[row * g.width + col] = b & 0x0f;
                if (col + 1 < g.width)
                    g.pixels[row * g.width + col + 1] = b >> 4;
            }
        }
    }
    return fnt;
}

} // namespace fmt
