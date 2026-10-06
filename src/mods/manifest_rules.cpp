// The [rules] table of mod.toml (docs/sdk/rules.md): what a mod's rules
// scripts declare to the engine. Every problem names the file and line.

#include "mods/manifest.hpp"
#include "mods/manifest_internal.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace opense4::mods {

namespace {

constexpr auto kArgTypes = std::to_array<std::string_view>({"int", "bool", "text", "empire", "system", "object", "colony", "vehicle", "fleet", "design"});
constexpr auto kOrderTargets = std::to_array<std::string_view>({"self", "vehicle", "fleet", "colony", "empire"});
constexpr auto kEventTargets = std::to_array<std::string_view>({"none", "empire", "colony", "vehicle", "system"});

template <class T>
const T* named(const std::vector<T>& list, std::string_view name) {
    for (const T& x : list)
        if (x.name == name) return &x;
    return nullptr;
}

template <size_t N>
std::string joinedNames(const std::array<std::string_view, N>& names) {
    std::string out;
    for (std::string_view n : names) out += std::format("{}\"{}\"", out.empty() ? "" : ", ", n);
    return out;
}

template <size_t N>
bool oneOf(const std::array<std::string_view, N>& names, std::string_view x) {
    return std::find(names.begin(), names.end(), x) != names.end();
}

class RulesParser {
public:
    RulesParser(std::string_view source, std::vector<std::string>& errors, RulesDecl& out) : source_(source), errors_(errors), out_(out) {}

    void parse(const toml::node& node) {
        const toml::table* t = node.as_table();
        if (!t) return error(node, "[rules] should be a table");
        for (const auto& [k, v] : *t) {
            if (k == "players_see_mod_data") {
                if (const auto* b = v.as_boolean()) out_.playersSeeModData = b->get();
                else error(v, "'players_see_mod_data' should be true or false");
            } else if (k == "modules") {
                const toml::array* a = v.as_array();
                if (!a) {
                    error(v, "'modules' should be a list of module names under scripts/, such as [\"crowding\"]");
                    continue;
                }
                for (const toml::node& e : *a) {
                    std::string name = text(e, "modules");
                    if (!dottedPythonName(name)) error(e, std::format("'{}' should be Python names separated by dots", name));
                    else out_.modules.push_back(std::move(name));
                }
            } else if (k == "options") {
                each(v, "[[rules.options]]", [&](const toml::table& e, const toml::node& n) { option(e, n); });
            } else if (k == "orders") {
                each(v, "[[rules.orders]]", [&](const toml::table& e, const toml::node& n) { order(e, n); });
            } else if (k == "events") {
                each(v, "[[rules.events]]", [&](const toml::table& e, const toml::node& n) { event(e, n); });
            } else if (k == "intel_projects") {
                each(v, "[[rules.intel_projects]]", [&](const toml::table& e, const toml::node& n) { intel(e, n); });
            } else if (k == "victory") {
                each(v, "[[rules.victory]]", [&](const toml::table& e, const toml::node& n) { victory(e, n); });
            } else {
                error(v, std::format("unknown key '{}' in [rules] (players_see_mod_data, modules, options, orders, events, intel_projects, victory)",
                                     k.str()));
            }
        }
        // Options a declaration names must be switches of the mod.
        for (const ModEventDecl& e : out_.events) checkSwitch(e.option, e.line, "event", e.name);
        for (const ModVictoryDecl& v : out_.victories) checkSwitch(v.option, v.line, "victory condition", v.name);
    }

private:
    void error(const toml::node& n, std::string_view what) { errors_.push_back(std::format("{}: {}", manifestAt(source_, n), what)); }
    void errorAt(int line, std::string_view what) {
        errors_.push_back(line > 0 ? std::format("{}:{}: {}", source_, line, what) : std::format("{}: {}", source_, what));
    }

