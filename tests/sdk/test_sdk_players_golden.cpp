// Golden checksums of games that script computer players play (docs/sdk/ai-protocol.md,
// docs/MODDING_SDK.md §14.4): two empires played by the fixture mod's Steady
// player, two by the built-in AI, for 60 turns in each turn style. The
// players run in the script runtime, whose results are the same on every
// computer, so these must hold on every platform CI builds (Linux, Windows,
// macOS, 64-bit and 32-bit ARM), as tests/test_determinism.cpp's do.
//
// A mismatch: print the new values with
//     OPENSE4_PRINT_GOLDEN=1 ./opense4_tests -tc="sdk players golden*" -s
// after a deliberate change to the rules, the protocol or the fixture
// players; otherwise something differs between platforms (the engine, or
// the runtime: see docs/sdk/runtime.md "The same on every computer").

#include "mod_fixture.hpp"
#include "players_fixture.hpp"

#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "sdk/view.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <optional>
#include <tuple>
#include <span>
#include <string>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;

namespace {

bool printGolden() { return std::getenv("OPENSE4_PRINT_GOLDEN") != nullptr; }

struct Milestone {
    uint32_t turn;
    uint64_t checksum;
};

void playGolden(std::string_view name, uint64_t seed, bool simultaneous, std::span<const Milestone> golden) {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    GameState s = playersGame(seed, simultaneous, {scriptPlayer("Steady"), Controller{}, scriptPlayer("Steady"), Controller{}});
    std::string printed;
    size_t next = 0;
    int battles = 0, scriptBattles = 0;
    for (uint32_t t = 0;; ++t) {
        if (golden[next].turn == t) {
            const uint64_t sum = stateChecksum(s);
            printed += std::format("        {{{}, 0x{:016x}ull}},\n", t, sum);
            if (!printGolden()) REQUIRE_MESSAGE(sum == golden[next].checksum, std::format("{} game, after turn {}: the state is 0x{:016x}, expected 0x{:016x}",
                                                                                        name, t, sum, golden[next].checksum));
            if (++next == golden.size()) break;
        }
        REQUIRE_FALSE(s.gameOver);
        processTurn(r, s, {});
        for (const CombatRecord& c : s.combats) {
            if (c.turn + 1 != s.turn) continue;
            ++battles;
            for (EmpireId e : c.participants) scriptBattles += e == EmpireId{0u} || e == EmpireId{2u} ? 1 : 0;
        }
        for (EmpireId e : {EmpireId{0u}, EmpireId{2u}}) REQUIRE_MESSAGE(s.empire(e).script.failures == 0, s.empire(e).name << " failed on turn " << t);
    }
    if (printGolden()) MESSAGE(std::format("{} game (seed {}):\n{}battles {} (script sides {})", name, seed, printed, battles, scriptBattles));
    // The players played: they planned every turn, founded colonies with
    // their own types, fought battle rounds of their own and kept out of a
    // sector now and then.
    int colonies = 0, rounds = 0, declined = 0;
    for (EmpireId e : {EmpireId{0u}, EmpireId{2u}}) {
        const script::Value m = memoryOf(s, e);
        CHECK(intIn(m, "plans") == static_cast<int64_t>(golden.back().turn));
        colonies += static_cast<int>(intIn(m, "colonies"));
        rounds += static_cast<int>(intIn(m, "battles"));
        declined += static_cast<int>(intIn(m, "declined"));
    }
    CHECK(colonies > 0);
    CHECK(scriptBattles > 0);
    CHECK(rounds > 0);
    CHECK(declined > 0);
    (void)battles;
}

} // namespace

TEST_CASE("sdk players golden: a simultaneous game two script players play gives the golden checksums") {
    static constexpr std::array<Milestone, 8> kGolden{{
        {0, 0xc7c9ef95c17cf5dbull},
        {1, 0xcc8bd5713ebf48feull},
        {2, 0x82787cd1619d4b40ull},
        {5, 0x9902eee9b68d7c5full},
        {10, 0x2ca9e298070c1c65ull},
        {20, 0x3eb4db537442ceb9ull},
        {40, 0xb5b36e1158a9f502ull},
        {60, 0xf2071fcd672edc9full},
    }};
    playGolden("simultaneous", 51, true, kGolden);
}

