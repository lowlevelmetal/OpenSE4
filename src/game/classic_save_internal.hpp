#pragma once

// Tables shared by the import and the export of the original's saved games
// (docs/spec/08 §3, §6): the built-in numbers of the format and their
// OpenSE4 counterparts.

#include "datafile/datafile.hpp"
#include "game/classic_save.hpp"
#include "game/combat.hpp"
#include "game/events.hpp"
#include "game/rules.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace opense4::game::classic::detail {

using combat::TargetCategory;
using combat::kTargetCategories;
using combat::identifier;

// §3.6.1: native atmosphere 1..5 and surface 1..3.
inline constexpr std::array<std::string_view, 5> kAtmospheres{"None", "Methane", "Oxygen", "Hydrogen", "Carbon Dioxide"};
inline constexpr std::array<std::string_view, 3> kSurfaces{"Rock", "Ice", "Gas Giant"};

// §3.5: physical type 1..3.
inline constexpr std::array<std::string_view, 3> kPhysicalTypes{"Normal", "Nebulae", "Black Hole"};

// §3.8.6: OpenSE4's headings 0..7 clockwise from north, as the file's codes
// (0 N, 1 E, 2 S, 3 W, 4 NE, 5 NW, 6 SE, 7 SW).
inline constexpr std::array<uint8_t, 8> kHeadingCode{0, 4, 1, 6, 2, 7, 3, 5};
inline uint8_t headingFromCode(uint8_t code) {
    for (uint8_t h = 0; h < kHeadingCode.size(); ++h)
        if (kHeadingCode[h] == code) return h;
    return 0;
}

// §3.6.4: treaty codes 1..11 (3 is "no contact").
inline constexpr uint8_t kNoContact = 3;
inline std::optional<Treaty> treatyFromCode(uint8_t code) {
    switch (code) {
        case 1: return Treaty::War;
        case 2: return Treaty::NonIntercourse;
        case 4: return Treaty::None;
        case 5: return Treaty::NonAggression;
        case 6: return Treaty::Subjugation;
        case 7: return Treaty::Protectorate;
        case 8: return Treaty::TradeAlliance;
        case 9: return Treaty::TradeResearchAlliance;
        case 10: return Treaty::MilitaryAlliance;
        case 11: return Treaty::Partnership;
        default: return std::nullopt;
    }
}
inline uint8_t treatyCode(Treaty t) {
    switch (t) {
        case Treaty::War: return 1;
        case Treaty::NonIntercourse: return 2;
        case Treaty::None: return 4;
        case Treaty::NonAggression: return 5;
        case Treaty::Subjugation: return 6;
        case Treaty::Protectorate: return 7;
        case Treaty::TradeAlliance: return 8;
        case Treaty::TradeResearchAlliance: return 9;
        case Treaty::MilitaryAlliance: return 10;
        case Treaty::Partnership: return 11;
        case Treaty::Count: break;
    }
    return 4;
}

// §3.6.11: message types 1..38, in the order of spec 05 §7.3's anger file
// (observed in the sample saves: 20 is a surrender demand, 38 a refused
// demand). 16 and 17 (accept and refuse a tribute) and 34 (the generic
// demand) have no OpenSE4 type.
inline constexpr uint8_t kMessageTypes = 38;
inline std::optional<MessageType> messageTypeFromCode(uint8_t code) {
    using M = MessageType;
    static constexpr std::array<std::optional<M>, kMessageTypes + 1> kTable{
        std::nullopt, M::General, M::ProposeTreaty, M::AcceptTreaty, M::RefuseTreaty, M::CounterTreaty, M::BreakTreaty, M::DeclareWar,
        M::ProposeTrade, M::AcceptTrade, M::RefuseTrade, M::CounterTrade, M::Gift, M::AcceptGift, M::RefuseGift, M::Tribute,
        std::nullopt, std::nullopt, M::DemandGift, M::DemandTribute, M::DemandSurrender, M::DemandRemoveShips, M::DemandRemoveColonies,
        M::DemandLeavePlanet, M::RequestStopHostilities, M::RequestBreakTreaty, M::RequestDeclareWar, M::RequestMakePeace,
        M::RequestSupport, M::RequestAttackEmpire, M::RequestAttackPlanet, M::DemandStopEspionage, M::DemandStopSabotage,
        M::DemandStopAttacks, std::nullopt, M::Surrender, M::GrantIndependence, M::AcceptDemand, M::RefuseDemand,
    };
    return code <= kMessageTypes ? kTable[code] : std::nullopt;
}
inline uint8_t messageTypeCode(MessageType t) {
    for (uint8_t c = 1; c <= kMessageTypes; ++c)
        if (messageTypeFromCode(c) == t) return c;
    return 1;
}

