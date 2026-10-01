#include "client/settings_window.hpp"

#include "client/app_settings.hpp"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace opense4::client {

namespace {

const ImVec4 kLabel{0.44f, 0.61f, 1.0f, 1.0f};

void section(const char* text) {
    ImGui::Spacing();
    ImGui::TextColored(kLabel, "%s", text);
    ImGui::Separator();
}

template <class E, size_t N>
bool choice(const char* label, E& value, const std::array<E, N>& options, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, displayName(value))) {
        for (E o : options)
            if (ImGui::Selectable(displayName(o), o == value)) {
                value = o;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

void graphicsSettingsPage(SettingsPanelState& state, AppControl& app, float px) {
    GraphicsSettings& g = appSettings().graphics;
    bool save = false;
    const float w = 300 * px;

    section("Display");
    if (choice("Window mode", g.displayMode,
               std::array{DisplayMode::Windowed, DisplayMode::Borderless, DisplayMode::Fullscreen}, w))
        state.displayDirty = true;
    if (g.displayMode == DisplayMode::Windowed) {
        static constexpr std::array<std::pair<int, int>, 8> kSizes{
            {{1280, 720}, {1280, 800}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1080}, {2560, 1440}, {3440, 1440}}};
        const std::string current = std::format("{} x {}", g.windowWidth, g.windowHeight);
        ImGui::SetNextItemWidth(w);
        if (ImGui::BeginCombo("Window size", current.c_str())) {
            for (const auto& [sw, sh] : kSizes)
                if (ImGui::Selectable(std::format("{} x {}", sw, sh).c_str(), sw == g.windowWidth && sh == g.windowHeight)) {
                    g.windowWidth = sw;
                    g.windowHeight = sh;
                    state.displayDirty = true;
                }
            ImGui::EndCombo();
        }
    } else if (g.displayMode == DisplayMode::Fullscreen) {
        const auto modes = app.displayModes();
        const std::string current =
            g.fullscreenWidth > 0 ? std::format("{} x {} @ {:.0f} Hz", g.fullscreenWidth, g.fullscreenHeight, g.fullscreenRefresh) : "Desktop";
        ImGui::SetNextItemWidth(w);
        if (ImGui::BeginCombo("Resolution", current.c_str())) {
            if (ImGui::Selectable("Desktop", g.fullscreenWidth == 0)) {
                g.fullscreenWidth = g.fullscreenHeight = 0;
                g.fullscreenRefresh = 0;
                state.displayDirty = true;
            }
            for (const DisplayModeInfo& m : modes) {
                const bool sel = m.width == g.fullscreenWidth && m.height == g.fullscreenHeight && m.refresh == g.fullscreenRefresh;
                if (ImGui::Selectable(std::format("{} x {} @ {:.0f} Hz", m.width, m.height, m.refresh).c_str(), sel)) {
                    g.fullscreenWidth = m.width;
                    g.fullscreenHeight = m.height;
                    g.fullscreenRefresh = m.refresh;
                    state.displayDirty = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::BeginDisabled(!state.displayDirty);
    if (ImGui::Button("Apply display", ImVec2(140 * px, 0))) {
        app.applyGraphics();
        state.displayDirty = false;
        save = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Alt+Enter switches between the window and fullscreen.");

    if (ImGui::Checkbox("Vertical sync", &g.vsync)) {
        app.applyGraphics();
        save = true;
    }
    ImGui::BeginDisabled(g.vsync);
    ImGui::SetNextItemWidth(w);
    static constexpr std::array<int, 6> kLimits{0, 30, 60, 120, 144, 240};
    const std::string limit = g.frameLimit > 0 ? std::format("{} fps", g.frameLimit) : std::string("Unlimited");
    if (ImGui::BeginCombo("Frame rate limit", limit.c_str())) {
        for (int l : kLimits)
            if (ImGui::Selectable(l > 0 ? std::format("{} fps", l).c_str() : "Unlimited", l == g.frameLimit)) {
                g.frameLimit = l;
                save = true;
            }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    save |= ImGui::Checkbox("Show frame rate", &g.showFps);

    section("Classic screens");
    save |= choice("Screen layout", g.layout, std::array{LayoutChoice::Auto, LayoutChoice::Small800, LayoutChoice::Large1024}, w);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The classic game's 800x600 or 1024x768 layout (OpenSE4 can force either)");
    save |= choice("Widescreen layout", g.widescreen, std::array{WidescreenLayout::Extended, WidescreenLayout::Classic}, w);
    save |= ImGui::Checkbox("Sharp pixels (no smoothing of the game's art)", &g.sharpPixels);
    save |= ImGui::Checkbox("Scale by whole multiples only", &g.integerScaling);
    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("Text size", &g.textScale, 0.75f, 1.5f, "%.2fx")) save = true;

    section("Renderer");
    RendererChoice renderer = g.renderer;
    if (choice("Renderer", renderer, std::array{RendererChoice::Auto, RendererChoice::Vulkan, RendererChoice::OpenGL}, w)) {
        g.renderer = renderer;
        save = true;
        state.message = "The renderer changes the next time OpenSE4 starts.";
    }
    ImGui::TextDisabled("In use: %s", app.rendererInfo().c_str());
    if (!state.message.empty()) ImGui::TextColored(ImVec4(1, 0.85f, 0.45f, 1), "%s", state.message.c_str());

    if (save) saveAppSettings();
}

void controlsSettingsPage(SettingsPanelState& state, float px) {
    ControlSettings& c = appSettings().controls;
    bool save = false;

    section("Mouse");
    save |= ImGui::Checkbox("Right-click a sector to move the selected ship there", &c.rightClickMoves);
    ImGui::SetNextItemWidth(300 * px);
    save |= ImGui::SliderFloat("Double-click time", &c.doubleClickSeconds, 0.1f, 1.0f, "%.2f s");

    section("Keys");
    ImGui::TextDisabled("Click a key to change it, then press the new key (Escape cancels, Backspace clears).");
    if (state.capturing) {
        const auto [action, slot] = *state.capturing;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            state.capturing.reset();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !ImGui::GetIO().KeyCtrl) {
            c.bindings.set(action, slot, {});
            state.capturing.reset();
            save = true;
        } else if (auto chord = capturePressedChord()) {
            if (auto other = c.bindings.boundTo(*chord, action))
                state.message = std::format("{} was also used by \"{}\"; that binding was removed.", chordName(*chord), actionInfo(*other).label);
            else
                state.message.clear();
            if (auto other = c.bindings.boundTo(*chord, action))
                for (int s = 0; s < 2; ++s)
                    if (c.bindings.chords(*other)[static_cast<size_t>(s)] == *chord) c.bindings.set(*other, s, {});
            c.bindings.set(action, slot, *chord);
            state.capturing.reset();
            save = true;
        }
    }
    if (!state.message.empty()) ImGui::TextColored(ImVec4(1, 0.85f, 0.45f, 1), "%s", state.message.c_str());
    if (ImGui::Button("Restore default keys")) {
        c.bindings.resetAll();
        state.message.clear();
        save = true;
    }

    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 150 * px);
        ImGui::TableSetupColumn("Alternative", ImGuiTableColumnFlags_WidthFixed, 150 * px);
        ImGui::TableHeadersRow();
        const char* group = nullptr;
        for (const ActionInfo& info : actionInfos()) {
            if (!group || std::string_view(group) != info.group) {
                group = info.group;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(kLabel, "%s", group);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.label);
            for (int slot = 0; slot < 2; ++slot) {
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(info.action) * 2 + slot);
                const bool waiting = state.capturing && state.capturing->first == info.action && state.capturing->second == slot;
                const std::string label = waiting ? std::string("Press a key...") : chordName(c.bindings.chords(info.action)[static_cast<size_t>(slot)]);
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) state.capturing = std::pair{info.action, slot};
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (save) saveAppSettings();
}

} // namespace opense4::client
