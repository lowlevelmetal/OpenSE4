// The effects API of rules scripts (docs/sdk/rules.md "Effects"): each effect
// is an engine function that checks what it is given and keeps the game
// valid. A refused effect raises an exception in the script and changes
// nothing; whole numbers only (floats cannot cross into the engine).

#include "sdk/rules_engine.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/generate.hpp"
#include "game/map_file.hpp"
#include "game/movement.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/sight.hpp"
#include "sdk/names.hpp"
#include "sdk/value_io.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>

namespace opense4::sdk::detail {

using game::EmpireId;
using script::NativeError;
using script::Value;
using script::ValueList;

namespace {

// What each effect charges, in bytecodes.
constexpr int64_t kEffectCost = 1'000;
// The largest amount an effect takes at once: far beyond any game's figures,
// and far from overflowing.
constexpr int64_t kMaxAmount = int64_t{1} << 50;

// One effect's arguments, read with the effect's name in every message.
class Args {
public:
    Args(std::string_view effect, const Value& v) : effect_(effect), v_(v) {
        if (!v.isMap()) throw NativeError("TypeError", std::format("fx.{}: the arguments should be a map", effect));
    }

    // Refuses keys the effect does not take.
    void only(std::initializer_list<std::string_view> keys) const {
        for (const auto& [k, x] : v_.asMap())
            if (std::find(keys.begin(), keys.end(), k) == keys.end())
                throw NativeError("TypeError", std::format("fx.{}: no argument '{}'", effect_, k));
    }
    bool has(std::string_view key) const {
        const Value* x = v_.find(key);
        return x && !x->isNull();
    }
    const Value& raw(std::string_view key) const {
        static const Value none;
        const Value* x = v_.find(key);
        return x ? *x : none;
    }
    [[noreturn]] void refuse(std::string_view key, std::string_view what) const {
        throw NativeError("ValueError", std::format("fx.{}: {}: {}", effect_, key, what));
    }
    [[noreturn]] void refuse(std::string_view what) const { throw NativeError("ValueError", std::format("fx.{}: {}", effect_, what)); }

    int64_t integer(std::string_view key, std::optional<int64_t> fallback = std::nullopt, int64_t lo = -kMaxAmount, int64_t hi = kMaxAmount) const {
        const Value& x = raw(key);
        if (x.isNull()) {
            if (fallback) return *fallback;
            refuse(key, "missing");
        }
        if (!x.isInt()) throw NativeError("TypeError", std::format("fx.{}: {}: expected a whole number", effect_, key));
        if (x.asInt() < lo || x.asInt() > hi) refuse(key, std::format("{} is outside {} to {}", x.asInt(), lo, hi));
        return x.asInt();
    }
    bool flag(std::string_view key, bool fallback) const {
        const Value& x = raw(key);
        if (x.isNull()) return fallback;
        if (!x.isBool()) throw NativeError("TypeError", std::format("fx.{}: {}: expected True or False", effect_, key));
        return x.asBool();
    }
    std::string text(std::string_view key, std::optional<std::string> fallback = std::nullopt, size_t limit = 4096) const {
        const Value& x = raw(key);
        if (x.isNull()) {
            if (fallback) return *fallback;
            refuse(key, "missing");
        }
        if (!x.isString()) throw NativeError("TypeError", std::format("fx.{}: {}: expected text", effect_, key));
        if (x.asString().size() > limit) refuse(key, std::format("longer than {} bytes", limit));
        return x.asString();
    }
    uint32_t index(std::string_view key) const { return static_cast<uint32_t>(integer(key, std::nullopt, 0, int64_t{UINT32_MAX})); }

