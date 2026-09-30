// Ground combat and planet capture (docs/spec/04 §13).
//
// Invading troops are troop stacks in a colony's cargo whose design owner is
// hostile to the colony owner (see combat.hpp). Each game turn, turn phase 4
// fights every such colony for up to `Number Of Ground Combat Turns` rounds:
// the invaders against the defender's troops plus militia raised from the
// population. When the defenders are gone the planet changes hands with its
// facilities, stored units and population.

#include "game/combat.hpp"

#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace opense4::game::combat {

namespace {

struct GroundUnit {
    bool militia = false;
    size_t stack = 0;            // troops: index of the cargo stack
    Vehicle unit;                // troops: one soldier's component damage
    detail::ShieldState sh;
    int hp = 0;                  // militia hit points
    int firepower = 0;           // damage per round before modifiers
    bool alive = true;
};

struct Shot {
    bool atDefender = false;
    size_t target = 0;
    int damage = 0;
    DesignId shooter;            // for design statistics (invalid for militia)
};

// Entry-1 damage of every intact weapon on one troop unit (spec 04 §13, inferred).
int troopFirepower(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    int total = 0;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const ruleset::Component& c = r.component(d.entries[i].component);
        if (!c.isWeapon() || c.weapon.kind == ruleset::WeaponKind::Warhead || !entryIntact(r, s, v, i)) continue;
        total += weaponDamageAtRange(r, d.entries[i], 1);
    }
    return total;
}

// Culture Ground Combat plus Physical Strength, as a percentage (inferred).
int groundModifier(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return 100 + (c ? c->groundCombat : 0) + e.race.characteristic(Characteristic::PhysicalStrength) - 100;
}

int scaled(int base, int groundPercent, int modifier) {
    if (base <= 0) return 0;
    const int64_t v = int64_t{base} * groundPercent / 100 * std::max(0, modifier) / 100;
    return static_cast<int>(std::max<int64_t>(1, v));   // (inferred) an armed unit always does at least 1
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
    Empire& e = s.empire(captor);
    if (sys.index() < e.knowledge.explored.size()) e.knowledge.explored[sys.index()] = 1;
    ctx.mood(old, "Any Our Planet Captured", sys, c.planet);
    ctx.mood(old, "Any Planet Lost", sys, c.planet);
    if (home) ctx.mood(old, "Homeworld Lost", sys, c.planet);
    ctx.mood(captor, "Any Enemy Planet Captured", sys, c.planet);
    const Location where = locationOf(s.galaxy, c.planet);
    ctx.log(old, LogCategory::Combat, std::format("{} captured", name),
            std::format("{} troops overran our defenders and took {}.", e.name, name), where);
    ctx.log(captor, LogCategory::Combat, std::format("{} captured", name),
            std::format("Our troops took {} from the {}.", name, s.empire(old).name), where);
}

