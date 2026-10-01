#pragma once

// The learning content as the client holds it (docs/LEARNING.md): the
// library of manual pages, tutorials and training games, where it came from,
// the original's HTML manual if the install has one, and the progress kept
// with the client settings.

#include "assets/assets.hpp"
#include "learn/library.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace opense4::client::classic {

struct LearnContent {
    learn::Library library;
    std::string origin;                     // "built in", "built in and <dir>", or the --learn-dir folder
    std::filesystem::path originalManual;   // the install's Manual/ index page (or the folder); empty: none
};

// Loads the content: from `learnDir` alone when it is given (--learn-dir),
// else the built-in copies, over which developer builds (OPENSE4_DEV_PATHS)
// let the files under <assetsDir>/learn win. Problems are logged.
std::unique_ptr<LearnContent> loadLearnContent(const std::filesystem::path& assetsDir, const std::filesystem::path& learnDir,
                                               const assets::InstallFiles& install);

// Opens the original manual's index page in the browser (SDL_OpenURL).
bool openOriginalManual(const LearnContent& content);

// Progress (ClassicSettings::learnDone).
bool lessonDone(learn::LessonKind kind, std::string_view slug);
void markLessonDone(learn::LessonKind kind, std::string_view slug);

} // namespace opense4::client::classic
