#include "game/abilities.hpp"

#include "datafile/datafile.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace opense4::game {

namespace {

constexpr std::array<std::string_view, static_cast<size_t>(AbilityKind::Count)> kIdentifiers{
#define OPENSE4_ABILITY_TEXT(name, text) text,
    OPENSE4_ABILITIES(OPENSE4_ABILITY_TEXT)
#undef OPENSE4_ABILITY_TEXT
    "Unknown",
};

const std::unordered_map<std::string, AbilityKind>& table() {
    static const std::unordered_map<std::string, AbilityKind> t = [] {
        std::unordered_map<std::string, AbilityKind> m;
        for (size_t i = 0; i < static_cast<size_t>(AbilityKind::Unknown); ++i)
            m.emplace(datafile::normalizeKey(kIdentifiers[i]), static_cast<AbilityKind>(i));
        return m;
    }();
    return t;
}

} // namespace

std::string_view identifier(AbilityKind k) { return kIdentifiers[static_cast<size_t>(k)]; }

std::optional<AbilityKind> parseAbilityKind(std::string_view text) {
    const std::string key = datafile::normalizeKey(text);
    if (key.empty() || key == "none") return std::nullopt;
    if (key.starts_with("ai tag")) return AbilityKind::AITag;
    const auto& t = table();
    if (auto it = t.find(key); it != t.end()) return it->second;
    return AbilityKind::Unknown;
}

ParsedAbility parseAbility(const ruleset::Ability& a) {
    ParsedAbility p;
    p.kind = parseAbilityKind(a.type).value_or(AbilityKind::Unknown);
    p.value1 = a.number1();
    p.value2 = a.number2();
    p.text1 = a.value1;
    p.raw = a.type;
    return p;
}

std::vector<ParsedAbility> parseAbilities(std::span<const ruleset::Ability> list) {
    std::vector<ParsedAbility> out;
    out.reserve(list.size());
    for (const auto& a : list)
        if (parseAbilityKind(a.type)) out.push_back(parseAbility(a));
    return out;
}

int64_t sumValue1(std::span<const ParsedAbility> list, AbilityKind k) {
    int64_t total = 0;
    for (const auto& a : list)
        if (a.kind == k) total += a.value1;
    return total;
}

int64_t bestValue1(std::span<const ParsedAbility> list, AbilityKind k) {
    int64_t best = 0;
    bool found = false;
    for (const auto& a : list)
        if (a.kind == k && (!found || a.value1 > best)) {
            best = a.value1;
            found = true;
        }
    return best;
}

bool hasAbility(std::span<const ParsedAbility> list, AbilityKind k) {
    for (const auto& a : list)
        if (a.kind == k) return true;
    return false;
}

// ---- Aggregation modes ------------------------------------------------------------------------

Aggregation aggregationOf(AbilityKind k) {
    using K = AbilityKind;
    switch (k) {
        case K::StandardShipMovement: case K::SupplyStorage: case K::CargoStorage: case K::ShieldGeneration:
        case K::PhasedShieldGeneration: case K::ShieldRegeneration: case K::ArmorRegeneration: case K::ShieldGenerationFromDamage:
        case K::MineSweeping: case K::BoardingAttack: case K::BoardingDefense: case K::EmergencyEnergy: case K::EmergencyResupply:
        case K::ComponentRepair: case K::SolarSupplyGeneration: case K::LaunchRecoverFighters: case K::LaunchRecoverSatellites:
        case K::LayMines: case K::LaunchDrones: case K::SectorSensorInterference: case K::SectorShieldDisruption: case K::SectorDamage:
        case K::WarpPointTurbulence: case K::SystemMovementTowardsCenter: case K::SystemMovementRandom: case K::SystemDestructiveCenter:
        case K::PlanetShieldGeneration: case K::PlanetChangeConditions: case K::PlanetChangeGroundDefense:
        case K::PlanetChangePopulationHappiness: case K::ModifiedMaintenanceCost:
            return Aggregation::Sum;
        case K::EmissiveArmor: case K::MultiplexTracking: case K::CombatMovement: case K::LongRangeScanner: case K::MedicalBay:
        case K::OpenWarpPointDistance: case K::CreatePlanetSize: case K::DestroyPlanetSize: case K::CreateConstructedPlanet:
        case K::SectorSightObscuration: case K::ResourceReclamation: case K::ResourceConversion: case K::ShipTraining:
        case K::FleetTraining: case K::ShipTrainingSystem: case K::FleetTrainingSystem: case K::CombatModifierSystem:
        case K::DamageModifierSystem: case K::ShieldModifierSystem: case K::PlanetValueChangeSystem:
        case K::PlanetConditionsChangeSystem: case K::ChangeBadIntelChanceSystem: case K::ChangePopulationHappinessSystem:
        case K::ModifyReproductionSystem: case K::ChangePopulationSystem: case K::PlaguePreventionSystem: case K::AncientRuins:
        case K::AncientRuinsUnique: case K::PlanetChangeAtmosphere:
            return Aggregation::Largest;
        case K::MovementBonus: case K::ReducedMaintenanceSystem: return Aggregation::Smallest;
        case K::ShipLifeSupport: case K::ShipCrewQuarters: return Aggregation::Count;
        case K::ShipBridge: case K::ShipAuxiliaryControl: case K::MasterComputer: case K::QuantumReactor: case K::Armor:
        case K::ScannerJammer: case K::SpaceYard: case K::SelfDestruct: case K::CombatBestExperience: case K::WeaponsAlwaysHit:
        case K::ComponentDestroyedOnUse: case K::LongRangeScannerSystem: case K::ColonizeRock: case K::ColonizeIce:
        case K::ColonizeGas: case K::CloseWarpPoint: case K::CreateStar: case K::DestroyStar: case K::CreateStorm:
        case K::DestroyStorm: case K::CreateNebulae: case K::DestroyNebulae: case K::CreateBlackHole: case K::DestroyBlackHole:
        case K::StopPlanetDestroyer: case K::StopStarDestroyer: case K::StopNebulaeCreator: case K::StopBlackHoleCreator:
        case K::StopOpenWarpPoint: case K::StopCloseWarpPoint:
            return Aggregation::Present;
        case K::CloakLevel: case K::SensorLevel: return Aggregation::PerSightType;
        case K::ExtraMovementGeneration: return Aggregation::FirstPerId;
        case K::CombatToHitOffensePlus: case K::CombatToHitDefensePlus: case K::CombatToHitOffenseMinus:
        case K::CombatToHitDefenseMinus:
            return Aggregation::PerFamily;
        default: return Aggregation::Unspecified;
    }
}

