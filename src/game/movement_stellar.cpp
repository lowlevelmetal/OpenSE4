// Stellar manipulation (spec 01 §9, confirmed: binary).
//
// Object ids stay stable: objects are converted in place where possible
// (asteroids <-> planet); new objects are appended; removed objects leave
// their system's object list and keep their record.

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/events.hpp"
#include "game/generate.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::game::movement {

namespace {
// Titles of the reports that destroy a planet or a star (isDestructiveStellarReport).
constexpr std::string_view kPlanetDestroyed = "Planet Destroyed: ";
constexpr std::string_view kStarDestroyed = "Star Destroyed: ";
} // namespace

bool isDestructiveStellarReport(std::string_view title) { return title.starts_with(kPlanetDestroyed) || title.starts_with(kStarDestroyed); }

std::string stellarReportText(const GameState& s, EmpireId culprit, std::string_view vehicle) {
    return std::format("By {} of the {}.", vehicle, effects::empireFullName(s.empire(culprit)));
}

bool stellarReportNames(const GameState& s, const LogEntry& entry, EmpireId culprit) {
    if (!isDestructiveStellarReport(entry.title) || !culprit.valid() || culprit.index() >= s.empires.size()) return false;
    return entry.text.ends_with(std::format(" of the {}.", effects::empireFullName(s.empire(culprit))));
}

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
bool isStar(ObjectKind k) { return isStarKind(k); }

bool matches(ObjectKind want, ObjectKind have) { return want == have || (isStar(want) && isStar(have)); }

// A colony lost to a stellar manipulation or event: its owner is told why.
// `Homeworld Lost` goes with the colony type "Homeworld", which every starting
// planet has (spec 01 §3.6, spec 02 §2).
void loseColony(TurnContext& ctx, ObjectId planet, std::string_view cause) {
    GameState& s = ctx.state;
    Colony* c = s.colony(planet);
    if (!c) return;
    const EmpireId owner = c->owner;
    const SystemId sys = s.galaxy.object(planet).system;
    ctx.log(owner, LogCategory::Events, std::format("{} lost", s.galaxy.object(planet).name), std::string(cause), locationOf(s.galaxy, planet));
    addHistory(s, owner, owner, std::format("Lost the colony on {}. {}", s.galaxy.object(planet).name, cause), locationOf(s.galaxy, planet));
    ctx.mood(owner, "Any Planet Lost", sys, planet);
    if (keysEqual(c->colonyType, "Homeworld")) ctx.mood(owner, "Homeworld Lost", sys, planet);
    s.colonies[planet.index()].reset();
}

// A planet or asteroid field becomes a random natural asteroid field that
// keeps its name, values and conditions (of `size`, 0 = any).
void toAsteroids(const Rules& r, SpaceObject& obj, int size, Rng& rng) {
    std::vector<uint32_t> types = naturalSectorTypes(r.data(), ObjectKind::Asteroids, size);
    if (types.empty()) types = naturalSectorTypes(r.data(), ObjectKind::Asteroids);
    obj.kind = ObjectKind::Asteroids;
    obj.abilities.clear();
    if (!types.empty()) applySectorType(r.data(), obj, types[rng.below(types.size())]);
}

// An object leaves its system's object list; its record stays (ids are stable).
void removeObject(GameState& s, ObjectId id) {
    SpaceObject& obj = s.galaxy.object(id);
    std::erase(s.galaxy.system(obj.system).objects, id);
    if (obj.kind == ObjectKind::WarpPoint) obj.destination = {};
}

// The shockwave of a destroyed star (spec 01 §9): every planet and asteroid
// field becomes a new asteroid field of any size that keeps its name, values
// and conditions (colonies lost); everything else except warp points is
// destroyed, ships and unit groups included.
void shockwave(TurnContext& ctx, SystemId sys, std::string_view cause, Rng& rng) {
    GameState& s = ctx.state;
    for (Vehicle& v : s.vehicles)
        if (alive(v) && v.location.system == sys) vehicleLost(ctx, v, cause);
    const std::vector<ObjectId> objects = s.galaxy.system(sys).objects;
    for (ObjectId o : objects) {
        SpaceObject& obj = s.galaxy.object(o);
        if (obj.kind == ObjectKind::WarpPoint) continue;
        if (obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) {
            loseColony(ctx, o, cause);
            toAsteroids(ctx.rules, s.galaxy.object(o), 0, rng);
        } else {
            removeObject(s, o);
        }
    }
}

