#include "ruleset/races.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"

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

void scan(const std::filesystem::path& dir, bool neutral, std::vector<RacePreset>& out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::filesystem::path> folders;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_directory(ec)) folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());
    for (const auto& folder : folders) {
        // Case-insensitive search for *_AI_General.txt and *_AI_Settings.txt.
        std::filesystem::path general, settings;
        for (const auto& f : std::filesystem::directory_iterator(folder, ec)) {
            const std::string n = lower(f.path().filename().string());
            if (n.ends_with("_ai_general.txt")) general = f.path();
            if (n.ends_with("_ai_settings.txt")) settings = f.path();
        }
        if (general.empty()) continue;
        auto file = datafile::load(general);
        if (!file || file->records.empty()) continue;
        const datafile::Record& r = file->records.front();
        RacePreset p;
        p.folder = folder.filename().string();
        p.neutral = neutral;
        p.name = field(r, "Name");
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
            if (auto s = datafile::load(settings); s && !s->records.empty()) p.personalityGroup = number(s->records.front(), "Personality Group");
        if (p.name.empty()) p.name = p.folder;
        out.push_back(std::move(p));
    }
}

} // namespace

std::vector<RacePreset> loadRacePresets(const std::filesystem::path& gameRoot) {
    std::vector<RacePreset> out;
    // Folder names differ in case between installs; accept any spelling.
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(gameRoot / "Pictures", ec)) {
        const std::string n = lower(e.path().filename().string());
        if (n == "races") scan(e.path(), false, out);
        if (n == "raceneutral") scan(e.path(), true, out);
    }
    return out;
}

} // namespace opense4::ruleset
