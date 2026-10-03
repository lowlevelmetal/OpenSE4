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
    g.info = std::move(file->info);
    g.empire = file->empire;
    g.verifier = std::move(file->verifier);
    g.viewChecksum = file->viewChecksum;
    // The host refuses a game made with another data set; so do we, since the
    // turn would not play the same here.
    if (!g.info.dataSet.empty()) {
        const std::string mine = game::dataSetIdentity(rules);
        if (!game::sameDataSet(g.info.dataSet, mine))
            return std::unexpected(std::format("The game was created with data set {}, but this data set is {}.", g.info.dataSet, mine));
    }
    auto view = game::deserializeState(file->view);
    if (!view) return std::unexpected("The turn file is damaged: " + view.error());
    // The player checks the view the host made: a changed one does not match.
    if (game::stateChecksum(*view) != g.viewChecksum)
        return std::unexpected(std::string("The turn file is damaged: its game does not match its checksum."));
    g.state = std::move(*view);
    if (std::string problem = game::validateState(g.state, &rules); !problem.empty())
        return std::unexpected("The game does not fit this data set: " + problem);
    if (!g.empire.valid() || g.empire.index() >= g.state.empires.size())
        return std::unexpected(std::string("The turn file is damaged: it names no empire of the game."));
    // The host read the game for the turn already (colonies recalculated,
    // a turn-based game played on to this player): the view is ready as it is.
    if (g.state.gameOver) return std::unexpected(std::string("The game is over."));
    return g;
}

game::EmpireId pbemActivePlayer(const PbemGame& g) {
    if (!game::turnBased(g.state) || !g.state.playerTurn.started) return {};
    return game::activePlayer(g.state);
}

std::vector<PbemEmpireChoice> pbemEmpires(const PbemGame& g) {
    std::vector<PbemEmpireChoice> out;
    const game::EmpireId active = pbemActivePlayer(g);
    const bool turnBased = game::turnBased(g.state);
    for (const game::Empire& e : g.state.empires) {
        PbemEmpireChoice c;
        c.id = e.id;
        c.name = e.name;
        if (e.id.index() < g.info.players.size()) c.player = g.info.players[e.id.index()];
        c.playable = e.id == g.empire && e.alive && e.kind == game::PlayerKind::Human;
        c.password = e.id == g.empire && !g.verifier.empty();
        c.yourTurn = !turnBased || e.id == active;
        out.push_back(std::move(c));
    }
    return out;
}

bool pbemNeedsNewPassword(const PbemGame& g) { return net::isLegacyVerifier(g.verifier); }

std::expected<PbemTurn, std::string> beginPbemTurn(const PbemGame& g, game::EmpireId empire, std::string_view password, fs::path ordersDir,
                                                   std::string_view newPassword) {
    const game::GameState& s = g.state;
    if (!empire.valid() || empire.index() >= s.empires.size()) return std::unexpected(std::string("No such empire in this game."));
    const game::Empire& e = s.empire(empire);
    if (empire != g.empire)
        return std::unexpected(std::format("This turn file is {}'s, not {}'s. Ask the host for your own.", empireName(s, g.empire), e.name));
    if (!living(s, empire)) return std::unexpected(std::format("{} has been destroyed.", e.name));
    if (e.kind != game::PlayerKind::Human) return std::unexpected(std::format("{} is played by the computer.", e.name));
    // The same verifier the host checks the .plr's signature with.
    const std::string hash = net::hashPassword(password);
    if (!net::checkPassword(g.verifier, hash)) return std::unexpected(std::format("Wrong password for {}.", e.name));
    const bool turnBased = game::turnBased(s);
    if (turnBased) {
        const game::EmpireId active = pbemActivePlayer(g);
        if (active != empire)
            return std::unexpected(std::format("It is {}'s turn, not {}'s. Wait for the turn file the host sends for your turn.",
                                               empireName(s, active), e.name));
    }
    // The old password's hash travels in this turn's .plr, so a new password
    // takes over from it (nothing may be made of a hash that was seen).
    std::string signingHash = hash;
    std::string legacyHash;
    if (pbemNeedsNewPassword(g)) {
        if (newPassword.empty())
            return std::unexpected(std::format("This game was made by OpenSE4 0.6: choose a new password for {}. This turn's orders file "
                                               "shows the old one once, so the old one stops counting.",
                                               e.name));
        signingHash = net::hashPassword(newPassword);
        if (signingHash == hash) return std::unexpected(std::string("Choose a new password other than the old one."));
        legacyHash = hash;
    }
    PbemTurn t;
    t.gameFile = g.gameFile;
    t.ordersDir = !ordersDir.empty() ? std::move(ordersDir) : g.gameFile.has_parent_path() ? g.gameFile.parent_path() : fs::path(".");
    t.info = g.info;
    t.empire = empire;
    t.turn = s.turn;
    t.passwordHash = std::move(signingHash);
    t.turnBased = turnBased;
    t.startChecksum = g.viewChecksum;
    t.legacyPasswordHash = std::move(legacyHash);
    return t;
}

std::expected<fs::path, std::string> writePbemOrders(const PbemTurn& t, std::span<const game::Command> commands) {
    std::error_code ec;
    fs::create_directories(t.ordersDir, ec);
    if (!fs::is_directory(t.ordersDir, ec)) return std::unexpected(std::format("{}: cannot create the folder", t.ordersDir.string()));
    const game::EmpireOrders orders{t.empire, t.turn, std::vector<game::Command>(commands.begin(), commands.end())};
    return net::pbem::writePlayerOrders(t.ordersDir, t.info, orders, t.startChecksum, t.passwordHash, t.legacyPasswordHash);
}

std::expected<fs::path, std::string> writePbemDraft(const PbemTurn& t, const fs::path& dir, std::span<const game::Command> commands) {
    PbemTurn draft = t;
    draft.ordersDir = dir;
    draft.passwordHash.clear();  // unsigned: nothing of the password is left on the disk
    draft.legacyPasswordHash.clear();
    return writePbemOrders(draft, commands);
}

std::optional<std::vector<game::Command>> readPbemDraft(const PbemTurn& t, const fs::path& dir) {
    const fs::path file = dir / net::pbem::ordersFileName(t.info, t.empire);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    auto f = net::pbem::readOrdersFile(file);
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
