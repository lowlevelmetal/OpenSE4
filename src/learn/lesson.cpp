#include "learn/lesson.hpp"

#include "learn/ids.hpp"

#include "datafile/datafile.hpp"
#include "game/ai_data.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opense4::learn {

namespace {

// Named choices of the setup keys, in the order of their game option values.
constexpr std::string_view kLowMediumHigh[] = {"low", "medium", "high"};
constexpr std::string_view kSizes[] = {"small", "medium", "large"};
constexpr std::string_view kEvents[] = {"none", "low", "medium", "high"};

class Reader {
public:
    Reader(std::string_view file, std::vector<Diagnostic>& problems) : file_(file), problems_(problems) {}

    size_t errors() const { return errors_; }

    void error(const toml::node* n, std::string message) {
        const int line = n ? static_cast<int>(n->source().begin.line) : 0;
        problems_.push_back({std::string(file_), line, std::move(message)});
        ++errors_;
    }
    void error(const toml::node& n, std::string message) { error(&n, std::move(message)); }

    // Reports every key of `t` that is not in `allowed`.
    void allowOnly(const toml::table& t, std::string_view where, std::initializer_list<std::string_view> allowed) {
        for (const auto& [key, value] : t)
            if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end())
                error(value, where.empty() ? std::format("unknown key '{}'", key.str()) : std::format("unknown key '{}' in {}", key.str(), where));
    }

    std::optional<std::string> string(const toml::table& t, std::string_view key, std::string_view where, bool required) {
        const toml::node* n = t.get(key);
        if (!n) {
            if (required) error(&t, std::format("{} needs '{}'", where, key));
            return std::nullopt;
        }
        if (const auto* s = n->as_string()) return s->get();
        error(n, std::format("'{}' must be a string", key));
        return std::nullopt;
    }

    std::optional<int64_t> integer(const toml::table& t, std::string_view key, int64_t min, int64_t max) {
        const toml::node* n = t.get(key);
        if (!n) return std::nullopt;
        const auto* i = n->as_integer();
        if (!i) {
            error(n, std::format("'{}' must be a whole number", key));
            return std::nullopt;
        }
        if (i->get() < min || i->get() > max) {
            error(n, std::format("'{}' must be between {} and {}", key, min, max));
            return std::nullopt;
        }
        return i->get();
    }

    std::optional<bool> boolean(const toml::table& t, std::string_view key) {
        const toml::node* n = t.get(key);
        if (!n) return std::nullopt;
        if (const auto* b = n->as_boolean()) return b->get();
        error(n, std::format("'{}' must be true or false", key));
        return std::nullopt;
    }

    // A string key with named choices; returns the index of the choice.
    std::optional<int> choice(const toml::table& t, std::string_view key, std::span<const std::string_view> choices) {
        const toml::node* n = t.get(key);
        if (!n) return std::nullopt;
        const auto* s = n->as_string();
        const auto it = s ? std::find(choices.begin(), choices.end(), std::string_view(s->get())) : choices.end();
        if (it == choices.end()) {
            std::string list;
            for (std::string_view c : choices) list += (list.empty() ? "" : ", ") + std::format("\"{}\"", c);
            error(n, std::format("'{}' must be one of {}", key, list));
            return std::nullopt;
        }
        return static_cast<int>(it - choices.begin());
    }

    std::vector<Block> markdown(const toml::table& t, std::string_view key, std::string_view where, bool required) {
        const auto text = string(t, key, where, required);
        if (!text) return {};
        // Lines inside the text count from the line of the value; a multi-line
        // string drops the line break right after its opening quotes.
        const toml::node* n = t.get(key);
        int base = n ? static_cast<int>(n->source().begin.line) : 0;
        if (n) {
            const auto& src = n->source();
            const auto breaks = static_cast<std::ptrdiff_t>(std::count(text->begin(), text->end(), '\n'));
            if (src.end.line > src.begin.line && static_cast<std::ptrdiff_t>(src.end.line - src.begin.line) > breaks) ++base;
        }
        Document doc = parseMarkdown(*text, file_, false);
        for (Diagnostic& d : doc.problems) {
            if (d.line > 0) d.line += base - 1;
            problems_.push_back(std::move(d));
            ++errors_;
        }
        offsetLines(doc.blocks, base - 1);
        return std::move(doc.blocks);
    }

