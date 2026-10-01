// Settings (Game Menu → Settings, Ctrl+, or the intro): graphics, controls
// and sound, in the classic dialog layout.

#include "client/audio.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/settings_window.hpp"

#include <algorithm>
#include <format>

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
        if (d.tab("Graphics", page_ == Page::Graphics)) page_ = Page::Graphics;
        if (d.tab("Controls", page_ == Page::Controls)) page_ = Page::Controls;
        if (d.tab("Sound", page_ == Page::Sound)) page_ = Page::Sound;
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
    // Music: Off or one of five volumes (docs/spec/06 §1.9, §5.5).
    ImGui::TextUnformatted("Music");
    int step = s.musicOn ? std::max(1, musicStep(s.musicVolume)) : 0;
    for (int i = 0; i <= 5; ++i) {
        ImGui::SameLine(i == 0 ? 90 * px : 0.0f);
        const std::string label = i == 0 ? std::string("Off") : std::format("{}%", i * 20);
        if (ImGui::RadioButton(label.c_str(), step == i)) {
            step = i;
            s.musicOn = i > 0;
            if (i > 0) s.musicVolume = float(i) / 5.0f;
            changed = true;
        }
    }
    changed |= ImGui::Checkbox("Use the remastered sounds when the game has them", &s.remasteredSounds);
    ImGui::TextDisabled("Sounds and music are read from the game's Sounds and Music folders.");
    if (changed) saveSettings();
}

std::unique_ptr<Screen> makeSettings(const ScreenArgs&) { return std::make_unique<SettingsScreen>(); }

} // namespace opense4::client::classic
