#pragma once

#include "sim/state.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace opense4::sim {

// Movement graph: every sector of every system is a node. A ship can step to
// any of the 8 neighbouring sectors, or - when standing on a warp point - jump
// to the linked warp point in another system. Every step costs one move.
//
// Returned paths exclude the start location. Among equally short paths the
// straightest-looking one is preferred. Results are fully deterministic.
//
// With `knowledge`, only warp points in systems that empire has explored can
// be used: nobody can plot a course through a warp point they have never seen.
// Player and AI orders always pass their empire; omit it only for omniscient
// queries (galaxy generation, tests).

std::optional<std::vector<Location>> findPath(const GameState& s, Location from, Location to,
                                              const Empire* knowledge = nullptr);

// Path to the nearest location for which `goal` returns true.
std::optional<std::vector<Location>> findPathTo(const GameState& s, Location from,
                                                const std::function<bool(Location)>& goal,
                                                const Empire* knowledge = nullptr);

// Minimum number of warp jumps from `from` to every system (-1 = unreachable).
std::vector<int> warpHopDistances(const GameState& s, SystemId from, const Empire* knowledge = nullptr);

} // namespace opense4::sim
