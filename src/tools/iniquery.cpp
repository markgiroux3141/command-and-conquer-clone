// iniquery — inspect a Westwood INI file.
//   iniquery <file.ini>                 list sections
//   iniquery <file.ini> <section>       print all keys in section
//   iniquery <file.ini> <section> <key> print one value

#include <cstdio>

#include "formats/ini.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: iniquery <file.ini> [section] [key]\n");
        return 2;
    }
    try {
        auto ini = fmt::IniFile::load(argv[1]);
        if (argc == 2) {
            for (const auto& s : ini.sections())
                std::printf("[%s] (%zu keys)\n", s.name.c_str(), s.entries.size());
        } else if (argc == 3) {
            const auto* s = ini.section(argv[2]);
            if (!s) {
                std::fprintf(stderr, "no such section: %s\n", argv[2]);
                return 1;
            }
            for (const auto& [k, v] : s->entries)
                std::printf("%s=%s\n", k.c_str(), v.c_str());
        } else {
            std::string v = ini.get(argv[2], argv[3], "<missing>");
            std::printf("%s\n", v.c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