    static void offsetLines(std::vector<Block>& blocks, int by) {
        for (Block& b : blocks) {
            b.line += by;
            offsetLines(b.tip, by);
        }
    }

    std::optional<Condition> condition(const toml::node& n) {
        const auto* t = n.as_table();
        if (!t) {
            error(n, "a condition is a table, such as { colonies = 5 }");
            return std::nullopt;
        }
        if (t->empty()) {
            error(n, "an empty condition");
            return std::nullopt;
        }
        // Several keys in one table must all hold.
        if (t->size() > 1) {
            Condition all;
            all.op = Condition::Op::All;
            all.line = static_cast<int>(n.source().begin.line);
            for (const auto& [key, value] : *t)
                if (auto c = single(key.str(), value)) all.children.push_back(std::move(*c));
            return all;
        }
        const auto first = t->begin();   // toml++ iterators own the pair they point at
        const auto& [key, value] = *first;
        return single(key.str(), value);
    }

private:
    std::optional<Condition> single(std::string_view key, const toml::node& value) {
        Condition c;
        c.line = static_cast<int>(value.source().begin.line);
        if (key == "all" || key == "any") {
            c.op = key == "all" ? Condition::Op::All : Condition::Op::Any;
            const auto* arr = value.as_array();
            if (!arr || arr->empty()) {
                error(value, std::format("'{}' takes a list of conditions, such as [{{ turn = 5 }}, {{ colonies = 2 }}]", key));
                return std::nullopt;
            }
            bool ok = true;
            for (const toml::node& child : *arr) {
                auto sub = condition(child);
                if (sub) c.children.push_back(std::move(*sub));
                else ok = false;
            }
            return ok ? std::optional(std::move(c)) : std::nullopt;
        }
        if (key == "not") {
            c.op = Condition::Op::Not;
            auto sub = condition(value);
            if (!sub) return std::nullopt;
            c.children.push_back(std::move(*sub));
            return c;
        }
        const FactInfo* fact = findFact(key);
        if (!fact) {
            error(value, std::format("unknown condition key '{}'", key));
            return std::nullopt;
        }
        c.op = Condition::Op::Fact;
        c.fact = fact->fact;
        if (fact->value == FactValue::Flag) {
            const auto* b = value.as_boolean();
            if (!b) {
                error(value, std::format("'{}' takes true or false", key));
                return std::nullopt;
            }
            c.number = b->get() ? 1 : 0;
            return c;
        }
        if (fact->value == FactValue::Text) {
            const auto* s = value.as_string();
            if (!s) {
                error(value, std::format("'{}' takes a string", key));
                return std::nullopt;
            }
            c.text = s->get();
            const char* what = nullptr;   // the vocabulary the value is not in
            switch (fact->fact) {
                case Fact::Window: what = findWindow(c.text) ? nullptr : "window id"; break;
                case Fact::Selected: what = isSelectionKind(c.text) ? nullptr : "selection kind"; break;
                case Fact::Command: what = isCommandName(c.text) ? nullptr : "command name"; break;
                case Fact::Order: what = orderKindFromId(c.text) ? nullptr : "order kind"; break;
                case Fact::Tab: what = isWindowTab(c.text) ? nullptr : "window tab"; break;
                case Fact::Option: what = isOptionName(c.text) ? nullptr : "option"; break;
                case Fact::Treaty: what = treatyFromId(c.text) ? nullptr : "treaty kind"; break;
                case Fact::BattleOrder: what = isBattleOrderKind(c.text) ? nullptr : "battle order kind"; break;
                default: break;
            }
            if (what) {
                error(value, std::format("unknown {} '{}'", what, c.text));
                return std::nullopt;
            }
            return c;
        }
        const auto* i = value.as_integer();
        if (!i || i->get() < 0) {
            error(value, std::format("'{}' takes a whole number of at least 0", key));
            return std::nullopt;
        }
        c.number = i->get();
        if (c.number == 0 && fact->fact != Fact::Turn) {
            // "At least 0" always holds: almost certainly meant as "none".
            error(value, std::format("'{0} = 0' always holds (numbers mean \"at least\"); for none, write not = {{ {0} = 1 }}", key));
            return std::nullopt;
        }
        return c;
    }

public:
    std::optional<Condition> conditionKey(const toml::table& t, std::string_view key, std::string_view where, bool required) {
        const toml::node* n = t.get(key);
        if (!n) {
            if (required) error(&t, std::format("{} needs '{}'", where, key));
            return std::nullopt;
        }
        return condition(*n);
    }

