#pragma once

// The fixed vocabularies that lessons and manual pages refer to: window ids,
// Help tabs, kinds of selection, order kinds, command names and the UI tags
// the client registers (docs/LEARNING.md "Reference"). The content is
// validated against these lists, so they are kept here, headless; the client
// uses the same strings when it tags its windows and widgets.

#include "game/state.hpp"

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

// The main window's tags (command buttons, the order strip, End Turn, the
// status bar, the panels) and the widget tags inside windows, without the
// `window:<id>` tags, which exist for every window.
std::span<const std::string_view> fixedUiTags();
// The id of an order-strip slot ("Move" -> "move-to"); empty for an empty slot.
std::string_view orderStripId(std::string_view slotKey);
// True for a tag the client registers: a fixed tag or `window:<id>`.
bool isUiTag(std::string_view tag);

} // namespace opense4::learn
