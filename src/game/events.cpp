#include "game/events.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
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
void loseColony(TurnContext& ctx, ObjectId planet) {
    GameState& s = ctx.state;
    Colony* c = s.colony(planet);
    if (!c) return;
    const EmpireId owner = c->owner;
    const bool home = c->homeworld;
    s.colonies[planet.index()].reset();
    ctx.mood(owner, "Any Planet Lost");
    if (home) ctx.mood(owner, "Homeworld Lost");
}

// Turns a planet into an asteroid field; its colony is lost (spec 01 §9).
void shatterPlanet(TurnContext& ctx, ObjectId planet, Rng& rng) {
    SpaceObject& obj = ctx.state.galaxy.object(planet);
    if (obj.kind != ObjectKind::Planet) return;
    loseColony(ctx, planet);
    const std::string oldSize = obj.size;
    obj.kind = ObjectKind::Asteroids;
    obj.atmosphere = "None";
    const std::string_view size = keysEqual(oldSize, "Tiny") ? "Small" : keysEqual(oldSize, "Huge") ? "Large" : std::string_view(oldSize);
    obj.size = std::string(size);
    if (auto st = pickSectorType(ctx.rules, "Asteroids", size, rng)) applySectorType(ctx.rules, obj, *st);
}

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

std::vector<ruleset::TechAreaId> stealableAreas(const Rules& r, const GameState& s, EmpireId source, EmpireId target) {
    std::vector<ruleset::TechAreaId> out;
    const Empire& src = s.empire(source);
    const Empire& tgt = s.empire(target);
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const ruleset::TechAreaId a{i};
        if (tgt.techLevel(a) > src.techLevel(a) && src.techLevel(a) < r.tech(a).maxLevel && r.techVisible(s, src, a)) out.push_back(a);
    }
    return out;
}

