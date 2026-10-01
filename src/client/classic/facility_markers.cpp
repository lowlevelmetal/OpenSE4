#include "client/classic/facility_markers.hpp"

#include "game/abilities.hpp"

#include <array>
#include <initializer_list>
#include <vector>

namespace opense4::client::classic {

namespace {

using game::AbilityKind;

struct Marker {
    const char* letters;
    std::initializer_list<AbilityKind> abilities;   // any of them
};

// Per group, its letters in the order of the Empire Options row.
const std::array<std::vector<Marker>, game::kFacilityMarkerGroups>& groups() {
    static const std::array<std::vector<Marker>, game::kFacilityMarkerGroups> kGroups{{
        {{"R", {AbilityKind::SupplyGeneration}}, {"S", {AbilityKind::Spaceport}}, {"Y", {AbilityKind::SpaceYard}}},
        {{"Ca", {AbilityKind::PlanetChangeAtmosphere}},
         {"Cc", {AbilityKind::PlanetChangeConditions}},
         {"Cv", {AbilityKind::PlanetChangeMineralsValue, AbilityKind::PlanetChangeOrganicsValue, AbilityKind::PlanetChangeRadioactivesValue}}},
        {{"St", {AbilityKind::ShipTraining}}, {"Ft", {AbilityKind::FleetTraining}}},
        {{"Rc", {AbilityKind::ComponentRepair}}, {"Rr", {AbilityKind::ResourceReclamation}}},
        {{"Sst", {AbilityKind::ShipTrainingSystem}}, {"Sft", {AbilityKind::FleetTrainingSystem}}},
        {{"Spv", {AbilityKind::PlanetValueChangeSystem}}, {"Spc", {AbilityKind::PlanetConditionsChangeSystem}}},
        {{"Sph", {AbilityKind::ChangePopulationHappinessSystem}}, {"Spa", {AbilityKind::ChangePopulationSystem}}},
        {{"Scm", {AbilityKind::CombatModifierSystem}}, {"Sdm", {AbilityKind::DamageModifierSystem}}},
        {{"Srm", {AbilityKind::ModifyReproductionSystem}}, {"Ssm", {AbilityKind::ShieldModifierSystem}}},
        {{"Spp", {AbilityKind::PlaguePreventionSystem}}, {"Src", {AbilityKind::ReducedMaintenanceSystem}}},
        {{"Sbe", {AbilityKind::ChangeBadEventChanceSystem}}, {"Sbi", {AbilityKind::ChangeBadIntelChanceSystem}}},
        {{"Slr", {AbilityKind::LongRangeScannerSystem}}},
    }};
    return kGroups;
}

} // namespace

std::string facilityMarkers(const game::Rules& r, const game::Colony& c, uint16_t on) {
    std::string out;
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
            if (!present) continue;
            if (!out.empty()) out += ' ';
            out += m.letters;
        }
    }
    return out;
}

} // namespace opense4::client::classic
