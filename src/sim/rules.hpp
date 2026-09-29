#pragma once

// Read-only rule queries over Content + GameState. Everything the UI shows
// (costs, speeds, outputs) and everything the turn processor applies must come
// from these functions so the two can never disagree.

#include "sim/state.hpp"

#include <span>
#include <string>
#include <vector>

namespace opense4::sim {

// --- Technology ------------------------------------------------------------------
bool meetsRequirements(const Empire& e, std::span<const TechRequirement> reqs);
bool canResearch(const Content& c, const Empire& e, TechIndex t);  // prerequisites met, below max level
int64_t nextLevelCost(const Content& c, const Empire& e, TechIndex t);

bool isAvailable(const Content& c, const Empire& e, HullIndex h);
bool isAvailable(const Content& c, const Empire& e, ComponentIndex comp);
bool isAvailable(const Content& c, const Empire& e, FacilityIndex f);

// --- Designs ---------------------------------------------------------------------
DesignStats computeDesignStats(const Content& c, HullIndex hull, std::span<const ComponentIndex> components);

// --- Planets & colonies ------------------------------------------------------------
bool canBreathe(const Empire& e, const Planet& p);
int64_t maxPopulation(const Content& c, const Empire& e, const Planet& p);
int facilitySlots(const Content& c, const Planet& p);
int usedFacilitySlots(const Colony& col);  // built + queued
bool hasSpaceYard(const Content& c, const Colony& col);
int64_t constructionRate(const Content& c, const Colony& col);

struct ColonyOutput {
    Resources resources;
    int64_t research = 0;
    int efficiencyPercent = 100;  // population-based production multiplier
};
ColonyOutput colonyOutput(const Content& c, const GameState& s, const Planet& p);

// Empty string if `ship` could colonize `planet` once it gets there.
std::string colonizeProblem(const GameState& s, const Ship& ship, const Planet& planet);

// --- Spatial queries -------------------------------------------------------------
const WarpPoint* warpPointAt(const GameState& s, Location loc);
const Planet* planetAt(const GameState& s, Location loc);
std::vector<const Ship*> shipsAt(const GameState& s, Location loc);
std::vector<const Ship*> shipsInSystem(const GameState& s, SystemId sys);

// --- Empires ---------------------------------------------------------------------
bool atWar(const GameState& s, EmpireId a, EmpireId b);
// An empire sees the contents of a system when it has a ship or colony there.
bool hasPresence(const GameState& s, EmpireId e, SystemId sys);
int64_t totalPopulation(const GameState& s, EmpireId e);
int colonyCount(const GameState& s, EmpireId e);
int shipCount(const GameState& s, EmpireId e);

// Deterministic checksum of the full game state (desync / determinism checks).
uint64_t stateChecksum(const GameState& s);

} // namespace opense4::sim
