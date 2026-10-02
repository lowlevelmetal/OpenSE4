// Golden checksums: fixed games and battles on the test rules must end in
// exactly these states with every compiler, standard library and platform
// (GCC, Clang, MSVC and MinGW-w64; Linux and Windows, also under Wine). CI
// builds and runs this with each of them, so a difference shows up as a failing
// job rather than as a desync between players on different systems
// (docs/ENGINE.md, "Same on every platform").
//
// A mismatch means one of two things:
//   - the rules changed on purpose: print the new values with
//         OPENSE4_PRINT_GOLDEN=1 ./opense4_tests -tc="determinism*" -s
//     paste them here, and check that the other compilers agree (CI);
//   - the engine depends on something that differs between compilers or
//     libraries: a sort with ties, the evaluation order of two calls with side
//     effects, an unordered container, floating point... The message names the
//     first milestone that differs, and the hashes of the state's parts there
//     show where to look (compare them with a build that passes).

#include "combat_fixture.hpp"
#include "engine_fixture.hpp"

#include "game/combat.hpp"
#include "game/serialize.hpp"
#include "game/serialize_io.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace opense4;
using namespace opense4::game;

namespace {

bool printGolden() { return std::getenv("OPENSE4_PRINT_GOLDEN") != nullptr; }

// The hash of one part of the state, computed as stateChecksum hashes the whole.
template <class T>
uint64_t partHash(const T& part) {
    serial::HashWriter w;
    serial::io(w, const_cast<T&>(part));
    return w.value();
}

std::string partHashes(const GameState& s) {
    return std::format("galaxy {:016x}, colonies {:016x}, empires {:016x}, designs {:016x}, vehicles {:016x}, fleets {:016x}, "
                       "messages {:016x}, events {:016x}, combats {:016x}, rng {:016x}",
                       partHash(s.galaxy), partHash(s.colonies), partHash(s.empires), partHash(s.designs), partHash(s.vehicles),
                       partHash(s.fleets), partHash(s.messages), partHash(s.pendingEvents), partHash(s.combats), partHash(s.rng));
}

// Compares one checksum with its golden value (unless OPENSE4_PRINT_GOLDEN is
// set) and returns it.
uint64_t checkGolden(std::string_view what, const GameState& s, uint64_t expected) {
    const uint64_t sum = stateChecksum(s);
    if (printGolden()) return sum;
    INFO(what << ": " << partHashes(s));
    CHECK_MESSAGE(sum == expected, std::format("{}: the state is 0x{:016x}, expected 0x{:016x}", what, sum, expected));
    return sum;
}

// ---- Whole games -----------------------------------------------------------------------------------

struct Milestone {
    uint32_t turn;      // after this many turns (0: the new game)
    uint64_t checksum;  // stateChecksum
};

// What a game went through, so that the test proves it covered the subsystems.
struct Coverage {
    int battles = 0;         // space battles fought
    int eventEntries = 0;    // log entries of the Events category
    int intelTurns = 0;      // empire-turns with intelligence projects ordered
    int politics = 0;        // log entries of the Politics category (messages, treaties)
    int colonies = 0;        // at the end
    int vehicles = 0;        // at the end
};

// Four computer players in a small quadrant, with frequent events of every
// severity and finite resources.
GameSetup goldenSetup(uint64_t seed, bool simultaneous) {
    GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = 14;
    setup.options.simultaneous = simultaneous;
    setup.options.eventFrequency = 3;
    setup.options.maxEventSeverity = 3;
    setup.options.finiteResources = true;
    for (int i = 0; i < 4; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = PlayerKind::Computer;
        setup.empires.push_back(std::move(e));
    }
    return setup;
}

// Plays the game to its last milestone, checking the state at every one. The
// first one that differs stops the game: everything after it differs too.
Coverage playGolden(std::string_view name, uint64_t seed, bool simultaneous, std::span<const Milestone> golden) {
    const Rules& r = test::engineRules();
    auto created = createGame(r, goldenSetup(seed, simultaneous));
    REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
    GameState s = std::move(*created);

    std::string printed;
    Coverage cov;
    size_t next = 0;
    for (uint32_t t = 0;; ++t) {
        if (golden[next].turn == t) {
            const uint64_t sum = checkGolden(std::format("{} game, after turn {}", name, t), s, golden[next].checksum);
            printed += std::format("        {{{}, 0x{:016x}ull}},\n", t, sum);
            if (!printGolden()) REQUIRE(sum == golden[next].checksum);
            if (++next == golden.size()) break;
        }
        REQUIRE_FALSE(s.gameOver);
        processTurn(r, s, {});
        // The turn just played: its battles (a turn-based game's records also
        // keep the turn before's) and its log entries.
        for (const CombatRecord& c : s.combats) cov.battles += c.turn + 1 == s.turn;
        for (const Empire& e : s.empires) {
            cov.intelTurns += !e.intel.empty();
            for (const LogEntry& l : e.log) {
                if (l.turn + 1 != s.turn) continue;
                cov.eventEntries += l.category == LogCategory::Events;
                cov.politics += l.category == LogCategory::Politics;
            }
        }
    }
    for (const auto& c : s.colonies) cov.colonies += c.has_value();
    cov.vehicles = static_cast<int>(s.vehicles.size());
    if (printGolden())
        MESSAGE(std::format("{} game (seed {}):\n{}battles {}, events {}, intelligence {}, politics {}, colonies {}, vehicles {}", name,
                            seed, printed, cov.battles, cov.eventEntries, cov.intelTurns, cov.politics, cov.colonies, cov.vehicles));
    return cov;
}

void checkCoverage(const Coverage& c) {
    CHECK(c.battles >= 5);
    CHECK(c.eventEntries >= 10);
    CHECK(c.intelTurns > 0);
    CHECK(c.politics > 0);
    CHECK(c.colonies > 20);
    CHECK(c.vehicles > 8);
}

} // namespace

