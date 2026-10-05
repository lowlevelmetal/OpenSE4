#include "ruleset/races.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <format>

namespace opense4::ruleset {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string field(const datafile::Record& r, std::string_view key) {
    const auto* f = r.find(key);
    return f ? f->value : std::string{};
}

int number(const datafile::Record& r, std::string_view key) {
    const auto* f = r.find(key);
    if (!f) return 0;
    return static_cast<int>(datafile::parseInteger(f->value).value_or(0));
}

// Entries by their names as written, byte by byte: the order every platform
// agrees on.
std::vector<FileEntry> byName(std::vector<FileEntry> entries) {
    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });
    return entries;
}

void scan(const GameFiles& files, const std::string& dir, bool neutral, std::vector<RacePreset>& out) {
    // Folders by name; in each, the first *_AI_General.txt and
    // *_AI_Settings.txt by name when there are several.
    for (const FileEntry& folder : byName(files.list(dir))) {
        if (!folder.directory) continue;
        const std::string path = dir + "/" + folder.name;
        std::string general, settings;
        for (const FileEntry& f : byName(files.list(path))) {
            if (f.directory) continue;
            const std::string n = lower(f.name);
            if (n.ends_with("_ai_general.txt") && general.empty()) general = path + "/" + f.name;
            if (n.ends_with("_ai_settings.txt") && settings.empty()) settings = path + "/" + f.name;
        }
        if (general.empty()) continue;
        auto file = files.file(general);
        if (!file || file->records.empty()) continue;
        const datafile::Record& r = file->records.front();
        RacePreset p;
        p.folder = folder.name;
        p.neutral = neutral;
        p.name = field(r, "Name");
        p.description = field(r, "Description");
        p.empireName = field(r, "Empire Name");
        p.empireType = field(r, "Empire Type");
        p.emperorName = field(r, "Emperor Name");
        p.emperorTitle = field(r, "Emperor Title");
        p.biology = field(r, "Biological Description");
        p.society = field(r, "Society Description");
        p.history = field(r, "General History Description");
        p.demeanor = field(r, "Demeanor");
        p.culture = field(r, "Culture");
        p.happinessType = field(r, "Happiness Type");
        p.planetType = field(r, "Planet Type");
        p.atmosphere = field(r, "Atmosphere");
        p.designNameFile = field(r, "Design Name File");
        for (int t = 1; t <= 3; ++t) {
            RaceTier tier;
            const int nc = number(r, std::format("Race Opt {} Num Characteristics", t));
            for (int i = 1; i <= nc; ++i)
                tier.characteristics.emplace_back(field(r, std::format("Race Opt {} Characteristic {} Type", t, i)),
                                                  number(r, std::format("Race Opt {} Characteristic {} Amount", t, i)));
            const int nt = number(r, std::format("Race Opt {} Num Advanced Traits", t));
            for (int i = 1; i <= nt; ++i)
                if (auto trait = field(r, std::format("Race Opt {} Adv Trait {}", t, i)); !trait.empty()) tier.traits.push_back(trait);
            if (nc > 0 || nt > 0) p.tiers.push_back(std::move(tier));
        }
        if (!settings.empty())
            if (auto s = files.file(settings); s && !s->records.empty()) p.personalityGroup = number(s->records.front(), "Personality Group");
        if (p.name.empty()) p.name = p.folder;
        out.push_back(std::move(p));
    }
}

} // namespace

std::vector<RacePreset> loadRacePresets(const std::filesystem::path& gameRoot) { return loadRacePresets(*openInstallFiles(gameRoot)); }

std::vector<RacePreset> loadRacePresets(const GameFiles& files) {
    std::vector<RacePreset> out;
    // Folder names differ in case between installs; any spelling is found.
    // Normal races come first, then neutral ones: the list's order must be the
    // same on every platform.
    std::string pictures;
    for (const FileEntry& e : files.list(""))
        if (e.directory && lower(e.name) == "pictures") pictures = e.name;
    if (pictures.empty()) return out;
    std::string races, neutral;
    for (const FileEntry& e : files.list(pictures)) {
        if (!e.directory) continue;
        const std::string n = lower(e.name);
        if (n == "races") races = pictures + "/" + e.name;
        if (n == "raceneutral") neutral = pictures + "/" + e.name;
    }
    if (!races.empty()) scan(files, races, false, out);
    if (!neutral.empty()) scan(files, neutral, true, out);
    return out;
}

} // namespace opense4::ruleset
