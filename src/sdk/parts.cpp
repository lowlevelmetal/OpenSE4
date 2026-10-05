#include "sdk/parts.hpp"

#include "game/economy.hpp"
#include "game/research.hpp"

#include <algorithm>
#include <charconv>

namespace opense4::sdk::detail {

using game::AbilityKind;
using game::ParsedAbility;

// ---- Abilities ----------------------------------------------------------------------------------------

Value abilityNumberOrText(const std::string& raw) {
    size_t a = 0, b = raw.size();
    while (a < b && raw[a] == ' ') ++a;
    while (b > a && raw[b - 1] == ' ') --b;
    if (a == b) return Value(int64_t{0});
    int64_t n = 0;
    const char* first = raw.data() + a;
    const char* last = raw.data() + b;
    const auto [end, ec] = std::from_chars(first, last, n);
    if (ec == std::errc{} && end == last) return Value(n);
    return Value(raw);
}

Value abilityEntry(const ruleset::Ability& a) {
    return Map(4)("name", Value(a.type))("value1", abilityNumberOrText(a.value1))("value2", abilityNumberOrText(a.value2))(
               "description", Value(a.description))
        .done();
}

Value abilityEntries(std::span<const ruleset::Ability> list) { return listOf(list, abilityEntry); }

std::string_view abilityName(const ParsedAbility& a) {
    if (a.kind == AbilityKind::AITag || a.kind == AbilityKind::Unknown) return a.raw;
    return game::identifier(a.kind);
}

Value abilityTotals(std::span<const ParsedAbility> list) {
    std::vector<const ParsedAbility*> firsts;
    for (const ParsedAbility& a : list) {
        const std::string_view name = abilityName(a);
        if (std::none_of(firsts.begin(), firsts.end(), [&](const ParsedAbility* f) { return abilityName(*f) == name; }))
            firsts.push_back(&a);
    }
    ValueList out;
    out.reserve(firsts.size());
    for (const ParsedAbility* f : firsts) {
        const game::Aggregation mode = game::aggregationOf(f->kind);
        int64_t value = 0;
        if (f->kind == AbilityKind::AITag || f->kind == AbilityKind::Unknown) {
            // Several names share one kind: each name on its own, its values summed.
            for (const ParsedAbility& a : list)
                if (a.raw == f->raw) value += a.value1;
        } else {
            value = game::abilityValue(list, f->kind);
        }
        Value perType;
        if (mode == game::Aggregation::PerSightType) {
            Map m(game::kSightTypes);
            for (size_t t = 0; t < game::kSightTypes; ++t) {
                const auto type = static_cast<game::SightType>(t);
                m(enumName(type), Value(game::abilityPerSightType(list, f->kind, type)));
            }
            perType = m.done();
        }
        out.push_back(Map(4)("name", Value(abilityName(*f)))("aggregation", Value(enumName(mode)))("value", Value(value))(
                          "per_sight_type", std::move(perType))
                          .done());
    }
    return Value(std::move(out));
}

// ---- Designs ---------------------------------------------------------------------------------------------

Value designFigures(const game::Rules& r, const game::GameState& s, const game::DesignStats& st, uint32_t hull,
                    std::span<const game::DesignEntry> entries, const game::Design* maintenanceOf) {
    int64_t damage = 0;
    for (const game::DesignEntry& e : entries)
        if (e.component < r.data().components.size() && r.component(e.component).isWeapon()) damage += game::weaponDamageAtRange(r, e, 1);
    const auto [offense, defense] = game::designToHit(r, hull, entries);
    ValueList colonize;
    if (st.canColonizeRock) colonize.push_back(Value("Rock"));
    if (st.canColonizeIce) colonize.push_back(Value("Ice"));
    if (st.canColonizeGas) colonize.push_back(Value("Gas Giant"));
    return Map(24)("vehicle_type", enc(st.vehicleType))("tonnage_used", num(st.tonnageUsed))("tonnage_max", num(st.tonnageMax))(
               "cost", enc(st.cost))("maintenance", maintenanceOf ? enc(game::economy::designMaintenance(r, s, *maintenanceOf)) : Value())(
               "structure", num(st.structure))("movement", num(st.movement))("supply", num(st.supplyCapacity))("cargo", num(st.cargoCapacity))(
               "shields", num(st.shields))("phased_shields", num(st.phasedShields))("engines", num(st.engines))("weapons", num(st.weapons))(
               "max_weapon_range", num(st.maxWeaponRange))("weapon_damage", num(damage))("offense_bonus", num(offense))(
               "defense_bonus", num(defense))("space_yard", Value(st.spaceYard))("colonize", Value(std::move(colonize)))(
               "valid", Value(st.problems.empty()))("problems", enc(st.problems))
        .done();
}

// ---- Construction queues -----------------------------------------------------------------------------------

std::vector<QueueEstimate> estimateQueue(const game::Rules& r, const game::GameState& s, game::EmpireId owner,
                                         const game::cmd::QueueTarget& t, const game::ConstructionQueue& q, const game::Resources& rate) {
    std::vector<QueueEstimate> out;
    out.reserve(q.items.size());
    int total = 0;
    bool never = q.onHold;
    for (const game::QueueItem& item : q.items) {
        QueueEstimate est;
        est.cost = game::economy::itemCost(r, s, owner, t, item);
        est.remaining = game::max(est.cost - item.spent, game::Resources{});
        est.turns = game::economy::turnsToComplete(est.remaining, rate);
        if (est.turns >= 0) est.turns = std::max(1, est.turns);   // one item completes per turn at most
        if (est.turns < 0 || q.onHold) never = true;
        else total += est.turns;
        est.doneIn = never ? -1 : total;
        out.push_back(est);
    }
    return out;
}

Value queueValue(const game::Rules& r, const game::GameState& s, game::EmpireId owner, const game::cmd::QueueTarget& t,
                 const game::ConstructionQueue& q) {
    const game::Resources rate = game::economy::constructionRate(r, s, owner, t);
    const std::vector<QueueEstimate> est = estimateQueue(r, s, owner, t, q, rate);
    ValueList items;
    items.reserve(q.items.size());
    for (size_t i = 0; i < q.items.size(); ++i) {
        const game::QueueItem& item = q.items[i];
        items.push_back(Map(9)("kind", enc(item.kind))("design", id(item.design))("facility", num(item.facility))("count", num(item.count))(
                            "spent", enc(item.spent))("cost", enc(est[i].cost))("remaining", enc(est[i].remaining))("turns", num(est[i].turns))(
                            "done_in", num(est[i].doneIn))
                            .done());
    }
    return Map(8)("on_hold", Value(q.onHold))("repeat", Value(q.repeat))("emergency", Value(q.emergency))(
               "emergency_turns", num(q.emergencyTurns))("slow_turns", num(q.slowTurns))("auto_waypoint", num(q.autoWaypoint))(
               "rate", enc(rate))("items", Value(std::move(items)))
        .done();
}

// ---- Research ------------------------------------------------------------------------------------------------

std::vector<LevelEstimate> estimateResearch(const game::Rules& r, const game::GameState& s, const game::Empire& e, ruleset::TechAreaId area,
                                            int level) {
    std::vector<LevelEstimate> out;
    if (!area.valid() || area.index() >= r.data().techAreas.size()) return out;
    const int current = e.techLevel(area);
    const int top = std::min(level, r.tech(area).maxLevel);
    int64_t progress = 0;
    for (const game::ResearchProject& p : e.research)
        if (p.area == area) progress = p.progress;
    // Only what research::etaTurns reads: the levels, the queue, the pool and the income.
    game::Empire probe;
    probe.techLevels = e.techLevels;
    if (probe.techLevels.size() <= area.index()) probe.techLevels.resize(area.index() + 1, 0);
    probe.researchEvenly = true;
    probe.economy.research = e.economy.research;
    bool never = false;
    for (int l = current + 1; l <= top; ++l) {
        LevelEstimate est{l, game::research::levelCost(r, s, area, l), -1};
        if (!never) {
            const bool first = l == current + 1;
            probe.research = {{area, first ? progress : 0}};
            probe.researchPool = first ? e.researchPool : e.economy.research;
            est.turns = game::research::etaTurns(r, s, probe, 0);
            never = est.turns < 0;
        }
        probe.techLevels[area.index()] = l;
        out.push_back(est);
    }
    return out;
}

// ---- Reports -------------------------------------------------------------------------------------------------

Value economyValue(const game::EconomyReport& e) {
    const game::Resources net = e.colonies + e.remoteMining + e.otherIncome + e.trade + e.tariffsIn - e.tariffsOut - e.maintenance - e.construction;
    return Map(14)("colonies", enc(e.colonies))("remote_mining", enc(e.remoteMining))("other_income", enc(e.otherIncome))(
               "trade", enc(e.trade))("tariffs_in", enc(e.tariffsIn))("tariffs_out", enc(e.tariffsOut))("maintenance", enc(e.maintenance))(
               "construction", enc(e.construction))("lost_to_storage", enc(e.lostToStorage))("undelivered", enc(e.undelivered))(
               "storage_capacity", enc(e.storageCap))("research", num(e.research))("intelligence", num(e.intelligence))("net", enc(net))
        .done();
}

Value statsValue(const game::TurnStats& t) {
    return Map(12)("turn", num(t.turn))("score", num(t.score))("production", enc(t.production))("research", num(t.research))(
               "intelligence", num(t.intelligence))("tech_levels", num(t.techLevels))("systems", num(t.systems))("planets", num(t.planets))(
               "population", num(t.population))("units", num(t.units))("ships", num(t.ships))("bases", num(t.bases))
        .done();
}

Value raceValue(const game::Race& race) {
    Map ch(game::kCharacteristics);
    for (size_t i = 0; i < game::kCharacteristics; ++i)
        ch(enumName(static_cast<game::Characteristic>(i)), num(race.characteristics[i]));
    return Map(12)("name", Value(race.name))("style", Value(race.style))("biology", Value(race.biology))("society", Value(race.society))(
               "history", Value(race.history))("characteristics", ch.done())("traits", enc(race.traits))("culture", num(race.culture))(
               "happiness_model", num(race.happinessModel))("native_surface", Value(race.nativeSurface))("atmosphere", Value(race.atmosphere))(
               "demeanor", Value(race.demeanor))
        .done();
}

} // namespace opense4::sdk::detail
