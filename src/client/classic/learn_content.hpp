#pragma once

// The learning content as the client holds it (docs/LEARNING.md): the
// library of manual pages, tutorials and training games, where it came from,
// the original's HTML manual if the install has one, and the progress kept
// with the client settings.

#include "assets/assets.hpp"
#include "learn/library.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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

// A lesson was started (ClassicSettings::learnStarted: the intro's hint by
// Tutorial shows until then).
bool lessonsStarted();
void markLessonsStarted();

// Resuming a tutorial (docs/LEARNING.md "Resuming a lesson"): the place the
// player left it at, kept with the client settings, and its game, kept in
// <userDataDir>/lessons. Nothing when there is none.
struct LessonPlace {
    size_t leftAt = 0;       // the active step then (0-based)
    size_t resumeAt = 0;     // the step it resumes at
    uint64_t fingerprint = 0;
};
std::optional<LessonPlace> lessonPlace(learn::LessonKind kind, std::string_view slug);
std::filesystem::path lessonPlaceFile(learn::LessonKind kind, std::string_view slug);
// The lesson of that kind the player left last, if one keeps its place.
std::optional<std::string> lastLeftLesson(learn::LessonKind kind);
// Records the place (its game is already in lessonPlaceFile), as the most recent.
void rememberLessonPlace(learn::LessonKind kind, std::string_view slug, const LessonPlace& place);
// Forgets the place and deletes its game.
void forgetLessonPlace(learn::LessonKind kind, std::string_view slug);
// Whether the place can still be resumed: the lesson's steps are as they were
// and its game is there. Otherwise why not, in words.
std::optional<std::string> lessonPlaceProblem(const learn::Lesson& lesson, const LessonPlace& place);

} // namespace opense4::client::classic
