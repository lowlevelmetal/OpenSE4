#include "client/classic/lesson_audit.hpp"

#include "learn/condition.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return out;
}

bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// The lower-case words of a text (letters and digits).
std::vector<std::string> wordsOf(std::string_view s) {
    std::vector<std::string> out;
    std::string w;
    for (const char c : s) {
        if (isLetter(c)) {
            w += c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        } else if (!w.empty()) {
            out.push_back(std::move(w));
            w.clear();
        }
    }
    if (!w.empty()) out.push_back(std::move(w));
    return out;
}

bool has(const std::vector<std::string>& v, std::string_view x) { return std::find(v.begin(), v.end(), x) != v.end(); }

std::string trim(std::string_view s) {
    auto edge = [](char c) { return c == ' ' || c == '"' || c == '(' || c == ')' || c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?'; };
    while (!s.empty() && edge(s.front())) s.remove_prefix(1);
    while (!s.empty() && edge(s.back())) s.remove_suffix(1);
    return std::string(s);
}

// Words that start a sentence or an instruction, not a name on the screen.
bool isStopWord(std::string_view w) {
    static constexpr std::string_view kStop[] = {
        "a", "add", "after", "an", "and", "any", "at", "before", "both", "but", "check", "choose", "click", "close", "each", "end",
        "every", "find", "first", "fix", "for", "from", "give", "here", "how", "if", "in", "it", "its", "later", "look", "move",
        "name", "no", "now", "on", "once", "one", "only", "open", "or", "out", "pick", "play", "point", "press", "propose", "read",
        "right", "select", "send", "set", "so", "some", "start", "switch", "that", "the", "their", "then", "there", "these", "this",
        "those", "three", "to", "try", "turn", "two", "type", "up", "use", "watch", "what", "when", "while", "with", "without",
        "you", "your", "welcome", "well", "done", "green", "red", "other", "another", "more", "most", "less", "key"};
    return std::find(std::begin(kStop), std::end(kStop), lower(w)) != std::end(kStop);
}

// Phrases that name a part of the screen without a label of their own.
struct Phrase {
    std::string_view words;                 // as it appears in the text (lower case)
    std::vector<std::string_view> tags;     // the parts it names
};
const std::vector<Phrase>& phrases() {
    static const std::vector<Phrase> kPhrases = {
        {"report panel", {"panel:report"}},
        {"the report", {"panel:report"}},
        {"system view", {"panel:system"}},
        {"galaxy panel", {"panel:galaxy"}},
        {"status bar", {"status:empire", "status:date", "status:resources"}},
        {"command buttons", {"panel:commands"}},
        {"command button", {"command:*"}},
        {"order buttons", {"panel:orders"}},
        {"ship arrows", {"cycle:ship"}},
        {"colony arrows", {"cycle:colony"}},
        {"selectors", {"cycle:ship", "cycle:fleet", "cycle:colony"}},
        {"the tabs", {"*tabs"}},
        {"its tabs", {"*tabs"}},
        {"the list", {"*list"}},
        {"in the list", {"*list"}},
        {"on the right", {"*right"}},
        {"title strip", {"*title", "research:points"}},
        {"top line", {"research:points", "*title"}},
        {"column", {"*column"}},
        {"the map", {"tactical-combat:map", "panel:system"}},
        {"portrait", {"empires:list"}},
        {"the strip above", {"create-design:on-design"}},
        {"projects are listed below", {"research:queue"}},
    };
    return kPhrases;
}

void addRef(std::vector<std::string>& out, std::string ref) {
    ref = trim(ref);
    if (ref.empty() || ref.size() < 2) return;
    if (std::none_of(out.begin(), out.end(), [&](const std::string& r) { return lower(r) == lower(ref); })) out.push_back(std::move(ref));
}

// Runs of capitalised words ("Components Available", "Design Name"), with
// "on", "of", "for", "to" and "&" kept between two of them; a stop word that
// starts a run is dropped.
void capitalised(std::string_view text, std::vector<std::string>& out) {
    std::vector<std::string> run;
    std::string pending;     // a connector waiting for the next capitalised word
    bool sentenceStart = true;   // the next word starts a sentence
    bool runAtStart = false;     // the run began a sentence
    auto flush = [&] {
        while (!run.empty() && isStopWord(run.front())) {
            run.erase(run.begin());
            runAtStart = false;
        }
        // One capitalised word that begins a sentence is how sentences begin ("Ships cost upkeep").
        if (run.size() == 1 && runAtStart) run.clear();
        if (!run.empty()) {
            std::string s;
            for (const std::string& w : run) s += (s.empty() ? "" : " ") + w;
            addRef(out, s);
        }
        run.clear();
        pending.clear();
    };
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ' || text[i] == '\n') {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\n') ++j;
        std::string word(text.substr(i, j - i));
        const bool sentenceEnd = !word.empty() && (word.back() == '.' || word.back() == ',' || word.back() == ':' || word.back() == ';' ||
                                                   word.back() == '!' || word.back() == '?' || word.back() == ')');
        const std::string bare = trim(word);
        std::string core = bare;
        while (!core.empty() && (core.front() == '(' || core.front() == '"')) core.erase(core.begin());
        const bool cap = !core.empty() && core.front() >= 'A' && core.front() <= 'Z' && core != "KEY";
        const bool atStart = sentenceStart;
        sentenceStart = sentenceEnd && word.back() != ',' && word.back() != ')';
        if (cap) {
            if (run.empty()) runAtStart = atStart;
            if (!pending.empty() && !run.empty()) run.push_back(pending);
            pending.clear();
            run.push_back(core);
        } else if (!run.empty() && (core == "on" || core == "of" || core == "for" || core == "to" || core == "&") && pending.empty()) {
            pending = core;
        } else {
            flush();
        }
        if (sentenceEnd) flush();
        i = j;
    }
    flush();
}

