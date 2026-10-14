// The original's command line (docs/spec/05 §9.2; src/client/classic/command_line.*):
// `<game file> <password> [<player number> [<mod folder>]]`. Without a player
// number the program hosts the turn of a play-by-e-mail game file and quits;
// with one it signs in as that player, the password first, then the data files.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/command_line.hpp"
#include "client/classic/pbem_play.hpp"
#include "client/classic/session.hpp"
#include "game/classic_save.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/pbem.hpp"
#include "net/secure.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace opense4;
using namespace opense4::client::classic;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<const game::Rules> sharedRules() {
    return std::shared_ptr<const game::Rules>(&test::engineRules(), [](const game::Rules*) {});
}

const net::secure::PbemHostKeys& hostKeys() {
    static const net::secure::PbemHostKeys keys = [] {
        net::crypto::Key secret{};
        net::crypto::randomBytes(secret);
        return net::secure::hostIdentity(secret).pbem;
    }();
    return keys;
}

bool holds(const std::vector<std::string>& lines, std::string_view text) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
}

// Two human empires and a computer empire. `gameId` 0: a local (hotseat)
// game with the client's password hashes; otherwise a host's game file with
// verifiers, the master password `master` and the turn files beside it.
game::GameState newGame(bool simultaneous) {
    game::GameSetup setup;
    setup.seed = 17;
    setup.options.systemCount = 8;
    setup.options.simultaneous = simultaneous;
    for (int i = 0; i < 3; ++i) {
        game::EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = i < 2 ? game::PlayerKind::Human : game::PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    auto state = game::createGame(test::engineRules(), setup);
    REQUIRE(state.has_value());
    return std::move(*state);
}

fs::path writeHostGame(const fs::path& dir, std::string_view master, uint64_t gameId) {
    const game::Rules& r = test::engineRules();
    game::GameState s = newGame(true);
    for (int i = 0; i < 2; ++i) s.empires[static_cast<size_t>(i)].passwordHash = net::passwordVerifier(std::format("pw{}", i), gameId);
    game::SaveInfo info;
    info.gameName = "Wire Game";
    info.gameId = gameId;
    info.dataSet = game::dataSetIdentity(r);
    info.players = {"ann", "ben", ""};
    info.masterPasswordVerifier = net::passwordVerifier(master, gameId);
    const fs::path gam = dir / "wire.gam";
    REQUIRE(game::saveGame(gam, s, info).has_value());
    REQUIRE(net::pbem::writeTurnFiles(r, gam, dir, hostKeys()).has_value());
    return gam;
}

fs::path turnFileOf(const fs::path& dir, int empire) { return dir / std::format("Wire_Game_{:02}.turn", empire); }

fs::path writeLocalGame(const fs::path& dir, bool simultaneous, std::string_view dataSet = "Test Data") {
    game::GameState s = newGame(simultaneous);
    s.empires[0].passwordHash = game::hashPassword("first");
    // Empire 2 has no password.
    game::SaveInfo info;
    info.gameName = "Hearth";
    info.dataSet = std::string(dataSet);
    const fs::path file = dir / (simultaneous ? "hearth_sim.gam" : "hearth_tb.gam");
    REQUIRE(game::saveGame(file, s, info).has_value());
    return file;
}

uint32_t turnOf(const fs::path& gam) {
    auto info = game::readSaveInfo(gam);
    REQUIRE(info.has_value());
    return info->turn;
}

} // namespace

