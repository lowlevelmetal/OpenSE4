#include "client/classic/facility_markers.hpp"

#include "game/abilities.hpp"
#include "game/query.hpp"

#include <array>
#include <vector>

namespace opense4::client::classic {

namespace {

using game::AbilityKind;

struct Marker {
    const char* letters;
    std::vector<AbilityKind> abilities;   // any of them (a vector: an initializer_list here would dangle)
    bool yard = false;                    // Y: a working space yard
};

// Per Empire Options row, its letters in the order of the Q44 table.
const std::array<std::vector<Marker>, game::kFacilityMarkerGroups>& groups() {
    static const std::array<std::vector<Marker>, game::kFacilityMarkerGroups> kGroups{{
        {{"R", {AbilityKind::SupplyGeneration}}, {"S", {AbilityKind::Spaceport}}, {"Y", {AbilityKind::SpaceYard}, true}},
        {{"Ca", {AbilityKind::PlanetChangeAtmosphere}},
         {"Cc", {AbilityKind::PlanetChangeConditions}},
         {"Cv", {AbilityKind::PlanetChangeMineralsValue, AbilityKind::PlanetChangeOrganicsValue, AbilityKind::PlanetChangeRadioactivesValue}}},
        {{"St", {AbilityKind::ShipTraining}}, {"Ft", {AbilityKind::FleetTraining}}},
        {{"Rc", {AbilityKind::ResourceConversion}}, {"Rr", {AbilityKind::ResourceReclamation}}},
        {{"Sst", {AbilityKind::ShipTrainingSystem}}, {"Sft", {AbilityKind::FleetTrainingSystem}}},
        {{"Spv", {AbilityKind::PlanetValueChangeSystem}}, {"Spc", {AbilityKind::PlanetConditionsChangeSystem}}},
        {{"Sph", {AbilityKind::ChangePopulationHappinessSystem}}, {"Spa", {AbilityKind::ChangePopulationSystem}}},
        {{"Scm", {AbilityKind::CombatModifierSystem}}, {"Sdm", {AbilityKind::DamageModifierSystem}}},
        {{"Srm", {AbilityKind::ReducedMaintenanceSystem}}, {"Ssm", {AbilityKind::ShieldModifierSystem}}},
        {{"Spp", {AbilityKind::PlaguePreventionSystem}}, {"Src", {AbilityKind::ModifyReproductionSystem}}},
        {{"Sbe", {AbilityKind::ChangeBadEventChanceSystem}}, {"Sbi", {AbilityKind::ChangeBadIntelChanceSystem}}},
        {{"Slr", {AbilityKind::LongRangeScannerSystem}}},
    }};
    return kGroups;
}

} // namespace

std::vector<std::string> facilityMarkerGroups(const game::Rules& r, const game::Colony& c, uint16_t on, bool yardWorks) {
    std::vector<std::string> out;
    if (on == 0) return out;
    auto has = [&](AbilityKind k) {
        for (const uint32_t f : c.facilities)
            if (f < r.data().facilities.size() && game::hasAbility(r.facilityAbilities(f), k)) return true;
        return false;
    };
    for (size_t g = 0; g < groups().size(); ++g) {
        if ((on & (1u << g)) == 0) continue;
        for (const Marker& m : groups()[g]) {
            bool present = false;
            for (const AbilityKind k : m.abilities) present = present || has(k);
            if (m.yard) present = present && yardWorks;
            if (present) out.emplace_back(m.letters);
        }
    }
    return out;
}

std::string facilityMarkers(const game::Rules& r, const game::Colony& c, uint16_t on) {
    std::string out;
    for (const std::string& g : facilityMarkerGroups(r, c, on)) {
        if (!out.empty()) out += ' ';
        out += g;
    }
    return out;
}

bool showsFacilityMarkers(const game::GameState& s, game::EmpireId viewer, game::EmpireId owner) {
    return owner == viewer || game::allied(s, viewer, owner);
}

std::vector<MarkerPlace> packFacilityMarkers(const std::vector<int>& widths, int squareWidth) {
    std::vector<MarkerPlace> out;
    int right = squareWidth, line = 0;
    for (const int w : widths) {
        // At or left of the left edge: a new line, again from the right edge
        // (a group too wide for a line of its own still starts one).
        if (right - w <= 0 && right != squareWidth) {
            ++line;
            right = squareWidth;
        }
        out.push_back({right - w, line});
        right -= w;
    }
    return out;
}

} // namespace opense4::client::classic