void inlineRefs(const learn::Inline& spans, std::vector<std::string>& out, std::string& plain) {
    // Runs of one weight, a key (`Esc`) kept as a word of its run: "**Close the Log (`Esc`), then open Colonies**".
    std::vector<std::pair<bool, std::string>> runs;
    for (const learn::Span& s : spans) {
        plain += s.text;
        const std::string text = s.code ? "KEY" : s.text;
        if (!runs.empty() && runs.back().first == s.bold) runs.back().second += text;
        else runs.emplace_back(s.bold, text);
    }
    for (const auto& [bold, text] : runs) {
        if (bold && wordsOf(text).size() <= 4) {
            // A short bold term names a thing ("**Create**", "**Ship Designs**"), after its verb; a term in
            // lower case names an idea ("**minerals**", "**explored**"), not a label.
            std::string t = trim(text);
            const size_t space = t.find(' ');
            if (space != std::string::npos && isStopWord(t.substr(0, space))) t = trim(t.substr(space + 1));
            const bool label = !t.empty() && ((t.front() >= 'A' && t.front() <= 'Z') || t.front() == '{');
            if (label && !isStopWord(t) && t.find("KEY") == std::string::npos) addRef(out, t);
            else if (!label) capitalised(t, out);
            continue;
        }
        capitalised(text, out);
    }
    plain += ' ';
}

std::string visible(std::string_view label) {
    if (label.starts_with("text:")) label.remove_prefix(5);
    const size_t hash = label.find("##");
    if (hash != std::string_view::npos) label = label.substr(0, hash);
    return trim(label);
}

bool onScreen(const LockArea& a, ImVec2 display) {
    return a.max.x > a.min.x && a.max.y > a.min.y && a.max.x > 0 && a.max.y > 0 && a.min.x < display.x && a.min.y < display.y;
}

// The widget part of a tag's id ("create-design:warnings" "warnings", "window:designs" "designs").
std::string_view widgetOf(std::string_view tag) {
    const size_t colon = tag.find(':');
    return colon == std::string_view::npos ? tag : tag.substr(colon + 1);
}

