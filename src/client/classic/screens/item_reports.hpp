#pragma once

// Reports about catalogue items (components, facilities, vehicle sizes, tech
// areas, intelligence projects, treaties, formations), shared by the Help,
// Designs and Create Design windows: the detail pane and the small item
// report popup that closes when clicked (docs/spec/06 §1.4). Also the list
// row and lamp-button widgets these windows have in common.

#include "client/classic/ui.hpp"

#include <span>
#include <string>
#include <string_view>

namespace opense4::client::classic {

struct ItemRef {
    enum class Kind : uint8_t { None, Component, Facility, Hull, TechArea, IntelProject, Treaty, Formation };
    Kind kind = Kind::None;
    uint32_t index = 0;
    int32_t mount = -1;  // components: weapon mount (CompEnhancement index)

    bool valid() const { return kind != Kind::None; }
    bool operator==(const ItemRef&) const = default;
};

// Full: the Help pane (128 px picture). Report: the popup. Compact: narrow hover panes.
enum class DetailStyle { Full, Report, Compact };

// Draws everything known about an item into the current window.
void itemDetail(UiContext& ui, const ItemRef& item, DetailStyle style);
// A small picture for list rows.
Sprite itemIcon(UiContext& ui, const ItemRef& item);
std::string itemName(const UiContext& ui, const ItemRef& item);

// The item report popup. Call open() from anywhere in the owning window and
// draw() once per frame after the window (outside its Dialog).
class ItemReportPopup {
public:
    void open(ItemRef item) {
        item_ = item;
        request_ = true;
    }
    void draw(UiContext& ui);

private:
    ItemRef item_;
    bool request_ = false;
};

// ---- Widgets --------------------------------------------------------------------------------

// A right-column tab/toggle button with the classic indicator lamp (lit when active).
bool lampButton(UiContext& ui, Dialog& d, const char* label, bool lit, bool enabled = true);

struct RowResult {
    bool clicked = false;
    bool rightClicked = false;
    bool doubleClicked = false;
    bool hovered = false;
};
struct RowStyle {
    float icon = 28.0f;                  // frame pixels
    ImU32 color = IM_COL32(230, 235, 245, 255);
    std::string_view sub;                // optional second line
    std::string_view right;              // optional right-aligned text
    std::string_view badge;              // optional letter drawn on the icon (mount code)
    const game::Resources* cost = nullptr;  // optional cost drawn after `sub`
};
// A selectable list row: icon, text, optional second line. `id` must be unique in the list.
RowResult itemRow(UiContext& ui, int id, const Sprite& icon, std::string_view text, bool selected, const RowStyle& style = {});
// A non-selectable group heading inside a list.
void listHeading(UiContext& ui, std::string_view text);

// Draws a sprite with the window draw list (no layout).
void drawSprite(const Sprite& s, ImVec2 min, ImVec2 max, ImU32 tint = IM_COL32_WHITE);

// ---- Text helpers ----------------------------------------------------------------------------

std::string_view weaponKindName(ruleset::WeaponKind k);
std::string vehicleTypesText(const ruleset::Component& c);
std::string requirementsText(const game::Rules& r, std::span<const ruleset::TechRequirement> reqs);
// An ability as the reports show it: its description with tokens filled in,
// or its identifier and values.
std::string abilityText(const ruleset::Ability& a, int shieldPercent = 100);
std::string mountLabel(const game::Rules& r, int32_t mount);  // "" when unmounted
bool containsNoCase(std::string_view haystack, std::string_view needle);

// Colours shared by these windows.
inline constexpr ImVec4 kDimText{0.55f, 0.62f, 0.72f, 1.0f};
inline constexpr ImVec4 kWarnText{1.0f, 0.45f, 0.40f, 1.0f};
inline constexpr ImVec4 kGoodText{0.45f, 0.90f, 0.50f, 1.0f};
inline constexpr ImVec4 kBlueText{0.44f, 0.61f, 1.0f, 1.0f};

} // namespace opense4::client::classic
