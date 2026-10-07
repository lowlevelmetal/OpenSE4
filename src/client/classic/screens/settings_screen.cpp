// The Options window (Game Menu → Options, docs/spec/06 §1.9): this
// computer's animation, sound, music, combat, display and autosave switches.
// Settings (Options → Settings, Ctrl+, or the intro): OpenSE4's
// graphics, controls and sound pages, in the classic dialog layout.

#include "client/app_settings.hpp"
#include "client/classic/mod_ui.hpp"
#include "client/classic/movement_pace.hpp"
#include "client/classic/net_transport.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/settings_window.hpp"
#include "client/script/items.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace opense4::client::classic {

namespace {

enum class Page { Graphics, Controls, Sound, Modding };

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
            case Page::Modding: moddingSettingsPage(ui.k(), &ui.rules()); break;
        }
        ImGui::EndChild();
        d.beginButtons();
        if (d.tab("Graphics", page_ == Page::Graphics)) page_ = Page::Graphics;
        if (d.tab("Controls", page_ == Page::Controls)) page_ = Page::Controls;
        if (d.tab("Sound", page_ == Page::Sound)) page_ = Page::Sound;
        if (d.tab("Modding", page_ == Page::Modding)) page_ = Page::Modding;
        // While a key is being captured, Escape cancels the capture instead of closing.
        if (!state_.capturingKey()) d.close();
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

// OpenSE4's ship movement speed (movement_pace.hpp): a slider over the steps
// of kMovementSpeeds, each twice the one before, the original's waits marked.
std::string movementSpeedName(size_t step) {
    const double v = kMovementSpeeds[std::min(step, kMovementSpeeds.size() - 1)];
    std::string name = v >= 1.0 ? std::format("{}x", std::lround(v)) : std::format("1/{}x", std::lround(1.0 / v));
    if (step == kOriginalMovementSpeed) name += " (the original's)";
    return name;
}

bool movementSpeedRow(UiContext& ui, ClassicSettings& s) {
    int step = int(movementSpeedStep(s.systemMovementSpeed));
    // The slider's text is its value's name (no printf directive in it).
    const std::string shown = movementSpeedName(size_t(step));
    ImGui::SetNextItemWidth(ui.px(220));
    const bool moved = ImGui::SliderInt("Ship movement speed", &step, 0, int(kMovementSpeeds.size()) - 1, shown.c_str(), ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How fast ships turn and slide in the system window when they move,\n"
                          "and in the movement log replay. 1x is the original's own pace; each\n"
                          "step halves or doubles it. A click or a key shows a new turn's moves at once.");
    if (!moved) return false;
    s.systemMovementSpeed = kMovementSpeeds[size_t(std::clamp(step, 0, int(kMovementSpeeds.size()) - 1))];
    return true;
}

class OptionsScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Options", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        ClassicSettings& s = settings();
        bool changed = false;
        // Opening the window with music off, or with Settings.txt `Allow CD
        // Music` FALSE, lights Music Off and stores music off at once; music
        // is stored on again only when a volume lamp is picked (spec 06 §1.9).
        if (!opened_) {
            opened_ = true;
            changed |= openMusicRows(s, musicAllowed(ui.rules().data().settings));
        }
        d.beginContent();
        ImGui::TextColored(kLabelBlue, "Options In Use");
        beginList(ui, "##options", ImVec2(0, 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
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
        // OpenSE4's own, beside the original's rows: how fast ships move in the system window.
        ImGui::Spacing();
        heading(ui, "OpenSE4");
        changed |= movementSpeedRow(ui, s);
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
        endList(ui);
        if (changed) saveSettings();
        d.beginButtons();
        // OpenSE4's own: graphics, controls and the effects volume.
        if (d.button("Settings")) ui.open(ScreenId::Settings);
        // Reset Passwords: only on the host of a simultaneous game (spec 06
        // §1.9, confirmed: binary). A click first discards every reset chosen
        // earlier and not applied yet, even if the picker is then cancelled.
        if (resetOffered(ui) && d.button("Reset Passwords")) {
            if (net::HostSession* host = hostOf(ui)) host->clearPasswordResets();
            resetLamps_.assign(ui.state().empires.size(), 0);
            ImGui::OpenPopup("Select Empires to have password reset");
        }
        resetPicker(ui);
        resetNotice(ui);
        d.close();
        return d.keepOpen();
    }

private:
    // The in-game host (the HostSession runs here), or none.
    static net::HostSession* hostOf(UiContext& ui) {
        auto* t = dynamic_cast<HostTransport*>(ui.session.transport());
        return t ? &t->host() : nullptr;
    }
    // A player who gave the master password to a headless server stands in
    // for its host (inferred: the dedicated server has no window of its own).
    static net::ClientSession* adminOf(UiContext& ui) {
        auto* t = dynamic_cast<ClientTransport*>(ui.session.transport());
        return t && t->client().admin() ? &t->client() : nullptr;
    }
    static bool resetOffered(UiContext& ui) { return !ui.session.turnBased() && (hostOf(ui) || adminOf(ui)); }

    // The Player Computer Control check list, titled for the purpose: every
    // empire, all lamps off; OK resets the lit ones.
    void resetPicker(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({420, 0}));
        if (!ImGui::BeginPopupModal("Select Empires to have password reset", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
            return;
        const game::GameState& s = ui.state();
        resetLamps_.resize(s.empires.size(), 0);
        for (const game::Empire& e : s.empires) {
            ImGui::PushID(int(e.id.index()));
            bool on = resetLamps_[e.id.index()] != 0;
            if (lampToggle(ui, std::format("{}. {}", e.id.value + 1, e.name).c_str(), &on)) resetLamps_[e.id.index()] = on;
            ImGui::PopID();
        }
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("OK", ImVec2(w, ui.px(26)))) {
            std::vector<game::EmpireId> empires;
            for (size_t i = 0; i < resetLamps_.size(); ++i)
                if (resetLamps_[i]) empires.push_back(game::EmpireId{static_cast<uint32_t>(i)});
            if (net::HostSession* host = hostOf(ui)) {
                if (auto r = host->resetPasswords(empires); r)
                    for (const auto& p : *r)
                        resets_.push_back(std::format("Player {} ({}) has the new password {}. It takes effect when the next turn is processed.",
                                                      p.empire.value + 1, s.empire(p.empire).name, p.password));
                else
                    resets_.push_back(r.error());
            } else if (net::ClientSession* admin = adminOf(ui)) {
                admin->requestPasswordReset(empires);
                if (!empires.empty()) resets_.push_back("The host will answer with the new passwords in the chat log.");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, ui.px(26)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (!resets_.empty()) ImGui::OpenPopup("Password Reset");
    }

    // One "Password Reset" message per empire, to the host only.
    void resetNotice(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({380, 0}));
        if (!ImGui::BeginPopupModal("Password Reset", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) return;
        if (!resets_.empty()) ImGui::TextWrapped("%s", resets_.front().c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) {
            if (!resets_.empty()) resets_.erase(resets_.begin());
            if (resets_.empty()) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool opened_ = false;
    std::vector<uint8_t> resetLamps_;
    std::vector<std::string> resets_;
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
    changed |= ImGui::Checkbox("Mute when the game is in the background", &s.muteInBackground);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("While another window has the focus, or the game is minimized or hidden, its sound effects and\n"
                          "music fade out. The music pauses where it is and goes on from there when you come back.");
    ImGui::TextDisabled("Sounds and music are read from the game's Sounds and Music folders.");
    if (changed) saveSettings();
}

void moddingSettingsPage(float px, const game::Rules* rules) {
    ClassicSettings& s = settings();
    bool changed = false;
    // The language of the mods' text (docs/sdk/interface.md "Text"): English and
    // every language the mods in use have strings in; only while they have some.
    std::vector<std::string> languages{"en"};
    if (rules)
        for (const std::string& l : modUi(*rules).texts.languages())
            if (std::find(languages.begin(), languages.end(), l) == languages.end()) languages.push_back(l);
    if (std::find(languages.begin(), languages.end(), s.modLanguage) == languages.end()) languages.push_back(s.modLanguage);
    if (languages.size() > 1) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.44f, 0.61f, 1.0f, 1.0f), "The mods' text");
        ImGui::Separator();
        ImGui::SetNextItemWidth(160 * px);
        const ImVec2 comboAt = ImGui::GetCursorScreenPos();
        const bool comboOpen = ImGui::BeginCombo("Language of the mods' text", s.modLanguage.c_str());
        // Input scripts open it by its label.
        script::reportItem("Language of the mods' text", comboAt, ImVec2(comboAt.x + 160 * px, comboAt.y + ImGui::GetFrameHeight()));
        if (comboOpen) {
            for (const std::string& l : languages)
                if (ImGui::Selectable(l.c_str(), l == s.modLanguage)) {
                    s.modLanguage = l;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * px);
        ImGui::TextDisabled("The names mods give their orders, options, panels and pages, from their text/<language>.toml files; "
                            "English, then the mods' own words, where a mod has no text in this language. The game's own windows stay in English.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.44f, 0.61f, 1.0f, 1.0f), "Computer players of mods");
    ImGui::Separator();
    const std::string keys = chordName(appSettings().controls.bindings.chords(Action::AiNotes)[0]);
    changed |= ImGui::Checkbox(std::format("Show the computer players' notes ({} in a game)", keys).c_str(), &s.showAiNotes);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * px);
    ImGui::TextDisabled("A debug view for making computer players: the notes a mod's computer players write about what they "
                        "think, on the system and galaxy maps and in the reports. It shows the notes of every computer player "
                        "this computer runs, whatever your empire knows: in a game on this computer, or in a network game you host.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * px);
    ImGui::TextDisabled("When such a player fails, the game says so in the main window, and its Details show what went wrong. "
                        "The game's log file (opense4.log in OpenSE4's user folder) has every failure too.");
    ImGui::PopTextWrapPos();
    if (changed) saveSettings();
}

std::unique_ptr<Screen> makeSettings(const ScreenArgs&) { return std::make_unique<SettingsScreen>(); }
std::unique_ptr<Screen> makeOptions(const ScreenArgs&) { return std::make_unique<OptionsScreen>(); }

} // namespace opense4::client::classic
