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

#include <doctest/doctest.h>

#include <array>
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
        {1, 0x66a03c96323cc001ull},
        {2, 0xa60b5ba6a0f5478bull},
        {5, 0x30a397c32dd210d7ull},
        {10, 0xcb18afdf78e327fdull},
        {20, 0x7e0f57c19bf660f8ull},
        {40, 0x854f76693d53616eull},
        {60, 0x17cbe1e024aa0a19ull},
    }};
    playGolden("simultaneous", 51, true, kGolden);
}

TEST_CASE("sdk players golden: a turn-based game two script players play gives the golden checksums") {
    static constexpr std::array<Milestone, 8> kGolden{{
        {0, 0xf911580bae3b9897ull},
        {1, 0xb9ed0d441a5f2670ull},
        {2, 0xd8d4cad49dc4c7e4ull},
        {5, 0x9f112a5cadfe7890ull},
        {10, 0xa7f6ee39719d8939ull},
        {20, 0xdd728cda427bfa76ull},
        {40, 0x76ffd32d3e0cf69full},
        {60, 0x0f7c734cb2c9b0daull},
    }};
    playGolden("turn-based", 46, false, kGolden);
}