TEST_CASE("command line: the arguments as the original reads them") {
    CHECK_FALSE(commandLineGame(std::vector<std::string>{}).has_value());
    // Only the game file: the host, with an empty password.
    auto one = commandLineGame(std::vector<std::string>{"game.gam"});
    REQUIRE(one.has_value());
    CHECK(one->gameFile == "game.gam");
    CHECK(one->password.empty());
    CHECK(one->host());
    // No player number, 0, or text that is not a number: the host.
    for (const char* third : {"0", "x", "", "2b", "-1", "99999999999"}) {
        auto g = commandLineGame(std::vector<std::string>{"game.gam", "boss", third});
        REQUIRE(g.has_value());
        CHECK_MESSAGE(g->host(), third);
    }
    auto player = commandLineGame(std::vector<std::string>{"game.gam", "secret", "3", "My Mod", "extra"});
    REQUIRE(player.has_value());
    CHECK(player->password == "secret");
    CHECK(player->player == 3);
    CHECK_FALSE(player->host());
    CHECK(player->modFolder == "My Mod");
    CHECK(playerNumberArgument("12") == 12);
    CHECK(playerNumberArgument("012") == 12);
    CHECK(playerNumberArgument(" 1") == 0);

    // The nd switch, with a dash or a slash, in any letter case.
    for (const char* s : {"-nd", "/nd", "-ND", "/Nd"}) CHECK_MESSAGE(isNoDialogsSwitch(s), s);
    for (const char* s : {"nd", "--nd", "-n", "/ndx", "-dn"}) CHECK_FALSE_MESSAGE(isNoDialogsSwitch(s), s);

    // Passwords: the spaces around them and letter case do not count.
    CHECK(samePassword("  Secret ", "sECRET"));
    CHECK(samePassword("", "   "));
    CHECK_FALSE(samePassword("secret", "secrets"));
    CHECK_FALSE(samePassword("se cret", "secret"));
}

TEST_CASE("command line: the game file is found as given, then in the folders in turn") {
    const test::TempDir tmp("cmdline_find");
    const fs::path a = tmp.path() / "a", b = tmp.path() / "b";
    fs::create_directories(a);
    fs::create_directories(b / "sub");
    std::ofstream(b / "game.gam") << "x";
    std::ofstream(b / "sub" / "deep.gam") << "x";
    std::ofstream(a / "game.gam") << "x";
    const std::vector<fs::path> folders{a, b};
    CHECK(findGameFile("game.gam", folders) == a / "game.gam");
    CHECK(findGameFile("sub/deep.gam", folders) == b / "sub" / "deep.gam");
    CHECK(findGameFile((b / "game.gam").string(), {}) == b / "game.gam");
    CHECK_FALSE(findGameFile("missing.gam", folders).has_value());
    CHECK_FALSE(findGameFile((tmp.path() / "missing.gam").string(), folders).has_value());
    CHECK_FALSE(findGameFile("", folders).has_value());
    CHECK_FALSE(findGameFile("sub", std::vector<fs::path>{b}).has_value());   // a folder is no game file
    CHECK(gameFileKind(a / "game.gam") == GameFileKind::Unknown);

    // The mod folder comes first, then the saves and pbem folders.
    CommandLineGame line;
    line.modFolder = "TestMod";
    const auto where = gameFileFolders(line, tmp.path());
    REQUIRE(where.size() == 3);
    CHECK(where[0] == fs::path("TestMod"));   // no such folder in the game folder: the name as a path
    CHECK(where[1] == savesDir());
    CHECK(where[2] == pbemDir());
    fs::create_directories(tmp.path() / "TestMod");
    CHECK(modFolderPath("TestMod", tmp.path()) == tmp.path() / "TestMod");
    CHECK(gameFileFolders(CommandLineGame{}, tmp.path()).size() == 2);
}

