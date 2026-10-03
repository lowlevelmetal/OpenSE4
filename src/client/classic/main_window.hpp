#pragma once

// The classic main window (docs/spec/06 §2): status bar, command panel with
// command buttons, the order strip and selection cycles, the system panel,
// the report/list panel and the galaxy panel, all in the 1024×768 frame.

#include "client/classic/finale.hpp"
#include "client/classic/movement_replay.hpp"
#include "client/classic/order_rules.hpp"
#include "client/classic/movement_line.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/sector_view.hpp"
#include "client/classic/ship_glides.hpp"
#include "client/classic/ui.hpp"
#include "gfx/renderer2d.hpp"

#include <functional>
#include <map>
#include <set>
#include <optional>
#include <string>
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
    // What is selected, as lessons name it (learn::ClientFacts::selected),
    // and how many selections the player has made (the one a game starts
    // with is not counted).
    std::vector<std::string> selectionKinds(const UiContext& ui) const;
    uint64_t selections() const { return selections_; }

private:
    enum class Pick { None, MoveTo, Warp, Colonize, Attack, Patrol, LoadCargo, DropCargo, LaunchRemote, RecoverRemote, Callback };
    // Who an order goes to: a vehicle, a fleet or a colony.
    struct OrderOwner {
        game::VehicleId vehicle;
        game::FleetId fleet;
        game::ObjectId planet;
    };
    // A small picker over the main window (a cargo type, a component, a
    // formation, a confirmation...).
    struct Choice {
        std::string label;
        std::function<void()> action;   // none: a heading row
        bool chosen = false;            // drawn with the green lamp
    };
    struct Chooser {
        std::string title;
        std::string note;
        std::vector<Choice> items;
    };

    // Selection.
    void selectSector(UiContext& ui, game::Sector s, bool cycle);
    void selectVehicle(UiContext& ui, game::VehicleId v);
    void selectPlanet(UiContext& ui, game::ObjectId p);
    void clearSelection();
    const game::Vehicle* selectedVehicle(const UiContext& ui) const;
    const game::Colony* selectedColony(const UiContext& ui) const;
    std::vector<game::ObjectId> objectsAt(const UiContext& ui, game::Sector s) const;
    // The selected sector is marked (the yellow corners, and the coordinate
    // line's range) only while it holds an object the viewer sees, tested at
    // every redraw; the selection itself stays as clicked (spec 06 §2.4, §7 Q64).
    bool selectedSectorMarked(const UiContext& ui) const;
    std::vector<const game::Vehicle*> vehiclesAt(const UiContext& ui, game::Location where) const;
    void cycleVehicle(UiContext& ui, int dir, bool idleOnly);
    void cycleFleet(UiContext& ui, int dir);
    void cycleColony(UiContext& ui, int dir);
    // Tagging (Shift+click, Shift+A, Shift+C; §2.5): a vehicle in a fleet tags the fleet.
    void toggleTag(UiContext& ui, game::VehicleId v);
    void tagAll(UiContext& ui);
    bool tagged(game::VehicleId v) const;

    // Orders.
    LitOrders litNow(UiContext& ui) const;
    std::vector<OrderOwner> orderOwners(UiContext& ui) const;
    void runOrder(UiContext& ui, OrderId o);
    void giveOrder(UiContext& ui, game::Order o);
    void replaceOrders(UiContext& ui, std::vector<game::Order> orders, bool repeat);
    void orderDone();  // the tagged group dissolves after an order
    void startPick(UiContext& ui, Pick p, std::string prompt);
    void completePick(UiContext& ui, game::Location where, std::optional<game::ObjectId> object);
    void finishPatrol(UiContext& ui);
    void openFor(UiContext& ui, ScreenId id);   // a window about the selected vehicle, fleet or colony
    void chooseCargo(UiContext& ui, Pick p);    // Load / Drop Cargo, Launch / Recover Units Remotely: the type first
    void note(UiContext& ui, std::string text);
    void hotkeys(UiContext& ui);
    void drawChooser(UiContext& ui);

    // Drawing.
    void statusBar(UiContext& ui);
    // The minimize button, and the T button that shows the lesson panel again (docs/LEARNING.md).
    void statusButtons(UiContext& ui);
    void commandPanel(UiContext& ui);
    void reportPanel(UiContext& ui);
    void overlayText(UiContext& ui);
    void mouse(UiContext& ui);
    void drawSystem(gfx::Renderer2D& r, UiContext& ui);
    void drawFrame(gfx::Renderer2D& r, UiContext& ui);
    void drawGalaxy(gfx::Renderer2D& r, UiContext& ui);
    std::optional<game::SystemId> galaxySystemAt(const UiContext& ui, Vec2 p, bool exploredOnly) const;

    // Ship movement animation (ship_glides.hpp) and the movement log replay
    // (movement_replay.hpp), updated once per frame.
    void trackMovement(UiContext& ui);
    // Ctrl+P, Ctrl+I, Ctrl+O, Ctrl+U: builds this turn's log first if needed.
    void startReplay(UiContext& ui, OrderId id);
    // What each sector of the shown system draws this frame.
    void prepareSectors(UiContext& ui);
    struct ShownSector {
        game::Sector sector;
        SectorView view;
        std::vector<const game::Vehicle*> vehicles;
    };
    std::vector<ShownSector> sectors_;

    game::SystemId shown_;
    uint64_t selections_ = 0;   // selections the player made (selectSector, selectVehicle, selectPlanet, list rows)
    std::optional<game::Sector> sector_;
    std::optional<game::ObjectId> object_;
    std::optional<game::VehicleId> vehicle_;
    std::optional<game::FleetId> fleet_;
    bool listMode_ = false;
    std::vector<game::VehicleId> tagged_;
    ReportTab tab_ = ReportTab::Detail;

    Pick pick_ = Pick::None;
    std::string pickPrompt_;
    std::vector<game::Location> patrol_;
    std::function<void(game::Location)> pickCallback_;
    game::DesignId pickDesign_;
    std::optional<game::Sector> hover_;
    std::optional<game::SystemId> galaxyHover_;
    std::optional<Chooser> chooser_;
    // The hover hint over the system panel (§2.3): the button's name and key.
    std::string hintName_, hintKey_;
    std::string note_;
    double noteUntil_ = 0.0;
    int orderPage_ = 0;   // the order strip's page at 800x600 (§2.3)

    // The movement line's route, worked out again when the game or the report changes.
    struct MovementLineCache {
        LineSubject subject;
        uint64_t revision = 0;
        uint32_t turn = 0;
        game::movement::PlannedRoute route;
    };
    std::optional<MovementLineCache> line_;

    ShipGlides glides_;
    MovementReplay replay_;
    FinaleWatch finale_;   // the ending windows, each as it comes at a turn's start
    std::set<game::VehicleId> replaySeen_;          // the vehicles of the log the player sees
    std::optional<game::SystemId> replayShownBefore_;
    std::map<game::VehicleId, game::Location> beforeTurn_;  // where we saw everything before this turn
    std::map<game::VehicleId, int> beforeTurnHeadings_;     // and the headings then
    uint32_t beforeTurnFor_ = UINT32_MAX;
    uint32_t seenTurn_ = UINT32_MAX;
    double trackedAt_ = -1.0;
};

} // namespace opense4::client::classic
