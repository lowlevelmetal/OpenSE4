#pragma once

// Widgets shared by the planet and construction-queue windows (planets.cpp,
// queues.cpp): lamp buttons, the quadrant mini-map, sortable-table helpers,
// report and scrap popups, and a status line for command results.

#include "client/classic/reports.hpp"
#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/ui.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace opense4::client::classic {

inline constexpr ImVec4 kTextDim{0.55f, 0.62f, 0.72f, 1.0f};
inline constexpr ImVec4 kTextWarn{1.0f, 0.55f, 0.35f, 1.0f};
inline constexpr ImVec4 kTextGood{0.45f, 0.9f, 0.5f, 1.0f};
inline constexpr ImVec4 kTextLabel{0.44f, 0.61f, 1.0f, 1.0f};
inline constexpr ImVec4 kTextHighlight{1.0f, 0.86f, 0.35f, 1.0f};

// A right-column tab, filter or toggle button with the classic lamp: lit
// green when on (docs/spec/06, "radio lights").
bool lampButton(Dialog& d, UiContext& ui, const char* label, bool on, bool enabled = true, const char* tooltip = nullptr);
// A tooltip for the last item, in the dialog font.
void itemTooltip(const char* text);

// The quadrant: systems, known warp links and presence. `marked` (per
// SystemId, may be empty) draws systems holding listed items brighter;
// `highlight` rings one system. Returns the system under the mouse.
std::optional<game::SystemId> quadrantMap(UiContext& ui, const char* id, Vec2 size, const std::vector<uint8_t>& marked,
                                          std::optional<game::SystemId> highlight);

// ---- Tables ----------------------------------------------------------------------------------

inline constexpr float kRowHeight = 26.0f;  // frame pixels
// Text vertically centred in a table row of kRowHeight.
void cellText(UiContext& ui, const std::string& text, const ImVec4& color = ImVec4(1, 1, 1, 1));
void cellImage(UiContext& ui, const Sprite& s, float size = 22.0f);
// Three resource amounts with their icons, centred in the row.
void cellResources(UiContext& ui, const game::Resources& r);
// A thin bar with a caption, centred in the row.
void cellProgress(UiContext& ui, float fraction, const std::string& caption, float width = 0.0f);
// Status icons (docs/spec/06 §4.4) in a row.
void statusIconRow(UiContext& ui, const std::vector<int>& icons, float size = 18.0f);
// Begins a table row with a full-width selectable (id unique within the table)
// and leaves the cursor at the start of column 0.
struct RowEvents {
    bool clicked = false;
    bool doubleClicked = false;
    bool rightClicked = false;
    bool hovered = false;
};
RowEvents tableRow(UiContext& ui, int id, bool selected);

using SortKey = std::variant<int64_t, std::string>;
bool sortKeyLess(const SortKey& a, const SortKey& b);
// Reads the current table's sort column (by user id) and direction.
void readSortSpecs(int& column, bool& ascending);

// ---- Text helpers ------------------------------------------------------------------------------

std::string turnsText(int turns);                     // "3 turns", "1 turn", "never"
std::string resourcesText(const game::Resources& r);  // "1,200 / 0 / 350"
Sprite designSprite(UiContext& ui, game::DesignId d);
Sprite queueItemSprite(UiContext& ui, const game::QueueItem& item);

// ---- Command results ---------------------------------------------------------------------------

struct StatusLine {
    std::string text;
    bool error = false;
    void info(std::string t) { text = std::move(t), error = false; }
    void fail(std::string t) { text = std::move(t), error = true; }
    // Issues a command; on failure records the reason. Returns success.
    bool issue(UiContext& ui, game::Command c);
    void draw(UiContext& ui) const;
    // At a cursor position of the window's content (frame pixels), cut to `width`.
    void drawAt(UiContext& ui, Vec2 at, float width) const;
};

// ---- Popups --------------------------------------------------------------------------------------
//
// Each popup is opened with open...() from anywhere in a window and drawn by
// draw() from one fixed place in the window (ImGui popups need a stable ID
// scope), typically right after the Close button.

// Centred modal popup with a title strip; size in frame pixels. Pair with ImGui::EndPopup().
bool beginModal(UiContext& ui, const char* id, Vec2 size);

// The planet or ship report (docs/spec/06 §1.4) with its tabs.
class ReportPopup {
public:
    void openPlanet(game::ObjectId p);
    void openVehicle(game::VehicleId v);
    void draw(UiContext& ui);

private:
    bool pending_ = false;
    std::optional<game::ObjectId> planet_;
    std::optional<game::VehicleId> vehicle_;
    ReportTab tab_ = ReportTab::Detail;
};

// The Design Report (spec 06 §1.8.3): a design's name, size, design type,
// date created, "(Obsolete)", cost, maintenance cost, movement, shields,
// cargo space, supply capacity and components. A right-click on a queued
// ship, base or unit opens it, as on a design in the buildable list of Set
// Construction Queue and on a design in the Combat Simulator.
class DesignReportPopup {
public:
    void open(game::DesignId d);
    void draw(UiContext& ui);

private:
    bool pending_ = false;
    game::DesignId design_;
};

// "Select Facilities": a checklist of one colony's facilities with their
// refunds; scraps the checked ones.
class ScrapFacilitiesPopup {
public:
    void open(game::ObjectId planet);
    void draw(UiContext& ui, StatusLine& status);

private:
    bool pending_ = false;
    game::ObjectId planet_;
    std::vector<uint8_t> checked_;
};

// Scraps every facility of one chosen type on a set of colonies (all own
// colonies when the set is empty).
class ScrapTypePopup {
public:
    void open(std::vector<game::ObjectId> colonies);
    void draw(UiContext& ui, StatusLine& status);

private:
    bool pending_ = false;
    std::vector<game::ObjectId> colonies_;
    std::optional<uint32_t> chosen_;
};

// A yes/no question; draw() returns true once when confirmed.
class ConfirmPopup {
public:
    void open(std::string question, std::string title = "Confirm");
    bool draw(UiContext& ui);

private:
    bool pending_ = false;
    std::string question_;
    std::string title_ = "Confirm";
};

// A message box with OK only (Esc or Enter also close it).
class NoticePopup {
public:
    void open(std::string text, std::string title);
    void draw(UiContext& ui);

private:
    bool pending_ = false;
    std::string text_;
    std::string title_;
};

} // namespace opense4::client::classic