TEST_CASE("command line: hosting processes a play-by-e-mail game's turn with the host password only") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("cmdline_host");
    const fs::path gam = writeHostGame(tmp.path(), "Boss", 4242);
    CHECK(gameFileKind(gam) == GameFileKind::OpenSE4Game);
    CHECK(gameFileKind(turnFileOf(tmp.path(), 1)) == GameFileKind::TurnFile);
    const uint32_t before = turnOf(gam);

    // A wrong password: refused with exit code 1, nothing written.
    HostOutcome wrong = hostTurn(r, gam, "boss2", {}, hostKeys());
    CHECK(wrong.exitCode == kHostRefused);
    CHECK(wrong.wrongPassword);
    CHECK(turnOf(gam) == before);
    CHECK(hostTurn(r, gam, "", {}, hostKeys()).wrongPassword);

    // Player 1 plays the turn from the turn file: the password without regard
    // to the spaces around it and to letter case (as far as a verifier allows).
    const PbemTrust trust{tmp.path() / "known_hosts.txt"};
    TurnFileSignIn in = signInToTurnFile(r, turnFileOf(tmp.path(), 1), 1, "  PW0 ", tmp.path(), trust);
    REQUIRE_MESSAGE(in.check.ok(), in.check.why);
    REQUIRE(in.turn.has_value());
    CHECK(in.turn->empire == game::EmpireId{0u});
    auto plr = writePbemOrders(*in.turn, {});
    REQUIRE_MESSAGE(plr.has_value(), (plr ? std::string{} : plr.error()));

    // The host password as typed with spaces around it: processed, saved, the
    // .plr used and deleted, the next turn files written; the report as
    // `opense4-server pbem process` prints it.
    HostOutcome done = hostTurn(r, gam, " Boss ", {}, hostKeys());
    REQUIRE_MESSAGE(done.exitCode == 0, done.error);
    CHECK_FALSE(done.wrongPassword);
    CHECK(turnOf(gam) == before + 1);
    CHECK(holds(done.report, std::format("Processed turn {}; the game is now at turn {}.", before, before + 1)));
    CHECK(holds(done.report, "  orders: Empire 1"));
    CHECK(holds(done.report, "  no orders, played by the computer: Empire 2"));
    CHECK(holds(done.report, "  deleted 1 processed .plr files"));
    CHECK(holds(done.report, "Send "));
    CHECK(holds(done.report, "to empire 2, player ben."));
    CHECK_FALSE(fs::exists(*plr));
    CHECK(fs::exists(tmp.path() / "wire.gam.bak"));

    // The orders files may come from another folder.
    const fs::path inbox = tmp.path() / "inbox";
    fs::create_directories(inbox);
    TurnFileSignIn second = signInToTurnFile(r, turnFileOf(tmp.path(), 2), 2, "pw1", inbox, trust);
    REQUIRE_MESSAGE(second.check.ok(), second.check.why);
    REQUIRE(writePbemOrders(*second.turn, {}).has_value());
    HostOutcome fromInbox = hostTurn(r, gam, "Boss", inbox, hostKeys());
    REQUIRE_MESSAGE(fromInbox.exitCode == 0, fromInbox.error);
    CHECK(holds(fromInbox.report, "  orders: Empire 2"));
}

TEST_CASE("command line: a game without a master password is hosted with an empty password only") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("cmdline_host_open");
    const fs::path gam = writeHostGame(tmp.path(), "", 77);
    CHECK(hostTurn(r, gam, "anything", {}, hostKeys()).wrongPassword);
    const HostOutcome done = hostTurn(r, gam, "  ", {}, hostKeys());
    CHECK_MESSAGE(done.exitCode == 0, done.error);
}

