#pragma once

#include "core/rng.hpp"

#include <string>

namespace opense4::sim {

std::string generateStarName(Rng& rng);
std::string romanNumeral(int n);

} // namespace opense4::sim