struct Actor {
    VehicleId vehicle;
    size_t entry = 0;
    int64_t value = 0;
    int64_t supply = 0;  // supply the device uses
};

// What the checks found for the manipulation to act on.
struct Plan {
    std::optional<ObjectId> object;              // the asteroid field, planet, star, warp point or storm
    std::vector<uint32_t> types;                 // Create Planet, Create Star: the records to draw from
    const ruleset::PlanetSize* world = nullptr;  // Construct: the planet size to build
    std::vector<std::pair<int, int64_t>> needs;  // Construct: (Custom Group, kT) requirements
    SystemId to;                                 // Open Warp Point: the destination
};

// The checks of a stellar manipulation (spec 01 §9, confirmed: binary), on a
// state they do not change. `planning`: the order is being given, not
// carried out, so the movement test uses the movement the vehicle will have
// when the next movement phase starts, and Open Warp Point without a
// destination skips the tests that need one.
class Checks {
public:
    Checks(const Rules& r, const GameState& s, const Order& o, bool planning) : r_(r), cs_(s), o_(o), planning_(planning) {}

    // Empty when the manipulation can happen now; noop() then tells a
    // harmless no-op (closing a link that is already gone).
    std::string check(std::span<const VehicleId> members) {
        if (o_.amount < 0 || o_.amount >= static_cast<int>(StellarAction::Count)) return "Unknown stellar manipulation";
        action_ = static_cast<StellarAction>(o_.amount);
        if (std::string why = chooseActor(members); !why.empty()) return why;
        const Vehicle& v = *cs_.vehicle(actor_.vehicle);
        owner_ = v.owner;
        here_ = v.location;
        name_ = v.name;
        abilities_ = r_.componentAbilities(cs_.design(v.design).entries[actor_.entry].component);
        // A harmless no-op: closing a link that is already gone (a simultaneous-turn race).
        if (action_ == StellarAction::CloseWarpPoint && o_.object.valid() && o_.object.index() < cs_.galaxy.objects.size() &&
            cs_.galaxy.object(o_.object).kind == ObjectKind::WarpPoint && !inSystem(cs_.galaxy, o_.object)) {
            noop_ = true;
            return {};
        }
        // Destroy Planet makes no such check (spec 01 §9, confirmed: binary).
        if (action_ != StellarAction::DestroyPlanet && hostileHere()) return "A hostile presence in the sector prevents it";
        return conditions();
    }

    bool noop() const { return noop_; }
    const Plan& plan() const { return plan_; }

protected:
    const StarSystem& system() const { return cs_.galaxy.system(here_.system); }

