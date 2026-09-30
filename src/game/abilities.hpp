#pragma once

// The ability system (docs/spec/03 §3). Data files name abilities by string;
// the engine works with this closed enumeration. Unknown identifiers (mods)
// are kept as AbilityKind::Unknown with their raw text.

#include "game/types.hpp"
#include "ruleset/ruleset.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {

// X(enumerator, "Identifier in data")
#define OPENSE4_ABILITIES(X)                                                                   \
    X(WarpPointTurbulence, "Warp Point - Turbulence")                                           \
    X(StarUnstable, "Star - Unstable")                                                          \
    X(SectorSightObscuration, "Sector - Sight Obscuration")                                     \
    X(SectorSensorInterference, "Sector - Sensor Interference")                                 \
    X(SectorShieldDisruption, "Sector - Shield Disruption")                                     \
    X(SectorDamage, "Sector - Damage")                                                          \
    X(SystemSensorInterference, "System - Sensor Interference")                                 \
    X(SystemDamage, "System - Damage")                                                          \
    X(SystemAbilityRequired, "System - Ability Required")                                       \
    X(SystemMovementTowardsCenter, "System - Movement Towards Center")                          \
    X(SystemMovementRandom, "System - Movement Random")                                         \
    X(SystemDestructiveCenter, "System - Destructive Center")                                   \
    X(AncientRuins, "Ancient Ruins")                                                            \
    X(AncientRuinsUnique, "Ancient Ruins Unique")                                               \
    X(ResourceGenMinerals, "Resource Generation - Minerals")                                    \
    X(ResourceGenOrganics, "Resource Generation - Organics")                                    \
    X(ResourceGenRadioactives, "Resource Generation - Radioactives")                            \
    X(PointGenResearch, "Point Generation - Research")                                          \
    X(PointGenIntelligence, "Point Generation - Intelligence")                                  \
    X(Spaceport, "Spaceport")                                                                   \
    X(Palace, "Palace")                                                                         \
    X(SupplyGeneration, "Supply Generation")                                                    \
    X(PlanetChangeMineralsValue, "Planet - Change Minerals Value")                              \
    X(PlanetChangeOrganicsValue, "Planet - Change Organics Value")                              \
    X(PlanetChangeRadioactivesValue, "Planet - Change Radioactives Value")                      \
    X(PlanetChangeConditions, "Planet - Change Conditions")                                     \
    X(PlanetChangePopulationHappiness, "Planet - Change Population Happiness")                  \
    X(PlanetChangeGroundDefense, "Planet - Change Ground Defense")                              \
    X(PlanetShieldGeneration, "Planet - Shield Generation")                                     \
    X(PlanetChangeAtmosphere, "Planet - Change Atmosphere")                                     \
    X(ShieldGeneration, "Shield Generation")                                                    \
    X(PhasedShieldGeneration, "Phased Shield Generation")                                       \
    X(ComponentRepair, "Component Repair")                                                      \
    X(CargoStorage, "Cargo Storage")                                                            \
    X(DropTroops, "Drop Troops")                                                                \
    X(LaunchRecoverFighters, "Launch/Recover Fighters")                                         \
    X(LaunchRecoverSatellites, "Launch/Recover Satellites")                                     \
    X(LayMines, "Lay Mines")                                                                    \
    X(LaunchDrones, "Launch Drones")                                                            \
    X(MultiplexTracking, "Multiplex Tracking")                                                  \
    X(CombatToHitOffensePlus, "Combat To Hit Offense Plus")                                     \
    X(CombatToHitDefensePlus, "Combat To Hit Defense Plus")                                     \
    X(CombatToHitOffenseMinus, "Combat To Hit Offense Minus")                                   \
    X(CombatToHitDefenseMinus, "Combat To Hit Defense Minus")                                   \
    X(MineSweeping, "Mine Sweeping")                                                            \
    X(MedicalBay, "Medical Bay")                                                                \
    X(MovementBonus, "Movement Bonus")                                                          \
    X(EmissiveArmor, "Emissive Armor")                                                          \
    X(ShieldRegeneration, "Shield Regeneration")                                                \
    X(MasterComputer, "Master Computer")                                                        \
    X(CloakLevel, "Cloak Level")                                                                \
    X(SensorLevel, "Sensor Level")                                                              \
    X(EmergencyResupply, "Emergency Resupply")                                                  \
    X(EmergencyEnergy, "Emergency Energy")                                                      \
    X(LongRangeScanner, "Long Range Scanner")                                                   \
    X(LongRangeScannerSystem, "Long Range Scanner - System")                                    \
    X(OpenWarpPointDistance, "Open Warp Point Distance")                                        \
    X(CloseWarpPoint, "Close Warp Point")                                                       \
    X(CreatePlanetSize, "Create Planet Size")                                                   \
    X(DestroyPlanetSize, "Destroy Planet Size")                                                 \
    X(CreateStar, "Create Star")                                                                \
    X(DestroyStar, "Destroy Star")                                                              \
    X(CreateStorm, "Create Storm")                                                              \
    X(DestroyStorm, "Destroy Storm")                                                            \
    X(CreateNebulae, "Create Nebulae")                                                          \
    X(DestroyNebulae, "Destroy Nebulae")                                                        \
    X(CreateBlackHole, "Create Black Hole")                                                     \
    X(DestroyBlackHole, "Destroy Black Hole")                                                   \
    X(CreateConstructedPlanet, "Create Constructed Planet")                                     \
    X(ConstructedPlanetRequirements, "Constructed Planet Requirements")                         \
    X(StopPlanetDestroyer, "Stop Planet Destroyer")                                             \
    X(StopStarDestroyer, "Stop Star Destroyer")                                                 \
    X(StopNebulaeCreator, "Stop Nebulae Creator")                                               \
    X(StopBlackHoleCreator, "Stop Black Hole Creator")                                          \
    X(StopOpenWarpPoint, "Stop Open Warp Point")                                                \
    X(StopCloseWarpPoint, "Stop Close Warp Point")                                              \
    X(BoardingAttack, "Boarding Attack")                                                        \
    X(BoardingDefense, "Boarding Defense")                                                      \
    X(StandardShipMovement, "Standard Ship Movement")                                           \
    X(ShipBridge, "Ship Bridge")                                                                \
    X(ShipAuxiliaryControl, "Ship Auxiliary Control")                                          \
    X(ShipLifeSupport, "Ship Life Support")                                                     \
    X(ShipCrewQuarters, "Ship Crew Quarters")                                                   \
    X(ScannerJammer, "Scanner Jammer")                                                          \
    X(QuantumReactor, "Quantum Reactor")                                                        \
    X(SupplyStorage, "Supply Storage")                                                          \
    X(SpaceYard, "Space Yard")                                                                  \
    X(ResourceStorageMinerals, "Resource Storage - Mineral")                                    \
    X(ResourceStorageOrganics, "Resource Storage - Organics")                                   \
    X(ResourceStorageRadioactives, "Resource Storage - Radioactives")                           \
    X(ResourceGenModPlanetMinerals, "Resource Gen Modifier Planet - Minerals")                  \
    X(ResourceGenModPlanetOrganics, "Resource Gen Modifier Planet - Organics")                  \
    X(ResourceGenModPlanetRadioactives, "Resource Gen Modifier Planet - Radioactives")          \
    X(ResourceGenModSystemMinerals, "Resource Gen Modifier System - Minerals")                  \
    X(ResourceGenModSystemOrganics, "Resource Gen Modifier System - Organics")                  \
    X(ResourceGenModSystemRadioactives, "Resource Gen Modifier System - Radioactives")          \
    X(PlanetPointGenModResearch, "Planet Point Generation Modifier - Research")                 \
    X(PlanetPointGenModIntelligence, "Planet Point Generation Modifier - Intelligence")         \
    X(SystemPointGenModResearch, "System Point Generation Modifier - Research")                 \
    X(SystemPointGenModIntelligence, "System Point Generation Modifier - Intelligence")         \
    X(CombatModifierSystem, "Combat Modifier - System")                                         \
    X(DamageModifierSystem, "Damage Modifier - System")                                         \
    X(PlanetValueChangeSystem, "Planet Value Change - System")                                  \
    X(PlanetConditionsChangeSystem, "Planet Conditions Change - System")                        \
    X(ChangeBadEventChanceSystem, "Change Bad Event Chance - System")                           \
    X(ChangeBadIntelChanceSystem, "Change Bad Intelligence Chance - System")                    \
    X(ChangePopulationHappinessSystem, "Change Population Happiness - System")                  \
    X(ShipTraining, "Ship Training")                                                            \
    X(FleetTraining, "Fleet Training")                                                          \
    X(ShipTrainingSystem, "Ship Training - System")                                             \
    X(FleetTrainingSystem, "Fleet Training - System")                                           \
    X(ModifyReproductionSystem, "Modify Reproduction - System")                                 \
    X(ChangePopulationSystem, "Change Population - System")                                     \
    X(PlaguePreventionSystem, "Plague Prevention - System")                                     \
    X(ResourceConversion, "Resource Conversion")                                                \
    X(ResourceReclamation, "Resource Reclamation")                                              \
    X(SelfDestruct, "Self-Destruct")                                                            \
    X(ColonizeRock, "Colonize Planet - Rock")                                                   \
    X(ColonizeIce, "Colonize Planet - Ice")                                                     \
    X(ColonizeGas, "Colonize Planet - Gas")                                                     \
    X(PointDefense, "Point-Defense")                                                            \
    X(Armor, "Armor")                                                                           \
    X(RemoteResourceGenMinerals, "Remote Resource Generation - Minerals")                       \
    X(RemoteResourceGenOrganics, "Remote Resource Generation - Organics")                       \
    X(RemoteResourceGenRadioactives, "Remote Resource Generation - Radioactives")               \
    X(ArmorRegeneration, "Armor Regeneration")                                                  \
    X(ShieldGenerationFromDamage, "Shield Generation From Damage")                              \
    X(ComponentDestroyedOnUse, "Component Destroyed On Use")                                    \
    X(CombatBestExperience, "Combat Best Experience")                                           \
    X(CombatMovement, "Combat Movement")                                                        \
    X(SolarSupplyGeneration, "Solar Supply Generation")                                         \
    X(ExtraMovementGeneration, "Extra Movement Generation")                                     \
    X(WeaponsAlwaysHit, "Weapons Always Hit")                                                   \
    X(ModifiedMaintenanceCost, "Modified Maintenance Cost")                                     \
    X(SolarResourceGenMinerals, "Solar Resource Generation - Minerals")                         \
    X(SolarResourceGenOrganics, "Solar Resource Generation - Organics")                         \
    X(SolarResourceGenRadioactives, "Solar Resource Generation - Radioactives")                 \
    X(ReducedMaintenanceSystem, "Reduced Maintenance Cost - System")                            \
    X(ShieldModifierSystem, "Shield Modifier - System")                                         \
    X(GeneratePointsMinerals, "Generate Points Minerals")                                       \
    X(GeneratePointsOrganics, "Generate Points Organics")                                       \
    X(GeneratePointsRadioactives, "Generate Points Radioactives")                               \
    X(GeneratePointsResearch, "Generate Points Research")                                       \
    X(GeneratePointsIntelligence, "Generate Points Intelligence")                               \
    X(AITag, "AI Tag")

