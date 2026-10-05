#pragma once

// What a data set knows about the mods it was built with (docs/MODDING_SDK.md
// §3, docs/sdk/packages-and-data.md): the mod set as games record it, and the
// ability names mods declare. The packages themselves are src/mods/.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::ruleset {

// One mod of a game's mod set.
struct ModRecord {
    std::string id;            // "example.better-carriers"
    std::string version;       // "1.2.0"
    std::string hash;          // the package's identity: 32 hex digits
    bool affectsGame = false;  // data, AI or rules: every player of a game needs the same
    bool operator==(const ModRecord&) const = default;
};

// How the values of a declared ability combine over a list (a design's
// components, a planet's facilities): their sum, the largest or the smallest.
enum class Combine : uint8_t { Sum, Max, Min, Count };
std::string_view displayName(Combine c);  // "sum", "max", "min"

// An ability name a mod declares, so data may carry it (docs/sdk/packages-and-data.md).
struct DeclaredAbility {
    std::string name;
    Combine combine = Combine::Sum;
    std::string mod;  // the id of the mod that declared it first
};

// The identity of the game-affecting mods in load order: empty when there are
// none, else 16 hex digits.
std::string modSetIdentity(std::span<const ModRecord> mods);

// "id 1.2.0, other 0.3": the mods, in order, for messages.
std::string describeMods(std::span<const ModRecord> mods);

// What keeps a player's mods from playing a game: each game-affecting mod the
// game uses that the player lacks, has in another version or with other
// files, each one the player has that the game does not use, and a different
// load order. Asset-only and interface-only mods do not count. `theirs` names
// the other side in the messages ("the host", "the saved game"). Empty: the
// two match.
std::vector<std::string> compareModSets(std::span<const ModRecord> game, std::span<const ModRecord> mine, std::string_view theirs = "the game");

} // namespace opense4::ruleset
