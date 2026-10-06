// opense4-sdk arena (docs/sdk/bots-and-arena.md): headless games between
// computer players, played in parallel processes (`arena-game`, one game
// each), with a report of who won and how.

#include "sdk_tool.hpp"

#include "core/log.hpp"
#include "game/players.hpp"
#include "game/score.hpp"
#include "game/serialize.hpp"
#include "script/json.hpp"
#include "sdk/match.hpp"
#include "sdk/players.hpp"
#include "sdk/process.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>

namespace opense4::sdktool {

namespace fs = std::filesystem;
using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

constexpr std::string_view kArenaUsage = R"(opense4-sdk arena: headless games between computer players (docs/sdk/bots-and-arena.md).

Usage:
  opense4-sdk arena --ai=SPEC --ai=SPEC [--ai=SPEC...] [options]
  opense4-sdk arena --replay=DIR/games/game-0003.json

Each --ai is one computer player, NAME=SPEC to name it in the report:
  builtin                  the classic AI
  MOD:PLAYER               a player a mod declares ([[ai.players]]; give the mod with --mod)
  external:COMMAND         an external bot: COMMAND (run by the system's shell) is started
                           for each game it plays, with OPENSE4_BOT_HOST, _PORT, _TOKEN and
                           _SLOT in its environment, and PYTHONPATH reaching OpenSE4's
                           opense4 package, e.g. external:python3 -m opense4.bot mybot:Mine

Options:
  --games=N           Games to play (default 10)
  --turns=N           Game turns per game at most (default 100); a game also ends at a
                      victory or when one empire is left
  --seed=N            Game i (from 0) has the seed N + i (default 1)
  --jobs=N            Games played at once, each in a process of its own (default: the
                      computer's cores, at most 4)
  --empires=N         Empires per game (default: one per --ai)
  --no-swap           Keep each player in its seat; by default game i moves every player
                      i seats on, so that each plays every position and race
  --setup=FILE.toml   Options and empires from a server setup file (docs/MULTIPLAYER.md);
                      its seed and players are the arena's
  --systems=N, --quadrant-size=0|1|2, --quadrant=NAME, --turn-based, --race=NAME...
                      The galaxy and the races (default: small quadrant, simultaneous
                      turns, races drawn from each game's seed)
  --data=DIR          The game folder (or --classic-dir; default: the installed game)
  --mod=MOD           A mod to play with (repeatable, in load order); --mods-dir=DIR
  --timeout=SEC       An external bot's time per request (default 60)
  --out=DIR           Where the report and games go (default ./arena)
  --ratings=FILE      Elo ratings kept across runs (read, then updated)

It writes DIR/report.json, DIR/report.csv (one line per player), DIR/games.csv (one line
per player and game), DIR/over_time.csv (each player's means turn by turn) and, per game,
DIR/games/game-NNNN.gam (the final state, which the game opens), game-NNNN.json (what
--replay plays again) and game-NNNN.log.
)";

// ---- The players --------------------------------------------------------------------------------

struct Ai {
    std::string name;          // in the report
    std::string spec;          // as given
    game::Controller controller;
    std::string botCommand;    // external
};

std::string trimmed(std::string s) {
    const size_t a = s.find_first_not_of(" \t");
    const size_t b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::expected<Ai, std::string> parseAi(std::string text) {
    Ai ai;
    if (const size_t eq = text.find('='); eq != std::string::npos) {
        const std::string label = text.substr(0, eq);
        if (!label.empty() && label.find(':') == std::string::npos && label.find(' ') == std::string::npos) {
            ai.name = label;
            text = text.substr(eq + 1);
        }
    }
    ai.spec = text;
    if (text.starts_with("external:")) {
        ai.botCommand = trimmed(text.substr(9));
        if (ai.botCommand.empty())
            return std::unexpected(std::string("external: needs the bot's command, such as external:python3 -m opense4.bot mybot:Mine"));
        if (std::all_of(ai.botCommand.begin(), ai.botCommand.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::unexpected(std::format("--ai={}: the arena starts each game's bots itself: give the bot's command, not a slot", text));
        ai.controller.kind = game::Controller::Kind::External;
    } else {
        auto c = game::parseController(text);
        if (!c) return std::unexpected(std::format("--ai={}: not builtin, MOD:PLAYER or external:COMMAND", text));
        ai.controller = *c;
    }
    if (ai.name.empty()) ai.name = text.size() <= 48 ? text : text.substr(0, 45) + "...";
    return ai;
}

// ---- A game's description, for its process ---------------------------------------------------

Value str(const std::string& s) { return Value(s); }
Value num(int64_t v) { return Value(v); }

const Value& at(const Value& v, std::string_view key) {
    static const Value none;
    const Value* x = v.find(key);
    return x ? *x : none;
}
int64_t intAt(const Value& v, std::string_view key, int64_t fallback = 0) {
    const Value& x = at(v, key);
    return x.isInt() ? x.asInt() : fallback;
}
std::string strAt(const Value& v, std::string_view key) {
    const Value& x = at(v, key);
    return x.isString() ? x.asString() : std::string();
}
bool boolAt(const Value& v, std::string_view key) {
    const Value& x = at(v, key);
    return x.isBool() && x.asBool();
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool writeFile(const fs::path& p, std::string_view text) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::expected<Value, std::string> readJson(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return std::unexpected(std::format("{}: no such file", p.string()));
    auto v = script::parseJson(readFile(p));
    if (!v) return std::unexpected(std::format("{}: {}", p.string(), v.error().describe()));
    return std::move(*v);
}

GameChoice choiceOf(const Value& spec) {
    GameChoice c;
    c.setupFile = strAt(spec, "setup");
    c.systems = static_cast<int>(intAt(spec, "systems"));
    c.quadrantSize = static_cast<int>(intAt(spec, "quadrant_size"));
    c.quadrant = strAt(spec, "quadrant");
    c.turnBased = boolAt(spec, "turn_based");
    for (const Value& r : at(spec, "races").isList() ? at(spec, "races").asList() : ValueList{})
        if (r.isString()) c.races.push_back(r.asString());
    return c;
}

std::string hex16(uint64_t v) { return std::format("{:016x}", v); }

// ---- One game (its own process) ---------------------------------------------------------------

Value seriesOf(const sdk::SeatResult& seat, int64_t (*pick)(const sdk::SeatTurn&)) {
    ValueList l;
    for (const sdk::SeatTurn& t : seat.turns) l.push_back(Value(pick(t)));
    return Value(std::move(l));
}

Value resultValue(const Value& spec, const sdk::MatchResult& m) {
    const ValueList& seats = at(spec, "seats").asList();
    ValueList out;
    for (size_t i = 0; i < m.seats.size(); ++i) {
        const sdk::SeatResult& s = m.seats[i];
        const sdk::SeatTurn last = s.turns.empty() ? sdk::SeatTurn{} : s.turns.back();
        ValueMap seat;
        seat.emplace_back("empire", num(s.empire.value));
        seat.emplace_back("ai", i < seats.size() ? at(seats[i], "ai") : Value());
        seat.emplace_back("name", str(s.name));
        seat.emplace_back("race", str(s.race));
        seat.emplace_back("alive", Value(last.alive));
        seat.emplace_back("eliminated", s.eliminated ? num(*s.eliminated) : Value());
        seat.emplace_back("score", num(last.score));
        seat.emplace_back("colonies", num(last.colonies));
        seat.emplace_back("systems", num(last.systems));
        seat.emplace_back("planets", num(last.planets));
        seat.emplace_back("population", num(last.population));
        seat.emplace_back("ships", num(last.ships));
        seat.emplace_back("bases", num(last.bases));
        seat.emplace_back("units", num(last.units));
        seat.emplace_back("tech_levels", num(last.techLevels));
        seat.emplace_back("research", num(last.research));
        seat.emplace_back("battles_won", num(s.battlesWon));
        seat.emplace_back("battles_lost", num(s.battlesLost));
        seat.emplace_back("battles_drawn", num(s.battlesDrawn));
        seat.emplace_back("requests", num(s.requests));
        seat.emplace_back("replayed", num(s.replayed));
        seat.emplace_back("failures", num(s.failures));
        seat.emplace_back("fallbacks", num(s.fallbacks));
        seat.emplace_back("player_us", num(std::chrono::duration_cast<std::chrono::microseconds>(s.playerTime).count()));
        seat.emplace_back("turn_us_max", num(std::chrono::duration_cast<std::chrono::microseconds>(s.turnTimeMax).count()));
        seat.emplace_back("planning_budget_max", num(s.planningBudgetMax));
        seat.emplace_back("call_budget_max", num(s.callBudgetMax));
        ValueList errors;
        for (const std::string& e : s.errors) errors.push_back(str(e));
        seat.emplace_back("errors", Value(std::move(errors)));
        ValueMap series;
        series.emplace_back("score", seriesOf(s, [](const sdk::SeatTurn& t) { return t.score; }));
        series.emplace_back("colonies", seriesOf(s, [](const sdk::SeatTurn& t) { return int64_t{t.colonies}; }));
        series.emplace_back("systems", seriesOf(s, [](const sdk::SeatTurn& t) { return int64_t{t.systems}; }));
        series.emplace_back("ships", seriesOf(s, [](const sdk::SeatTurn& t) { return int64_t{t.ships}; }));
        series.emplace_back("tech_levels", seriesOf(s, [](const sdk::SeatTurn& t) { return int64_t{t.techLevels}; }));
        series.emplace_back("research", seriesOf(s, [](const sdk::SeatTurn& t) { return t.research; }));
        series.emplace_back("battles_won", seriesOf(s, [](const sdk::SeatTurn& t) { return int64_t{t.battlesWon}; }));
        series.emplace_back("battles_lost", seriesOf(s, [](const sdk::SeatTurn& t) { return int64_t{t.battlesLost}; }));
        seat.emplace_back("series", Value(std::move(series)));
        out.push_back(Value(std::move(seat)));
    }
    ValueMap r;
    r.emplace_back("game", at(spec, "game"));
    r.emplace_back("seed", num(static_cast<int64_t>(m.seed)));
    r.emplace_back("turns_played", num(m.turnsPlayed));
    r.emplace_back("final_turn", num(m.state.turn));
    r.emplace_back("game_over", Value(m.gameOver));
    r.emplace_back("winner", m.winner.valid() ? num(m.winner.value) : Value());
    r.emplace_back("won_by", str(m.winnerBy));
    r.emplace_back("checksum", str(hex16(m.checksum)));
    r.emplace_back("ms", num(std::chrono::duration_cast<std::chrono::milliseconds>(m.time).count()));
    r.emplace_back("seats", Value(std::move(out)));
    return Value(std::move(r));
}

// Plays the game a description gives; the result, with the final state saved when it says where.
std::expected<Value, std::string> playSpec(const Value& spec, bool save) {
    std::vector<std::string> modPaths;
    for (const Value& m : at(spec, "mods").isList() ? at(spec, "mods").asList() : ValueList{})
        if (m.isString()) modPaths.push_back(m.asString());
    auto modSet = chooseMods(modPaths, strAt(spec, "mods_dir"));
    if (!modSet) return std::unexpected(modSet.error());
    auto loaded = loadRules(strAt(spec, "data"), std::move(*modSet));
    if (!loaded) return std::unexpected(loaded.error());
    const game::Rules& r = *loaded->rules;
    const ValueList& seats = at(spec, "seats").asList();
    auto setup = makeGameSetup(r, choiceOf(spec), static_cast<uint64_t>(intAt(spec, "seed")), seats.size());
    if (!setup) return std::unexpected(setup.error());
    if (setup->empires.size() != seats.size())
        return std::unexpected(std::format("the setup has {} empires for {} seats", setup->empires.size(), seats.size()));

    sdk::MatchSetup match;
    match.game = std::move(*setup);
    match.turns = static_cast<uint32_t>(intAt(spec, "turns", 100));
    for (const Value& s : seats) {
        sdk::Seat seat;
        auto c = game::parseController(strAt(s, "controller"));
        if (!c) return std::unexpected(std::format("the description's controller '{}' is not one", strAt(s, "controller")));
        seat.controller = *c;
        seat.botCommand = strAt(s, "bot");
        match.seats.push_back(std::move(seat));
    }
    match.botTimeout = std::chrono::milliseconds(intAt(spec, "timeout_ms", 60'000));
    if (const std::string py = strAt(spec, "python_path"); !py.empty()) match.botEnvironment.emplace_back("PYTHONPATH", pythonPathWith(py));
    match.botLogDir = strAt(spec, "bot_logs");
    auto played = sdk::playMatch(r, std::move(match));
    if (!played) return std::unexpected(played.error());
    if (save && !strAt(spec, "save").empty()) {
        game::SaveInfo info;
        info.gameName = std::format("Arena game {} (seed {})", intAt(spec, "game"), intAt(spec, "seed"));
        info.dataSet = game::dataSetIdentity(r);
        info.mods = loaded->mods.records();
        if (auto w = game::saveGame(strAt(spec, "save"), played->state, info); !w) return std::unexpected(w.error());
    }
    return resultValue(spec, *played);
}

// ---- The report -------------------------------------------------------------------------------

// JSON with fractions, which script::Value does not hold.
class JsonOut {
public:
    std::string text;
    JsonOut& open(char c) {
        sep();
        text += c;
        first_ = true;
        return *this;
    }
    JsonOut& close(char c) {
        text += c;
        first_ = false;
        return *this;
    }
    JsonOut& key(std::string_view k) {
        sep();
        text += quote(k) + ":";
        first_ = true;   // the value follows without a comma
        return *this;
    }
    JsonOut& raw(const std::string& v) {
        sep();
        text += v;
        return *this;
    }
    JsonOut& value(std::string_view v) { return raw(quote(v)); }
    JsonOut& value(const std::string& v) { return raw(quote(v)); }
    JsonOut& value(const char* v) { return raw(quote(v)); }
    JsonOut& value(int64_t v) { return raw(std::to_string(v)); }
    JsonOut& value(double v) { return raw(std::isfinite(v) ? std::format("{:.3f}", v) : std::string("null")); }
    JsonOut& value(bool v) { return raw(v ? "true" : "false"); }
    JsonOut& null() { return raw("null"); }
    JsonOut& value(const Value& v) {
        auto j = script::toJson(v);
        return raw(j ? *j : std::string("null"));
    }

    static std::string quote(std::string_view s) {
        auto j = script::toJson(Value(s));
        return j ? *j : std::string("\"\"");
    }

private:
    void sep() {
        if (!first_) text += ',';
        first_ = false;
    }
    bool first_ = true;
};

std::string csvField(std::string_view s) {
    if (s.find_first_of(",\"\n") == std::string_view::npos) return std::string(s);
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0;
    double sum = 0;
    for (double x : v) sum += x;
    return sum / static_cast<double>(v.size());
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

struct Totals {
    std::string name, spec;
    int games = 0, seats = 0, wins = 0, eliminations = 0;
    std::vector<double> score, colonies, systems, techLevels, research, ships;
    int64_t battlesWon = 0, battlesLost = 0, battlesDrawn = 0;
    int64_t requests = 0, failures = 0, fallbacks = 0, playerUs = 0, seatTurns = 0;
    int64_t turnUsMax = 0, planningBudgetMax = 0, callBudgetMax = 0;   // the most in any game
    std::map<size_t, std::vector<double>> overScore, overColonies, overSystems, overShips, overTech, overResearch, overWon, overLost;
    double elo = 1500;
    int64_t ratedGames = 0;
};

// Elo across the seats of each game: every pair of seats of different players, the
// winner first, then by score.
void rateGame(const Value& result, std::map<std::string, Totals>& totals) {
    const ValueList& seats = at(result, "seats").asList();
    const int64_t winner = intAt(result, "winner", -1);
    std::vector<size_t> order(seats.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    auto rank = [&](size_t i) {
        return std::make_tuple(intAt(seats[i], "empire") == winner ? 1 : 0, boolAt(seats[i], "alive") ? 1 : 0, intAt(seats[i], "score"));
    };
    std::map<std::string, double> delta;
    std::set<std::string> rated;
    const double k = 32.0 / static_cast<double>(std::max<size_t>(1, seats.size() - 1));
    for (size_t a = 0; a < seats.size(); ++a)
        for (size_t b = a + 1; b < seats.size(); ++b) {
            const std::string na = strAt(seats[a], "ai"), nb = strAt(seats[b], "ai");
            if (na == nb || !totals.contains(na) || !totals.contains(nb)) continue;
            const double ra = totals[na].elo, rb = totals[nb].elo;
            const double expectA = 1.0 / (1.0 + std::pow(10.0, (rb - ra) / 400.0));
            const auto ka = rank(a), kb = rank(b);
            const double scoreA = ka > kb ? 1.0 : ka < kb ? 0.0 : 0.5;
            delta[na] += k * (scoreA - expectA);
            delta[nb] -= k * (scoreA - expectA);
            rated.insert(na);
            rated.insert(nb);
        }
    for (const auto& [name, d] : delta) totals[name].elo += d;
    for (const std::string& name : rated) ++totals[name].ratedGames;
}

void addGame(const Value& result, std::map<std::string, Totals>& totals) {
    const int64_t winner = intAt(result, "winner", -1);
    std::set<std::string> played;
    std::set<std::string> won;
    for (const Value& seat : at(result, "seats").asList()) {
        const std::string name = strAt(seat, "ai");
        auto it = totals.find(name);
        if (it == totals.end()) continue;
        Totals& t = it->second;
        played.insert(name);
        if (intAt(seat, "empire") == winner) won.insert(name);
        ++t.seats;
        t.score.push_back(static_cast<double>(intAt(seat, "score")));
        t.colonies.push_back(static_cast<double>(intAt(seat, "colonies")));
        t.systems.push_back(static_cast<double>(intAt(seat, "systems")));
        t.techLevels.push_back(static_cast<double>(intAt(seat, "tech_levels")));
        t.research.push_back(static_cast<double>(intAt(seat, "research")));
        t.ships.push_back(static_cast<double>(intAt(seat, "ships")));
        t.battlesWon += intAt(seat, "battles_won");
        t.battlesLost += intAt(seat, "battles_lost");
        t.battlesDrawn += intAt(seat, "battles_drawn");
        if (!at(seat, "eliminated").isNull()) ++t.eliminations;
        t.requests += intAt(seat, "requests");
        t.failures += intAt(seat, "failures");
        t.fallbacks += intAt(seat, "fallbacks");
        t.playerUs += intAt(seat, "player_us");
        t.turnUsMax = std::max(t.turnUsMax, intAt(seat, "turn_us_max"));
        t.planningBudgetMax = std::max(t.planningBudgetMax, intAt(seat, "planning_budget_max"));
        t.callBudgetMax = std::max(t.callBudgetMax, intAt(seat, "call_budget_max"));
        t.seatTurns += intAt(result, "turns_played");
        const Value& series = at(seat, "series");
        auto collect = [&](std::string_view key, std::map<size_t, std::vector<double>>& into) {
            const Value& list = at(series, key);
            if (!list.isList()) return;
            for (size_t i = 0; i < list.asList().size(); ++i)
                if (list.asList()[i].isInt()) into[i + 1].push_back(static_cast<double>(list.asList()[i].asInt()));
        };
        collect("score", t.overScore);
        collect("colonies", t.overColonies);
        collect("systems", t.overSystems);
        collect("ships", t.overShips);
        collect("tech_levels", t.overTech);
        collect("research", t.overResearch);
        collect("battles_won", t.overWon);
        collect("battles_lost", t.overLost);
    }
    for (const std::string& name : played) ++totals[name].games;
    for (const std::string& name : won) ++totals[name].wins;
}

double perTurnMs(const Totals& t) { return t.seatTurns > 0 ? static_cast<double>(t.playerUs) / 1000.0 / static_cast<double>(t.seatTurns) : 0.0; }

// Ratings kept across runs: {"ratings": [{"name", "elo_tenths", "games"}]}.
void readRatings(const fs::path& file, std::map<std::string, Totals>& totals) {
    auto v = readJson(file);
    if (!v) return;
    for (const Value& e : at(*v, "ratings").isList() ? at(*v, "ratings").asList() : ValueList{}) {
        auto it = totals.find(strAt(e, "name"));
        if (it == totals.end()) continue;
        it->second.elo = static_cast<double>(intAt(e, "elo_tenths", 15000)) / 10.0;
        it->second.ratedGames = intAt(e, "games");
    }
}

void writeRatings(const fs::path& file, const std::map<std::string, Totals>& totals) {
    std::map<std::string, Value> keep;
    if (auto v = readJson(file))
        for (const Value& e : at(*v, "ratings").isList() ? at(*v, "ratings").asList() : ValueList{}) keep[strAt(e, "name")] = e;
    for (const auto& [name, t] : totals) {
        ValueMap e;
        e.emplace_back("name", str(name));
        e.emplace_back("elo_tenths", num(static_cast<int64_t>(std::llround(t.elo * 10.0))));
        e.emplace_back("games", num(t.ratedGames));
        keep[name] = Value(std::move(e));
    }
    ValueList list;
    for (auto& [name, e] : keep) list.push_back(e);
    ValueMap root;
    root.emplace_back("ratings", Value(std::move(list)));
    if (auto j = script::toJson(Value(std::move(root)), true)) writeFile(file, *j + "\n");
}

struct ArenaRun {
    Value header;                    // the arena's settings
    std::vector<std::string> order; // players, as given
    std::map<std::string, Totals> totals;
    std::vector<Value> results;      // finished games, by game number
    std::vector<std::string> failed;
};

void writeReport(const fs::path& out, ArenaRun& run, double seconds) {
    JsonOut j;
    j.open('{');
    j.key("arena").value(run.header);
    j.key("seconds").value(seconds);
    j.key("failed_games").open('[');
    for (const std::string& f : run.failed) j.value(f);
    j.close(']');
    j.key("ais").open('[');
    for (const std::string& name : run.order) {
        const Totals& t = run.totals.at(name);
        j.open('{');
        j.key("name").value(t.name);
        j.key("spec").value(t.spec);
        j.key("games").value(int64_t{t.games});
        j.key("seats").value(int64_t{t.seats});
        j.key("wins").value(int64_t{t.wins});
        j.key("win_rate").value(t.games ? static_cast<double>(t.wins) / t.games : 0.0);
        j.key("score_mean").value(mean(t.score));
        j.key("score_median").value(median(t.score));
        j.key("colonies_mean").value(mean(t.colonies));
        j.key("systems_mean").value(mean(t.systems));
        j.key("tech_levels_mean").value(mean(t.techLevels));
        j.key("research_mean").value(mean(t.research));
        j.key("ships_mean").value(mean(t.ships));
        j.key("battles_won").value(t.battlesWon);
        j.key("battles_lost").value(t.battlesLost);
        j.key("battles_drawn").value(t.battlesDrawn);
        j.key("eliminations").value(int64_t{t.eliminations});
        j.key("requests").value(t.requests);
        j.key("failures").value(t.failures);
        j.key("fallbacks").value(t.fallbacks);
        j.key("player_ms_per_turn").value(perTurnMs(t));
        j.key("player_ms_turn_max").value(static_cast<double>(t.turnUsMax) / 1000.0);
        j.key("planning_budget_max").value(t.planningBudgetMax);
        j.key("call_budget_max").value(t.callBudgetMax);
        j.key("elo").value(t.elo);
        j.key("over_time").open('[');
        for (const auto& [turn, scores] : t.overScore) {
            auto m = [&](const std::map<size_t, std::vector<double>>& series) {
                auto it = series.find(turn);
                return it == series.end() ? 0.0 : mean(it->second);
            };
            j.open('{');
            j.key("turn").value(static_cast<int64_t>(turn));
            j.key("seats").value(static_cast<int64_t>(scores.size()));
            j.key("score").value(mean(scores));
            j.key("colonies").value(m(t.overColonies));
            j.key("systems").value(m(t.overSystems));
            j.key("ships").value(m(t.overShips));
            j.key("tech_levels").value(m(t.overTech));
            j.key("research").value(m(t.overResearch));
            j.key("battles_won").value(m(t.overWon));
            j.key("battles_lost").value(m(t.overLost));
            j.close('}');
        }
        j.close(']');
        j.close('}');
    }
    j.close(']');
    j.key("games").open('[');
    for (const Value& r : run.results) {
        // Each game without its turn-by-turn series (game-NNNN.result.json has them).
        Value brief = r;
        if (brief.find("seats")) {
            ValueList seats = brief.find("seats")->asList();
            for (Value& s : seats)
                if (s.isMap()) std::erase_if(s.editMap(), [](const auto& kv) { return kv.first == "series"; });
            brief.set("seats", Value(std::move(seats)));
        }
        j.value(brief);
    }
    j.close(']');
    j.close('}');
    writeFile(out / "report.json", j.text + "\n");

    std::string csv = "ai,spec,games,seats,wins,win_rate,score_mean,score_median,colonies_mean,systems_mean,tech_levels_mean,research_mean,"
                      "ships_mean,battles_won,battles_lost,battles_drawn,eliminations,requests,failures,fallbacks,player_ms_per_turn,elo,"
                      "player_ms_turn_max,planning_budget_max,call_budget_max\n";
    for (const std::string& name : run.order) {
        const Totals& t = run.totals.at(name);
        csv += std::format("{},{},{},{},{},{:.3f},{:.1f},{:.1f},{:.2f},{:.2f},{:.2f},{:.1f},{:.2f},{},{},{},{},{},{},{},{:.3f},{:.1f},{:.3f},{},{}\n",
                           csvField(t.name), csvField(t.spec), t.games, t.seats, t.wins, t.games ? static_cast<double>(t.wins) / t.games : 0.0,
                           mean(t.score), median(t.score), mean(t.colonies), mean(t.systems), mean(t.techLevels), mean(t.research), mean(t.ships),
                           t.battlesWon, t.battlesLost, t.battlesDrawn, t.eliminations, t.requests, t.failures, t.fallbacks, perTurnMs(t), t.elo,
                           static_cast<double>(t.turnUsMax) / 1000.0, t.planningBudgetMax, t.callBudgetMax);
    }
    writeFile(out / "report.csv", csv);

    std::string games = "game,seed,ai,empire,race,won,score,colonies,systems,tech_levels,research,ships,alive,eliminated,battles_won,battles_lost,"
                        "failures,fallbacks,turns_played,won_by,checksum,save\n";
    for (const Value& r : run.results)
        for (const Value& s : at(r, "seats").asList())
            games += std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n", intAt(r, "game"), intAt(r, "seed"),
                                 csvField(strAt(s, "ai")), intAt(s, "empire"), csvField(strAt(s, "race")),
                                 intAt(s, "empire") == intAt(r, "winner", -1) ? 1 : 0, intAt(s, "score"), intAt(s, "colonies"), intAt(s, "systems"),
                                 intAt(s, "tech_levels"), intAt(s, "research"), intAt(s, "ships"), boolAt(s, "alive") ? 1 : 0,
                                 at(s, "eliminated").isInt() ? std::to_string(at(s, "eliminated").asInt()) : std::string(), intAt(s, "battles_won"),
                                 intAt(s, "battles_lost"), intAt(s, "failures"), intAt(s, "fallbacks"), intAt(r, "turns_played"), strAt(r, "won_by"),
                                 strAt(r, "checksum"), csvField(std::format("games/game-{:04}.gam", intAt(r, "game"))));
    writeFile(out / "games.csv", games);

    std::string over = "turn,ai,seats,score,colonies,systems,ships,tech_levels,research,battles_won,battles_lost\n";
    for (const std::string& name : run.order) {
        const Totals& t = run.totals.at(name);
        for (const auto& [turn, scores] : t.overScore) {
            auto m = [&](const std::map<size_t, std::vector<double>>& series) {
                auto it = series.find(turn);
                return it == series.end() ? 0.0 : mean(it->second);
            };
            over += std::format("{},{},{},{:.1f},{:.2f},{:.2f},{:.2f},{:.2f},{:.1f},{:.3f},{:.3f}\n", turn, csvField(name), scores.size(), mean(scores),
                                m(t.overColonies), m(t.overSystems), m(t.overShips), m(t.overTech), m(t.overResearch), m(t.overWon), m(t.overLost));
        }
    }
    writeFile(out / "over_time.csv", over);
}

void printSummary(const ArenaRun& run, const fs::path& out, double seconds) {
    size_t width = 8;
    for (const std::string& name : run.order) width = std::max(width, name.size());
    width = std::min<size_t>(width, 40);
    std::printf("\n%-*s %5s %5s %6s %9s %9s %8s %7s %6s %6s %9s %5s %9s %8s %7s\n", static_cast<int>(width), "player", "games", "wins", "win%", "score",
                "median", "colonies", "systems", "techs", "ships", "battles", "elim", "fail/fb", "ms/turn", "elo");
    for (const std::string& name : run.order) {
        const Totals& t = run.totals.at(name);
        std::printf("%-*s %5d %5d %5.1f%% %9.0f %9.0f %8.1f %7.1f %6.1f %6.1f %4lld/%-4lld %5d %4lld/%-4lld %8.2f %7.1f\n", static_cast<int>(width),
                    name.substr(0, width).c_str(), t.games, t.wins, t.games ? 100.0 * t.wins / t.games : 0.0, mean(t.score), median(t.score),
                    mean(t.colonies), mean(t.systems), mean(t.techLevels), mean(t.ships), static_cast<long long>(t.battlesWon),
                    static_cast<long long>(t.battlesLost), t.eliminations, static_cast<long long>(t.failures), static_cast<long long>(t.fallbacks),
                    perTurnMs(t), t.elo);
    }
    std::printf("\n%zu games in %.1f s; report in %s (report.json, report.csv, games.csv, over_time.csv)\n", run.results.size(), seconds,
                out.string().c_str());
    if (!run.failed.empty()) std::printf("%zu games failed: %s\n", run.failed.size(), run.failed.front().c_str());
}

int replay(const fs::path& specFile) {
    auto spec = readJson(specFile);
    if (!spec) return fail(spec.error(), 2);
    const fs::path resultFile = strAt(*spec, "result");
    auto before = readJson(resultFile);
    std::printf("Playing game %lld (seed %lld) again...\n", static_cast<long long>(intAt(*spec, "game")), static_cast<long long>(intAt(*spec, "seed")));
    auto result = playSpec(*spec, false);
    if (!result) return fail(result.error(), 1);
    const std::string now = strAt(*result, "checksum");
    std::printf("Final turn %lld, checksum %s", static_cast<long long>(intAt(*result, "final_turn")), now.c_str());
    if (!before) {
        std::printf(" (no recorded result to compare)\n");
        return 0;
    }
    const std::string then = strAt(*before, "checksum");
    if (then == now) {
        std::printf(": the same as when it was played.\n");
        return 0;
    }
    std::printf(": the game was played to checksum %s. An external bot that is not deterministic, or other data or mods, explain it.\n",
                then.c_str());
    return 1;
}

} // namespace

int cmdArenaGame(const std::vector<std::string>& argv) {
    if (argv.size() != 3) return fail("arena-game needs the game's description (DIR/games/game-NNNN.json)");
    auto spec = readJson(argv[2]);
    if (!spec) return fail(spec.error(), 2);
    auto result = playSpec(*spec, true);
    if (!result) return fail(result.error(), 1);
    auto text = script::toJson(*result);
    if (!text || !writeFile(strAt(*spec, "result"), *text + "\n")) return fail(std::format("{}: could not be written", strAt(*spec, "result")), 1);
    return 0;
}

int cmdArena(const std::vector<std::string>& argv) {
    auto parsed = parseOptions(argv, 2,
                               {"ai", "games", "turns", "seed", "jobs", "empires", "setup", "systems", "quadrant-size", "quadrant", "race", "data",
                                "classic-dir", "mod", "mods-dir", "timeout", "out", "ratings", "replay"},
                               {"no-swap", "turn-based", "help"});
    if (!parsed) return fail(parsed.error());
    const Options& o = *parsed;
    if (o.has("help")) {
        std::printf("%.*s", static_cast<int>(kArenaUsage.size()), kArenaUsage.data());
        return 0;
    }
    if (o.has("replay")) return replay(o.get("replay"));
    if (!o.positional.empty()) return fail(std::format("unexpected argument '{}' (see opense4-sdk arena --help)", o.positional.front()));

    std::vector<Ai> ais;
    for (const std::string& spec : o.all("ai")) {
        auto ai = parseAi(spec);
        if (!ai) return fail(ai.error());
        ais.push_back(std::move(*ai));
    }
    if (ais.empty()) return fail("the arena needs players: --ai=SPEC, at least once (see opense4-sdk arena --help)");
    std::map<std::string, int> seen;
    for (Ai& ai : ais)
        if (int n = ++seen[ai.name]; n > 1) ai.name += std::format(" #{}", n);

    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    auto games = o.integer("games", 10, 1, 100000);
    auto turns = o.integer("turns", 100, 1, 100000);
    auto seed = o.integer("seed", 1, 0, std::numeric_limits<int64_t>::max() / 2);
    auto jobs = o.integer("jobs", std::min<int64_t>(4, cores), 1, 64);
    auto empires = o.integer("empires", 0, 0, 20);
    auto systems = o.integer("systems", 0, 0, 500);
    auto quadrantSize = o.integer("quadrant-size", 0, 0, 2);
    auto timeout = o.integer("timeout", 60, 1, 7 * 24 * 3600);
    for (const auto* v : {&games, &turns, &seed, &jobs, &empires, &systems, &quadrantSize, &timeout})
        if (!*v) return fail(v->error());

    // The data set, once here: the players and the setup are checked before any game.
    auto modSet = chooseMods(o.all("mod"), o.get("mods-dir"));
    if (!modSet) return fail(modSet.error(), 2);
    std::vector<std::string> modPaths;
    for (const mods::Package& p : modSet->packages) modPaths.push_back(fs::absolute(p.source).string());
    auto loaded = loadRules(dataOption(o), std::move(*modSet));
    if (!loaded) return fail(loaded.error(), 2);
    const game::Rules& r = *loaded->rules;
    GameChoice choice;
    if (o.has("setup")) choice.setupFile = fs::absolute(o.get("setup"));
    choice.systems = static_cast<int>(*systems);
    choice.quadrantSize = static_cast<int>(*quadrantSize);
    choice.quadrant = o.get("quadrant");
    choice.turnBased = o.has("turn-based");
    choice.races = o.all("race");
    auto fromFile = setupFileEmpires(r, choice.setupFile);
    if (!fromFile) return fail(fromFile.error(), 2);
    const size_t seatCount = *fromFile > 0 ? *fromFile : std::max<size_t>(static_cast<size_t>(*empires), std::max<size_t>(2, ais.size()));
    if (seatCount < 2) return fail("a game needs two empires at least");
    if (auto probe = makeGameSetup(r, choice, static_cast<uint64_t>(*seed), seatCount); !probe) return fail(probe.error(), 2);
    for (const Ai& ai : ais) {
        std::vector<game::EmpireSetup> one(1);
        one[0].controller = ai.controller;
        const std::vector<std::string> problems = sdk::checkControllers(one, sdk::gamePackages(r));
        if (!problems.empty()) return fail(std::format("--ai={}: {}", ai.spec, problems.front()), 2);
    }

    const fs::path out = fs::absolute(o.get("out", "arena"));
    std::error_code ec;
    fs::create_directories(out / "games", ec);
    if (ec) return fail(std::format("{}: {}", out.string(), ec.message()), 2);
    std::string pythonPath;
    if (std::any_of(ais.begin(), ais.end(), [](const Ai& a) { return !a.botCommand.empty(); })) {
        auto py = writePythonPackage(out / "python");
        if (!py) return fail(py.error(), 2);
        pythonPath = py->string();
    }
    const fs::path self = sdk::executableDir() / sdk::executableName("opense4-sdk");

    ArenaRun run;
    for (const Ai& ai : ais) {
        run.order.push_back(ai.name);
        Totals& t = run.totals[ai.name];
        t.name = ai.name;
        t.spec = ai.spec;
    }
    if (o.has("ratings")) readRatings(o.get("ratings"), run.totals);
    {
        ValueMap h;
        h.emplace_back("games", num(*games));
        h.emplace_back("turns", num(*turns));
        h.emplace_back("seed", num(*seed));
        h.emplace_back("jobs", num(*jobs));
        h.emplace_back("seats", num(static_cast<int64_t>(seatCount)));
        h.emplace_back("swap", Value(!o.has("no-swap")));
        h.emplace_back("turn_based", Value(choice.turnBased));
        h.emplace_back("data", str(loaded->dataDir.string()));
        h.emplace_back("data_set", str(game::dataSetIdentity(r)));
        ValueList mods;
        for (const std::string& m : modPaths) mods.push_back(str(m));
        h.emplace_back("mods", Value(std::move(mods)));
        run.header = Value(std::move(h));
    }

    std::printf("Arena: %lld games of up to %lld turns, %zu empires each, seeds %lld to %lld, %lld at a time, in %s\n",
                static_cast<long long>(*games), static_cast<long long>(*turns), seatCount, static_cast<long long>(*seed),
                static_cast<long long>(*seed + *games - 1), static_cast<long long>(*jobs), out.string().c_str());
    std::fflush(stdout);

    // Each game's description, then its process.
    auto describe = [&](int64_t game) {
        ValueMap spec;
        spec.emplace_back("game", num(game));
        spec.emplace_back("seed", num(*seed + game));
        spec.emplace_back("turns", num(*turns));
        spec.emplace_back("data", str(loaded->dataDir.string()));
        ValueList mods;
        for (const std::string& m : modPaths) mods.push_back(str(m));
        spec.emplace_back("mods", Value(std::move(mods)));
        spec.emplace_back("mods_dir", str(o.get("mods-dir")));
        spec.emplace_back("setup", str(choice.setupFile.string()));
        spec.emplace_back("systems", num(choice.systems));
        spec.emplace_back("quadrant_size", num(choice.quadrantSize));
        spec.emplace_back("quadrant", str(choice.quadrant));
        spec.emplace_back("turn_based", Value(choice.turnBased));
        ValueList races;
        for (const std::string& rc : choice.races) races.push_back(str(rc));
        spec.emplace_back("races", Value(std::move(races)));
        spec.emplace_back("timeout_ms", num(*timeout * 1000));
        spec.emplace_back("python_path", str(pythonPath));
        const std::string stem = std::format("game-{:04}", game);
        spec.emplace_back("save", str((out / "games" / (stem + ".gam")).string()));
        spec.emplace_back("result", str((out / "games" / (stem + ".result.json")).string()));
        spec.emplace_back("bot_logs", str((out / "games" / (stem + "-bots")).string()));
        ValueList seats;
        const size_t shift = o.has("no-swap") ? 0 : static_cast<size_t>(game);
        for (size_t s = 0; s < seatCount; ++s) {
            const Ai& ai = ais[(s + shift) % ais.size()];
            game::Controller c = ai.controller;
            if (c.kind == game::Controller::Kind::External) c.slot = static_cast<uint32_t>(s);
            ValueMap seat;
            seat.emplace_back("ai", str(ai.name));
            seat.emplace_back("spec", str(ai.spec));
            seat.emplace_back("controller", str(game::controllerText(c)));
            seat.emplace_back("bot", str(ai.botCommand));
            seats.push_back(Value(std::move(seat)));
        }
        spec.emplace_back("seats", Value(std::move(seats)));
        return Value(std::move(spec));
    };

    struct Running {
        int64_t game;
        sdk::Process process;
        std::chrono::steady_clock::time_point started;
    };
    std::vector<Running> running;
    std::map<int64_t, Value> results;
    int64_t next = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (next < *games || !running.empty()) {
        while (static_cast<int64_t>(running.size()) < *jobs && next < *games) {
            const int64_t game = next++;
            const Value spec = describe(game);
            const fs::path specFile = out / "games" / std::format("game-{:04}.json", game);
            auto text = script::toJson(spec, true);
            if (!text || !writeFile(specFile, *text + "\n")) return fail(std::format("{}: could not be written", specFile.string()), 1);
            std::error_code rm;
            fs::remove(strAt(spec, "result"), rm);
            sdk::ProcessOptions po;
            po.args = {self.string(), "arena-game", specFile.string()};
            po.output = out / "games" / std::format("game-{:04}.log", game);
            auto p = sdk::Process::start(po);
            if (!p) return fail(p.error(), 1);
            running.push_back({game, std::move(*p), std::chrono::steady_clock::now()});
        }
        for (size_t i = 0; i < running.size();) {
            const std::optional<int> code = running[i].process.poll();
            if (!code) {
                ++i;
                continue;
            }
            const int64_t game = running[i].game;
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - running[i].started).count();
            const fs::path stem = out / "games" / std::format("game-{:04}", game);
            std::expected<Value, std::string> result = std::unexpected(std::string());
            if (*code == 0) result = readJson(fs::path(stem.string() + ".result.json"));
            if (!result) {
                std::string why = std::format("game {} (seed {}) failed (exit {}): see {}.log", game, *seed + game, *code, stem.string());
                std::printf("%s\n", why.c_str());
                run.failed.push_back(std::move(why));
            } else {
                std::string who = "nobody";
                for (const Value& s : at(*result, "seats").asList())
                    if (intAt(s, "empire") == intAt(*result, "winner", -1)) who = strAt(s, "ai") + " (" + strAt(s, "name") + ")";
                int64_t failures = 0;
                for (const Value& s : at(*result, "seats").asList()) failures += intAt(s, "failures");
                const std::string failed = failures ? std::format(", {} failed requests", failures) : std::string();
                std::printf("game %lld/%lld, seed %lld: %s won by %s after %lld turns%s (%.1f s)\n", static_cast<long long>(game + 1),
                            static_cast<long long>(*games), static_cast<long long>(*seed + game), who.c_str(), strAt(*result, "won_by").c_str(),
                            static_cast<long long>(intAt(*result, "turns_played")), failed.c_str(), secs);
                results[game] = std::move(*result);
            }
            std::fflush(stdout);
            running.erase(running.begin() + static_cast<std::ptrdiff_t>(i));
        }
        if (!running.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // In game order, whatever order they finished in, so the report is the same every time.
    for (auto& [game, result] : results) {
        addGame(result, run.totals);
        rateGame(result, run.totals);
        run.results.push_back(std::move(result));
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    writeReport(out, run, seconds);
    if (o.has("ratings")) writeRatings(o.get("ratings"), run.totals);
    printSummary(run, out, seconds);
    return run.failed.empty() ? 0 : 1;
}

} // namespace opense4::sdktool
