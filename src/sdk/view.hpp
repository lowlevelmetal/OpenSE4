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

#include <cstdint>
#include <memory>
#include <string_view>

namespace opense4::sdk {

// The SDK's interface version (docs/MODDING_SDK.md §14.5).
inline constexpr int kApiVersion = 1;

struct ViewOptions {
    // The whole state, not only what the empire knows.
    bool whole = false;
    // Rules scripts' reading (docs/sdk/rules.md): vehicles already destroyed
    // but not yet removed from the state are left out.
    bool liveOnly = false;
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

// One part of the view, as buildView makes it: "game", "my", "empires",
// "systems", "objects", "colonies", "vehicles", "fleets", "designs",
// "messages", "log" or "battles" (rules scripts read the game a part at a
// time). Null for an unknown part.
script::Value buildViewPart(const Perspective& p, std::string_view part);
// One record of the view: an "empire", "system", "object", "colony" (by its
// planet's id), "vehicle", "fleet", "design" or "message" by its id, as the
// view's list would hold it; null when the view does not list it.
script::Value buildViewRecord(const Perspective& p, std::string_view kind, int64_t id);
// A battle's record as the view's `battles` list holds it.
script::Value battleRecord(const game::CombatRecord& c);
// A vehicle's record as the view's `vehicles` list would hold it, for a
// vehicle that need not be in the state any more (one just destroyed).
script::Value buildVehicleRecord(const Perspective& p, const game::Vehicle& v);

} // namespace opense4::sdk
