#pragma once

// The data behind the New Game front end (docs/spec/01 §2, spec 02 §8-§9,
// spec 05 §4-§6): the settings the eight Game Setup pages edit, the empire
// drafts Empire Setup edits, racial point accounting, trait rules, our TOML
// empire files, and the conversion to a game::GameSetup. No UI code lives
// here so the rules can be unit-tested (tests/test_setup_model.cpp).

#include "game/generate.hpp"
#include "game/map_file.hpp"
#include "game/rules.hpp"
#include "game/setup.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <optional>
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
// Game Setup's one "Number of Computer Players" choice sets both levels
// (observed, spec 01 §2.2); the model keeps one per kind.
struct RandomPlayers {
    bool enabled = false;
    int level = 1;  // 0 low, 1 medium, 2 high
};

// Everything the Game Setup pages edit.
struct NewGameSettings {
    uint64_t seed = 1;             // quadrant and game seed (the previewed map uses it)
    game::GameOptions options;
    std::vector<game::EmpireSetup> players;  // the explicit empires, in order
    // A new game has random computer and neutral players, Medium (observed,
    // spec 01 §2.2, spec 07 session 5).
    RandomPlayers computers{true, 1};
    RandomPlayers neutrals{true, 1};
    // A loaded map (spec 01 §12): the game starts on it instead of a generated
    // quadrant, and its starting points place the empires first.
    std::optional<game::QuadrantMap> map;
    // Who plays the computer empires that do not name a player of their own
    // (EmpireSetup::controller), the random ones included: the built-in AI,
    // or a script player of the game's mods (docs/sdk/ai-protocol.md).
    game::Controller computerPlayer;
};

// A new game's settings (spec 01 §2.2, spec 07 session 5): no empire in the
// list, random computer and neutral players (Medium), the options' defaults,
// the unit and ship caps from Settings.
NewGameSettings defaultSettings(const game::Rules& r, uint64_t seed);

// The first Quick Start style (else the first playable race) as an empire of
// its costliest preset tier within `racialPoints`; nullopt without races.
// The empire OpenSE4's Game Setup used to start with, kept for tests and tools.
std::optional<game::EmpireSetup> firstStyleEmpire(const game::Rules& r, int racialPoints);

// The races of the Quick Start picker, in Settings.txt's `Quick Start Style
// N` order (spec 01 §2.1, spec 07 session 5); without that list, every
// playable race in the data set's order. Indices into Rules::racePresets().
std::vector<size_t> quickStartStyles(const game::Rules& r);

// ---- Option lists (spec 01 §2.2, spec 02 §9; confirmed: binary) ----------------------------

