#include "game/players.hpp"

#include "game/turn.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace opense4::game {

Players::~Players() = default;

std::string controllerText(const Controller& c) {
    switch (c.kind) {
        case Controller::Kind::Builtin: return "builtin";
        case Controller::Kind::Script: return std::format("{}:{}", c.mod, c.player);
        case Controller::Kind::External: return std::format("external:{}", c.slot);
        case Controller::Kind::Count: break;
    }
    return "builtin";
}

std::optional<Controller> parseController(std::string_view text) {
    Controller c;
    if (text == "builtin") return c;
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) return std::nullopt;
    const std::string_view left = text.substr(0, colon);
    const std::string_view right = text.substr(colon + 1);
    if (left == "external") {
        uint32_t slot = 0;
        const auto [end, ec] = std::from_chars(right.data(), right.data() + right.size(), slot);
        if (ec != std::errc{} || end != right.data() + right.size()) return std::nullopt;
        c.kind = Controller::Kind::External;
        c.slot = slot;
        return c;
    }
    if (right.find(':') != std::string_view::npos) return std::nullopt;
    c.kind = Controller::Kind::Script;
    c.mod = std::string(left);
    c.player = std::string(right);
    return c;
}

bool hasPlayerController(const GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return false;
    const Empire& emp = s.empire(e);
    return emp.alive && emp.kind != PlayerKind::Human && !emp.controller.builtin();
}

bool playedByController(const TurnContext& ctx, EmpireId e) { return ctx.players && hasPlayerController(ctx.state, e); }

std::string_view callName(PlanCall c) {
    switch (c) {
        case PlanCall::Politics: return "politics";
        case PlanCall::Orders: return "orders";
        case PlanCall::Economy: return "economy";
        case PlanCall::Count: break;
    }
    return "?";
}

namespace {

PlayersFactory& factory() {
    static PlayersFactory f;
    return f;
}

} // namespace

void setPlayersFactory(PlayersFactory f) { factory() = std::move(f); }

std::unique_ptr<Players> makePlayers(const Rules& r, GameState& s) {
    const PlayersFactory& f = factory();
    if (!f) return nullptr;
    return f(r, s);
}

CallSession::CallSession(TurnContext& ctx) : ctx_(ctx), players_(makePlayers(ctx.rules, ctx.state)) {
    ctx_.players = players_.get();
    ctx_.hooks = players_ ? players_->hooks() : nullptr;
    if (players_) players_->begin(ctx_);
}

CallSession::~CallSession() {
    if (ctx_.players == players_.get()) ctx_.players = nullptr;
    if (players_ && ctx_.hooks == players_->hooks()) ctx_.hooks = nullptr;
}

void CallSession::end() {
    if (!players_) return;
    deliverHooks(ctx_);
    players_->endSession(ctx_);
    ctx_.players = nullptr;
    ctx_.hooks = nullptr;
    players_.reset();
}

void replayJournal(GameState& again, const GameState& played) {
    again.journal.replay.clear();
    for (const JournalEntry& entry : played.journal.entries)
        if (entry.turn >= again.turn) again.journal.replay.push_back(entry);
}

void pruneJournal(GameState& s) {
    std::erase_if(s.journal.entries, [&](const JournalEntry& e) { return e.turn + 1 < s.turn; });
}

} // namespace opense4::game
