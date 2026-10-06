#include "sdk/rules_engine.hpp"

#include "core/log.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"
#include "learn/condition.hpp"
#include "script/json.hpp"
#include "sdk/names.hpp"
#include "sdk/player_values.hpp"
#include "sdk/players.hpp"
#include "sdk/queries.hpp"
#include "sdk/rules.hpp"
#include "sdk/rules_view.hpp"
#include "sdk/scenario.hpp"
#include "sdk/value_io.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <mutex>
#include <utility>

namespace opense4::sdk::detail {

using game::EmpireId;
using game::Hook;
using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

// What reading and the other native functions charge, in bytecodes
// (docs/sdk/rules.md "Budgets and failures").
constexpr int64_t kReadNodeCost = 5;
constexpr int64_t kQueryCost = 20'000;
constexpr int64_t kNodeCost = 20;
constexpr int64_t kRandomCost = 10;
constexpr int64_t kAbilityCost = 100;
// The budget of loading the rules scripts (importing their modules), not
// counted against any mod's turn.
constexpr int64_t kLoadBudget = 200'000'000;
// Events raised while the events of one safe point are delivered: more is a
// loop, and the rest are dropped.
constexpr size_t kMaxEventsPerDelivery = 100'000;

Value errorMap(const Value& response) {
    const Value* e = response.find("error");
    return e && e->isMap() ? *e : Value();
}

std::string errorText(const Value& error) {
    const Value* type = error.find("type");
    const Value* message = error.find("message");
    std::string out = type && type->isString() ? type->asString() : std::string("Error");
    if (message && message->isString() && !message->asString().empty()) out += ": " + message->asString();
    return out;
}

std::string tracebackOf(const Value& error) {
    const Value* t = error.find("traceback");
    return t && t->isString() ? t->asString() : std::string();
}

// The registrations of a mod set's rules scripts, per process: what a module
// registers depends on its text only, so the mods' identities key it.
std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, std::vector<ModRegistrations>>& registrationCache() {
    static std::map<std::string, std::vector<ModRegistrations>> c;
    return c;
}

// The modules a rules mod's scripts/ folder holds directly, by name: the
// files and the packages.
std::vector<std::string> topModules(const mods::Package& p) {
    if (!p.manifest.rules.modules.empty()) return p.manifest.rules.modules;
    std::vector<std::string> out;
    for (const mods::PackageFile& f : p.files) {
        if (!f.path.starts_with("scripts/") || !f.path.ends_with(".py")) continue;
        const std::string rest = f.path.substr(8);
        std::string name;
        if (rest.find('/') == std::string::npos) name = rest.substr(0, rest.size() - 3);
        else if (rest.ends_with("/__init__.py") && rest.find('/') == rest.size() - 12) name = rest.substr(0, rest.size() - 12);
        if (!name.empty() && mods::validPythonName(name) && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

Value idOrNull(EmpireId e) { return e.valid() ? Value(static_cast<int64_t>(e.value)) : Value(); }

} // namespace

std::vector<const mods::Package*> rulesModsOf(const game::Rules& r, const game::GameState& s, std::span<const mods::Package> extra) {
    std::vector<const mods::Package*> out;
    auto consider = [&](const mods::Package& p) {
        if (!(p.tiers & mods::kTierScripts)) return;
        if (std::any_of(out.begin(), out.end(), [&](const mods::Package* q) { return q->id() == p.id(); })) return;
        const auto rec = std::find_if(s.mods.begin(), s.mods.end(), [&](const ruleset::ModRecord& m) { return m.id == p.id() && m.affectsGame; });
        if (rec == s.mods.end()) return;
        out.push_back(&p);
    };
    for (const mods::Package& p : gamePackages(r)) consider(p);
    for (const mods::Package& p : extra) consider(p);
    // In the game's load order.
    auto rank = [&](const mods::Package* p) {
        return std::find_if(s.mods.begin(), s.mods.end(), [&](const ruleset::ModRecord& m) { return m.id == p->id(); }) - s.mods.begin();
    };
    std::stable_sort(out.begin(), out.end(), [&](const mods::Package* a, const mods::Package* b) { return rank(a) < rank(b); });
    return out;
}

RulesEngine::RulesEngine(const game::Rules& r, game::GameState& s, std::vector<const mods::Package*> mods, bool ownPackage, ScriptHost& host)
    : rules_(r), state_(s), mods_(std::move(mods)), ownPackage_(ownPackage), host_(host) {
    // What the game keeps about each rules mod (players_see_mod_data above all,
    // which the views other empires get read).
    for (size_t i = 0; i < mods_.size(); ++i) stateOf(s, i);
}

RulesEngine::~RulesEngine() = default;

game::ModRulesState& RulesEngine::stateOf(game::GameState& s, size_t mod) {
    const mods::Package& p = *mods_[mod];
    auto it = std::find_if(s.modRules.begin(), s.modRules.end(), [&](const game::ModRulesState& m) { return m.mod == p.id(); });
    if (it == s.modRules.end()) {
        game::ModRulesState st;
        st.mod = p.id();
        st.playersSee = p.manifest.rules.playersSeeModData;
        st.turn = s.turn;
        s.modRules.push_back(std::move(st));
        it = s.modRules.end() - 1;
    }
    if (it->turn != s.turn) {
        it->turn = s.turn;
        it->failures = 0;
        it->budgetUsed = 0;
        it->failedHooks.clear();
    }
    return *it;
}

// ---- Loading ------------------------------------------------------------------------------------------------

void RulesEngine::ensureLoaded() const {
    if (loaded_ || loading_) return;
    std::string key = ownPackage_ ? "own" : "package";
    for (const mods::Package* p : mods_) key += std::format("|{}@{}", p->id(), p->hash);
    {
        std::lock_guard lock(cacheMutex());
        auto it = registrationCache().find(key);
        if (it != registrationCache().end()) {
            loaded_ = it->second;
            return;
        }
    }
    // The first time in this process: the scripts are imported to learn
    // what they register.
    auto* self = const_cast<RulesEngine*>(this);
    loading_ = true;
    std::vector<ModRegistrations> regs(mods_.size());
    for (size_t i = 0; i < mods_.size(); ++i) regs[i].mod = mods_[i]->id();
    bool cacheable = false;
    if (!self->host_.startScripts()) {
        for (ModRegistrations& m : regs) m.error = self->host_.scriptsError();
        log::warn("Rules mods: {}", self->host_.scriptsError());
    } else {
        ValueList list;
        for (const mods::Package* p : mods_) {
            ValueList modules;
            for (const std::string& m : topModules(*p)) modules.push_back(Value(m));
            list.push_back(Map(2)("id", Value(p->id()))("modules", Value(std::move(modules))).done());
        }
        auto r = self->host_.runScript("opense4._rules_engine", "dispatch",
                                        Map(3)("api", Value(kApiVersion))("call", Value("load"))("mods", Value(std::move(list))).done(), kLoadBudget);
        if (!r) {
            for (ModRegistrations& m : regs) m.error = r.error().describe();
            log::warn("Rules mods: the rules scripts could not be loaded: {}", r.error().describe());
            if (!r.error().traceback.empty()) log::warn("{}", r.error().traceback);
        } else if (const Value* loadedMods = r->find("mods"); loadedMods && loadedMods->isList() && loadedMods->size() == regs.size()) {
            cacheable = true;
            for (size_t i = 0; i < regs.size(); ++i) {
                const Value& m = loadedMods->asList()[i];
                if (const Value* e = m.find("error"); e && e->isMap()) {
                    regs[i].error = errorText(*e);
                    log::warn("Rules mod {}: its scripts did not load: {}; its rules do not run", regs[i].mod, regs[i].error);
                    if (const std::string tb = tracebackOf(*e); !tb.empty()) log::warn("{}", tb);
                }
                if (const Value* reg = m.find("registered"); reg && reg->isList())
                    for (const Value& x : reg->asList()) {
                        Registration g;
                        auto text = [&](std::string_view k) {
                            const Value* v = x.find(k);
                            return v && v->isString() ? v->asString() : std::string();
                        };
                        g.kind = text("kind");
                        g.name = text("name");
                        g.step = text("step");
                        g.when = text("when");
                        regs[i].registered.push_back(std::move(g));
                    }
            }
        } else {
            for (ModRegistrations& m : regs) m.error = "the rules scripts' loader answered in the wrong shape";
        }
        self->interpLoaded_ = true;
    }
    loading_ = false;
    if (cacheable) {
        std::lock_guard lock(cacheMutex());
        registrationCache()[key] = regs;
    }
    loaded_ = std::move(regs);
}

bool RulesEngine::registered(size_t mod, std::string_view kind, std::string_view name, const game::HookArgs* args) const {
    ensureLoaded();
    if (!loaded_ || mod >= loaded_->size()) return false;
    for (const Registration& g : (*loaded_)[mod].registered) {
        if (g.kind != kind || g.name != name) continue;
        if (args && kind == "hook" && name == game::hookName(Hook::EmpireEndOfTurn)) {
            if (!g.step.empty() && g.step != game::endStepName(args->step)) continue;
            if (!g.when.empty() && g.when != (args->after ? "after" : "before")) continue;
        }
        return true;
    }
    return false;
}

// ---- Calls ------------------------------------------------------------------------------------------------

std::optional<Value> RulesEngine::callMod(game::TurnContext& ctx, size_t mod, std::string_view call, std::string_view name, Value args,
                                          std::string_view failureKey, int64_t extraBudget) {
    game::GameState& s = ctx.state;
    const std::string& id = mods_[mod]->id();
    {
        game::ModRulesState& st = stateOf(s, mod);
        if (st.failures >= kRulesFailuresPerTurn) return std::nullopt;
        if (st.budgetUsed >= s.options.rulesTurnBudget) {
            if (std::find(st.failedHooks.begin(), st.failedHooks.end(), "*budget*") == st.failedHooks.end()) {
                st.failedHooks.emplace_back("*budget*");
                log::warn("Rules mod {}: its functions used up the turn's budget ({} bytecodes); they are skipped for the rest of the turn", id,
                          s.options.rulesTurnBudget);
            }
            return std::nullopt;
        }
        if (std::find(st.failedHooks.begin(), st.failedHooks.end(), failureKey) != st.failedHooks.end()) return std::nullopt;
    }
    if (host_.inScriptCall()) {
        // An engine function of a running script (a mod order through a
        // computer player's apply service) cannot call scripts again.
        return std::nullopt;
    }
    if (!host_.startScripts()) {
        fail(ctx, mod, failureKey, host_.scriptsError());
        return std::nullopt;
    }
    if (!interpLoaded_) {
        loaded_.reset();
        // The registrations are known (from the cache); import the modules
        // in this interpreter.
        ValueList list;
        for (const mods::Package* p : mods_) {
            ValueList modules;
            for (const std::string& m : topModules(*p)) modules.push_back(Value(m));
            list.push_back(Map(2)("id", Value(p->id()))("modules", Value(std::move(modules))).done());
        }
        auto r = host_.runScript("opense4._rules_engine", "dispatch",
                                  Map(3)("api", Value(kApiVersion))("call", Value("load"))("mods", Value(std::move(list))).done(), kLoadBudget);
        interpLoaded_ = true;
        if (!r) log::warn("Rules mods: the rules scripts could not be loaded: {}", r.error().describe());
        ensureLoaded();
    }
    const int64_t left = s.options.rulesTurnBudget - stateOf(s, mod).budgetUsed;
    int64_t budget = std::min(s.options.rulesHookBudget, left);
    if (extraBudget >= 0) budget = std::min(budget, extraBudget);

    ValueMap request;
    request.emplace_back("api", Value(kApiVersion));
    request.emplace_back("call", Value(call));
    request.emplace_back("mod", Value(id));
    request.emplace_back("name", Value(name));
    request.emplace_back("turn", Value(static_cast<int64_t>(s.turn)));
    request.emplace_back("args", std::move(args));

    Active active{&ctx, mod, std::string(call), std::string(name)};
    script::Result<Value> r = std::unexpected(script::Error{});
    {
        struct Handling {
            Active*& slot;
            Active* before;
            Handling(Active*& at, Active& a) : slot(at), before(at) { slot = &a; }
            ~Handling() { slot = before; }
            Handling(const Handling&) = delete;
            Handling& operator=(const Handling&) = delete;
        } handling(active_, active);
        r = host_.runScript("opense4._rules_engine", "dispatch", Value(std::move(request)), budget);
    }
    stateOf(s, mod).budgetUsed += host_.lastCallBudget();
    if (!r) {
        // Running out of what was left of the turn's budget is not the
        // function's failure: the mod's functions stop for the turn.
        if (r.error().kind == script::ErrorKind::Budget && budget < s.options.rulesHookBudget && budget == left) {
            game::ModRulesState& st = stateOf(s, mod);
            st.budgetUsed = std::max(st.budgetUsed, s.options.rulesTurnBudget);
            st.failedHooks.emplace_back("*budget*");
            log::warn("Rules mod {}: {} {} used up the turn's budget ({} bytecodes); its functions are skipped for the rest of the turn", id, call,
                      name, s.options.rulesTurnBudget);
            return std::nullopt;
        }
        fail(ctx, mod, failureKey, std::format("{} {}: {}", call, name, r.error().describe()), r.error().traceback);
        return std::nullopt;
    }
    const Value& response = *r;
    if (const Value* lines = response.find("log"); lines && lines->isList())
        for (const Value& line : lines->asList())
            if (line.isString()) log::info("Rules mod {}: {}", id, line.asString());
    if (const Value error = errorMap(response); error.isMap()) {
        fail(ctx, mod, failureKey, std::format("{} {}: {}", call, name, errorText(error)), tracebackOf(error));
        return std::nullopt;
    }
    if (const Value* data = response.find("mod_data"); data && data->isList()) {
        std::string problem;
        storeModData(s, mod, *data, problem);
        if (!problem.empty()) {
            fail(ctx, mod, failureKey, std::format("{} {}: {}", call, name, problem));
            return std::nullopt;
        }
    }
    const Value* result = response.find("result");
    return result ? *result : Value();
}

void RulesEngine::fail(game::TurnContext& ctx, size_t mod, std::string_view key, const std::string& what, const std::string& traceback) {
    game::ModRulesState& st = stateOf(ctx.state, mod);
    ++st.failures;
    if (!key.empty() && std::find(st.failedHooks.begin(), st.failedHooks.end(), key) == st.failedHooks.end()) st.failedHooks.emplace_back(key);
    log::warn("Rules mod {}: {} failed: {}; {}", mods_[mod]->id(), key, what,
              st.failures >= kRulesFailuresPerTurn ? "the mod's rules are off for the rest of the turn" : "it is skipped for the rest of the turn");
    if (!traceback.empty()) log::warn("{}", traceback);
}

// ---- Mod data ---------------------------------------------------------------------------------------------

namespace {

std::vector<game::ModData>* modDataOf(game::GameState& s, std::string_view kind, int64_t id) {
    if (kind == "game") return &s.modData;
    if (id < 0 || id > int64_t{UINT32_MAX}) return nullptr;
    const auto i = static_cast<uint32_t>(id);
    if (kind == "empire") return i < s.empires.size() ? &s.empires[i].modData : nullptr;
    if (kind == "colony") {
        game::Colony* c = s.colony(game::ObjectId{i});
        return c ? &c->modData : nullptr;
    }
    if (kind == "vehicle") {
        game::Vehicle* v = s.vehicle(game::VehicleId{i});
        return v && v->count > 0 ? &v->modData : nullptr;
    }
    return nullptr;
}

} // namespace

Value modDataValue(const game::GameState& s, std::string_view mod, std::string_view kind, int64_t id) {
    std::vector<game::ModData>* list = modDataOf(const_cast<game::GameState&>(s), kind, id);
    if (!list) return Value();
    for (const game::ModData& d : *list)
        if (d.mod == mod) {
            auto v = script::parseJson(d.value);
            return v ? std::move(*v) : Value::emptyMap();
        }
    return Value::emptyMap();
}

std::string setModData(game::GameState& s, std::string_view mod, std::string_view kind, int64_t id, const Value& value) {
    std::vector<game::ModData>* list = modDataOf(s, kind, id);
    if (!list) return std::format("no {} {} to keep mod data on", kind, id);
    auto j = script::toJson(value);
    if (!j) return "mod data must be plain values (None, True, False, whole numbers, text, lists and dicts with text keys)";
    if (static_cast<int64_t>(j->size()) > s.options.modDataLimit)
        return std::format("the mod data of {} {} takes {} bytes as JSON; the game allows {}", kind, id, j->size(), s.options.modDataLimit);
    auto it = std::find_if(list->begin(), list->end(), [&](const game::ModData& d) { return d.mod == mod; });
    if (it != list->end() && it->value == *j) return {};
    // An empty map (or null) is no data at all.
    if (value.isNull() || (value.isMap() && value.size() == 0)) {
        if (it != list->end()) list->erase(it);
        return {};
    }
    if (it == list->end()) list->push_back({std::string(mod), std::move(*j)});
    else it->value = std::move(*j);
    return {};
}

void RulesEngine::storeModData(game::GameState& s, size_t mod, const Value& entries, std::string& problem) {
    for (const Value& e : entries.asList()) {
        const Value* kind = e.find("kind");
        const Value* id = e.find("id");
        const Value* value = e.find("value");
        if (!kind || !kind->isString() || !value) continue;
        const int64_t which = id && id->isInt() ? id->asInt() : -1;
        if (!modDataOf(s, kind->asString(), which)) continue;   // gone during the call
        if (std::string why = setModData(s, mods_[mod]->id(), kind->asString(), which, *value); !why.empty()) {
            problem = why;
            return;
        }
    }
}

// ---- Hooks -------------------------------------------------------------------------------------------------

bool RulesEngine::wants(Hook h, const game::HookArgs* args) const {
    ensureLoaded();
    for (size_t i = 0; i < mods_.size(); ++i)
        if (registered(i, "hook", game::hookName(h), args)) return true;
    if (h == Hook::NewGame)
        for (const mods::Package* p : mods_)
            if (!p->manifest.rules.options.empty()) return true;
    if (h == Hook::CheckVictory) {
        if (!state_.scenario.mod.empty()) return true;
        for (size_t i = 0; i < mods_.size(); ++i)
            for (const mods::ModVictoryDecl& v : mods_[i]->manifest.rules.victories)
                if (registered(i, "victory", v.name)) return true;
    }
    return false;
}

Value RulesEngine::hookArgs(game::TurnContext& ctx, Hook h, const game::HookArgs& a) {
    const game::GameState& s = ctx.state;
    auto loc = [&]() { return a.where ? enc(*a.where) : Value(); };
    switch (h) {
        case Hook::NewGame: {
            ValueList empires;
            if (a.setup)
                for (const game::EmpireSetup& e : a.setup->empires)
                    empires.push_back(Map(4)("name", Value(e.name))("kind", enc(e.kind))("preset", Value(e.preset))(
                                          "controller", Value(game::controllerText(e.controller)))
                                          .done());
            return Map(1)("setup", Map(2)("seed", Value(static_cast<int64_t>(a.setup ? a.setup->seed & 0x7fffffffffffffffull : 0)))(
                                       "empires", Value(std::move(empires)))
                                       .done())
                .done();
        }
        case Hook::OrdersApplied: return Map(1)("empire", idOrNull(a.empire)).done();
        case Hook::MovementDay: return Map(2)("day", Value(a.day))("empire", idOrNull(a.empire)).done();
        case Hook::VehicleEnteredSector:
            return Map(3)("vehicle", id(a.vehicle))("location", loc())("empire", idOrNull(a.empire)).done();
        case Hook::BeforeBattle: {
            ValueList present;
            if (a.where) {
                std::vector<EmpireId> seen;
                for (const game::Vehicle& v : s.vehicles)
                    if (v.count > 0 && v.location == *a.where && std::find(seen.begin(), seen.end(), v.owner) == seen.end()) seen.push_back(v.owner);
                for (game::ObjectId o : s.galaxy.system(a.where->system).objects)
                    if (const game::Colony* c = s.colony(o); c && s.galaxy.object(o).sector == a.where->sector &&
                                                             std::find(seen.begin(), seen.end(), c->owner) == seen.end())
                        seen.push_back(c->owner);
                std::sort(seen.begin(), seen.end());
                for (EmpireId e : seen) present.push_back(id(e));
            }
            return Map(2)("location", loc())("empires", Value(std::move(present))).done();
        }
        case Hook::AfterBattle:
            return Map(1)("battle", a.battle && *a.battle < s.combats.size() ? battleRecord(s.combats[*a.battle]) : Value()).done();
        case Hook::VehicleDestroyed:
            return Map(4)("vehicle", a.lost && perspective_ ? buildVehicleRecord(*perspective_, *a.lost) : Value())("cause", Value(a.text))(
                       "location", loc())("empire", idOrNull(a.empire))
                .done();
        case Hook::EmpireEndOfTurn:
            return Map(3)("empire", idOrNull(a.empire))("step", Value(game::endStepName(a.step)))("when", Value(a.after ? "after" : "before")).done();
        case Hook::ColonyEndOfTurn: return Map(1)("colony", id(a.planet)).done();
        case Hook::ColonyFounded: return Map(3)("colony", id(a.planet))("vehicle", id(a.vehicle))("empire", idOrNull(a.empire)).done();
        case Hook::VehicleBuilt:
            return Map(6)("vehicle", id(a.vehicle))("design", id(a.design))("count", Value(a.count))("colony", id(a.planet))("location", loc())(
                       "empire", idOrNull(a.empire))
                .done();
        case Hook::TechResearched:
            return Map(3)("empire", idOrNull(a.empire))("area", Value(static_cast<int64_t>(a.area.value)))("level", Value(a.level)).done();
        case Hook::TreatyChanged:
            return Map(4)("empire", idOrNull(a.empire))("other", idOrNull(a.other))("treaty", enc(a.treaty))("old_treaty", enc(a.oldTreaty)).done();
        case Hook::MessageSent: {
            Value message;
            for (const game::DiplomaticMessage& m : s.messages)
                if (m.id == a.message) message = enc(m);
            return Map(1)("message", std::move(message)).done();
        }
        case Hook::EventFired:
            return Map(7)("name", Value(a.text))("mod", Value())("empire", idOrNull(a.empire))("colony", s.colony(a.planet) ? id(a.planet) : Value())(
                       "object", id(a.planet))("vehicle", id(a.vehicle))("location", loc())
                .done();
        default: return Value::emptyMap();
    }
}

void RulesEngine::run(game::TurnContext& ctx, Hook h, const game::HookArgs& args) {
    if (!perspective_) perspective_.emplace(rules_, ctx.state, EmpireId{}, ViewOptions{true, true});
    if (game::isEventHook(h)) {
        // Noted with what it needs from the state now; delivered at the next safe point.
        pending_.push_back(Pending{h, hookArgs(ctx, h, args), std::nullopt});
        return;
    }
    if (h == Hook::NewGame) fillOptions(ctx.state);
    runEverywhere(ctx, h, args);
    if (h == Hook::CheckVictory && !ctx.state.gameOver) {
        deliver(ctx);
        checkVictories(ctx);
        if (!ctx.state.gameOver) checkObjectives(ctx);
    }
}

void RulesEngine::runEverywhere(game::TurnContext& ctx, Hook h, const game::HookArgs& args) {
    const std::string_view name = game::hookName(h);
    std::optional<Value> built;
    for (size_t i = 0; i < mods_.size(); ++i) {
        if (!registered(i, "hook", name, &args)) continue;
        if (!built) built = hookArgs(ctx, h, args);
        callMod(ctx, i, "hook", name, *built, name);
    }
}

void RulesEngine::deliver(game::TurnContext& ctx) {
    size_t delivered = 0;
    while (!pending_.empty() && delivered < kMaxEventsPerDelivery) {
        Pending p = std::move(pending_.front());
        pending_.pop_front();
        ++delivered;
        if (p.fired) {
            fireModEvent(ctx, *p.fired);
            continue;
        }
        const std::string_view name = game::hookName(p.hook);
        for (size_t i = 0; i < mods_.size(); ++i)
            if (registered(i, "hook", name)) callMod(ctx, i, "hook", name, p.args, name);
    }
    if (!pending_.empty()) {
        log::warn("Rules mods: {} events raised while delivering the events of one moment are dropped (a loop?)", pending_.size());
        pending_.clear();
    }
}

// ---- Options ----------------------------------------------------------------------------------------------

void RulesEngine::fillOptions(game::GameState& s) {
    // Every option the game's rules mods declare, in load order and
    // declaration order, at its value in the setup (in range) or its default.
    std::vector<game::ModOption> all;
    for (const mods::Package* p : mods_)
        for (const mods::ModOptionDecl& d : p->manifest.rules.options) {
            auto it = std::find_if(s.options.modOptions.begin(), s.options.modOptions.end(),
                                   [&](const game::ModOption& o) { return o.mod == p->id() && o.name == d.name; });
            all.push_back({p->id(), d.name, it == s.options.modOptions.end() ? d.defaultValue : std::clamp(it->value, d.min, d.max)});
        }
    s.options.modOptions = std::move(all);
}

std::optional<int64_t> RulesEngine::optionValue(size_t mod, std::string_view name) const {
    const mods::ModOptionDecl* d = mods_[mod]->manifest.rules.option(name);
    if (!d) return std::nullopt;
    for (const game::ModOption& o : state_.options.modOptions)
        if (o.mod == mods_[mod]->id() && o.name == name) return std::clamp(o.value, d->min, d->max);
    return d->defaultValue;
}

bool RulesEngine::switchedOn(size_t mod, const std::string& option) const {
    if (option.empty()) return true;
    const auto v = optionValue(mod, option);
    return v && *v != 0;
}

// ---- Events, intelligence projects, victory, objectives -------------------------------------------------------

void RulesEngine::eventStep(game::TurnContext& ctx, Rng& rng) {
    game::GameState& s = ctx.state;
    for (size_t i = 0; i < mods_.size(); ++i)
        for (const mods::ModEventDecl& e : mods_[i]->manifest.rules.events) {
            if (e.chance <= 0 || s.turn < e.firstTurn || !switchedOn(i, e.option) || !registered(i, "event", e.name)) continue;
            if (rng.range(1, 100) > e.chance) continue;
            Value target;
            std::vector<int64_t> candidates;
            if (e.target == "empire") {
                for (const game::Empire& x : s.empires)
                    if (x.alive) candidates.push_back(x.id.value);
            } else if (e.target == "colony") {
                for (const auto& c : s.colonies)
                    if (c && c->owner.valid() && c->owner.index() < s.empires.size() && s.empire(c->owner).alive) candidates.push_back(c->planet.value);
            } else if (e.target == "vehicle") {
                for (const game::Vehicle& v : s.vehicles)
                    if (v.count > 0) candidates.push_back(v.id.value);
            } else if (e.target == "system") {
                for (const game::StarSystem& sys : s.galaxy.systems) candidates.push_back(sys.id.value);
            }
            if (e.target != "none") {
                if (candidates.empty()) continue;
                target = Map(2)("kind", Value(e.target))("id", Value(candidates[rng.index(candidates.size())])).done();
            }
            fireModEvent(ctx, Fired{i, e.name, std::move(target)});
        }
}

void RulesEngine::fireModEvent(game::TurnContext& ctx, const Fired& f) {
    const mods::ModEventDecl* decl = mods_[f.mod]->manifest.rules.event(f.event);
    if (!decl) return;
    const Value* kind = f.target.find("kind");
    const Value* which = f.target.find("id");
    const std::string targetKind = kind && kind->isString() ? kind->asString() : std::string("none");
    const int64_t targetId = which && which->isInt() ? which->asInt() : -1;
    // The empire it concerns: the target's owner, or the target empire.
    EmpireId empire;
    std::optional<game::Location> where;
    game::ObjectId planet;
    game::VehicleId vehicle;
    const game::GameState& s = ctx.state;
    if (targetKind == "empire" && targetId >= 0 && static_cast<uint64_t>(targetId) < s.empires.size()) empire = EmpireId{static_cast<uint32_t>(targetId)};
    if (targetKind == "colony")
        if (const game::Colony* c = s.colony(game::ObjectId{static_cast<uint32_t>(std::max<int64_t>(targetId, 0))}); c && targetId >= 0) {
            empire = c->owner;
            planet = c->planet;
            where = game::locationOf(s.galaxy, c->planet);
        }
    if (targetKind == "vehicle")
        if (const game::Vehicle* v = s.vehicle(game::VehicleId{static_cast<uint32_t>(std::max<int64_t>(targetId, 0))}); v && targetId >= 0) {
            empire = v->owner;
            vehicle = v->id;
            where = v->location;
        }
    Value event = Map(7)("name", Value(f.event))("mod", Value(mods_[f.mod]->id()))("label", Value(decl->label))("target", f.target)(
                      "empire", idOrNull(empire))("location", where ? enc(*where) : Value())("turn", Value(static_cast<int64_t>(s.turn)))
                      .done();
    if (!callMod(ctx, f.mod, "event", f.event, Map(1)("event", event).done(), "event:" + f.event)) return;
    // Every mod's event_fired hooks hear of it, as of a classic event.
    if (wants(Hook::EventFired)) {
        Value args = Map(7)("name", Value(f.event))("mod", Value(mods_[f.mod]->id()))("empire", idOrNull(empire))(
                         "colony", planet.valid() ? id(planet) : Value())("object", id(planet))("vehicle", id(vehicle))(
                         "location", where ? enc(*where) : Value())
                         .done();
        pending_.push_back(Pending{Hook::EventFired, std::move(args), std::nullopt});
    }
}

std::optional<bool> RulesEngine::intelProject(game::TurnContext& ctx, EmpireId source, const game::IntelProjectOrder& order) {
    const auto& projects = rules_.data().intelProjects;
    if (order.project >= projects.size()) return std::nullopt;
    const ruleset::IntelProject& p = projects[order.project];
    for (size_t i = 0; i < mods_.size(); ++i)
        for (const mods::ModIntelDecl& d : mods_[i]->manifest.rules.intelProjects) {
            if (!datafile::keysEqual(d.type, p.type)) continue;
            if (!registered(i, "intel_project", d.type)) return false;
            Value project = Map(10)("type", Value(d.type))("project", Value(static_cast<int64_t>(order.project)))("name", Value(p.name))(
                                "empire", idOrNull(source))("target", idOrNull(order.target))("target_planet", id(order.targetPlanet))(
                                "target_vehicle", id(order.targetVehicle))("third_empire", idOrNull(order.thirdEmpire))(
                                "target_tech", order.targetTech.valid() ? Value(static_cast<int64_t>(order.targetTech.value)) : Value())(
                                "amount", Value(static_cast<int64_t>(p.effectAmount)))
                                .done();
            const std::optional<Value> r = callMod(ctx, i, "intel_project", d.type, Map(1)("project", std::move(project)).done(), "intel:" + d.type);
            if (!r) return false;
            return !(r->isNull() || (r->isBool() && !r->asBool()));
        }
    return std::nullopt;
}

void RulesEngine::endGame(game::TurnContext& ctx, EmpireId winner, const std::string& reason) {
    game::GameState& s = ctx.state;
    if (s.gameOver) return;
    s.gameOver = true;
    s.winner = winner;
    s.endReason = reason;
    const std::string who = winner.valid() && winner.index() < s.empires.size() ? s.empire(winner).name : std::string();
    for (const game::Empire& e : s.empires)
        if (e.alive)
            ctx.log(e.id, game::LogCategory::Misc, "The game is over", who.empty() ? reason : std::format("{} The {} won.", reason, who));
}

void RulesEngine::checkVictories(game::TurnContext& ctx) {
    for (size_t i = 0; i < mods_.size() && !ctx.state.gameOver; ++i)
        for (const mods::ModVictoryDecl& v : mods_[i]->manifest.rules.victories) {
            if (ctx.state.gameOver) break;
            if (!switchedOn(i, v.option) || !registered(i, "victory", v.name)) continue;
            const std::optional<Value> r = callMod(ctx, i, "victory", v.name, Value::emptyMap(), "victory:" + v.name);
            if (!r || r->isNull() || (r->isBool() && !r->asBool())) continue;
            EmpireId winner;
            if (r->isInt() && r->asInt() >= 0 && static_cast<uint64_t>(r->asInt()) < ctx.state.empires.size())
                winner = EmpireId{static_cast<uint32_t>(r->asInt())};
            else if (!r->isBool()) {
                fail(ctx, i, "victory:" + v.name, std::format("a victory condition answers an empire's id, True (no winner) or None, not {}",
                                                              script::describe(*r, 60)));
                continue;
            }
            endGame(ctx, winner, v.label);
        }
}

void RulesEngine::checkObjectives(game::TurnContext& ctx) {
    game::GameState& s = ctx.state;
    if (s.scenario.mod.empty()) return;
    const auto mod = std::find_if(mods_.begin(), mods_.end(), [&](const mods::Package* p) { return p->id() == s.scenario.mod; });
    if (mod == mods_.end()) return;
    const size_t m = static_cast<size_t>(mod - mods_.begin());
    if (!scenario_) {
        auto loaded = loadScenario(**mod, s.scenario.name);
        if (!loaded) {
            log::warn("Rules mod {}: the scenario {} could not be read: {}", s.scenario.mod, s.scenario.name, loaded.error().front());
            scenario_ = std::make_shared<Scenario>();
            return;
        }
        scenario_ = std::make_shared<Scenario>(std::move(*loaded));
    }
    const learn::ClientFacts client;
    const learn::Tracker tracker;
    const learn::Mark mark;
    for (const ScenarioObjective& o : scenario_->objectives) {
        if (s.gameOver) return;
        if (o.byTurn && s.turn > *o.byTurn) continue;
        for (const game::Empire& e : s.empires) {
            if (s.gameOver) return;
            if (!e.alive || (o.empire && *o.empire != e.id.value)) continue;
            const std::string key = std::format("{}@{}", o.name, e.id.value);
            if (std::find(s.scenario.met.begin(), s.scenario.met.end(), key) != s.scenario.met.end()) continue;
            const learn::EvalContext ec{rules_, s, e.id, client, tracker, mark};
            if (!learn::holds(o.when, ec)) continue;
            s.scenario.met.push_back(key);
            ctx.log(e.id, game::LogCategory::Misc, "Objective met", o.text.empty() ? o.name : o.text);
            if (!o.action.empty() && registered(m, "objective", o.action)) {
                Value objective = Map(5)("name", Value(o.name))("text", Value(o.text))("action", Value(o.action))("empire", idOrNull(e.id))(
                                      "scenario", Value(s.scenario.name))
                                      .done();
                callMod(ctx, m, "objective", o.action, Map(1)("objective", std::move(objective)).done(), "objective:" + o.action);
                deliver(ctx);
            }
            if (o.victory) endGame(ctx, e.id, o.text.empty() ? o.name : o.text);
        }
    }
}

// ---- Mod orders -------------------------------------------------------------------------------------------

game::CommandResult RulesEngine::modCommand(game::TurnContext& ctx, EmpireId empire, const game::cmd::ModCommand& c) {
    using R = game::CommandResult;
    game::GameState& s = ctx.state;
    const auto mod = std::find_if(mods_.begin(), mods_.end(), [&](const mods::Package* p) { return p->id() == c.mod; });
    if (mod == mods_.end()) return R::fail(std::format("The game has no rules of the mod {}.", c.mod));
    const size_t m = static_cast<size_t>(mod - mods_.begin());
    const mods::ModOrderDecl* decl = (*mod)->manifest.rules.order(c.name);
    if (!decl) return R::fail(std::format("The mod {} has no order named {}.", c.mod, c.name));
    auto args = modOrderArguments(rules_, s, empire, *decl, c);
    if (!args) return R::fail(args.error());
    if (stateOf(s, m).failures >= kRulesFailuresPerTurn) return R::fail(std::format("The rules of the mod {} are off for the rest of the turn.", c.mod));
    if (host_.inScriptCall()) return R::fail("A mod's order cannot be given while a script is running (give it with the call's commands instead).");
    if (!registered(m, "order", c.name)) return R::fail(std::format("The mod {} gives its order {} no effect.", c.mod, c.name));
    if (!perspective_) perspective_.emplace(rules_, s, EmpireId{}, ViewOptions{true, true});
    Value order = Map(8)("mod", Value(c.mod))("name", Value(c.name))("empire", idOrNull(empire))("vehicle", id(c.vehicle))("fleet", id(c.fleet))(
                      "colony", id(c.planet))("target_empire", idOrNull(c.empire))("args", std::move(*args))
                      .done();
    if (registered(m, "order_check", c.name)) {
        const std::optional<Value> r = callMod(ctx, m, "order_check", c.name, Map(1)("order", order).done(), "order:" + c.name);
        if (!r) return R::fail(std::format("The order {} of the mod {} could not be checked.", c.name, c.mod));
        if (r->isString()) return R::fail(r->asString());
        if (r->isBool() && !r->asBool()) return R::fail(std::format("The order {} cannot be given now.", decl->label));
    }
    const std::optional<Value> r = callMod(ctx, m, "order", c.name, Map(1)("order", std::move(order)).done(), "order:" + c.name);
    deliver(ctx);
    if (!r) return R::fail(std::format("The order {} of the mod {} failed.", c.name, c.mod));
    return {};
}

// ---- Native functions (_opense4_rules) ---------------------------------------------------------------------

std::string RulesEngine::addNatives(script::Interpreter& interp) {
    for (const char* name : {"read", "effect", "random", "ability", "option", "query", "rules"}) {
        const std::string n(name);
        auto r = interp.addNativeFunction("_opense4_rules", n, [this, n](std::span<const Value> args) -> Value {
            if (args.size() > 1) throw script::NativeError("TypeError", std::format("_opense4_rules.{} takes one argument", n));
            return native(n, args.empty() ? Value::emptyMap() : args[0]);
        });
        if (!r) return "the rules' native functions could not be added: " + r.error().describe();
    }
    return {};
}

namespace {

const Value& argOf(const Value& arg, std::string_view key) {
    static const Value none;
    const Value* v = arg.find(key);
    return v ? *v : none;
}

int64_t intArg(const Value& arg, std::string_view key, std::string_view what) {
    const Value& v = argOf(arg, key);
    if (!v.isInt()) throw script::NativeError("TypeError", std::format("{}: '{}' should be a whole number", what, key));
    return v.asInt();
}

} // namespace

Value RulesEngine::native(std::string_view name, const Value& arg) {
    if (!active_) throw script::NativeError("RuntimeError", "the engine answers rules scripts only while it runs one of their functions");
    if (!arg.isMap()) throw script::NativeError("TypeError", std::format("_opense4_rules.{} takes a map", name));
    game::GameState& s = active_->ctx->state;
    if (name == "read") return read(arg);
    if (name == "rules") {
        if (rulesView_.isNull()) rulesView_ = buildRulesView(rules_);
        host_.chargeScript(static_cast<int64_t>(valueNodes(rulesView_)) * kReadNodeCost);
        return rulesView_;
    }
    if (name == "effect") {
        const Value& what = argOf(arg, "name");
        if (!what.isString()) throw script::NativeError("TypeError", "effect: 'name' should be the effect's name");
        const Value& args = argOf(arg, "args");
        return EffectRunner{*this}.run(what.asString(), args.isNull() ? Value::emptyMap() : args);
    }
    if (name == "random") {
        host_.chargeScript(kRandomCost);
        const Value& op = argOf(arg, "op");
        const std::string o = op.isString() ? op.asString() : std::string();
        if (o == "below") {
            const int64_t n = intArg(arg, "n", "random");
            if (n <= 0) throw script::NativeError("ValueError", "game.rng.below(n): n should be at least 1");
            return Value(static_cast<int64_t>(s.rng.below(static_cast<uint64_t>(n))));
        }
        if (o == "range") {
            const int64_t a = intArg(arg, "a", "random"), b = intArg(arg, "b", "random");
            if (b < a) throw script::NativeError("ValueError", std::format("game.rng.range({}, {}): the range is empty", a, b));
            return Value(s.rng.range(a, b));
        }
        if (o == "chance") {
            const int64_t p = intArg(arg, "percent", "random");
            return Value(s.rng.range(1, 100) <= p);
        }
        throw script::NativeError("ValueError", "random: 'op' should be \"below\", \"range\" or \"chance\"");
    }
    if (name == "ability") {
        host_.chargeScript(kAbilityCost);
        const Value& what = argOf(arg, "name");
        const Value& kind = argOf(arg, "kind");
        if (!what.isString() || !kind.isString()) throw script::NativeError("TypeError", "ability: 'name' and 'kind' should be text");
        const int64_t i = intArg(arg, "id", "ability");
        const auto v = abilityValue(rules_, s, kind.asString(), i, what.asString());
        if (!v) throw script::NativeError("ValueError", v.error());
        return *v ? Value(**v) : Value();
    }
    if (name == "option") {
        const Value& what = argOf(arg, "name");
        if (!what.isString()) throw script::NativeError("TypeError", "option: 'name' should be the option's name");
        const auto v = optionValue(active_->mod, what.asString());
        if (!v) throw script::NativeError("KeyError", std::format("the mod {} declares no option '{}'", mods_[active_->mod]->id(), what.asString()));
        return Value(*v);
    }
    if (name == "query") {
        const Value& q = argOf(arg, "name");
        if (!q.isString()) throw script::NativeError("TypeError", "query: 'name' should be the query's name");
        Queries queries(*perspective_);
        const Value& qargs = argOf(arg, "args");
        auto result = queries.call(q.asString(), qargs.isNull() ? Value::emptyMap() : qargs);
        if (!result) throw script::NativeError("ValueError", result.error().text());
        host_.chargeScript(kQueryCost + static_cast<int64_t>(valueNodes(*result)) * kNodeCost);
        return std::move(*result);
    }
    throw script::NativeError("ValueError", std::format("'{}' is not one of the rules' functions", name));
}

Value RulesEngine::read(const Value& arg) {
    const game::GameState& s = active_->ctx->state;
    const Value& what = argOf(arg, "what");
    if (!what.isString()) throw script::NativeError("TypeError", "read: 'what' should be what to read");
    const std::string& w = what.asString();
    Value out;
    if (w == "mod_data") {
        const Value& kind = argOf(arg, "kind");
        if (!kind.isString()) throw script::NativeError("TypeError", "read: 'kind' should be game, empire, colony or vehicle");
        const Value& i = argOf(arg, "id");
        out = modDataValue(s, mods_[active_->mod]->id(), kind.asString(), i.isInt() ? i.asInt() : -1);
    } else if (const Value& i = argOf(arg, "id"); i.isInt()) {
        out = buildViewRecord(*perspective_, w, i.asInt());
    } else if (w == "view") {
        out = buildView(*perspective_);
    } else {
        out = buildViewPart(*perspective_, w);
    }
    host_.chargeScript(static_cast<int64_t>(valueNodes(out)) * kReadNodeCost);
    return out;
}

} // namespace opense4::sdk::detail
