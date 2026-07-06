#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace fmt {

// A Westwood .PAL file: 256 RGB triplets with 6-bit color channels.
struct Palette {
    struct Color { uint8_t r, g, b; };
    std::array<Color, 256> colors{};

    static Palette load(const std::string& path);
};

} // namespace fmt
