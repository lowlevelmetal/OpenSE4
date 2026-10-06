#include "client/classic/pbem_play.hpp"

#include "client/classic/session.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/pbem.hpp"
#include "net/secure.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace opense4::client::classic {

namespace fs = std::filesystem;

namespace {

bool living(const game::GameState& s, game::EmpireId e) {
    return e.valid() && e.index() < s.empires.size() && s.empire(e).alive;
}

std::string empireName(const game::GameState& s, game::EmpireId e) {
    return e.valid() && e.index() < s.empires.size() ? s.empire(e).name : std::string("nobody");
}

} // namespace

std::expected<PbemGame, std::string> loadPbemGame(const game::Rules& rules, const fs::path& gameFile) {
    auto bytes = game::readFileBytes(gameFile);
    if (!bytes) return std::unexpected(bytes.error());
    if (game::readSaveInfo(gameFile))
        return std::unexpected(std::string("This is a host's game file, with the whole game in it. The host sends each player a turn file of "
                                           "their own (<game>_<NN>.turn): open that one."));
    auto file = net::pbem::decodeTurnFile(*bytes);
    if (!file) return std::unexpected(file.error());
    PbemGame g;
    g.gameFile = gameFile;
    g.info = file->info;
    g.empire = file->empire;
    g.turnBased = file->turnBased;
    g.verifier = file->verifier;
    g.file = std::move(*file);
    // The host refuses a game made with another data set; so do we, since the
    // turn would not play the same here.
    if (const auto mods = game::modDifferences(g.info.mods, rules, "the game"); !mods.empty()) {
        std::string all;
        for (const std::string& m : mods) all += (all.empty() ? "" : "; ") + m;
        return std::unexpected(
            std::format("The game was played with other mods than the ones in use (choose them in the main menu's Mods window): {}.", all));
    }
    if (!game::sameDataSet(g.info, rules))
        return std::unexpected(std::format("The game was created with data set {}, but this data set is {}.", g.info.dataSet,
                                           g.info.formatVersion <= 8 ? game::legacyDataSetIdentity(rules) : game::dataSetIdentity(rules)));
    if (!g.empire.valid() || g.empire.index() >= g.info.empires.size())
        return std::unexpected(std::string("The turn file is damaged: it names no empire of the game."));
    return g;
}

game::EmpireId pbemActivePlayer(const PbemGame& g) {
    // A turn-based game sends a turn file to the player whose turn it is only.
    return g.turnBased ? g.empire : game::EmpireId{};
}

std::vector<PbemEmpireChoice> pbemEmpires(const PbemGame& g) {
    std::vector<PbemEmpireChoice> out;
    for (size_t i = 0; i < g.info.empires.size(); ++i) {
        const game::EmpireId id{static_cast<uint32_t>(i)};
        PbemEmpireChoice c;
        c.id = id;
        c.name = g.info.empires[i];
        if (i < g.info.players.size()) c.player = g.info.players[i];
        c.playable = id == g.empire;
        c.password = id == g.empire && !g.verifier.empty();
        c.yourTurn = !g.turnBased || id == g.empire;
        out.push_back(std::move(c));
    }
    return out;
}

bool pbemNeedsNewPassword(const PbemGame& g) { return net::isLegacyVerifier(g.verifier); }

fs::path pbemKnownHostsFile() { return userDataDir() / net::secure::kKnownHostsFileName; }

net::pbem::HostKeyCheck pbemHostKey(const PbemGame& g, const fs::path& knownHosts) {
    return net::pbem::checkHostKey(g.file, net::secure::KnownHosts(knownHosts.empty() ? pbemKnownHostsFile() : knownHosts));
}

