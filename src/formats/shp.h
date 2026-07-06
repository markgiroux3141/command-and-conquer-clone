#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fmt {

// A Westwood SHP (TD/RA variant) sprite: a sequence of equally-sized frames of
// 8-bit palette indices. Index 0 is transparent.
struct ShpFile {
    int width = 0;
    int height = 0;
    // Each frame is width*height bytes.
    std::vector<std::vector<uint8_t>> frames;

    static ShpFile load(const std::string& path);
};

} // namespace fmt
