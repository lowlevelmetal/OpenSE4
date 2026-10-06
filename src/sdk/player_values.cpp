#include "sdk/player_values.hpp"

#include "sdk/names.hpp"
#include "sdk/value_io.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <vector>

namespace opense4::sdk::detail {

void hashValue(Hasher& h, const script::Value& v) {
    h.add(static_cast<uint8_t>(v.kind()));
    switch (v.kind()) {
        case script::Kind::Null: break;
        case script::Kind::Bool: h.add(v.asBool()); break;
        case script::Kind::Int: h.add(v.asInt()); break;
        case script::Kind::String: h.add(std::string_view(v.asString())); break;
        case script::Kind::List:
            h.addSize(v.asList().size());
            for (const script::Value& x : v.asList()) hashValue(h, x);
            break;
        case script::Kind::Map:
            h.addSize(v.asMap().size());
            for (const auto& [k, x] : v.asMap()) {
                h.add(std::string_view(k));
                hashValue(h, x);
            }
            break;
    }
}

size_t valueNodes(const script::Value& v) {
    size_t n = 1;
    if (v.isList())
        for (const script::Value& x : v.asList()) n += valueNodes(x);
    else if (v.isMap())
        for (const auto& [k, x] : v.asMap()) n += valueNodes(x);
    return n;
}

// ---- The battle (docs/sdk/ai-protocol.md §5) --------------------------------------------------------

namespace {

Value orNone(int i) { return Value(i); }

Value stacks(const std::vector<game::UnitStack>& list) {
    return listOf(list, [](const game::UnitStack& u) { return enc(u); });
}

Value weaponValue(size_t index, const game::combat::TacticalWeapon& w) {
    int ready = 0;
    for (size_t k = 0; k < w.reload.size() && static_cast<int>(k) < w.instances; ++k) ready += w.reload[k] == 0 ? 1 : 0;
    return Map(10)("index", num(index))("component", num(w.component))("kind", Value(enumName(w.kind)))("range", num(w.reach))(
               "reload", listOf(w.reload, [](int r) { return num(r); }))("reload_rate", num(w.reloadRate))("ready", num(ready))(
               "instances", num(w.instances))("together", num(w.together))("enabled", Value(w.enabled))
        .done();
}

Value pieceValue(size_t index, const game::combat::TacticalPiece& p) {
    using K = game::CombatPiece::Kind;
    const bool unit = p.kind == K::Vehicle || p.kind == K::UnitGroup;
    ValueList weapons;
    for (size_t k = 0; k < p.weapons.size(); ++k) weapons.push_back(weaponValue(k, p.weapons[k]));
    return Map(40)("id", num(index))("kind", Value(enumName(p.kind)))("owner", id(p.owner))("start_owner", id(p.startOwner))(
               "vehicle", id(p.vehicle))("planet", id(p.planet))("design", id(p.design))("name", Value(p.name))(
               "type", unit ? Value(enumName(p.type)) : Value())("position", Map(2)("x", num(p.x))("y", num(p.y)).done())(
               "size", num(p.size))("facing", num(p.facing))("alive", Value(p.alive))("mothballed", Value(p.mothballed))(
               "cloaked", Value(p.cloaked))("captured", Value(p.captured))("damage", num(p.damagePercent))(
               "hit_points", num(p.hitPoints))("full_hit_points", num(p.fullHitPoints))("shields", num(p.shields))(
               "shields_max", num(p.shieldsMax))("movement", num(p.movement))("speed", num(p.movementMax))("supply", num(p.supply))(
               "has_supply", Value(p.hasSupply))("count", num(p.count))("acted", Value(p.acted))("leader", orNone(p.leader))(
               "is_leader", Value(p.isLeader))("group", orNone(p.group))("formation", orNone(p.formation))("budget", num(p.budget))(
               "engaged", num(p.engaged))("launch_left", Map(3)("fighters", num(p.launchLeft[0]))("satellites", num(p.launchLeft[1]))(
                                                             "drones", num(p.launchLeft[2]))
                                                             .done())("seek_target", orNone(p.seekTarget))("launcher", orNone(p.launcher))(
               "carrier", orNone(p.carrier))("drone_target", orNone(p.droneTarget))("weapons", Value(std::move(weapons)))(
               "cargo", stacks(p.cargo))("troops", Value(p.troops))("boarding_attack", num(p.boardingAttack))
        .done();
}

} // namespace

Value battleValue(const game::Rules&, const game::GameState&, const game::BattleRound& b) {
    using K = game::CombatPiece::Kind;
    ValueList pieces, objects;
    for (size_t k = 0; k < b.pieces.size(); ++k) {
        const game::combat::TacticalPiece& p = b.pieces[k];
        pieces.push_back(pieceValue(k, p));
        if (p.kind == K::Planet || p.kind == K::Obstacle)
            objects.push_back(Map(6)("piece", num(k))("planet", id(p.planet))("population", num(p.population))("plague", num(p.plague))(
                                  "invader", id(p.invader))("landed", stacks(p.landed))
                                  .done());
    }
    ValueList order;
    for (game::EmpireId e : b.phaseOrder) order.push_back(id(e));
    return Map(6)("location", enc(b.where))("round", num(b.round))("rounds_max", num(b.roundsMax))("phase_order", Value(std::move(order)))(
               "pieces", Value(std::move(pieces)))("objects", Value(std::move(objects)))
        .done();
}

// ---- What changed in the view (§6, apply) ----------------------------------------------------------------

namespace {

// The key a list's records are known by: `id`, or a colony's `planet`.
const Value* recordKey(const Value& v) {
    if (!v.isMap()) return nullptr;
    if (const Value* k = v.find("id")) return k;
    return v.find("planet");
}

bool keyedList(const Value& list) {
    if (!list.isList()) return false;
    for (const Value& v : list.asList())
        if (const Value* k = recordKey(v); !k || !k->isInt()) return false;
    return true;
}

} // namespace

Value viewChanges(const Value& before, const Value& after) {
    ValueMap changed, removed;
    if (!before.isMap() || !after.isMap()) return Map(2)("changed", after)("removed", Value::emptyMap()).done();
    for (const auto& [key, now] : after.asMap()) {
        const Value* was = before.find(key);
        if (was && *was == now) continue;
        if (was && keyedList(*was) && keyedList(now)) {
            ValueList added;
            std::vector<int64_t> nowKeys;
            for (const Value& rec : now.asList()) {
                const int64_t k = recordKey(rec)->asInt();
                nowKeys.push_back(k);
                const auto old = std::find_if(was->asList().begin(), was->asList().end(), [&](const Value& o) { return recordKey(o)->asInt() == k; });
                if (old == was->asList().end() || !(*old == rec)) added.push_back(rec);
            }
            ValueList gone;
            for (const Value& rec : was->asList()) {
                const int64_t k = recordKey(rec)->asInt();
                if (std::find(nowKeys.begin(), nowKeys.end(), k) == nowKeys.end()) gone.push_back(Value(k));
            }
            if (!added.empty()) changed.emplace_back(key, Value(std::move(added)));
            if (!gone.empty()) removed.emplace_back(key, Value(std::move(gone)));
            continue;
        }
        changed.emplace_back(key, now);
    }
    return Map(2)("changed", Value(std::move(changed)))("removed", Value(std::move(removed))).done();
}

// ---- Responses (§4) ----------------------------------------------------------------------------------------

std::string responseProblem(const Value& r, bool planning) {
    if (!r.isMap()) return std::format("the response is a {}, not a map", script::kindName(r.kind()));
    for (const auto& [key, v] : r.asMap()) {
        if (key == "commands") {
            if (!v.isNull() && !v.isList()) return "commands: expected a list of commands";
            if (v.isList() && !v.asList().empty() && !planning) return "commands: only the planning calls (politics, orders, economy) give commands";
        } else if (key == "answer" || key == "memory") {
            // anything
        } else if (key == "notes") {
            if (v.isNull()) continue;
            if (!v.isList()) return "notes: expected a list of {object, text}";
            for (size_t i = 0; i < v.asList().size(); ++i) {
                const Value& n = v.asList()[i];
                const Value* text = n.find("text");
                const Value* object = n.find("object");
                const Value* kind = n.find("kind");
                if (!n.isMap() || !text || !text->isString()) return std::format("notes[{}]: expected {{object, text}} with a text", i);
                if (object && !object->isNull() && !object->isInt()) return std::format("notes[{}].object: expected an id or null", i);
                if (kind && !kind->isNull() && !kind->isString()) return std::format("notes[{}].kind: expected a text", i);
                for (const auto& [nk, nv] : n.asMap())
                    if (nk != "text" && nk != "object" && nk != "kind") return std::format("notes[{}].{}: unknown field", i, nk);
            }
        } else if (key == "log") {
            if (v.isNull()) continue;
            if (!v.isList()) return "log: expected a list of lines";
            for (size_t i = 0; i < v.asList().size(); ++i)
                if (!v.asList()[i].isString()) return std::format("log[{}]: expected a text", i);
        } else if (key == "error") {
            if (!v.isNull() && !v.isMap()) return "error: expected {type, message, traceback}";
        } else {
            return std::format("{}: unknown field (a response has commands, answer, memory, notes, log and error)", key);
        }
    }
    return {};
}

} // namespace opense4::sdk::detail
