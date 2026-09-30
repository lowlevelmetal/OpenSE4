#include "game/events.hpp"

#include "datafile/datafile.hpp"
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

void learnDesign(Empire& e, DesignId d) {
    auto& seen = e.knowledge.seenDesigns;
    auto it = std::lower_bound(seen.begin(), seen.end(), d);
    if (it == seen.end() || *it != d) seen.insert(it, d);
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
    v.count = 0;
    ++ctx.state.design(v.design).lost;
    ctx.mood(v.owner, "Any Ship Lost");
    ctx.mood(v.owner, "Ship Lost in System", v.location.system);
}

// Kills `kill` M spread over the races in proportion to their numbers.
int64_t removePopulation(Colony& c, int64_t kill) {
    const int64_t total = c.totalPopulation();
    kill = std::clamp<int64_t>(kill, 0, total);
    if (kill == 0) return 0;
    int64_t left = kill;
    for (PopulationGroup& p : c.population) {
        const int64_t k = p.millions * kill / total;
        p.millions -= k;
        left -= k;
    }
    // Rounding remainder: from the largest groups first (stable by position).
    while (left > 0) {
        auto it = std::max_element(c.population.begin(), c.population.end(),
                                   [](const PopulationGroup& a, const PopulationGroup& b) { return a.millions < b.millions; });
        if (it == c.population.end() || it->millions <= 0) break;
        --it->millions;
        --left;
    }
    std::erase_if(c.population, [](const PopulationGroup& p) { return p.millions <= 0; });
    return kill - left;
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

// Candidate third empires for a political operation against `target`.
std::vector<EmpireId> politicalPartners(const GameState& s, Effect e, const Target& t) {
    std::vector<EmpireId> out;
    const Empire& tgt = s.empire(t.empire);
    for (const Empire& o : s.empires) {
        if (!o.alive || o.id == t.empire || o.id == t.source) continue;
        const Relation& rel = tgt.relation(o.id);
        if (!rel.contact) continue;
        if (e == Effect::PoliticsDisruptTrade && !treatyTradesResources(rel.treaty)) continue;
        out.push_back(o.id);
    }
    return out;
}

bool politicalPartnerValid(const GameState& s, Effect e, const Target& t, EmpireId other) {
    const std::vector<EmpireId> ok = politicalPartners(s, e, t);
    return std::find(ok.begin(), ok.end(), other) != ok.end();
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

bool shipCandidate(const Rules& r, const GameState& s, Effect e, EmpireId owner, const Vehicle& v) {
    if (v.owner != owner || !isShip(r, s, v)) return false;
    if (e == Effect::ShipCargoDamage && v.cargo.empty()) return false;
    // Mothballed ships are immune to new orders (spec 05 §2.3, confirmed: binary).
    if (e == Effect::ShipOrdersChange && v.status == VehicleStatus::Mothballed) return false;
    return true;
}

bool colonyCandidate(const Rules& r, const GameState& s, Effect e, EmpireId owner, const Colony& c, bool anyTarget) {
    if (c.owner != owner) return false;
    switch (e) {
        case Effect::PlanetPopulationChange: return c.totalPopulation() > 0;
        case Effect::PlanetPopulationAngerChange:
        case Effect::PlanetPopulationRiot: return !emotionless(r, s, c) && c.totalPopulation() > 0;
        case Effect::PlanetPopulationRebel: return !(anyTarget && c.homeworld);
        case Effect::PlanetCargoDamage: return !c.cargo.empty();
        case Effect::PlanetFacilityDamage: return !c.facilities.empty();
        case Effect::PlanetPlague: return !plagueImmune(r, s, c) && c.totalPopulation() > 0;
        case Effect::PlanetPlagueCured: return c.plagueLevel > 0;
        default: return true;
    }
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
    // An "Any" candidate in a system may be rejected by `Change Bad
    // Intelligence Chance - System` (spec 05 §2.4).
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
                if (!v || !shipCandidate(r, s, e, owner, *v)) return std::nullopt;
                return t;
            }
            std::vector<VehicleId> ships;
            for (const Vehicle& v : s.vehicles)
                if (v.owner == owner && isShip(r, s, v)) ships.push_back(v.id);
            auto pick = drawCandidate(ships, rng, [&](VehicleId id) {
                const Vehicle* v = s.vehicle(id);
                return v && shipCandidate(r, s, e, owner, *v) && !badIntel(v->location.system);
            });
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
                if (!c || !colonyCandidate(r, s, e, owner, *c, false)) return std::nullopt;
                if (e == Effect::PlanetInfo && !s.empire(request.source).hasExplored(s.galaxy.object(c->planet).system))
                    return std::nullopt;
                return t;
            }
            std::vector<ObjectId> planets;
            for (const auto& c : s.colonies)
                if (c && c->owner == owner) planets.push_back(c->planet);
            auto pick = drawCandidate(planets, rng, [&](ObjectId o) {
                const Colony* c = s.colony(o);
                return c && colonyCandidate(r, s, e, owner, *c, true) && !badIntel(s.galaxy.object(o).system);
            });
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
        case Effect::TechLevelInfo: return t;

        case Effect::ShipDesignsSteal:
        case Effect::UnitDesignsSteal:
            if (designsOfClass(r, s, owner, e == Effect::UnitDesignsSteal).empty()) return std::nullopt;
            return t;

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
            if (request.other.valid()) {
                if (!politicalPartnerValid(s, e, t, request.other)) return std::nullopt;
                return t;
            }
            std::vector<EmpireId> others;
            for (const Empire& o : s.empires)
                if (o.alive && o.id != owner && o.id != request.source) others.push_back(o.id);
            auto pick = drawCandidate(others, rng, [&](EmpireId o) { return politicalPartnerValid(s, e, t, o); });
            if (!pick) return std::nullopt;
            t.other = *pick;
            return t;
        }

        case Effect::SystemInfo: {
            if (request.system.valid()) {
                if (!validSystem(s, request.system) || !emp.hasExplored(request.system)) return std::nullopt;
                return t;
            }
            // Prefers a system the target knows and we do not (inferred).
            const Empire& src = s.empire(request.source);
            std::vector<SystemId> unknown, known;
            for (const StarSystem& sys : s.galaxy.systems)
                if (emp.hasExplored(sys.id)) (src.hasExplored(sys.id) ? known : unknown).push_back(sys.id);
            auto pick = drawCandidate(unknown, rng, [&](SystemId sys) { return !badIntel(sys); });
            if (!pick) pick = drawCandidate(known, rng, [&](SystemId sys) { return !badIntel(sys); });
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
    GameState& s = ctx.state;
    const Colony* col = s.colony(planet);
    if (!col || !living(s, col->owner) || s.empires.size() >= kMaxEmpires) return {};
    const EmpireId former = col->owner;
    const bool home = col->homeworld;
    // The race of the colony's largest population group (inferred).
    EmpireId raceOf = former;
    int64_t most = -1;
    for (const PopulationGroup& p : col->population)
        if (validEmpire(s, p.race) && p.millions > most) {
            most = p.millions;
            raceOf = p.race;
        }

    const EmpireId id{s.empires.size()};
    const Empire& old = s.empire(former);
    const SystemId system = s.galaxy.object(planet).system;
    const uint8_t seen = s.options.allSystemsSeen ? 1 : 0;
    Empire e;
    e.id = id;
    e.name = std::format("Free {}", s.galaxy.object(planet).name);
    e.empireType = "Republic";
    e.leaderTitle = "Governor";
    e.race = s.empire(raceOf).race;
    e.color = defaultEmpireColor(id.index());
    e.kind = PlayerKind::Computer;
    e.racialPointsSpent = s.empire(raceOf).racialPointsSpent;
    e.techLevels = old.techLevels;
    e.uniqueAreasUnlocked = old.uniqueAreasUnlocked;
    e.strategies = old.strategies;
    e.designTypes = old.designTypes;
    e.colonyTypes = old.colonyTypes;
    e.repairPriorities = old.repairPriorities;
    e.claimedSystems.push_back(system);
    e.knowledge.explored.assign(s.galaxy.systems.size(), seen);
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
    // The rebels of the new empire's race are its own people (inferred).
    for (PopulationGroup& g : c.population)
        if (g.race == raceOf) g.race = id;
    // The planet is the new empire's capital (spec 02 §4), so its anger is
    // at most the capital limit (spec 02 §2).
    c.homeworld = true;
    c.anger = std::min(c.anger, c.maxAnger());
    diplomacy::makeContact(ctx, former, id);
    diplomacy::declareWar(ctx, id, former);
    return id;
}

