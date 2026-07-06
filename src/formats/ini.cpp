#include "formats/ini.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace fmt {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

const std::string* IniFile::Section::find(const std::string& key) const {
    std::string k = lower(key);
    const std::string* result = nullptr;
    for (const auto& [ek, ev] : entries)
        if (lower(ek) == k)
            result = &ev; // last wins
    return result;
}

IniFile IniFile::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open INI: " + path);

    IniFile ini;
    Section* current = nullptr;
    std::string line;
    bool firstLine = true;

    while (std::getline(f, line)) {
        if (firstLine) { // strip UTF-8 BOM
            if (line.size() >= 3 && line[0] == '\xEF' && line[1] == '\xBB' && line[2] == '\xBF')
                line.erase(0, 3);
            firstLine = false;
        }

        // strip comment (';' anywhere on the line, per Westwood INIClass)
        size_t semi = line.find(';');
        if (semi != std::string::npos)
            line.erase(semi);
        line = trim(line);
        if (line.empty())
            continue;

        if (line.front() == '[') {
            size_t close = line.find(']');
            if (close == std::string::npos)
                continue; // malformed; original engine ignores
            std::string name = trim(line.substr(1, close - 1));
            std::string key = lower(name);
            auto it = ini.index_.find(key);
            if (it == ini.index_.end()) {
                ini.index_[key] = ini.sections_.size();
                ini.sections_.push_back({name, {}});
                current = &ini.sections_.back();
            } else {
                current = &ini.sections_[it->second]; // duplicate section: merge
            }
        } else if (current) {
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            current->entries.emplace_back(trim(line.substr(0, eq)),
                                          trim(line.substr(eq + 1)));
        }
    }
    return ini;
}

const IniFile::Section* IniFile::section(const std::string& name) const {
    auto it = index_.find(lower(name));
    return it == index_.end() ? nullptr : &sections_[it->second];
}

std::string IniFile::get(const std::string& sec, const std::string& key,
                         const std::string& fallback) const {
    const Section* s = section(sec);
    if (!s)
        return fallback;
    const std::string* v = s->find(key);
    return v ? *v : fallback;
}

int IniFile::getInt(const std::string& sec, const std::string& key, int fallback) const {
    std::string v = get(sec, key);
    if (v.empty())
        return fallback;
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

} // namespace fmt
