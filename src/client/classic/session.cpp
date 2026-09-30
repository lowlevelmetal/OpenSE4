#include "client/classic/session.hpp"

#include "client/classic/screens/setup_model.hpp"
#include "core/log.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <format>

namespace opense4::client::classic {

ClassicSession::ClassicSession(std::shared_ptr<const game::Rules> rules, game::GameState state, game::EmpireId player, SessionKind kind)
    : rules_(std::move(rules)), state_(std::move(state)), player_(player), kind_(kind) {
    ended_.assign(state_.empires.size(), 0);
    // A PBEM game file already holds the player's turn (loadPbemGame).
    if (turnBased() && kind_ != SessionKind::NetworkClient && kind_ != SessionKind::Pbem) resumeTurnBased();
    if (turnBased() && kind_ == SessionKind::NetworkClient) waiting_ = !myTurn();
}

std::unique_ptr<ClassicSession> ClassicSession::pbem(std::shared_ptr<const game::Rules> rules, PbemGame game, PbemTurn turn,
                                                     std::filesystem::path draftsDir) {
    const game::EmpireId player = turn.empire;
    auto session = std::make_unique<ClassicSession>(std::move(rules), std::move(game.state), player, SessionKind::Pbem);
    session->pbem_ = std::move(turn);
    session->pbemDrafts_ = std::move(draftsDir);
    session->waiting_ = session->turnBased() && !session->myTurn();
    // A turn saved earlier: its commands again, in order (the game is the same, so they play the same).
    if (!session->waiting_ && !session->pbemDrafts_.empty())
        if (auto commands = readPbemDraft(*session->pbem_, session->pbemDrafts_)) {
            for (game::Command& c : *commands) session->issue(std::move(c));
            session->pbemResumed_ = commands->size();
            session->newBattle_.reset();  // battles of the replayed commands were seen when they were given
        }
    return session;
}

std::expected<std::filesystem::path, std::string> ClassicSession::savePbemDraft() const {
    if (!pbem_) return std::unexpected(std::string("This is not a play-by-e-mail game."));
    if (!ordersFile_.empty()) return std::unexpected(std::string("The orders of this turn are already saved for the host."));
    if (pbemDrafts_.empty()) return std::unexpected(std::string("No folder to save the turn in."));
    return writePbemDraft(*pbem_, pbemDrafts_, state_, orders_);
}

bool ClassicSession::myTurn() const {
    return turnBased() && !state_.gameOver && state_.playerTurn.started && state_.playerTurn.empire == player_;
}

const std::vector<game::EntryQuestion>& ClassicSession::questions() const {
    static const std::vector<game::EntryQuestion> none;
    return myTurn() ? state_.playerTurn.questions : none;
}

game::CommandResult ClassicSession::issue(game::Command c) {
    if (waiting_ && kind_ == SessionKind::Pbem)
        return game::CommandResult::fail(ordersFile_.empty() ? "It is not your turn." : "This turn's orders are saved; the turn is over here.");
    if (waiting_) return game::CommandResult::fail("Waiting for the other players");
    if (turnBased() && kind_ == SessionKind::NetworkClient) {
        // The host carries the command out; our copy shows it at once and is
        // replaced by the host's result when that arrives.
        if (!myTurn()) return game::CommandResult::fail("It is not your turn.");
        game::CommandResult r = game::apply(*rules_, state_, player_, c);
        // As the engine does: an answer (even a refused one), or new orders, drop the group's question.
        auto drop = [&](game::VehicleId v, game::FleetId f) {
            std::erase_if(state_.playerTurn.questions, [&](const game::EntryQuestion& q) {
                return f.valid() ? q.fleet == f : !q.fleet.valid() && q.vehicle == v;
            });
            ++revision_;
        };
        if (const auto* a = std::get_if<game::cmd::EnterSector>(&c)) drop(a->vehicle, a->fleet);
        if (!r.ok) return r;
        if (const auto* o = std::get_if<game::cmd::SetOrders>(&c); o && !o->planet.valid()) drop(o->vehicle, o->fleet);
        if (transport_) transport_->playCommand(c);
        orders_.push_back(std::move(c));
        ++revision_;
        return r;
    }
    if (turnBased()) {
        if (kind_ == SessionKind::Pbem && !myTurn()) return game::CommandResult::fail("It is not your turn.");
        const size_t battles = state_.combats.size();
        const game::TurnResult res = game::applyLive(*rules_, state_, player_, c);
        ++revision_;
        for (size_t i = battles; i < state_.combats.size() && !newBattle_; ++i) {
            const auto& who = state_.combats[i].participants;
            if (std::find(who.begin(), who.end(), player_) != who.end()) newBattle_ = i;
        }
        // PBEM: the host replays every command given, refused ones too (a
        // refused answer still settles its question), so all are kept.
        if (kind_ == SessionKind::Pbem) orders_.push_back(c);
        if (!res.rejected.empty()) return game::CommandResult::fail(res.rejected.front().second);
        if (kind_ != SessionKind::Pbem) orders_.push_back(std::move(c));
        return {};
    }
    game::CommandResult r = game::apply(*rules_, state_, player_, c);
    if (r.ok) {
        orders_.push_back(std::move(c));
        ++revision_;
    }
    return r;
}

std::string ClassicSession::empirePasswordValue(std::string_view password) const {
    if (password.empty()) return {};
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem) return net::passwordVerifier(net::hashPassword(password));
    return game::hashPassword(password);
}

