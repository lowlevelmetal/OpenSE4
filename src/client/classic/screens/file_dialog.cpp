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

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

void FileDialog::alsoList(std::string extension) {
    extraExtensions_.push_back(std::move(extension));
    scanned_ = false;
}

void FileDialog::alsoListFolder(std::filesystem::path folder, std::string folderExtension) {
    extraFolders_.emplace_back(std::move(folder), std::move(folderExtension));
    scanned_ = false;
}

void FileDialog::scan() {
    files_ = listFiles(directory_, extension_);
    // Files of the other extensions show theirs (TERRAN.EMP beside TERRAN).
    auto named = [](std::vector<FileEntry> list) {
        for (FileEntry& f : list) f.name = f.path.filename().string();
        return list;
    };
    for (const std::string& ext : extraExtensions_)
        for (FileEntry& f : named(listFiles(directory_, ext))) files_.push_back(std::move(f));
    sortFiles(files_, sort_);
    // The other folders' files come after, each sorted the same way.
    std::error_code ec;
    for (const auto& [folder, ext] : extraFolders_) {
        if (folder.empty() || !std::filesystem::is_directory(folder, ec) || std::filesystem::equivalent(folder, directory_, ec)) continue;
        std::vector<FileEntry> more = named(listFiles(folder, ext));
        sortFiles(more, sort_);
        for (FileEntry& f : more) files_.push_back(std::move(f));
    }
    scanned_ = true;
}

FileDialog::Result FileDialog::draw(const Painter& p, const char* window, UiContext* game) {
    Result result = Result::None;
    if (!scanned_) scan();
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
                scan();   // sorted again, the other folders' files still after
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
        if (classicButton(p, "Change Directory", {396, 26})) ImGui::OpenPopup("Select Directory");
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

namespace {

// The folders in a folder, by name (any case).
std::vector<std::string> subfolders(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        std::error_code type;
        if (!it->is_directory(type)) continue;
        std::string name = it->path().filename().string();
#ifndef _WIN32
        if (name.starts_with(".")) continue;   // hidden folders (ours)
#endif
        out.push_back(std::move(name));
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        const std::string la = lowerAscii(a), lb = lowerAscii(b);
        return la != lb ? la < lb : a < b;
    });
    return out;
}

std::vector<std::string> driveRoots() {
#ifdef _WIN32
    std::vector<std::string> out;
    for (char d = 'a'; d <= 'z'; ++d) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(std::string{d, ':', '\\'}), ec)) out.push_back(std::string{d, ':'});
    }
    return out;
#else
    return {"/"};
#endif
}

} // namespace

void FileDialog::browse(std::filesystem::path dir) {
    std::error_code ec;
    const std::filesystem::path full = std::filesystem::absolute(dir, ec).lexically_normal();
    browsing_ = ec ? dir : full;
    if (browsing_.has_relative_path() && !browsing_.has_filename()) browsing_ = browsing_.parent_path();   // no trailing separator
    folders_ = subfolders(browsing_);
    if (browsing_.has_relative_path()) folders_.insert(folders_.begin(), "..");
    pickedFolder_ = -1;
    folderField_ = browsing_.string();
    folderError_.clear();
}

