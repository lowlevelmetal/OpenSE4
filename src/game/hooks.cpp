#include "game/hooks.hpp"

#include "game/turn.hpp"

#include <array>
#include <format>
#include <utility>

namespace opense4::game {

RulesHooks::~RulesHooks() = default;

namespace {

constexpr auto kHookNames = std::to_array<std::string_view>({
    "new_game", "generate_galaxy", "after_galaxy",
    "turn_start", "orders_applied",
    "movement_day", "vehicle_entered_sector", "before_battle", "after_battle", "vehicle_destroyed",
    "empire_end_of_turn", "colony_end_of_turn",
    "colony_founded", "vehicle_built", "tech_researched", "treaty_changed", "message_sent", "event_fired",
    "check_victory", "turn_end",
});
static_assert(kHookNames.size() == static_cast<size_t>(Hook::Count), "name the new hook here");

constexpr auto kStepNames = std::to_array<std::string_view>({
    "intelligence", "research", "income", "maintenance", "population", "happiness", "construction", "repair", "supply", "ground_combat",
});
static_assert(kStepNames.size() == static_cast<size_t>(EndStep::Count), "name the new step here");

ModCommandHandler& handler() {
    static ModCommandHandler h;
    return h;
}

} // namespace

std::string_view hookName(Hook h) { return h < Hook::Count ? kHookNames[static_cast<size_t>(h)] : std::string_view("?"); }

std::optional<Hook> hookByName(std::string_view name) {
    for (size_t i = 0; i < kHookNames.size(); ++i)
        if (kHookNames[i] == name) return static_cast<Hook>(i);
    return std::nullopt;
}

bool isEventHook(Hook h) {
    switch (h) {
        case Hook::VehicleEnteredSector:
        case Hook::VehicleDestroyed:
        case Hook::ColonyFounded:
        case Hook::VehicleBuilt:
        case Hook::TechResearched:
        case Hook::TreatyChanged:
        case Hook::MessageSent:
        case Hook::EventFired: return true;
        default: return false;
    }
}

std::string_view endStepName(EndStep s) { return s < EndStep::Count ? kStepNames[static_cast<size_t>(s)] : std::string_view("?"); }

std::optional<EndStep> endStepByName(std::string_view name) {
    for (size_t i = 0; i < kStepNames.size(); ++i)
        if (kStepNames[i] == name) return static_cast<EndStep>(i);
    return std::nullopt;
}

bool hookWanted(const TurnContext& ctx, Hook h, const HookArgs* args) { return ctx.hooks && ctx.hooks->wants(h, args); }

void runHook(TurnContext& ctx, Hook h, const HookArgs& args) {
    if (ctx.hooks && ctx.hooks->wants(h, &args)) ctx.hooks->run(ctx, h, args);
}

void deliverHooks(TurnContext& ctx) {
    if (ctx.hooks) ctx.hooks->deliver(ctx);
}

void noteVehicleLost(TurnContext& ctx, const Vehicle& v, std::string_view cause) {
    if (!ctx.hooks || v.count <= 0 || !ctx.hooks->wants(Hook::VehicleDestroyed)) return;
    HookArgs a;
    a.empire = v.owner;
    a.vehicle = v.id;
    a.where = v.location;
    a.text = std::string(cause);
    a.lost = &v;
    ctx.hooks->run(ctx, Hook::VehicleDestroyed, a);
}

void setModCommandHandler(ModCommandHandler h) { handler() = std::move(h); }

CommandResult applyModCommand(const Rules& r, GameState& s, EmpireId empire, const cmd::ModCommand& c) {
    const ModCommandHandler& h = handler();
    if (!h) return CommandResult::fail(std::format("The order {} of the mod {} needs the modding SDK, which this program does not have.", c.name, c.mod));
    return h(r, s, empire, c);
}

} // namespace opense4::game
