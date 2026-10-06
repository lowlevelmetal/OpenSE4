#include "sdk/scenario.hpp"

#include "game/players.hpp"
#include "learn/lesson.hpp"
#include "sdk/rules.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace opense4::sdk {

const ScenarioObjective* Scenario::objective(std::string_view n) const {
    for (const ScenarioObjective& o : objectives)
        if (o.name == n) return &o;
    return nullptr;
}

namespace {

// Marks a table and every table inside it inline, so that it prints as one value.
void makeInline(toml::node& n) {
    if (toml::table* t = n.as_table()) {
        t->is_inline(true);
        for (auto&& [k, v] : *t) makeInline(v);
    } else if (toml::array* a = n.as_array()) {
        for (toml::node& v : *a) makeInline(v);
    }
}

class Reader {
public:
    Reader(std::string_view file, std::vector<std::string>& errors) : file_(file), errors_(errors) {}

    void error(const toml::node& n, std::string_view what) {
        const auto line = n.source().begin.line;
        errors_.push_back(line > 0 ? std::format("{}:{}: {}", file_, line, what) : std::format("{}: {}", file_, what));
    }
    std::optional<int64_t> integer(const toml::node& n, std::string_view key, int64_t lo, int64_t hi) {
        const auto* i = n.as_integer();
        if (!i || i->get() < lo || i->get() > hi) {
            error(n, std::format("'{}' should be a whole number from {} to {}", key, lo, hi));
            return std::nullopt;
        }
        return i->get();
    }
    std::optional<bool> flag(const toml::node& n, std::string_view key) {
        if (const auto* b = n.as_boolean()) return b->get();
        error(n, std::format("'{}' should be true or false", key));
        return std::nullopt;
    }
    std::string text(const toml::node& n, std::string_view key) {
        if (const auto* s = n.as_string()) return std::string(s->get());
        error(n, std::format("'{}' should be text", key));
        return {};
    }

