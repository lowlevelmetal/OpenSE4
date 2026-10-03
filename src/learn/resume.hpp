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
#include <span>
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
// A step that uses the work a window held (usesWork) goes back as far as
// rewindStep would, and from there in the same way. A step in the main
// window resumes as it is.
size_t resumeStep(const Lesson& lesson, size_t active);

// Going back when a window the active step works in closed before the step
// was done (docs/LEARNING.md "Getting back"). Only a few windows hold work
// of the client's own that closing them loses: the designer's design being
// built, the Combat Simulator's battle being set up, Communicate's message
// being written (lostWithWindow). What is done in other windows is done by
// commands the game keeps (a fleet created, an item queued), so closing them
// loses nothing and the lesson only shows the way back.
//
// For such a window, when the active step uses that work (usesWork), the
// lesson goes back to the earliest step before `active` whose condition read
// it, but never past a step whose condition the game itself met (a command,
// an order, an option, a count): those stay done. Returns `active` when there
// is nothing to go back to.
size_t rewindStep(const Lesson& lesson, size_t active, std::string_view window);
// The facts a window holds only in the client, lost when it closes (none for
// most windows).
std::span<const Fact> lostWithWindow(std::string_view window);
// Whether a step uses that work: one of its highlighted or allowed tags lies
// in the window and needs it (not a Close button, nor the simulator's
// Strategies, which works whatever the battle set up).
bool usesWork(const Step& step, std::string_view window);
// Whether a condition holds only by what the client knows (windows, tabs, the
// selection, the work in a window, a battle in the simulator), not by a
// command the game kept or a change of its state.
bool clientOnly(const Condition& c);
// Whether a step works in `window` (one of its highlighted, allowed or shown
// tags lies in it; Close buttons do not count), and whether one of its tags
// opens it.
bool worksIn(const Step& step, std::string_view window);
bool opens(const Step& step, std::string_view window);

// What a saved place in a tutorial depends on: the number of steps and each
// step's tags, keys and condition. Rewording a step keeps it; adding,
// removing, reordering or changing what a step waits for changes it.
uint64_t lessonFingerprint(const Lesson& lesson);

} // namespace opense4::learn
