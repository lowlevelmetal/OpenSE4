// The Options window (Game Menu → Options, docs/spec/06 §1.9): this
// computer's animation, sound, music, combat, display and autosave switches.
// Settings (Options → Settings, Ctrl+, or the intro): OpenSE4's
// graphics, controls and sound pages, in the classic dialog layout.

#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
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

// The music rows: Off, then the five volume steps (pick one).
bool musicRows(UiContext& ui, ClassicSettings& s) {
    bool changed = false;
    bool off = !s.musicOn;
    if (lampToggle(ui, "Music Off", &off) && off) {
        s.musicOn = false;
        changed = true;
    }
    for (const int step : kMusicVolumes) {
        bool on = s.musicOn && s.musicVolume == step;
        if (lampToggle(ui, std::format("Music Volume {}%", step).c_str(), &on) && on) {
            s.musicOn = true;
            s.musicVolume = step;
            changed = true;
        }
    }
    return changed;
}

class OptionsScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Options", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        ClassicSettings& s = settings();
        bool changed = false;
        d.beginContent();
        ImGui::TextColored(kLabelBlue, "Options In Use");
        ImGui::BeginChild("##options", ImVec2(0, 0), ImGuiChildFlags_Borders);
        heading(ui, "Animation");
        changed |= lampToggle(ui, "Animate ship movement in the system window", &s.animateSystemMovement);
        changed |= lampToggle(ui, "Animate ship movement in combat", &s.animateCombatMovement);
        ImGui::Spacing();
        heading(ui, "Sound");
        changed |= lampToggle(ui, "Sound On", &s.soundOn);
        changed |= lampToggle(ui, "Classic Sound Effects", &s.classicSoundEffects);
        ImGui::Spacing();
        heading(ui, "Music");
        changed |= musicRows(ui, s);
        ImGui::Spacing();
        heading(ui, "Tactical Combat");
        changed |= lampToggle(ui, "Fast Tactical Combat", &s.fastTacticalCombat);
        ImGui::Spacing();
        heading(ui, "System Display");
        changed |= lampToggle(ui, "Display Ship Movement Lines", &s.showMovementLines);
        // The game's autosave choice (spec 01 §2.2) belongs to the game; network
        // and e-mail games are saved by their host.
        if (ui.session.kind() == SessionKind::Local || ui.session.kind() == SessionKind::Hotseat) {
            ImGui::Spacing();
            heading(ui, "Autosave For This Game");
            const int current = ui.state().options.autosaveTurns;
            for (const int n : setup::kAutosaveTurns) {
                bool on = n == current;
                const std::string label = n == 0 ? std::string("None") : n == 1 ? std::string("Every Turn") : std::format("Every {} Turns", n);
                if (lampToggle(ui, label.c_str(), &on) && on) ui.session.setAutosaveTurns(n);
            }
        }
        ImGui::EndChild();
        if (changed) saveSettings();
        d.beginButtons();
        // OpenSE4's own: graphics, controls and the effects volume.
        if (d.button("Settings")) ui.open(ScreenId::Settings);
        d.close();
        return d.keepOpen();
    }
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
    // The music volume has the Options window's five steps (spec 06 §1.9). The
    // label is a printf format: a lone % at its end is undefined (glibc shows
    // stray bytes, the Windows runtime may stop the program), so it is doubled.
    ImGui::SetNextItemWidth(300 * px);
    int step = 0;
    for (size_t i = 0; i < kMusicVolumes.size(); ++i)
        if (kMusicVolumes[i] == s.musicVolume) step = int(i);
    if (ImGui::SliderInt("Music volume", &step, 0, int(kMusicVolumes.size()) - 1, std::format("{}%%", kMusicVolumes[size_t(step)]).c_str())) {
        s.musicVolume = kMusicVolumes[size_t(std::clamp(step, 0, int(kMusicVolumes.size()) - 1))];
        changed = true;
    }
    changed |= ImGui::Checkbox("Classic sound effects (the original set instead of the remastered one)", &s.classicSoundEffects);
    ImGui::TextDisabled("Sounds and music are read from the game's Sounds and Music folders.");
    if (changed) saveSettings();
}

std::unique_ptr<Screen> makeSettings(const ScreenArgs&) { return std::make_unique<SettingsScreen>(); }
std::unique_ptr<Screen> makeOptions(const ScreenArgs&) { return std::make_unique<OptionsScreen>(); }

} // namespace opense4::client::classic