bool isLessonTag(std::string_view tag) { return tag.starts_with("lesson:") || tag == "status:lesson"; }

bool isListPart(std::string_view tag) {
    return tag.ends_with(":up") || tag.ends_with(":down") || tag.ends_with(":track") || tag.ends_with(":thumb");
}

struct Match {
    std::string what;   // "item \"Create\"", "tag designs:list"
    LockArea area;
};

// The share of an area the lesson panel covers (0 to 1).
float panelShare(const AuditInput& in, const LockArea& a) {
    if (!in.panel) return 0.0f;
    const float w = std::min(a.max.x, in.panel->max.x) - std::max(a.min.x, in.panel->min.x);
    const float h = std::min(a.max.y, in.panel->max.y) - std::max(a.min.y, in.panel->min.y);
    const float area = (a.max.x - a.min.x) * (a.max.y - a.min.y);
    return w > 0 && h > 0 && area > 0 ? std::min(1.0f, w * h / area) : 0.0f;
}

// `strict`: a part the step outlines or shows, which the panel is to keep off
// altogether; a name the text matches may lie partly under it.
std::string status(const AuditInput& in, const LockArea& a, bool& ok, bool strict = false) {
    const ImVec2 c = a.centre();
    const bool shown = onScreen(a, in.display);
    const bool lit = in.spotlight.lit(c);
    const float share = panelShare(in, a);
    const bool under = (in.panel && in.panel->contains(c)) || share > (strict ? 0.05f : 0.25f);
    ok = shown && lit && !under;
    std::string s = shown ? "on screen" : "OFF SCREEN";
    s += lit ? ", clear" : ", DIMMED";
    if (under) s += std::format(", UNDER THE PANEL ({:.0f}%)", share * 100.0f);
    else if (share > 0) s += std::format(", the panel over {:.0f}%", share * 100.0f);
    return s;
}

} // namespace

std::vector<std::string> textReferences(const std::vector<learn::Block>& text) {
    std::vector<std::string> out;
    std::string plain;
    for (const learn::Block& b : text) {
        if (b.kind == learn::Block::Kind::Paragraph || b.kind == learn::Block::Kind::Heading) inlineRefs(b.text, out, plain);
        if (b.kind == learn::Block::Kind::List)
            for (const learn::ListItem& item : b.list.items) inlineRefs(item.text, out, plain);
    }
    const std::string low = lower(plain);
    for (const Phrase& p : phrases())
        if (low.find(p.words) != std::string::npos) addRef(out, std::string(p.words));
    return out;
}

