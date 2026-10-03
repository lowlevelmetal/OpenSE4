#pragma once

// The frame and controls shared by Game Setup, Empire Setup and the Quick
// Start picker (front end, MenuContext based), in the original's layout
// (docs/spec/07 session 5, spec 06 §2.1.1): an 800×600 area centred on the
// screen over a starfield, the screen's name at its top left, the page
// buttons in a column at the left, the content frame at area (216,0)–(799,549)
// and Begin Game (or Create Empire) and Cancel under it. Every place is given
// in area pixels (the area's top left is (0,0)). Inside the frame a group is
// a heading over a box with lamp rows (one choice) or check boxes (on/off).

#include "client/classic/frontend.hpp"

#include <imgui.h>

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic::setup {

// Status and note colours of our own lines.
inline const ImVec4 kGood{0.45f, 0.95f, 0.5f, 1.0f};
inline const ImVec4 kBad{1.0f, 0.45f, 0.4f, 1.0f};
inline const ImVec4 kDim{0.62f, 0.66f, 0.76f, 1.0f};
inline const ImVec4 kHighlight{1.0f, 1.0f, 0.65f, 1.0f};

// The colours seen in the original's setup screens (spec 07 session 5).
inline constexpr uint32_t kHeadingRgb = 0x7d9fff;   // headings and labels
inline constexpr uint32_t kBoxRgb = 0x647ec7;       // box lines
inline constexpr uint32_t kInnerRgb = 0x4f65a2;     // the content frame's inner line
inline constexpr uint32_t kPageRgb = 0x6c8adc;      // the page buttons' labels
inline constexpr uint32_t kExplainRgb = 0xa0a0a0;   // explanations
inline constexpr uint32_t kDimTextRgb = 0x606060;   // dim text
inline constexpr uint32_t kDimBoxRgb = 0x2d2d2d;    // dim boxes
inline constexpr uint32_t kWhite = 0xffffff;

// The pictures under the page buttons (they change from one launch to the next).
enum class Decoration { None, GameSetup, EmpireSetup };

enum class Face { Body, Small, Title };

// The whole-screen window of a setup screen. Draw everything between the
// constructor and the destructor; nothing is drawn when open() is false.
class SetupArea {
public:
    // `window`: the Dear ImGui window's name (input scripts and --open rely on it).
    SetupArea(MenuContext& ctx, const char* window, const char* title, Decoration decoration);
    ~SetupArea();
    SetupArea(const SetupArea&) = delete;
    SetupArea& operator=(const SetupArea&) = delete;

    bool open() const { return visible_; }
    MenuContext& ctx() const { return ctx_; }
    Painter painter() const { return ctx_.painter(); }

    // Area pixels to Dear ImGui screen units.
    ImVec2 at(Vec2 p) const;
    ImVec2 size(Vec2 s) const { return ctx_.size(s); }
    float px(float v) const { return ctx_.px(v); }
    // Puts the cursor at an area point.
    void place(Vec2 p) const;

    // A page button in the left column (slot 0 at the top): a tab with its
    // label in the button font, the current page hatched with a green lamp.
    bool pageButton(int slot, const char* label, bool current);
    // A classic button over the area rectangle [a, b].
    bool button(Vec2 a, Vec2 b, const char* label, bool enabled = true);
    // Begin Game (Create Empire) and Cancel under the content frame; Cancel
    // also answers Esc while no field or popup has the keys.
    bool beginButton(const char* label, bool enabled = true);
    bool cancelButton();
    // Our own line under the frame, left of those buttons: an error, a note or a summary.
    void status(std::string_view text, const ImVec4& color);

    // Text with its glyphs' top at p (the places the spec gives).
    void text(Vec2 p, std::string_view s, uint32_t rgb = kWhite, Face face = Face::Body) const;
    // Right-aligned: its right edge at p.x.
    void textRight(Vec2 p, std::string_view s, uint32_t rgb = kWhite, Face face = Face::Body) const;
    // Wrapped to `width`; returns the height used.
    float textWrapped(Vec2 p, std::string_view s, float width, uint32_t rgb = kExplainRgb, Face face = Face::Body) const;
    void heading(Vec2 p, std::string_view s) const { text(p, s, kHeadingRgb); }
    // A 1 px box line.
    void box(Vec2 a, Vec2 b, uint32_t rgb = kBoxRgb) const;
    // The hatched background of a pressed button or a list's last clicked row.
    void hatch(Vec2 a, Vec2 b) const;
    float textWidth(std::string_view s, Face face = Face::Body) const;

    // A lamp list in the box [a, b]: one 18 px row per label, the lamp at the
    // box's x+9 (green: chosen, blue: not), the label at x+21; a click
    // chooses the row. With `scroll` the rows scroll in the shared list
    // widget with its arrow column. Returns true when the choice changed.
    bool lampList(const char* id, Vec2 a, Vec2 b, int& value, std::span<const std::string> labels, bool enabled = true,
                  bool scroll = false);
    bool lampList(const char* id, Vec2 a, Vec2 b, int& value, std::initializer_list<const char*> labels, bool enabled = true);
    // A check list in the box [a, b]: one 18 px row per entry, a 16×17 box at
    // the box's x+2 holding a green lamp when on, the label at x+21.
    struct Check {
        std::string label;
        bool* value = nullptr;
        bool enabled = true;
    };
    bool checkList(const char* id, Vec2 a, Vec2 b, std::span<Check> rows, bool scroll = false);
    // One check box (16×17) at `boxAt`, its label at labelX (white), wrapped to `wrap` when > 0.
    bool checkBox(const char* id, Vec2 boxAt, std::string_view label, bool& value, bool enabled = true, float labelX = 0, float wrap = 0);
    // A lamp alone, centred at c.
    void lamp(Vec2 c, bool on, bool enabled = true) const;

    // A spin control: ◁ at `left` (20×20), the value box `valueW` wide, ▷;
    // dim while disabled. The value box can be typed into. Returns true when
    // the value changed.
    bool spin(const char* id, Vec2 left, float valueW, int64_t& value, int64_t step, int64_t lo, int64_t hi, std::string_view shown,
              bool enabled = true);
    // An edit box over [a, b] (20 px tall).
    bool edit(const char* id, Vec2 a, Vec2 b, std::string& value, ImGuiInputTextFlags flags = 0, bool enabled = true);
    bool editMultiline(const char* id, Vec2 a, Vec2 b, std::string& value);
    // The small ▽ button that opens a picker (20×20 at `at`).
    bool dropButton(const char* id, Vec2 at, bool enabled = true);
    // Up and down arrows (24×24) at `at`, dim when they cannot move.
    bool arrow(const char* id, Vec2 at, bool up, bool enabled);
    // Left and right arrows of a strip.
    bool sideArrow(const char* id, Vec2 a, Vec2 b, bool left, bool enabled);
    // A picture stretched over [a, b] (a dim box when missing).
    void picture(const Sprite& s, Vec2 a, Vec2 b, bool frame = false) const;

private:
    MenuContext& ctx_;
    Vec2 origin_;   // the area's top left, in frame pixels
    bool visible_ = false;
    ImFont* font(Face f) const;
    float fontSize(Face f) const;
};

// The starfield over the whole screen (Starmap.bmp tiled) behind the setup screens.
void drawStarfield(MenuContext& ctx);

// Escape pressed while no field is being edited and no popup is open. Call it
// before drawing any widget: leaving a text field with Escape must not also
// close the window.
bool escapePressed();

// Text fields editing a std::string (ImGui at the cursor).
bool inputText(const char* id, std::string& value, float width, ImGuiInputTextFlags flags = 0);
bool inputMultiline(const char* id, std::string& value, ImVec2 size);

// The original's list picker (spec 07 session 5): a 340×370 window centred on
// the screen, its name in the title font at the top left, a list with a
// 20 px heading and 17 px rows, and Cancel. Call open() once, then draw()
// every frame; draw() returns the row clicked (once), or -1.
class ListPicker {
public:
    void open(std::string title, std::string heading, std::vector<std::string> rows);
    bool isOpen() const { return open_; }
    int draw(MenuContext& ctx);

private:
    bool open_ = false;
    bool pending_ = false;
    std::string title_, heading_;
    std::vector<std::string> rows_;
};

} // namespace opense4::client::classic::setup
