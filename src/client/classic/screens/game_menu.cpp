// Game Menu (F2) and the save-file windows: Save Game, Load Game and
// Delete Game (docs/spec/06 §1.1-§1.2).

#include "client/audio.hpp"
#include "client/classic/mods_model.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/file_dialog.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/pointers.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"
#include "game/classic_files.hpp"
#include "game/map_file.hpp"
#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

const ImVec4 kErrorText{1.0f, 0.5f, 0.45f, 1.0f};
const ImVec4 kGoodText{0.5f, 0.9f, 0.5f, 1.0f};

using SaveFile = FileEntry;

std::string formatTime(std::filesystem::file_time_type t) { return fileDate(t); }

// Characters allowed in a save name (it becomes a file name).
std::string cleanName(std::string_view in) {
    std::string out;
    for (char c : in)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')') out += c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// Yes/No popup: true for Yes, false for No, nothing while it waits (or is closed).
// Under -nd (openBox) it answers No at once, without showing (spec 05 §9.2).
std::optional<bool> answerPopup(UiContext& ui, const char* id, const std::string& question) {
    if (boxSkipped(id)) return false;
    std::optional<bool> answer;
    ImGui::SetNextWindowSize(ui.size({340, 0}));
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) {
        ImGui::TextWrapped("%s", question.c_str());
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        // Y means Yes; N, Esc and Enter mean No (spec 06 §3.4).
        const std::optional<bool> key = yesNoKey();
        if (ImGui::Button("Yes", ImVec2(w, ui.px(26))) || key == true) answer = true;
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(w, ui.px(26))) || key == false) answer = answer.value_or(false);
        if (answer) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return answer;
}

// Yes/No confirmation popup; returns true when Yes was chosen.
bool confirmPopup(UiContext& ui, const char* id, const std::string& question) { return answerPopup(ui, id, question) == true; }

// ---- Game Menu -----------------------------------------------------------------------------

class GameMenuScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        // The original's 173×320 menu: a column of 150×26 buttons, one every
        // 30 px, in the pipe frame (spec 06 §1.2). OpenSE4's Learn button sits
        // in a small frame of its own just below it.
        const Vec2 menu{173, 320};
        const Vec2 extra{173, 50};
        const Vec2 size{menu.x, menu.y + 6 + extra.y};
        const Vec2 min{(frameW() - menu.x) * 0.5f, (frameH() - menu.y) * 0.5f};
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        bool keep = true;
        const bool open = ImGui::Begin("Game Menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ui.windowFlags());
        ImGui::PopStyleVar(2);
        if (open) {
            if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
            ui.tagWindow(ui.at(min), ui.at(min + size));
            drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), Rect{min, min + menu}, nullptr, 0);
            const Vec2 extraMin = min + Vec2{0, menu.y + 6};
            drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), Rect{extraMin, extraMin + extra}, nullptr, 0);
            int row = 0;
            auto button = [&](const char* label, bool enabled = true) {
                ImGui::SetCursorPos(ImVec2(ui.px(12), ui.px(12 + 30 * float(row++))));
                const bool clicked = classicButton(ui, label, {149, 26}, 0, false, enabled);
                if (clicked) audio().play("button");
                return clicked;
            };
            if (button("New")) openBox("New Game");
            if (button("Load")) {
                ui.open(ScreenId::LoadGame);
                keep = false;
            }
            if (button("Save Game")) {
                if (ui.session.pbemTurn()) {
                    // Play by e-mail: the turn so far, to finish later (spec 05 §9.2).
                    auto saved = ui.session.savePbemDraft();
                    draftNote_ = saved ? "Your turn so far is saved. Open the game file again to finish it."
                                       : std::format("The turn was not saved: {}", saved.error());
                    openBox("Turn Saved");
                } else {
                    ui.open(ScreenId::SaveGame);
                    keep = false;
                }
            }
            // Save Map: only when the game lets players save the map (spec 01 §2.2, §12).
            if (button("Save Map", ui.state().options.playersCanSaveMap)) {
                mapName_ = std::format("{} {}", ui.me().name, formatDate(ui.state().turn));
                mapNote_.clear();
                ImGui::OpenPopup("Save Map");
            }
            // Save Empire asks first whether the designs go with it (spec 06 §7 Q48).
            if (button("Save Empire")) openBox("Save Designs");
            // Always enabled; a game with a master password asks for it first (spec 06 §1.2.1).
            if (button("Players")) {
                if (ui.session.hasMasterPassword()) {
                    masterInput_.clear();
                    ImGui::OpenPopup("Enter Game Master Password");
                } else {
                    openPlayers(ui);
                }
            }
            // The per-computer Options window (spec 06 §1.9); Empire Options opens
            // from Empire Status. OpenSE4's graphics and controls open from Options.
            if (button("Options")) {
                ui.open(ScreenId::Options);
                keep = false;
            }
            if (button("Delete Game")) {
                ui.open(ScreenId::LoadGame, ScreenArgs{.index = 1});
                keep = false;
            }
            if (button("Quit")) ImGui::OpenPopup("Quit Game");
            if (button("Close")) keep = false;
            // Tutorials, training games and the manual (docs/LEARNING.md): an
            // OpenSE4 extension below the original's ten buttons.
            ImGui::SetCursorPos(ImVec2(ui.px(12), ui.px(menu.y + 6 + 12)));
            if (classicButton(ui, "Learn", {149, 26}, 0, false, ui.learn != nullptr)) {
                audio().play("button");
                ui.open(ScreenId::Learn);
                keep = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
                keep = false;

            // New asks whether to cancel this game for a new one; Yes opens the
            // Game Setup at once (its Cancel goes to the intro) (spec 06 §1.2,
            // confirmed: binary).
            if (confirmPopup(ui, "New Game", "Cancel this game and set up a new one? Anything not saved is lost.")) ui.requests.newGame = true;
            if (confirmPopup(ui, "Quit Game", "Quit OpenSE4? Anything not saved is lost.")) ui.requests.quitGame = true;
            masterPasswordPopup(ui);
            notePopup(ui, "Invalid Password", masterNote_);
            playersPopup(ui);
            saveMapPopup(ui);
            draftPopup(ui);
            if (const auto designs = answerPopup(ui, "Save Designs", "Save the empire's designs with it?")) {
                saveEmpire(ui, *designs);
                ImGui::OpenPopup("Save Empire");
            }
            saveEmpirePopup(ui);
        }
        ImGui::End();
        return keep;
    }

