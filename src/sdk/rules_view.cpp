#include "sdk/rules_view.hpp"

#include "game/design.hpp"
#include "sdk/parts.hpp"
#include "sdk/view.hpp"

namespace opense4::sdk {

namespace detail {

namespace {

Value cost(const ruleset::Cost& c) { return enc(game::Resources::from(c)); }

Value requirements(const std::vector<ruleset::TechRequirement>& reqs) {
    return listOf(reqs, [](const ruleset::TechRequirement& q) { return Map(2)("area", id(q.area))("level", num(q.level)).done(); });
}

Value textList(const std::vector<std::string>& list) { return enc(list); }

Value vehicleTypes(ruleset::VehicleTypeMask mask) {
    ValueList out;
    for (size_t t = 0; t < static_cast<size_t>(ruleset::VehicleType::Count); ++t)
        if (mask & ruleset::maskOf(static_cast<ruleset::VehicleType>(t))) out.push_back(Value(enumName(static_cast<ruleset::VehicleType>(t))));
    return Value(std::move(out));
}

Value weapon(const game::Rules& r, uint32_t component) {
    const ruleset::Weapon& w = r.component(component).weapon;
    if (w.kind == ruleset::WeaponKind::None) return Value();
    return Map(12)("kind", enc(w.kind))("targets", textList(w.targets))("damage_at_range", enc(w.damageAtRange))(
               "damage_type", Value(w.damageType))("reload_rate", num(w.reloadRate))("modifier", num(w.modifier))("family", num(w.family))(
               "seeker_speed", num(w.seekerSpeed))("seeker_resistance", num(w.seekerDamageResistance))(
               "max_range", num(game::weaponMaxRange(r, game::DesignEntry{component, -1})))
        .done();
}

Value components(const game::Rules& r) {
    const auto& list = r.data().components;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::Component& c = list[i];
        out.push_back(Map(18)("id", num(i))("name", Value(c.name))("description", Value(c.description))("tonnage", num(c.tonnage))(
                          "structure", num(c.structure))("cost", cost(c.cost))("vehicle_types", vehicleTypes(c.vehicles))(
                          "supply_used", num(c.supplyUsed))("max_per_vehicle", num(c.maxPerVehicle))("group", Value(c.generalGroup))(
                          "family", num(c.family))("roman_numeral", num(c.romanNumeral))("custom_group", num(c.customGroup))(
                          "requirements", requirements(c.requirements))("abilities", abilityEntries(c.abilities))(
                          "weapon", weapon(r, static_cast<uint32_t>(i)))
                          .done());
    }
    return Value(std::move(out));
}

Value facilities(const game::Rules& r) {
    const auto& list = r.data().facilities;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::Facility& f = list[i];
        out.push_back(Map(10)("id", num(i))("name", Value(f.name))("description", Value(f.description))("group", Value(f.group))(
                          "family", num(f.family))("roman_numeral", num(f.romanNumeral))("restriction", Value(f.restriction))(
                          "cost", cost(f.cost))("requirements", requirements(f.requirements))("abilities", abilityEntries(f.abilities))
                          .done());
    }
    return Value(std::move(out));
}

Value hulls(const game::Rules& r) {
    const auto& list = r.data().vehicleSizes;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::VehicleSize& h = list[i];
        out.push_back(Map(20)("id", num(i))("name", Value(h.name))("short_name", Value(h.shortName))("description", Value(h.description))(
                          "code", Value(h.code))("type", enc(h.type))("tonnage", num(h.tonnage))("cost", cost(h.cost))(
                          "engines_per_move", num(h.enginesPerMove))("requirements", requirements(h.requirements))(
                          "abilities", abilityEntries(h.abilities))("must_have_bridge", Value(h.mustHaveBridge))(
                          "can_have_aux_control", Value(h.canHaveAuxControl))("min_life_support", num(h.minLifeSupport))(
                          "min_crew_quarters", num(h.minCrewQuarters))("uses_engines", Value(h.usesEngines))("max_engines", num(h.maxEngines))(
                          "max_percent_fighter_bays", num(h.maxPercentFighterBays))(
                          "max_percent_colony_modules", num(h.maxPercentColonyModules))("max_percent_cargo", num(h.maxPercentCargo))
                          .done());
    }
    return Value(std::move(out));
}

