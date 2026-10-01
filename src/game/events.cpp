#include "game/events.hpp"

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>

namespace opense4::game::effects {

namespace {

using datafile::keysEqual;

constexpr std::array<std::string_view, static_cast<size_t>(Effect::Count)> kEffectIds{
#define OPENSE4_EFFECT_ID(name, text) text,
    OPENSE4_EFFECTS(OPENSE4_EFFECT_ID)
#undef OPENSE4_EFFECT_ID
};

bool validEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }
bool living(const GameState& s, EmpireId e) { return validEmpire(s, e) && s.empire(e).alive; }
bool validObject(const GameState& s, ObjectId o) { return o.valid() && o.index() < s.galaxy.objects.size(); }
bool validSystem(const GameState& s, SystemId sys) { return sys.valid() && sys.index() < s.galaxy.systems.size(); }

bool isShip(const Rules& r, const GameState& s, const Vehicle& v) {
    return v.count > 0 && !isUnitType(vehicleType(r, s, v));
}

bool emotionless(const Rules& r, const GameState& s, const Colony& c) {
    return validEmpire(s, c.owner) && r.hasTrait(s.empire(c.owner).race, "Population Emotionless");
}

bool plagueImmune(const Rules& r, const GameState& s, const Colony& c) {
    return validEmpire(s, c.owner) && r.hasTrait(s.empire(c.owner).race, "No Plagues");
}

// Best "Plague Prevention - System" level protecting a colony's system.
int64_t plagueProtection(const Rules& r, const GameState& s, const Colony& c) {
    const SystemId sys = s.galaxy.object(c.planet).system;
    int64_t best = 0;
    for (const auto& other : s.colonies)
        if (other && other->owner == c.owner && s.galaxy.object(other->planet).system == sys)
            best = std::max(best, bestValue1(colonyAbilities(r, s, *other), AbilityKind::PlaguePreventionSystem));
    return best;
}

// Systems holding at least one colony of the empire, in id order.
std::vector<SystemId> colonySystems(const GameState& s, EmpireId e) {
    std::vector<SystemId> out;
    for (const auto& c : s.colonies)
        if (c && c->owner == e) out.push_back(s.galaxy.object(c->planet).system);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool systemHasHomeworld(const GameState& s, SystemId sys) {
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && c->homeworld) return true;
    return false;
}

std::vector<Sector> emptySectors(const GameState& s, SystemId sys, bool outerRingOnly) {
    std::vector<Sector> out;
    for (int y = 0; y < kSystemSize; ++y)
        for (int x = 0; x < kSystemSize; ++x) {
            const Sector sec{x, y};
            if (outerRingOnly && chebyshev(sec, Sector{kSystemCenter, kSystemCenter}) != kSystemCenter) continue;
            const auto& objs = s.galaxy.system(sys).objects;
            if (std::none_of(objs.begin(), objs.end(), [&](ObjectId o) { return s.galaxy.object(o).sector == sec; })) out.push_back(sec);
        }
    return out;
}

template <class T>
std::optional<T> choose(const std::vector<T>& options, Rng& rng) {
    if (options.empty()) return std::nullopt;
    return options[rng.below(options.size())];
}

// The original's way of picking a target (spec 05 §2.1, §4, confirmed:
// binary): up to kTargetDraws uniform draws among the candidates; one that
// fails `check` is removed from the draw.
template <class T, class Check>
std::optional<T> drawCandidate(std::vector<T> candidates, Rng& rng, Check&& check) {
    for (int draw = 0; draw < kTargetDraws && !candidates.empty(); ++draw) {
        const size_t i = rng.below(candidates.size());
        if (check(candidates[i])) return candidates[i];
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return std::nullopt;
}

bool validArea(const Rules& r, ruleset::TechAreaId a) { return a.valid() && a.index() < r.data().techAreas.size(); }

// An object that is still part of its system (closed warp points and objects
// swept away by a shockwave are not).
bool inSystem(const GameState& s, ObjectId o) {
    if (!validObject(s, o)) return false;
    const SpaceObject& obj = s.galaxy.object(o);
    if (!validSystem(s, obj.system)) return false;
    const auto& objs = s.galaxy.system(obj.system).objects;
    return std::find(objs.begin(), objs.end(), o) != objs.end();
}

void setLocationTokens(Tokens& t, const GameState& s, Location where) {
    if (!validSystem(s, where.system)) return;
    t.systemName = s.galaxy.system(where.system).name;
    t.sectorName = std::format("({}, {})", where.sector.x, where.sector.y);
}

void setVehicleTokens(Tokens& t, const Rules& r, const GameState& s, const Vehicle& v) {
    t.vehicleName = v.name;
    t.vehicleSize = r.hull(s.design(v.design).hull).name;
    t.designName = s.design(v.design).name;
    setLocationTokens(t, s, v.location);
}

void setObjectTokens(Tokens& t, const GameState& s, ObjectId o) {
    const SpaceObject& obj = s.galaxy.object(o);
    switch (obj.kind) {
        case ObjectKind::Star:
        case ObjectKind::DestroyedStar: t.starName = obj.name; break;
        case ObjectKind::WarpPoint: t.warpPointName = obj.name; break;
        default: t.planetName = obj.name; break;
    }
    setLocationTokens(t, s, locationOf(s.galaxy, o));
}

void explore(GameState& s, EmpireId e, SystemId sys) {
    if (!validEmpire(s, e) || !validSystem(s, sys)) return;
    auto& explored = s.empire(e).knowledge.explored;
    if (explored.size() < s.galaxy.systems.size()) explored.resize(s.galaxy.systems.size(), 0);
    explored[sys.index()] = 1;
}

// Keeps per-object arrays in step after the galaxy gained objects.
void objectsAdded(GameState& s) {
    s.colonies.resize(s.galaxy.objects.size());
    for (Empire& e : s.empires) e.knowledge.knownWarpLink.resize(s.galaxy.objects.size(), 0);
}

ObjectId addObject(GameState& s, SystemId sys, SpaceObject obj) {
    obj.id = ObjectId{s.galaxy.objects.size()};
    obj.system = sys;
    s.galaxy.system(sys).objects.push_back(obj.id);
    s.galaxy.objects.push_back(std::move(obj));
    objectsAdded(s);
    return s.galaxy.objects.back().id;
}

// A random sector-object appearance of a physical type (optionally of a size).
std::optional<uint32_t> pickSectorType(const Rules& r, std::string_view physical, std::string_view size, Rng& rng) {
    std::vector<uint32_t> exact, any;
    const auto& types = r.data().sectorObjectTypes;
    for (uint32_t i = 0; i < types.size(); ++i) {
        if (!keysEqual(types[i].physicalType, physical)) continue;
        any.push_back(i);
        const std::string_view typeSize = !types[i].planetSize.empty() ? std::string_view(types[i].planetSize) : types[i].starSize;
        if (!size.empty() && keysEqual(typeSize, size)) exact.push_back(i);
    }
    return choose(exact.empty() ? any : exact, rng);
}

void applySectorType(const Rules& r, SpaceObject& obj, uint32_t index) {
    const ruleset::SectorObjectType& st = r.data().sectorObjectTypes[index];
    obj.sectorType = index;
    if (obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) {
        if (!st.planetSize.empty()) obj.size = st.planetSize;
        if (!st.planetPhysicalType.empty()) obj.surface = st.planetPhysicalType;
        if (!st.planetAtmosphere.empty()) obj.atmosphere = st.planetAtmosphere;
    } else if (obj.kind == ObjectKind::Star || obj.kind == ObjectKind::DestroyedStar) {
        if (!st.starSize.empty()) obj.size = st.starSize;
        obj.starAge = st.starAge;
        obj.starColor = st.starColor;
        obj.starLuminosity = st.starLuminosity;
    }
}

// Removes a colony (the planet becomes uncolonized) with the owner's mood events.
void destroyVehicle(TurnContext& ctx, Vehicle& v) {
    if (v.count <= 0) return;
    for (const UnitStack& st : groupStacks(v)) ctx.state.design(st.design).lost += st.count;   // every unit of a group
    v.count = 0;
    v.mixed.clear();
    ctx.mood(v.owner, "Any Ship Lost");
    ctx.mood(v.owner, "Ship Lost in System", v.location.system);
}

std::string designClassName(bool units) { return units ? "unit" : "ship"; }

// Designs of `owner` of the ship class (ships, bases) or the unit class.
std::vector<DesignId> designsOfClass(const Rules& r, const GameState& s, EmpireId owner, bool units) {
    std::vector<DesignId> out;
    for (DesignId d : s.empire(owner).designs) {
        if (d.index() >= s.designs.size()) continue;
        const bool unit = isUnitType(r.hull(s.design(d).hull).type);
        if (unit == units) out.push_back(d);
    }
    return out;
}

// Research - Steal works on an area where the target is ahead of the thief
// and that is neither racial nor unique (spec 05 §2.3, confirmed: binary).
bool stealable(const Rules& r, const GameState& s, EmpireId source, EmpireId target, ruleset::TechAreaId a) {
    if (!validArea(r, a)) return false;
    const ruleset::TechArea& t = r.tech(a);
    return s.empire(target).techLevel(a) > s.empire(source).techLevel(a) && t.racialArea <= 0 && t.uniqueArea <= 0;
}

std::vector<SystemId> unlinkedSystems(const GameState& s, SystemId from) {
    const std::vector<SystemId> linked = s.galaxy.neighbors(from);
    std::vector<SystemId> out;
    for (const StarSystem& sys : s.galaxy.systems)
        if (sys.id != from && std::find(linked.begin(), linked.end(), sys.id) == linked.end() &&
            !emptySectors(s, sys.id, true).empty())
            out.push_back(sys.id);
    return out;
}

// A ship target: one of the target empire's ships or bases. Nothing else is
// checked here: an "Any" pick applies only the steal-level and bad-chance
// tests, and the handler checks the rest on the chosen target (spec 05 §2.3,
// confirmed: binary).
bool shipCandidate(const Rules& r, const GameState& s, EmpireId owner, const Vehicle& v) { return v.owner == owner && isShip(r, s, v); }

// A planet target: one of the target empire's colonies (as above).
bool colonyCandidate(EmpireId owner, const Colony& c) { return c.owner == owner; }

// Destroys `kill` million people of a population list from the first group
// on; returns how many died.
int64_t killFromFirst(std::vector<PopulationGroup>& groups, int64_t kill) {
    int64_t killed = 0;
    for (PopulationGroup& g : groups) {
        if (kill <= 0) break;
        const int64_t k = std::min(std::max<int64_t>(0, g.millions), kill);
        g.millions -= k;
        kill -= k;
        killed += k;
    }
    std::erase_if(groups, [](const PopulationGroup& g) { return g.millions <= 0; });
    return killed;
}

// A hull-damaging hit of `damage` on the units in a cargo, as on a unit group
// (spec 04 §9.4, confirmed: binary): the pool P (at most 50,000), then up to
// 20 draws of a stack, each equally likely (a stack with none left wastes the
// draw); a unit dies when P reaches its hit points, which then leave P. What
// is left is lost. Returns the units killed.
int64_t hitCargoUnits(const Rules& r, GameState& s, Cargo& c, int64_t damage, Rng& rng) {
    int64_t pool = std::min<int64_t>(damage, combat::kMaxShotDamage);
    int64_t killed = 0;
    for (int n = 0; n < 20 && pool > 0 && !c.units.empty(); ++n) {
        UnitStack& st = c.units[rng.below(c.units.size())];
        if (st.count <= 0 || st.design.index() >= s.designs.size()) continue;
        const int64_t hp = std::max<int64_t>(1, combat::detail::unitHitPoints(r, s.design(st.design)));
        if (pool < hp) continue;
        pool -= hp;
        --st.count;
        ++s.design(st.design).lost;
        ++killed;
    }
    std::erase_if(c.units, [](const UnitStack& u) { return u.count <= 0; });
    return killed;
}

} // namespace

// ---- Tokens ---------------------------------------------------------------------------------------

std::string substitute(std::string_view text, const Tokens& t) {
    struct Entry {
        std::string_view name;
        const std::string* value;
    };
    const std::string amount = t.actualAmount ? std::to_string(*t.actualAmount) : std::string{};
    const std::array<Entry, 18> table{{
        {"SystemName", &t.systemName},
        {"SectorName", &t.sectorName},
        {"SourceEmperorName", &t.sourceEmperorName},
        {"SourceEmpireName", &t.sourceEmpireName},
        {"TargetEmperorName", &t.targetEmperorName},
        {"TargetEmpireName", &t.targetEmpireName},
        {"OtherEmperorName", &t.otherEmperorName},
        {"OtherEmpireName", &t.otherEmpireName},
        {"VehicleName", &t.vehicleName},
        {"VehicleSize", &t.vehicleSize},
        {"PlanetName", &t.planetName},
        {"DesignName", &t.designName},
        {"TechName", &t.techName},
        {"TreatyName", &t.treatyName},
        {"FacilityName", &t.facilityName},
        {"StarName", &t.starName},
        {"WarpPointName", &t.warpPointName},
        {"ActualAmount", &amount},
    }};
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const size_t open = text.find("[%", i);
        if (open == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        out.append(text.substr(i, open - i));
        const size_t close = text.find(']', open + 2);
        if (close == std::string_view::npos) {
            out.append(text.substr(open));
            break;
        }
        const std::string_view name = text.substr(open + 2, close - open - 2);
        const auto it = std::find_if(table.begin(), table.end(), [&](const Entry& e) { return keysEqual(e.name, name); });
        if (it != table.end()) out.append(*it->value);
        else out.append(text.substr(open, close - open + 1));
        i = close + 1;
    }
    return out;
}

std::string empireFullName(const Empire& e) { return e.empireType.empty() ? e.name : std::format("{} {}", e.name, e.empireType); }

std::string emperorFullName(const Empire& e) {
    if (e.leaderTitle.empty() && e.leaderName.empty()) return e.name;
    if (e.leaderTitle.empty()) return e.leaderName;
    if (e.leaderName.empty()) return e.leaderTitle;
    return std::format("{} {}", e.leaderTitle, e.leaderName);
}

void setEmpireTokens(Tokens& t, const GameState& s, EmpireId source, EmpireId target, EmpireId other) {
    if (validEmpire(s, source)) {
        t.sourceEmpireName = empireFullName(s.empire(source));
        t.sourceEmperorName = emperorFullName(s.empire(source));
    }
    if (validEmpire(s, target)) {
        t.targetEmpireName = empireFullName(s.empire(target));
        t.targetEmperorName = emperorFullName(s.empire(target));
    }
    if (validEmpire(s, other)) {
        t.otherEmpireName = empireFullName(s.empire(other));
        t.otherEmperorName = emperorFullName(s.empire(other));
    }
}

const ruleset::Message* pickMessage(std::span<const ruleset::Message> messages, Rng& rng) {
    if (messages.empty()) return nullptr;
    return &messages[rng.below(messages.size())];
}

// ---- Effect classification ---------------------------------------------------------------------

std::string_view identifier(Effect e) { return e < Effect::Count ? kEffectIds[static_cast<size_t>(e)] : "?"; }

std::optional<Effect> parseEffect(std::string_view type) {
    for (size_t i = 0; i < kEffectIds.size(); ++i)
        if (keysEqual(type, kEffectIds[i])) return static_cast<Effect>(i);
    return std::nullopt;
}

bool needsSource(Effect e) {
    switch (e) {
        case Effect::ShipRebel:
        case Effect::ShipLocations:
        case Effect::ShipConcentrations:
        case Effect::ShipConstructionInfo:
        case Effect::ShipDesignsSteal:
        case Effect::UnitDesignsSteal:
        case Effect::PlanetInfo:
        case Effect::PlanetLocations:
        case Effect::PointsSteal:
        case Effect::ResearchSteal:
        case Effect::PoliticsDisruptTrade:
        case Effect::PoliticsInterceptMessages:
        case Effect::PoliticsFakeMessages:
        case Effect::PoliticsPreventMessages:
        case Effect::PoliticsTreatyInfo:
        case Effect::SystemInfo:
        case Effect::EmpireInfo:
        case Effect::TechLevelInfo: return true;
        default: return false;
    }
}

bool isSabotage(Effect e) {
    switch (e) {
        case Effect::ShipLocations:
        case Effect::ShipConcentrations:
        case Effect::ShipConstructionInfo:
        case Effect::ShipDesignsSteal:
        case Effect::UnitDesignsSteal:
        case Effect::PlanetInfo:
        case Effect::PlanetLocations:
        case Effect::ResearchSteal:
        case Effect::PoliticsInterceptMessages:
        case Effect::PoliticsTreatyInfo:
        case Effect::SystemInfo:
        case Effect::EmpireInfo:
        case Effect::TechLevelInfo:
        case Effect::IntelligenceDefense:
        case Effect::PlanetPlagueCured:
        case Effect::PlanetCreated:
        case Effect::StarCreated:
        case Effect::WarpPointOpened: return false;
        default: return true;
    }
}

bool isBad(Effect e, int amount) {
    switch (e) {
        case Effect::ShipExperienceChange:
        case Effect::PlanetConditionsChange:
        case Effect::PlanetValueChange:
        case Effect::PlanetPopulationChange:
        case Effect::PointsChange: return amount < 0;
        case Effect::PlanetPopulationAngerChange: return amount > 0;
        default: return isSabotage(e);
    }
}

// ---- Targets -------------------------------------------------------------------------------------------

std::optional<Target> pickTarget(const Rules& r, const GameState& s, Effect e, const Target& request, Rng& rng) {
    if (!living(s, request.empire)) return std::nullopt;
    if (needsSource(e) && (!living(s, request.source) || request.source == request.empire)) return std::nullopt;
    Target t = request;
    const EmpireId owner = request.empire;
    const Empire& emp = s.empire(owner);
    // "Any" (spec 05 §2.1, §2.3, confirmed: binary): up to 1,000 draws among
    // the candidates of the right kind, and only two candidate checks: the
    // inverted Research - Steal level test and `Change Bad Intelligence
    // Chance - System` (§2.4). The handler checks the rest on the chosen
    // target; a named target is only checked to exist and belong to the
    // target empire.
    auto badIntel = [&](SystemId sys) { return chanceRejects(unownedChanceValue(s, sys, AbilityKind::ChangeBadIntelChanceSystem), rng); };

    switch (e) {
        case Effect::ShipDamage:
        case Effect::ShipLoseMovement:
        case Effect::ShipLoseSupply:
        case Effect::ShipRebel:
        case Effect::ShipExperienceChange:
        case Effect::ShipCargoDamage:
        case Effect::ShipOrdersChange:
        case Effect::ShipMoved: {
            if (request.vehicle.valid()) {
                const Vehicle* v = s.vehicle(request.vehicle);
                if (!v || !shipCandidate(r, s, owner, *v)) return std::nullopt;
                return t;
            }
            std::vector<VehicleId> ships;
            for (const Vehicle& v : s.vehicles)
                if (shipCandidate(r, s, owner, v)) ships.push_back(v.id);
            auto pick = drawCandidate(ships, rng, [&](VehicleId id) { return !badIntel(s.vehicle(id)->location.system); });
            if (!pick) return std::nullopt;
            t.vehicle = *pick;
            return t;
        }

        case Effect::PlanetConditionsChange:
        case Effect::PlanetValueChange:
        case Effect::PlanetPopulationChange:
        case Effect::PlanetPopulationAngerChange:
        case Effect::PlanetPopulationRiot:
        case Effect::PlanetPopulationRebel:
        case Effect::PlanetCargoDamage:
        case Effect::PlanetFacilityDamage:
        case Effect::PlanetInfo:
        case Effect::PlanetPlague:
        case Effect::PlanetPlagueCured: {
            if (request.object.valid()) {
                const Colony* c = s.colony(request.object);
                if (!c || !colonyCandidate(owner, *c)) return std::nullopt;
                return t;
            }
            // A homeworld is a candidate like any colony: it can rebel (confirmed: binary).
            std::vector<ObjectId> planets;
            for (const auto& c : s.colonies)
                if (c && colonyCandidate(owner, *c)) planets.push_back(c->planet);
            auto pick = drawCandidate(planets, rng, [&](ObjectId o) { return !badIntel(s.galaxy.object(o).system); });
            if (!pick) return std::nullopt;
            t.object = *pick;
            return t;
        }

        case Effect::ShipLocations:
        case Effect::ShipConcentrations:
        case Effect::ShipConstructionInfo:
        case Effect::PlanetLocations:
        case Effect::PointsChange:
        case Effect::PointsSteal:
        case Effect::EmpireInfo:
        case Effect::TechLevelInfo:
        // Design theft picks its design itself; a named target is ignored (§2.3).
        case Effect::ShipDesignsSteal:
        case Effect::UnitDesignsSteal: return t;

        case Effect::ResearchSteal: {
            if (request.tech.valid()) return validArea(r, request.tech) ? std::optional<Target>(t) : std::nullopt;
            // "Any" (confirmed: binary): the operatives keep only areas where
            // the thief is already ahead of the target, so the steal that
            // follows always fails. Reproduced on purpose.
            std::vector<ruleset::TechAreaId> areas;
            for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) areas.push_back(ruleset::TechAreaId{i});
            const Empire& thief = s.empire(request.source);
            auto pick = drawCandidate(areas, rng, [&](ruleset::TechAreaId a) { return thief.techLevel(a) > emp.techLevel(a); });
            if (!pick) return std::nullopt;
            t.tech = *pick;
            return t;
        }
        case Effect::ResearchDeleteProject:
            if (emp.research.empty()) return std::nullopt;
            return t;
        case Effect::IntelDeleteProject:
            if (emp.intel.empty()) return std::nullopt;
            return t;

        case Effect::PoliticsDisruptTrade:
        case Effect::PoliticsInterceptMessages:
        case Effect::PoliticsFakeMessages:
        case Effect::PoliticsPreventMessages:
        case Effect::PoliticsTreatyInfo: {
            // A third empire named in the order is only checked to exist; the
            // handler checks the rest. "Any" (confirmed: binary, spec 05 §2.1,
            // open question 38): the living empires the source has contact
            // with, other than the source and the target, in empire order; one
            // is drawn and no further check applies. None: the operation fails.
            if (request.other.valid()) return validEmpire(s, request.other) ? std::optional<Target>(t) : std::nullopt;
            std::vector<EmpireId> others;
            if (living(s, request.source))
                for (const Empire& o : s.empires)
                    if (o.alive && o.id != owner && o.id != request.source && s.empire(request.source).relation(o.id).contact) others.push_back(o.id);
            if (others.empty()) return std::nullopt;
            t.other = others[static_cast<size_t>(rng.below(others.size()))];
            return t;
        }

        case Effect::SystemInfo: {
            // The handler chooses the system itself (§2.3); the draw only
            // applies the bad-chance test (confirmed: binary).
            if (request.system.valid()) return validSystem(s, request.system) ? std::optional<Target>(t) : std::nullopt;
            std::vector<SystemId> systems;
            for (const StarSystem& sys : s.galaxy.systems) systems.push_back(sys.id);
            auto pick = drawCandidate(systems, rng, [&](SystemId sys) { return !badIntel(sys); });
            if (!pick) return std::nullopt;
            t.system = *pick;
            return t;
        }

        case Effect::PlanetCreated:
        case Effect::PlanetDestroyed:
        case Effect::StarDestroyed:
        case Effect::WarpPointClosed: {
            auto fits = [&](ObjectId o) {
                const SpaceObject& obj = s.galaxy.object(o);
                switch (e) {
                    case Effect::PlanetCreated: return obj.kind == ObjectKind::Asteroids;
                    case Effect::PlanetDestroyed: {
                        const Colony* c = s.colony(o);
                        return obj.kind == ObjectKind::Planet && !(c && c->homeworld);
                    }
                    case Effect::StarDestroyed: return obj.kind == ObjectKind::Star && !systemHasHomeworld(s, obj.system);
                    default: return obj.kind == ObjectKind::WarpPoint;
                }
            };
            if (request.object.valid()) {
                if (!inSystem(s, request.object) || !fits(request.object)) return std::nullopt;
                return t;
            }
            std::vector<ObjectId> options;
            for (SystemId sys : colonySystems(s, owner))
                for (ObjectId o : s.galaxy.system(sys).objects) options.push_back(o);
            auto pick = drawCandidate(options, rng, [&](ObjectId o) { return fits(o) && !badIntel(s.galaxy.object(o).system); });
            if (!pick) return std::nullopt;
            t.object = *pick;
            return t;
        }

        case Effect::StarCreated:
        case Effect::WarpPointOpened: {
            auto fits = [&](SystemId sys) {
                if (e == Effect::StarCreated) return !emptySectors(s, sys, false).empty();
                return !emptySectors(s, sys, true).empty() && !unlinkedSystems(s, sys).empty();
            };
            if (request.system.valid()) {
                if (!validSystem(s, request.system) || !fits(request.system)) return std::nullopt;
                return t;
            }
            auto pick = drawCandidate(colonySystems(s, owner), rng, [&](SystemId sys) { return fits(sys) && !badIntel(sys); });
            if (!pick) return std::nullopt;
            t.system = *pick;
            return t;
        }

        case Effect::IntelligenceDefense:
        case Effect::Count: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Location> targetLocation(const GameState& s, const Target& t) {
    if (t.vehicle.valid())
        if (const Vehicle* v = s.vehicle(t.vehicle)) return v->location;
    if (validObject(s, t.object)) return locationOf(s.galaxy, t.object);
    if (validSystem(s, t.system)) return Location{t.system, Sector{kSystemCenter, kSystemCenter}};
    return std::nullopt;
}

int64_t unownedChanceValue(const GameState& s, SystemId sys, AbilityKind k) {
    if (!validSystem(s, sys)) return 0;
    int64_t best = 0;
    auto consider = [&](const ruleset::Ability& a) {
        if (const ParsedAbility p = parseAbility(a); p.kind == k && p.value1 > best) best = p.value1;
    };
    for (const auto& a : s.galaxy.system(sys).abilities) consider(a);
    // Stellar abilities of the objects in the system; a colony's facilities
    // belong to its empire and never count.
    for (ObjectId o : s.galaxy.system(sys).objects)
        for (const auto& a : s.galaxy.object(o).abilities) consider(a);
    return best;
}

bool chanceRejects(int64_t v, Rng& rng) {
    if (v <= 0 || v == 100) return false;
    return rng.range(1, 100) <= v;
}

EmpireId breakAway(TurnContext& ctx, ObjectId planet) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const Colony* col = s.colony(planet);
    // Fewer than 20 empires; destroyed ones count, since their numbers are
    // never reused (confirmed: binary).
    if (!col || !living(s, col->owner) || s.empires.size() >= kMaxEmpires) return {};
    const EmpireId former = col->owner;
    const bool home = col->homeworld;
    const int64_t people = col->totalPopulation();
    const SpaceObject& obj = s.galaxy.object(planet);
    const SystemId system = obj.system;
    const int difficulty = ai::rebelDifficulty(s);
    Rng rng = s.rng.fork();

    // A copy of the former owner (spec 05 §2.3, confirmed: binary): race,
    // traits, characteristics, culture, technology, queues and options.
    const EmpireId id{s.empires.size()};
    Empire e = s.empire(former);
    e.id = id;
    e.alive = true;
    // Named after the system, or a random empire name when another empire has that name.
    auto taken = [&](std::string_view name) {
        return std::any_of(s.empires.begin(), s.empires.end(), [&](const Empire& x) { return x.name == name; });
    };
    e.name = s.galaxy.system(system).name;
    const auto& names = r.data().names;
    if (taken(e.name) && !names.empireNames.empty()) e.name = names.empireNames[rng.below(names.empireNames.size())];
    // A new leader name and the pictures of an unused neutral race (the
    // former owner's when none is left, inferred, spec 05 open question 41).
    if (!names.emperorNames.empty()) e.leaderName = names.emperorNames[rng.below(names.emperorNames.size())];
    std::vector<const ruleset::RacePreset*> pictures;
    for (const ruleset::RacePreset& p : r.racePresets())
        if (p.neutral && std::none_of(s.empires.begin(), s.empires.end(), [&](const Empire& x) { return datafile::keysEqual(x.race.style, p.folder); }))
            pictures.push_back(&p);
    if (!pictures.empty()) e.race.style = pictures[rng.below(pictures.size())]->folder;
    // Its home planet type and atmosphere are the planet's.
    if (!obj.surface.empty()) e.race.nativeSurface = obj.surface;
    if (!obj.atmosphere.empty()) e.race.atmosphere = obj.atmosphere;
    e.color = defaultEmpireColor(id.index());
    // Computer controlled with all ministers on, not neutral, at the highest
    // computer difficulty (spec 05 §7.1).
    e.kind = PlayerKind::Computer;
    e.passwordHash.clear();
    e.ministerAll = true;
    e.aiMinimalChanges = false;
    e.aiDifficulty = difficulty;
    e.aiState = 0;
    e.aiTurnsInState = 0;
    e.aiMemory = AiMemory{};
    e.ministerStyle.clear();  // a rebel empire always gets an empty style (spec 02 §10)
    e.politicsMark = PoliticsMark{};
    e.experience = 0;
    // Its own things start empty: no designs, no contact with anyone (its
    // treaties all "no contact"), an empty log and record.
    e.designs.clear();
    e.stockpile = {};
    e.economy = EconomyReport{};
    e.researchPool = e.intelPool = 0;
    e.relations.clear();
    e.log.clear();
    e.historyEvents.clear();
    e.history.clear();
    e.claimedSystems = {system};
    e.homeSystem = system;  // the capital's system, never moved (spec 02 §2)
    e.colonyTypeChoices.clear();
    e.systemsToAvoid.clear();
    e.taggedMinefields.clear();
    e.waypoints = {};
    // It has explored only its own system (every system under the option
    // that shows them all).
    const uint8_t seen = s.options.allSystemsSeen ? 1 : 0;
    e.knowledge = Knowledge{};
    e.knowledge.explored.assign(s.galaxy.systems.size(), seen);
    e.knowledge.explored[system.index()] = 1;
    e.knowledge.present.assign(s.galaxy.systems.size(), 0);
    e.knowledge.lastSeen.assign(s.galaxy.systems.size(), 0);
    e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), seen);
    e.knowledge.notes.assign(s.galaxy.systems.size(), {});
    s.empires.push_back(std::move(e));
    for (Empire& x : s.empires) x.relations.resize(s.empires.size());

    ctx.mood(former, "Any Planet Lost");
    if (home) ctx.mood(former, "Homeworld Lost");
    diplomacy::transferColony(s, planet, id);
    Colony& c = *s.colony(planet);
    // The whole population, of every race, becomes one group of its own
    // people; the colony becomes a Homeworld and capital with anger 25.
    c.population.clear();
    if (people > 0) c.population.push_back({id, people});
    c.homeworld = true;
    c.anger = std::min(kRebelAnger, c.maxAnger());
    // Each of its five stocks starts at 4 × its production of that kind.
    const diplomacy::Generated made = diplomacy::generated(r, s, id);
    Empire& rebel = s.empire(id);
    for (Resource res : kResources) rebel.stockpile[res] = std::clamp<int64_t>(made.resources[res] * 4, 0, research::kPoolCap);
    rebel.researchPool = std::clamp<int64_t>(made.research * 4, 0, research::kPoolCap);
    rebel.intelPool = std::clamp<int64_t>(made.intelligence * 4, 0, research::kPoolCap);
    return id;
}

