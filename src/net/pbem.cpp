#include "net/pbem.hpp"

#include "game/serialize_io.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/types.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

namespace opense4::net::pbem {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kPlrMagic = "OSE4PLRF";
constexpr std::string_view kWhat = "orders file (.plr)";

std::string lowerExtension(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

} // namespace

template <class Ar>
void io(Ar& ar, OrdersFile& f) {
    game::serial::fields(ar, f.gameName, f.gameId, f.empire, f.turn, f.passwordHash, f.orders, f.startChecksum, f.endChecksum);
}

std::vector<uint8_t> encodeOrdersFile(const OrdersFile& f) { return game::wrapEnvelope(kPlrMagic, game::serial::encode(f)); }

std::expected<OrdersFile, std::string> decodeOrdersFile(std::span<const uint8_t> bytes) {
    auto env = game::unwrapEnvelope(bytes, kPlrMagic, kWhat);
    if (!env) return std::unexpected(env.error());
    OrdersFile f;
    std::string error;
    if (!game::serial::decode(env->payload, f, error, env->version)) return std::unexpected(std::format("the {} is corrupt: {}", kWhat, error));
    return f;
}

std::expected<void, std::string> writeOrdersFile(const fs::path& file, const OrdersFile& f) {
    return game::writeFileAtomic(file, encodeOrdersFile(f));
}

std::expected<OrdersFile, std::string> readOrdersFile(const fs::path& file) {
    auto bytes = game::readFileBytes(file, size_t{64} << 20);
    if (!bytes) return std::unexpected(bytes.error());
    auto f = decodeOrdersFile(*bytes);
    if (!f) return std::unexpected(std::format("{}: {}", file.filename().string(), f.error()));
    return f;
}

std::string ordersFileName(const game::SaveInfo& info, game::EmpireId empire) {
    std::string base;
    for (char c : info.gameName) {
        const auto u = static_cast<unsigned char>(c);
        base.push_back(std::isalnum(u) || c == '-' || c == '_' ? c : '_');
    }
    if (base.empty()) base = "game";
    return std::format("{}_{:02}{}", base, empire.value + 1, kOrdersExtension);
}

std::expected<fs::path, std::string> writePlayerOrders(const fs::path& dir, const game::SaveInfo& info, const game::EmpireOrders& orders,
                                                       std::string_view passwordHash) {
    OrdersFile f;
    f.gameName = info.gameName;
    f.gameId = info.gameId;
    f.empire = orders.empire;
    f.turn = orders.turn;
    f.passwordHash = std::string(passwordHash);
    f.orders = orders;
    const fs::path file = dir / ordersFileName(info, orders.empire);
    if (auto r = writeOrdersFile(file, f); !r) return std::unexpected(r.error());
    return file;
}

std::expected<fs::path, std::string> writePlayerTurn(const fs::path& dir, const game::SaveInfo& info, uint64_t startChecksum,
                                                     const game::EmpireOrders& commands, uint64_t endChecksum, std::string_view passwordHash) {
    OrdersFile f;
    f.gameName = info.gameName;
    f.gameId = info.gameId;
    f.empire = commands.empire;
    f.turn = commands.turn;
    f.passwordHash = std::string(passwordHash);
    f.orders = commands;
    f.startChecksum = startChecksum;
    f.endChecksum = endChecksum;
    const fs::path file = dir / ordersFileName(info, commands.empire);
    if (auto r = writeOrdersFile(file, f); !r) return std::unexpected(r.error());
    return file;
}

std::expected<ProcessReport, std::string> processTurn(const game::Rules& rules, game::GameState& state, const game::SaveInfo& info,
                                                      const fs::path& ordersDir) {
    ProcessReport rep;
    rep.turnBefore = state.turn;
    auto refused = [&](const game::TurnResult& r) {
        for (const auto& [empire, why] : r.rejected) {
            const std::string who = empire.valid() && empire.index() < state.empires.size() ? state.empire(empire).name : std::string("?");
            rep.rejectedCommands.push_back(std::format("{}: {}", who, why));
        }
    };
    const bool turnBased = game::turnBased(state);
    // A turn-based game file between player turns goes on to the next human
    // first; players make their .plr from that game.
    bool resumed = false;
    if (turnBased && !state.gameOver && !state.playerTurn.started) {
        refused(game::resumeTurnBased(rules, state));
        resumed = true;
    }
    const game::EmpireId active = turnBased ? game::activePlayer(state) : game::EmpireId{};
    const bool playerTurn = turnBased && active.valid() && state.playerTurn.started;
    const uint64_t startChecksum = turnBased ? game::stateChecksum(state) : 0;
    std::error_code ec;
    if (!fs::is_directory(ordersDir, ec)) return std::unexpected(std::format("{}: no such directory", ordersDir.string()));

    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(ordersDir, ec))
        if (entry.is_regular_file(ec) && lowerExtension(entry.path()) == kOrdersExtension) files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    struct Candidate {
        fs::path path;
        OrdersFile file;
        fs::file_time_type time;
    };
    std::vector<std::optional<Candidate>> best(state.empires.size());
    for (const fs::path& path : files) {
        const std::string name = path.filename().string();
        auto f = readOrdersFile(path);
        if (!f) {
            rep.warnings.push_back(f.error());
            continue;
        }
        if (f->gameId != info.gameId || f->gameName != info.gameName) {
            rep.warnings.push_back(std::format("{}: belongs to another game ('{}')", name, f->gameName));
            continue;
        }
        if (f->turn != state.turn) {
            rep.warnings.push_back(std::format("{}: out of date: it is for turn {}, and the game is at turn {}", name, f->turn, state.turn));
            continue;
        }
        if (turnBased && f->empire != active) {
            rep.warnings.push_back(std::format("{}: it is {}'s turn, not {}'s", name, active.valid() ? state.empire(active).name : std::string("nobody"),
                                               f->empire.valid() && f->empire.index() < state.empires.size() ? state.empire(f->empire).name
                                                                                                             : std::string("?")));
            continue;
        }
        if (turnBased && f->startChecksum != startChecksum) {
            rep.warnings.push_back(std::format("{}: made from another copy of the game (not the current game file)", name));
            continue;
        }
        if (!f->empire.valid() || f->empire.index() >= state.empires.size()) {
            rep.warnings.push_back(std::format("{}: names an empire that does not exist", name));
            continue;
        }
        const game::Empire& e = state.empire(f->empire);
        if (e.kind != game::PlayerKind::Human) {
            rep.warnings.push_back(std::format("{}: {} is a computer empire", name, e.name));
            continue;
        }
        if (!e.alive) {
            rep.warnings.push_back(std::format("{}: {} has been destroyed", name, e.name));
            continue;
        }
        if (!checkPassword(e.passwordHash, f->passwordHash)) {
            rep.warnings.push_back(std::format("{}: wrong password for {}", name, e.name));
            continue;
        }
        if (f->orders.empire != f->empire || f->orders.turn != f->turn) {
            rep.warnings.push_back(std::format("{}: its orders do not match its header", name));
            continue;
        }
        Candidate c{path, std::move(*f), fs::last_write_time(path, ec)};
        auto& slot = best[c.file.empire.index()];
        if (!slot) {
            slot = std::move(c);
        } else if (c.time >= slot->time) {
            rep.warnings.push_back(std::format("{}: replaces the older {} for {}", name, slot->path.filename().string(), e.name));
            rep.used.push_back(slot->path);
            slot = std::move(c);
        } else {
            rep.warnings.push_back(std::format("{}: ignored, {} for {} is newer", name, slot->path.filename().string(), e.name));
            rep.used.push_back(path);
        }
    }

