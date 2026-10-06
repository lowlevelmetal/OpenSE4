// The Hegemon computer player (mods/hegemon, docs: mods/hegemon/README.md).
//
// On our own fixture data (in CI): it plays simultaneous and turn-based games
// beside the built-in AI without a single failed request, keeps its memory
// plain and small, and plays the same way every time.
//
// On the player's installed data set (opt-in: OPENSE4_CLASSIC_DATA): a free-for-all
// against three built-in AIs in both turn styles, with no failed request, and what
// its requests cost a turn. OPENSE4_HEGEMON_TURNS=N plays longer games (to measure
// late-game costs in a release build).

#include "players_fixture.hpp"

#include "game/score.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "mods/data_set.hpp"
#include "mods/mod_set.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <map>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
namespace fs = std::filesystem;

namespace {

fs::path hegemonDir() { return fs::path(OPENSE4_DOCS_DIR).parent_path() / "mods" / "hegemon"; }

mods::Package hegemonMod() {
    auto p = mods::openPackage(hegemonDir());
    REQUIRE_MESSAGE(p.has_value(), (p ? std::string{} : p.error()));
    return std::move(*p);
}

Controller hegemon() {
    Controller c;
    c.kind = Controller::Kind::Script;
    c.mod = "opense4.hegemon";
    c.player = "Hegemon";
    return c;
}

// What the players' requests did in one game: failures (with why), the time each
// empire's player took in each game turn, the most bytecodes a request used (planning
// calls and the others), and per call its count, mean and worst time.
struct Watch {
    int failures = 0;
    std::vector<std::string> errors;
    std::map<std::pair<uint32_t, uint32_t>, double> ms;   // (empire, turn) -> milliseconds
    int64_t asked = 0;
    int64_t planningBudget = 0, callBudget = 0;
    struct PerCall {
        int64_t n = 0;
        double total = 0, worst = 0;
    };
    std::map<std::pair<uint32_t, std::string>, PerCall> calls;   // (empire, call)
};

sdk::PlayerSetup watched(Watch& watch) {
    sdk::PlayerSetup setup;
    setup.mods.push_back(hegemonMod());
    setup.observe = [&watch](const sdk::RequestEvent& e) {
        if (e.kind == sdk::RequestEvent::Kind::Failed) {
            ++watch.failures;
            if (watch.errors.size() < 5) watch.errors.emplace_back(e.error);
        } else if (e.kind == sdk::RequestEvent::Kind::Asked) {
            ++watch.asked;
            watch.ms[{e.empire.value, e.turn}] += std::chrono::duration<double, std::milli>(e.time).count();
            Watch::PerCall& pc = watch.calls[{e.empire.value, std::string(e.call)}];
            const double ms = std::chrono::duration<double, std::milli>(e.time).count();
            ++pc.n;
            pc.total += ms;
            pc.worst = std::max(pc.worst, ms);
            int64_t& peak = e.planning ? watch.planningBudget : watch.callBudget;
            peak = std::max(peak, e.budget);
        }
    };
    return setup;
}

// Within the budgets a game gives script players (docs/sdk/ai-protocol.md §7), with
// room to spare: a planning call's, and any other call's.
void checkLimits(const GameState& s, const Watch& w) {
    CHECK(w.planningBudget > 0);
    CHECK(w.planningBudget < s.options.aiPlanningBudget / 2);
    CHECK(w.callBudget < s.options.aiCallBudget / 2);
}

std::string allErrors(const Watch& w) {
    std::string out;
    for (const std::string& e : w.errors) out += e + "\n";
    return out;
}

void checkMemory(const GameState& s, EmpireId e) {
    const std::string& text = s.empire(e).script.memory;
    CHECK_FALSE(text.empty());
    CHECK(static_cast<int64_t>(text.size()) < s.options.aiMemoryLimit / 4);
}

std::optional<fs::path> installedData() {
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) return std::nullopt;
    return ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? fs::path{} : fs::path(env));
}

} // namespace