    void setup(const toml::table& t, game::GameSetup& out) {
        game::GameOptions& o = out.options;
        for (const auto& [k, v] : t) {
            const std::string_view key = k.str();
            auto ranged = [&](int& field, int64_t lo, int64_t hi) {
                if (const auto x = integer(v, key, lo, hi)) field = static_cast<int>(*x);
            };
            auto boolean = [&](bool& field) {
                if (const auto x = flag(v, key)) field = *x;
            };
            if (key == "seed") {
                if (const auto x = integer(v, key, 0, INT64_MAX)) out.seed = static_cast<uint64_t>(*x);
            } else if (key == "turn_style") {
                const std::string style = text(v, key);
                if (style == "simultaneous") o.simultaneous = true;
                else if (style == "turn_based") o.simultaneous = false;
                else error(v, "'turn_style' is \"simultaneous\" or \"turn_based\"");
            } else if (key == "systems") ranged(o.systemCount, 2, 1000);
            else if (key == "quadrant_size") ranged(o.quadrantSize, 0, 2);
            else if (key == "event_frequency") ranged(o.eventFrequency, 0, 3);
            else if (key == "max_event_severity") ranged(o.maxEventSeverity, 0, 3);
            else if (key == "tech_cost") ranged(o.techCost, 0, 2);
            else if (key == "start_tech_level") ranged(o.startTechLevel, 0, 2);
            else if (key == "starting_planets") ranged(o.startingPlanets, 1, 10);
            else if (key == "racial_points") ranged(o.racialPoints, 0, 100'000);
            else if (key == "ai_difficulty") ranged(o.aiDifficulty, 0, 4);
            else if (key == "no_ruins") boolean(o.noRuins);
            else if (key == "finite_resources") boolean(o.finiteResources);
            else if (key == "all_systems_seen") boolean(o.allSystemsSeen);
            else if (key == "allow_intel") boolean(o.allowIntel);
            else if (key == "empire") {
                const toml::array* a = v.as_array();
                if (!a) {
                    error(v, "the empires are [[setup.empire]] tables");
                    continue;
                }
                for (const toml::node& e : *a)
                    if (const toml::table* et = e.as_table()) empire(*et, e, out);
                    else error(e, "the empires are [[setup.empire]] tables");
            } else {
                error(v, std::format("unknown key '{}' in [setup] (seed, turn_style, systems, quadrant_size, event_frequency, max_event_severity, "
                                     "tech_cost, start_tech_level, starting_planets, racial_points, ai_difficulty, no_ruins, finite_resources, "
                                     "all_systems_seen, allow_intel, empire)",
                                     key));
            }
        }
        if (out.empires.empty()) errors_.push_back(std::format("{}: [setup] needs its empires: [[setup.empire]] tables", file_));
    }

    void empire(const toml::table& t, const toml::node& n, game::GameSetup& out) {
        game::EmpireSetup e;
        for (const auto& [k, v] : t) {
            const std::string_view key = k.str();
            if (key == "name") e.name = text(v, key);
            else if (key == "preset") e.preset = text(v, key);
            else if (key == "kind") {
                const std::string kind = text(v, key);
                if (kind == "human") e.kind = game::PlayerKind::Human;
                else if (kind == "computer") e.kind = game::PlayerKind::Computer;
                else if (kind == "neutral") e.kind = game::PlayerKind::Neutral;
                else error(v, "'kind' is \"human\", \"computer\" or \"neutral\"");
            } else if (key == "controller") {
                const auto c = game::parseController(text(v, key));
                if (!c) error(v, "'controller' is \"builtin\", \"<mod id>:<player>\" or \"external:<slot>\"");
                else e.controller = *c;
            } else {
                error(v, std::format("unknown key '{}' in [[setup.empire]] (name, kind, preset, controller)", key));
            }
        }
        (void)n;
        out.empires.push_back(std::move(e));
    }

    void objective(const toml::table& t, const toml::node& n, Scenario& out) {
        ScenarioObjective o;
        o.line = static_cast<int>(n.source().begin.line);
        bool hasWhen = false;
        for (const auto& [k, v] : t) {
            const std::string_view key = k.str();
            if (key == "name") o.name = text(v, key);
            else if (key == "text") o.text = text(v, key);
            else if (key == "empire") {
                if (const auto* s = v.as_string(); s && s->get() == "every") o.empire.reset();
                else if (const auto x = integer(v, key, 0, 255)) o.empire = static_cast<uint32_t>(*x);
            } else if (key == "victory") {
                if (const auto x = flag(v, key)) o.victory = *x;
            } else if (key == "action") {
                o.action = text(v, key);
                if (!o.action.empty() && !mods::validRulesName(o.action)) error(v, std::format("the action '{}' is not a name a mod registers", o.action));
            } else if (key == "by_turn") {
                if (const auto x = integer(v, key, 0, 1'000'000)) o.byTurn = static_cast<uint32_t>(*x);
            } else if (key == "when") {
                hasWhen = true;
                // The lessons' condition language (docs/LEARNING.md), read as the lessons read it.
                toml::table wrapper;
                wrapper.insert("when", v);
                makeInline(*wrapper.get("when"));
                std::ostringstream printed;
                printed << wrapper;   // when = { ... }
                std::string written = printed.str();
                if (const size_t eq = written.find('='); eq != std::string::npos) written = written.substr(eq + 1);
                std::vector<learn::Diagnostic> problems;
                auto c = learn::parseCondition(written, file_, static_cast<int>(v.source().begin.line), problems);
                for (const learn::Diagnostic& d : problems) errors_.push_back(d.text());
                if (c) o.when = std::move(*c);
            } else {
                error(v, std::format("unknown key '{}' in [[objective]] (name, text, empire, when, victory, action, by_turn)", key));
            }
        }
        if (o.name.empty() || !mods::validRulesName(o.name)) error(n, "an objective needs a name: lowercase letters, digits and '_'");
        else if (out.objective(o.name)) error(n, std::format("two objectives are named '{}'", o.name));
        if (!hasWhen) error(n, std::format("the objective '{}' needs its condition: when = {{ ... }}", o.name));
        out.objectives.push_back(std::move(o));
    }

private:
    std::string_view file_;
    std::vector<std::string>& errors_;
};

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

std::expected<Scenario, std::vector<std::string>> parseScenario(std::string_view text, std::string_view file, std::string_view mod,
                                                                std::string_view name) {
    std::vector<std::string> errors;
    toml::table root;
    try {
        root = toml::parse(text, std::string_view(file));
    } catch (const toml::parse_error& e) {
        errors.push_back(std::format("{}:{}: {}", file, e.source().begin.line, e.description()));
        return std::unexpected(errors);
    }
    Scenario s;
    s.mod = std::string(mod);
    s.name = std::string(name);
    Reader rd(file, errors);
    for (const auto& [k, v] : root) {
        const std::string_view key = k.str();
        if (key == "title") s.title = rd.text(v, key);
        else if (key == "summary") s.summary = rd.text(v, key);
        else if (key == "setup") {
            if (const toml::table* t = v.as_table()) rd.setup(*t, s.setup);
            else rd.error(v, "[setup] should be a table");
        } else if (key == "options") {
            const toml::table* t = v.as_table();
            if (!t) {
                rd.error(v, "[options] should be a table of the mod's options");
                continue;
            }
            for (const auto& [ok, ov] : *t) {
                if (const auto* b = ov.as_boolean()) s.options.emplace_back(std::string(ok.str()), b->get() ? 1 : 0);
                else if (const auto* i = ov.as_integer()) s.options.emplace_back(std::string(ok.str()), i->get());
                else rd.error(ov, "an option's value is a whole number, true or false");
            }
        } else if (key == "objective") {
            const toml::array* a = v.as_array();
            if (!a) {
                rd.error(v, "objectives are [[objective]] tables");
                continue;
            }
            for (const toml::node& e : *a)
                if (const toml::table* t = e.as_table()) rd.objective(*t, e, s);
                else rd.error(e, "objectives are [[objective]] tables");
        } else {
            rd.error(v, std::format("unknown key '{}' (a scenario has title, summary, [setup], [options] and [[objective]])", key));
        }
    }
    if (s.title.empty()) s.title = s.name;
    for (const ScenarioObjective& o : s.objectives)
        if (o.empire && *o.empire >= s.setup.empires.size())
            errors.push_back(std::format("{}:{}: the objective '{}' names empire {}; the scenario has {}", file, o.line, o.name, *o.empire,
                                         s.setup.empires.size()));
    if (!errors.empty()) return std::unexpected(errors);
    return s;
}

std::vector<std::string> scenarioNames(const mods::Package& p) {
    std::vector<std::string> out;
    for (const mods::PackageFile& f : p.files) {
        if (!f.path.starts_with("scenarios/") || !f.path.ends_with(".toml")) continue;
        const std::string rest = f.path.substr(10);
        if (rest.find('/') != std::string::npos) continue;
        out.push_back(rest.substr(0, rest.size() - 5));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::expected<Scenario, std::vector<std::string>> loadScenario(const mods::Package& p, std::string_view name) {
    const std::string path = std::format("scenarios/{}.toml", name);
    const mods::PackageFile* f = p.file(path);
    if (!f) return std::unexpected(std::vector<std::string>{std::format("the mod {} has no scenario {} ({})", p.id(), name, path)});
    return parseScenario(readFile(f->real), std::format("mod {}: {}", p.id(), path), p.id(), name);
}

std::expected<game::GameState, std::string> startScenario(const game::Rules& r, const mods::Package& p, std::string_view name) {
    auto scenario = loadScenario(p, name);
    if (!scenario) return std::unexpected(scenario.error().front());
    game::GameSetup setup = scenario->setup;
    const std::vector<ModOptionChoice> choices = modOptions(std::span<const mods::Package>(&p, 1));
    for (const auto& [option, value] : scenario->options)
        if (std::string why = setModOption(setup.options, choices, std::format("{}:{}", p.id(), option), value); !why.empty())
            return std::unexpected(std::format("the scenario {}: {}", name, why));
    setup.scenario.mod = p.id();
    setup.scenario.name = std::string(name);
    return game::createGame(r, setup);
}

} // namespace opense4::sdk
