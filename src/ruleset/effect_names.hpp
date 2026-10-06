#pragma once

// The effect types intelligence projects and events name in their `Type`
// (docs/spec/05 §2.3, docs/spec/01 §10), shared by the loader (which reports
// event types it does not know) and the engine's effect enumeration
// (game/events.hpp).

// X(enumerator, "Type identifier in data")
#define OPENSE4_EFFECTS(X)                                                        \
    X(ShipDamage, "Ship - Damage")                                                \
    X(ShipLoseMovement, "Ship - Lose Movement")                                   \
    X(ShipLoseSupply, "Ship - Lose Supply")                                       \
    X(ShipRebel, "Ship - Rebel")                                                  \
    X(ShipExperienceChange, "Ship - Experience Change")                           \
    X(ShipCargoDamage, "Ship - Cargo Damage")                                     \
    X(ShipOrdersChange, "Ship - Orders Change")                                   \
    X(ShipMoved, "Ship - Moved")                                                  \
    X(ShipLocations, "Ship - Locations")                                          \
    X(ShipConcentrations, "Ship - Concentrations")                                \
    X(ShipConstructionInfo, "Ship - Construction Info")                           \
    X(ShipDesignsSteal, "Ship Designs - Steal")                                   \
    X(UnitDesignsSteal, "Unit Designs - Steal")                                   \
    X(PlanetConditionsChange, "Planet - Conditions Change")                       \
    X(PlanetValueChange, "Planet - Value Change")                                 \
    X(PlanetPopulationChange, "Planet - Population Change")                       \
    X(PlanetPopulationAngerChange, "Planet - Population Anger Change")            \
    X(PlanetPopulationRiot, "Planet - Population Riot")                           \
    X(PlanetPopulationRebel, "Planet - Population Rebel")                         \
    X(PlanetCargoDamage, "Planet - Cargo Damage")                                 \
    X(PlanetFacilityDamage, "Planet - Facility Damage")                           \
    X(PlanetInfo, "Planet - Info")                                                \
    X(PlanetLocations, "Planet - Locations")                                      \
    X(PlanetPlague, "Planet - Plague")                                            \
    X(PlanetPlagueCured, "Planet - Plague Cured")                                 \
    X(PlanetCreated, "Planet - Created")                                          \
    X(PlanetDestroyed, "Planet - Destroyed")                                      \
    X(StarCreated, "Star - Created")                                              \
    X(StarDestroyed, "Star - Destroyed")                                          \
    X(WarpPointOpened, "Warp Point - Opened")                                     \
    X(WarpPointClosed, "Warp Point - Closed")                                     \
    X(PointsChange, "Points - Change")                                            \
    X(PointsSteal, "Points - Steal")                                              \
    X(ResearchSteal, "Research - Steal")                                          \
    X(ResearchDeleteProject, "Research - Delete Project")                         \
    X(IntelDeleteProject, "Intel - Delete Project")                               \
    X(PoliticsDisruptTrade, "Politics - Disrupt Trade")                           \
    X(PoliticsInterceptMessages, "Politics - Intercept Messages")                 \
    X(PoliticsFakeMessages, "Politics - Fake Messages")                           \
    X(PoliticsPreventMessages, "Politics - Prevent Messages")                     \
    X(PoliticsTreatyInfo, "Politics - Treaty Info")                               \
    X(SystemInfo, "System - Info")                                                \
    X(EmpireInfo, "Empire - Info")                                                \
    X(TechLevelInfo, "Tech Level - Info")                                         \
    X(IntelligenceDefense, "Intelligence Defense")

#include <array>
#include <string_view>

namespace opense4::ruleset {

// The identifiers, in the order of OPENSE4_EFFECTS.
inline constexpr std::array kEffectTypes{
#define OPENSE4_EFFECT_TEXT(name, text) std::string_view{text},
    OPENSE4_EFFECTS(OPENSE4_EFFECT_TEXT)
#undef OPENSE4_EFFECT_TEXT
};

} // namespace opense4::ruleset