std::expected<PbemTurn, std::string> beginPbemTurn(PbemGame& g, game::EmpireId empire, std::string_view password, fs::path ordersDir,
                                                   std::string_view newPassword, const PbemTrust& trust) {
    if (!empire.valid() || empire.index() >= g.info.empires.size()) return std::unexpected(std::string("No such empire in this game."));
    const std::string& name = g.info.empires[empire.index()];
    if (empire != g.empire)
        return std::unexpected(std::format("This turn file is {}'s, not {}'s. Ask the host for your own.", g.info.empires[g.empire.index()], name));
    // The host's signature and key, the password, then the view
    // (net::pbem::openTurnForPlayer); the host's key is trusted from the
    // first turn file of the game on.
    net::secure::KnownHosts known(trust.knownHosts.empty() ? pbemKnownHostsFile() : trust.knownHosts);
    auto opened = net::pbem::openTurnForPlayer(g.file, known, password, newPassword,
                                               net::pbem::PlayerTrust{trust.trustChangedHostKey, trust.showOldPassword});
    if (!opened) return std::unexpected(opened.error());
    auto state = game::deserializeState(opened->view.view);
    if (!state) return std::unexpected("The turn file is damaged: " + state.error());
    // The player checks the view the host made: a changed one does not match.
    if (game::stateChecksum(*state) != opened->view.viewChecksum)
        return std::unexpected(std::string("The turn file is damaged: its game does not match its checksum."));
    g.state = std::move(*state);
    g.viewChecksum = opened->view.viewChecksum;
    std::optional<net::PasswordKeys> keys = std::move(opened->keys);
    std::string legacyHash = std::move(opened->legacyPasswordHash);
    const game::GameState& s = g.state;
    if (s.gameOver) return std::unexpected(std::string("The game is over."));
    if (empire.index() >= s.empires.size()) return std::unexpected(std::string("No such empire in this game."));
    const game::Empire& e = s.empire(empire);
    if (!living(s, empire)) return std::unexpected(std::format("{} has been destroyed.", e.name));
    if (e.kind != game::PlayerKind::Human) return std::unexpected(std::format("{} is played by the computer.", e.name));
    const bool turnBased = game::turnBased(s);
    if (turnBased) {
        const game::EmpireId active = s.playerTurn.started ? game::activePlayer(s) : game::EmpireId{};
        if (active != empire)
            return std::unexpected(std::format("It is {}'s turn, not {}'s. Wait for the turn file the host sends for your turn.",
                                               empireName(s, active), e.name));
    }
    PbemTurn t;
    t.gameFile = g.gameFile;
    t.ordersDir = !ordersDir.empty() ? std::move(ordersDir) : g.gameFile.has_parent_path() ? g.gameFile.parent_path() : fs::path(".");
    t.info = g.info;
    t.empire = empire;
    t.turn = s.turn;
    t.keys = std::move(keys);
    t.legacyPasswordHash = std::move(legacyHash);
    t.hostKey = g.file.hostKey;
    t.turnBased = turnBased;
    t.startChecksum = g.viewChecksum;
    return t;
}

std::expected<fs::path, std::string> writePbemOrders(const PbemTurn& t, std::span<const game::Command> commands) {
    std::error_code ec;
    fs::create_directories(t.ordersDir, ec);
    if (!fs::is_directory(t.ordersDir, ec)) return std::unexpected(std::format("{}: cannot create the folder", t.ordersDir.string()));
    const game::EmpireOrders orders{t.empire, t.turn, std::vector<game::Command>(commands.begin(), commands.end())};
    return net::pbem::writePlayerOrders(t.ordersDir, t.info, orders, t.startChecksum, t.keys, t.hostKey, t.legacyPasswordHash);
}

std::expected<fs::path, std::string> writePbemDraft(const PbemTurn& t, const fs::path& dir, std::span<const game::Command> commands) {
    // Kept on this machine: unsigned, not encrypted, nothing of the password in it.
    net::pbem::OrdersFile f;
    f.gameName = t.info.gameName;
    f.gameId = t.info.gameId;
    f.empire = t.empire;
    f.turn = t.turn;
    f.orders = game::EmpireOrders{t.empire, t.turn, std::vector<game::Command>(commands.begin(), commands.end())};
    f.startChecksum = t.startChecksum;
    const fs::path file = dir / net::pbem::ordersFileName(t.info, t.empire);
    if (auto r = net::pbem::writeDraft(file, f); !r) return std::unexpected(r.error());
    return file;
}

std::optional<std::vector<game::Command>> readPbemDraft(const PbemTurn& t, const fs::path& dir) {
    const fs::path file = dir / net::pbem::ordersFileName(t.info, t.empire);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    auto f = net::pbem::readDraft(file);
    if (!f || f->gameId != t.info.gameId || f->gameName != t.info.gameName || f->empire != t.empire || f->turn != t.turn ||
        f->orders.empire != t.empire || f->orders.turn != t.turn || f->startChecksum != t.startChecksum)
        return std::nullopt;
    return std::move(f->orders.commands);
}

void removePbemDraft(const PbemTurn& t, const fs::path& dir) {
    std::error_code ec;
    fs::remove(dir / net::pbem::ordersFileName(t.info, t.empire), ec);
}

fs::path pbemDir() {
    const fs::path dir = userDataDir() / "pbem";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path pbemDraftsDir() {
    const fs::path dir = pbemDir() / "drafts";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::vector<fs::path> listTurnFiles(const fs::path& dir) {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == net::pbem::kTurnExtension) files.emplace_back(entry.last_write_time(ec), entry.path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::vector<fs::path> out;
    for (auto& [time, path] : files) out.push_back(std::move(path));
    return out;
}

} // namespace opense4::client::classic