// ---- Damage helpers ------------------------------------------------------------------------------------

int64_t damageVehicle(const Rules& r, GameState& s, Vehicle& v, int64_t amount, Rng& rng) {
    // A unit group loses whole units (movement::damageUnitGroup records them).
    if (isUnitType(vehicleType(r, s, v))) return movement::damageUnitGroup(r, s, v, amount, rng);
    const Design& d = s.design(v.design);
    if (v.damage.size() < d.entries.size()) v.damage.resize(d.entries.size(), 0);
    int64_t left = std::max<int64_t>(0, amount);
    int64_t applied = 0;
    auto hit = [&](size_t i) {
        const int64_t room = entryStructure(r, d, i) - v.damage[i];
        if (room <= 0 || left <= 0) return;
        const int64_t take = std::min(room, left);
        v.damage[i] += static_cast<int>(take);
        left -= take;
        applied += take;
    };
    auto armor = [&](size_t i) { return hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::Armor); };
    for (size_t i = 0; i < d.entries.size() && left > 0; ++i)
        if (armor(i)) hit(i);
    while (left > 0) {
        std::vector<size_t> intact;
        for (size_t i = 0; i < d.entries.size(); ++i)
            if (!armor(i) && entryIntact(r, s, v, i)) intact.push_back(i);
        if (intact.empty()) break;
        hit(intact[rng.below(intact.size())]);
    }
    // Storage lost to the damage takes supply and cargo with it (spec 03 §7, §11).
    if (applied > 0 && !vehicleDestroyed(r, s, v)) movement::fitToCapacity(r, s, v);
    return applied;
}

