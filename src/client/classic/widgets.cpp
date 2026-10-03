#include "client/classic/widgets.hpp"

#include "client/script/items.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace opense4::client::classic {

namespace {


// The indicator lamps in General.bmp: 13 px cells from x=178 (blue, green, red, grey).
Sprite lampSprite(UiContext& ui, bool on) { return ui.art.region("Pictures/Game/General.bmp", 178 + 13 * (on ? 1 : 3), 0, 13, 13); }

bool inputImpl(const char* label, std::string& value, size_t maxLength, ImGuiInputTextFlags flags, bool multiline, ImVec2 size) {
    std::vector<char> buffer(std::max(value.size(), maxLength) + 1, '\0');
    std::copy(value.begin(), value.end(), buffer.begin());
    const bool changed = multiline ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), size, flags)
                                   : ImGui::InputText(label, buffer.data(), buffer.size(), flags);
    if (changed) value = buffer.data();
    return changed;
}

} // namespace

void lamp(UiContext& ui, bool on, float frameSize) {
    if (const Sprite s = lampSprite(ui, on)) {
        image(ui, s, {frameSize, frameSize});
        return;
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = ui.px(frameSize) * 0.4f;
    ImGui::GetWindowDrawList()->AddCircleFilled({p.x + r * 1.25f, p.y + r * 1.25f}, r, on ? IM_COL32(80, 220, 90, 255) : IM_COL32(90, 90, 90, 255));
    ImGui::Dummy(ui.size({frameSize, frameSize}));
}

bool lampToggle(UiContext& ui, const char* label, bool* value, bool enabled) {
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = std::max(ImGui::GetTextLineHeight(), ui.px(16));
    const bool clicked = ImGui::Selectable("##row", false, enabled ? 0 : ImGuiSelectableFlags_Disabled, ImVec2(0, h));
    script::reportItem(label);   // input scripts find the row by its label
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ls = ui.px(13);
    const ImVec2 l0{p.x + ui.px(3), p.y + (h - ls) * 0.5f};
    if (const Sprite s = lampSprite(ui, *value)) drawSprite(dl, s, l0, {l0.x + ls, l0.y + ls}, enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110));
    else dl->AddCircleFilled({l0.x + ls * 0.5f, l0.y + ls * 0.5f}, ls * 0.4f, *value ? IM_COL32(80, 220, 90, 255) : IM_COL32(90, 90, 90, 255));
    const ImU32 text = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    dl->AddText({p.x + ui.px(22), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, text, label);
    ImGui::PopID();
    if (clicked && enabled) *value = !*value;
    return clicked && enabled;
}

bool checkRow(UiContext& ui, const char* label, bool* value, float indent, bool enabled) {
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.px(18);
    const bool clicked = ImGui::Selectable("##row", false, enabled ? 0 : ImGuiSelectableFlags_Disabled, ImVec2(0, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b0{p.x + ui.px(indent), p.y + std::floor((h - ui.px(17)) * 0.5f)};
    const ImVec2 b1{b0.x + ui.px(16), b0.y + ui.px(17)};
    dl->AddRect(b0, b1, imColor(enabled ? palette::kSecondary : palette::kDisabled), 0.0f, std::max(1.0f, ui.px(1)));
    if (*value)
        if (const Sprite s = ui.art.region("Pictures/Game/General.bmp", 191, 0, 13, 13)) {
            const ImVec2 l0{std::floor((b0.x + b1.x - ui.px(13)) * 0.5f), std::floor((b0.y + b1.y - ui.px(13)) * 0.5f)};
            drawSprite(dl, s, l0, {l0.x + ui.px(13), l0.y + ui.px(13)}, enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110));
        }
    const ImU32 text = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    dl->AddText({b1.x + ui.px(6), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, text, label);
    ImGui::PopID();
    if (clicked && enabled) *value = !*value;
    return clicked && enabled;
}

bool inputString(const char* label, std::string& value, size_t maxLength, ImGuiInputTextFlags flags) {
    return inputImpl(label, value, maxLength, flags, false, {});
}

bool inputMultiline(const char* label, std::string& value, ImVec2 size, size_t maxLength) {
    return inputImpl(label, value, maxLength, 0, true, size);
}

ImTextureRef textureOf(const Sprite& s) { return ImTextureRef(static_cast<ImTextureID>(s.tex.value)); }

void drawSprite(ImDrawList* dl, const Sprite& s, ImVec2 min, ImVec2 max, ImU32 tint) {
    if (!s) return;
    dl->AddImage(textureOf(s), min, max, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y}, tint);
}

void drawSpriteRotated(ImDrawList* dl, const Sprite& s, ImVec2 c, float w, float h, float angle, ImU32 tint) {
    if (!s) return;
    const float cs = std::cos(angle), sn = std::sin(angle);
    auto corner = [&](float x, float y) { return ImVec2{c.x + x * cs - y * sn, c.y + x * sn + y * cs}; };
    const float hw = w * 0.5f, hh = h * 0.5f;
    dl->AddImageQuad(textureOf(s), corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh), {s.uv.min.x, s.uv.min.y},
                     {s.uv.max.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y}, {s.uv.min.x, s.uv.max.y}, tint);
}

void drawSpriteAlong(ImDrawList* dl, const Sprite& s, ImVec2 a, ImVec2 b, float width, ImU32 tint) {
    if (!s) return;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    const ImVec2 n{-dy / len * width * 0.5f, dx / len * width * 0.5f};
    dl->AddImageQuad(textureOf(s), {a.x + n.x, a.y + n.y}, {a.x - n.x, a.y - n.y}, {b.x - n.x, b.y - n.y}, {b.x + n.x, b.y + n.y},
                     {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y}, {s.uv.min.x, s.uv.max.y}, tint);
}

void dimText(const char* text) { ImGui::TextColored(kDimText, "%s", text); }

void wrappedDim(const std::string& text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDimText, "%s", text.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace opense4::client::classic
