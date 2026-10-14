// The game's flow as the classic client plays it (docs/spec/05 §6, docs/spec/06
// §1.1, §1.2, §1.2.1, §1.7, §6.1, §7 Q83, Q99): the endings of a turn-based
// game's last turn and of a human destroyed at its turn start, the sign-in a
// loaded game asks for, the passwords as the sign-in windows compare them,
// the autosave before the first human's turn, Load Game's sorting, Save
// Game's names, and a simultaneous turn processed in the background.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/finale.hpp"
#include "client/classic/screens/file_dialog.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/session.hpp"
#include "game/ai.hpp"
#include "game/score.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
namespace classic = opense4::client::classic;
namespace fs = std::filesystem;
using classic::FinaleKind;
using classic::SessionKind;

namespace {

constexpr EmpireId kA{0u};
constexpr EmpireId kB{1u};

std::shared_ptr<const Rules> sharedRules() { return std::shared_ptr<const Rules>(&engineRules(), [](const Rules*) {}); }

GameState newGame(bool simultaneous, bool allHuman) {
    GameState s = newEngineGame(7, 2, 12, allHuman);
    s.options.simultaneous = simultaneous;
    return s;
}

void loseEverything(GameState& s, EmpireId e) {
    for (auto& c : s.colonies)
        if (c && c->owner == e) c.reset();
    for (Vehicle& v : s.vehicles)
        if (v.owner == e) v.count = 0;
    s.removeDeadVehicles();
}

classic::FileEntry entry(std::string name, int minutesAgo) {
    classic::FileEntry f;
    f.path = name + ".gam";
    f.name = std::move(name);
    static const auto now = std::filesystem::file_time_type::clock::now();
    f.modified = now - std::chrono::minutes(minutesAgo);
    return f;
}

std::vector<std::string> names(const std::vector<classic::FileEntry>& files) {
    std::vector<std::string> out;
    for (const classic::FileEntry& f : files) out.push_back(f.name);
    return out;
}

} // namespace

TEST_CASE("game flow: a turn-based victory shows Victory at each human's last turn, then nothing but Game Completed") {
    // Spec 05 §6 (confirmed: binary): the victory check marks the game
    // completed and every player plays one more game turn, the humans after
    // the Victory ending; when it wraps the game is over.
    GameState s = newGame(false, true);
    s.lastTurn = true;
    s.playerTurn = PlayerTurn{kA, true, {}, {}};
    CHECK(classic::finaleKinds(s, kA, SessionKind::Hotseat) == std::vector<FinaleKind>{FinaleKind::Victory});
    // Each human sees it once, at its own turn (a hotseat game's watch is per player).
    classic::FinaleWatch watch;
    CHECK(watch.update(s, kA, SessionKind::Hotseat) == std::vector<FinaleKind>{FinaleKind::Victory});
    s.playerTurn = PlayerTurn{kB, true, {}, {}};
    CHECK(watch.update(s, kB, SessionKind::Hotseat) == std::vector<FinaleKind>{FinaleKind::Victory});
    // The last turn is over: no ending window; the client's Game Completed box comes instead.
    s.gameOver = true;
    s.playerTurn = PlayerTurn{};
    CHECK(classic::finaleKinds(s, kA, SessionKind::Hotseat).empty());
    CHECK(classic::finaleKinds(s, EmpireId{}, SessionKind::NetworkClient).empty());
    // A simultaneous game's victory ends it at once (today's behaviour, spec 05 open question).
    GameState sim = newGame(true, true);
    sim.gameOver = true;
    CHECK(classic::finaleKinds(sim, kA, SessionKind::Local) == std::vector<FinaleKind>{FinaleKind::Victory});
}

TEST_CASE("game flow: a turn-based human destroyed at its turn start sees Lose in its last turn, then Human Dead") {
    // Spec 06 §7 Q83, Q99 (confirmed: binary).
    const Rules& r = engineRules();
    GameState s = newGame(false, false);   // A human, B a computer player
    s.empire(kB).kind = PlayerKind::Computer;
    loseEverything(s, kA);
    resumeTurnBased(r, s);
    // A's turn started: it is destroyed, the Lose mark set, and it plays the turn.
    REQUIRE(s.playerTurn.empire == kA);
    CHECK_FALSE(s.empire(kA).alive);
    CHECK(playsLastTurn(s, kA));
    CHECK(ai::anyHumanLeft(s));
    CHECK(classic::finaleKinds(s, kA, SessionKind::Local) == std::vector<FinaleKind>{FinaleKind::Lose});
    // Its End Turn: no human is left, and the local game ends there.
    endPlayerTurn(r, s, kA, LiveOptions{{}, true}, nullptr);
    CHECK_FALSE(ai::anyHumanLeft(s));
    CHECK(classic::finaleKinds(s, kA, SessionKind::Local) == std::vector<FinaleKind>{FinaleKind::HumanDead});
}