// ---- Application ---------------------------------------------------------------------------------------

Outcome apply(TurnContext& ctx, Effect e, const Target& t, int amount, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Outcome out;
    // The affected empire; events on objects nobody owns have none. Effects on
    // an empire's stock, queues or relations need one.
    Empire* victim = living(s, t.empire) ? &s.empire(t.empire) : nullptr;

    // Ship effects.
    Vehicle* v = t.vehicle.valid() ? s.vehicle(t.vehicle) : nullptr;
    if (v && v->count <= 0) v = nullptr;
    if (v) setVehicleTokens(out.tokens, r, s, *v);
    // Planet effects.
    Colony* col = validObject(s, t.object) ? s.colony(t.object) : nullptr;
    if (validObject(s, t.object)) setObjectTokens(out.tokens, s, t.object);
    if (validSystem(s, t.system) && !validObject(s, t.object) && !v)
        setLocationTokens(out.tokens, s, Location{t.system, Sector{kSystemCenter, kSystemCenter}});

    switch (e) {
        case Effect::ShipDamage:
            if (!v) return out;
            out.actual = damageVehicle(r, s, *v, amount, rng);
            if (vehicleDestroyed(r, s, *v)) destroyVehicle(ctx, *v);
            break;
        case Effect::ShipLoseMovement:
            // Movement points of the current turn only, never below 0 (confirmed:
            // binary). They are refilled when movement starts, so after the
            // movement phases this changes nothing.
            if (!v) return out;
            out.actual = std::clamp<int64_t>(amount, 0, std::max(0, v->movement));
            v->movement -= static_cast<int>(out.actual);
            break;
        case Effect::ShipLoseSupply:
            // Nothing happens to unlimited supply (spec 03 §19 Q57, confirmed: binary).
            if (!v || vehicleHasUnlimitedSupply(r, s, *v)) return out;
            out.actual = std::clamp<int64_t>(amount, 0, std::max<int64_t>(0, v->supply));
            v->supply -= out.actual;
            break;
        case Effect::ShipRebel:
            if (!v || !living(s, t.source)) return out;
            ctx.mood(v->owner, "Any Ship Lost");
            diplomacy::transferVehicle(s, v->id, t.source);
            out.actual = 1;
            break;
        case Effect::ShipExperienceChange: {
            if (!v) return out;
            const int before = v->experience;
            v->experience = std::max(0, before + amount);
            out.actual = v->experience - before;
            break;
        }
        case Effect::ShipCargoDamage:
            // The whole cargo, every person and unit, is destroyed; Amount is
            // not used, and the message shows its absolute value (confirmed:
            // binary).
            if (!v || v->cargo.empty()) return out;
            for (const UnitStack& u : v->cargo.units)
                if (u.design.index() < s.designs.size()) s.design(u.design).lost += u.count;
            v->cargo = Cargo{};
            out.actual = amount < 0 ? -int64_t{amount} : int64_t{amount};
            break;
        case Effect::ShipOrdersChange: {
            // One order to move to a random system of the quadrant (confirmed:
            // binary; the sector is random, inferred). Mothballed ships are immune.
            if (!v || v->status == VehicleStatus::Mothballed || s.galaxy.systems.empty()) return out;
            detachFromFleet(s, *v);
            Order o;
            o.kind = OrderKind::MoveTo;
            const SystemId dest{static_cast<uint32_t>(rng.below(s.galaxy.systems.size()))};
            o.location = {dest, Sector{static_cast<int>(rng.below(kSystemSize)), static_cast<int>(rng.below(kSystemSize))}};
            v->orders = {o};
            v->repeatOrders = false;
            out.actual = 1;
            break;
        }
        case Effect::ShipMoved: {
            // To a random system of the quadrant, at a random sector, orders
            // cleared; Amount is not used (confirmed: binary).
            if (!v || s.galaxy.systems.empty()) return out;
            const SystemId dest{static_cast<uint32_t>(rng.below(s.galaxy.systems.size()))};
            const Sector sector{static_cast<int>(rng.below(kSystemSize)), static_cast<int>(rng.below(kSystemSize))};
            detachFromFleet(s, *v);
            v->location = {dest, sector};
            v->orders.clear();
            v->repeatOrders = false;
            explore(s, v->owner, dest);
            setLocationTokens(out.tokens, s, v->location);
            out.actual = 1;
            break;
        }

        case Effect::ShipLocations: {
            for (const Vehicle& x : s.vehicles)
                if (x.owner == t.empire && isShip(r, s, x)) {
                    out.report.push_back(std::format("{} ({}) in {} at ({}, {})", x.name, r.hull(s.design(x.design).hull).name,
                                                     s.galaxy.system(x.location.system).name, x.location.sector.x, x.location.sector.y));
                    ++out.actual;
                }
            if (out.report.empty()) out.report.push_back("They have no ships.");
            break;
        }
        case Effect::ShipConcentrations: {
            std::map<SystemId, int> count;
            for (const Vehicle& x : s.vehicles)
                if (x.owner == t.empire && isShip(r, s, x)) ++count[x.location.system];
            std::vector<std::pair<SystemId, int>> rows(count.begin(), count.end());
            std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [sys, n] : rows) {
                out.report.push_back(std::format("{}: {} ship{}", s.galaxy.system(sys).name, n, n == 1 ? "" : "s"));
                explore(s, t.source, sys);
            }
            if (out.report.empty()) out.report.push_back("They have no ships.");
            out.actual = static_cast<int64_t>(rows.size());
            break;
        }
        case Effect::ShipConstructionInfo: {
            std::vector<std::pair<ObjectId, int>> rows;
            for (const auto& c : s.colonies) {
                if (!c || c->owner != t.empire) continue;
                int n = 0;
                for (const QueueItem& q : c->queue.items)
                    if (q.kind == QueueItem::Kind::Vehicle) n += q.count;
                if (n > 0) rows.emplace_back(c->planet, n);
            }
            std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [planet, n] : rows) {
                std::string items;
                for (const QueueItem& q : s.colony(planet)->queue.items)
                    if (q.kind == QueueItem::Kind::Vehicle && q.design.index() < s.designs.size())
                        items += std::format("{}{} x{}", items.empty() ? "" : ", ", s.design(q.design).name, q.count);
                out.report.push_back(std::format("{}: {}", s.galaxy.object(planet).name, items));
            }
            if (out.report.empty()) out.report.push_back("No vehicles are under construction.");
            out.actual = static_cast<int64_t>(rows.size());
            break;
        }
        case Effect::ShipDesignsSteal:
        case Effect::UnitDesignsSteal: {
            // The thief learns exactly one design of the target: the newest of
            // the right class (ship or base, or a unit type) that has been
            // built at least once, that the thief does not know and that its
            // owner can still build. It is dated as seen this turn and is not
            // copied into the thief's designs; with no such design the
            // operation fails (spec 05 §2.3, §8, confirmed: binary). "Built at
            // least once" reads Design::built (inferred, spec 05 open question 41).
            if (!victim || !living(s, t.source)) return out;
            const bool units = e == Effect::UnitDesignsSteal;
            const Knowledge& known = s.empire(t.source).knowledge;
            std::optional<DesignId> newest;
            for (DesignId d : designsOfClass(r, s, t.empire, units)) {
                const Design& design = s.design(d);
                if (design.built <= 0 || knowsDesign(known, d) || !r.designTechnology(*victim, design)) continue;
                if (!newest || d > *newest) newest = d;   // designs are numbered in creation order
            }
            if (!newest) return out;
            seeDesign(s.empire(t.source).knowledge, *newest, s.turn);
            out.tokens.designName = s.design(*newest).name;
            out.report.push_back(std::format("We now know the {} {} design.", s.design(*newest).name, designClassName(units)));
            out.actual = 1;
            break;
        }

        case Effect::PlanetConditionsChange: {
            // Any planet, colonized or not: conditions + Amount / 10 on the
            // 0–1.5 scale (Amount in tenths), kept within the scale (spec 05
            // §2.3, confirmed: binary). The planet sends no notice of its own;
            // the record's messages go out as for any other effect (open
            // question 39). `actual` is the change in hundredths.
            if (!validObject(s, t.object)) return out;
            SpaceObject& obj = s.galaxy.object(t.object);
            if (obj.kind != ObjectKind::Planet && obj.kind != ObjectKind::Asteroids) return out;
            const Conditions before = obj.conditions;
            obj.conditions = conditionsPlus(before, xmath::Ext(amount) / xmath::Ext(10));
            out.actual = obj.conditions.inHundredths() - before.inHundredths();
            break;
        }
        case Effect::PlanetValueChange: {
            // Each of the three values changes by Amount; in finite-resource
            // games by Amount × 1,000 when that is strictly between −500,000
            // and +500,000, otherwise by Amount. A result below 0 becomes 0;
            // any other result is pulled into the `Minimum/Maximum Planet
            // Percent/Resource Value` limits (spec 05 §2.3, confirmed: binary).
            if (!validObject(s, t.object)) return out;
            SpaceObject& obj = s.galaxy.object(t.object);
            if (obj.kind != ObjectKind::Planet && obj.kind != ObjectKind::Asteroids) return out;
            const bool finite = s.options.finiteResources;
            const int64_t thousand = int64_t{amount} * 1000;
            const int64_t change = finite && thousand > -500'000 && thousand < 500'000 ? thousand : int64_t{amount};
            const int64_t lo = r.setting(finite ? "Minimum Planet Resource Value" : "Minimum Planet Percent Value", 0);
            const int64_t hi = r.setting(finite ? "Maximum Planet Resource Value" : "Maximum Planet Percent Value", finite ? 999'000'000 : 250);
            int64_t delta = 0;
            for (int& value : obj.value) {
                const int before = value;
                const int64_t result = int64_t{value} + change;
                value = static_cast<int>(result < 0 ? 0 : std::clamp<int64_t>(result, lo, std::max(lo, hi)));
                delta += value - before;
            }
            out.actual = delta / static_cast<int64_t>(obj.value.size());
            break;
        }
        case Effect::PlanetPopulationChange: {
            // With s = trunc(|Amount| / 5) the change is Amount + d, d uniform
            // from −s to +s. A loss comes from the groups in list order; a
            // gain goes only to an existing group of the owner's race, up to
            // the planet's maximum; no group is created. The message shows the
            // absolute change (spec 05 §2.3, confirmed: binary).
            if (!col) return out;
            const SystemId sys = s.galaxy.object(t.object).system;
            const int64_t spread = (amount < 0 ? -int64_t{amount} : int64_t{amount}) / 5;
            const int64_t change = int64_t{amount} + (spread > 0 ? rng.range(-spread, spread) : 0);
            if (change < 0) {
                out.actual = killFromFirst(col->population, -change);
                if (out.actual > 0) ctx.mood(col->owner, "1M Population Killed", sys, t.object, static_cast<int>(std::min<int64_t>(out.actual, INT32_MAX)));
                // A colony whose people are all gone dies out (spec 02 §2).
                if (out.actual > 0 && col->totalPopulation() <= 0) economy::colonyDiesOut(ctx, t.object);
            } else if (change > 0) {
                auto it = std::find_if(col->population.begin(), col->population.end(), [&](const PopulationGroup& p) { return p.race == col->owner; });
                if (it != col->population.end()) {
                    const int64_t room = std::max<int64_t>(0, maxPopulation(r, s, *col) - col->totalPopulation());
                    const int64_t add = std::min(change, room);
                    it->millions += add;
                    out.actual = add;
                }
            }
            break;
        }
        case Effect::PlanetPopulationAngerChange: {
            // Needs a colony whose owner's race is not `Population
            // Emotionless`; anger changes by Amount in whole percent, within 0
            // and 100, at most 80 on a capital (spec 05 §2.3, confirmed: binary).
            if (!col || emotionless(r, s, *col)) return out;
            const int before = col->anger;
            col->anger = static_cast<int>(std::clamp<int64_t>(int64_t{before} + amount, 0, col->maxAnger()));
            out.actual = col->anger - before;
            break;
        }
        case Effect::PlanetPopulationRiot: {
            if (!col || emotionless(r, s, *col)) return out;
            const int before = col->anger;
            col->anger = col->maxAnger();  // 100, or 80 on a capital (spec 02 §4)
            out.actual = col->anger - before;
            break;
        }
        case Effect::PlanetPopulationRebel: {
            // As an event the colony breaks away as a new empire. As an
            // intelligence project: a roll of 1–4 equal to 1 breaks it away,
            // else a second such roll makes it join the source, else nothing
            // happens: 25 / 18.75 / 56.25 %. When the 20-empire limit blocks
            // the break-away, nothing happens and no roll is made (confirmed:
            // binary). `victim` is not used past this point: a new empire
            // invalidates references into GameState::empires.
            if (!col || s.empires.size() >= kMaxEmpires) return out;
            const EmpireId owner = col->owner;
            const bool home = col->homeworld;
            const bool intel = living(s, t.source);
            if (!intel || rng.range(1, 4) == 1) {
                if (!breakAway(ctx, t.object).valid()) return out;
            } else if (rng.range(1, 4) == 1) {
                diplomacy::transferColony(s, t.object, t.source);
                ctx.mood(owner, "Any Planet Lost");
                if (home) ctx.mood(owner, "Homeworld Lost");
            } else {
                return out;
            }
            out.actual = 1;
            break;
        }
        case Effect::PlanetCargoDamage: {
            // Nothing when Amount <= 0 or the cargo is empty. Amount damage
            // points strike the people aboard first, killing Amount ÷ `Damage
            // Points To Kill One Population` million (truncated) from the
            // first group on; when fewer are aboard all die, the damage they
            // absorbed is subtracted and the rest is a hull-damaging hit on
            // the units (spec 05 §2.3, spec 04 §9.4, confirmed: binary).
            if (!col || amount <= 0 || col->cargo.empty()) return out;
            const int64_t perMillion = std::max<int64_t>(1, r.setting("Damage Points To Kill One Population", 10));
            const int64_t aboard = col->cargo.totalPopulation();
            const int64_t kill = int64_t{amount} / perMillion;
            if (aboard >= kill) {
                out.actual = killFromFirst(col->cargo.population, kill);
            } else {
                col->cargo.population.clear();
                out.actual = aboard + hitCargoUnits(r, s, col->cargo, int64_t{amount} - aboard * perMillion, rng);
            }
            break;
        }
        case Effect::PlanetFacilityDamage: {
            // n = min(Amount, facilities), nothing when n <= 0. Each of the n
            // draws picks a kind with a chance in proportion to how many of it
            // the planet had at the start; a kind with none left is drawn
            // again (spec 05 §2.3, confirmed: binary).
            if (!col) return out;
            const int64_t n = std::min<int64_t>(amount, static_cast<int64_t>(col->facilities.size()));
            if (n <= 0) return out;
            const std::vector<uint32_t> start = col->facilities;
            for (int64_t k = 0; k < n && !col->facilities.empty(); ++k) {
                for (;;) {
                    const uint32_t kind = start[rng.below(start.size())];
                    const auto it = std::find(col->facilities.begin(), col->facilities.end(), kind);
                    if (it == col->facilities.end()) continue;
                    out.tokens.facilityName = r.facility(kind).name;
                    col->facilities.erase(it);
                    break;
                }
            }
            out.actual = n;
            break;
        }
        case Effect::PlanetInfo: {
            if (!col) return out;
            const SpaceObject& obj = s.galaxy.object(t.object);
            explore(s, t.source, obj.system);
            out.report.push_back(std::format("{}: {} {}, atmosphere {}, conditions {}", obj.name, obj.size, obj.surface, obj.atmosphere,
                                             economy::conditionsName(economy::conditionsBand(obj.conditions))));
            out.report.push_back(std::format("Value: minerals {}, organics {}, radioactives {}", obj.value[0], obj.value[1], obj.value[2]));
            for (const PopulationGroup& p : col->population)
                out.report.push_back(std::format("Population: {}M {}", p.millions,
                                                 validEmpire(s, p.race) ? s.empire(p.race).race.name : std::string("unknown")));
            out.report.push_back(std::format("Mood: {}", economy::moodName(r, s, *col)));
            std::map<uint32_t, int> facilities;
            for (uint32_t f : col->facilities) ++facilities[f];
            for (const auto& [f, n] : facilities) out.report.push_back(std::format("Facility: {} x{}", r.facility(f).name, n));
            for (const UnitStack& u : col->cargo.units)
                if (u.design.index() < s.designs.size()) out.report.push_back(std::format("Cargo: {} x{}", s.design(u.design).name, u.count));
            for (const PopulationGroup& p : col->cargo.population) out.report.push_back(std::format("Cargo: {}M population", p.millions));
            out.actual = 1;
            break;
        }
        case Effect::PlanetLocations: {
            for (const auto& c : s.colonies)
                if (c && c->owner == t.empire) {
                    const SpaceObject& obj = s.galaxy.object(c->planet);
                    out.report.push_back(std::format("{} in the {} system", obj.name, s.galaxy.system(obj.system).name));
                    explore(s, t.source, obj.system);
                    ++out.actual;
                }
            if (out.report.empty()) out.report.push_back("They have no colonies.");
            break;
        }
        case Effect::PlanetPlague: {
            if (!col || plagueImmune(r, s, *col)) return out;
            const int level = std::max(1, amount);
            if (plagueProtection(r, s, *col) >= level) return out;
            col->plagueLevel = std::max(col->plagueLevel, level);
            ctx.mood(col->owner, "Planet Plagued", s.galaxy.object(t.object).system, t.object);
            out.actual = level;
            break;
        }
        case Effect::PlanetPlagueCured:
            if (!col || col->plagueLevel <= 0) return out;
            out.actual = col->plagueLevel;
            col->plagueLevel = 0;
            break;

        case Effect::PlanetCreated: {
            if (!validObject(s, t.object)) return out;
            SpaceObject& obj = s.galaxy.object(t.object);
            if (obj.kind != ObjectKind::Asteroids) return out;
            obj.kind = ObjectKind::Planet;
            if (auto st = pickSectorType(r, "Planet", obj.size, rng)) applySectorType(r, obj, *st);
            out.actual = 1;
            break;
        }
        case Effect::PlanetDestroyed: {
            // The result of the Destroy Planet manipulation (spec 05 §4, spec 01 §9).
            if (!validObject(s, t.object) || s.galaxy.object(t.object).kind != ObjectKind::Planet) return out;
            movement::destroyPlanet(ctx, t.object, "The planet was destroyed.", rng);
            out.actual = 1;
            break;
        }
        case Effect::StarCreated: {
            if (!validSystem(s, t.system)) return out;
            auto sector = choose(emptySectors(s, t.system, false), rng);
            if (!sector) return out;
            SpaceObject star;
            star.kind = ObjectKind::Star;
            star.sector = *sector;
            int stars = 0;
            for (ObjectId o : s.galaxy.system(t.system).objects)
                if (s.galaxy.object(o).kind == ObjectKind::Star || s.galaxy.object(o).kind == ObjectKind::DestroyedStar) ++stars;
            star.name = std::format("{} {}", s.galaxy.system(t.system).name, static_cast<char>('A' + std::min(stars, 25)));
            if (auto st = pickSectorType(r, "Star", {}, rng)) applySectorType(r, star, *st);
            const ObjectId id = addObject(s, t.system, std::move(star));
            setObjectTokens(out.tokens, s, id);
            out.actual = 1;
            break;
        }
        case Effect::StarDestroyed: {
            // The result of the Destroy Star manipulation (spec 05 §4, spec 01
            // §9, confirmed: binary): the shockwave.
            if (!inSystem(s, t.object) || s.galaxy.object(t.object).kind != ObjectKind::Star) return out;
            movement::destroyStar(ctx, t.object, "The star exploded.", rng);
            out.actual = 1;
            break;
        }
        case Effect::WarpPointOpened: {
            if (!validSystem(s, t.system)) return out;
            auto dest = choose(unlinkedSystems(s, t.system), rng);
            auto here = choose(emptySectors(s, t.system, true), rng);
            if (!dest || !here) return out;
            auto there = choose(emptySectors(s, *dest, true), rng);
            if (!there) return out;
            auto makeWp = [&](SystemId sys, Sector sec) {
                SpaceObject wp;
                wp.kind = ObjectKind::WarpPoint;
                wp.sector = sec;
                wp.name = std::format("{} Warp Point {}", s.galaxy.system(sys).name, s.galaxy.warpPoints(sys).size() + 1);
                if (auto st = pickSectorType(r, "Warp Point", {}, rng)) wp.sectorType = *st;
                return addObject(s, sys, std::move(wp));
            };
            const ObjectId a = makeWp(t.system, *here);
            const ObjectId b = makeWp(*dest, *there);
            s.galaxy.object(a).destination = b;
            s.galaxy.object(b).destination = a;
            setObjectTokens(out.tokens, s, a);
            out.actual = 1;
            break;
        }
        case Effect::WarpPointClosed: {
            // The result of the Close Warp Point manipulation: both ends disappear (spec 01 §8, §9).
            if (!validObject(s, t.object) || s.galaxy.object(t.object).kind != ObjectKind::WarpPoint) return out;
            movement::closeWarpPoint(s, t.object);
            out.actual = 1;
            break;
        }

        case Effect::PointsChange:
            // Each of the three resources changes by Amount, never below 0.
            // Research and intelligence points are not touched.
            if (!victim) return out;
            for (Resource res : kResources) {
                const int64_t before = victim->stockpile[res];
                victim->stockpile[res] = std::max<int64_t>(0, before + amount);
                out.actual += before > victim->stockpile[res] ? before - victim->stockpile[res] : victim->stockpile[res] - before;
            }
            break;
        case Effect::PointsSteal: {
            if (!victim || !living(s, t.source)) return out;
            Empire& thief = s.empire(t.source);
            const int64_t want = amount < 0 ? -int64_t{amount} : int64_t{amount};
            for (Resource res : kResources) {
                const int64_t take = std::clamp<int64_t>(victim->stockpile[res], 0, want);
                victim->stockpile[res] -= take;
                thief.stockpile[res] += take;
                out.actual += take;
            }
            break;
        }

        case Effect::ResearchSteal: {
            // The area must be one where the target is ahead of us and that is
            // neither racial nor unique; we gain exactly one level (confirmed:
            // binary).
            if (!victim || !living(s, t.source) || !validArea(r, t.tech)) return out;
            out.tokens.techName = r.tech(t.tech).name;
            if (!stealable(r, s, t.source, t.empire, t.tech)) return out;
            const int before = s.empire(t.source).techLevel(t.tech);
            research::grantLevel(ctx, t.source, t.tech, before + 1, "espionage");
            if (s.empire(t.source).techLevel(t.tech) == before) return out;
            out.actual = s.empire(t.source).techLevel(t.tech);
            break;
        }
        case Effect::ResearchDeleteProject: {
            if (!victim || victim->research.empty()) return out;
            const size_t idx = rng.below(victim->research.size());
            out.tokens.techName = r.tech(victim->research[idx].area).name;
            out.actual = victim->research[idx].progress;
            victim->research.erase(victim->research.begin() + static_cast<std::ptrdiff_t>(idx));
            break;
        }
        case Effect::IntelDeleteProject: {
            // Can remove a defense project too.
            if (!victim || victim->intel.empty()) return out;
            const size_t idx = rng.below(victim->intel.size());
            out.actual = victim->intel[idx].progress;
            victim->intel.erase(victim->intel.begin() + static_cast<std::ptrdiff_t>(idx));
            break;
        }

        case Effect::PoliticsDisruptTrade: {
            // Running trade between the two restarts its counter at 0 (confirmed: binary).
            if (!victim || !living(s, t.other) || !treatyTradesResources(victim->relation(t.other).treaty)) return out;
            Relation& a = victim->relation(t.other);
            Relation& b = s.empire(t.other).relation(t.empire);
            out.actual = diplomacy::tradePercent(r, s, t.empire, t.other);
            a.tradeTurns = b.tradeTurns = 0;
            break;
        }
        case Effect::PoliticsInterceptMessages: {
            // The latest political message between the two from the last two
            // turns; with none, the operation fails (confirmed: binary).
            if (!victim || !living(s, t.other)) return out;
            const DiplomaticMessage* latest = nullptr;
            for (const DiplomaticMessage& m : s.messages) {
                const bool between = (m.from == t.empire && m.to == t.other) || (m.from == t.other && m.to == t.empire);
                if (!between || m.sentTurn + 1 < s.turn) continue;
                if (!latest || m.sentTurn > latest->sentTurn || (m.sentTurn == latest->sentTurn && m.id > latest->id)) latest = &m;
            }
            if (!latest) return out;
            out.report.push_back(std::format("{} from the {} to the {}{}{}", displayName(latest->type), empireFullName(s.empire(latest->from)),
                                             empireFullName(s.empire(latest->to)), latest->text.empty() ? "" : ": ", latest->text));
            out.actual = 1;
            break;
        }
        case Effect::PoliticsFakeMessages: {
            // A declaration of war sent in the target's name, and it takes
            // effect: the two are at War (confirmed: binary).
            if (!victim || !living(s, t.other)) return out;
            DiplomaticMessage m;
            m.id = MessageId{s.nextMessageId++};
            m.from = t.empire;
            m.to = t.other;
            m.sentTurn = s.turn;
            m.type = MessageType::DeclareWar;
            m.tone = 2;
            m.delivered = m.answered = true;
            s.messages.push_back(m);
            diplomacy::declareWar(ctx, t.empire, t.other);
            out.actual = 1;
            break;
        }
        case Effect::PoliticsPreventMessages: {
            // The political messages of the last two turns between the two are
            // deleted, so they are never answered; nothing is blocked afterwards
            // (confirmed: binary).
            if (!victim || !living(s, t.other)) return out;
            const size_t before = s.messages.size();
            std::erase_if(s.messages, [&](const DiplomaticMessage& m) {
                const bool between = (m.from == t.empire && m.to == t.other) || (m.from == t.other && m.to == t.empire);
                return between && m.sentTurn + 1 >= s.turn;
            });
            out.actual = static_cast<int64_t>(before - s.messages.size());
            break;
        }
        case Effect::PoliticsTreatyInfo: {
            if (!victim || !living(s, t.other)) return out;
            const Relation& rel = victim->relation(t.other);
            out.tokens.treatyName = std::string(displayName(rel.treaty));
            out.report.push_back(std::format("The {} and the {}: {}", empireFullName(*victim), empireFullName(s.empire(t.other)),
                                             displayName(rel.treaty)));
            out.actual = 1;
            break;
        }
        case Effect::SystemInfo: {
            // The highest-numbered system the target has explored and the
            // thief has not becomes explored for the thief (with the target's
            // warp links there, inferred); with none, the operation fails. The
            // system drawn for the order is not used (confirmed: binary).
            if (!victim || !living(s, t.source)) return out;
            Empire& src = s.empire(t.source);
            std::optional<SystemId> chosen;
            for (const StarSystem& sys : s.galaxy.systems)
                if (victim->hasExplored(sys.id) && !src.hasExplored(sys.id)) chosen = sys.id;
            if (!chosen) return out;
            explore(s, t.source, *chosen);
            for (ObjectId wp : s.galaxy.warpPoints(*chosen))
                if (wp.index() < victim->knowledge.knownWarpLink.size() && victim->knowledge.knownWarpLink[wp.index()] &&
                    wp.index() < src.knowledge.knownWarpLink.size())
                    src.knowledge.knownWarpLink[wp.index()] = 1;
            setLocationTokens(out.tokens, s, Location{*chosen, Sector{kSystemCenter, kSystemCenter}});
            out.actual = 1;
            break;
        }
        case Effect::EmpireInfo: {
            if (!victim) return out;
            const TurnStats st = score::currentStats(r, s, t.empire);
            out.report.push_back(std::format("Stored resources: {} minerals, {} organics, {} radioactives", victim->stockpile[Resource::Minerals],
                                             victim->stockpile[Resource::Organics], victim->stockpile[Resource::Radioactives]));
            out.report.push_back(std::format("Production per turn: {} minerals, {} organics, {} radioactives", st.production[Resource::Minerals],
                                             st.production[Resource::Organics], st.production[Resource::Radioactives]));
            out.report.push_back(std::format("Research {} and intelligence {} per turn", st.research, st.intelligence));
            out.report.push_back(std::format("{} planets in {} systems, {}M population", st.planets, st.systems, st.population));
            out.report.push_back(std::format("{} ships, {} bases, {} units", st.ships, st.bases, st.units));
            out.report.push_back(std::format("{} technology levels, score {}", st.techLevels, st.score));
            out.actual = st.score;
            break;
        }
        case Effect::TechLevelInfo: {
            if (!victim) return out;
            for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
                const int level = victim->techLevel(ruleset::TechAreaId{i});
                if (level > 0) out.report.push_back(std::format("{}: level {}", r.data().techAreas[i].name, level));
            }
            if (out.report.empty()) out.report.push_back("They know no technology.");
            out.actual = research::totalLevels(r, *victim);
            break;
        }

        case Effect::IntelligenceDefense:
        case Effect::Count: return out;
    }
    out.applied = true;
    return out;
}

