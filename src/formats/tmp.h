#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fmt {

// RA terrain template (.tem/.sno/.int): a set of fixed-size tiles (24x24).
// A template is a rectangular arrangement of tiles on the map; individual
// slots can be empty.
struct TmpFile {
    int tileWidth = 0;
    int tileHeight = 0;
    // One entry per template slot; empty vector = empty slot.
    std::vector<std::vector<uint8_t>> tiles;

    static TmpFile load(const std::string& path);
};

} // namespace fmt
