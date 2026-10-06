// opense4-ai-eval: headless games between computer players on the player's
// installed data set, with statistics (mods/hegemon/README.md, "Evaluation").
//
//   opense4-ai-eval --mod=DIR --players=builtin,MOD:PLAYER,... [--seed=N]
//       [--seeds=N] [--turns=N] [--systems=N] [--style=simultaneous|turn-based]
//       [--races=A,B,...] [--rotate] [--every=N] [--out=FILE.jsonl] [--data=DIR]
//
// Each seed is one galaxy. With --rotate it is played once per seat, the
// players shifted one seat each time, so that every player plays every
// position and race of that galaxy. Every game writes one JSON line (--out):
// the setup, and per empire its controller, race, final statistics, curves
// sampled every --every turns, its script requests' failures and costs.
// A stand-in until `opense4-sdk arena` lands; the JSON is meant to be easy to
// read from either.

#include "game/players.hpp"
#include "game/rules.hpp"
#include "game/score.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "mods/data_set.hpp"
#include "mods/mod_set.hpp"
#include "mods/package.hpp"
#include "ruleset/ruleset.hpp"
#include "script/json.hpp"
#include "sdk/players.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace opense4;
namespace fs = std::filesystem;

namespace {

constexpr const char* kUsage = R"(opense4-ai-eval: headless games between computer players, with statistics.

  --players=A,B,...     one controller per seat: builtin or MOD_ID:PLAYER (required)
  --mod=DIR             a mod folder to load (repeat for several)
  --seed=N              the first galaxy's seed (default 1)
  --seeds=N             how many galaxies (default 1)
  --rotate              play each galaxy once per seat, the players shifted one seat each time
  --turns=N             turns per game (default 100)
  --systems=N           star systems (default 30)
  --style=S             simultaneous or turn-based (default simultaneous)
  --races=A,B,...       race preset folders by seat (default: shuffled from the seed)
  --every=N             sample the curves every N turns (default 10)
  --out=FILE            append one JSON line per game
  --data=DIR            the game folder (default: the installed game)
  --see-all             computer players see everything (off by default: fair games)
)";

struct Args {
    std::map<std::string, std::vector<std::string>> opts;
    std::string get(const std::string& k, std::string fallback = {}) const {
        auto it = opts.find(k);
        return it == opts.end() || it->second.empty() ? fallback : it->second.back();
    }
    bool has(const std::string& k) const { return opts.contains(k); }
};

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= s.size()) {
        const size_t comma = s.find(',', at);
        const std::string part = s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!part.empty()) out.push_back(part);
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out;
}

std::string jsonString(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) {
            out += std::format("\\u{:04x}", static_cast<int>(c));
            continue;
        }
        out += c;
    }
    return out + "\"";
}

// What one empire's script requests cost in one turn.
struct TurnCost {
    double ms = 0;
    int64_t bytecodes = 0;
    int requests = 0;
    int failures = 0;
};

struct Costs {
    std::map<std::pair<uint32_t, uint32_t>, TurnCost> byEmpireTurn;   // (empire, turn)
    std::map<uint32_t, std::map<std::string, int64_t>> maxByCall;    // empire -> call -> most bytecodes in one request
};
Costs* g_costs = nullptr;

struct Sample {
    uint32_t turn = 0;
    int64_t score = 0, production = 0, research = 0, planets = 0, systems = 0, population = 0, ships = 0, tonnage = 0, techs = 0;
    bool alive = false;
};