void detachFromFleet(GameState& s, Vehicle& v) {
    if (!v.fleet.valid()) return;
    if (Fleet* f = s.fleet(v.fleet)) {
        std::erase(f->members, v.id);
        if (f->leader == v.id) f->leader = f->members.empty() ? VehicleId{} : f->members.front();
    }
    v.fleet = FleetId{};
    std::erase_if(s.fleets, [](const Fleet& f) { return f.members.empty(); });
}

} // namespace opense4::game::effects

// ==== Random events ==================================================================================

namespace opense4::game::events {

namespace {

using effects::Effect;
using effects::Target;

bool validEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }
bool living(const GameState& s, EmpireId e) { return validEmpire(s, e) && s.empire(e).alive; }

// An object that is still part of its system.
bool inSystem(const GameState& s, ObjectId o) {
    if (!o.valid() || o.index() >= s.galaxy.objects.size()) return false;
    const SpaceObject& obj = s.galaxy.object(o);
    if (!obj.system.valid() || obj.system.index() >= s.galaxy.systems.size()) return false;
    const auto& objs = s.galaxy.system(obj.system).objects;
    return std::find(objs.begin(), objs.end(), o) != objs.end();
}

// An empire's home planet location, system and sector (spec 05 §4): where a
// capital colony (Colony::homeworld) lies (inferred: the engine records only
// the home system, Empire::homeSystem, not the sector; spec 05 open question 42).
bool atHomeLocation(const GameState& s, Location where) {
    for (const auto& c : s.colonies)
        if (c && c->homeworld && inSystem(s, c->planet) && locationOf(s.galaxy, c->planet) == where) return true;
    return false;
}

// Empires that hear about an event under its `Message To` setting.
std::vector<EmpireId> recipients(const GameState& s, const ruleset::EventType& ev, const Target& t, std::optional<Location> where) {
    std::vector<EmpireId> out;
    auto add = [&](EmpireId e) {
        if (living(s, e) && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
    };
    const std::string& to = ev.messageTo;
    if (datafile::keysEqual(to, "None")) return out;
    if (datafile::keysEqual(to, "All")) {
        for (const Empire& e : s.empires) add(e.id);
        return out;
    }
    add(t.empire);
    if (!where) return out;
    const bool sector = datafile::keysEqual(to, "Sector");
    const bool system = datafile::keysEqual(to, "System");
    if (!sector && !system) return out;
    auto here = [&](Location l) { return l.system == where->system && (!sector || l.sector == where->sector); };
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && here(v.location)) add(v.owner);
    for (const auto& c : s.colonies)
        if (c && here(locationOf(s.galaxy, c->planet))) add(c->owner);
    std::sort(out.begin(), out.end());
    return out;
}

void sendMessages(TurnContext& ctx, const ruleset::EventType& ev, std::span<const ruleset::Message> messages, const Target& t,
                  const effects::Tokens& tokens, std::optional<Location> where, Rng& rng) {
    const ruleset::Message* m = effects::pickMessage(messages, rng);
    std::string title = m ? effects::substitute(m->title, tokens) : std::string{};
    std::string text = m ? effects::substitute(m->text, tokens) : std::string{};
    if (title.empty()) {
        if (auto e = effects::parseEffect(ev.type)) title = std::string(effects::identifier(*e));
    }
    if (text.empty()) {
        // Some stock records leave the text blank: name what was affected.
        for (const std::string* name : {&tokens.planetName, &tokens.starName, &tokens.warpPointName, &tokens.vehicleName})
            if (!name->empty()) {
                text = tokens.systemName.empty() ? *name : std::format("{} ({} system)", *name, tokens.systemName);
                break;
            }
    }
    // Random events go to the General list of the History window (inferred).
    const std::string line = text.empty() ? title : title.empty() ? text : std::format("{}: {}", title, text);
    for (EmpireId e : recipients(ctx.state, ev, t, where)) {
        ctx.log(e, LogCategory::Events, title, text, where, ev.picture);
        addHistory(ctx.state, e, {}, line, where);
    }
}

effects::Tokens baseTokens(const GameState& s, const Target& t, const effects::Tokens& fromEffect) {
    effects::Tokens tokens = fromEffect;
    // For events the affected empire stands in for both source and target.
    effects::setEmpireTokens(tokens, s, t.empire, t.empire);
    return tokens;
}

void fire(TurnContext& ctx, uint32_t record, const Target& t, Rng& rng) {
    const ruleset::EventType& ev = ctx.rules.data().eventTypes[record];
    const auto effect = effects::parseEffect(ev.type);
    if (!effect) return;
    const std::optional<Location> where = effects::targetLocation(ctx.state, t);
    const effects::Outcome out = effects::apply(ctx, *effect, t, ev.effectAmount, rng);
    if (!out.applied) return;
    effects::Tokens tokens = baseTokens(ctx.state, t, out.tokens);
    tokens.actualAmount = out.actual < 0 ? -out.actual : out.actual;
    sendMessages(ctx, ev, ev.messages, t, tokens, where, rng);
}

// The luck roll for an owned target (spec 05 §4, confirmed: binary): a roll
// of 1–100 must be below 100 + the owner's Luck trait values; a total of 0
// or less skips the roll. An empire target rolls with its own Luck, a
// destroyed one too.
bool luckHolds(const Rules& r, const GameState& s, EmpireId owner, Rng& rng) {
    if (!validEmpire(s, owner)) return true;
    const int64_t total = 100 + r.traitValue(s.empire(owner).race, "Luck");
    if (total <= 0) return true;
    return rng.range(1, 100) < total;
}

// For star events every empire with a colony in the system rolls (vehicles
// do not count); a total of exactly 100 skips the roll (confirmed: binary).
bool starLuckHolds(const Rules& r, const GameState& s, SystemId sys, Rng& rng) {
    std::vector<uint8_t> present(s.empires.size(), 0);
    for (const auto& c : s.colonies)
        if (c && validEmpire(s, c->owner) && s.galaxy.object(c->planet).system == sys) present[c->owner.index()] = 1;
    for (size_t i = 0; i < present.size(); ++i) {
        if (!present[i]) continue;
        const int64_t total = 100 + r.traitValue(s.empires[i].race, "Luck");
        if (total == 100) continue;
        if (rng.range(1, 100) >= total) return false;
    }
    return true;
}

// The target of an event, rebuilt from its object (the owner may have changed).
Target eventTarget(const GameState& s, TargetKind kind, uint32_t index) {
    Target t;
    switch (kind) {
        case TargetKind::Ship: {
            t.vehicle = VehicleId{index};
            if (const Vehicle* v = s.vehicle(t.vehicle)) t.empire = v->owner;
            break;
        }
        case TargetKind::Planet:
        case TargetKind::Star:
        case TargetKind::WarpPoint:
            t.object = ObjectId{index};
            if (const Colony* c = s.colony(t.object)) t.empire = c->owner;
            break;
        case TargetKind::Empire: t.empire = EmpireId{index}; break;
        case TargetKind::None: break;
    }
    return t;
}

} // namespace

