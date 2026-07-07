// Unit stats and terrain movement percentages from rules.ini.
//
// Speed= is scaled exactly like TechnoTypeClass::Read_INI (TECHNO.CPP):
// MaxSpeed = Speed * 256 / 100, i.e. leptons per game tick at 100% land.
// Vehicles default to wheels; Tracked=yes selects tracks (UNIT.CPP Read_INI).
// Land percentages come from the [Clear]/[Road]/... sections (GroundType).

#include "game/rules.h"

#include <algorithm>

namespace game {

Land landFromControl(uint8_t control) {
    // TemplateTypeClass::Land_Type (CDATA.CPP) lookup table.
    static const Land kTable[16] = {
        Land::Clear, Land::Clear, Land::Clear, Land::Clear,
        Land::Clear, Land::Clear, Land::Beach, Land::Clear,
        Land::Rock,  Land::Road,  Land::Water, Land::River,
        Land::Clear, Land::Clear, Land::Rough, Land::Clear,
    };
    return kTable[control & 0x0f];
}

Rules Rules::load(const std::string& rulesIniPath) {
    Rules r;
    r.ini_ = fmt::IniFile::load(rulesIniPath);

    static const char* kLandSections[size_t(Land::Count)] = {
        "Clear", "Road", "Water", "Rock", "Wall", "Ore", "Beach", "Rough", "River",
    };
    static const char* kSpeedKeys[size_t(SpeedClass::Count)] = {
        "Foot", "Track", "Wheel", nullptr /*Winged*/, "Float",
    };
    for (size_t l = 0; l < size_t(Land::Count); l++) {
        for (size_t s = 0; s < size_t(SpeedClass::Count); s++) {
            if (!kSpeedKeys[s]) {
                r.landSpeed_[l][s] = 100; // aircraft ignore terrain
                continue;
            }
            // Values look like "90%"; getInt's atoi stops at the '%'.
            r.landSpeed_[l][s] = r.ini_.getInt(kLandSections[l], kSpeedKeys[s], 0);
        }
    }
    return r;
}

const UnitStats& Rules::unit(const std::string& type, UnitKind kind) const {
    auto it = cache_.find(type);
    if (it != cache_.end())
        return it->second;

    UnitStats s;
    switch (kind) {
    case UnitKind::Infantry:
        s.speedClass = SpeedClass::Foot;
        s.rot = 127; // infantry turn instantly
        break;
    case UnitKind::Ship:
        s.speedClass = SpeedClass::Float;
        s.rot = 5;
        break;
    case UnitKind::Vehicle:
        s.speedClass = SpeedClass::Wheel;
        s.rot = 5;
        break;
    }
    if (const auto* sec = ini_.section(type)) {
        s.strength = ini_.getInt(type, "Strength", s.strength);
        s.speed = std::min(255, ini_.getInt(type, "Speed", 0) * 256 / 100);
        s.rot = ini_.getInt(type, "ROT", s.rot);
        s.sight = ini_.getInt(type, "Sight", s.sight);
        if (kind == UnitKind::Vehicle) {
            std::string tracked = ini_.get(type, "Tracked", "no");
            if (!tracked.empty() && (tracked[0] == 'y' || tracked[0] == 'Y' ||
                                     tracked[0] == 't' || tracked[0] == 'T' ||
                                     tracked[0] == '1'))
                s.speedClass = SpeedClass::Track;
        }
        (void)sec;
    }
    return cache_.emplace(type, s).first->second;
}

} // namespace game
