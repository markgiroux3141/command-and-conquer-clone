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
enum class UnitKind { Vehicle, Infantry, Ship, Structure };

// ArmorType order matches the Verses= list (DEFINES.H).
enum class Armor : uint8_t { None, Wood, Light, Heavy, Concrete, Count };

// A [warhead] section (WARHEAD.CPP Read_INI).
struct WarheadStats {
    int spread = 0;                             // damage falloff divisor
    int verses[size_t(Armor::Count)] = {100, 100, 100, 100, 100}; // percent
    int explosion = 0;                          // Combat_Anim set selector
    int infDeath = 0;                           // infantry death animation
};

// A [weapon] section (WEAPON.CPP Read_INI).
struct WeaponStats {
    int damage = 0;
    int rof = 1;             // ticks between shots
    int range = 0;           // leptons (Range= cells * 256)
    int speed = 255;         // projectile leptons/tick (Speed= * 256/100)
    const WarheadStats* warhead = nullptr;
    std::string projectileImage; // bullet art SHP name, empty = invisible
};

struct UnitStats {
    int strength = 100;   // hit points
    int speed = 0;        // leptons per tick at 100% land (Speed= * 256/100)
    int rot = 127;        // facing units (0-255 scale) turned per tick
    int sight = 1;        // cells
    int power = 0;        // structures: produced (+) or drained (-)
    SpeedClass speedClass = SpeedClass::Track;
    Armor armor = Armor::None;
    const WeaponStats* primary = nullptr;
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

    // Weapon/warhead sections by name; nullptr if the section is missing.
    const WeaponStats* weapon(const std::string& name) const;
    const WarheadStats* warhead(const std::string& name) const;

    // COMBAT.CPP Modify_Damage: armor multiplier, distance falloff by the
    // warhead's Spread, [General] MinDamage/MaxDamage clamps.
    int modifyDamage(int damage, const WarheadStats* wh, Armor armor,
                     int distanceLeptons) const;

    // [General] economy knobs.
    int goldValue() const { return goldValue_; }
    int gemValue() const { return gemValue_; }
    int bailCount() const { return bailCount_; }

private:
    fmt::IniFile ini_;
    int landSpeed_[size_t(Land::Count)][size_t(SpeedClass::Count)] = {};
    int minDamage_ = 1, maxDamage_ = 1000;
    int goldValue_ = 25, gemValue_ = 50, bailCount_ = 28;
    mutable std::unordered_map<std::string, UnitStats> cache_;
    mutable std::unordered_map<std::string, WeaponStats> weaponCache_;
    mutable std::unordered_map<std::string, WarheadStats> warheadCache_;
};

} // namespace game
