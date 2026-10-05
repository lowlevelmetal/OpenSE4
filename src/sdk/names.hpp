#pragma once

// The names scripts use for the engine's enumerations and command kinds
// (docs/sdk/commands.md, docs/sdk/view.md): lower_snake_case words, one per
// value, in the enumeration's own order. They are part of the SDK's interface
// (api = 1): a value keeps its name once published.

#include "game/abilities.hpp"
#include "game/commands.hpp"
#include "game/economy.hpp"
#include "game/tactical.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace opense4::sdk {

// Specialised for every enumeration scripts see: `kWhat` says what a value is
// (for error messages), `kNames` holds the names in value order.
template <class E>
struct EnumNames;

template <class E>
concept NamedEnum = std::is_enum_v<E> && requires {
    EnumNames<E>::kWhat;
    EnumNames<E>::kNames;
};

// The value's name; empty for a value outside the list.
template <NamedEnum E>
constexpr std::string_view enumName(E e) {
    const auto i = static_cast<size_t>(e);
    return i < EnumNames<E>::kNames.size() ? EnumNames<E>::kNames[i] : std::string_view{};
}
// The value with that name (exactly, case included).
template <NamedEnum E>
constexpr std::optional<E> parseEnum(std::string_view name) {
    for (size_t i = 0; i < EnumNames<E>::kNames.size(); ++i)
        if (EnumNames<E>::kNames[i] == name) return static_cast<E>(i);
    return std::nullopt;
}
template <NamedEnum E>
constexpr std::span<const std::string_view> enumNames() {
    return EnumNames<E>::kNames;
}

#define OPENSE4_SDK_ENUM(Type, what, ...)                                                                                            \
    template <>                                                                                                                      \
    struct EnumNames<Type> {                                                                                                         \
        static constexpr std::string_view kWhat = what;                                                                              \
        static constexpr auto kNames = std::to_array<std::string_view>({__VA_ARGS__});                                               \
    }

OPENSE4_SDK_ENUM(game::OrderKind, "order kind", "move_to", "warp", "attack", "resupply", "repair", "explore", "colonize", "sentry",
                 "load_cargo", "drop_cargo", "launch_units", "recover_units", "cloak", "decloak", "sweep_mines", "use_component",
                 "stellar_manipulation", "move_to_waypoint", "self_destruct", "use_facility", "convert_resources", "scrap", "analyze",
                 "mothball", "unmothball", "retrofit", "fire_on", "seek", "join_fleet");
OPENSE4_SDK_ENUM(game::StellarAction, "stellar manipulation", "create_planet", "destroy_planet", "create_star", "destroy_star",
                 "open_warp_point", "close_warp_point", "create_storm", "destroy_storm", "create_nebulae", "destroy_nebulae",
                 "create_black_hole", "destroy_black_hole", "create_constructed_planet");
OPENSE4_SDK_ENUM(game::Resource, "resource", "minerals", "organics", "radioactives");
OPENSE4_SDK_ENUM(game::Treaty, "treaty", "war", "non_intercourse", "none", "non_aggression", "subjugation", "protectorate",
                 "trade_alliance", "trade_research_alliance", "military_alliance", "partnership");
OPENSE4_SDK_ENUM(game::MessageType, "message type", "general", "propose_treaty", "accept_treaty", "refuse_treaty", "counter_treaty",
                 "break_treaty", "declare_war", "propose_trade", "accept_trade", "refuse_trade", "counter_trade", "gift", "tribute",
                 "accept_gift", "refuse_gift", "surrender", "grant_independence", "demand_gift", "demand_tribute", "demand_surrender",
                 "demand_remove_ships", "demand_remove_colonies", "demand_leave_planet", "request_stop_hostilities",
                 "request_break_treaty", "request_declare_war", "request_make_peace", "request_support", "request_attack_empire",
                 "request_attack_planet", "demand_stop_espionage", "demand_stop_sabotage", "demand_stop_attacks", "accept_demand",
                 "refuse_demand");
OPENSE4_SDK_ENUM(game::PackageItem::Kind, "package item kind", "resources", "technology", "planet", "vehicle", "star_chart",
                 "treaty", "comm_channel", "system");
