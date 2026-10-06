#pragma once

// Script and external computer players (docs/sdk/ai-protocol.md): the SDK's
// session behind game::Players. One session serves one engine call (a
// simultaneous turn, a turn-based call); the game makes it through the
// factory installPlayers sets.
//
// A session asks nothing until a decision needs a player that has no answer
// in the journal. Then it starts a thread with a large stack (sdk/worker.hpp)
// and, on it, one script::Interpreter with the `opense4` package
// (python/opense4, built in) and the `ai/` files of every mod whose players
// the game uses, and sends each request through
// `opense4._engine.dispatch(request) -> response`. Several script empires
// share the interpreter; each has its own player object and memory. An
// external empire's requests go to its bot (ExternalBot) instead.
//
// Every request and its response go into the game's journal
// (GameState::journal); answers waiting in the journal are given again
// without asking (a call made again after a battle stop, a turn played
// again). See docs/sdk/ai-protocol.md for the requests, responses, services,
// budgets and failures.

#include "game/players.hpp"
#include "game/rules.hpp"
#include "game/setup.hpp"
#include "game/state.hpp"
#include "mods/package.hpp"
#include "script/runtime.hpp"
#include "script/value.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::mods {
struct ModSet;
}

namespace opense4::sdk {

// The `opense4` package built into OpenSE4 (python/opense4), as paths under
// the interpreter's root ("opense4/__init__.py") and their text.
std::span<const script::LibraryFile> packageFiles();

// ---- External bots (docs/sdk/ai-protocol.md §6, §7) -------------------------------------

// A service an external bot asks while it handles a request: its name
// ("query", "rules", "builtin", "builtin_answer", "apply") and argument.
using ServiceCall = std::function<std::expected<script::Value, std::string>(std::string_view service, const script::Value& args)>;

// The host's side of one external bot's connection. The transport (a local
// connection, or the network) comes later; tests use a double.
class ExternalBot {
public:
    virtual ~ExternalBot();
    // Sends a request and waits for the response. While the bot works on it
    // it may ask `services`. An error (no answer within the host's time, a
    // lost connection) counts as a failed request.
    virtual std::expected<script::Value, std::string> request(const script::Value& request, const ServiceCall& services) = 0;
};

// ---- Sessions ------------------------------------------------------------------------------

struct PlayerSetup {
    // The `opense4` package's files, in place of the built-in one (tests).
    std::vector<std::pair<std::string, std::string>> package;
    // Mods to take players from besides the game's own (the Rules' mod set):
    // tests and tools.
    std::vector<mods::Package> mods;
    // The bot connected to an external slot (null: none; its requests fail).
    std::function<ExternalBot*(uint32_t slot)> externals;
    // The interpreter's heap, shared by the call's script players.
    size_t heapBytes = size_t{64} << 20;
};

// Installs the SDK's sessions for every engine call in this process
// (game::setPlayersFactory). The client, the server and the tests call it
// once at start; installing again replaces the setup.
void installPlayers(PlayerSetup setup = {});
// Removes them: every empire is played by the built-in AI.
void uninstallPlayers();
// Whether an engine call on the game needs a session: an empire is played by
// a script or external player, or the game has mods with rules scripts.
bool needsSession(const game::Rules& r, const game::GameState& s, const PlayerSetup& setup);

// A session for one engine call on `s` (what the installed factory makes;
// also for a battle a window shows, TacticalBattle::Setup::scriptPlayers).
std::unique_ptr<game::Players> makeSession(const game::Rules& r, game::GameState& s, std::shared_ptr<const PlayerSetup> setup);

// ---- Choosing players ----------------------------------------------------------------------

// A player a mod declares (mod.toml [[ai.players]]).
struct PlayerChoice {
    std::string mod;          // the mod's id
    std::string name;         // the player's name
    std::string description;
    game::Controller controller() const;
};
// Every player the mods declare, in load order and declaration order.
std::vector<PlayerChoice> availablePlayers(const mods::ModSet& mods);
std::vector<PlayerChoice> availablePlayers(std::span<const mods::Package> packages);
// ... those of the mods a data set was loaded with (none without mods).
std::vector<PlayerChoice> availablePlayers(const game::Rules& r);
// The mods a data set was loaded with (mods::GameData), in load order.
std::span<const mods::Package> gamePackages(const game::Rules& r);
// What is wrong with the setup's controllers: a script player of a mod the
// game does not use, or that the mod does not declare. Empty: they are good.
std::vector<std::string> checkControllers(std::span<const game::EmpireSetup> empires, std::span<const mods::Package> packages);

// What opense4-sdk check says about a mod's computer players: each
// [[ai.players]] entry's module under ai/ and its class, the ai/ files the
// runtime cannot import (names, syntax), and ai/ files no player uses.
struct PlayerCheck {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};
PlayerCheck checkModPlayers(const mods::Package& p);
// The same for a mod's rules (docs/sdk/rules.md): its scripts/ files import
// and compile, what they register matches what mod.toml declares (an order,
// event, intelligence project type and victory condition each has its
// function, and nothing registered is undeclared), and its scenarios read.
PlayerCheck checkModRules(const mods::Package& p);

} // namespace opense4::sdk
