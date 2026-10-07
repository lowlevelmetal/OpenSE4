// Settings → Files: where OpenSE4 keeps the player's own files (docs/SETUP.md
// "Where OpenSE4 keeps its own files"): the user folder in use and the rule
// that chose it, keeping the files in OpenSE4's own folder (a portable copy,
// core/user_folder.hpp), and the folder of saved games.

#include "client/app_settings.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"
#include "core/log.hpp"
#include "core/user_folder.hpp"
#include "game/rules.hpp"
#include "ruleset/files.hpp"

#include <imgui.h>
#include <SDL3/SDL_misc.h>

#include <cfloat>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

namespace opense4::client::classic {

namespace {

namespace fs = std::filesystem;

const ImVec4 kHeading{0.44f, 0.61f, 1.0f, 1.0f};
const ImVec4 kNote{1, 0.85f, 0.45f, 1};
const ImVec4 kProblem{1, 0.55f, 0.45f, 1};
constexpr const char* kPortableLabel = "Keep saves and settings in OpenSE4's folder";
constexpr const char* kSwitchPopup = "Move OpenSE4's Files";
constexpr const char* kRefusalPopup = "OpenSE4's Folder Cannot Be Used";

// What the page remembers while the Settings window is open (one at a time).
struct FilesPage {
    bool portableWanted = false;     // the question asked: on (true) or off
    fs::path from, to;               // its folders: the files now, and then
    std::string refusal;             // why OpenSE4's folder cannot be used
    std::string savesInput;          // the saves folder field
    bool savesInputSet = false;
    std::string message;             // what the last change did
    bool problem = false;            // ... and whether it went wrong
};

FilesPage& state() {
    static FilesPage p;
    return p;
}

std::string text(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// A file:// address for the system's file manager (UTF-8, percent-encoded).
std::string fileUrl(const fs::path& folder) {
    const std::u8string generic = folder.generic_u8string();
    std::string url = "file://";
    if (!generic.starts_with(u8"/")) url += '/';   // C:/Users/... on Windows
    for (const char8_t c8 : generic) {
        const auto c = static_cast<unsigned char>(c8);
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                           c == '~' || c == '/' || c == ':';
        url += plain ? std::string(1, static_cast<char>(c)) : std::format("%{:02X}", static_cast<unsigned>(c));
    }
    return url;
}

void wrapped(const ImVec4& color, const std::string& s, float px) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * px);
    ImGui::TextColored(color, "%s", s.c_str());
    ImGui::PopTextWrapPos();
}

void dimWrapped(const std::string& s, float px) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * px);
    ImGui::TextDisabled("%s", s.c_str());
    ImGui::PopTextWrapPos();
}

void section(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(kHeading, "%s", title);
    ImGui::Separator();
}

// The installed game's folder, where OpenSE4 never writes.
fs::path gameFolder(const game::Rules* rules) {
    if (!rules) return {};
    const ruleset::Ruleset& data = rules->data();
    if (data.files && !data.files->root().empty()) return data.files->root();
    return data.dataDir.empty() ? fs::path() : data.dataDir.parent_path();
}

std::string refusalText(const std::string& problem) {
    return std::format("OpenSE4 cannot keep its files in its own folder: it {}.\n\n"
                       "A copy installed with the Windows installer (in Program Files) or by a package manager lives in a "
                       "folder that belongs to the system. For a portable copy, unpack OpenSE4's zip file (Windows) or tarball "
                       "(Linux) into a folder of your own, such as one on a USB stick, and play from there.\n\n"
                       "To keep only your saved games somewhere else, choose a Saves folder on this page.",
                       problem);
}

