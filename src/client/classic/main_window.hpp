#pragma once

// The classic main window (docs/spec/06 §2): status bar, command panel with
// command buttons, the order strip and selection cycles, the system panel,
// the report/list panel and the galaxy panel, all in the 1024×768 frame.

#include "client/classic/reports.hpp"
#include "client/classic/ui.hpp"
#include "gfx/renderer2d.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace opense4::client::classic {

class MainWindow {
public:
    // Shows the player's home system with the homeworld selected.
    void reset(UiContext& ui);
    // Input and ImGui overlays; `blocked` when a modal window has the input.
    void update(UiContext& ui, bool blocked);
    void render(gfx::Renderer2D& r, UiContext& ui);
    // Applies navigation requests from other windows.
    void applyRequests(UiContext& ui);

    game::SystemId shownSystem() const { return shown_; }

private:
    enum class Pick { None, MoveTo, Warp, Colonize, Attack, Patrol, LoadCargo, DropCargo, Callback };
    struct OrderButton {
        const char* label;
        const char* tooltip;
        Sprite icon;
        bool enabled;
        std::function<void()> action;
    };

    // Selection.
    void selectSector(UiContext& ui, game::Sector s, bool cycle);
    void selectVehicle(UiContext& ui, game::VehicleId v);
    void selectPlanet(UiContext& ui, game::ObjectId p);
    void clearSelection();
    const game::Vehicle* selectedVehicle(const UiContext& ui) const;
    const game::Colony* selectedColony(const UiContext& ui) const;
    std::vector<game::ObjectId> objectsAt(const UiContext& ui, game::Sector s) const;
    std::vector<const game::Vehicle*> vehiclesAt(const UiContext& ui, game::Location where) const;
    void cycleVehicle(UiContext& ui, int dir, bool idleOnly);
    void cycleFleet(UiContext& ui, int dir);
    void cycleColony(UiContext& ui, int dir);

    // Orders.
    std::vector<OrderButton> availableOrders(UiContext& ui);
    void giveOrder(UiContext& ui, game::Order o);
    void replaceOrders(UiContext& ui, std::vector<game::Order> orders, bool repeat);
    void startPick(UiContext& ui, Pick p, std::string prompt);
    void completePick(UiContext& ui, game::Location where, std::optional<game::ObjectId> object);
    void hotkeys(UiContext& ui);

    // Drawing.
    void statusBar(UiContext& ui);
    void commandPanel(UiContext& ui);
    void reportPanel(UiContext& ui);
    void overlayText(UiContext& ui);
    void mouse(UiContext& ui);
    void drawSystem(gfx::Renderer2D& r, UiContext& ui);
    void drawGalaxy(gfx::Renderer2D& r, UiContext& ui);

    game::SystemId shown_;
    std::optional<game::Sector> sector_;
    std::optional<game::ObjectId> object_;
    std::optional<game::VehicleId> vehicle_;
    std::optional<game::FleetId> fleet_;
    bool listMode_ = false;
    ReportTab tab_ = ReportTab::Detail;
    int orderPage_ = 0;
    bool showMovementLines_ = true;

    Pick pick_ = Pick::None;
    std::string pickPrompt_;
    std::vector<game::Location> patrol_;
    std::function<void(game::Location)> pickCallback_;
    game::DesignId pickDesign_;
    std::optional<game::Sector> hover_;
};

} // namespace opense4::client::classic
