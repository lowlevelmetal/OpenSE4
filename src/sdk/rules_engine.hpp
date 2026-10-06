#pragma once

// Internal to the SDK: the rules hooks of one engine call (docs/sdk/rules.md),
// behind game::RulesHooks. The session of the call (sdk/players.cpp) owns it
// and lends it its interpreter: the computer players and the rules scripts
// of a game share one interpreter, on the session's thread.
//
// The engine asks wants() before each hook; the first time, the rules
// scripts are loaded (their modules imported, so that they register their
// functions) and what they registered is kept per process, keyed by the mods'
// identities, so later calls know it without starting an interpreter.

#include "game/hooks.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"
#include "game/turn.hpp"
#include "mods/package.hpp"
#include "script/runtime.hpp"
#include "script/value.hpp"
#include "sdk/view.hpp"

#include <cstdint>
#include <deque>
#include <expected>
#include <span>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::sdk {
struct Scenario;
}

namespace opense4::sdk::detail {

// Failures of one mod's rules in a game turn after which they are off for
// the rest of it (docs/sdk/rules.md "Budgets and failures").
inline constexpr int kRulesFailuresPerTurn = 3;

// What the session lends the rules: its interpreter, started on demand.
class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    // Starts the session's interpreter (with the rules' files and native
    // functions) if it is not running; false when it cannot.
    virtual bool startScripts() = 0;
    virtual const std::string& scriptsError() const = 0;
    // Calls `module.function(arg)` on the session's thread with at most `budget`.
    virtual script::Result<script::Value> runScript(std::string_view module, std::string_view function, script::Value arg, int64_t budget) = 0;
    // What the last call used, its native work included.
    virtual int64_t lastCallBudget() const = 0;
    // Counts engine work against the running call.
    virtual void chargeScript(int64_t units) = 0;
    // A script call is running (an engine function inside it may not start another).
    virtual bool inScriptCall() const = 0;
};

// What one rules mod registered (its functions, by kind and name).
struct Registration {
    std::string kind;    // "hook", "order", "order_check", "event", "intel_project", "victory", "objective"
    std::string name;    // the hook's, order's, event's... name
    std::string step;    // empire_end_of_turn: the step, or empty for every one
    std::string when;    // empire_end_of_turn: "before", "after", or empty for both
};

struct ModRegistrations {
    std::string mod;
    std::vector<Registration> registered;
    std::string error;   // why its scripts did not load (empty: they did)
};

class RulesEngine final : public game::RulesHooks {
public:
    RulesEngine(const game::Rules& r, game::GameState& s, std::vector<const mods::Package*> mods, bool ownPackage, ScriptHost& host);
    ~RulesEngine() override;

    // The mods whose rules run, in load order.
    const std::vector<const mods::Package*>& mods() const { return mods_; }
    // Adds the rules' native functions (module _opense4_rules) to the session's interpreter.
    std::string addNatives(script::Interpreter& interp);

    // game::RulesHooks
    bool wants(game::Hook h, const game::HookArgs* args = nullptr) const override;
    void run(game::TurnContext& ctx, game::Hook h, const game::HookArgs& args) override;
    void deliver(game::TurnContext& ctx) override;
    void eventStep(game::TurnContext& ctx, Rng& rng) override;
    std::optional<bool> intelProject(game::TurnContext& ctx, game::EmpireId source, const game::IntelProjectOrder& order) override;

    // A mod's order (cmd::ModCommand): checked against its declaration, then
    // its check and effect run.
    game::CommandResult modCommand(game::TurnContext& ctx, game::EmpireId empire, const game::cmd::ModCommand& c);

    // A mod's event fired now (fx.fire_event, the event step): noted for delivery.
    struct Fired {
        size_t mod = 0;
        std::string event;
        script::Value target;   // {kind, id} or null
    };

private:
    struct Pending {
        game::Hook hook;
        script::Value args;
        std::optional<Fired> fired;   // a mod's event to fire (not a hook)
    };
    // The call being handled: what the native functions work on.
    struct Active {
        game::TurnContext* ctx = nullptr;
        size_t mod = 0;
        std::string call;      // "hook", "order", ...
        std::string name;      // the hook's (or order's, event's...) name
    };

    void ensureLoaded() const;
    void fillOptions(game::GameState& s);
    std::optional<int64_t> optionValue(size_t mod, std::string_view name) const;
    bool switchedOn(size_t mod, const std::string& option) const;
    void endGame(game::TurnContext& ctx, game::EmpireId winner, const std::string& reason);
    bool registered(size_t mod, std::string_view kind, std::string_view name, const game::HookArgs* args = nullptr) const;
    // One call of one mod's function; nullopt when it failed or did not run.
    std::optional<script::Value> callMod(game::TurnContext& ctx, size_t mod, std::string_view call, std::string_view name, script::Value args,
                                         std::string_view failureKey, int64_t extraBudget = -1);
    void runEverywhere(game::TurnContext& ctx, game::Hook h, const game::HookArgs& args);
    script::Value hookArgs(game::TurnContext& ctx, game::Hook h, const game::HookArgs& args);
    void fireModEvent(game::TurnContext& ctx, const Fired& f);
    void checkVictories(game::TurnContext& ctx);
    void checkObjectives(game::TurnContext& ctx);
    game::ModRulesState& stateOf(game::GameState& s, size_t mod);
    void fail(game::TurnContext& ctx, size_t mod, std::string_view key, const std::string& what, const std::string& traceback = {});
    script::Value native(std::string_view name, const script::Value& arg);
    script::Value read(const script::Value& arg);
    void storeModData(game::GameState& s, size_t mod, const script::Value& entries, std::string& problem);

    friend struct EffectRunner;

    const game::Rules& rules_;
    game::GameState& state_;
    std::vector<const mods::Package*> mods_;
    bool ownPackage_;
    ScriptHost& host_;
    mutable std::optional<std::vector<ModRegistrations>> loaded_;
    mutable bool loading_ = false;
    bool interpLoaded_ = false;   // the scripts are imported in the session's interpreter
    std::shared_ptr<const Scenario> scenario_;
    std::deque<Pending> pending_;
    Active* active_ = nullptr;
    std::optional<Perspective> perspective_;   // the whole game, for reading
    script::Value rulesView_;
};

// The effects API (docs/sdk/rules.md "Effects"; sdk/rules_effects.cpp): one
// effect of the call being handled, or a NativeError the script sees.
struct EffectRunner {
    RulesEngine& engine;
    script::Value run(std::string_view name, const script::Value& args);
};

// One mod's data on a thing ("game", "empire", "colony" by planet,
// "vehicle"): its value, an empty map when it has none, null when there is
// no such thing.
script::Value modDataValue(const game::GameState& s, std::string_view mod, std::string_view kind, int64_t id);
// Keeps `value` as the mod's data on the thing (an empty map or null drops
// it); the reason when it cannot (no such thing, not plain values, too large).
std::string setModData(game::GameState& s, std::string_view mod, std::string_view kind, int64_t id, const script::Value& value);

// The rules mods of a game: the packages (the data set's, then `extra`) the
// game's mod set names (GameState::mods), whose rules scripts are there
// (scripts/), in load order.
std::vector<const mods::Package*> rulesModsOf(const game::Rules& r, const game::GameState& s, std::span<const mods::Package> extra);

} // namespace opense4::sdk::detail