enum class AbilityKind : uint16_t {
#define OPENSE4_ABILITY_ENUM(name, text) name,
    OPENSE4_ABILITIES(OPENSE4_ABILITY_ENUM)
#undef OPENSE4_ABILITY_ENUM
    Unknown,
    Count
};

std::string_view identifier(AbilityKind k);
// "AI Tag 07" maps to AITag; "None"/empty maps to nullopt.
std::optional<AbilityKind> parseAbilityKind(std::string_view text);

// An ability with its values pre-parsed.
struct ParsedAbility {
    AbilityKind kind = AbilityKind::Unknown;
    int64_t value1 = 0;      // numeric Val 1 (0 if blank or text)
    int64_t value2 = 0;
    std::string text1;       // raw Val 1 (sight-type names etc.)
    std::string raw;         // original identifier (for Unknown and AI tags)
};

ParsedAbility parseAbility(const ruleset::Ability& a);
std::vector<ParsedAbility> parseAbilities(std::span<const ruleset::Ability> list);

// Helpers over a list.
int64_t sumValue1(std::span<const ParsedAbility> list, AbilityKind k);
int64_t bestValue1(std::span<const ParsedAbility> list, AbilityKind k);  // max, 0 if absent
bool hasAbility(std::span<const ParsedAbility> list, AbilityKind k);

// Space Yard abilities encode the resource in Val 1 (1..3) and the rate in Val 2.
Resources spaceYardRates(std::span<const ParsedAbility> list);

} // namespace opense4::game
