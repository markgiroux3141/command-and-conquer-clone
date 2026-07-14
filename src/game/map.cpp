// RA map INI loader. Spec: OpenRA ImportRedAlertMapCommand.cs (ReadPackedSection,
// UnpackTileData, UnpackOverlayData) and the original IOMAP.CPP.
//
// [MapPack]/[OverlayPack]: numbered keys hold one base64 stream; decoded bytes
// are a sequence of chunks (u32 length & 0xdfffffff, then `length` bytes of
// LCW) each decompressing to 8192 bytes. MapPack = 16384 u16 template IDs
// (row-major) followed by 16384 u8 icon indices; OverlayPack = 16384 u8
// overlay types. Template IDs 0 (RAED quirk), 255 (CELL.CPP
// Recalc_Attributes treats it like TEMPLATE_NONE) and 0xffff all mean clear.

#include "game/map.h"

#include "formats/ini.h"
#include "formats/lcw.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace game {

const char* const kOverlayNames[25] = {
    "sbag", "cycl", "brik", "barb", "wood",
    "gold01", "gold02", "gold03", "gold04",
    "gem01", "gem02", "gem03", "gem04",
    "v12", "v13", "v14", "v15", "v16", "v17", "v18",
    "fpls", "wcrate", "scrate", "fenc", "sbag",
};

namespace {

std::vector<uint8_t> base64Decode(const std::string& text) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1; // '=' padding or garbage
    };
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v = val(c);
        if (v < 0)
            continue;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(acc >> bits));
        }
    }
    return out;
}

// Concatenate the numbered keys of a packed section and decode base64.
std::vector<uint8_t> readPackedSection(const fmt::IniFile& ini, const std::string& name) {
    const auto* sec = ini.section(name);
    if (!sec)
        throw std::runtime_error("map: missing [" + name + "] section");
    std::string b64;
    for (int i = 1;; i++) {
        const std::string* line = sec->find(std::to_string(i));
        if (!line)
            break;
        b64 += *line;
    }
    auto packed = base64Decode(b64);

    // Chunked LCW: u32 length (top bits masked), then length bytes of LCW
    // that expand to 8192 bytes. Repeat until input is exhausted.
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos + 4 <= packed.size()) {
        uint32_t length = uint32_t(packed[pos]) | (uint32_t(packed[pos + 1]) << 8) |
                          (uint32_t(packed[pos + 2]) << 16) | (uint32_t(packed[pos + 3]) << 24);
        length &= 0xdfffffff;
        pos += 4;
        if (pos + length > packed.size())
            break; // truncated trailing chunk; match OpenRA's EOF tolerance
        std::vector<uint8_t> chunkIn(packed.begin() + pos, packed.begin() + pos + length);
        std::vector<uint8_t> chunk(8192, 0);
        fmt::lcwDecode(chunkIn, 0, chunk);
        out.insert(out.end(), chunk.begin(), chunk.end());
        pos += length;
    }
    return out;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

// Split a comma-separated object line, trimming surrounding spaces per field.
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos)
            comma = s.size();
        std::string field = s.substr(start, comma - start);
        while (!field.empty() && field.front() == ' ')
            field.erase(field.begin());
        while (!field.empty() && field.back() == ' ')
            field.pop_back();
        out.push_back(field);
        start = comma + 1;
    }
    return out;
}

// Parse [UNITS]/[SHIPS]/[INFANTRY]/[STRUCTURES] lines. Field order (shared by
// both games): UNITS/SHIPS house,type,health,cell,facing,mission,trigger;
// INFANTRY house,type,health,cell,subcell,mission,facing,trigger; STRUCTURES
// house,type,health,cell,facing,trigger. cellFn maps a raw INI cell index to
// the engine's 128-wide grid index (identity for RA, 64->128 for TD).
template <typename CellFn>
void parseObjects(const fmt::IniFile& ini, const char* section,
                  std::vector<MapFile::Object>& out, bool isInfantry,
                  bool hasMission, CellFn cellFn) {
    const auto* sec = ini.section(section);
    if (!sec)
        return;
    for (const auto& [key, value] : sec->entries) {
        auto f = splitCsv(value);
        if (f.size() < 5)
            continue;
        MapFile::Object obj;
        obj.house = f[0];
        obj.type = toLower(f[1]);
        obj.health = std::stoi(f[2]);
        int rawCell = std::stoi(f[3]);
        obj.cell = cellFn(rawCell);
        if (isInfantry) {
            obj.subcell = std::stoi(f[4]);
            obj.facing = f.size() > 6 ? std::stoi(f[6]) : 0;
        } else {
            obj.facing = std::stoi(f[4]);
        }
        // UNITS/SHIPS: house,type,health,cell,facing,mission,trigger.
        // INFANTRY:    house,type,health,cell,subcell,mission,facing,trigger.
        // Either way the per-unit order (Guard/Area Guard/Hunt/...) is field 5.
        // STRUCTURES has a trigger there instead, so callers opt in.
        if (hasMission && f.size() > 5)
            obj.mission = f[5];
        if (obj.cell >= 0 && obj.cell < MapFile::kSize * MapFile::kSize)
            out.push_back(std::move(obj));
    }
}

