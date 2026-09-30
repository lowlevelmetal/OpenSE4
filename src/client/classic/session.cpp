#include "client/classic/session.hpp"

#include "core/log.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <format>

namespace opense4::client::classic {

ClassicSession::ClassicSession(std::shared_ptr<const game::Rules> rules, game::GameState state, game::EmpireId player, SessionKind kind)
    : rules_(std::move(rules)), state_(std::move(state)), player_(player), kind_(kind) {
    ended_.assign(state_.empires.size(), 0);
}

game::CommandResult ClassicSession::issue(game::Command c) {
    if (waiting_) return game::CommandResult::fail("Waiting for the other players");
    game::CommandResult r = game::apply(*rules_, state_, player_, c);
    if (r.ok) {
        orders_.push_back(std::move(c));
        ++revision_;
    }
    return r;
}

void ClassicSession::endTurn() {
    if (waiting_) return;
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
    beginTurn();
}

void ClassicSession::poll() {
    if (!transport_) return;
    if (auto s = transport_->pollState()) {
        state_ = std::move(*s);
        beginTurn();
    }
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
    beginTurn();
}

void ClassicSession::simulateTurns(int n) {
    for (int i = 0; i < n && !state_.gameOver; ++i) game::processTurn(*rules_, state_, {});
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
