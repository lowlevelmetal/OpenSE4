#include "learn/markdown.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>

namespace opense4::learn {

std::string Diagnostic::text() const {
    if (line > 0) return std::format("{}:{}: {}", file, line, message);
    return file.empty() ? message : std::format("{}: {}", file, message);
}

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && (isSpace(s.front()) || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (isSpace(s.back()) || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool blank(std::string_view s) { return trim(s).empty(); }

// Leading spaces, a tab counting as four.
int indentOf(std::string_view s) {
    int n = 0;
    for (char c : s) {
        if (c == ' ') ++n;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

// ---- Inline markup ---------------------------------------------------------------------------

bool escapable(char c) { return std::ispunct(static_cast<unsigned char>(c)) != 0; }

class InlineParser {
public:
    Inline run(std::string_view text) {
        parse(text, Style{});
        flush(Style{});
        return std::move(out_);
    }

private:
    struct Style {
        bool bold = false;
        bool italic = false;
        std::string link;
    };

    void flush(const Style& st) {
        if (buffer_.empty()) return;
        push(Span{std::move(buffer_), st.bold, st.italic, false, st.link});
        buffer_.clear();
    }

    void push(Span s) {
        // Runs of the same style are merged.
        if (!out_.empty()) {
            Span& last = out_.back();
            if (last.bold == s.bold && last.italic == s.italic && last.code == s.code && last.link == s.link) {
                last.text += s.text;
                return;
            }
        }
        out_.push_back(std::move(s));
    }

    // The closing `]` of a link text that starts at `open`, skipping code and nested brackets.
    static size_t closingBracket(std::string_view t, size_t open) {
        int depth = 0;
        for (size_t i = open; i < t.size(); ++i) {
            if (t[i] == '\\') {
                ++i;
            } else if (t[i] == '`') {
                const size_t end = t.find('`', i + 1);
                if (end == std::string_view::npos) return std::string_view::npos;
                i = end;
            } else if (t[i] == '[') {
                ++depth;
            } else if (t[i] == ']' && --depth == 0) {
                return i;
            }
        }
        return std::string_view::npos;
    }

    void parse(std::string_view t, Style st) {
        for (size_t i = 0; i < t.size();) {
            const char c = t[i];
            if (c == '\\' && i + 1 < t.size() && escapable(t[i + 1])) {
                buffer_ += t[i + 1];
                i += 2;
                continue;
            }
            if (c == '`') {
                if (const size_t end = t.find('`', i + 1); end != std::string_view::npos) {
                    flush(st);
                    push(Span{std::string(t.substr(i + 1, end - i - 1)), st.bold, st.italic, true, st.link});
                    i = end + 1;
                    continue;
                }
            }
            if (c == '*' && i + 1 < t.size() && t[i + 1] == '*') {
                // A bold run opens only when it closes later on.
                if (st.bold || t.find("**", i + 2) != std::string_view::npos) {
                    flush(st);
                    st.bold = !st.bold;
                    i += 2;
                    continue;
                }
            } else if (c == '*') {
                if (st.italic || t.find('*', i + 1) != std::string_view::npos) {
                    flush(st);
                    st.italic = !st.italic;
                    i += 1;
                    continue;
                }
            }
            if (c == '[') {
                const size_t close = closingBracket(t, i);
                if (close != std::string_view::npos && close + 1 < t.size() && t[close + 1] == '(') {
                    if (const size_t end = t.find(')', close + 2); end != std::string_view::npos) {
                        flush(st);
                        Style inner = st;
                        inner.link = std::string(trim(t.substr(close + 2, end - close - 2)));
                        parse(t.substr(i + 1, close - i - 1), inner);
                        flush(inner);
                        i = end + 1;
                        continue;
                    }
                }
            }
            buffer_ += c;
            ++i;
        }
        flush(st);
    }

    Inline out_;
    std::string buffer_;
};

// ---- Blocks ----------------------------------------------------------------------------------

struct Line {
    std::string_view text;
    int number = 0;
};

// A list marker: "- " (bullet) or "12. " (numbered). Returns the text after it.
struct Marker {
    bool numbered = false;
    int number = 0;
    std::string_view rest;
};
std::optional<Marker> listMarker(std::string_view line) {
    const std::string_view t = trim(line);
    if (t.size() >= 2 && t[0] == '-' && isSpace(t[1])) return Marker{false, 0, trim(t.substr(2))};
    if (t == "-") return Marker{false, 0, {}};
    size_t digits = 0;
    while (digits < t.size() && digits < 9 && std::isdigit(static_cast<unsigned char>(t[digits]))) ++digits;
    if (digits > 0 && digits + 1 < t.size() && t[digits] == '.' && isSpace(t[digits + 1])) {
        int n = 0;
        for (size_t i = 0; i < digits; ++i) n = n * 10 + (t[i] - '0');
        return Marker{true, n, trim(t.substr(digits + 2))};
    }
    return std::nullopt;
}

int headingLevel(std::string_view line) {
    if (line.empty() || line[0] != '#') return 0;
    size_t n = 0;
    while (n < line.size() && line[n] == '#') ++n;
    if (n > 6 || (n < line.size() && !isSpace(line[n]))) return 0;
    return static_cast<int>(n);
}

// Cells of a pipe-table row; `|` inside code or escaped stays in the cell.
std::vector<std::string> splitRow(std::string_view line) {
    std::string_view t = trim(line);
    if (!t.empty() && t.front() == '|') t.remove_prefix(1);
    if (!t.empty() && t.back() == '|' && (t.size() < 2 || t[t.size() - 2] != '\\')) t.remove_suffix(1);
    std::vector<std::string> cells;
    std::string cell;
    bool code = false;
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cell += '|';
            ++i;
        } else if (c == '`') {
            code = !code;
            cell += c;
        } else if (c == '|' && !code) {
            cells.emplace_back(trim(cell));
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells.emplace_back(trim(cell));
    return cells;
}

bool separatorRow(std::string_view line, std::vector<Align>* align) {
    const std::string_view t = trim(line);
    if (t.empty() || t.front() != '|') return false;
    std::vector<Align> out;
    for (const std::string& cell : splitRow(t)) {
        const std::string_view c = trim(cell);
        if (c.empty()) return false;
        const bool left = c.front() == ':', right = c.back() == ':';
        const std::string_view dashes = c.substr(left ? 1 : 0, c.size() - (left ? 1 : 0) - (right ? 1 : 0));
        if (dashes.empty() || dashes.find_first_not_of('-') != std::string_view::npos) return false;
        out.push_back(left && right ? Align::Center : right ? Align::Right : Align::Left);
    }
    if (align) *align = std::move(out);
    return true;
}

bool tableStart(const std::vector<Line>& lines, size_t i) {
    return i + 1 < lines.size() && trim(lines[i].text).starts_with('|') && separatorRow(lines[i + 1].text, nullptr);
}

class BlockParser {
public:
    BlockParser(std::string_view file, std::vector<Diagnostic>& problems) : file_(file), problems_(problems) {}

    std::vector<Block> parse(const std::vector<Line>& lines) {
        std::vector<Block> out;
        size_t i = 0;
        while (i < lines.size()) {
            const std::string_view raw = lines[i].text;
            if (blank(raw)) {
                ++i;
                continue;
            }
            const std::string_view t = trim(raw);
            if (const int level = headingLevel(t)) {
                Block b;
                b.kind = Block::Kind::Heading;
                b.line = lines[i].number;
                b.level = level;
                if (level > 3) {
                    problem(b.line, "only #, ## and ### headings are used; this one is shown as ###");
                    b.level = 3;
                }
                std::string_view text = trim(t.substr(static_cast<size_t>(level)));
                while (!text.empty() && text.back() == '#') text.remove_suffix(1);  // "## Title ##"
                b.text = parseInline(trim(text));
                out.push_back(std::move(b));
                ++i;
            } else if (t.starts_with('>')) {
                // A tip box: the quoted lines are parsed as blocks of their own.
                std::vector<Line> inner;
                const int first = lines[i].number;
                while (i < lines.size() && trim(lines[i].text).starts_with('>')) {
                    std::string_view q = trim(lines[i].text).substr(1);
                    if (!q.empty() && q.front() == ' ') q.remove_prefix(1);
                    inner.push_back({q, lines[i].number});
                    ++i;
                }
                Block b;
                b.kind = Block::Kind::Tip;
                b.line = first;
                b.tip = parse(inner);
                for (const Block& x : b.tip)
                    if (x.kind == Block::Kind::Heading || x.kind == Block::Kind::Tip || x.kind == Block::Kind::Table)
                        problem(x.line, "a tip box holds only paragraphs and lists");
                out.push_back(std::move(b));
            } else if (tableStart(lines, i)) {
                out.push_back(table(lines, i));
            } else if (listMarker(raw) && indentOf(raw) < 2) {
                out.push_back(list(lines, i));
            } else {
                Block b;
                b.kind = Block::Kind::Paragraph;
                b.line = lines[i].number;
                std::string text;
                while (i < lines.size() && !blank(lines[i].text)) {
                    const std::string_view l = trim(lines[i].text);
                    if (!text.empty() && (headingLevel(l) || l.starts_with('>') || tableStart(lines, i) ||
                                          (listMarker(lines[i].text) && indentOf(lines[i].text) < 2)))
                        break;
                    if (!text.empty()) text += ' ';
                    text += l;
                    ++i;
                }
                b.text = parseInline(text);
                out.push_back(std::move(b));
            }
        }
        return out;
    }

private:
    void problem(int line, std::string message) { problems_.push_back({std::string(file_), line, std::move(message)}); }

    Block table(const std::vector<Line>& lines, size_t& i) {
        Block b;
        b.kind = Block::Kind::Table;
        b.line = lines[i].number;
        for (const std::string& cell : splitRow(lines[i].text)) b.table.header.push_back(parseInline(cell));
        separatorRow(lines[i + 1].text, &b.table.align);
        const size_t columns = b.table.header.size();
        if (b.table.align.size() != columns) problem(lines[i + 1].number, "the separator row has a different number of columns");
        b.table.align.resize(columns, Align::Left);
        i += 2;
        while (i < lines.size() && trim(lines[i].text).starts_with('|')) {
            std::vector<Inline> row;
            for (const std::string& cell : splitRow(lines[i].text)) row.push_back(parseInline(cell));
            if (row.size() > columns) problem(lines[i].number, std::format("this row has {} cells, the header {}", row.size(), columns));
            row.resize(columns);
            b.table.rows.push_back(std::move(row));
            ++i;
        }
        return b;
    }

    // A list with one level of nesting. Items continue on following lines
    // (indented or not) until a blank line; a blank line followed by another
    // item of the same kind keeps the list going.
    Block list(const std::vector<Line>& lines, size_t& i) {
        Block b;
        b.kind = Block::Kind::List;
        b.line = lines[i].number;
        const Marker first = *listMarker(lines[i].text);
        b.list.numbered = first.numbered;
        b.list.start = first.numbered ? first.number : 1;
        struct Raw {
            std::string text;
            int line = 0;
            std::vector<std::pair<bool, std::string>> sub;   // nested items: (numbered, text)
            int subStart = 1;
        };
        std::vector<Raw> items;
        bool afterBlank = false;
        while (i < lines.size()) {
            const std::string_view raw = lines[i].text;
            if (blank(raw)) {
                afterBlank = true;
                ++i;
                continue;
            }
            const auto marker = listMarker(raw);
            const int indent = indentOf(raw);
            if (marker && indent < 2) {
                if (marker->numbered != b.list.numbered) break;   // another kind of list starts
                items.push_back({std::string(marker->rest), lines[i].number, {}, 1});
            } else if (marker && !items.empty()) {
                Raw& parent = items.back();
                if (indent >= 4 && !parent.sub.empty()) problem(lines[i].number, "lists nest one level only; this item joins the nested list");
                if (!parent.sub.empty() && parent.sub.front().first != marker->numbered)
                    problem(lines[i].number, "a nested list mixes bullets and numbers");
                if (parent.sub.empty()) parent.subStart = marker->numbered ? marker->number : 1;
                parent.sub.emplace_back(marker->numbered, std::string(marker->rest));
            } else if (!afterBlank && !items.empty()) {
                // Continuation of the last item (or of its last nested item).
                const std::string_view l = trim(raw);
                if (headingLevel(l) || l.starts_with('>') || tableStart(lines, i)) break;
                std::string& target = items.back().sub.empty() ? items.back().text : items.back().sub.back().second;
                if (!target.empty()) target += ' ';
                target += l;
            } else {
                break;
            }
            afterBlank = false;
            ++i;
        }
        for (Raw& r : items) {
            ListItem item;
            item.text = parseInline(r.text);
            if (!r.sub.empty()) {
                List sub;
                sub.numbered = r.sub.front().first;
                sub.start = r.subStart;
                for (auto& [numbered, text] : r.sub) sub.items.push_back(ListItem{parseInline(text), {}});
                item.sub.push_back(std::move(sub));
            }
            b.list.items.push_back(std::move(item));
        }
        return b;
    }

    std::string_view file_;
    std::vector<Diagnostic>& problems_;
};

void assignAnchors(std::vector<Block>& blocks, std::map<std::string, int>& seen) {
    for (Block& b : blocks) {
        if (b.kind == Block::Kind::Heading) {
            std::string anchor = anchorOf(plainText(b.text));
            const int n = seen[anchor]++;
            if (n > 0) anchor += std::format("-{}", n);
            b.anchor = std::move(anchor);
        } else if (b.kind == Block::Kind::Tip) {
            assignAnchors(b.tip, seen);
        }
    }
}

void appendPlain(std::string& out, const Inline& text) {
    if (!out.empty() && out.back() != '\n') out += ' ';
    out += plainText(text);
}

void plainBlocks(std::string& out, const std::vector<Block>& blocks);

void plainList(std::string& out, const List& l) {
    for (const ListItem& item : l.items) {
        appendPlain(out, item.text);
        for (const List& sub : item.sub) plainList(out, sub);
    }
}

void plainBlocks(std::string& out, const std::vector<Block>& blocks) {
    for (const Block& b : blocks) {
        switch (b.kind) {
            case Block::Kind::Heading:
            case Block::Kind::Paragraph: appendPlain(out, b.text); break;
            case Block::Kind::List: plainList(out, b.list); break;
            case Block::Kind::Table:
                for (const Inline& cell : b.table.header) appendPlain(out, cell);
                for (const auto& row : b.table.rows)
                    for (const Inline& cell : row) appendPlain(out, cell);
                break;
            case Block::Kind::Tip: plainBlocks(out, b.tip); break;
        }
        out += '\n';
    }
}

void linksOf(std::vector<LinkUse>& out, const Inline& text, int line) {
    std::string_view last;
    for (const Span& s : text) {
        if (!s.link.empty() && s.link != last) out.push_back({s.link, line});
        last = s.link;
    }
}

void linksOfList(std::vector<LinkUse>& out, const List& l, int line) {
    for (const ListItem& item : l.items) {
        linksOf(out, item.text, line);
        for (const List& sub : item.sub) linksOfList(out, sub, line);
    }
}

} // namespace

Inline parseInline(std::string_view text) { return InlineParser{}.run(text); }

Document parseMarkdown(std::string_view text, std::string_view file, bool frontMatter) {
    Document doc;
    std::vector<Line> lines;
    int number = 1;
    for (size_t pos = 0; pos <= text.size(); ++number) {
        const size_t end = text.find('\n', pos);
        std::string_view l = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        lines.push_back({l, number});
        if (end == std::string_view::npos) break;
        pos = end + 1;
    }
    size_t first = 0;
    if (frontMatter && !lines.empty() && trim(lines[0].text) == "---") {
        size_t close = 1;
        while (close < lines.size() && trim(lines[close].text) != "---") ++close;
        if (close == lines.size()) {
            doc.problems.push_back({std::string(file), 1, "the front matter has no closing '---'"});
        } else {
            for (size_t i = 1; i < close; ++i) {
                const std::string_view l = trim(lines[i].text);
                if (l.empty() || l.starts_with('#')) continue;
                const size_t colon = l.find(':');
                const std::string_view key = colon == std::string_view::npos ? l : trim(l.substr(0, colon));
                if (key != "windows") {
                    doc.problems.push_back({std::string(file), lines[i].number, std::format("unknown front matter key '{}'", key)});
                    continue;
                }
                std::string_view rest = colon == std::string_view::npos ? std::string_view{} : l.substr(colon + 1);
                while (!rest.empty()) {
                    const size_t comma = rest.find(',');
                    const std::string_view id = trim(rest.substr(0, comma));
                    if (!id.empty()) doc.windows.emplace_back(id);
                    if (comma == std::string_view::npos) break;
                    rest.remove_prefix(comma + 1);
                }
            }
            first = close + 1;
        }
    }
    BlockParser parser(file, doc.problems);
    doc.blocks = parser.parse(std::vector<Line>(lines.begin() + static_cast<std::ptrdiff_t>(first), lines.end()));
    std::map<std::string, int> seen;
    assignAnchors(doc.blocks, seen);
    for (const Block& b : doc.blocks) {
        if (b.kind != Block::Kind::Heading || b.level != 1) continue;
        if (doc.title.empty()) doc.title = plainText(b.text);
        else doc.problems.push_back({std::string(file), b.line, "a page has one '#' title; use ## for sections"});
    }
    return doc;
}

std::string anchorOf(std::string_view heading) {
    std::string out;
    for (const char ch : heading) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (c == ' ' || c == '-') out += '-';
        else if (c >= 0x80) out += ch;   // letters of other alphabets (UTF-8) stay
    }
    return out;
}

std::string plainText(const Inline& text) {
    std::string out;
    for (const Span& s : text) out += s.text;
    return out;
}

std::string plainText(const std::vector<Block>& blocks) {
    std::string out;
    plainBlocks(out, blocks);
    return out;
}

Link parseLink(std::string_view target) {
    target = trim(target);
    Link l;
    if (target.empty()) return l;
    if (target.starts_with("window:")) {
        l.kind = Link::Kind::Window;
        l.target = std::string(trim(target.substr(7)));
    } else if (target.starts_with("help:")) {
        l.kind = Link::Kind::Help;
        l.target = std::string(trim(target.substr(5)));
    } else if (target.starts_with("https://") || target.starts_with("http://")) {
        l.kind = Link::Kind::External;
        l.target = std::string(target);
        return l;
    } else if (target.find(':') != std::string_view::npos || target.find_first_of(" \t") != std::string_view::npos) {
        return l;   // an unknown scheme
    } else {
        l.kind = Link::Kind::Page;
        const size_t hash = target.find('#');
        l.target = std::string(target.substr(0, hash));
        if (hash != std::string_view::npos) l.anchor = std::string(target.substr(hash + 1));
        if (l.target.empty() && l.anchor.empty()) l.kind = Link::Kind::Invalid;
        return l;
    }
    if (l.target.empty()) l.kind = Link::Kind::Invalid;
    return l;
}

std::vector<LinkUse> collectLinks(const std::vector<Block>& blocks) {
    std::vector<LinkUse> out;
    for (const Block& b : blocks) {
        switch (b.kind) {
            case Block::Kind::Heading:
            case Block::Kind::Paragraph: linksOf(out, b.text, b.line); break;
            case Block::Kind::List: linksOfList(out, b.list, b.line); break;
            case Block::Kind::Table:
                for (const Inline& cell : b.table.header) linksOf(out, cell, b.line);
                for (const auto& row : b.table.rows)
                    for (const Inline& cell : row) linksOf(out, cell, b.line);
                break;
            case Block::Kind::Tip: {
                auto inner = collectLinks(b.tip);
                out.insert(out.end(), inner.begin(), inner.end());
                break;
            }
        }
    }
    return out;
}

std::vector<std::string> collectAnchors(const std::vector<Block>& blocks) {
    std::vector<std::string> out;
    for (const Block& b : blocks) {
        if (b.kind == Block::Kind::Heading) out.push_back(b.anchor);
        else if (b.kind == Block::Kind::Tip) {
            auto inner = collectAnchors(b.tip);
            out.insert(out.end(), inner.begin(), inner.end());
        }
    }
    return out;
}

} // namespace opense4::learn