    EmpireId empire(const game::GameState& s, std::string_view key, bool living = true) const {
        const uint32_t i = index(key);
        if (i >= s.empires.size()) refuse(key, std::format("no empire {}", i));
        if (living && !s.empires[i].alive) refuse(key, std::format("the {} is no more", s.empires[i].name));
        return EmpireId{i};
    }
    game::Colony& colony(game::GameState& s, std::string_view key) const {
        const uint32_t i = index(key);
        game::Colony* c = s.colony(game::ObjectId{i});
        if (!c) refuse(key, std::format("no colony on planet {}", i));
        return *c;
    }
    game::Vehicle& vehicle(game::GameState& s, std::string_view key) const {
        const uint32_t i = index(key);
        game::Vehicle* v = s.vehicle(game::VehicleId{i});
        if (!v || v->count <= 0) refuse(key, std::format("no vehicle {}", i));
        return *v;
    }
    game::SystemId system(const game::GameState& s, std::string_view key) const {
        const uint32_t i = index(key);
        if (i >= s.galaxy.systems.size()) refuse(key, std::format("no system {}", i));
        return game::SystemId{i};
    }
    game::Location location(const game::GameState& s, std::string_view key) const {
        const Value& x = raw(key);
        if (!x.isMap()) throw NativeError("TypeError", std::format("fx.{}: {}: expected a location {{system, x, y}}", effect_, key));
        const Args l(effect_, x);
        game::Location out;
        out.system = l.system(s, "system");
        out.sector.x = static_cast<int8_t>(l.integer("x", std::nullopt, 0, game::kSystemSize - 1));
        out.sector.y = static_cast<int8_t>(l.integer("y", std::nullopt, 0, game::kSystemSize - 1));
        return out;
    }
    template <NamedEnum E>
    E named(std::string_view key, std::optional<E> fallback = std::nullopt) const {
        const Value& x = raw(key);
        if (x.isNull() && fallback) return *fallback;
        const auto v = x.isString() ? parseEnum<E>(x.asString()) : std::nullopt;
        if (!v) refuse(key, std::format("expected a {} name", EnumNames<E>::kWhat));
        return *v;
    }

private:
    std::string_view effect_;
    const Value& v_;
};

std::string_view targetKindOf(const Value& target) {
    const Value* k = target.find("kind");
    return k && k->isString() ? std::string_view(k->asString()) : std::string_view();
}

} // namespace

Value EffectRunner::run(std::string_view name, const Value& raw) {
    RulesEngine& e = engine;
    if (!e.active_) throw NativeError("RuntimeError", "effects apply only while the engine runs a rules function");
    RulesEngine::Active& active = *e.active_;
    game::TurnContext& ctx = *active.ctx;
    game::GameState& s = ctx.state;
    const game::Rules& r = e.rules_;
    const std::string& mod = e.mods_[active.mod]->id();
    e.host_.chargeScript(kEffectCost);
    const Args a(name, raw);
    const bool inHook = active.call == "hook";
    auto onlyIn = [&](std::initializer_list<std::string_view> hooks, std::string_view where) {
        if (!inHook || std::find(hooks.begin(), hooks.end(), active.name) == hooks.end())
            throw NativeError("RuntimeError", std::format("fx.{} works only in {}", name, where));
    };

    // ---- Resources, research and population ----
    if (name == "add_resources") {
        a.only({"empire", "minerals", "organics", "radioactives"});
        game::Empire& emp = s.empire(a.empire(s, "empire"));
        const std::array<int64_t, 3> want{a.integer("minerals", 0), a.integer("organics", 0), a.integer("radioactives", 0)};
        game::Resources done;
        for (size_t i = 0; i < 3; ++i) {
            const int64_t before = emp.stockpile.v[i];
            emp.stockpile.v[i] = std::clamp<int64_t>(before + want[i], 0, kMaxAmount);
            done.v[i] = emp.stockpile.v[i] - before;
        }
        return enc(done);
    }
    if (name == "add_research" || name == "add_intelligence") {
        a.only({"empire", "points"});
        game::Empire& emp = s.empire(a.empire(s, "empire"));
        int64_t& pool = name == "add_research" ? emp.researchPool : emp.intelPool;
        pool = std::clamp<int64_t>(pool + a.integer("points"), 0, kMaxAmount);
        return Value(pool);
    }
    if (name == "grant_tech") {
        a.only({"empire", "area", "levels"});
        const EmpireId who = a.empire(s, "empire");
        const uint32_t area = a.index("area");
        if (area >= r.data().techAreas.size()) a.refuse("area", std::format("no tech area {}", area));
        const int levels = static_cast<int>(a.integer("levels", 1, 1, 100));
        const ruleset::TechAreaId id{area};
        const int top = r.tech(id).maxLevel;
        for (int i = 0; i < levels && (top <= 0 || s.empire(who).techLevel(id) < top); ++i)
            game::research::grantLevel(ctx, who, id, s.empire(who).techLevel(id) + 1, "a mod's rules");
        return Value(int64_t{s.empire(who).techLevel(id)});
    }
    if (name == "change_population") {
        a.only({"colony", "millions", "race"});
        game::Colony& c = a.colony(s, "colony");
        const int64_t change = a.integer("millions");
        const EmpireId race = a.has("race") ? a.empire(s, "race", false) : c.owner;
        auto it = std::find_if(c.population.begin(), c.population.end(), [&](const game::PopulationGroup& p) { return p.race == race; });
        if (change < 0) {
            const int64_t have = it == c.population.end() ? 0 : it->millions;
            const int64_t take = std::min(have, -change);
            if (take >= c.totalPopulation()) a.refuse("millions", "the colony would have nobody left");
            if (take == 0) return Value(int64_t{0});
            it->millions -= take;
            if (it->millions <= 0) c.population.erase(it);
            return Value(-take);
        }
        const int64_t room = std::max<int64_t>(0, game::maxPopulation(r, s, c) - c.totalPopulation());
        const int64_t add = std::min(change, room);
        if (add <= 0) return Value(int64_t{0});
        if (it == c.population.end()) c.population.push_back({race, add});
        else it->millions += add;
        return Value(add);
    }
    if (name == "change_happiness") {
        a.only({"colony", "change"});
        game::Colony& c = a.colony(s, "colony");
        // Positive: happier, which is less anger (spec 02 §4).
        c.anger = static_cast<int>(std::clamp<int64_t>(c.anger - a.integer("change", std::nullopt, -1000, 1000), 0, c.maxAnger()));
        return Value(int64_t{c.anger});
    }
    if (name == "set_colony_type") {
        a.only({"colony", "type"});
        game::Colony& c = a.colony(s, "colony");
        const std::string type = a.text("type");
        const std::vector<std::string>& types = s.empire(c.owner).colonyTypes;
        if (std::find(types.begin(), types.end(), type) == types.end()) a.refuse("type", std::format("'{}' is not one of the owner's colony types", type));
        c.colonyType = type;
        return Value();
    }
    if (name == "set_plague") {
        a.only({"colony", "level"});
        a.colony(s, "colony").plagueLevel = static_cast<int>(a.integer("level", std::nullopt, 0, 1000));
        return Value();
    }

    // ---- Damage, repair and supply ----
    if (name == "damage") {
        a.only({"vehicle", "amount", "cause"});
        const game::Vehicle& v = a.vehicle(s, "vehicle");
        const int amount = static_cast<int>(a.integer("amount", std::nullopt, 1, 1'000'000'000));
        const std::string cause = a.text("cause", std::string("Damaged."), 200);
        return Value(game::movement::detail::hurt(ctx, v.id, amount, cause));
    }
    if (name == "repair") {
        a.only({"vehicle", "components"});
        game::Vehicle& v = a.vehicle(s, "vehicle");
        int64_t left = a.integer("components", -1, -1, 1'000'000);
        int64_t fixed = 0;
        for (int& d : v.damage) {
            if (left == 0) break;
            if (d <= 0) continue;
            d = 0;
            ++fixed;
            if (left > 0) --left;
        }
        return Value(fixed);
    }
    if (name == "change_supply") {
        a.only({"vehicle", "amount"});
        game::Vehicle& v = a.vehicle(s, "vehicle");
        const int64_t cap = std::max<int64_t>(game::vehicleSupplyCapacity(r, s, v), 0);
        v.supply = std::clamp<int64_t>(v.supply + a.integer("amount"), 0, cap);
        return Value(v.supply);
    }

    // ---- Vehicles and facilities ----
    if (name == "create_vehicle") {
        a.only({"empire", "design", "location", "count"});
        const EmpireId owner = a.empire(s, "empire");
        const uint32_t d = a.index("design");
        if (d >= s.designs.size() || s.designs[d].owner != owner) a.refuse("design", std::format("no design {} of the {}", d, s.empire(owner).name));
        const game::Design& design = s.designs[d];
        if (design.hull >= r.data().vehicleSizes.size()) a.refuse("design", "its hull is not in the data set");
        const game::Location where = a.location(s, "location");
        const ruleset::VehicleType type = r.hull(design.hull).type;
        const bool unit = game::isUnitType(type);
        const int count = static_cast<int>(a.integer("count", 1, 1, unit ? 100'000 : 1));
        if (!unit && game::shipCount(r, s, owner) >= s.options.maxShipsPerPlayer) a.refuse("the empire has reached its limit on ships");
        game::Vehicle& v = game::movement::spawnVehicle(r, s, owner, design.id, where);
        if (unit && count > 1) {
            v.count = count;
            s.design(design.id).built += count - 1;
        }
        return Value(static_cast<int64_t>(v.id.value));
    }
    if (name == "remove_vehicle") {
        a.only({"vehicle"});
        game::Vehicle& v = a.vehicle(s, "vehicle");
        // Gone, not destroyed: no loss is counted; the engine removes it at its next tidying.
        v.count = 0;
        v.mixed.clear();
        return Value();
    }
    if (name == "add_facility") {
        a.only({"colony", "facility"});
        game::Colony& c = a.colony(s, "colony");
        const uint32_t f = a.index("facility");
        if (f >= r.data().facilities.size()) a.refuse("facility", std::format("no facility {}", f));
        if (static_cast<int64_t>(c.facilities.size()) >= game::facilitySlots(r, s, c)) a.refuse("the colony has no free facility slot");
        c.facilities.push_back(f);
        game::sight::recalculateColony(r, c);
        return Value(static_cast<int64_t>(c.facilities.size() - 1));
    }
    if (name == "remove_facility") {
        a.only({"colony", "index"});
        game::Colony& c = a.colony(s, "colony");
        const uint32_t i = a.index("index");
        if (i >= c.facilities.size()) a.refuse("index", std::format("the colony has {} facilities", c.facilities.size()));
        const uint32_t f = c.facilities[i];
        c.facilities.erase(c.facilities.begin() + static_cast<std::ptrdiff_t>(i));
        game::sight::recalculateColony(r, c);
        return Value(static_cast<int64_t>(f));
    }

    // ---- Treaties, the log and events ----
    if (name == "set_treaty") {
        a.only({"empire", "other", "treaty"});
        const EmpireId x = a.empire(s, "empire"), y = a.empire(s, "other");
        if (x == y) a.refuse("other", "an empire has no treaty with itself");
        if (!s.empire(x).relation(y).contact) a.refuse("the two empires have not met");
        game::diplomacy::setTreaty(ctx, x, y, a.named<game::Treaty>("treaty"));
        return Value();
    }
    if (name == "log") {
        a.only({"empire", "text", "title", "category", "location"});
        const EmpireId who = a.empire(s, "empire", false);
        std::optional<game::Location> where;
        if (a.has("location")) where = a.location(s, "location");
        const std::string title = a.text("title", std::string(), 200);
        ctx.log(who, a.named<game::LogCategory>("category", game::LogCategory::Misc), title.empty() ? std::string("News") : title, a.text("text"),
                where);
        return Value();
    }
    if (name == "fire_event") {
        a.only({"name", "target"});
        const std::string event = a.text("name", std::nullopt, 64);
        const mods::ModEventDecl* decl = e.mods_[active.mod]->manifest.rules.event(event);
        if (!decl) a.refuse("name", std::format("the mod {} declares no event '{}'", mod, event));
        const Value& target = a.raw("target");
        if (!target.isNull() && (!target.isMap() || targetKindOf(target).empty())) a.refuse("target", "expected {kind, id} or None");
        // It fires at the next safe point, after this function.
        e.pending_.push_back(RulesEngine::Pending{game::Hook::EventFired, Value(), RulesEngine::Fired{active.mod, event, target}});
        return Value();
    }

    // ---- The galaxy ----
    if (name == "set_planet") {
        a.only({"planet", "minerals", "organics", "radioactives", "conditions", "size", "surface", "atmosphere"});
        const uint32_t o = a.index("planet");
        if (o >= s.galaxy.objects.size()) a.refuse("planet", std::format("no object {}", o));
        game::SpaceObject& obj = s.galaxy.objects[o];
        if (obj.kind != game::ObjectKind::Planet && obj.kind != game::ObjectKind::Asteroids) a.refuse("planet", "not a planet or an asteroid field");
        static constexpr std::array<std::string_view, 3> kValues{"minerals", "organics", "radioactives"};
        for (size_t i = 0; i < 3; ++i)
            if (a.has(kValues[i])) obj.value[i] = static_cast<int>(a.integer(kValues[i], std::nullopt, 0, 1'000'000'000));
        if (a.has("conditions")) obj.conditions = game::Conditions::hundredths(a.integer("conditions", std::nullopt, 0, 150));
        auto known = [&](std::string_view key, auto&& field) {
            if (!a.has(key)) return;
            const std::string v = a.text(key, std::nullopt, 64);
            const bool ok = std::any_of(r.data().planetSizes.begin(), r.data().planetSizes.end(), [&](const ruleset::PlanetSize& p) {
                return datafile::keysEqual(field(p), v);
            });
            if (!ok) a.refuse(key, std::format("'{}' is not one the data set knows", v));
        };
        known("size", [](const ruleset::PlanetSize& p) -> const std::string& { return p.name; });
        if (a.has("size")) obj.size = a.text("size");
        if (a.has("surface")) obj.surface = a.text("surface", std::nullopt, 64);
        if (a.has("atmosphere")) obj.atmosphere = a.text("atmosphere", std::nullopt, 64);
        return Value();
    }
    if (name == "rename") {
        a.only({"kind", "id", "name"});
        const std::string kind = a.text("kind", std::nullopt, 16);
        const std::string text = a.text("name", std::nullopt, 64);
        if (text.empty()) a.refuse("name", "empty");
        if (kind == "system") s.galaxy.system(a.system(s, "id")).name = text;
        else if (kind == "object") {
            const uint32_t o = a.index("id");
            if (o >= s.galaxy.objects.size()) a.refuse("id", std::format("no object {}", o));
            s.galaxy.objects[o].name = text;
        } else if (kind == "vehicle") a.vehicle(s, "id").name = text;
        else if (kind == "fleet") {
            game::Fleet* f = s.fleet(game::FleetId{a.index("id")});
            if (!f) a.refuse("id", "no such fleet");
            f->name = text;
        } else a.refuse("kind", "a system, object, vehicle or fleet");
        return Value();
    }
    if (name == "link_systems") {
        a.only({"a", "b"});
        const game::SystemId x = a.system(s, "a"), y = a.system(s, "b");
        if (x == y) a.refuse("b", "a system is not linked to itself");
        for (game::ObjectId o : s.galaxy.system(x).objects)
            if (const game::SpaceObject& w = s.galaxy.object(o);
                w.kind == game::ObjectKind::WarpPoint && w.destination.valid() && s.galaxy.object(w.destination).system == y)
                a.refuse("the two systems are linked already");
        const std::vector<uint32_t> types = game::naturalSectorTypes(r.data(), game::ObjectKind::WarpPoint);
        // An empty sector on each system's edge, drawn from the game's generator.
        auto place = [&](game::SystemId sys) -> std::optional<game::Sector> {
            std::vector<game::Sector> edge;
            for (const game::Sector& c : game::emptySectors(s.galaxy, sys))
                if (c.x == 0 || c.y == 0 || c.x == game::kSystemSize - 1 || c.y == game::kSystemSize - 1) edge.push_back(c);
            if (edge.empty()) return std::nullopt;
            return edge[s.rng.index(edge.size())];
        };
        const auto at = place(x);
        const auto bt = place(y);
        if (!at || !bt) a.refuse("no empty sector on a system's edge for the warp point");
        auto make = [&](game::SystemId sys, game::Sector sector) {
            game::SpaceObject w;
            w.kind = game::ObjectKind::WarpPoint;
            w.name = "Warp Point";
            w.sector = sector;
            if (!types.empty()) game::applySectorType(r.data(), w, types.front());
            return s.addObject(std::move(w), sys);
        };
        const game::ObjectId wa = make(x, *at);
        const game::ObjectId wb = make(y, *bt);
        s.galaxy.object(wa).destination = wb;
        s.galaxy.object(wb).destination = wa;
        return Value(ValueList{Value(static_cast<int64_t>(wa.value)), Value(static_cast<int64_t>(wb.value))});
    }
    if (name == "replace_galaxy") {
        onlyIn({"generate_galaxy"}, "generate_galaxy");
        a.only({"map"});
        auto map = game::mapFromText(r.data(), a.text("map", std::nullopt, size_t{64} << 20));
        if (!map) a.refuse("map", map.error());
        if (map->map.galaxy.systems.empty()) a.refuse("map", "the map has no systems");
        s.galaxy = std::move(map->map.galaxy);
        s.colonies.assign(s.galaxy.objects.size(), std::nullopt);
        ValueList warnings;
        for (const std::string& w : map->warnings) warnings.push_back(Value(w));
        return Value(std::move(warnings));
    }

    // ---- The game ----
    if (name == "set_option") {
        onlyIn({"new_game"}, "new_game");
        a.only({"name", "value"});
        const std::string option = a.text("name", std::nullopt, 64);
        const Value& v = a.raw("value");
        const int64_t value = v.isBool() ? (v.asBool() ? 1 : 0) : a.integer("value");
        if (const mods::ModOptionDecl* d = e.mods_[active.mod]->manifest.rules.option(option)) {
            if (value < d->min || value > d->max) a.refuse("value", std::format("the option {} takes {} to {}", option, d->min, d->max));
            auto it = std::find_if(s.options.modOptions.begin(), s.options.modOptions.end(),
                                   [&](const game::ModOption& o) { return o.mod == mod && o.name == option; });
            if (it == s.options.modOptions.end()) s.options.modOptions.push_back({mod, option, value});
            else it->value = value;
            return Value();
        }
        // The classic options a mod may change before the quadrant is made.
        game::GameOptions& o = s.options;
        auto ranged = [&](int& field, int lo, int hi) {
            if (value < lo || value > hi) a.refuse("value", std::format("{} takes {} to {}", option, lo, hi));
            field = static_cast<int>(value);
        };
        auto flag = [&](bool& field) {
            if (value != 0 && value != 1) a.refuse("value", std::format("{} is True or False", option));
            field = value != 0;
        };
        if (option == "event_frequency") ranged(o.eventFrequency, 0, 3);
        else if (option == "max_event_severity") ranged(o.maxEventSeverity, 0, 3);
        else if (option == "tech_cost") ranged(o.techCost, 0, 2);
        else if (option == "start_tech_level") ranged(o.startTechLevel, 0, 2);
        else if (option == "quadrant_size") ranged(o.quadrantSize, 0, 2);
        else if (option == "system_count") ranged(o.systemCount, 0, 1000);
        else if (option == "no_ruins") flag(o.noRuins);
        else if (option == "finite_resources") flag(o.finiteResources);
        else if (option == "all_systems_seen") flag(o.allSystemsSeen);
        else if (option == "allow_intel") flag(o.allowIntel);
        else a.refuse("name", std::format("'{}' is neither one of the mod's options nor a classic option a mod may set", option));
        return Value();
    }
    if (name == "victory") {
        if (!((inHook && (active.name == "check_victory" || active.name == "turn_end")) || active.call == "objective" || active.call == "victory"))
            throw NativeError("RuntimeError", "fx.victory works only in check_victory, turn_end, a victory condition or an objective's action");
        a.only({"empire", "reason"});
        const EmpireId winner = a.has("empire") ? a.empire(s, "empire") : EmpireId{};
        e.endGame(ctx, winner, a.text("reason", std::string("A mod's rules ended the game."), 200));
        return Value();
    }
    if (name == "set_mod_data") {
        a.only({"target", "value"});
        const Value& target = a.raw("target");
        std::string kind = "game";
        int64_t id = -1;
        if (!target.isNull()) {
            if (!target.isMap() || targetKindOf(target).empty()) a.refuse("target", "expected {kind, id} or None (the game)");
            kind = std::string(targetKindOf(target));
            const Value* i = target.find("id");
            id = i && i->isInt() ? i->asInt() : -1;
        }
        if (std::string why = setModData(s, mod, kind, id, a.raw("value")); !why.empty()) a.refuse(why);
        return Value();
    }
    throw NativeError("ValueError", std::format("fx has no effect '{}'", name));
}

} // namespace opense4::sdk::detail
