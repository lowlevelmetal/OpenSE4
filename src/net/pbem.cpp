#include "net/pbem.hpp"

#include "game/diplomacy.hpp"
#include "game/redact.hpp"
#include "game/serialize_io.hpp"
#include "game/turn.hpp"
#include "net/protocol.hpp"
#include "net/types.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <optional>
#include <set>

namespace opense4::net::pbem {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kPlrMagic = "OSE4PLR2";
constexpr std::string_view kDraftMagic = "OSE4DRFT";
constexpr std::string_view kTurnMagic = "OSE4TURN";
constexpr std::string_view kWhat = "orders file (.plr)";
constexpr std::string_view kDraftWhat = "draft of a turn";
constexpr std::string_view kTurnWhat = "player turn file (.turn)";
constexpr std::string_view kTurnDomain = "OpenSE4 turn file v1";
constexpr std::string_view kOrdersDomain = "OpenSE4 orders file v1";

std::string lowerExtension(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::string fileBase(const game::SaveInfo& info) {
    std::string base;
    for (char c : info.gameName) {
        const auto u = static_cast<unsigned char>(c);
        base.push_back(std::isalnum(u) || c == '-' || c == '_' ? c : '_');
    }
    return base.empty() ? std::string("game") : base;
}

// What a turn file says in the clear, bound to its encrypted view.
struct TurnHeader {
    game::SaveInfo info;
    game::EmpireId empire;
    bool turnBased = false;
    std::string verifier;
    crypto::Key hostKey{};
};

// The .plr on the disk: what it is for, and the orders file encrypted to the host.
struct SealedOrders {
    uint64_t gameId = 0;
    game::EmpireId empire;
    uint32_t turn = 0;
    crypto::Key ephemeral{};
    crypto::Mac mac{};
    std::vector<uint8_t> content;
};

template <class Ar> void io(Ar& ar, TurnHeader& h) { game::serial::fields(ar, h.info, h.empire, h.turnBased, h.verifier, h.hostKey); }
template <class Ar> void io(Ar& ar, SealedOrders& o) { game::serial::fields(ar, o.gameId, o.empire, o.turn, o.ephemeral, o.mac, o.content); }

} // namespace

template <class Ar>
void io(Ar& ar, OrdersFile& f) {
    game::serial::fields(ar, f.gameName, f.gameId, f.empire, f.turn, f.orders, f.startChecksum, f.revision, f.verifier, f.signature,
                         f.legacyPasswordHash);
}

template <class Ar>
void io(Ar& ar, TurnFile& f) {
    game::serial::fields(ar, f.info, f.empire, f.turnBased, f.verifier, f.hostKey, f.encrypted, f.ephemeral, f.mac, f.content);
}

template <class Ar> void io(Ar& ar, TurnView& v) { game::serial::fields(ar, v.viewChecksum, v.view); }

namespace {

std::vector<uint8_t> headerBytes(const TurnFile& f) { return game::serial::encode(TurnHeader{f.info, f.empire, f.turnBased, f.verifier, f.hostKey}); }

// What a .plr says in the clear, bound to its content (the fresh key is
// bound by the encryption key itself).
std::vector<uint8_t> routeBytes(const SealedOrders& o) {
    std::vector<uint8_t> out;
    game::serial::ByteWriter w(out);
    uint64_t gameId = o.gameId;
    game::EmpireId empire = o.empire;
    uint32_t turn = o.turn;
    game::serial::fields(w, gameId, empire, turn);
    return out;
}

} // namespace

// ---- Turn files ----------------------------------------------------------------------------------

std::vector<uint8_t> encodeTurnView(const TurnView& v) { return game::serial::encode(v); }

std::vector<uint8_t> encodeTurnFile(const TurnFile& f) { return game::wrapEnvelope(kTurnMagic, game::serial::encode(f)); }

std::expected<TurnFile, std::string> decodeTurnFile(std::span<const uint8_t> bytes) {
    auto env = game::unwrapEnvelope(bytes, kTurnMagic, kTurnWhat);
    if (!env) return std::unexpected(env.error());
    TurnFile f;
    std::string error;
    if (!game::serial::decode(env->payload, f, error, env->version))
        return std::unexpected(std::format("the {} is corrupt: {}", kTurnWhat, error));
    return f;
}

std::expected<TurnFile, std::string> readTurnFile(const fs::path& file) {
    auto bytes = game::readFileBytes(file);
    if (!bytes) return std::unexpected(bytes.error());
    auto f = decodeTurnFile(*bytes);
    if (!f) return std::unexpected(std::format("{}: {}", file.filename().string(), f.error()));
    return f;
}

std::expected<TurnView, std::string> openTurnFile(const TurnFile& f, const std::optional<PasswordKeys>& keys) {
    std::vector<uint8_t> content = f.content;
    if (f.encrypted) {
        if (!keys || !crypto::openFrom(keys->box, f.ephemeral, kTurnDomain, headerBytes(f), content, f.mac))
            return std::unexpected(std::string("the turn file does not open with this password"));
    }
    TurnView v;
    std::string error;
    if (!game::serial::decode(content, v, error)) return std::unexpected(std::format("the {} is corrupt: {}", kTurnWhat, error));
    return v;
}

std::string turnFileName(const game::SaveInfo& info, game::EmpireId empire) {
    return std::format("{}_{:02}{}", fileBase(info), empire.value + 1, kTurnExtension);
}

void readForTurn(const game::Rules& rules, game::GameState& state) { game::diplomacy::recalculateColonies(rules, state); }

std::vector<game::EmpireId> empiresToPlay(const game::GameState& state) {
    std::vector<game::EmpireId> out;
    if (state.gameOver) return out;
    if (game::turnBased(state)) {
        const game::EmpireId active = game::activePlayer(state);
        if (state.playerTurn.started && active.valid() && active.index() < state.empires.size() &&
            state.empire(active).kind == game::PlayerKind::Human && state.empire(active).alive)
            out.push_back(active);
        return out;
    }
    for (const game::Empire& e : state.empires)
        if (e.alive && e.kind == game::PlayerKind::Human) out.push_back(e.id);
    return out;
}

game::GameState playerView(const game::Rules& rules, const game::GameState& state, game::EmpireId empire) {
    return game::redactForEmpire(rules, state, empire);
}

std::expected<std::vector<std::pair<game::EmpireId, fs::path>>, std::string> writeTurnFiles(const game::Rules& rules, const fs::path& gameFile,
                                                                                            const fs::path& dir, const crypto::Key& hostKey) {
    // From the file as saved, exactly as the next processing will read it.
    auto loaded = game::loadGame(gameFile);
    if (!loaded) return std::unexpected(loaded.error());
    game::GameState& s = loaded->first;
    game::SaveInfo info = loaded->second;
    readForTurn(rules, s);
    // A turn-based game between player turns plays on to the next human, as processTurn does first.
    if (game::turnBased(s) && !s.gameOver && !s.playerTurn.started) game::resumeTurnBased(rules, s);
    info.turn = s.turn;
    info.empires.clear();
    for (const game::Empire& e : s.empires) info.empires.push_back(e.name);
    // The host's master password stays with the host, as with a network player's copy.
    info.masterPasswordVerifier.clear();
    std::vector<std::pair<game::EmpireId, fs::path>> written;
    for (game::EmpireId e : empiresToPlay(s)) {
        const game::GameState view = playerView(rules, s, e);
        TurnFile f;
        f.info = info;
        f.empire = e;
        f.turnBased = game::turnBased(s);
        f.verifier = s.empire(e).passwordHash;
        f.hostKey = hostKey;
        f.content = game::serial::encode(TurnView{game::stateChecksum(view), game::serializeState(view)});
        // For the empire's eyes only, when it has a key to encrypt to.
        if (const auto keys = verifierKeys(f.verifier)) {
            f.encrypted = true;
            crypto::sealTo(keys->box, kTurnDomain, headerBytes(f), f.content, f.ephemeral, f.mac);
        }
        const fs::path file = dir / turnFileName(info, e);
        if (auto r = game::writeFileAtomic(file, encodeTurnFile(f)); !r) return std::unexpected(r.error());
        written.emplace_back(e, file);
    }
    return written;
}

// ---- Orders files ------------------------------------------------------------------------------------

std::vector<uint8_t> encodeOrdersFile(const OrdersFile& f, const crypto::Key& hostKey) {
    SealedOrders o;
    o.gameId = f.gameId;
    o.empire = f.empire;
    o.turn = f.turn;
    o.content = game::serial::encode(f);
    crypto::sealTo(hostKey, kOrdersDomain, routeBytes(o), o.content, o.ephemeral, o.mac);
    return game::wrapEnvelope(kPlrMagic, game::serial::encode(o));
}

std::expected<OrdersFile, std::string> decodeOrdersFile(std::span<const uint8_t> bytes, const crypto::KeyPair& hostKey) {
    auto env = game::unwrapEnvelope(bytes, kPlrMagic, kWhat);
    if (!env) return std::unexpected(env.error());
    SealedOrders o;
    std::string error;
    if (!game::serial::decode(env->payload, o, error, env->version)) return std::unexpected(std::format("the {} is corrupt: {}", kWhat, error));
    if (!crypto::openFrom(hostKey, o.ephemeral, kOrdersDomain, routeBytes(o), o.content, o.mac))
        return std::unexpected(std::format("the {} is not for this host's key, or was changed on its way", kWhat));
    OrdersFile f;
    if (!game::serial::decode(o.content, f, error, env->version)) return std::unexpected(std::format("the {} is corrupt: {}", kWhat, error));
    if (f.gameId != o.gameId || f.empire != o.empire || f.turn != o.turn)
        return std::unexpected(std::format("the {} does not match what it says it is for", kWhat));
    return f;
}

std::expected<void, std::string> writeOrdersFile(const fs::path& file, const OrdersFile& f, const crypto::Key& hostKey) {
    return game::writeFileAtomic(file, encodeOrdersFile(f, hostKey));
}

std::expected<OrdersFile, std::string> readOrdersFile(const fs::path& file, const crypto::KeyPair& hostKey) {
    auto bytes = game::readFileBytes(file, size_t{64} << 20);
    if (!bytes) return std::unexpected(bytes.error());
    auto f = decodeOrdersFile(*bytes, hostKey);
    if (!f) return std::unexpected(std::format("{}: {}", file.filename().string(), f.error()));
    return f;
}

std::expected<void, std::string> writeDraft(const fs::path& file, const OrdersFile& f) {
    return game::writeFileAtomic(file, game::wrapEnvelope(kDraftMagic, game::serial::encode(f)));
}

std::expected<OrdersFile, std::string> readDraft(const fs::path& file) {
    auto bytes = game::readFileBytes(file, size_t{64} << 20);
    if (!bytes) return std::unexpected(bytes.error());
    auto env = game::unwrapEnvelope(*bytes, kDraftMagic, kDraftWhat);
    if (!env) return std::unexpected(env.error());
    OrdersFile f;
    std::string error;
    if (!game::serial::decode(env->payload, f, error, env->version)) return std::unexpected(std::format("the {} is corrupt: {}", kDraftWhat, error));
    return f;
}

crypto::Key ordersDigest(const OrdersFile& f) {
    return crypto::Hash()
        .add("OpenSE4 orders file v2")
        .add(f.gameName)
        .add(f.gameId)
        .add(static_cast<uint64_t>(f.empire.value))
        .add(static_cast<uint64_t>(f.turn))
        .add(game::serializeOrders(f.orders))
        .add(f.startChecksum)
        .add(f.revision)
        .add(f.verifier)
        .finish32();
}

void signOrdersFile(OrdersFile& f, const std::optional<PasswordKeys>& keys, std::string_view legacyPasswordHash) {
    f.verifier = keys ? keys->verifier() : std::string{};
    f.signature = signWith(keys, ordersDigest(f));
    f.legacyPasswordHash = std::string(legacyPasswordHash);
}

std::string ordersFileName(const game::SaveInfo& info, game::EmpireId empire) {
    return std::format("{}_{:02}{}", fileBase(info), empire.value + 1, kOrdersExtension);
}

std::expected<fs::path, std::string> writePlayerOrders(const fs::path& dir, const game::SaveInfo& info, const game::EmpireOrders& orders,
                                                       uint64_t startChecksum, const std::optional<PasswordKeys>& keys, const crypto::Key& hostKey,
                                                       std::string_view legacyPasswordHash) {
    OrdersFile f;
    f.gameName = info.gameName;
    f.gameId = info.gameId;
    f.empire = orders.empire;
    f.turn = orders.turn;
    f.orders = orders;
    f.startChecksum = startChecksum;
    // When it was made: of two files the later counts (signed, unlike a file's date).
    f.revision = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    signOrdersFile(f, keys, legacyPasswordHash);
    const fs::path file = dir / ordersFileName(info, orders.empire);
    if (auto r = writeOrdersFile(file, f, hostKey); !r) return std::unexpected(r.error());
    return file;
}

// ---- The host's processing ---------------------------------------------------------------------------

std::expected<ProcessReport, std::string> processTurn(const game::Rules& rules, game::GameState& state, const game::SaveInfo& info,
                                                      const fs::path& ordersDir, const crypto::KeyPair& hostKey) {
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
    // first; that player's turn file was made from that game.
    bool resumed = false;
    if (turnBased && !state.gameOver && !state.playerTurn.started) {
        refused(game::resumeTurnBased(rules, state));
        resumed = true;
    }
    const game::EmpireId active = turnBased ? game::activePlayer(state) : game::EmpireId{};
    const bool playerTurn = turnBased && active.valid() && state.playerTurn.started;
    std::error_code ec;
    if (!fs::is_directory(ordersDir, ec)) return std::unexpected(std::format("{}: no such directory", ordersDir.string()));

    // The checksum of the view each empire's turn file holds, made as needed.
    std::map<uint32_t, uint64_t> views;
    auto viewChecksum = [&](game::EmpireId e) {
        auto it = views.find(e.value);
        if (it == views.end()) it = views.emplace(e.value, game::stateChecksum(playerView(rules, state, e))).first;
        return it->second;
    };

    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(ordersDir, ec))
        if (entry.is_regular_file(ec) && lowerExtension(entry.path()) == kOrdersExtension) files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    struct Candidate {
        fs::path path;
        OrdersFile file;
        std::string upgrade;  // the verifier to keep instead of an OpenSE4 0.6 one
    };
    std::vector<std::vector<Candidate>> valid(state.empires.size());
    for (const fs::path& path : files) {
        const std::string name = path.filename().string();
        auto f = readOrdersFile(path, hostKey);
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
        if (f->orders.empire != f->empire || f->orders.turn != f->turn) {
            rep.warnings.push_back(std::format("{}: its orders do not match its header", name));
            continue;
        }
        if (!proto::usablePasswordValues(f->orders.commands)) {
            rep.warnings.push_back(std::format("{}: its orders set a password of a kind the host does not take", name));
            continue;
        }
        // Made from the turn file of this very game and turn (the host's own view of it).
        if (f->startChecksum != viewChecksum(f->empire)) {
            rep.warnings.push_back(std::format("{}: made from another turn file than {}'s current one", name, e.name));
            continue;
        }
        // The password: its signature of the file. For a verifier of
        // OpenSE4 0.6, the old password's hash once, and the signature of a
        // new password, which counts from then on (the old hash travelled in
        // the clear, so no key may come from it).
        std::string upgrade;
        if (isLegacyVerifier(e.passwordHash)) {
            if (!checkLegacyPassword(e.passwordHash, f->legacyPasswordHash)) {
                rep.warnings.push_back(std::format("{}: wrong password for {}", name, e.name));
                continue;
            }
            if (f->verifier.empty() || !usableVerifier(f->verifier)) {
                rep.warnings.push_back(std::format("{}: {} needs a new password", name, e.name));
                continue;
            }
            upgrade = f->verifier;
            if (!checkPasswordSignature(upgrade, ordersDigest(*f), f->signature)) {
                rep.warnings.push_back(std::format("{}: the file was changed after it was signed", name));
                continue;
            }
        } else if (!checkPasswordSignature(e.passwordHash, ordersDigest(*f), f->signature)) {
            rep.warnings.push_back(std::format("{}: wrong password for {}, or the file was changed after it was signed", name, e.name));
            continue;
        }
        const size_t index = f->empire.index();
        valid[index].push_back(Candidate{path, std::move(*f), std::move(upgrade)});
    }
    // One file per empire: the highest (signed) revision. Two different moves
    // of an OpenSE4 0.6 empire to a new password, or two different files of
    // one revision, leave the host unable to tell which is the player's: none
    // counts, and the files stay for the host to look at.
    std::vector<std::optional<Candidate>> best(state.empires.size());
    for (size_t i = 0; i < valid.size(); ++i) {
        std::vector<Candidate>& list = valid[i];
        if (list.empty()) continue;
        const std::string& who = state.empires[i].name;
        std::set<std::string> upgrades;
        for (const Candidate& c : list)
            if (!c.upgrade.empty()) upgrades.insert(c.upgrade);
        if (upgrades.size() > 1) {
            std::string names;
            for (const Candidate& c : list) names += (names.empty() ? "" : ", ") + c.path.filename().string();
            rep.warnings.push_back(std::format("{}: different new passwords for {}; none is taken (ask the player which is theirs)", names, who));
            continue;
        }
        std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return a.file.revision > b.file.revision; });
        if (list.size() > 1 && list[0].file.revision == list[1].file.revision && ordersDigest(list[0].file) != ordersDigest(list[1].file)) {
            rep.warnings.push_back(std::format("{} and {}: two different files of the same revision for {}; none is taken",
                                               list[0].path.filename().string(), list[1].path.filename().string(), who));
            continue;
        }
        for (size_t k = 1; k < list.size(); ++k) {
            rep.warnings.push_back(std::format("{}: ignored, {} for {} is newer", list[k].path.filename().string(), list[0].path.filename().string(), who));
            rep.used.push_back(list[k].path);
        }
        best[i] = std::move(list[0]);
    }
    // Verifiers of OpenSE4 0.6 are replaced once their password was shown.
    for (auto& c : best)
        if (c && !c->upgrade.empty()) {
            state.empire(c->file.empire).passwordHash = c->upgrade;
            rep.warnings.push_back(std::format("{}'s new password counts from now on", state.empire(c->file.empire).name));
        }

    if (turnBased) {
        rep.turnBased = true;
        // One player's turn: its commands one after another, as the player
        // gave them, then the end of its turn (spec 05 §8). The player's copy
        // previewed them on its own view; the whole game decides.
        if (playerTurn) {
            const std::string name = state.empire(active).name;
            game::LiveOptions options;
            if (const auto& chosen = best[active.index()]) {
                rep.submitted.push_back(name);
                rep.used.push_back(chosen->path);
                for (const game::Command& c : chosen->file.orders.commands) refused(game::applyLive(rules, state, active, c));
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

namespace {

std::expected<ProcessReport, std::string> processGameFileWithKeys(const game::Rules& rules, const fs::path& gameFile, const fs::path& ordersDir,
                                                                  const ProcessOptions& options) {
    auto loaded = game::loadGame(gameFile);
    if (!loaded) return std::unexpected(loaded.error());
    game::GameState& state = loaded->first;
    game::SaveInfo info = loaded->second;
    if (!info.masterPasswordVerifier.empty() && !checkPassword(info.masterPasswordVerifier, options.masterPassword, info.gameId))
        return std::unexpected(std::string("This game has a master password, and the one given does not match."));
    // A master password of OpenSE4 0.6 moves to the current kind, from the password itself.
    if (isLegacyVerifier(info.masterPasswordVerifier)) info.masterPasswordVerifier = passwordVerifier(options.masterPassword, info.gameId);
    if (!options.allowDataSetMismatch && !info.dataSet.empty()) {
        const std::string mine = game::dataSetIdentity(rules);
        if (!game::sameDataSet(info.dataSet, mine))
            return std::unexpected(std::format("The game was created with data set {}, but this data set is {}.", info.dataSet, mine));
    }
    if (std::string problem = game::validateState(state, &rules); !problem.empty())
        return std::unexpected("The game does not fit this data set: " + problem);
    readForTurn(rules, state);
    if (state.gameOver) return std::unexpected(std::string("The game is over."));
    if (!options.resetPasswords.empty() && game::turnBased(state))
        return std::unexpected(std::string("Reset Passwords is for simultaneous games."));
    for (game::EmpireId e : options.resetPasswords)
        if (!e.valid() || e.index() >= state.empires.size()) return std::unexpected(std::string("Reset Passwords names an empire that does not exist."));

    auto rep = processTurn(rules, state, info, ordersDir, options.hostKey);
    if (!rep) return rep;
    // The new passwords win over those the orders files carried (spec 06 §1.9).
    for (game::EmpireId e : options.resetPasswords) {
        std::string password = resetPassword();
        state.empire(e).passwordHash = passwordVerifier(password, info.gameId);
        rep->passwordResets.emplace_back(e, std::move(password));
    }

    // Keep the previous turn next to the game file, then replace it.
    std::error_code ec;
    fs::path backup = gameFile;
    backup += ".bak";
    fs::copy_file(gameFile, backup, fs::copy_options::overwrite_existing, ec);
    if (auto saved = game::saveGame(gameFile, state, info); !saved) return std::unexpected(saved.error());

    // Each player of the new turn gets their own view, never the whole game.
    const fs::path turnDir = !options.turnFilesDir.empty() ? options.turnFilesDir : gameFile.has_parent_path() ? gameFile.parent_path() : fs::path(".");
    auto turnFiles = writeTurnFiles(rules, gameFile, turnDir, options.hostKey.publicKey);
    if (!turnFiles) return std::unexpected("The turn was processed and saved, but the turn files could not be written: " + turnFiles.error());
    rep->turnFiles = std::move(*turnFiles);

    if (options.deleteProcessed)
        for (const fs::path& p : rep->used)
            if (!fs::remove(p, ec) && ec) rep->warnings.push_back(std::format("{}: could not delete: {}", p.filename().string(), ec.message()));
    return rep;
}

} // namespace

std::expected<ProcessReport, std::string> processGameFile(const game::Rules& rules, const fs::path& gameFile, const fs::path& ordersDir,
                                                          const ProcessOptions& options) {
    // The master password's check and the new passwords run Argon2id, which
    // needs its memory at once; nothing is written before they are done.
    try {
        return processGameFileWithKeys(rules, gameFile, ordersDir, options);
    } catch (const PasswordWorkError& e) {
        return std::unexpected(std::string(e.what()));
    }
}

} // namespace opense4::net::pbem
