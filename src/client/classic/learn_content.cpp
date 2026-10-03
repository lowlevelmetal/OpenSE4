#include "client/classic/learn_content.hpp"

#include "client/classic/session.hpp"
#include "client/classic/settings.hpp"
#include "core/embedded.hpp"
#include "core/log.hpp"
#include "learn/resume.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

namespace opense4::client::classic {

namespace {

constexpr std::string_view kEmbeddedPrefix = "assets/learn/";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The index page of the install's Manual folder: index.htm(l) if there is one,
// else the first web page in it, else the folder itself. Only names are looked at.
std::filesystem::path findOriginalManual(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path folder;
    for (const auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory(ec) && lower(e.path().filename().string()) == "manual") folder = e.path();
    if (folder.empty()) return {};
    std::vector<fs::path> pages;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        const std::string ext = lower(e.path().extension().string());
        if (e.is_regular_file(ec) && (ext == ".htm" || ext == ".html")) pages.push_back(e.path());
    }
    std::sort(pages.begin(), pages.end());
    for (const fs::path& p : pages)
        if (lower(p.stem().string()) == "index") return p;
    return pages.empty() ? folder : pages.front();
}

// A file:// URL with the characters browsers need escaped.
std::string fileUrl(const std::filesystem::path& file) {
    std::string path = file.generic_string();
    std::string out = "file://";
    if (!path.starts_with('/')) out += '/';   // C:/... on Windows
    for (const unsigned char c : path) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

std::string progressKey(learn::LessonKind kind, std::string_view slug) { return std::format("{}:{}", learn::kindName(kind), slug); }

} // namespace

std::unique_ptr<LearnContent> loadLearnContent(const std::filesystem::path& assetsDir, const std::filesystem::path& learnDir,
                                               const assets::InstallFiles& install) {
    auto content = std::make_unique<LearnContent>();
    if (!learnDir.empty()) {
        content->library = learn::loadLibrary(learn::DirectorySource(learnDir));
        content->origin = learnDir.string();
    } else {
        auto builtIn = std::make_unique<learn::MemorySource>(std::string(kEmbeddedPrefix));
        for (std::string_view path : embeddedResourcePaths(kEmbeddedPrefix))
            builtIn->add(std::string(path.substr(kEmbeddedPrefix.size())), embeddedResource(path));
        std::vector<std::unique_ptr<learn::Source>> layers;
        content->origin = "built in";
#ifdef OPENSE4_SOURCE_DIR
        // Developer builds: the files in the assets folder win, so content can
        // be edited and checked without building again.
        const std::filesystem::path disk = assetsDir / "learn";
        if (std::error_code ec; std::filesystem::is_directory(disk, ec)) {
            layers.push_back(std::make_unique<learn::DirectorySource>(disk));
            content->origin = std::format("built in and {}", disk.string());
        }
#else
        (void)assetsDir;
#endif
        layers.push_back(std::move(builtIn));
        content->library = learn::loadLibrary(learn::LayeredSource(std::move(layers)));
    }
    learn::validate(content->library);
    for (const learn::Diagnostic& d : content->library.problems) log::warn("Learning content: {}", d.text());
    log::info("Learning content ({}): {} manual pages, {} tutorials, {} training games", content->origin, content->library.manual.size(),
              content->library.tutorials.size(), content->library.training.size());
    content->originalManual = findOriginalManual(install.root());
    return content;
}

bool openOriginalManual(const LearnContent& content) {
    if (content.originalManual.empty()) return false;
    const std::string url = fileUrl(content.originalManual);
    if (SDL_OpenURL(url.c_str())) return true;
    log::warn("Could not open {}: {}", url, SDL_GetError());
    return false;
}

bool lessonDone(learn::LessonKind kind, std::string_view slug) {
    const auto& done = settings().learnDone;
    return std::find(done.begin(), done.end(), progressKey(kind, slug)) != done.end();
}

void markLessonDone(learn::LessonKind kind, std::string_view slug) {
    if (lessonDone(kind, slug)) return;
    settings().learnDone.push_back(progressKey(kind, slug));
    saveSettings();
}

bool lessonsStarted() { return settings().learnStarted; }

void markLessonsStarted() {
    if (settings().learnStarted) return;
    settings().learnStarted = true;
    saveSettings();
}

std::optional<LessonPlace> lessonPlace(learn::LessonKind kind, std::string_view slug) {
    const std::string key = progressKey(kind, slug);
    for (const ClassicSettings::ResumeRecord& r : settings().learnResume) {
        if (r.lesson != key) continue;
        LessonPlace p;
        p.leftAt = r.leftAt;
        p.resumeAt = r.resumeAt;
        const auto [end, ec] = std::from_chars(r.fingerprint.data(), r.fingerprint.data() + r.fingerprint.size(), p.fingerprint, 16);
        if (ec != std::errc{} || end != r.fingerprint.data() + r.fingerprint.size()) p.fingerprint = 0;
        return p;
    }
    return std::nullopt;
}

std::optional<std::string> lastLeftLesson(learn::LessonKind kind) {
    const std::string prefix = progressKey(kind, "");
    const auto& list = settings().learnResume;
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (it->lesson.starts_with(prefix)) return it->lesson.substr(prefix.size());
    return std::nullopt;
}

std::filesystem::path lessonPlaceFile(learn::LessonKind kind, std::string_view slug) {
    return userDataDir() / "lessons" / std::format("{}-{}.gam", learn::kindName(kind), slug);
}

void rememberLessonPlace(learn::LessonKind kind, std::string_view slug, const LessonPlace& place) {
    auto& list = settings().learnResume;
    const std::string key = progressKey(kind, slug);
    std::erase_if(list, [&](const ClassicSettings::ResumeRecord& r) { return r.lesson == key; });
    list.push_back({key, uint32_t(place.leftAt), uint32_t(place.resumeAt), std::format("{:016x}", place.fingerprint)});
    saveSettings();
}

void forgetLessonPlace(learn::LessonKind kind, std::string_view slug) {
    std::error_code ec;
    std::filesystem::remove(lessonPlaceFile(kind, slug), ec);
    auto& list = settings().learnResume;
    const std::string key = progressKey(kind, slug);
    if (std::erase_if(list, [&](const ClassicSettings::ResumeRecord& r) { return r.lesson == key; }) > 0) saveSettings();
}

std::optional<std::string> lessonPlaceProblem(const learn::Lesson& lesson, const LessonPlace& place) {
    if (place.fingerprint != learn::lessonFingerprint(lesson) || place.resumeAt >= lesson.steps.size())
        return std::string("The lesson has changed since you left it");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(lessonPlaceFile(lesson.kind, lesson.slug), ec)) return std::string("Its saved game is missing");
    return std::nullopt;
}

} // namespace opense4::client::classic