TEST_CASE("game flow: the sign-in windows compare passwords as the original does, as far as a hash allows") {
    // Spec 06 §1.1, §1.2.1 (confirmed: binary): Next Player ignores the spaces
    // around the password and keeps letter case; Login To Game ignores both.
    GameState s = newGame(true, true);
    s.empire(kA).passwordHash = hashPassword("secret");
    s.empire(kB).passwordHash = hashPassword("Secret");
    classic::ClassicSession session(sharedRules(), s, kA, SessionKind::Hotseat);
    const Empire& a = session.state().empire(kA);
    const Empire& b = session.state().empire(kB);
    CHECK(*session.passwordMatches(a, "secret"));
    CHECK(*session.passwordMatches(a, "  secret "));
    CHECK_FALSE(*session.passwordMatches(a, "SECRET"));
    CHECK_FALSE(*session.passwordMatches(a, ""));
    CHECK(*session.loginPasswordMatches(a, " SeCrEt "));
    // A password set with capitals: only those capitals open it (OpenSE4 keeps its hash only).
    CHECK(*session.loginPasswordMatches(b, "Secret"));
    CHECK(*session.loginPasswordMatches(b, " Secret"));
    CHECK_FALSE(*session.loginPasswordMatches(b, "secret"));
    // "master" in any letter case counts as no password in Next Player.
    CHECK(session.passwordAsked(a));
    Empire master = a;
    master.passwordHash = hashPassword("MaStEr");
    CHECK_FALSE(session.passwordAsked(master));
    // Login To Game's Game Master row: a local game has no master password, so an empty entry.
    CHECK(*session.masterLoginMatches(""));
    CHECK(*session.masterLoginMatches("   "));
    CHECK_FALSE(*session.masterLoginMatches("anything"));
}

TEST_CASE("game flow: a loaded game asks for a sign-in: Login To Game when simultaneous, Next Player for a turn-based autosave") {
    // Spec 06 §1.1, spec 08 §5 (confirmed: binary).
    TempDir dir("gameflow_signin");
    const fs::path plain = dir.path() / "plain.gam";
    const fs::path autosave = dir.path() / "AutoSav1.gam";
    {
        GameState s = newGame(false, true);
        classic::ClassicSession session(sharedRules(), s, kA, SessionKind::Hotseat);
        CHECK(session.takeSignIn() == classic::ClassicSession::SignIn::None);
        CHECK_FALSE(session.fromSave());
        REQUIRE(session.save(plain, "plain"));
        SaveInfo info;
        info.gameName = "AutoSav1";
        info.autosave = true;
        REQUIRE(saveGame(autosave, session.state(), info));
    }
    auto loaded = classic::ClassicSession::load(sharedRules(), plain);
    REQUIRE(loaded);
    CHECK((*loaded)->fromSave());
    CHECK_FALSE((*loaded)->loadedInfo().autosave);
    CHECK((*loaded)->takeSignIn() == classic::ClassicSession::SignIn::None);
    auto fromAutosave = classic::ClassicSession::load(sharedRules(), autosave);
    REQUIRE(fromAutosave);
    CHECK((*fromAutosave)->loadedInfo().autosave);
    CHECK((*fromAutosave)->takeSignIn() == classic::ClassicSession::SignIn::NextPlayer);
    CHECK((*fromAutosave)->takeSignIn() == classic::ClassicSession::SignIn::None);   // taken once

    const fs::path sim = dir.path() / "sim.gam";
    {
        classic::ClassicSession session(sharedRules(), newGame(true, true), kA, SessionKind::Hotseat);
        REQUIRE(session.save(sim, "sim"));
    }
    auto simultaneous = classic::ClassicSession::load(sharedRules(), sim);
    REQUIRE(simultaneous);
    CHECK((*simultaneous)->takeSignIn() == classic::ClassicSession::SignIn::Login);
    CHECK((*simultaneous)->dataSetMatches());
}

