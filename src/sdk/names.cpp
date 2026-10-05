#include "sdk/names.hpp"

namespace opense4::sdk {

namespace {

template <class E>
constexpr bool coversCount() {
    return EnumNames<E>::kNames.size() == static_cast<size_t>(E::Count);
}
static_assert(coversCount<game::OrderKind>());
static_assert(coversCount<game::StellarAction>());
static_assert(coversCount<game::Treaty>());
static_assert(coversCount<game::MessageType>());
static_assert(coversCount<game::Minister>());
static_assert(coversCount<game::ObjectKind>());
static_assert(coversCount<game::Characteristic>());
static_assert(coversCount<game::SightType>());
static_assert(coversCount<ruleset::VehicleType>());
static_assert(EnumNames<game::combat::TacticalOrder::Kind>::kNames.size() ==
              static_cast<size_t>(game::combat::TacticalOrder::Kind::ResolveCombat) + 1);
static_assert(EnumNames<game::LogGoto>::kNames.size() == static_cast<size_t>(game::LogGoto::Designs) + 1);
static_assert(EnumNames<game::LogCategory>::kNames.size() == static_cast<size_t>(game::LogCategory::Misc) + 1);
static_assert(EnumNames<game::PackageItem::Kind>::kNames.size() == static_cast<size_t>(game::PackageItem::Kind::System) + 1);
static_assert(EnumNames<game::Aggregation>::kNames.size() == static_cast<size_t>(game::Aggregation::Unspecified) + 1);
static_assert(EnumNames<game::CombatPiece::Kind>::kNames.size() == static_cast<size_t>(game::CombatPiece::Kind::Obstacle) + 1);
static_assert(EnumNames<ruleset::WeaponKind>::kNames.size() == static_cast<size_t>(ruleset::WeaponKind::PointDefense) + 1);

// In the order of the game::Command variant.
constexpr auto kCommandKindNames = std::to_array<std::string_view>({
    "set_orders", "create_fleet", "join_fleet", "leave_fleet", "disband_fleet", "set_fleet_options",
    "set_vehicle_strategy", "rename", "scrap", "mothball", "set_minister",
    "queue_add", "queue_remove", "queue_move", "queue_set_count", "queue_flags", "retrofit",
    "set_colony_type", "abandon_planet", "transfer_cargo",
    "create_design", "set_design_obsolete", "delete_design",
    "set_research", "set_intel",
    "send_message", "answer_message",
    "set_waypoint", "set_system_flags", "set_system_note", "tag_minefield", "set_strategy",
    "set_repair_priorities", "set_design_types", "set_colony_types", "set_empire_options",
    "set_ministers", "set_encounter_options", "enter_sector", "edit_design", "open_vehicle_report",
    "queue_replace_facility", "decide_war", "set_interface_options", "carry_out_demand", "use_demand_entry", "jettison_cargo",
    "cloak_colony", "analyze", "self_destruct", "fire_on", "set_email", "order_tagged", "set_fleet_leader",
});
static_assert(kCommandKindNames.size() == kCommandKinds, "name the new command kind here");

} // namespace

std::span<const std::string_view> commandKindNames() { return kCommandKindNames; }

std::string_view commandKindName(const game::Command& c) { return kCommandKindNames[c.index()]; }

std::optional<size_t> commandKindIndex(std::string_view name) {
    for (size_t i = 0; i < kCommandKindNames.size(); ++i)
        if (kCommandKindNames[i] == name) return i;
    return std::nullopt;
}

std::string snakeCase(std::string_view camel) {
    std::string out;
    out.reserve(camel.size() + 4);
    for (size_t i = 0; i < camel.size(); ++i) {
        const char c = camel[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0) out += '_';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace opense4::sdk