// Parse the mission-scripting sections ([Triggers]/[TeamTypes]/[Waypoints]),
// shared by both games. cellFn maps raw INI cell numbers to the engine grid.
template <typename CellFn>
void parseScripting(const fmt::IniFile& ini, MapFile& map, CellFn cellFn) {
    // [Triggers]: Name=Event,Action,Data,House,Team,Persist (TRIGGER.CPP Fill_In).
    if (const auto* sec = ini.section("Triggers")) {
        for (const auto& [key, value] : sec->entries) {
            auto f = splitCsv(value);
            if (f.size() < 5)
                continue;
            MapFile::Trigger t;
            t.name = key;
            t.event = f[0];
            t.action = f[1];
            t.data = std::atoi(f[2].c_str());
            t.house = f[3];
            t.team = f[4];
            t.persist = f.size() > 5 && std::atoi(f[5].c_str()) != 0;
            map.triggers.push_back(std::move(t));
        }
    }
    // [TeamTypes]: Name=House,<9 flags>,ClassCount,Class:Num...,MissionCount,
    // Mission:Arg... (TEAMTYPE.CPP Fill_In). We keep the house, the roster, and
    // the scripted mission list (Move:wpt / Attack Units / Guard / Loop / ...),
    // which the sim replays to drive spawned squads faithfully.
    if (const auto* sec = ini.section("TeamTypes")) {
        for (const auto& [key, value] : sec->entries) {
            auto f = splitCsv(value);
            if (f.size() < 11)
                continue;
            MapFile::TeamType tt;
            tt.name = key;
            tt.house = f[0];
            int classCount = std::atoi(f[10].c_str());
            int i = 11;
            for (int c = 0; c < classCount && i < int(f.size()); c++, i++) {
                const std::string& tok = f[i];
                size_t colon = tok.find(':');
                if (colon == std::string::npos)
                    continue;
                tt.roster.emplace_back(toLower(tok.substr(0, colon)),
                                       std::atoi(tok.c_str() + colon + 1));
            }
            // MissionCount then that many "Mission:Argument" tokens. Mission
            // names keep their original spacing/case ("Attack Units", "Move").
            if (i < int(f.size())) {
                int missionCount = std::atoi(f[i].c_str());
                i++;
                for (int m = 0; m < missionCount && i < int(f.size()); m++, i++) {
                    const std::string& tok = f[i];
                    size_t colon = tok.find(':');
                    if (colon == std::string::npos)
                        tt.missions.emplace_back(tok, 0);
                    else
                        tt.missions.emplace_back(tok.substr(0, colon),
                                                 std::atoi(tok.c_str() + colon + 1));
                }
            }
            map.teamTypes.push_back(std::move(tt));
        }
    }
    // [Waypoints]: index=cell (-1 = unset). Sparse, so size to the max index.
    if (const auto* sec = ini.section("Waypoints")) {
        for (const auto& [key, value] : sec->entries) {
            int idx = std::atoi(key.c_str());
            int raw = std::atoi(value.c_str());
            if (idx < 0)
                continue;
            if (idx >= int(map.waypoints.size()))
                map.waypoints.resize(idx + 1, -1);
            map.waypoints[idx] = raw < 0 ? -1 : cellFn(raw);
        }
    }
}

MapFile loadTd(fmt::IniFile& ini, const std::string& iniPath);
MapFile loadCustom(fmt::IniFile& ini, const std::string& iniPath);

} // namespace

