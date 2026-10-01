#pragma once

// The learning content (docs/LEARNING.md "Where the content lives"): manual
// chapters, tutorials and training games, read from a Source — the copies
// built into the executable, a folder on disk, or both with the disk
// winning — and checked as a whole (links, anchors, window ids, UI tags).
//
// Paths are relative to the content root: "manual/01-basics.md",
// "tutorials/01-first-steps.toml", "training/01-expansion.toml".

#include "learn/lesson.hpp"
#include "learn/markdown.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

class Source {
public:
    virtual ~Source() = default;
    // Every file, relative to the content root, sorted.
    virtual std::vector<std::string> list() const = 0;
    virtual std::optional<std::string> read(std::string_view path) const = 0;
    // Where a file comes from: "built in", or the folder on disk.
    virtual std::string origin(std::string_view path) const = 0;
    // A name for diagnostics ("assets/learn/manual/01-basics.md" or a path on disk).
    virtual std::string describe(std::string_view path) const = 0;
};

// The files under a folder on disk.
class DirectorySource final : public Source {
public:
    explicit DirectorySource(std::filesystem::path root) : root_(std::move(root)) {}
    std::vector<std::string> list() const override;
    std::optional<std::string> read(std::string_view path) const override;
    std::string origin(std::string_view) const override { return root_.string(); }
    std::string describe(std::string_view path) const override;

private:
    std::filesystem::path root_;
};

// Files held in memory: the copies built into the executable.
class MemorySource final : public Source {
public:
    // `label` prefixes the paths in diagnostics ("assets/learn/").
    explicit MemorySource(std::string label = {}) : label_(std::move(label)) {}
    void add(std::string path, std::span<const unsigned char> bytes);
    void add(std::string path, std::string_view text);
    std::vector<std::string> list() const override;
    std::optional<std::string> read(std::string_view path) const override;
    std::string origin(std::string_view) const override { return "built in"; }
    std::string describe(std::string_view path) const override { return label_ + std::string(path); }

private:
    std::string label_;
    std::map<std::string, std::string, std::less<>> files_;
};

// Several sources; for a path in more than one, the first wins.
class LayeredSource final : public Source {
public:
    explicit LayeredSource(std::vector<std::unique_ptr<Source>> layers);
    std::vector<std::string> list() const override;
    std::optional<std::string> read(std::string_view path) const override;
    std::string origin(std::string_view path) const override;
    std::string describe(std::string_view path) const override;

private:
    const Source* owner(std::string_view path) const;
    std::vector<std::unique_ptr<Source>> layers_;
    std::vector<std::vector<std::string>> lists_;   // each layer's files, listed once
};

struct Section {
    std::string anchor;
    std::string title;
    int level = 2;
};

struct ManualPage {
    std::string slug;
    std::string file;
    std::string origin;
    Document doc;
    std::vector<Section> sections;   // ## and ### headings, in order
    // The page's plain text cut at its headings, for search.
    struct Chunk {
        std::string anchor;          // empty: the top of the page
        std::string title;
        std::string text;
        std::string lower;           // text in lower case
    };
    std::vector<Chunk> chunks;
};

struct Library {
    std::vector<ManualPage> manual;   // in contents order
    std::vector<Lesson> tutorials;    // in list order
    std::vector<Lesson> training;
    std::vector<Diagnostic> problems; // from loading and from validate()

    const ManualPage* page(std::string_view slug) const;
    const Lesson* lesson(LessonKind kind, std::string_view slug) const;
    const std::vector<Lesson>& lessons(LessonKind kind) const { return kind == LessonKind::Tutorial ? tutorials : training; }
    // The next lesson of the same kind after `slug`, if any.
    const Lesson* next(LessonKind kind, std::string_view slug) const;
    // The page whose front matter names this window (`main`: the main window).
    const ManualPage* pageForWindow(std::string_view windowId) const;

    // A search over the manual: pages and sections whose text holds every word.
    struct Hit {
        const ManualPage* page = nullptr;
        std::string anchor;      // the section the first match is in (empty: the page's top)
        std::string section;     // its title
        std::string excerpt;     // the text around the match
    };
    std::vector<Hit> search(std::string_view query, size_t limit = 50) const;
};

// Reads every file of the source. Files of unknown kinds are reported.
Library loadLibrary(const Source& source);
// Checks the content as a whole: links to pages and anchors, `window:` and
// `help:` links, front matter window ids, Read more links. Appends to
// library.problems and returns what it found.
std::vector<Diagnostic> validate(Library& library);

// Whether a link target resolves in the library (a page link: the page and
// anchor exist; window and help links: the id is known). `fromPage`: the
// page the link is on, for "#anchor".
std::optional<std::string> linkProblem(const Library& library, std::string_view target, std::string_view fromPage = {});

} // namespace opense4::learn