AuditReport auditStep(const AuditInput& in) {
    AuditReport r;
    if (!in.step) return r;
    const learn::Step& st = *in.step;
    auto line = [&](std::string text, std::string_view flag = {}) {
        if (!flag.empty()) {
            text += std::format(" FLAG: {}", flag);
            ++r.flags;
        }
        r.lines.push_back(std::move(text));
    };
    std::vector<std::string> named = st.highlight;
    named.insert(named.end(), st.allow.begin(), st.allow.end());
    // The step's text, plain and lower case, to tell what it names.
    const std::string plain = learn::plainText(in.text);
    // The text before its tokens were filled in: a name not in it came from a {design:<type>} token.
    const std::string raw = lower(learn::plainText(st.text));
    const std::vector<std::string> refs = textReferences(in.text);
    const std::string text = lower(plain);
    auto textNames = [&](std::string_view label) {
        const std::string l = lower(visible(label));
        return l.size() >= 2 && text.find(l) != std::string::npos;
    };
    // Every word of an id ("enemy-ship-designs") somewhere in the text (as is or as a plural).
    const std::vector<std::string> textWords = wordsOf(plain);
    auto textHasWords = [&](std::string_view id) {
        const std::vector<std::string> w = wordsOf(id);
        return !w.empty() && std::all_of(w.begin(), w.end(), [&](const std::string& x) {
            return has(textWords, x) || has(textWords, x + "s") || (x.size() > 3 && x.ends_with("s") && has(textWords, x.substr(0, x.size() - 1)));
        });
    };

    // ---- (A) What the lock lets through ----------------------------------------------------
    // The step's own parts, the ways to closed windows, the windows it leaves free.
    auto windowAt = [&](ImVec2 p) -> const LockWindow* {
        for (const LockWindow& w : in.lock.windows)
            if (w.area.contains(p)) return &w;
        return nullptr;
    };
    auto inTop = [&](ImVec2 p) { return std::any_of(in.lock.top.begin(), in.lock.top.end(), [&](const LockArea& a) { return a.contains(p); }); };
    // The tag a point lets through for: the smallest tag the step names
    // there, else the smallest other tag the lock lets through there.
    auto stepTagAt = [&](ImVec2 p) -> const TaggedArea* {
        const TaggedArea* best = nullptr;
        auto better = [&](const TaggedArea& t) {
            if (!best) return true;
            const bool mine = has(named, t.name), bestMine = has(named, best->name);
            if (mine != bestMine) return mine;
            auto size = [](const LockArea& a) { return (a.max.x - a.min.x) * (a.max.y - a.min.y); };
            return size(t.area) < size(best->area);
        };
        // The layer the point is in: a prompt, the front-most window there, or the main window.
        const bool top = inTop(p);
        const LockWindow* front = top ? nullptr : windowAt(p);
        for (const TaggedArea& t : in.tags) {
            if (!t.area.contains(p) || isLessonTag(t.name) || isListPart(t.name) || learn::choiceGroupOf(t.name, true)) continue;
            if (t.name.starts_with("window:") && !has(named, t.name)) continue;
            const auto own = tagWindowId(t.name);
            if (t.top != top || (!top && (front ? !own || *own != front->id : own.has_value()))) continue;
            if (better(t)) best = &t;
        }
        return best;
    };
    // Whether the lock lets a tag through somewhere it is not covered: at one
    // of nine points of it where its own window (or the main window, or a
    // prompt) is the front-most.
    auto letThrough = [&](const TaggedArea& t) {
        const auto own = tagWindowId(t.name);
        for (const float fy : {0.5f, 1.0f / 6.0f, 5.0f / 6.0f})
            for (const float fx : {0.5f, 1.0f / 6.0f, 5.0f / 6.0f}) {
                const ImVec2 p(t.area.min.x + (t.area.max.x - t.area.min.x) * fx, t.area.min.y + (t.area.max.y - t.area.min.y) * fy);
                if (!t.top && inTop(p)) continue;
                const LockWindow* w = t.top ? nullptr : windowAt(p);
                if (w && (!own || w->id != *own)) continue;
                if (!w && own && !t.top) continue;
                if (in.lock.allows(p)) return true;
            }
        return false;
    };

    // Groups of widgets let through, by why.
    struct Group {
        std::string key;   // "tag:designs:list", "free:log", "prompt"
        std::vector<std::string> labels;
    };
    std::vector<Group> groups;
    auto groupFor = [&](std::string key) -> Group& {
        auto at = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.key == key; });
        if (at != groups.end()) return *at;
        groups.push_back({std::move(key), {}});
        return groups.back();
    };
    for (const AuditItem& item : in.items) {
        if (item.disabled || item.scope == "lesson" || item.label.starts_with("text:") || item.label.starts_with("window:") ||
            item.label.starts_with("note:") || item.label.starts_with("hint:") || item.label.starts_with("recovery:"))
            continue;
        const ImVec2 c = item.area.centre();
        if (in.panel && in.panel->contains(c)) continue;
        // Only where the widget is the front-most: its own window (or the main window) there, or a prompt.
        if (!inTop(c)) {
            const LockWindow* front = windowAt(c);
            if (item.scope == "main" ? front != nullptr : (!front || front->id != item.scope)) continue;
        }
        if (std::any_of(in.tags.begin(), in.tags.end(), [&](const TaggedArea& t) { return isLessonTag(t.name) && t.area.contains(c); })) continue;
        if (!in.lock.allows(c)) continue;
        std::string key;
        if (inTop(c)) key = "prompt";
        else if (const LockWindow* w = windowAt(c); w && !w->constrained) key = "free:" + w->id;
        else if (const TaggedArea* t = stepTagAt(c)) key = "tag:" + std::string(t->name);
        else key = "untagged";
        Group& g = groupFor(key);
        const std::string v = visible(item.label);
        if (!v.empty()) g.labels.push_back(v);   // a widget without a label of its own: its group still shows
    }
    // Tags let through that hold no labelled widget (the system view, a map),
    // and the step's own; another tag only where it is let through for itself
    // (a way to a closed window), not as a part of one the step names.
    for (const TaggedArea& t : in.tags) {
        if (isLessonTag(t.name) || t.name.starts_with("window:") || isListPart(t.name) || learn::choiceGroupOf(t.name, true)) continue;
        if (!onScreen(t.area, in.display) || !letThrough(t)) continue;
        if (!has(named, t.name)) {
            // Let through for itself where its middle is, not as a part of one the step names,
            // nor of a window the step leaves free (that one is listed whole).
            const ImVec2 c = t.area.centre();
            if (!in.lock.allows(c)) continue;
            if (const LockWindow* w = inTop(c) ? nullptr : windowAt(c); w && !w->constrained) continue;
            const TaggedArea* at = stepTagAt(c);
            if (at && at != &t && has(named, at->name)) continue;
        }
        groupFor("tag:" + std::string(t.name));
    }
    for (const Group& g : groups) {
        std::string labels;
        for (size_t i = 0; i < g.labels.size() && i < 12; ++i) labels += (i ? ", " : "") + g.labels[i];
        if (g.labels.size() > 12) labels += std::format(", ... ({} in all)", g.labels.size());
        const std::string head = std::format("A let {}: {}", g.key, labels.empty() ? "(the area itself)" : labels);
        if (g.key.starts_with("free:")) {
            line(head, std::format("the step leaves the {} window free: every part of it responds", g.key.substr(5)));
            continue;
        }
        if (g.key == "untagged") {
            line(head, "let through outside every tag the step names");
            continue;
        }
        if (!g.key.starts_with("tag:")) {
            line(head);
            continue;
        }
        const std::string tag = g.key.substr(4);
        if (!has(named, tag)) {
            // A way to a closed window, or a covering window's Close.
            const bool closer = tag.ends_with(":close");
            line(head + (closer ? " (a covering window's Close)" : " (opens a window the step works in)"));
            continue;
        }
        // Another tab: a tab the step allows that its text does not name (nor its condition).
        if (learn::isWindowTab(tag)) {
            std::string label = g.labels.empty() ? std::string(widgetOf(tag)) : g.labels.front();
            const bool doneTab = st.done && learn::describe(*st.done).find(tag) != std::string::npos;
            // "Its tabs choose the columns: try them": every tab of a window the step names.
            const bool tabsNamed = has(textWords, "tabs") && std::any_of(named.begin(), named.end(), [&](const std::string& t) {
                return t != tag && tagWindowId(t) && tagWindowId(t) == tagWindowId(tag);
            });
            if (!doneTab && !tabsNamed && !textNames(label) && !textHasWords(widgetOf(tag))) {
                line(head, "another tab the text does not ask for");
                continue;
            }
        }
        line(head);
    }
    // Whole windows the step allows.
    for (const std::string& t : st.allow)
        if (t.starts_with("window:")) line(std::format("A allow {}", t), "the step allows the whole window");
    // Choosers: the options the step lets through.
    for (const learn::ChoiceGroup& g : learn::choiceGroups()) {
        std::vector<std::string> chosen;
        for (const std::string& t : named)
            if (learn::choiceGroupOf(t) == &g) chosen.push_back(t.substr(g.tag.size() + 1));
        // Its options on screen, and which the lock lets through.
        std::vector<std::string> passing, refused;
        for (const TaggedArea& t : in.tags) {
            if (learn::choiceGroupOf(t.name, true) != &g || !onScreen(t.area, in.display)) continue;
            const std::string option(t.name.substr(g.tag.size() + 1));
            auto& into = in.lock.allows(t.area.centre()) ? passing : refused;
            if (!has(into, option)) into.push_back(option);
        }
        std::string chosenText;
        for (const std::string& c : chosen) chosenText += (chosenText.empty() ? "" : ", ") + c;
        if (g.picker) {
            // Its options show once the chooser is pressed: whether that can be.
            const TaggedArea* opener = nullptr;
            for (const TaggedArea& t : in.tags)
                if (t.name == g.tag) opener = &t;
            const bool asked = g.tag == "colony-type" && (text.find("colony type") != std::string::npos || text.find("colony's type") != std::string::npos);
            const bool opens = (opener && letThrough(*opener)) || asked || !passing.empty();
            if (!opens && chosen.empty()) continue;
            if (chosen.empty())
                line(std::format("A chooser {}: every option passes once it opens", g.tag), "the step does not narrow the chooser");
            else
                line(std::format("A chooser {}: only {}", g.tag, chosenText));
            continue;
        }
        if (passing.empty() && chosen.empty()) continue;
        if (chosen.empty()) {
            std::string list;
            for (const std::string& p : passing) list += (list.empty() ? "" : ", ") + p;
            if (passing.size() > 1) line(std::format("A chooser {}: options {} all pass", g.tag, list), "the step does not narrow the chooser");
            else line(std::format("A chooser {}: options {} pass", g.tag, list));
        } else {
            std::string list;
            for (const std::string& p : refused) list += (list.empty() ? "" : ", ") + p;
            line(std::format("A chooser {}: only {} (refused: {})", g.tag, chosenText, list.empty() ? "none on screen" : list));
        }
    }

    // ---- (B) What the text names -----------------------------------------------------------
    for (const std::string& ref : refs) {
        const std::string lref = lower(ref);
        // The lesson panel's own buttons are always to be seen.
        if (lref == "next" || lref == "finish" || lref == "back" || lref == "skip" || lref == "read more") continue;
        // A kind of ship ("attack ships", "a colony ship") names a thing of the game, not of the screen,
        // unless it is an option of a chooser the step names (matched below).
        {
            std::string single = lref;
            if (single.ends_with("s")) single.pop_back();
            const bool kind = learn::isDesignTypeName(single) || single == "colony ship" || single == "warship";
            const bool chooserOption = std::any_of(named.begin(), named.end(), [&](const std::string& t) {
                const learn::ChoiceGroup* g = learn::choiceGroupOf(t);
                return g && std::find(g->options.begin(), g->options.end(), learn::optionId(ref)) != g->options.end();
            }) || std::any_of(named.begin(), named.end(), [&](const std::string& t) { return t == "create-design:type"; });
            if (kind && !chooserOption) continue;
        }
        const std::vector<std::string> rw = wordsOf(ref);
        std::vector<Match> found;
        // Widgets and texts by their labels.
        for (const AuditItem& item : in.items) {
            if (item.scope == "lesson") continue;
            if (item.label.starts_with("window:") || item.label.starts_with("note:") || item.label.starts_with("hint:")) continue;
            const std::string l = lower(visible(item.label));
            if (l.size() < 2) continue;
            const bool same = l == lref || l.starts_with(lref + " ") || l.starts_with(lref + " (") || (l.size() >= 4 && lref.starts_with(l + " "));
            if (same) found.push_back({std::format("item \"{}\"", visible(item.label)), item.area});
        }
        // Tags by their ids, and the phrases' tags.
        auto addTag = [&](const TaggedArea& t) {
            if (std::none_of(found.begin(), found.end(), [&](const Match& m) { return m.what == "tag " + std::string(t.name); }))
                found.push_back({"tag " + std::string(t.name), t.area});
        };
        for (const Phrase& p : phrases()) {
            if (p.words != lref) continue;
            for (std::string_view want : p.tags) {
                for (const TaggedArea& t : in.tags) {
                    if (isLessonTag(t.name) || isListPart(t.name) || !onScreen(t.area, in.display)) continue;
                    // "command:*": any tag of that kind; "*list": any list tag of an open window; "*tabs": its tabs, ...
                    if (want.ends_with(":*")) {
                        if (t.name.starts_with(want.substr(0, want.size() - 1))) addTag(t);
                    } else if (want.starts_with("*")) {
                        const std::string_view kind = want.substr(1);
                        const std::string_view w = widgetOf(t.name);
                        const bool inWindow = tagWindowId(t.name).has_value() && !t.name.starts_with("window:");
                        const bool ok = (kind == "list" && inWindow &&
                                         (w == "list" || w.ends_with("list") || w == "available" || w == "items" || w == "areas" || w == "ships" ||
                                          w == "projects" || w == "messages" || w == "queue")) ||
                                        (kind == "tabs" && (learn::isWindowTab(t.name) || t.name == "help:tabs" || t.name == "panel:report-tabs")) ||
                                        (kind == "right" && inWindow && (w == "details" || w == "filters" || w == "topics")) ||
                                        (kind == "title" && t.name.starts_with("window:")) || (kind == "column" && w.find("column") != std::string_view::npos);
                        if (ok) addTag(t);
                    } else if (t.name == want) {
                        addTag(t);
                    }
                }
            }
        }
        // A design a token names: the row the step chooses in a list (`<list>:named`).
        if (raw.find(lref) == std::string::npos)
            for (const TaggedArea& t : in.tags)
                if (t.name.ends_with(":named") && onScreen(t.area, in.display)) found.push_back({"tag " + std::string(t.name) + " (the row named)", t.area});
        // An option of a chooser the step names ("Attack Ship" in the Design Type list, "Race 2"):
        // it can be seen once its chooser opens, so the chooser stands for it.
        bool option = false;
        for (const learn::ChoiceGroup& g : learn::choiceGroups()) {
            const std::string id = learn::optionId(ref);
            if (std::find(g.options.begin(), g.options.end(), id) == g.options.end()) continue;
            if (std::none_of(named.begin(), named.end(), [&](const std::string& t) { return t == g.tag || learn::choiceGroupOf(t) == &g; }))
                continue;
            const std::string tag = std::format("{}:{}", g.tag, id);
            for (const TaggedArea& t : in.tags)
                if (t.name == tag || (g.picker && t.name == g.tag)) {
                    found.push_back({"tag " + std::string(t.name) + (t.name == g.tag ? " (its chooser)" : ""), t.area});
                    option = true;
                }
        }
        for (const TaggedArea& t : in.tags) {
            if (option) break;
            if (isLessonTag(t.name) || isListPart(t.name) || learn::choiceGroupOf(t.name, true)) continue;
            const std::vector<std::string> tw = wordsOf(widgetOf(t.name));
            if (tw.empty() || rw.empty()) continue;
            // Every word of the widget's id is in the reference ("Warnings" create-design:warnings), and
            // the reference says little more: words of the window's id ("Design Type" create-design:type)
            // or of what the part is ("the Designs window", "Send Colony Ship button").
            auto same = [](const std::string& a, const std::string& b) {
                return a == b || a + "s" == b || b + "s" == a;
            };
            const bool all = std::all_of(tw.begin(), tw.end(), [&](const std::string& w) {
                return std::any_of(rw.begin(), rw.end(), [&](const std::string& x) { return same(w, x); });
            });
            static const std::vector<std::string> kGeneric{"the", "its", "a", "an", "window", "button", "box", "list", "tab", "panel", "order", "outlined"};
            const std::vector<std::string> ww = wordsOf(tagWindowId(t.name).value_or(""));
            const bool little = std::all_of(rw.begin(), rw.end(), [&](const std::string& x) {
                return has(kGeneric, x) || std::any_of(tw.begin(), tw.end(), [&](const std::string& w) { return same(w, x); }) ||
                       std::any_of(ww.begin(), ww.end(), [&](const std::string& w) { return same(w, x); });
            });
            if (all && little) addTag(t);
        }
        if (found.empty()) {
            ++r.unmatched;
            line(std::format("B ref \"{}\": not matched (check by hand)", ref));
            continue;
        }
        // A window the text names to have it closed ("Close the Log", "close the Tech Tree and the
        // Research window", "closing the Log") need not be read: the sentence it is in closes it.
        bool closing = false;
        for (size_t at = text.find(lref); at != std::string::npos && !closing; at = text.find(lref, at + 1)) {
            size_t start = text.find_last_of(".\n:", at);
            start = start == std::string::npos ? 0 : start + 1;
            const std::string_view before = std::string_view(text).substr(start, at - start);
            closing = before.find("close") != std::string_view::npos || before.find("closing") != std::string_view::npos;
        }
        if (closing) {
            line(std::format("B ref \"{}\": named to be closed", ref));
            continue;
        }
        // Fine when one match is on the screen, clear and not under the panel.
        bool anyOk = false;
        std::string where;
        for (size_t i = 0; i < found.size(); ++i) {
            bool ok = false;
            const std::string s = status(in, found[i].area, ok);
            anyOk = anyOk || ok;
            if (i < 3) where += std::format("{}{} ({})", i ? "; " : "", found[i].what, s);
        }
        if (found.size() > 3) where += std::format("; ... ({} matches)", found.size());
        if (anyOk) line(std::format("B ref \"{}\": {}", ref, where));
        else line(std::format("B ref \"{}\": {}", ref, where), "named by the text but not to be seen clearly");
    }
    // The parts the step outlines and shows: on the screen, clear, and none of
    // them under the panel (it keeps off them).
    std::vector<std::string> parts = st.highlight;
    parts.insert(parts.end(), st.show.begin(), st.show.end());
    for (const std::string& part : parts) {
        if (isLessonTag(part) || learn::choiceGroupOf(part)) continue;
        bool any = false, anyOk = false;
        std::string where;
        for (const TaggedArea& t : in.tags) {
            if (t.name != part || !onScreen(t.area, in.display)) continue;
            bool ok = false;
            std::string s = status(in, t.area, ok, true);
            // A list whose options the step narrows is clear where they are (not at its middle).
            if (!ok && !in.spotlight.lit(t.area.centre()) && panelShare(in, t.area) <= 0.05f) {
                bool litPart = false;
                for (int k = 1; k < 8 && !litPart; ++k)
                    for (int j = 1; j < 4 && !litPart; ++j)
                        litPart = in.spotlight.lit(ImVec2(t.area.min.x + (t.area.max.x - t.area.min.x) * float(j) / 4.0f,
                                                          t.area.min.y + (t.area.max.y - t.area.min.y) * float(k) / 8.0f));
                if (litPart) {
                    ok = true;
                    s += " (its options the step chooses are clear)";
                }
            }
            // A part another window covers, which the step lets the player close (its way back).
            if (!ok && !inTop(t.area.centre()))
                if (const LockWindow* w = windowAt(t.area.centre()); w && tagWindowId(t.name) != std::optional<std::string_view>(w->id))
                    for (const TaggedArea& c : in.tags)
                        if (c.name == w->id + ":close" && in.lock.allows(c.area.centre())) {
                            ok = true;
                            s += std::format(" (under {}, which the step lets the player close first)", w->id);
                            break;
                        }
            if (!any) where = s;
            any = true;
            anyOk = anyOk || ok;
        }
        if (!any) continue;   // not on the screen: the lesson check says so
        const bool shown = std::find(st.show.begin(), st.show.end(), part) != st.show.end();
        const std::string head = std::format("B {} {}: {}", shown ? "shows" : "outlines", part, where);
        if (anyOk) line(head);
        else line(head, "a part of the step that is not to be seen clearly");
    }
    return r;
}

} // namespace opense4::client::classic