// ---- Damage helpers ------------------------------------------------------------------------------------

int64_t damageVehicle(const Rules& r, GameState& s, Vehicle& v, int64_t amount, Rng& rng) {
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
    return applied;
}

int64_t damageCargo(const Rules& r, const GameState& s, Cargo& c, int64_t amount, Rng& rng) {
    const int64_t mass = std::max<int64_t>(1, r.setting("Population Mass", 5));
    const int64_t used = cargoSpaceUsed(r, s, c);
    if (used <= 0) return 0;
    int64_t budget = amount <= 1 ? (used + 1) / 2 : amount;
    int64_t destroyed = 0;
    // One unit or 1M of population at a time, picked at random.
    for (int guard = 0; budget > 0 && guard < 100000; ++guard) {
        const size_t slots = c.units.size() + c.population.size();
        if (slots == 0) break;
        const size_t pick = rng.below(slots);
        int64_t size = 0;
        if (pick < c.units.size()) {
            UnitStack& u = c.units[pick];
            size = std::max<int64_t>(1, r.hull(s.design(u.design).hull).tonnage);
            --u.count;
        } else {
            PopulationGroup& p = c.population[pick - c.units.size()];
            size = mass;
            --p.millions;
        }
        budget -= size;
        destroyed += size;
        std::erase_if(c.units, [](const UnitStack& u) { return u.count <= 0; });
        std::erase_if(c.population, [](const PopulationGroup& p) { return p.millions <= 0; });
    }
    return destroyed;
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
            if (!v) return out;
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
            if (!v) return out;
            out.actual = damageCargo(r, s, v->cargo, amount, rng);
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
            if (!living(s, t.source)) return out;
            if (!victim) return out;
            const std::vector<DesignId> all = designsOfClass(r, s, t.empire, e == Effect::UnitDesignsSteal);
            const auto& seen = s.empire(t.source).knowledge.seenDesigns;
            std::vector<DesignId> fresh;
            for (DesignId d : all)
                if (!std::binary_search(seen.begin(), seen.end(), d)) fresh.push_back(d);
            auto pick = choose(fresh.empty() ? all : fresh, rng);
            if (!pick) return out;
            learnDesign(s.empire(t.source), *pick);
            // The blueprints become one of our own designs (inferred); building
            // it still needs the technology.
            Design copy = s.design(*pick);
            const std::string original = copy.name;
            copy.owner = t.source;
            copy.createdTurn = s.turn;
            resetDesignStatistics(copy);  // a new design starts without statistics (spec 04 §15)
            copy.obsolete = false;
            copy.strategy = 0;
            // Design names are unique in the whole game (spec 03 §4.1), and the victim's still exists.
            copy.name = uniqueDesignName(s, std::format("{} ({})", original, empireFullName(*victim)));
            addDesign(s, std::move(copy));
            out.tokens.designName = original;
            out.report.push_back(std::format("The {} {} design is now in our design list.", original, designClassName(e == Effect::UnitDesignsSteal)));
            out.actual = 1;
            break;
        }

        case Effect::PlanetConditionsChange: {
            if (!col) return out;
            // Conditions are hundredths of the 0–1.5 scale (spec 02 §2); Amount
            // adds that many hundredths, within the scale (inferred: spec 01
            // §10 calls it a percentage change).
            SpaceObject& obj = s.galaxy.object(t.object);
            const int before = obj.conditions;
            obj.conditions = std::clamp(before + amount, 0, economy::kConditionsMax);
            out.actual = obj.conditions - before;
            break;
        }
        case Effect::PlanetValueChange: {
            // Each of the three values changes by Amount; in finite-resource
            // games by Amount × 1,000 when that stays within ±500,000 (spec 05
            // §2.3; otherwise by Amount, inferred). The result stays within the
            // `Minimum/Maximum Planet Percent/Resource Value` settings (spec 01
            // §2.3; a value already outside is not pulled in, inferred).
            if (!validObject(s, t.object)) return out;
            SpaceObject& obj = s.galaxy.object(t.object);
            if (obj.kind != ObjectKind::Planet && obj.kind != ObjectKind::Asteroids) return out;
            const bool finite = s.options.finiteResources;
            const int64_t change = finite && std::abs(int64_t{amount} * 1000) <= 500'000 ? int64_t{amount} * 1000 : int64_t{amount};
            const int64_t lo = r.setting(finite ? "Minimum Planet Resource Value" : "Minimum Planet Percent Value", 0);
            const int64_t hi = r.setting(finite ? "Maximum Planet Resource Value" : "Maximum Planet Percent Value", finite ? 999'000'000 : 250);
            int64_t delta = 0;
            for (int& value : obj.value) {
                const int before = value;
                const int64_t lowest = std::min<int64_t>(lo, value), highest = std::max<int64_t>(hi, value);
                value = static_cast<int>(std::clamp<int64_t>(int64_t{value} + change, lowest, highest));
                delta += value - before;
            }
            out.actual = delta / static_cast<int64_t>(obj.value.size());
            break;
        }
        case Effect::PlanetPopulationChange: {
            if (!col) return out;
            const SystemId sys = s.galaxy.object(t.object).system;
            if (amount < 0) {
                out.actual = removePopulation(*col, -int64_t{amount});
                if (out.actual > 0) ctx.mood(col->owner, "1M Population Killed", sys, t.object, static_cast<int>(out.actual));
                // A colony whose people are all gone dies out (spec 02 §2).
                if (out.actual > 0 && col->totalPopulation() <= 0) economy::colonyDiesOut(ctx, t.object);
            } else {
                const int64_t room = std::max<int64_t>(0, maxPopulation(r, s, *col) - col->totalPopulation());
                const int64_t add = std::min<int64_t>(amount, room);
                auto it = std::find_if(col->population.begin(), col->population.end(), [&](const PopulationGroup& p) { return p.race == col->owner; });
                if (it == col->population.end()) {
                    col->population.push_back({col->owner, 0});
                    it = col->population.end() - 1;
                }
                it->millions += add;
                out.actual = add;
            }
            break;
        }
        case Effect::PlanetPopulationAngerChange: {
            // Amount is in tenths of a percent, like Happiness.txt (spec 05
            // §2.3, inferred); anger is a whole percent, so the change is
            // trunc(Amount / 10), within 0 and 100 (80 on a capital) (spec 02 §4).
            if (!col || emotionless(r, s, *col)) return out;
            const int before = col->anger;
            col->anger = std::clamp(before + amount / 10, 0, col->maxAnger());
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
            // As an event the colony breaks away as a new empire (below 20
            // empires). As an intelligence project: a roll of 1–4 equal to 1
            // breaks it away, else a second such roll makes it join the
            // source, else nothing happens: 25 / 18.75 / 56.25 % (confirmed:
            // binary). `victim` is not used past this point: a new empire
            // invalidates references into GameState::empires.
            if (!col) return out;
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
        case Effect::PlanetCargoDamage:
            if (!col) return out;
            out.actual = damageCargo(r, s, col->cargo, amount, rng);
            break;
        case Effect::PlanetFacilityDamage: {
            if (!col || col->facilities.empty()) return out;
            const int64_t n = std::min<int64_t>(std::max(1, amount), static_cast<int64_t>(col->facilities.size()));
            for (int64_t k = 0; k < n; ++k) {
                const size_t idx = rng.below(col->facilities.size());
                out.tokens.facilityName = r.facility(col->facilities[idx]).name;
                col->facilities.erase(col->facilities.begin() + static_cast<std::ptrdiff_t>(idx));
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
            // turns (confirmed: binary).
            if (!victim || !living(s, t.other)) return out;
            const DiplomaticMessage* latest = nullptr;
            for (const DiplomaticMessage& m : s.messages) {
                const bool between = (m.from == t.empire && m.to == t.other) || (m.from == t.other && m.to == t.empire);
                if (!between || m.sentTurn + 1 < s.turn) continue;
                if (!latest || m.sentTurn > latest->sentTurn || (m.sentTurn == latest->sentTurn && m.id > latest->id)) latest = &m;
            }
            if (latest) {
                out.report.push_back(std::format("{} from the {} to the {}{}{}", displayName(latest->type), empireFullName(s.empire(latest->from)),
                                                 empireFullName(s.empire(latest->to)), latest->text.empty() ? "" : ": ", latest->text));
                out.actual = 1;
            } else {
                out.report.push_back("No recent messages passed between them.");
            }
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
            if (!victim || !validSystem(s, t.system) || !living(s, t.source)) return out;
            explore(s, t.source, t.system);
            Empire& src = s.empire(t.source);
            for (ObjectId wp : s.galaxy.warpPoints(t.system))
                if (wp.index() < victim->knowledge.knownWarpLink.size() && victim->knowledge.knownWarpLink[wp.index()] &&
                    wp.index() < src.knowledge.knownWarpLink.size())
                    src.knowledge.knownWarpLink[wp.index()] = 1;
            setLocationTokens(out.tokens, s, Location{t.system, Sector{kSystemCenter, kSystemCenter}});
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

bool planetLike(ObjectKind k) { return k == ObjectKind::Planet || k == ObjectKind::Asteroids; }

bool homeworldInSystem(const GameState& s, SystemId sys) {
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && c->homeworld) return true;
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
    for (EmpireId e : recipients(ctx.state, ev, t, where)) ctx.log(e, LogCategory::Events, title, text, where, ev.picture);
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
// or less skips the roll.
bool luckHolds(const Rules& r, const GameState& s, EmpireId owner, Rng& rng) {
    if (!living(s, owner)) return true;
    const int64_t total = 100 + r.traitValue(s.empire(owner).race, "Luck");
    if (total <= 0) return true;
    return rng.range(1, 100) < total;
}

// For star events every empire present in the system rolls; a total of
// exactly 100 skips the roll (confirmed: binary). Present: a vehicle or a
// colony there (inferred).
bool starLuckHolds(const Rules& r, const GameState& s, SystemId sys, Rng& rng) {
    std::vector<uint8_t> present(s.empires.size(), 0);
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.location.system == sys && validEmpire(s, v.owner)) present[v.owner.index()] = 1;
    for (const auto& c : s.colonies)
        if (c && validEmpire(s, c->owner) && s.galaxy.object(c->planet).system == sys) present[c->owner.index()] = 1;
    for (size_t i = 0; i < present.size(); ++i) {
        if (!present[i] || !s.empires[i].alive) continue;
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

bool targetExists(const Rules& r, const GameState& s, const Target& t) {
    if (t.vehicle.valid()) {
        const Vehicle* v = s.vehicle(t.vehicle);
        return v && v->count > 0 && !isUnitType(vehicleType(r, s, *v));
    }
    if (t.object.valid()) return inSystem(s, t.object);
    if (t.system.valid()) return t.system.index() < s.galaxy.systems.size();
    return living(s, t.empire);
}

std::optional<Target> pickEventTarget(const Rules& r, const GameState& s, uint32_t record, Rng& rng) {
    if (record >= r.data().eventTypes.size()) return std::nullopt;
    const ruleset::EventType& ev = r.data().eventTypes[record];
    const auto effect = effects::parseEffect(ev.type);
    if (!effect) return std::nullopt;
    const TargetKind kind = targetKind(*effect);
    const Severity severity = parseSeverity(ev.severity);
    const bool spareHomes = severity >= Severity::High && (kind == TargetKind::Planet || kind == TargetKind::Star);

    // Candidates of the right kind from the whole galaxy, in id order.
    std::vector<uint32_t> candidates;
    switch (kind) {
        case TargetKind::Ship:
            for (const Vehicle& v : s.vehicles)
                if (v.count > 0 && !isUnitType(vehicleType(r, s, v))) candidates.push_back(v.id.value);
            break;
        case TargetKind::Planet:
        case TargetKind::Star:
        case TargetKind::WarpPoint:
            for (const StarSystem& sys : s.galaxy.systems)
                for (ObjectId o : sys.objects) {
                    const ObjectKind k = s.galaxy.object(o).kind;
                    if ((kind == TargetKind::Planet && planetLike(k)) || (kind == TargetKind::Star && k == ObjectKind::Star) ||
                        (kind == TargetKind::WarpPoint && k == ObjectKind::WarpPoint))
                        candidates.push_back(o.value);
                }
            std::sort(candidates.begin(), candidates.end());
            break;
        case TargetKind::Empire:
            for (const Empire& e : s.empires)
                if (e.alive) candidates.push_back(e.id.value);
            break;
        case TargetKind::None: return std::nullopt;
    }

    // Up to 1,000 draws; a candidate that fails a check is removed.
    for (int draw = 0; draw < effects::kTargetDraws && !candidates.empty(); ++draw) {
        const size_t i = rng.below(candidates.size());
        const Target t = eventTarget(s, kind, candidates[i]);
        bool ok = targetExists(r, s, t);
        std::optional<SystemId> sys;
        if (ok) {
            if (const auto where = effects::targetLocation(s, t)) sys = where->system;
            // Homeworlds are safe from High and Catastrophic planet and star events.
            if (spareHomes && kind == TargetKind::Planet) {
                const Colony* c = s.colony(t.object);
                ok = !(c && c->homeworld);
            } else if (spareHomes && kind == TargetKind::Star) {
                ok = !homeworldInSystem(s, *sys);
            }
        }
        // Luck, good events and bad alike.
        if (ok) ok = kind == TargetKind::Star ? starLuckHolds(r, s, *sys, rng) : luckHolds(r, s, t.empire, rng);
        if (ok && sys) ok = !effects::chanceRejects(effects::unownedChanceValue(s, *sys, AbilityKind::ChangeBadEventChanceSystem), rng);
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
