#pragma once

// Shared UI plumbing for the classic client: the 1024×768 virtual frame,
// the per-frame UI context handed to every window, the Screen interface and
// helpers that give every dialog the classic layout (title strip, content on
// the left, a column of buttons on the right with Close at the bottom —
// docs/spec/06 §1).

#include "client/classic/art.hpp"
#include "client/classic/session.hpp"
#include "client/view_context.hpp"
#include "game/state.hpp"

#include <imgui.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace opense4::client::classic {

inline constexpr float kFrameW = 1024.0f;
inline constexpr float kFrameH = 768.0f;

// Frame (1024×768) <-> framebuffer pixels, recomputed each frame.
struct FrameMapping {
    float scale = 1.0f;
    Vec2 offset;
    Vec2 toFb(Vec2 p) const { return offset + p * scale; }
    Vec2 fromFb(Vec2 p) const { return (p - offset) / scale; }
};

// Every window of the classic client. The main window is not a Screen.
enum class ScreenId {
    // Command buttons (docs/spec/06 §1.2).
    GameMenu, Designs, CreateDesign, Planets, Colonies, Ships, Queues, SetQueue, Research, TechTree, Empires, Log,
    EmpireStatus, Help, GalaxyMap,
    // Empire Status sub-windows.
    EmpireOptions, Ministers, SystemsToAvoid, Waypoints, Strategies, RepairPriorities,
    // Order dialogs (§1.3).
    FleetTransfer, CargoTransfer, LaunchRecover, Scrap, ViewOrders, SelectWaypoint, StellarManipulation, Rename,
    // Diplomacy and comparisons (§1.5).
    Communicate, Intelligence, TreatyGrid, Scores, Comparisons, History, RaceReport, VictoryConditions,
    // Combat (§1.6).
    CombatReplay,
    // Files.
    SaveGame, LoadGame,
    Count
};

// Optional context for opening a screen.
struct ScreenArgs {
    game::ObjectId planet;
    game::VehicleId vehicle;
    game::FleetId fleet;
    game::DesignId design;
    game::EmpireId empire;
    std::optional<game::Location> location;
    int index = -1;
    std::string text;
};

// Requests from windows to the main window / mode.
struct UiRequests {
    std::optional<game::SystemId> showSystem;
    std::optional<game::Location> focus;          // show the system and select the sector
    std::optional<game::VehicleId> selectVehicle;
    std::optional<game::ObjectId> selectPlanet;
    bool endTurn = false;
    bool quitToIntro = false;
    bool quitGame = false;
    // Pick a location in the main window, then call back (e.g. Set Waypoint).
    std::function<void(game::Location)> pickLocation;
    std::string pickPrompt;
};

class UiContext;

class Screen {
public:
    virtual ~Screen() = default;
    // Draws the window; returns false once it should close.
    virtual bool draw(UiContext& ui) = 0;
    // Screens that block the main window (modal) dim it and take all input.
    virtual bool modal() const { return false; }
};

class UiContext {
public:
    ClassicSession& session;
    Art& art;
    const Fonts& fonts;
    FrameMapping map;
    float fbScale = 1.0f;
    double time = 0.0;
    float dt = 0.0f;
    UiRequests requests;
    std::function<void(ScreenId, ScreenArgs)> opener;

    UiContext(ClassicSession& s, Art& a, const Fonts& f) : session(s), art(a), fonts(f) {}

    const game::Rules& rules() const { return session.rules(); }
    const game::GameState& state() const { return session.state(); }
    const game::Empire& me() const { return session.me(); }

    void open(ScreenId id, ScreenArgs args = {}) {
        if (opener) opener(id, std::move(args));
    }

    // Frame pixels -> ImGui units.
    float k() const { return map.scale / fbScale; }
    ImVec2 at(Vec2 framePos) const {
        const Vec2 p = map.toFb(framePos) / fbScale;
        return {p.x, p.y};
    }
    ImVec2 size(Vec2 frameSize) const { return {frameSize.x * k(), frameSize.y * k()}; }
    float px(float framePixels) const { return framePixels * k(); }
};

// ---- Drawing helpers (ImGui, sizes in frame pixels) --------------------------------------

void image(UiContext& ui, const Sprite& s, Vec2 frameSize, Color tint = Color{1, 1, 1, 1});
// An image button; returns true when clicked.
bool imageButton(UiContext& ui, const char* id, const Sprite& s, Vec2 frameSize, bool enabled = true);
// "icon 1234  icon 567  icon 89" with the resource icons.
void resources(UiContext& ui, const game::Resources& r, bool compact = false);
void labelValue(UiContext& ui, const char* label, const std::string& value, float valueColumn = 110.0f);
// Heading text in the classic label blue.
void heading(UiContext& ui, const char* text);
std::string formatNumber(int64_t v);             // 12,345
std::string formatDate(uint32_t turn);           // 2400.3
ImU32 empireColor(const game::GameState& s, game::EmpireId e);

// ---- Classic dialog layout -----------------------------------------------------------------

enum class DialogSize { Large, Tall, Report, Picker, Prompt, Full };

// Usage:
//   Dialog d(ui, "Designs", DialogSize::Large);
//   if (d.open()) { d.beginContent(); ...; d.beginButtons(); if (d.button("Create")) ...; if (d.close()) return false; }
//   return d.keepOpen();
class Dialog {
public:
    Dialog(UiContext& ui, const char* title, DialogSize size, float buttonColumn = 190.0f);
    ~Dialog();
    Dialog(const Dialog&) = delete;
    Dialog& operator=(const Dialog&) = delete;

    bool open() const { return visible_; }
    void beginContent();
    void beginButtons();
    // A right-column button (fixed width). `active` draws it highlighted (toggles/tabs).
    bool button(const char* label, bool enabled = true, bool active = false);
    void spacer();
    // The bottom Close button; also true on Escape.
    bool close();
    bool keepOpen() const { return keep_; }
    void requestClose() { keep_ = false; }

private:
    void endChild();
    UiContext& ui_;
    bool visible_ = false;
    bool keep_ = true;
    bool inChild_ = false;
    bool buttonsStarted_ = false;
    float buttonColumn_;
};

// Applies the classic look (dark navy, blue frames) to ImGui; call once.
void applyClassicStyle();

// Screen factory (screens/registry.cpp).
std::unique_ptr<Screen> makeScreen(ScreenId id, const ScreenArgs& args);
const char* screenTitle(ScreenId id);
// "designs", "Construction Queues", "EmpireStatus"... (case and spaces ignored).
std::optional<ScreenId> screenFromName(std::string_view name);

} // namespace opense4::client::classic
