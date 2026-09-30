// Settings (Game Menu → Settings, Ctrl+, or the intro): graphics, controls
// and sound, in the classic dialog layout.

#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/settings_window.hpp"

namespace opense4::client::classic {

namespace {

enum class Page { Graphics, Controls, Sound };

class SettingsScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Settings", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        ImGui::BeginChild("##page", ImVec2(0, 0));
        switch (page_) {
            case Page::Graphics:
                if (ui.app) graphicsSettingsPage(state_, *ui.app, ui.k());
                break;
            case Page::Controls: controlsSettingsPage(state_, ui.k()); break;
            case Page::Sound: soundSettingsPage(ui.k()); break;
        }
        ImGui::EndChild();
        d.beginButtons();
        if (d.button("Graphics", true, page_ == Page::Graphics)) page_ = Page::Graphics;
        if (d.button("Controls", true, page_ == Page::Controls)) page_ = Page::Controls;
        if (d.button("Sound", true, page_ == Page::Sound)) page_ = Page::Sound;
        // While a key is being captured, Escape cancels the capture instead of closing.
        if (!state_.capturing) d.close();
        return d.keepOpen();
    }

private:
    Page page_ = Page::Graphics;
    SettingsPanelState state_;
};

} // namespace

void soundSettingsPage(float px) {
    ClassicSettings& s = settings();
    bool changed = false;
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.44f, 0.61f, 1.0f, 1.0f), "Sound");
    ImGui::Separator();
    changed |= ImGui::Checkbox("Sound effects", &s.soundOn);
    ImGui::SetNextItemWidth(300 * px);
    changed |= ImGui::SliderFloat("Effects volume", &s.soundVolume, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::Checkbox("Music", &s.musicOn);
    ImGui::SetNextItemWidth(300 * px);
    changed |= ImGui::SliderFloat("Music volume", &s.musicVolume, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::Checkbox("Use the remastered sounds when the game has them", &s.remasteredSounds);
    ImGui::TextDisabled("Sounds and music are read from the game's Sounds and Music folders.");
    if (changed) saveSettings();
}

std::unique_ptr<Screen> makeSettings(const ScreenArgs&) { return std::make_unique<SettingsScreen>(); }

} // namespace opense4::client::classic