// Candidate third empires for a political operation against `target`.
std::vector<EmpireId> politicalPartners(const GameState& s, Effect e, const Target& t) {
    std::vector<EmpireId> out;
    const Empire& tgt = s.empire(t.empire);
    for (const Empire& o : s.empires) {
        if (!o.alive || o.id == t.empire || o.id == t.source) continue;
        const Relation& rel = tgt.relation(o.id);
        if (!rel.contact) continue;
        if (e == Effect::PoliticsDisruptTrade && !(treatyTradesResources(rel.treaty) && rel.tradePercent > 0)) continue;
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
            std::vector<VehicleId> options;
            for (const Vehicle& v : s.vehicles)
                if (shipCandidate(r, s, e, owner, v)) options.push_back(v.id);
            auto pick = choose(options, rng);
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
            std::vector<ObjectId> options;
            for (const auto& c : s.colonies)
                if (c && colonyCandidate(r, s, e, owner, *c, true)) options.push_back(c->planet);
            auto pick = choose(options, rng);
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

        case Effect::ResearchSteal:
            if (stealableAreas(r, s, request.source, owner).empty()) return std::nullopt;
            return t;
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
            auto pick = choose(politicalPartners(s, e, t), rng);
            if (!pick) return std::nullopt;
            t.other = *pick;
            return t;
        }

        case Effect::SystemInfo: {
            if (request.system.valid()) {
                if (!validSystem(s, request.system) || !emp.hasExplored(request.system)) return std::nullopt;
                return t;
            }
            const Empire& src = s.empire(request.source);
            std::vector<SystemId> unknown, known;
            for (const StarSystem& sys : s.galaxy.systems)
                if (emp.hasExplored(sys.id)) (src.hasExplored(sys.id) ? known : unknown).push_back(sys.id);
            auto pick = choose(unknown.empty() ? known : unknown, rng);
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
                if (!validObject(s, request.object) || !fits(request.object)) return std::nullopt;
                const auto& objs = s.galaxy.system(s.galaxy.object(request.object).system).objects;
                if (std::find(objs.begin(), objs.end(), request.object) == objs.end()) return std::nullopt;
                return t;
            }
            std::vector<ObjectId> options;
            for (SystemId sys : colonySystems(s, owner))
                for (ObjectId o : s.galaxy.system(sys).objects)
                    if (fits(o)) options.push_back(o);
            auto pick = choose(options, rng);
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
            std::vector<SystemId> options;
            for (SystemId sys : colonySystems(s, owner))
                if (fits(sys)) options.push_back(sys);
            auto pick = choose(options, rng);
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

int64_t systemChanceModifier(const Rules& r, const GameState& s, EmpireId empire, SystemId sys, AbilityKind k) {
    if (!validSystem(s, sys)) return 0;
    int64_t total = 0;
    for (const auto& a : s.galaxy.system(sys).abilities)
        if (const ParsedAbility p = parseAbility(a); p.kind == k) total += p.value1;
    std::optional<int64_t> best;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != empire || s.galaxy.object(c->planet).system != sys) continue;
        for (const ParsedAbility& a : colonyAbilities(r, s, *c))
            if (a.kind == k && (!best || a.value1 < *best)) best = a.value1;
    }
    return total + best.value_or(0);
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
    if (!living(s, t.empire)) return out;
    Empire& victim = s.empire(t.empire);

    // Ship effects.
    Vehicle* v = t.vehicle.valid() ? s.vehicle(t.vehicle) : nullptr;
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
            if (!v) return out;
            out.actual = std::max(1, amount);
            v->immobileUntil = std::max(v->immobileUntil, s.turn + 1 + static_cast<uint32_t>(out.actual));
            v->movement = 0;
            break;
        case Effect::ShipLoseSupply:
            if (!v) return out;
            out.actual = amount <= 0 ? v->supply : std::min<int64_t>(v->supply, amount);
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
            if (!v) return out;
            detachFromFleet(s, *v);
            Order o;
            o.kind = OrderKind::MoveTo;
            o.location = {v->location.system, Sector{static_cast<int>(rng.below(kSystemSize)), static_cast<int>(rng.below(kSystemSize))}};
            v->orders = {o};
            v->repeatOrders = false;
            out.actual = 1;
            break;
        }
        case Effect::ShipMoved: {
            if (!v) return out;
            // Breadth-first over warp links up to `amount` jumps (inferred reading
            // of the stock record: the ship lands in another system).
            const int jumps = std::max(1, amount);
            std::vector<int> dist(s.galaxy.systems.size(), -1);
            std::vector<SystemId> frontier{v->location.system};
            dist[v->location.system.index()] = 0;
            std::vector<SystemId> reached;
            for (size_t f = 0; f < frontier.size(); ++f) {
                const SystemId cur = frontier[f];
                if (dist[cur.index()] >= jumps) continue;
                for (SystemId nb : s.galaxy.neighbors(cur))
                    if (dist[nb.index()] < 0) {
                        dist[nb.index()] = dist[cur.index()] + 1;
                        frontier.push_back(nb);
                        reached.push_back(nb);
                    }
            }
            std::sort(reached.begin(), reached.end());
            if (reached.empty())
                for (const StarSystem& sys : s.galaxy.systems)
                    if (sys.id != v->location.system) reached.push_back(sys.id);
            const SystemId dest = reached.empty() ? v->location.system : reached[rng.below(reached.size())];
            detachFromFleet(s, *v);
            v->location = {dest, Sector{static_cast<int>(rng.below(kSystemSize)), static_cast<int>(rng.below(kSystemSize))}};
            v->orders.clear();
            v->repeatOrders = false;
            explore(s, v->owner, dest);
            setLocationTokens(out.tokens, s, v->location);
            out.actual = dist[dest.index()] > 0 ? dist[dest.index()] : 1;
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
            copy.built = copy.lost = copy.kills = 0;
            copy.obsolete = false;
            copy.strategy = 0;
            for (DesignId own : s.empire(t.source).designs)
                if (s.design(own).name == copy.name) copy.name = std::format("{} ({})", original, empireFullName(victim));
            addDesign(s, std::move(copy));
            out.tokens.designName = original;
            out.report.push_back(std::format("The {} {} design is now in our design list.", original, designClassName(e == Effect::UnitDesignsSteal)));
            out.actual = 1;
            break;
        }

        case Effect::PlanetConditionsChange: {
            if (!col) return out;
            SpaceObject& obj = s.galaxy.object(t.object);
            const int before = obj.conditions;
            obj.conditions = std::clamp(before + amount, 0, 100);
            out.actual = obj.conditions - before;
            break;
        }
        case Effect::PlanetValueChange: {
            if (!col) return out;
            SpaceObject& obj = s.galaxy.object(t.object);
            const bool asteroids = obj.kind == ObjectKind::Asteroids;
            const int64_t lo = r.setting(asteroids ? "Asteroids Value Low Percent" : "Planet Value Low Percent", asteroids ? 50 : 0);
            const int64_t hi = r.setting(asteroids ? "Asteroids Value High Percent" : "Planet Value High Percent", asteroids ? 300 : 150);
            int64_t delta = 0;
            for (int& value : obj.value) {
                const int before = value;
                if (s.options.finiteResources) {
                    value = static_cast<int>(std::max<int64_t>(0, int64_t{value} * (100 + amount) / 100));
                } else {
                    const int64_t lowest = std::min<int64_t>(lo, value), highest = std::max<int64_t>(hi, value);
                    value = static_cast<int>(std::clamp<int64_t>(int64_t{value} + amount, lowest, highest));
                }
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
            if (!col || emotionless(r, s, *col)) return out;
            const int before = col->anger;
            col->anger = std::clamp(before + amount, 0, 1000);
            out.actual = col->anger - before;
            break;
        }
        case Effect::PlanetPopulationRiot: {
            if (!col || emotionless(r, s, *col)) return out;
            const int before = col->anger;
            col->anger = std::max(before, 750);  // the Rioting band (types.hpp)
            out.actual = col->anger - before;
            break;
        }
        case Effect::PlanetPopulationRebel: {
            if (!col) return out;
            const EmpireId owner = col->owner;
            const bool home = col->homeworld;
            if (living(s, t.source)) {
                // The planet joins the operation's source.
                diplomacy::transferColony(s, t.object, t.source);
                ctx.mood(owner, "Any Planet Lost");
                if (home) ctx.mood(owner, "Homeworld Lost");
            } else {
                // No one to join: the planet breaks away and is lost (inferred).
                loseColony(ctx, t.object);
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
            out.report.push_back(std::format("{}: {} {}, atmosphere {}, conditions {}%", obj.name, obj.size, obj.surface, obj.atmosphere,
                                             obj.conditions));
            out.report.push_back(std::format("Value: minerals {}, organics {}, radioactives {}", obj.value[0], obj.value[1], obj.value[2]));
            for (const PopulationGroup& p : col->population)
                out.report.push_back(std::format("Population: {}M {}", p.millions,
                                                 validEmpire(s, p.race) ? s.empire(p.race).race.name : std::string("unknown")));
            out.report.push_back(std::format("Mood: {}", displayName(moodFromAnger(col->anger))));
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
            if (!validObject(s, t.object) || s.galaxy.object(t.object).kind != ObjectKind::Planet) return out;
            shatterPlanet(ctx, t.object, rng);
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
            if (!validObject(s, t.object) || s.galaxy.object(t.object).kind != ObjectKind::Star) return out;
            const SystemId sys = s.galaxy.object(t.object).system;
            SpaceObject& star = s.galaxy.object(t.object);
            star.kind = ObjectKind::DestroyedStar;
            if (auto st = pickSectorType(r, "Destroyed Star", {}, rng)) applySectorType(r, star, *st);
            // The shockwave leaves only warp points and rubble (spec 01 §9): planets
            // become asteroid fields and every vehicle in the system is lost (inferred).
            const std::vector<ObjectId> objects = s.galaxy.system(sys).objects;
            for (ObjectId o : objects)
                if (s.galaxy.object(o).kind == ObjectKind::Planet) shatterPlanet(ctx, o, rng);
            for (Vehicle& x : s.vehicles)
                if (x.location.system == sys) destroyVehicle(ctx, x);
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
                if (auto st = pickSectorType(r, "Warp Point", {}, rng)) {
                    wp.sectorType = *st;
                    wp.oneWay = false;
                }
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
            if (!validObject(s, t.object) || s.galaxy.object(t.object).kind != ObjectKind::WarpPoint) return out;
            // Both ends disappear from their systems (spec 01 §8).
            const ObjectId ends[2] = {t.object, s.galaxy.object(t.object).destination};
            for (ObjectId o : ends) {
                if (!validObject(s, o)) continue;
                SpaceObject& wp = s.galaxy.object(o);
                std::erase(s.galaxy.system(wp.system).objects, o);
                wp.destination = ObjectId{};
            }
            out.actual = 1;
            break;
        }

        case Effect::PointsChange:
            for (Resource res : kResources) {
                const int64_t before = victim.stockpile[res];
                victim.stockpile[res] = std::max<int64_t>(0, before + amount);
                out.actual += before > victim.stockpile[res] ? before - victim.stockpile[res] : victim.stockpile[res] - before;
            }
            break;
        case Effect::PointsSteal: {
            if (!living(s, t.source)) return out;
            Empire& thief = s.empire(t.source);
            const int64_t want = amount < 0 ? -int64_t{amount} : int64_t{amount};
            for (Resource res : kResources) {
                const int64_t take = std::clamp<int64_t>(victim.stockpile[res], 0, want);
                victim.stockpile[res] -= take;
                thief.stockpile[res] += take;
                out.actual += take;
            }
            break;
        }

        case Effect::ResearchSteal: {
            if (!living(s, t.source)) return out;
            auto area = choose(stealableAreas(r, s, t.source, t.empire), rng);
            if (!area) return out;
            out.tokens.techName = r.tech(*area).name;
            // One level beyond ours (spec 05 open question 6, inferred).
            research::grantLevel(ctx, t.source, *area, s.empire(t.source).techLevel(*area) + 1, "espionage");
            out.actual = s.empire(t.source).techLevel(*area);
            break;
        }
        case Effect::ResearchDeleteProject: {
            if (victim.research.empty()) return out;
            const size_t idx = rng.below(victim.research.size());
            out.tokens.techName = r.tech(victim.research[idx].area).name;
            out.actual = victim.research[idx].progress;
            victim.research.erase(victim.research.begin() + static_cast<std::ptrdiff_t>(idx));
            break;
        }
        case Effect::IntelDeleteProject: {
            if (victim.intel.empty()) return out;
            const size_t idx = rng.below(victim.intel.size());
            out.actual = victim.intel[idx].progress;
            victim.intel.erase(victim.intel.begin() + static_cast<std::ptrdiff_t>(idx));
            break;
        }

        case Effect::PoliticsDisruptTrade: {
            if (!living(s, t.other)) return out;
            Relation& a = victim.relation(t.other);
            Relation& b = s.empire(t.other).relation(t.empire);
            out.actual = a.tradePercent;
            // The trade percentage starts over (spec 05 open question 6, inferred).
            a.tradePercent = b.tradePercent = 0;
            break;
        }
        case Effect::PoliticsInterceptMessages: {
            if (!living(s, t.other)) return out;
            for (const DiplomaticMessage& m : s.messages) {
                const bool between = (m.from == t.empire && m.to == t.other) || (m.from == t.other && m.to == t.empire);
                if (!between || !m.delivered) continue;
                out.report.push_back(std::format("{} from the {} to the {}{}{}", displayName(m.type), empireFullName(s.empire(m.from)),
                                                 empireFullName(s.empire(m.to)), m.text.empty() ? "" : ": ", m.text));
                ++out.actual;
            }
            if (out.report.empty()) out.report.push_back("No recent messages passed between them.");
            break;
        }
        case Effect::PoliticsFakeMessages: {
            if (!living(s, t.other)) return out;
            // A forged demand that seems to come from the target (inferred message type).
            DiplomaticMessage m;
            m.id = MessageId{s.nextMessageId++};
            m.from = t.empire;
            m.to = t.other;
            m.sentTurn = s.turn;
            m.type = MessageType::DemandTribute;
            m.tone = 2;
            m.delivered = true;
            s.messages.push_back(m);
            ctx.log(t.other, LogCategory::Politics, std::string(displayName(m.type)), std::format("From the {}.", empireFullName(victim)));
            out.actual = 1;
            break;
        }
        case Effect::PoliticsPreventMessages: {
            if (!living(s, t.other)) return out;
            const uint32_t until = s.turn + 1 + static_cast<uint32_t>(std::max(1, amount));
            Relation& a = victim.relation(t.other);
            Relation& b = s.empire(t.other).relation(t.empire);
            a.messagesBlockedUntil = b.messagesBlockedUntil = std::max(a.messagesBlockedUntil, until);
            out.actual = std::max(1, amount);
            break;
        }
        case Effect::PoliticsTreatyInfo: {
            if (!living(s, t.other)) return out;
            const Relation& rel = victim.relation(t.other);
            out.tokens.treatyName = std::string(displayName(rel.treaty));
            out.report.push_back(std::format("The {} and the {}: {}", empireFullName(victim), empireFullName(s.empire(t.other)),
                                             displayName(rel.treaty)));
            out.actual = 1;
            break;
        }
        case Effect::SystemInfo: {
            if (!validSystem(s, t.system) || !living(s, t.source)) return out;
            explore(s, t.source, t.system);
            Empire& src = s.empire(t.source);
            for (ObjectId wp : s.galaxy.warpPoints(t.system))
                if (wp.index() < victim.knowledge.knownWarpLink.size() && victim.knowledge.knownWarpLink[wp.index()] &&
                    wp.index() < src.knowledge.knownWarpLink.size())
                    src.knowledge.knownWarpLink[wp.index()] = 1;
            setLocationTokens(out.tokens, s, Location{t.system, Sector{kSystemCenter, kSystemCenter}});
            out.actual = 1;
            break;
        }
        case Effect::EmpireInfo: {
            const TurnStats st = score::currentStats(r, s, t.empire);
            out.report.push_back(std::format("Stored resources: {} minerals, {} organics, {} radioactives", victim.stockpile[Resource::Minerals],
                                             victim.stockpile[Resource::Organics], victim.stockpile[Resource::Radioactives]));
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
            for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
                const int level = victim.techLevel(ruleset::TechAreaId{i});
                if (level > 0) out.report.push_back(std::format("{}: level {}", r.data().techAreas[i].name, level));
            }
            if (out.report.empty()) out.report.push_back("They know no technology.");
            out.actual = research::totalLevels(victim);
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

// Empires that hear about an event under its `Message To` setting.
std::vector<EmpireId> recipients(const GameState& s, const ruleset::EventType& ev, const Target& t, std::optional<Location> where) {
    std::vector<EmpireId> out;
    auto add = [&](EmpireId e) {
        if (e.valid() && e.index() < s.empires.size() && s.empire(e).alive && std::find(out.begin(), out.end(), e) == out.end())
            out.push_back(e);
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
        if (here(v.location)) add(v.owner);
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

void fire(TurnContext& ctx, uint32_t eventType, const Target& t, Rng& rng) {
    const ruleset::EventType& ev = ctx.rules.data().eventTypes[eventType];
    const auto effect = effects::parseEffect(ev.type);
    if (!effect) return;
    const std::optional<Location> where = effects::targetLocation(ctx.state, t);
    const effects::Outcome out = effects::apply(ctx, *effect, t, ev.effectAmount, rng);
    if (!out.applied) return;
    effects::Tokens tokens = baseTokens(ctx.state, t, out.tokens);
    tokens.actualAmount = out.actual < 0 ? -out.actual : out.actual;
    sendMessages(ctx, ev, ev.messages, t, tokens, where, rng);
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

std::vector<uint32_t> eligibleEvents(const Rules& r, const GameState& s) {
    std::vector<uint32_t> out;
    const auto& types = r.data().eventTypes;
    for (uint32_t i = 0; i < types.size(); ++i) {
        const auto effect = effects::parseEffect(types[i].type);
        if (!effect || effects::needsSource(*effect) || *effect == Effect::IntelligenceDefense) continue;
        if (static_cast<int>(parseSeverity(types[i].severity)) > s.options.maxEventSeverity) continue;
        out.push_back(i);
    }
    return out;
}

bool trigger(TurnContext& ctx, uint32_t eventType, const Target& target, Rng& rng) {
    GameState& s = ctx.state;
    if (eventType >= ctx.rules.data().eventTypes.size()) return false;
    const ruleset::EventType& ev = ctx.rules.data().eventTypes[eventType];
    const auto effect = effects::parseEffect(ev.type);
    if (!effect) return false;
    const auto t = effects::pickTarget(ctx.rules, s, *effect, target, rng);
    if (!t) return false;
    if (ev.turnsToComplete <= 0) {
        fire(ctx, eventType, *t, rng);
        return true;
    }
    // Timed: warn now, strike later (the realized amount is unknown yet).
    const std::optional<Location> where = effects::targetLocation(s, *t);
    effects::Tokens tokens;
    if (t->vehicle.valid())
        if (const Vehicle* v = s.vehicle(t->vehicle)) {
            tokens.vehicleName = v->name;
            tokens.designName = s.design(v->design).name;
            tokens.vehicleSize = ctx.rules.hull(s.design(v->design).hull).name;
        }
    if (t->object.valid() && t->object.index() < s.galaxy.objects.size()) {
        const SpaceObject& obj = s.galaxy.object(t->object);
        (obj.kind == ObjectKind::Star ? tokens.starName : obj.kind == ObjectKind::WarpPoint ? tokens.warpPointName : tokens.planetName) = obj.name;
    }
    if (where) {
        tokens.systemName = s.galaxy.system(where->system).name;
        tokens.sectorName = std::format("({}, {})", where->sector.x, where->sector.y);
    }
    tokens = baseTokens(s, *t, tokens);
    if (!ev.startMessages.empty()) sendMessages(ctx, ev, ev.startMessages, *t, tokens, where, rng);
    PendingEvent pe;
    pe.eventType = eventType;
    pe.empire = t->empire;
    pe.object = t->object;
    pe.vehicle = t->vehicle;
    pe.system = t->system;
    pe.fireTurn = s.turn + static_cast<uint32_t>(ev.turnsToComplete);
    s.pendingEvents.push_back(pe);
    return true;
}

void runEvents(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Rng rng = s.rng.fork();

    // Timed events that are due, in the order they were started.
    for (size_t i = 0; i < s.pendingEvents.size();) {
        if (s.pendingEvents[i].fireTurn > s.turn) {
            ++i;
            continue;
        }
        const PendingEvent pe = s.pendingEvents[i];
        s.pendingEvents.erase(s.pendingEvents.begin() + static_cast<std::ptrdiff_t>(i));
        if (pe.eventType >= r.data().eventTypes.size()) continue;
        const auto effect = effects::parseEffect(r.data().eventTypes[pe.eventType].type);
        if (!effect) continue;
        Target request;
        request.empire = pe.empire;
        request.object = pe.object;
        request.vehicle = pe.vehicle;
        request.system = pe.system;
        // The target must still be valid (a destroyed ship or lost colony ends the event).
        if (auto t = effects::pickTarget(r, s, *effect, request, rng)) fire(ctx, pe.eventType, *t, rng);
    }

    // New events: one roll per empire (inferred), then a uniform pick among
    // the eligible records and a random valid target of the affected empire.
    const int chance = eventChance(r, s);
    const std::vector<uint32_t> eligible = eligibleEvents(r, s);
    if (chance > 0 && !eligible.empty()) {
        for (size_t ei = 0; ei < s.empires.size(); ++ei) {
            if (!s.empires[ei].alive) continue;
            if (!rng.percent(chance)) continue;
            const EmpireId id = s.empires[ei].id;
            const uint32_t idx = eligible[rng.below(eligible.size())];
            const ruleset::EventType& ev = r.data().eventTypes[idx];
            const Effect effect = *effects::parseEffect(ev.type);
            Target request;
            request.empire = id;
            const auto t = effects::pickTarget(r, s, effect, request, rng);
            if (!t) continue;
            if (effects::isBad(effect, ev.effectAmount)) {
                // Luck (e.g. -50 halves the chance) and "Change Bad Event Chance - System".
                const int64_t luck = r.traitValue(s.empire(id).race, "Luck");
                const auto where = effects::targetLocation(s, *t);
                const int64_t sys = where ? effects::systemChanceModifier(r, s, id, where->system, AbilityKind::ChangeBadEventChanceSystem) : 0;
                const int64_t keep = std::max<int64_t>(0, 100 + luck) * std::max<int64_t>(0, 100 + sys) / 100;
                if (keep < 100 && !rng.percent(static_cast<int>(keep))) continue;
            }
            trigger(ctx, idx, *t, rng);
        }
    }
    s.removeDeadVehicles();
}

} // namespace opense4::game::events
