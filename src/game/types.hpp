#pragma once

// Basic value types of the classic-rules engine. See docs/spec/ for rules.

#include "core/id.hpp"
#include "game/xmath.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <string_view>

namespace opense4::ruleset {
struct Cost;
}

namespace opense4::game {

using SystemId = Id<struct SystemTag>;
using ObjectId = Id<struct ObjectTag>;  // any space object (star, planet, warp point, ...)
using EmpireId = Id<struct EmpireTag>;
using VehicleId = Id<struct VehicleTag>;
using FleetId = Id<struct FleetTag>;
using DesignId = Id<struct DesignTag>;
using MessageId = Id<struct MessageTag>;

// ---- Resources ----------------------------------------------------------------
enum class Resource : uint8_t { Minerals, Organics, Radioactives };
inline constexpr std::array<Resource, 3> kResources{Resource::Minerals, Resource::Organics, Resource::Radioactives};
std::string_view displayName(Resource r);

struct Resources {
    std::array<int64_t, 3> v{};

    constexpr Resources() = default;
    constexpr Resources(int64_t m, int64_t o, int64_t r) : v{m, o, r} {}
    static Resources from(const ruleset::Cost& c);

    constexpr int64_t& operator[](Resource r) { return v[static_cast<size_t>(r)]; }
    constexpr int64_t operator[](Resource r) const { return v[static_cast<size_t>(r)]; }
    constexpr Resources operator+(const Resources& o) const { return {v[0] + o.v[0], v[1] + o.v[1], v[2] + o.v[2]}; }
    constexpr Resources operator-(const Resources& o) const { return {v[0] - o.v[0], v[1] - o.v[1], v[2] - o.v[2]}; }
    constexpr Resources& operator+=(const Resources& o) { return *this = *this + o; }
    constexpr Resources& operator-=(const Resources& o) { return *this = *this - o; }
    // truncate(value × pct %) per resource, in the original's floating point
    // (xmath.hpp; spec 02 "Arithmetic").
    constexpr Resources percent(int64_t pct) const {
        return {xmath::pctTrunc(v[0], pct), xmath::pctTrunc(v[1], pct), xmath::pctTrunc(v[2], pct)};
    }
    // round(value × pct %) per resource, halves to even, in the same floating point.
    constexpr Resources percentRounded(int64_t pct) const {
        return {xmath::pctRound(v[0], pct), xmath::pctRound(v[1], pct), xmath::pctRound(v[2], pct)};
    }
    constexpr bool covers(const Resources& c) const { return v[0] >= c.v[0] && v[1] >= c.v[1] && v[2] >= c.v[2]; }
    constexpr bool isZero() const { return v[0] == 0 && v[1] == 0 && v[2] == 0; }
    constexpr bool anyNegative() const { return v[0] < 0 || v[1] < 0 || v[2] < 0; }
    constexpr int64_t total() const { return v[0] + v[1] + v[2]; }
    constexpr bool operator==(const Resources&) const = default;
};

constexpr Resources min(const Resources& a, const Resources& b) {
    return {a.v[0] < b.v[0] ? a.v[0] : b.v[0], a.v[1] < b.v[1] ? a.v[1] : b.v[1], a.v[2] < b.v[2] ? a.v[2] : b.v[2]};
}
constexpr Resources max(const Resources& a, const Resources& b) {
    return {a.v[0] > b.v[0] ? a.v[0] : b.v[0], a.v[1] > b.v[1] ? a.v[1] : b.v[1], a.v[2] > b.v[2] ? a.v[2] : b.v[2]};
}

// ---- Diplomacy ------------------------------------------------------------------
// Ordered from worst to best (spec 05 §3.2).
enum class Treaty : uint8_t {
    War,
    NonIntercourse,
    None,
    NonAggression,
    Subjugation,
    Protectorate,
    TradeAlliance,
    TradeResearchAlliance,
    MilitaryAlliance,
    Partnership,
    Count
};
std::string_view displayName(Treaty t);
// War, Non-Intercourse and None fight on contact; Non-Aggression and better do not.
constexpr bool treatyIsHostile(Treaty t) { return t == Treaty::War || t == Treaty::NonIntercourse || t == Treaty::None; }
constexpr bool treatyTradesResources(Treaty t) { return t >= Treaty::TradeAlliance; }
constexpr bool treatyTradesResearch(Treaty t) { return t >= Treaty::TradeResearchAlliance; }
constexpr bool treatyAllowsResupply(Treaty t) { return t >= Treaty::MilitaryAlliance; }
constexpr bool treatySharesSight(Treaty t) { return t == Treaty::Partnership; }

// ---- Population mood (spec 02 §1.8, §4) --------------------------------------------
// Anger is one whole percent per colony, 0 (calm) to 100 (confirmed: binary).
inline constexpr int kMaxAnger = 100;
inline constexpr int kCapitalMaxAnger = 80;   // capitals never riot (spec 02 §2)
inline constexpr int kNewColonyAnger = 25;    // a new colony starts Happy (spec 02 §2)
inline constexpr int kEmotionlessAnger = 35;  // any change to an Emotionless colony sets this (spec 02 §4)
enum class Mood : uint8_t { Jubilant, Happy, Indifferent, Unhappy, Angry, Rioting };
// The game's own band limits, not the Happiness.txt header's (confirmed: binary).
constexpr Mood moodFromAnger(int anger) {
    return anger >= 90 ? Mood::Rioting
           : anger >= 60 ? Mood::Angry
           : anger >= 45 ? Mood::Unhappy
           : anger >= 30 ? Mood::Indifferent
           : anger >= 15 ? Mood::Happy
                         : Mood::Jubilant;
}
std::string_view displayName(Mood m);

// ---- Sight (spec 01 §6) -------------------------------------------------------------
enum class SightType : uint8_t { EMActive, EMPassive, Psychic, Gravitic, Temporal, Count };
inline constexpr size_t kSightTypes = static_cast<size_t>(SightType::Count);
std::string_view displayName(SightType t);
bool parseSightType(std::string_view s, SightType& out);

// ---- Racial characteristics (spec 02 §8.1) ------------------------------------------
enum class Characteristic : uint8_t {
    PhysicalStrength,
    Intelligence,
    Cunning,
    EnvironmentalResistance,
    Reproduction,
    Happiness,
    Aggressiveness,
    Defensiveness,
    PoliticalSavvy,
    MiningAptitude,
    FarmingAptitude,
    RefiningAptitude,
    ConstructionAptitude,
    RepairAptitude,
    MaintenanceAptitude,
    Count
};
inline constexpr size_t kCharacteristics = static_cast<size_t>(Characteristic::Count);
std::string_view displayName(Characteristic c);  // as used in Settings keys: "Physical Strength"
bool parseCharacteristic(std::string_view s, Characteristic& out);

enum class PlayerKind : uint8_t { Human, Computer, Neutral };

// The 25 ministers in the original's order (spec 05 §7.1, confirmed: binary):
// the first 11 are global, the rest individual. Bit i of Empire::ministers.
enum class Minister : uint8_t {
    Design, ShipConstruction, Expenses, ProductionOutput, Research, Intelligence, Politics, Repair, Resupply, Scrap, Retrofit,
    FacilityConstruction, Transports, Carriers, Colonization, Attack, Defense, Exploration, Patrol, MinesSatellitesDrones, Fleets,
    StellarManipulation, ShipCloaking, SpaceYardShips, Troops,
    Count
};
inline constexpr size_t kMinisters = static_cast<size_t>(Minister::Count);
constexpr uint32_t ministerBit(Minister m) { return uint32_t{1} << static_cast<unsigned>(m); }
inline constexpr uint32_t kAllMinisters = (uint32_t{1} << kMinisters) - 1;
inline constexpr uint32_t kIndividualMinisters = kAllMinisters & ~(ministerBit(Minister::FacilityConstruction) - 1);
constexpr bool isGlobalMinister(Minister m) { return m < Minister::FacilityConstruction; }

// Computer Player Difficulty (spec 05 §7.1): stored per empire.
inline constexpr int kDifficultyLow = 0;
inline constexpr int kDifficultyMedium = 1;
inline constexpr int kDifficultyHigh = 2;

enum class LogCategory : uint8_t { Construction, Research, Intelligence, Events, Politics, Combat, Misc };
std::string_view displayName(LogCategory c);

} // namespace opense4::game
