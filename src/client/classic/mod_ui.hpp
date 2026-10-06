#pragma once

// The interface tier of the modding SDK in the classic client
// (docs/sdk/interface.md, sdk/ui.hpp): the extensions of the mods the data
// set was loaded with, their text in the chosen language, the values they
// show (from the player's own view, worked out again only when the game
// changes) and the pieces the windows draw them with: report panels, list
// columns, Empires pages, mod orders' labels and icons, error boxes.
//
// Without such mods nothing here draws anything, and the classic windows are
// exactly the original's.

#include "client/classic/ui.hpp"
#include "client/input.hpp"
#include "sdk/rules.hpp"
#include "sdk/ui.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// The mods' interface extensions and strings for a data set (the rules'
// mods, sdk::gamePackages), read once per data set and language.
struct ModUi {
    sdk::UiExtensions ext;
    sdk::UiTexts texts;
};
const ModUi& modUi(const game::Rules& r);

// ---- Text ------------------------------------------------------------------------------------------

// A mod's string (text/<lang>.toml), else `fallback`.
std::string modText(const game::Rules& r, std::string_view mod, std::string_view key, std::string_view fallback);
// The mod's name ("mod.name", else its manifest's name, else its id).
std::string modName(const game::Rules& r, std::string_view mod);
std::string modOrderLabel(const game::Rules& r, std::string_view mod, const mods::ModOrderDecl& o);
std::string modOrderDescription(const game::Rules& r, std::string_view mod, const mods::ModOrderDecl& o);
std::string modOptionLabel(const game::Rules& r, std::string_view mod, const mods::ModOptionDecl& o);
std::string modOptionDescription(const game::Rules& r, std::string_view mod, const mods::ModOptionDecl& o);
std::string modPanelTitle(const game::Rules& r, const sdk::UiPanel& p);
std::string modRowLabel(const game::Rules& r, const sdk::UiPanel& p, const sdk::UiRow& row, size_t index);
std::string modColumnLabel(const game::Rules& r, const sdk::UiColumn& c);
std::string modPageTitle(const game::Rules& r, const sdk::UiEmpirePage& p);
std::string modPageColumnLabel(const game::Rules& r, const sdk::UiEmpirePage& p, const sdk::UiColumn& c, size_t index);

// ---- Keys --------------------------------------------------------------------------------------------

// The actions the mods' interfaces bring to the rebindable keys: each order
// of a rules mod (with the key its [[order]] suggests), each panel and
// Empires page that suggests a key (input.hpp ModAction).
std::vector<ModAction> modActionsOf(const game::Rules& r);
// Makes them the keys in use (setModActions).
void useModActions(const game::Rules& r);
// The action's key went down this frame.
bool modActionPressed(std::string_view id);

// ---- Values ------------------------------------------------------------------------------------------

// The values for these requests in the session's game as the player sees
// it, worked out once per game state (ClassicSession::revision) and kept.
std::vector<sdk::UiShown> modValues(UiContext& ui, std::span<const sdk::UiValueRequest> requests);
// Whether a value of the last modValues call waited for the script runtime.
bool modValuesPending();

// ---- Drawing -----------------------------------------------------------------------------------------

// A small red box in place of a mod's panel, page or cell that failed: what
// failed and why, the traceback under the pointer.
void modErrorBox(UiContext& ui, std::string_view what, const sdk::UiShown& failed, float width = 0.0f);

// The panels that apply to a report about `thing`, owned by `owner` (none: nobody's).
std::vector<const sdk::UiPanel*> modPanelsFor(UiContext& ui, sdk::UiReport report, game::EmpireId owner);
// Draws the panels in the current window's layout (title, label and value
// rows, order buttons), each failing one as an error box; returns the order
// button clicked (its mod and order).
std::optional<sdk::UiButton> drawModPanels(UiContext& ui, std::span<const sdk::UiPanel* const> panels, const sdk::UiThing& thing);

// The values of `columns` for each of `things` (a list window's rows), by row then column.
std::vector<std::vector<sdk::UiShown>> modColumnValues(UiContext& ui, std::span<const sdk::UiColumn* const> columns, std::span<const sdk::UiThing> things);
// The columns a list window shows on its Mods tab (none: no such tab).
std::vector<const sdk::UiColumn*> modColumnsFor(UiContext& ui, sdk::UiList list);
// One mod cell: its value, or "error" in red with the reason under the pointer.
void modCell(UiContext& ui, const sdk::UiShown& v);

// A list window's Mods tab: its rows (each with its picture and name) and the
// mods' columns for that list, in the classic list's box with its arrow
// column. Returns the row clicked with the left or right button.
struct ModListRow {
    std::string name;
    Sprite picture;
    sdk::UiThing thing;
};
struct ModListClick {
    std::optional<size_t> left, right;
    std::optional<size_t> hovered;
};
ModListClick drawModList(UiContext& ui, const char* id, std::span<const ModListRow> rows, sdk::UiList list, ImVec2 size, float nameWidth = 134.0f);

// An Empires page: a table over the empires listed (the player's first).
void drawModEmpirePage(UiContext& ui, const sdk::UiEmpirePage& page, std::span<const game::EmpireId> empires);

// The picture a mod gives one of its orders (sdk::UiOrderStyle::icon), or none.
Sprite modOrderIcon(UiContext& ui, std::string_view mod, std::string_view order);

} // namespace opense4::client::classic
