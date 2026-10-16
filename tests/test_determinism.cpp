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
        {0, 0xe9905a70b1dc72a7ull},
        {1, 0xf6c89a1e3aec91a8ull},
        {2, 0x93ef4e29e2ba2ecbull},
        {5, 0x69a046e0aabbed7aull},
        {10, 0x6b89cfd0fa46719eull},
        {20, 0xbecf5a54b89f16bdull},
        {40, 0xd9120704c1033339ull},
        {60, 0x42984f001eb7f8b2ull},
        {80, 0x6499f3eab64e1abbull},
        {100, 0xbb06426a3aa7783dull},
    }};
    checkCoverage(playGolden("simultaneous", 38, true, kGolden));
}

TEST_CASE("determinism: a turn-based game gives the golden checksums") {
    static constexpr std::array<Milestone, 10> kGolden{{
        {0, 0x202c8f4c2d020ebeull},
        {1, 0xd5ae0f7192622e1full},
        {2, 0x5e07a709e2b91704ull},
        {5, 0xbcc0de51c114a250ull},
        {10, 0x89b2769a8b786b7eull},
        {20, 0x4cf40dcb070dba09ull},
        {40, 0x6405235cf7019f50ull},
        {60, 0xc8e5738ba1afb729ull},
        {80, 0x8b3f1d5955001061ull},
        {100, 0x2687582d07f6ac4cull},
    }};
    checkCoverage(playGolden("turn-based", 42, false, kGolden));
}

// ---- The movement observer -------------------------------------------------------------------------

// Seeing each group's steps as movement makes them (TurnOptions::movementStep,
// the client's movement animation, spec 06 §2.4) changes nothing in the game:
// the same games, watched and not, come out the same turn after turn. Each
// step is reported before the group moves, from where its members stand.
TEST_CASE("determinism: the movement observer changes nothing in the game") {
    const Rules& r = test::engineRules();
    for (const bool simultaneous : {true, false}) {
        CAPTURE(simultaneous);
        auto created = createGame(r, goldenSetup(41, simultaneous));
        REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
        GameState plain = *created;
        GameState watched = std::move(*created);
        size_t steps = 0, jumps = 0, wrongFrom = 0, wrongDay = 0, wrongHeading = 0, empty = 0;
        TurnOptions options;
        options.movementStep = [&](const MovementStep& st) {
            if (st.members.empty()) ++empty;
            if (st.kind == MovementStep::Kind::MinesStruck) {
                // Mines striking a group where it stands, no move.
                if (st.from != st.to || st.warp.valid()) ++wrongFrom;
                return;
            }
            ++steps;
            for (const VehicleId id : st.members)
                if (const Vehicle* v = watched.vehicle(id); !v || v->location != st.from || v->owner != st.owner) ++wrongFrom;
            if (simultaneous ? st.day < 1 || st.day > 30 : st.day != 0) ++wrongDay;
            if (st.to.system != st.from.system) {
                ++jumps;
                if (!st.warp.valid()) ++wrongHeading;
            } else if (st.warp.valid() || st.heading != movement::headingFor(st.from.sector, st.to.sector)) {
                ++wrongHeading;
            }
        };
        for (int t = 0; t < 15; ++t) {
            processTurn(r, plain, {});
            processTurn(r, watched, {}, options);
            REQUIRE_MESSAGE(stateChecksum(plain) == stateChecksum(watched), std::format("after turn {}", t + 1));
        }
        CHECK(steps > 100);
        CHECK(jumps > 0);
        CHECK(empty == 0);
        CHECK(wrongFrom == 0);
        CHECK(wrongDay == 0);
        CHECK(wrongHeading == 0);
    }
}

// ---- Battles ---------------------------------------------------------------------------------------

// The combat fixture's varied battles (fleets, fighters, drones, seekers and
// point defence, boarding, ramming, tractors, crew conversion, a third empire,
// an invasion of a planet with platforms) fought by the strategies, then the
// ground combat on the invaded planet.
TEST_CASE("determinism: varied battles give the golden checksums") {
    static constexpr std::array<uint64_t, 10> kGolden{{
        0xb45aac3ede0ab72aull,
        0x9cb8c6c3671d5ecdull,
        0xd87c43b74a8bb1ceull,
        0x8a71dc5b98431b14ull,
        0xfbc5cfe635c41803ull,
        0x658132d4b12ff02full,
        0xddbf7f811830be8eull,
        0x23015ed9a4c18ef9ull,
        0x20d142269ea9900dull,
        0x9ea4c9c4636ad13aull,
    }};
    std::string printed;
    int fought = 0, landed = 0;
    for (size_t variant = 0; variant < kGolden.size(); ++variant) {
        auto [s, where] = ctest::battleScenario(static_cast<int>(variant), 19);
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
