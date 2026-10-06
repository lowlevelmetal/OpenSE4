#pragma once

// The view a script of one empire sees (docs/sdk/view.md), as a script::Value
// tree: the game, the empire's own affairs, and the galaxy, the empires,
// colonies, vehicles, fleets, designs, messages, log and battles as that
// empire knows them.
//
// By default the view is built from game::redactForEmpire: exactly what a
// human player of that empire is sent in a network game. ViewOptions::whole
// builds it from the whole state instead (the game option "computer players
// see everything", docs/MODDING_SDK.md §6.2).
//
// Entities are lists, each element with its integer id (ObjectId, VehicleId,
// ...), so scripts build the indexes they want; references are those ids, or
// null for none; enumerations are lower_snake_case names (sdk/names.hpp).
// Every element of a kind has the same keys: what the empire does not know
// is null.
//
// Pure: building a view never changes the game.

#include "game/rules.hpp"
#include "game/state.hpp"
#include "script/value.hpp"

#include <memory>

namespace opense4::sdk {

// The SDK's interface version (docs/MODDING_SDK.md §14.5).
inline constexpr int kApiVersion = 1;

struct ViewOptions {
    // The whole state, not only what the empire knows.
    bool whole = false;
};

// What one empire's scripts may read during one engine call: the state as
// that empire knows it (a redacted copy, made once), or with `whole` the
// given state itself, which must then outlive the perspective. The view and
// the queries (sdk/queries.hpp) of one call share it.
class Perspective {
public:
    Perspective(const game::Rules& r, const game::GameState& s, game::EmpireId empire, ViewOptions options = {});

    const game::Rules& rules() const { return *rules_; }
    const game::GameState& state() const { return *state_; }
    game::EmpireId empire() const { return empire_; }
    const ViewOptions& options() const { return options_; }
    bool whole() const { return options_.whole; }
    // The empire's own record (null for an empire the game does not have).
    const game::Empire* me() const;

private:
    const game::Rules* rules_;
    std::shared_ptr<const game::GameState> redacted_;
    const game::GameState* state_;
    game::EmpireId empire_;
    ViewOptions options_;
};

script::Value buildView(const Perspective& p);
script::Value buildView(const game::Rules& r, const game::GameState& s, game::EmpireId empire, ViewOptions options = {});

// One record as the view would hold it, without building the rest: a colony
// (by its planet: `colony`) or a stellar object (`space_object`); null when
// the view would not hold it.
script::Value colonyRecord(const Perspective& p, game::ObjectId planet);
script::Value objectRecord(const Perspective& p, game::ObjectId object);

} // namespace opense4::sdk