// Moves to the new user folder: the files copied first if asked, then the
// marker made or removed, then the settings in use written there.
void switchFolders(bool copy) {
    FilesPage& p = state();
    core::CopyReport report;
    if (copy) report = core::copyUserFiles(p.from, p.to);
    const fs::path program = core::programFolder();
    if (auto done = core::setPortable(program, p.portableWanted); !done) {
        p.message = std::format("Nothing was changed: {}.", done.error());
        p.problem = true;
        log::warn("Settings, Files: {}", done.error());
        return;
    }
    // The settings in use go on in the new folder, copied or not.
    saveAppSettings();
    saveSettings();
    p.message = p.portableWanted ? std::format("OpenSE4 now keeps its files in its own folder, {}.", text(p.to))
                                 : std::format("OpenSE4 now keeps its files in your user folder, {}.", text(p.to));
    if (copy) {
        p.message += std::format(" Files copied: {}.", report.copied);
        if (report.kept) p.message += std::format(" Files it had already, kept as they were: {}.", report.kept);
    }
    p.problem = report.failed > 0;
    if (report.failed) p.message += std::format(" {} could not be copied: {}.", report.failed, report.firstError);
    log::info("Settings, Files: the user folder is now {} (portable: {}); copied {}, kept {}, failed {}", text(p.to), p.portableWanted,
              report.copied, report.kept, report.failed);
}