    if (turnBased) {
        rep.turnBased = true;
        // One player's turn: its commands one after another, as the player
        // gave them, then the end of its turn (spec 05 §8).
        if (playerTurn) {
            const std::string name = state.empire(active).name;
            game::LiveOptions options;
            if (const auto& chosen = best[active.index()]) {
                rep.submitted.push_back(name);
                rep.used.push_back(chosen->path);
                for (const game::Command& c : chosen->file.orders.commands) refused(game::applyLive(rules, state, active, c));
                if (game::stateChecksum(state) != chosen->file.endChecksum)
                    rep.warnings.push_back(std::format("{}: the replay of {}'s turn differs from the player's own game; the host's result counts",
                                                       chosen->path.filename().string(), name));
            } else {
                rep.playedByComputer.push_back(name);
                options.computerPlays.push_back(active);
            }
            refused(game::endPlayerTurn(rules, state, active, options));
        } else if (!resumed && !state.gameOver) {
            // No human left to play: the computer players play one game turn.
            refused(game::resumeTurnBased(rules, state));
        }
        rep.turnAfter = state.turn;
        rep.nextEmpire = game::activePlayer(state);
        if (rep.nextEmpire.valid() && state.playerTurn.started) rep.next = state.empire(rep.nextEmpire).name;
        else rep.nextEmpire = {};
        return rep;
    }

    std::vector<game::EmpireOrders> orders;
    for (size_t i = 0; i < best.size(); ++i) {
        const game::Empire& e = state.empires[i];
        if (best[i]) {
            orders.push_back(best[i]->file.orders);
            rep.submitted.push_back(e.name);
            rep.used.push_back(best[i]->path);
        } else if (e.kind == game::PlayerKind::Human && e.alive) {
            rep.playedByComputer.push_back(e.name);
        }
    }
    refused(game::processTurn(rules, state, orders));
    rep.turnAfter = state.turn;
    return rep;
}

std::expected<ProcessReport, std::string> processGameFile(const game::Rules& rules, const fs::path& gameFile, const fs::path& ordersDir,
                                                          const ProcessOptions& options) {
    auto loaded = game::loadGame(gameFile);
    if (!loaded) return std::unexpected(loaded.error());
    game::GameState& state = loaded->first;
    const game::SaveInfo& info = loaded->second;
    if (!info.masterPasswordVerifier.empty() && !checkPassword(info.masterPasswordVerifier, options.masterPasswordHash))
        return std::unexpected(std::string("This game has a master password, and the one given does not match."));
    if (!options.allowDataSetMismatch && !info.dataSet.empty()) {
        const std::string mine = game::dataSetIdentity(rules);
        if (!game::sameDataSet(info.dataSet, mine))
            return std::unexpected(std::format("The game was created with data set {}, but this data set is {}.", info.dataSet, mine));
    }
    if (std::string problem = game::validateState(state, &rules); !problem.empty())
        return std::unexpected("The game does not fit this data set: " + problem);
    if (state.gameOver) return std::unexpected(std::string("The game is over."));

    auto rep = processTurn(rules, state, info, ordersDir);
    if (!rep) return rep;

    // Keep the previous turn next to the game file, then replace it.
    std::error_code ec;
    fs::path backup = gameFile;
    backup += ".bak";
    fs::copy_file(gameFile, backup, fs::copy_options::overwrite_existing, ec);
    if (auto saved = game::saveGame(gameFile, state, info); !saved) return std::unexpected(saved.error());

    if (options.deleteProcessed)
        for (const fs::path& p : rep->used)
            if (!fs::remove(p, ec) && ec) rep->warnings.push_back(std::format("{}: could not delete: {}", p.filename().string(), ec.message()));
    return rep;
}

} // namespace opense4::net::pbem
