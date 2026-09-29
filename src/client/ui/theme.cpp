#include "client/ui/theme.hpp"

#include "core/log.hpp"

#include <imgui.h>

namespace opense4::client {

Fonts loadFonts(const std::filesystem::path& assetsDir) {
    ImGuiIO& io = ImGui::GetIO();
    Fonts fonts;
    auto load = [&](const char* file) -> ImFont* {
        const auto path = assetsDir / "fonts" / file;
        if (!std::filesystem::exists(path)) {
            log::warn("Font not found: {}", path.string());
            return nullptr;
        }
        return io.Fonts->AddFontFromFileTTF(path.string().c_str(), 16.0f);
    };
    fonts.regular = load("NotoSans-Regular.ttf");
    fonts.medium = load("NotoSans-Medium.ttf");
    fonts.bold = load("NotoSans-Bold.ttf");
    if (!fonts.regular) fonts.regular = io.Fonts->AddFontDefault();
    if (!fonts.medium) fonts.medium = fonts.regular;
    if (!fonts.bold) fonts.bold = fonts.medium;
    io.FontDefault = fonts.regular;
    return fonts;
}

void applyTheme(float uiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    style.FontSizeBase = 16.0f;
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.CellPadding = ImVec2(6, 3);
    style.ScrollbarSize = 12.0f;
    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;

    auto rgba = [](float r, float g, float b, float a = 1.0f) { return ImVec4(r, g, b, a); };
    const ImVec4 accent = rgba(0.30f, 0.78f, 0.90f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = rgba(0.86f, 0.91f, 0.96f);
    c[ImGuiCol_TextDisabled] = rgba(0.47f, 0.55f, 0.64f);
    c[ImGuiCol_WindowBg] = rgba(0.045f, 0.065f, 0.10f, 0.94f);
    c[ImGuiCol_ChildBg] = rgba(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = rgba(0.05f, 0.075f, 0.11f, 0.97f);
    c[ImGuiCol_Border] = rgba(0.22f, 0.40f, 0.55f, 0.55f);
    c[ImGuiCol_FrameBg] = rgba(0.10f, 0.15f, 0.21f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = rgba(0.14f, 0.22f, 0.30f, 1.0f);
    c[ImGuiCol_FrameBgActive] = rgba(0.18f, 0.28f, 0.38f, 1.0f);
    c[ImGuiCol_TitleBg] = rgba(0.06f, 0.10f, 0.15f, 1.0f);
    c[ImGuiCol_TitleBgActive] = rgba(0.08f, 0.17f, 0.25f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = rgba(0.05f, 0.08f, 0.12f, 0.9f);
    c[ImGuiCol_MenuBarBg] = rgba(0.06f, 0.09f, 0.14f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = rgba(0.03f, 0.05f, 0.08f, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = rgba(0.18f, 0.30f, 0.40f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(0.24f, 0.40f, 0.52f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = rgba(0.26f, 0.60f, 0.72f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = rgba(0.12f, 0.25f, 0.34f, 1.0f);
    c[ImGuiCol_ButtonHovered] = rgba(0.18f, 0.37f, 0.48f, 1.0f);
    c[ImGuiCol_ButtonActive] = rgba(0.24f, 0.48f, 0.62f, 1.0f);
    c[ImGuiCol_Header] = rgba(0.12f, 0.26f, 0.36f, 0.75f);
    c[ImGuiCol_HeaderHovered] = rgba(0.18f, 0.36f, 0.48f, 0.85f);
    c[ImGuiCol_HeaderActive] = rgba(0.22f, 0.44f, 0.58f, 1.0f);
    c[ImGuiCol_Separator] = rgba(0.20f, 0.34f, 0.46f, 0.6f);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = rgba(0.20f, 0.40f, 0.52f, 0.4f);
    c[ImGuiCol_ResizeGripHovered] = rgba(0.26f, 0.55f, 0.70f, 0.7f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = rgba(0.09f, 0.17f, 0.24f, 1.0f);
    c[ImGuiCol_TabHovered] = rgba(0.18f, 0.36f, 0.48f, 1.0f);
    c[ImGuiCol_TabSelected] = rgba(0.14f, 0.29f, 0.40f, 1.0f);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = rgba(0.50f, 0.90f, 1.0f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = rgba(0.08f, 0.15f, 0.21f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = rgba(0.20f, 0.34f, 0.46f, 0.7f);
    c[ImGuiCol_TableBorderLight] = rgba(0.16f, 0.26f, 0.36f, 0.5f);
    c[ImGuiCol_TableRowBgAlt] = rgba(1.0f, 1.0f, 1.0f, 0.025f);
    c[ImGuiCol_TextSelectedBg] = rgba(0.26f, 0.60f, 0.72f, 0.35f);
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_ModalWindowDimBg] = rgba(0.0f, 0.0f, 0.0f, 0.55f);

    style.ScaleAllSizes(uiScale);
    style.FontScaleDpi = uiScale;
}

} // namespace opense4::client
