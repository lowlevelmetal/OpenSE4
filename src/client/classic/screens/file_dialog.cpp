#include "client/classic/screens/file_dialog.hpp"

#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/session.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

std::string upperAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

constexpr Vec2 kDialogSize{420, 520};
constexpr float kRowH = 32.0f;

} // namespace

std::filesystem::path& loadGameDirectory() {
    // The saves folder, and another one once Change Directory chose it. While
    // it is still the saves folder it follows that folder when Settings →
    // Files moves it (another saves folder, a portable copy).
    static std::filesystem::path dir, saves;
    const std::filesystem::path now = savesDir();
    if (dir.empty() || (dir == saves && now != saves)) dir = now;
    saves = now;
    return dir;
}

FileDialog::FileDialog(std::string title, std::string nameHeading, std::string extension, std::filesystem::path& directory,
                       std::filesystem::path defaultDirectory)
    : title_(std::move(title)), nameHeading_(std::move(nameHeading)), extension_(std::move(extension)), directory_(directory),
      defaultDirectory_(std::move(defaultDirectory)) {}

FileDialog::Result FileDialog::draw(const Painter& p, const char* window, UiContext* game) {
    Result result = Result::None;
    if (!scanned_) {
        files_ = listFiles(directory_, extension_);
        sortFiles(files_, sort_);
        scanned_ = true;
    }
    const Vec2 min{std::floor((frameW() - kDialogSize.x) * 0.5f), std::floor((frameH() - kDialogSize.y) * 0.5f)};
    const Rect rect{min, min + kDialogSize};
    ImGui::SetNextWindowPos(p.at(rect.min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(p.size(kDialogSize), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const bool visible = ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                           ImGuiWindowFlags_NoScrollWithMouse | p.windowFlags);
    if (visible) {
        if (game) game->tagWindow(p.at(rect.min), p.at(rect.max));
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto P = [&](float x, float y) { return p.at(rect.min + Vec2{x, y}); };
        drawWindowFrame(p, dl, rect, nullptr, 0);
        // Its name in the title font at (10,10).
        dl->AddText(p.fonts.bold, p.fontPx(kTitleSize), P(10, 10), IM_COL32_WHITE, title_.c_str());

        // The list with its heading row.
        const float lw = std::max(1.0f, std::floor(p.map.scale)) / p.fbScale;
        dl->AddRect(P(10, 36), P(410, 440), imColor(palette::kFrameLight), 0.0f, lw);
        ImFont* body = p.fonts.regular;
        const float bodySize = p.fontPx(kTextSize);
        dl->AddText(body, bodySize, P(14, 37 + 3 - kTextLead), imColor(palette::kLabel), nameHeading_.c_str());
        dl->AddText(body, bodySize, P(214, 37 + 3 - kTextLead), imColor(palette::kLabel), "Date");
        dl->AddLine(P(211, 37), P(211, 56), imColor(palette::kFrame), lw);
        dl->AddLine(P(11, 56), P(409, 56), imColor(palette::kFrame), lw);
        // A click on a heading sorts by its column.
        auto heading = [&](const char* id, float x0, float x1, FileSortKey key) {
            ImGui::SetCursorScreenPos(P(x0, 37));
            if (ImGui::InvisibleButton(id, p.size({x1 - x0, 19}))) {
                sort_.click(key);
                sortFiles(files_, sort_);
            }
        };
        heading("##sortname", 11, 211, FileSortKey::Name);
        heading("##sortdate", 211, 409, FileSortKey::Date);
        ImGui::SetCursorScreenPos(P(11, 57));
        beginList(p, "##files", p.size({398, 382}), kRowH, ImGuiChildFlags_None, false);
        const float rowW = ImGui::GetContentRegionAvail().x;
        ImDrawList* rows = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < files_.size(); ++i) {
            const FileEntry& f = files_[i];
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##file", ImVec2(rowW, p.px(kRowH)));
            const std::string shown = upperAscii(f.name);
            script::reportItem(shown);   // input scripts find a file by its name as shown
            if (ImGui::IsItemHovered())
                if (Sprite grid = p.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(rowW / p.k()), int(kRowH), false))
                    rows->AddImage(ImTextureRef(static_cast<ImTextureID>(grid.tex.value)), r0, {r0.x + rowW, r0.y + p.px(kRowH)},
                                   {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
            const float textY = r0.y + p.px((kRowH - kTextCell) * 0.5f);
            rows->PushClipRect(r0, {r0.x + p.px(196), r0.y + p.px(kRowH)}, true);
            rows->AddText(body, bodySize, {r0.x + p.px(3), textY}, IM_COL32_WHITE, shown.c_str());
            rows->PopClipRect();
            rows->AddText(body, bodySize, {r0.x + p.px(203), textY}, IM_COL32_WHITE, fileDate(f.modified).c_str());
            ImGui::PopID();
            if (clicked) {
                chosen_ = f;
                result = Result::Chosen;
            }
        }
        if (files_.empty()) {
            const ImVec2 c = ImGui::GetCursorScreenPos();
            rows->AddText(body, bodySize, {c.x + p.px(3), c.y + p.px(6)}, imColor(palette::kSecondary), "(none)");
        }
        endList(p);
        if (!error_.empty()) {
            dl->PushClipRect(P(10, 440), P(410, 452), true);
            dl->AddText(p.fonts.small ? p.fonts.small : body, p.fontPx(kSmallSize), P(12, 441), IM_COL32(255, 128, 100, 255), error_.c_str());
            dl->PopClipRect();
        }

        ImGui::SetCursorScreenPos(P(12, 452));
        if (classicButton(p, "Change Directory", {396, 26})) {
            folderField_ = directory_.string();
            folderError_.clear();
            ImGui::OpenPopup("Change Directory");
        }
        changeDirectoryPopup(p);
        ImGui::SetCursorScreenPos(P(12, 482));
        const bool cancel = classicButton(p, "Cancel", {396, 26});
        if (game && game->drawing) game->tagItem(std::string(windowId(*game->drawing)) + ":close");
        if (cancel || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() &&
                       !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)))
            result = Result::Cancelled;
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    return result;
}

void FileDialog::changeDirectoryPopup(const Painter& p) {
    // Ours: the folder typed in (the original's folder browser is not described).
    ImGui::SetNextWindowSize(p.size({400, 0}));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, p.size({10, 10}));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, p.size({6, 6}));
    if (ImGui::BeginPopupModal("Change Directory", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(kLabelBlue, "Folder");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##folder", folderField_, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
        if (!folderError_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%s", folderError_.c_str());
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
        if (ImGui::Button("OK", ImVec2(w, p.px(26))) || enter) {
            std::error_code ec;
            const std::filesystem::path dir(folderField_);
            if (std::filesystem::is_directory(dir, ec)) {
                directory_ = dir;
                scanned_ = false;
                error_.clear();
                ImGui::CloseCurrentPopup();
            } else {
                folderError_ = "There is no such folder.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Default", ImVec2(w, p.px(26)))) folderField_ = defaultDirectory_.string();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, p.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

} // namespace opense4::client::classic
