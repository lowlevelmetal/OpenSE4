#pragma once

// The fixed vocabularies that lessons and manual pages refer to: window ids,
// Help tabs, kinds of selection, order kinds, command names and the UI tags
// the client registers (docs/LEARNING.md "Reference"). The content is
// validated against these lists, so they are kept here, headless; the client
// uses the same strings when it tags its windows and widgets.

#include "game/state.hpp"
#include "game/tactical.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

struct WindowInfo {
    std::string_view id;      // "research", "create-design": the kebab-case of the client's ScreenId name
    bool openable = true;     // a `window:` link may open it (battle windows need a battle)
};
// Every window of the classic client, in ScreenId order, then `learn` and `manual`.
std::span<const WindowInfo> windows();
const WindowInfo* findWindow(std::string_view id);

// Tabs of the Help window that `help:<tab>` links open.
std::span<const std::string_view> helpTabs();
bool isHelpTab(std::string_view tab);

// What `selected = "<kind>"` can name (see Facts::selected).
std::span<const std::string_view> selectionKinds();
bool isSelectionKind(std::string_view kind);

// Order kinds for `order = "<kind>"`: "move-to", "colonize", ...
std::string_view orderKindId(game::OrderKind k);
std::optional<game::OrderKind> orderKindFromId(std::string_view id);

// Command names for `command = "<name>"`: the command types of
// game/commands.hpp ("QueueAdd", "SetResearch", ...), as game::commandName gives them.
std::span<const std::string_view> commandNames();
bool isCommandName(std::string_view name);

// The tabs and filters windows show, as "<window>:<tab>" ("log:combat",
// "planets:colonizable"): what `tab = "<window>:<tab>"` names; each is a UI tag too.
std::span<const std::string_view> windowTabs();
bool isWindowTab(std::string_view tab);

// On/off settings of an empire for `option = "<name>"`: the research and
// intelligence switches, the movement and colonization options, and the
// Empire Options (game::InterfaceOptions), in kebab case.
std::vector<std::string_view> optionNames();
bool isOptionName(std::string_view id);
std::optional<bool> optionValue(const game::Empire& e, std::string_view id);

// Orders in a tactical battle for `battle_order = "<kind>"`: "move", "fire",
// "end-turn", "auto", ... and the kind of a tactical order.
std::span<const std::string_view> battleOrderKinds();
bool isBattleOrderKind(std::string_view kind);
std::string_view battleOrderId(game::combat::TacticalOrder::Kind kind);

// Treaties for `treaty = "<kind>"`: "war", "non-aggression", "trade-alliance", ...
std::vector<std::string_view> treatyKinds();
std::optional<game::Treaty> treatyFromId(std::string_view id);

// The main window's tags (command buttons, the order strip, End Turn, the
// status bar, the panels), the widget tags inside windows and the window
// tabs, without the `window:<id>` tags, which exist for every window.
std::span<const std::string_view> fixedUiTags();
// The id of an order-strip slot ("Move" -> "move-to"); empty for an empty slot.
std::string_view orderStripId(std::string_view slotKey);
// True for a tag the client registers: a fixed tag, `window:<id>`, or
// `<id>:close` (the Close button at the bottom of most windows), or an option
// of a chooser (`designs:create:ship`, below).
bool isUiTag(std::string_view tag);

// ---- Choices (docs/LEARNING.md "Choices") ----------------------------------------------------

// A chooser whose options a tutorial step can name one by one: the tag of the
// chooser (a list, a drop-down box, or the button that opens a picker) and its
// options, each tagged `<chooser>:<option>` where it is drawn. A step that
// names an option (`allow = ["designs:create:ship"]`) lets only the options it
// names through the input lock; `<chooser>:*` names them all ("any will do").
struct ChoiceGroup {
    std::string_view tag;                       // "designs:create"
    std::vector<std::string_view> options;      // "ship", "base", ...
    // Its options show only once the chooser is pressed (a picker or a
    // drop-down list) or the game asks (Colony Type): not on screen when a step
    // begins.
    bool picker = false;
    std::string_view what;                      // for the reference: "the vehicle types Create asks for"
    // Its options are names from the data set (the race's design types, which
    // need not be the AI's): every `<chooser>:<id>` drawn is an option of it,
    // while a step names one of `options`.
    bool open = false;
};
std::span<const ChoiceGroup> choiceGroups();
// The chooser a tag is an option of (`designs:create:ship`, `designs:create:*`).
// `drawn`: a tag the client drew, which for an open chooser may be any id.
const ChoiceGroup* choiceGroupOf(std::string_view tag, bool drawn = false);
// A name as an option id: lower case, letters and digits kept, every other run
// of characters one hyphen ("Attack Ship" "attack-ship", "Colony (Rock)"
// "colony-rock", "Trade & Research Alliance" "trade-research-alliance").
std::string optionId(std::string_view name);
// The vehicle types of Create's picker and of `design_vehicle` ("ship", "base",
// "fighter", ..., "weapon-platform").
std::string_view vehicleTypeId(ruleset::VehicleType t);
std::span<const std::string_view> vehicleTypeIds();

// Key chords for a tutorial step's `keys`: optional "Ctrl+", "Shift+" and
// "Alt+" (in that order), then a key named as Dear ImGui names it: "A".."Z",
// "0".."9", "F1".."F12", "Enter", "Escape", "Space", "Tab", "Backspace",
// "Delete", "Insert", "Home", "End", "PageUp", "PageDown", the arrows
// ("LeftArrow", ...), "Comma", "Period", "Minus", "Equal" and "Slash".
bool isKeyChord(std::string_view chord);

} // namespace opense4::learn
