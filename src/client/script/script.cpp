#include "client/script/script.hpp"

#include "learn/lesson.hpp"

#include <charconv>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace opense4::client::script {

namespace {

// A word of a line; `quoted` marks the characters that stood in quotes.
struct Token {
    std::string text;
    std::vector<bool> quoted;
    size_t start = 0;   // where it begins in the line
};

// Splits a line into words: spaces separate them, "..." quotes (with \" and
// \\ inside), and a # that begins a word starts a comment.
std::vector<Token> tokenize(std::string_view line, std::string& error) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] == '#') break;
        Token t;
        t.start = i;
        bool any = false;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            if (line[i] == '"') {
                ++i;
                bool closed = false;
                while (i < line.size()) {
                    char c = line[i];
                    if (c == '\\' && i + 1 < line.size()) {
                        c = line[++i];
                    } else if (c == '"') {
                        closed = true;
                        ++i;
                        break;
                    }
                    t.text += c;
                    t.quoted.push_back(true);
                    ++i;
                }
                if (!closed) {
                    error = "a quote is not closed";
                    return {};
                }
                any = true;
                continue;
            }
            t.text += line[i];
            t.quoted.push_back(false);
            any = true;
            ++i;
        }
        if (any) out.push_back(std::move(t));
    }
    return out;
}

bool parseInt(std::string_view s, int64_t& out) {
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && p == s.data() + s.size();
}

bool parseFloat(std::string_view s, float& out) {
    // Whole numbers or one decimal point; from_chars for floats is not everywhere yet.
    bool negative = false;
    if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    if (s.empty()) return false;
    double value = 0, scale = 0;
    for (const char c : s) {
        if (c == '.') {
            if (scale != 0) return false;
            scale = 1;
            continue;
        }
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
        if (scale != 0) scale *= 10;
    }
    if (scale != 0) value /= scale;
    out = static_cast<float>(negative ? -value : value);
    return true;
}

// "12,-4" or "50%,10%".
bool parsePair(std::string_view s, float& x, float& y, bool* xPercent = nullptr, bool* yPercent = nullptr) {
    const size_t comma = s.find(',');
    if (comma == std::string_view::npos) return false;
    std::string_view a = s.substr(0, comma), b = s.substr(comma + 1);
    auto percent = [](std::string_view& v, bool* flag) {
        const bool p = !v.empty() && v.back() == '%';
        if (p) v.remove_suffix(1);
        if (flag) *flag = p;
        return p;
    };
    if ((percent(a, xPercent) && !xPercent) || (percent(b, yPercent) && !yPercent)) return false;
    return parseFloat(a, x) && parseFloat(b, y);
}

std::optional<Target> parseTarget(const Token& tok, std::string& error) {
    const std::string& s = tok.text;
    const size_t colon = s.find(':');
    if (colon == std::string::npos || colon == 0 || tok.quoted[0]) {
        error = std::format("'{}' is not a target (tag:, item:, window:, sector:, system: or at:)", s);
        return std::nullopt;
    }
    const std::string kind = s.substr(0, colon);
    Target t;
    t.text = s;
    if (kind == "tag") t.kind = TargetKind::Tag;
    else if (kind == "item") t.kind = TargetKind::Item;
    else if (kind == "window") t.kind = TargetKind::Window;
    else if (kind == "sector") t.kind = TargetKind::Sector;
    else if (kind == "system") t.kind = TargetKind::System;
    else if (kind == "at") t.kind = TargetKind::At;
    else {
        error = std::format("unknown target kind '{}:' (tag:, item:, window:, sector:, system: or at:)", kind);
        return std::nullopt;
    }
    // An unquoted @ starts the offset.
    size_t end = s.size();
    for (size_t i = s.size(); i-- > colon + 1;)
        if (s[i] == '@' && !tok.quoted[i]) {
            end = i;
            break;
        }
    t.name = s.substr(colon + 1, end - colon - 1);
    if (end < s.size()) {
        Offset& o = t.offset;
        o.set = true;
        if (!parsePair(std::string_view(s).substr(end + 1), o.x, o.y, &o.xPercent, &o.yPercent)) {
            error = std::format("'{}': an offset is @x,y in frame pixels (negative: from the right or bottom) or @x%,y%", s);
            return std::nullopt;
        }
    }
    if (t.name.empty()) {
        error = std::format("'{}' names nothing", s);
        return std::nullopt;
    }
    if (t.kind == TargetKind::At) {
        if (!parsePair(t.name, t.x, t.y)) {
            error = std::format("'{}': at: takes x,y in frame pixels", s);
            return std::nullopt;
        }
    } else if (t.kind == TargetKind::Sector) {
        if (parsePair(t.name, t.x, t.y)) t.numeric = true;
    }
    return t;
}

