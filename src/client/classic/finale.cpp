#include "client/classic/finale.hpp"

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
        case FinaleKind::Victory: return "Victory";
        case FinaleKind::Lose: return "Lose";
        case FinaleKind::HumanDead: return "Human Dead";
    }
    return "Victory";
}

std::optional<FinaleKind> finaleKind(const game::GameState& s, game::EmpireId player, SessionKind kind) {
    if (s.gameOver) return FinaleKind::Victory;
    if (kind == SessionKind::Local || kind == SessionKind::Hotseat) {
        const bool human = std::any_of(s.empires.begin(), s.empires.end(),
                                       [](const game::Empire& e) { return e.alive && e.kind == game::PlayerKind::Human; });
        if (!human && !s.empires.empty()) return FinaleKind::HumanDead;
    }
    if (player.valid() && player.index() < s.empires.size() && !s.empire(player).alive) return FinaleKind::Lose;
    return std::nullopt;
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

std::optional<FinaleKind> FinaleWatch::update(const game::GameState& s, game::EmpireId player, SessionKind kind) {
    const std::optional<FinaleKind> now = finaleKind(s, player, kind);
    if (now == shown_) return std::nullopt;
    shown_ = now;
    return now;
}

} // namespace opense4::client::classic
