#include "game/ai.hpp"

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <format>

namespace opense4::game::ai {

namespace detail {

using datafile::keysEqual;

namespace {

bool designHas(const Rules& r, const Design& d, AbilityKind k) {
    for (const auto& en : d.entries)
        if (hasAbility(r.componentAbilities(en.component), k)) return true;
    return false;
}

} // namespace

// ---- Planner core -----------------------------------------------------------------------------

Planner::Planner(const Rules& rules, const GameState& s, EmpireId e, Mode m)
    : r(rules),
      st(s),
      id(e),
      mode(m),
      prof(profileFor(rules, s.empire(e))),
      state(stateOf(s.empire(e))),
      rng(s.seed ^ ((uint64_t{s.turn} + 1) * 0x9E3779B97F4A7C15ull) ^ ((uint64_t{e.value} + 1) * 0xC2B2AE3D27D4EB4Full)) {
    difficulty = std::clamp(s.options.aiDifficulty, 0, 3);
    neutral = emp().kind == PlayerKind::Neutral;

    const size_t nSys = st.galaxy.systems.size();
    links.resize(nSys);
    for (size_t i = 0; i < nSys; ++i)
        for (ObjectId wp : st.galaxy.system(SystemId{i}).objects) {
            const SpaceObject& o = st.galaxy.object(wp);
            if (o.kind != ObjectKind::WarpPoint || !o.destination.valid()) continue;
            links[i].push_back({wp, st.galaxy.object(o.destination).system});
        }

    ownSystem.assign(nSys, 0);
    for (const auto& c : st.colonies) {
        if (!c || c->owner != id) continue;
        const SystemId sys = st.galaxy.object(c->planet).system;
        ownSystem[sys.index()] = 1;
        if (!home.valid()) {
            home = sys;
            homeLocation = locationOf(st.galaxy, c->planet);
        }
    }
    for (const auto& c : st.colonies)  // the homeworld wins over other colonies
        if (c && c->owner == id && c->homeworld) {
            home = st.galaxy.object(c->planet).system;
            homeLocation = locationOf(st.galaxy, c->planet);
            break;
        }
    if (!home.valid())
        for (const Vehicle& v : st.vehicles)
            if (v.owner == id) {
                home = v.location.system;
                homeLocation = v.location;
                break;
            }

    threat.assign(nSys, 0);
    for (VehicleId vid : emp().knowledge.visibleVehicles) {
        const Vehicle* v = st.vehicle(vid);
        if (!v || v->owner == id || !v->owner.valid() || !fightsWith(v->owner)) continue;
        if (v->location.system.index() >= nSys) continue;
        const DesignInfo& di = info(v->design);
        if (di.attack <= 0) continue;
        threat[v->location.system.index()] += di.combat() * std::max(1, v->count);
    }
}

bool Planner::emit(Command c) {
    const CommandResult res = apply(r, st, id, c);
    if (!res.ok) {
        dropped.push_back(std::format("{}: {}", commandName(c), res.error));
        return false;
    }
    out.push_back(std::move(c));
    return true;
}

bool Planner::fullControl() const {
    if (mode == Mode::Minister) return emp().ministerAll;
    return true;
}

bool Planner::controlsColony(const Colony& c) const {
    if (c.owner != id) return false;
    return mode != Mode::Minister || emp().ministerAll || c.minister;
}

bool Planner::controlsFleet(const Fleet& f) const {
    if (f.owner != id) return false;
    if (mode != Mode::Minister || emp().ministerAll || f.minister) return true;
    // A fleet whose members are all under minister control is the minister's too.
    for (VehicleId m : f.members)
        if (const Vehicle* v = st.vehicle(m); !v || !v->minister) return false;
    return !f.members.empty();
}

bool Planner::controlsVehicle(const Vehicle& v) const {
    if (v.owner != id) return false;
    if (mode != Mode::Minister || emp().ministerAll || v.minister) return true;
    if (v.fleet.valid())
        if (const Fleet* f = st.fleet(v.fleet)) return f->minister;
    return false;
}

// The same knowledge movement plans with (sight::knowsWarpLink), so every
// route the computer picks is one its ships can actually fly.
bool Planner::knownLink(ObjectId wp) const { return sight::knowsWarpLink(st, id, wp); }

std::vector<int> Planner::jumpsFrom(SystemId from) const {
    std::vector<int> dist(st.galaxy.systems.size(), -1);
    if (!from.valid() || from.index() >= dist.size()) return dist;
    std::deque<SystemId> queue{from};
    dist[from.index()] = 0;
    while (!queue.empty()) {
        const SystemId s = queue.front();
        queue.pop_front();
        for (const Link& l : links[s.index()]) {
            if (dist[l.to.index()] >= 0 || !knownLink(l.warpPoint)) continue;
            dist[l.to.index()] = dist[s.index()] + 1;
            queue.push_back(l.to);
        }
    }
    return dist;
}

const std::vector<int>& Planner::jumpsFromHome() {
    if (!homeJumpsReady_) {
        homeJumps_ = jumpsFrom(home);
        homeJumpsReady_ = true;
    }
    return homeJumps_;
}

const DesignInfo& Planner::info(DesignId d) {
    if (infos_.size() < st.designs.size()) infos_.resize(st.designs.size());
    DesignInfo& di = infos_[d.index()];
    if (!di.ready) {
        const Design& design = st.design(d);
        const Empire* owner = design.owner == id ? &emp() : nullptr;
        di.stats = computeDesignStats(r, owner, design);
        di.aiType = aiTypeOf(r, prof, design, di.stats);
        di.role = roleOf(di.aiType, di.stats);
        di.attack = weaponStrength(r, design);
        di.defense = di.stats.structure + int64_t{di.stats.shields} * 2 + int64_t{di.stats.phasedShields} * 3;
        di.ready = true;
    }
    return di;
}

std::vector<DesignId> Planner::designsOfType(std::string_view aiType) {
    std::vector<std::pair<int64_t, DesignId>> scored;
    const DesignTemplate* t = prof.design(aiType);
    for (DesignId d : emp().designs) {
        const Design& design = st.design(d);
        if (design.obsolete) continue;
        const DesignInfo& di = info(d);
        if (!di.stats.problems.empty() || !keysEqual(di.aiType, aiType)) continue;
        const int64_t score = t ? designScore(r, design, di.stats, *t) : di.combat() + di.stats.movement * 10;
        scored.emplace_back(score, d);
    }
    // Best first; newer designs win ties.
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second > b.second; });
    std::vector<DesignId> ids;
    for (const auto& [score, d] : scored) ids.push_back(d);
    return ids;
}