MapFile MapFile::load(const std::string& iniPath) {
    fmt::IniFile ini = fmt::IniFile::load(iniPath);

    // Custom levels authored by mapedit carry the [Basic] NewINIFormat marker
    // and, like TD maps, have no [MapPack]. Detect them first so the TD .bin
    // branch below does not claim them.
    if (ini.getInt("Basic", "NewINIFormat") != 0)
        return loadCustom(ini, iniPath);

    // Tiberian Dawn maps have no packed [MapPack]; terrain lives in a sibling
    // .bin. Detect by section, then fall back to the .bin's presence.
    std::filesystem::path binPath = iniPath;
    binPath.replace_extension(".bin");
    if (!ini.section("MapPack") && std::filesystem::exists(binPath))
        return loadTd(ini, iniPath);

    MapFile map;
    map.game = Game::RedAlert;
    map.theater = toUpper(ini.get("Map", "Theater", "TEMPERATE"));
    map.x = ini.getInt("Map", "X");
    map.y = ini.getInt("Map", "Y");
    map.width = ini.getInt("Map", "Width");
    map.height = ini.getInt("Map", "Height");

    constexpr size_t total = size_t(kSize) * kSize;
    map.cells.assign(total, {});

    auto tileData = readPackedSection(ini, "MapPack");
    if (tileData.size() < total * 3)
        throw std::runtime_error("map: MapPack too short: " + iniPath);
    for (size_t i = 0; i < total; i++) {
        uint16_t id = uint16_t(tileData[i * 2] | (tileData[i * 2 + 1] << 8));
        if (id == 0 || id == 255) // both mean clear, like the original engine
            id = 0xffff;
        map.cells[i].templateId = id;
        map.cells[i].icon = tileData[total * 2 + i];
    }

    auto overlayData = readPackedSection(ini, "OverlayPack");
    if (overlayData.size() < total)
        throw std::runtime_error("map: OverlayPack too short: " + iniPath);
    for (size_t i = 0; i < total; i++)
        map.cells[i].overlay = overlayData[i];

    auto identity = [](int c) { return c; };
    parseObjects(ini, "UNITS", map.units, false, true, identity);
    parseObjects(ini, "SHIPS", map.units, false, true, identity);
    parseObjects(ini, "INFANTRY", map.infantry, true, true, identity);
    parseObjects(ini, "STRUCTURES", map.structures, false, false, identity);
    parseScripting(ini, map, identity);

    if (const auto* terrain = ini.section("TERRAIN")) {
        for (const auto& [key, value] : terrain->entries) {
            TerrainObject obj;
            obj.cell = std::stoi(key);
            obj.name = toLower(value);
            if (obj.cell >= 0 && obj.cell < int(total))
                map.terrain.push_back(std::move(obj));
        }
    }

    return map;
}

