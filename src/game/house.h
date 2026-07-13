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

// --- Tiberian Dawn house colors ---
// TD's HousesType index for a scenario house name (GoodGuy=0/GDI, BadGuy=1/Nod,
// Neutral=2, Special/JP=3, Multi1..6=4..9). Unknown names return 0.
int tdHouseIndex(const std::string& houseName);

// TD unit/structure art carries the player color in a placeholder band at
// palette indices 176-191; each house remaps that band to the 16-color block
// at (houseIndex+11)*16 (DisplayClass::One_Time). Returns a 256-entry table,
// identity outside the band. No PALETTE.CPS needed (TD builds this in code).
RemapTable tdRemap(const std::string& houseName);

} // namespace game