TEST_CASE("determinism: a simultaneous game gives the golden checksums") {
    static constexpr std::array<Milestone, 10> kGolden{{
        {0, 0x7d5e7c73c1e126cfull},
        {1, 0x037bc09275e4ab22ull},
        {2, 0x540d84efdb44fbbfull},
        {5, 0x7b3e6e1984e2119eull},
        {10, 0xb3d161dbddbefe8bull},
        {20, 0x0ad7cde13ee30a7dull},
        {40, 0x957d98bbd3d07d9full},
        {60, 0xb7831ee6d2db6b81ull},
        {80, 0x7d8a5161fb9f5401ull},
        {100, 0x08961ba5d14f3f49ull},
    }};
    checkCoverage(playGolden("simultaneous", 11, true, kGolden));
}

TEST_CASE("determinism: a turn-based game gives the golden checksums") {
    static constexpr std::array<Milestone, 10> kGolden{{
        {0, 0xba0f8539e6ac0e5eull},
        {1, 0x2c274f33d9a42afcull},
        {2, 0xb2ced2259b0e9082ull},
        {5, 0x38ae2bc0486c9511ull},
        {10, 0x0f9120dc049b2b1aull},
        {20, 0xfef0ea893f996cb0ull},
        {40, 0x7b142cd8fc0faf0eull},
        {60, 0x592242a816b7ca99ull},
        {80, 0x5755fd889d5477d1ull},
        {100, 0xad17e6c64b9a5542ull},
    }};
    checkCoverage(playGolden("turn-based", 11, false, kGolden));
}

// ---- Battles ---------------------------------------------------------------------------------------

// The combat fixture's varied battles (fleets, fighters, drones, seekers and
// point defence, boarding, ramming, tractors, crew conversion, a third empire,
// an invasion of a planet with platforms) fought by the strategies, then the
// ground combat on the invaded planet.
TEST_CASE("determinism: varied battles give the golden checksums") {
    static constexpr std::array<uint64_t, 10> kGolden{{
        0xac0be65c5cf23de5ull,
        0xa2285c32e391c6bdull,
        0x5d8a3fff53ab1818ull,
        0xd8a03907c405b3d1ull,
        0x06d45b58595a9d37ull,
        0x870e9355bef24031ull,
        0x73f4e04f830c3c64ull,
        0xa4ae4eb4c5c5765eull,
        0x50e888c00d97201dull,
        0x41837b13e5bcb6f8ull,
    }};
    std::string printed;
    int fought = 0, landed = 0;
    for (size_t variant = 0; variant < kGolden.size(); ++variant) {
        auto [s, where] = ctest::battleScenario(static_cast<int>(variant), 5);
        TurnContext ctx = ctest::context(s);
        combat::resolveSpaceCombat(ctx, where);
        fought += static_cast<int>(s.combats.size());
        for (const CombatRecord& c : s.combats) landed += static_cast<int>(c.grounds.size());
        for (const Empire& e : s.empires) combat::runGroundCombat(ctx, e.id);
        const uint64_t sum = checkGolden(std::format("battle variant {}", variant), s, kGolden[variant]);
        printed += std::format("        0x{:016x}ull,\n", sum);
    }
    if (printGolden()) MESSAGE(std::format("battles:\n{}battles {}, landings {}", printed, fought, landed));
    CHECK(fought == static_cast<int>(kGolden.size()));
    CHECK(landed > 0);
}
