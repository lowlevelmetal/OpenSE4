#pragma once

// The computer players of the setup screens (OpenSE4's own, docs/SETUP.md
// "Computer players", docs/sdk/ai-protocol.md §1). When the game's mods offer
// computer players ([[ai.players]]), Game Setup, Empire Setup and Quick
// Start let the player choose who plays the computer empires: the classic AI
// or one of those players. Without such mods the screens are the original's.
//
//   - PlayerPicker: the Computer Players window, a list of the players, each
//     with its mod and description; a click lights a row and chooses it.
//   - LimitsWindow: Game Settings' Computer Player Limits, the budgets of the
//     script players (GameOptions::aiPlanningBudget, aiCallBudget,
//     aiMemoryLimit), advanced options.

#include "client/classic/frontend.hpp"
#include "client/classic/screens/setup_model.hpp"

#include <optional>
#include <string>

namespace opense4::client::classic::setup {

// Whether the game's mods offer computer players: only then do the setup
// screens show their choices.
bool offersComputerPlayers(const game::Rules& r);

// Quick Start's line about who plays the computer empires, and the button
// for the Computer Players window under it (true when pressed): OpenSE4's
// own, in the area's left column above the line about the mods. The text
// ends at `textBottom`; places in ImGui units.
bool playersLine(MenuContext& ctx, std::string_view text, float textBottom, ImVec2 textLeft, ImVec2 buttonAt, ImVec2 buttonSize, float width);

class PlayerPicker {
public:
    struct Options {
        std::string title = "Computer Players";
        std::string question = "Who plays the computer empires that have no player of their own?";
        // The first row is the game's choice (an empire of Empire Setup), named
        // after it: `gameChoice` is that player's name.
        bool gameChoiceRow = false;
        std::string gameChoice;
        // The game option "computer players see everything" as a check box
        // under the list (Quick Start, which has no option pages).
        bool* seesEverything = nullptr;
    };

    void open() { open_ = pending_ = true; }
    bool isOpen() const { return open_; }
    // Draws the window while it is open. `choice` is the row lit (nullopt:
    // the game's choice row); a click on a row changes it at once (true that
    // frame). Done or Esc closes the window.
    bool draw(MenuContext& ctx, const game::Rules& r, std::optional<game::Controller>& choice, const Options& o);

private:
    bool open_ = false, pending_ = false;
};

class LimitsWindow {
public:
    void open() { open_ = pending_ = true; }
    bool isOpen() const { return open_; }
    // Draws the window while it is open; changes `o` at once (true that frame).
    bool draw(MenuContext& ctx, game::GameOptions& o);

private:
    bool open_ = false, pending_ = false;
};

// ---- The mods' game options (docs/sdk/rules.md "Game options", docs/SETUP.md "Mods' options") --------

// Whether the game's rules mods declare game options: only then do the setup
// screens and the lobby show their Mod Options.
bool offersModOptions(const game::Rules& r);
// "Mod options: Beacon bonus 120, Solar flares off": the options' values in
// `o` (their defaults until set), for a line under the setup or the lobby.
std::string modOptionsSummary(const game::Rules& r, const game::GameOptions& o);

// The Mod Options window: each option of the game's rules mods with its value,
// a switch (a lamp) or a whole number in its range, in the mod's language. A
// change is made in `o` at once (true that frame); `readOnly` only shows them.
class ModOptionsWindow {
public:
    void open() { open_ = pending_ = true; }
    bool isOpen() const { return open_; }
    bool draw(MenuContext& ctx, const game::Rules& r, game::GameOptions& o, bool readOnly = false);

private:
    bool open_ = false, pending_ = false;
};

// A line of text in the setup area's left column with a button under it
// (the Computer Players and Mod Options lines of Quick Start): true when the
// button is pressed. Places in ImGui units.
bool setupLine(MenuContext& ctx, std::string_view text, float textBottom, ImVec2 textLeft, ImVec2 buttonAt, ImVec2 buttonSize, float width,
               const char* button);

} // namespace opense4::client::classic::setup
