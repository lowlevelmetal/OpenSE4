// What opense4-sdk check says about a mod's rules (docs/sdk/rules.md
// "Checking a rules mod"): its scripts/ files import and compile, what they
// register matches what mod.toml declares, and its scenarios read.

#include "sdk/players.hpp"
#include "sdk/rules.hpp"
#include "sdk/scenario.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace opense4::sdk {

namespace {

std::string fileText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool importable(std::string_view path) {
    for (size_t from = 0; from < path.size();) {
        size_t slash = path.find('/', from);
        if (slash == std::string_view::npos) slash = path.size() - 3;   // the file's name, without ".py"
        if (!mods::validPythonName(path.substr(from, slash - from))) return false;
        from = slash + 1;
        if (slash == path.size() - 3) break;
    }
    return true;
}

} // namespace

PlayerCheck checkModRules(const mods::Package& p) {
    PlayerCheck out;
    const mods::RulesDecl& decl = p.manifest.rules;
    std::vector<std::pair<std::string, std::string>> files;   // under scripts/, as the interpreter gets them
    std::vector<std::string> modules = decl.modules;
    for (const mods::PackageFile& f : p.files) {
        if (!f.path.starts_with("scripts/") || !f.path.ends_with(".py")) continue;
        const std::string rel = f.path.substr(8);
        if (!importable(rel)) {
            out.warnings.push_back(std::format("{}: not a Python module name, so the rules cannot import it", f.path));
            continue;
        }
        files.emplace_back(rel, fileText(f.real));
        if (decl.modules.empty() && rel.find('/') == std::string::npos) modules.push_back(rel.substr(0, rel.size() - 3));
        else if (decl.modules.empty() && rel.ends_with("/__init__.py") && rel.find('/') == rel.size() - 12) modules.push_back(rel.substr(0, rel.size() - 12));
    }
    std::sort(modules.begin(), modules.end());
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
    for (const std::string& name : p.manifest.rules.modules) {
        std::string path = name;
        std::replace(path.begin(), path.end(), '.', '/');
        const bool found = std::any_of(files.begin(), files.end(), [&](const auto& f) { return f.first == path + ".py" || f.first == path + "/__init__.py"; });
        if (!found) out.errors.push_back(std::format("mod.toml: [rules] modules names {}, but scripts/ has no {}.py nor {}/__init__.py", name, path, path));
    }
    if (files.empty()) {
        if (!decl.empty()) out.warnings.push_back("mod.toml declares rules ([rules]), but the mod has no scripts/ to carry them out");
    }

    // The scripts import, and what they register matches the declarations.
    std::vector<std::pair<std::string, std::string>> registered;   // kind, name
    if (!files.empty()) {
        script::Limits limits;
        limits.cStackBytes = size_t{1} << 20;
        limits.heapBytes = size_t{64} << 20;
        auto interp = script::Interpreter::create(limits);
        if (!interp) {
            out.warnings.push_back("the rules scripts were not checked: " + interp.error().describe());
        } else {
            for (const script::LibraryFile& f : packageFiles()) (void)(*interp)->addFile(f.path, std::string(f.text));
            for (const auto& [path, text] : files)
                if (auto r = (*interp)->addFile(path, text); !r) out.errors.push_back(std::format("scripts/{}: {}", path, r.error().describe()));
            script::ValueList names;
            for (const std::string& m : modules) names.push_back(script::Value(m));
            script::ValueMap mod{{"id", script::Value(p.id())}, {"modules", script::Value(std::move(names))}};
            script::ValueMap request{{"api", script::Value(int64_t{1})},
                                     {"call", script::Value("load")},
                                     {"mods", script::Value(script::ValueList{script::Value(std::move(mod))})}};
            auto r = (*interp)->call("opense4._rules_engine", "dispatch", std::vector<script::Value>{script::Value(std::move(request))},
                                     script::CallOptions{200'000'000});
            if (!r) {
                out.errors.push_back("the rules scripts could not be loaded: " + r.error().describe());
            } else if (const script::Value* loaded = r->find("mods"); loaded && loaded->isList() && loaded->size() == 1) {
                const script::Value& m = loaded->asList()[0];
                if (const script::Value* e = m.find("error"); e && e->isMap()) {
                    const script::Value* type = e->find("type");
                    const script::Value* message = e->find("message");
                    const script::Value* tb = e->find("traceback");
                    out.errors.push_back(std::format("the rules scripts do not import: {}: {}{}", type && type->isString() ? type->asString() : "Error",
                                                     message && message->isString() ? message->asString() : "",
                                                     tb && tb->isString() && !tb->asString().empty() ? "\n" + tb->asString() : ""));
                }
                if (const script::Value* regs = m.find("registered"); regs && regs->isList())
                    for (const script::Value& x : regs->asList()) {
                        const script::Value* kind = x.find("kind");
                        const script::Value* name = x.find("name");
                        if (kind && kind->isString() && name && name->isString()) registered.emplace_back(kind->asString(), name->asString());
                    }
            }
        }
    }
    auto has = [&](std::string_view kind, std::string_view name) {
        return std::any_of(registered.begin(), registered.end(), [&](const auto& r) { return r.first == kind && r.second == name; });
    };
    if (!files.empty() && out.errors.empty()) {
        for (const mods::ModOrderDecl& o : decl.orders)
            if (!has("order", o.name))
                out.errors.push_back(std::format("mod.toml:{}: the order '{}' has no effect: register one with @rules.order(\"{}\")", o.line, o.name, o.name));
        for (const mods::ModEventDecl& e : decl.events)
            if (!has("event", e.name))
                out.errors.push_back(std::format("mod.toml:{}: the event '{}' has no effect: register one with @rules.event(\"{}\")", e.line, e.name, e.name));
        for (const mods::ModIntelDecl& i : decl.intelProjects)
            if (!has("intel_project", i.type))
                out.errors.push_back(std::format("mod.toml:{}: the intelligence project type '{}' has no effect: register one with @rules.intel_project(\"{}\")",
                                                 i.line, i.type, i.type));
        for (const mods::ModVictoryDecl& v : decl.victories)
            if (!has("victory", v.name))
                out.errors.push_back(std::format("mod.toml:{}: the victory condition '{}' has no test: register one with @rules.victory(\"{}\")", v.line,
                                                 v.name, v.name));
        for (const auto& [kind, name] : registered) {
            const bool declared = kind == "hook" || kind == "objective" ||
                                  ((kind == "order" || kind == "order_check") && decl.order(name)) || (kind == "event" && decl.event(name)) ||
                                  (kind == "victory" && decl.victory(name)) ||
                                  (kind == "intel_project" && std::any_of(decl.intelProjects.begin(), decl.intelProjects.end(),
                                                                           [&](const mods::ModIntelDecl& i) { return i.type == name; }));
            if (!declared)
                out.errors.push_back(std::format("the scripts register a {} '{}' that mod.toml does not declare, so it never runs", kind, name));
        }
    }

    // The scenarios read, and their actions are registered.
    for (const std::string& name : scenarioNames(p)) {
        auto s = loadScenario(p, name);
        if (!s) {
            out.errors.insert(out.errors.end(), s.error().begin(), s.error().end());
            continue;
        }
        const std::vector<ModOptionChoice> options = modOptions(std::span<const mods::Package>(&p, 1));
        for (const auto& [option, value] : s->options) {
            game::GameOptions o;
            if (std::string why = setModOption(o, options, std::format("{}:{}", p.id(), option), value); !why.empty())
                out.errors.push_back(std::format("scenarios/{}.toml: {}", name, why));
        }
        if (out.errors.empty() && !files.empty())
            for (const ScenarioObjective& o : s->objectives)
                if (!o.action.empty() && !has("objective", o.action))
                    out.errors.push_back(std::format("scenarios/{}.toml:{}: the objective '{}' names the action '{}', which the scripts do not register",
                                                     name, o.line, o.name, o.action));
    }
    return out;
}

} // namespace opense4::sdk
