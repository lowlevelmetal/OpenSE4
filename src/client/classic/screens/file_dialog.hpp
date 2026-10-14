#pragma once

// The original's Load Game dialog (spec 07 session 5), shared by the intro's
// Load Game, the Game Menu's Load (and Delete) and Game Setup's Add Existing
// (as "Load Empire"): a 420×520 window centred on the screen, its name in the
// title font at (10,10), a list (10,36)–(409,439) with a 20 px heading (the
// file name column at x 4, "Date" at x 204, divided at x 201), the names in
// capitals and each file's date and time, rows 32 px apart, then Change
// Directory (12,452)–(407,477) and Cancel (12,482)–(407,507). A click on a
// row chooses the file. The list starts sorted by name, A to Z; a click on a
// column's heading sorts by it (FileSortHistory).

#include "client/classic/screens/file_list.hpp"
#include "client/classic/ui.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// The folder Load Game lists: the saved games folder until Change Directory
// picks another, kept for the rest of the session (from the intro and in a game).
std::filesystem::path& loadGameDirectory();

class FileDialog {
public:
    enum class Result { None, Chosen, Cancelled };

    // `directory` is the folder listed; Change Directory changes it (its
    // Default button offers `defaultDirectory`).
    FileDialog(std::string title, std::string nameHeading, std::string extension, std::filesystem::path& directory,
               std::filesystem::path defaultDirectory);

    // Draws the dialog as the Dear ImGui window `window`. In a game, `game`
    // registers the window's tag and `<window id>:close` on Cancel.
    Result draw(const Painter& p, const char* window, UiContext* game = nullptr);
    const FileEntry& chosen() const { return chosen_; }
    // A line under the list (a file that could not be read).
    void setError(std::string text) { error_ = std::move(text); }
    void rescan() { scanned_ = false; }

private:
    // Change Directory's "Select Directory" window, and its folder shown.
    void changeDirectoryPopup(const Painter& p);
    void browse(std::filesystem::path dir);

    std::string title_, nameHeading_, extension_;
    std::filesystem::path& directory_;
    std::filesystem::path defaultDirectory_;
    std::vector<FileEntry> files_;
    FileSortHistory sort_;
    bool scanned_ = false;
    FileEntry chosen_;
    std::string error_;
    std::string folderField_, folderError_;
    std::filesystem::path browsing_;      // Select Directory's current folder
    std::vector<std::string> folders_;    // its folders, ".." first
    int pickedFolder_ = -1;
};

} // namespace opense4::client::classic