    // [[name]] tables (or none).
    std::vector<const toml::table*> tables(const toml::table& root, std::string_view key) {
        std::vector<const toml::table*> out;
        const toml::node* n = root.get(key);
        if (!n) return out;
        const auto* arr = n->as_array();
        if (!arr) {
            error(n, std::format("write each {0} as a [[{0}]] table", key));
            return out;
        }
        for (const toml::node& e : *arr) {
            if (const auto* t = e.as_table()) out.push_back(t);
            else error(e, std::format("write each {0} as a [[{0}]] table", key));
        }
        return out;
    }

private:
    std::string_view file_;
    std::vector<Diagnostic>& problems_;
    size_t errors_ = 0;
};

Setup readSetup(Reader& rd, const toml::table& t) {
    rd.allowOnly(t, "[setup]",
                 {"seed", "race", "computer_players", "systems", "quadrant", "quadrant_size", "turn_style", "tech_level", "tech_cost",
                  "starting_resources", "starting_planets", "events", "ai_difficulty", "no_tactical_combat", "all_systems_seen",
                  "omnipresent", "no_ruins", "starting_ships"});
    Setup s;
    if (auto v = rd.integer(t, "seed", 0, std::numeric_limits<int64_t>::max())) s.seed = static_cast<uint64_t>(*v);
    if (auto v = rd.string(t, "race", "[setup]", false)) s.race = *v;
    if (auto v = rd.integer(t, "computer_players", 0, 19)) s.computerPlayers = static_cast<int>(*v);
    if (auto v = rd.integer(t, "systems", 1, 255)) s.systems = static_cast<int>(*v);
    if (auto v = rd.string(t, "quadrant", "[setup]", false)) s.quadrant = *v;
    s.quadrantSize = rd.choice(t, "quadrant_size", kSizes);
    constexpr std::string_view kStyles[] = {"turn-based", "simultaneous"};
    if (auto v = rd.choice(t, "turn_style", kStyles)) s.turnBased = *v == 0;
    s.techLevel = rd.choice(t, "tech_level", kLowMediumHigh);
    s.techCost = rd.choice(t, "tech_cost", kLowMediumHigh);
    s.startingResources = rd.integer(t, "starting_resources", 0, 1'000'000'000);
    if (const toml::node* n = t.get("starting_planets")) {
        const auto* i = n->as_integer();
        if (i && (i->get() == 1 || i->get() == 3 || i->get() == 5 || i->get() == 10)) s.startingPlanets = static_cast<int>(i->get());
        else rd.error(n, "'starting_planets' must be 1, 3, 5 or 10");
    }
    s.events = rd.choice(t, "events", kEvents);
    s.aiDifficulty = rd.choice(t, "ai_difficulty", kLowMediumHigh);
    s.noTacticalCombat = rd.boolean(t, "no_tactical_combat");
    s.allSystemsSeen = rd.boolean(t, "all_systems_seen");
    s.omnipresent = rd.boolean(t, "omnipresent");
    s.noRuins = rd.boolean(t, "no_ruins");
    if (const toml::node* n = t.get("starting_ships")) {
        const auto* list = n->as_array();
        if (!list) rd.error(n, "'starting_ships' must be a list of design types, such as [\"Attack Ship\", \"Colony\"]");
        else
            for (const toml::node& item : *list) {
                const auto* type = item.as_string();
                if (type && (game::ai::isAiDesignType(type->get()) || datafile::keysEqual(type->get(), "Colony")))
                    s.startingShips.push_back(type->get());
                else
                    rd.error(item, "each of 'starting_ships' must be a design type (\"Attack Ship\", \"Colony (Rock)\", ...) or \"Colony\"");
            }
    }
    return s;
}

} // namespace

