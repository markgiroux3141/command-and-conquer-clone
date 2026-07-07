#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "formats/ini.h"

namespace game {

// LandType from the original DEFINES.H (order matches rules.ini sections).
enum class Land : uint8_t {
    Clear,
    Road,
    Water,
    Rock,
    Wall,
    Ore, // LAND_TIBERIUM
    Beach,
    Rough,
    River,
    Count,
};

// Locomotion class (SpeedType in the original).
enum class SpeedClass : uint8_t {
    Foot,
    Track,
    Wheel,
    Winged,
    Float,
    Count,
};

// TMP control-map byte (0-15) -> land, per TemplateTypeClass::Land_Type.
Land landFromControl(uint8_t control);

// What kind of object a map entry is; picks stat defaults and speed class.
enum class UnitKind { Vehicle, Infantry, Ship };

struct UnitStats {
    int strength = 100;   // hit points
    int speed = 0;        // leptons per tick at 100% land (Speed= * 256/100)
    int rot = 127;        // facing units (0-255 scale) turned per tick
    int sight = 1;        // cells
    SpeedClass speedClass = SpeedClass::Track;
};

// Stat database backed by rules.ini. Sections are looked up lazily per unit
// type name (the game defines its type lists in code, not in the INI).
class Rules {
public:
    static Rules load(const std::string& rulesIniPath);

    // Stats for a type name ("jeep", "e1", "dd"); never fails — missing
    // sections get the defaults for the kind.
    const UnitStats& unit(const std::string& type, UnitKind kind) const;

    // Movement percent (0-100+) for a speed class on a land type, from the
    // [Clear]/[Road]/... sections. 0 = impassable.
    int landSpeed(Land land, SpeedClass cls) const {
        return landSpeed_[size_t(land)][size_t(cls)];
    }

private:
    fmt::IniFile ini_;
    int landSpeed_[size_t(Land::Count)][size_t(SpeedClass::Count)] = {};
    mutable std::unordered_map<std::string, UnitStats> cache_;
};

} // namespace game
