#include "client/classic/finale.hpp"

#include "game/ai.hpp"
#include "game/score.hpp"
#include "game/turn.hpp"
#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

// More than this many pictures in a list is a broken Settings.txt.
constexpr int64_t kMaxFinalePictures = 100;

} // namespace

std::string_view finaleKeyName(FinaleKind k) {
    switch (k) {
        case FinaleKind::Victory:
        case FinaleKind::Conquered: return "Victory";
        case FinaleKind::Lose: return "Lose";
        case FinaleKind::HumanDead: return "Human Dead";
    }
    return "Victory";
}

std::string_view finaleArgName(FinaleKind k) {
    switch (k) {
        case FinaleKind::Victory: return "victory";
        case FinaleKind::Lose: return "lose";
        case FinaleKind::HumanDead: return "human-dead";
        case FinaleKind::Conquered: return "conquered";
    }
    return "victory";
}

std::vector<FinaleKind> finaleKinds(const game::GameState& s, game::EmpireId player, SessionKind kind) {
    std::vector<FinaleKind> out;
    // No human left (a human playing its last turn still counts, ai::anyHumanLeft).
    if ((kind == SessionKind::Local || kind == SessionKind::Hotseat) && !s.empires.empty() && !game::ai::anyHumanLeft(s)) return {FinaleKind::HumanDead};
    const bool turnBased = game::turnBased(s);
    // A turn-based game whose last turn every player has played: the Game
    // Completed box, not an ending window (ClassicMode).
    if (turnBased && s.gameOver && s.lastTurn) return out;
    const bool valid = player.valid() && player.index() < s.empires.size();
    const bool alive = valid && s.empire(player).alive;
    // A turn-based game's human destroyed at the start of this turn plays it, its last.
    const bool lastTurn = valid && game::playsLastTurn(s, player);
    if (!alive && !lastTurn) {
        if (s.gameOver && !player.valid()) out.push_back(FinaleKind::Victory);
        return out;
    }
    // Lose: in a turn-based game the mark the engine set as this turn came up
    // (the empire owned nothing then, before its start of turn); in a
    // simultaneous game when it owns nothing now, before it is marked dead.
    const bool lose = turnBased ? s.playerTurn.empire == player && s.playerTurn.loseEnding : game::score::ownsNothing(s, player);
    if (lose) out.push_back(FinaleKind::Lose);
    // Victory: the game is over (simultaneous), or this is a turn-based game's last turn.
    if (s.gameOver || (turnBased && s.lastTurn)) out.push_back(FinaleKind::Victory);
    const bool alone = std::none_of(s.empires.begin(), s.empires.end(), [&](const game::Empire& e) { return e.id != player && e.alive; });
    if (alive && alone) out.push_back(FinaleKind::Conquered);
    return out;
}

std::vector<std::string> finalePictures(const ruleset::Settings& data, FinaleKind k) {
    const std::string_view name = finaleKeyName(k);
    const int64_t count = std::clamp<int64_t>(data.integer(std::format("Num Finale {} Pictures", name), 0), 0, kMaxFinalePictures);
    std::vector<std::string> out;
    for (int64_t i = 1; i <= count; ++i)
        if (auto file = data.text(std::format("Finale {} Picture {}", name, i)); file && !file->empty())
            out.push_back("Pictures/Game/Finale/" + *file);
    return out;
}

std::vector<FinaleKind> FinaleWatch::update(const game::GameState& s, game::EmpireId player, SessionKind kind) {
    const Key key{s.turn, player, game::turnBased(s) ? game::activePlayer(s) : game::EmpireId{}};
    if (key_ == key) return {};
    key_ = key;
    std::vector<FinaleKind> due = finaleKinds(s, player, kind);
    // Each player sees an ending once while it holds: what was due at that
    // player's previous start of a turn is not shown again (a hotseat game's
    // humans each see the Victory of the last turn).
    std::vector<FinaleKind>& before = due_[player];
    std::vector<FinaleKind> out;
    for (FinaleKind k : due)
        if (std::find(before.begin(), before.end(), k) == before.end()) out.push_back(k);
    before = std::move(due);
    return out;
}

} // namespace opense4::client::classic
