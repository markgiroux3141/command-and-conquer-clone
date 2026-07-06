#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "formats/palette.h"

namespace fmt {

// Westwood CPS full-screen image: 320x200 8-bit indexed, LCW-compressed,
// with an optional embedded palette.
struct CpsFile {
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;

    std::vector<uint8_t> pixels; // kWidth*kHeight palette indices
    bool hasPalette = false;
    Palette palette;

    uint8_t at(int x, int y) const { return pixels[y * kWidth + x]; }

    static CpsFile load(const std::string& path);
};

} // namespace fmt