std::optional<DesignId> Planner::bestDesign(std::string_view aiType) {
    auto list = designsOfType(aiType);
    if (list.empty()) return std::nullopt;
    return list.front();
}

bool Planner::atWarWith(EmpireId o) const {
    return o.valid() && o != id && o.index() < st.empires.size() && emp().relation(o).treaty == Treaty::War;
}

bool Planner::fightsWith(EmpireId o) const { return o.valid() && o != id && hostile(st, id, o); }

int Planner::colonyCount() const {
    int n = 0;
    for (const auto& c : st.colonies) n += c && c->owner == id;
    return n;
}

int64_t Planner::vehicleCombat(const Vehicle& v) {
    const DesignInfo& di = info(v.design);
    const int structure = std::max(1, di.stats.structure);
    const int64_t health = std::max<int64_t>(0, structure - vehicleDamageTaken(st, v));
    return di.combat() * std::max(1, v.count) * health / structure;
}

std::vector<VehicleId> Planner::ownVehicles() const {
    std::vector<VehicleId> ids;
    for (const Vehicle& v : st.vehicles)
        if (controlsVehicle(v)) ids.push_back(v.id);
    return ids;
}

bool Planner::setOrders(VehicleId vid, std::vector<Order> orders, bool repeat) {
    const Vehicle* v = st.vehicle(vid);
    if (!v) return false;
    busy.insert(vid);
    if (v->orders == orders && v->repeatOrders == repeat) return true;
    return emit(cmd::SetOrders{vid, {}, std::move(orders), repeat});
}

bool Planner::setFleetOrders(FleetId fid, std::vector<Order> orders) {
    const Fleet* f = st.fleet(fid);
    if (!f) return false;
    busyFleets.insert(fid);
    for (VehicleId m : f->members) busy.insert(m);
    if (f->orders == orders && !f->repeatOrders) return true;
    return emit(cmd::SetOrders{{}, fid, std::move(orders), false});
}

