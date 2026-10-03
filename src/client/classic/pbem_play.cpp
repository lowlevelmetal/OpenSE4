#include "client/classic/pbem_play.hpp"

#include "client/classic/session.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/pbem.hpp"

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
    if (!g.info.dataSet.empty()) {
        const std::string mine = game::dataSetIdentity(rules);
        if (!game::sameDataSet(g.info.dataSet, mine))
            return std::unexpected(std::format("The game was created with data set {}, but this data set is {}.", g.info.dataSet, mine));
    }
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

std::expected<PbemTurn, std::string> beginPbemTurn(PbemGame& g, game::EmpireId empire, std::string_view password, fs::path ordersDir,
                                                   std::string_view newPassword) {
    if (!empire.valid() || empire.index() >= g.info.empires.size()) return std::unexpected(std::string("No such empire in this game."));
    const std::string& name = g.info.empires[empire.index()];
    if (empire != g.empire)
        return std::unexpected(std::format("This turn file is {}'s, not {}'s. Ask the host for your own.", g.info.empires[g.empire.index()], name));
    // The password: its keys open the view and sign the orders. An empire of
    // OpenSE4 0.6 shows its old password's hash once and takes a new password,
    // whose keys come from that password, never from the old hash.
    std::optional<net::PasswordKeys> keys;
    std::string legacyHash;
    try {
        if (pbemNeedsNewPassword(g)) {
            legacyHash = net::legacyPasswordHash(password);
            if (!net::checkLegacyPassword(g.verifier, legacyHash)) return std::unexpected(std::format("Wrong password for {}.", name));
            if (newPassword.empty())
                return std::unexpected(std::format("This game was made by OpenSE4 0.6: choose a new password for {}. This turn's orders file "
                                                   "shows the old one once, so the old one stops counting.",
                                                   name));
            if (newPassword == password) return std::unexpected(std::string("Choose a new password other than the old one."));
            keys = net::passwordKeys(newPassword, g.info.gameId);
        } else if (!g.verifier.empty()) {
            keys = net::passwordKeysFor(g.verifier, password, g.info.gameId);
            if (!keys || !net::constantTimeEquals(keys->verifier(), g.verifier)) return std::unexpected(std::format("Wrong password for {}.", name));
        }
    } catch (const net::PasswordWorkError& e) {
        return std::unexpected(std::string(e.what()));
    }
    // The view: for this empire's eyes (with a password of the current kind).
    if (g.viewChecksum == 0 || g.state.empires.empty()) {
        const std::optional<net::PasswordKeys> opener = pbemNeedsNewPassword(g) ? std::nullopt : keys;
        auto view = net::pbem::openTurnFile(g.file, opener);
        if (!view) return std::unexpected(std::format("Wrong password for {}, or the turn file was changed.", name));
        auto state = game::deserializeState(view->view);
        if (!state) return std::unexpected("The turn file is damaged: " + state.error());
        // The player checks the view the host made: a changed one does not match.
        if (game::stateChecksum(*state) != view->viewChecksum)
            return std::unexpected(std::string("The turn file is damaged: its game does not match its checksum."));
        g.state = std::move(*state);
        g.viewChecksum = view->viewChecksum;
    }
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
