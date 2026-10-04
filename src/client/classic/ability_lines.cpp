#include "client/classic/ability_lines.hpp"

#include "datafile/datafile.hpp"
#include "game/economy.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
#include <utility>

namespace opense4::client::classic {

namespace {

bool keyIn(std::string_view key, std::initializer_list<std::string_view> keys) {
    return std::any_of(keys.begin(), keys.end(), [&](std::string_view k) { return datafile::keysEqual(key, k); });
}

// The trait types whose descriptions the planet page lists, and the ship page's.
bool planetTraitType(std::string_view type) {
    return keyIn(type, {"Reproduction", "Mineral Production", "Organics Production", "Radioactives Production", "Research Production",
                        "Intelligence Production", "SY Rate", "No Plagues", "No Spaceports", "Population Happiness", "Planet Storage Space",
                        "Planetary SY Rate", "Mineral Storage", "Organics Storage", "Radioactives Storage", "Production", "Trade",
                        "Ground Combat", "Tolerance", "Tollerance"});
}
bool shipTraitType(std::string_view type) {
    return keyIn(type, {"Maintenance Cost", "Supply Cost", "Vehicle Speed", "Ship Bonus", "Space Combat", "Repair", "Ship Attack", "Ship Defense"});
}

// "+10%" for the racial and culture lines; the population and mood lines put
// a space before a negative value instead (a quirk the spec records).
std::string signedPercent(int v) { return std::format("{:+}%", v); }
std::string quirkPercent(int v) { return v < 0 ? std::format(" {}%", v) : std::format("+{}%", v); }

// A population as the page writes it: millions, or billions above 9999M.
std::string populationAmount(int64_t millions) { return millions > 9999 ? std::format("{}B", millions / 1000) : std::format("{}M", millions); }

const game::Race* ownerRace(const game::GameState& s, game::EmpireId e) {
    return e.valid() && e.index() < s.empires.size() ? &s.empire(e).race : nullptr;
}

void traitDescriptions(const game::Rules& r, const game::Race& race, bool planet, std::vector<std::string>& out) {
    for (uint32_t ti : race.traits) {
        if (ti >= r.data().racialTraits.size()) continue;
        const ruleset::RacialTrait& t = r.data().racialTraits[ti];
        if (planet ? planetTraitType(t.traitType) : shipTraitType(t.traitType)) out.push_back(t.description);
    }
}

} // namespace

std::vector<std::string> planetAbilityLines(const game::Rules& r, const game::GameState& s, game::ObjectId planet, game::EmpireId viewer,
                                            bool racial) {
    std::vector<std::string> out;
    if (!planet.valid() || planet.index() >= s.galaxy.objects.size()) return out;
    // 1. The planet's own natural abilities, in the planet's order.
    for (const ruleset::Ability& a : s.galaxy.object(planet).abilities) out.push_back(a.description);
    const game::Colony* c = s.colony(planet);
    if (!c || c->owner != viewer) return out;
    const game::Race* race = ownerRace(s, c->owner);
    if (!race) return out;
    // 2. The owner's racial traits that concern planets.
    if (racial) traitDescriptions(r, *race, true, out);
    // 3. Each characteristic that is not 100 % and concerns planets, in characteristic order.
    using game::Characteristic;
    static constexpr std::array<std::pair<Characteristic, const char*>, 11> kPlanetCharacteristics{{
        {Characteristic::PhysicalStrength, "ground combat"},
        {Characteristic::Intelligence, "research"},
        {Characteristic::Cunning, "intelligence"},
        {Characteristic::EnvironmentalResistance, "tolerance"},
        {Characteristic::Reproduction, "reproduction"},
        {Characteristic::Happiness, "happiness"},
        {Characteristic::PoliticalSavvy, "trade"},
        {Characteristic::MiningAptitude, "mineral production"},
        {Characteristic::FarmingAptitude, "organics production"},
        {Characteristic::RefiningAptitude, "radioactives production"},
        {Characteristic::ConstructionAptitude, "space yard rate"},
    }};
    for (const auto& [ch, what] : kPlanetCharacteristics)
        if (const int d = race->characteristic(ch) - 100; racial && d != 0) out.push_back(std::format("Racial Trait: {} {}", signedPercent(d), what));
    // 4. The culture's modifiers that concern planets, in the culture's order.
    if (const ruleset::Culture* culture = racial ? r.culture(*race) : nullptr) {
        const std::array<std::pair<int, const char*>, 7> kPlanetCulture{{
            {culture->production, "production"},
            {culture->research, "research"},
            {culture->intelligence, "intelligence"},
            {culture->trade, "trade"},
            {culture->groundCombat, "ground combat"},
            {culture->happiness, "happiness"},
            {culture->shipyardRate, "space yard rate"},
        }};
        for (const auto& [v, what] : kPlanetCulture)
            if (v != 0) out.push_back(std::format("Cultural Trait: {} {}", signedPercent(v), what));
    }
    // 5. The population level: the first row of the population-modifier table
    // whose amount is at least the colony's population; its lower bound L is 0
    // for the first row, else the previous row's amount plus 1.
    const int64_t population = c->totalPopulation();
    const auto rows = r.populationRows();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].amount < population) continue;
        const std::string level = populationAmount(i == 0 ? 0 : rows[i - 1].amount + 1);
        if (rows[i].production != 100)
            out.push_back(std::format("Population level {}: {} production", level, quirkPercent(rows[i].production - 100)));
        if (rows[i].shipyard != 100)
            out.push_back(std::format("Population level {}: {} construction rate", level, quirkPercent(rows[i].shipyard - 100)));
        break;
    }
    // 6. The mood band's modifier.
    if (const int mood = game::economy::moodOutputPercent(r, game::moodFromAnger(c->anger)); mood != 100)
        out.push_back(std::format("Happiness: {} production", quirkPercent(mood - 100)));
    return out;
}

std::vector<std::string> vehicleAbilityLines(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, bool racial) {
    std::vector<std::string> out;
    if (!v.design.valid() || v.design.index() >= s.designs.size()) return out;
    const game::Design& d = s.design(v.design);
    // The hull's entries; designs and vehicles carry none of their own, and
    // the components' abilities are on their own reports.
    if (d.hull < r.data().vehicleSizes.size())
        for (const ruleset::Ability& a : r.hull(d.hull).abilities) out.push_back(a.description);
    const game::Race* race = racial ? ownerRace(s, v.owner) : nullptr;
    if (!race) return out;
    traitDescriptions(r, *race, false, out);
    using game::Characteristic;
    static constexpr std::array<std::pair<Characteristic, const char*>, 4> kShipCharacteristics{{
        {Characteristic::Aggressiveness, "ship attack"},
        {Characteristic::Defensiveness, "ship defense"},
        {Characteristic::RepairAptitude, "repair rate"},
        {Characteristic::MaintenanceAptitude, "maintenance"},
    }};
    for (const auto& [ch, what] : kShipCharacteristics)
        if (const int dv = race->characteristic(ch) - 100; dv != 0) out.push_back(std::format("Racial Trait: {} {}", signedPercent(dv), what));
    if (const ruleset::Culture* culture = r.culture(*race)) {
        const std::array<std::pair<int, const char*>, 3> kShipCulture{{
            {culture->spaceCombat, "space combat"},
            {culture->maintenance, "maintenance cost"},
            {culture->repair, "repair"},
        }};
        for (const auto& [cv, what] : kShipCulture)
            if (cv != 0) out.push_back(std::format("{} {}", signedPercent(cv), what));
    }
    return out;
}

} // namespace opense4::client::classic
