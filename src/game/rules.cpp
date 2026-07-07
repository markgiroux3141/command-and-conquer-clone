// Unit stats and terrain movement percentages from rules.ini.
//
// Speed= is scaled exactly like TechnoTypeClass::Read_INI (TECHNO.CPP):
// MaxSpeed = Speed * 256 / 100, i.e. leptons per game tick at 100% land.
// Vehicles default to wheels; Tracked=yes selects tracks (UNIT.CPP Read_INI).
// Land percentages come from the [Clear]/[Road]/... sections (GroundType).

#include "game/rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace game {

namespace {
Armor armorFromName(const std::string& s) {
    if (s.empty())
        return Armor::None;
    switch (std::tolower((unsigned char)s[0])) {
    case 'w': return Armor::Wood;
    case 'l': return Armor::Light;
    case 'h': return Armor::Heavy;
    case 'c': return Armor::Concrete;
    default:  return Armor::None;
    }
}
} // namespace

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
    r.minDamage_ = r.ini_.getInt("General", "MinDamage", 1);
    r.maxDamage_ = r.ini_.getInt("General", "MaxDamage", 1000);
    r.goldValue_ = r.ini_.getInt("General", "GoldValue", 25);
    r.gemValue_ = r.ini_.getInt("General", "GemValue", 50);
    r.bailCount_ = r.ini_.getInt("General", "BailCount", 28);
    return r;
}

const WarheadStats* Rules::warhead(const std::string& name) const {
    auto it = warheadCache_.find(name);
    if (it != warheadCache_.end())
        return &it->second;
    if (!ini_.section(name))
        return nullptr;
    WarheadStats w;
    w.spread = ini_.getInt(name, "Spread", 0);
    w.explosion = ini_.getInt(name, "Explosion", 0);
    w.infDeath = ini_.getInt(name, "InfDeath", 0);
    std::string verses = ini_.get(name, "Verses", "");
    if (!verses.empty()) {
        const char* p = verses.c_str();
        for (size_t a = 0; a < size_t(Armor::Count) && *p; a++) {
            w.verses[a] = std::atoi(p); // stops at '%'
            const char* comma = std::strchr(p, ',');
            if (!comma)
                break;
            p = comma + 1;
        }
    }
    return &warheadCache_.emplace(name, w).first->second;
}

const WeaponStats* Rules::weapon(const std::string& name) const {
    auto it = weaponCache_.find(name);
    if (it != weaponCache_.end())
        return &it->second;
    if (!ini_.section(name))
        return nullptr;
    WeaponStats w;
    w.damage = ini_.getInt(name, "Damage", 0);
    w.rof = std::max(1, ini_.getInt(name, "ROF", 1));
    // Range= is fixed-point cells (Get_Lepton): "4.75" -> 1216 leptons.
    w.range = int(std::strtod(ini_.get(name, "Range", "0").c_str(), nullptr) * 256.0);
    w.speed = std::min(255, std::max(1, ini_.getInt(name, "Speed", 100) * 256 / 100));
    w.warhead = warhead(ini_.get(name, "Warhead", ""));
    std::string bullet = ini_.get(name, "Projectile", "");
    if (!bullet.empty()) {
        std::string image = ini_.get(bullet, "Image", "");
        std::string inviso = ini_.get(bullet, "Inviso", "no");
        bool visible = !image.empty() && image != "none" &&
                       (inviso.empty() || (inviso[0] != 'y' && inviso[0] != 't' &&
                                           inviso[0] != '1' && inviso[0] != 'Y' &&
                                           inviso[0] != 'T'));
        if (visible) {
            std::transform(image.begin(), image.end(), image.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            w.projectileImage = image;
        }
    }
    return &weaponCache_.emplace(name, w).first->second;
}

int Rules::modifyDamage(int damage, const WarheadStats* wh, Armor armor,
                        int distanceLeptons) const {
    if (damage <= 0 || !wh)
        return 0;
    damage = damage * wh->verses[size_t(armor)] / 100;
    if (damage) {
        // Distance falloff (COMBAT.CPP; PIXEL_LEPTON_W == 10, so the
        // divisor is Spread*5 leptons, or 2 for point warheads).
        int dist = distanceLeptons / (wh->spread ? wh->spread * 5 : 2);
        dist = std::clamp(dist, 0, 16);
        if (dist)
            damage /= dist;
        if (dist < 4)
            damage = std::max(damage, minDamage_);
    }
    return std::min(damage, maxDamage_);
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
    case UnitKind::Structure:
        s.rot = 127;
        break;
    }
    if (const auto* sec = ini_.section(type)) {
        s.strength = ini_.getInt(type, "Strength", s.strength);
        s.speed = std::min(255, ini_.getInt(type, "Speed", 0) * 256 / 100);
        s.rot = ini_.getInt(type, "ROT", s.rot);
        s.sight = ini_.getInt(type, "Sight", s.sight);
        s.power = ini_.getInt(type, "Power", 0);
        s.armor = armorFromName(ini_.get(type, "Armor", ""));
        std::string primary = ini_.get(type, "Primary", "");
        if (!primary.empty() && primary != "none")
            s.primary = weapon(primary);
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