void groundBattle(TurnContext& ctx, Colony& c, EmpireId attacker, const CombatSettings& cs, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const EmpireId defender = c.owner;
    std::vector<GroundUnit> att, def;
    for (size_t k = 0; k < c.cargo.units.size(); ++k) {
        const UnitStack& st = c.cargo.units[k];
        if (st.count <= 0 || !isTroopDesign(r, s, st.design)) continue;
        const EmpireId owner = s.design(st.design).owner;
        std::vector<GroundUnit>* side = nullptr;
        if (owner == attacker) side = &att;
        else if (owner == defender || !detail::enemies(s, owner, defender)) side = &def;
        if (!side) continue;   // another invader waits for its own fight
        for (int n = 0; n < st.count; ++n) {
            GroundUnit u;
            u.stack = k;
            u.unit.owner = owner;
            u.unit.design = st.design;
            u.unit.damage.assign(s.design(st.design).entries.size(), 0);
            u.unit.supply = 1;
            detail::refreshShields(r, s, u.unit, u.sh, true);
            u.firepower = troopFirepower(r, s, u.unit);
            side->push_back(std::move(u));
        }
    }
    if (att.empty()) return;
    // Satellites, mines, fighters and weapon platforms never fight on the ground (history 1.24, 1.40).
    const int militia = militiaCount(cs, c.totalPopulation());
    for (int n = 0; n < militia; ++n) {
        GroundUnit u;
        u.militia = true;
        u.hp = cs.militiaHitPoints;
        u.firepower = cs.militiaAttack;
        def.push_back(std::move(u));
    }

    const int attMod = groundModifier(r, s.empire(attacker));
    const int defMod = groundModifier(r, s.empire(defender)) +
                       static_cast<int>(sumValue1(colonyAbilities(r, s, c), AbilityKind::PlanetChangeGroundDefense));
    // (inferred) the hit chance of an unmodified shot at one square (spec 04 §19 Q17).
    const int chance = detail::toHitChance(cs, 1, 0, 0, 0);

    auto alive = [](const std::vector<GroundUnit>& v, bool troopsOnly) {
        std::vector<size_t> out;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].alive && (!troopsOnly || !v[i].militia)) out.push_back(i);
        return out;
    };
    int rounds = 0;
    std::vector<Shot> shots;
    for (int round = 1; round <= cs.groundTurns; ++round) {
        const std::vector<size_t> attAlive = alive(att, false);
        const std::vector<size_t> defAlive = alive(def, false);
        if (attAlive.empty() || defAlive.empty()) break;
        ++rounds;
        const std::vector<size_t> defTroops = alive(def, true);
        shots.clear();
        // Troops pick enemy troops before anything else (history 1.24); everyone fires at once.
        for (size_t i : attAlive) {
            const int dmg = scaled(att[i].firepower, cs.groundDamagePercent, attMod);
            if (dmg <= 0) continue;
            const std::vector<size_t>& pool = defTroops.empty() ? defAlive : defTroops;
            const size_t t = pool[rng.below(pool.size())];
            if (rng.rangeInt(1, 100) <= chance) shots.push_back({true, t, dmg, att[i].unit.design});
        }
        for (size_t i : defAlive) {
            const int dmg = scaled(def[i].firepower, cs.groundDamagePercent, defMod);
            if (dmg <= 0) continue;
            const size_t t = attAlive[rng.below(attAlive.size())];
            if (rng.rangeInt(1, 100) <= chance) shots.push_back({false, t, dmg, def[i].militia ? DesignId{} : def[i].unit.design});
        }
        for (const Shot& shot : shots) {
            GroundUnit& u = shot.atDefender ? def[shot.target] : att[shot.target];
            if (!u.alive) continue;
            bool died = false;
            if (u.militia) {
                u.hp -= shot.damage;
                died = u.hp <= 0;
            } else {
                // Troops absorb damage through shields, armor and components (history 1.62).
                died = detail::hitUnit(r, s, u.unit, u.sh, shot.damage, DamageType::Normal, rng).destroyed;
            }
            if (!died) continue;
            u.alive = false;
            if (shot.shooter.valid()) ++s.design(shot.shooter).kills;
            if (!u.militia) ++s.design(u.unit.design).lost;
        }
    }

    // Survivors go back into their stacks; militia losses cost no population (inferred).
    int attLost = 0, defTroopsLost = 0, militiaLost = 0;
    auto settle = [&](const std::vector<GroundUnit>& side, int& troopsLost) {
        for (const GroundUnit& u : side) {
            if (u.alive) continue;
            if (u.militia) {
                ++militiaLost;
                continue;
            }
            ++troopsLost;
            --c.cargo.units[u.stack].count;
        }
    };
    settle(att, attLost);
    settle(def, defTroopsLost);
    std::erase_if(c.cargo.units, [](const UnitStack& u) { return u.count <= 0; });

    const bool defendersGone = alive(def, false).empty();
    const bool attackersGone = alive(att, false).empty();
    const std::string name = s.galaxy.object(c.planet).name;
    const Location where = locationOf(s.galaxy, c.planet);
    const std::string report = std::format("{} rounds. Invaders lost {} of {} troops; defenders lost {} troops and {} militia.", rounds,
                                           attLost, att.size(), defTroopsLost, militiaLost);
    if (!defendersGone || attackersGone) {
        ctx.log(attacker, LogCategory::Combat, std::format("Ground combat on {}", name),
                std::format("{} {}", report, attackersGone ? "Our invasion failed." : "The fight goes on."), where);
        ctx.log(defender, LogCategory::Combat, std::format("Ground combat on {}", name),
                std::format("{} {}", report, attackersGone ? "The invaders were destroyed." : "The fight goes on."), where);
    }
    if (defendersGone && !attackersGone) {
        ctx.log(attacker, LogCategory::Combat, std::format("Ground combat on {}", name), report, where);
        capturePlanet(ctx, c, attacker);
    }
}

} // namespace

void runGroundCombat(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    std::optional<CombatSettings> cs;
    std::optional<Rng> rng;   // forked only when a fight happens, so quiet turns leave GameState::rng alone
    for (size_t idx = 0; idx < s.colonies.size(); ++idx) {
        if (!s.colonies[idx]) continue;
        const std::vector<EmpireId> attackers = invaders(r, s, *s.colonies[idx]);
        for (EmpireId e : attackers) {
            Colony* c = s.colony(ObjectId{idx});
            // Fighting stops at once on peace or surrender (history 1.03, 1.20).
            if (!c || c->owner == e || !detail::enemies(s, e, c->owner)) continue;
            if (!cs) cs = loadSettings(r);
            if (!rng) rng = s.rng.fork();
            groundBattle(ctx, *c, e, *cs, *rng);
        }
    }
}

} // namespace opense4::game::combat
