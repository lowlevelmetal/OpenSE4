#include "sdk/match.hpp"

#include "core/log.hpp"
#include "game/players.hpp"
#include "mods/package.hpp"
#include "game/score.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "script/value.hpp"
#include "sdk/bots.hpp"
#include "sdk/process.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <memory>

namespace opense4::sdk {

using game::EmpireId;

std::vector<BattleOutcome> battleOutcomes(const game::GameState& s, uint32_t turn) {
    std::map<uint32_t, BattleOutcome> out;
    for (const game::CombatRecord& c : s.combats) {
        if (c.turn != turn) continue;
        auto kept = [&](EmpireId e) {
            return std::any_of(c.pieces.begin(), c.pieces.end(), [&](const game::CombatPiece& p) {
                return p.kind != game::CombatPiece::Kind::Seeker && p.kind != game::CombatPiece::Kind::Obstacle && p.survivor == e;
            });
        };
        for (EmpireId e : c.participants) {
            if (!e.valid()) continue;
            const bool mine = kept(e);
            const bool theirs = std::any_of(c.participants.begin(), c.participants.end(), [&](EmpireId o) { return o.valid() && o != e && kept(o); });
            BattleOutcome& b = out[e.value];
            b.empire = e;
            if (mine && !theirs) ++b.won;
            else if (!mine && theirs) ++b.lost;
            else ++b.drawn;
        }
    }
    std::vector<BattleOutcome> list;
    for (auto& [id, b] : out) list.push_back(b);
    return list;
}

namespace {

SeatTurn seatTurn(const game::Rules& r, const game::GameState& s, EmpireId e, uint32_t turn) {
    SeatTurn t;
    t.turn = turn;
    const game::Empire& emp = s.empire(e);
    t.alive = emp.alive;
    for (const auto& c : s.colonies)
        if (c && c->owner == e) ++t.colonies;
    if (!emp.alive) return t;
    const game::TurnStats st = game::score::currentStats(r, s, e);
    t.score = st.score;
    t.systems = st.systems;
    t.planets = st.planets;
    t.population = st.population;
    t.ships = st.ships;
    t.bases = st.bases;
    t.units = st.units;
    t.techLevels = st.techLevels;
    t.research = st.research;
    return t;
}

// What the match counts of its players' requests, per empire.
struct Counts {
    std::vector<SeatResult>* seats = nullptr;
    std::function<void(const RequestEvent&)> chained;
    // Per seat: the game turn being counted and its players' time so far.
    std::shared_ptr<std::vector<std::pair<uint32_t, std::chrono::nanoseconds>>> turnTime =
        std::make_shared<std::vector<std::pair<uint32_t, std::chrono::nanoseconds>>>();