PlanReport Planner::run() {
    if (!emp().alive) return {};
    if (mode == Mode::Minimal) {
        // An absent human: keep research and construction going, nothing else.
        planResearch(*this);
        planConstruction(*this);
        return {std::move(out), std::move(dropped)};
    }
    const bool full = fullControl();
    if (full) {
        if (mode == Mode::Computer) planStrategies(*this);
        planResearch(*this);
        planIntel(*this);
        planDesigns(*this);
    }
    planColonyTypes(*this);
    planConstruction(*this);
    planLogistics(*this);
    planExploration(*this);
    planColonization(*this);
    planMilitary(*this);
    if (full) planDiplomacy(*this);
    return {std::move(out), std::move(dropped)};
}

// ---- Strategies ------------------------------------------------------------------------------

void planStrategies(Planner& p) {
    // The AI's combat strategies (AI_Strategies) join the empire's list once,
    // so design templates and fleets can refer to them by name.
    for (const auto& s : p.prof.strategies) {
        bool have = false;
        for (const auto& mine : p.emp().strategies) have = have || keysEqual(mine.name, s.name);
        if (!have) p.emit(cmd::SetStrategy{-1, s, false});
    }
}

// ---- Shared helpers ----------------------------------------------------------------------------

std::string_view surfaceKey(std::string_view surface) {
    if (keysEqual(surface, "Ice")) return "Ice";
    if (keysEqual(surface, "Rock")) return "Rock";
    return "Gas";
}

std::string colonyTypeName(std::string_view surface) { return std::format("Colony ({})", surfaceKey(surface)); }

std::string aiTypeOf(const Rules& r, const AiProfile& prof, const Design& d, const DesignStats& st) {
    if (const DesignTemplate* t = prof.design(d.designType)) return t->name;
    if (keysEqual(d.designType, "Scout")) return "Scout";
    using ruleset::VehicleType;
    if (st.canColonizeRock) return "Colony (Rock)";
    if (st.canColonizeIce) return "Colony (Ice)";
    if (st.canColonizeGas) return "Colony (Gas)";
    switch (st.vehicleType) {
        case VehicleType::Base: return st.spaceYard && !st.armed() ? "Base Space Yard" : "Defense Base";
        case VehicleType::Fighter: return "Fighter";
        case VehicleType::Satellite: return "Satellite";
        case VehicleType::Mine: return "Mine";
        case VehicleType::Troop: return "Troop";
        case VehicleType::WeaponPlatform: return "Weapon Platform";
        case VehicleType::Drone: return "Anti-Ship Drone";
        default: break;
    }
    if (st.spaceYard) return "Space Yard Ship";
    if (designHas(r, d, AbilityKind::LaunchRecoverFighters)) return "Carrier";
    if (designHas(r, d, AbilityKind::LayMines)) return "Mine Layer";
    if (designHas(r, d, AbilityKind::LaunchRecoverSatellites)) return "Satellite Layer";
    if (designHas(r, d, AbilityKind::MineSweeping)) return "Mine Sweeper";
    if (st.armed()) return "Attack Ship";
    if (st.cargoCapacity > 0) return "Population Transport";
    return "Scout";
}

Role roleOf(std::string_view aiType, const DesignStats& st) {
    if (isUnitType(st.vehicleType)) return Role::Unit;
    if (st.vehicleType == ruleset::VehicleType::Base || st.vehicleType == ruleset::VehicleType::WeaponPlatform) return Role::Base;
    if (keysEqual(aiType, "Scout")) return Role::Scout;
    if (st.canColonizeRock || st.canColonizeIce || st.canColonizeGas) return Role::Colonizer;
    const std::string n = datafile::normalizeKey(aiType);
    if (n.find("transport") != std::string::npos) return Role::Transport;
    if (n.find("carrier") != std::string::npos) return Role::Carrier;
    if (n.find("layer") != std::string::npos) return Role::Layer;
    if (n.find("sweeper") != std::string::npos) return Role::Sweeper;
    if (st.spaceYard) return Role::YardShip;
    if (st.armed()) return Role::Warship;
    if (st.cargoCapacity > 0) return Role::Transport;
    return st.movement > 0 ? Role::Scout : Role::Other;
}

int64_t weaponStrength(const Rules& r, const Design& d) {
    int64_t total = 0;
    for (const DesignEntry& en : d.entries) {
        const ruleset::Component& c = r.component(en.component);
        if (!c.isWeapon()) continue;
        int best = 0;
        const int range = std::max(1, weaponMaxRange(r, en));
        for (int i = 1; i <= range; ++i) best = std::max(best, weaponDamageAtRange(r, en, i));
        const int64_t reload = std::max(1, c.weapon.reloadRate);
        // Point-defense and warheads count less for ship-to-ship fighting.
        const int64_t weight = c.weapon.kind == ruleset::WeaponKind::PointDefense ? 3 : 10;
        total += int64_t{best} * weight / reload;
    }
    return total;
}

