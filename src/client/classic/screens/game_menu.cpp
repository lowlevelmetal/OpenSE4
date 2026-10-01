// Game Menu (F2) and the save-file windows: Save Game, Load Game and
// Delete Game (docs/spec/06 §1.1-§1.2).

#include "client/audio.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "game/map_file.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>

namespace opense4::client::classic {

namespace {

const ImVec4 kErrorText{1.0f, 0.5f, 0.45f, 1.0f};
const ImVec4 kGoodText{0.5f, 0.9f, 0.5f, 1.0f};

struct SaveFile {
    std::filesystem::path path;
    std::string name;
    std::filesystem::file_time_type modified;
};

std::vector<SaveFile> listSaves() {
    std::vector<SaveFile> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(savesDir(), ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".gam") continue;
        out.push_back({entry.path(), entry.path().stem().string(), entry.last_write_time(ec)});
    }
    std::sort(out.begin(), out.end(), [](const SaveFile& a, const SaveFile& b) { return a.modified > b.modified; });
    return out;
}

std::string formatTime(std::filesystem::file_time_type t) {
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

// Characters allowed in a save name (it becomes a file name).
std::string cleanName(std::string_view in) {
    std::string out;
    for (char c : in)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')') out += c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// Yes/No confirmation popup; returns true when Yes was chosen.
bool confirmPopup(UiContext& ui, const char* id, const std::string& question) {
    bool yes = false;
    ImGui::SetNextWindowSize(ui.size({340, 0}));
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) {
        ImGui::TextWrapped("%s", question.c_str());
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        // Y means Yes; N, Esc and Enter mean No (spec 06 §3.4).
        const std::optional<bool> key = yesNoKey();
        if (ImGui::Button("Yes", ImVec2(w, ui.px(26))) || key == true) {
            yes = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(w, ui.px(26))) || key == false) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return yes;
}

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
        const Vec2 min{(kFrameW - menu.x) * 0.5f, (kFrameH - menu.y) * 0.5f};
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        bool keep = true;
        const bool open = ImGui::Begin("Game Menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
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
            if (button("New")) ImGui::OpenPopup("New Game");
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
                    ImGui::OpenPopup("Turn Saved");
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
            if (button("Save Empire")) {
                saveEmpire(ui);
                ImGui::OpenPopup("Save Empire");
            }
            if (button("Players")) ImGui::OpenPopup("Player Computer Control");
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

            if (confirmPopup(ui, "New Game", "Leave this game and return to the title screen? Anything not saved is lost."))
                ui.requests.quitToIntro = true;
            if (confirmPopup(ui, "Quit Game", "Quit OpenSE4? Anything not saved is lost.")) ui.requests.quitGame = true;
            playersPopup(ui);
            saveMapPopup(ui);
            draftPopup(ui);
            notePopup(ui, "Save Empire", empireNote_);
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
        const std::filesystem::path file = dir / (game::mapFileStem(mapName_) + std::string(game::kMapExtension));
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) wrappedDim(std::format("{} exists and will be replaced.", file.filename().string()).c_str());
        if (!mapNote_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(mapSaved_ ? kGoodText : kErrorText, "%s", mapNote_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Save", ImVec2(w, ui.px(26))) || enter) {
            const auto saved = game::saveMapFile(file, ui.session.rules().data(), game::mapOfGame(ui.state(), mapName_));
            mapSaved_ = saved.has_value();
            mapNote_ = saved ? std::format("Saved to {}", file.string()) : saved.error();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Save Empire (spec 06 §6.1): the empire's name, leader, race and minister
    // style as an empire file for a later game's setup (our format, under
    // <user data>/empires). Our empire files hold no designs, so the original's
    // question whether to include them is not asked.
    void saveEmpire(UiContext& ui) {
        const game::Empire& e = ui.me();
        game::EmpireSetup out;
        out.name = e.name;
        out.empireType = e.empireType;
        out.leaderTitle = e.leaderTitle;
        out.leaderName = e.leaderName;
        out.customRace = e.race;
        out.ministerStyle = e.ministerStyle;
        out.useRaceMinisterStyle = e.useRaceMinisterStyle;
        out.experience = e.experience;
        const auto saved = setup::saveEmpireFile(ui.rules(), userDataDir() / "empires", out);
        empireNote_ = saved ? std::format("The {} empire is saved as {}. New games can use it in the empire setup.", e.name, saved->string())
                            : std::format("The empire was not saved: {}", saved.error());
    }

    void notePopup(UiContext& ui, const char* id, const std::string& note) {
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

    void draftPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({380, 0}));
        if (!ImGui::BeginPopupModal("Turn Saved", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped("%s", draftNote_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void playersPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({420, 0}));
        if (!ImGui::BeginPopupModal("Player Computer Control", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        const game::GameState& s = ui.state();
        ImGui::TextColored(kLabelBlue, "A lit lamp means the computer plays that empire.");
        ImGui::Spacing();
        for (const game::Empire& e : s.empires) {
            if (e.kind == game::PlayerKind::Neutral) continue;
            ImGui::PushID(int(e.id.index()));
            lamp(ui, e.kind == game::PlayerKind::Computer);
            ImGui::SameLine();
            if (Sprite flag = ui.art.flag(e.race.style, false)) {
                image(ui, flag, {14, 10});
                ImGui::SameLine();
            }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e.id)), "%s", e.name.c_str());
            ImGui::SameLine(ui.px(260));
            dimText(!e.alive ? "Eliminated" : e.id == ui.session.player() ? "You" : e.kind == game::PlayerKind::Computer ? "Computer" : "Human");
            ImGui::PopID();
        }
        ImGui::Spacing();
        wrappedDim("Handing an empire to the computer or taking it back is not possible yet in this version.");
        ImGui::Spacing();
        // A check list: no keys at all (spec 06 §3.4).
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
};

// ---- Save Game -------------------------------------------------------------------------------

class SaveGameScreen final : public Screen {
public:
    explicit SaveGameScreen(std::string name) : name_(std::move(name)) {}

    bool draw(UiContext& ui) override {
        Dialog d(ui, "Save Game", DialogSize::Picker, 150);
        if (!d.open()) return d.keepOpen();
        if (name_.empty()) name_ = std::format("{} {}", ui.me().name, formatDate(ui.state().turn));
        if (saves_.empty() && !listed_) {
            saves_ = listSaves();
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
        ImGui::Spacing();
        ImGui::TextColored(kLabelBlue, "Saved games (click one to reuse its name)");
        ImGui::BeginChild("##saves", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (const SaveFile& f : saves_) {
            if (ImGui::Selectable(f.name.c_str(), f.name == name_)) name_ = f.name;
            ImGui::SameLine(ImGui::GetWindowWidth() - ui.px(130));
            dimText(formatTime(f.modified).c_str());
        }
        if (saves_.empty()) dimText("No saved games yet.");
        ImGui::EndChild();

        d.beginButtons();
        const std::string clean = cleanName(name_);
        if (d.button("Save", !clean.empty()) || (enter && !clean.empty())) {
            const std::filesystem::path file = savesDir() / (clean + ".gam");
            if (std::filesystem::exists(file)) ImGui::OpenPopup("Overwrite");
            else save(ui, file, clean);
        }
        if (confirmPopup(ui, "Overwrite", std::format("A saved game named \"{}\" exists. Replace it?", clean))) save(ui, savesDir() / (clean + ".gam"), clean);
        d.close();
        return d.keepOpen();
    }

private:
    void save(UiContext& ui, const std::filesystem::path& file, const std::string& name) {
        const auto result = ui.session.save(file, name);
        if (result) {
            // The players' History files go beside the save; the session has
            // made it the game Resume Game loads (docs/spec/06 §6.1).
            if (ui.session.kind() == SessionKind::Local || ui.session.kind() == SessionKind::Hotseat) copyHistoryNextTo(file);
            error_.clear();
            saved_ = name;
            saves_ = listSaves();
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

class LoadGameScreen final : public Screen {
public:
    LoadGameScreen(bool deleteMode, const std::string& loadError)
        : delete_(deleteMode), error_(loadError.empty() ? std::string{} : "Could not load the game: " + loadError) {}

    bool draw(UiContext& ui) override {
        const char* title = delete_ ? "Delete Game###files" : "Load Game###files";
        Dialog d(ui, title, DialogSize::Picker, 150);
        if (!d.open()) return d.keepOpen();
        if (!listed_) {
            saves_ = listSaves();
            listed_ = true;
        }
        bool keep = true;
        bool askDelete = false;
        d.beginContent();
        ImGui::TextColored(kLabelBlue, "%s", delete_ ? "Click a saved game to delete it." : "Click a saved game to load it.");
        if (!error_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kErrorText, "%s", error_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::BeginChild("##list", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (ImGui::BeginTable("##saves", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("Last saved", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < saves_.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(i));
                if (ImGui::Selectable(saves_[i].name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (delete_) {
                        pending_ = i;
                        askDelete = true;
                    } else {
                        ui.requests.loadGame = saves_[i].path;
                        keep = false;
                    }
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                dimText(formatTime(saves_[i].modified).c_str());
            }
            ImGui::EndTable();
        }
        if (saves_.empty()) dimText("There are no saved games.");
        ImGui::EndChild();
        if (askDelete) ImGui::OpenPopup("Delete");
        if (pending_ < saves_.size() && confirmPopup(ui, "Delete", std::format("Delete the saved game \"{}\"?", saves_[pending_].name))) {
            std::error_code ec;
            std::filesystem::remove(saves_[pending_].path, ec);
            error_ = ec ? "Could not delete: " + ec.message() : std::string{};
            saves_ = listSaves();
            pending_ = SIZE_MAX;
        }
        d.beginButtons();
        dimText(std::format("{} saved games", saves_.size()).c_str());
        d.close();
        return keep && d.keepOpen();
    }

private:
    bool delete_ = false;
    std::string error_;
    std::vector<SaveFile> saves_;
    bool listed_ = false;
    size_t pending_ = SIZE_MAX;
};

} // namespace

std::unique_ptr<Screen> makeGameMenu(const ScreenArgs&) { return std::make_unique<GameMenuScreen>(); }
std::unique_ptr<Screen> makeSaveGame(const ScreenArgs& args) { return std::make_unique<SaveGameScreen>(args.text); }
std::unique_ptr<Screen> makeLoadGame(const ScreenArgs& args) { return std::make_unique<LoadGameScreen>(args.index == 1, args.text); }

} // namespace opense4::client::classic