TEST_CASE("command line: files that have no host are refused for hosting") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("cmdline_host_refused");
    // A player's turn file.
    const fs::path gam = writeHostGame(tmp.path(), "boss", 91);
    HostOutcome turn = hostTurn(r, turnFileOf(tmp.path(), 1), "boss", {}, hostKeys());
    CHECK(turn.exitCode == kHostRefused);
    CHECK_FALSE(turn.wrongPassword);
    CHECK(turn.error.find("turn file") != std::string::npos);
    // A local game: no host.
    const fs::path local = writeLocalGame(tmp.path(), true);
    HostOutcome alone = hostTurn(r, local, "", {}, hostKeys());
    CHECK(alone.exitCode == kHostRefused);
    CHECK(alone.error.find("no host") != std::string::npos);
    // A saved game of the original: its players' orders files are not read.
    game::classic::ConversionReport report;
    game::GameState s = newGame(true);
    const fs::path classicFile = tmp.path() / "Original.gam";
    REQUIRE(game::classic::writeClassicGame(r, s, classicFile, report, {5, {}}).has_value());
    CHECK(gameFileKind(classicFile) == GameFileKind::ClassicGame);
    HostOutcome original = hostTurn(r, classicFile, "", {}, hostKeys());
    CHECK(original.exitCode == kHostRefused);
    CHECK(original.error.find("Space Empires IV") != std::string::npos);
    // Something else.
    std::ofstream(tmp.path() / "notes.gam") << "not a game";
    CHECK(hostTurn(r, tmp.path() / "notes.gam", "", {}, hostKeys()).exitCode == kHostRefused);
    CHECK(turnOf(gam) == 0);
}

TEST_CASE("command line: signing in to a turn file") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("cmdline_turnfile");
    writeHostGame(tmp.path(), "boss", 515);
    const PbemTrust trust{tmp.path() / "known_hosts.txt"};
    const fs::path first = turnFileOf(tmp.path(), 1);
    CHECK(signInToTurnFile(r, first, 1, "pw1", {}, trust).check.verdict == SignInVerdict::WrongPassword);
    CHECK(signInToTurnFile(r, first, 1, "", {}, trust).check.verdict == SignInVerdict::WrongPassword);
    // Another empire's turn file signs nobody in.
    CHECK(signInToTurnFile(r, first, 2, "pw1", {}, trust).check.verdict == SignInVerdict::NotPlayable);
    TurnFileSignIn in = signInToTurnFile(r, first, 1, "PW0", {}, trust);
    REQUIRE_MESSAGE(in.check.ok(), in.check.why);
    REQUIRE(in.game.has_value());
    CHECK(in.turn->ordersDir == tmp.path());
    // A session of the game plays it as --pbem does.
    auto session = ClassicSession::pbem(sharedRules(), std::move(*in.game), std::move(*in.turn), {});
    CHECK(session->kind() == SessionKind::Pbem);
    CHECK(session->player() == game::EmpireId{0u});
    CHECK(signInToTurnFile(r, tmp.path() / "missing.turn", 1, "pw0", {}, trust).check.verdict == SignInVerdict::Unreadable);
}

TEST_CASE("command line: signing in to an OpenSE4 saved game, the password before the data files") {
    const test::TempDir tmp("cmdline_saved");
    const fs::path sim = writeLocalGame(tmp.path(), true);
    // Wrong passwords, and an empire the game does not have.
    CHECK(signInToSavedGame(sharedRules(), sim, 1, "second").check.verdict == SignInVerdict::WrongPassword);
    CHECK(signInToSavedGame(sharedRules(), sim, 2, "something").check.verdict == SignInVerdict::WrongPassword);   // no password: an empty one only
    CHECK(signInToSavedGame(sharedRules(), sim, 9, "").check.verdict == SignInVerdict::WrongPassword);
    // The computer's empire signs nobody in.
    CHECK(signInToSavedGame(sharedRules(), sim, 3, "").check.verdict == SignInVerdict::NotPlayable);

    // Signed in: that empire's turn, no Login To Game.
    SavedGameSignIn second = signInToSavedGame(sharedRules(), sim, 2, "  ");
    REQUIRE_MESSAGE(second.check.ok(), second.check.why);
    CHECK(second.session->player() == game::EmpireId{1u});
    CHECK(second.session->takeSignIn() == ClassicSession::SignIn::None);
    SavedGameSignIn first = signInToSavedGame(sharedRules(), sim, 1, " FIRST ");
    REQUIRE_MESSAGE(first.check.ok(), first.check.why);
    CHECK(first.session->player() == game::EmpireId{0u});
    CHECK(first.session->kind() == SessionKind::Hotseat);

    // Another data set: "Invalid Data Files", but only after the password.
    const test::TempDir other("cmdline_saved_other");
    const fs::path foreign = writeLocalGame(other.path(), true, "Another#0123456789abcdef");
    CHECK(signInToSavedGame(sharedRules(), foreign, 1, "wrong").check.verdict == SignInVerdict::WrongPassword);
    CHECK(signInToSavedGame(sharedRules(), foreign, 1, "first").check.verdict == SignInVerdict::InvalidDataFiles);

    // A turn-based game: only the empire whose turn it is.
    const fs::path tb = writeLocalGame(tmp.path(), false);
    SavedGameSignIn notYet = signInToSavedGame(sharedRules(), tb, 2, "");
    CHECK(notYet.check.verdict == SignInVerdict::NotPlayable);
    CHECK(notYet.check.why.find("turn") != std::string::npos);
    SavedGameSignIn now = signInToSavedGame(sharedRules(), tb, 1, "first");
    REQUIRE_MESSAGE(now.check.ok(), now.check.why);
    CHECK(now.session->myTurn());

    CHECK(signInToSavedGame(sharedRules(), tmp.path() / "missing.gam", 1, "").check.verdict == SignInVerdict::Unreadable);
}