Severity parseSeverity(std::string_view text) {
    if (datafile::keysEqual(text, "Catastrophic")) return Severity::Catastrophic;
    if (datafile::keysEqual(text, "High")) return Severity::High;
    if (datafile::keysEqual(text, "Medium")) return Severity::Medium;
    return Severity::Low;
}

int eventChance(const Rules& r, const GameState& s) {
    switch (s.options.eventFrequency) {
        case 1: return static_cast<int>(r.setting("Event Percent Chance Low", 5));
        case 2: return static_cast<int>(r.setting("Event Percent Chance Medium", 10));
        case 3: return static_cast<int>(r.setting("Event Percent Chance High", 25));
        default: return 0;
    }
}

uint32_t allowedRecordCount(const Rules& r, const GameState& s) {
    uint32_t n = 0;
    for (const auto& ev : r.data().eventTypes)
        if (static_cast<int>(parseSeverity(ev.severity)) <= s.options.maxEventSeverity) ++n;
    return n;
}

std::optional<uint32_t> pickRecord(const Rules& r, const GameState& s, Rng& rng) {
    // A quirk of the original: the severity filter only counts the records;
    // the pick is uniform among the first N records of the file.
    const uint32_t n = allowedRecordCount(r, s);
    if (n == 0) return std::nullopt;
    return static_cast<uint32_t>(rng.below(n));
}

