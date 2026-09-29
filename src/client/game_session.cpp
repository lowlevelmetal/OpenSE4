#include "client/game_session.hpp"

#include "sim/rules.hpp"
#include "sim/turn.hpp"

#include <SDL3/SDL_timer.h>

namespace opense4::client {

GameSession::GameSession(sim::Content content, sim::NewGame game)
    : content_(std::move(content)), state_(std::move(game.state)), player_(game.player) {
    collectEvents();
}

bool GameSession::issue(const sim::Command& command) {
    auto result = sim::applyCommand(state_, content_, player_, command);
    if (!result) setStatus(result.error());
    return result.has_value();
}

void GameSession::endTurn() {
    sim::advanceTurn(state_, content_);
    collectEvents();
}

void GameSession::autoplay(int turns) {
    state_.empire(player_).ai = true;
    for (int i = 0; i < turns; ++i) endTurn();
    state_.empire(player_).ai = false;
}

std::vector<const sim::Ship*> GameSession::idleShips() const {
    std::vector<const sim::Ship*> idle;
    for (const sim::Ship& ship : state_.ships)
        if (ship.owner == player_ && ship.path.empty() && ship.order.type == sim::OrderType::None) idle.push_back(&ship);
    return idle;
}

bool GameSession::seesShipsIn(sim::SystemId s) const { return sim::hasPresence(state_, player_, s); }

void GameSession::setStatus(std::string message) {
    status_ = std::move(message);
    statusTime_ = static_cast<double>(SDL_GetTicks()) / 1000.0;
}

void GameSession::collectEvents() {
    // Events are produced by the turn that just ended (turn - 1), or by setup.
    const uint32_t turn = state_.turn > 1 ? state_.turn - 1 : state_.turn;
    for (const sim::GameEvent& e : state_.events) {
        if (e.empire != player_) continue;
        log_.push_back(LogEntry{turn, e});
        ++newEvents_;
    }
}

} // namespace opense4::client