    // The first member with a working device that can act: movement remaining
    // (except Construct, which needs none; the movement is not spent), not
    // cloaked, and enough supply for the device (spec 01 §9, confirmed: binary).
    std::string chooseActor(std::span<const VehicleId> members) {
        const AbilityKind k = abilityFor(action_);
        std::string firstReason;
        for (VehicleId id : members) {
            const Vehicle* v = cs_.vehicle(id);
            if (!v || !alive(*v) || v->status == VehicleStatus::Mothballed) continue;
            const Design& d = cs_.design(v->design);
            const int movement = planning_ ? turnMovement(r_, cs_, *v) : v->movement;
            for (size_t i = 0; i < d.entries.size(); ++i) {
                if (!entryIntact(r_, cs_, *v, i)) continue;
                const auto ab = r_.componentAbilities(d.entries[i].component);
                if (!hasAbility(ab, k)) continue;
                const int64_t supply = scaledSupply(r_, cs_, v->owner, mounted(r_, d.entries[i]).supplyUsed);
                std::string why;
                if (action_ != StellarAction::CreateConstructedPlanet && movement <= 0) why = "No movement left for stellar manipulation";
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

    // An owner without a treaty of Non-Aggression or better with us (war,
    // non-intercourse, none, or no contact).
    bool hostileTo(EmpireId other) const {
        if (!other.valid() || other == owner_ || other.index() >= cs_.empires.size()) return false;
        const Relation& rel = cs_.empire(owner_).relation(other);
        return !rel.contact || rel.treaty < Treaty::NonAggression;
    }

    // A hostile object in the sector that we can see: a ship or base that is
    // not mothballed, or a colonized planet or asteroid field. Unit groups
    // (fighters, mines, satellites, drones) never block (spec 01 §9, §14 Q34,
    // confirmed: binary).
    bool hostileHere() const {
        for (const Vehicle& v : cs_.vehicles)
            if (alive(v) && v.location == here_ && v.status != VehicleStatus::Mothballed && isShipOrBase(vehicleType(r_, cs_, v)) &&
                hostileTo(v.owner) && sight::canSeeVehicle(r_, cs_, owner_, v))
                return true;
        for (ObjectId o : cs_.galaxy.system(here_.system).objects) {
            if (cs_.galaxy.object(o).sector != here_.sector) continue;
            if (const Colony* c = cs_.colony(o); c && hostileTo(c->owner) && sight::canSeePlanet(r_, cs_, owner_, o)) return true;
        }
        return false;
    }

    // The object an order names, or the first object of that kind in the sector;
    // visible to us. `uncolonized`: only one without a colony.
    std::optional<ObjectId> target(ObjectKind kind, bool uncolonized = false) const {
        auto usable = [&](ObjectId id) {
            const SpaceObject& obj = cs_.galaxy.object(id);
            return matches(kind, obj.kind) && inSystem(cs_.galaxy, id) && obj.system == here_.system && obj.sector == here_.sector &&
                   (!uncolonized || !cs_.colony(id)) && sight::canSeePlanet(r_, cs_, owner_, id);
        };
        if (o_.object.valid() && o_.object.index() < cs_.galaxy.objects.size() && usable(o_.object)) return o_.object;
        for (ObjectId id : system().objects)
            if (usable(id)) return id;
        return std::nullopt;
    }

    // `Stop ...` abilities on any owned object: a ship's own abilities or a
    // colony's, whoever the owner, the acting empire included. `sector`
    // limits the search to one sector (Stop Planet Destroyer).
    bool blocked(SystemId sys, AbilityKind stop, std::optional<Sector> sector = std::nullopt) const {
        for (ObjectId o : cs_.galaxy.system(sys).objects) {
            if (sector && cs_.galaxy.object(o).sector != *sector) continue;
            if (const Colony* c = cs_.colony(o); c && hasAbility(colonyAbilities(r_, cs_, *c), stop)) return true;
        }
        for (const Vehicle& v : cs_.vehicles) {
            if (!alive(v) || v.location.system != sys || (sector && v.location.sector != *sector)) continue;
            if (hasAbility(vehicleAbilities(r_, cs_, v), stop)) return true;
        }
        return false;
    }

    int count(SystemId sys, auto&& pred) const {
        int n = 0;
        for (ObjectId o : cs_.galaxy.system(sys).objects) n += pred(cs_.galaxy.object(o)) ? 1 : 0;
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

    // Each action's own conditions; what it will act on goes into plan_.
    std::string conditions() {
        const StarSystem& sys = system();
        const auto& rs = r_.data();
        switch (action_) {
            case StellarAction::CreatePlanet: {
                // A colonized asteroid field is not a valid target (confirmed: binary).
                plan_.object = target(ObjectKind::Asteroids, true);
                if (!plan_.object) return "No asteroid field without a colony here";
                if (count(sys.id, [](const SpaceObject& o) { return isStar(o.kind); }) == 0) return "A planet needs a star in the system";
                // Exactly min(Val 1, the field's size).
                const int size = static_cast<int>(std::min<int64_t>(actor_.value, stellarSizeOf(rs, cs_.galaxy.object(*plan_.object))));
                plan_.types = naturalSectorTypes(rs, ObjectKind::Planet, size);
                if (plan_.types.empty()) return "No planet of that size can be made";
                return {};
            }
            case StellarAction::DestroyPlanet: {
                plan_.object = target(ObjectKind::Planet);
                if (!plan_.object) return "No planet here";
                const SpaceObject& obj = cs_.galaxy.object(*plan_.object);
                if (sizeRecordNumber(obj) > actor_.value) return "The planet is too large";
                if (blocked(sys.id, AbilityKind::StopPlanetDestroyer, obj.sector)) return "The planet is protected";
                return {};
            }
            case StellarAction::CreateStar:
                if (keysEqual(sys.physicalType, "Nebulae") || keysEqual(sys.physicalType, "Black Hole"))
                    return "Stars cannot be created in this system";
                if (count(sys.id, [](const SpaceObject& o) { return isStar(o.kind); }) > 0) return "The system already has a star";
                if (count(sys.id, [&](const SpaceObject& o) { return constructedWorld(o); }) > 0) return "A constructed world blocks it";
                plan_.types = naturalSectorTypes(rs, ObjectKind::Star);
                if (plan_.types.empty()) return "No star type exists";
                return {};
            case StellarAction::DestroyStar:
                plan_.object = target(ObjectKind::Star);
                if (!plan_.object) return "No star here";
                if (blocked(sys.id, AbilityKind::StopStarDestroyer)) return "The star is protected";
                return {};
            case StellarAction::OpenWarpPoint: return openWarpPointConditions();
            case StellarAction::CloseWarpPoint: {
                plan_.object = target(ObjectKind::WarpPoint);
                if (!plan_.object) return "No warp point here";
                const ObjectId far = cs_.galaxy.object(*plan_.object).destination;
                if (blocked(sys.id, AbilityKind::StopCloseWarpPoint) ||
                    (far.valid() && blocked(cs_.galaxy.object(far).system, AbilityKind::StopCloseWarpPoint)))
                    return "Warp point closure is blocked";
                return {};
            }
            case StellarAction::CreateStorm: return {};
            case StellarAction::DestroyStorm:
                plan_.object = target(ObjectKind::Storm);
                if (!plan_.object) return "No storm here";
                return {};
            case StellarAction::CreateNebulae:
            case StellarAction::CreateBlackHole: {
                const bool nebula = action_ == StellarAction::CreateNebulae;
                plan_.object = target(ObjectKind::Star);
                if (!plan_.object) return "No star here";
                if (blocked(sys.id, nebula ? AbilityKind::StopNebulaeCreator : AbilityKind::StopBlackHoleCreator)) return "The star is protected";
                return {};
            }
            case StellarAction::DestroyNebulae:
            case StellarAction::DestroyBlackHole: {
                const bool nebula = action_ == StellarAction::DestroyNebulae;
                if (!keysEqual(sys.physicalType, nebula ? "Nebulae" : "Black Hole"))
                    return nebula ? "This system is not a nebula" : "This system is not a black hole";
                return {};
            }
            case StellarAction::CreateConstructedPlanet: return constructConditions();
            case StellarAction::Count: break;
        }
        return "Unknown stellar manipulation";
    }

    std::string openWarpPointConditions() {
        const SystemId from = here_.system;
        const SystemId to = o_.location.system;
        const bool anyDestination = planning_ && !to.valid();  // the destination is picked later
        if (anyDestination) {
            if (static_cast<int>(cs_.galaxy.warpPoints(from).size()) >= kMaxWarpPoints) return "Too many warp points";
            if (blocked(from, AbilityKind::StopOpenWarpPoint)) return "Warp point creation is blocked";
            return {};
        }
        if (!to.valid() || to.index() >= cs_.galaxy.systems.size() || to == from) return "Choose another system to open a warp point to";
        const GalaxyPos a = cs_.galaxy.system(from).position, b = cs_.galaxy.system(to).position;
        if (galaxyDistance(a, b) > actor_.value) return "That system is out of range";  // Val 1 in quadrant squares
        const auto nb = cs_.galaxy.neighbors(from);
        if (std::find(nb.begin(), nb.end(), to) != nb.end()) return "A warp point already leads there";
        if (static_cast<int>(cs_.galaxy.warpPoints(from).size()) >= kMaxWarpPoints ||
            static_cast<int>(cs_.galaxy.warpPoints(to).size()) >= kMaxWarpPoints)
            return "Too many warp points";
        if (blocked(from, AbilityKind::StopOpenWarpPoint) || blocked(to, AbilityKind::StopOpenWarpPoint)) return "Warp point creation is blocked";
        plan_.to = to;
        return {};
    }

    std::string constructConditions() {
        plan_.object = target(ObjectKind::Star);
        if (!plan_.object) return "No star here";
        for (const auto& ps : r_.data().planetSizes)
            if (ps.constructed && ps.specialAbilityId == actor_.value) plan_.world = &ps;
        if (!plan_.world) return "Unknown constructed planet";
        // Every requirement: the ships in this sector, whoever owns them, carry at
        // least Val 2 kT of components whose Custom Group is Val 1. The count goes
        // by design: every such component, damaged or not, with its mounted size;
        // mothballed ships and unit groups do not count (spec 01 §9, §14 Q33,
        // confirmed: binary). Bases count like ships (inferred, spec 01 §14 Q42).
        for (const ParsedAbility& a : abilities_)
            if (a.kind == AbilityKind::ConstructedPlanetRequirements) plan_.needs.emplace_back(static_cast<int>(a.value1), a.value2);
        for (const auto& [group, tons] : plan_.needs) {
            int64_t have = 0;
            for (const Vehicle& v : cs_.vehicles) {
                if (!alive(v) || v.location != here_ || v.status == VehicleStatus::Mothballed || !isShipOrBase(vehicleType(r_, cs_, v))) continue;
                const Design& d = cs_.design(v.design);
                for (size_t i = 0; i < d.entries.size(); ++i)
                    if (r_.component(d.entries[i].component).customGroup == group) have += mounted(r_, d.entries[i]).tonnage;
            }
            if (have < tons) return "The construction materials are not all here";
        }
        return {};
    }

    const Rules& r_;
    const GameState& cs_;
    const Order& o_;
    bool planning_ = false;
    StellarAction action_ = StellarAction::Count;
    Actor actor_;
    std::span<const ParsedAbility> abilities_;
    EmpireId owner_;
    Location here_;
    std::string name_;
    Plan plan_;
    bool noop_ = false;
};

// A manipulation carried out during the movement phase: the checks, then the result.
class Manipulation : public Checks {
public:
    Manipulation(TurnContext& ctx, const Order& o) : Checks(ctx.rules, ctx.state, o, false), ctx_(ctx), s_(ctx.state) {}

    std::string run(std::span<const VehicleId> members, bool& consumed) {
        consumed = false;
        if (std::string why = check(members); !why.empty()) return why;
        if (noop()) return {};
        perform();
        consumed = true;
        // Pay for the device; one-shot devices are used up (the ship may be gone already).
        if (Vehicle* after = s_.vehicle(actor_.vehicle); after && alive(*after) && !vehicleDestroyed(r_, s_, *after)) {
            spendSupply(r_, s_, *after, actor_.supply);
            if (hasAbility(abilities_, AbilityKind::ComponentDestroyedOnUse)) {
                const Design& d = s_.design(after->design);
                if (after->damage.size() < d.entries.size()) after->damage.resize(d.entries.size(), 0);
                after->damage[actor_.entry] = entryStructure(r_, d, actor_.entry);
                fitToCapacity(r_, s_, *after);  // storage the device held goes with it (spec 03 §7, §11)
            }
        }
        return {};
    }

private:
    StarSystem& system() { return s_.galaxy.system(here_.system); }

    uint32_t pick(const std::vector<uint32_t>& types) { return types[s_.rng.below(types.size())]; }

    ObjectId append(SpaceObject obj, SystemId sys) {
        obj.id = ObjectId{s_.galaxy.objects.size()};
        obj.system = sys;
        s_.galaxy.system(sys).objects.push_back(obj.id);
        s_.galaxy.objects.push_back(std::move(obj));
        objectsAppended(s_);
        return s_.galaxy.objects.back().id;
    }

    void remove(ObjectId id) { removeObject(s_, id); }

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

    void announce(std::string title) {
        addHistory(s_, owner_, owner_, std::format("{} (by {})", title, name_), here_);
        const std::string text = stellarReportText(s_, owner_, name_);
        // A destroyed planet or star (a new nebula or black hole reports the
        // star it consumed) is reported to every empire present in the system
        // when it happens, naming the empire responsible: what the computer
        // players' anger term 2 counts in their own logs (spec 05 §7.3, open
        // question 44, confirmed: binary).
        if (isDestructiveStellarReport(title))
            for (EmpireId w : witnesses_)
                if (w != owner_) ctx_.log(w, LogCategory::Events, title, text, here_);
        ctx_.log(owner_, LogCategory::Events, std::move(title), text, here_);
    }

    // The empires present in the system: a ship, base, colony, or fighter,
    // satellite or drone group there; mine fields do not count (spec 05 §7.3).
    // Taken as the manipulation is carried out, before its result removes
    // anything (inferred, spec 05 open question 50).
    void noteWitnesses() {
        witnesses_.clear();
        auto add = [&](EmpireId e) {
            if (e.valid() && std::find(witnesses_.begin(), witnesses_.end(), e) == witnesses_.end()) witnesses_.push_back(e);
        };
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.location.system != here_.system) continue;
            const ruleset::VehicleType t = vehicleType(r_, s_, v);
            if (t != ruleset::VehicleType::Mine && t != ruleset::VehicleType::Troop) add(v.owner);
        }
        for (ObjectId o : s_.galaxy.system(here_.system).objects)
            if (const Colony* c = s_.colony(o)) add(c->owner);
        std::sort(witnesses_.begin(), witnesses_.end());
    }
    std::vector<EmpireId> witnesses_;

    std::string planetName(SystemId sys) const { return std::format("{} {}", s_.galaxy.system(sys).name, romanNumeral(nextPlanetNumeral(s_.galaxy, sys))); }

    // The result, after every check has passed.
    void perform() {
        noteWitnesses();
        StarSystem& sys = system();
        const auto& rs = r_.data();
        switch (action_) {
            case StellarAction::CreatePlanet: {
                // The field has no colony (checked). Its rolled ability does not carry
                // over, and the planet rolls none (spec 01 §14 Q34, confirmed: binary).
                const std::string name = planetName(sys.id);
                SpaceObject& obj = s_.galaxy.object(*plan_.object);
                obj.kind = ObjectKind::Planet;
                obj.abilities.clear();
                applySectorType(rs, obj, pick(plan_.types));
                obj.conditions = rollConditions(false, s_.rng);  // the values are kept
                obj.name = name;
                announce(std::format("Planet Created: {}", obj.name));
                return;
            }
            case StellarAction::DestroyPlanet: {
                destroyPlanet(ctx_, *plan_.object, "The planet was destroyed.", s_.rng);
                announce(std::format("{}{}", kPlanetDestroyed, s_.galaxy.object(*plan_.object).name));
                return;
            }
            case StellarAction::CreateStar: {
                SpaceObject star;
                star.kind = ObjectKind::Star;
                star.sector = here_.sector;
                applySectorType(rs, star, pick(plan_.types));
                star.name = sys.name + " Star";
                announce(std::format("Star Created: {}", star.name));
                append(std::move(star), sys.id);
                return;
            }
            case StellarAction::DestroyStar:
                announce(std::format("{}{}", kStarDestroyed, s_.galaxy.object(*plan_.object).name));
                destroyStar(ctx_, *plan_.object, "A star exploded in the system.", s_.rng);
                return;
            case StellarAction::OpenWarpPoint: openWarpPoint(); return;
            case StellarAction::CloseWarpPoint: {
                announce(std::format("Warp Point Closed: {}", sight::warpPointName(s_, owner_, *plan_.object)));
                closeWarpPoint(s_, *plan_.object);
                return;
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
                return;
            }
            case StellarAction::DestroyStorm:
                announce(std::format("Storm destroyed in {}", sys.name));
                remove(*plan_.object);
                return;
            case StellarAction::CreateNebulae:
            case StellarAction::CreateBlackHole: {
                const bool nebula = action_ == StellarAction::CreateNebulae;
                announce(std::format("{}{}", kStarDestroyed, s_.galaxy.object(*plan_.object).name));
                announce(std::format("{} created in {}", nebula ? "Nebula" : "Black hole", sys.name));
                shockwave(ctx_, sys.id, nebula ? "The system became a nebula." : "The system collapsed into a black hole.", s_.rng);
                if (nebula) setSystemKind(system(), "Nebulae", {ability(AbilityKind::SectorSightObscuration, 3)});
                else
                    setSystemKind(system(), "Black Hole",
                                  {ability(AbilityKind::SystemMovementTowardsCenter, 2), ability(AbilityKind::SystemDestructiveCenter, 5000),
                                   ability(AbilityKind::SectorShieldDisruption, 5000)});
                return;
            }
            case StellarAction::DestroyNebulae:
            case StellarAction::DestroyBlackHole: {
                const bool nebula = action_ == StellarAction::DestroyNebulae;
                announce(std::format("{} removed from {}", nebula ? "Nebula" : "Black hole", sys.name));
                setSystemKind(sys, "Normal", {});  // a standard, start-eligible system; the objects stay
                return;
            }
            case StellarAction::CreateConstructedPlanet: construct(); return;
            case StellarAction::Count: return;
        }
    }

    void openWarpPoint() {
        const SystemId from = here_.system;
        const SystemId to = plan_.to;
        const GalaxyPos a = s_.galaxy.system(from).position, b = s_.galaxy.system(to).position;
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
    }

    void construct() {
        const ruleset::PlanetSize& size = *plan_.world;
        // A planet of that size, of the builder's type and atmosphere when such a record exists.
        const Race& race = s_.empire(owner_).race;
        const auto& types = r_.data().sectorObjectTypes;
        std::vector<uint32_t> own, any;
        for (uint32_t i = 0; i < types.size(); ++i) {
            if (parseObjectKind(types[i].physicalType) != ObjectKind::Planet || !keysEqual(types[i].planetSize, size.name)) continue;
            any.push_back(i);
            if (keysEqual(types[i].planetPhysicalType, race.nativeSurface) && keysEqual(types[i].planetAtmosphere, race.atmosphere)) own.push_back(i);
        }
        const SystemId sysId = here_.system;
        SpaceObject world;
        world.kind = ObjectKind::Planet;
        world.sector = here_.sector;
        if (!own.empty() || !any.empty()) applySectorType(r_.data(), world, pick(own.empty() ? any : own));
        world.size = size.name;
        if (world.surface.empty()) world.surface = race.nativeSurface;
        if (world.atmosphere.empty()) world.atmosphere = race.atmosphere;
        const bool finite = s_.options.finiteResources;
        const int value = static_cast<int>(r_.setting(finite ? "Planet Value High Resources" : "Planet Value High Percent", finite ? 50000 : 150));
        world.value = {value, value, value};
        world.conditions = kOptimalConditions;  // 1.5
        world.name = planetName(sysId);
        announce(std::format("Planet Created: {}", world.name));
        remove(*plan_.object);  // the star is used up
        append(std::move(world), sysId);
        // Every object of the builder here carrying the device or any component of a
        // required group is destroyed, whole ship included, mothballed ships too
        // (spec 01 §9, confirmed: binary).
        for (Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.owner != owner_ || v.location != here_) continue;
            const Design& d = s_.design(v.design);
            bool used = false;
            for (size_t i = 0; i < d.entries.size() && !used; ++i) {
                used = hasAbility(r_.componentAbilities(d.entries[i].component), AbilityKind::CreateConstructedPlanet);
                for (const auto& need : plan_.needs) used = used || r_.component(d.entries[i].component).customGroup == need.first;
            }
            if (used) vehicleLost(ctx_, v, "Used up in planet construction.");
        }
    }

    TurnContext& ctx_;
    GameState& s_;
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

namespace opense4::game::movement {

std::string stellarProblem(const Rules& r, const GameState& s, VehicleId vehicle, const Order& o, ObjectId* target) {
    detail::Checks checks(r, s, o, true);
    const std::array<VehicleId, 1> members{vehicle};
    std::string why = checks.check(members);
    if (why.empty() && target && checks.plan().object) *target = *checks.plan().object;
    return why;
}

void destroyStar(TurnContext& ctx, ObjectId star, std::string_view cause, Rng& rng) {
    GameState& s = ctx.state;
    if (!star.valid() || star.index() >= s.galaxy.objects.size() || !detail::inSystem(s.galaxy, star)) return;
    detail::shockwave(ctx, s.galaxy.object(star).system, cause, rng);
}

void closeWarpPoint(GameState& s, ObjectId warpPoint) {
    if (!warpPoint.valid() || warpPoint.index() >= s.galaxy.objects.size() || s.galaxy.object(warpPoint).kind != ObjectKind::WarpPoint) return;
    const ObjectId far = s.galaxy.object(warpPoint).destination;
    if (far.valid() && far.index() < s.galaxy.objects.size() && detail::inSystem(s.galaxy, far)) detail::removeObject(s, far);
    if (detail::inSystem(s.galaxy, warpPoint)) detail::removeObject(s, warpPoint);
}

void destroyPlanet(TurnContext& ctx, ObjectId planet, std::string_view cause, Rng& rng) {
    GameState& s = ctx.state;
    if (!planet.valid() || planet.index() >= s.galaxy.objects.size() || s.galaxy.object(planet).kind != ObjectKind::Planet) return;
    detail::loseColony(ctx, planet, cause);
    SpaceObject& obj = s.galaxy.object(planet);
    detail::toAsteroids(ctx.rules, obj, stellarSizeOf(ctx.rules.data(), obj), rng);
}

} // namespace opense4::game::movement
