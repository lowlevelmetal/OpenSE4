#pragma once

// What a tutorial step lets the player use while the input lock is on
// (docs/LEARNING.md "The input lock"), and whether its `done` condition can
// be reached with only that. The content test runs this over every built-in
// tutorial, so a step can never ask for something the lock forbids.

#include "learn/condition.hpp"
#include "learn/lesson.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

// The step's highlighted and allowed UI tags, and its keys.
struct StepAccess {
    std::vector<std::string> tags;
    std::vector<std::string> keys;
    bool has(std::string_view tag) const;
    bool hasKey(std::string_view chord) const;
};
StepAccess stepAccess(const Step& step);

// The windows a click on a tag opens ("command:research" opens research,
// "designs:simulator" combat-simulator, "panel:galaxy" galaxy-map with the
// right button).
std::vector<std::string_view> windowsOpenedBy(std::string_view tag);
// The other way: the tags that open a window.
std::vector<std::string> openersOf(std::string_view window);

// Why `done` cannot be reached with what the step allows; empty when it can.
// Each fact needs one of the tags (or keys) that bring it about: a window
// needs a tag that opens it (and closing one, its `window:` or `:close` tag or
// Esc), an order its order button, a command the widget that gives it,
// anything that comes with the turns End Turn, and so on.
std::vector<std::string> reachProblems(const Condition& done, const StepAccess& access);

} // namespace opense4::learn