TEST_CASE("game flow: a turn-based game's autosave holds the game before the first human's turn starts") {
    // Spec 06 §6.1 (confirmed: binary): written at the start of the first
    // human player's turn of each new game turn, before that turn's start;
    // the file is marked as an autosave, and loading it starts that turn.
    GameState s = newGame(false, true);
    s.options.autosaveTurns = 1;
    classic::ClassicSession session(sharedRules(), s, kA, SessionKind::Hotseat);
    REQUIRE(session.myTurn());
    const uint32_t t0 = session.state().turn;
    const fs::path file = classic::savesDir() / (*classic::setup::autosaveName(1, t0 + 1) + ".gam");
    std::error_code ec;
    fs::remove(file, ec);
    session.endTurn();   // A's: B's turn comes, no autosave in this game turn
    CHECK(session.player() == kB);
    CHECK_FALSE(fs::exists(file));
    session.endTurn();   // B's: the game turn ends, A's turn starts in the next
    REQUIRE(session.state().turn == t0 + 1);
    REQUIRE(fs::exists(file));
    auto saved = loadGame(file);
    REQUIRE(saved);
    CHECK(saved->second.autosave);
    CHECK(saved->first.turn == t0 + 1);
    CHECK(saved->first.playerTurn.empire == kA);
    CHECK_FALSE(saved->first.playerTurn.started);
    // Loading it starts A's turn as it started here, and asks for A's sign-in.
    auto loaded = classic::ClassicSession::load(sharedRules(), file);
    REQUIRE(loaded);
    CHECK((*loaded)->myTurn());
    CHECK((*loaded)->takeSignIn() == classic::ClassicSession::SignIn::NextPlayer);
    CHECK(stateChecksum((*loaded)->state()) == stateChecksum(session.state()));
    // B's turn in the same game turn writes nothing more.
    const auto when = fs::last_write_time(file);
    session.endTurn();
    CHECK(fs::last_write_time(file) == when);
}

TEST_CASE("game flow: a simultaneous turn processed in the background comes out as one processed at once") {
    GameState s = newGame(true, true);
    classic::ClassicSession at(sharedRules(), s, kA, SessionKind::Hotseat);
    classic::ClassicSession behind(sharedRules(), s, kA, SessionKind::Hotseat);
    behind.setBackground(true);
    for (int turn = 0; turn < 2; ++turn) {
        at.endTurn();
        at.endTurn();   // B's, and the turn is processed
        behind.endTurn();
        CHECK_FALSE(behind.processing());   // A's: the turn passes to B
        behind.endTurn();
        // Meanwhile the windows see the game as it was.
        const uint32_t before = behind.state().turn;
        for (int guard = 0; behind.processing() && guard < 20000; ++guard) {
            CHECK(behind.state().turn == before);
            const auto [done, total] = behind.progress();
            CHECK(done <= total);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            behind.poll();
        }
        REQUIRE_FALSE(behind.processing());
        CHECK(behind.state().turn == before + 1);
        CHECK(stateChecksum(behind.state()) == stateChecksum(at.state()));
    }
}

TEST_CASE("game flow: Load Game sorts by name, A to Z, and by date when its heading is clicked") {
    // Spec 06 §1.1 (confirmed: binary): the list opens sorted by name, shown
    // in capitals; a heading click sorts by its column (dates newest first)
    // with the usual five-key history.
    std::vector<classic::FileEntry> files{entry("beta", 30), entry("Alpha", 10), entry("gamma", 20), entry("alpha2", 10)};
    classic::FileSortHistory history;
    classic::sortFiles(files, history);
    CHECK(names(files) == std::vector<std::string>{"Alpha", "alpha2", "beta", "gamma"});
    history.click(classic::FileSortKey::Date);
    classic::sortFiles(files, history);
    // Newest first; the two of the same moment by name, the next key.
    CHECK(names(files) == std::vector<std::string>{"Alpha", "alpha2", "gamma", "beta"});
    for (int i = 0; i < 6; ++i) history.click(classic::FileSortKey::Name);
    CHECK(history.keys.size() == 5);
    classic::sortFiles(files, history);
    CHECK(names(files).front() == "Alpha");
}

TEST_CASE("game flow: Save Game refuses a name that cannot be a file name") {
    // Spec 06 §1.2 (confirmed: binary): "Invalid File Name.".
    CHECK(classic::validSaveName("Terran Federation 2401.3"));
    CHECK(classic::validSaveName("  spaced  "));
    CHECK(classic::trimmedSaveName("  spaced  ") == "spaced");
    CHECK(classic::validSaveName("It's mine (2)"));
    CHECK_FALSE(classic::validSaveName(""));
    CHECK_FALSE(classic::validSaveName("   "));
    CHECK_FALSE(classic::validSaveName("a/b"));
    CHECK_FALSE(classic::validSaveName("a\\b"));
    CHECK_FALSE(classic::validSaveName("what?"));
    CHECK_FALSE(classic::validSaveName("ends."));
    CHECK_FALSE(classic::validSaveName("con"));
    CHECK_FALSE(classic::validSaveName("LPT1.txt"));
    CHECK_FALSE(classic::validSaveName("caf\xC3\xA9"));
}
