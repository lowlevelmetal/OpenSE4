#include "client/classic/learn_content.hpp"

#include "client/classic/settings.hpp"
#include "core/embedded.hpp"
#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace opense4::client::classic {

namespace {

constexpr std::string_view kEmbeddedPrefix = "assets/learn/";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The index page of the install's Manual folder: index.htm(l) if there is one,
// else the first web page in it. Only names are looked at.
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
    return pages.empty() ? fs::path{} : pages.front();
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

} // namespace opense4::client::classic
