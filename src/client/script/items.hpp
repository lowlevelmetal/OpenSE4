#pragma once

// The widgets of the frame drawn last, for input scripts (docs/BUILDING.md
// "Input scripts"): every labelled Dear ImGui item, through Dear ImGui's item
// hooks (imgui_item_hook.h), and the widgets of ours that draw their own
// label over an invisible button (the classic buttons, list headings,
// Markdown links), which report themselves. Collected only while a script
// runs or a session is recorded; otherwise every call here returns at once.

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::script {

struct Item {
    std::string label;   // as written: "Keep Playing", "Name##col", "##up", "link:economy#minerals"
    std::string scope;   // what was being drawn: a window id ("research"), "main", "lesson", "front", or ""
    std::string window;  // the Dear ImGui window it is in (the root window's name)
    ImVec2 min, max;     // the visible part (clipped to its window)
    bool disabled = false;
};

// The label as shown: "Name##col" shows "Name", "##up" nothing.
std::string_view visibleLabel(std::string_view label);
// Whether `wanted` names `label`: the label as written, the part shown, or
// (for "##id") the hidden id.
bool labelMatches(std::string_view label, std::string_view wanted);

// Collecting on or off (and Dear ImGui's hooks with it).
void collectItems(bool on);
bool collectingItems();

// A widget of ours, the last ImGui item: its label as shown.
void reportItem(std::string_view label);
// ... or anything at a place (ImGui units), such as a link or a piece on a map.
void reportItem(std::string_view label, ImVec2 min, ImVec2 max, bool disabled = false);

// The scope of the items drawn while it lives (the window being drawn).
class ItemScope {
public:
    explicit ItemScope(std::string_view scope);
    ~ItemScope();
    ItemScope(const ItemScope&) = delete;
    ItemScope& operator=(const ItemScope&) = delete;

private:
    std::string previous_;
    bool active_ = false;
};

// At the end of a frame (after the mode drew): its items become the ones
// lastItems() returns, until the next frame ends.
void endItemFrame();
const std::vector<Item>& lastItems();

} // namespace opense4::client::script
