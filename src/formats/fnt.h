#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fmt {

// A Westwood .FNT bitmap font (TD/RA "new font format", FontDataBlocks == 5).
// Each glyph is a small block of 4-bit palette-color values: value 0 is
// transparent, value 1 is the "foreground" (recolored at draw time), values
// 2-15 are literal palette indices. See WIN32LIB TXTPRNT.ASM / SET_FONT.CPP.
struct FntFile {
    struct Glyph {
        int width = 0;   // pixel width of the glyph
        int height = 0;  // rows of actual data (blank top/bottom are implicit)
        int yOffset = 0; // blank rows above the data (baseline alignment)
        // width*height color values (0 transparent, 1 foreground, else literal).
        std::vector<uint8_t> pixels;
    };

    int maxHeight = 0; // full cell height (yOffset + data + bottom blank)
    int maxWidth = 0;
    std::vector<Glyph> glyphs; // indexed by character code

    static FntFile load(const std::string& path);

    const Glyph* glyph(unsigned char c) const {
        return c < glyphs.size() ? &glyphs[c] : nullptr;
    }
};

} // namespace fmt
