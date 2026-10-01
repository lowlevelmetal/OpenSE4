#pragma once

// The Markdown subset of OpenSE4's manual pages and lesson texts
// (docs/LEARNING.md "Manual pages"), parsed into a block model that the
// client draws with its own fonts: headings with anchors, paragraphs of
// inline spans (bold, italic, code, links), lists with one level of
// nesting, pipe tables and tip boxes, plus an optional front matter block
// that names the windows a page explains.
//
// Anything outside the subset is kept as plain text; the parser never
// fails, it reports what it did not understand in Document::problems.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

// A problem in a content file, with the line it is on (1-based; 0: the whole file).
struct Diagnostic {
    std::string file;
    int line = 0;
    std::string message;
    // "file:line: message".
    std::string text() const;
};

// A run of text with one style. Links may be bold or italic too.
struct Span {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool code = false;      // `F1`: keys and on-screen labels, never styled further
    std::string link;       // the link target, empty for plain text
    bool operator==(const Span&) const = default;
};
using Inline = std::vector<Span>;

struct ListItem;
struct List {
    bool numbered = false;
    int start = 1;          // the first number of a numbered list
    std::vector<ListItem> items;
};
struct ListItem {
    Inline text;
    std::vector<List> sub;  // a nested list (at most one level)
};

enum class Align : uint8_t { Left, Center, Right };
struct Table {
    std::vector<Inline> header;
    std::vector<Align> align;               // per column
    std::vector<std::vector<Inline>> rows;  // each padded to the header's width
};

struct Block {
    enum class Kind : uint8_t { Heading, Paragraph, List, Table, Tip };
    Kind kind = Kind::Paragraph;
    int line = 0;
    int level = 0;            // Heading: 1 (the title), 2 or 3
    std::string anchor;       // Heading
    Inline text;              // Heading, Paragraph
    List list;                // List
    Table table;              // Table
    std::vector<Block> tip;   // Tip: the blocks inside the box
};

struct Document {
    std::string title;                 // the text of the `#` heading
    std::vector<std::string> windows;  // front matter `windows:`
    std::vector<Block> blocks;         // the title heading included
    std::vector<Diagnostic> problems;
};

// Parses a manual page or a lesson text. `file` only labels the problems.
// `frontMatter`: accept a leading `---` block (manual pages only).
Document parseMarkdown(std::string_view text, std::string_view file = {}, bool frontMatter = true);
// Inline markup of one paragraph or cell.
Inline parseInline(std::string_view text);

// The anchor of a heading: its plain text in lower case, letters and digits
// kept, spaces and hyphens as `-`, everything else dropped ("Ships & fleets"
// gives "ships--fleets"). A page's later duplicates get "-1", "-2", ...
std::string anchorOf(std::string_view heading);
std::string plainText(const Inline& text);
// The plain text of blocks (for search), paragraphs separated by newlines.
std::string plainText(const std::vector<Block>& blocks);

// Where a link goes (docs/LEARNING.md "Links").
struct Link {
    enum class Kind : uint8_t { Page, Window, Help, External, Invalid };
    Kind kind = Kind::Invalid;
    std::string target;   // Page: the slug (empty: this page); Window: the window id; Help: the tab; External: the URL
    std::string anchor;   // Page: the section, if any
};
// "slug", "slug#anchor", "#anchor", "window:<id>", "help:<tab>", "https://...".
Link parseLink(std::string_view target);

// Every link in blocks, with the line of the block it is in.
struct LinkUse {
    std::string target;
    int line = 0;
};
std::vector<LinkUse> collectLinks(const std::vector<Block>& blocks);
// Every heading anchor of blocks, the title's included.
std::vector<std::string> collectAnchors(const std::vector<Block>& blocks);

} // namespace opense4::learn
