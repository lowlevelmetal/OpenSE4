// Golden checksums of games a rules mod changes (docs/sdk/rules.md,
// docs/MODDING_SDK.md §7.3, §14.4): four computer empires on the engine's test
// rules with the rules golden mod (tests/fixtures/mods/rules-golden): an
// overcrowding rule on every colony, a declared ability given an effect
// (Test Field Battery), and a dust storm event of the mod's own, for 60 turns
// in each turn style. Rules scripts run in the script runtime, whose results
// are the same on every computer, so these must hold on every platform CI
// builds, as tests/test_determinism.cpp's do.
//
// A mismatch: print the new values with
//     OPENSE4_PRINT_GOLDEN=1 ./opense4_tests -tc="sdk rules golden*" -s
// after a deliberate change to the rules, the hooks or the fixture mod;
// otherwise something differs between platforms (the engine, or the runtime:
// see docs/sdk/runtime.md "The same on every computer").

#include "rules_fixture.hpp"

#include "mod_fixture.hpp"

#include "game/serialize.hpp"
#include "game/turn.hpp"

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

const mods::Package& goldenMod() {
    static const mods::Package p = [] {
        auto opened = mods::openPackage(fixturePath() / "mods" / "rules-golden");
        REQUIRE_MESSAGE(opened.has_value(), (opened ? std::string{} : opened.error()));
        return std::move(*opened);
    }();
    return p;
}

const Rules& goldenRules() {
    static const Rules r{rulesRuleset({&goldenMod()})};
    return r;
}

struct Installed {
    Installed() {
        sdk::PlayerSetup setup;
        setup.mods.push_back(goldenMod());
        sdk::installPlayers(std::move(setup));
    }
    ~Installed() { sdk::uninstallPlayers(); }
    Installed(const Installed&) = delete;
    Installed& operator=(const Installed&) = delete;
};

GameState goldenGame(const Rules& r, uint64_t seed, bool simultaneous) {
    auto created = createGame(r, playersSetup(seed, simultaneous, std::vector<Controller>(4)));
    REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
    return std::move(*created);
}

int64_t counted(const GameState& s, std::string_view key) {
    const script::Value data = modDataIn(s.modData, "test.rules-golden");
    const script::Value* v = data.find(key);
    return v && v->isInt() ? v->asInt() : 0;
}

void playGolden(std::string_view name, uint64_t seed, bool simultaneous, std::span<const Milestone> golden) {
    Installed installed;
    const Rules& r = goldenRules();
    GameState s = goldenGame(r, seed, simultaneous);
    std::string printed;
    size_t next = 0;
    for (uint32_t t = 0;; ++t) {
        if (golden[next].turn == t) {
            const uint64_t sum = stateChecksum(s);
            printed += std::format("        {{{}, 0x{:016x}ull}},\n", t, sum);
            if (!printGolden())
                REQUIRE_MESSAGE(sum == golden[next].checksum,
                                std::format("{} game, after turn {}: the state is 0x{:016x}, expected 0x{:016x}", name, t, sum, golden[next].checksum));
            if (++next == golden.size()) break;
        }
        REQUIRE_FALSE(s.gameOver);
        processTurn(r, s, {});
        const ModRulesState* st = rulesStateOf(s, "test.rules-golden");
        REQUIRE(st != nullptr);
        REQUIRE_MESSAGE(st->failures == 0, name << " game: the rules failed on turn " << t);
    }
    if (printGolden())
        MESSAGE(std::format("{} game (seed {}):\n{}crowded {}, charged {}, storms {}", name, seed, printed, counted(s, "crowded"),
                            counted(s, "charged"), counted(s, "storms")));
    // The rules did their work: each of the three acted.
    CHECK(counted(s, "crowded") > 0);
    CHECK(counted(s, "charged") > 0);
    CHECK(counted(s, "storms") > 0);
}

} // namespace

TEST_CASE("sdk rules golden: a simultaneous game a rules mod changes gives the golden checksums") {
    static constexpr std::array<Milestone, 8> kGolden{{
        {0, 0x47f7f82ba79144cbull},
        {1, 0xcdbc7df72ba67522ull},
        {2, 0xfec010795b2a2a97ull},
        {5, 0x339165ea46a01542ull},
        {10, 0x11070d293dd750aeull},
        {20, 0xdfb1c1c39be4af1full},
        {40, 0xfe306a8044a0a63aull},
        {60, 0x89b2b58264771746ull},
    }};
    playGolden("simultaneous", 151, true, kGolden);
}

TEST_CASE("sdk rules golden: a turn-based game a rules mod changes gives the golden checksums") {
    static constexpr std::array<Milestone, 8> kGolden{{
        {0, 0xa21fbbfe6617c8faull},
        {1, 0x9faa5be7b8d0d182ull},
        {2, 0x9d707cb96db57c64ull},
        {5, 0xbefd0530b0c32642ull},
        {10, 0xc56371629af55880ull},
        {20, 0x158e34d1ba4ad11dull},
        {40, 0xc04d2e78b98728edull},
        {60, 0xb0fb83719a413019ull},
    }};
    playGolden("turn-based", 157, false, kGolden);
}

TEST_CASE("sdk rules golden: what a rules mod costs a turn (measured, not checked)") {
    // The golden game's 30 turns: without rules; with one hook that runs
    // once a turn (what starting the scripts costs each turn); and with the
    // golden mod's rules, which run on every colony, after every empire's
    // supply step and for the event.
    test::ModDir once("rules_once", "test.rules-once");
    once.file("scripts/once.py", "from opense4 import rules\n\n\n@rules.on(\"turn_end\")\ndef tick(game, fx):\n"
                                 "    game.mod_data[\"turns\"] = game.mod_data.get(\"turns\", 0) + 1\n");
    const mods::Package onceMod = once.open();
    const Rules onceRules(rulesRuleset({&onceMod}));
    auto time = [](const Rules& r, const mods::Package* mod, int64_t* calls) {
        std::optional<sdk::PlayerSetup> setup;
        if (mod) {
            setup.emplace();
            setup->mods.push_back(*mod);
            sdk::installPlayers(*setup);
        }
        GameState s = goldenGame(r, 151, true);
        const auto start = std::chrono::steady_clock::now();
        for (int t = 0; t < 30; ++t) processTurn(r, s, {});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 30;
        if (calls) {
            *calls = 0;
            for (const auto& c : s.colonies) *calls += c ? 1 : 0;   // colony_end_of_turn, per turn at the end
        }
        if (mod) sdk::uninstallPlayers();
        return ms;
    };
    int64_t colonies = 0;
    const double without = time(test::engineRules(), nullptr, nullptr);
    const double one = time(onceRules, &onceMod, nullptr);
    const double golden = time(goldenRules(), &goldenMod(), &colonies);
    MESSAGE(std::format("a turn: {:.2f} ms without rules; {:.2f} ms with one hook a turn ({:+.2f} ms); {:.2f} ms with the golden mod's "
                        "rules ({:+.2f} ms; {} colonies at the end)",
                        without, one, one - without, golden, golden - without, colonies));
}
