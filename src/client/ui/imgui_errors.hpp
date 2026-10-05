#pragma once

// Dear ImGui's recoverable errors: a widget used the wrong way, such as the
// cursor placed past a window's content with no item after it. Dear ImGui
// goes on after one; by default it also shows a red tooltip over the game
// and asserts. Players never see either: every distinct error goes to the
// log (opense4.log) once, and release builds stop there (docs/BUILDING.md,
// "Dear ImGui's errors").

#include <cstddef>
#include <string>
#include <string_view>

struct ImGuiIO;

namespace opense4::client {

// How the current Dear ImGui context reports its recoverable errors. Each
// distinct error (its window and message) is written to the log once.
//  - `forPlayers` (release builds): nothing else; no tooltip, no assert, and
//    no tooltip for widgets sharing an id.
//  - otherwise (debug builds): Dear ImGui's tooltip and assert stay, so that
//    a developer sees the error at once; with `scripted` it does not assert:
//    an input script fails with the message instead (App::frame).
void setupImGuiErrors(ImGuiIO& io, bool forPlayers, bool scripted);

// The distinct errors reported so far in this run, and the newest one, as
// logged: "In window '<name>': <message>".
size_t imguiErrorCount();
std::string lastImGuiError();

// The line logged for an error, or nothing when the same one (window and
// message) was logged before in this run. Used by the error callback.
std::string noteImGuiError(std::string_view window, std::string_view message);

} // namespace opense4::client
