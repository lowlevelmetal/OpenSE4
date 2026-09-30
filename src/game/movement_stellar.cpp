// Stellar manipulation (spec 01 §9, confirmed: binary).
//
// Object ids stay stable: objects are converted in place where possible
// (asteroids <-> planet); new objects are appended; removed objects leave
// their system's object list and keep their record.

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/generate.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::movement {

namespace {
// Titles of the reports that destroy a planet or a star (isDestructiveStellarReport).
constexpr std::string_view kPlanetDestroyed = "Planet Destroyed: ";
constexpr std::string_view kStarDestroyed = "Star Destroyed: ";
} // namespace

bool isDestructiveStellarReport(std::string_view title) { return title.starts_with(kPlanetDestroyed) || title.starts_with(kStarDestroyed); }

} // namespace opense4::game::movement

namespace opense4::game::movement::detail {

namespace {

using datafile::keysEqual;

AbilityKind abilityFor(StellarAction a) {
    switch (a) {
        case StellarAction::CreatePlanet: return AbilityKind::CreatePlanetSize;
        case StellarAction::DestroyPlanet: return AbilityKind::DestroyPlanetSize;
        case StellarAction::CreateStar: return AbilityKind::CreateStar;
        case StellarAction::DestroyStar: return AbilityKind::DestroyStar;
        case StellarAction::OpenWarpPoint: return AbilityKind::OpenWarpPointDistance;
        case StellarAction::CloseWarpPoint: return AbilityKind::CloseWarpPoint;
        case StellarAction::CreateStorm: return AbilityKind::CreateStorm;
        case StellarAction::DestroyStorm: return AbilityKind::DestroyStorm;
        case StellarAction::CreateNebulae: return AbilityKind::CreateNebulae;
        case StellarAction::DestroyNebulae: return AbilityKind::DestroyNebulae;
        case StellarAction::CreateBlackHole: return AbilityKind::CreateBlackHole;
        case StellarAction::DestroyBlackHole: return AbilityKind::DestroyBlackHole;
        case StellarAction::CreateConstructedPlanet: return AbilityKind::CreateConstructedPlanet;
        case StellarAction::Count: break;
    }
    return AbilityKind::Unknown;
}

// A destroyed star is a star for every manipulation (spec 01 §5.4).
bool isStar(ObjectKind k) { return k == ObjectKind::Star || k == ObjectKind::DestroyedStar; }

bool matches(ObjectKind want, ObjectKind have) { return want == have || (isStar(want) && isStar(have)); }

struct Actor {
    VehicleId vehicle;
    size_t entry = 0;
    int64_t value = 0;
    int64_t supply = 0;  // supply the device uses
};

class Manipulation {
public:
    Manipulation(TurnContext& ctx, const Order& o) : ctx_(ctx), r_(ctx.rules), s_(ctx.state), o_(o) {}

    std::string run(std::span<const VehicleId> members, bool& consumed) {
        consumed = false;
        if (o_.amount < 0 || o_.amount >= static_cast<int>(StellarAction::Count)) return "Unknown stellar manipulation";
        action_ = static_cast<StellarAction>(o_.amount);
        if (std::string why = chooseActor(members); !why.empty()) return why;
        const Vehicle& v = *s_.vehicle(actor_.vehicle);
        owner_ = v.owner;
        here_ = v.location;
        name_ = v.name;
        const Design& d = s_.design(v.design);
        const DesignEntry entry = d.entries[actor_.entry];
        const auto abilities = r_.componentAbilities(entry.component);

        // A harmless no-op: closing a link that is already gone (a simultaneous-turn race).
        if (action_ == StellarAction::CloseWarpPoint && o_.object.valid() && o_.object.index() < s_.galaxy.objects.size() &&
            s_.galaxy.object(o_.object).kind == ObjectKind::WarpPoint && !inSystem(s_.galaxy, o_.object))
            return {};
        if (hostileHere()) return "A hostile presence in the sector prevents it";
        if (std::string why = perform(abilities); !why.empty()) return why;
        consumed = true;
        // Pay for the device; one-shot devices are used up (the ship may be gone already).
        if (Vehicle* after = s_.vehicle(actor_.vehicle); after && alive(*after) && !vehicleDestroyed(r_, s_, *after)) {
            spendSupply(r_, s_, *after, actor_.supply);
            if (hasAbility(abilities, AbilityKind::ComponentDestroyedOnUse)) {
                if (after->damage.size() < d.entries.size()) after->damage.resize(d.entries.size(), 0);
                after->damage[actor_.entry] = entryStructure(r_, d, actor_.entry);
            }
        }
        return {};
    }

private:
    StarSystem& system() { return s_.galaxy.system(here_.system); }