void ClassicSession::answer(bool enter) {
    if (questions().empty()) return;
    const game::EntryQuestion q = questions().front();  // the answer drops it (applyLive, or issue() on a network copy)
    issue(game::cmd::EnterSector{q.vehicle, q.fleet, q.where, enter});
}

std::optional<size_t> ClassicSession::takeNewBattle() {
    std::optional<size_t> out = newBattle_;
    newBattle_.reset();
    return out;
}

void ClassicSession::takeResult(const game::TurnResult& result) {
    notices_.clear();
    for (const auto& [empire, text] : result.rejected)
        if (empire == player_) notices_.push_back(text);
}

void ClassicSession::resumeTurnBased() {
    const game::TurnResult result = game::resumeTurnBased(*rules_, state_);
    // The session belongs to the human whose turn it is (hotseat: the next one).
    if (const game::EmpireId e = game::activePlayer(state_); e.valid() && state_.empire(e).kind == game::PlayerKind::Human) player_ = e;
    takeResult(result);
}

void ClassicSession::endTurn() {
    if (waiting_) return;
    if (kind_ == SessionKind::Pbem) {
        // The host processes the turn: write the orders file for it and wait.
        if (!pbem_ || (turnBased() && !myTurn())) return;
        auto file = writePbemOrders(*pbem_, state_, orders_);
        if (!file) {
            pbemError_ = file.error();
            log::warn("PBEM: {}", pbemError_);
            return;
        }
        pbemError_.clear();
        ordersFile_ = *file;
        waiting_ = true;
        if (!pbemDrafts_.empty()) removePbemDraft(*pbem_, pbemDrafts_);
        ++revision_;
        return;
    }
    if (turnBased() && kind_ == SessionKind::NetworkClient) {
        if (!myTurn()) return;
        if (transport_) transport_->endPlayerTurn();
        waiting_ = true;
        return;
    }
    if (turnBased()) {
        // The player's end-of-turn processing; the computer players' turns;
        // then the next human's turn starts.
        const game::TurnResult result = game::endPlayerTurn(*rules_, state_, player_);
        if (const game::EmpireId e = game::activePlayer(state_); e.valid() && state_.empire(e).kind == game::PlayerKind::Human) player_ = e;
        takeResult(result);
        orders_.clear();
        waiting_ = false;
        ++revision_;
        if (onNewTurn) onNewTurn();
        return;
    }
    if (kind_ == SessionKind::NetworkClient) {
        if (transport_) transport_->submitOrders(game::EmpireOrders{player_, state_.turn, orders_});
        waiting_ = true;
        return;
    }
    if (kind_ == SessionKind::Hotseat) {
        ended_[player_.index()] = 1;
        for (const game::Empire& e : state_.empires)
            if (e.alive && e.kind == game::PlayerKind::Human && !ended_[e.id.index()]) {
                setPlayer(e.id);
                orders_.clear();
                ++revision_;
                return;
            }
    }
    // Every human's orders are already applied to this state; an empty list
    // marks them as submitted so the computer does not play for them.
    std::vector<game::EmpireOrders> submitted;
    for (const game::Empire& e : state_.empires)
        if (e.alive && e.kind == game::PlayerKind::Human) submitted.push_back({e.id, state_.turn, {}});
    const game::TurnResult result = game::processTurn(*rules_, state_, submitted);
    notices_.clear();
    for (const auto& [empire, text] : result.rejected)
        if (empire == player_) notices_.push_back(text);
    if (kind_ == SessionKind::Hotseat) {
        for (const game::Empire& e : state_.empires)
            if (e.alive && e.kind == game::PlayerKind::Human) {
                setPlayer(e.id);
                break;
            }
    }
    autosave();
    beginTurn();
}

