#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace game {

// PlayerColorType from the original DEFINES.H (first 8 are the usable
// unit-remap schemes; the rest are dialog colors).
enum class PlayerColor : uint8_t {
    Gold,
    LtBlue,
    Red,
    Green,
    Orange,
    Grey,
    Blue, // actually a red scheme in the original dialogs; unused for units
    Brown,
    Count,
};

// 256-entry palette-index remap (identity except the 16 house-color entries).
using RemapTable = std::array<uint8_t, 256>;

// Builds the per-color remap tables from PALETTE.CPS exactly like
// Init_Color_Remaps: row 0 of the image lists the 16 source indices, row
// `color` lists the replacement for each.
std::array<RemapTable, size_t(PlayerColor::Count)> buildRemaps(const std::string& paletteCpsPath);

// Default remap color for a scenario house name ("Greece", "USSR", "Multi3",
// ...), per HDATA.CPP. Unknown names get Gold (the Neutral color).
PlayerColor houseColor(const std::string& houseName);

} // namespace game