    // The first member with a working device that can act: movement remaining
    // (except Construct, which needs none; the movement is not spent), not
    // cloaked, and enough supply for the device (spec 01 §9, confirmed: binary).
    std::string chooseActor(std::span<const VehicleId> members) {
        const AbilityKind k = abilityFor(action_);
        std::string firstReason;
        for (VehicleId id : members) {
            const Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || v->status == VehicleStatus::Mothballed) continue;
            const Design& d = s_.design(v->design);
            for (size_t i = 0; i < d.entries.size(); ++i) {
                if (!entryIntact(r_, s_, *v, i)) continue;
                const auto ab = r_.componentAbilities(d.entries[i].component);
                if (!hasAbility(ab, k)) continue;
                const int64_t supply = scaledSupply(r_, s_, v->owner, mounted(r_, d.entries[i]).supplyUsed);
                std::string why;
                if (action_ != StellarAction::CreateConstructedPlanet && v->movement <= 0) why = "No movement left for stellar manipulation";
                else if (v->status == VehicleStatus::Cloaked) why = "A cloaked ship cannot manipulate stars";
                else if (v->supply < supply) why = "Not enough supply for stellar manipulation";
                if (why.empty()) {
                    actor_ = Actor{id, i, bestValue1(ab, k), supply};
                    return {};
                }
                if (firstReason.empty()) firstReason = why;
            }
        }
        return firstReason.empty() ? "No working component for that manipulation" : firstReason;
    }

    // A visible object in the sector owned by an empire without a treaty of
    // Non-Aggression or better with us (war, non-intercourse, none, or no contact).
    bool hostileTo(EmpireId other) const {
        if (!other.valid() || other == owner_ || other.index() >= s_.empires.size()) return false;
        const Relation& rel = s_.empire(owner_).relation(other);
        return !rel.contact || rel.treaty < Treaty::NonAggression;
    }

    bool hostileHere() const {
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location == here_ && hostileTo(v.owner) && sight::canSeeVehicle(r_, s_, owner_, v)) return true;
        for (ObjectId o : s_.galaxy.system(here_.system).objects) {
            if (s_.galaxy.object(o).sector != here_.sector) continue;
            if (const Colony* c = s_.colony(o); c && hostileTo(c->owner) && sight::canSeePlanet(r_, s_, owner_, o)) return true;
        }
        return false;
    }

    // The object an order names, or the first object of that kind in the sector; visible to us.
    std::optional<ObjectId> target(ObjectKind kind) {
        auto usable = [&](ObjectId id) {
            const SpaceObject& obj = s_.galaxy.object(id);
            return matches(kind, obj.kind) && inSystem(s_.galaxy, id) && obj.system == here_.system && obj.sector == here_.sector &&
                   sight::canSeePlanet(r_, s_, owner_, id);
        };
        if (o_.object.valid() && o_.object.index() < s_.galaxy.objects.size() && usable(o_.object)) return o_.object;
        for (ObjectId id : system().objects)
            if (usable(id)) return id;
        return std::nullopt;
    }