    std::string text(const toml::node& n, std::string_view key) {
        if (const auto* s = n.as_string()) return std::string(s->get());
        error(n, std::format("'{}' should be text in quotes", key));
        return {};
    }
    std::optional<int64_t> number(const toml::node& n, std::string_view key) {
        if (const auto* i = n.as_integer()) return i->get();
        error(n, std::format("'{}' should be a whole number", key));
        return std::nullopt;
    }

    template <class F>
    void each(const toml::node& v, std::string_view what, F&& f) {
        const toml::array* a = v.as_array();
        if (!a) return error(v, std::format("write them as {} tables", what));
        for (const toml::node& e : *a) {
            const toml::table* t = e.as_table();
            if (!t) {
                error(e, std::format("write them as {} tables", what));
                continue;
            }
            f(*t, e);
        }
    }

    bool checkName(const toml::node& n, const std::string& name, std::string_view what, bool taken) {
        if (name.empty()) {
            error(n, std::format("{} needs a name", what));
            return false;
        }
        if (!validRulesName(name)) {
            error(n, std::format("the name '{}' may hold only lowercase letters, digits and '_', starting with a letter (at most 64)", name));
            return false;
        }
        if (taken) {
            error(n, std::format("two of the mod's {} are named '{}'", what, name));
            return false;
        }
        return true;
    }

    void checkSwitch(const std::string& option, int line, std::string_view what, const std::string& name) {
        if (option.empty()) return;
        const ModOptionDecl* o = out_.option(option);
        if (!o) errorAt(line, std::format("the {} '{}' names the option '{}', which the mod does not declare", what, name, option));
        else if (!o->isSwitch) errorAt(line, std::format("the {} '{}' names the option '{}', which is not a switch (type = \"bool\")", what, name, option));
    }

    void option(const toml::table& t, const toml::node& n) {
        ModOptionDecl o;
        o.line = static_cast<int>(n.source().begin.line);
        std::string type = "int";
        const toml::node* def = nullptr;
        bool hasMin = false, hasMax = false;
        for (const auto& [k, v] : t) {
            if (k == "name") o.name = text(v, "name");
            else if (k == "label") o.label = text(v, "label");
            else if (k == "description") o.description = text(v, "description");
            else if (k == "type") type = text(v, "type");
            else if (k == "min") hasMin = (o.min = number(v, "min").value_or(0), true);
            else if (k == "max") hasMax = (o.max = number(v, "max").value_or(0), true);
            else if (k == "default") def = &v;
            else error(v, std::format("unknown key '{}' in [[rules.options]] (name, label, description, type, min, max, default)", k.str()));
        }
        if (!checkName(n, o.name, "options", named(out_.options, o.name) != nullptr)) return;
        if (type == "bool") {
            o.isSwitch = true;
            o.min = 0;
            o.max = 1;
            if (hasMin || hasMax) error(n, std::format("the switch '{}' takes no min or max", o.name));
            if (def) {
                if (const auto* b = def->as_boolean()) o.defaultValue = b->get() ? 1 : 0;
                else error(*def, "a switch's default is true or false");
            }
        } else if (type == "int") {
            if (!hasMin || !hasMax) error(n, std::format("the option '{}' needs its range: min and max", o.name));
            else if (o.min > o.max) error(n, std::format("the option '{}' has min {} above max {}", o.name, o.min, o.max));
            o.defaultValue = o.min;
            if (def) {
                if (const auto* i = def->as_integer()) o.defaultValue = i->get();
                else error(*def, "an option's default is a whole number");
            }
            if (hasMin && hasMax && (o.defaultValue < o.min || o.defaultValue > o.max))
                error(n, std::format("the option '{}' has its default {} outside {} to {}", o.name, o.defaultValue, o.min, o.max));
        } else {
            error(n, std::format("the option '{}' has type '{}': an option is \"int\" or \"bool\"", o.name, type));
        }
        if (o.label.empty()) o.label = o.name;
        out_.options.push_back(std::move(o));
    }

