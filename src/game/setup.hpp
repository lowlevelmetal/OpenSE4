#pragma once

// New game creation (docs/spec/01 §3, spec 02 §9): quadrant, empires with
// their races, homeworlds and starting technology. No empire gets a ship or a
// design of its own (spec 01 §3.6 "Starting assets"): only an empire file's
// designs, the Quick Start design run and a lesson's ships (StartExtras).

#include "game/map_file.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace opense4::game {

struct EmpireSetup {
    std::string name;             // empty: from the race preset
    std::string empireType;
    std::string leaderTitle;
    std::string leaderName;
    // Race: either a preset folder (Pictures/Races/<folder>) with a tier
    // (0..2, the preset's "Race Opt" sets) or a fully custom race.
    std::string preset;
    int presetTier = 0;
    std::optional<Race> customRace;
    uint32_t color = 0;           // 0: automatic
    PlayerKind kind = PlayerKind::Human;
    std::string passwordHash;
    // Empire Setup, General page (spec 02 §9, spec 05 §7.1): the minister
    // style (a folder under Ai/; empty: none, the race's own AI files) and
    // "Use Race Minister Style". They become Empire::ministerStyle and
    // useRaceMinisterStyle, so the style also drives an empire marked
    // Computer Controlled.
    std::string ministerStyle;
    bool useRaceMinisterStyle = false;
    // Experience kept with the empire (spec 02 §9): it comes from the empire
    // file, becomes Empire::experience and grows during play. Only shown.
    int experience = 0;
    // Designs saved with the empire (Save Empire with its designs, spec 06
    // §7 Q48): the new game gives them to the empire. Only name, type, hull,
    // entries, strategy and the obsolete flag are read; ids, owners and
    // statistics are the new game's.
    std::vector<Design> designs;
};

struct GameSetup {
    uint64_t seed = 1;
    GameOptions options;
    std::vector<EmpireSetup> empires;   // index = EmpireId
    // A loaded map (spec 01 §12): the quadrant is taken from it instead of
    // being generated, and its starting points place the empires first.
    std::optional<QuadrantMap> map;
};

// What a new game gets beyond its setup, from the client's own entry points.
// Kept apart from GameSetup, which the lobby sends over the network: these
// are never sent or saved.
struct StartExtras {
    // Quick Start (spec 01 §2.1, confirmed: binary): these empires (the
    // human player) get one run of the Design minister at creation
    // (ai::designMinisterRun), dated the first turn.
    std::vector<EmpireId> designMinisterRun;
    // OpenSE4 lesson extension (docs/LEARNING.md, `starting_ships`), never
    // part of a normal game. Per EmpireId: one ship per entry, built at
    // creation at the homeworld from the empire's newest design of that
    // design type ("Attack Ship", "Colony (Rock)", ...; "Colony" stands for
    // the colony ship of the race's own planet type). An entry without such
    // a design builds nothing.
    std::vector<std::vector<std::string>> lessonShips;
};

// Builds a race from a preset tier: characteristics, traits (by name), culture,
// happiness model, environment.
Race raceFromPreset(const Rules& r, const ruleset::RacePreset& preset, int tier);
const ruleset::RacePreset* findPreset(const Rules& r, std::string_view folderOrName);

// Racial points a race costs (spec 02 §8.1); used to validate custom races.
int racialPointCost(const Rules& r, const Race& race);

std::expected<GameState, std::string> createGame(const Rules& r, const GameSetup& setup, const StartExtras& extras = {});

// Starting technology levels per the tech-start option (spec 05 §1.2).
std::vector<int> startingTechLevels(const Rules& r, const GameOptions& o, const Race& race);

// Distinct empire colours for automatic assignment.
uint32_t defaultEmpireColor(size_t index);

// The digest stored in Empire::passwordHash / EmpireSetup::passwordHash
// ("fnv1a64:<hex>"; empty password -> empty). It only keeps hotseat and
// network players out of each other's empires; it is not a security measure.
std::string hashPassword(std::string_view password);

} // namespace opense4::game