struct OpInfo {
    std::string_view name;
    Op op;
};
constexpr OpInfo kOps[] = {
    {"click", Op::Click},
    {"double-click", Op::DoubleClick},
    {"right-click", Op::RightClick},
    {"middle-click", Op::MiddleClick},
    {"drag", Op::Drag},
    {"move", Op::Move},
    {"wheel", Op::Wheel},
    {"key", Op::Key},
    {"type", Op::Type},
    {"window-event", Op::WindowEvent},
    {"wait", Op::Wait},
    {"wait-for", Op::WaitFor},
    {"wait-gone", Op::WaitGone},
    {"wait-window", Op::WaitWindow},
    {"wait-closed", Op::WaitClosed},
    {"wait-step", Op::WaitStep},
    {"wait-until", Op::WaitUntil},
    {"wait-turn", Op::WaitTurn},
    {"wait-result", Op::WaitResult},
    {"wait-screen", Op::WaitScreen},
    {"wait-lesson", Op::WaitLesson},
    {"assert-present", Op::AssertPresent},
    {"assert-absent", Op::AssertAbsent},
    {"assert-enabled", Op::AssertEnabled},
    {"assert-disabled", Op::AssertDisabled},
    {"assert-window", Op::AssertWindow},
    {"assert-no-window", Op::AssertNoWindow},
    {"assert-step", Op::AssertStep},
    {"assert", Op::Assert},
    {"assert-log", Op::AssertLog},
    {"assert-no-log", Op::AssertNoLog},
    {"assert-result", Op::AssertResult},
    {"assert-screen", Op::AssertScreen},
    {"assert-lesson", Op::AssertLesson},
    {"assert-turn", Op::AssertTurn},
    {"assert-inside", Op::AssertInside},
    {"assert-fits", Op::AssertFits},
    {"screenshot", Op::Screenshot},
    {"echo", Op::Echo},
    {"print", Op::Print},
    {"dump", Op::Dump},
    {"audit", Op::Audit},
    {"repeat", Op::Repeat},
    {"end", Op::End},
};

constexpr std::pair<WindowChange, std::string_view> kWindowChanges[] = {
    {WindowChange::FocusLost, "focus-lost"}, {WindowChange::FocusGained, "focus-gained"}, {WindowChange::Minimized, "minimized"},
    {WindowChange::Restored, "restored"},    {WindowChange::Hidden, "hidden"},            {WindowChange::Shown, "shown"},
    {WindowChange::Occluded, "occluded"},    {WindowChange::Exposed, "exposed"},
};

bool takesTarget(Op op) {
    switch (op) {
        case Op::Click:
        case Op::DoubleClick:
        case Op::RightClick:
        case Op::MiddleClick:
        case Op::Drag:
        case Op::Move:
        case Op::Wheel:
        case Op::WaitFor:
        case Op::WaitGone:
        case Op::AssertPresent:
        case Op::AssertAbsent:
        case Op::AssertEnabled:
        case Op::AssertDisabled:
        case Op::AssertInside: return true;
        default: return false;
    }
}