private:
    // Writes the current quadrant, with the starting points the game still holds
    // (spec 01 §12: a loaded map's specific and unused common points; none for a
    // generated game), to <user data>/maps (our map format, docs/MAPS.md).
    void saveMapPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({380, 0}));
        if (!ImGui::BeginPopupModal("Save Map", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        const std::filesystem::path dir = userDataDir() / "maps";
        ImGui::TextColored(kLabelBlue, "Map name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##mapname", mapName_, 60, ImGuiInputTextFlags_EnterReturnsTrue);
        const std::filesystem::path file = setup::mapFilePath(dir, mapName_);
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) wrappedDim(std::format("{} exists and will be replaced.", file.filename().string()).c_str());
        if (!mapNote_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(mapSaved_ ? kGoodText : kErrorText, "%s", mapNote_.c_str());
            script::reportItem(mapSaved_ ? "result:saved" : "result:failed", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());   // for input scripts
            ImGui::PopTextWrapPos();
        }
        // Save for SE IV: the original's map file (docs/spec/08 §14), in a folder of the player's.
        ImGui::TextColored(kLabelBlue, "Folder for SE IV");
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputString("##classicmaps", classicMapsFolder(), 1024);
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
        if (ImGui::Button("Save", ImVec2(w, ui.px(26))) || enter) {
            const auto saved = game::saveMapFile(file, ui.session.rules().data(), game::mapOfGame(ui.state(), mapName_));
            mapSaved_ = saved.has_value();
            mapNote_ = saved ? std::format("Saved to {}", file.string()) : saved.error();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save for SE IV", ImVec2(w, ui.px(26)))) {
            const BusyPointer busy;
            const std::filesystem::path classicFile =
                std::filesystem::path(classicMapsFolder()) / game::classic::classicFileName(mapName_, game::classic::kMapFileExtension);
            game::classic::ConversionReport report;
            const auto saved = game::classic::writeClassicMap(ui.session.rules().data(), game::mapOfGame(ui.state(), mapName_), classicFile, report,
                                                              std::random_device{}());
            mapSaved_ = saved.has_value();
            mapNote_ = saved ? setup::withNotes(std::format("Saved for Space Empires IV to {}. Copy it into the original's Maps folder to load it there.",
                                                            classicFile.string()),
                                                report.notes)
                             : saved.error();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // The folders Save for SE IV writes the original's maps and empire files
    // to: OpenSE4's own maps and empires folders until another is typed, kept
    // for the session.
    static std::string& classicMapsFolder() {
        static std::string dir = (userDataDir() / "maps").string();
        return dir;
    }
    static std::string& classicEmpiresFolder() {
        static std::string dir = (userDataDir() / "empires").string();
        return dir;
    }

    // Save Empire (spec 06 §6.1, §7 Q48, Q72, spec 08 §13.2): the empire's
    // name, leader, race, password, e-mail, minister style and settings,
    // experience, combat strategies and name lists, and with `designs` every
    // design of the player's (each losing its obsolete and built marks,
    // keeping its strategy and creation date), as an empire file for a later
    // game's setup (our format, named after the empire, under <user
    // data>/empires); the game state is not kept. Save for SE IV in the
    // window that follows writes it as the original's empire file too.
    void saveEmpire(UiContext& ui, bool designs) {
        const game::Empire& e = ui.me();
        empireCopy_ = setup::savedCopy(setup::empireOfGame(ui.state(), e.id, designs), designs);
        empireDesigns_ = designs;
        empirePlayer_ = static_cast<uint8_t>(std::min<size_t>(e.id.index() + 1, 20));
        classicEmpireName_ = e.name;
        classicEmpireNote_.clear();
        const BusyPointer busy;
        const auto saved = setup::saveEmpireFile(ui.rules(), userDataDir() / "empires", empireCopy_);
        empireNote_ = saved ? std::format("The {} empire is saved as {}{}. New games can use it in the empire setup.", e.name, saved->string(),
                                          designs ? std::format(" with {} designs", empireCopy_.designs.size()) : std::string{})
                            : std::format("The empire was not saved: {}", saved.error());
    }

    void saveEmpirePopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({420, 0}));
        if (!ImGui::BeginPopupModal("Save Empire", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) return;
        ImGui::TextWrapped("%s", empireNote_.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        // Save for SE IV: the original's empire file (docs/spec/08 §13), in a folder of the player's.
        wrappedDim("Save for SE IV also writes the empire as an empire file of the original Space Empires IV, for its Add Existing.");
        ImGui::TextColored(kLabelBlue, "Folder");
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputString("##classicempires", classicEmpiresFolder(), 1024);
        ImGui::TextColored(kLabelBlue, "File name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputString("##classicempire", classicEmpireName_, 80);
        const std::string fileName = game::classic::classicFileName(classicEmpireName_, game::classic::kEmpireFileExtension);
        wrappedDim(fileName.c_str());
        if (!classicEmpireNote_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(classicEmpireSaved_ ? kGoodText : kErrorText, "%s", classicEmpireNote_.c_str());
            script::reportItem(classicEmpireSaved_ ? "result:saved" : "result:failed", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());   // for input scripts
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Save for SE IV", ImVec2(w, ui.px(26)))) {
            const BusyPointer busy;
            const std::filesystem::path file = std::filesystem::path(classicEmpiresFolder()) / fileName;
            game::classic::ConversionReport report;
            game::classic::EmpireExportOptions options;
            options.keySeed = std::random_device{}();
            options.player = empirePlayer_;
            options.designs = empireDesigns_;
            const auto saved = game::classic::writeClassicEmpire(ui.rules(), empireCopy_, file, report, options);
            classicEmpireSaved_ = saved.has_value();
            classicEmpireNote_ = saved ? setup::withNotes(std::format("Saved for Space Empires IV as {}. Copy it into the original's Empires folder to add "
                                                                      "it there.",
                                                                      file.string()),
                                                          report.notes)
                                       : std::format("Not saved: {}", saved.error());
        }
        ImGui::SameLine();
        if (ImGui::Button("OK", ImVec2(w, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void notePopup(UiContext& ui, const char* id, const std::string& note) {
        if (boxSkipped(id)) return skippedBox(id, note);   // -nd (spec 05 §9.2)
        ImGui::SetNextWindowSize(ui.size({380, 0}));
        if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) return;
        ImGui::TextWrapped("%s", note.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    std::string mapName_;
    std::string mapNote_;
    std::string draftNote_;
    std::string empireNote_;
    bool mapSaved_ = false;
    game::EmpireSetup empireCopy_;   // the empire Save Empire saved, for Save for SE IV
    bool empireDesigns_ = false;
    uint8_t empirePlayer_ = 1;
    std::string classicEmpireName_;
    std::string classicEmpireNote_;
    bool classicEmpireSaved_ = false;

    void draftPopup(UiContext& ui) {
        if (boxSkipped("Turn Saved")) return skippedBox("Turn Saved", draftNote_);   // -nd (spec 05 §9.2)
        ImGui::SetNextWindowSize(ui.size({380, 0}));
        if (!ImGui::BeginPopupModal("Turn Saved", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped("%s", draftNote_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Player Computer Control (spec 06 §1.2.1, confirmed: binary) ----

    void masterPasswordPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({340, 0}));
        if (!ImGui::BeginPopupModal("Enter Game Master Password", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##master", masterInput_, 64, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        bool ok = ImGui::Button("OK", ImVec2(w, ui.px(26))) || enter;
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        bool open = false, invalid = false;
        if (ok) {
            // An exact comparison: letter case and spaces count.
            const std::expected<bool, std::string> matches = ui.session.masterPasswordMatches(masterInput_);
            if (matches && *matches) open = true;
            else invalid = true;
            masterNote_ = matches ? std::string("The Game Master password is not correct.") : matches.error();
        }
        if (ok || cancel) {
            masterInput_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        if (open) openPlayers(ui);
        if (invalid) openBox("Invalid Password");
    }

    void openPlayers(UiContext& ui) {
        const game::GameState& s = ui.state();
        lamps_.clear();
        clicked_.assign(s.empires.size(), 0);
        for (const game::Empire& e : s.empires) lamps_.push_back(e.kind != game::PlayerKind::Human);
        ImGui::OpenPopup("Empires Under Computer Control");
    }

    // One row per empire in empire-number order, neutral and destroyed ones
    // included; a lit lamp means computer-controlled. A click flips the lamp;
    // OK applies every row clicked at least once, with its final state, even
    // one clicked back; Cancel applies nothing. A neutral empire switches like
    // any other and stays neutral (spec 06 §7 Q84, confirmed: binary).
    void playersPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({420, 0}));
        if (!ImGui::BeginPopupModal("Empires Under Computer Control", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
            return;
        const game::GameState& s = ui.state();
        if (lamps_.size() != s.empires.size()) openPlayers(ui);
        ImGui::BeginChild("##empires", ImVec2(0, ui.px(std::min(380.0f, 26.0f * float(s.empires.size()) + 8.0f))));
        for (const game::Empire& e : s.empires) {
            const size_t i = e.id.index();
            ImGui::PushID(int(i));
            bool on = lamps_[i] != 0;
            const bool neutral = game::isNeutral(e);
            if (lampToggle(ui, "##lamp", &on)) {
                lamps_[i] = on;
                clicked_[i] = 1;
            }
            ImGui::SameLine();
            if (Sprite flag = ui.art.flag(e.race.style, false)) {
                image(ui, flag, {14, 10});
                ImGui::SameLine();
            }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e.id)), "%s", e.name.c_str());
            ImGui::SameLine(ui.px(280));
            dimText(neutral ? "Neutral" : !e.alive ? "Eliminated" : e.id == ui.session.player() ? "You" : "");
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (ui.session.kind() == SessionKind::NetworkClient || ui.session.kind() == SessionKind::Pbem)
            wrappedDim("This changes only your copy of the game. Your own empire's minister switches go to the host with your orders.");
        ImGui::Spacing();
        // A check list: no keys at all (spec 06 §3.4).
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("OK", ImVec2(w, ui.px(26)))) {
            std::vector<std::pair<game::EmpireId, bool>> rows;
            for (size_t i = 0; i < clicked_.size(); ++i)
                if (clicked_[i]) rows.emplace_back(game::EmpireId{static_cast<uint32_t>(i)}, lamps_[i] != 0);
            ui.session.setComputerControl(rows);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, ui.px(26)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    std::vector<uint8_t> lamps_;
    std::vector<uint8_t> clicked_;
    std::string masterInput_;
    std::string masterNote_ = "The Game Master password is not correct.";
};

// Whether the installed game itself (not a mod) has a ship picture of this
// base name, for one of the game's races or the generic one.
bool installHasShipPicture(UiContext& ui, std::string_view name) {
    const assets::InstallFiles& files = ui.art.files();
    if (files.findInstalledPicture(std::format("Pictures/RaceGeneric/Generic_Mini_{}.bmp", name))) return true;
    for (const game::Empire& e : ui.state().empires)
        for (std::string_view folder : {"Races", "RaceNeutral"})
            if (!e.race.style.empty() && files.findInstalledPicture(std::format("Pictures/{}/{}/{}_Mini_{}.bmp", folder, e.race.style, e.race.style, name)))
                return true;
    return false;
}

// ---- Save Game -------------------------------------------------------------------------------

// OpenSE4's own Save Game window, with the original's Change Directory (here
// Change Folder: this window's saves go to another folder) and its refusal
// of a name that cannot be a file name ("Invalid File Name.", titled
// "Error"); the overwrite question is ours (spec 06 §1.2).
class SaveGameScreen final : public Screen {
public:
    explicit SaveGameScreen(std::string name) : name_(std::move(name)) {}

    bool draw(UiContext& ui) override {
        Dialog d(ui, "Save Game", DialogSize::Picker, 150);
        if (!d.open()) return d.keepOpen();
        if (name_.empty()) name_ = std::format("{} {}", ui.me().name, formatDate(ui.state().turn));
        if (folder_.empty()) folder_ = savesDir();
        if (saves_.empty() && !listed_) {
            saves_ = listFiles(folder_, ".gam");
            listed_ = true;
        }
        d.beginContent();
        ImGui::TextColored(kLabelBlue, "Save as");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##name", name_, 80, ImGuiInputTextFlags_EnterReturnsTrue);
        if (!error_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kErrorText, "%s", error_.c_str());
            ImGui::PopTextWrapPos();
        }
        if (!saved_.empty()) ImGui::TextColored(kGoodText, "Saved as %s", saved_.c_str());
        if (folder_ != savesDir()) wrappedDim(std::format("Saving in {}", folder_.string()).c_str());
        ImGui::Spacing();
        ImGui::TextColored(kLabelBlue, "Saved games (click one to reuse its name)");
        beginList(ui, "##saves", ImVec2(0, 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        for (const SaveFile& f : saves_) {
            if (ImGui::Selectable(f.name.c_str(), f.name == name_)) name_ = f.name;
            ImGui::SameLine(ImGui::GetWindowWidth() - ui.px(130));
            dimText(formatTime(f.modified).c_str());
        }
        if (saves_.empty()) dimText("No saved games yet.");
        endList(ui);

        d.beginButtons();
        const std::string typed = trimmedSaveName(name_);
        if (d.button("Save", !typed.empty()) || (enter && !typed.empty())) {
            if (!validSaveName(typed)) {
                openBox("Error");
            } else {
                // A save whose name differs only in case is the same game, as on Windows.
                const std::filesystem::path file = ruleset::childIgnoringCase(folder_, typed + ".gam");
                if (std::filesystem::exists(file)) openBox("Overwrite");
                else save(ui, file, typed);
            }
        }
        if (confirmPopup(ui, "Overwrite", std::format("A saved game named \"{}\" exists. Replace it?", typed)))
            save(ui, ruleset::childIgnoringCase(folder_, typed + ".gam"), typed);
        notePopup(ui, "Error", "Invalid File Name.");
        // The original's Change Directory (its whole caption does not fit our
        // button): this window's saves go to another folder; the saves folder
        // stays the default for the next one.
        if (d.button("Change Folder")) {
            folderField_ = folder_.string();
            folderError_.clear();
            ImGui::OpenPopup("Change Directory");
        }
        folderPopup(ui);
        const std::string clean = cleanName(name_);
        // A saved game for the original's Load Game (docs/spec/08 §7), in a folder of the player's.
        const bool whole = ui.session.kind() == SessionKind::Local || ui.session.kind() == SessionKind::Hotseat;
        if (d.button("Save for SE IV", whole && !clean.empty())) {
            classicName_ = clean;
            classicFolder_ = classicSavesDirectory().string();
            classicNotes_.clear();
            classicResult_.clear();
            ImGui::OpenPopup("Save for Space Empires IV");
        }
        classicPopup(ui);
        d.close();
        return d.keepOpen();
    }

private:
    // The folder the original's saves are written to: "Space Empires IV" in
    // the saves folder (the player's user data, never the installation) until
    // another is typed, kept for the session.
    static std::filesystem::path& classicSavesDirectory() {
        // Following the saves folder while it was not changed (as loadGameDirectory).
        static std::filesystem::path dir, from;
        const std::filesystem::path now = savesDir() / "Space Empires IV";
        if (dir.empty() || (dir == from && now != from)) dir = now;
        from = now;
        return dir;
    }

    void classicPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({460, 0}));
        if (!ImGui::BeginPopupModal("Save for Space Empires IV", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("Writes this game as a saved game of the original Space Empires IV, for its Load Game. "
                               "The game here, and its OpenSE4 saves, stay as they are.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::TextColored(kLabelBlue, "Folder");
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputString("##classicfolder", classicFolder_, 1024);
        ImGui::TextColored(kLabelBlue, "File name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##classicname", classicName_, 80, ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string name = cleanName(classicName_);
        const std::filesystem::path file = std::filesystem::path(classicFolder_) / (name + ".gam");
        std::error_code ec;
        if (!name.empty() && std::filesystem::exists(file, ec)) wrappedDim(std::format("{} exists and will be replaced.", file.filename().string()).c_str());
        if (!classicResult_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(classicSaved_ ? kGoodText : kErrorText, "%s", classicResult_.c_str());
            script::reportItem(classicSaved_ ? "result:saved" : "result:failed", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());   // for input scripts
            for (const std::string& note : classicNotes_) {
                ImGui::Bullet();
                ImGui::TextUnformatted(note.c_str());
            }
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if ((ImGui::Button("Save", ImVec2(w, ui.px(26))) || enter) && !name.empty() && !classicFolder_.empty()) {
            const BusyPointer busy;
            // What the game's mods hold that the original cannot (docs/sdk/packages-and-data.md "Saving for the original").
            auto notes = ui.session.exportClassic(file, scriptedMods(loadedMods().packages),
                                                  [&ui](std::string_view picture) { return installHasShipPicture(ui, picture); });
            classicSaved_ = notes.has_value();
            if (notes) {
                classicSavesDirectory() = classicFolder_;
                classicResult_ = std::format("Saved as {}. Copy it to the original's SaveGame folder, or open this folder with "
                                             "its Change Directory, to load it there. Not carried over:",
                                             file.string());
                classicNotes_ = std::move(*notes);
            } else {
                classicResult_ = "Not saved: " + notes.error();
                classicNotes_.clear();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void notePopup(UiContext& ui, const char* id, const std::string& note) {
        if (boxSkipped(id)) return skippedBox(id, note);   // -nd (spec 05 §9.2)
        ImGui::SetNextWindowSize(ui.size({300, 0}));
        if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) return;
        ImGui::TextWrapped("%s", note.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // The folder typed in, as Load Game's Change Directory asks for it.
    void folderPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({400, 0}));
        if (!ImGui::BeginPopupModal("Change Directory", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextColored(kLabelBlue, "Folder");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##savefolder", folderField_, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
        if (!folderError_.empty()) ImGui::TextColored(kErrorText, "%s", folderError_.c_str());
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
        if (ImGui::Button("OK", ImVec2(w, ui.px(26))) || enter) {
            std::error_code ec;
            const std::filesystem::path dir(folderField_);
            if (std::filesystem::is_directory(dir, ec)) {
                folder_ = dir;
                saves_ = listFiles(folder_, ".gam");
                listed_ = true;
                ImGui::CloseCurrentPopup();
            } else {
                folderError_ = "There is no such folder.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Default", ImVec2(w, ui.px(26)))) folderField_ = savesDir().string();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    std::filesystem::path folder_;
    std::string folderField_, folderError_;
    std::string classicName_;
    std::string classicFolder_;
    std::string classicResult_;
    std::vector<std::string> classicNotes_;
    bool classicSaved_ = false;

    void save(UiContext& ui, const std::filesystem::path& file, const std::string& name) {
        const BusyPointer busy;  // the Hourglass while it saves (spec 06 §5.8)
        const auto result = ui.session.save(file, name);
        if (result) {
            // The players' History files go beside the save; the session has
            // made it the game Resume Game loads (docs/spec/06 §6.1).
            if (ui.session.kind() == SessionKind::Local || ui.session.kind() == SessionKind::Hotseat) copyHistoryNextTo(file);
            error_.clear();
            saved_ = name;
            saves_ = listFiles(folder_, ".gam");
        } else {
            error_ = "Could not save: " + result.error();
            saved_.clear();
        }
    }

    std::string name_;
    std::string error_;
    std::string saved_;
    std::vector<SaveFile> saves_;
    bool listed_ = false;
};

// ---- Load Game / Delete Game ---------------------------------------------------------------------

// Load Game and Delete Game: the original's Load Game dialog (spec 07
// session 5), the one the intro's Load Game shows (screens/file_dialog.hpp).
class LoadGameScreen final : public Screen {
public:
    LoadGameScreen(bool deleteMode, const std::string& loadError)
        : delete_(deleteMode),
          dialog_(deleteMode ? "Delete Game" : "Load Game", "Save Game Name", ".gam", loadGameDirectory(), savesDir()) {
        if (!loadError.empty()) dialog_.setError("Could not load the game: " + loadError);
    }

    bool draw(UiContext& ui) override {
        bool keep = true;
        const char* window = delete_ ? "Delete Game###files" : "Load Game###files";
        const FileDialog::Result r = dialog_.draw(ui.painter(), window, &ui);
        if (r == FileDialog::Result::Cancelled) {
            keep = false;
        } else if (r == FileDialog::Result::Chosen) {
            if (delete_) {
                pending_ = dialog_.chosen();
                ask_ = true;
            } else {
                ui.requests.loadGame = dialog_.chosen().path;
                keep = false;
            }
        }
        if (ask_) {
            openBox("Delete");
            ask_ = false;
        }
        if (pending_ && confirmPopup(ui, "Delete", std::format("Delete the saved game \"{}\"?", pending_->name))) {
            std::error_code ec;
            std::filesystem::remove(pending_->path, ec);
            dialog_.setError(ec ? "Could not delete: " + ec.message() : std::string{});
            dialog_.rescan();
            pending_.reset();
        }
        return keep;
    }

private:
    bool delete_ = false;
    FileDialog dialog_;
    std::optional<FileEntry> pending_;
    bool ask_ = false;
};

} // namespace

std::unique_ptr<Screen> makeGameMenu(const ScreenArgs&) { return std::make_unique<GameMenuScreen>(); }
std::unique_ptr<Screen> makeSaveGame(const ScreenArgs& args) { return std::make_unique<SaveGameScreen>(args.text); }
std::unique_ptr<Screen> makeLoadGame(const ScreenArgs& args) { return std::make_unique<LoadGameScreen>(args.index == 1, args.text); }

} // namespace opense4::client::classic