std::string_view kindName(LessonKind k) { return k == LessonKind::Tutorial ? "tutorial" : "training"; }

void applySetup(const Setup& s, game::GameSetup& g, game::StartExtras& extras) {
    game::GameOptions& o = g.options;
    if (s.systems) o.systemCount = *s.systems;
    if (!s.quadrant.empty()) o.quadrantType = s.quadrant;
    if (s.quadrantSize) o.quadrantSize = *s.quadrantSize;
    o.simultaneous = !s.turnBased;
    if (s.techLevel) o.startTechLevel = *s.techLevel;
    if (s.techCost) o.techCost = *s.techCost;
    if (s.startingResources) o.startingResources = game::Resources{*s.startingResources, *s.startingResources, *s.startingResources};
    if (s.startingPlanets) o.startingPlanets = *s.startingPlanets;
    if (s.events) o.eventFrequency = *s.events;
    if (s.noTacticalCombat) o.noTacticalCombat = *s.noTacticalCombat;
    if (s.allSystemsSeen) o.allSystemsSeen = *s.allSystemsSeen;
    if (s.omnipresent) o.omnipresent = *s.omnipresent;
    if (s.noRuins) o.noRuins = *s.noRuins;
    if (!g.empires.empty() && !s.startingShips.empty()) {
        if (extras.lessonShips.empty()) extras.lessonShips.resize(1);
        extras.lessonShips.front() = s.startingShips;
    }
    if (s.aiDifficulty) {
        o.aiDifficulty = *s.aiDifficulty;
        o.randomAiPlayers.assign(g.empires.size(), 0);
        for (size_t i = 0; i < g.empires.size(); ++i)
            if (g.empires[i].kind == game::PlayerKind::Computer) o.randomAiPlayers[i] = 1;
    }
}

