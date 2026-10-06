#pragma once

// The rules tier of the modding SDK (docs/sdk/rules.md, docs/MODDING_SDK.md §7):
// what the rest of the program needs from it besides the hooks themselves,
// which run inside the computer players' sessions (sdk/players.hpp,
// installPlayers): the mods' orders as the client lists and gives them,
// their game options as the setup screens and files set them, the values of
// declared abilities, and the data generators.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"
#include "mods/generator.hpp"
#include "mods/manifest.hpp"
#include "mods/package.hpp"
#include "script/value.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::sdk {

// ---- Mod orders (cmd::ModCommand; docs/sdk/rules.md "Orders") ---------------------------------------

// What an order is given to: "self" (the empire itself), or a "vehicle",
// "fleet" or "colony" (by its planet) of the empire's, or another "empire",
// by id.
struct ModOrderTarget {
    std::string kind = "self";
    int64_t id = -1;
};

struct ModOrderChoice {
    std::string mod;
    mods::ModOrderDecl order;   // its name, label, description and arguments
};

// The mods' orders the empire may give that target: those of the game's
// rules mods that apply to its kind, when the target is the empire's (or, for
// "empire", another living empire). The mod's own check runs when the order
// is given (game::apply). The client offers them in the order strip's Mod
// Orders, panels' buttons and keys (docs/sdk/interface.md).
std::vector<ModOrderChoice> modOrders(const game::Rules& r, const game::GameState& s, game::EmpireId empire, const ModOrderTarget& target,
                                      std::span<const mods::Package> extra = {});
// The command giving it, with these arguments (a map; those left out take
// their defaults).
game::cmd::ModCommand modOrderCommand(const ModOrderChoice& choice, const ModOrderTarget& target, const script::Value& args = script::Value());

// A mod order's target and arguments checked against its declaration: the
// arguments as its functions see them (every declared one, defaults filled
// in), or why the order is refused.
std::expected<script::Value, std::string> modOrderArguments(const game::Rules& r, const game::GameState& s, game::EmpireId empire,
                                                            const mods::ModOrderDecl& decl, const game::cmd::ModCommand& c);

// ---- Mod options (docs/sdk/rules.md "Game options") ---------------------------------------------------

struct ModOptionChoice {
    std::string mod;
    mods::ModOptionDecl option;
    // "<mod id>:<name>", as setup files and the command line write it.
    std::string key() const;
};

// Every option the mods' rules declare, in load order and declaration order.
std::vector<ModOptionChoice> modOptions(std::span<const mods::Package> packages);
// ... those of the mods a data set was loaded with.
std::vector<ModOptionChoice> modOptions(const game::Rules& r);
// The option's value in a game's options: what is set, else its default.
int64_t modOptionValue(const game::GameOptions& o, const ModOptionChoice& c);
// Sets one, by "<mod id>:<name>" among `choices`; the reason when the
// option is unknown or the value out of its range (empty: set).
std::string setModOption(game::GameOptions& o, std::span<const ModOptionChoice> choices, std::string_view key, int64_t value);

// ---- Abilities (docs/sdk/rules.md "Abilities") ----------------------------------------------------------

// The value of an ability (a mod's declared one, or one of the game's own,
// combined as it combines) on a "vehicle" (its design), "design", "colony"
// (its facilities, by planet), "system", "component", "facility" or "hull";
// nullopt inside when the thing does not carry it; an error for a thing
// that is not there.
std::expected<std::optional<int64_t>, std::string> abilityValue(const game::Rules& r, const game::GameState& s, std::string_view kind, int64_t id,
                                                                std::string_view name);

// ---- Data generators (docs/sdk/rules.md "Data generators") ------------------------------------------------

// Runs data/*.py generators in the script runtime: each one's `generate()`
// returns a patch, as a .toml patch file holds it. installPlayers makes it
// the data loader's default (mods::setDefaultGeneratorRunner).
std::shared_ptr<mods::GeneratorRunner> scriptGenerators();

} // namespace opense4::sdk
