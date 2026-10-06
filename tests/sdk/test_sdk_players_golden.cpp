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

#include "players_fixture.hpp"

#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "sdk/view.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
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
        {0, 0xe3335b8208255bb7ull},
        {1, 0x5f3e6d84ea051671ull},
        {2, 0x49850fec64bc587bull},
        {5, 0x15e3a65c2b198fa7ull},
        {10, 0x6c32b6c6e244546dull},
        {20, 0x8ccb36baa259da98ull},
        {40, 0x84a591115644d6feull},
        {60, 0xda4d405bf8fe09f9ull},
    }};
    playGolden("simultaneous", 51, true, kGolden);
}

TEST_CASE("sdk players golden: a turn-based game two script players play gives the golden checksums") {
    static constexpr std::array<Milestone, 8> kGolden{{
        {0, 0xf911580bae3b9897ull},
        {1, 0xf1dcc560845b5260ull},
        {2, 0x2c95e08eb8bc1344ull},
        {5, 0xab3cf002daef4d90ull},
        {10, 0x3e529d4a31b022d9ull},
        {20, 0x48f55427dae64166ull},
        {40, 0xf0afb19dca07dacfull},
        {60, 0x1513138813b49daaull},
    }};
    playGolden("turn-based", 46, false, kGolden);
}


// What a script empire whose player does nothing costs a turn: the session
// (the interpreter, the package and the mod imported), the view built and
// converted for each planning call, the requests. Against the same empire
// played by nobody (a human without orders). Run it in a release build:
//     OPENSE4_SDK_BENCH=1 ./opense4_tests -tc="sdk players bench*"
TEST_CASE("sdk players bench: the cost of a script empire that does nothing" * doctest::skip(std::getenv("OPENSE4_SDK_BENCH") == nullptr)) {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    constexpr int kTurns = 30;
    auto play = [&](bool script) {
        GameState s = playersGame(51, true, {Controller{}, Controller{}, Controller{}, Controller{}});
        if (script) s.empire(EmpireId{1u}).controller = scriptPlayer("Idle");
        else s.empire(EmpireId{1u}).kind = PlayerKind::Human;
        TurnOptions o;
        o.aiForMissing = false;
        for (int t = 0; t < 3; ++t) processTurn(r, s, {}, o);   // past the first turn's caches
        const auto start = std::chrono::steady_clock::now();
        for (int t = 0; t < kTurns; ++t) processTurn(r, s, {}, o);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return std::pair{ms / kTurns, s};
    };
    const auto [nobody, quiet] = play(false);
    const auto [idle, scripted] = play(true);
    // The view alone, three times a turn, as the planning calls build it.
    const auto start = std::chrono::steady_clock::now();
    size_t nodes = 0;
    for (int t = 0; t < kTurns * 3; ++t) nodes += sdk::buildView(r, scripted, EmpireId{1u}).size();
    const double view = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kTurns;
    MESSAGE(std::format("a turn: {:.3f} ms with the empire played by nobody, {:.3f} ms by an idle script player: {:.3f} ms more; "
                        "of it {:.3f} ms building its three views ({} galaxy systems, {} vehicles)",
                        nobody, idle, idle - nobody, view, scripted.galaxy.systems.size(), scripted.vehicles.size()));
    CHECK(nodes > 0);
}