bool colonizable(const Rules& r, const GameState& s, const Empire& e, const SpaceObject& planet) {
    (void)r;
    if (planet.kind != ObjectKind::Planet) return false;
    if (s.colony(planet.id)) return false;
    if (s.options.onlyBreathable && !keysEqual(planet.atmosphere, e.race.atmosphere)) return false;
    if (s.options.onlyHomeType && surfaceKey(planet.surface) != surfaceKey(e.race.nativeSurface)) return false;
    return true;
}

int64_t colonyTargetValue(const Rules& r, const GameState& s, const Empire& e, const SpaceObject& planet) {
    (void)s;
    const bool breathes = keysEqual(planet.atmosphere, e.race.atmosphere);
    const ruleset::PlanetSize* ps = planetSize(r, planet);
    const int64_t slots = ps ? (breathes ? ps->maxFacilities : ps->maxFacilitiesDomed) : 1;
    const int64_t pop = ps ? (breathes ? ps->maxPopulation : ps->maxPopulationDomed) : 0;
    int64_t v = slots * 100 + pop / 10 + (breathes ? 300 : 0);
    v += planet.value[0] + planet.value[1] + planet.value[2];
    v += planet.conditions * 2;
    if (surfaceKey(planet.surface) == surfaceKey(e.race.nativeSurface)) v += 50;
    for (const auto& a : planet.abilities)
        if (auto k = parseAbilityKind(a.type); k == AbilityKind::AncientRuins || k == AbilityKind::AncientRuinsUnique) v += 400;
    return v;
}

bool facilityHas(const Rules& r, uint32_t facility, std::string_view ability) {
    const auto kind = parseAbilityKind(ability);
    if (!kind) return false;
    for (const ParsedAbility& a : r.facilityAbilities(facility)) {
        if (*kind != AbilityKind::Unknown && *kind != AbilityKind::AITag && a.kind == *kind) return true;
        if (keysEqual(a.raw, ability)) return true;
    }
    return false;
}

std::optional<uint32_t> bestFacilityFor(const Rules& r, const Empire& e, std::string_view ability) {
    const auto kind = parseAbilityKind(ability);
    if (!kind) return std::nullopt;
    if (*kind != AbilityKind::Unknown && *kind != AbilityKind::AITag) {
        if (auto f = r.bestFacilityWith(e, *kind)) return f;
    }
    std::optional<uint32_t> best;
    for (uint32_t i = 0; i < r.data().facilities.size(); ++i) {
        if (!r.facilityAvailable(e, i) || !facilityHas(r, i, ability)) continue;
        if (!best || r.facility(i).romanNumeral > r.facility(*best).romanNumeral) best = i;
    }
    return best;
}

bool systemWideAbility(std::string_view ability) {
    const std::string n = datafile::normalizeKey(ability);
    return n == "spaceport" || n == "supply generation" || n.find("system") != std::string::npos;
}

int64_t knownStrength(Planner& p, EmpireId of) {
    int64_t total = 0;
    for (VehicleId vid : p.emp().knowledge.visibleVehicles) {
        const Vehicle* v = p.st.vehicle(vid);
        if (!v || v->owner != of) continue;
        total += p.vehicleCombat(*v);
    }
    // Planets defend themselves; a rough allowance per known colony (inferred).
    for (const auto& c : p.st.colonies)
        if (c && c->owner == of && p.explored(p.st.galaxy.object(c->planet).system)) total += 50;
    return total;
}

} // namespace detail

// ---- Public entry points -------------------------------------------------------------------------

PlanReport planTurnReport(const Rules& r, const GameState& s, EmpireId e, bool minimal) {
    if (!e.valid() || e.index() >= s.empires.size() || !s.empire(e).alive) return {};
    detail::Planner p(r, s, e, minimal ? detail::Mode::Minimal : detail::Mode::Computer);
    return p.run();
}

std::vector<Command> planTurn(const Rules& r, const GameState& s, EmpireId e, bool minimal) {
    return planTurnReport(r, s, e, minimal).commands;
}

