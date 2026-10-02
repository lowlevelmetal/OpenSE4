#pragma once

// The ending window (docs/spec/06 §1.7 Finale, §1.2.1 "No human left", §1.9
// "Other Settings.txt keys", confirmed: binary): a full picture from
// Pictures/Game/Finale/, drawn from the Settings.txt list of its kind. All
// humans eliminated shows a "Human Dead" picture, "your empire was
// destroyed" a "Lose" one, and the end of the game by victory conditions a
// "Victory" one. The original draws the picture with the game's random
// numbers; OpenSE4 uses a separate source, so showing it changes nothing.
// Headless (tests/test_settings_keys.cpp); the window is screens/finale_screen.cpp.

#include "client/classic/session.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::ruleset {
class Settings;
}

namespace opense4::client::classic {

enum class FinaleKind : uint8_t { Victory, Lose, HumanDead };

// The kind as the Settings.txt keys spell it ("Victory", "Lose", "Human Dead").
std::string_view finaleKeyName(FinaleKind k);

// The ending this game shows now, if any, in this order: Victory once the
// game is over (GameState::gameOver); in local and hotseat games Human Dead
// when no living empire is human-controlled (spec 06 §1.2.1: the host of a
// game on different machines has no such check); Lose when the player's own
// empire has been destroyed. So a single player whose last colony and ship
// are lost sees Human Dead, all humans being gone (inferred, spec 06 §7 Q83).
std::optional<FinaleKind> finaleKind(const game::GameState& s, game::EmpireId player, SessionKind kind);

// The pictures of a kind: `Num Finale <Kind> Pictures` names, each `Finale
// <Kind> Picture N` (N from 1), as paths under the install
// ("Pictures/Game/Finale/<file>"); empty names are skipped.
std::vector<std::string> finalePictures(const ruleset::Settings& data, FinaleKind k);

// Opens the ending once per occurrence: update() returns the kind the first
// frame it holds, and again only after it stopped holding.
class FinaleWatch {
public:
    std::optional<FinaleKind> update(const game::GameState& s, game::EmpireId player, SessionKind kind);

private:
    std::optional<FinaleKind> shown_;
};

} // namespace opense4::client::classic
