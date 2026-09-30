#pragma once

// The ability type names a data set may use (docs/spec/03 §3.3), shared by the
// loader (which reports names it does not know) and the engine's ability
// enumeration (game/abilities.hpp).

#include <string_view>

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
    X(RandomAbility, "Random")                                                                  \
    X(WarpPointUnstable, "Warp Point - Unstable")                                               \
    X(WarpPointPeriodic, "Warp Point - Periodic")                                               \
    X(WarpPointAbilityRequired, "Warp Point - Ability Required")                                \
    X(SectorAbilityRequired, "Sector - Ability Required")                                       \
    X(ResupplyPod, "Resupply Pod")                                                              \
    X(MaximumPopulation, "Maximum Population")                                                  \
    X(AITag, "AI Tag")

namespace opense4::ruleset {

enum class AbilityNameStatus : unsigned char {
    Known,    // a type the original accepts
    Ignored,  // listed in some file headers but not a type the original knows: loaded with a warning, no effect
    Unknown,  // a data error (spec 03 §2.1)
};

// "AI Tag 01" .. "AI Tag 20" are known; "None" and blanks are placeholders the
// loader drops before asking.
AbilityNameStatus abilityNameStatus(std::string_view type);

// At most this many abilities are read per record (spec 03 §2.1).
inline constexpr int kMaxAbilitiesPerRecord = 20;

} // namespace opense4::ruleset