void FileDialog::changeDirectoryPopup(const Painter& p) {
    // "Select Directory" (confirmed: binary): a 400×500 window with a Drive
    // box, a 300 px list of the folders (a double click opens one), the
    // Current Directory box, OK and Cancel. The places are ours (inferred);
    // typing a folder into Current Directory and the Default button are
    // OpenSE4's.
    constexpr Vec2 kSize{400, 500};
    const Vec2 min{std::floor((frameW() - kSize.x) * 0.5f), std::floor((frameH() - kSize.y) * 0.5f)};
    const Rect rect{min, min + kSize};
    ImGui::SetNextWindowPos(p.at(rect.min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(p.size(kSize), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (ImGui::BeginPopupModal("Select Directory", nullptr,
                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto P = [&](float x, float y) { return p.at(rect.min + Vec2{x, y}); };
        drawWindowFrame(p, dl, rect, nullptr, 0);
        dl->AddText(p.fonts.bold, p.fontPx(kTitleSize), P(10, 10), IM_COL32_WHITE, "Select Directory");
        ImFont* body = p.fonts.regular;
        const float bodySize = p.fontPx(kTextSize);
        const float lw = std::max(1.0f, std::floor(p.map.scale)) / p.fbScale;
        if (ImGui::IsWindowAppearing()) browse(directory_);

        // Drive: the roots of this computer's file systems.
        dl->AddText(body, bodySize, P(12, 42), imColor(palette::kLabel), "Drive");
        ImGui::SetCursorScreenPos(P(80, 38));
        ImGui::SetNextItemWidth(p.px(308));
        const std::string root = browsing_.root_name().string() + (browsing_.root_name().empty() ? browsing_.root_directory().string() : std::string());
        if (ImGui::BeginCombo("##drive", lowerAscii(root).c_str())) {
            for (const std::string& d : driveRoots())
                if (ImGui::Selectable(d.c_str(), lowerAscii(d) == lowerAscii(root))) browse(std::filesystem::path(d + (d.ends_with(':') ? "\\" : "")));
            ImGui::EndCombo();
        } else {
            script::reportItem("##drive");   // a combo box names itself to input scripts only so
        }

        // Directory: the folders here, ".." first; a double click opens one.
        dl->AddText(body, bodySize, P(12, 66), imColor(palette::kLabel), "Directory");
        dl->AddRect(P(10, 82), P(390, 382), imColor(palette::kFrameLight), 0.0f, lw);
        ImGui::SetCursorScreenPos(P(11, 83));
        beginList(p, "##folders", p.size({378, 298}), 18.0f, ImGuiChildFlags_None, false);
        const float rowW = ImGui::GetContentRegionAvail().x;
        ImDrawList* rows = ImGui::GetWindowDrawList();
        std::optional<std::filesystem::path> open;
        for (size_t i = 0; i < folders_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##folder-row", ImVec2(rowW, p.px(18)))) pickedFolder_ = static_cast<int>(i);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                open = folders_[i] == ".." ? browsing_.parent_path() : browsing_ / folders_[i];
            ImGui::PopID();
            script::reportItem(folders_[i]);   // input scripts find a folder by its name
            if (pickedFolder_ == static_cast<int>(i))
                if (Sprite grid = p.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(rowW / p.k()), 18, false))
                    rows->AddImage(ImTextureRef(static_cast<ImTextureID>(grid.tex.value)), r0, {r0.x + rowW, r0.y + p.px(18)},
                                   {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
            rows->PushClipRect(r0, {r0.x + rowW, r0.y + p.px(18)}, true);
            rows->AddText(body, bodySize, {r0.x + p.px(4), r0.y + p.px((18 - kTextCell) * 0.5f)}, IM_COL32_WHITE, folders_[i].c_str());
            rows->PopClipRect();
        }
        endList(p);
        if (open) browse(*open);

        // Current Directory: the folder chosen; typing another and Enter goes there (ours).
        dl->AddText(body, bodySize, P(12, 392), imColor(palette::kLabel), "Current Directory");
        ImGui::SetCursorScreenPos(P(10, 408));
        ImGui::SetNextItemWidth(p.px(380));
        const bool enter = inputString("##folder", folderField_, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
        auto typedFolder = [&]() -> std::optional<std::filesystem::path> {
            std::error_code ec;
            const std::filesystem::path typed(folderField_);
            if (!folderField_.empty() && std::filesystem::is_directory(typed, ec)) return typed;
            folderError_ = "There is no such folder.";
            return std::nullopt;
        };
        if (enter)
            if (auto typed = typedFolder()) browse(*typed);
        if (!folderError_.empty()) {
            dl->AddText(body, bodySize, P(12, 434), IM_COL32(255, 128, 100, 255), folderError_.c_str());
            script::reportItem(folderError_, P(12, 434), P(388, 450));   // input scripts read it
        }

        const float w = 121;
        ImGui::SetCursorScreenPos(P(12, 462));
        if (classicButton(p, "OK", {w, 26})) {
            if (auto chosen = folderField_ == browsing_.string() ? std::optional<std::filesystem::path>(browsing_) : typedFolder()) {
                directory_ = *chosen;
                scanned_ = false;
                error_.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SetCursorScreenPos(P(12 + w + 6, 462));
        if (classicButton(p, "Default", {w, 26})) browse(defaultDirectory_);
        ImGui::SetCursorScreenPos(P(12 + 2 * (w + 6), 462));
        if (classicButton(p, "Cancel", {w, 26}) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
}

} // namespace opense4::client::classic