    void argument(const toml::table& t, const toml::node& n, ModOrderDecl& owner) {
        ModArgDecl a;
        a.line = static_cast<int>(n.source().begin.line);
        const toml::node* def = nullptr;
        for (const auto& [k, v] : t) {
            if (k == "name") a.name = text(v, "name");
            else if (k == "type") a.type = text(v, "type");
            else if (k == "min") a.min = number(v, "min");
            else if (k == "max") a.max = number(v, "max");
            else if (k == "default") def = &v;
            else error(v, std::format("unknown key '{}' in [[rules.orders.args]] (name, type, min, max, default)", k.str()));
        }
        if (!checkName(n, a.name, std::format("order {}'s arguments", owner.name), named(owner.args, a.name) != nullptr)) return;
        if (!oneOf(kArgTypes, a.type)) {
            error(n, std::format("the argument '{}' has type '{}'; the types are {}", a.name, a.type, joinedNames(kArgTypes)));
            return;
        }
        if ((a.min || a.max) && a.type != "int") error(n, std::format("only whole-number arguments take min and max ('{}' is {})", a.name, a.type));
        if (a.min && a.max && *a.min > *a.max) error(n, std::format("the argument '{}' has min {} above max {}", a.name, *a.min, *a.max));
        if (def) {
            a.hasDefault = true;
            if (a.type == "bool") {
                if (const auto* b = def->as_boolean()) a.defaultValue = script::Value(b->get());
                else error(*def, "this argument's default is true or false");
            } else if (a.type == "text") {
                if (const auto* s = def->as_string()) a.defaultValue = script::Value(std::string(s->get()));
                else error(*def, "this argument's default is text");
            } else if (a.type == "int") {
                if (const auto* i = def->as_integer()) {
                    a.defaultValue = script::Value(i->get());
                    if ((a.min && i->get() < *a.min) || (a.max && i->get() > *a.max))
                        error(*def, std::format("the default {} of '{}' is outside its range", i->get(), a.name));
                } else {
                    error(*def, "this argument's default is a whole number");
                }
            } else {
                error(*def, std::format("an argument naming a {} has no default (it may be null when not given)", a.type));
            }
        }
        owner.args.push_back(std::move(a));
    }

    void order(const toml::table& t, const toml::node& n) {
        ModOrderDecl o;
        o.line = static_cast<int>(n.source().begin.line);
        const toml::node* args = nullptr;
        for (const auto& [k, v] : t) {
            if (k == "name") o.name = text(v, "name");
            else if (k == "label") o.label = text(v, "label");
            else if (k == "description") o.description = text(v, "description");
            else if (k == "applies_to") o.appliesTo = text(v, "applies_to");
            else if (k == "args") args = &v;
            else error(v, std::format("unknown key '{}' in [[rules.orders]] (name, label, description, applies_to, args)", k.str()));
        }
        if (!checkName(n, o.name, "orders", named(out_.orders, o.name) != nullptr)) return;
        if (!oneOf(kOrderTargets, o.appliesTo))
            error(n, std::format("the order '{}' applies to '{}'; an order applies to {}", o.name, o.appliesTo, joinedNames(kOrderTargets)));
        if (args) each(*args, "[[rules.orders.args]]", [&](const toml::table& e, const toml::node& en) { argument(e, en, o); });
        if (o.label.empty()) o.label = o.name;
        out_.orders.push_back(std::move(o));
    }

