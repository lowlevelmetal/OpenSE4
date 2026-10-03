#pragma once

// The ending windows (docs/spec/06 §1.7 Finale, §1.2.1 "No human left", §1.9
// "Other Settings.txt keys", §7 Q83, confirmed: binary): a full picture from
// Pictures/Game/Finale/, drawn from the Settings.txt list of its kind. All
// humans eliminated shows a "Human Dead" picture, "your empire was
// destroyed" a "Lose" one, and the end of the game by victory conditions or
// the conquest of the galaxy a "Victory" one. The original draws the picture with the game's random
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

// Victory: the victory conditions are met (the game is over); Conquered:
// every other empire is dead (the player may play on); both use the Victory
// pictures.
enum class FinaleKind : uint8_t { Victory, Lose, HumanDead, Conquered };

// The kind as the Settings.txt keys spell it ("Victory", "Lose", "Human Dead").
std::string_view finaleKeyName(FinaleKind k);
// The kind as a screen argument names it ("victory", "lose", "human-dead", "conquered").
std::string_view finaleArgName(FinaleKind k);

// The endings due when a turn begins for `player`, in the order they are
// shown (spec 06 §7 Q83, confirmed: binary). In a local or hotseat game,
// first: no living empire is human-controlled, Human Dead alone (the game
// stops there; a host on different machines has no such check). Otherwise,
// for a player whose empire is not yet marked dead: Lose when it owns no
// colony and no vehicle (the start of its last turn; the engine marks it
// dead when that turn ends, score::checkDestruction); then Victory when the
// game is over; then Conquered when every other empire is dead (followed by
// the question whether to play on). Without a player, only Victory.
std::vector<FinaleKind> finaleKinds(const game::GameState& s, game::EmpireId player, SessionKind kind);

// The pictures of a kind: `Num Finale <Kind> Pictures` names, each `Finale
// <Kind> Picture N` (N from 1), as paths under the install
// ("Pictures/Game/Finale/<file>"); empty names are skipped.
std::vector<std::string> finalePictures(const ruleset::Settings& data, FinaleKind k);

// Shows each ending as it comes: at each start of a turn (a new date, a new
// player or, in a turn-based game, a new active empire; and the first call)
// update() returns the endings due that were not already due at the previous
// start of a turn, in order; nothing between turn starts.
class FinaleWatch {
public:
    std::vector<FinaleKind> update(const game::GameState& s, game::EmpireId player, SessionKind kind);

private:
    struct Key {
        uint32_t turn = 0;
        game::EmpireId player, active;
        bool operator==(const Key&) const = default;
    };
    std::optional<Key> key_;
    std::vector<FinaleKind> due_;
};

} // namespace opense4::client::classic
