#include "mods/tables.hpp"

#include "datafile/datafile.hpp"

#include <format>

namespace opense4::mods {

namespace {

const GroupSpec kRequirements{"requirements", "Number of Tech Req", {"Tech Area Req {}", "Tech Level Req {}"}};
const GroupSpec kAbilities{"abilities", "Number of Abilities", {"Ability {} Type", "Ability {} Descr", "Ability {} Val 1", "Ability {} Val 2"}};

std::vector<TableSpec> makeTables() {
    using K = TableKind;
    std::vector<TableSpec> t;
    // ---- The data folder ------------------------------------------------------------------------
    t.push_back({"tech_areas", "TechArea.txt", false, K::Records, "Name", false, {kRequirements}});
    t.push_back({"vehicle_sizes", "VehicleSize.txt", false, K::Records, "Name", false, {kRequirements, kAbilities}});
    t.push_back({"components", "Components.txt", false, K::Records, "Name", false, {kRequirements, kAbilities}});
    t.push_back({"facilities", "Facility.txt", false, K::Records, "Name", false, {kRequirements, kAbilities}});
    t.push_back({"planet_sizes", "PlanetSize.txt", false, K::Records, "Name", false, {}});
    t.push_back({"racial_traits", "RacialTraits.txt", false, K::Records, "Name", false,
                 {{"values", "", {"Value {}"}}, {"required_traits", "", {"Required Trait {}"}}, {"restricted_traits", "", {"Restricted Trait {}"}}}});
    t.push_back({"cultures", "Cultures.txt", false, K::Records, "Name", false, {}});
    t.push_back({"sector_types", "SectType.txt", false, K::Records, "", false, {}});
    t.push_back({"stellar_ability_types", "StellarAbilityTypes.txt", false, K::Records, "Name", false,
                 {{"abilities", "Number of Poss Abilities",
                   {"Ability {} Chance", "Ability {} Type", "Ability {} Descr", "Ability {} Val 1", "Ability {} Val 2"}, 1}}});
    t.push_back({"system_types", "SystemTypes.txt", false, K::Records, "Name", false,
                 {kAbilities,
                  {"objects", "Number of System Objs",
                   {"Obj {} Physical Type", "Obj {} Position", "Obj {} Stellar Abil Type", "Obj {} Size", "Obj {} Age", "Obj {} Color",
                    "Obj {} Luminosity", "Obj {} Atmosphere", "Obj {} Composition"}}}});
    t.push_back({"quadrant_types", "QuadrantTypes.txt", false, K::Records, "Name", false,
                 {{"system_types", "Number of System Types", {"Type {} Name", "Type {} Chance"}}}});
    t.push_back({"weapon_mounts", "CompEnhancement.txt", false, K::Records, "Long Name", false, {kRequirements}});
    t.push_back({"formations", "Formations.txt", false, K::Records, "Name", false,
                 {{"positions", "Number of Positions", {"Position {} Xpos", "Position {} Ypos", "Position {} Type"}}}});
    t.push_back({"happiness", "Happiness.txt", false, K::Records, "Name", true, {}});
    t.push_back({"intel_projects", "IntelProjects.txt", false, K::Records, "Name", false,
                 {kRequirements,
                  {"source_messages", "Num Source Messages", {"Source Message {}"}},
                  {"target_messages", "Num Target Messages", {"Target Message Title {}", "Target Message {}"}}}});
    t.push_back({"events", "Events.txt", false, K::Records, "Type", false,
                 {{"messages", "Num Messages", {"Message Title {}", "Message {}"}},
                  {"start_messages", "Num Start Messages", {"Start Message Title {}", "Start Message {}"}}}});
    t.push_back({"strategies", "DefaultStrategies.txt", false, K::Records, "Name", true, {}});
    t.push_back({"settings", "Settings.txt", false, K::Single, "", true, {}});
    t.push_back({"design_types", "DefaultDesignTypes.txt", false, K::Records, "Name", false, {}});
    t.push_back({"colony_types", "DefaultColonyTypes.txt", false, K::Records, "Name", false, {}});
    // ---- The name lists --------------------------------------------------------------------------
    t.push_back({"empire_names", "EmpireNames.txt", false, K::List, "", false, {}});
    t.push_back({"empire_types", "EmpireTypes.txt", false, K::List, "", false, {}});
    t.push_back({"emperor_names", "EmperorNames.txt", false, K::List, "", false, {}});
    t.push_back({"emperor_titles", "EmperorTitles.txt", false, K::List, "", false, {}});
    t.push_back({"demeanors", "Demeanors.txt", false, K::List, "", false, {}});
    t.push_back({"system_names", "SystemNames.txt", false, K::List, "", false, {}});
    t.push_back({"repair_priorities", "RepairPriorities.txt", false, K::List, "", false, {}});
    // ---- The computer players' tables (Ai/ and the race folders) -----------------------------------
    t.push_back({"ai.anger", "Anger", true, K::Single, "", false, {}});
    t.push_back({"ai.politics", "Politics", true, K::Single, "", false, {}});
    t.push_back({"ai.settings", "Settings", true, K::Single, "", true, {}});
    {
        // The race's own file: read by the race list as well, so any key the files use.
        TableSpec general{"ai.general", "General", true, K::Single, "", true, {}};
        general.groups.push_back({"characteristics_1", "Race Opt 1 Num Characteristics", {"Race Opt 1 Characteristic {} Type", "Race Opt 1 Characteristic {} Amount"}});
        general.groups.push_back({"characteristics_2", "Race Opt 2 Num Characteristics", {"Race Opt 2 Characteristic {} Type", "Race Opt 2 Characteristic {} Amount"}});
        general.groups.push_back({"characteristics_3", "Race Opt 3 Num Characteristics", {"Race Opt 3 Characteristic {} Type", "Race Opt 3 Characteristic {} Amount"}});
        general.groups.push_back({"advanced_traits_1", "Race Opt 1 Num Advanced Traits", {"Race Opt 1 Adv Trait {}"}});
        general.groups.push_back({"advanced_traits_2", "Race Opt 2 Num Advanced Traits", {"Race Opt 2 Adv Trait {}"}});
        general.groups.push_back({"advanced_traits_3", "Race Opt 3 Num Advanced Traits", {"Race Opt 3 Adv Trait {}"}});
        t.push_back(std::move(general));
    }
    t.push_back({"ai.fleets", "Fleets", true, K::Single, "", false, {}});
    t.push_back({"ai.research", "Research", true, K::Records, "", false, {}});
    t.push_back({"ai.design_creation", "DesignCreation", true, K::Records, "Name", false,
                 {{"must_have", "Num Must Have At Least 1 Ability", {"Must Have Ability {}"}},
                  {"misc_abilities", "Num Misc Abilities", {"Misc Ability {} Name", "Misc Ability {} Spaces Per One"}}}});
    t.push_back({"ai.construction_facilities", "Construction_Facilities", true, K::Records, "", false,
                 {{"entries", "Num Queue Entries", {"Facility {} Ability", "Facility {} Amount"}}}});
    t.push_back({"ai.construction_vehicles", "Construction_Vehicles", true, K::Records, "", false,
                 {{"entries", "Num Queue Entries", {"Entry {} Type", "Entry {} Planet Per Item", "Entry {} Must Have At Least"}}}});
    t.push_back({"ai.construction_units", "Construction_Units", true, K::Records, "", false,
                 {{"entries", "Num Queue Entries", {"Entry {} Type", "Entry {} Maximum in kT"}}}});
    t.push_back({"ai.planet_types", "Planet_Types", true, K::Records, "", false, {}});
    t.push_back({"ai.speech", "Speech", true, K::Single, "", true, {}});
    t.push_back({"ai.strategies", "Strategies", true, K::Records, "Name", true, {}});
    return t;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

} // namespace

const GroupSpec* TableSpec::group(std::string_view groupName) const {
    for (const GroupSpec& g : groups)
        if (g.name == groupName) return &g;
    return nullptr;
}

std::span<const TableSpec> tables() {
    static const std::vector<TableSpec> kTables = makeTables();
    return kTables;
}

const TableSpec* findTable(std::string_view patchName) {
    for (const TableSpec& t : tables())
        if (t.name == patchName) return &t;
    return nullptr;
}

const TableSpec* tableOfDataFile(std::string_view fileName) {
    for (const TableSpec& t : tables())
        if (!t.ai && lower(t.file) == lower(fileName)) return &t;
    return nullptr;
}

const TableSpec* tableOfAiFile(std::string_view aiTable) {
    for (const TableSpec& t : tables())
        if (t.ai && lower(t.file) == lower(aiTable)) return &t;
    return nullptr;
}

std::optional<std::string> aiTableOfFileName(std::string_view fileName) {
    const std::string n = lower(fileName);
    // The longest table name that fits wins ("Construction_Units" before a shorter one).
    const TableSpec* best = nullptr;
    for (const TableSpec& t : tables())
        if (t.ai && n.ends_with("_ai_" + lower(t.file) + ".txt") && (!best || t.file.size() > best->file.size())) best = &t;
    if (!best) return std::nullopt;
    return std::string(best->file);
}

std::span<const ReferenceSpec> references() {
    using C = Cascade;
    static const std::vector<ReferenceSpec> kRefs{
        // Technology: an item that needs a removed area can no longer be researched.
        {"tech_areas", "Tech Area Req {}", "tech_areas", C::RemoveRecord, "", ""},
        {"vehicle_sizes", "Tech Area Req {}", "tech_areas", C::RemoveRecord, "", ""},
        {"components", "Tech Area Req {}", "tech_areas", C::RemoveRecord, "", ""},
        {"facilities", "Tech Area Req {}", "tech_areas", C::RemoveRecord, "", ""},
        {"weapon_mounts", "Tech Area Req {}", "tech_areas", C::RemoveRecord, "", ""},
        {"intel_projects", "Tech Area Req {}", "tech_areas", C::RemoveRecord, "", ""},
        {"ai.research", "Tech Area Name", "tech_areas", C::RemoveRecord, "", ""},
        // The galaxy.
        {"quadrant_types", "Type {} Name", "system_types", C::RemoveEntry, "system_types", ""},
        {"system_types", "WP Stellar Abil Type", "stellar_ability_types", C::ClearField, "", "None"},
        {"system_types", "Obj {} Stellar Abil Type", "stellar_ability_types", C::ClearField, "", "None"},
        // Races.
        {"racial_traits", "Required Trait {}", "racial_traits", C::RemoveEntry, "required_traits", ""},
        {"racial_traits", "Restricted Trait {}", "racial_traits", C::RemoveEntry, "restricted_traits", ""},
        {"ai.general", "Race Opt 1 Adv Trait {}", "racial_traits", C::RemoveEntry, "advanced_traits_1", ""},
        {"ai.general", "Race Opt 2 Adv Trait {}", "racial_traits", C::RemoveEntry, "advanced_traits_2", ""},
        {"ai.general", "Race Opt 3 Adv Trait {}", "racial_traits", C::RemoveEntry, "advanced_traits_3", ""},
        {"ai.general", "Culture", "cultures", C::Refuse, "", ""},
        {"ai.general", "Happiness Type", "happiness", C::Refuse, "", ""},
        // The computer players' tables.
        {"ai.planet_types", "Minimum Planet Size for Type", "planet_sizes", C::ClearField, "", ""},
        {"ai.design_creation", "Design Type", "design_types", C::RemoveRecord, "", ""},
        {"ai.design_creation", "Default Strategy", "strategies", C::ClearField, "", ""},
        {"ai.fleets", "Fleets Default Strategy", "strategies", C::ClearField, "", ""},
        {"ai.fleets", "Fleets Default Formation", "formations", C::ClearField, "", ""},
        {"ai.construction_facilities", "Construction Queue Type", "colony_types", C::RemoveRecord, "", ""},
    };
    return kRefs;
}

std::optional<int> matchNumbered(std::string_view key, std::string_view format) {
    const size_t hole = format.find("{}");
    const std::string k = datafile::normalizeKey(key);
    if (hole == std::string_view::npos) return datafile::keysEqual(key, format) ? std::optional<int>(0) : std::nullopt;
    // The parts around the number, normalized as keys are (a space stays a space).
    std::string prefix = lower(format.substr(0, hole));
    std::string suffix = lower(format.substr(hole + 2));
    if (!k.starts_with(prefix)) return std::nullopt;
    size_t i = prefix.size(), digits = 0;
    int n = 0;
    while (i < k.size() && k[i] >= '0' && k[i] <= '9' && digits < 6) {
        n = n * 10 + (k[i] - '0');
        ++i;
        ++digits;
    }
    if (digits == 0 || std::string_view(k).substr(i) != suffix) return std::nullopt;
    return n;
}

std::string numberedKey(std::string_view format, int n) {
    const size_t hole = format.find("{}");
    if (hole == std::string_view::npos) return std::string(format);
    return std::format("{}{}{}", format.substr(0, hole), n, format.substr(hole + 2));
}

std::string entryFieldName(std::string_view format) {
    std::string s(format);
    if (const size_t hole = s.find("{}"); hole != std::string::npos) s.erase(hole, 2);
    std::string out;
    for (char c : s) {
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

} // namespace opense4::mods