    // `Stop ...` abilities on any owned object: a ship's own abilities or a
    // colony's, whoever the owner, the acting empire included. `sector`
    // limits the search to one sector (Stop Planet Destroyer).
    bool blocked(SystemId sys, AbilityKind stop, std::optional<Sector> sector = std::nullopt) const {
        for (ObjectId o : s_.galaxy.system(sys).objects) {
            if (sector && s_.galaxy.object(o).sector != *sector) continue;
            if (const Colony* c = s_.colony(o); c && hasAbility(colonyAbilities(r_, s_, *c), stop)) return true;
        }
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.location.system != sys || (sector && v.location.sector != *sector)) continue;
            if (hasAbility(vehicleAbilities(r_, s_, v), stop)) return true;
        }
        return false;
    }

    uint32_t pick(const std::vector<uint32_t>& types) { return types[s_.rng.below(types.size())]; }

    int count(SystemId sys, auto&& pred) const {
        int n = 0;
        for (ObjectId o : s_.galaxy.system(sys).objects) n += pred(s_.galaxy.object(o)) ? 1 : 0;
        return n;
    }

    bool constructedWorld(const SpaceObject& obj) const {
        if (obj.kind != ObjectKind::Planet) return false;
        const ruleset::PlanetSize* ps = planetSize(r_, obj);
        return ps && ps->constructed;
    }

    // The planet's PlanetSize record number: its position in PlanetSize.txt, from 1.
    int64_t sizeRecordNumber(const SpaceObject& obj) const {
        const auto& sizes = r_.data().planetSizes;
        for (size_t i = 0; i < sizes.size(); ++i)
            if (keysEqual(sizes[i].physicalType, "Planet") && keysEqual(sizes[i].name, obj.size)) return static_cast<int64_t>(i) + 1;
        for (size_t i = 0; i < sizes.size(); ++i)
            if (keysEqual(sizes[i].name, obj.size)) return static_cast<int64_t>(i) + 1;
        return 0;
    }

    ObjectId append(SpaceObject obj, SystemId sys) {
        obj.id = ObjectId{s_.galaxy.objects.size()};
        obj.system = sys;
        s_.galaxy.system(sys).objects.push_back(obj.id);
        s_.galaxy.objects.push_back(std::move(obj));
        objectsAppended(s_);
        return s_.galaxy.objects.back().id;
    }

    void remove(ObjectId id) {
        SpaceObject& obj = s_.galaxy.object(id);
        std::erase(s_.galaxy.system(obj.system).objects, id);
        if (obj.kind == ObjectKind::WarpPoint) obj.destination = {};
    }

    void loseColony(ObjectId planet, std::string_view cause) {
        Colony* c = s_.colony(planet);
        if (!c) return;
        const EmpireId owner = c->owner;
        const SystemId sys = s_.galaxy.object(planet).system;
        ctx_.log(owner, LogCategory::Events, std::format("{} lost", s_.galaxy.object(planet).name), std::string(cause), locationOf(s_.galaxy, planet));
        ctx_.mood(owner, "Any Planet Lost", sys, planet);
        if (c->homeworld) ctx_.mood(owner, "Homeworld Lost", sys, planet);
        s_.colonies[planet.index()].reset();
    }

    // A planet or asteroid field becomes a random natural asteroid field that
    // keeps its name, values and conditions (of `size`, 0 = any).
    void toAsteroids(SpaceObject& obj, int size) {
        std::vector<uint32_t> types = naturalSectorTypes(r_.data(), ObjectKind::Asteroids, size);
        if (types.empty()) types = naturalSectorTypes(r_.data(), ObjectKind::Asteroids);
        obj.kind = ObjectKind::Asteroids;
        obj.abilities.clear();
        if (!types.empty()) applySectorType(r_.data(), obj, pick(types));
    }

    // The shockwave of a destroyed star: every planet and asteroid field
    // becomes a new asteroid field (colonies lost); everything else except
    // warp points is destroyed, ships and unit groups included.
    void shockwave(SystemId sys, std::string_view cause) {
        for (Vehicle& v : s_.vehicles)
            if (alive(v) && v.location.system == sys) vehicleLost(ctx_, v, cause);
        const std::vector<ObjectId> objects = s_.galaxy.system(sys).objects;
        for (ObjectId o : objects) {
            SpaceObject& obj = s_.galaxy.object(o);
            if (obj.kind == ObjectKind::WarpPoint) continue;
            if (obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) {
                loseColony(o, cause);
                toAsteroids(obj, 0);
            } else {
                remove(o);
            }
        }
    }

    // Created nebulae and black holes use no system type: the type is set
    // directly; we keep a matching record's backdrop when there is one.
    void setSystemKind(StarSystem& sys, std::string_view physical, std::vector<ruleset::Ability> abilities) {
        const auto& types = r_.data().systemTypes;
        std::vector<uint32_t> backdrops;
        for (uint32_t i = 0; i < types.size(); ++i) {
            if (!keysEqual(types[i].physicalType, physical)) continue;
            if (keysEqual(physical, "Normal") && (!types[i].empiresCanStartIn || !types[i].abilities.empty())) continue;
            backdrops.push_back(i);
        }
        if (!backdrops.empty()) {
            // A random nebula or black hole backdrop; the first standard system for a restored one (inferred).
            sys.type = ruleset::SystemTypeId{keysEqual(physical, "Normal") ? backdrops.front() : pick(backdrops)};
        }
        sys.physicalType = std::string(physical);
        sys.abilities = std::move(abilities);
    }

    static ruleset::Ability ability(AbilityKind k, int64_t v1) {
        ruleset::Ability a;
        a.type = std::string(identifier(k));
        a.value1 = std::to_string(v1);
        return a;
    }

    void announce(std::string title) { ctx_.log(owner_, LogCategory::Events, std::move(title), std::format("By {}.", name_), here_); }

    std::string planetName(SystemId sys) const { return std::format("{} {}", s_.galaxy.system(sys).name, romanNumeral(nextPlanetNumeral(s_.galaxy, sys))); }

    std::string perform(std::span<const ParsedAbility> abilities) {
        StarSystem& sys = system();
        const auto& rs = r_.data();
        switch (action_) {
            case StellarAction::CreatePlanet: {
                const auto field = target(ObjectKind::Asteroids);
                if (!field) return "No asteroid field here";
                if (count(sys.id, [](const SpaceObject& o) { return isStar(o.kind); }) == 0) return "A planet needs a star in the system";
                // Exactly min(Val 1, the field's size).
                const int size = static_cast<int>(std::min<int64_t>(actor_.value, stellarSizeOf(rs, s_.galaxy.object(*field))));
                const auto types = naturalSectorTypes(rs, ObjectKind::Planet, size);
                if (types.empty()) return "No planet of that size can be made";
                loseColony(*field, "The asteroid field became a planet.");
                const std::string name = planetName(sys.id);
                SpaceObject& obj = s_.galaxy.object(*field);
                obj.kind = ObjectKind::Planet;
                obj.abilities.clear();
                applySectorType(rs, obj, pick(types));
                obj.conditions = rollConditions(false, s_.rng);  // the values are kept
                obj.name = name;
                announce(std::format("Planet Created: {}", obj.name));
                return {};
            }
            case StellarAction::DestroyPlanet: {
                const auto planet = target(ObjectKind::Planet);
                if (!planet) return "No planet here";
                SpaceObject& obj = s_.galaxy.object(*planet);
                if (sizeRecordNumber(obj) > actor_.value) return "The planet is too large";
                if (blocked(sys.id, AbilityKind::StopPlanetDestroyer, obj.sector)) return "The planet is protected";
                loseColony(*planet, "The planet was destroyed.");
                toAsteroids(obj, stellarSizeOf(rs, obj));
                announce(std::format("{}{}", kPlanetDestroyed, obj.name));
                return {};
            }
            case StellarAction::CreateStar: {
                if (keysEqual(sys.physicalType, "Nebulae") || keysEqual(sys.physicalType, "Black Hole"))
                    return "Stars cannot be created in this system";
                if (count(sys.id, [](const SpaceObject& o) { return isStar(o.kind); }) > 0) return "The system already has a star";
                if (count(sys.id, [&](const SpaceObject& o) { return constructedWorld(o); }) > 0) return "A constructed world blocks it";
                const auto types = naturalSectorTypes(rs, ObjectKind::Star);
                if (types.empty()) return "No star type exists";
                SpaceObject star;
                star.kind = ObjectKind::Star;
                star.sector = here_.sector;
                applySectorType(rs, star, pick(types));
                star.name = sys.name + " Star";
                announce(std::format("Star Created: {}", star.name));
                append(std::move(star), sys.id);
                return {};
            }
            case StellarAction::DestroyStar: {
                const auto star = target(ObjectKind::Star);
                if (!star) return "No star here";
                if (blocked(sys.id, AbilityKind::StopStarDestroyer)) return "The star is protected";
                announce(std::format("{}{}", kStarDestroyed, s_.galaxy.object(*star).name));
                shockwave(sys.id, "A star exploded in the system.");
                return {};
            }
            case StellarAction::OpenWarpPoint: return openWarpPoint();
            case StellarAction::CloseWarpPoint: {
                const auto wp = target(ObjectKind::WarpPoint);
                if (!wp) return "No warp point here";
                const ObjectId far = s_.galaxy.object(*wp).destination;
                if (blocked(sys.id, AbilityKind::StopCloseWarpPoint) ||
                    (far.valid() && blocked(s_.galaxy.object(far).system, AbilityKind::StopCloseWarpPoint)))
                    return "Warp point closure is blocked";
                announce(std::format("Warp Point Closed: {}", sight::warpPointName(s_, owner_, *wp)));
                if (far.valid() && inSystem(s_.galaxy, far)) remove(far);
                remove(*wp);
                return {};
            }
            case StellarAction::CreateStorm: {
                const auto types = naturalSectorTypes(rs, ObjectKind::Storm);
                SpaceObject storm;
                storm.kind = ObjectKind::Storm;
                storm.sector = here_.sector;
                if (!types.empty()) applySectorType(rs, storm, pick(types));
                // One effect drawn uniformly, redrawing those whose setting is 0 or less;
                // its value is the setting itself.
                const std::array<std::pair<AbilityKind, int64_t>, 3> effects{{
                    {AbilityKind::SectorSightObscuration, r_.setting("Created Storm Maximum Obscuration Level", 0)},
                    {AbilityKind::SectorDamage, r_.setting("Created Storm Maximum Turbulence Damage", 0)},
                    {AbilityKind::SectorShieldDisruption, r_.setting("Created Storm Maximum Shield Disruption", 0)},
                }};
                if (std::any_of(effects.begin(), effects.end(), [](const auto& e) { return e.second > 0; })) {
                    size_t k = s_.rng.below(effects.size());
                    while (effects[k].second <= 0) k = s_.rng.below(effects.size());
                    storm.abilities.push_back(ability(effects[k].first, effects[k].second));
                }
                storm.name = "Storm";
                announce(std::format("Storm created in {}", sys.name));
                append(std::move(storm), sys.id);
                return {};
            }
            case StellarAction::DestroyStorm: {
                const auto storm = target(ObjectKind::Storm);
                if (!storm) return "No storm here";
                announce(std::format("Storm destroyed in {}", sys.name));
                remove(*storm);
                return {};
            }
            case StellarAction::CreateNebulae:
            case StellarAction::CreateBlackHole: {
                const bool nebula = action_ == StellarAction::CreateNebulae;
                const auto star = target(ObjectKind::Star);
                if (!star) return "No star here";
                if (blocked(sys.id, nebula ? AbilityKind::StopNebulaeCreator : AbilityKind::StopBlackHoleCreator))
                    return "The star is protected";
                announce(std::format("{}{}", kStarDestroyed, s_.galaxy.object(*star).name));
                announce(std::format("{} created in {}", nebula ? "Nebula" : "Black hole", sys.name));
                shockwave(sys.id, nebula ? "The system became a nebula." : "The system collapsed into a black hole.");
                if (nebula) setSystemKind(system(), "Nebulae", {ability(AbilityKind::SectorSightObscuration, 3)});
                else
                    setSystemKind(system(), "Black Hole",
                                  {ability(AbilityKind::SystemMovementTowardsCenter, 2), ability(AbilityKind::SystemDestructiveCenter, 5000),
                                   ability(AbilityKind::SectorShieldDisruption, 5000)});
                return {};
            }
            case StellarAction::DestroyNebulae:
            case StellarAction::DestroyBlackHole: {
                const bool nebula = action_ == StellarAction::DestroyNebulae;
                if (!keysEqual(sys.physicalType, nebula ? "Nebulae" : "Black Hole"))
                    return nebula ? "This system is not a nebula" : "This system is not a black hole";
                announce(std::format("{} removed from {}", nebula ? "Nebula" : "Black hole", sys.name));
                setSystemKind(sys, "Normal", {});  // a standard, start-eligible system; the objects stay
                return {};
            }
            case StellarAction::CreateConstructedPlanet: return construct(abilities);
            case StellarAction::Count: break;
        }
        return "Unknown stellar manipulation";
    }

    std::string openWarpPoint() {
        const SystemId from = here_.system;
        const SystemId to = o_.location.system;
        if (!to.valid() || to.index() >= s_.galaxy.systems.size() || to == from) return "Choose another system to open a warp point to";
        const GalaxyPos a = s_.galaxy.system(from).position, b = s_.galaxy.system(to).position;
        if (galaxyDistance(a, b) > actor_.value) return "That system is out of range";  // Val 1 in quadrant squares
        const auto nb = s_.galaxy.neighbors(from);
        if (std::find(nb.begin(), nb.end(), to) != nb.end()) return "A warp point already leads there";
        if (static_cast<int>(s_.galaxy.warpPoints(from).size()) >= kMaxWarpPoints ||
            static_cast<int>(s_.galaxy.warpPoints(to).size()) >= kMaxWarpPoints)
            return "Too many warp points";
        if (blocked(from, AbilityKind::StopOpenWarpPoint) || blocked(to, AbilityKind::StopOpenWarpPoint)) return "Warp point creation is blocked";
        // Both ends use the first plain warp point record and carry no ability.
        std::optional<uint32_t> type;
        const auto& types = r_.data().sectorObjectTypes;
        for (uint32_t i = 0; i < types.size() && !type; ++i)
            if (parseObjectKind(types[i].physicalType) == ObjectKind::WarpPoint && !types[i].unusual) type = i;
        for (uint32_t i = 0; i < types.size() && !type; ++i)
            if (parseObjectKind(types[i].physicalType) == ObjectKind::WarpPoint) type = i;
        auto make = [&](SystemId where, Sector sector) {
            SpaceObject wp;
            wp.kind = ObjectKind::WarpPoint;
            wp.sector = sector;
            if (type) wp.sectorType = *type;
            wp.name = "Warp Point";
            return append(std::move(wp), where);
        };
        // The far end goes on the target's edge facing this system (edge placement, spec 01 §3.5).
        const Sector farSector = warpEdgeSector(galaxyBearing(b, a), placedWarpPoints(s_.galaxy, to));
        const ObjectId near = make(from, here_.sector);
        const ObjectId far = make(to, farSector);
        s_.galaxy.object(near).destination = far;
        s_.galaxy.object(far).destination = near;
        sight::learnWarpLink(s_, owner_, near);
        announce(std::format("Warp Point Opened to {}", s_.galaxy.system(to).name));
        return {};
    }

    std::string construct(std::span<const ParsedAbility> abilities) {
        const auto star = target(ObjectKind::Star);
        if (!star) return "No star here";
        const ruleset::PlanetSize* size = nullptr;
        for (const auto& ps : r_.data().planetSizes)
            if (ps.constructed && ps.specialAbilityId == actor_.value) size = &ps;
        if (!size) return "Unknown constructed planet";
        // Every requirement: the ships in this sector, whoever owns them, carry at
        // least Val 2 kT of components whose Custom Group is Val 1.
        std::vector<std::pair<int, int64_t>> needs;
        for (const ParsedAbility& a : abilities)
            if (a.kind == AbilityKind::ConstructedPlanetRequirements) needs.emplace_back(static_cast<int>(a.value1), a.value2);
        auto inGroup = [&](const Vehicle& v, size_t i, int group) {
            return entryIntact(r_, s_, v, i) && r_.component(s_.design(v.design).entries[i].component).customGroup == group;
        };
        for (const auto& [group, tons] : needs) {
            int64_t have = 0;
            for (const Vehicle& v : s_.vehicles) {
                if (!alive(v) || v.location != here_) continue;
                const Design& d = s_.design(v.design);
                for (size_t i = 0; i < d.entries.size(); ++i)
                    if (inGroup(v, i, group)) have += mounted(r_, d.entries[i]).tonnage;
            }
            if (have < tons) return "The construction materials are not all here";
        }

        // A planet of that size, of the builder's type and atmosphere when such a record exists.
        const Race& race = s_.empire(owner_).race;
        const auto& types = r_.data().sectorObjectTypes;
        std::vector<uint32_t> own, any;
        for (uint32_t i = 0; i < types.size(); ++i) {
            if (parseObjectKind(types[i].physicalType) != ObjectKind::Planet || !keysEqual(types[i].planetSize, size->name)) continue;
            any.push_back(i);
            if (keysEqual(types[i].planetPhysicalType, race.nativeSurface) && keysEqual(types[i].planetAtmosphere, race.atmosphere)) own.push_back(i);
        }
        const SystemId sysId = here_.system;
        SpaceObject world;
        world.kind = ObjectKind::Planet;
        world.sector = here_.sector;
        if (!own.empty() || !any.empty()) applySectorType(r_.data(), world, pick(own.empty() ? any : own));
        world.size = size->name;
        if (world.surface.empty()) world.surface = race.nativeSurface;
        if (world.atmosphere.empty()) world.atmosphere = race.atmosphere;
        const bool finite = s_.options.finiteResources;
        const int value = static_cast<int>(r_.setting(finite ? "Planet Value High Resources" : "Planet Value High Percent", finite ? 50000 : 150));
        world.value = {value, value, value};
        world.conditions = 150;  // Optimal (1.5)
        world.name = planetName(sysId);
        announce(std::format("Planet Created: {}", world.name));
        remove(*star);  // the star is used up
        append(std::move(world), sysId);
        // Every builder ship here carrying the device or any required material is destroyed.
        for (Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.owner != owner_ || v.location != here_) continue;
            const Design& d = s_.design(v.design);
            bool used = false;
            for (size_t i = 0; i < d.entries.size() && !used; ++i) {
                used = hasAbility(r_.componentAbilities(d.entries[i].component), AbilityKind::CreateConstructedPlanet);
                for (const auto& need : needs) used = used || r_.component(d.entries[i].component).customGroup == need.first;
            }
            if (used) vehicleLost(ctx_, v, "Used up in planet construction.");
        }
        return {};
    }

    TurnContext& ctx_;
    const Rules& r_;
    GameState& s_;
    const Order& o_;
    StellarAction action_ = StellarAction::Count;
    Actor actor_;
    EmpireId owner_;
    Location here_;
    std::string name_;
};

} // namespace