    void operator()(const RequestEvent& ev) const {
        if (ev.empire.valid() && ev.empire.index() < seats->size()) {
            SeatResult& seat = (*seats)[ev.empire.index()];
            switch (ev.kind) {
                case RequestEvent::Kind::Asked: {
                    ++seat.requests;
                    seat.playerTime += ev.time;
                    if (turnTime->size() < seats->size()) turnTime->resize(seats->size());
                    auto& [turn, time] = (*turnTime)[ev.empire.index()];
                    if (turn != ev.turn) time = {};
                    turn = ev.turn;
                    time += ev.time;
                    seat.turnTimeMax = std::max(seat.turnTimeMax, time);
                    int64_t& peak = ev.planning ? seat.planningBudgetMax : seat.callBudgetMax;
                    peak = std::max(peak, ev.budget);
                    break;
                }
                case RequestEvent::Kind::Replayed: ++seat.replayed; break;
                case RequestEvent::Kind::Skipped: ++seat.fallbacks; break;
                case RequestEvent::Kind::Failed:
                    ++seat.failures;
                    ++seat.fallbacks;
                    if (seat.errors.size() < 10) seat.errors.push_back(std::format("turn {}: {}", ev.turn, ev.error));
                    break;
            }
        }
        if (chained) chained(ev);
    }
};

// The players' sessions for the length of the match.
struct Installed {
    explicit Installed(PlayerSetup setup) { installPlayers(std::move(setup)); }
    ~Installed() { uninstallPlayers(); }
    Installed(const Installed&) = delete;
    Installed& operator=(const Installed&) = delete;
};

} // namespace

std::expected<MatchResult, std::string> playMatch(const game::Rules& r, MatchSetup setup) {
    const auto started = std::chrono::steady_clock::now();
    if (setup.seats.size() != setup.game.empires.size())
        return std::unexpected(std::format("{} seats for {} empires", setup.seats.size(), setup.game.empires.size()));
    for (size_t i = 0; i < setup.seats.size(); ++i) {
        game::EmpireSetup& e = setup.game.empires[i];
        if (e.kind == game::PlayerKind::Human) e.kind = game::PlayerKind::Computer;
        e.controller = setup.seats[i].controller;
    }
    // The players of the data set's mods, and of mods a test gives the sessions.
    std::vector<mods::Package> packages(gamePackages(r).begin(), gamePackages(r).end());
    packages.insert(packages.end(), setup.players.mods.begin(), setup.players.mods.end());
    if (auto problems = checkControllers(setup.game.empires, packages); !problems.empty()) {
        std::string why;
        for (const std::string& p : problems) why += (why.empty() ? "" : "\n") + p;
        return std::unexpected(why);
    }
    auto created = game::createGame(r, setup.game);
    if (!created) return std::unexpected("the game could not be made: " + created.error());

    MatchResult result;
    result.seed = setup.game.seed;
    result.state = std::move(*created);
    game::GameState& s = result.state;
    for (size_t i = 0; i < s.empires.size(); ++i) {
        SeatResult seat;
        seat.empire = s.empires[i].id;
        seat.name = s.empires[i].name;
        seat.race = s.empires[i].race.name;
        result.seats.push_back(std::move(seat));
    }

    // External bots: a host on this computer, and each seat's own bot.
    const std::vector<uint32_t> slots = externalSlots(setup.game.empires);
    std::unique_ptr<BotHost> ownHost;
    BotHost* bots = setup.bots;
    std::vector<Process> botProcesses;
    if (!slots.empty() && !bots) {
        BotHostOptions o;
        o.slots = slots;
        o.gameName = std::format("match {}", setup.game.seed);
        o.requestTimeout = setup.botTimeout;
        auto opened = BotHost::open(std::move(o));
        if (!opened) return std::unexpected(opened.error());
        ownHost = std::move(*opened);
        bots = ownHost.get();
    }
    if (bots) bots->setRequestTimeout(setup.botTimeout);
    for (size_t i = 0; i < setup.seats.size(); ++i) {
        const Seat& seat = setup.seats[i];
        if (seat.controller.kind != game::Controller::Kind::External || seat.botCommand.empty()) continue;
        ProcessOptions po;
        po.shellCommand = seat.botCommand;
        po.environment = setup.botEnvironment;
        po.environment.emplace_back("OPENSE4_BOT_HOST", "127.0.0.1");
        po.environment.emplace_back("OPENSE4_BOT_PORT", std::to_string(bots->port()));
        po.environment.emplace_back("OPENSE4_BOT_TOKEN", bots->token());
        po.environment.emplace_back("OPENSE4_BOT_SLOT", std::to_string(seat.controller.slot));
        if (!setup.botLogDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(setup.botLogDir, ec);
            po.output = setup.botLogDir / std::format("bot-{}.log", seat.controller.slot);
        }
        auto p = Process::start(po);
        if (!p) return std::unexpected(std::format("the bot of seat {} could not start: {}", i + 1, p.error()));
        botProcesses.push_back(std::move(*p));
    }
    if (bots && !slots.empty() && !bots->waitFor(slots, setup.botConnectTimeout)) {
        std::string missing;
        for (uint32_t slot : slots)
            if (!bots->connected(slot)) missing += (missing.empty() ? "" : ", ") + std::to_string(slot);
        std::string why = std::format("no bot connected to external slot {} within {} s", missing,
                                      std::chrono::duration_cast<std::chrono::seconds>(setup.botConnectTimeout).count());
        if (!setup.botLogDir.empty()) why += std::format(" (the bots' output is in {})", setup.botLogDir.string());
        return std::unexpected(why);
    }

    {
        PlayerSetup players = std::move(setup.players);
        Counts counts{&result.seats, std::move(players.observe)};
        players.observe = counts;
        if (bots) players.externals = [bots](uint32_t slot) { return bots->bot(slot); };
        Installed installed(std::move(players));

        for (uint32_t played = 0; played < setup.turns && !s.gameOver; ++played) {
            const uint32_t turn = s.turn;
            std::vector<bool> aliveBefore;
            for (const game::Empire& e : s.empires) aliveBefore.push_back(e.alive);
            if (game::turnBased(s)) {
                // No human: each call plays one whole game turn.
                for (int guard = 0; guard < 4 && s.turn == turn && !s.gameOver; ++guard) game::resumeTurnBased(r, s);
            } else {
                game::processTurn(r, s, {});
            }
            ++result.turnsPlayed;
            const std::vector<BattleOutcome> battles = battleOutcomes(s, turn);
            for (size_t i = 0; i < result.seats.size(); ++i) {
                SeatResult& seat = result.seats[i];
                SeatTurn t = seatTurn(r, s, seat.empire, turn);
                for (const BattleOutcome& b : battles)
                    if (b.empire == seat.empire) {
                        t.battlesWon = b.won;
                        t.battlesLost = b.lost;
                        t.battlesDrawn = b.drawn;
                    }
                seat.battlesWon += t.battlesWon;
                seat.battlesLost += t.battlesLost;
                seat.battlesDrawn += t.battlesDrawn;
                if (aliveBefore[i] && !t.alive) seat.eliminated = turn;
                seat.turns.push_back(t);
            }
            if (setup.afterTurn && !setup.afterTurn(s)) break;
            const auto alive = std::count_if(s.empires.begin(), s.empires.end(), [](const game::Empire& e) { return e.alive; });
            if (alive <= 1) break;
        }
    }

    result.gameOver = s.gameOver;
    std::vector<EmpireId> living;
    for (const game::Empire& e : s.empires)
        if (e.alive) living.push_back(e.id);
    if (s.gameOver && s.winner.valid()) {
        result.winner = s.winner;
        result.winnerBy = "victory";
    } else if (living.size() == 1) {
        result.winner = living.front();
        result.winnerBy = "last standing";
    } else if (const std::vector<EmpireId> ranked = game::score::ranking(r, s); !ranked.empty()) {
        result.winner = ranked.front();
        result.winnerBy = "score";
    }
    result.checksum = game::stateChecksum(s);

    if (bots) {
        // The bots learn how it ended: the turn, the winner, every empire's score.
        script::ValueList scores;
        for (const SeatResult& seat : result.seats) {
            script::ValueMap one;
            one.emplace_back("empire", script::Value(static_cast<int64_t>(seat.empire.value)));
            one.emplace_back("alive", script::Value(s.empire(seat.empire).alive));
            one.emplace_back("score", script::Value(seat.turns.empty() ? int64_t{0} : seat.turns.back().score));
            scores.push_back(script::Value(std::move(one)));
        }
        script::ValueMap details;
        details.emplace_back("reason", script::Value(s.gameOver ? "the game is over" : "the match is over"));
        details.emplace_back("turn", script::Value(static_cast<int64_t>(s.turn)));
        details.emplace_back("game_over", script::Value(s.gameOver));
        details.emplace_back("winner", result.winner.valid() ? script::Value(static_cast<int64_t>(result.winner.value)) : script::Value());
        details.emplace_back("won_by", script::Value(result.winnerBy));
        details.emplace_back("scores", script::Value(std::move(scores)));
        bots->sayGoodbye(script::Value(std::move(details)));
    }
    for (Process& p : botProcesses)
        if (!p.wait(std::chrono::milliseconds(3000))) p.kill();
    result.time = std::chrono::steady_clock::now() - started;
    return result;
}

} // namespace opense4::sdk
