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

} // namespace

MapFile MapFile::load(const std::string& iniPath) {
    fmt::IniFile ini = fmt::IniFile::load(iniPath);

    MapFile map;
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

std::string MapFile::theaterExt() const {
    if (theater == "SNOW")
        return ".sno";
    if (theater == "INTERIOR")
        return ".int";
    return ".tem";
}

std::string MapFile::theaterPalette() const {
    if (theater == "SNOW")
        return "snow";
    if (theater == "INTERIOR")
        return "interior";
    return "temperat";
}

} // namespace game
