#pragma once

// The list windows of Create Design and the Weapons Report (docs/spec/06
// §7 Q94, §1.2, confirmed: binary): a modal window holding one list and
// Cancel. A click on a row picks it and closes the window; Cancel (or Esc)
// picks nothing, which the caller turns into the empty choice ("All", no
// size, "Normal Mount"). Open one with ImGui::OpenPopup(id) at the same
// place of the ID stack as the call.

#include "client/classic/ui.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace opense4::client::classic {

// What a list window answered this frame.
struct ListPick {
    bool done = false;            // it closed this frame
    std::optional<size_t> row;    // the row picked; none: Cancel
};

// A list window with one "Name" column of `rows` ("Select Component Group").
ListPick nameListWindow(UiContext& ui, const char* id, std::span<const std::string> rows);

// "Select Weapon Mount": "Normal Mount" first, then each of `mounts` (indices
// of the data set's mounts) with its description in grey small type under
// its name, and under the list a pane with the description of the row under
// the pointer (else of `current`, -1 for Normal Mount). The row picked: 0 for
// Normal Mount, k for mounts[k - 1].
ListPick weaponMountWindow(UiContext& ui, const char* id, std::span<const uint32_t> mounts, int32_t current);

} // namespace opense4::client::classic
