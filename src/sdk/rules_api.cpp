// The rules tier's interface to the rest of the program (sdk/rules.hpp): mod
// orders, mod options and ability values.

#include "sdk/rules.hpp"

#include "datafile/datafile.hpp"
#include "game/abilities.hpp"
#include "script/json.hpp"
#include "sdk/players.hpp"
#include "sdk/rules_engine.hpp"

#include <algorithm>
#include <format>

namespace opense4::sdk {

using game::EmpireId;
using script::Value;
using script::ValueMap;

namespace {

bool livingEmpire(const game::GameState& s, int64_t id) {
    return id >= 0 && static_cast<uint64_t>(id) < s.empires.size() && s.empires[static_cast<size_t>(id)].alive;
}

// Whether an id names something of that kind in the game (an argument naming it).
bool exists(const game::GameState& s, std::string_view kind, int64_t id) {
    if (id < 0 || id > int64_t{UINT32_MAX}) return false;
    const auto i = static_cast<uint32_t>(id);
    if (kind == "empire") return i < s.empires.size();
    if (kind == "system") return i < s.galaxy.systems.size();
    if (kind == "object") return i < s.galaxy.objects.size();
    if (kind == "colony") return s.colony(game::ObjectId{i}) != nullptr;
    if (kind == "vehicle") {
        const game::Vehicle* v = s.vehicle(game::VehicleId{i});
        return v && v->count > 0;
    }
    if (kind == "fleet") return s.fleet(game::FleetId{i}) != nullptr;
    if (kind == "design") return i < s.designs.size();
    return false;
}

} // namespace

// ---- Mod orders ------------------------------------------------------------------------------------------

std::expected<Value, std::string> modOrderArguments(const game::Rules&, const game::GameState& s, EmpireId empire, const mods::ModOrderDecl& decl,
                                                    const game::cmd::ModCommand& c) {
    auto unexpected = [](std::string why) { return std::unexpected(std::move(why)); };
    const std::string& label = decl.label;
    // Its target: exactly the one its declaration names, the empire's own.
    const bool vehicle = c.vehicle.valid(), fleet = c.fleet.valid(), colony = c.planet.valid(), other = c.empire.valid();
    const int given = int{vehicle} + int{fleet} + int{colony} + int{other};
    if (decl.appliesTo == "self") {
        if (given != 0) return unexpected(std::format("The order {} is given to the empire itself, not to a vehicle, fleet, colony or empire.", label));
    } else if (given != 1) {
        return unexpected(std::format("The order {} is given to one {}.", label, decl.appliesTo));
    } else if (decl.appliesTo == "vehicle") {
        const game::Vehicle* v = vehicle ? s.vehicle(c.vehicle) : nullptr;
        if (!v || v->count <= 0 || v->owner != empire) return unexpected(std::format("The order {} needs one of your vehicles.", label));
    } else if (decl.appliesTo == "fleet") {
        const game::Fleet* f = fleet ? s.fleet(c.fleet) : nullptr;
        if (!f || f->owner != empire) return unexpected(std::format("The order {} needs one of your fleets.", label));
    } else if (decl.appliesTo == "colony") {
        const game::Colony* col = colony ? s.colony(c.planet) : nullptr;
        if (!col || col->owner != empire) return unexpected(std::format("The order {} needs one of your colonies.", label));
    } else if (decl.appliesTo == "empire") {
        if (!other || c.empire == empire || !livingEmpire(s, c.empire.value)) return unexpected(std::format("The order {} needs another empire.", label));
    }
    // Its arguments: a map of the declared ones.
    Value given_;
    if (c.args.empty()) {
        given_ = Value::emptyMap();
    } else {
        auto parsed = script::parseJson(c.args);
        if (!parsed || !parsed->isMap()) return unexpected(std::format("The order {}'s arguments are not a map.", label));
        given_ = std::move(*parsed);
    }
    for (const auto& [k, v] : given_.asMap())
        if (std::none_of(decl.args.begin(), decl.args.end(), [&](const mods::ModArgDecl& a) { return a.name == k; }))
            return unexpected(std::format("The order {} has no argument '{}'.", label, k));
    ValueMap out;
    for (const mods::ModArgDecl& a : decl.args) {
        const Value* v = given_.find(a.name);
        Value value;
        if (!v || v->isNull()) {
            if (a.hasDefault) value = a.defaultValue;
            else if (a.type == "int" || a.type == "bool" || a.type == "text")
                return unexpected(std::format("The order {} needs its argument '{}'.", label, a.name));
        } else if (a.type == "int") {
            if (!v->isInt()) return unexpected(std::format("{}: '{}' should be a whole number.", label, a.name));
            if ((a.min && v->asInt() < *a.min) || (a.max && v->asInt() > *a.max))
                return unexpected(std::format("{}: '{}' is {}, outside {} to {}.", label, a.name, v->asInt(), a.min ? std::to_string(*a.min) : "any",
                                              a.max ? std::to_string(*a.max) : "any"));
            value = *v;
        } else if (a.type == "bool") {
            if (!v->isBool()) return unexpected(std::format("{}: '{}' should be true or false.", label, a.name));
            value = *v;
        } else if (a.type == "text") {
            if (!v->isString()) return unexpected(std::format("{}: '{}' should be text.", label, a.name));
            if (v->asString().size() > 4096) return unexpected(std::format("{}: '{}' is too long.", label, a.name));
            value = *v;
        } else {
            if (!v->isInt() || !exists(s, a.type, v->asInt())) return unexpected(std::format("{}: '{}' names no {} of the game.", label, a.name, a.type));
            value = *v;
        }
        out.emplace_back(a.name, std::move(value));
    }
    return Value(std::move(out));
}

std::vector<ModOrderChoice> modOrders(const game::Rules& r, const game::GameState& s, EmpireId empire, const ModOrderTarget& target,
                                      std::span<const mods::Package> extra) {
    std::vector<ModOrderChoice> out;
    if (!empire.valid() || empire.index() >= s.empires.size() || !s.empire(empire).alive) return out;
    bool ok = false;
    const int64_t id = target.id;
    if (target.kind == "self") ok = true;
    else if (target.kind == "vehicle" && id >= 0) {
        const game::Vehicle* v = s.vehicle(game::VehicleId{static_cast<uint32_t>(id)});
        ok = v && v->count > 0 && v->owner == empire;
    } else if (target.kind == "fleet" && id >= 0) {
        const game::Fleet* f = s.fleet(game::FleetId{static_cast<uint32_t>(id)});
        ok = f && f->owner == empire;
    } else if (target.kind == "colony" && id >= 0) {
        const game::Colony* c = s.colony(game::ObjectId{static_cast<uint32_t>(id)});
        ok = c && c->owner == empire;
    } else if (target.kind == "empire") {
        ok = livingEmpire(s, id) && id != empire.value;
    }
    if (!ok) return out;
    for (const mods::Package* p : detail::rulesModsOf(r, s, extra))
        for (const mods::ModOrderDecl& o : p->manifest.rules.orders)
            if (o.appliesTo == target.kind) out.push_back({p->id(), o});
    return out;
}

game::cmd::ModCommand modOrderCommand(const ModOrderChoice& choice, const ModOrderTarget& target, const Value& args) {
    game::cmd::ModCommand c;
    c.mod = choice.mod;
    c.name = choice.order.name;
    if (target.id >= 0 && target.id <= int64_t{UINT32_MAX}) {
        const auto i = static_cast<uint32_t>(target.id);
        if (target.kind == "vehicle") c.vehicle = game::VehicleId{i};
        else if (target.kind == "fleet") c.fleet = game::FleetId{i};
        else if (target.kind == "colony") c.planet = game::ObjectId{i};
        else if (target.kind == "empire") c.empire = EmpireId{i};
    }
    if (args.isMap() && args.size() > 0)
        if (auto j = script::toJson(args)) c.args = std::move(*j);
    return c;
}

// ---- Mod options ------------------------------------------------------------------------------------------

std::string ModOptionChoice::key() const { return std::format("{}:{}", mod, option.name); }

std::vector<ModOptionChoice> modOptions(std::span<const mods::Package> packages) {
    std::vector<ModOptionChoice> out;
    for (const mods::Package& p : packages)
        if (p.affectsGame())
            for (const mods::ModOptionDecl& o : p.manifest.rules.options) out.push_back({p.id(), o});
    return out;
}

std::vector<ModOptionChoice> modOptions(const game::Rules& r) { return modOptions(gamePackages(r)); }

int64_t modOptionValue(const game::GameOptions& o, const ModOptionChoice& c) {
    for (const game::ModOption& x : o.modOptions)
        if (x.mod == c.mod && x.name == c.option.name) return std::clamp(x.value, c.option.min, c.option.max);
    return c.option.defaultValue;
}

std::string setModOption(game::GameOptions& o, std::span<const ModOptionChoice> choices, std::string_view key, int64_t value) {
    const auto it = std::find_if(choices.begin(), choices.end(), [&](const ModOptionChoice& c) { return c.key() == key; });
    if (it == choices.end()) return std::format("no mod of the game declares the option {}", key);
    if (value < it->option.min || value > it->option.max)
        return it->option.isSwitch ? std::format("the option {} is a switch: true or false", key)
                                   : std::format("the option {} takes {} to {}, not {}", key, it->option.min, it->option.max, value);
    auto set = std::find_if(o.modOptions.begin(), o.modOptions.end(), [&](const game::ModOption& x) { return x.mod == it->mod && x.name == it->option.name; });
    if (set == o.modOptions.end()) o.modOptions.push_back({it->mod, it->option.name, value});
    else set->value = value;
    return {};
}

// ---- Abilities ---------------------------------------------------------------------------------------------

std::expected<std::optional<int64_t>, std::string> abilityValue(const game::Rules& r, const game::GameState& s, std::string_view kind, int64_t id,
                                                                std::string_view name) {
    using List = std::vector<game::ParsedAbility>;
    if (id < 0 || id > int64_t{UINT32_MAX}) return std::unexpected(std::format("ability: no {} {}", kind, id));
    const auto i = static_cast<uint32_t>(id);
    List list;
    auto add = [&](std::span<const game::ParsedAbility> more) { list.insert(list.end(), more.begin(), more.end()); };
    auto addDesign = [&](const game::Design& d) {
        if (d.hull < r.data().vehicleSizes.size()) add(r.hullAbilities(d.hull));
        for (const game::DesignEntry& e : d.entries)
            if (e.component < r.data().components.size()) add(r.componentAbilities(e.component));
    };
    if (kind == "vehicle") {
        const game::Vehicle* v = s.vehicle(game::VehicleId{i});
        if (!v || v->count <= 0 || !v->design.valid() || v->design.index() >= s.designs.size()) return std::unexpected(std::format("ability: no vehicle {}", id));
        addDesign(s.design(v->design));
    } else if (kind == "design") {
        if (i >= s.designs.size()) return std::unexpected(std::format("ability: no design {}", id));
        addDesign(s.designs[i]);
    } else if (kind == "colony") {
        const game::Colony* c = s.colony(game::ObjectId{i});
        if (!c) return std::unexpected(std::format("ability: no colony on planet {}", id));
        for (uint32_t f : c->facilities)
            if (f < r.data().facilities.size()) add(r.facilityAbilities(f));
    } else if (kind == "system") {
        if (i >= s.galaxy.systems.size()) return std::unexpected(std::format("ability: no system {}", id));
        const game::StarSystem& sys = s.galaxy.systems[i];
        list = game::parseAbilities(sys.abilities);
        for (game::ObjectId o : sys.objects) {
            const List more = game::parseAbilities(s.galaxy.object(o).abilities);
            list.insert(list.end(), more.begin(), more.end());
        }
    } else if (kind == "component") {
        if (i >= r.data().components.size()) return std::unexpected(std::format("ability: no component {}", id));
        add(r.componentAbilities(i));
    } else if (kind == "facility") {
        if (i >= r.data().facilities.size()) return std::unexpected(std::format("ability: no facility {}", id));
        add(r.facilityAbilities(i));
    } else if (kind == "hull") {
        if (i >= r.data().vehicleSizes.size()) return std::unexpected(std::format("ability: no hull {}", id));
        add(r.hullAbilities(i));
    } else {
        return std::unexpected(std::format("ability: '{}' is not a vehicle, design, colony, system, component, facility or hull", kind));
    }
    if (r.data().findDeclaredAbility(name)) {
        const bool present = std::any_of(list.begin(), list.end(), [&](const game::ParsedAbility& a) {
            return a.kind == game::AbilityKind::Unknown && datafile::keysEqual(a.raw, name);
        });
        if (!present) return std::optional<int64_t>();
        return r.declaredAbility(list, name);
    }
    const auto k = game::parseAbilityKind(name);
    if (!k || *k == game::AbilityKind::Unknown) return std::unexpected(std::format("ability: '{}' is neither one of the game's abilities nor one a mod declares", name));
    if (!game::hasAbility(list, *k)) return std::optional<int64_t>();
    return std::optional<int64_t>(game::abilityValue(list, *k));
}

} // namespace opense4::sdk