Value mounts(const game::Rules& r) {
    const auto& list = r.data().weaponMounts;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::WeaponMount& m = list[i];
        out.push_back(Map(20)("id", num(i))("name", Value(m.longName))("short_name", Value(m.shortName))("description", Value(m.description))(
                          "code", Value(m.code))("cost_percent", num(m.costPercent))("tonnage_percent", num(m.tonnagePercent))(
                          "structure_percent", num(m.structurePercent))("damage_percent", num(m.damagePercent))(
                          "supply_percent", num(m.supplyPercent))("shield_percent", num(m.shieldPercent))("range_modifier", num(m.rangeModifier))(
                          "to_hit_modifier", num(m.toHitModifier))("minimum_hull_size", num(m.minimumVehicleSize))(
                          "maximum_hull_size", num(m.maximumVehicleSize))("families", enc(m.familyRequirement))(
                          "weapon_type_requirement", Value(m.weaponTypeRequirement))("vehicle_type", Value(m.vehicleType))(
                          "requirements", requirements(m.requirements))
                          .done());
    }
    return Value(std::move(out));
}

Value techs(const game::Rules& r) {
    const auto& list = r.data().techAreas;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::TechArea& t = list[i];
        out.push_back(Map(12)("id", num(i))("name", Value(t.name))("group", Value(t.group))("description", Value(t.description))(
                          "max_level", num(t.maxLevel))("level_cost", num(t.levelCost))("start_level", num(t.startLevel))(
                          "raise_level", num(t.raiseLevel))("racial_area", num(t.racialArea))("unique_area", num(t.uniqueArea))(
                          "can_be_removed", Value(t.canBeRemoved))("requirements", requirements(t.requirements))
                          .done());
    }
    return Value(std::move(out));
}

Value racialTraits(const game::Rules& r) {
    const auto& list = r.data().racialTraits;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::RacialTrait& t = list[i];
        out.push_back(Map(9)("id", num(i))("name", Value(t.name))("description", Value(t.description))("general_type", Value(t.generalType))(
                          "cost", num(t.cost))("trait_type", Value(t.traitType))("values", textList(t.values))(
                          "required_traits", textList(t.requiredTraits))("restricted_traits", textList(t.restrictedTraits))
                          .done());
    }
    return Value(std::move(out));
}

Value cultures(const game::Rules& r) {
    const auto& list = r.data().cultures;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::Culture& c = list[i];
        out.push_back(Map(13)("id", num(i))("name", Value(c.name))("description", Value(c.description))("production", num(c.production))(
                          "research", num(c.research))("intelligence", num(c.intelligence))("trade", num(c.trade))(
                          "space_combat", num(c.spaceCombat))("ground_combat", num(c.groundCombat))("happiness", num(c.happiness))(
                          "maintenance", num(c.maintenance))("shipyard_rate", num(c.shipyardRate))("repair", num(c.repair))
                          .done());
    }
    return Value(std::move(out));
}

Value happinessModels(const game::Rules& r) {
    const auto& list = r.data().happinessModels;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::HappinessModel& h = list[i];
        Value triggers = listOf(h.triggers, [](const std::pair<std::string, int>& t) {
            return Map(2)("trigger", Value(t.first))("change", num(t.second)).done();
        });
        out.push_back(Map(6)("id", num(i))("name", Value(h.name))("description", Value(h.description))(
                          "max_positive_change", num(h.maxPositiveChange))("max_negative_change", num(h.maxNegativeChange))(
                          "triggers", std::move(triggers))
                          .done());
    }
    return Value(std::move(out));
}

Value races(const game::Rules& r) {
    return listOf(r.racePresets(), [](const ruleset::RacePreset& p) {
        Value tiers = listOf(p.tiers, [](const ruleset::RaceTier& t) {
            Value ch = listOf(t.characteristics, [](const std::pair<std::string, int>& c) {
                return Map(2)("name", Value(c.first))("value", num(c.second)).done();
            });
            return Map(2)("characteristics", std::move(ch))("traits", enc(t.traits)).done();
        });
        return Map(16)("name", Value(p.name))("style", Value(p.folder))("neutral", Value(p.neutral))("description", Value(p.description))(
                   "empire_name", Value(p.empireName))("empire_type", Value(p.empireType))("emperor_name", Value(p.emperorName))(
                   "emperor_title", Value(p.emperorTitle))("demeanor", Value(p.demeanor))("culture", Value(p.culture))(
                   "happiness_type", Value(p.happinessType))("planet_type", Value(p.planetType))("atmosphere", Value(p.atmosphere))(
                   "personality_group", num(p.personalityGroup))("tiers", std::move(tiers))
            .done();
    });
}

Value planetSizes(const game::Rules& r) {
    const auto& list = r.data().planetSizes;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::PlanetSize& p = list[i];
        out.push_back(Map(11)("id", num(i))("name", Value(p.name))("physical_type", Value(p.physicalType))("stellar_size", Value(p.stellarSize))(
                          "max_facilities", num(p.maxFacilities))("max_population", num(p.maxPopulation))("max_cargo", num(p.maxCargo))(
                          "max_facilities_domed", num(p.maxFacilitiesDomed))("max_population_domed", num(p.maxPopulationDomed))(
                          "max_cargo_domed", num(p.maxCargoDomed))("constructed", Value(p.constructed))
                          .done());
    }
    return Value(std::move(out));
}

