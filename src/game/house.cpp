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

int tdHouseIndex(const std::string& houseName) {
    static const std::unordered_map<std::string, int> kIndex = {
        {"goodguy", 0}, {"badguy", 1}, {"neutral", 2}, {"special", 3},
        {"jp", 3},      {"multi1", 4}, {"multi2", 5},  {"multi3", 6},
        {"multi4", 7},  {"multi5", 8}, {"multi6", 9},
    };
    std::string key = houseName;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    auto it = kIndex.find(key);
    return it != kIndex.end() ? it->second : 0;
}

RemapTable tdRemap(const std::string& houseName) {
    // TD remaps are identity everywhere except the 16-entry player-color
    // placeholder band at indices 176-191. These bands are the exact targets
    // from CONST.CPP (RemapYellow/Red/BlueGreen/Orange/Green/Blue/None).
    static const uint8_t kYellow[16] = {176, 177, 178, 179, 180, 181, 182, 183,
                                        184, 185, 186, 187, 188, 189, 190, 191};
    static const uint8_t kRed[16] = {127, 126, 125, 124, 122, 46, 120, 47,
                                     125, 124, 123, 122, 42, 121, 120, 120};
    static const uint8_t kBlueGreen[16] = {2,   119, 118, 135, 136, 138, 112, 12,
                                           118, 135, 136, 137, 138, 139, 114, 112};
    static const uint8_t kOrange[16] = {24, 25, 26, 27, 29, 31, 46, 47,
                                        26, 27, 28, 29, 30, 31, 43, 47};
    static const uint8_t kGreen[16] = {5,   165, 166, 167, 159, 142, 140, 199,
                                       166, 167, 157, 3,   159, 143, 142, 141};
    static const uint8_t kBlue[16] = {161, 200, 201, 202, 204, 205, 206, 12,
                                      201, 202, 203, 204, 205, 115, 198, 114};

    // House -> band. Per HDATA.CPP defaults, except BadGuy (Nod), whose source
    // default is Blue; we use Red to match the familiar GDI-gold/Nod-red
    // convention (and the Remastered Collection). Neutral/Special = identity.
    int idx = tdHouseIndex(houseName);
    const uint8_t* band = kYellow;
    switch (idx) {
    case 1: band = kRed; break;        // BadGuy / Nod
    case 4: band = kBlueGreen; break;  // Multi1
    case 5: band = kOrange; break;     // Multi2
    case 6: band = kGreen; break;      // Multi3
    case 7: band = kBlue; break;       // Multi4
    case 9: band = kRed; break;        // Multi6
    default: band = kYellow; break;    // GoodGuy, Neutral, Special, Multi5
    }

    RemapTable table;
    for (int i = 0; i < 256; i++)
        table[i] = uint8_t(i);
    for (int k = 0; k < 16; k++)
        table[176 + k] = band[k];
    return table;
}

} // namespace game
