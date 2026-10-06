// opense4-sdk env-host: the engine behind the training environment
// (opense4.env, docs/sdk/bots-and-arena.md). It makes a game whose empire 0
// is played by the environment, connected as the external bot of slot 0,
// and the other empires by the opponents given, plays it, and ends when the
// turns are played, the game is over, empire 0 is gone, or the environment
// leaves.
//
// Its first line on standard output is {"port": N, "empire": 0}: where the
// environment connects, presenting the token it gave in OPENSE4_BOT_TOKEN.

#include "sdk_tool.hpp"

#include "core/environment.hpp"
#include "game/players.hpp"
#include "script/json.hpp"
#include "sdk/bots.hpp"
#include "sdk/match.hpp"

#include <cstdio>
#include <format>
#include <limits>

namespace opense4::sdktool {

namespace {

constexpr std::string_view kEnvUsage = R"(opense4-sdk env-host: the engine of opense4.env (docs/sdk/bots-and-arena.md).

Usage:
  OPENSE4_BOT_TOKEN=TOKEN opense4-sdk env-host --seed=N [options]

Options:
  --seed=N            The game's seed (default 1)
  --turns=N           Game turns to play at most (default 100)
  --opponent=SPEC     builtin or MOD:PLAYER, once per other empire (default: one builtin)
  --setup=FILE.toml, --systems=N, --quadrant-size=0|1|2, --quadrant=NAME, --race=NAME...,
  --turn-based        The game, as for the arena
  --data=DIR, --classic-dir=DIR, --mod=MOD..., --mods-dir=DIR
  --port=N            The port to listen on (default 0: any free one)
  --timeout=SEC       How long the environment may take over one request (default 86400)
  --token=TOKEN       Instead of OPENSE4_BOT_TOKEN (others on the computer may see it)
)";

} // namespace

int cmdEnvHost(const std::vector<std::string>& argv) {
    auto parsed = parseOptions(argv, 2,
                               {"seed", "turns", "opponent", "setup", "systems", "quadrant-size", "quadrant", "race", "data", "classic-dir", "mod",
                                "mods-dir", "port", "timeout", "token"},
                               {"turn-based", "help"});
    if (!parsed) return fail(parsed.error());
    const Options& o = *parsed;
    if (o.has("help")) {
        std::printf("%.*s", static_cast<int>(kEnvUsage.size()), kEnvUsage.data());
        return 0;
    }
    auto seed = o.integer("seed", 1, 0, std::numeric_limits<int64_t>::max());
    auto turns = o.integer("turns", 100, 1, 1'000'000);
    auto systems = o.integer("systems", 0, 0, 500);
    auto quadrantSize = o.integer("quadrant-size", 0, 0, 2);
    auto port = o.integer("port", 0, 0, 65535);
    auto timeout = o.integer("timeout", 86400, 1, 30 * 86400);
    for (const auto* v : {&seed, &turns, &systems, &quadrantSize, &port, &timeout})
        if (!*v) return fail(v->error());

    std::vector<sdk::Seat> seats(1);
    seats[0].controller.kind = game::Controller::Kind::External;
    seats[0].controller.slot = 0;
    std::vector<std::string> opponents = o.all("opponent");
    if (opponents.empty()) opponents.push_back("builtin");
    for (const std::string& spec : opponents) {
        auto c = game::parseController(spec);
        if (!c || c->kind == game::Controller::Kind::External) return fail(std::format("--opponent={}: builtin or MOD:PLAYER", spec));
        sdk::Seat seat;
        seat.controller = *c;
        seats.push_back(std::move(seat));
    }

    auto modSet = chooseMods(o.all("mod"), o.get("mods-dir"));
    if (!modSet) return fail(modSet.error(), 2);
    auto loaded = loadRules(dataOption(o), std::move(*modSet));
    if (!loaded) return fail(loaded.error(), 2);
    const game::Rules& r = *loaded->rules;
    GameChoice choice;
    choice.setupFile = o.get("setup");
    choice.systems = static_cast<int>(*systems);
    choice.quadrantSize = static_cast<int>(*quadrantSize);
    choice.quadrant = o.get("quadrant");
    choice.turnBased = o.has("turn-based");
    choice.races = o.all("race");
    auto setup = makeGameSetup(r, choice, static_cast<uint64_t>(*seed), seats.size());
    if (!setup) return fail(setup.error(), 2);
    if (setup->empires.size() != seats.size())
        return fail(std::format("the setup file has {} empires; the environment and its opponents are {}", setup->empires.size(), seats.size()), 2);

    sdk::BotHostOptions bo;
    bo.port = static_cast<uint16_t>(*port);
    bo.token = o.has("token") ? o.get("token") : core::environment("OPENSE4_BOT_TOKEN").value_or("");
    const bool madeToken = bo.token.empty();
    bo.gameName = std::format("training game {}", *seed);
    bo.slots = {0};
    bo.requestTimeout = std::chrono::seconds(*timeout);
    auto host = sdk::BotHost::open(std::move(bo));
    if (!host) return fail(host.error(), 1);
    {
        script::ValueMap hello;
        hello.emplace_back("port", script::Value(static_cast<int64_t>((*host)->port())));
        hello.emplace_back("empire", script::Value(0));
        if (madeToken) hello.emplace_back("token", script::Value((*host)->token()));
        std::printf("%s\n", script::toJson(script::Value(std::move(hello))).value_or("{}").c_str());
        std::fflush(stdout);
    }

    sdk::MatchSetup match;
    match.game = std::move(*setup);
    match.seats = std::move(seats);
    match.turns = static_cast<uint32_t>(*turns);
    match.bots = host->get();
    match.botTimeout = std::chrono::seconds(*timeout);
    match.botConnectTimeout = std::chrono::seconds(60);
    sdk::BotHost* bots = host->get();
    // The environment's empire gone, or the environment itself: nothing to train on.
    match.afterTurn = [bots](const game::GameState& s) { return bots->connected(0) && s.empire(game::EmpireId{0u}).alive; };
    auto result = sdk::playMatch(r, std::move(match));
    if (!result) return fail(result.error(), 1);
    std::printf("{\"done\":true,\"turns_played\":%u,\"final_turn\":%u,\"checksum\":\"%016llx\"}\n", result->turnsPlayed, result->state.turn,
                static_cast<unsigned long long>(result->checksum));
    std::fflush(stdout);
    return 0;
}

} // namespace opense4::sdktool
