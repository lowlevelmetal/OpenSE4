#include "learn/library.hpp"

#include "learn/ids.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace opense4::learn {

namespace {

constexpr std::string_view kManualDir = "manual/";
constexpr std::string_view kTutorialDir = "tutorials/";
constexpr std::string_view kTrainingDir = "training/";

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The page's text cut at its ## and ### headings.
std::vector<ManualPage::Chunk> chunksOf(const Document& doc) {
    std::vector<ManualPage::Chunk> out(1);
    out.front().title = doc.title;
    for (const Block& b : doc.blocks) {
        if (b.kind == Block::Kind::Heading && b.level >= 2) {
            ManualPage::Chunk c;
            c.anchor = b.anchor;
            c.title = plainText(b.text);
            out.push_back(std::move(c));
            continue;
        }
        if (b.kind == Block::Kind::Heading) continue;
        out.back().text += plainText(std::vector<Block>{b});
    }
    for (ManualPage::Chunk& c : out) {
        c.lower = lower(c.title + "\n" + c.text);
    }
    return out;
}

// Moves `pos` back to the start of a UTF-8 character.
size_t charStart(std::string_view s, size_t pos) {
    while (pos > 0 && pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80) --pos;
    return pos;
}

std::string excerpt(const ManualPage::Chunk& c, std::string_view word) {
    const std::string full = c.title + "\n" + c.text;
    const size_t at = c.lower.find(word);
    if (at == std::string::npos) return {};
    size_t from = at > 40 ? charStart(full, at - 40) : 0;
    size_t to = std::min(full.size(), at + word.size() + 80);
    to = charStart(full, to);
    std::string out = full.substr(from, to - from);
    for (char& ch : out)
        if (ch == '\n') ch = ' ';
    if (from > 0) out = "..." + out;
    if (to < full.size()) out += "...";
    return out;
}

} // namespace

// ---- Sources ---------------------------------------------------------------------------------

