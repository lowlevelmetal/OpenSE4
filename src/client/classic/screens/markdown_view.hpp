#pragma once

// Draws the learning content's Markdown blocks (learn/markdown.hpp) in the
// classic look: the game's text font, headings in the title font, keys and
// labels in the label blue, links that can be clicked, tables and tip boxes.
// Text wraps to the width of the current ImGui window.

#include "client/classic/ui.hpp"
#include "learn/markdown.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

struct MarkdownOptions {
    // An anchor to bring to the top of the window; cleared once it is reached.
    std::string scrollTo;
    // Whether a link can be followed here (window links only during a game);
    // links that cannot are dimmed and say why.
    std::function<bool(const learn::Link&)> canFollow;
    std::string cannotFollow = "Only during a game";
};

// Draws the blocks at the cursor. Returns the target of the link clicked
// this frame, if any.
std::optional<std::string> drawMarkdown(const Painter& p, const std::vector<learn::Block>& blocks, MarkdownOptions& options);

} // namespace opense4::client::classic
