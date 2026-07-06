#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace game {

// A Red Alert scenario/skirmish map loaded from its INI file.
// The terrain grid is always 128x128 cells; [Map] X/Y/Width/Height give the
// playable bounds within it.
struct MapFile {
    static constexpr int kSize = 128;

    struct Cell {
        uint16_t templateId = 0xffff; // 0xffff = clear
        uint8_t icon = 0;             // tile index within the template
        uint8_t overlay = 0xff;       // OverlayType, 0xff = none
    };

    std::string theater; // uppercased, e.g. "TEMPERATE", "SNOW", "INTERIOR"
    int x = 0, y = 0, width = 0, height = 0; // playable bounds in cells

    std::vector<Cell> cells; // kSize*kSize, row-major: cells[y*kSize + x]

    struct TerrainObject {
        int cell = 0;     // y*kSize + x
        std::string name; // art name, e.g. "t01", "tc04" (SHP with theater ext)
    };
    std::vector<TerrainObject> terrain;

    static MapFile load(const std::string& iniPath);

    // Theater art extension: ".tem", ".sno" or ".int".
    std::string theaterExt() const;
    // Theater palette base name, e.g. "temperat" -> temperat.pal.
    std::string theaterPalette() const;
};

// OverlayType names in enum order (index = overlay byte in OverlayPack).
extern const char* const kOverlayNames[25];

} // namespace game