TEST_CASE("hegemon: plays fixture games beside the built-in AI without a failed request, the same way every time") {
    const Rules& r = test::engineRules();
    for (const bool simultaneous : {true, false}) {
        INFO("simultaneous " << simultaneous);
        uint64_t first = 0;
        for (int run = 0; run < 2; ++run) {
            Watch watch;
            InstalledPlayers installed(watched(watch), true);
            GameState s = playersGame(23, simultaneous, {hegemon(), Controller{}, hegemon(), Controller{}}, 16);
            for (int t = 0; t < 15; ++t) processTurn(r, s, {});
            INFO(allErrors(watch));
            CHECK(watch.failures == 0);
            CHECK(watch.asked > 30);
            checkLimits(s, watch);
            for (EmpireId e : {EmpireId{0u}, EmpireId{2u}}) {
                CHECK(s.empire(e).script.failures == 0);
                checkMemory(s, e);
            }
            const uint64_t sum = stateChecksum(s);
            if (run == 0) first = sum;
            else CHECK(sum == first);
        }
    }
}

TEST_CASE("hegemon: a free-for-all on the installed data set, in both turn styles, with no failed request (opt-in: OPENSE4_CLASSIC_DATA)") {
    const auto dir = installedData();
    if (!dir) {
        MESSAGE("skipped: set OPENSE4_CLASSIC_DATA to play Hegemon on an installed data set");
        return;
    }
    mods::ModSet set;
    set.packages.push_back(hegemonMod());
    const mods::LoadedDataSet loaded = mods::loadDataSet(dir->parent_path(), *dir, set);
    REQUIRE(loaded.ruleset);
    const Rules r(*loaded.ruleset);
    std::vector<std::string> races;
    for (const auto& p : r.racePresets())
        if (!p.neutral) races.push_back(p.folder);
    REQUIRE(races.size() >= 4);
    int turns = 40;
    if (const char* t = std::getenv("OPENSE4_HEGEMON_TURNS")) turns = std::max(1, std::atoi(t));
    for (const bool simultaneous : {true, false}) {
        INFO("simultaneous " << simultaneous);
        Watch watch;
        sdk::PlayerSetup setup = watched(watch);
        setup.mods.clear();   // the game's own mods hold it
        sdk::installPlayers(std::move(setup));
        GameSetup gs;
        gs.seed = 77;
        gs.options.systemCount = 40;
        gs.options.simultaneous = simultaneous;
        for (size_t i = 0; i < 4; ++i) {
            EmpireSetup e;
            e.preset = races[i];
            e.kind = PlayerKind::Computer;
            if (i == 1) e.controller = hegemon();
            gs.empires.push_back(e);
        }
        auto created = createGame(r, gs);
        REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
        GameState s = std::move(*created);
        for (int t = 0; t < turns && !s.gameOver; ++t) processTurn(r, s, {});
        sdk::uninstallPlayers();
        INFO(allErrors(watch));
        CHECK(watch.failures == 0);
        CHECK(s.empire(EmpireId{1u}).script.failures == 0);
        checkMemory(s, EmpireId{1u});
        checkLimits(s, watch);
        double total = 0, worst = 0;
        int n = 0;
        for (const auto& [key, ms] : watch.ms) {
            if (key.first != 1u) continue;
            total += ms;
            worst = std::max(worst, ms);
            ++n;
        }
        std::string perCall;
        for (const auto& [key, pc] : watch.calls)
            if (key.first == 1u)
                perCall += std::format(" {} {}x {:.1f}/{:.1f} ms;", key.second, pc.n, pc.n ? pc.total / static_cast<double>(pc.n) : 0.0, pc.worst);
        MESSAGE("Hegemon, " << std::string(simultaneous ? "simultaneous" : "turn-based") << ", " << s.turn << " turns: " << n << " turns asked, "
                            << (n ? total / n : 0.0) << " ms a turn on average, " << worst << " ms at most; at most "
                            << watch.planningBudget << " bytecodes in a planning call, " << watch.callBudget << " in another; memory "
                            << s.empire(EmpireId{1u}).script.memory.size() << " bytes; per call (mean/worst):" << perCall << " score "
                            << score::empireScore(r, s, EmpireId{1u}) << " (best built-in " << std::max({score::empireScore(r, s, EmpireId{0u}),
                                                                                                         score::empireScore(r, s, EmpireId{2u}),
                                                                                                         score::empireScore(r, s, EmpireId{3u})})
                            << ")");
    }
}