TEST_CASE("command line: signing in to a saved game of the original compares the seven data-set checksums") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("cmdline_classic");
    game::GameState s = newGame(true);
    const fs::path exported = tmp.path() / "Exported.gam";
    game::classic::ConversionReport report;
    REQUIRE(game::classic::writeClassicGame(r, s, exported, report, {5, {}}).has_value());
    auto bytes = game::readFileBytes(exported);
    REQUIRE(bytes.has_value());
    auto save = game::classic::decodeClassicSave(*bytes, r.data().racialTraits.size());
    REQUIRE(save.has_value());
    // The export wrote the checksums of the data files (spec 08 §3.2.1).
    const game::classic::DataSetChecksums sums = save->options.checksums;
    CHECK(std::any_of(sums.begin(), sums.end(), [](int32_t v) { return v != 0; }));
    // The original keeps passwords in the clear.
    save->empires[0].password = "Open Sesame";
    auto write = [&](const fs::path& file, const game::classic::ClassicSave& c) {
        auto encoded = game::classic::encodeClassicSave(c);
        REQUIRE(encoded.has_value());
        REQUIRE(game::writeFileAtomic(file, *encoded).has_value());
    };
    const fs::path same = tmp.path() / "Same.gam";
    write(same, *save);
    CHECK(signInToSavedGame(sharedRules(), same, 1, "open").check.verdict == SignInVerdict::WrongPassword);
    SavedGameSignIn in = signInToSavedGame(sharedRules(), same, 1, " open sesame ");
    REQUIRE_MESSAGE(in.check.ok(), in.check.why);
    CHECK(in.session->player() == game::EmpireId{0u});

    // Other data files: "Invalid Data Files", after the password.
    game::classic::ClassicSave changed = *save;
    changed.options.checksums[2] += 1;
    const fs::path otherData = tmp.path() / "OtherData.gam";
    write(otherData, changed);
    CHECK(signInToSavedGame(sharedRules(), otherData, 1, "nope").check.verdict == SignInVerdict::WrongPassword);
    CHECK(signInToSavedGame(sharedRules(), otherData, 1, "OPEN SESAME").check.verdict == SignInVerdict::InvalidDataFiles);

    // A file without checksums (a Quick Start game's are all 0) is not compared (inferred).
    game::classic::ClassicSave none = *save;
    none.options.checksums = {};
    const fs::path quick = tmp.path() / "Quick.gam";
    write(quick, none);
    CHECK_MESSAGE(signInToSavedGame(sharedRules(), quick, 1, "open sesame").check.ok(),
                  signInToSavedGame(sharedRules(), quick, 1, "open sesame").check.why);
}