void switchPopup(float px) {
    FilesPage& p = state();
    ImGui::SetNextWindowSize(ImVec2(560 * px, 0));
    if (!ImGui::BeginPopupModal(kSwitchPopup, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(0.0f);
    if (p.portableWanted)
        ImGui::TextUnformatted("From now on OpenSE4 keeps your saved games, settings, logs, mods and history in the folder "
                               "\"userdata\" beside the program, so that they go wherever its folder goes:");
    else
        ImGui::TextUnformatted("From now on OpenSE4 keeps your saved games, settings, logs, mods and history in your user "
                               "folder again:");
    ImGui::TextColored(kHeading, "%s", text(p.to).c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("Copy your files there from the folder used until now?");
    ImGui::TextColored(kHeading, "%s", text(p.from).c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Nothing is deleted or replaced: the files in the folder used until now stay as they are, and a file the "
                        "new folder already has is kept. The log of this run stays where it is until OpenSE4 starts again.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const float w = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    const float h = 26 * px;
    if (ImGui::Button("Copy and Switch", ImVec2(w, h))) {
        switchFolders(true);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Switch Only", ImVec2(w, h))) {
        switchFolders(false);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(w, h)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void refusalPopup(float px) {
    FilesPage& p = state();
    ImGui::SetNextWindowSize(ImVec2(560 * px, 0));
    if (!ImGui::BeginPopupModal(kRefusalPopup, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(p.refusal.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(-FLT_MIN, 26 * px)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Why `folder` cannot hold saved games: inside the installed game (the
// original would list OpenSE4's saves, which it cannot read), or not writable.
std::optional<std::string> savesProblem(const fs::path& folder, const fs::path& game) {
    if (!game.empty() && core::pathInside(folder, game))
        return std::format("{} is in the game's own folder. OpenSE4 never writes there: the original game cannot read OpenSE4's "
                           "saved games. Save for SE IV (in Save Game) writes games for the original wherever you choose.",
                           text(folder));
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) return std::format("{} cannot be made: {}.", text(folder), ec.message());
    const fs::path probe = folder / ".opense4-write-test";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out || !(out << "OpenSE4\n") || !out.flush()) return std::format("OpenSE4 cannot write in {}.", text(folder));
    }
    fs::remove(probe, ec);
    return std::nullopt;
}

} // namespace

void filesSettingsPage(float px, const game::Rules* rules) {
    FilesPage& p = state();
    const core::UserFolder now = userFolderInUse();
    const fs::path program = core::programFolder();

    section("Your files");
    ImGui::TextUnformatted("Saved games, settings, logs, mods and history are kept in:");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * px);
    ImGui::TextColored(kHeading, "%s", text(now.path).c_str());
    ImGui::PopTextWrapPos();
    // Input scripts read which rule chose it.
    script::reportItem(std::format("user-folder:{}", core::userFolderSourceName(now.source)), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    switch (now.source) {
        case core::UserFolderSource::System: dimWrapped("Your user folder: the place this system keeps programs' files for your account.", px); break;
        case core::UserFolderSource::Portable:
            dimWrapped("OpenSE4's own folder: the file portable.txt beside the program makes this copy keep its files beside it.", px);
            break;
        case core::UserFolderSource::Environment:
            dimWrapped(std::format("The folder the environment variable {} names. It comes before the setting below.", core::kUserDirVariable), px);
            break;
    }
    if (ImGui::Button("Open Folder")) {
        if (!SDL_OpenURL(fileUrl(now.path).c_str())) {
            p.message = std::format("The folder could not be opened: {}", SDL_GetError());
            p.problem = true;
        }
    }

    section("A portable copy");
    bool portable = !program.empty() && core::portableMarkerPresent(program);
    ImGui::BeginDisabled(now.source == core::UserFolderSource::Environment || program.empty());
    if (ImGui::Checkbox(kPortableLabel, &portable)) {
        p.message.clear();
        p.portableWanted = portable;
        p.from = now.path;
        if (portable) {
            if (auto problem = core::portableProblem(program)) {
                p.refusal = refusalText(*problem);
                log::info("Settings, Files: OpenSE4's folder cannot be used: {}", *problem);
                ImGui::OpenPopup(kRefusalPopup);
            } else {
                p.to = core::portableFolder(program);
                ImGui::OpenPopup(kSwitchPopup);
            }
        } else {
            p.to = systemUserDirectory();
            ImGui::OpenPopup(kSwitchPopup);
        }
    }
    ImGui::EndDisabled();
    dimWrapped(std::format("Keeps everything above in the folder \"userdata\" beside the program ({}) instead of your user folder, so "
                           "that it goes wherever the program's folder goes: a copy on a USB stick, or one of several copies. The "
                           "file portable.txt beside the program marks such a copy; deleting it does the same as unticking this. "
                           "When you switch, OpenSE4 offers to copy your files across and never deletes any.",
                           program.empty() ? std::string("unknown") : text(program)),
               px);
    switchPopup(px);
    refusalPopup(px);

    section("Saved games");
    FileSettings& files = appSettings().files;
    if (!p.savesInputSet) {
        p.savesInput = files.savesFolder;
        p.savesInputSet = true;
    }
    const fs::path game = gameFolder(rules);
    ImGui::TextUnformatted("Saves folder");
    ImGui::SetNextItemWidth(360 * px);
    const bool enter = inputString("##savesfolder", p.savesInput, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Use") || enter) {
        const fs::path folder = savesFolderPath(now.path, p.savesInput);
        if (auto problem = savesProblem(folder, game)) {
            p.message = *problem;
            p.problem = true;
        } else {
            files.savesFolder = p.savesInput;
            saveAppSettings();
            p.message = std::format("Saved games now go to {}.", text(folder));
            p.problem = false;
            log::info("Settings, Files: the saves folder is now {}", text(folder));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Default")) {
        p.savesInput.clear();
        files.savesFolder.clear();
        saveAppSettings();
        p.message = std::format("Saved games go to {} again.", text(savesFolderPath(now.path, {})));
        p.problem = false;
    }
    ImGui::TextDisabled("In use:");
    ImGui::SameLine();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 470 * px);
    ImGui::TextUnformatted(text(savesFolderPath(now.path, files.savesFolder)).c_str());
    ImGui::PopTextWrapPos();
    dimWrapped("Save Game, the autosaves, Load Game and Resume Game use this folder: empty for \"saves\" in the folder above. A "
               "folder written without its drive or root lies in the folder above, and goes with it. OpenSE4 never writes in the "
               "game's own folder; Save for SE IV (in Save Game) writes games for the original where you choose.",
               px);

    if (!p.message.empty()) {
        ImGui::Spacing();
        wrapped(p.problem ? kProblem : kNote, p.message, px);
        script::reportItem(p.message);   // input scripts read it
    }
}

} // namespace opense4::client::classic