std::optional<Lesson> parseLesson(std::string_view text, std::string_view file, LessonKind kind, std::vector<Diagnostic>& problems) {
    toml::table root;
    try {
        root = toml::parse(text, std::string(file));
    } catch (const toml::parse_error& e) {
        problems.push_back({std::string(file), static_cast<int>(e.source().begin.line), std::string(e.description())});
        return std::nullopt;
    }
    Reader rd(file, problems);
    Lesson l;
    l.kind = kind;
    l.file = std::string(file);
    if (kind == LessonKind::Tutorial) rd.allowOnly(root, "", {"title", "summary", "minutes", "setup", "step"});
    else rd.allowOnly(root, "", {"title", "summary", "minutes", "setup", "objective", "page", "hint", "fail"});
    l.title = rd.string(root, "title", "the file", true).value_or(std::string{});
    l.summary = rd.string(root, "summary", "the file", false).value_or(std::string{});
    l.minutes = static_cast<int>(rd.integer(root, "minutes", 0, 600).value_or(0));
    if (const toml::node* n = root.get("setup")) {
        if (const auto* t = n->as_table()) l.setup = readSetup(rd, *t);
        else rd.error(n, "[setup] must be a table");
    }

    if (kind == LessonKind::Tutorial) {
        for (const toml::table* t : rd.tables(root, "step")) {
            rd.allowOnly(*t, "a [[step]]", {"title", "text", "highlight", "allow", "keys", "done", "manual"});
            Step s;
            s.line = static_cast<int>(t->source().begin.line);
            s.title = rd.string(*t, "title", "a [[step]]", true).value_or(std::string{});
            s.text = rd.markdown(*t, "text", "a [[step]]", true);
            // A string or a list of strings, each checked.
            auto strings = [&](std::string_view key, auto&& check, std::vector<std::string>& out) {
                const toml::node* n = t->get(key);
                if (!n) return;
                auto add = [&](const toml::node& item) {
                    const auto* v = item.as_string();
                    if (!v) rd.error(item, std::format("'{}' takes strings", key));
                    else if (auto problem = check(v->get())) rd.error(item, *problem);
                    else out.push_back(v->get());
                };
                if (const auto* arr = n->as_array())
                    for (const toml::node& item : *arr) add(item);
                else add(*n);
            };
            auto uiTag = [](const std::string& tag) -> std::optional<std::string> {
                if (isUiTag(tag)) return std::nullopt;
                return std::format("unknown UI tag '{}'", tag);
            };
            strings("highlight", uiTag, s.highlight);
            strings("allow", uiTag, s.allow);
            strings("keys", [](const std::string& chord) -> std::optional<std::string> {
                if (isKeyChord(chord)) return std::nullopt;
                return std::format("unknown key '{}' (keys are written as \"F12\", \"Ctrl+L\", \"Alt+1\", \"Escape\")", chord);
            }, s.keys);
            s.done = rd.conditionKey(*t, "done", "a [[step]]", false);
            s.manual = rd.string(*t, "manual", "a [[step]]", false).value_or(std::string{});
            l.steps.push_back(std::move(s));
        }
        if (l.steps.empty()) rd.error(&root, "a tutorial needs at least one [[step]]");
    } else {
        for (const toml::table* t : rd.tables(root, "objective")) {
            rd.allowOnly(*t, "an [[objective]]", {"text", "when", "by_turn"});
            Objective o;
            o.line = static_cast<int>(t->source().begin.line);
            o.text = rd.string(*t, "text", "an [[objective]]", true).value_or(std::string{});
            if (auto c = rd.conditionKey(*t, "when", "an [[objective]]", true)) o.when = std::move(*c);
            if (auto v = rd.integer(*t, "by_turn", 0, 100000)) o.byTurn = static_cast<uint32_t>(*v);
            l.objectives.push_back(std::move(o));
        }
        if (l.objectives.empty()) rd.error(&root, "a training game needs at least one [[objective]]");
        for (const toml::table* t : rd.tables(root, "page")) {
            rd.allowOnly(*t, "a [[page]]", {"turn", "series", "title", "text"});
            BriefingPage p;
            p.line = static_cast<int>(t->source().begin.line);
            p.turn = static_cast<uint32_t>(rd.integer(*t, "turn", 0, 100000).value_or(0));
            p.series = rd.string(*t, "series", "a [[page]]", false).value_or(std::string{});
            p.title = rd.string(*t, "title", "a [[page]]", true).value_or(std::string{});
            p.text = rd.markdown(*t, "text", "a [[page]]", true);
            l.pages.push_back(std::move(p));
        }
        for (const toml::table* t : rd.tables(root, "hint")) {
            rd.allowOnly(*t, "a [[hint]]", {"title", "text", "when"});
            Hint h;
            h.line = static_cast<int>(t->source().begin.line);
            h.title = rd.string(*t, "title", "a [[hint]]", false).value_or("Hint");
            h.text = rd.markdown(*t, "text", "a [[hint]]", true);
            if (auto c = rd.conditionKey(*t, "when", "a [[hint]]", true)) h.when = std::move(*c);
            l.hints.push_back(std::move(h));
        }
        if (const toml::node* n = root.get("fail")) {
            if (const auto* t = n->as_table()) {
                rd.allowOnly(*t, "[fail]", {"when", "text"});
                FailRule f;
                f.line = static_cast<int>(t->source().begin.line);
                if (auto c = rd.conditionKey(*t, "when", "[fail]", true)) f.when = std::move(*c);
                f.text = rd.markdown(*t, "text", "[fail]", false);
                l.fail = std::move(f);
            } else {
                rd.error(n, "[fail] must be a table");
            }
        }
    }
    if (rd.errors() > 0) return std::nullopt;
    return l;
}

std::string slugOf(std::string_view name) {
    if (const size_t slash = name.find_last_of("/\\"); slash != std::string_view::npos) name.remove_prefix(slash + 1);
    if (const size_t dot = name.rfind('.'); dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
    size_t digits = 0;
    while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits]))) ++digits;
    if (digits > 0 && digits + 1 < name.size() && name[digits] == '-') name.remove_prefix(digits + 1);
    return std::string(name);
}

} // namespace opense4::learn