namespace {

int64_t valueOf(const ParsedAbility& a, bool value2) { return value2 ? a.value2 : a.value1; }

} // namespace

int64_t abilitySum(std::span<const ParsedAbility> list, AbilityKind k, bool value2) {
    int64_t total = 0;
    for (const auto& a : list)
        if (a.kind == k) total += valueOf(a, value2);
    return std::min(kAbilitySumCap, total);
}

int64_t abilityLargest(std::span<const ParsedAbility> list, AbilityKind k, bool value2) {
    int64_t best = 0;
    for (const auto& a : list)
        if (a.kind == k) best = std::max(best, valueOf(a, value2));
    return best;
}

int64_t abilitySmallest(std::span<const ParsedAbility> list, AbilityKind k) {
    // The original starts from 99,999 and reads a final 99,999 as "none".
    constexpr int64_t kStart = 99'999;
    int64_t least = kStart;
    for (const auto& a : list)
        if (a.kind == k) least = std::min(least, a.value1);
    return least == kStart ? 0 : least;
}

int64_t abilityCount(std::span<const ParsedAbility> list, AbilityKind k) {
    int64_t n = 0;
    for (const auto& a : list) n += a.kind == k;
    return n;
}

int64_t abilityPerSightType(std::span<const ParsedAbility> list, AbilityKind k, SightType t) {
    int64_t best = 0;
    for (const auto& a : list) {
        SightType at{};
        if (a.kind == k && parseSightType(a.text1, at) && at == t) best = std::max(best, a.value2);
    }
    return best;
}

int64_t abilityFirstPerId(std::span<const ParsedAbility> list, AbilityKind k) {
    std::vector<uint16_t> seen;
    int64_t total = 0;
    for (const auto& a : list) {
        if (a.kind != k) continue;
        const auto id = static_cast<uint16_t>(static_cast<uint64_t>(a.value2) & 0xffffu);
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back(id);
        total += a.value1;
    }
    return total;
}

int64_t abilityPerFamily(std::span<const ParsedAbility> list, AbilityKind k) {
    int64_t total = 0;
    std::vector<std::pair<int, int64_t>> families;  // family -> its largest value, in first-seen order
    for (const auto& a : list) {
        if (a.kind != k) continue;
        if (!a.fromComponent) {
            total += a.value1;
            continue;
        }
        auto it = std::find_if(families.begin(), families.end(), [&](const auto& f) { return f.first == a.family; });
        if (it == families.end()) families.emplace_back(a.family, a.value1);
        else it->second = std::max(it->second, a.value1);
    }
    for (const auto& [family, value] : families) total += value;
    return std::min(kAbilitySumCap, total);
}

int64_t abilityValue(std::span<const ParsedAbility> list, AbilityKind k) {
    switch (aggregationOf(k)) {
        case Aggregation::Sum: return abilitySum(list, k);
        case Aggregation::Largest: return abilityLargest(list, k);
        case Aggregation::Smallest: return abilitySmallest(list, k);
        case Aggregation::Count: return abilityCount(list, k);
        case Aggregation::Present: return hasAbility(list, k) ? 1 : 0;
        case Aggregation::PerSightType: {
            int64_t best = 0;
            for (size_t t = 0; t < kSightTypes; ++t) best = std::max(best, abilityPerSightType(list, k, static_cast<SightType>(t)));
            return best;
        }
        case Aggregation::FirstPerId: return abilityFirstPerId(list, k);
        case Aggregation::PerFamily: return abilityPerFamily(list, k);
        case Aggregation::Unspecified: break;
    }
    return abilitySum(list, k);
}

Resources spaceYardRates(std::span<const ParsedAbility> list) {
    Resources r;
    for (const auto& a : list)
        if (a.kind == AbilityKind::SpaceYard && a.value1 >= 1 && a.value1 <= 3) r.v[static_cast<size_t>(a.value1 - 1)] += a.value2;
    return r;
}

} // namespace opense4::game
