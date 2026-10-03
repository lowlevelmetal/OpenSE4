#pragma once

// Widgets shared by the research, intelligence, diplomacy and log windows:
// the known-empires list, a galaxy mini-map drawn with ImDrawList, empire
// labels, progress bars, project paging, the Reorder dialog and a status line
// for command results.

#include "client/classic/screens/empire_logic.hpp"
#include "client/classic/screens/reorder_popup.hpp"
#include "client/classic/ui.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

inline const ImVec4 kTextDim{0.55f, 0.62f, 0.72f, 1.0f};
inline const ImVec4 kTextBlue{0.44f, 0.61f, 1.0f, 1.0f};
inline const ImVec4 kTextGood{0.45f, 0.85f, 0.45f, 1.0f};
inline const ImVec4 kTextBad{1.0f, 0.45f, 0.40f, 1.0f};
inline const ImVec4 kTextWarn{1.0f, 0.82f, 0.35f, 1.0f};

// Empires the player has met, in id order (the diplomacy windows list these).
std::vector<game::EmpireId> knownEmpires(const UiContext& ui);
// The player followed by the empires they have met.
std::vector<game::EmpireId> usAndKnown(const UiContext& ui);

// Small flag, then the empire name in its colour. `framed` lines it up with
// widgets (checkboxes, buttons) on the same line.
void empireLabel(UiContext& ui, game::EmpireId e, bool large = false, bool framed = false);
// Text wrapped to the current column.
void wrappedText(const std::string& text, ImVec4 color = ImVec4(0.85f, 0.88f, 0.94f, 1.0f));
// A progress bar (fraction 0..1) with a centred caption, `width` in frame pixels (0 = fill).
void progressBar(UiContext& ui, float fraction, const std::string& caption, float width = 0.0f, float height = 16.0f);
// A picture framed by a thin border, or an empty frame when it is missing.
void framedImage(UiContext& ui, const Sprite& s, Vec2 frameSize);
// Multi-line text input on a std::string (size in ImGui units).
bool inputTextMultiline(const char* id, std::string& text, ImVec2 size);

// ---- Galaxy mini-map --------------------------------------------------------------------------

struct MiniMapStyle {
    std::vector<game::SystemId> highlight;                  // ringed in yellow
    std::vector<std::pair<game::SystemId, ImU32>> fills;    // explicit colours (claims ...)
    bool owners = true;                                      // colour systems by known colonies
    bool clickable = false;
};

struct MiniMapResult {
    std::optional<game::SystemId> hovered;
    std::optional<game::SystemId> clicked;
};

// Draws the quadrant into a `frameSize` box at the cursor.
MiniMapResult miniMap(UiContext& ui, const char* id, Vec2 frameSize, const MiniMapStyle& style);

// ---- Project queues (Research, Intelligence) ------------------------------------------------------

inline constexpr int kProjectsPerPage = 4;
inline constexpr int kMaxProjects = 12;
// "Projects 1-4", "Projects 5-8", "Projects 9-12" tab buttons in the button column.
void projectPageButtons(Dialog& d, int& page);

// One project box of the Research and Intelligence windows (spec 06 §7 Q92,
// confirmed: binary; Intelligence uses the Research layout, spec 07 session 5).
struct ProjectBox {
    std::string name;        // the area or project; empty: an empty slot ("None")
    std::string level;       // "Research Level N" under the name, or empty
    int64_t remaining = 0;   // points it still needs
    int64_t perTurn = 0;     // the points it gets this turn
    int percent = 0;         // points paid of the cost, 0..100
};
// Where the four boxes of a page start, in the window (140×130 each, side by side).
inline constexpr Vec2 kProjectBoxesAt{15, 330};
// The four boxes of the shown page from `at` (window coordinates of `d`): a
// black cell (the RowGrid pattern under the pointer when it holds a project)
// with its top, right and bottom edges in #617BC2; the name word-wrapped in
// (3,3)-(138,55) ("None" for an empty slot), the level at (3,54) in small
// type, "Completion:" and "Cost Per Turn:" at (3,75) and (3,87) with their
// values at x 82; at its foot the small box (1,102)-(138,128) holding a bar of
// up to 19 green 5×22 blocks, 7 px apart from x 3. Returns the box clicked
// (one holding a project), or -1; `hovered` gets the one under the pointer.
int projectBoxes(UiContext& ui, const Dialog& d, Vec2 at, const std::vector<ProjectBox>& boxes, int* hovered = nullptr);
// Completion as the boxes write it: the whole turns left, rounded up and at
// least one, times the turn length ("0.3 years"); "Never" without points
// (OpenSE4's word, inferred).
std::string completionText(int64_t remaining, int64_t perTurn);


// The last command result or notice, shown under a window's content.
class StatusLine {
public:
    void set(std::string text, bool error) {
        text_ = std::move(text);
        error_ = error;
    }
    // Shows an error from a command result (nothing on success).
    void result(const game::CommandResult& r) {
        if (!r.ok) set(r.error, true);
        else text_.clear();
    }
    void draw() const;
    bool empty() const { return text_.empty(); }

private:
    std::string text_;
    bool error_ = false;
};

} // namespace opense4::client::classic