// §3.6.13: target categories in the file's order, as the strategy keys name them.
inline constexpr std::array<TargetCategory, kStrategyCategories> kStrategyCategoryOrder{
    TargetCategory::Bases,          TargetCategory::Ships,       TargetCategory::Carriers,         TargetCategory::ColonyShips,
    TargetCategory::Fighters,       TargetCategory::Satellites,  TargetCategory::Mines,            TargetCategory::Planets,
    TargetCategory::Transports,     TargetCategory::BasesNoWeapons, TargetCategory::ShipsNoWeapons, TargetCategory::SeekersOnUs,
    TargetCategory::SeekersOnOthers, TargetCategory::Drones,
};
// The movement strategy codes as the data files write them (code: index + 1).
inline constexpr std::array<std::string_view, 8> kMovementNames{
    "Don't Get Hurt",              // 1
    "Drop Troops (if carrying)",   // 2
    "Maximum Weapons Range",       // 3
    "Optimal Weapons Range",       // 4
    "Short Weapons Range",         // 5
    "Point Blank",                 // 6
    "Board Enemy Ships",           // 7
    "Ram",                         // 8
};
// The target choice codes (code: index; 0 none).
inline constexpr std::array<std::string_view, 13> kTargetingNames{
    "None",                    // 0
    "Nearest",                 // 1
    "Farthest",                // 2
    "Largest",                 // 3
    "Smallest",                // 4
    "Most Damaged",            // 5
    "Least Damaged",           // 6
    "Fastest",                 // 7
    "Slowest",                 // 8
    "Strongest",               // 9
    "Weakest",                 // 10
    "Has Weapons",             // 11
    "Does Not Have Weapons",   // 12
};

// §3.8.8 and §3.9: cargo kinds of Load and Drop (1 population, 2 troops, 3
// fighters, 4 mines, 5 satellites, 6 drones, 7 weapon platforms) and the unit
// kinds of the launch list (3 fighters, 4 satellites, 5 mines, 7 drones: the
// vehicle type numbers).
inline std::optional<ruleset::VehicleType> cargoKindType(uint8_t kind) {
    using V = ruleset::VehicleType;
    switch (kind) {
        case 2: return V::Troop;
        case 3: return V::Fighter;
        case 4: return V::Mine;
        case 5: return V::Satellite;
        case 6: return V::Drone;
        case 7: return V::WeaponPlatform;
        default: return std::nullopt;
    }
}
inline uint8_t cargoKindOf(ruleset::VehicleType t) {
    using V = ruleset::VehicleType;
    switch (t) {
        case V::Troop: return 2;
        case V::Fighter: return 3;
        case V::Mine: return 4;
        case V::Satellite: return 5;
        case V::Drone: return 6;
        case V::WeaponPlatform: return 7;
        default: return 1;
    }
}
inline std::optional<ruleset::VehicleType> unitKindType(uint8_t kind) {
    if (kind < 1 || kind > static_cast<uint8_t>(ruleset::VehicleType::Count)) return std::nullopt;
    return static_cast<ruleset::VehicleType>(kind - 1);
}
inline uint8_t unitKindOf(ruleset::VehicleType t) { return static_cast<uint8_t>(static_cast<uint8_t>(t) + 1); }

// What the target word of a timed event names (§3.4), by its effect.
enum class EventTarget : uint8_t { Vehicle, Object, System, Empire };
inline EventTarget eventTarget(const Rules& r, uint32_t eventType) {
    if (eventType >= r.data().eventTypes.size()) return EventTarget::Empire;
    const std::string_view type = r.data().eventTypes[eventType].type;
    auto starts = [&](std::string_view p) { return type.size() >= p.size() && datafile::keysEqual(type.substr(0, p.size()), p); };
    if (starts("Ship")) return EventTarget::Vehicle;
    if (starts("Planet") || starts("Star") || starts("Warp Point")) return EventTarget::Object;
    if (starts("System")) return EventTarget::System;
    return EventTarget::Empire;
}

// What an intelligence project's specific target names (§3.6.3), by its effect.
enum class IntelTarget : uint8_t { None, Vehicle, Planet, Tech, Empire, System };
inline IntelTarget intelTarget(const Rules& r, uint32_t project) {
    if (project >= r.data().intelProjects.size()) return IntelTarget::None;
    const auto effect = effects::parseEffect(r.data().intelProjects[project].type);
    if (!effect) return IntelTarget::None;
    const std::string_view id = effects::identifier(*effect);
    auto starts = [&](std::string_view p) { return id.starts_with(p); };
    if (*effect == effects::Effect::ResearchSteal) return IntelTarget::Tech;
    if (starts("Ship -")) return IntelTarget::Vehicle;
    if (starts("Planet -")) return IntelTarget::Planet;
    if (starts("Politics -")) return IntelTarget::Empire;
    if (starts("System -")) return IntelTarget::System;
    return IntelTarget::None;
}

// Messages the recipient may still answer (diplomacy: the rest are answered on delivery).
inline bool answerableMessage(MessageType t) {
    switch (t) {
        case MessageType::ProposeTreaty:
        case MessageType::CounterTreaty:
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade:
        case MessageType::Gift:
        case MessageType::Tribute: return true;
        default: return t >= MessageType::DemandGift && t <= MessageType::DemandStopAttacks;
    }
}

inline std::string trimmed(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return std::string(s.substr(a, b - a));
}

} // namespace opense4::game::classic::detail