    void event(const toml::table& t, const toml::node& n) {
        ModEventDecl e;
        e.line = static_cast<int>(n.source().begin.line);
        bool hasChance = false;
        for (const auto& [k, v] : t) {
            if (k == "name") e.name = text(v, "name");
            else if (k == "label") e.label = text(v, "label");
            else if (k == "chance") {
                hasChance = true;
                const auto c = number(v, "chance");
                if (c && (*c < 0 || *c > 100)) error(v, "'chance' is a percentage per game turn, 0 to 100");
                else if (c) e.chance = static_cast<int>(*c);
            } else if (k == "target") e.target = text(v, "target");
            else if (k == "first_turn") {
                const auto f = number(v, "first_turn");
                if (f && (*f < 0 || *f > 1'000'000)) error(v, "'first_turn' is a game turn, from 0");
                else if (f) e.firstTurn = static_cast<uint32_t>(*f);
            } else if (k == "option") e.option = text(v, "option");
            else error(v, std::format("unknown key '{}' in [[rules.events]] (name, label, chance, target, first_turn, option)", k.str()));
        }
        if (!checkName(n, e.name, "events", named(out_.events, e.name) != nullptr)) return;
        if (!hasChance) error(n, std::format("the event '{}' needs its chance, a percentage per game turn (0 to 100)", e.name));
        if (!oneOf(kEventTargets, e.target))
            error(n, std::format("the event '{}' has target '{}'; the targets are {}", e.name, e.target, joinedNames(kEventTargets)));
        if (e.label.empty()) e.label = e.name;
        out_.events.push_back(std::move(e));
    }

    void intel(const toml::table& t, const toml::node& n) {
        ModIntelDecl d;
        d.line = static_cast<int>(n.source().begin.line);
        for (const auto& [k, v] : t) {
            if (k == "type") d.type = text(v, "type");
            else if (k == "description") d.description = text(v, "description");
            else error(v, std::format("unknown key '{}' in [[rules.intel_projects]] (type, description)", k.str()));
        }
        if (d.type.empty()) return error(n, "[[rules.intel_projects]] needs the IntelProjects.txt Type it carries out, such as type = \"Mod - Data Theft\"");
        if (std::any_of(out_.intelProjects.begin(), out_.intelProjects.end(), [&](const ModIntelDecl& x) { return x.type == d.type; }))
            return error(n, std::format("the intelligence project type '{}' is declared twice", d.type));
        out_.intelProjects.push_back(std::move(d));
    }

    void victory(const toml::table& t, const toml::node& n) {
        ModVictoryDecl v;
        v.line = static_cast<int>(n.source().begin.line);
        for (const auto& [k, x] : t) {
            if (k == "name") v.name = text(x, "name");
            else if (k == "label") v.label = text(x, "label");
            else if (k == "option") v.option = text(x, "option");
            else error(x, std::format("unknown key '{}' in [[rules.victory]] (name, label, option)", k.str()));
        }
        if (!checkName(n, v.name, "victory conditions", named(out_.victories, v.name) != nullptr)) return;
        if (v.label.empty()) v.label = v.name;
        out_.victories.push_back(std::move(v));
    }

    std::string_view source_;
    std::vector<std::string>& errors_;
    RulesDecl& out_;
};

} // namespace

bool isModArgType(std::string_view type) { return oneOf(kArgTypes, type); }
bool isModOrderTarget(std::string_view appliesTo) { return oneOf(kOrderTargets, appliesTo); }

bool validRulesName(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() < 'a' || name.front() > 'z') return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool dottedPythonName(std::string_view name) {
    if (name.empty()) return false;
    for (size_t from = 0; from <= name.size();) {
        const size_t dot = std::min(name.find('.', from), name.size());
        if (!validPythonName(name.substr(from, dot - from))) return false;
        from = dot + 1;
    }
    return true;
}

const ModOptionDecl* RulesDecl::option(std::string_view name) const { return named(options, name); }
const ModOrderDecl* RulesDecl::order(std::string_view name) const { return named(orders, name); }
const ModEventDecl* RulesDecl::event(std::string_view name) const { return named(events, name); }
const ModVictoryDecl* RulesDecl::victory(std::string_view name) const { return named(victories, name); }

bool RulesDecl::empty() const {
    return !playersSeeModData && modules.empty() && options.empty() && orders.empty() && events.empty() && intelProjects.empty() &&
           victories.empty();
}

void parseRulesTable(const toml::node& node, std::string_view source, RulesDecl& out, std::vector<std::string>& errors) {
    RulesParser(source, errors, out).parse(node);
}

} // namespace opense4::mods
