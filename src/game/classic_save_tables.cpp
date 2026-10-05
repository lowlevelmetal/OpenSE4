// Built-in numbers of the original's saved games (docs/spec/08 Appendix A,
// §3.7): ability ids and the computer players' design type codes.

#include "game/classic_save.hpp"

#include "datafile/datafile.hpp"

#include <array>
#include <format>

namespace opense4::game::classic {

namespace {

// Appendix A, ids 0..143; 144..163 are "AI Tag 01".."AI Tag 20", then 164..168.
constexpr std::array<std::string_view, 144> kAbilities{
    "",   // 0: none
    "Random",
    "Warp Point - Unstable",
    "Warp Point - Turbulence",
    "Warp Point - Periodic",
    "Warp Point - Ability Required",
    "Star - Unstable",
    "Sector - Sight Obscuration",
    "Sector - Sensor Interference",
    "Sector - Shield Disruption",
    "Sector - Damage",
    "Sector - Ability Required",
    "Resource Generation - Minerals",
    "Resource Generation - Organics",
    "Resource Generation - Radioactives",
    "Point Generation - Research",
    "Point Generation - Intelligence",
    "Spaceport",
    "Palace",
    "Supply Generation",
    "Planet - Change Minerals Value",
    "Planet - Change Organics Value",
    "Planet - Change Radioactives Value",
    "Planet - Change Conditions",
    "Planet - Change Population Happiness",
    "Planet - Change Ground Defense",
    "Planet - Shield Generation",
    "Shield Generation",
    "Phased Shield Generation",
    "Component Repair",
    "Cargo Storage",
    "Drop Troops",
    "Launch/Recover Fighters",
    "Lay Mines",
    "Multiplex Tracking",
    "Combat To Hit Offense Plus",
    "Combat To Hit Defense Plus",
    "Mine Sweeping",
    "Medical Bay",
    "Resupply Pod",
    "Movement Bonus",
    "Emissive Armor",
    "Shield Regeneration",
    "Master Computer",
    "Cloak Level",
    "Sensor Level",
    "Emergency Resupply",
    "Emergency Energy",
    "Long Range Scanner",
    "Open Warp Point Distance",
    "Create Planet Size",
    "Destroy Planet Size",
    "Boarding Attack",
    "Boarding Defense",
    "Standard Ship Movement",
    "Ship Bridge",
    "Ship Auxiliary Control",
    "Ship Life Support",
    "Ship Crew Quarters",
    "Scanner Jammer",
    "Quantum Reactor",
    "Supply Storage",
    "Space Yard",
    "Maximum Population",
    "Resource Storage - Mineral",
    "Resource Storage - Organics",
    "Resource Storage - Radioactives",
    "Resource Gen Modifier Planet - Minerals",
    "Resource Gen Modifier Planet - Organics",
    "Resource Gen Modifier Planet - Radioactives",
    "Resource Gen Modifier System - Minerals",
    "Resource Gen Modifier System - Organics",
    "Resource Gen Modifier System - Radioactives",
    "Planet Point Generation Modifier - Research",
    "Planet Point Generation Modifier - Intelligence",
    "System Point Generation Modifier - Research",
    "System Point Generation Modifier - Intelligence",
    "Combat Modifier - System",
    "Damage Modifier - System",
    "Planet Value Change - System",
    "Planet Conditions Change - System",
    "Change Bad Event Chance - System",
    "Change Bad Intelligence Chance - System",
    "Change Population Happiness - System",
    "Ship Training",
    "Fleet Training",
    "Modify Reproduction - System",
    "Change Population - System",
    "Plague Prevention - System",
    "Resource Conversion",
    "Resource Reclamation",
    "Close Warp Point",
    "Destroy Star",
    "Create Star",
    "Destroy Storm",
    "Create Storm",
    "Self-Destruct",
    "Colonize Planet - Rock",
    "Colonize Planet - Ice",
    "Colonize Planet - Gas",
    "Point-Defense",
    "Armor",
    "Launch/Recover Satellites",
    "Remote Resource Generation - Minerals",
    "Remote Resource Generation - Organics",
    "Remote Resource Generation - Radioactives",
    "Armor Regeneration",
    "Shield Generation From Damage",
    "System - Movement Towards Center",
    "System - Movement Random",
    "System - Destructive Center",
    "Destroy Nebulae",
    "Create Nebulae",
    "Destroy Black Hole",
    "Create Black Hole",
    "Stop Planet Destroyer",
    "Stop Star Destroyer",
    "Stop Nebulae Creator",
    "Stop Black Hole Creator",
    "Stop Open Warp Point",
    "Stop Close Warp Point",
    "Component Destroyed On Use",
    "Ancient Ruins",
    "Ancient Ruins Unique",
    "Combat Best Experience",
    "Combat Movement",
    "Solar Supply Generation",
    "Extra Movement Generation",
    "Planet - Change Atmosphere",
    "Weapons Always Hit",
    "Create Constructed Planet",
    "Constructed Planet Requirements",
    "Modified Maintenance Cost",
    "Ship Training - System",
    "Fleet Training - System",
    "Long Range Scanner - System",
    "Solar Resource Generation - Minerals",
    "Solar Resource Generation - Organics",
    "Solar Resource Generation - Radioactives",
    "Reduced Maintenance Cost - System",
    "Shield Modifier - System",
    "Combat To Hit Offense Minus",
    "Combat To Hit Defense Minus",
    "Launch Drones",
};

constexpr std::array<std::string_view, 5> kAbilitiesTail{
    "Generate Points Minerals", "Generate Points Organics", "Generate Points Radioactives", "Generate Points Research",
    "Generate Points Intelligence",
};

std::array<std::string, 20> aiTags() {
    std::array<std::string, 20> out;
    for (size_t i = 0; i < out.size(); ++i) out[i] = std::format("AI Tag {:02}", i + 1);
    return out;
}

} // namespace

std::string_view abilityName(uint16_t id) {
    static const std::array<std::string, 20> tags = aiTags();
    if (size_t{id} < kAbilities.size()) return kAbilities[id];
    if (id < 164) return tags[id - 144];
    if (id < kAbilityIds) return kAbilitiesTail[id - 164];
    return {};
}

std::optional<uint16_t> abilityId(std::string_view name) {
    if (name.empty()) return std::nullopt;
    for (uint16_t id = 1; id < kAbilityIds; ++id)
        if (datafile::keysEqual(abilityName(id), name)) return id;
    return std::nullopt;
}

// ---- Design type codes (§3.7) -------------------------------------------------------------------------

namespace {

// The design type code of each of the 39 names the computer players know
// (spec 08 §3.7; every code observed with its name in the sample saves).
struct TypeCode {
    uint16_t code;
    std::string_view name;
};
constexpr std::array<TypeCode, 39> kTypeCodes{{
    {1, "Attack Ship"},
    {2, "Defense Ship"},
    {3, "Attack Base"},
    {4, "Defense Base"},
    {5, "Base Space Yard"},
    {6, "Population Transport"},
    {7, "Troop Transport"},
    {8, "Carrier"},
    {9, "Colony (Rock)"},
    {10, "Colony (Ice)"},
    {11, "Colony (Gas)"},
    {12, "Mine Layer"},
    {13, "Mine Sweeper"},
    {14, "Boarding Ship"},
    {15, "Open Warp Point"},
    {16, "Close Warp Point"},
    {17, "Create Planet"},
    {18, "Destroy Planet"},
    {19, "Create Star"},
    {20, "Destroy Star"},
    {21, "Create Storm"},
    {22, "Destroy Storm"},
    {23, "Space Yard Ship"},
    {24, "Mine"},
    {25, "Satellite"},
    {26, "Weapon Platform"},
    {27, "Troop"},
    {28, "Fighter"},
    {29, "Create Black Hole"},
    {30, "Destroy Black Hole"},
    {31, "Create Nebulae"},
    {32, "Destroy Nebulae"},
    {33, "Satellite Layer"},
    {34, "Kamikaze Attack Ship"},
    {35, "Recon Satellite"},
    {36, "Cargo Transport"},
    {37, "Anti-Planet Drone"},
    {38, "Anti-Ship Drone"},
    {39, "Drone Carrier"},
}};

} // namespace

std::optional<uint16_t> designTypeCode(std::string_view designType) {
    for (const TypeCode& t : kTypeCodes)
        if (t.name == designType) return t.code;
    return std::nullopt;
}

std::string_view designTypeName(uint16_t code) {
    for (const TypeCode& t : kTypeCodes)
        if (t.code == code) return t.name;
    return {};
}

// ---- Reports ----------------------------------------------------------------------------------------------

void ConversionReport::note(std::string line) {
    if (std::find(notes.begin(), notes.end(), line) == notes.end()) notes.push_back(std::move(line));
}

void ConversionReport::detail(std::string line) { details.push_back(std::move(line)); }

} // namespace opense4::game::classic
