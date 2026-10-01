#pragma once

// The generic Reorder dialog (docs/spec/06 §1.3), shared by Research,
// Intelligence and Set Construction Queue (empire_widgets.cpp).

#include "client/classic/ui.hpp"

#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

// Move Up / Down / To Top / To Bottom, OK, Cancel. Call open() with the rows,
// draw() every frame; draw() returns the new order (indices into the original
// rows) when OK is pressed.
class ReorderPopup {
public:
    void open(std::vector<std::string> rows);
    std::optional<std::vector<size_t>> draw(UiContext& ui);

private:
    bool pending_ = false;
    std::vector<std::string> rows_;
    std::vector<size_t> order_;
    int selected_ = 0;
};

} // namespace opense4::client::classic
