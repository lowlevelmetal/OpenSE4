#pragma once

// Drawing helpers shared by the ship windows (ships.cpp and friends): list
// panels with picture rows and indicator lamps, a mini galaxy map, a status
// line for command results, and small modal popups (report, text prompt,
// list picker, confirmation).
//
// Popups follow one pattern: open() only records the request; draw() must be
// called every frame from one fixed place in the window (the ship windows
// call them after the Close button), so ImGui sees OpenPopup and BeginPopup
// under the same ID stack.

#include "client/classic/reports.hpp"
#include "client/classic/screens/ships_logic.hpp"
#include "client/classic/ui.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic::shipui {

inline constexpr ImVec4 kDim{0.55f, 0.62f, 0.72f, 1.0f};
inline constexpr ImVec4 kGood{0.45f, 0.95f, 0.55f, 1.0f};
inline constexpr ImVec4 kBad{1.0f, 0.45f, 0.40f, 1.0f};

// ---- Defaults for windows opened without arguments ------------------------------------------

// The first own ship (not a unit), else the first own vehicle.
game::VehicleId firstOwnShip(const UiContext& ui);
std::optional<game::ObjectId> homeworld(const UiContext& ui);
const game::Vehicle* ownVehicle(const UiContext& ui, game::VehicleId id);
const game::Colony* ownColony(const UiContext& ui, game::ObjectId planet);
std::vector<const game::Vehicle*> ownVehiclesAt(const UiContext& ui, game::Location where);
std::vector<const game::Colony*> ownColoniesAt(const UiContext& ui, game::Location where);

// A cargo holder in a sector: an own vehicle or an own colony.
struct Holder {
    game::VehicleId vehicle;
    game::ObjectId planet;
    bool valid() const { return vehicle.valid() || planet.valid(); }
    bool operator==(const Holder&) const = default;
};

// Asks the main window for a location (the calling window should close so
// the map takes input), then gives `order` there: appended to the owner's
// orders, or at the head when `immediate`.
void pickLocationForOrder(UiContext& ui, OrderOwner owner, game::Order order, std::string prompt, bool immediate = false);

// ---- Text ---------------------------------------------------------------------------------

std::string describeOrder(const UiContext& ui, const game::Order& o, game::DesignId design = {});
std::string cargoSummary(const UiContext& ui, const game::Cargo& c);
std::string ownerName(const UiContext& ui, OrderOwner o);

// ---- Pictures -------------------------------------------------------------------------------

Sprite designMini(UiContext& ui, game::DesignId d);
// Unit groups use the race's group icon when there is one.
Sprite unitMini(UiContext& ui, const game::Vehicle& v);
Sprite fleetMini(UiContext& ui);
Sprite colonySprite(UiContext& ui, game::ObjectId planet);

// ---- Status line ------------------------------------------------------------------------------

class Status {
public:
    void ok(std::string text);
    void error(std::string text);
    void clear() { text_.clear(); }
    // Issues a command; shows its error (or `done` when it succeeds and done is non-empty).
    bool issue(UiContext& ui, game::Command c, std::string done = {});
    void draw(UiContext& ui) const;
    const std::string& text() const { return text_; }

private:
    std::string text_;
    bool error_ = false;
};

// ---- Lists -------------------------------------------------------------------------------

// A captioned, bordered list; size in ImGui units (0 = fill). Always call endPanel.
void beginPanel(UiContext& ui, const char* id, const std::string& caption, ImVec2 size);
void endPanel(UiContext& ui, const char* footnote = nullptr);

enum class Lamp { None, Off, On };
struct RowClick {
    bool left = false;
    bool right = false;
    bool hovered = false;
};
struct RowStyle {
    float height = 36.0f;    // frame pixels
    float picture = 30.0f;
    float indent = 0.0f;
    Lamp lamp = Lamp::None;
    bool selected = false;
    bool enabled = true;
};
// One picture row: lamp, picture, a title line and an optional dim second line.
RowClick row(UiContext& ui, int id, const Sprite& picture, std::string_view title, std::string_view detail, const RowStyle& style = {});

// The right-hand button column's step buttons; returns true when changed.
bool stepButtons(Dialog& d, Step& step);

// ---- Mini galaxy map ------------------------------------------------------------------------

// Explored systems, known warp links, own presence; `highlight` gets a ring.
void miniMap(UiContext& ui, ImVec2 size, std::optional<game::SystemId> highlight, const std::vector<game::SystemId>& marked = {});

// ---- Popups ---------------------------------------------------------------------------------

// Ship, fleet or planet report (docs/spec/06 §1.4), opened by right-clicking a row.
class ReportPopup {
public:
    void vehicle(game::VehicleId id);
    void fleet(game::FleetId id);
    void planet(game::ObjectId id);
    void draw(UiContext& ui);

private:
    game::VehicleId vehicle_;
    game::FleetId fleet_;
    game::ObjectId planet_;
    ReportTab tab_ = ReportTab::Detail;
    bool request_ = false;
};

// A one-line text prompt with OK / Cancel; draw() returns the text on OK.
class TextPrompt {
public:
    void open(std::string title, std::string text, std::string initial);
    std::optional<std::string> draw(UiContext& ui);

private:
    std::string title_, text_;
    char buffer_[65]{};
    bool request_ = false;
    bool focus_ = false;
};

// Pick one entry of a list; draw() returns the chosen index.
class ListPicker {
public:
    struct Item {
        std::string label;
        std::string detail;
        bool enabled = true;
        Sprite picture;
    };
    void open(std::string title, std::vector<Item> items, int current = -1);
    std::optional<size_t> draw(UiContext& ui);

private:
    std::string title_;
    std::vector<Item> items_;
    int current_ = -1;
    bool request_ = false;
};

// Cargo or unit type plus an amount, for the deferred Load / Drop Cargo and
// remote Launch / Recover orders; the caller then asks for a location.
class TypeAmountPicker {
public:
    struct Choice {
        std::string label;
        game::DesignId design;  // invalid = population
        Sprite picture;
    };
    struct Result {
        game::DesignId design;
        int amount = -1;  // -1 = all
    };
    void open(std::string title, std::string text, std::vector<Choice> choices);
    std::optional<Result> draw(UiContext& ui);

private:
    std::string title_, text_;
    std::vector<Choice> choices_;
    int selected_ = 0;
    bool all_ = true;
    int amount_ = 10;
    bool request_ = false;
};

// Yes / No question; draw() returns true once confirmed.
class Confirm {
public:
    void open(std::string title, std::string text);
    bool draw(UiContext& ui);

private:
    std::string title_, text_;
    bool request_ = false;
};

} // namespace opense4::client::classic::shipui
