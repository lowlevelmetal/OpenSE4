#pragma once

// The data behind the New Game front end (docs/spec/01 §2, spec 02 §8-§9,
// spec 05 §4-§6): the settings the eight Game Setup pages edit, the empire
// drafts Empire Setup edits, racial point accounting, trait rules, our TOML
// empire files, and the conversion to a game::GameSetup. No UI code lives
// here so the rules can be unit-tested (tests/test_setup_model.cpp).

#include "game/generate.hpp"
#include "game/rules.hpp"
#include "game/setup.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic::setup {

// Most empires a game may hold, explicit and random together (inferred from
// the 20-empire status grids, spec 01 §11).
inline constexpr int kMaxEmpires = 20;

// "Random computer players": a count rolled from Settings
// `Minimum/Maximum {Computer,Neutral} Player {Low,Medium,High} Setting`.
struct RandomPlayers {
    bool enabled = false;
    int level = 1;  // 0 low, 1 medium, 2 high
};

// Everything the Game Setup pages edit.
struct NewGameSettings {
    uint64_t seed = 1;             // quadrant and game seed (the previewed map uses it)
    game::GameOptions options;
    std::vector<game::EmpireSetup> players;  // the explicit empires, in order
    RandomPlayers computers{true, 1};
    RandomPlayers neutrals{false, 0};
};

// Defaults from the data set: one human empire (the first Quick Start style),
// medium random computer players, caps from Settings.
NewGameSettings defaultSettings(const game::Rules& r, uint64_t seed);

// ---- Quadrant -----------------------------------------------------------------------------

int maxSystems(const game::Rules& r);  // Settings `Maximum Number Of Systems` (at most 255)
game::QuadrantOptions quadrantOptions(const game::GameOptions& o);
// Generates the quadrant exactly as game::createGame will for this seed and
// options, so the preview is the map the game starts with.
std::expected<game::Generated, std::string> previewQuadrant(const game::Rules& r, uint64_t seed, const game::GameOptions& o);

// ---- Players ------------------------------------------------------------------------------

// [min, max] random players for a level.
std::pair<int, int> randomPlayerRange(const game::Rules& r, bool neutral, int level);

// The game::GameSetup for these settings: explicit players first, then the
// random computer and neutral players (rolled from the seed), with the
// options as edited. Fails with a message the setup screen shows.
std::expected<game::GameSetup, std::string> buildGameSetup(const game::Rules& r, const NewGameSettings& s);

// ---- Races --------------------------------------------------------------------------------

const ruleset::RacePreset* presetOf(const game::Rules& r, const game::EmpireSetup& e);
// The race an empire entry plays (custom race, or its preset tier).
game::Race raceOf(const game::Rules& r, const game::EmpireSetup& e);
bool sameRace(const game::Race& a, const game::Race& b);
// The costliest preset tier within a racial point budget (tier 0 if none fits).
int bestTierWithin(const game::Rules& r, const ruleset::RacePreset& p, int budget);
// "Racial points" of each tier of a preset.
std::vector<int> tierCosts(const game::Rules& r, const ruleset::RacePreset& p);

struct CharacteristicLimits {
    int min = 50;
    int max = 150;
};
// Settings `Characteristic <Name> Min Pct` / `Max Pct`.
CharacteristicLimits characteristicLimits(const game::Rules& r, game::Characteristic c);
// Points one characteristic at `value` costs (negative: a refund), priced by game::racialPointCost.
int characteristicCost(const game::Rules& r, game::Characteristic c, int value);
int racialPointsLeft(const game::Rules& r, const game::Race& race, int budget);

// Advanced traits (RacialTraits.txt `Required Trait N` / `Restricted Trait N`).
struct TraitCheck {
    bool ok = true;
    std::string reason;  // why a trait cannot be added
};
bool hasTrait(const game::Race& race, uint32_t trait);
TraitCheck canAddTrait(const game::Rules& r, const game::Race& race, uint32_t trait);
bool addTrait(const game::Rules& r, game::Race& race, uint32_t trait);
// Removes a trait and every trait that required it.
void removeTrait(const game::Rules& r, game::Race& race, uint32_t trait);

// ---- Empire drafts (Empire Setup) ------------------------------------------------------------

struct EmpireDraft {
    game::EmpireSetup setup;  // identity, player kind, colour, preset + tier, password hash
    game::Race race;          // always complete while editing
};

EmpireDraft draftFromPreset(const game::Rules& r, const ruleset::RacePreset& p, int tier);
EmpireDraft draftFromSetup(const game::Rules& r, const game::EmpireSetup& e);
// Create Empire: refused while the racial point balance is negative. The result
// names the preset tier when the race is unmodified, and carries the custom
// race otherwise.
std::expected<game::EmpireSetup, std::string> finishDraft(const game::Rules& r, const EmpireDraft& d, int racialPoints);

// Our own password digest (FNV-1a, hex); empty stays empty. Not security, only
// keeps hotseat players out of each other's turns.
std::string hashPassword(std::string_view password);

// ---- Choice lists from the data set ------------------------------------------------------------

std::vector<std::string> planetSurfaces(const game::Rules& r);  // physical types of planets in SectType.txt
std::vector<std::string> atmospheres(const game::Rules& r);      // planet atmospheres in SectType.txt
std::vector<std::string> designNameFiles(const game::Rules& r);  // Dsgnname/*.txt of the install
// The SectType picture of a planet with this surface and atmosphere, if any.
std::optional<int> planetPicture(const game::Rules& r, std::string_view surface, std::string_view atmosphere);

// ---- Empire files: our TOML format under <user data>/empires/ ----------------------------------

std::string empireToToml(const game::Rules& r, const game::EmpireSetup& e);
struct LoadedEmpire {
    game::EmpireSetup empire;
    std::vector<std::string> warnings;  // things the current data set does not have
};
std::expected<LoadedEmpire, std::string> empireFromToml(const game::Rules& r, std::string_view text);

std::expected<std::filesystem::path, std::string> saveEmpireFile(const game::Rules& r, const std::filesystem::path& dir,
                                                                 const game::EmpireSetup& e);
std::expected<LoadedEmpire, std::string> loadEmpireFile(const game::Rules& r, const std::filesystem::path& file);

struct EmpireFileInfo {
    std::filesystem::path path;
    std::string name;   // empire name
    std::string race;   // race name
    std::string style;  // race art style
};
std::vector<EmpireFileInfo> listEmpireFiles(const game::Rules& r, const std::filesystem::path& dir);

} // namespace opense4::client::classic::setup