TEST_CASE("sdk players golden: a turn-based game two script players play gives the golden checksums") {
    static constexpr std::array<Milestone, 8> kGolden{{
        {0, 0x900ff020b01d01d3ull},
        {1, 0x7e28805585c72bdfull},
        {2, 0xf6b3a65a81cb535eull},
        {5, 0x47c708e9b9129375ull},
        {10, 0xb7211059e819efbaull},
        {20, 0x67c2849380812c83ull},
        {40, 0x3f185b425b3fe912ull},
        {60, 0x739befbab43e6874ull},
    }};
    playGolden("turn-based", 46, false, kGolden);
}


// What a script empire whose player does nothing costs a turn: the session
// (the interpreter, the package and the mod imported), the view built and
// converted for each planning call, the requests. Against the same empire
// played by nobody (a human without orders). Two players that do nothing: one
// written straight to the protocol (the fixture's Idle, with the stand-in
// package), one written with OpenSE4's own package (it wraps the view); and
// what the classic AI's bookkeeping for the empire adds (the packaged player
// again with classic_state = false). Run it in a release build:
//     OPENSE4_SDK_BENCH=1 ./opense4_tests -tc="sdk players bench*"
TEST_CASE("sdk players bench: the cost of a script empire that does nothing" * doctest::skip(std::getenv("OPENSE4_SDK_BENCH") == nullptr)) {
    const Rules& r = test::engineRules();
    constexpr int kTurns = 30;
    test::ModDir bystander("bench", "test.bystander");
    test::writeText(bystander.root / "mod.toml",
              "[mod]\nid = \"test.bystander\"\nname = \"Bystander\"\nversion = \"1.0\"\napi = 1\n"
              "[[ai.players]]\nname = \"Bystander\"\nmodule = \"bystander\"\nclass = \"Bystander\"\n"
              "[[ai.players]]\nname = \"Hermit\"\nmodule = \"bystander\"\nclass = \"Bystander\"\nclassic_state = false\n");
    bystander.file("ai/bystander.py", "from opense4 import ai\n\n\nclass Bystander(ai.Player):\n"
                                      "    def politics(self, view, orders):\n        pass\n\n"
                                      "    def orders(self, view, orders):\n        pass\n\n"
                                      "    def economy(self, view, orders):\n        pass\n");
    auto play = [&](std::optional<Controller> player) {
        GameState s = playersGame(51, true, {Controller{}, Controller{}, Controller{}, Controller{}});
        if (player) s.empire(EmpireId{1u}).controller = *player;
        else s.empire(EmpireId{1u}).kind = PlayerKind::Human;
        TurnOptions o;
        o.aiForMissing = false;
        for (int t = 0; t < 3; ++t) processTurn(r, s, {}, o);   // past the first turns' caches
        const auto start = std::chrono::steady_clock::now();
        for (int t = 0; t < kTurns; ++t) processTurn(r, s, {}, o);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        CHECK(s.empire(EmpireId{1u}).script.failures == 0);
        return std::pair{ms / kTurns, s};
    };
    double nobody = 0, idle = 0, packaged = 0, hermit = 0;
    GameState scripted;
    {
        InstalledPlayers installed;
        nobody = play(std::nullopt).first;
        std::tie(idle, scripted) = play(scriptPlayer("Idle"));
    }
    {
        sdk::PlayerSetup setup;
        setup.mods.push_back(bystander.open());
        InstalledPlayers installed(std::move(setup), true);
        Controller c;
        c.kind = Controller::Kind::Script;
        c.mod = "test.bystander";
        c.player = "Bystander";
        packaged = play(c).first;
        // The same player without the classic AI's bookkeeping (classic_state = false).
        c.player = "Hermit";
        hermit = play(c).first;
    }
    // The view alone, three times a turn, as the planning calls build it.
    const auto start = std::chrono::steady_clock::now();
    size_t nodes = 0;
    for (int t = 0; t < kTurns * 3; ++t) nodes += sdk::buildView(r, scripted, EmpireId{1u}).size();
    const double view = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kTurns;
    MESSAGE(std::format("a turn: {:.3f} ms with the empire played by nobody; {:.3f} ms more with a player straight to the protocol, "
                        "{:.3f} ms more with one written with the opense4 package; of it {:.3f} ms building its three views "
                        "({} systems, {} vehicles), and {:.3f} ms the classic AI's bookkeeping (the same player with classic_state = false)",
                        nobody, idle - nobody, packaged - nobody, view, scripted.galaxy.systems.size(), scripted.vehicles.size(), packaged - hermit));
    CHECK(nodes > 0);
}
