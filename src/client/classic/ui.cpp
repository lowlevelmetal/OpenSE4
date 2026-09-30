#include "client/classic/ui.hpp"

#include <format>

namespace opense4::client::classic {

namespace {

const ImVec4 kLabelBlue{0.44f, 0.61f, 1.0f, 1.0f};

Rect dialogRect(DialogSize size) {
    Vec2 s;
    switch (size) {
        case DialogSize::Large: s = {998, 608}; break;
        case DialogSize::Tall: s = {998, 750}; break;
        case DialogSize::Report: s = {400, 540}; break;
        case DialogSize::Picker: s = {540, 660}; break;
        case DialogSize::Prompt: s = {420, 190}; break;
        case DialogSize::Full: s = {kFrameW, kFrameH}; break;
    }
    const Vec2 min{(kFrameW - s.x) * 0.5f, (kFrameH - s.y) * 0.5f};
    return Rect{min, min + s};
}

} // namespace

void image(UiContext& ui, const Sprite& s, Vec2 frameSize, Color tint) {
    if (!s) {
        ImGui::Dummy(ui.size(frameSize));
        return;
    }
    ImGui::ImageWithBg(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), ui.size(frameSize), ImVec2(s.uv.min.x, s.uv.min.y),
                       ImVec2(s.uv.max.x, s.uv.max.y), ImVec4(0, 0, 0, 0), ImVec4(tint.r, tint.g, tint.b, tint.a));
}

bool imageButton(UiContext& ui, const char* id, const Sprite& s, Vec2 frameSize, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    bool clicked = false;
    if (s)
        clicked = ImGui::ImageButton(id, ImTextureRef(static_cast<ImTextureID>(s.tex.value)), ui.size(frameSize), ImVec2(s.uv.min.x, s.uv.min.y),
                                     ImVec2(s.uv.max.x, s.uv.max.y));
    else
        clicked = ImGui::Button(id, ui.size(frameSize));
    ImGui::EndDisabled();
    return clicked;
}

void resources(UiContext& ui, const game::Resources& r, bool compact) {
    static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
    static constexpr std::array<ImVec4, 3> kColors{ImVec4(0.45f, 0.65f, 1.0f, 1), ImVec4(0.35f, 0.85f, 0.4f, 1), ImVec4(1.0f, 0.4f, 0.35f, 1)};
    for (size_t i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0, ui.px(compact ? 6 : 12));
        if (Sprite s = ui.art.icon16(kIcons[i])) {
            image(ui, s, {14, 14});
            ImGui::SameLine(0, ui.px(3));
            ImGui::TextUnformatted(formatNumber(r.v[i]).c_str());
        } else {
            ImGui::TextColored(kColors[i], "%s", formatNumber(r.v[i]).c_str());
        }
    }
}

void labelValue(UiContext& ui, const char* label, const std::string& value, float valueColumn) {
    ImGui::TextColored(kLabelBlue, "%s", label);
    ImGui::SameLine(ui.px(valueColumn));
    ImGui::TextUnformatted(value.c_str());
}

void heading(UiContext& ui, const char* text) {
    ImGui::PushFont(ui.fonts.bold, ImGui::GetFontSize());
    ImGui::TextColored(kLabelBlue, "%s", text);
    ImGui::PopFont();
}

std::string formatNumber(int64_t v) {
    const bool neg = v < 0;
    std::string digits = std::to_string(neg ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return neg ? "-" + out : out;
}

std::string formatDate(uint32_t turn) { return std::format("{}.{}", 2400 + turn / 10, turn % 10); }

ImU32 empireColor(const game::GameState& s, game::EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return IM_COL32(200, 200, 200, 255);
    const uint32_t c = s.empire(e).color;
    return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
}

// ---- Dialog --------------------------------------------------------------------------------

Dialog::Dialog(UiContext& ui, const char* title, DialogSize size, float buttonColumn) : ui_(ui), buttonColumn_(buttonColumn) {
    const Rect r = dialogRect(size);
    ImGui::SetNextWindowPos(ui.at(r.min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ui.size(r.size()), ImGuiCond_Always);
    ImGui::PushFont(ui.fonts.regular, 14.0f * ui.k());
    visible_ = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    if (visible_ && ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
}

Dialog::~Dialog() {
    endChild();
    ImGui::End();
    ImGui::PopFont();
}

void Dialog::endChild() {
    if (inChild_) ImGui::EndChild();
    inChild_ = false;
}

void Dialog::beginContent() {
    endChild();
    const float w = buttonColumn_ > 0 ? -(ui_.px(buttonColumn_) + ImGui::GetStyle().ItemSpacing.x) : 0.0f;
    ImGui::BeginChild("##content", ImVec2(w, 0), ImGuiChildFlags_None);
    inChild_ = true;
}

void Dialog::beginButtons() {
    endChild();
    ImGui::SameLine();
    ImGui::BeginChild("##buttons", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    inChild_ = true;
    buttonsStarted_ = true;
}

bool Dialog::button(const char* label, bool enabled, bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.36f, 0.75f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.75f, 1));
    }
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, ui_.px(26)));
    ImGui::EndDisabled();
    if (active) ImGui::PopStyleColor(2);
    return clicked;
}

void Dialog::spacer() { ImGui::Dummy(ImVec2(0, ui_.px(8))); }

bool Dialog::close() {
    const float h = ui_.px(26);
    const float y = ImGui::GetWindowHeight() - h - ImGui::GetStyle().WindowPadding.y;
    if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
    const bool clicked = ImGui::Button("Close", ImVec2(-FLT_MIN, h));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (clicked || escape) keep_ = false;
    return clicked || escape;
}

void applyClassicStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.WindowBorderSize = 1.5f;
    style.FrameBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.035f, 0.09f, 0.97f);
    c[ImGuiCol_PopupBg] = ImVec4(0.02f, 0.035f, 0.09f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.26f, 0.42f, 0.85f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.12f, 0.32f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.18f, 0.45f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.05f, 0.09f, 0.22f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.22f, 0.50f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.36f, 0.75f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.03f, 0.06f, 0.16f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.12f, 0.24f, 0.55f, 0.85f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.30f, 0.65f, 0.85f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.36f, 0.75f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.06f, 0.12f, 0.32f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.05f, 0.08f, 0.18f, 0.6f);
}

} // namespace opense4::client::classic