OPENSE4_SDK_ENUM(game::QueueItem::Kind, "queue item kind", "vehicle", "facility", "upgrade");
OPENSE4_SDK_ENUM(game::cmd::DemandList, "demand list", "war", "break", "peace");
OPENSE4_SDK_ENUM(game::EncounterClear, "encounter setting", "never", "enemy", "any");
OPENSE4_SDK_ENUM(game::Minister, "minister", "design", "ship_construction", "expenses", "production_output", "research",
                 "intelligence", "politics", "repair", "resupply", "scrap", "retrofit", "facility_construction", "transports",
                 "carriers", "colonization", "attack", "defense", "exploration", "patrol", "mines_satellites_drones", "fleets",
                 "stellar_manipulation", "ship_cloaking", "space_yard_ships", "troops");
OPENSE4_SDK_ENUM(game::combat::TacticalOrder::Kind, "tactical order kind", "move", "fire", "toggle_weapon", "launch",
                 "launch_fighters", "drop_troops", "ram", "capture", "set_leader", "set_member", "clear_group", "clear_all_groups",
                 "auto", "auto_phase", "end_phase", "resolve_combat");
OPENSE4_SDK_ENUM(game::VehicleStatus, "vehicle status", "normal", "mothballed", "cloaked");
OPENSE4_SDK_ENUM(game::ObjectKind, "object kind", "star", "planet", "asteroids", "storm", "warp_point", "destroyed_star", "comet");
OPENSE4_SDK_ENUM(game::PlayerKind, "player kind", "human", "computer", "neutral");
OPENSE4_SDK_ENUM(game::LogCategory, "log category", "construction", "research", "intelligence", "events", "politics", "combat", "misc");
OPENSE4_SDK_ENUM(game::LogGoto, "log target", "none", "location", "research", "intelligence", "empires", "construction_queues",
                 "empire_options", "designs");
OPENSE4_SDK_ENUM(game::Mood, "mood", "jubilant", "happy", "indifferent", "unhappy", "angry", "rioting");
OPENSE4_SDK_ENUM(ruleset::VehicleType, "vehicle type", "ship", "base", "fighter", "satellite", "mine", "troop", "drone",
                 "weapon_platform");
OPENSE4_SDK_ENUM(ruleset::WeaponKind, "weapon kind", "none", "direct_fire", "seeking", "warhead", "point_defense");
OPENSE4_SDK_ENUM(game::Characteristic, "characteristic", "physical_strength", "intelligence", "cunning", "environmental_resistance",
                 "reproduction", "happiness", "aggressiveness", "defensiveness", "political_savvy", "mining_aptitude",
                 "farming_aptitude", "refining_aptitude", "construction_aptitude", "repair_aptitude", "maintenance_aptitude");
OPENSE4_SDK_ENUM(game::SightType, "sight type", "em_active", "em_passive", "psychic", "gravitic", "temporal");
OPENSE4_SDK_ENUM(game::Aggregation, "aggregation", "sum", "largest", "smallest", "count", "present", "per_sight_type", "first_per_id",
                 "per_family", "unspecified");
OPENSE4_SDK_ENUM(game::economy::ConditionsBand, "conditions band", "optimal", "good", "mild", "unpleasant", "harsh", "deadly");
OPENSE4_SDK_ENUM(game::CombatPiece::Kind, "battle piece kind", "vehicle", "planet", "unit_group", "seeker", "obstacle");

#undef OPENSE4_SDK_ENUM

// ---- Command kinds ---------------------------------------------------------------------------------

inline constexpr size_t kCommandKinds = std::variant_size_v<game::Command>;
// The `kind` of each cmd::Command alternative, in variant order:
// snakeCase(commandName) of the alternative ("QueueAdd" is "queue_add").
std::span<const std::string_view> commandKindNames();
std::string_view commandKindName(const game::Command& c);
// The variant index of the command kind with that name.
std::optional<size_t> commandKindIndex(std::string_view name);

// "QueueAdd" -> "queue_add": a capital letter after the first starts a new word.
std::string snakeCase(std::string_view camel);

} // namespace opense4::sdk
