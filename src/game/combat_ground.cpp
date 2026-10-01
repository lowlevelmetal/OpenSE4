// Ground combat and planet capture (docs/spec/04 §13).
//
// Troops landed on a colony are kept in Colony::landedTroops and fight for
// Colony::invader, the empire that owned the ship that dropped them; the
// units in the colony's cargo always serve the colony's owner (see
// combat.hpp). The same fight runs at once when troops drop during a space
// battle (combat_space.cpp), and in the colony owner's end-of-turn
// processing (spec 05 §8, its ground-combat step) for each of its colonies
// where landed troops still fight: up to `Number Of Ground Combat Turns`
// rounds of the invaders against the defender's troops, militia and other
// stored units. When the defending troops and militia are gone the planet
// changes hands with its facilities, stored units and population, and the
// surviving invaders join its cargo.

#include "game/combat.hpp"

#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"
#include "game/turn_internal.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <climits>
#include <format>
#include <map>
#include <memory>
#include <optional>

namespace opense4::game::combat {

namespace detail {

namespace {

using ruleset::VehicleType;

// One stack of a side, in the order damage reaches it.
struct Stack {
    std::vector<UnitStack>* list = nullptr;   // where the units are (null: militia)
    size_t index = 0;
    DesignId design;
    int count = 0;
    int start = 0;
    int64_t hitPoints = 1;     // per unit
    int attack = 0;            // per unit that hits (0: does not attack)
    bool troop = false;        // troops and militia: they hold the planet and roll to hit
};

// Hit points of one unit: structure plus shields for troops, fighters and
// weapon platforms, structure alone for other stored units (confirmed: binary).
int64_t groundHitPoints(const Rules& r, const Design& d) {
    const VehicleType t = r.hull(d.hull).type;
    int64_t shields = 0;
    if (t == VehicleType::Troop || t == VehicleType::Fighter || t == VehicleType::WeaponPlatform) {
        shields = hullSum(r, d, AbilityKind::ShieldGeneration) + hullSum(r, d, AbilityKind::PhasedShieldGeneration);
        for (const DesignEntry& e : d.entries)
            shields += mountedShield(r, e, AbilityKind::ShieldGeneration) + mountedShield(r, e, AbilityKind::PhasedShieldGeneration);
    }
    return std::max<int64_t>(1, designStructure(r, d) + shields);
}

// The sum over a troop's weapons of each weapon's largest table entry (confirmed: binary).
int troopAttack(const Rules& r, const Design& d) {
    int64_t total = 0;
    for (const DesignEntry& e : d.entries) {
        const ruleset::Component& c = r.component(e.component);
        if (c.isWeapon() && c.weapon.kind != ruleset::WeaponKind::Warhead) total += weaponLargestDamage(r, e);
    }
    return static_cast<int>(std::min<int64_t>(total, INT32_MAX));
}

// (Plus − Minus) ÷ 2, truncated, over the side's living troop designs only:
// the single best hull value among them plus the best component of each
// family, the families added up (confirmed: binary). Recomputed every round.
int sideModifier(const Rules& r, const GameState& s, const std::vector<Stack>& side, AbilityKind plus, AbilityKind minus) {
    std::map<int, int64_t> bestPlus, bestMinus;
    auto note = [&](std::map<int, int64_t>& best, uint32_t component, AbilityKind k) {
        const auto ab = r.componentAbilities(component);
        if (!hasAbility(ab, k)) return;
        const int family = r.component(component).family;
        const int64_t value = bestValue1(ab, k);
        auto it = best.find(family);
        if (it == best.end()) best.emplace(family, value);
        else it->second = std::max(it->second, value);
    };
    std::optional<int64_t> hullPlus, hullMinus;
    for (const Stack& st : side) {
        if (!st.troop || !st.list || st.count <= 0) continue;
        const Design& d = s.design(st.design);
        hullPlus = std::max(hullPlus.value_or(INT64_MIN), hullSum(r, d, plus));
        hullMinus = std::max(hullMinus.value_or(INT64_MIN), hullSum(r, d, minus));
        for (const DesignEntry& e : d.entries) {
            note(bestPlus, e.component, plus);
            note(bestMinus, e.component, minus);
        }
    }
    int64_t total = hullPlus.value_or(0) - hullMinus.value_or(0);
    for (const auto& [f, v] : bestPlus) total += v;
    for (const auto& [f, v] : bestMinus) total -= v;
    return static_cast<int>(total / 2);
}

int holders(const std::vector<Stack>& side) {
    int n = 0;
    for (const Stack& st : side)
        if (st.troop) n += st.count;
    return n;
}

int64_t hits(const std::vector<Stack>& side, int chance, Rng& rng) {
    int64_t total = 0;
    for (const Stack& st : side) {
        if (st.attack <= 0) continue;
        for (int n = 0; n < st.count; ++n)
            if (rng.rangeInt(1, 100) <= chance) total += st.attack;   // no clamp (confirmed: binary)
    }
    return total;
}

// Whole units die while the damage covers their hit points; the rest moves on
// to the next stack. Each loss credits a random stack of the killing side with
// the dead units' hull tonnage, unless that stack is militia (confirmed:
// binary). Returns what is left over.
int64_t applyDamage(const Rules& r, GameState& s, std::vector<Stack>& side, const std::vector<Stack>& killers, int64_t damage, Rng& rng) {
    for (Stack& st : side) {
        if (damage <= 0) break;
        if (st.count <= 0) continue;
        const int64_t killed = std::min<int64_t>(st.count, damage / st.hitPoints);
        st.count -= static_cast<int>(killed);
        damage -= killed * st.hitPoints;
        if (killed <= 0 || !st.list || killers.empty()) continue;
        const Stack& credit = killers[rng.below(killers.size())];
        if (credit.list && credit.design.valid())
            s.design(credit.design).enemyTonnageDestroyed += killed * designTonnage(r, s.design(st.design));
    }
    return std::max<int64_t>(0, damage);
}

std::vector<Stack> living(const std::vector<Stack>& side) {
    std::vector<Stack> out;
    for (const Stack& st : side)
        if (st.count > 0) out.push_back(st);
    return out;
}

} // namespace

void logGroundCombat(TurnContext& ctx, ObjectId planet, EmpireId attacker, EmpireId defender, const GroundOutcome& o) {
    const GameState& s = ctx.state;
    const SpaceObject& obj = s.galaxy.object(planet);
    const std::string system = obj.system.valid() ? s.galaxy.system(obj.system).name : std::string("?");
    const Location where = locationOf(s.galaxy, planet);
    const char* outcome = o.captured ? "The planet was taken." : o.attackersGone ? "The invaders were defeated." : "It is still a stalemate.";
    auto name = [&](EmpireId e) { return e.valid() && e.index() < s.empires.size() ? s.empire(e).name : std::string("unknown"); };
    const std::string losses = std::format("{} rounds: the invaders lost {} of {} troops, the defenders {} units and {} militia.", o.rounds,
                                           o.attackersLost, o.attackersAtStart, o.defendersLost, o.militiaLost);
    const std::string title = std::format("Ground combat at {}", system);
    if (attacker.valid() && attacker.index() < s.empires.size() && s.empire(attacker).alive)
        ctx.log(attacker, LogCategory::Combat, title, std::format("Our troops fought the {} on {}. {} {}", name(defender), obj.name, losses, outcome),
                where);
    if (defender.valid() && defender.index() < s.empires.size() && s.empire(defender).alive)
        ctx.log(defender, LogCategory::Combat, title,
                std::format("Troops of the {} fought our defenders on {}. {} {}", name(attacker), obj.name, losses, outcome), where);
}

int groundModifier(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return (c ? c->groundCombat : 0) + e.race.characteristic(Characteristic::PhysicalStrength) - 100;
}

GroundOutcome fightGround(const Rules& r, GameState& s, const CombatSettings& cs, const GroundFight& f, Rng& rng) {
    GroundOutcome out;
    auto stackOf = [&](std::vector<UnitStack>& list, size_t k) {
        const UnitStack& u = list[k];
        const Design& d = s.design(u.design);
        Stack st;
        st.list = &list;
        st.index = k;
        st.design = u.design;
        st.count = st.start = std::max(0, u.count);
        st.hitPoints = groundHitPoints(r, d);
        st.troop = isTroopDesign(r, s, u.design);
        st.attack = st.troop ? troopAttack(r, d) : 0;   // stored units other than troops never attack
        return st;
    };
    // The invaders: armed troops first, then the rest, each in landing order.
    std::vector<Stack> att;
    std::vector<Stack> attRest;
    for (size_t k = 0; k < f.invaders->size(); ++k) {
        if ((*f.invaders)[k].count <= 0 || !isTroopDesign(r, s, (*f.invaders)[k].design)) continue;
        Stack st = stackOf(*f.invaders, k);
        (st.attack > 0 ? att : attRest).push_back(st);
    }
    att.insert(att.end(), attRest.begin(), attRest.end());
    if (holders(att) == 0) return out;
    out.attackersAtStart = holders(att);

    // Militia: the colony's pool, raised per population group (confirmed: binary).
    if (*f.militia < 0) *f.militia = militiaCount(cs, *f.population);
    Stack militia;
    militia.troop = true;
    militia.attack = cs.militiaAttack;
    militia.hitPoints = std::max(1, cs.militiaHitPoints);
    {
        int left = std::max(0, *f.militia);
        for (const PopulationGroup& g : *f.population) {
            const int take = std::min(militiaCount(cs, g.millions), left);
            militia.count += take;
            left -= take;
        }
        militia.start = militia.count;
    }
    // The defenders in the order damage reaches them: the units that attack
    // (armed troops in cargo order, then the militia, added at the end of the
    // cargo list), then everything left in cargo order (confirmed: binary).
    std::vector<Stack> def, defRest;
    for (size_t k = 0; k < f.cargo->units.size(); ++k) {
        if (f.cargo->units[k].count <= 0) continue;
        Stack st = stackOf(f.cargo->units, k);
        (st.attack > 0 ? def : defRest).push_back(st);
    }
    def.push_back(militia);
    def.insert(def.end(), defRest.begin(), defRest.end());

    // The window's record: both sides as the fight begins (stacks with units, in
    // their lists' order), then the counts after every round (spec 06 §1.10.6).
    GroundCombat* rec = f.record;
    std::vector<int> attPos(f.invaders->size(), -1), defPos(f.cargo->units.size(), -1);
    if (rec) {
        rec->attacker = f.attacker;
        rec->defender = f.defender;
        rec->attackers.clear();
        rec->defenders.clear();
        rec->perRound.clear();
        for (size_t k = 0; k < f.invaders->size(); ++k)
            if ((*f.invaders)[k].count > 0) {
                attPos[k] = static_cast<int>(rec->attackers.size());
                rec->attackers.push_back((*f.invaders)[k]);
            }
        for (size_t k = 0; k < f.cargo->units.size(); ++k)
            if (f.cargo->units[k].count > 0) {
                defPos[k] = static_cast<int>(rec->defenders.size());
                rec->defenders.push_back(f.cargo->units[k]);
            }
        rec->militia = militia.start;
    }
    auto counts = [&] {
        GroundRound round;
        for (const UnitStack& u : rec->attackers) round.attackers.push_back(u.count);
        for (const UnitStack& u : rec->defenders) round.defenders.push_back(u.count);
        auto note = [&](const Stack& st) {
            if (!st.list) {
                round.militia = st.count;
            } else if (st.list == f.invaders) {
                if (attPos[st.index] >= 0) round.attackers[static_cast<size_t>(attPos[st.index])] = st.count;
            } else if (st.list == &f.cargo->units) {
                if (defPos[st.index] >= 0) round.defenders[static_cast<size_t>(defPos[st.index])] = st.count;
            }
        };
        for (const Stack& st : att) note(st);
        for (const Stack& st : def) note(st);
        return round;
    };

    const int attRacial = groundModifier(r, s.empire(f.attacker));
    const int defRacial = f.defender.valid() ? groundModifier(r, s.empire(f.defender)) : 0;
    const int percent = cs.groundDamagePercent;

    int64_t carryAtt = 0, carryDef = 0;
    for (int round = 1; round <= cs.groundTurns; ++round) {
        if (holders(att) == 0 || holders(def) == 0) break;
        ++out.rounds;
        const int attOffense = sideModifier(r, s, att, AbilityKind::CombatToHitOffensePlus, AbilityKind::CombatToHitOffenseMinus);
        const int attDefense = sideModifier(r, s, att, AbilityKind::CombatToHitDefensePlus, AbilityKind::CombatToHitDefenseMinus);
        const int defOffense = sideModifier(r, s, def, AbilityKind::CombatToHitOffensePlus, AbilityKind::CombatToHitOffenseMinus);
        const int defDefense = sideModifier(r, s, def, AbilityKind::CombatToHitDefensePlus, AbilityKind::CombatToHitDefenseMinus);
        const int64_t attHits = hits(att, attOffense + 50 - defDefense, rng);
        const int64_t defHits = hits(def, defOffense + 50 - attDefense, rng);
        // The totals with last round's carry, times the ground percentage
        // (truncated), then the modifiers, chained and rounded: the defender's
        // planet first, then each side's race (confirmed: binary).
        const int64_t attBase = xmath::pctTrunc(attHits + carryAtt, percent);
        const int64_t defBase = xmath::pctTrunc(defHits + carryDef, percent);
        const int64_t attTotal = attBase + xmath::pctRound(attBase, attRacial);
        const int64_t defPlanet = defBase + xmath::pctRound(defBase, f.groundDefensePercent);
        const int64_t defTotal = defPlanet + xmath::pctRound(defPlanet, defRacial);
        const std::vector<Stack> attKillers = living(att), defKillers = living(def);
        const int64_t attLeft = applyDamage(r, s, def, attKillers, attTotal, rng);
        const int64_t defLeft = applyDamage(r, s, att, defKillers, defTotal, rng);
        // What is left is carried to the next round, divided back by the percentage.
        carryAtt = percent != 0 ? (xmath::Ext(attLeft) / xmath::percent(percent)).trunc() : 0;
        carryDef = percent != 0 ? (xmath::Ext(defLeft) / xmath::percent(percent)).trunc() : 0;
        if (rec) rec->perRound.push_back(counts());
    }

    // Losses back into the lists and the design statistics; militia losses cost no population.
    auto settle = [&](const std::vector<Stack>& side, int& lost) {
        for (const Stack& st : side) {
            const int dead = st.start - st.count;
            if (dead <= 0) continue;
            if (!st.list) {
                out.militiaLost += dead;
                continue;
            }
            lost += dead;
            (*st.list)[st.index].count -= dead;
            s.design(st.design).lost += dead;
        }
    };
    settle(att, out.attackersLost);
    settle(def, out.defendersLost);
    for (const Stack& st : def)
        if (!st.list) *f.militia = st.count;   // the survivors are the new pool
    // Empty stacks stay in the lists so that stack indices remain valid for the caller.

    out.attackersGone = holders(att) == 0;
    out.captured = !out.attackersGone && holders(def) == 0;
    if (rec) {
        const GroundRound last = rec->perRound.empty() ? counts() : rec->perRound.back();
        rec->attackersLeft = rec->attackers;
        rec->defendersLeft = rec->defenders;
        for (size_t k = 0; k < rec->attackersLeft.size(); ++k) rec->attackersLeft[k].count = last.attackers[k];
        for (size_t k = 0; k < rec->defendersLeft.size(); ++k) rec->defendersLeft[k].count = last.defenders[k];
        rec->militiaLeft = last.militia;
        rec->rounds = out.rounds;
        rec->captured = out.captured;
    }
    return out;
}

void joinUnits(std::vector<UnitStack>& into, std::span<const UnitStack> units) {
    for (const UnitStack& u : units) {
        if (u.count <= 0) continue;
        auto it = std::find_if(into.begin(), into.end(), [&](const UnitStack& x) { return x.design == u.design; });
        if (it != into.end()) it->count += u.count;
        else into.push_back(u);
    }
}

void endInvasion(Colony& c, bool joinCargo) {
    if (joinCargo) joinUnits(c.cargo.units, c.landedTroops);
    c.landedTroops.clear();
    c.invader = {};
    c.militia = -1;
}

void capturePlanet(TurnContext& ctx, Colony& c, EmpireId captor) {
    GameState& s = ctx.state;
    const EmpireId old = c.owner;
    const bool home = c.homeworld;
    const SystemId sys = s.galaxy.object(c.planet).system;
    const std::string name = s.galaxy.object(c.planet).name;
    // The captor takes the planet with its facilities, stored units and
    // population; its surviving troops join the cargo (spec 04 §13).
    c.owner = captor;
    c.homeworld = false;
    c.queue = ConstructionQueue{};
    c.minister = false;
    endInvasion(c, c.invader == captor);
    Empire& e = s.empire(captor);
    if (sys.index() < e.knowledge.explored.size()) e.knowledge.explored[sys.index()] = 1;
    ctx.mood(old, "Any Our Planet Captured", sys, c.planet);
    ctx.mood(old, "Any Planet Lost", sys, c.planet);
    if (home) ctx.mood(old, "Homeworld Lost", sys, c.planet);
    ctx.mood(captor, "Any Enemy Planet Captured", sys, c.planet);
    const Location where = locationOf(s.galaxy, c.planet);
    ctx.log(old, LogCategory::Combat, std::format("{} captured", name), std::format("{} troops overran our defenders and took {}.", e.name, name),
            where);
    ctx.log(captor, LogCategory::Combat, std::format("{} captured", name), std::format("Our troops took {} from the {}.", name, s.empire(old).name),
            where);
    addHistory(s, old, captor, std::format("The {} captured {}", e.name, name), where);
    addHistory(s, captor, old, std::format("Captured {} from the {}", name, s.empire(old).name), where);
}

} // namespace detail

namespace {

// Whether the colony owner's end-of-turn fight stops the call to be shown
// (spec 06 §1.10.6, spec 05 §8 step 17, confirmed: binary): only in a
// turn-based game whose call takes answers (one machine), and only when one
// of the two empires is human-controlled.
bool groundShown(const TurnContext& ctx, EmpireId attacker, EmpireId defender) {
    if (!ctx.battles || !ctx.battles->answers || !turnBased(ctx.state)) return false;
    auto human = [&](EmpireId e) { return e.valid() && e.index() < ctx.state.empires.size() && ctx.state.empire(e).kind == PlayerKind::Human; };
    return human(attacker) || human(defender);
}

} // namespace

void runGroundCombat(TurnContext& ctx, EmpireId owner) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!owner.valid() || owner.index() >= s.empires.size()) return;
    std::optional<CombatSettings> cs;
    std::optional<Rng> rng;   // forked only when a fight happens, so quiet turns leave GameState::rng alone
    for (size_t idx = 0; idx < s.colonies.size(); ++idx) {
        Colony* c = s.colony(ObjectId{idx});
        if (!c || c->owner != owner) continue;
        const std::vector<EmpireId> attackers = invaders(r, s, *c);
        if (attackers.empty()) {
            if (c->invader.valid() || !c->landedTroops.empty() || c->militia >= 0) detail::endInvasion(*c, false);   // nobody invades it (any more)
            continue;
        }
        const EmpireId attacker = attackers.front();
        const std::string name = s.galaxy.object(c->planet).name;
        const Location where = locationOf(s.galaxy, c->planet);
        // The colony's owner is the landed empire, or at Non-Aggression or better
        // with it: no fight; the troops join the cargo and serve the owner, and
        // the invasion ends (confirmed: binary). The treaty counts only here
        // (spec 04 §13 "Treaties").
        if (attacker == owner || !hostile(s, owner, attacker)) {
            detail::endInvasion(*c, true);
            if (attacker != owner)
                ctx.log(attacker, LogCategory::Combat, std::format("Ground combat on {} ended", name),
                        std::format("Our troops on {} now serve the {}.", name, s.empire(owner).name), where);
            ctx.log(owner, LogCategory::Combat, std::format("Ground combat on {} ended", name),
                    std::format("The troops landed on {} joined our garrison.", name), where);
            continue;
        }
        if (!s.empire(owner).alive) continue;
        if (!cs) cs = loadSettings(r);
        if (!rng) rng = s.rng.fork();
        // A fight a window shows stops the call (turn.hpp): the engine fights it
        // and hands the record over; the call made again with the answer fights
        // it again the same way and goes on.
        TurnContext::Battles* ask = groundShown(ctx, attacker, owner) ? ctx.battles : nullptr;
        std::shared_ptr<GameState> before;
        if (ask && ask->next >= ask->answers->size()) before = std::make_shared<GameState>(s);
        GroundCombat record;
        record.planet = c->planet;
        for (const PopulationGroup& g : c->population) record.population += g.millions;
        record.facilities = c->facilities;
        detail::GroundFight fight;
        fight.attacker = attacker;
        fight.defender = owner;
        fight.invaders = &c->landedTroops;
        fight.cargo = &c->cargo;
        fight.population = &c->population;
        fight.militia = &c->militia;
        fight.groundDefensePercent = sumValue1(colonyAbilities(r, s, *c), AbilityKind::PlanetChangeGroundDefense);
        fight.record = &record;
        const detail::GroundOutcome o = detail::fightGround(r, s, *cs, fight, *rng);
        if (ask) {
            if (ask->next >= ask->answers->size()) {
                BattleQuestion q;
                q.kind = BattleQuestion::Kind::Ground;
                q.where = where;
                q.participants = {owner, attacker};
                for (EmpireId e : q.participants)
                    if (s.empire(e).kind == PlayerKind::Human) q.humans.push_back(e);
                q.state = std::move(before);
                q.index = ask->next;
                q.ground = std::move(record);
                throw game::detail::BattleQuestionRaised{std::move(q)};
            }
            ++ask->next;   // shown: the answer says nothing more
        }
        std::erase_if(c->cargo.units, [](const UnitStack& u) { return u.count <= 0; });
        std::erase_if(c->landedTroops, [](const UnitStack& u) { return u.count <= 0; });
        detail::logGroundCombat(ctx, c->planet, attacker, owner, o);
        if (o.captured) detail::capturePlanet(ctx, *c, attacker);
        else if (o.attackersGone) detail::endInvasion(*c, false);
    }
}

} // namespace opense4::game::combat
