#include "game/house.h"

#include "formats/cps.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace game {

std::array<RemapTable, size_t(PlayerColor::Count)> buildRemaps(
    const std::string& paletteCpsPath) {
    fmt::CpsFile cps = fmt::CpsFile::load(paletteCpsPath);

    std::array<RemapTable, size_t(PlayerColor::Count)> remaps;
    for (size_t color = 0; color < remaps.size(); color++) {
        auto& table = remaps[color];
        for (int i = 0; i < 256; i++)
            table[i] = uint8_t(i);
        // INIT.CPP Init_Color_Remaps: pixel (i,0) is the source index,
        // pixel (i,color) the replacement.
        for (int i = 0; i < 16; i++)
            table[cps.at(i, 0)] = cps.at(i, int(color));
    }
    return remaps;
}

PlayerColor houseColor(const std::string& houseName) {
    static const std::unordered_map<std::string, PlayerColor> kColors = {
        {"england", PlayerColor::Green},   {"germany", PlayerColor::Grey},
        {"france", PlayerColor::Blue},     {"ukraine", PlayerColor::Orange},
        {"ussr", PlayerColor::Red},        {"greece", PlayerColor::LtBlue},
        {"turkey", PlayerColor::Brown},    {"spain", PlayerColor::Gold},
        {"goodguy", PlayerColor::LtBlue},  {"badguy", PlayerColor::Red},
        {"neutral", PlayerColor::Gold},    {"special", PlayerColor::Gold},
        {"multi1", PlayerColor::Gold},     {"multi2", PlayerColor::LtBlue},
        {"multi3", PlayerColor::Red},      {"multi4", PlayerColor::Green},
        {"multi5", PlayerColor::Orange},   {"multi6", PlayerColor::Grey},
        {"multi7", PlayerColor::Blue},     {"multi8", PlayerColor::Brown},
    };
    std::string key = houseName;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    auto it = kColors.find(key);
    return it != kColors.end() ? it->second : PlayerColor::Gold;
}

} // namespace game
