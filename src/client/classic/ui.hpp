#pragma once

// Shared UI plumbing for the classic client: the 1024×768 virtual frame,
// the per-frame UI context handed to every window, the Screen interface and
// helpers that give every dialog the classic layout (title strip, content on
// the left, a column of buttons on the right with Close at the bottom —
// docs/spec/06 §1).

#include "client/classic/art.hpp"
#include "client/classic/session.hpp"
#include "client/mode.hpp"
#include "client/view_context.hpp"
#include "game/state.hpp"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace opense4::client::classic {

inline constexpr float kFrameW = 1024.0f;
inline constexpr float kFrameH = 768.0f;

// Font sizes in frame pixels that draw the classic bitmap fonts at their
// native pixels (client/ui/bitmap_font.hpp): Futurist Medium for text,
// Futurist Small for fine print, SE4 Text Button for titles and buttons.
inline constexpr float kTextSize = 13.0f;
inline constexpr float kSmallSize = 10.0f;
inline constexpr float kTitleSize = 17.0f;

// The classic palette, measured on the original's screens (docs/spec/07 §UI).
namespace palette {
inline constexpr uint32_t kFrame = 0x4f65a2;       // panel and box lines
inline constexpr uint32_t kFrameLight = 0x647ec7;  // the brighter rail of double lines
inline constexpr uint32_t kButton = 0x617bc2;      // button outlines and captions
inline constexpr uint32_t kButtonHot = 0xa8bcff;   // hovered caption (ours)
inline constexpr uint32_t kLabel = 0x7d9fff;       // field labels
inline constexpr uint32_t kHeading = 0xc0c0c0;     // section headings
inline constexpr uint32_t kSecondary = 0xa0a0a0;   // second lines, notes
inline constexpr uint32_t kDim = 0x606060;         // unavailable rows
inline constexpr uint32_t kDisabled = 0x2d2d2d;    // disabled buttons, empty button slots
inline constexpr uint32_t kGrid = 0x1c2a4c;        // map grid lines
inline constexpr uint32_t kMinerals = 0x4665cc;
inline constexpr uint32_t kOrganics = 0x008000;
inline constexpr uint32_t kRadioactives = 0xff0000;
} // namespace palette

constexpr ImU32 imColor(uint32_t rgb, float alpha = 1.0f) {
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, int(alpha * 255.0f + 0.5f));
}
constexpr ImVec4 imColorV(uint32_t rgb, float alpha = 1.0f) {
    return ImVec4(float((rgb >> 16) & 0xff) / 255.0f, float((rgb >> 8) & 0xff) / 255.0f, float(rgb & 0xff) / 255.0f, alpha);
}
inline constexpr ImVec4 kLabelBlue = imColorV(palette::kLabel);
inline constexpr ImVec4 kDimText = imColorV(palette::kSecondary);

// The classic fonts from the install's Fonts folder, falling back to `app`
// for any that are missing. Call outside an ImGui frame.
Fonts loadClassicFonts(const Fonts& app, const assets::InstallFiles& files);

// Frame <-> framebuffer pixels, recomputed each frame. The classic screens
// are laid out in a 1024×768 frame; on a wider window the frame extends
// equally to the left and right (x from `left` to `right`), so centred
// windows stay centred and the main window can use the extra width.
struct FrameMapping {
    float scale = 1.0f;
    Vec2 offset;
    float left = 0.0f;
    float right = kFrameW;
    Vec2 toFb(Vec2 p) const { return offset + p * scale; }
    Vec2 fromFb(Vec2 p) const { return (p - offset) / scale; }
    float width() const { return right - left; }
};

// Scale and extent for a framebuffer, per the Graphics settings.
FrameMapping frameMappingFor(float framebufferWidth, float framebufferHeight);

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
    // Graphics, controls and sound.
    Settings,
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
    // Replace the running game with this save (Load Game window).
    std::optional<std::filesystem::path> loadGame;
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
    AppControl* app = nullptr;

    UiContext(ClassicSession& s, Art& a, const Fonts& f) : session(s), art(a), fonts(f) {}

    const game::Rules& rules() const { return session.rules(); }
    const game::GameState& state() const { return session.state(); }
    const game::Empire& me() const { return session.me(); }

    void open(ScreenId id, ScreenArgs args = {}) {
        if (opener) opener(id, std::move(args));
    }

    // Frame pixels -> ImGui units.
    float k() const { return map.scale / fbScale; }
    // A font size in frame pixels, with the Text size setting applied.
    float fontPx(float framePixels) const { return framePixels * k() * textScale; }
    float textScale = 1.0f;
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
std::string formatNumber(int64_t v);             // 12345 (the classic screens use no digit grouping)
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
    // Right-column buttons (180 × 28, one slot per 31 px). A plain action button;
    bool button(const char* label, bool enabled = true);
    // a page or filter tab (chamfered corner, green lamp when selected);
    bool tab(const char* label, bool selected, bool enabled = true);
    // an on/off setting (a check box that shows a lamp when on).
    bool check(const char* label, bool on, bool enabled = true);
    void spacer();
    // The bottom Close button; also true on Escape.
    bool close();
    bool keepOpen() const { return keep_; }
    void requestClose() { keep_ = false; }

private:
    void endChild();
    bool slot(const char* label, int style, bool on, bool enabled);
    UiContext& ui_;
    Rect rect_;
    bool visible_ = false;
    bool keep_ = true;
    bool inChild_ = false;
    bool buttonsStarted_ = false;
    float buttonColumn_;
    int nextSlot_ = 0;
    float pitch_ = 31.0f;    // slot pitch; tighter when a window has more buttons than slots
    float buttonH_ = 28.0f;
};

// A classic text button drawn at the cursor: 1 px outline and caption in the
// button blue. `style` 0 plain, 1 tab (chamfer, lamp when on), 2 check box.
bool classicButton(UiContext& ui, const char* label, Vec2 frameSize, int style = 0, bool on = false, bool enabled = true);
// An empty button slot (the classic dark placeholder box).
void emptySlot(UiContext& ui, Vec2 frameSize);
// The classic frame around a window: pipes at the sides, double rails, a title strip.
void drawWindowFrame(UiContext& ui, ImDrawList* dl, const Rect& frameRect, const char* title, float buttonColumn);

// Applies the classic look (black, 1 px blue lines) to ImGui; call once.
void applyClassicStyle();

// Screen factory (screens/registry.cpp).
std::unique_ptr<Screen> makeScreen(ScreenId id, const ScreenArgs& args);
const char* screenTitle(ScreenId id);
// "designs", "Construction Queues", "EmpireStatus"... (case and spaces ignored).
std::optional<ScreenId> screenFromName(std::string_view name);

} // namespace opense4::client::classic
