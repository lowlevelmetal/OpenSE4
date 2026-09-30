// Ground combat and planet capture (docs/spec/04 §13).
//
// Invading troops are troop stacks in a colony's cargo whose design owner is
// hostile to the colony owner (see combat.hpp). The same fight runs at once
// when troops drop during a space battle (combat_space.cpp) and in each
// empire's end-of-turn processing (spec 05 §8 step 17) for every colony where
// its troops still invade: up to `Number Of Ground Combat Turns` rounds of the invaders against the defender's troops, militia and
// other stored units. When the defending troops and militia are gone the
// planet changes hands with its facilities, stored units and population.

#include "game/combat.hpp"

#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <optional>

namespace opense4::game::combat {

namespace detail {

namespace {

// One stack of a side, in the order damage reaches it.
struct Stack {
    size_t cargo = SIZE_MAX;   // index into the cargo (SIZE_MAX: militia)
    int count = 0;
    int start = 0;
    int64_t hitPoints = 1;     // per unit
    int attack = 0;            // per unit that hits (0: does not attack)
    bool rolls = false;        // troops and militia roll to hit
};

// Structure plus shields of one unit (spec 04 §13; shields counted once here).
int64_t groundHitPoints(const Rules& r, const Design& d) {
    int64_t shields = hullSum(r, d, AbilityKind::ShieldGeneration) + hullSum(r, d, AbilityKind::PhasedShieldGeneration);
    for (const DesignEntry& e : d.entries)
        shields += mountedShield(r, e, AbilityKind::ShieldGeneration) + mountedShield(r, e, AbilityKind::PhasedShieldGeneration);
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

// (Offense or defense plus − minus) over a side's troop units, the best part of
// each component family counting once, halved and truncated (confirmed: binary).
int sideModifier(const Rules& r, const GameState& s, const Cargo& cargo, const std::vector<Stack>& side, AbilityKind plus, AbilityKind minus) {
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
    int64_t hull = 0;
    for (const Stack& st : side) {
        if (!st.rolls || st.cargo == SIZE_MAX || st.count <= 0) continue;
        const Design& d = s.design(cargo.units[st.cargo].design);
        hull += hullSum(r, d, plus) - hullSum(r, d, minus);   // (inferred) each troop design's hull counts once
        for (const DesignEntry& e : d.entries) {
            note(bestPlus, e.component, plus);
            note(bestMinus, e.component, minus);
        }
    }
    int64_t total = hull;
    for (const auto& [f, v] : bestPlus) total += v;
    for (const auto& [f, v] : bestMinus) total -= v;
    return static_cast<int>(total / 2);
}

int alive(const std::vector<Stack>& side, bool rollersOnly) {
    int n = 0;
    for (const Stack& st : side)
        if (!rollersOnly || st.rolls) n += st.count;
    return n;
}

int64_t hits(const std::vector<Stack>& side, int chance, Rng& rng) {
    int64_t total = 0;
    for (const Stack& st : side) {
        if (!st.rolls || st.attack <= 0) continue;
        for (int n = 0; n < st.count; ++n)
            if (rng.rangeInt(1, 100) <= chance) total += st.attack;   // no clamp (confirmed: binary)
    }
    return total;
}

// Whole units die while the damage covers their hit points; the rest moves on
// to the next stack. Returns what is left over.
int64_t applyDamage(std::vector<Stack>& side, int64_t damage) {
    for (Stack& st : side) {
        if (damage <= 0) break;
        if (st.count <= 0) continue;
        const int64_t killed = std::min<int64_t>(st.count, damage / st.hitPoints);
        st.count -= static_cast<int>(killed);
        damage -= killed * st.hitPoints;
    }
    return std::max<int64_t>(0, damage);
}

} // namespace

int groundModifier(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return (c ? c->groundCombat : 0) + e.race.characteristic(Characteristic::PhysicalStrength) - 100;
}

GroundOutcome fightGround(const Rules& r, GameState& s, const CombatSettings& cs, const GroundFight& f, Rng& rng) {
    GroundOutcome out;
    Cargo& cargo = *f.cargo;
    std::vector<Stack> att, defTroops, defOther;
    for (size_t k = 0; k < cargo.units.size(); ++k) {
        const UnitStack& u = cargo.units[k];
        if (u.count <= 0) continue;
        const Design& d = s.design(u.design);
        Stack st;
        st.cargo = k;
        st.count = st.start = u.count;
        st.hitPoints = groundHitPoints(r, d);
        if (isTroopDesign(r, s, u.design)) {
            st.rolls = true;
            st.attack = troopAttack(r, d);
            if (d.owner == f.attacker) att.push_back(st);
            else if (d.owner == f.defender || !enemies(s, d.owner, f.defender)) defTroops.push_back(st);
            // Another invader's troops wait for their own fight.
        } else {
            defOther.push_back(st);   // stored units absorb damage but never attack
        }
    }
    if (att.empty()) return out;
    out.attackersAtStart = alive(att, true);

    // Militia: the colony's pool, raised per population group (confirmed: binary).
    if (*f.militia < 0) *f.militia = militiaCount(cs, *f.population);
    Stack militia;
    militia.rolls = true;
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
    // Damage reaches troops and militia first, then the other stored units.
    std::vector<Stack> def = defTroops;
    def.push_back(militia);
    def.insert(def.end(), defOther.begin(), defOther.end());

    const int attOffense = sideModifier(r, s, cargo, att, AbilityKind::CombatToHitOffensePlus, AbilityKind::CombatToHitOffenseMinus);
    const int attDefense = sideModifier(r, s, cargo, att, AbilityKind::CombatToHitDefensePlus, AbilityKind::CombatToHitDefenseMinus);
    const int defOffense = sideModifier(r, s, cargo, def, AbilityKind::CombatToHitOffensePlus, AbilityKind::CombatToHitOffenseMinus);
    const int defDefense = sideModifier(r, s, cargo, def, AbilityKind::CombatToHitDefensePlus, AbilityKind::CombatToHitDefenseMinus);
    const int attChance = attOffense + 50 - defDefense;
    const int defChance = defOffense + 50 - attDefense;
    const int attRacial = groundModifier(r, s.empire(f.attacker));
    const int defRacial = f.defender.valid() ? groundModifier(r, s.empire(f.defender)) : 0;
    const int percent = cs.groundDamagePercent;

    int64_t carryAtt = 0, carryDef = 0;
    for (int round = 1; round <= cs.groundTurns; ++round) {
        if (alive(att, true) == 0 || alive(def, true) == 0) break;
        ++out.rounds;
        const int64_t attHits = hits(att, attChance, rng);
        const int64_t defHits = hits(def, defChance, rng);
        // The totals with last round's carry, times the ground percentage (truncated),
        // then the planet's and the races' modifiers (rounded) (confirmed: binary).
        const int64_t attBase = xmath::pctTrunc(attHits + carryAtt, percent);
        const int64_t defBase = xmath::pctTrunc(defHits + carryDef, percent);
        const int64_t attTotal = attBase + xmath::pctRound(attBase, attRacial);
        const int64_t defTotal = defBase + xmath::pctRound(defBase, f.groundDefensePercent) + xmath::pctRound(defBase, defRacial);
        const int64_t attLeft = applyDamage(def, attTotal);
        const int64_t defLeft = applyDamage(att, defTotal);
        // What is left is carried to the next round, divided back by the percentage.
        carryAtt = percent != 0 ? (xmath::Ext(attLeft) / xmath::percent(percent)).trunc() : 0;
        carryDef = percent != 0 ? (xmath::Ext(defLeft) / xmath::percent(percent)).trunc() : 0;
    }

    // Losses back into the cargo and the design statistics; militia losses cost no population.
    auto settle = [&](const std::vector<Stack>& side, int& lost) {
        for (const Stack& st : side) {
            const int dead = st.start - st.count;
            if (dead <= 0) continue;
            if (st.cargo == SIZE_MAX) {
                out.militiaLost += dead;
                continue;
            }
            lost += dead;
            cargo.units[st.cargo].count -= dead;
            s.design(cargo.units[st.cargo].design).lost += dead;
        }
    };
    settle(att, out.attackersLost);
    settle(def, out.defendersLost);
    for (const Stack& st : def)
        if (st.cargo == SIZE_MAX) *f.militia = st.count;   // the survivors are the new pool
    // Empty stacks stay in the cargo so that stack indices remain valid for the caller.

    out.attackersGone = alive(att, true) == 0;
    out.captured = !out.attackersGone && alive(def, true) == 0;
    return out;
}

void capturePlanet(TurnContext& ctx, Colony& c, EmpireId captor) {
    GameState& s = ctx.state;
    const EmpireId old = c.owner;
    const bool home = c.homeworld;
    const SystemId sys = s.galaxy.object(c.planet).system;
    const std::string name = s.galaxy.object(c.planet).name;
    // The captor takes the planet with its facilities, stored units and population (spec 04 §13).
    c.owner = captor;
    c.homeworld = false;
    c.queue = ConstructionQueue{};
    c.minister = false;
    c.militia = -1;
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
}

} // namespace detail

namespace {

void logGround(TurnContext& ctx, const Colony& c, EmpireId attacker, EmpireId defender, const detail::GroundOutcome& o) {
    const GameState& s = ctx.state;
    const std::string name = s.galaxy.object(c.planet).name;
    const Location where = locationOf(s.galaxy, c.planet);
    const std::string report = std::format("{} rounds. Invaders lost {} of {} troops; defenders lost {} units and {} militia.", o.rounds,
                                           o.attackersLost, o.attackersAtStart, o.defendersLost, o.militiaLost);
    if (o.captured) {
        ctx.log(attacker, LogCategory::Combat, std::format("Ground combat on {}", name), report, where);
        return;
    }
    ctx.log(attacker, LogCategory::Combat, std::format("Ground combat on {}", name),
            std::format("{} {}", report, o.attackersGone ? "Our invasion failed." : "The fight goes on."), where);
    ctx.log(defender, LogCategory::Combat, std::format("Ground combat on {}", name),
            std::format("{} {}", report, o.attackersGone ? "The invaders were destroyed." : "The fight goes on."), where);
}

} // namespace

void runGroundCombat(TurnContext& ctx, EmpireId attacker) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!attacker.valid() || attacker.index() >= s.empires.size() || !s.empire(attacker).alive) return;
    std::optional<CombatSettings> cs;
    std::optional<Rng> rng;   // forked only when a fight happens, so quiet turns leave GameState::rng alone
    for (size_t idx = 0; idx < s.colonies.size(); ++idx) {
        Colony* c = s.colony(ObjectId{idx});
        if (!c) continue;
        const std::vector<EmpireId> attackers = invaders(r, s, *c);
        if (attackers.empty()) c->militia = -1;  // nobody invades it (any more)
        if (std::find(attackers.begin(), attackers.end(), attacker) == attackers.end()) continue;
        // Fighting stops at once on peace or surrender (history 1.03, 1.20).
        if (c->owner == attacker || !detail::enemies(s, attacker, c->owner)) continue;
        if (!cs) cs = loadSettings(r);
        if (!rng) rng = s.rng.fork();
        const EmpireId defender = c->owner;
        detail::GroundFight fight;
        fight.attacker = attacker;
        fight.defender = defender;
        fight.cargo = &c->cargo;
        fight.population = &c->population;
        fight.militia = &c->militia;
        fight.groundDefensePercent = sumValue1(colonyAbilities(r, s, *c), AbilityKind::PlanetChangeGroundDefense);
        const detail::GroundOutcome o = detail::fightGround(r, s, *cs, fight, *rng);
        std::erase_if(c->cargo.units, [](const UnitStack& u) { return u.count <= 0; });
        logGround(ctx, *c, attacker, defender, o);
        if (o.captured) detail::capturePlanet(ctx, *c, attacker);
        if (Colony* after = s.colony(ObjectId{idx}); after && invaders(r, s, *after).empty()) after->militia = -1;
    }
}

} // namespace opense4::game::combat