std::optional<std::filesystem::path> ClassicSession::autosave() {
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem) return std::nullopt;  // the host keeps the game
    const auto name = setup::autosaveName(state_.options.autosaveTurns, state_.turn);
    if (!name) return std::nullopt;
    const std::filesystem::path file = savesDir() / (*name + ".gam");
    if (auto saved = save(file, *name); !saved) {
        autosaveNote_ = std::format("Autosave failed: {}", saved.error());
        log::warn("{}", autosaveNote_);
        return std::nullopt;
    }
    autosaveNote_ = std::format("Saved as {}", *name);
    return file;
}

void ClassicSession::poll() {
    if (!transport_) return;
    auto s = transport_->pollState();
    if (!s) return;
    if (!game::turnBased(*s)) {
        state_ = std::move(*s);
        beginTurn();
        return;
    }
    // Turn-based: the host's state after our commands, a battle we fought in
    // another player's turn, or the turn passing on.
    const bool wasMine = myTurn();
    const uint32_t oldTurn = state_.turn;
    const size_t oldBattles = state_.combats.size();
    state_ = std::move(*s);
    ++revision_;
    if (state_.turn == oldTurn)
        for (size_t i = oldBattles; i < state_.combats.size() && !newBattle_; ++i) {
            const auto& who = state_.combats[i].participants;
            if (std::find(who.begin(), who.end(), player_) != who.end()) newBattle_ = i;
        }
    const bool mine = myTurn();
    if (mine && (!wasMine || state_.turn != oldTurn)) beginTurn();  // our turn starts
    waiting_ = !mine;
}

void ClassicSession::beginTurn() {
    orders_.clear();
    ended_.assign(state_.empires.size(), 0);
    waiting_ = false;
    ++revision_;
    if (onNewTurn) onNewTurn();
}

void ClassicSession::setPlayer(game::EmpireId e) {
    player_ = e;
    ++revision_;
}

void ClassicSession::replaceState(game::GameState s) {
    state_ = std::move(s);
    newBattle_.reset();
    if (turnBased() && kind_ != SessionKind::NetworkClient && kind_ != SessionKind::Pbem) resumeTurnBased();
    beginTurn();
}

void ClassicSession::simulateTurns(int n) {
    if (kind_ == SessionKind::Pbem) return;  // only the host plays PBEM turns
    // A turn-based game plays whole game turns the same way (processTurn).
    for (int i = 0; i < n && !state_.gameOver; ++i) game::processTurn(*rules_, state_, {});
    if (n > 0 && turnBased() && kind_ != SessionKind::NetworkClient) resumeTurnBased();
    if (n > 0) beginTurn();
}

std::expected<void, std::string> ClassicSession::save(const std::filesystem::path& file, const std::string& gameName) const {
    game::SaveInfo info;
    info.gameName = gameName;
    info.dataSet = rules_->data().dataDir.parent_path().filename().string();
    info.turn = state_.turn;
    for (const game::Empire& e : state_.empires) info.empires.push_back(e.name);
    return game::saveGame(file, state_, info);
}

std::expected<std::unique_ptr<ClassicSession>, std::string> ClassicSession::load(std::shared_ptr<const game::Rules> rules,
                                                                                 const std::filesystem::path& file) {
    auto loaded = game::loadGame(file);
    if (!loaded) return std::unexpected(loaded.error());
    game::GameState& s = loaded->first;
    game::EmpireId player;
    int humans = 0;
    for (const game::Empire& e : s.empires)
        if (e.alive && e.kind == game::PlayerKind::Human) {
            if (!player.valid()) player = e.id;
            ++humans;
        }
    if (!player.valid()) player = game::EmpireId{0u};
    const SessionKind kind = humans > 1 ? SessionKind::Hotseat : SessionKind::Local;
    return std::make_unique<ClassicSession>(std::move(rules), std::move(s), player, kind);
}

std::filesystem::path userDataDir() {
    std::filesystem::path dir;
    if (char* pref = SDL_GetPrefPath("", "OpenSE4")) {
        dir = pref;
        SDL_free(pref);
    } else {
        dir = std::filesystem::current_path() / "userdata";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path savesDir() {
    const std::filesystem::path dir = userDataDir() / "saves";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace opense4::client::classic
