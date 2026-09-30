#pragma once

// Widgets shared by the Game Setup and Empire Setup windows (front end,
// MenuContext based): the full-screen setup frame with its right-hand column of
// page buttons, classic "lamp" options, text fields over std::string, and
// sprite helpers.

#include "client/classic/frontend.hpp"

#include <imgui.h>

#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace opense4::client::classic::setup {

inline const ImVec4 kLabelBlue{0.44f, 0.61f, 1.0f, 1.0f};
inline const ImVec4 kGood{0.45f, 0.95f, 0.5f, 1.0f};
inline const ImVec4 kBad{1.0f, 0.45f, 0.4f, 1.0f};
inline const ImVec4 kDim{0.62f, 0.66f, 0.76f, 1.0f};
inline const ImVec4 kHighlight{1.0f, 1.0f, 0.65f, 1.0f};

// A full-screen setup window: title strip, content on the left, a column of
// buttons on the right (pages at the top, the main actions at the bottom).
class SetupFrame {
public:
    SetupFrame(MenuContext& ctx, const char* title);
    ~SetupFrame();
    SetupFrame(const SetupFrame&) = delete;
    SetupFrame& operator=(const SetupFrame&) = delete;

    bool open() const { return visible_; }
    void beginContent();
    void beginButtons();
    // A page button, highlighted when active.
    bool pageButton(const char* label, bool active);
    // Places the cursor so `count` buttons end at the bottom of the column.
    void toBottom(int count);
    bool button(const char* label, bool enabled = true);

private:
    void endChild();
    MenuContext& ctx_;
    bool visible_ = false;
    bool inChild_ = false;
};

// Escape pressed while no field is being edited and no popup is open. Call it
// before drawing any widget: leaving a text field with Escape must not also
// close the window.
bool escapePressed();

// Section heading in the classic label blue.
void heading(MenuContext& ctx, const char* text);
// Small dim explanatory text, wrapped.
void note(const char* text);
// "Label   value" with the label in blue.
void labelValue(MenuContext& ctx, const char* label, const std::string& value, float valueColumn = 150.0f);

// An on/off option drawn as a row with a lamp that is lit when set.
bool lamp(MenuContext& ctx, const char* label, bool& value, bool enabled = true);
// One-of-several options as a row (or column) of lamps; returns true when changed.
bool lampChoice(MenuContext& ctx, const char* id, int& value, std::span<const std::string> labels, bool vertical = false);
bool lampChoice(MenuContext& ctx, const char* id, int& value, std::initializer_list<const char*> labels, bool vertical = false);

// Text fields editing a std::string.
bool inputText(const char* id, std::string& value, float width, ImGuiInputTextFlags flags = 0);
bool inputMultiline(const char* id, std::string& value, ImVec2 size);
// A text field with a drop-down of suggestions (name lists).
bool inputWithSuggestions(MenuContext& ctx, const char* id, std::string& value, const std::vector<std::string>& suggestions, float width);

// Sprites (empty sprites draw a dim placeholder box).
void sprite(const Sprite& s, ImVec2 size);
bool spriteButton(const char* id, const Sprite& s, ImVec2 size, bool selected);

// Arrow buttons ("<" / ">") of a fixed frame size.
bool arrowButton(MenuContext& ctx, const char* id, bool left, Vec2 frameSize);

} // namespace opense4::client::classic::setup
