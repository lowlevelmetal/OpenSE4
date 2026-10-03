#pragma once

// Small controls shared by the Empire Status, Game Menu, Galaxy Map and
// Combat Replay windows: the classic on/off lamp rows, std::string text
// input, and drawing sprites with the ImGui draw list.

#include "client/classic/ui.hpp"

#include <string>
#include <string_view>

namespace opense4::client::classic {

// A small on/off lamp (the classic list "light"), drawn at the cursor.
void lamp(UiContext& ui, bool on, float frameSize = 13.0f);
// A full-width row with a lamp and a label; clicking toggles *value. Returns true when toggled.
bool lampToggle(UiContext& ui, const char* label, bool* value, bool enabled = true);
// An on/off row of an options list as Combat Replay Options draws them (spec
// 07 session 5, observed): an 18 px row with a 16 x 17 check box at `indent`
// frame pixels from the row's left, holding the green lamp when on, and the
// label after it. A click on the row flips `value`; true when it did.
bool checkRow(UiContext& ui, const char* label, bool* value, float indent = 5.0f, bool enabled = true);

// ImGui::InputText on a std::string (up to maxLength bytes).
bool inputString(const char* label, std::string& value, size_t maxLength = 200, ImGuiInputTextFlags flags = 0);
bool inputMultiline(const char* label, std::string& value, ImVec2 size, size_t maxLength = 2000);

// Draw-list helpers (screen coordinates in ImGui units).
ImTextureRef textureOf(const Sprite& s);
void drawSprite(ImDrawList* dl, const Sprite& s, ImVec2 min, ImVec2 max, ImU32 tint = IM_COL32_WHITE);
// A sprite centred at c, size w×h, rotated clockwise by `angle` radians.
void drawSpriteRotated(ImDrawList* dl, const Sprite& s, ImVec2 c, float w, float h, float angle, ImU32 tint = IM_COL32_WHITE);
// A sprite stretched from a to b (its vertical axis along the segment), `width` wide.
void drawSpriteAlong(ImDrawList* dl, const Sprite& s, ImVec2 a, ImVec2 b, float width, ImU32 tint = IM_COL32_WHITE);

// Text with its glyph cell's top at `at` (window coordinates of `d`) in a font
// of `size` frame pixels whose face has `lead` (ui.hpp kTextLead ...).
void textAt(UiContext& ui, const Dialog& d, ImFont* font, float size, float lead, Vec2 at, ImU32 color, std::string_view text);
// The same, right-aligned to x.
void textRightAt(UiContext& ui, const Dialog& d, ImFont* font, float size, float lead, Vec2 at, ImU32 color, std::string_view text);

// Text in the classic label blue / dim grey.
void dimText(const char* text);
void wrappedDim(const std::string& text);

} // namespace opense4::client::classic