Sample sampleOf(const game::Rules& r, const game::GameState& s, game::EmpireId e) {
    Sample x;
    x.turn = s.turn;
    const game::Empire& emp = s.empire(e);
    x.alive = emp.alive;
    if (!emp.alive) return x;
    const game::TurnStats st = game::score::currentStats(r, s, e);
    const game::score::ScoreParts parts = game::score::scoreParts(r, s, e);
    x.score = st.score;
    x.production = st.production.v[0] + st.production.v[1] + st.production.v[2];
    x.research = st.research;
    x.planets = st.planets;
    x.systems = st.systems;
    x.population = st.population;
    x.ships = st.ships + st.bases;
    x.tonnage = parts.tonnage;
    x.techs = st.techLevels;
    return x;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        }
        if (!a.starts_with("--")) {
            std::fprintf(stderr, "opense4-ai-eval: unexpected argument %s\n", a.c_str());
            return 2;
        }
        const size_t eq = a.find('=');
        if (eq == std::string::npos) args.opts[a.substr(2)].push_back("1");
        else args.opts[a.substr(2, eq - 2)].push_back(a.substr(eq + 1));
    }
    const std::vector<std::string> players = split(args.get("players"));
    if (players.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const uint64_t seed0 = std::stoull(args.get("seed", "1"));
    const int seeds = std::stoi(args.get("seeds", "1"));
    const bool rotate = args.has("rotate");
    const int turns = std::stoi(args.get("turns", "100"));
    const int systems = std::stoi(args.get("systems", "30"));
    const bool simultaneous = args.get("style", "simultaneous") != "turn-based";
    const int every = std::max(1, std::stoi(args.get("every", "10")));
    const std::vector<std::string> seatRaces = split(args.get("races"));

    // The data set, with the mods.
    const auto dataDir = ruleset::findInstalledDataDir(args.has("data") ? fs::path(args.get("data")) : fs::path{});
    if (!dataDir) {
        std::fputs("opense4-ai-eval: no installed game found (--data=DIR)\n", stderr);
        return 1;
    }
    mods::ModSet modSet;
    if (auto it = args.opts.find("mod"); it != args.opts.end())
        for (const std::string& m : it->second) {
            auto p = mods::openPackage(m);
            if (!p) {
                std::fprintf(stderr, "opense4-ai-eval: %s: %s\n", m.c_str(), p.error().c_str());
                return 1;
            }
            modSet.packages.push_back(std::move(*p));
        }
    const mods::LoadedDataSet loaded = mods::loadDataSet(dataDir->parent_path(), *dataDir, modSet);
    if (!loaded.ruleset || !loaded.diagnostics.errors.empty()) {
        std::fputs("opense4-ai-eval: the data set has errors\n", stderr);
        for (const std::string& e : loaded.diagnostics.errors) std::fprintf(stderr, "  %s\n", e.c_str());
        return 1;
    }
    const game::Rules rules(*loaded.ruleset);
    std::vector<std::string> racePool;
    for (const auto& p : rules.racePresets())
        if (!p.neutral) racePool.push_back(p.folder);
    std::sort(racePool.begin(), racePool.end());

    std::vector<game::Controller> controllers;
    for (const std::string& p : players) {
        auto c = game::parseController(p);
        if (!c) {
            std::fprintf(stderr, "opense4-ai-eval: '%s' is not a controller (builtin or MOD:PLAYER)\n", p.c_str());
            return 2;
        }
        controllers.push_back(*c);
    }

    Costs costs;
    g_costs = &costs;
    sdk::PlayerSetup setup;
    setup.observe = [](const sdk::RequestCost& c) {
        TurnCost& t = g_costs->byEmpireTurn[{c.empire.value, c.turn}];
        t.ms += std::chrono::duration<double, std::milli>(c.time).count();
        t.bytecodes += c.bytecodes;
        ++t.requests;
        if (c.failed) ++t.failures;
        int64_t& most = g_costs->maxByCall[c.empire.value][c.call];
        most = std::max(most, c.bytecodes);
    };
    sdk::installPlayers(std::move(setup));

    std::ofstream out;
    if (args.has("out")) out.open(args.get("out"), std::ios::app);

    const size_t seats = controllers.size();
    const int rounds = rotate ? static_cast<int>(seats) : 1;
    for (int g = 0; g < seeds; ++g) {
        const uint64_t seed = seed0 + static_cast<uint64_t>(g);
        // The races of this galaxy, by seat.
        std::vector<std::string> races = seatRaces;
        if (races.empty()) {
            races = racePool;
            uint64_t x = seed * 0x9e3779b97f4a7c15ull + 1;
            for (size_t i = races.size(); i > 1; --i) {
                x ^= x >> 33;
                x *= 0xff51afd7ed558ccdull;
                x ^= x >> 33;
                std::swap(races[i - 1], races[x % i]);
            }
        }
        for (int rot = 0; rot < rounds; ++rot) {
            game::GameSetup gs;
            gs.seed = seed;
            gs.options.systemCount = systems;
            gs.options.simultaneous = simultaneous;
            gs.options.aiSeesEverything = args.has("see-all");
            gs.options.victory = {};
            std::vector<size_t> who(seats);   // seat -> controller index
            for (size_t i = 0; i < seats; ++i) {
                who[i] = (i + static_cast<size_t>(rot)) % seats;
                game::EmpireSetup e;
                e.preset = races[i % races.size()];
                e.kind = game::PlayerKind::Computer;
                e.controller = controllers[who[i]];
                gs.empires.push_back(std::move(e));
            }
            auto created = game::createGame(rules, gs);
            if (!created) {
                std::fprintf(stderr, "opense4-ai-eval: seed %llu: %s\n", static_cast<unsigned long long>(seed), created.error().c_str());
                return 1;
            }
            game::GameState s = std::move(*created);
            costs = {};
            std::vector<std::vector<Sample>> curves(seats);
            std::vector<double> turnMs;
            const auto started = std::chrono::steady_clock::now();
            for (int t = 0; t < turns && !s.gameOver; ++t) {
                const auto t0 = std::chrono::steady_clock::now();
                game::processTurn(rules, s, {});
                turnMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                if (static_cast<int>(s.turn) % every == 0 || t + 1 == turns)
                    for (size_t i = 0; i < seats; ++i) curves[i].push_back(sampleOf(rules, s, game::EmpireId{static_cast<uint32_t>(i)}));
            }
            const double gameSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

            // The winner: the best score among the living (lower seat on a tie).
            const std::vector<game::EmpireId> ranking = game::score::ranking(rules, s);
            const int winner = ranking.empty() ? -1 : static_cast<int>(ranking.front().value);

            std::string line = std::format("{{\"seed\":{},\"rotation\":{},\"turns\":{},\"systems\":{},\"style\":\"{}\",\"seconds\":{:.1f},\"winner\":{},\"empires\":[",
                                           seed, rot, s.turn, systems, simultaneous ? "simultaneous" : "turn-based", gameSeconds, winner);
            std::string summary = std::format("seed {} rot {} ({:.0f} s):", seed, rot, gameSeconds);
            for (size_t i = 0; i < seats; ++i) {
                const game::EmpireId e{static_cast<uint32_t>(i)};
                const Sample last = sampleOf(rules, s, e);
                int rank = 0;
                for (size_t k = 0; k < ranking.size(); ++k)
                    if (ranking[k] == e) rank = static_cast<int>(k) + 1;
                double scriptMs = 0, worstMs = 0;
                int64_t worstBytecodes = 0;
                int failures = 0, requests = 0;
                for (const auto& [key, c] : costs.byEmpireTurn) {
                    if (key.first != e.value) continue;
                    scriptMs += c.ms;
                    worstMs = std::max(worstMs, c.ms);
                    worstBytecodes = std::max(worstBytecodes, c.bytecodes);
                    failures += c.failures;
                    requests += c.requests;
                }
                std::string curve;
                for (const Sample& x : curves[i])
                    curve += std::format("{}[{},{},{},{},{},{},{},{},{},{}]", curve.empty() ? "" : ",", x.turn, x.alive ? 1 : 0, x.score, x.production,
                                         x.research, x.planets, x.systems, x.ships, x.tonnage, x.techs);
                std::string perCall;
                for (const auto& [call, most] : costs.maxByCall[e.value]) perCall += std::format("{}{}:{}", perCall.empty() ? "" : ",", jsonString(call), most);
                line += std::format(
                    "{}{{\"seat\":{},\"controller\":{},\"race\":{},\"alive\":{},\"rank\":{},\"score\":{},\"production\":{},\"research\":{},\"planets\":{},"
                    "\"systems\":{},\"population\":{},\"ships\":{},\"tonnage\":{},\"techs\":{},\"requests\":{},\"failures\":{},\"script_ms\":{:.1f},"
                    "\"worst_turn_ms\":{:.1f},\"worst_turn_bytecodes\":{},\"max_bytecodes\":{{{}}},"
                    "\"curve\":[{}]}}",
                    i ? "," : "", i, jsonString(game::controllerText(controllers[who[i]])), jsonString(races[i % races.size()]), last.alive ? "true" : "false",
                    rank, last.score, last.production, last.research, last.planets, last.systems, last.population, last.ships, last.tonnage, last.techs,
                    requests, failures, scriptMs, worstMs, worstBytecodes, perCall, curve);
                summary += std::format(" [{} {}: {}{} sc {} pl {} sh {}{}]", i, game::controllerText(controllers[who[i]]), rank, last.alive ? "" : " dead",
                                       last.score, last.planets, last.ships, failures ? std::format(" FAIL {}", failures) : std::string());
            }
            double maxTurn = 0;
            for (double m : turnMs) maxTurn = std::max(maxTurn, m);
            line += std::format("],\"max_turn_ms\":{:.1f}}}", maxTurn);
            std::printf("%s\n", summary.c_str());
            std::fflush(stdout);
            if (out.is_open()) {
                out << line << "\n";
                out.flush();
            }
        }
    }
    sdk::uninstallPlayers();
    return 0;
}