std::optional<Location> stellarTarget(const GameState& s, const Order& o, Location here) {
    if (o.amount < 0 || o.amount >= static_cast<int>(StellarAction::Count)) return std::nullopt;
    if (static_cast<StellarAction>(o.amount) == StellarAction::OpenWarpPoint) return here;
    if (o.object.valid()) {
        if (o.object.index() >= s.galaxy.objects.size()) return std::nullopt;
        if (!inSystem(s.galaxy, o.object)) {
            // Closing a link that is already closed is a harmless no-op, done where the ship is.
            if (static_cast<StellarAction>(o.amount) == StellarAction::CloseWarpPoint) return here;
            return std::nullopt;
        }
        return locationOf(s.galaxy, o.object);
    }
    if (o.location.system.valid() && o.location.system.index() < s.galaxy.systems.size() && o.location.sector.valid()) return o.location;
    return here;
}

std::string stellarManipulation(TurnContext& ctx, std::span<const VehicleId> members, const Order& o, bool& consumed) {
    Manipulation m(ctx, o);
    return m.run(members, consumed);
}

void objectsAppended(GameState& s) {
    s.colonies.resize(s.galaxy.objects.size());
    for (Empire& e : s.empires) e.knowledge.knownWarpLink.resize(s.galaxy.objects.size(), s.options.omnipresent ? 1 : 0);
}

} // namespace opense4::game::movement::detail