TargetKind targetKind(Effect e) {
    switch (e) {
        case Effect::ShipDamage:
        case Effect::ShipLoseMovement:
        case Effect::ShipLoseSupply:
        case Effect::ShipRebel:
        case Effect::ShipExperienceChange:
        case Effect::ShipCargoDamage:
        case Effect::ShipOrdersChange:
        case Effect::ShipMoved: return TargetKind::Ship;
        case Effect::PlanetConditionsChange:
        case Effect::PlanetValueChange:
        case Effect::PlanetPopulationChange:
        case Effect::PlanetPopulationAngerChange:
        case Effect::PlanetPopulationRiot:
        case Effect::PlanetPopulationRebel:
        case Effect::PlanetCargoDamage:
        case Effect::PlanetFacilityDamage:
        case Effect::PlanetDestroyed:
        case Effect::PlanetPlague:
        case Effect::PlanetPlagueCured: return TargetKind::Planet;
        case Effect::PoliticsDisruptTrade:
        case Effect::PoliticsInterceptMessages:
        case Effect::PoliticsFakeMessages:
        case Effect::PoliticsPreventMessages:
        case Effect::PoliticsTreatyInfo: return TargetKind::Empire;
        case Effect::StarDestroyed: return TargetKind::Star;
        case Effect::WarpPointClosed: return TargetKind::WarpPoint;
        // No target list (confirmed: binary for Points - Change, Planet -
        // Created, Warp Point - Opened; inferred for the delete-project and
        // espionage types): such a record never fires.
        default: return TargetKind::None;
    }
}

