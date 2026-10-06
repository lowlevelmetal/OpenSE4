// The values interface extensions show (sdk/ui.hpp, docs/sdk/interface.md
// "Values"): read from the player's own view of the game, and computed by the
// mods' ui/*.py functions in the script runtime, a batch at a time.

#include "sdk/players.hpp"
#include "sdk/player_values.hpp"
#include "sdk/rules.hpp"
#include "sdk/rules_view.hpp"
#include "sdk/ui.hpp"
#include "sdk/view.hpp"
#include "sdk/worker.hpp"

#include "core/log.hpp"
#include "script/runtime.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#if defined(__SANITIZE_ADDRESS__)
#define OPENSE4_SDK_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OPENSE4_SDK_SANITIZED 1
#endif
#endif

namespace opense4::sdk {

using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

// What reading charges a computed value, in bytecodes (as the rules' reading).
constexpr int64_t kReadNodeCost = 5;
constexpr int64_t kAbilityCost = 100;
// Importing the mods' ui/ modules, apart from the batch's budget.
constexpr int64_t kLoadBudget = 50'000'000;
#if defined(OPENSE4_SDK_SANITIZED)
constexpr size_t kCStackBytes = size_t{1} << 20;
#else
constexpr size_t kCStackBytes = size_t{256} << 10;
#endif

std::string fileText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The modules a mod's ui/ folder holds directly, by name: its files and packages.
std::vector<std::string> uiModules(const mods::Package& p) {
    std::vector<std::string> out;
    for (const mods::PackageFile& f : p.files) {
        if (!f.path.starts_with("ui/") || !f.path.ends_with(".py")) continue;
        const std::string rest = f.path.substr(3);
        std::string name;
        if (rest.find('/') == std::string::npos) name = rest.substr(0, rest.size() - 3);
        else if (rest.ends_with("/__init__.py") && rest.find('/') == rest.size() - 12) name = rest.substr(0, rest.size() - 12);
        if (!name.empty() && mods::validPythonName(name) && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string errorLine(const Value& e) {
    const Value* type = e.find("type");
    const Value* message = e.find("message");
    std::string out = type && type->isString() ? type->asString() : std::string("Error");
    if (message && message->isString() && !message->asString().empty()) out += ": " + message->asString();
    return out;
}

std::string tracebackOf(const Value& e) {
    const Value* t = e.find("traceback");
    return t && t->isString() ? t->asString() : std::string();
}

std::string keyOf(const UiValueSource& s, const UiThing& t) {
    return std::format("{}|{}|{}|{}|{}|{}", static_cast<int>(s.kind), s.mod, s.path, s.format, t.kind, t.id);
}

} // namespace

struct UiValues::Impl {
    const game::Rules& rules;
    const game::GameState& state;
    game::EmpireId player;
    std::vector<const mods::Package*> packages;
    UiBudgets budgets;
    std::optional<Perspective> perspective;
    std::optional<Value> my;
    std::map<std::pair<std::string, int64_t>, Value> records;
    std::map<std::string, UiShown> cache;
    Value rulesView;
    script::Interpreter* interp = nullptr;   // while a batch runs

    Impl(const game::Rules& r, const game::GameState& s, game::EmpireId e, std::span<const mods::Package> p, UiBudgets b)
        : rules(r), state(s), player(e), budgets(b) {
        for (const mods::Package& x : p) packages.push_back(&x);
    }

    const Perspective& view() {
        if (!perspective) perspective.emplace(rules, state, player);
        return *perspective;
    }

    const Value& myPart() {
        if (!my) my = buildViewPart(view(), "my");
        return *my;
    }

    const Value& record(const UiThing& t) {
        const auto key = std::pair{t.kind, t.id};
        auto it = records.find(key);
        if (it == records.end()) it = records.emplace(key, buildViewRecord(view(), t.kind, t.id)).first;
        return it->second;
    }

    // The mod's data on the thing as the player's view holds it (their own empire, colonies and vehicles).
    Value modData(const UiValueSource& s, const UiThing& t) {
        const Value* data = myPart().find("mod_data");
        const Value* entry = data ? data->find(s.mod) : nullptr;
        if (!entry) return Value();
        const Value* v = nullptr;
        if (t.kind == "empire") {
            if (t.id == static_cast<int64_t>(player.value)) v = entry->find("empire");
        } else if (t.kind == "colony" || t.kind == "object") {
            if (const Value* c = entry->find("colonies")) v = c->find(std::to_string(t.id));
        } else if (t.kind == "vehicle") {
            if (const Value* c = entry->find("vehicles")) v = c->find(std::to_string(t.id));
        }
        if (!v) return Value();
        return uiFieldValue(*v, s.path);
    }

    Value ability(const UiValueSource& s, const UiThing& t) {
        std::string kind = t.kind;
        if (kind == "object") {
            // A planet: its colony's facilities, when the player sees one.
            if (t.id < 0 || !view().state().colony(game::ObjectId{static_cast<uint32_t>(t.id)})) return Value();
            kind = "colony";
        }
        if (kind != "vehicle" && kind != "colony" && kind != "system" && kind != "design") return Value();
        // Only what the view lists: never the abilities of a thing the player does not know.
        if (record(t).isNull()) return Value();
        auto v = abilityValue(rules, view().state(), kind, t.id, s.path);
        if (!v || !*v) return Value();
        return Value(**v);
    }

    UiShown plain(const UiValueSource& s, const UiThing& t) {
        Value v;
        switch (s.kind) {
            case UiValueSource::Kind::Field: v = uiFieldValue(record(t), s.path); break;
            case UiValueSource::Kind::ModData: v = modData(s, t); break;
            case UiValueSource::Kind::Ability: v = ability(s, t); break;
            case UiValueSource::Kind::Computed: break;
        }
        return UiShown{formatUiValue(v, s.format), v.isNull(), {}, {}};
    }

    // ---- The batch's native functions (module _opense4_ui) ----

    Value native(std::string_view name, const Value& arg) {
        if (!arg.isMap()) throw script::NativeError("TypeError", std::format("_opense4_ui.{} takes a map", name));
        if (name == "read") {
            const Value* what = arg.find("what");
            if (!what || !what->isString()) throw script::NativeError("TypeError", "read: 'what' should be what to read");
            Value out;
            if (const Value* id = arg.find("id"); id && id->isInt()) out = record(UiThing{what->asString(), id->asInt()});
            else if (what->asString() == "my") out = myPart();
            else out = buildViewPart(view(), what->asString());
            if (interp) interp->charge(static_cast<int64_t>(detail::valueNodes(out)) * kReadNodeCost);
            return out;
        }
        if (name == "ability") {
            if (interp) interp->charge(kAbilityCost);
            const Value* kind = arg.find("kind");
            const Value* id = arg.find("id");
            const Value* abilityName = arg.find("name");
            if (!kind || !kind->isString() || !id || !id->isInt() || !abilityName || !abilityName->isString())
                throw script::NativeError("TypeError", "ability: 'kind', 'id' and 'name' are needed");
            return ability(UiValueSource{UiValueSource::Kind::Ability, abilityName->asString(), {}, {}}, UiThing{kind->asString(), id->asInt()});
        }
        if (name == "rules") {
            if (rulesView.isNull()) rulesView = buildRulesView(rules);
            if (interp) interp->charge(static_cast<int64_t>(detail::valueNodes(rulesView)) * kReadNodeCost);
            return rulesView;
        }
        throw script::NativeError("NameError", std::format("_opense4_ui has no {}", name));
    }

    // Runs the computed values of a batch in one interpreter; the results by key.
    std::map<std::string, UiShown> run(const std::vector<std::pair<std::string, UiValueRequest>>& batch, std::string& problem) {
        std::map<std::string, UiShown> out;
        script::Limits limits;
        limits.heapBytes = budgets.heapBytes;
        limits.budget = kLoadBudget + budgets.perBatch;
        limits.cStackBytes = kCStackBytes;
        auto made = script::Interpreter::create(limits);
        if (!made) {
            problem = "the script runtime could not start: " + made.error().describe();
            return out;
        }
        std::unique_ptr<script::Interpreter> in = std::move(*made);
        interp = in.get();
        for (const script::LibraryFile& f : packageFiles()) (void)in->addFile(f.path, std::string(f.text));
        // Each UI mod's ui/ files, at the interpreter's root.
        std::vector<std::string> added;
        ValueList mods;
        for (const mods::Package* p : packages) {
            const std::vector<std::string> modules = uiModules(*p);
            if (modules.empty()) continue;
            for (const mods::PackageFile& f : p->files) {
                if (!f.path.starts_with("ui/") || !f.path.ends_with(".py")) continue;
                const std::string path = f.path.substr(3);
                if (std::find(added.begin(), added.end(), path) != added.end()) {
                    log::warn("Interface: the mod {} has ui/{}, which another mod's interface already has: left out", p->id(), path);
                    continue;
                }
                if (auto r = in->addFile(path, fileText(f.real)); !r) {
                    log::warn("Interface: the mod {}: ui/{}: {}", p->id(), path, r.error().describe());
                    continue;
                }
                added.push_back(path);
            }
            ValueList names;
            for (const std::string& m : modules) names.emplace_back(m);
            mods.emplace_back(ValueMap{{"id", Value(p->id())}, {"modules", Value(std::move(names))}});
        }
        (void)in->addNativeFunction("_opense4_ui", "read", [this](std::span<const Value> a) { return native("read", a.empty() ? Value::emptyMap() : a[0]); });
        (void)in->addNativeFunction("_opense4_ui", "ability",
                                    [this](std::span<const Value> a) { return native("ability", a.empty() ? Value::emptyMap() : a[0]); });
        (void)in->addNativeFunction("_opense4_ui", "rules", [this](std::span<const Value> a) { return native("rules", a.empty() ? Value::emptyMap() : a[0]); });
        // Import the modules: they register their values.
        std::map<std::string, std::string> loadErrors;   // mod -> why its ui/ modules did not import
        {
            ValueMap request{{"api", Value(int64_t{1})}, {"call", Value("load")}, {"mods", Value(std::move(mods))}};
            auto r = in->call("opense4.ui", "dispatch", std::vector<Value>{Value(std::move(request))}, script::CallOptions{kLoadBudget});
            if (!r) {
                problem = "the interface scripts could not be loaded: " + r.error().describe();
            } else if (const Value* loaded = r->find("mods"); loaded && loaded->isList()) {
                for (const Value& m : loaded->asList())
                    if (const Value* e = m.find("error"); e && e->isMap()) {
                        const Value* id = m.find("id");
                        if (id && id->isString()) {
                            loadErrors[id->asString()] = errorLine(*e) + (tracebackOf(*e).empty() ? "" : "\n" + tracebackOf(*e));
                            log::warn("Interface: the mod {}'s ui/ scripts do not import: {}", id->asString(), errorLine(*e));
                        }
                    }
            }
        }
        for (const auto& [key, req] : batch) {
            UiShown shown;
            if (!problem.empty()) {
                shown.error = problem;
            } else if (auto le = loadErrors.find(req.source->mod); le != loadErrors.end()) {
                const size_t nl = le->second.find('\n');
                shown.error = "the mod's ui/ scripts do not import: " + le->second.substr(0, nl);
                if (nl != std::string::npos) shown.traceback = le->second.substr(nl + 1);
            } else if (in->budgetLeft() <= 0) {
                shown.error = "BudgetExceeded: the values shown at once used up their budget";
            } else {
                ValueMap request{{"api", Value(int64_t{1})},        {"call", Value("value")},
                                 {"mod", Value(req.source->mod)},     {"name", Value(req.source->path)},
                                 {"kind", Value(req.thing.kind)},     {"id", Value(req.thing.id)},
                                 {"empire", Value(static_cast<int64_t>(player.value))}};
                const int64_t budget = std::min(budgets.perValue, in->budgetLeft());
                auto r = in->call("opense4.ui", "dispatch", std::vector<Value>{Value(std::move(request))}, script::CallOptions{budget});
                if (!r) {
                    shown.error = r.error().describe();
                    shown.traceback = r.error().traceback;
                } else if (const Value* e = r->find("error"); e && e->isMap()) {
                    shown.error = errorLine(*e);
                    shown.traceback = tracebackOf(*e);
                } else {
                    const Value* v = r->find("value");
                    const Value value = v ? *v : Value();
                    shown.text = formatUiValue(value, req.source->format);
                    shown.none = value.isNull();
                }
            }
            if (!shown.error.empty()) shown.text = "error";
            out.emplace(key, std::move(shown));
        }
        interp = nullptr;
        in.reset();
        return out;
    }
};

UiValues::UiValues(const game::Rules& r, const game::GameState& s, game::EmpireId player, std::span<const mods::Package> packages, UiBudgets budgets)
    : impl_(std::make_unique<Impl>(r, s, player, packages, budgets)) {}

UiValues::~UiValues() = default;

Value UiValues::record(const UiThing& t) { return impl_->record(t); }

std::vector<UiShown> UiValues::get(std::span<const UiValueRequest> requests) {
    pending_ = false;
    std::vector<std::pair<std::string, UiValueRequest>> batch;
    for (const UiValueRequest& q : requests) {
        if (!q.source) continue;
        const std::string key = keyOf(*q.source, q.thing);
        if (impl_->cache.contains(key)) continue;
        if (q.source->kind != UiValueSource::Kind::Computed) {
            impl_->cache.emplace(key, impl_->plain(*q.source, q.thing));
        } else if (std::none_of(batch.begin(), batch.end(), [&](const auto& b) { return b.first == key; })) {
            batch.emplace_back(key, q);
        }
    }
    std::map<std::string, UiShown> computed;
    if (!batch.empty()) {
        // The runtime holds one interpreter per process: a game's session may have it now.
        std::unique_lock slot(interpreterSlot(), std::try_to_lock);
        if (!slot.owns_lock() || script::Interpreter::active()) {
            pending_ = true;
        } else {
            std::string problem;
            try {
                Worker worker;
                worker.run([&] { computed = impl_->run(batch, problem); });
                ++batches_;
            } catch (const std::exception& e) {
                problem = e.what();
            }
            if (!problem.empty()) log::warn("Interface: {}", problem);
            for (auto& [key, shown] : computed) impl_->cache.insert_or_assign(key, std::move(shown));
        }
    }
    std::vector<UiShown> out;
    out.reserve(requests.size());
    for (const UiValueRequest& q : requests) {
        if (!q.source) {
            out.push_back(UiShown{"-", true, {}, {}});
            continue;
        }
        auto it = impl_->cache.find(keyOf(*q.source, q.thing));
        out.push_back(it != impl_->cache.end() ? it->second : UiShown{"...", true, {}, {}});
    }
    return out;
}

// ---- opense4-sdk check ---------------------------------------------------------------------------------

PlayerCheck checkModUi(const mods::Package& p, std::span<const mods::Package> others) {
    PlayerCheck out;
    if (p.classic) return out;
    // The declarations, with the mods it names.
    std::vector<mods::Package> set(others.begin(), others.end());
    std::erase_if(set, [&](const mods::Package& q) { return q.id() == p.id(); });
    set.push_back(p);
    const UiExtensions ui = loadUiExtensions(set);
    for (const std::string& problem : ui.problemsOf(p.id())) out.errors.push_back(problem);
    UiTexts texts;
    texts.add(p);
    for (const std::string& problem : texts.problems()) out.errors.push_back(problem);
    for (const mods::PackageFile& f : p.files) {
        if (!f.path.starts_with("ui/")) continue;
        const std::string rest = f.path.substr(3);
        const bool toml = rest.find('/') == std::string::npos && rest.ends_with(".toml");
        if (!toml && !rest.ends_with(".py"))
            out.warnings.push_back(std::format("{}: ui/ holds .toml declarations and .py modules; this file is not read", f.path));
    }
    // Computed values: the ui/*.py modules import and register every value named.
    std::vector<std::string> named;
    auto note = [&](const UiValueSource& s, const std::string& file, int line) {
        if (s.kind == UiValueSource::Kind::Computed && s.mod == p.id()) named.push_back(std::format("{}\n{}:{}", s.path, file, line));
    };
    for (const UiPanel& panel : ui.panels)
        if (panel.mod == p.id())
            for (const UiRow& row : panel.rows) note(row.source, panel.file, row.line);
    for (const UiColumn& c : ui.columns)
        if (c.mod == p.id()) note(c.source, c.file, c.line);
    for (const UiEmpirePage& page : ui.pages)
        if (page.mod == p.id())
            for (const UiColumn& c : page.columns) note(c.source, page.file, c.line);
    const std::vector<std::string> modules = uiModules(p);
    if (modules.empty()) {
        for (const std::string& n : named) {
            const size_t nl = n.find('\n');
            out.errors.push_back(std::format("{}: the value '{}' is computed, but the mod has no ui/*.py to register it", n.substr(nl + 1), n.substr(0, nl)));
        }
        return out;
    }
    script::Limits limits;
    limits.cStackBytes = size_t{1} << 20;
    limits.heapBytes = size_t{32} << 20;
    auto interp = script::Interpreter::create(limits);
    if (!interp) {
        out.warnings.push_back("the interface scripts were not checked: " + interp.error().describe());
        return out;
    }
    for (const script::LibraryFile& f : packageFiles()) (void)(*interp)->addFile(f.path, std::string(f.text));
    for (const mods::PackageFile& f : p.files)
        if (f.path.starts_with("ui/") && f.path.ends_with(".py"))
            if (auto r = (*interp)->addFile(f.path.substr(3), fileText(f.real)); !r) out.errors.push_back(std::format("{}: {}", f.path, r.error().describe()));
    ValueList names;
    for (const std::string& m : modules) names.emplace_back(m);
    ValueMap request{{"api", Value(int64_t{1})},
                     {"call", Value("load")},
                     {"mods", Value(ValueList{Value(ValueMap{{"id", Value(p.id())}, {"modules", Value(std::move(names))}})})}};
    std::vector<std::string> registered;
    auto r = (*interp)->call("opense4.ui", "dispatch", std::vector<Value>{Value(std::move(request))}, script::CallOptions{kLoadBudget});
    if (!r) {
        out.errors.push_back("the interface scripts could not be loaded: " + r.error().describe());
        return out;
    }
    if (const Value* loaded = r->find("mods"); loaded && loaded->isList() && loaded->size() == 1) {
        const Value& m = loaded->asList()[0];
        if (const Value* e = m.find("error"); e && e->isMap())
            out.errors.push_back(std::format("the ui/ scripts do not import: {}{}", errorLine(*e), tracebackOf(*e).empty() ? "" : "\n" + tracebackOf(*e)));
        if (const Value* regs = m.find("registered"); regs && regs->isList())
            for (const Value& x : regs->asList())
                if (x.isString()) registered.push_back(x.asString());
    }
    for (const std::string& n : named) {
        const size_t nl = n.find('\n');
        const std::string name = n.substr(0, nl);
        if (std::find(registered.begin(), registered.end(), name) == registered.end())
            out.errors.push_back(std::format("{}: the value '{}' is not registered: @ui.value(\"{}\") in a ui/*.py", n.substr(nl + 1), name, name));
    }
    for (const std::string& name : registered)
        if (std::none_of(named.begin(), named.end(), [&](const std::string& n) { return n.substr(0, n.find('\n')) == name; }))
            out.warnings.push_back(std::format("ui/: the value '{}' is registered, but no panel, column or page shows it", name));
    return out;
}

} // namespace opense4::sdk