std::vector<Command> ministerCommands(const Rules& r, const GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return {};
    const Empire& emp = s.empire(e);
    if (!emp.alive || emp.kind != PlayerKind::Human) return {};
    bool any = emp.ministerAll;
    for (const auto& c : s.colonies) any = any || (c && c->owner == e && c->minister);
    for (const Vehicle& v : s.vehicles) any = any || (v.owner == e && v.minister);
    for (const Fleet& f : s.fleets) any = any || (f.owner == e && f.minister);
    if (!any) return {};
    detail::Planner p(r, s, e, detail::Mode::Minister);
    return p.run().commands;
}

std::string_view moodLabel(int anger) {
    // Inferred bands on the 0..100 anger scale (spec 05 §7.3).
    if (anger < 10) return "Friendly";
    if (anger < 30) return "Calm";
    if (anger < 50) return "Wary";
    if (anger < 70) return "Annoyed";
    if (anger < 90) return "Angry";
    return "Furious";
}

std::vector<std::string> randomComputerPresets(const Rules& r, int setting, bool neutral, Rng& rng) {
    static constexpr std::array<std::string_view, 3> kLevels{"Low", "Medium", "High"};
    static constexpr std::array<int64_t, 3> kMin{1, 2, 4}, kMax{2, 5, 8};  // our fallbacks
    const size_t level = static_cast<size_t>(std::clamp(setting, 0, 2));
    const std::string_view kind = neutral ? "Neutral" : "Computer";
    const int64_t lo = r.setting(std::format("Minimum {} Player {} Setting", kind, kLevels[level]), kMin[level]);
    const int64_t hi = r.setting(std::format("Maximum {} Player {} Setting", kind, kLevels[level]), kMax[level]);
    const int64_t count = rng.range(std::max<int64_t>(0, lo), std::max<int64_t>(0, hi));

    const auto& presets = r.racePresets();
    std::vector<uint8_t> used(presets.size(), 0);
    auto candidates = [&](auto&& accept) {
        std::vector<size_t> out;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].neutral == neutral && !used[i] && accept(presets[i])) out.push_back(i);
        return out;
    };
    std::vector<std::string> out;
    for (int64_t n = 0; n < count; ++n) {
        std::vector<size_t> pool;
        const int64_t groups = neutral ? 0 : r.setting("Random Player Personality Groups", 0);
        if (groups > 0) {
            int64_t total = 0;
            for (int64_t g = 1; g <= groups; ++g)
                total += std::max<int64_t>(0, r.setting(std::format("Random Player Personality Group {} Percent", g), 0));
            if (total > 0) {
                int64_t roll = static_cast<int64_t>(rng.below(static_cast<uint64_t>(total)));
                int64_t group = 1;
                for (; group <= groups; ++group) {
                    roll -= std::max<int64_t>(0, r.setting(std::format("Random Player Personality Group {} Percent", group), 0));
                    if (roll < 0) break;
                }
                pool = candidates([&](const ruleset::RacePreset& p) { return p.personalityGroup == group; });
            }
            if (pool.empty()) pool = candidates([](const ruleset::RacePreset& p) { return p.personalityGroup != 0; });
        }
        if (pool.empty()) pool = candidates([](const ruleset::RacePreset&) { return true; });
        if (pool.empty()) {
            std::fill(used.begin(), used.end(), 0);  // more players than races: allow repeats
            pool = candidates([](const ruleset::RacePreset&) { return true; });
        }
        if (pool.empty()) break;
        const size_t pick = pool[static_cast<size_t>(rng.below(pool.size()))];
        used[pick] = 1;
        out.push_back(presets[pick].folder);
    }
    return out;
}

namespace {

// The "Computer Player Bonus" setting as 0 (None) .. 3 (High) for a computer
// empire, -1 for a human one. Settings above High count as High (inferred).
int bonusLevel(const GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size() || s.empire(e).kind == PlayerKind::Human) return -1;
    return std::clamp(s.options.aiBonus, 0, 3);
}

} // namespace

int incomeBonusFactor(const GameState& s, EmpireId e) {
    static constexpr std::array<int, 4> kFactor{1, 2, 3, 5};  // (confirmed: binary)
    const int level = bonusLevel(s, e);
    return level < 0 ? 1 : kFactor[static_cast<size_t>(level)];
}

int constructionBonusPercent(const GameState& s, EmpireId e) {
    static constexpr std::array<int, 4> kPercent{100, 150, 200, 300};  // (confirmed: binary)
    const int level = bonusLevel(s, e);
    return level < 0 ? 100 : kPercent[static_cast<size_t>(level)];
}

} // namespace opense4::game::ai
