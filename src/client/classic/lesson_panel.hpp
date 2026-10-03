#pragma once

// The lesson panel's layout (docs/LEARNING.md "The lesson panel"), apart from
// drawing it so that tests can check it: the rows its buttons fit in at the
// Text size setting, and the place on the screen that hides the least of
// what the active step outlines and of the prompts that are open.

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace opense4::client::classic::panel {

// A rectangle in Dear ImGui's screen units.
struct Box {
    ImVec2 min, max;
    float width() const { return max.x - min.x; }
    float height() const { return max.y - min.y; }
    float area() const { return std::max(0.0f, width()) * std::max(0.0f, height()); }
};
// The area two boxes share.
float overlap(const Box& a, const Box& b);

// Buttons in rows, in order: each gets at least its width, a row ends where
// the next button no longer fits, and each row is spread over the whole
// width (equal widths when they all fit). Buttons never overlap; one wider
// than the row gets the row to itself.
struct Slot {
    float x = 0, w = 0;
    int row = 0;
};
std::vector<Slot> flowButtons(std::span<const float> minWidths, float width, float gap);
int rowCount(std::span<const Slot> slots);

// What the panel should not hide, and how much each counts: each part by the
// share of it hidden, so a small button weighs as much as a large map.
struct Avoid {
    std::vector<Box> targets;   // what the active step outlines
    std::vector<Box> allowed;   // what else it lets the player use: half as much
    std::vector<Box> prompts;   // open prompts and pop-ups: as much as a target
};
float hiddenShare(const Box& panel, std::span<const Box> parts);
float spotScore(const Box& panel, const Avoid& avoid);

// A place the panel may take; `id` names the place from frame to frame.
struct Spot {
    int id = 0;
    Box box;
};
// The spot with the lowest score, the first of equals. The spot taken last
// frame (`previous`) stays while it is about as good, so the panel does not
// jump between near-equal places as its height settles.
size_t bestSpot(std::span<const Spot> spots, const Avoid& avoid, std::optional<int> previous);

// Moves `box` inside `bounds` (as far as it fits), keeping its size.
Box keepInside(Box box, const Box& bounds);

} // namespace opense4::client::classic::panel
