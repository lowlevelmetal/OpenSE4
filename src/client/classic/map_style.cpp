#include "client/classic/map_style.hpp"

#include "game/movement.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace opense4::client::classic::map_style {

GridCell gridCell(int boxWidth, int boxHeight) { return {std::max(1, boxWidth / kGridColumns), std::max(1, boxHeight / kGridRows)}; }

Symbol baseSymbol(bool explored) { return Symbol{Shape::Ring, std::nullopt, explored ? kExplored : kUnexplored, false}; }

Symbol presenceSymbol(bool explored, std::span<const game::EmpireId> present, game::EmpireId viewer) {
    if (present.empty()) return baseSymbol(explored);
    if (present.size() == 1) return Symbol{Shape::Ring, present.front(), 0, false};
    if (std::find(present.begin(), present.end(), viewer) != present.end()) return Symbol{Shape::Triangle, viewer, 0, false};
    return Symbol{Shape::Triangle, *std::max_element(present.begin(), present.end()), 0, false};
}

Symbol avoidSymbol(bool explored, bool avoided, game::EmpireId viewer) {
    if (explored && avoided) return Symbol{Shape::Ring, viewer, 0, false};
    return baseSymbol(explored);
}

Symbol claimedSymbol(bool explored, std::span<const game::EmpireId> claimants) {
    if (claimants.empty()) return baseSymbol(explored);
    if (claimants.size() == 1) return Symbol{Shape::Ring, claimants.front(), 0, false};
    return Symbol{Shape::Disc, std::nullopt, kYellow, true};
}

Symbol facilitySymbol(bool explored, bool colony, bool facility) {
    if (!colony) return baseSymbol(explored);
    return Symbol{Shape::Ring, std::nullopt, facility ? kGreen : kYellow, false};
}

int headingStep(game::Sector from, game::Sector to) { return game::movement::headingFor(from, to); }

Point nameCorner(float x, float y, float cellW, float cellH, float textW, float textH, float boxW, float boxH) {
    const Point candidates[] = {
        {x + cellW, y - textH},   // above-right
        {x + cellW, y + cellH},   // below-right
        {x - textW, y - textH},   // above-left
        {x - textW, y + cellH},   // below-left
    };
    for (const Point& p : candidates)
        if (p.x >= 0 && p.y >= 0 && p.x + textW <= boxW && p.y + textH <= boxH) return p;
    return candidates[3];
}

} // namespace opense4::client::classic::map_style