namespace {

// Tiberian Dawn map loader. Terrain is a sibling .bin of 64x64 cells, each two
// bytes: template id then icon index (template 0xff = clear). Overlay, terrain
// objects and pre-placed units live in INI sections keyed/valued by TD's
// 64-wide cell numbering. We place it all into the top-left 64x64 of the
// engine's 128-wide grid so the renderer/sim stay unchanged.
MapFile loadTd(fmt::IniFile& ini, const std::string& iniPath) {
    MapFile map;
    map.game = Game::TiberianDawn;
    map.theater = toUpper(ini.get("Map", "Theater", "TEMPERATE"));
    map.x = ini.getInt("Map", "X");
    map.y = ini.getInt("Map", "Y");
    map.width = ini.getInt("Map", "Width");
    map.height = ini.getInt("Map", "Height");

    constexpr size_t total = size_t(MapFile::kSize) * MapFile::kSize;
    map.cells.assign(total, {});

    // TD cell index (0..4095) -> engine 128-grid index.
    auto tdCell = [](int c) {
        int x = c % MapFile::kTdSize, y = c / MapFile::kTdSize;
        return y * MapFile::kSize + x;
    };

    std::filesystem::path binPath = iniPath;
    binPath.replace_extension(".bin");
    std::FILE* f = std::fopen(binPath.string().c_str(), "rb");
    if (!f)
        throw std::runtime_error("map: cannot open " + binPath.string());
    std::vector<uint8_t> bin(size_t(MapFile::kTdSize) * MapFile::kTdSize * 2);
    size_t got = std::fread(bin.data(), 1, bin.size(), f);
    std::fclose(f);
    if (got < bin.size())
        throw std::runtime_error("map: short .bin: " + binPath.string());
    for (int y = 0; y < MapFile::kTdSize; y++) {
        for (int x = 0; x < MapFile::kTdSize; x++) {
            size_t src = size_t(y * MapFile::kTdSize + x) * 2;
            uint8_t type = bin[src], icon = bin[src + 1];
            auto& cell = map.cells[y * MapFile::kSize + x];
            cell.templateId = (type == 0xff) ? 0xffff : type;
            cell.icon = icon;
        }
    }

    auto identity = tdCell;
    parseObjects(ini, "UNITS", map.units, false, true, identity);
    parseObjects(ini, "INFANTRY", map.infantry, true, true, identity);
    parseObjects(ini, "STRUCTURES", map.structures, false, false, identity);
    parseScripting(ini, map, tdCell);

    // [OVERLAY] cell=NAME (tiberium ti1-ti12, walls, crates).
    if (const auto* overlay = ini.section("OVERLAY")) {
        for (const auto& [key, value] : overlay->entries) {
            MapFile::TerrainObject obj;
            obj.cell = tdCell(std::stoi(key));
            obj.name = toLower(value);
            if (obj.cell >= 0 && obj.cell < int(total))
                map.tdOverlay.push_back(std::move(obj));
        }
    }
    // [TERRAIN] cell=NAME,... (trees, rocks). Value may carry a trailing
    // action after a comma; keep only the art name.
    if (const auto* terrain = ini.section("TERRAIN")) {
        for (const auto& [key, value] : terrain->entries) {
            MapFile::TerrainObject obj;
            obj.cell = tdCell(std::stoi(key));
            obj.name = toLower(splitCsv(value)[0]);
            if (obj.cell >= 0 && obj.cell < int(total))
                map.terrain.push_back(std::move(obj));
        }
    }

    return map;
}

// Custom-level loader for maps authored by our mapedit tool. Unlike the native
// TD path, terrain uses the engine's full 128-wide identity cell numbering and
// the sibling .bin holds kSize*kSize cells (not 64x64). Content is TD-flavored
// (TD template table, theaters, type lists), so game is TiberianDawn.
MapFile loadCustom(fmt::IniFile& ini, const std::string& iniPath) {
    MapFile map;
    map.game = Game::TiberianDawn;
    map.theater = toUpper(ini.get("Map", "Theater", "TEMPERATE"));
    map.x = ini.getInt("Map", "X");
    map.y = ini.getInt("Map", "Y");
    map.width = ini.getInt("Map", "Width");
    map.height = ini.getInt("Map", "Height");

    constexpr size_t total = size_t(MapFile::kSize) * MapFile::kSize;
    map.cells.assign(total, {});

    std::filesystem::path binPath = iniPath;
    binPath.replace_extension(".bin");
    std::FILE* f = std::fopen(binPath.string().c_str(), "rb");
    if (!f)
        throw std::runtime_error("map: cannot open " + binPath.string());
    std::vector<uint8_t> bin(total * 2);
    size_t got = std::fread(bin.data(), 1, bin.size(), f);
    std::fclose(f);
    if (got < bin.size())
        throw std::runtime_error("map: short custom .bin: " + binPath.string());
    for (size_t i = 0; i < total; i++) {
        uint8_t type = bin[i * 2], icon = bin[i * 2 + 1];
        map.cells[i].templateId = (type == 0xff) ? 0xffff : type;
        map.cells[i].icon = icon;
    }

    auto identity = [](int c) { return c; };
    parseObjects(ini, "UNITS", map.units, false, true, identity);
    parseObjects(ini, "INFANTRY", map.infantry, true, true, identity);
    parseObjects(ini, "STRUCTURES", map.structures, false, false, identity);
    parseScripting(ini, map, identity);

    // [OVERLAY] cell=NAME (walls, tiberium, crates), TD-style overlay list.
    if (const auto* overlay = ini.section("OVERLAY")) {
        for (const auto& [key, value] : overlay->entries) {
            MapFile::TerrainObject obj;
            obj.cell = std::stoi(key);
            obj.name = toLower(value);
            if (obj.cell >= 0 && obj.cell < int(total))
                map.tdOverlay.push_back(std::move(obj));
        }
    }
    // [TERRAIN] cell=NAME[,action] (trees, rocks). Keep only the art name.
    if (const auto* terrain = ini.section("TERRAIN")) {
        for (const auto& [key, value] : terrain->entries) {
            MapFile::TerrainObject obj;
            obj.cell = std::stoi(key);
            obj.name = toLower(splitCsv(value)[0]);
            if (obj.cell >= 0 && obj.cell < int(total))
                map.terrain.push_back(std::move(obj));
        }
    }

    return map;
}

} // namespace

