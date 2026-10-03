#pragma once

// Text tokens of lesson texts (docs/LEARNING.md "Text tokens"): words the
// panel fills in from the player's game when it shows them. One so far:
//
//   {design:<type>}   the name of the player's design of that design type
//                     ("{design:Attack Ship}" shows "Aaouane"), or the type
//                     itself when the player has no such design.
//
// Lessons name design types because the names come from the race's list and
// differ from game to game, while the windows list designs by name.

#include "game/state.hpp"
#include "learn/markdown.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

// What is wrong with the tokens of a text (an unknown token, a design type
// that does not exist, a token left open); empty when nothing is.
std::vector<std::string> tokenProblems(std::string_view text);
std::vector<std::string> tokenProblems(const std::vector<Block>& blocks);

// The name of the empire's newest design of that type that is not obsolete
// ("Colony": the colony ship of the race's own planet type, else any colony
// ship); empty when it has none.
std::string designNameOfType(const game::GameState& state, game::EmpireId empire, std::string_view type);

// The designs a text names: the names its {design:<type>} tokens show, in
// order, each once (a type the empire has no design of is left out).
std::vector<std::string> namedDesigns(const std::vector<Block>& blocks, const game::GameState& state, game::EmpireId empire);

// The text with every token filled in for the empire.
std::string expandTokens(std::string_view text, const game::GameState& state, game::EmpireId empire);
std::vector<Block> expandTokens(const std::vector<Block>& blocks, const game::GameState& state, game::EmpireId empire);

// The text with every token as one word (for word counts).
std::string tokensAsWords(std::string_view text);

} // namespace opense4::learn