bool isPointer(Op op) {
    return op == Op::Click || op == Op::DoubleClick || op == Op::RightClick || op == Op::MiddleClick || op == Op::Drag || op == Op::Move ||
           op == Op::Wheel;
}

} // namespace

std::string_view windowChangeName(WindowChange c) {
    for (const auto& [change, name] : kWindowChanges)
        if (change == c) return name;
    return "?";
}

std::string_view opName(Op op) {
    for (const OpInfo& i : kOps)
        if (i.op == op) return i.name;
    return "?";
}

std::string quoteWord(std::string_view text) {
    bool plain = !text.empty();
    for (const char c : text)
        if (c == ' ' || c == '\t' || c == '"' || c == '\\' || c == '#') plain = false;
    if (plain) return std::string(text);
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::optional<Script> parseScript(std::string_view text, std::string_view file, std::vector<std::string>& errors) {
    Script script;
    script.file = std::string(file);
    int timeout = kDefaultTimeout;
    const size_t errorsBefore = errors.size();
    std::vector<size_t> open;   // repeats without their end yet
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        auto fail = [&](const std::string& message) { errors.push_back(std::format("{}:{}: {}", file, lineNo, message)); };

        std::string error;
        std::vector<Token> tokens = tokenize(line, error);
        if (!error.empty()) {
            fail(error);
            continue;
        }
        if (tokens.empty()) continue;
        const std::string verb = tokens.front().text;

        if (verb == "options") {
            if (!script.steps.empty()) fail("'options' come before the first step");
            for (size_t i = 1; i < tokens.size(); ++i) script.options.push_back(tokens[i].text);
            continue;
        }
        if (verb == "timeout") {
            int64_t n = 0;
            if (tokens.size() != 2 || !parseInt(tokens[1].text, n) || n < 1) fail("'timeout' takes a number of frames");
            else timeout = static_cast<int>(n);
            continue;
        }
        const OpInfo* info = nullptr;
        for (const OpInfo& i : kOps)
            if (i.name == verb) info = &i;
        if (!info) {
            fail(std::format("unknown step '{}'", verb));
            continue;
        }

        Step st;
        st.line = lineNo;
        st.op = info->op;
        st.timeout = timeout;
        // The line without its comment.
        {
            const Token& lastTok = tokens.back();
            size_t end = lastTok.start;
            // Up to the end of the last word (quotes included).
            bool inQuote = false;
            while (end < line.size()) {
                const char c = line[end];
                if (c == '"' && (end == 0 || line[end - 1] != '\\')) inQuote = !inQuote;
                if (!inQuote && (c == ' ' || c == '\t')) break;
                ++end;
            }
            st.source = std::string(line.substr(0, end));
            while (!st.source.empty() && (st.source.front() == ' ' || st.source.front() == '\t')) st.source.erase(st.source.begin());
        }

        // Conditions take the rest of the line as written (TOML), after any options.
        if (st.op == Op::WaitUntil || st.op == Op::Assert) {
            size_t i = 1;
            for (; i < tokens.size(); ++i) {
                const std::string& w = tokens[i].text;
                if (w.starts_with("timeout=")) {
                    int64_t n = 0;
                    if (!parseInt(std::string_view(w).substr(8), n) || n < 1) fail("timeout= takes a number of frames");
                    st.timeout = static_cast<int>(n);
                    continue;
                }
                break;
            }
            if (i >= tokens.size()) {
                fail(std::format("'{}' needs a condition, such as {{ colonies = 2 }}", verb));
                continue;
            }
            const std::string_view cond = line.substr(tokens[i].start);
            std::vector<learn::Diagnostic> problems;
            st.condition = learn::parseCondition(cond, file, lineNo, problems);
            st.source = std::string(line);
            for (const learn::Diagnostic& d : problems) fail(d.message);
            if (!st.condition) continue;
            script.steps.push_back(std::move(st));
            continue;
        }

        // repeat N [until {condition}]: the condition is TOML, as for wait-until.
        if (st.op == Op::Repeat) {
            int64_t n = 0;
            if (tokens.size() < 2 || !parseInt(tokens[1].text, n) || n < 1) {
                fail("'repeat' takes how many times at most, and optionally until { condition }");
                continue;
            }
            st.number = n;
            if (tokens.size() > 2) {
                if (tokens[2].text != "until" || tokens.size() < 4) {
                    fail("'repeat N' is followed by nothing, or by until { condition }");
                    continue;
                }
                std::vector<learn::Diagnostic> problems;
                st.condition = learn::parseCondition(line.substr(tokens[3].start), file, lineNo, problems);
                st.source = std::string(line);
                for (const learn::Diagnostic& d : problems) fail(d.message);
                if (!st.condition) continue;
            }
            open.push_back(script.steps.size());
            script.steps.push_back(std::move(st));
            continue;
        }
        if (st.op == Op::End) {
            if (tokens.size() != 1) {
                fail("'end' stands alone");
                continue;
            }
            if (open.empty()) {
                fail("'end' without a 'repeat'");
                continue;
            }
            st.jump = open.back();
            script.steps[open.back()].jump = script.steps.size();
            open.pop_back();
            script.steps.push_back(std::move(st));
            continue;
        }

        // The other steps: their arguments, then key=value options and flags.
        std::vector<const Token*> args;
        Target* lastTarget = nullptr;
        bool explicitTimeout = false;
        bool sawTo = false;
        bool bad = false;
        for (size_t i = 1; i < tokens.size() && !bad; ++i) {
            const Token& tok = tokens[i];
            const std::string& w = tok.text;
            const bool anyQuoted = !tok.quoted.empty() && tok.quoted.front();
            auto option = [&](std::string_view key) { return !anyQuoted && w.starts_with(key) && w.size() > key.size(); };
            if (option("timeout=") || option("nth=") || option("frames=")) {
                const size_t eq = w.find('=');
                int64_t n = 0;
                if (!parseInt(std::string_view(w).substr(eq + 1), n) || n < 1) {
                    fail(std::format("'{}' takes a whole number of at least 1", w.substr(0, eq + 1)));
                    bad = true;
                    break;
                }
                if (w.starts_with("timeout=")) {
                    st.timeout = static_cast<int>(n);
                    explicitTimeout = true;
                }
                else if (w.starts_with("frames=")) st.dragFrames = static_cast<int>(n);
                else if (lastTarget) lastTarget->nth = static_cast<int>(n);
                else {
                    fail("nth= follows a target");
                    bad = true;
                }
                continue;
            }
            if (option("in=")) {
                if (!lastTarget) {
                    fail("in= follows a target");
                    bad = true;
                    break;
                }
                lastTarget->scope = w.substr(3);
                if (const size_t at = lastTarget->scope.rfind('@'); at != std::string::npos && at + 1 < lastTarget->scope.size() &&
                                                                    !tok.quoted[3 + at] && lastTarget->scope.find(',', at) != std::string::npos) {
                    fail(std::format("'{}': an offset goes right after the target (item:\"label\"@x,y in=scope)", w));
                    bad = true;
                    break;
                }
                continue;
            }
            if (!anyQuoted && isPointer(st.op) && (w == "shift" || w == "ctrl" || w == "alt" || w == "refused" || w == "optional")) {
                if (w == "shift") st.shift = true;
                if (w == "ctrl") st.ctrl = true;
                if (w == "alt") st.alt = true;
                if (w == "refused") st.refused = true;
                if (w == "optional") st.optional = true;
                continue;
            }
            if (!anyQuoted && st.op == Op::Key && w == "refused") {
                st.refused = true;
                continue;
            }
            if (!anyQuoted && st.op == Op::Drag && w == "to") {
                sawTo = true;
                continue;
            }
            if (!anyQuoted && st.op == Op::Drag && (w == "middle" || w == "right")) {
                st.button = w == "middle" ? 2 : 3;
                continue;
            }
            if (takesTarget(st.op) && (args.empty() || (st.op == Op::Drag && sawTo && args.size() == 1) || (st.op == Op::AssertInside && args.size() == 1))) {
                auto t = parseTarget(tok, error);
                if (!t) {
                    fail(error);
                    bad = true;
                    break;
                }
                if (args.empty()) {
                    st.target = std::move(*t);
                    lastTarget = &st.target;
                } else {
                    st.to = std::move(*t);
                    lastTarget = &st.to;
                }
                args.push_back(&tok);
                continue;
            }
            args.push_back(&tok);
        }
        if (bad) continue;

        auto needArgs = [&](size_t n, std::string_view what) {
            if (args.size() == n) return true;
            fail(std::format("'{}' takes {}", verb, what));
            return false;
        };
        auto number = [&](const Token* t, int64_t min, std::string_view what) {
            int64_t n = 0;
            if (!parseInt(t->text, n) || n < min) {
                fail(std::format("'{}' takes {}", verb, what));
                return false;
            }
            st.number = n;
            return true;
        };
        bool ok = true;
        switch (st.op) {
            case Op::Click:
            case Op::DoubleClick:
            case Op::RightClick:
            case Op::MiddleClick:
            case Op::Move:
            case Op::WaitFor:
            case Op::WaitGone:
            case Op::AssertPresent:
            case Op::AssertAbsent:
            case Op::AssertEnabled:
            case Op::AssertDisabled: ok = needArgs(1, "a target (tag:, item:, window:, sector:, system: or at:)"); break;
            case Op::AssertInside: ok = needArgs(2, "two targets: the point of the first must lie in the second"); break;
            case Op::Drag:
                ok = needArgs(2, "a target, 'to' and a second target") && sawTo;
                if (args.size() == 2 && !sawTo) fail("'drag' takes a target, 'to' and a second target");
                break;
            case Op::Wheel: {
                ok = needArgs(2, "a target and the notches to turn (positive: up)");
                if (ok) {
                    int64_t n = 0;
                    ok = parseInt(args[1]->text, n) && n != 0;
                    if (!ok) fail("'wheel' turns a whole number of notches other than 0");
                    st.number = n;
                }
                break;
            }
            case Op::Key: {
                ok = needArgs(1, "a key chord (\"F12\", \"Ctrl+H\", \"Shift+F1\", \"Escape\")");
                if (ok) {
                    const auto chord = parseChord(args[0]->text);
                    ok = chord && !chord->empty();
                    if (!ok) fail(std::format("unknown key '{}' (written as \"F12\", \"Ctrl+H\", \"Alt+LeftArrow\", \"Escape\")", args[0]->text));
                    else st.chord = *chord;
                    st.text = args[0]->text;
                }
                break;
            }
            case Op::Type:
                ok = needArgs(1, "the text to type (in quotes when it has spaces)");
                if (ok) st.text = args[0]->text;
                if (ok && st.text.empty()) {
                    fail("'type' needs some text");
                    ok = false;
                }
                break;
            case Op::WindowEvent: {
                constexpr std::string_view what = "focus-lost, focus-gained, minimized, restored, hidden, shown, occluded or exposed";
                ok = needArgs(1, what);
                if (!ok) break;
                ok = false;
                for (const auto& [change, name] : kWindowChanges)
                    if (args[0]->text == name) {
                        st.window = change;
                        ok = true;
                    }
                if (!ok) fail(std::format("'window-event' takes {}", what));
                break;
            }
            case Op::Wait: ok = needArgs(1, "a number of frames") && number(args[0], 1, "a number of frames"); break;
            case Op::WaitStep:
            case Op::AssertStep: ok = needArgs(1, "a step number (from 1)") && number(args[0], 1, "a step number (from 1)"); break;
            case Op::WaitTurn:
            case Op::AssertTurn: ok = needArgs(1, "a turn number (the first turn is 0)") && number(args[0], 0, "a turn number (the first turn is 0)"); break;
            case Op::WaitWindow:
            case Op::WaitClosed:
            case Op::AssertWindow:
            case Op::AssertNoWindow:
                ok = needArgs(1, "a window id (docs/LEARNING.md \"Window ids\")");
                if (ok) st.text = args[0]->text;
                break;
            case Op::WaitResult:
            case Op::AssertResult:
                ok = needArgs(1, "none, done, won or lost");
                if (ok) {
                    st.text = args[0]->text;
                    ok = st.text == "none" || st.text == "done" || st.text == "won" || st.text == "lost";
                    if (!ok) fail(std::format("'{}' takes none, done, won or lost", verb));
                }
                break;
            case Op::WaitScreen:
            case Op::AssertScreen:
                ok = needArgs(1, "game or front");
                if (ok) {
                    st.text = args[0]->text;
                    ok = st.text == "game" || st.text == "front";
                    if (!ok) fail(std::format("'{}' takes game or front", verb));
                }
                break;
            case Op::WaitLesson:
            case Op::AssertLesson:
                ok = needArgs(1, "a lesson's slug (or none)");
                if (ok) st.text = args[0]->text;
                break;
            case Op::AssertLog:
            case Op::AssertNoLog:
                ok = needArgs(1, "the text to look for (in quotes when it has spaces)");
                if (ok) st.text = args[0]->text;
                break;
            case Op::Screenshot:
                ok = needArgs(1, "a file name (.png)");
                if (ok) st.text = args[0]->text;
                break;
            case Op::Echo:
                for (const Token* t : args) st.text += (st.text.empty() ? "" : " ") + t->text;
                break;
            case Op::Audit:
                ok = args.empty();
                if (!ok) fail("'audit' takes nothing");
                break;
            case Op::Dump:
                ok = args.size() <= 1;
                if (!ok) fail("'dump' takes at most a scope (a window id, main, lesson, front)");
                if (ok && !args.empty()) st.text = args[0]->text;
                break;
            case Op::AssertFits:
                ok = args.size() == 1;
                if (!ok) fail("'assert-fits' takes a scope (a window id, main, lesson, front, or a Dear ImGui window's name)");
                if (ok) st.text = args[0]->text;
                break;
            case Op::Print:
                ok = !args.empty();
                if (!ok) fail("'print' takes condition keys with numbers (colonies, turn, systems_explored, ...)");
                for (const Token* t : args) {
                    const learn::FactInfo* f = learn::findFact(t->text);
                    if (!f || f->value != learn::FactValue::Number) {
                        fail(std::format("'{}' is not a condition key with a number", t->text));
                        ok = false;
                    }
                    st.facts.push_back(t->text);
                }
                break;
            case Op::WaitUntil:
            case Op::Assert:
            case Op::Repeat:
            case Op::End: break;
        }
        if (st.optional && !explicitTimeout) st.timeout = kOptionalTimeout;
        if (ok) script.steps.push_back(std::move(st));
    }
    for (const size_t i : open) errors.push_back(std::format("{}:{}: this 'repeat' has no 'end'", file, script.steps[i].line));
    if (errors.size() != errorsBefore) return std::nullopt;
    if (script.steps.empty()) {
        errors.push_back(std::format("{}: no steps", file));
        return std::nullopt;
    }
    return script;
}

std::optional<Script> loadScript(const std::filesystem::path& path, std::vector<std::string>& errors) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors.push_back(std::format("{}: cannot be read", path.string()));
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parseScript(text, path.string(), errors);
}

} // namespace opense4::client::script
