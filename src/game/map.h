#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace game {

// Which classic game a map came from. Determines the template table, theater
// art layout, and cell-numbering width used when the map was authored.
enum class Game { RedAlert, TiberianDawn };

// A scenario/skirmish map loaded from its INI file (Red Alert or Tiberian
// Dawn). The terrain grid is always 128x128 cells; [Map] X/Y/Width/Height give
// the playable bounds. Tiberian Dawn maps are 64x64 and are loaded into the
// top-left quadrant, so the rest of the engine can stay 128-wide.
struct MapFile {
    static constexpr int kSize = 128;
    static constexpr int kTdSize = 64; // Tiberian Dawn native map width

    Game game = Game::RedAlert;

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

    // Tiberian Dawn overlay (tiberium, walls, crates) as cell+art-name entries.
    // Red Alert stores overlay per-cell in Cell::overlay instead.
    std::vector<TerrainObject> tdOverlay;

    // Pre-placed objects from [UNITS]/[INFANTRY]/[STRUCTURES]/[SHIPS].
    struct Object {
        std::string house;   // "Greece", "USSR", "Multi1", ...
        std::string type;    // art/type name lowercased: "jeep", "e1", "fact"
        int health = 256;    // 0-256
        int cell = 0;        // y*kSize + x
        int facing = 0;      // 0-255, 0 = north
        int subcell = 0;     // infantry only: 0 = center, 1-4 = corners
    };
    std::vector<Object> units;      // includes ships
    std::vector<Object> infantry;
    std::vector<Object> structures;

    // --- mission scripting ([Triggers]/[TeamTypes]/[Waypoints]) ---
    // A scenario trigger: an event → action pair scoped to a house, optionally
    // referencing a team. INI form: Name=Event,Action,Data,House,Team,Persist.
    struct Trigger {
        std::string name;
        std::string event;  // "Time", "All Destr.", "Bldgs Destr.", ...
        std::string action; // "Win", "Lose", "Reinforce.", "Create Team", ...
        int data = 0;       // Time: fires after data * 90 ticks (6s units)
        std::string house;  // house the event watches / the team belongs to
        std::string team;   // TeamType name, or "None"
        bool persist = false;
    };
    // A reusable squad template: a house + a roster of (type, count). The
    // scripted mission/behaviour fields are parsed past but not retained (the
    // skirmish AI drives spawned units).
    struct TeamType {
        std::string name;
        std::string house;
        std::vector<std::pair<std::string, int>> roster; // type -> count
    };
    std::vector<Trigger> triggers;
    std::vector<TeamType> teamTypes;
    std::vector<int> waypoints; // index -> cell (-1 = unset)

    static MapFile load(const std::string& iniPath);

    // Theater art extension: ".tem", ".sno" or ".int".
    std::string theaterExt() const;
    // Theater palette base name, e.g. "temperat" -> temperat.pal.
    std::string theaterPalette() const;
};

// OverlayType names in enum order (index = overlay byte in OverlayPack).
extern const char* const kOverlayNames[25];

} // namespace game
