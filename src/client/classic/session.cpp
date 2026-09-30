#include "client/classic/session.hpp"

#include "client/classic/screens/setup_model.hpp"
#include "core/log.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <format>

namespace opense4::client::classic {

ClassicSession::ClassicSession(std::shared_ptr<const game::Rules> rules, game::GameState state, game::EmpireId player, SessionKind kind)
    : rules_(std::move(rules)), state_(std::move(state)), player_(player), kind_(kind) {
    ended_.assign(state_.empires.size(), 0);
    if (turnBased() && kind_ != SessionKind::NetworkClient) resumeTurnBased();
    if (turnBased() && kind_ == SessionKind::NetworkClient) waiting_ = !myTurn();
}

bool ClassicSession::myTurn() const {
    return turnBased() && !state_.gameOver && state_.playerTurn.started && state_.playerTurn.empire == player_;
}

const std::vector<game::EntryQuestion>& ClassicSession::questions() const {
    static const std::vector<game::EntryQuestion> none;
    return myTurn() ? state_.playerTurn.questions : none;
}

game::CommandResult ClassicSession::issue(game::Command c) {
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
        if (call_ != Call::None) return game::CommandResult::fail("A battle waits to be fought first.");
        issued_ = {};
        beginCall(Call::Issue, std::move(c));
        // While a battle waits for its answer the order is under way; a refusal shows up as a notice.
        return issued_;
    }
    game::CommandResult r = game::apply(*rules_, state_, player_, c);
    if (r.ok) {
        orders_.push_back(std::move(c));
        ++revision_;
    }
    return r;
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

void ClassicSession::resumeTurnBased() { beginCall(Call::Resume); }

// Network games (the host's own player included, whose session is a network
// client too) never ask: their battles are strategic (docs/MULTIPLAYER.md).
bool ClassicSession::offersTactical() const { return kind_ != SessionKind::NetworkClient && game::tacticalOffered(state_); }

void ClassicSession::beginCall(Call call, std::optional<game::Command> command) {
    call_ = call;
    callCommand_ = std::move(command);
    callBattles_ = state_.combats.size();
    answers_.clear();
    fought_.clear();
    battle_.reset();
    runCall();
}

void ClassicSession::runCall() {
    const std::vector<game::BattleAnswer>* answers = offersTactical() ? &answers_ : nullptr;
    game::TurnResult res;
    switch (call_) {
        case Call::Issue: res = game::applyLive(*rules_, state_, player_, *callCommand_, answers); break;
        case Call::EndTurn: res = game::endPlayerTurn(*rules_, state_, player_, {}, answers); break;
        case Call::Resume: res = game::resumeTurnBased(*rules_, state_, {}, answers); break;
        case Call::None: return;
    }
    ++revision_;
    if (res.battle) {
        // The call stopped before a battle with human sides; the game is as it was.
        battle_ = std::move(res.battle);
        log::info("A battle at system {} ({}, {}) asks {} human side(s) for Tactical or Strategic", battle_->where.system.value,
                  battle_->where.sector.x, battle_->where.sector.y, battle_->humans.size());
        return;
    }
    const Call call = std::exchange(call_, Call::None);
    const bool tactical = std::any_of(answers_.begin(), answers_.end(), [](const game::BattleAnswer& a) { return !a.tactical.empty(); });
    // The battles fought in the Tactical Combat window must have come out the same here.
    for (const game::CombatRecord& fought : fought_) {
        const auto same = [&](const game::CombatRecord& r) {
            return r.location == fought.location && r.turn == fought.turn && r.summary == fought.summary && r.pieces.size() == fought.pieces.size() &&
                   r.events.size() == fought.events.size();
        };
        if (std::none_of(state_.combats.begin() + std::ptrdiff_t(std::min(callBattles_, state_.combats.size())), state_.combats.end(), same))
            log::warn("The tactical battle at system {} came out differently in the game", fought.location.system.value);
    }
    fought_.clear();
    const bool answered = !answers_.empty();
    answers_.clear();
    auto nextHuman = [&] {
        // The session belongs to the human whose turn it is (hotseat: the next one).
        if (const game::EmpireId e = game::activePlayer(state_); e.valid() && state_.empire(e).kind == game::PlayerKind::Human) player_ = e;
    };
    switch (call) {
        case Call::Issue: {
            // Attack Sector questions stay in the game (GameState::playerTurn.questions).
            // A battle fought here in the Tactical Combat window has been seen already.
            if (!tactical)
                for (size_t i = callBattles_; i < state_.combats.size() && !newBattle_; ++i) {
                    const auto& who = state_.combats[i].participants;
                    if (std::find(who.begin(), who.end(), player_) != who.end()) newBattle_ = i;
                }
            if (!res.rejected.empty()) {
                issued_ = game::CommandResult::fail(res.rejected.front().second);
                if (answered) notices_.push_back(res.rejected.front().second);
            } else {
                orders_.push_back(std::move(*callCommand_));
                issued_ = {};
            }
            break;
        }
        case Call::EndTurn:
            nextHuman();
            takeResult(res);
            orders_.clear();
            waiting_ = false;
            if (onNewTurn) onNewTurn();
            break;
        case Call::Resume:
            nextHuman();
            takeResult(res);
            break;
        case Call::None: break;
    }
    callCommand_.reset();
}

void ClassicSession::answerBattle(game::BattleAnswer answer) {
    if (!battle_ || call_ == Call::None) return;
    answers_.push_back(std::move(answer));
    battle_.reset();
    runCall();
}

void ClassicSession::startTactical(TacticalFight fight) { tactical_ = std::make_unique<TacticalFight>(std::move(fight)); }

void ClassicSession::endTactical() {
    if (!tactical_) return;
    std::unique_ptr<TacticalFight> fight = std::move(tactical_);
    if (fight->kind != TacticalFight::Kind::Game || !fight->battle) return;
    // Phases left are played by the strategies, as a script that runs out does.
    fight->battle->finish();
    fought_.push_back(fight->battle->record());
    answerBattle(game::BattleAnswer{fight->players, fight->battle->script()});
}

void ClassicSession::endTurn() {
    if (waiting_) return;
    if (turnBased() && kind_ == SessionKind::NetworkClient) {
        if (!myTurn()) return;
        if (transport_) transport_->endPlayerTurn();
        waiting_ = true;
        return;
    }
    if (turnBased()) {
        // The player's end-of-turn processing; the computer players' turns;
        // then the next human's turn starts (after any battles that ask).
        if (call_ != Call::None) return;
        beginCall(Call::EndTurn);
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
    if (kind_ == SessionKind::NetworkClient) return std::nullopt;  // the host keeps the game
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
    call_ = Call::None;
    battle_.reset();
    answers_.clear();
    tactical_.reset();
    if (turnBased() && kind_ != SessionKind::NetworkClient) resumeTurnBased();
    beginTurn();
}

void ClassicSession::simulateTurns(int n) {
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
