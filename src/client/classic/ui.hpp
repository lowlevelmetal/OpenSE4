#pragma once

// Shared UI plumbing for the classic client: the 1024×768 virtual frame,
// the per-frame UI context handed to every window, the Screen interface and
// helpers that give every dialog the classic layout (title strip, content on
// the left, a column of buttons on the right with Close at the bottom —
// docs/spec/06 §1).

#include "client/classic/art.hpp"
#include "client/classic/layout.hpp"
#include "client/classic/screen_id.hpp"
#include "client/classic/session.hpp"
#include "client/fonts.hpp"
#include "client/mode.hpp"
#include "game/state.hpp"
#include "learn/lesson.hpp"

#include <imgui.h>

struct ImGuiWindow;

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic {

// The 1024x768 frame; the frame in use (it is 800x600 in the small layout,
// layout.hpp) is frameW() x frameH().
inline constexpr float kFrameW = 1024.0f;
inline constexpr float kFrameH = 768.0f;

// Font sizes in frame pixels that draw the classic bitmap fonts at their
// native pixels (client/ui/bitmap_font.hpp): Futurist Medium for text,
// Futurist Small for fine print, SE4 Text Button for titles and buttons.
inline constexpr float kTextSize = 13.0f;
inline constexpr float kSmallSize = 10.0f;
inline constexpr float kTitleSize = 17.0f;
// OpenSE4's own small raster face for the map numbers (Fonts::tiny), 8 px.
inline constexpr float kTinySize = 8.0f;
// Text drawn at y has its glyph cell's top this far above y (each face's
// internal leading), so a place the spec gives as the cell's top is drawn at
// that y plus this: Futurist Medium 3, Futurist small 2, SE4 Text button 0.
inline constexpr float kTextLead = 3.0f;
inline constexpr float kSmallLead = 2.0f;
inline constexpr float kTitleLead = 0.0f;
// The cell heights of the faces (for text placed by its bottom edge).
inline constexpr float kTextCell = 16.0f;
inline constexpr float kSmallCell = 12.0f;
inline constexpr float kTinyCell = 8.0f;

// The classic palette, measured on the original's screens (docs/spec/07 §UI).
namespace palette {
inline constexpr uint32_t kFrame = 0x4f65a2;       // panel and box lines
inline constexpr uint32_t kFrameLight = 0x647ec7;  // the brighter rail of double lines
inline constexpr uint32_t kButton = 0x617bc2;      // button outlines and captions
inline constexpr uint32_t kButtonHot = 0x6c8adc;   // the same under the pointer (spec 06 §5.4)
inline constexpr uint32_t kButtonHeld = 0x7d9fff;  // and while the mouse button is held on it
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

// Optional context for opening a screen.
struct ScreenArgs {
    game::ObjectId planet;
    game::VehicleId vehicle;
    game::FleetId fleet;
    game::DesignId design;
    game::EmpireId empire;
    std::optional<game::Location> location;
    int index = -1;
    int sub = -1;          // a second index (a ground combat within a battle)
    std::string text;
};

// Requests from windows to the main window / mode.
struct UiRequests {
    // Show a system: as a galaxy-panel click, keeping the report (the Galaxy
    // Map's Goto System), or with the report panel emptied (the Log's Goto to
    // a system without a sector) (spec 06 §2.5, §2.6).
    std::optional<game::SystemId> showSystem;
    bool showSystemEmptiesReport = false;
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
    // The learning system: replace the game with a lesson's (Learn window),
    // leave the running lesson (its panel), show or hide the lesson panel.
    std::optional<std::pair<learn::LessonKind, std::string>> startLesson;
    std::optional<std::pair<learn::LessonKind, std::string>> resumeLesson;   // at the place the player left it
    bool leaveLesson = false;
    bool toggleLessonPanel = false;
};

// A rectangle a lesson can outline (docs/LEARNING.md "UI tags"), in ImGui
// screen units, registered while it is drawn. `window` is the ImGui window it
// was drawn in (null outside every window): its outline goes in that window's
// layer, so whatever lies above the window covers the outline too.
struct UiTag {
    std::string name;
    ImVec2 min, max;
    ImGuiWindow* window = nullptr;
    // A stand-in: the tag of an order on another page of the order strip
    // (800x600), registered on the page arrow that leads there. A lesson
    // outlines the arrow and says to press it (docs/LEARNING.md "Getting back").
    bool pager = false;
};

struct LearnContent;
class UiContext;

// What the classic widgets need to draw: the pictures, the fonts and the frame
// scale. The in-game UiContext and the front end's MenuContext both provide one.
struct Painter {
    Art& art;
    const Fonts& fonts;
    FrameMapping map;
    float fbScale = 1.0f;
    float textScale = 1.0f;
    // In a game: where widgets register their UI tags (the lists' arrow column); null elsewhere.
    UiContext* tagger = nullptr;
    // Flags every classic window drawn with it adds (UiContext::windowFlags: a
    // window behind the one in front takes no input).
    ImGuiWindowFlags windowFlags = 0;

    float k() const { return map.scale / fbScale; }
    float px(float framePixels) const { return framePixels * k(); }
    // Font sizes (see "Text and the Text size setting" below): a classic font
    // at its own raster size, and text the Text size setting enlarges.
    float fontPx(float framePixels) const { return framePixels * k(); }
    float textPx(float framePixels) const { return framePixels * k() * textScale; }
    ImVec2 at(Vec2 framePos) const {
        const Vec2 p = map.toFb(framePos) / fbScale;
        return {p.x, p.y};
    }
    ImVec2 size(Vec2 frameSize) const { return {frameSize.x * k(), frameSize.y * k()}; }
};

class UiContext;

// Every classic window is modal (docs/spec/06 §1, §3.4, confirmed: binary):
// while one is open the main window takes no input, and only the window in
// front (the one opened last) does; the windows behind it wait until it closes.
class Screen {
public:
    virtual ~Screen() = default;
    // Draws the window; returns false once it should close.
    virtual bool draw(UiContext& ui) = 0;
    // Every window is modal; the battle windows still say so themselves.
    virtual bool modal() const { return true; }
    // Windows that belong to the window under them (a report about something in
    // it) close with it: when that window closes, they close too.
    virtual bool closesWithParent() const { return false; }
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
    // The empire's Empire Options and window memories (spec 06 §1.9), and a
    // change to them (cmd::SetInterfaceOptions; nothing is issued when they
    // are unchanged). False when the change was refused (not our turn).
    const game::InterfaceOptions& options() const { return me().interfaceOptions; }
    bool setOptions(const game::InterfaceOptions& o);

    void open(ScreenId id, ScreenArgs args = {}) {
        if (opener) opener(id, std::move(args));
    }

    // Frame pixels -> ImGui units.
    float k() const { return map.scale / fbScale; }
    // A font size in frame pixels: a classic font at its own raster size (the
    // classic layouts' fixed places), and text that the Text size setting
    // enlarges (reading text and OpenSE4's own; see "Text and the Text size
    // setting" below).
    float fontPx(float framePixels) const { return framePixels * k(); }
    float textPx(float framePixels) const { return framePixels * k() * textScale; }
    float textScale = 1.0f;
    ImVec2 at(Vec2 framePos) const {
        const Vec2 p = map.toFb(framePos) / fbScale;
        return {p.x, p.y};
    }
    ImVec2 size(Vec2 frameSize) const { return {frameSize.x * k(), frameSize.y * k()}; }
    float px(float framePixels) const { return framePixels * k(); }
    Painter painter() const { return {art, fonts, map, fbScale, textScale, nullptr, windowFlags()}; }

    // The window being drawn lies behind the one in front (every window is
    // modal): it takes no mouse or keyboard input. Set by the mode around
    // each Screen::draw; every root window a screen begins adds windowFlags().
    bool behind = false;
    ImGuiWindowFlags windowFlags() const {
        return behind ? int(ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoFocusOnAppearing)
                      : 0;
    }

    // UI tags of this frame (cleared at its start): `window:<id>` for each
    // window (Dialog registers it), the main window's buttons and panels and
    // a few widgets inside windows (learn/ids.hpp lists them all).
    std::vector<UiTag> tags;
    void tag(std::string_view name, ImVec2 min, ImVec2 max);
    // The parts of the arrow column of the list drawn last (list_widgets.hpp):
    // a tag given to that list (`planets:list`) also tags its parts
    // (`planets:list:up`, `:down`, `:track`, `:thumb`).
    struct ListParts {
        ImVec2 min, max;
        std::array<std::pair<ImVec2, ImVec2>, 4> parts;  // up, down, track, thumb
        bool valid = false;
    };
    ListParts lastList;
    void tagListParts(std::string_view base, const ListParts& list);
    // The last ImGui item (a button, a child window).
    void tagItem(std::string_view name) { tag(name, ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }
    // The last ImGui item, an option of a chooser (learn/ids.hpp choiceGroups:
    // a row of a list, an entry of a picker), as `<chooser>:<option>`: only the
    // part its list shows (nothing for a row scrolled out of view).
    void tagOption(std::string_view chooser, std::string_view option);
    // The last ImGui item, a page arrow of the order strip, as the stand-in
    // for the order `name` on the page it leads to (UiTag::pager).
    void tagPager(std::string_view name);
    void tagFrame(std::string_view name, const Rect& frameRect) { tag(name, at(frameRect.min), at(frameRect.max)); }
    // The window being drawn (set by the mode around each Screen::draw): its
    // first Dialog registers `window:<id>`.
    std::optional<ScreenId> drawing;
    bool windowTagged = false;
    ImGuiID drawingWindow = 0;   // and the Dear ImGui window of that Dialog
    void tagWindow(ImVec2 min, ImVec2 max);
    // A tab or filter button of the window being drawn, just drawn: tags it
    // `<window>:<tab>` and, when it is the one shown, reports it to lessons
    // (facts.tabs). learn/ids.hpp windowTabs lists them.
    void tagTab(std::string_view tab, bool shown);

    // The game's own prompts this frame (End Turn's question, Colony Type,
    // Attack Sector, error boxes): a tutorial's input lock never covers them.
    std::vector<std::pair<ImVec2, ImVec2>> promptAreas;
    // The ImGui window being drawn is such a prompt.
    void promptWindow();

    // The learning content and whether a lesson is running (its T button),
    // and whether a tutorial step locks the input (window links then stay shut).
    const LearnContent* learn = nullptr;
    bool lessonRunning = false;
    bool lessonLocked = false;
    // The rows the active tutorial step names: its {design:<type>} tokens
    // filled in (docs/LEARNING.md "Text tokens"). A list that shows one
    // scrolls it into view the first time (lessonRow, screens/list_widgets.hpp);
    // `lessonRowsFor` tells the steps apart (0: no step).
    std::vector<std::string> lessonRows;
    uint64_t lessonRowsFor = 0;
    // What windows tell lessons this frame (cleared at its start): their
    // tabs, the designer's and the simulator's work in progress. The mode
    // adds the open windows and the selection.
    learn::ClientFacts facts;
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
void heading(const Painter& p, const char* text);
std::string formatNumber(int64_t v);             // 12345 (the classic screens use no digit grouping)
std::string formatDate(uint32_t turn);           // 2400.3
// An empire's colour: its race's swatch (Art::swatchColor), as 0xRRGGBB or for ImGui.
uint32_t empireRgb(const game::GameState& s, game::EmpireId e);
ImU32 empireColor(const game::GameState& s, game::EmpireId e);

// ---- Text and the Text size setting -----------------------------------------------------------
// Settings → Graphics → Text size (0.75 to 1.5) enlarges the text that has room
// to grow: OpenSE4's own (the lesson panel, the manual, the Learn window,
// Settings, the questions and pickers our client draws) and the classic
// windows' reading text, which wraps and scrolls (descriptions, the report
// pages that scroll, the Log's entries, Help's details, messages): textPx.
// Text at a fixed place of a classic layout (button and tab captions, window
// titles, the status bar, label and value lines, list rows and headings, the
// system panel's names) keeps the original's raster size (spec 06 §5.4), so
// the layouts stay as they are: fontPx. Where even that does not fit its box
// (a long name), the text is cut short with "…" and the whole text shows as a
// tooltip under the pointer (fitText, drawFitted, fittedText); text of ours in
// a fixed box first shrinks back towards the classic size (`scaled`).

struct TextFit {
    float size = 0.0f;   // the font size to draw at (ImGui units)
    std::string text;    // the text to draw: cut short with "…" when `cut`
    bool cut = false;
    ImVec2 extent;       // its size (ImGui units)
};
// `framePx`: the font's classic size in frame pixels (kTextSize...); `maxWidth`
// and `maxHeight` (0: any) in ImGui units. `scaled`: the text starts at the
// Text size setting's size (textPx) and shrinks to the classic size to fit.
TextFit fitText(const Painter& p, ImFont* font, float framePx, std::string_view text, float maxWidth, float maxHeight = 0.0f, bool scaled = false);
// Draws the text fitted to `maxWidth` from `pos` (its top left, ImGui units),
// placed by `align` across the box (0 left, 0.5 centred, 1 right), and shows the
// whole text as a tooltip while the pointer is over a cut one. A null `dl` draws
// into the current window.
TextFit drawFitted(const Painter& p, ImDrawList* dl, ImFont* font, float framePx, ImVec2 pos, float maxWidth, ImU32 color, std::string_view text,
                   float align = 0.0f, float maxHeight = 0.0f, bool scaled = false);
// The same as an item in the current window's layout (in the window's font,
// at its size, and colour), at most `maxWidth` wide (ImGui units; 0: to the
// right edge of the window or table cell).
void fittedText(std::string_view text, float maxWidth = 0.0f, ImU32 color = 0);
// The text cut short with "…" to `maxWidth` (ImGui units) in the current font
// and size, for a widget's label; `cut` tells whether it was. Show the whole
// text as a tooltip over the widget when it was (cutTooltip).
std::string elided(std::string_view text, float maxWidth, bool* cut = nullptr);
void cutTooltip(bool cut, std::string_view text);
// The reading text of a classic window (see above) in the window's font, at the
// Text size setting's size, while it lives.
class ReadingText {
public:
    explicit ReadingText(const Painter& p, ImFont* font = nullptr, float framePx = kTextSize);
    ~ReadingText() { ImGui::PopFont(); }
    ReadingText(const ReadingText&) = delete;
    ReadingText& operator=(const ReadingText&) = delete;
};
// A bullet and its text wrapped to the window's width (ImGui::BulletText does
// not wrap: a long line ran out of its box).
void wrappedBullet(std::string_view text, const ImVec4* color = nullptr);

// ---- Keys in dialogs (spec 06 §3.4, confirmed: binary) ---------------------------------------
// Call inside the prompt's window. Keys pressed on the frame the window
// appears are ignored: they belong to whatever opened it.

// A Yes/No message box: Y means Yes; N, Esc and Enter mean No. True for Yes,
// false for No, nothing without a key.
std::optional<bool> yesNoKey();
// A message box with OK (or a notice with Begin): Esc or Enter.
bool okKey();
// A prompt that offers Tactical and Strategic: T or S. True for Tactical.
std::optional<bool> tacticalStrategicKey();
// Window flags for prompts: no keyboard navigation, so Enter never presses
// the focused button (the keys above decide).
inline constexpr ImGuiWindowFlags kPromptFlags = ImGuiWindowFlags_NoNavInputs;

class UiContext;
// A Yes/No message box with those keys, centred on the frame. open() asks;
// call draw() every frame: it returns true once, when Yes is chosen.
class YesNoPrompt {
public:
    void open(std::string question, std::string title = "Confirm");
    bool draw(UiContext& ui);
    // The same, telling No apart: true for Yes, false for No, nothing until answered.
    std::optional<bool> answer(UiContext& ui);

private:
    std::string question_, title_;
    bool pending_ = false;
};

// ---- Classic dialog layout -----------------------------------------------------------------

enum class DialogSize { Large, Tall, Report, Picker, Prompt, Full };

// Usage:
//   Dialog d(ui, "Designs", DialogSize::Large);
//   if (d.open()) { d.beginContent(); ...; d.beginButtons(); if (d.button("Create")) ...; if (d.close()) return false; }
//   return d.keepOpen();
class Dialog {
public:
    // In a game: also registers the window's UI tag. `contentFrame` false
    // leaves out the box around the content area (the Galaxy Map's).
    Dialog(UiContext& ui, const char* title, DialogSize size, float buttonColumn = 190.0f, bool contentFrame = true);
    // A window of its own size (frame pixels), centred.
    Dialog(UiContext& ui, const char* title, Vec2 size, float buttonColumn = 190.0f);
    // Anywhere (the front end's Learn and Manual windows).
    Dialog(const Painter& painter, const char* title, DialogSize size, float buttonColumn = 190.0f);
    ~Dialog();
    Dialog(const Dialog&) = delete;
    Dialog& operator=(const Dialog&) = delete;

    bool open() const { return visible_; }
    // Screen position (ImGui units) of a point given in frame pixels from the window's top left.
    ImVec2 at(Vec2 windowPos) const { return ui_.at(rect_.min + windowPos); }
    // Extra text or a picture in the title strip (e.g. Research's points), at x frame pixels from the window's left.
    void titleText(float x, ImU32 color, std::string_view text);
    void titleIcon(float x, const Sprite& icon);
    // The content area: from (15,35) to the left of the button column (x 571),
    // or to `right` (window coordinates) for windows whose original layout
    // reaches further (lists to x 575).
    void beginContent(float right = 0.0f);
    void beginButtons();
    // Leaves `n` slots of the button column to the window, which draws in them
    // (its own content, as a legend); returns their top left (ImGui screen units).
    ImVec2 skipSlots(int n);
    // Right-column buttons (180 × 28, one slot per 31 px). A plain action button;
    bool button(const char* label, bool enabled = true);
    // a page or filter tab (chamfered corner, green lamp when selected);
    bool tab(const char* label, bool selected, bool enabled = true);
    // an on/off setting (a check box that shows a lamp when on).
    bool check(const char* label, bool on, bool enabled = true);
    void spacer();
    // The bottom Close button; also true on Esc or Enter (spec 06 §3.4), unless
    // a text field takes the keys.
    bool close();
    // The bottom button with another label (Cancel: Esc only) or dim (no keys).
    bool close(bool enabled, const char* label = "Close");
    bool keepOpen() const { return keep_; }
    void requestClose() { keep_ = false; }

private:
    Dialog(const Painter& painter, const char* title, const Rect& rect, float buttonColumn, bool contentFrame = true);
    void endChild();
    bool slot(const char* label, int style, bool on, bool enabled);
    Painter ui_;
    UiContext* game_ = nullptr;   // in a game: the bottom button registers `<window>:close`
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

// The Tactical Combat and Combat Replay title strip (spec 06 §2.1.1, §5.4):
// "Location", "Turn" and "Empires" (label blue) with their values (white) at
// the layout's places, the empires' large flags 28 px apart from the
// layout's x (6 of them on a frame narrower than 1024, else 10), the flag of
// the empire whose phase it is (`phase`, or -1) framed in yellow. An empty
// style leaves its place empty (a simulation's side draws its box there).
void combatTitleStrip(UiContext& ui, const Dialog& d, std::string_view location, std::string_view turn, const std::vector<std::string>& flagStyles,
                      int phase);

// A classic text button drawn at the cursor: 1 px outline and caption in the
// button blue. `style` 0 plain, 1 tab (chamfer, lamp when on), 2 check box.
bool classicButton(const Painter& p, const char* label, Vec2 frameSize, int style = 0, bool on = false, bool enabled = true);
inline bool classicButton(UiContext& ui, const char* label, Vec2 frameSize, int style = 0, bool on = false, bool enabled = true) {
    return classicButton(ui.painter(), label, frameSize, style, on, enabled);
}
// An empty button slot (the classic dark placeholder box).
void emptySlot(const Painter& p, Vec2 frameSize);
// The classic frame around a window: pipes at the sides, rails, a title strip
// (none when `title` is null) and, with a button column, a second box for it.
void drawWindowFrame(const Painter& p, ImDrawList* dl, const Rect& frameRect, const char* title, float buttonColumn, bool contentFrame = true);

// Applies the classic look (black, 1 px blue lines) to ImGui; call once.
void applyClassicStyle();

// Screen factory (screens/registry.cpp).
std::unique_ptr<Screen> makeScreen(ScreenId id, const ScreenArgs& args);
const char* screenTitle(ScreenId id);
// "designs", "Construction Queues", "EmpireStatus"... (case and spaces ignored).
std::optional<ScreenId> screenFromName(std::string_view name);


} // namespace opense4::client::classic