std::vector<std::string> DirectorySource::list() const {
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(root_, ec)) return out;
    for (auto it = std::filesystem::recursive_directory_iterator(root_, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        out.push_back(std::filesystem::relative(it->path(), root_, ec).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<std::string> DirectorySource::read(std::string_view path) const {
    std::ifstream in(root_ / std::filesystem::path(std::string(path)), std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string DirectorySource::describe(std::string_view path) const { return (root_ / std::filesystem::path(std::string(path))).string(); }

void MemorySource::add(std::string path, std::span<const unsigned char> bytes) {
    files_[std::move(path)] = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void MemorySource::add(std::string path, std::string_view text) { files_[std::move(path)] = std::string(text); }

std::vector<std::string> MemorySource::list() const {
    std::vector<std::string> out;
    for (const auto& [path, text] : files_) out.push_back(path);
    return out;
}

std::optional<std::string> MemorySource::read(std::string_view path) const {
    const auto it = files_.find(path);
    if (it == files_.end()) return std::nullopt;
    return it->second;
}

LayeredSource::LayeredSource(std::vector<std::unique_ptr<Source>> layers) : layers_(std::move(layers)) {
    for (const auto& layer : layers_) lists_.push_back(layer->list());
}

const Source* LayeredSource::owner(std::string_view path) const {
    for (size_t i = 0; i < layers_.size(); ++i)
        if (std::binary_search(lists_[i].begin(), lists_[i].end(), path, [](std::string_view a, std::string_view b) { return a < b; }))
            return layers_[i].get();
    return nullptr;
}

std::vector<std::string> LayeredSource::list() const {
    std::set<std::string> all;
    for (const auto& files : lists_) all.insert(files.begin(), files.end());
    return {all.begin(), all.end()};
}

std::optional<std::string> LayeredSource::read(std::string_view path) const {
    const Source* s = owner(path);
    return s ? s->read(path) : std::nullopt;
}

std::string LayeredSource::origin(std::string_view path) const {
    const Source* s = owner(path);
    return s ? s->origin(path) : std::string{};
}

std::string LayeredSource::describe(std::string_view path) const {
    const Source* s = owner(path);
    return s ? s->describe(path) : std::string(path);
}

// ---- Loading ---------------------------------------------------------------------------------

Library loadLibrary(const Source& source) {
    Library lib;
    std::set<std::pair<int, std::string>> slugs;   // (kind, slug): manual 0, tutorials 1, training 2
    for (const std::string& path : source.list()) {
        const std::string name = source.describe(path);
        const auto text = source.read(path);
        if (!text) {
            lib.problems.push_back({name, 0, "could not be read"});
            continue;
        }
        const std::string_view p = path;
        int kind = -1;
        if (p.starts_with(kManualDir) && p.ends_with(".md")) kind = 0;
        else if (p.starts_with(kTutorialDir) && p.ends_with(".toml")) kind = 1;
        else if (p.starts_with(kTrainingDir) && p.ends_with(".toml")) kind = 2;
        if (kind < 0) {
            // Notes for content writers (README.md and the like) may sit at the top.
            if (p.find('/') != std::string_view::npos)
                lib.problems.push_back({name, 0, "not a manual page (manual/*.md) or lesson (tutorials/*.toml, training/*.toml)"});
            continue;
        }
        const std::string slug = slugOf(path);
        if (!slugs.insert({kind, slug}).second) {
            lib.problems.push_back({name, 0, std::format("another file already has the slug '{}'", slug)});
            continue;
        }
        if (kind == 0) {
            ManualPage page;
            page.slug = slug;
            page.file = name;
            page.origin = source.origin(path);
            page.doc = parseMarkdown(*text, name, true);
            for (Diagnostic& d : page.doc.problems) lib.problems.push_back(d);
            if (page.doc.title.empty()) lib.problems.push_back({name, 1, "a manual page starts with a '#' title"});
            for (const Block& b : page.doc.blocks)
                if (b.kind == Block::Kind::Heading && b.level >= 2) page.sections.push_back({b.anchor, plainText(b.text), b.level});
            page.chunks = chunksOf(page.doc);
            lib.manual.push_back(std::move(page));
        } else {
            const LessonKind lk = kind == 1 ? LessonKind::Tutorial : LessonKind::Training;
            auto lesson = parseLesson(*text, name, lk, lib.problems);
            if (!lesson) continue;
            lesson->slug = slug;
            lesson->origin = source.origin(path);
            (lk == LessonKind::Tutorial ? lib.tutorials : lib.training).push_back(std::move(*lesson));
        }
    }
    return lib;
}

const ManualPage* Library::page(std::string_view slug) const {
    for (const ManualPage& p : manual)
        if (p.slug == slug) return &p;
    return nullptr;
}

const Lesson* Library::lesson(LessonKind kind, std::string_view slug) const {
    for (const Lesson& l : lessons(kind))
        if (l.slug == slug) return &l;
    return nullptr;
}

const Lesson* Library::next(LessonKind kind, std::string_view slug) const {
    const auto& list = lessons(kind);
    for (size_t i = 0; i + 1 < list.size(); ++i)
        if (list[i].slug == slug) return &list[i + 1];
    return nullptr;
}

const ManualPage* Library::pageForWindow(std::string_view windowId) const {
    for (const ManualPage& p : manual)
        if (std::find(p.doc.windows.begin(), p.doc.windows.end(), windowId) != p.doc.windows.end()) return &p;
    return nullptr;
}

std::vector<Library::Hit> Library::search(std::string_view query, size_t limit) const {
    std::vector<std::string> words;
    std::istringstream in{lower(query)};
    for (std::string w; in >> w;) words.push_back(w);
    std::vector<Hit> out;
    if (words.empty()) return out;
    for (const ManualPage& p : manual)
        for (const ManualPage::Chunk& c : p.chunks) {
            if (!std::all_of(words.begin(), words.end(), [&](const std::string& w) { return c.lower.find(w) != std::string::npos; })) continue;
            out.push_back({&p, c.anchor, c.title, excerpt(c, words.front())});
            if (out.size() >= limit) return out;
        }
    return out;
}

// ---- Validation ------------------------------------------------------------------------------

std::optional<std::string> linkProblem(const Library& lib, std::string_view target, std::string_view fromPage) {
    const Link l = parseLink(target);
    switch (l.kind) {
        case Link::Kind::Invalid: return std::format("'{}' is not a link OpenSE4 understands", target);
        case Link::Kind::External: return std::nullopt;
        case Link::Kind::Window: {
            const WindowInfo* w = findWindow(l.target);
            if (!w) return std::format("unknown window id '{}'", l.target);
            if (!w->openable) return std::format("the window '{}' cannot be opened from a link", l.target);
            return std::nullopt;
        }
        case Link::Kind::Help:
            if (!isHelpTab(l.target)) return std::format("unknown Help tab '{}'", l.target);
            return std::nullopt;
        case Link::Kind::Page: {
            const std::string_view slug = l.target.empty() ? fromPage : std::string_view(l.target);
            const ManualPage* p = lib.page(slug);
            if (!p) return std::format("no manual page '{}'", slug);
            if (!l.anchor.empty()) {
                const auto anchors = collectAnchors(p->doc.blocks);
                if (std::find(anchors.begin(), anchors.end(), l.anchor) == anchors.end())
                    return std::format("the page '{}' has no section '{}'", slug, l.anchor);
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::vector<Diagnostic> validate(Library& lib) {
    std::vector<Diagnostic> out;
    auto links = [&](const std::vector<Block>& blocks, const std::string& file, std::string_view fromPage) {
        for (const LinkUse& use : collectLinks(blocks))
            if (auto problem = linkProblem(lib, use.target, fromPage)) out.push_back({file, use.line, *problem});
    };
    for (const ManualPage& p : lib.manual) {
        links(p.doc.blocks, p.file, p.slug);
        for (const std::string& w : p.doc.windows)
            if (w != "main" && !findWindow(w)) out.push_back({p.file, 2, std::format("unknown window id '{}' in the front matter", w)});
    }
    for (const Lesson& l : lib.tutorials)
        for (const Step& s : l.steps) {
            links(s.text, l.file, {});
            if (!s.manual.empty()) {
                if (parseLink(s.manual).kind != Link::Kind::Page)
                    out.push_back({l.file, s.line, "'manual' names a manual page: \"slug\" or \"slug#anchor\""});
                else if (auto problem = linkProblem(lib, s.manual)) out.push_back({l.file, s.line, *problem});
            }
        }
    for (const Lesson& l : lib.training) {
        for (const BriefingPage& p : l.pages) links(p.text, l.file, {});
        for (const Hint& h : l.hints) links(h.text, l.file, {});
        if (l.fail) links(l.fail->text, l.file, {});
    }
    lib.problems.insert(lib.problems.end(), out.begin(), out.end());
    return out;
}

} // namespace opense4::learn
