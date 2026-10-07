#pragma once

// The classic main window (docs/spec/06 §2): status bar, command panel with
// command buttons, the order strip and selection cycles, the system panel,
// the report/list panel and the galaxy panel, all in the 1024×768 frame.

#include "client/classic/computer_players.hpp"
#include "client/classic/mod_ui.hpp"
#include "client/classic/finale.hpp"
#include "client/classic/movement_replay.hpp"
#include "client/classic/order_rules.hpp"
#include "client/classic/movement_line.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/ships_common.hpp"
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

    // Moves in the shown system are still being shown (ship_glides.hpp): the
    // glides of the moves made since the last call start first, so a call
    // right after an engine call sees its moves (turn_start.hpp).
    bool movesShowing(UiContext& ui);
    // The player skipped them: every ship is drawn on its square at once.
    void showMovesAtOnce() { glides_.finish(); }
    // The ending windows due at this start of a turn, in the order they are
    // shown (finale.hpp); nothing until the next start of a turn. The caller
    // opens them when the turn's start lets it (turn_start.hpp).
    std::vector<FinaleKind> endingsDue(UiContext& ui);

    game::SystemId shownSystem() const { return shown_; }
    // What is selected, as lessons name it (learn::ClientFacts::selected),
    // and how many selections the player has made (the one a game starts
    // with is not counted).
    std::vector<std::string> selectionKinds(const UiContext& ui) const;
    uint64_t selections() const { return selections_; }
    // The order whose place the player is picking ("move-to", "patrol",
    // "location" for a window's request), as lessons name it; empty when none.
    std::string_view pickingId() const;
    // The selected vehicle, if any (lessons read its design type).
    std::optional<game::VehicleId> selectedVehicleId() const { return vehicle_; }

    // Input scripts (docs/BUILDING.md "Input scripts"): the sectors of the
    // shown system that a query names, as frame rectangles in row order
    // ("3,4", or words joined by + and negated by !: empty, home, colony,
    // planet, colonizable, star, warp-point, ship, enemy, selected, gliding,
    // line, line-start, any),
    // and the sector at a frame point; the galaxy panel's systems a query
    // names ("12", home, shown, explored, any, joined and negated the same
    // way), as frame points, in the order of their ids.
    std::vector<Rect> findSectors(const UiContext& ui, std::string_view query, std::string& error) const;
    std::optional<game::Sector> sectorAtFrame(Vec2 p) const;
    std::vector<Vec2> findSystems(const UiContext& ui, std::string_view query, std::string& error) const;
    std::optional<game::SystemId> systemAtFrame(const UiContext& ui, Vec2 p) const;
    // Whether a Dear ImGui window is one of the main window's own (its panels and buttons).
    static bool ownsWindow(ImGuiID window);
    float galaxyCellSize() const;

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
        std::string tooltip;            // under the pointer (a mod order's description)
        Sprite icon;                    // before the label (a mod order's picture)
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
    // Shows another system and changes nothing else: no sector of it is
    // current, and the report or list, its tab, the lit orders and the tags
    // stay (the galaxy panel's left-click, the Galaxy Map's Goto System; spec
    // 06 §2.6, §7 Q103). The system already shown: nothing.
    void showOtherSystem(game::SystemId sys);
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
    // Orders for the tagged group, as one cmd::OrderTagged; `repeat` switches Repeat on (Set Patrol).
    void giveTagged(UiContext& ui, std::vector<game::Order> orders, bool repeat);
    void replaceOrders(UiContext& ui, std::vector<game::Order> orders, bool repeat);
    void orderDone();  // the tagged group dissolves after an order
    void startPick(UiContext& ui, Pick p, std::string prompt);
    void completePick(UiContext& ui, game::Location where, std::optional<game::ObjectId> object);
    // Giving an order its target (spec 06 §2.9): Colonize, Warp, Drop Cargo
    // and a pursuing Attack need one object of the clicked sector. None: no
    // order, silently; one: the target at once; several: the Pick Object
    // window asks.
    struct PickCandidate {
        game::ObjectId object;
        game::VehicleId vehicle;
    };
    struct PickObject {
        Pick pick = Pick::None;
        game::Location where;
        std::vector<PickCandidate> candidates;
    };
    bool pursuingAttack(UiContext& ui) const;
    std::vector<PickCandidate> pickCandidates(UiContext& ui, Pick p, game::Location where) const;
    void givePicked(UiContext& ui, Pick p, game::Location where, const PickCandidate& c);
    void drawPickObject(UiContext& ui);
    void finishPatrol(UiContext& ui);
    void openFor(UiContext& ui, ScreenId id);   // a window about the selected vehicle, fleet or colony
    void chooseCargo(UiContext& ui, Pick p);    // Load / Drop Cargo, Launch / Recover Units Remotely: the type first
    void note(UiContext& ui, std::string text);
    void hotkeys(UiContext& ui);
    void drawChooser(UiContext& ui);

    // Mod orders (main_window_mods.cpp; docs/sdk/interface.md "Mod orders"):
    // the order strip's free place, lit when the selection (or the empire
    // itself) takes orders of the game's rules mods, opens a menu of them;
    // each order's arguments are asked for one after another (a number, text,
    // a choice from a list, a pick on the map), then it is given as a command.
    struct ModOrderOffer {
        sdk::ModOrderChoice choice;
        sdk::ModOrderTarget target;
        std::string label;
    };
    std::vector<ModOrderOffer> modOrderOffers(UiContext& ui) const;
    void openModOrders(UiContext& ui);
    void startModOrder(UiContext& ui, ModOrderOffer offer);
    void nextModStep(UiContext& ui);
    void answerModStep(UiContext& ui, opense4::script::Value answer);
    void pickModArgument(UiContext& ui, game::Location where);
    void drawModPrompt(UiContext& ui);
    void modHotkeys(UiContext& ui);
    struct ModOrderDraft {
        ModOrderOffer offer;
        std::vector<sdk::UiArgStep> steps;
        size_t at = 0;
        opense4::script::Value answers = opense4::script::Value::emptyMap();
    };
    std::optional<ModOrderDraft> modOrder_;
    // A number or text asked for now (the draft's step).
    struct ModPrompt {
        std::string title, question;
        bool number = true;
        int64_t value = 0;
        std::string text;
        bool appearing = true;
    };
    std::optional<ModPrompt> modPrompt_;
    // The report shows the mods' panels in place of its page (the report's MOD button).
    bool modPage_ = false;
    // The MOD button at the report's top right (only while panels apply); true when clicked.
    bool modPageButton(UiContext& ui, Vec2 at);
    // The mods' panels about what the report shows: true when there are any.
    bool modReport(UiContext& ui, bool draw);

    // Drawing.
    void statusBar(UiContext& ui);
    // The minimize button, and the T button that shows the lesson panel again (docs/LEARNING.md).
    void statusButtons(UiContext& ui);
    void commandPanel(UiContext& ui);
    void reportPanel(UiContext& ui);
    void overlayText(UiContext& ui);
    // OpenSE4's AI notes view (computer_players.hpp): the notes of script
    // computer players on the system panel (each noted thing's sector, and a
    // list), the galaxy panel (drawGalaxy) and in the reports.
    void aiNotesOverlay(UiContext& ui);
    void aiNotesInReport(UiContext& ui, std::string_view kind, int64_t id, const std::vector<game::VehicleId>* members = nullptr);
    std::vector<ShownNote> notes_;   // this frame's, while the view is on
    // The notice of computer players that failed: the newest failure, with
    // Details (Computer Player Errors) and Dismiss; `failuresSeen_` were dismissed.
    void playerFailureNotice(UiContext& ui);
    size_t failuresSeen_ = 0;
    void mouse(UiContext& ui);
    void drawSystem(gfx::Renderer2D& r, UiContext& ui);
    void drawFrame(gfx::Renderer2D& r, UiContext& ui);
    void drawGalaxy(gfx::Renderer2D& r, UiContext& ui);
    std::optional<game::SystemId> galaxySystemAt(const UiContext& ui, Vec2 p, bool exploredOnly) const;

    // Ship movement animation (ship_glides.hpp) and the movement log replay
    // (movement_replay.hpp), updated once per frame; the glides also when the
    // game changed since (trackGlides). A new turn is noted first.
    void trackMovement(UiContext& ui);
    void noteNewTurn(UiContext& ui);
    void trackGlides(UiContext& ui);
    void trackReplay(UiContext& ui);
    double glidesAt_ = -1.0;
    uint64_t glidesRevision_ = 0;
    // The object whose movement line the panel shows is gliding (a fleet: one of its members).
    bool lineSubjectGliding(const UiContext& ui) const;
    // Turn-based games: the view follows the player's own objects whose
    // orders ran during its turn (spec 06 §2.7), from the session's steps.
    void followOwnMoves(UiContext& ui);
    uint64_t followedCall_ = 0;
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
    // The game the list was made from: its vehicle pointers hold only until
    // the next state change (ClassicSession::revision()), which can come
    // between update() and render() (see render()).
    uint64_t sectorsRevision_ = 0;
    const game::Vehicle* sectorsVehicles_ = nullptr;

    game::SystemId shown_;
    uint64_t selections_ = 0;   // selections the player made (selectSector, selectVehicle, selectPlanet, list rows)
    std::optional<game::Sector> sector_;
    std::optional<game::ObjectId> object_;
    std::optional<game::VehicleId> vehicle_;
    std::optional<game::FleetId> fleet_;
    bool listMode_ = false;
    // Where the list in the report panel was built: it stays that sector's
    // list while another system is shown (spec 06 §7 Q103).
    std::optional<game::Location> listAt_;
    // The Log's Goto to a system without a sector empties the report panel
    // until the next selection (spec 06 §2.5).
    bool emptyReport_ = false;
    // The report was opened from the sector's list (a click on one of its
    // entries): only then does it show the up-arrow back to the list (spec 06
    // §7 Q98). Every other selection clears it.
    bool reportFromList_ = false;
    std::vector<game::VehicleId> tagged_;
    ReportTab tab_ = ReportTab::Detail;
    ItemReportPopup itemReport_;   // a facility's or component's report, from a right-click on Facil or Comps
    shipui::ReportPopup shipReport_;   // a fleet member's Ship Report, from a right-click on its row in the Fleet Report

    Pick pick_ = Pick::None;
    std::string pickPrompt_;
    std::vector<game::Location> patrol_;
    std::function<void(game::Location)> pickCallback_;
    game::DesignId pickDesign_;
    std::optional<game::Sector> hover_;
    std::optional<game::SystemId> galaxyHover_;
    std::optional<Chooser> chooser_;
    std::optional<PickObject> pickObject_;   // the Pick Object window, while it asks
    // The hover hint over the system panel (§2.3): the button's name and key.
    std::string hintName_, hintKey_;
    std::string note_;
    double noteUntil_ = 0.0;
    // A window, question or the picker is open: the panels take no input (update()).
    bool inputBlocked_ = false;
    ImGuiWindowFlags blockedFlags() const { return inputBlocked_ ? int(ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoNavInputs) : 0; }
    int orderPage_ = 0;   // the order strip's page at 800x600 (§2.3)

    // The movement line's route, worked out again when the game or the report
    // changes. While the object's move is shown, the route as it stood before
    // the move (`beforeMoves`, ClassicSession::movesBefore).
    struct MovementLineCache {
        LineSubject subject;
        uint64_t revision = 0;
        uint32_t turn = 0;
        bool beforeMoves = false;
        game::movement::PlannedRoute route;
    };
    std::optional<MovementLineCache> line_;
    // The sectors of the shown system the movement line was drawn through
    // this frame, and its start (input scripts' "line" and "line-start").
    std::vector<game::Sector> lineDrawn_;
    std::optional<game::Sector> lineStartDrawn_;

    ShipGlides glides_;
    MovementReplay replay_;
    FinaleWatch finale_;   // the ending windows, each as it comes at a turn's start
    std::set<game::VehicleId> replaySeen_;          // the vehicles of the log the player sees
    std::optional<game::SystemId> replayShownBefore_;
    std::optional<game::movement::PlannedRoute> replayLine_;   // the movement line stored when the replay began
    std::map<game::VehicleId, game::Location> beforeTurn_;  // where we saw everything before this turn
    std::map<game::VehicleId, int> beforeTurnHeadings_;     // and the headings then
    uint32_t beforeTurnFor_ = UINT32_MAX;
    uint32_t seenTurn_ = UINT32_MAX;
    double trackedAt_ = -1.0;
};

} // namespace opense4::client::classic