inline constexpr std::array<int64_t, 3> kStartingResources{5'000, 20'000, 100'000};  // Low, Medium (default), High
inline constexpr std::array<int, 4> kRacialPoints{0, 2'000, 3'000, 5'000};           // None, Low (default), Medium, High
inline constexpr std::array<int, 4> kStartingPlanets{1, 3, 5, 10};                   // 1 is the default
inline constexpr std::array<int, 6> kAutosaveTurns{0, 1, 2, 3, 5, 10};               // 0 None (the default), or every N turns

// Autosave (spec 01 §2.2, §14 Q38, confirmed: binary): the save written after
// a turn has been processed, when `turn` (the turns since 2400.0, the game
// date) is a multiple of `everyTurns`: "AutoSavD" with D the last digit of
// `turn`, so at most ten files (five for every 2 turns, two for every 5, one
// for every 10), named as the original names them (docs/spec/06 §6.1).
// Nothing when `everyTurns` is 0 or this turn is not saved.
std::optional<std::string> autosaveName(int everyTurns, uint32_t turn);

// ---- Quadrant -----------------------------------------------------------------------------

int maxSystems(const game::Rules& r);  // Settings `Maximum Number Of Systems` (at most 255)
// The system counts a Quadrant Size (0 small, 1 medium, 2 large) rolls from.
std::pair<int, int> quadrantSizeRange(const game::Rules& r, int quadrantSize);
game::QuadrantOptions quadrantOptions(const game::GameOptions& o);
// Generates the quadrant exactly as game::createGame will for this seed and
// options, so the preview is the map the game starts with.
std::expected<game::Generated, std::string> previewQuadrant(const game::Rules& r, uint64_t seed, const game::GameOptions& o);

// ---- Maps (spec 01 §12; our format, docs/MAPS.md) -----------------------------------------------

// Load Map: the game will start on `map`. Loading a map replaces the previous
// one with its starting points, so no earlier starting point survives.
void useMap(NewGameSettings& s, game::QuadrantMap map);
// Generate Map Now: back to a generated quadrant (without starting points).
void clearMap(NewGameSettings& s);
// The quadrant Save Map writes from Game Setup: the loaded map, or the
// generated preview (which has no starting points).
game::QuadrantMap mapToSave(const NewGameSettings& s, const game::Galaxy& preview, std::string name);

struct MapFileInfo {
    std::filesystem::path path;
    std::string name;
    int systems = 0;
    int startingPoints = 0;
};
// The map files in a folder (<user data>/maps), by name.
std::vector<MapFileInfo> listMapFiles(const game::Rules& r, const std::filesystem::path& dir);
// Where Save Map puts a map of this name in `dir`.
std::filesystem::path mapFilePath(const std::filesystem::path& dir, std::string_view name);

// ---- Players ------------------------------------------------------------------------------

// [min, max] random players for a level.
std::pair<int, int> randomPlayerRange(const game::Rules& r, bool neutral, int level);

// The game::GameSetup for these settings: explicit players first, then the
// random computer and neutral players (rolled from the seed), with the
// options as edited. Fails with a message the setup screen shows.
std::expected<game::GameSetup, std::string> buildGameSetup(const game::Rules& r, const NewGameSettings& s);

// ---- Computer players (docs/sdk/ai-protocol.md) ------------------------------------------------

// The players the game's mods declare (mod.toml [[ai.players]]), as the
// setup screens offer them after the built-in AI: "<mod id>:<player>" and
// its description.
struct ComputerPlayerChoice {
    game::Controller controller;
    std::string label;         // "test.ai-fixture:Steady"
    std::string description;
};
std::vector<ComputerPlayerChoice> computerPlayerChoices(const game::Rules& r);
// Has `controller` ("builtin" or "<mod id>:<player>") play every computer
// empire of `g`; the problem when it is no player of the game's mods.
std::optional<std::string> useComputerPlayer(const game::Rules& r, game::GameSetup& g, std::string_view controller);

// Appends the random computer players, then the neutral ones, to `g` after
// the empires it holds: counts and races drawn from an Rng seeded from
// g.seed (so the same seed gives the same players), races not yet in the
// game, names made unique. Marks them in g.options.randomAiPlayers.
void addRandomPlayers(const game::Rules& r, game::GameSetup& g, const RandomPlayers& computers, const RandomPlayers& neutrals);

// Quick Start (spec 01 §2.1, §2.2, spec 07 session 5): the player (`preset`,
// its first tier) in a new game's settings, so with random computer and
// neutral players at Medium, their numbers and races drawn from the seed.
// With `opponents` (a lesson's, or --empires): that many computer players of
// other races, shuffled with the seed, and no neutral one.
game::GameSetup quickStartGame(const game::Rules& r, std::string_view preset, uint64_t seed, std::optional<int> opponents = std::nullopt);

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
// Add New (spec 07 session 5): an empty empire of race style `style` (the
// first style when null): no name, type, title or leader, every
// characteristic 100 %, the Neutral culture, Oxygen and Rock, no trait, the
// Neutral demeanor and the Peaceful happiness type (each the first of its
// list when the data set lacks it), the style's design name file (inferred).
EmpireDraft blankDraft(const game::Rules& r, const ruleset::RacePreset* style);
// The atmospheres and planet types in the order Empire Setup lists them
// (spec 07 session 5): None, Methane, Oxygen, Hydrogen, Carbon Dioxide; Rock,
// Ice, Gas Giant; others the data set has after them.
std::vector<std::string> atmospheresInSetupOrder(const game::Rules& r);
std::vector<std::string> surfacesInSetupOrder(const game::Rules& r);
EmpireDraft draftFromSetup(const game::Rules& r, const game::EmpireSetup& e);
// Create Empire: refused while the racial point balance is negative. The result
// names the preset tier when the race is unmodified, and carries the custom
// race otherwise.
std::expected<game::EmpireSetup, std::string> finishDraft(const game::Rules& r, const EmpireDraft& d, int racialPoints);

// The minister style controls of the General page (spec 02 §10). A new empire
// starts with no style (the race's own AI files). Picking a style stores it;
// picking nothing (an empty name) leaves the field as it was, so once chosen a
// style cannot be emptied again. Ticking "Use Race Minister Style" stores an
// empty style.
void pickMinisterStyle(game::EmpireSetup& e, std::string_view style);
void setUseRaceMinisterStyle(game::EmpireSetup& e, bool on);

// Our own password digest (FNV-1a, hex); empty stays empty. Not security, only
// keeps hotseat players out of each other's turns.
std::string hashPassword(std::string_view password);

// ---- Choice lists from the data set ------------------------------------------------------------

std::vector<std::string> planetSurfaces(const game::Rules& r);  // physical types of planets in SectType.txt
std::vector<std::string> atmospheres(const game::Rules& r);      // planet atmospheres in SectType.txt
std::vector<std::string> designNameFiles(const game::Rules& r);  // Dsgnname/*.txt of the install
// Minister styles of the install (the folders under Ai/ holding AI tables, game::ai::ministerStyles).
std::vector<std::string> ministerStyleChoices(const game::Rules& r);
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