std::string MapFile::theaterExt() const {
    if (game == Game::TiberianDawn) {
        if (theater == "DESERT")
            return ".des";
        if (theater == "WINTER")
            return ".win";
        return ".tem"; // TEMPERATE
    }
    if (theater == "SNOW")
        return ".sno";
    if (theater == "INTERIOR")
        return ".int";
    return ".tem";
}

std::string MapFile::theaterPalette() const {
    if (game == Game::TiberianDawn) {
        if (theater == "DESERT")
            return "desert";
        if (theater == "WINTER")
            return "winter";
        return "temperat";
    }
    if (theater == "SNOW")
        return "snow";
    if (theater == "INTERIOR")
        return "interior";
    return "temperat";
}

void MapFile::save(const std::string& iniPath) const {
    constexpr size_t total = size_t(kSize) * kSize;

    // Terrain .bin: uncompressed, kSize*kSize cells of [templateId u8][icon u8];
    // template 0xff means clear (mirrors loadCustom).
    std::filesystem::path binPath = iniPath;
    binPath.replace_extension(".bin");
    {
        std::vector<uint8_t> bin(total * 2, 0);
        for (size_t i = 0; i < total && i < cells.size(); i++) {
            uint16_t id = cells[i].templateId;
            bin[i * 2] = (id == 0xffff) ? 0xff : uint8_t(id);
            bin[i * 2 + 1] = cells[i].icon;
        }
        std::ofstream bf(binPath, std::ios::binary | std::ios::trunc);
        if (!bf)
            throw std::runtime_error("map: cannot write " + binPath.string());
        bf.write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size()));
    }

    std::ofstream f(iniPath, std::ios::trunc);
    if (!f)
        throw std::runtime_error("map: cannot write " + iniPath);

    f << "[Basic]\n";
    f << "NewINIFormat=1\n\n";

    f << "[Map]\n";
    f << "Theater=" << theater << "\n";
    f << "X=" << x << "\n";
    f << "Y=" << y << "\n";
    f << "Width=" << width << "\n";
    f << "Height=" << height << "\n\n";

    // Object sections. Field orders must match parseObjects()'s reader.
    if (!structures.empty()) {
        f << "[STRUCTURES]\n"; // house,type,health,cell,facing,trigger
        int n = 0;
        for (const auto& o : structures)
            f << std::setfill('0') << std::setw(3) << n++ << "=" << o.house << ","
              << o.type << "," << o.health << "," << o.cell << "," << o.facing
              << ",None\n";
        f << "\n";
    }
    if (!units.empty()) {
        f << "[UNITS]\n"; // house,type,health,cell,facing,mission,trigger
        int n = 0;
        for (const auto& o : units)
            f << std::setfill('0') << std::setw(3) << n++ << "=" << o.house << ","
              << o.type << "," << o.health << "," << o.cell << "," << o.facing << ","
              << (o.mission.empty() ? "Guard" : o.mission) << ",None\n";
        f << "\n";
    }
    if (!infantry.empty()) {
        f << "[INFANTRY]\n"; // house,type,health,cell,subcell,mission,facing,trigger
        int n = 0;
        for (const auto& o : infantry)
            f << std::setfill('0') << std::setw(3) << n++ << "=" << o.house << ","
              << o.type << "," << o.health << "," << o.cell << "," << o.subcell << ","
              << (o.mission.empty() ? "Guard" : o.mission) << "," << o.facing
              << ",None\n";
        f << "\n";
    }

    // Overlay + terrain objects, keyed by cell.
    if (!tdOverlay.empty()) {
        f << "[OVERLAY]\n";
        for (const auto& t : tdOverlay)
            f << t.cell << "=" << t.name << "\n";
        f << "\n";
    }
    if (!terrain.empty()) {
        f << "[TERRAIN]\n";
        for (const auto& t : terrain)
            f << t.cell << "=" << t.name << "\n";
        f << "\n";
    }

    // Spawn/waypoints: index=cell, skipping unset (-1).
    bool anyWp = false;
    for (int c : waypoints)
        if (c >= 0) {
            anyWp = true;
            break;
        }
    if (anyWp) {
        f << "[Waypoints]\n";
        for (size_t i = 0; i < waypoints.size(); i++)
            if (waypoints[i] >= 0)
                f << i << "=" << waypoints[i] << "\n";
        f << "\n";
    }
}

} // namespace game
