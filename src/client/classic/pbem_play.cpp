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
    auto loaded = game::loadGame(gameFile);
    if (!loaded) return std::unexpected(loaded.error());
    PbemGame g;
    g.gameFile = gameFile;
    g.state = std::move(loaded->first);
    g.info = std::move(loaded->second);
    // The host refuses a game made with another data set; so do we, since the
    // turn would not play the same here.
    if (!g.info.dataSet.empty()) {
        const std::string mine = game::dataSetIdentity(rules);
        if (!game::sameDataSet(g.info.dataSet, mine))
            return std::unexpected(std::format("The game was created with data set {}, but this data set is {}.", g.info.dataSet, mine));
    }
    if (std::string problem = game::validateState(g.state, &rules); !problem.empty())
        return std::unexpected("The game does not fit this data set: " + problem);
    if (g.state.gameOver) return std::unexpected(std::string("The game is over."));
    // A turn-based game file between player turns: the computer players play
    // on to the next human, exactly as the host will before it reads the orders.
    if (game::turnBased(g.state) && !g.state.playerTurn.started) game::resumeTurnBased(rules, g.state);
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
        c.playable = e.alive && e.kind == game::PlayerKind::Human;
        c.password = !e.passwordHash.empty();
        c.yourTurn = !turnBased || e.id == active;
        out.push_back(std::move(c));
    }
    return out;
}

std::expected<PbemTurn, std::string> beginPbemTurn(const PbemGame& g, game::EmpireId empire, std::string_view password, fs::path ordersDir) {
    const game::GameState& s = g.state;
    if (!empire.valid() || empire.index() >= s.empires.size()) return std::unexpected(std::string("No such empire in this game."));
    const game::Empire& e = s.empire(empire);
    if (!living(s, empire)) return std::unexpected(std::format("{} has been destroyed.", e.name));
    if (e.kind != game::PlayerKind::Human) return std::unexpected(std::format("{} is played by the computer.", e.name));
    // The same check the host makes on the .plr (the game keeps only a verifier).
    const std::string hash = net::hashPassword(password);
    if (!net::checkPassword(e.passwordHash, hash)) return std::unexpected(std::format("Wrong password for {}.", e.name));
    const bool turnBased = game::turnBased(s);
    if (turnBased) {
        const game::EmpireId active = pbemActivePlayer(g);
        if (active != empire)
            return std::unexpected(std::format("It is {}'s turn, not {}'s. Wait for the game file the host sends for your turn.",
                                               empireName(s, active), e.name));
    }
    PbemTurn t;
    t.gameFile = g.gameFile;
    t.ordersDir = !ordersDir.empty() ? std::move(ordersDir) : g.gameFile.has_parent_path() ? g.gameFile.parent_path() : fs::path(".");
    t.info = g.info;
    t.empire = empire;
    t.turn = s.turn;
    t.passwordHash = hash;
    t.turnBased = turnBased;
    t.startChecksum = turnBased ? game::stateChecksum(s) : 0;
    return t;
}

std::expected<fs::path, std::string> writePbemOrders(const PbemTurn& t, const game::GameState& now, std::span<const game::Command> commands) {
    std::error_code ec;
    fs::create_directories(t.ordersDir, ec);
    if (!fs::is_directory(t.ordersDir, ec)) return std::unexpected(std::format("{}: cannot create the folder", t.ordersDir.string()));
    const game::EmpireOrders orders{t.empire, t.turn, std::vector<game::Command>(commands.begin(), commands.end())};
    if (t.turnBased)
        return net::pbem::writePlayerTurn(t.ordersDir, t.info, t.startChecksum, orders, game::stateChecksum(now), t.passwordHash);
    return net::pbem::writePlayerOrders(t.ordersDir, t.info, orders, t.passwordHash);
}

std::expected<fs::path, std::string> writePbemDraft(const PbemTurn& t, const fs::path& dir, const game::GameState& now,
                                                    std::span<const game::Command> commands) {
    PbemTurn draft = t;
    draft.ordersDir = dir;
    draft.passwordHash.clear();  // nothing to log in with is left on the disk
    return writePbemOrders(draft, now, commands);
}

std::optional<std::vector<game::Command>> readPbemDraft(const PbemTurn& t, const fs::path& dir) {
    const fs::path file = dir / net::pbem::ordersFileName(t.info, t.empire);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    auto f = net::pbem::readOrdersFile(file);
    if (!f || f->gameId != t.info.gameId || f->gameName != t.info.gameName || f->empire != t.empire || f->turn != t.turn ||
        f->orders.empire != t.empire || f->orders.turn != t.turn)
        return std::nullopt;
    if (t.turnBased && f->startChecksum != t.startChecksum) return std::nullopt;
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

std::vector<fs::path> listGameFiles(const fs::path& dir) {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == net::pbem::kGameExtension) files.emplace_back(entry.last_write_time(ec), entry.path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::vector<fs::path> out;
    for (auto& [time, path] : files) out.push_back(std::move(path));
    return out;
}

} // namespace opense4::client::classic