bool targetExists(const Rules&, const GameState& s, const Target& t) {
    if (t.vehicle.valid()) {
        const Vehicle* v = s.vehicle(t.vehicle);
        return v && v->count > 0;
    }
    if (t.object.valid()) return inSystem(s, t.object);
    if (t.system.valid()) return t.system.index() < s.galaxy.systems.size();
    return validEmpire(s, t.empire);
}

std::optional<Target> pickEventTarget(const Rules& r, const GameState& s, uint32_t record, Rng& rng) {
    if (record >= r.data().eventTypes.size()) return std::nullopt;
    const ruleset::EventType& ev = r.data().eventTypes[record];
    const auto effect = effects::parseEffect(ev.type);
    if (!effect) return std::nullopt;
    const TargetKind kind = targetKind(*effect);
    const Severity severity = parseSeverity(ev.severity);
    const bool spareHomes = severity >= Severity::High && (kind == TargetKind::Planet || kind == TargetKind::Star);

    // Candidates of the right kind from the whole galaxy (spec 05 §4,
    // confirmed: binary): every ship, base and unit group in space; every
    // colony; every star or warp point; every empire number, destroyed ones
    // included.
    std::vector<uint32_t> candidates;
    switch (kind) {
        case TargetKind::Ship:
            for (const Vehicle& v : s.vehicles)
                if (v.count > 0) candidates.push_back(v.id.value);
            break;
        case TargetKind::Planet:
            for (const auto& c : s.colonies)
                if (c && inSystem(s, c->planet)) candidates.push_back(c->planet.value);
            break;
        case TargetKind::Star:
        case TargetKind::WarpPoint:
            for (const StarSystem& sys : s.galaxy.systems)
                for (ObjectId o : sys.objects) {
                    const ObjectKind k = s.galaxy.object(o).kind;
                    if ((kind == TargetKind::Star && k == ObjectKind::Star) || (kind == TargetKind::WarpPoint && k == ObjectKind::WarpPoint))
                        candidates.push_back(o.value);
                }
            std::sort(candidates.begin(), candidates.end());
            break;
        case TargetKind::Empire:
            for (const Empire& e : s.empires) candidates.push_back(e.id.value);
            break;
        case TargetKind::None: return std::nullopt;
    }

    // Up to 1,000 draws; a candidate that fails a check is removed.
    for (int draw = 0; draw < effects::kTargetDraws && !candidates.empty(); ++draw) {
        const size_t i = rng.below(candidates.size());
        const Target t = eventTarget(s, kind, candidates[i]);
        bool ok = targetExists(r, s, t);
        std::optional<Location> where;
        if (ok) {
            where = effects::targetLocation(s, t);
            // High and Catastrophic planet and star events spare the exact
            // home planet locations: homeworlds, but practically never stars.
            if (spareHomes && where) ok = !atHomeLocation(s, *where);
        }
        // Luck, good events and bad alike.
        if (ok) ok = kind == TargetKind::Star ? starLuckHolds(r, s, where->system, rng) : luckHolds(r, s, t.empire, rng);
        if (ok && where) ok = !effects::chanceRejects(effects::unownedChanceValue(s, where->system, AbilityKind::ChangeBadEventChanceSystem), rng);
        if (ok) return t;
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return std::nullopt;
}

bool trigger(TurnContext& ctx, uint32_t record, const Target& target, Rng& rng) {
    GameState& s = ctx.state;
    if (record >= ctx.rules.data().eventTypes.size()) return false;
    const ruleset::EventType& ev = ctx.rules.data().eventTypes[record];
    if (!effects::parseEffect(ev.type) || !targetExists(ctx.rules, s, target)) return false;
    if (ev.turnsToComplete <= 0) {
        fire(ctx, record, target, rng);
        return true;
    }
    // Timed: the start message now, the strike later (its amount is unknown yet).
    const std::optional<Location> where = effects::targetLocation(s, target);
    effects::Tokens tokens;
    if (target.vehicle.valid())
        if (const Vehicle* v = s.vehicle(target.vehicle)) {
            tokens.vehicleName = v->name;
            tokens.designName = s.design(v->design).name;
            tokens.vehicleSize = ctx.rules.hull(s.design(v->design).hull).name;
        }
    if (target.object.valid() && target.object.index() < s.galaxy.objects.size()) {
        const SpaceObject& obj = s.galaxy.object(target.object);
        (obj.kind == ObjectKind::Star ? tokens.starName : obj.kind == ObjectKind::WarpPoint ? tokens.warpPointName : tokens.planetName) = obj.name;
    }
    if (where) {
        tokens.systemName = s.galaxy.system(where->system).name;
        tokens.sectorName = std::format("({}, {})", where->sector.x, where->sector.y);
    }
    tokens = baseTokens(s, target, tokens);
    if (!ev.startMessages.empty()) sendMessages(ctx, ev, ev.startMessages, target, tokens, where, rng);
    PendingEvent pe;
    pe.eventType = record;
    pe.empire = target.empire;
    pe.object = target.object;
    pe.vehicle = target.vehicle;
    pe.system = target.system;
    pe.fireTurn = s.turn + static_cast<uint32_t>(ev.turnsToComplete);
    s.pendingEvents.push_back(pe);
    return true;
}

void fireDueEvents(TurnContext& ctx, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // In the order they were started. A timed event strikes exactly its
    // `Time Till Completion` turns later if its target still exists, and is
    // dropped silently otherwise (confirmed: binary).
    for (size_t i = 0; i < s.pendingEvents.size();) {
        if (s.pendingEvents[i].fireTurn > s.turn) {
            ++i;
            continue;
        }
        const PendingEvent pe = s.pendingEvents[i];
        s.pendingEvents.erase(s.pendingEvents.begin() + static_cast<std::ptrdiff_t>(i));
        if (pe.eventType >= r.data().eventTypes.size()) continue;
        Target t;
        t.object = pe.object;
        t.vehicle = pe.vehicle;
        t.system = pe.system;
        t.empire = pe.empire;
        // The object's current owner is the one affected now.
        if (const Vehicle* v = t.vehicle.valid() ? s.vehicle(t.vehicle) : nullptr) t.empire = v->owner;
        else if (t.object.valid()) t.empire = s.colony(t.object) ? s.colony(t.object)->owner : EmpireId{};
        if (!targetExists(r, s, t)) continue;
        fire(ctx, pe.eventType, t, rng);
    }
}

void rollNewEvent(TurnContext& ctx, uint32_t date, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // One roll for the whole galaxy (None never rolls), and no new events
    // before 2402.0 (confirmed: binary).
    const int chance = eventChance(r, s);
    if (s.options.eventFrequency <= 0 || chance <= 0 || date < kFirstEventDate) return;
    if (rng.range(1, 100) > chance) return;
    const auto record = pickRecord(r, s, rng);
    if (!record) return;
    const auto target = pickEventTarget(r, s, *record, rng);
    if (!target) return;
    trigger(ctx, *record, *target, rng);
}

} // namespace opense4::game::events
