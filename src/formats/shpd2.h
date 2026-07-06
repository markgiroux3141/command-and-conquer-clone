#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fmt {

// Dune II-style SHP, used in RA for mouse.shp cursor art. Unlike the TD/RA
// SHP, frames have individual dimensions.
struct ShpD2File {
    struct Frame {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels; // width*height palette indices, 0 transparent
    };
    std::vector<Frame> frames;

    static ShpD2File load(const std::string& path);
};

} // namespace fmt
