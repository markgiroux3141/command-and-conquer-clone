#pragma once
#include <map>
#include <string>
#include <vector>

namespace fmt {

// Westwood INI: [Section] with Key=Value lines and ';' comments.
// Section/key lookup is case-insensitive; file order is preserved.
// Duplicate sections merge; duplicate keys: last one wins on lookup.
class IniFile {
public:
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string>> entries;

        // nullptr if missing
        const std::string* find(const std::string& key) const;
    };

    static IniFile load(const std::string& path);

    const Section* section(const std::string& name) const; // nullptr if missing
    std::string get(const std::string& sec, const std::string& key,
                    const std::string& fallback = "") const;
    int getInt(const std::string& sec, const std::string& key, int fallback = 0) const;
    const std::vector<Section>& sections() const { return sections_; }

private:
    std::vector<Section> sections_;
    std::map<std::string, size_t> index_; // lowercased name -> sections_ index
};

} // namespace fmt
