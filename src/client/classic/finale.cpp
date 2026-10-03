#include "client/classic/finale.hpp"

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
    if (kind == SessionKind::Local || kind == SessionKind::Hotseat) {
        const bool human = std::any_of(s.empires.begin(), s.empires.end(),
                                       [](const game::Empire& e) { return e.alive && e.kind == game::PlayerKind::Human; });
        if (!human && !s.empires.empty()) return {FinaleKind::HumanDead};
    }
    const bool playing = player.valid() && player.index() < s.empires.size() && s.empire(player).alive;
    if (!playing) {
        if (s.gameOver && !player.valid()) out.push_back(FinaleKind::Victory);
        return out;
    }
    const bool colony = std::any_of(s.colonies.begin(), s.colonies.end(), [&](const auto& c) { return c && c->owner == player; });
    const bool vehicle = std::any_of(s.vehicles.begin(), s.vehicles.end(), [&](const game::Vehicle& v) { return v.owner == player && v.count > 0; });
    if (!colony && !vehicle) out.push_back(FinaleKind::Lose);
    if (s.gameOver) out.push_back(FinaleKind::Victory);
    const bool alone = std::none_of(s.empires.begin(), s.empires.end(), [&](const game::Empire& e) { return e.id != player && e.alive; });
    if (alone) out.push_back(FinaleKind::Conquered);
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
    std::vector<FinaleKind> out;
    for (FinaleKind k : due)
        if (std::find(due_.begin(), due_.end(), k) == due_.end()) out.push_back(k);
    due_ = std::move(due);
    return out;
}

} // namespace opense4::client::classic
