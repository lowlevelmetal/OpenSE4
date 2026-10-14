#pragma once

// The original's type list window (docs/spec/06 §7 Q94, spec 02 §10,
// confirmed: binary): one Name column with Add Type, Delete Type, OK and
// Cancel. The colony-type pickers (Colonies' Set Colony Type and the
// question a new colony asks) and Create Design's Design Type box share it.
//
// A click selects a row; OK, a double click or a click on the row already
// selected chooses it. Add Type asks for a name and adds it at the end when it
// is not empty, not yet listed and the list is under its limit, else nothing;
// Delete Type removes the selected row without asking. The window does not
// change the empire's lists itself: its result says what was added or
// deleted, and the caller sends cmd::SetColonyTypes or cmd::SetDesignTypes.

#include "client/classic/screens/ships_common.hpp"
#include "client/classic/ui.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

class TypeListPicker {
public:
    struct Config {
        std::string title;       // the window's title ("Select Colony Type")
        std::string addTitle;    // Add Type's question ("Colony Type Name")
        size_t limit = 255;      // Add Type adds nothing once the list holds this many
        std::string marked;      // a type shown with " (suggested)" after its name
        std::string text;        // a line above the list (empty: none)
        size_t rows = 0;         // drawBody: the list shows up to this many rows (0: it fills the window)
        // For lessons: each row tagged as `<tagChooser>:<tagOption(type)>`.
        std::string tagChooser;
        std::function<std::string(const std::string&)> tagOption;
    };
    struct Result {
        std::optional<std::string> chosen;   // OK, a double click or a click on the selected row
        bool cancelled = false;              // Cancel or Esc
        std::optional<std::string> added;    // Add Type: append this name
        std::optional<std::string> deleted;  // Delete Type: remove this name
    };

    // Opens it as a modal popup (drawPopup), with `preselect` selected.
    void open(Config config, std::optional<std::string> preselect = std::nullopt);
    // Every frame from one fixed place, as the other popups (ships_common.hpp).
    Result drawPopup(UiContext& ui, const std::vector<std::string>& types);
    // The list and its buttons inside the window being drawn (for a window
    // that is a question of its own, such as the colonization one).
    void configure(Config config, std::optional<std::string> preselect = std::nullopt);
    Result drawBody(UiContext& ui, const std::vector<std::string>& types);
    const Config& config() const { return config_; }

private:
    Config config_;
    std::optional<std::string> selected_;
    bool request_ = false;
    shipui::TextPrompt prompt_;
};

} // namespace opense4::client::classic