Value systemTypes(const game::Rules& r) {
    const auto& list = r.data().systemTypes;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::SystemType& t = list[i];
        out.push_back(Map(6)("id", num(i))("name", Value(t.name))("description", Value(t.description))("physical_type", Value(t.physicalType))(
                          "empires_can_start_in", Value(t.empiresCanStartIn))("abilities", abilityEntries(t.abilities))
                          .done());
    }
    return Value(std::move(out));
}

Value sectorTypes(const game::Rules& r) {
    const auto& list = r.data().sectorObjectTypes;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::SectorObjectType& t = list[i];
        out.push_back(Map(14)("id", num(i))("physical_type", Value(t.physicalType))("description", Value(t.description))(
                          "planet_size", Value(t.planetSize))("planet_physical_type", Value(t.planetPhysicalType))(
                          "planet_atmosphere", Value(t.planetAtmosphere))("star_size", Value(t.starSize))("star_age", Value(t.starAge))(
                          "star_color", Value(t.starColor))("star_luminosity", Value(t.starLuminosity))("storm_size", Value(t.stormSize))(
                          "warp_point_size", Value(t.warpPointSize))("unusual", Value(t.unusual))
                          .done());
    }
    return Value(std::move(out));
}

Value abilities(const game::Rules& r) {
    ValueList out;
    for (size_t k = 0; k < static_cast<size_t>(game::AbilityKind::Unknown); ++k) {
        const auto kind = static_cast<game::AbilityKind>(k);
        out.push_back(Map(2)("name", Value(game::identifier(kind)))("aggregation", enc(game::aggregationOf(kind))).done());
    }
    // The ability names mods declare (docs/sdk/packages-and-data.md), combined as declared.
    for (const ruleset::DeclaredAbility& d : r.data().declaredAbilities) {
        const game::Aggregation how = d.combine == ruleset::Combine::Max   ? game::Aggregation::Largest
                                      : d.combine == ruleset::Combine::Min ? game::Aggregation::Smallest
                                                                           : game::Aggregation::Sum;
        out.push_back(Map(2)("name", Value(d.name))("aggregation", enc(how)).done());
    }
    return Value(std::move(out));
}

Value formationSlot(const ruleset::FormationSlot& f) {
    return Map(3)("x", num(f.x))("y", num(f.y))("design_type", Value(f.designType)).done();
}

Value formations(const game::Rules& r) {
    const auto& list = r.data().formations;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::Formation& f = list[i];
        out.push_back(Map(5)("id", num(i))("name", Value(f.name))("description", Value(f.description))("leader", formationSlot(f.leader))(
                          "positions", listOf(f.positions, formationSlot))
                          .done());
    }
    return Value(std::move(out));
}

Value strategies(const game::Rules& r) {
    const auto& list = r.data().combatStrategies;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i)
        out.push_back(Map(3)("id", num(i))("name", Value(list[i].name))("settings", enc(list[i].settings)).done());
    return Value(std::move(out));
}

Value intelProjects(const game::Rules& r) {
    const auto& list = r.data().intelProjects;
    ValueList out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::IntelProject& p = list[i];
        out.push_back(Map(8)("id", num(i))("name", Value(p.name))("description", Value(p.description))("group", Value(p.group))(
                          "cost", num(p.cost))("type", Value(p.type))("effect_amount", num(p.effectAmount))(
                          "requirements", requirements(p.requirements))
                          .done());
    }
    return Value(std::move(out));
}

} // namespace

} // namespace detail

script::Value buildRulesView(const game::Rules& r) {
    using namespace detail;
    const ruleset::NameLists& names = r.data().names;
    return Map(24)("api", num(kApiVersion))("components", components(r))("facilities", facilities(r))("hulls", hulls(r))(
               "mounts", mounts(r))("techs", techs(r))("racial_traits", racialTraits(r))("cultures", cultures(r))(
               "happiness_models", happinessModels(r))("races", races(r))("planet_sizes", planetSizes(r))("system_types", systemTypes(r))(
               "sector_types", sectorTypes(r))("abilities", abilities(r))("formations", formations(r))("strategies", strategies(r))(
               "intel_projects", intelProjects(r))("design_types", enc(names.designTypes))("colony_types", enc(names.colonyTypes))(
               "repair_priorities", enc(names.repairPriorities))
        .done();
}

} // namespace opense4::sdk
