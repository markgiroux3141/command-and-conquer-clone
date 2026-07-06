#include "formats/palette.h"

#include <fstream>
#include <stdexcept>

namespace fmt {

Palette Palette::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open palette: " + path);

    uint8_t raw[768];
    f.read(reinterpret_cast<char*>(raw), sizeof(raw));
    if (f.gcount() != sizeof(raw))
        throw std::runtime_error("palette too short: " + path);

    Palette pal;
    for (int i = 0; i < 256; i++) {
        // 6-bit VGA values; replicate top bits into the low bits so
        // 63 maps to 255 exactly.
        auto expand = [](uint8_t v) { return uint8_t((v << 2) | (v >> 4)); };
        pal.colors[i] = {expand(raw[i * 3]), expand(raw[i * 3 + 1]), expand(raw[i * 3 + 2])};
    }
    return pal;
}

} // namespace fmt
