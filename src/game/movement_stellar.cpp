// Stellar manipulation (spec 01 §9, spec 03 §8).
//
// Object ids stay stable: objects are converted in place where possible
// (asteroids <-> planet, star -> destroyed star); new objects are appended;
// removed objects leave their system's object list and keep their record.

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::movement::detail {

namespace {

using datafile::keysEqual;

std::string roman(int n) {
    static constexpr std::pair<int, const char*> kTable[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
                                                             {50, "L"},   {40, "XL"},  {10, "X"},  {9, "IX"},   {5, "V"},   {4, "IV"},
                                                             {1, "I"}};
    std::string out;
    for (const auto& [v, s] : kTable)
        while (n >= v) {
            out += s;
            n -= v;
        }
    return out;
}

int sizeIndex(std::string_view stellarSize) {
    static constexpr std::string_view kSizes[] = {"Tiny", "Small", "Medium", "Large", "Huge"};
    for (size_t i = 0; i < std::size(kSizes); ++i)
        if (keysEqual(stellarSize, kSizes[i])) return static_cast<int>(i) + 1;
    return 0;
}

// Tiny..Huge of a planet object (constructed worlds count as Huge).
int planetSizeIndex(const Rules& r, const SpaceObject& obj) {
    if (const ruleset::PlanetSize* ps = planetSize(r, obj)) return ps->constructed ? 5 : sizeIndex(ps->stellarSize);
    return sizeIndex(obj.size);
}

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

struct Actor {
    VehicleId vehicle;
    size_t entry = 0;
    int64_t value = 0;
};

class Manipulation {
public:
    Manipulation(TurnContext& ctx, const Order& o) : ctx_(ctx), r_(ctx.rules), s_(ctx.state), o_(o) {}

    std::string run(std::span<const VehicleId> members, bool& consumed) {
        consumed = false;
        if (o_.amount < 0 || o_.amount >= static_cast<int>(StellarAction::Count)) return "Unknown stellar manipulation";
        action_ = static_cast<StellarAction>(o_.amount);
        const AbilityKind k = abilityFor(action_);
        bool capable = false;
        for (VehicleId id : members) {
            const Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || v->status == VehicleStatus::Mothballed) continue;
            const Design& d = s_.design(v->design);
            for (size_t i = 0; i < d.entries.size(); ++i) {
                if (!entryIntact(r_, s_, *v, i)) continue;
                const auto ab = r_.componentAbilities(d.entries[i].component);
                if (!hasAbility(ab, k)) continue;
                capable = true;
                if (v->movement <= 0) continue;  // needs movement remaining (spec 01 §9); not spent (inferred)
                actor_ = Actor{id, i, bestValue1(ab, k)};
                break;
            }
            if (actor_.vehicle.valid()) break;
        }
        if (!actor_.vehicle.valid()) return capable ? "No movement left for stellar manipulation" : "No working component for that manipulation";
        const Vehicle& v = *s_.vehicle(actor_.vehicle);
        owner_ = v.owner;
        here_ = v.location;
        name_ = v.name;
        const Design& d = s_.design(v.design);
        const DesignEntry entry = d.entries[actor_.entry];
        const auto abilities = r_.componentAbilities(entry.component);

        bool noOp = false;
        if (std::string why = perform(abilities, noOp); !why.empty()) return why;
        if (noOp) return {};
        consumed = true;
        // Pay for the device; one-shot devices are used up (the ship may be gone already).
        if (Vehicle* after = s_.vehicle(actor_.vehicle); after && alive(*after)) {
            spendSupply(r_, s_, *after, scaledSupply(r_, s_, owner_, mounted(r_, entry).supplyUsed));
            if (hasAbility(abilities, AbilityKind::ComponentDestroyedOnUse)) {
                if (after->damage.size() < d.entries.size()) after->damage.resize(d.entries.size(), 0);
                after->damage[actor_.entry] = entryStructure(r_, d, actor_.entry);
            }
        }
        return {};
    }

private:
    StarSystem& system() { return s_.galaxy.system(here_.system); }

    // The object an order names, or the first object of that kind in the sector.
    std::optional<ObjectId> target(ObjectKind kind) {
        if (o_.object.valid() && o_.object.index() < s_.galaxy.objects.size()) {
            const SpaceObject& obj = s_.galaxy.object(o_.object);
            if (obj.kind == kind && inSystem(s_.galaxy, o_.object) && obj.system == here_.system && obj.sector == here_.sector) return o_.object;
        }
        for (ObjectId id : system().objects) {
            const SpaceObject& obj = s_.galaxy.object(id);
            if (obj.kind == kind && obj.sector == here_.sector) return id;
        }
        return std::nullopt;
    }

    bool blocked(SystemId sys, AbilityKind stop) const {
        for (ObjectId o : s_.galaxy.system(sys).objects)
            if (const Colony* c = s_.colony(o))
                for (uint32_t f : c->facilities)
                    if (hasAbility(r_.facilityAbilities(f), stop)) return true;
        return false;
    }

    std::vector<uint32_t> sectorTypes(ObjectKind kind, auto&& accept) const {
        std::vector<uint32_t> out;
        const auto& types = r_.data().sectorObjectTypes;
        for (uint32_t i = 0; i < types.size(); ++i)
            if (parseObjectKind(types[i].physicalType) == kind && accept(types[i])) out.push_back(i);
        return out;
    }

    void applySectorType(SpaceObject& obj, uint32_t index) {
        const ruleset::SectorObjectType& st = r_.data().sectorObjectTypes[index];
        obj.sectorType = index;
        if (obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) {
            obj.size = st.planetSize;
            obj.surface = st.planetPhysicalType;
            obj.atmosphere = st.planetAtmosphere;
        } else if (obj.kind == ObjectKind::Star || obj.kind == ObjectKind::DestroyedStar) {
            obj.size = st.starSize;
            obj.starAge = st.starAge;
            obj.starColor = st.starColor;
            obj.starLuminosity = st.starLuminosity;
        } else if (obj.kind == ObjectKind::Storm) {
            obj.size = st.stormSize;
        }
    }

    void rollValues(SpaceObject& obj) {
        const bool finite = s_.options.finiteResources;
        const std::string unit = finite ? "Resources" : "Percent";
        const int64_t lo = r_.setting(std::format("Planet Value Low {}", unit), finite ? 5000 : 50);
        const int64_t hi = r_.setting(std::format("Planet Value High {}", unit), finite ? 50000 : 150);
        for (int& v : obj.value) v = static_cast<int>(s_.rng.range(std::min(lo, hi), std::max(lo, hi)));
        obj.conditions = s_.rng.rangeInt(0, 100);
    }

    int count(SystemId sys, ObjectKind kind) const {
        int n = 0;
        for (ObjectId o : s_.galaxy.system(sys).objects) n += s_.galaxy.object(o).kind == kind;
        return n;
    }

    std::string planetName(SystemId sys) const { return std::format("{} {}", s_.galaxy.system(sys).name, roman(count(sys, ObjectKind::Planet))); }

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

    // Everything in the system except warp points (and `keep`) is destroyed.
    void wipeSystem(SystemId sys, ObjectId keep, std::string_view cause) {
        for (Vehicle& v : s_.vehicles)
            if (alive(v) && v.location.system == sys) vehicleLost(ctx_, v, cause);
        const std::vector<ObjectId> objects = s_.galaxy.system(sys).objects;
        for (ObjectId o : objects) {
            loseColony(o, cause);
            if (o != keep && s_.galaxy.object(o).kind != ObjectKind::WarpPoint) remove(o);
        }
    }

    // The first system type of that physical type (for "Normal": one without
    // abilities), or the fallback abilities if the data has none (inferred).
    void setSystemType(StarSystem& sys, std::string_view physical, std::vector<ruleset::Ability> fallback) {
        const auto& types = r_.data().systemTypes;
        for (uint32_t i = 0; i < types.size(); ++i)
            if (keysEqual(types[i].physicalType, physical) && (!keysEqual(physical, "Normal") || types[i].abilities.empty())) {
                sys.type = ruleset::SystemTypeId{i};
                sys.physicalType = types[i].physicalType;
                sys.abilities = types[i].abilities;
                return;
            }
        sys.physicalType = std::string(physical);
        sys.abilities = std::move(fallback);
    }

    static ruleset::Ability ability(AbilityKind k, int64_t v1) {
        ruleset::Ability a;
        a.type = std::string(identifier(k));
        a.value1 = std::to_string(v1);
        return a;
    }

    void announce(std::string title) { ctx_.log(owner_, LogCategory::Events, std::move(title), std::format("By {}.", name_), here_); }

    std::string perform(std::span<const ParsedAbility> abilities, bool& noOp) {
        StarSystem& sys = system();
        switch (action_) {
            case StellarAction::CreatePlanet: {
                const auto rock = target(ObjectKind::Asteroids);
                if (!rock) return "No asteroid field here";
                const int maxSize = static_cast<int>(actor_.value);
                const auto types = sectorTypes(ObjectKind::Planet, [&](const ruleset::SectorObjectType& t) {
                    for (const auto& ps : r_.data().planetSizes)
                        if (keysEqual(ps.name, t.planetSize) && keysEqual(ps.physicalType, "Planet"))
                            return !ps.constructed && sizeIndex(ps.stellarSize) > 0 && sizeIndex(ps.stellarSize) <= maxSize;
                    return false;
                });
                if (types.empty()) return "No planet of that size can be made";
                SpaceObject& obj = s_.galaxy.object(*rock);
                obj.kind = ObjectKind::Planet;
                applySectorType(obj, types[s_.rng.below(types.size())]);
                obj.abilities.clear();
                rollValues(obj);
                obj.name = planetName(sys.id);
                announce(std::format("Planet created: {}", obj.name));
                return {};
            }
            case StellarAction::DestroyPlanet: {
                const auto planet = target(ObjectKind::Planet);
                if (!planet) return "No planet here";
                if (planetSizeIndex(r_, s_.galaxy.object(*planet)) > actor_.value) return "The planet is too large";
                if (const Colony* c = s_.colony(*planet))
                    for (uint32_t f : c->facilities)
                        if (hasAbility(r_.facilityAbilities(f), AbilityKind::StopPlanetDestroyer)) return "The planet is protected";
                loseColony(*planet, "The planet was destroyed.");
                SpaceObject& obj = s_.galaxy.object(*planet);
                const std::string oldName = obj.name;
                obj.kind = ObjectKind::Asteroids;
                const auto types = sectorTypes(ObjectKind::Asteroids, [](const auto&) { return true; });
                if (!types.empty()) applySectorType(obj, types[s_.rng.below(types.size())]);
                obj.abilities.clear();
                obj.name = sys.name + " Asteroids";
                announce(std::format("Planet destroyed: {}", oldName));
                return {};
            }
            case StellarAction::CreateStar: {
                if (keysEqual(sys.physicalType, "Nebulae") || keysEqual(sys.physicalType, "Black Hole"))
                    return "Stars cannot be created in this system";
                const auto types = sectorTypes(ObjectKind::Star, [](const auto&) { return true; });
                if (types.empty()) return "No star type exists";
                SpaceObject star;
                star.kind = ObjectKind::Star;
                star.sector = here_.sector;
                applySectorType(star, types[s_.rng.below(types.size())]);
                const int stars = count(sys.id, ObjectKind::Star) + count(sys.id, ObjectKind::DestroyedStar);
                star.name = stars == 0 ? sys.name : std::format("{} {}", sys.name, static_cast<char>('A' + std::min(stars, 25)));
                announce(std::format("Star created: {}", star.name));
                append(std::move(star), sys.id);
                return {};
            }
            case StellarAction::DestroyStar: {
                const auto star = target(ObjectKind::Star);
                if (!star) return "No star here";
                if (blocked(sys.id, AbilityKind::StopStarDestroyer)) return "The star is protected";
                announce(std::format("Star destroyed: {}", s_.galaxy.object(*star).name));
                wipeSystem(sys.id, *star, "A star exploded in the system.");
                SpaceObject& obj = s_.galaxy.object(*star);
                obj.kind = ObjectKind::DestroyedStar;  // (inferred) the core remains
                obj.abilities.clear();
                return {};
            }
            case StellarAction::OpenWarpPoint: {
                const SystemId to = o_.location.system;
                if (!to.valid() || to.index() >= s_.galaxy.systems.size() || to == sys.id) return "Choose another system to open a warp point to";
                const GalaxyPos a = sys.position, b = s_.galaxy.system(to).position;
                const int64_t dx = a.x - b.x, dy = a.y - b.y;
                // Val 1 x 10 light years; one quadrant square is about 10 light years.
                if (dx * dx + dy * dy > actor_.value * actor_.value) return "That system is out of range";
                if (blocked(sys.id, AbilityKind::StopOpenWarpPoint) || blocked(to, AbilityKind::StopOpenWarpPoint))
                    return "Warp point creation is blocked";
                auto types = sectorTypes(ObjectKind::WarpPoint, [](const ruleset::SectorObjectType& t) { return t.unusual; });
                if (types.empty()) types = sectorTypes(ObjectKind::WarpPoint, [](const auto&) { return true; });
                auto make = [&](SystemId where, Sector sector) {
                    SpaceObject wp;
                    wp.kind = ObjectKind::WarpPoint;
                    wp.sector = sector;
                    if (!types.empty()) applySectorType(wp, types[s_.rng.below(types.size())]);
                    wp.oneWay = false;  // opened links are two-way (inferred)
                    wp.name = std::format("{} Warp Point {}", s_.galaxy.system(where).name, count(where, ObjectKind::WarpPoint) + 1);
                    return append(std::move(wp), where);
                };
                // The far end: the sector picked in the target system when it is on the
                // edge (anywhere if warp points may be anywhere), else the free edge
                // sector facing this system (inferred).
                const Sector picked = o_.location.sector;
                const bool edge = chebyshev(picked, Sector{kSystemCenter, kSystemCenter}) == kSystemCenter;
                const Sector farSector = picked.valid() && (edge || s_.options.warpPointsAnywhere) && picked != Sector{kSystemCenter, kSystemCenter}
                                             ? picked
                                             : edgeSectorToward(to, s_.galaxy.system(sys.id).position);
                const ObjectId here = make(sys.id, here_.sector);
                const ObjectId there = make(to, farSector);
                s_.galaxy.object(here).destination = there;
                s_.galaxy.object(there).destination = here;
                sight::learnWarpLink(s_, owner_, here);
                announce(std::format("Warp point opened to {}", s_.galaxy.system(to).name));
                return {};
            }
            case StellarAction::CloseWarpPoint: {
                if (o_.object.valid() && o_.object.index() < s_.galaxy.objects.size() &&
                    s_.galaxy.object(o_.object).kind == ObjectKind::WarpPoint && !inSystem(s_.galaxy, o_.object)) {
                    noOp = true;  // already closed this turn: harmless (spec 01 §9)
                    return {};
                }
                const auto wp = target(ObjectKind::WarpPoint);
                if (!wp) return "No warp point here";
                const ObjectId far = s_.galaxy.object(*wp).destination;
                if (blocked(sys.id, AbilityKind::StopCloseWarpPoint) ||
                    (far.valid() && blocked(s_.galaxy.object(far).system, AbilityKind::StopCloseWarpPoint)))
                    return "Warp point closure is blocked";
                announce(std::format("Warp point closed: {}", s_.galaxy.object(*wp).name));
                if (far.valid() && inSystem(s_.galaxy, far)) remove(far);
                remove(*wp);
                return {};
            }
            case StellarAction::CreateStorm: {
                const auto types = sectorTypes(ObjectKind::Storm, [](const auto&) { return true; });
                SpaceObject storm;
                storm.kind = ObjectKind::Storm;
                storm.sector = here_.sector;
                if (!types.empty()) applySectorType(storm, types[s_.rng.below(types.size())]);
                // One effect, capped by the Created Storm Maximum settings (inferred).
                const std::array<std::pair<AbilityKind, int64_t>, 3> effects{{
                    {AbilityKind::SectorSightObscuration, r_.setting("Created Storm Maximum Obscuration Level", 2)},
                    {AbilityKind::SectorDamage, r_.setting("Created Storm Maximum Turbulence Damage", 200)},
                    {AbilityKind::SectorShieldDisruption, r_.setting("Created Storm Maximum Shield Disruption", 5000)},
                }};
                const auto& [kind, cap] = effects[s_.rng.below(effects.size())];
                storm.abilities.push_back(ability(kind, s_.rng.range(1, std::max<int64_t>(1, cap))));
                storm.name = std::format("{} Storm {}", sys.name, roman(count(sys.id, ObjectKind::Storm) + 1));
                announce(std::format("Storm created: {}", storm.name));
                append(std::move(storm), sys.id);
                return {};
            }
            case StellarAction::DestroyStorm: {
                const auto storm = target(ObjectKind::Storm);
                if (!storm) return "No storm here";
                announce(std::format("Storm destroyed: {}", s_.galaxy.object(*storm).name));
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
                announce(std::format("{} created in {}", nebula ? "Nebula" : "Black hole", sys.name));
                wipeSystem(sys.id, {}, nebula ? "The system became a nebula." : "The system collapsed into a black hole.");
                std::vector<ruleset::Ability> fallback;
                if (nebula) {
                    fallback.push_back(ability(AbilityKind::SectorSightObscuration, r_.setting("Created Storm Maximum Obscuration Level", 2)));
                } else {  // (inferred) pull, centre damage and shield disruption
                    fallback.push_back(ability(AbilityKind::SystemMovementTowardsCenter, 1));
                    fallback.push_back(ability(AbilityKind::SystemDestructiveCenter, 1000));
                    fallback.push_back(ability(AbilityKind::SectorShieldDisruption, r_.setting("Created Storm Maximum Shield Disruption", 5000)));
                }
                setSystemType(system(), nebula ? "Nebulae" : "Black Hole", std::move(fallback));
                return {};
            }
            case StellarAction::DestroyNebulae:
            case StellarAction::DestroyBlackHole: {
                const bool nebula = action_ == StellarAction::DestroyNebulae;
                if (!keysEqual(sys.physicalType, nebula ? "Nebulae" : "Black Hole"))
                    return nebula ? "This system is not a nebula" : "This system is not a black hole";
                announce(std::format("{} removed from {}", nebula ? "Nebula" : "Black hole", sys.name));
                setSystemType(sys, "Normal", {});
                sys.abilities.clear();
                return {};
            }
            case StellarAction::CreateConstructedPlanet: return construct(abilities);
            case StellarAction::Count: break;
        }
        return "Unknown stellar manipulation";
    }

    // The free outer-ring sector closest to the direction of `toward` (integer math).
    Sector edgeSectorToward(SystemId sys, GalaxyPos toward) const {
        const GalaxyPos p = s_.galaxy.system(sys).position;
        const int dx = toward.x - p.x, dy = toward.y - p.y;
        const int m = std::max(std::abs(dx), std::abs(dy));
        const int ix = kSystemCenter + (m > 0 ? dx * kSystemCenter / m : kSystemCenter);
        const int iy = kSystemCenter + (m > 0 ? dy * kSystemCenter / m : 0);
        std::vector<Sector> taken;
        for (ObjectId o : s_.galaxy.system(sys).objects) taken.push_back(s_.galaxy.object(o).sector);
        std::optional<Sector> best;
        int bestD = 0;
        for (int y = 0; y < kSystemSize; ++y)
            for (int x = 0; x < kSystemSize; ++x) {
                const Sector sct{x, y};
                if (chebyshev(sct, Sector{kSystemCenter, kSystemCenter}) != kSystemCenter) continue;
                if (std::find(taken.begin(), taken.end(), sct) != taken.end()) continue;
                const int d = (x - ix) * (x - ix) + (y - iy) * (y - iy);
                if (!best || d < bestD) {
                    best = sct;
                    bestD = d;
                }
            }
        return best.value_or(Sector{ix, iy});
    }

    std::string construct(std::span<const ParsedAbility> abilities) {
        const auto star = target(ObjectKind::Star);
        if (!star) return "No star here";
        const ruleset::PlanetSize* size = nullptr;
        for (const auto& ps : r_.data().planetSizes)
            if (ps.constructed && ps.specialAbilityId == actor_.value) size = &ps;
        if (!size) return "Unknown constructed planet";
        // Every requirement: at least Val 2 kT of components of custom group Val 1 in this sector.
        struct Need {
            int group;
            int64_t tons;
        };
        std::vector<Need> needs;
        for (const ParsedAbility& a : abilities)
            if (a.kind == AbilityKind::ConstructedPlanetRequirements) needs.push_back({static_cast<int>(a.value1), a.value2});
        auto forEachPart = [&](int group, auto&& fn) {
            for (Vehicle& v : s_.vehicles) {
                if (!alive(v) || v.owner != owner_ || v.location != here_) continue;
                const Design& d = s_.design(v.design);
                for (size_t i = 0; i < d.entries.size(); ++i)
                    if (entryIntact(r_, s_, v, i) && r_.component(d.entries[i].component).customGroup == group)
                        if (!fn(v, d, i)) return;
            }
        };
        for (const Need& n : needs) {
            int64_t have = 0;
            forEachPart(n.group, [&](Vehicle&, const Design& d, size_t i) {
                have += mounted(r_, d.entries[i]).tonnage;
                return true;
            });
            if (have < n.tons) return "The construction materials are not all here";
        }
        // The materials are used up (inferred).
        for (const Need& n : needs) {
            int64_t used = 0;
            forEachPart(n.group, [&](Vehicle& v, const Design& d, size_t i) {
                if (used >= n.tons) return false;
                used += mounted(r_, d.entries[i]).tonnage;
                if (v.damage.size() < d.entries.size()) v.damage.resize(d.entries.size(), 0);
                v.damage[i] = entryStructure(r_, d, i);
                return true;
            });
        }
        SpaceObject world;
        world.kind = ObjectKind::Planet;
        world.sector = here_.sector;
        const auto types = sectorTypes(ObjectKind::Planet, [&](const ruleset::SectorObjectType& t) { return keysEqual(t.planetSize, size->name); });
        if (!types.empty()) applySectorType(world, types[s_.rng.below(types.size())]);
        world.size = size->name;
        if (world.surface.empty()) world.surface = "Rock";
        if (world.atmosphere.empty()) world.atmosphere = s_.empire(owner_).race.atmosphere;
        rollValues(world);
        const SystemId sys = here_.system;
        world.name = std::format("{} {}", s_.galaxy.system(sys).name, roman(count(sys, ObjectKind::Planet) + 1));
        announce(std::format("Planet constructed: {}", world.name));
        append(std::move(world), sys);
        for (Vehicle& v : s_.vehicles)
            if (alive(v) && vehicleDestroyed(r_, s_, v)) vehicleLost(ctx_, v, "Used up in planet construction.");
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
