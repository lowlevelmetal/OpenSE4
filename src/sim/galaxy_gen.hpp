#pragma once

#include "sim/state.hpp"

namespace opense4::sim {

// Fills an empty GameState with star systems, warp lanes (as linked warp point
// pairs) and planets according to state.galaxy. The warp network is always
// connected and planar: a Euclidean minimum spanning tree plus extra
// non-crossing lanes.
void generateGalaxy(GameState& state, Rng& rng);

} // namespace opense4::sim
