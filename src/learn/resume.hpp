#pragma once

// Resuming a tutorial the player left (docs/LEARNING.md "Resuming a
// lesson"): the client keeps the lesson's game and the step it was at, and
// later loads that game and shows the step again. Windows and what is in
// them (a design being built, a sample battle) are not part of the game, so
// a step that works in a window goes back to the step where the work in that
// window began. A lesson whose steps changed since cannot be resumed: its
// fingerprint tells.

#include "learn/lesson.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace opense4::learn {

// The windows a step works in: those its highlighted and allowed tags lie in
// ("research:areas" and "window:research" lie in research; "command:research"
// lies in the main window, so in none).
std::vector<std::string_view> stepWindows(const Step& step);

// The step (0-based) to resume at, for a tutorial the player left at step
// `active`: that step, or, while it works in a window, the first of the
// steps before it that work in that window or open it (one after another).
// A step in the main window resumes as it is.
size_t resumeStep(const Lesson& lesson, size_t active);

// What a saved place in a tutorial depends on: the number of steps and each
// step's tags, keys and condition. Rewording a step keeps it; adding,
// removing, reordering or changing what a step waits for changes it.
uint64_t lessonFingerprint(const Lesson& lesson);

} // namespace opense4::learn
