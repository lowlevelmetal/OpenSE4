// Space combat (docs/spec/04 §3-§12, §14-§16): the combat map, start boxes
// and formations, the turn sequence (strategic resolution and the phases of
// tactical combat, see combat_battle.hpp), strategy-driven movement and
// targeting, direct fire, seekers, point defense, launched units, planets,
// boarding, ramming, troop drops with their ground combat, and the battle's
// results, replay record and logs. The player's orders of tactical combat
// are checked and carried out in combat_tactical.cpp.
//
// Every piece works on a copy of its vehicle or colony; the results are
// written back once the battle ends. Randomness comes from a fork of
// GameState::rng; pieces act in a stable order. No floating point: the
// percentages the original applies in floating point go through xmath, and
// the straight-line distances of Don't Get Hurt are exact integer roots.

#include "game/combat.hpp"

#include "game/combat_battle.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"
#include "game/turn_internal.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <tuple>

namespace opense4::game::combat {

namespace detail {

namespace {

using ruleset::VehicleType;
using ruleset::WeaponKind;
using Kind = CombatPiece::Kind;
using Ev = CombatEvent::Kind;

constexpr int kCentreX = 36;                     // the map centre (confirmed: binary)
constexpr int kCentreY = 31;
constexpr int kPlanetTargets = 10;               // a planet engages up to 10 targets per turn
constexpr int kPlanetLaunch = 100;               // a planet launches up to 100 of each kind per turn
constexpr int kMaxReload = 250;
constexpr int64_t kImmovable = 500000;           // ram return damage from planets, and to drones
constexpr int64_t kBoardingSpend = 5000;
constexpr int64_t kSelfDestruct = 10000;
constexpr int64_t kRegenerationCap = 10000;      // organic armor pool, and the end-of-battle restore
constexpr int kShipKillTenths = 10;              // +1.0 for a ship, base or planet
constexpr int kUnitKillTenths = 1;               // +0.1 for a whole unit group or a seeker
constexpr int64_t kPlanetSizeRank = 1'000'000;   // planets rank as the largest targets
constexpr uint8_t kDroneTargets = kTargetShips | kTargetPlanets | kTargetSatellites;
constexpr int64_t kDangerOwnSquare = 30;         // a hostile piece's own square (spec 04 §16.1)

// Facings (spec 03 §10): 0 up, 1 right, 2 down, 3 left, then the diagonals.
constexpr std::array<std::pair<int, int>, 8> kFacing{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {-1, -1}, {1, 1}, {-1, 1}}};
constexpr std::array<std::pair<int, int>, 8> kDirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}}};
constexpr std::array<std::pair<int, int>, 4> kStraight{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

int gap(int a0, int aSize, int b0, int bSize) { return std::max({0, a0 - (b0 + bSize - 1), b0 - (a0 + aSize - 1)}); }
int cheb(int x0, int y0, int x1, int y1) { return std::max(std::abs(x0 - x1), std::abs(y0 - y1)); }

int facingOf(int dx, int dy) {
    for (size_t f = 0; f < kFacing.size(); ++f)
        if (kFacing[f].first == sgn(dx) && kFacing[f].second == sgn(dy)) return static_cast<int>(f);
    return 0;
}

// A formation slot's offset turned to the leader's facing (confirmed: binary, spec 03 §10).
std::pair<int, int> rotateSlot(int facing, int dx, int dy) {
    switch (facing) {
        case 1: return {-dy, dx};
        case 2: return {-dx, -dy};
        case 3: return {dy, -dx};
        case 4: return {dx - dy, dx + dy};
        case 5: return {dx + dy, dy - dx};
        case 6: return {-dx - dy, dx - dy};
        case 7: return {dy - dx, -dx - dy};
        default: return {dx, dy};
    }
}

bool warheadExplodes(DamageType t) {
    switch (t) {
        case DamageType::Normal:
        case DamageType::SkipsNormalShields:
        case DamageType::OnlyEngines:
        case DamageType::OnlyWeapons:
        case DamageType::OnlyShieldGenerators:
        case DamageType::SkipsArmor:
        case DamageType::SkipsShieldsAndArmor:
        case DamageType::SkipsAllShields:
        case DamageType::OnlyMasterComputers: return true;
        default: return false;
    }
}

bool isRangeStrategy(MoveStrategy m) {
    return m == MoveStrategy::MaximumRange || m == MoveStrategy::OptimalRange || m == MoveStrategy::ShortRange || m == MoveStrategy::PointBlank;
}

// floor(sqrt(v)) for v >= 0, exactly.
int64_t isqrt(int64_t v) {
    if (v <= 0) return 0;
    int64_t x = 1;
    while (x <= v / x) x <<= 1;
    int64_t lo = x >> 1, hi = x;   // lo² <= v < hi²
    while (hi - lo > 1) {
        const int64_t mid = lo + (hi - lo) / 2;
        if (mid <= v / mid) lo = mid;
        else hi = mid;
    }
    return lo;
}

// Truncated weight × straight-line distance: floor(weight × sqrt(dx² + dy²)).
int64_t weightedDistance(int64_t weight, int dx, int dy) { return isqrt(weight * weight * (int64_t{dx} * dx + int64_t{dy} * dy)); }

enum class Result : uint8_t { Win, Loss, Stalemate };

} // namespace

// ---- Setup ----------------------------------------------------------------------------------------

Battle::Battle(TurnContext& ctx, Location where, Rng& rng)
    : r_(ctx.rules), s_(ctx.state), ctx_(ctx), where_(where), rng_(rng), cs_(loadSettings(ctx.rules)) {
    occ_.assign(static_cast<size_t>(kW * kH), -1);
}

Weapon Battle::makeWeapon(const DesignEntry& de, size_t entry) const {
    Weapon w;
    w.entry = entry;
    w.de = de;
    w.comp = &r_.component(de.component);
    w.type = parseDamageType(w.comp->weapon.damageType);
    w.targets = parseWeaponTargets(w.comp->weapon.targets);
    w.reloadRate = std::max(1, w.comp->weapon.reloadRate);
    w.reach = weaponReach(r_, de);
    return w;
}

void Battle::buildWeapons(Piece& p) const {
    p.weapons.clear();
    if (p.mothballed) return;
    if (p.kind != Kind::UnitGroup) {
        const Design& d = s_.design(p.unit.design);
        for (size_t i = 0; i < d.entries.size(); ++i) {
            const ruleset::Component& c = r_.component(d.entries[i].component);
            if (!c.isWeapon() || c.weapon.kind == WeaponKind::Warhead) continue;
            Weapon w = makeWeapon(d.entries[i], i);
            w.reload.assign(1, 0);
            p.weapons.push_back(std::move(w));
        }
        return;
    }
    // A unit group: the weapons of each of its designs, design by design.
    const bool fighters = p.vtype == VehicleType::Fighter;
    for (size_t k = 0; k < p.stacks.size(); ++k) {
        const int stack = static_cast<int>(k);
        const Design& d = s_.design(p.stacks[k].design);
        for (size_t i = 0; i < d.entries.size(); ++i) {
            const ruleset::Component& c = r_.component(d.entries[i].component);
            if (!c.isWeapon() || c.weapon.kind == WeaponKind::Warhead) continue;
            if (fighters) {
                // A fighter group fires identical weapons (same part and mount) together,
                // whichever of its designs carries them (confirmed: binary).
                auto same = std::find_if(p.weapons.begin(), p.weapons.end(), [&](const Weapon& w) { return w.de == d.entries[i]; });
                if (same != p.weapons.end()) {
                    auto share = std::find_if(same->shares.begin(), same->shares.end(), [&](const auto& sh) { return sh.first == stack; });
                    if (share == same->shares.end()) same->shares.emplace_back(stack, 1);
                    else ++share->second;
                    if (same->stack == stack) ++same->perUnit;
                    continue;
                }
            }
            Weapon w = makeWeapon(d.entries[i], i);
            w.stack = stack;
            if (fighters) {
                w.shares.emplace_back(stack, 1);
                w.reload.assign(1, 0);
            } else {
                // Satellite and drone groups fire each weapon of each unit on its own.
                w.reload.assign(static_cast<size_t>(std::max(1, p.stacks[k].count)), 0);
            }
            p.weapons.push_back(std::move(w));
        }
    }
}

int64_t Battle::groupHitPoints(const Piece& p) const {
    int64_t hp = 0;
    for (const UnitStack& st : p.stacks)
        if (st.count > 0) hp += detail::unitHitPoints(r_, s_.design(st.design)) * st.count;
    return hp;
}

void Battle::syncGroup(Piece& p) { setGroupStacks(s_, p.unit, p.stacks); }

void Battle::buildPlanetWeapons(Piece& p) const {
    p.weapons.clear();
    // Every weapon of every platform is a separate weapon of the planet (confirmed: binary).
    for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
        const UnitStack& st = p.unit.cargo.units[k];
        if (st.count <= 0 || r_.hull(s_.design(st.design).hull).type != VehicleType::WeaponPlatform) continue;
        const Design& d = s_.design(st.design);
        for (size_t i = 0; i < d.entries.size(); ++i) {
            const ruleset::Component& comp = r_.component(d.entries[i].component);
            if (!comp.isWeapon() || comp.weapon.kind == WeaponKind::Warhead) continue;
            Weapon w = makeWeapon(d.entries[i], i);
            w.stack = static_cast<int>(k);
            w.reload.assign(static_cast<size_t>(st.count), 0);
            p.weapons.push_back(std::move(w));
        }
    }
}

int Battle::addPiece(Piece p) {
    const int i = static_cast<int>(pieces_.size());
    pieces_.push_back(std::move(p));
    acted_.push_back(pieces_.back().kind == Kind::Seeker ? 1 : 0);
    const Piece& q = pieces_.back();
    const bool big = q.kind == Kind::Planet || q.kind == Kind::Obstacle;
    const int32_t count = q.kind == Kind::UnitGroup ? q.unit.count : q.kind == Kind::Seeker ? q.members : 1;
    rec_.pieces.push_back(CombatPiece{q.kind, q.owner, q.source, q.object, big ? DesignId{} : q.unit.design, q.name,
                                      static_cast<int16_t>(q.x), static_cast<int16_t>(q.y), count});
    return i;
}

void Battle::addVehiclePiece(const Vehicle& v) {
    Piece p;
    p.vtype = detail::typeOf(r_, s_, v);
    p.kind = isUnitType(p.vtype) ? Kind::UnitGroup : Kind::Vehicle;
    p.owner = p.startOwner = v.owner;
    p.source = v.id;
    p.unit = v;
    const Design& d = s_.design(v.design);
    p.unit.damage.resize(d.entries.size(), 0);
    if (p.kind == Kind::UnitGroup) p.unit.damage.assign(d.entries.size(), 0);   // units are whole or dead
    p.name = v.name;
    p.mothballed = v.status == VehicleStatus::Mothballed;
    p.designStrategy = d.owner == v.owner ? d.strategy : 0;
    if (const Fleet* f = s_.fleet(v.fleet); f && f->owner == v.owner) {
        p.fleetStrategy = f->strategy;
        if (p.kind == Kind::Vehicle) {
            p.fleet = v.fleet;
            fleetExp_.emplace(v.fleet.value, std::pair{f->experience, f->experienceTenths});
        }
    }
    p.startCount = v.count;
    if (p.kind == Kind::UnitGroup) {
        p.stacks = groupStacks(v);
        p.hpStart = groupHitPoints(p);
        for (const UnitStack& st : p.stacks) p.tonnageHad += designTonnage(r_, s_.design(st.design)) * st.count;
    }
    p.arrived = detail::arrivedThisTurn(s_, v);
    p.warped = detail::arrivedByWarp(s_, v);
    std::tie(p.boxDx, p.boxDy) = detail::arrivalDirection(s_, v);
    buildWeapons(p);
    pieces_.push_back(std::move(p));
}

void Battle::addPlanetPiece(const Colony& c) {
    Piece p;
    p.kind = Kind::Planet;
    p.owner = p.startOwner = c.owner;
    p.object = c.planet;
    p.unit.owner = c.owner;
    p.unit.location = where_;
    p.unit.cargo = c.cargo;
    p.name = s_.galaxy.object(c.planet).name;
    p.size = kBigPieceSize;
    p.population = c.population;
    p.facilities = c.facilities;
    p.facilityLost.assign(c.facilities.size(), 0);
    p.landed = c.landedTroops;
    p.invader = c.invader;
    p.militia = c.militia;
    buildPlanetWeapons(p);
    pieces_.push_back(std::move(p));
}

void Battle::addObstaclePiece(ObjectId o) {
    Piece p;
    p.kind = Kind::Obstacle;
    p.object = o;
    p.name = s_.galaxy.object(o).name;
    p.size = kBigPieceSize;
    pieces_.push_back(std::move(p));
}

int Battle::shieldBonus(EmpireId e) const {
    const auto it = shieldBonus_.find(e.value);
    return it == shieldBonus_.end() ? 0 : it->second;
}

void Battle::planetShields(Piece& p, bool fill) const {
    // Facility generators (and Planet - Shield Generation); platform shield parts
    // do not add (confirmed: binary). Facilities lost in the battle keep working
    // until it ends, so the maximum never drops (spec 04 §11).
    int64_t normal = 0, phased = 0, planet = 0;
    int64_t pop = 0;
    for (const PopulationGroup& g : p.population) pop += g.millions;
    if (pop > 0)   // (inferred) facilities do not work without population
        for (uint32_t f : p.facilities) {
            const auto ab = r_.facilityAbilities(f);
            normal += sumValue1(ab, AbilityKind::ShieldGeneration);
            phased += sumValue1(ab, AbilityKind::PhasedShieldGeneration);
            planet += sumValue1(ab, AbilityKind::PlanetShieldGeneration);
        }
    int64_t total = normal + phased + planet;
    const int bonus = shieldBonus(p.owner);
    if (total > 0 && bonus > 0) total += bonus;
    total = std::max<int64_t>(0, total - disruption_);
    p.sh.max = static_cast<int>(std::min<int64_t>(total, 1'000'000'000));
    // Planetary generators alone count as neither kind, so phased weapons pass them (confirmed: binary).
    p.sh.kind = normal > 0 ? ShieldState::Kind::Normal
                : (phased > 0 && planet == 0) ? ShieldState::Kind::Phased
                                              : ShieldState::Kind::None;
    p.sh.current = fill ? p.sh.max : std::min(p.sh.current, p.sh.max);
}

bool Battle::setup() {
    if (!where_.system.valid() || where_.system.index() >= s_.galaxy.systems.size()) return false;
    // The combat simulator fights at a location of its own: that location's
    // interference and disruption, and no system modifier totals (spec 04 §17).
    interference_ = overrides_ ? overrides_->interference : detail::sensorInterference(s_, where_);
    disruption_ = overrides_ ? overrides_->disruption : detail::shieldDisruption(s_, where_);
    satelliteCap_ = static_cast<int>(r_.setting("Maximum Satellites Per Player Per Sector", 100));

    const detail::Forces forces = detail::battleForces(r_, s_, where_);
    if (!forces.battle) return false;
    empires_ = forces.empires;
    if (!overrides_)
        for (EmpireId e : empires_) {
            combatBonus_[e.value] = detail::systemModifier(r_, s_, e, where_.system, AbilityKind::CombatModifierSystem);
            damageBonus_[e.value] = detail::systemModifier(r_, s_, e, where_.system, AbilityKind::DamageModifierSystem);
            shieldBonus_[e.value] = detail::systemModifier(r_, s_, e, where_.system, AbilityKind::ShieldModifierSystem);
        }
    // Every owned object in the sector is a piece and decloaked until the battle ends (history 1.28).
    for (EmpireId e : empires_) {
        for (ObjectId o : forces.colonies)
            if (s_.colony(o)->owner == e) addPlanetPiece(*s_.colony(o));
        for (VehicleId id : forces.vehicles)
            if (s_.vehicle(id)->owner == e) addVehiclePiece(*s_.vehicle(id));
    }
    for (ObjectId o : forces.obstacles) addObstaclePiece(o);

    // Defenders had a piece in the sector already; every planet has (confirmed: binary).
    for (EmpireId e : empires_)
        for (const Piece& p : pieces_)
            if (p.owner == e && (p.kind == Kind::Planet || !p.arrived)) {
                defenders_.push_back(e);
                break;
            }
    // Drones keep the target they were given.
    for (Piece& p : pieces_)
        if (p.vtype == VehicleType::Drone && p.kind == Kind::UnitGroup && p.unit.targetVehicle.valid())
            for (size_t j = 0; j < pieces_.size(); ++j)
                if (pieces_[j].source == p.unit.targetVehicle) {
                    p.droneTarget = static_cast<int>(j);
                    p.droneTargetOwner = pieces_[j].owner;
                }

    // Starting values: shields full (none without supplies), reloads ready, full movement (confirmed: binary).
    for (size_t i = 0; i < pieces_.size(); ++i) {
        Piece& p = pieces_[i];
        if (p.kind == Kind::Vehicle) detail::refreshShields(r_, s_, p.unit, shieldBonus(p.owner), disruption_, p.sh, true);
        else if (p.kind == Kind::Planet) planetShields(p, true);
        if (p.kind == Kind::Planet) p.hpStart = planetHp(p);
        refreshPiece(static_cast<int>(i));
    }
    place();

    std::vector<Piece> built = std::move(pieces_);
    pieces_.clear();
    acted_.clear();
    for (Piece& p : built) addPiece(std::move(p));
    std::fill(occ_.begin(), occ_.end(), -1);
    for (size_t i = 0; i < pieces_.size(); ++i) occupy(static_cast<int>(i));

    // The phase order is drawn once: defenders first, then attackers, each in a random order (confirmed: binary).
    std::vector<EmpireId> att;
    order_ = defenders_;
    for (EmpireId e : empires_)
        if (std::find(defenders_.begin(), defenders_.end(), e) == defenders_.end()) att.push_back(e);
    rng_.shuffle(order_);
    rng_.shuffle(att);
    order_.insert(order_.end(), att.begin(), att.end());
    for (EmpireId e : empires_) planRng_.emplace(e.value, rng_.fork());

    rec_.turn = s_.turn;
    rec_.location = where_;
    rec_.participants = empires_;
    std::string names;
    for (EmpireId e : empires_) names += (names.empty() ? "" : ", ") + s_.empire(e).name;
    rec_.summary.push_back(std::format("Battle at {} between {}.", detail::sectorName(s_, where_), names));
    return true;
}

bool Battle::fits(int x, int y, int size, int self) const {
    for (int dy = 0; dy < size; ++dy)
        for (int dx = 0; dx < size; ++dx)
            if (!isFree(x + dx, y + dy, self)) return false;
    return true;
}

// A piece whose drawn square is taken or off the map first makes up to ten
// random hops of its own size in one of the four straight directions, stopping
// on the first free square; failing that, it takes the first free square in
// growing squares around the square first drawn, up to 100 squares away
// (confirmed: binary). The hops walk on from each other (inferred).
std::pair<int, int> Battle::settle(int x, int y, int size, int self) {
    if (fits(x, y, size, self)) return {x, y};
    int hx = x, hy = y;
    for (int hop = 0; hop < 10; ++hop) {
        const auto [dx, dy] = kStraight[rng_.below(kStraight.size())];
        hx += dx * size;
        hy += dy * size;
        if (fits(hx, hy, size, self)) return {hx, hy};
    }
    for (int rad = 1; rad <= 100; ++rad)
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != rad) continue;
                if (fits(x + dx, y + dy, size, self)) return {x + dx, y + dy};
            }
    return {-1, -1};
}

void Battle::occupy(int i) {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Seeker || !p.alive) return;
    for (int dy = 0; dy < p.size; ++dy)
        for (int dx = 0; dx < p.size; ++dx)
            if (onMap(p.x + dx, p.y + dy)) occ_[static_cast<size_t>((p.y + dy) * kW + p.x + dx)] = i;
}

void Battle::vacate(int i) {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Seeker) return;
    for (int dy = 0; dy < p.size; ++dy)
        for (int dx = 0; dx < p.size; ++dx)
            if (onMap(p.x + dx, p.y + dy) && occ_[static_cast<size_t>((p.y + dy) * kW + p.x + dx)] == i)
                occ_[static_cast<size_t>((p.y + dy) * kW + p.x + dx)] = -1;
}

// Start positions (spec 04 §3 step 4, confirmed: binary).
void Battle::place() {
    const size_t n = pieces_.size();
    const int S = n <= 20 ? 6 : n <= 40 ? 12 : n <= 60 ? 18 : 24;
    const int m = n > 60 ? 2 : 1;
    // A box by its top-left square, width and height; its squares run from
    // (x, y) to (x + w, y + h), both ends included.
    struct Box {
        int x = 0, y = 0, w = 0, h = 0;
        bool edge = false;
        int innerX = -1, innerY = -1;   // edge boxes: the column or row nearest the map centre
    };
    auto edgeBox = [&](int dx, int dy) -> Box {
        Box b;
        b.edge = true;
        if (dy != 0) {
            b.w = m * S;
            b.h = S;
            b.x = dx < 0 ? 0 : dx > 0 ? kW - 1 - m * S : kCentreX - m * S / 2;
            b.y = dy < 0 ? 0 : kH - 1 - S;
            b.innerY = dy < 0 ? S : kH - 1 - S;
        } else {
            b.w = S;
            b.h = m * S;
            b.x = dx < 0 ? 0 : kW - 1 - S;
            b.y = kCentreY - m * S / 2;
            b.innerX = dx < 0 ? S : kW - 1 - S;
        }
        return b;
    };
    auto edgeFacing = [](int dx, int dy) { return dy < 0 ? 2 : dy > 0 ? 0 : dx < 0 ? 1 : 3; };
    const Box warpBox{34, 29, 4, 4};
    const Box centreBox{kCentreX - S / 2, kCentreY - S / 2, S, S};
    auto numberedBox = [&](size_t k) -> Box {
        switch (k) {
            case 1: return {kCentreX - 2 * S, kCentreY - 2 * S, S, S};
            case 2: return {kCentreX + 2 * S, kCentreY + 2 * S, S, S};
            case 3: return {kCentreX + 2 * S, kCentreY - 2 * S, S, S};
            case 4: return {kCentreX - 2 * S, kCentreY + 2 * S, S, S};
            case 5: return {kCentreX - S / 2, kCentreY - 2 * S, S, S};
            case 6: return {kCentreX - S / 2, kCentreY + 2 * S, S, S};
            case 7: return {kCentreX + 2 * S, kCentreY - S / 2, S, S};
            case 8: return {kCentreX - 2 * S, kCentreY - S / 2, S, S};
            default: return centreBox;   // numbers above 8
        }
    };

    // Empires with a piece already in the sector, numbered in the order the
    // system lists their first object. Our system lists its planets, then the
    // vehicles in it in the game's vehicle order (inferred).
    const StarSystem& system = s_.galaxy.system(where_.system);
    std::map<uint32_t, size_t> firstObject;
    auto note = [&](EmpireId e, size_t rank) {
        auto it = firstObject.find(e.value);
        if (it == firstObject.end()) firstObject.emplace(e.value, rank);
        else it->second = std::min(it->second, rank);
    };
    for (size_t k = 0; k < system.objects.size(); ++k)
        if (const Colony* c = s_.colony(system.objects[k])) note(c->owner, k);
    for (size_t k = 0; k < s_.vehicles.size(); ++k)
        if (s_.vehicles[k].count > 0 && s_.vehicles[k].owner.valid() && s_.vehicles[k].location.system == where_.system)
            note(s_.vehicles[k].owner, system.objects.size() + k);
    std::vector<EmpireId> middle;
    for (const Piece& p : pieces_)
        if (p.owner.valid() && (p.kind == Kind::Planet || !p.arrived) && std::find(middle.begin(), middle.end(), p.owner) == middle.end())
            middle.push_back(p.owner);
    std::sort(middle.begin(), middle.end(), [&](EmpireId a, EmpireId b) {
        const size_t ra = firstObject.count(a.value) ? firstObject[a.value] : SIZE_MAX;
        const size_t rb = firstObject.count(b.value) ? firstObject[b.value] : SIZE_MAX;
        return ra != rb ? ra < rb : a < b;
    });
    // With two or more, the owner of a colony in the sector (the one listed last) keeps the centre.
    EmpireId keeper;
    for (ObjectId o : system.objects)
        if (const Colony* c = s_.colony(o); c && s_.galaxy.object(o).sector == where_.sector) keeper = c->owner;
    auto boxOf = [&](const Piece& p) -> Box {
        if (p.warped) return warpBox;
        if (p.boxDx != 0 || p.boxDy != 0) return edgeBox(p.boxDx, p.boxDy);
        if (middle.size() < 2 || p.owner == keeper) return centreBox;
        const size_t k = static_cast<size_t>(std::find(middle.begin(), middle.end(), p.owner) - middle.begin()) + 1;
        return numberedBox(k);
    };
    auto facingFor = [&](const Piece& p) {
        if (p.warped) return 2;
        if (p.boxDx != 0 || p.boxDy != 0) return edgeFacing(p.boxDx, p.boxDy);
        return rng_.rangeInt(1, 4);   // a random facing from 1 to 4, never 0
    };

    std::vector<char> placed(n, 0);
    auto put = [&](size_t i, int x, int y) {
        const auto [px, py] = settle(x, y, pieces_[i].size, static_cast<int>(i));
        pieces_[i].x = std::max(0, px);
        pieces_[i].y = std::max(0, py);
        occupy(static_cast<int>(i));
        placed[i] = 1;
    };
    auto putRandom = [&](size_t i) {
        const Box b = boxOf(pieces_[i]);
        pieces_[i].facing = facingFor(pieces_[i]);
        put(i, rng_.rangeInt(b.x, b.x + b.w), rng_.rangeInt(b.y, b.y + b.h));
    };
    // Planets and obstacles first, at a random top-left square in x 33-39, y 28-34.
    for (size_t i = 0; i < n; ++i)
        if (pieces_[i].kind == Kind::Planet || pieces_[i].kind == Kind::Obstacle) {
            pieces_[i].facing = rng_.rangeInt(1, 4);
            put(i, rng_.rangeInt(33, 39), rng_.rangeInt(28, 34));
        }

    // Then the fleets' leaders: a fleet's armed members form its combat group,
    // anchored by the fleet leader (or, when it is not here, the first armed
    // member); the others take formation positions 1, 2, 3... in piece order
    // (spec 03 §9, §10). Nothing breaks formation at placement.
    std::vector<FleetId> fleets;
    for (const Piece& p : pieces_)
        if (p.kind != Kind::Planet && p.kind != Kind::Obstacle && p.unit.fleet.valid() &&
            std::find(fleets.begin(), fleets.end(), p.unit.fleet) == fleets.end())
            fleets.push_back(p.unit.fleet);
    for (FleetId fid : fleets) {
        const Fleet* fleet = s_.fleet(fid);
        if (!fleet) continue;
        std::vector<size_t> members;
        for (size_t i = 0; i < n; ++i)
            if (!placed[i] && pieces_[i].unit.fleet == fid && pieces_[i].owner == fleet->owner &&
                std::find(fleet->members.begin(), fleet->members.end(), pieces_[i].source) != fleet->members.end())
                members.push_back(i);
        auto armed = [&](size_t i) { return !pieces_[i].mothballed && pieces_[i].armed; };
        size_t leader = SIZE_MAX;
        for (size_t i : members)
            if (pieces_[i].source == fleet->leader) leader = i;
        if (leader == SIZE_MAX)
            for (size_t i : members)
                if (armed(i)) {
                    leader = i;
                    break;
                }
        if (leader == SIZE_MAX) continue;
        const Box b = boxOf(pieces_[leader]);
        pieces_[leader].facing = facingFor(pieces_[leader]);
        // A leader in an edge box starts on the box's inner line at a random point along it.
        if (b.edge && b.innerY >= 0) put(leader, rng_.rangeInt(b.x, b.x + b.w), b.innerY);
        else if (b.edge) put(leader, b.innerX, rng_.rangeInt(b.y, b.y + b.h));
        else put(leader, rng_.rangeInt(b.x, b.x + b.w), rng_.rangeInt(b.y, b.y + b.h));
        const ruleset::Formation* formation =
            fleet->formation < r_.data().formations.size() ? &r_.data().formations[fleet->formation] : nullptr;
        size_t slot = 0;
        for (size_t i : members) {
            if (i == leader || !armed(i) || !formation) continue;
            if (slot >= formation->positions.size()) break;   // beyond the positions: no formation place
            const auto& pos = formation->positions[slot++];
            const int dx = pos.x - formation->leader.x, dy = pos.y - formation->leader.y;
            const auto [rx, ry] = rotateSlot(pieces_[leader].facing, dx, dy);
            pieces_[i].facing = facingFor(pieces_[i]);
            put(i, std::clamp(pieces_[leader].x + rx, 0, kW - 1), std::clamp(pieces_[leader].y + ry, 0, kH - 1));
            pieces_[i].leader = static_cast<int>(leader);
            pieces_[i].slotDx = dx;
            pieces_[i].slotDy = dy;
            pieces_[i].hasSlot = true;
            pieces_[i].fleetMember = true;
            pieces_[leader].isLeader = true;
        }
    }
    // Everything else at a random square of its box.
    for (size_t i = 0; i < n; ++i)
        if (!placed[i]) putRandom(i);
}

// ---- Per-round state ------------------------------------------------------------------------------

const Strategy& Battle::strategy(EmpireId e, uint32_t index) const {
    std::vector<Strategy>& list = strategies_[e.value];
    if (list.empty()) {
        if (e.valid() && e.index() < s_.empires.size())
            for (const auto& raw : s_.empire(e).strategies) list.push_back(parseStrategy(raw));
        if (list.empty()) list.emplace_back();
    }
    return list[index < list.size() ? index : 0];
}

uint32_t Battle::strategyIndex(int i) const {
    const Piece& p = pieces_[i];
    // While in its fleet's combat group a ship uses the fleet strategy, afterwards its design's (history 1.84).
    // A member whose leader left the formation stays in the group, with the fleet
    // strategy (spec 03 §10). A group a player formed in tactical combat is no fleet group (spec 04 §19.1).
    if (p.isLeader) return p.tacticalGroup ? p.designStrategy : p.fleetStrategy;
    if (p.fleetMember) return p.fleetStrategy;
    return p.designStrategy;
}

int Battle::leaderOf(int i) const {
    const Piece& p = pieces_[i];
    if (p.isLeader) return -1;
    if (p.group >= 0) {
        // A member of a player's group follows whichever piece of its side leads that number.
        for (size_t k = 0; k < pieces_.size(); ++k)
            if (static_cast<int>(k) != i && pieces_[k].alive && pieces_[k].owner == p.owner && pieces_[k].isLeader && pieces_[k].group == p.group)
                return static_cast<int>(k);
        return -1;
    }
    if (p.leader >= 0 && pieces_[p.leader].alive && pieces_[p.leader].isLeader && pieces_[p.leader].owner == p.owner) return p.leader;
    return -1;
}

bool Battle::combatant(int i) const {
    const Piece& p = pieces_[i];
    return p.alive && p.kind != Kind::Obstacle;
}

int Battle::bestCrewExperience(EmpireId e) const {
    int best = 0;
    for (const Piece& p : pieces_)
        if (p.alive && p.kind == Kind::Vehicle && p.owner == e && !p.mothballed) best = std::max(best, p.unit.experience);
    return best;
}

int Battle::fleetExp(const Piece& p) const {
    if (!p.fleet.valid() || p.captured) return 0;
    const auto it = fleetExp_.find(p.fleet.value);
    return it == fleetExp_.end() ? 0 : it->second.first;
}

int Battle::computeMp(int i) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Seeker) return p.speed;
    if (p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) return 0;
    if (p.mothballed || p.vtype == VehicleType::Satellite) return 0;
    // Half the system-map speed, rounded up (speed / 2 + 0.01, then Round), plus
    // the best Combat Movement part (confirmed: binary). The speed already holds
    // the 1-point override for missing supplies or command.
    const int speed = vehicleMaxMovement(r_, s_, p.unit);
    if (speed <= 0 && p.vtype == VehicleType::Base) return 0;
    return (std::max(0, speed) + 1) / 2 + static_cast<int>(detail::componentBest(r_, s_, p.unit, AbilityKind::CombatMovement));
}

Vehicle Battle::roster(const Piece& p) const {
    // Killed units stay in the group's records until the battle ends: its
    // abilities still count every design it had (confirmed: binary).
    Vehicle v = p.unit;
    std::vector<UnitStack> all = p.stacks;
    for (UnitStack& st : all) st.count = std::max(1, st.count);
    setGroupStacks(s_, v, std::move(all));
    return v;
}

void Battle::refreshCombatValues(int i) {
    Piece& p = pieces_[i];
    const int sys = combatBonus_.count(p.owner.value) ? combatBonus_.at(p.owner.value) : 0;
    p.alwaysHit = false;
    if (p.kind == Kind::Planet) {
        // Facilities (lost ones too, until the end) and weapon platforms, family by family (inferred: one pool of families) + racial + the setting.
        std::map<int, int64_t> plus, minus;
        auto note = [](std::map<int, int64_t>& m, int family, int64_t v) {
            auto it = m.find(family);
            if (it == m.end()) m.emplace(family, v);
            else it->second = std::max(it->second, v);
        };
        for (uint32_t f : p.facilities) {
            const auto ab = r_.facilityAbilities(f);
            if (hasAbility(ab, AbilityKind::CombatToHitOffensePlus)) note(plus, r_.facility(f).family, bestValue1(ab, AbilityKind::CombatToHitOffensePlus));
            if (hasAbility(ab, AbilityKind::CombatToHitOffenseMinus)) note(minus, r_.facility(f).family, bestValue1(ab, AbilityKind::CombatToHitOffenseMinus));
            p.alwaysHit = p.alwaysHit || hasAbility(ab, AbilityKind::WeaponsAlwaysHit);
        }
        for (const UnitStack& st : p.unit.cargo.units) {
            if (st.count <= 0 || r_.hull(s_.design(st.design).hull).type != VehicleType::WeaponPlatform) continue;
            for (const DesignEntry& e : s_.design(st.design).entries) {
                const auto ab = r_.componentAbilities(e.component);
                const int family = r_.component(e.component).family;
                if (hasAbility(ab, AbilityKind::CombatToHitOffensePlus)) note(plus, family, bestValue1(ab, AbilityKind::CombatToHitOffensePlus));
                if (hasAbility(ab, AbilityKind::CombatToHitOffenseMinus)) note(minus, family, bestValue1(ab, AbilityKind::CombatToHitOffenseMinus));
                p.alwaysHit = p.alwaysHit || hasAbility(ab, AbilityKind::WeaponsAlwaysHit);
            }
        }
        int64_t offense = 0;
        for (const auto& [f, v] : plus) offense += v;
        for (const auto& [f, v] : minus) offense -= v;
        p.offense = static_cast<int>(offense) + detail::racialOffense(r_, s_.empire(p.owner)) + cs_.planetOffense + sys;
        p.defense = cs_.planetDefense;   // the setting alone (confirmed: binary)
        return;
    }
    if (p.kind == Kind::Seeker) {
        p.offense = 0;
        p.defense = cs_.seekerDefense;
        return;
    }
    if (p.kind == Kind::Obstacle) return;
    if (p.kind == Kind::UnitGroup) {
        const Vehicle all = roster(p);
        p.offense = detail::unitOffense(r_, s_, all) + sys;
        p.defense = detail::unitDefense(r_, s_, all);
        p.alwaysHit = detail::hasIntactComponent(r_, s_, all, AbilityKind::WeaponsAlwaysHit);
        return;
    }
    if (p.mothballed) {
        p.offense = p.defense = 0;
        return;
    }
    int offense = detail::vehicleOffense(r_, s_, p.unit, p.unit.experience, fleetExp(p));
    int defense = detail::vehicleDefense(r_, s_, p.unit, p.unit.experience, fleetExp(p));
    if (detail::hasIntactComponent(r_, s_, p.unit, AbilityKind::CombatBestExperience)) {
        // A Neural Combat Net: the larger of its own value and the best crew experience here (confirmed: binary).
        const int best = bestCrewExperience(p.owner);
        offense = std::max(offense, best);
        defense = std::max(defense, best);
    }
    p.offense = offense + sys;
    p.defense = defense;
    p.alwaysHit = detail::hasIntactComponent(r_, s_, p.unit, AbilityKind::WeaponsAlwaysHit);
}

void Battle::refreshStats(int i) {
    Piece& p = pieces_[i];
    if (p.kind == Kind::Obstacle || p.kind == Kind::Seeker) return;
    // Target budget (confirmed: binary).
    if (p.kind == Kind::Planet) p.budget = kPlanetTargets;
    else if (p.vtype == VehicleType::Fighter) p.budget = 1;
    else if (p.kind == Kind::UnitGroup) {
        const int multiplex = static_cast<int>(std::max<int64_t>(1, detail::componentBest(r_, s_, roster(p), AbilityKind::MultiplexTracking)));
        p.budget = std::max(multiplex, p.unit.count);
    } else {
        p.budget = static_cast<int>(std::max<int64_t>(1, detail::componentBest(r_, s_, p.unit, AbilityKind::MultiplexTracking)));
    }
    refreshCombatValues(i);
    // Firepower by range, used to rank targets by strength.
    p.firepower.fill(0);
    p.strength = 0;
    p.armed = false;
    p.guns = false;
    for (const Weapon& w : p.weapons) {
        const int n = instances(i, w) * firedTogether(i, w);
        if (n <= 0) continue;
        p.armed = true;
        if (w.kind() != WeaponKind::PointDefense) p.guns = true;
        int64_t best = 0;
        for (int d = 1; d <= kRangeTable; ++d) {
            const int64_t dmg = int64_t{weaponDamage(r_, w.de, d)} * n / w.reloadRate;
            p.firepower[static_cast<size_t>(d)] += dmg;
            best = std::max(best, dmg);
        }
        p.strength += best;
    }
    // Viewer-independent category.
    if (p.kind == Kind::Planet) p.category = TargetCategory::Planets;
    else if (p.vtype == VehicleType::Fighter) p.category = TargetCategory::Fighters;
    else if (p.vtype == VehicleType::Satellite) p.category = TargetCategory::Satellites;
    else if (p.vtype == VehicleType::Drone) p.category = TargetCategory::Drones;
    else if (p.vtype == VehicleType::Base) p.category = p.armed ? TargetCategory::Bases : TargetCategory::BasesNoWeapons;
    else {
        const Design& d = s_.design(p.unit.design);
        const bool colony = detail::designHasComponent(r_, d, AbilityKind::ColonizeRock) ||
                            detail::designHasComponent(r_, d, AbilityKind::ColonizeIce) ||
                            detail::designHasComponent(r_, d, AbilityKind::ColonizeGas);
        const bool carrier = detail::designHasComponent(r_, d, AbilityKind::LaunchRecoverFighters) ||
                             detail::designHasComponent(r_, d, AbilityKind::LaunchDrones) ||
                             detail::designHasComponent(r_, d, AbilityKind::LaunchRecoverSatellites) ||
                             detail::designHasComponent(r_, d, AbilityKind::LayMines);
        if (colony) p.category = TargetCategory::ColonyShips;
        else if (carrier) p.category = TargetCategory::Carriers;
        else if (!p.armed && vehicleCargoCapacity(r_, s_, p.unit) > 0) p.category = TargetCategory::Transports;
        else p.category = p.armed ? TargetCategory::Ships : TargetCategory::ShipsNoWeapons;
    }
}

void Battle::refreshPiece(int i) {
    Piece& p = pieces_[i];
    if (!p.alive || p.kind == Kind::Seeker || p.kind == Kind::Obstacle) return;
    p.engaged.clear();
    p.pushed = false;
    p.launchedNow = {};
    p.mp = computeMp(i);
    p.reach = p.mp;
    refreshStats(i);
}

void Battle::afterDamage(int i) {
    // A survivor's shields are capped at their new maximum, its movement at the new allowance (confirmed: binary).
    Piece& p = pieces_[i];
    if (!p.alive || p.kind == Kind::Obstacle || p.kind == Kind::Seeker) return;
    if (p.kind == Kind::Vehicle) detail::refreshShields(r_, s_, p.unit, shieldBonus(p.owner), disruption_, p.sh, false);
    else if (p.kind == Kind::Planet) planetShields(p, false);
    const int allowance = computeMp(i);
    p.mp = std::min(p.mp, allowance);
    refreshStats(i);
    // The leader of an automated side left with 0 movement by damage dissolves its group (spec 03 §10).
    if (pieces_[i].isLeader && allowance == 0 && pieces_[i].reach > 0 && (!isPlayer(pieces_[i].owner) || autoAll_)) dissolve(i);
}

void Battle::startRound() {
    // At the start of every combat turn after the first (confirmed: binary).
    assigned_.clear();
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const int i = static_cast<int>(k);
        Piece& p = pieces_[k];
        if (!p.alive || p.kind == Kind::Seeker || p.kind == Kind::Obstacle) continue;
        for (Weapon& w : p.weapons)
            for (int& c : w.reload) c = std::max(0, c - 1);
        if (p.kind == Kind::Vehicle && !p.mothballed && hasSupply(i)) {
            const Design& d = s_.design(p.unit.design);
            const int64_t regen = detail::componentSum(r_, s_, p.unit, AbilityKind::ShieldRegeneration) +
                                  detail::hullSum(r_, d, AbilityKind::ShieldRegeneration);
            p.sh.current = static_cast<int>(std::min<int64_t>(p.sh.max, p.sh.current + regen));
            // Organic armor: the pool fills; destroyed parts come back whole in
            // design order, skipping those the pool cannot pay for; when none was
            // destroyed as the step began, the pool empties (history 1.80).
            const bool destroyed = detail::hasDestroyedRegeneratingArmor(r_, s_, p.unit);
            const int64_t organic = detail::componentSum(r_, s_, p.unit, AbilityKind::ArmorRegeneration);
            p.regenPool = std::min(kRegenerationCap, p.regenPool + organic);
            p.regenPool -= detail::restoreRegeneratingArmor(r_, s_, p.unit, p.regenPool);
            if (!destroyed) p.regenPool = 0;
            detail::refreshShields(r_, s_, p.unit, shieldBonus(p.owner), disruption_, p.sh, false);
        }
        // Planets and unit groups never regenerate shields in combat (confirmed: binary).
        refreshPiece(i);
    }
}

bool Battle::hasPieces(EmpireId e) const {
    return std::any_of(pieces_.begin(), pieces_.end(),
                       [&](const Piece& p) { return p.alive && p.owner == e && p.kind != Kind::Seeker && p.kind != Kind::Obstacle; });
}

bool Battle::over() const {
    // The battle ends as soon as no two empires that still have pieces are hostile (confirmed: binary).
    for (EmpireId a : empires_)
        for (EmpireId b : empires_)
            if (a < b && detail::enemies(s_, a, b) && hasPieces(a) && hasPieces(b)) return false;
    return true;
}

// ---- Queries ----------------------------------------------------------------------------------------

std::vector<EmpireId> Battle::fighting() const {
    std::vector<EmpireId> out;
    for (EmpireId e : empires_)
        for (EmpireId o : empires_)
            if (o != e && detail::enemies(s_, e, o)) {
                out.push_back(e);
                break;
            }
    return out;
}

bool Battle::hostileTo(int i, int t) const {
    const Piece& a = pieces_[i];
    const Piece& b = pieces_[t];
    return a.owner.valid() && b.owner.valid() && a.owner != b.owner && detail::enemies(s_, a.owner, b.owner);
}

int Battle::occupant(int x, int y) const { return onMap(x, y) ? occ_[static_cast<size_t>(y * kW + x)] : -1; }

std::array<int, 3> Battle::launchLeft(int i) const {
    const Piece& p = pieces_[i];
    std::array<int, 3> left{};
    if (!p.alive || p.mothballed) return left;
    if (p.kind == Kind::Planet) {
        // Up to 100 of each kind per combat turn (confirmed: binary).
        left = {kPlanetLaunch, kPlanetLaunch, kPlanetLaunch};
    } else if (p.kind == Kind::Vehicle) {
        left[kLaunchFighters] = static_cast<int>(detail::componentSum(r_, s_, p.unit, AbilityKind::LaunchRecoverFighters));
        left[kLaunchSatellites] = static_cast<int>(detail::componentSum(r_, s_, p.unit, AbilityKind::LaunchRecoverSatellites));
        left[kLaunchDrones] = static_cast<int>(detail::componentSum(r_, s_, p.unit, AbilityKind::LaunchDrones));
    }
    for (size_t k = 0; k < left.size(); ++k) left[k] = std::max(0, left[k] - p.launchedNow[k]);
    return left;
}

int Battle::launchKindOf(DesignId design) const {
    switch (r_.hull(s_.design(design).hull).type) {
        case VehicleType::Fighter: return kLaunchFighters;
        case VehicleType::Satellite: return kLaunchSatellites;
        case VehicleType::Drone: return kLaunchDrones;
        default: return -1;
    }
}

int Battle::satellitesPresent(EmpireId e) const {
    int present = 0;
    for (const Piece& q : pieces_)
        if (q.alive && q.owner == e && q.kind == Kind::UnitGroup && q.vtype == VehicleType::Satellite) present += q.unit.count;
    return present;
}

int Battle::dist(int a, int b) const {
    const Piece& p = pieces_[a];
    const Piece& q = pieces_[b];
    return std::max(gap(p.x, p.size, q.x, q.size), gap(p.y, p.size, q.y, q.size));
}

int Battle::aimDist(int a, int b) const {
    const Piece& p = pieces_[a];
    const Piece& q = pieces_[b];
    return std::max(std::abs(p.x - q.x), std::abs(p.y - q.y));
}

std::pair<int, int> Battle::centreOf(int i) const {
    const Piece& p = pieces_[i];
    return p.size > 1 ? std::pair{p.x + 1, p.y + 1} : std::pair{p.x, p.y};
}

bool Battle::isFree(int x, int y, int self) const {
    if (!onMap(x, y)) return false;
    const int o = occ_[static_cast<size_t>(y * kW + x)];
    return o < 0 || o == self;
}

uint8_t Battle::maskOf(int i) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet) return kTargetPlanets;
    if (p.kind == Kind::Seeker) return kTargetSeekers;
    if (p.kind == Kind::Obstacle) return 0;   // never a target (confirmed: binary)
    return targetMaskOf(p.vtype);
}

TargetCategory Battle::categoryFor(int j, EmpireId viewer) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Seeker)
        return p.seekTarget >= 0 && pieces_[p.seekTarget].owner == viewer ? TargetCategory::SeekersOnUs : TargetCategory::SeekersOnOthers;
    return p.category;
}

// Population × damage per population + the hit points of the stored units −
// their pool (confirmed: binary). Landed troops are not the planet's.
int64_t Battle::planetHp(const Piece& p) const {
    int64_t hp = 0;
    for (const PopulationGroup& g : p.population) hp += g.millions * cs_.damagePerPopulation;
    for (const UnitStack& st : p.unit.cargo.units)
        if (st.count > 0) hp += int64_t{st.count} * detail::unitHitPoints(r_, s_.design(st.design));
    return std::max<int64_t>(0, hp - p.pool);
}

int64_t Battle::hitPoints(int j) const {
    const Piece& p = pieces_[j];
    switch (p.kind) {
        case Kind::Seeker: return std::max<int64_t>(0, p.hp * p.members - p.pool);
        case Kind::Planet: return planetHp(p);
        case Kind::UnitGroup: return std::max<int64_t>(0, groupHitPoints(p) - p.pool);
        case Kind::Vehicle: return detail::remainingStructure(r_, s_, p.unit);
        case Kind::Obstacle: return 0;
    }
    return 0;
}

// Hit points in a ram (spec 04 §10.3, confirmed: binary): never shields.
int64_t Battle::ramHitPoints(int j) const {
    const Piece& p = pieces_[j];
    switch (p.kind) {
        case Kind::Vehicle: return std::max<int64_t>(0, detail::remainingStructure(r_, s_, p.unit) - p.pool);
        case Kind::UnitGroup: {
            // The living units' structure, shields once for fighters, troops and platforms; the pools are not subtracted.
            int64_t hp = 0;
            for (const UnitStack& st : p.stacks) {
                if (st.count <= 0) continue;
                const UnitToughness u = detail::unitToughness(r_, s_.design(st.design));
                hp += (u.structure + (u.doubled ? u.shields : 0)) * st.count;
            }
            return hp;
        }
        case Kind::Seeker: return std::max<int64_t>(0, p.hp - p.pool);   // one member, whatever the group's size
        case Kind::Planet: return planetHp(p);
        case Kind::Obstacle: return 0;
    }
    return 0;
}

int Battle::damagePercent(int j) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Seeker || p.kind == Kind::Obstacle) return 0;
    if (p.kind == Kind::Planet) return p.hpStart > 0 ? static_cast<int>(std::clamp<int64_t>(100 - planetHp(p) * 100 / p.hpStart, 0, 100)) : 0;
    const Design& d = s_.design(p.unit.design);
    if (p.kind == Kind::Vehicle) {
        const int structure = std::max(1, detail::designStructure(r_, d));
        return (structure - detail::remainingStructure(r_, s_, p.unit)) * 100 / structure;
    }
    const int64_t total = std::max<int64_t>(1, p.hpStart);
    return static_cast<int>(std::clamp<int64_t>((total - hitPoints(j)) * 100 / total, 0, 100));
}

int64_t Battle::sizeOf(int j) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Planet) return kPlanetSizeRank;
    if (p.kind == Kind::Seeker || p.kind == Kind::Obstacle) return 0;
    if (p.kind == Kind::UnitGroup) {
        int64_t tons = 0;
        for (const UnitStack& st : p.stacks) tons += int64_t{r_.hull(s_.design(st.design).hull).tonnage} * std::max(0, st.count);
        return tons;
    }
    return int64_t{r_.hull(s_.design(p.unit.design).hull).tonnage} * p.unit.count;
}

bool Battle::hasSupply(int i) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet) return true;   // planets never need supplies (confirmed: binary)
    if (p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) return false;
    return detail::hasSupplies(r_, s_, p.unit);
}

// Instances of a weapon that can fire: one on a ship or fighter group, one per
// unit of a satellite or drone group, one per platform of a planet stack.
int Battle::instances(int i, const Weapon& w) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet) return w.stack >= 0 ? std::min<int>(static_cast<int>(w.reload.size()), p.unit.cargo.units[static_cast<size_t>(w.stack)].count) : 0;
    if (p.kind == Kind::UnitGroup) {
        if (p.unit.count <= 0) return 0;
        if (p.vtype == VehicleType::Fighter) return firedTogether(i, w) > 0 ? 1 : 0;
        if (w.stack < 0 || static_cast<size_t>(w.stack) >= p.stacks.size()) return 0;
        return std::min<int>(static_cast<int>(w.reload.size()), p.stacks[static_cast<size_t>(w.stack)].count);
    }
    return entryIntact(r_, s_, p.unit, w.entry) ? 1 : 0;
}

int Battle::firedTogether(int i, const Weapon& w) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::UnitGroup && p.vtype == VehicleType::Fighter) {
        // Every unit's identical weapons, over the group's designs.
        int n = 0;
        for (const auto& [stack, per] : w.shares)
            if (static_cast<size_t>(stack) < p.stacks.size()) n += std::max(0, p.stacks[static_cast<size_t>(stack)].count) * per;
        return n;
    }
    return 1;
}

bool Battle::canMove(int att, int t) const {
    const Piece& a = pieces_[att];
    const Piece& b = pieces_[t];
    // Planets cannot be moved; unit groups cannot push or pull; a ship moves a ship
    // only if its hull is at least as large (confirmed: binary).
    if (b.kind == Kind::Planet || b.kind == Kind::Obstacle || b.kind == Kind::Seeker) return false;
    if (a.kind == Kind::UnitGroup) return false;
    if (a.kind == Kind::Vehicle && b.kind == Kind::Vehicle)
        return r_.hull(s_.design(a.unit.design).hull).tonnage >= r_.hull(s_.design(b.unit.design).hull).tonnage;
    return true;
}

// Whether a weapon of this damage type can do anything to piece t: the
// computer assigns weapons only then (spec 04 §16).
bool Battle::canAffect(DamageType type, int t, int att) const {
    const Piece& b = pieces_[t];
    const detail::DamageRule rule = detail::damageRule(type);
    switch (b.kind) {
        case Kind::Obstacle: return false;
        case Kind::Seeker: return rule.structural && !rule.shieldsOnly && !isSpecialEffect(type);
        case Kind::Planet:
            // Against planets (spec 04 §9.5, confirmed: binary).
            if (isPlanetOnlyDamage(type)) return true;
            switch (type) {
                case DamageType::OnlyWeapons:
                case DamageType::OnlyShieldGenerators:
                case DamageType::OnlyMasterComputers: return false;   // nothing at all
                case DamageType::ShieldsOnly:
                case DamageType::OnlyEngines:
                case DamageType::OnlyBoardingParties:
                case DamageType::OnlySecurityStations:
                case DamageType::OnlyPlanetDestroyers:
                case DamageType::PushesTarget:
                case DamageType::PullsTarget:
                case DamageType::RandomTargetMovement: return b.sh.current > 0;   // they only drain the shields
                case DamageType::IncreaseReloadTime:
                case DamageType::DisruptReloadTime: return b.armed;
                default: return true;
            }
        case Kind::UnitGroup:
            if (isPlanetOnlyDamage(type) || type == DamageType::CrewConversion) return false;
            if (type == DamageType::PushesTarget || type == DamageType::PullsTarget) return canMove(att, t);
            if (type == DamageType::IncreaseReloadTime || type == DamageType::DisruptReloadTime) return b.armed;
            return true;   // every other type kills units by the unit group rule (spec 04 §9.4)
        case Kind::Vehicle:
            if (type == DamageType::PushesTarget || type == DamageType::PullsTarget) return canMove(att, t);
            return detail::canAffectVehicle(r_, s_, b.unit, b.sh, type);
    }
    return false;
}

int Battle::damageBonus(EmpireId e) const {
    const auto it = damageBonus_.find(e.value);
    return it == damageBonus_.end() ? 0 : it->second;
}

int Battle::hitChance(int i, const Weapon& w, int t) const {
    // Base + offense + system bonus + weapon modifiers − defense − per square × aim distance − interference (confirmed: binary).
    const int offense = pieces_[i].offense + mounted(r_, w.de).toHitModifier;
    return detail::toHitChance(cs_, aimDist(i, t), offense, pieces_[t].defense, interference_);
}

bool Battle::hasTroops(int i) const {
    // Units in cargo have no owner of their own: a ship's troops land for the ship's owner (spec 04 §13).
    if (pieces_[i].kind != Kind::Vehicle) return false;
    for (const UnitStack& u : pieces_[i].unit.cargo.units)
        if (u.count > 0 && isTroopDesign(r_, s_, u.design)) return true;
    return false;
}

bool Battle::contestedBy(const Piece& planet, EmpireId e) const {
    if (!planet.invader.valid() || planet.invader == e) return false;
    return std::any_of(planet.landed.begin(), planet.landed.end(), [](const UnitStack& u) { return u.count > 0; });
}

// The computer stops assigning fire once 1.5 × (shields + hit points) of a ship
// or planet, or 1 × of a unit group or seeker, is on its way this turn;
// seekers in flight count separately (confirmed: binary).
bool Battle::overkill(int i, int t, bool seeker) const {
    const Piece& b = pieces_[t];
    const int64_t total = int64_t{b.kind == Kind::Vehicle || b.kind == Kind::Planet ? b.sh.current : 0} + hitPoints(t);
    const bool big = b.kind == Kind::Vehicle || b.kind == Kind::Planet;
    int64_t sent = 0;
    if (seeker) sent = incomingSeekerDamage(t);
    else if (const auto it = assigned_.find({pieces_[i].owner.value, t}); it != assigned_.end()) sent = it->second;
    return big ? sent * 2 >= total * 3 : sent >= total;
}

// ---- Targeting (spec 04 §6, §16) ------------------------------------------------------------------------

// While a side has a ship carrying troops whose strategy in effect is Drop
// Troops, it holds fire on enemy planets without guns (confirmed: binary).
bool Battle::holdsFire(EmpireId e) {
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& p = pieces_[k];
        if (p.alive && p.owner == e && p.kind == Kind::Vehicle && hasTroops(static_cast<int>(k)) &&
            strategyInEffect(static_cast<int>(k)) == MoveStrategy::DropTroops)
            return true;
    }
    return false;
}

std::vector<int> Battle::sortedTargets(int i, const Strategy& S) {
    struct Candidate {
        int idx = 0;
        bool fresh = true;
        int priority = 0;
        std::array<int64_t, 4> keys{};
    };
    std::vector<Candidate> list;
    const Piece& a = pieces_[i];
    // Checked afresh every time targets are chosen, for moving and for firing (confirmed: binary).
    const bool hold = holdsFire(a.owner);
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const int j = static_cast<int>(k);
        const Piece& b = pieces_[k];
        if (!combatant(j) || j == i || b.owner == a.owner || !detail::enemies(s_, a.owner, b.owner)) continue;
        const TargetCategory cat = categoryFor(j, a.owner);
        if (S.dontFireOn[static_cast<size_t>(cat)]) continue;
        if (b.kind == Kind::Planet) {
            if (hold && !b.guns) continue;
            // Never a planet where its own troops, or a friendly empire's, still fight on the ground.
            if (b.invader.valid() && (b.invader == a.owner || !detail::enemies(s_, a.owner, b.invader)) &&
                std::any_of(b.landed.begin(), b.landed.end(), [](const UnitStack& u) { return u.count > 0; }))
                continue;
        }
        Candidate c;
        c.idx = j;
        const int pr = S.typePriority[static_cast<size_t>(cat)];
        c.priority = pr > 0 ? pr : 1000;
        // Skip targets damaged past the strategy's percentage, unless they still have
        // weapons and the strategy says so; drones and seekers never (confirmed: binary).
        int threshold = S.damagePercentShip;
        if (b.kind == Kind::Planet) threshold = S.damagePercentPlanet;
        else if (b.vtype == VehicleType::Fighter) threshold = S.damagePercentFighters;
        else if (b.vtype == VehicleType::Satellite) threshold = S.damagePercentSatellites;
        const int dmgPct = damagePercent(j);
        c.fresh = b.kind == Kind::Seeker || b.vtype == VehicleType::Drone || (S.damageUntilWeaponsGone && b.armed) || dmgPct <= threshold;
        for (size_t n = 0; n < S.targeting.size(); ++n) {
            int64_t v = 0;
            switch (S.targeting[n]) {
                case TargetKey::None: break;
                case TargetKey::Nearest: v = dist(i, j); break;
                case TargetKey::Farthest: v = -dist(i, j); break;
                case TargetKey::Largest: v = -sizeOf(j); break;
                case TargetKey::Smallest: v = sizeOf(j); break;
                case TargetKey::MostDamaged: v = -dmgPct; break;
                case TargetKey::LeastDamaged: v = dmgPct; break;
                case TargetKey::Fastest: v = -b.mp; break;
                case TargetKey::Slowest: v = b.mp; break;
                case TargetKey::Strongest: v = -b.strength; break;
                case TargetKey::Weakest: v = b.strength; break;
                case TargetKey::HasWeapons: v = b.armed ? 0 : 1; break;
                case TargetKey::NoWeapons: v = b.armed ? 1 : 0; break;
            }
            c.keys[n] = v;
        }
        list.push_back(c);
    }
    const bool typeFirst = S.typePriorityFirst;
    // Targets past the damage percentage come last: the choice falls back to them when nothing else is left.
    std::sort(list.begin(), list.end(), [&](const Candidate& x, const Candidate& y) {
        if (x.fresh != y.fresh) return x.fresh;
        if (typeFirst && x.priority != y.priority) return x.priority < y.priority;
        if (x.keys != y.keys) return x.keys < y.keys;
        if (!typeFirst && x.priority != y.priority) return x.priority < y.priority;   // (inferred) later tie-break
        return x.idx < y.idx;
    });
    std::vector<int> out;
    out.reserve(list.size());
    for (const Candidate& c : list) out.push_back(c.idx);
    return out;
}

int64_t Battle::incomingSeekerDamage(int t) const {
    int64_t total = 0;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& sk = pieces_[k];
        if (!sk.alive || sk.kind != Kind::Seeker || sk.seekTarget != t) continue;
        const auto [cx, cy] = centreOf(t);
        const int travel = sk.travelled + std::max(std::abs(cx - sk.x), std::abs(cy - sk.y));
        total += int64_t{weaponDamage(r_, sk.seekWeapon.de, std::clamp(travel, 1, 20))} * sk.members;
    }
    return total;
}

int Battle::pickTarget(int i, const Weapon& w, const std::vector<int>& targets) {
    const Piece& a = pieces_[i];
    const bool pd = w.kind() == WeaponKind::PointDefense;
    const bool seeking = w.kind() == WeaponKind::Seeking;
    const bool moves = w.type == DamageType::PushesTarget || w.type == DamageType::PullsTarget || w.type == DamageType::RandomTargetMovement;
    for (int t : targets) {
        const Piece& b = pieces_[t];
        // A target converted by an earlier shot of this volley is no longer hostile (spec 04 §2).
        if (!combatant(t) || !hostileTo(i, t) || !(w.targets & maskOf(t))) continue;
        if (seeking) {
            // A seeker needs its target within travel range (inferred: straight to the centre square).
            const auto [cx, cy] = centreOf(t);
            const auto [sx, sy] = centreOf(i);
            if (std::max(std::abs(cx - sx), std::abs(cy - sy)) > w.reach) continue;
        } else if (weaponDamage(r_, w.de, dist(i, t)) <= 0) {
            continue;
        }
        if (overkill(i, t, seeking)) continue;
        const bool engaged = std::find(a.engaged.begin(), a.engaged.end(), t) != a.engaged.end();
        if (!pd && !engaged && static_cast<int>(a.engaged.size()) >= a.budget) continue;
        if (!canAffect(w.type, t, i)) continue;
        if (moves && b.pushed) continue;   // spread push/pull/teleport over different enemies (history 1.73)
        return t;
    }
    return -1;
}

// The target a ready weapon has while its piece plans its move: the first of
// the sorted targets it can hit, wherever it stands (inferred: range and the
// target budget are left to the moment it fires).
int Battle::planTarget(int i, const Weapon& w, const std::vector<int>& targets) {
    const bool seeking = w.kind() == WeaponKind::Seeking;
    for (int t : targets) {
        if (!combatant(t) || !hostileTo(i, t) || !(w.targets & maskOf(t))) continue;
        if (overkill(i, t, seeking) || !canAffect(w.type, t, i)) continue;
        return t;
    }
    return -1;
}

// ---- Actions --------------------------------------------------------------------------------------------

void Battle::event(Ev k, int piece, int target, int amount, uint32_t component) {
    const Piece& p = pieces_[piece];
    CombatEvent e;
    e.kind = k;
    e.round = static_cast<uint8_t>(std::clamp(round_, 0, 255));
    e.piece = static_cast<uint32_t>(piece);
    e.target = static_cast<uint32_t>(target);
    e.x = static_cast<int16_t>(p.x);
    e.y = static_cast<int16_t>(p.y);
    e.amount = amount;
    e.component = component;
    rec_.events.push_back(e);
}

std::string Battle::label(int i) const {
    const Piece& p = pieces_[i];
    std::string who = p.owner.valid() ? s_.empire(p.owner).name : std::string("?");
    if (p.kind == Kind::UnitGroup) return std::format("{} x{} ({})", p.name, p.unit.count, who);
    return std::format("{} ({})", p.name, who);
}

void Battle::phase(EmpireId e) {
    // The side's drones move and attack, then its seekers, then everything else
    // (confirmed: binary); computer carriers launch as they act.
    phaseDrones(e);
    moveSeekers(e);
    phasePieces(e);
}

void Battle::phaseDrones(EmpireId e) {
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (pieces_[k].alive && pieces_[k].owner == e && pieces_[k].kind == Kind::UnitGroup && pieces_[k].vtype == VehicleType::Drone &&
            !acted_[k]) {
            acted_[k] = 1;
            droneAct(static_cast<int>(k));
        }
}

void Battle::phasePieces(EmpireId e) {
    // Spec 04 §16.1: the danger map is built once, as the side's movement
    // begins; group leaders act first, then the others in piece order (planets
    // only launch then); after all have moved, every piece that has not acted
    // fires (confirmed: binary).
    buildDanger(e);
    auto ready = [&](size_t k) {
        return !acted_[k] && pieces_[k].alive && pieces_[k].owner == e && pieces_[k].kind != Kind::Seeker && pieces_[k].kind != Kind::Obstacle;
    };
    auto mover = [&](size_t k) { return pieces_[k].kind == Kind::Vehicle || pieces_[k].kind == Kind::UnitGroup; };
    auto still = [&](size_t k) { return pieces_[k].kind == Kind::Planet || (pieces_[k].kind == Kind::UnitGroup && pieces_[k].vtype == VehicleType::Satellite); };
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (ready(k) && pieces_[k].isLeader && mover(k) && !still(k)) act(static_cast<int>(k));
    for (size_t k = 0; k < pieces_.size(); ++k) {
        if (!ready(k)) continue;
        if (still(k)) {
            if (pieces_[k].kind == Kind::Planet) launchFrom(static_cast<int>(k));
            continue;
        }
        act(static_cast<int>(k));
    }
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (ready(k)) {
            acted_[k] = 1;
            fire(static_cast<int>(k));
        }
}

void Battle::act(int i) {
    acted_[static_cast<size_t>(i)] = 1;
    if (pieces_[i].mothballed) return;
    if (pieces_[i].kind == Kind::Vehicle || pieces_[i].kind == Kind::Planet) launchFrom(i);
    if (!pieces_[i].alive) return;
    if (pieces_[i].kind == Kind::Planet || (pieces_[i].kind == Kind::UnitGroup && pieces_[i].vtype == VehicleType::Satellite)) {
        fire(i);
        return;
    }
    if (pieces_[i].kind == Kind::UnitGroup && pieces_[i].vtype == VehicleType::Drone) {
        droneAct(i);
        return;
    }
    // A member that keeps its formation heads for its slot (spec 04 §16.1).
    if (leaderOf(i) >= 0 && pieces_[i].hasSlot) {
        if (!leavesFormation(i)) {
            followLeader(i, true);
            if (pieces_[i].alive) fire(i);
            if (pieces_[i].alive) pieces_[i].mp = 0;
            return;
        }
        // It leaves the formation: only its own marks are cleared (spec 03 §10).
        if (logging(i)) logOrder(TacticalOrder{TacticalOrder::Kind::ClearGroup, pieces_[i].owner, i});
        pieces_[i].leader = -1;
        pieces_[i].group = -1;
        pieces_[i].hasSlot = false;
        pieces_[i].fleetMember = false;
    } else if (pieces_[i].isLeader && !pieces_[i].tacticalGroup && leavesFormation(i)) {
        // A fleet's leader that leaves the formation clears only its own marks: its
        // members stay in the group with the fleet strategy, but have no leader to
        // follow and move on their own (spec 03 §10).
        if (logging(i)) logOrder(TacticalOrder{TacticalOrder::Kind::ClearGroup, pieces_[i].owner, i});
        pieces_[i].isLeader = false;
    }
    // Each piece: chooses a destination; fires first if that square is farther
    // (aim distance) from the target of its first ready weapon than its current
    // square is; moves; drops troops, rams or boards; then fires again with
    // whatever is still ready (confirmed: binary). One move per phase.
    const MovePlan mv = plan(i);
    const int t = mv.target;
    int aimTarget = -1;
    {
        const std::vector<int> targets = sortedTargets(i, strategyOf(i));
        for (const Weapon& w : pieces_[i].weapons) {
            if (w.kind() == WeaponKind::PointDefense || instances(i, w) <= 0) continue;
            bool readyNow = false;
            for (int k = 0; k < instances(i, w); ++k) readyNow = readyNow || w.reload[static_cast<size_t>(k)] == 0;
            if (!readyNow) continue;
            aimTarget = planTarget(i, w, targets);
            if (aimTarget >= 0) break;
        }
    }
    bool fireFirst = false;
    if (aimTarget >= 0 && !mv.path.empty()) {
        const Piece& q = pieces_[aimTarget];
        fireFirst = cheb(mv.path.back().first, mv.path.back().second, q.x, q.y) > cheb(pieces_[i].x, pieces_[i].y, q.x, q.y);
    }
    if (fireFirst) fire(i);
    if (!pieces_[i].alive) return;
    const bool leads = pieces_[i].isLeader;
    logMove(i, mv.path);
    walk(i, mv.path);
    if (!pieces_[i].alive) return;
    // After the leader's move the group dissolves when every square on the map
    // around it is taken (spec 03 §10); otherwise the members follow, each toward
    // its slot around the leader's new square (spec 04 §5).
    if (leads && pieces_[i].isLeader && surrounded(i)) dissolveByStrategy(i);
    if (leads && pieces_[i].isLeader)
        for (size_t m = 0; m < pieces_.size(); ++m)
            if (pieces_[m].alive && leaderOf(static_cast<int>(m)) == i && pieces_[m].hasSlot && pieces_[m].mp > 0 && !acted_[m] &&
                !leavesFormation(static_cast<int>(m)))
                followLeader(static_cast<int>(m), true);
    if (t >= 0 && combatant(t) && dist(i, t) <= 1) {
        switch (mv.mode) {
            case MoveStrategy::DropTroops: dropTroops(i, t); break;
            case MoveStrategy::BoardEnemyShips: board(i, t); break;
            case MoveStrategy::Ram:
                if (pieces_[i].mp > 0) ram(i, t);
                break;
            default: break;
        }
    }
    if (!pieces_[i].alive) return;
    fire(i);
    if (pieces_[i].alive) pieces_[i].mp = 0;
}

void Battle::fire(int i) {
    if (!combatant(i) || pieces_[i].kind == Kind::Seeker || pieces_[i].mothballed || !hasSupply(i)) return;
    const Strategy& S = strategyOf(i);
    const std::vector<int> targets = sortedTargets(i, S);
    // Weapons fire one at a time in design order (confirmed: binary).
    for (size_t wi = 0; wi < pieces_[i].weapons.size(); ++wi) {
        for (size_t k = 0;; ++k) {
            if (!pieces_[i].alive || !hasSupply(i)) return;
            const Weapon& w = pieces_[i].weapons[wi];
            if (k >= static_cast<size_t>(instances(i, w))) break;
            if (w.reload[k] > 0) continue;
            const int t = pickTarget(i, w, targets);
            if (t < 0) break;
            if (logging(i)) {
                TacticalOrder o{TacticalOrder::Kind::Fire, pieces_[i].owner, i, t};
                o.weapon = static_cast<int>(wi);
                o.instance = static_cast<int>(k);
                logOrder(std::move(o));
            }
            shoot(i, wi, k, t);
        }
    }
}

void Battle::shoot(int i, size_t wi, size_t k, int t) {
    const Weapon w = pieces_[i].weapons[wi];
    const int n = firedTogether(i, w);
    if (n <= 0) return;
    {
        Piece& a = pieces_[i];
        if ((a.kind == Kind::Vehicle || a.kind == Kind::UnitGroup) && detail::usesSupply(r_, s_, a.unit)) {
            if (a.unit.supply <= 0) return;   // zero supplies: cannot fire (confirmed: binary)
            // Supply Amount Used times the weapons fired together (confirmed: binary). A
            // group's supply is kept per unit here, so each fighter pays for its own guns.
            const int perHolder = a.kind == Kind::UnitGroup ? w.perUnit : n;
            a.unit.supply = std::max<int64_t>(0, a.unit.supply - int64_t{detail::supplyPerShot(r_, w.de)} * perHolder);
        }
        a.weapons[wi].reload[k] = w.reloadRate;
        a.fired = true;
        if (w.kind() != WeaponKind::PointDefense && std::find(a.engaged.begin(), a.engaged.end(), t) == a.engaged.end())
            a.engaged.push_back(t);
    }
    event(Ev::Fire, i, t, static_cast<int>(w.entry), w.de.component);
    if (w.kind() == WeaponKind::Seeking) {
        launchSeeker(i, w, t, n);
        return;
    }
    const int table = weaponDamage(r_, w.de, dist(i, t));
    assigned_[{pieces_[i].owner.value, t}] += int64_t{table} * n;
    // Direct fire and point-defense roll to hit; a fighter group's weapons roll one by one
    // and the hits add up to one hit (confirmed: binary).
    const int chance = pieces_[i].alwaysHit ? 100 : hitChance(i, w, t);
    int hits = 0;
    for (int m = 0; m < n; ++m)
        if (chance >= 100 || rng_.rangeInt(1, 100) <= chance) ++hits;
    if (hits == 0) {
        event(Ev::Miss, i, t, 0, w.de.component);
        return;
    }
    const int64_t damage = xmath::pctRound(int64_t{table} * hits, 100 + damageBonus(pieces_[i].owner));
    event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, damage)), w.de.component);
    applyHit(i, t, w.type, damage);
}

void Battle::launchSeeker(int i, const Weapon& w, int t, int count) {
    // A planet launches from one of its four central squares, at random (confirmed: binary).
    auto [x, y] = centreOf(i);
    if (pieces_[i].kind == Kind::Planet) {
        x = pieces_[i].x + 1 + static_cast<int>(rng_.below(2));
        y = pieces_[i].y + 1 + static_cast<int>(rng_.below(2));
    }
    // Any seeker of the same empire, component and target on that square takes
    // the new one in, whether it has moved or not and whoever launched it; it
    // keeps its travelled count and its first launcher. Mounts are not compared (confirmed: binary).
    for (size_t k = 0; k < pieces_.size(); ++k) {
        Piece& sk = pieces_[k];
        if (sk.alive && sk.kind == Kind::Seeker && sk.owner == pieces_[i].owner && sk.seekTarget == t &&
            sk.seekWeapon.de.component == w.de.component && sk.x == x && sk.y == y) {
            sk.members += count;
            event(Ev::Seeker, static_cast<int>(k), t, count, w.de.component);
            return;
        }
    }
    Piece sk;
    sk.kind = Kind::Seeker;
    sk.owner = sk.startOwner = pieces_[i].owner;
    sk.unit.owner = sk.owner;
    if (pieces_[i].kind == Kind::Planet) {
        if (w.stack >= 0) sk.unit.design = pieces_[i].unit.cargo.units[static_cast<size_t>(w.stack)].design;
    } else if (pieces_[i].kind == Kind::UnitGroup && w.stack >= 0 && static_cast<size_t>(w.stack) < pieces_[i].stacks.size()) {
        sk.unit.design = pieces_[i].stacks[static_cast<size_t>(w.stack)].design;   // the design that fired it
    } else {
        sk.unit.design = pieces_[i].unit.design;
    }
    sk.name = w.comp->name;
    sk.x = x;
    sk.y = y;
    sk.seekTarget = t;
    sk.launcher = i;
    sk.speed = std::max(0, w.comp->weapon.seekerSpeed);
    sk.hp = std::max(1, w.comp->weapon.seekerDamageResistance);
    sk.seekWeapon = w;
    sk.members = count;
    sk.launchRound = round_;
    sk.defense = cs_.seekerDefense;
    const int idx = addPiece(std::move(sk));
    event(Ev::Seeker, idx, t, count, w.de.component);
}

// Spec 04 §9.1: one hit of `damage` (after the system damage modifier) on piece t.
void Battle::applyHit(int att, int t, DamageType type, int64_t damage) {
    if (!combatant(t) || damage < 0) return;
    Piece& b = pieces_[t];
    // Push, pull and teleport move the target first; the value then still counts as damage.
    if ((type == DamageType::PushesTarget || type == DamageType::PullsTarget) && att >= 0 && canMove(att, t)) {
        forcedMove(t, att, damage, type == DamageType::PushesTarget);
        pieces_[t].pushed = true;
    } else if (type == DamageType::RandomTargetMovement && b.kind != Kind::Planet && b.kind != Kind::Seeker) {
        randomMove(t);
        pieces_[t].pushed = true;
    }
    // Crew Conversion and the reload types act, then do no damage; shields do not stop them.
    switch (type) {
        case DamageType::CrewConversion:
            if (att < 0) return;
            if (b.kind == Kind::Vehicle && detail::canAffectVehicle(r_, s_, b.unit, b.sh, type) && rng_.rangeInt(1, 100) <= damage)
                capture(t, att, false);
            // A conversion weapon able to target planets makes the planet's piece fight for the converter (confirmed: binary).
            else if (b.kind == Kind::Planet && rng_.rangeInt(1, 100) <= damage)
                convertPlanet(t, att);
            return;
        case DamageType::IncreaseReloadTime:
        case DamageType::DisruptReloadTime:
            // Every weapon's reload counter, up to 250; a planet has no Master Computer (confirmed: binary).
            if (b.kind == Kind::Vehicle || b.kind == Kind::UnitGroup || b.kind == Kind::Planet) {
                if (type == DamageType::IncreaseReloadTime && b.kind == Kind::Vehicle &&
                    detail::hasIntactComponent(r_, s_, b.unit, AbilityKind::MasterComputer))
                    return;
                for (Weapon& w : b.weapons)
                    for (int& c : w.reload) c = static_cast<int>(std::min<int64_t>(kMaxReload, c + damage));
            }
            return;
        default: break;
    }
    switch (pieces_[t].kind) {
        case Kind::Seeker: seekerHit(att, t, type, damage); break;
        case Kind::Planet: planetHit(att, t, type, damage); break;
        case Kind::UnitGroup: groupHit(att, t, type, damage); break;
        case Kind::Vehicle: shipHit(att, t, type, damage); break;
        case Kind::Obstacle: break;
    }
}

void Battle::shipHit(int att, int t, DamageType type, int64_t damage) {
    if (isPlanetOnlyDamage(type)) return;
    Piece& b = pieces_[t];
    const detail::HitResult h = detail::hitVehicle(r_, s_, b.unit, b.sh, b.pool, damage, type, rng_);
    if (h.reached > 0 || h.shieldDamage > 0) b.damaged = true;
    if (h.destroyed) {
        b.unitsLost = 1;
        kill(t, att);
        return;
    }
    // A destroyed cargo part takes its cargo at once, during the battle (confirmed: binary).
    if (h.reached > 0) detail::cutCargo(r_, s_, b.unit);
    afterDamage(t);
}

void Battle::groupHit(int att, int t, DamageType type, int64_t damage) {
    // Spec 04 §9.4 (confirmed: binary). A unit group has no piece-level shields
    // and no emissive armor; the shield-multiplier types still scale against a
    // pool of 0.
    Piece& b = pieces_[t];
    const detail::DamageRule rule = detail::damageRule(type);
    if (rule.shieldMultiply != 1 || rule.shieldDivide != 1) {
        ShieldState none;
        damage = detail::absorbShields(none, damage, rule);
    }
    if (damage <= 0) return;
    b.damaged = true;
    std::vector<size_t> entries(b.stacks.size());
    for (size_t k = 0; k < entries.size(); ++k) entries[k] = k;
    std::vector<int> killed;
    const int kills = detail::hitUnits(r_, s_, b.stacks, entries, b.pool, b.shieldPool, damage, type, rng_, killed);
    if (kills <= 0) return;
    for (size_t k = 0; k < b.stacks.size(); ++k) s_.design(b.stacks[k].design).lost += killed[k];   // units count as lost as they die
    syncGroup(b);
    b.unitsLost += kills;
    event(Ev::UnitsLost, t, att >= 0 ? att : t, kills);
    if (b.unit.count <= 0) kill(t, att);
    else afterDamage(t);
}

void Battle::seekerHit(int att, int t, DamageType type, int64_t damage) {
    // Spec 04 §10.1 (confirmed: binary): R hit points and a pool P for one member.
    const detail::DamageRule rule = detail::damageRule(type);
    if (!rule.structural || rule.shieldsOnly) return;   // a seeker has no shields to drain
    if (rule.shieldMultiply != 1 || rule.shieldDivide != 1) {
        ShieldState none;
        damage = detail::absorbShields(none, damage, rule);
    }
    Piece& b = pieces_[t];
    bool dies = false;
    if (rule.hullDamaging) {
        // The hit already carries the pool, which also still counts against the
        // member: it dies when D + 2 × P reaches R; otherwise the pool becomes P + D.
        dies = damage + 2 * b.pool >= b.hp;
        b.pool = dies ? 0 : b.pool + damage;
    } else {
        // Another type: a member dies when D reaches R − P; otherwise D joins the pool.
        dies = damage >= b.hp - b.pool;
        if (!dies) b.pool += damage;
    }
    if (!dies) return;
    if (--b.members <= 0) kill(t, att);
}

void Battle::planetHit(int att, int t, DamageType type, int64_t damage) {
    const detail::DamageRule rule = detail::damageRule(type);
    Piece& p = pieces_[t];
    if (isPlanetOnlyDamage(type)) {
        // Planet shields stop these too (confirmed: binary); then the effect.
        const int64_t rem = detail::absorbShields(p.sh, damage, detail::damageRule(DamageType::Normal));
        if (rem <= 0) return;
        p.damaged = true;
        switch (type) {
            case DamageType::PlagueLevel1:
            case DamageType::PlagueLevel2:
            case DamageType::PlagueLevel3:
            case DamageType::PlagueLevel4:
            case DamageType::PlagueLevel5:
                if (!r_.hasTrait(s_.empire(p.owner).race, "No Plagues"))
                    p.plague = std::max(p.plague, 1 + static_cast<int>(type) - static_cast<int>(DamageType::PlagueLevel1));
                return;
            case DamageType::OnlyPlanetPopulation: populationLoss(att, t, std::max<int64_t>(1, rem / cs_.damagePerPopulation)); return;
            case DamageType::OnlyPlanetConditions:
                // D × 0.1 on the 0-1.5 conditions scale (confirmed: binary); SpaceObject::conditions
                // holds hundredths of that scale (spec 02 §2), so the loss is D × 10.
                p.conditionsLost += rem * 10;
                return;
            case DamageType::OnlyResupplyDepots:
            case DamageType::OnlySpaceports: {
                // One intact facility of the first such stack in the planet's list (confirmed: binary).
                const AbilityKind k = type == DamageType::OnlySpaceports ? AbilityKind::Spaceport : AbilityKind::SupplyGeneration;
                for (size_t e = 0; e < p.facilities.size(); ++e) {
                    if (!hasAbility(r_.facilityAbilities(p.facilities[e]), k)) continue;
                    const uint32_t stack = p.facilities[e];
                    for (size_t f = 0; f < p.facilities.size(); ++f)
                        if (p.facilities[f] == stack && !p.facilityLost[f]) {
                            loseFacility(t, f);
                            return;
                        }
                }
                return;
            }
            default: return;
        }
    }
    // The other types against planets (confirmed: binary).
    switch (type) {
        case DamageType::OnlyWeapons:
        case DamageType::OnlyShieldGenerators:
        case DamageType::OnlyMasterComputers: return;   // nothing at all; Only Weapons does not hit weapon platforms
        case DamageType::ShieldsOnly:
        case DamageType::OnlyEngines:
        case DamageType::OnlyBoardingParties:
        case DamageType::OnlySecurityStations:
        case DamageType::OnlyPlanetDestroyers:
        case DamageType::PushesTarget:
        case DamageType::PullsTarget:
        case DamageType::RandomTargetMovement:
        {
            // They drain the shields and then do nothing (planets never move).
            const int before = p.sh.current;
            detail::absorbShields(p.sh, damage, detail::damageRule(DamageType::ShieldsOnly));
            if (p.sh.current != before) p.damaged = true;
            return;
        }
        default: break;
    }
    if (!rule.structural) return;
    // A hull-damaging hit: the planet keeps no pool, so shields take the hit alone (confirmed: binary).
    const int64_t d = detail::absorbShields(p.sh, damage, rule);
    if (d <= 0) return;
    p.damaged = true;
    // Weapon platforms take it first; when that kills the last of them and
    // other units remain, those take the same full hit again. Otherwise the
    // other stored units take it (spec 04 §11).
    auto anyUnits = [&](int platforms) {   // 1 platforms, 0 other units, -1 any
        for (const UnitStack& st : pieces_[t].unit.cargo.units) {
            if (st.count <= 0) continue;
            const bool platform = r_.hull(s_.design(st.design).hull).type == VehicleType::WeaponPlatform;
            if (platforms < 0 || platform == (platforms == 1)) return true;
        }
        return false;
    };
    if (anyUnits(1)) {
        cargoHit(t, type, d, true);
        if (!anyUnits(1) && anyUnits(0)) cargoHit(t, type, d, false);
    } else if (anyUnits(0)) {
        cargoHit(t, type, d, false);
    }
    if (!anyUnits(-1)) {
        // Only when no stored unit is left: the whole hit, however much the units took (confirmed: binary).
        populationLoss(att, t, std::max<int64_t>(1, d / cs_.damagePerPopulation));
        // Then the facilities: a roll of 1 to 3 is always made (confirmed: binary).
        const bool roll = rng_.rangeInt(1, 3) == 1;
        if (roll && pieces_[t].alive && !pieces_[t].colonyLost) facilityLoss(t);
    }
    afterDamage(t);
}

// The planet's stored units as one unit group (spec 04 §9.4): the platforms,
// or the other units. Units killed count as lost for their design; a planet
// credits and is credited no tonnage.
void Battle::cargoHit(int t, DamageType type, int64_t damage, bool platforms) {
    Piece& p = pieces_[t];
    std::vector<size_t> entries;
    for (size_t k = 0; k < p.unit.cargo.units.size(); ++k)
        if ((r_.hull(s_.design(p.unit.cargo.units[k].design).hull).type == VehicleType::WeaponPlatform) == platforms) entries.push_back(k);
    std::vector<int> killed;
    detail::hitUnits(r_, s_, p.unit.cargo.units, entries, p.pool, p.shieldPool, damage, type, rng_, killed);
    for (size_t k = 0; k < killed.size(); ++k)
        if (killed[k] > 0) s_.design(p.unit.cargo.units[k].design).lost += killed[k];
}

void Battle::populationLoss(int att, int t, int64_t millions) {
    Piece& p = pieces_[t];
    // From the first population group on (confirmed: binary).
    for (PopulationGroup& g : p.population) {
        if (millions <= 0) break;
        const int64_t n = std::min(millions, std::max<int64_t>(0, g.millions));
        g.millions -= n;
        millions -= n;
        p.popKilled += n;
    }
    int64_t left = 0;
    for (const PopulationGroup& g : p.population) left += g.millions;
    if (left > 0) return;
    // A colony at 0 population is lost; the planet stays as an unowned obstacle (confirmed: binary).
    p.colonyLost = true;
    note(std::format("the colony on {} was wiped out", p.name));
    event(Ev::Destroyed, t, att >= 0 ? att : t);
    if (att >= 0) gainExperience(att, kShipKillTenths);   // a planet: +1.0, no tonnage (spec 04 §15)
    if (p.isLeader) dissolve(t);
    p.kind = Kind::Obstacle;
    p.owner = {};
    p.weapons.clear();
    p.armed = p.guns = false;
}

int Battle::intactFacilities(const Piece& p) const {
    int n = 0;
    for (char lost : p.facilityLost) n += lost ? 0 : 1;
    return n;
}

void Battle::loseFacility(int t, size_t entry) {
    // A lost facility keeps working until the battle ends and is removed then (confirmed: binary).
    pieces_[t].facilityLost[entry] = 1;
}

void Battle::facilityLoss(int t) {
    // per = H0 ÷ n and allowed = (current hit points) ÷ per, truncated, with
    // allowed = 0 when per is 0; max(0, intact − allowed) facilities fall, each
    // from a facility stack drawn at random weighted by its size, destroyed ones
    // included; a stack with none intact is drawn again (confirmed: binary).
    Piece& p = pieces_[t];
    const int64_t n = static_cast<int64_t>(p.facilities.size());
    if (n == 0) return;
    const int64_t per = p.hpStart / n;
    const int64_t allowed = per > 0 ? planetHp(p) / per : 0;
    int64_t losses = std::max<int64_t>(0, intactFacilities(p) - allowed);
    // The stacks: one per facility kind, in the order of the planet's list.
    std::vector<uint32_t> kinds;
    for (uint32_t f : p.facilities)
        if (std::find(kinds.begin(), kinds.end(), f) == kinds.end()) kinds.push_back(f);
    while (losses-- > 0 && intactFacilities(p) > 0) {
        for (;;) {
            size_t pick = static_cast<size_t>(rng_.below(p.facilities.size()));   // weighted by stack size
            const uint32_t kind = p.facilities[pick];
            size_t victim = SIZE_MAX;
            for (size_t e = 0; e < p.facilities.size(); ++e)
                if (p.facilities[e] == kind && !p.facilityLost[e]) {
                    victim = e;
                    break;
                }
            if (victim == SIZE_MAX) continue;   // none intact in that stack: draw again
            loseFacility(t, victim);
            break;
        }
    }
}

void Battle::forcedMove(int t, int att, int64_t squares, bool push) {
    // Directly away from (toward) the firer, one square at a time; stops at a
    // taken square, the map edge, or (pull) next to the firer (confirmed: binary).
    const auto [ax, ay] = centreOf(att);
    int dx = sgn(pieces_[t].x - ax), dy = sgn(pieces_[t].y - ay);
    if (dx == 0 && dy == 0) std::tie(dx, dy) = kFacing[static_cast<size_t>(pieces_[att].facing)];   // same square: the firer's facing
    if (!push) {
        dx = -dx;
        dy = -dy;
    }
    for (int64_t k = 0; k < squares; ++k) {
        if (!push && dist(t, att) <= 1) break;
        const int nx = pieces_[t].x + dx, ny = pieces_[t].y + dy;
        if (!isFree(nx, ny, t)) break;
        moveTo(t, nx, ny);
    }
}

void Battle::randomMove(int t) {
    // A random free square at least 3 squares from the top and left edges, up to 100 tries (confirmed: binary).
    for (int attempt = 0; attempt < 100; ++attempt) {
        const int x = rng_.rangeInt(3, kW - 1), y = rng_.rangeInt(3, kH - 1);
        if (isFree(x, y, t)) {
            moveTo(t, x, y);
            return;
        }
    }
}

// Experience only for kills (spec 04 §15, confirmed: binary): +1.0 for a ship,
// base or planet, +0.1 for a whole unit group or a seeker; seekers credit their
// launcher (history 1.87). Unit groups and planets gain none.
void Battle::gainExperience(int k, int tenths) {
    if (k < 0) return;
    if (pieces_[k].kind == Kind::Seeker) k = pieces_[k].launcher;
    if (k < 0) return;
    Piece& killer = pieces_[k];
    if (killer.kind != Kind::Vehicle || !killer.alive) return;
    detail::addExperience(killer.unit.experience, killer.unit.experienceTenths, tenths);
    if (killer.fleet.valid() && !killer.captured && rng_.below(4) == 0) {
        auto& [whole, tenthsF] = fleetExp_[killer.fleet.value];
        detail::addExperience(whole, tenthsF, 1);   // the fleet's 1-in-4 chance of +0.1
    }
    for (size_t j = 0; j < pieces_.size(); ++j)
        if (pieces_[j].alive && pieces_[j].owner == killer.owner && pieces_[j].kind == Kind::Vehicle) refreshCombatValues(static_cast<int>(j));
}

// Enemy tonnage destroyed (spec 04 §15, confirmed: binary): the value goes in
// full to every design of the killing object: a ship's design, each design of
// a unit group, the launcher's design(s) for a seeker; a planet credits nobody.
void Battle::creditTonnage(int att, int64_t tonnage) {
    if (att < 0 || tonnage <= 0) return;
    int k = att;
    if (pieces_[k].kind == Kind::Seeker) k = pieces_[k].launcher;
    if (k < 0) return;
    const Piece& a = pieces_[k];
    std::set<uint32_t> designs;
    if (a.kind == Kind::Vehicle && a.unit.design.valid()) designs.insert(a.unit.design.value);
    else if (a.kind == Kind::UnitGroup)
        for (const UnitStack& st : a.stacks) designs.insert(st.design.value);
    for (uint32_t d : designs) s_.design(DesignId{d}).enemyTonnageDestroyed += tonnage;
}

void Battle::kill(int t, int att) {
    Piece& b = pieces_[t];
    if (!b.alive) return;
    b.alive = false;
    vacate(t);
    event(Ev::Destroyed, t, att >= 0 ? att : t);
    // The victim's value: a ship's or base's hull tonnage; a unit group's hull
    // tonnage times every unit it had in the battle, now that the whole group
    // is dead; nothing for a planet or a seeker.
    if (b.kind == Kind::Vehicle && b.unit.design.valid()) creditTonnage(att, designTonnage(r_, s_.design(b.unit.design)));
    else if (b.kind == Kind::UnitGroup) creditTonnage(att, b.tonnageHad);
    gainExperience(att, b.kind == Kind::Vehicle ? kShipKillTenths : kUnitKillTenths);
    if (b.isLeader) dissolve(t);
    if (b.kind != Kind::Seeker && b.kind != Kind::Planet) note(std::format("{} destroyed", label(t)));
}

void Battle::dissolve(int leader) {
    // The whole group: its members leave the fleet's group too (spec 03 §10).
    pieces_[leader].isLeader = false;
    for (Piece& p : pieces_)
        if (p.leader == leader) {
            p.leader = -1;
            p.hasSlot = false;
            p.fleetMember = false;
        }
}

void Battle::dissolveByStrategy(int leader) {
    if (logging(leader))
        for (size_t m = 0; m < pieces_.size(); ++m)
            if (pieces_[m].leader == leader && pieces_[m].alive) logOrder(TacticalOrder{TacticalOrder::Kind::ClearGroup, pieces_[leader].owner, static_cast<int>(m)});
    if (logging(leader)) logOrder(TacticalOrder{TacticalOrder::Kind::ClearGroup, pieces_[leader].owner, leader});
    dissolve(leader);
}

bool Battle::surrounded(int i) const {
    const Piece& p = pieces_[i];
    bool any = false;
    for (int y = p.y - 1; y <= p.y + p.size; ++y)
        for (int x = p.x - 1; x <= p.x + p.size; ++x) {
            if ((x >= p.x && x < p.x + p.size && y >= p.y && y < p.y + p.size) || !onMap(x, y)) continue;
            any = true;
            if (isFree(x, y, i)) return false;
        }
    return any;
}

void Battle::capture(int t, int capturer, bool boarding) {
    // The ship changes owner at once (confirmed: binary). Boarding also costs its
    // crew experience and adds the captured-ship reload; conversion does neither.
    // A capture changes no design statistic and gives no experience (spec 04 §15).
    Piece& b = pieces_[t];
    const EmpireId newOwner = pieces_[capturer].owner;
    b.owner = newOwner;
    b.unit.owner = newOwner;
    b.captured = true;
    if (boarding) {
        b.unit.experience = 0;   // (history 1.15)
        b.unit.experienceTenths = 0;
        for (Weapon& w : b.weapons)
            for (int& c : w.reload) c = std::min(kMaxReload, c + cs_.capturedReload);
    }
    b.fleet = {};
    b.unit.fleet = {};
    if (b.isLeader) dissolve(t);
    pieces_[t].leader = -1;
    pieces_[t].group = -1;
    pieces_[t].hasSlot = false;
    pieces_[t].fleetMember = false;
    pieces_[t].designStrategy = 0;   // (inferred) the captor's first strategy
    pieces_[t].fleetStrategy = 0;
    pieces_[t].droneTarget = -1;
    detail::refreshShields(r_, s_, pieces_[t].unit, shieldBonus(newOwner), disruption_, pieces_[t].sh, false);
    refreshStats(t);
    event(Ev::Captured, t, capturer, static_cast<int>(newOwner.value));
    note(std::format("{} {} by {}", pieces_[t].name, boarding ? "captured" : "converted", s_.empire(newOwner).name));
}

void Battle::convertPlanet(int t, int converter) {
    // The planet's piece fights for the converting empire for the rest of the
    // battle; the colony keeps its owner (confirmed: binary).
    Piece& p = pieces_[t];
    const EmpireId newOwner = pieces_[converter].owner;
    if (!newOwner.valid() || p.owner == newOwner) return;
    p.owner = newOwner;
    p.unit.owner = newOwner;
    if (p.isLeader) dissolve(t);
    refreshStats(t);
    event(Ev::Captured, t, converter, static_cast<int>(newOwner.value));
    note(std::format("{} converted by {}", p.name, s_.empire(newOwner).name));
}

void Battle::pdReact(int mover) {
    // Point-defense fires outside the budget, once per reload cycle, at categories in its set (confirmed: binary).
    auto shootPd = [&](int j, int target) {
        for (size_t wi = 0; wi < pieces_[j].weapons.size(); ++wi) {
            // Other weapons never react (checked first: the supply test below is costly).
            if (pieces_[j].weapons[wi].kind() != WeaponKind::PointDefense) continue;
            for (size_t k = 0;; ++k) {
                if (!combatant(target) || !combatant(j) || !hasSupply(j)) return;
                const Weapon& w = pieces_[j].weapons[wi];
                if (k >= static_cast<size_t>(instances(j, w))) break;
                if (w.reload[k] > 0) continue;
                if (!(w.targets & maskOf(target)) || weaponDamage(r_, w.de, dist(j, target)) <= 0 || !canAffect(w.type, target, j)) break;
                shoot(j, wi, k, target);
            }
        }
    };
    const Piece& m = pieces_[mover];
    const bool small = m.kind == Kind::Seeker || (m.kind == Kind::UnitGroup && (m.vtype == VehicleType::Fighter || m.vtype == VehicleType::Drone));
    if (small) {
        // After each step of a seeker, fighter or drone group, hostile point-defense in range may fire at it.
        for (size_t k = 0; k < pieces_.size() && combatant(mover); ++k) {
            const int j = static_cast<int>(k);
            const Piece& p = pieces_[k];
            if (!combatant(j) || j == mover || p.kind == Kind::Seeker || p.mothballed || !detail::enemies(s_, p.owner, pieces_[mover].owner))
                continue;
            shootPd(j, mover);
        }
        return;
    }
    // After a step of any other piece, its own point-defense fires at a hostile target now in range.
    if (m.mothballed || m.kind == Kind::Planet || m.kind == Kind::Obstacle) return;
    for (size_t k = 0; k < pieces_.size() && combatant(mover); ++k) {
        const int j = static_cast<int>(k);
        if (!combatant(j) || j == mover || !detail::enemies(s_, pieces_[mover].owner, pieces_[k].owner)) continue;
        shootPd(mover, j);
    }
}

void Battle::expire(int i) {
    pieces_[i].alive = false;
    event(Ev::Destroyed, i, i);
}

void Battle::moveSeekers(EmpireId e) {
    // An empire's seekers move in its phase, from the turn after launch, one square
    // at a time toward the target's centre square (confirmed: binary).
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const int i = static_cast<int>(k);
        if (pieces_[k].kind != Kind::Seeker || !pieces_[k].alive || pieces_[k].owner != e || pieces_[k].launchRound >= round_) continue;
        for (int stepNo = 0; stepNo < pieces_[k].speed && pieces_[k].alive; ++stepNo) {
            const int t = pieces_[k].seekTarget;
            // Expired: target gone or no longer hostile (history 1.04, 1.18).
            if (t < 0 || !combatant(t) || !detail::enemies(s_, e, pieces_[t].owner)) {
                expire(i);
                break;
            }
            const auto [tx, ty] = centreOf(t);
            pieces_[k].x += sgn(tx - pieces_[k].x);
            pieces_[k].y += sgn(ty - pieces_[k].y);
            ++pieces_[k].travelled;
            event(Ev::Move, i, i);
            // A gap in the table (at the travelled count, capped at 20) ends the flight.
            const int table = weaponDamage(r_, pieces_[k].seekWeapon.de, std::min(pieces_[k].travelled, 20));
            if (table <= 0) {
                expire(i);
                break;
            }
            pdReact(i);
            if (!pieces_[k].alive) break;
            if (pieces_[k].x == tx && pieces_[k].y == ty) {
                // Impact: the table value times the members, one hit, no roll (confirmed: binary).
                const Weapon w = pieces_[k].seekWeapon;
                const int64_t damage = xmath::pctRound(int64_t{table} * pieces_[k].members, 100 + damageBonus(e));
                event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, damage)), w.de.component);
                pieces_[k].alive = false;
                event(Ev::Destroyed, i, i);
                applyHit(i, t, w.type, damage);
                break;
            }
        }
    }
}

// A computer carrier's or planet's launches (spec 04 §10.4, §10.5, §10.7,
// confirmed: binary): never satellites or mines. Fighters in batches of the
// strategy's Fighters Launch Group Amount, each a new group from one cargo
// stack (0: all in one group). Drones one per group, in batches of Drones Per
// Target, at most Drones Per Target × the hostile ships and bases in the
// battle − the side's drones already alive (0: all of them). All within the
// per-turn launch rate.
void Battle::launchFrom(int i) {
    Piece& c = pieces_[i];
    if (!c.alive || c.mothballed || (c.kind != Kind::Vehicle && c.kind != Kind::Planet) || c.unit.cargo.units.empty()) return;
    std::array<int, 3> left = launchLeft(i);
    if (left[kLaunchFighters] + left[kLaunchDrones] <= 0) return;
    const EmpireId e = c.owner;
    const uint32_t sIndex = strategyIndex(i);
    const Strategy& S = strategy(e, sIndex);
    auto logLaunch = [&](DesignId design, int count) {
        if (!logging(i)) return;
        TacticalOrder o{TacticalOrder::Kind::Launch, e, i};
        o.design = design;
        o.count = count;
        o.group = static_cast<int>(rec_.events.size());   // a window of its own (a fresh group)
        logOrder(std::move(o));
    };
    for (size_t u = 0; u < pieces_[i].unit.cargo.units.size(); ++u) {
        const UnitStack st = pieces_[i].unit.cargo.units[u];
        if (st.count <= 0) continue;
        const VehicleType type = r_.hull(s_.design(st.design).hull).type;
        if (type == VehicleType::Fighter) {
            const int batch = S.fighterLaunchGroup > 0 ? S.fighterLaunchGroup : INT_MAX;
            while (left[kLaunchFighters] > 0 && pieces_[i].unit.cargo.units[u].count > 0) {
                const int n = std::min({batch, left[kLaunchFighters], pieces_[i].unit.cargo.units[u].count});
                if (spawnUnit(i, st.design, n, sIndex) < 0) break;
                logLaunch(st.design, n);
                pieces_[i].unit.cargo.units[u].count -= n;
                pieces_[i].launchedNow[kLaunchFighters] += n;
                left[kLaunchFighters] -= n;
            }
        } else if (type == VehicleType::Drone) {
            int limit = INT_MAX;
            if (S.dronesPerTarget > 0) {
                int hostile = 0, alive = 0;
                for (size_t k = 0; k < pieces_.size(); ++k) {
                    const Piece& q = pieces_[k];
                    if (!q.alive) continue;
                    if (q.kind == Kind::Vehicle && detail::enemies(s_, e, q.owner)) ++hostile;
                    if (q.kind == Kind::UnitGroup && q.vtype == VehicleType::Drone && q.owner == e) alive += q.unit.count;
                }
                limit = std::max(0, S.dronesPerTarget * hostile - alive);
            }
            const int batch = S.dronesPerTarget > 0 ? S.dronesPerTarget : INT_MAX;
            while (limit > 0 && left[kLaunchDrones] > 0 && pieces_[i].unit.cargo.units[u].count > 0) {
                const int n = std::min({batch, limit, left[kLaunchDrones], pieces_[i].unit.cargo.units[u].count});
                int launched = 0;
                for (; launched < n; ++launched)   // every drone is a group of its own
                    if (spawnUnit(i, st.design, 1, sIndex) < 0) break;
                if (launched <= 0) break;
                logLaunch(st.design, launched);
                pieces_[i].unit.cargo.units[u].count -= launched;
                pieces_[i].launchedNow[kLaunchDrones] += launched;
                left[kLaunchDrones] -= launched;
                limit -= launched;
                if (launched < n) break;
            }
        }
    }
    refreshStats(i);
}

int Battle::spawnUnit(int carrier, DesignId design, int count, uint32_t strategyIndex) {
    const auto [cx, cy] = centreOf(carrier);
    const auto [x, y] = settle(cx, cy, 1, -1);
    if (x < 0) return -1;
    const Design& d = s_.design(design);
    Piece u;
    u.kind = Kind::UnitGroup;
    u.owner = u.startOwner = pieces_[carrier].owner;
    u.unit.owner = u.owner;
    u.unit.design = design;
    u.unit.count = count;
    u.stacks = {UnitStack{design, count}};
    u.unit.location = where_;
    u.unit.damage.assign(d.entries.size(), 0);
    int64_t supply = detail::hullSum(r_, d, AbilityKind::SupplyStorage);
    for (const DesignEntry& e : d.entries) supply += sumValue1(r_.componentAbilities(e.component), AbilityKind::SupplyStorage);
    u.unit.supply = supply;   // per unit; new groups start full
    u.vtype = r_.hull(d.hull).type;
    u.name = d.name;
    u.x = x;
    u.y = y;
    u.facing = pieces_[carrier].facing;
    u.carrier = carrier;
    u.launched = true;
    u.designStrategy = strategyIndex;
    u.startCount = count;
    u.hpStart = groupHitPoints(u);
    u.tonnageHad = designTonnage(r_, d) * count;
    buildWeapons(u);
    const int idx = addPiece(std::move(u));
    occupy(idx);
    refreshPiece(idx);   // launched units get their full movement at once (history 1.55, 1.71)
    event(Ev::Launch, idx, carrier, count);
    return idx;
}

// More units join a group launched earlier in the same Launch Units window
// (spec 04 §10.4), whatever their design.
void Battle::joinUnit(int group, DesignId design, int count) {
    Piece& g = pieces_[group];
    const std::vector<Weapon> old = g.weapons;
    auto st = std::find_if(g.stacks.begin(), g.stacks.end(), [&](const UnitStack& x) { return x.design == design; });
    if (st != g.stacks.end()) st->count += count;
    else g.stacks.push_back({design, count});
    syncGroup(g);
    if (g.vtype != VehicleType::Satellite) {
        // Supply is kept per unit: a group that mixes designs keeps the smallest full load (inferred).
        const Design& d = s_.design(design);
        int64_t supply = detail::hullSum(r_, d, AbilityKind::SupplyStorage);
        for (const DesignEntry& e : d.entries) supply += sumValue1(r_.componentAbilities(e.component), AbilityKind::SupplyStorage);
        g.unit.supply = std::min(g.unit.supply, supply);
    }
    buildWeapons(g);
    // The weapons already there keep their reload counters; the new units' are ready.
    for (Weapon& w : g.weapons)
        for (const Weapon& o : old)
            if (o.stack == w.stack && o.entry == w.entry && o.de == w.de)
                for (size_t k = 0; k < w.reload.size() && k < o.reload.size(); ++k) w.reload[k] = o.reload[k];
    g.startCount += count;
    g.hpStart = groupHitPoints(g);
    g.tonnageHad += designTonnage(r_, s_.design(design)) * count;
    rec_.pieces[static_cast<size_t>(group)].count += count;
    refreshStats(group);
    event(Ev::Launch, group, g.carrier, count);
}

// ---- Movement (spec 04 §5, §16.1) ------------------------------------------------------------------------

void Battle::moveTo(int i, int x, int y) {
    vacate(i);
    if (x != pieces_[i].x || y != pieces_[i].y) pieces_[i].facing = facingOf(x - pieces_[i].x, y - pieces_[i].y);
    pieces_[i].x = x;
    pieces_[i].y = y;
    occupy(i);
    event(Ev::Move, i, i);
}

void Battle::step(int i, int x, int y) {
    moveTo(i, x, y);
    --pieces_[i].mp;
    pdReact(i);   // point-defense on the move (confirmed: binary)
}

void Battle::walk(int i, const std::vector<std::pair<int, int>>& path) {
    for (const auto& [x, y] : path) {
        if (!pieces_[i].alive || pieces_[i].mp <= 0 || !isFree(x, y, i)) return;
        step(i, x, y);
    }
}

// Spec 04 §16.1: the primary strategy unless it is impossible, else the
// secondary tested the same way, else Don't Get Hurt (a secondary Ram never
// falls back). An Optimal ship with no weapons at all rams.
MoveStrategy Battle::strategyInEffect(int i) {
    const Piece& p = pieces_[i];
    const Strategy& S = strategyOf(i);
    if (p.kind == Kind::Vehicle && S.primary == MoveStrategy::OptimalRange && !p.armed) return MoveStrategy::Ram;
    auto possible = [&](MoveStrategy m) {
        switch (m) {
            case MoveStrategy::DontGetHurt: return true;
            case MoveStrategy::DropTroops: return p.kind == Kind::Vehicle && hasTroops(i);
            case MoveStrategy::BoardEnemyShips:
                return p.kind == Kind::Vehicle && detail::componentSum(r_, s_, p.unit, AbilityKind::BoardingAttack) > 0;
            case MoveStrategy::Ram:
                if (p.kind == Kind::UnitGroup && p.vtype == VehicleType::Drone) return p.droneTarget >= 0 && combatant(p.droneTarget);
                return true;
            default: return p.guns;   // the range strategies need a weapon other than point-defense and warheads
        }
    };
    if (possible(S.primary)) return S.primary;
    if (possible(S.secondary) || S.secondary == MoveStrategy::Ram) return S.secondary;
    return MoveStrategy::DontGetHurt;
}

bool Battle::leavesFormation(int i) {
    // Its strategy in effect, or its category's Break Formation flag (spec 04 §16.1).
    const MoveStrategy m = strategyInEffect(i);
    if (m == MoveStrategy::DontGetHurt || m == MoveStrategy::DropTroops || m == MoveStrategy::BoardEnemyShips || m == MoveStrategy::Ram)
        return true;
    return strategyOf(i).breakFormation[static_cast<size_t>(pieces_[i].category)];
}

std::pair<MoveStrategy, int> Battle::chooseMode(int i, const Strategy& S) {
    const MoveStrategy m = strategyInEffect(i);
    switch (m) {
        case MoveStrategy::DontGetHurt: return {m, -1};
        case MoveStrategy::DropTroops: return {m, troopTarget(i)};
        case MoveStrategy::BoardEnemyShips: return {m, boardTarget(i)};
        case MoveStrategy::Ram:
            for (int t : sortedTargets(i, S))
                if (pieces_[t].kind != Kind::Seeker) return {m, t};
            return {m, -1};
        default: {
            // The range strategies' target: the first one an intact weapon other than point-defense can engage.
            uint8_t reach = 0;
            for (const Weapon& w : pieces_[i].weapons)
                if (w.kind() != WeaponKind::PointDefense && instances(i, w) > 0) reach |= w.targets;
            if (reach)
                for (int t : sortedTargets(i, S))
                    if (maskOf(t) & reach) return {m, t};
            return {m, -1};
        }
    }
}

// Spec 04 §16.1: the danger map, built once as a side's movement begins.
void Battle::buildDanger(EmpireId e) {
    danger_.assign(static_cast<size_t>(kW * kH), 0);
    dangerFor_ = e;
    auto add = [&](int x, int y, int64_t v) { danger_[static_cast<size_t>(y * kW + x)] += v; };
    // A ring of squares at distance d, cut at the map's border: rows and columns
    // beyond the border collapse onto it, so border squares are counted again for
    // every larger ring (confirmed: binary).
    auto ring = [&](int cx, int cy, int d, int64_t v) {
        if (v == 0) return;
        const int x0 = std::clamp(cx - d, 0, kW - 1), x1 = std::clamp(cx + d, 0, kW - 1);
        const int y0 = std::clamp(cy - d, 0, kH - 1), y1 = std::clamp(cy + d, 0, kH - 1);
        if (d == 0) {
            add(x0, y0, v);
            return;
        }
        for (int x = x0; x <= x1; ++x) {
            add(x, y0, v);
            if (y1 != y0) add(x, y1, v);
        }
        for (int y = y0 + 1; y < y1; ++y) {
            add(x0, y, v);
            if (x1 != x0) add(x1, y, v);
        }
    };
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const int h = static_cast<int>(k);
        const Piece& p = pieces_[k];
        if (!p.alive) continue;
        // Neutral obstacles count as hostile here.
        if (p.kind != Kind::Obstacle && (!p.owner.valid() || p.owner == e || !detail::enemies(s_, e, p.owner))) continue;
        const int cx = std::clamp(p.x, 0, kW - 1), cy = std::clamp(p.y, 0, kH - 1);
        add(cx, cy, kDangerOwnSquare);
        if (p.kind == Kind::Obstacle || p.kind == Kind::Seeker || p.mothballed) continue;
        const int M = computeMp(h);
        // Each ready intact weapon other than point-defense (seeking weapons and
        // warheads alike) adds its damage at range max(1, d − M) to the squares at
        // distance d, out to its longest range + M. An enemy ship's weapons that
        // cannot target ships are left out.
        std::vector<int64_t> rings;
        auto addWeapon = [&](const DesignEntry& de, uint8_t targets, int64_t count) {
            if (count <= 0 || (p.kind == Kind::Vehicle && !(targets & kTargetShips))) return;
            const int reach = weaponReach(r_, de);
            if (static_cast<int>(rings.size()) < reach + M + 1) rings.resize(static_cast<size_t>(reach + M + 1), 0);
            for (int d = 0; d <= reach + M; ++d) rings[static_cast<size_t>(d)] += int64_t{weaponDamage(r_, de, std::max(1, d - M))} * count;
        };
        for (const Weapon& w : p.weapons) {
            if (w.kind() == WeaponKind::PointDefense) continue;
            int ready = 0;
            for (int n = 0; n < instances(h, w); ++n) ready += w.reload[static_cast<size_t>(n)] == 0 ? 1 : 0;
            addWeapon(w.de, w.targets, int64_t{ready} * firedTogether(h, w));
        }
        if (p.kind == Kind::Vehicle || p.kind == Kind::UnitGroup) {
            const std::vector<UnitStack> designs = p.kind == Kind::UnitGroup ? p.stacks : std::vector<UnitStack>{{p.unit.design, 1}};
            for (const UnitStack& st : designs) {
                if (st.count <= 0) continue;
                const Design& d = s_.design(st.design);
                for (size_t n = 0; n < d.entries.size(); ++n) {
                    const ruleset::Component& c = r_.component(d.entries[n].component);
                    if (c.weapon.kind != WeaponKind::Warhead || (p.kind == Kind::Vehicle && !entryIntact(r_, s_, p.unit, n))) continue;
                    addWeapon(d.entries[n], parseWeaponTargets(c.weapon.targets), st.count);
                }
            }
        }
        for (size_t d = 0; d < rings.size(); ++d) ring(cx, cy, static_cast<int>(d), rings[d]);
        // An enemy ship with total Boarding Attack B adds B ÷ 2 + 1 to every square within its movement.
        if (p.kind == Kind::Vehicle) {
            const int64_t b = detail::componentSum(r_, s_, p.unit, AbilityKind::BoardingAttack);
            if (b > 0)
                for (int y = std::max(0, cy - M); y <= std::min(kH - 1, cy + M); ++y)
                    for (int x = std::max(0, cx - M); x <= std::min(kW - 1, cx + M); ++x) add(x, y, b / 2 + 1);
        }
    }
}

// The danger map for one mover: the side's map plus, for each enemy seeker
// group aimed at it, its weapon's damage at range d on the squares at
// distance d from the seeker, out to the last non-zero entry, once per group
// whatever its size and however far it has flown (confirmed: binary).
std::vector<int64_t> Battle::dangerFor(int i) const {
    std::vector<int64_t> map = danger_;
    if (map.size() != static_cast<size_t>(kW * kH)) map.assign(static_cast<size_t>(kW * kH), 0);
    for (const Piece& sk : pieces_) {
        if (!sk.alive || sk.kind != Kind::Seeker || sk.seekTarget != i || !detail::enemies(s_, sk.owner, pieces_[i].owner)) continue;
        int last = 0;
        for (int d = 1; d <= 20; ++d)
            if (weaponDamage(r_, sk.seekWeapon.de, d) > 0) last = d;
        const int cx = std::clamp(sk.x, 0, kW - 1), cy = std::clamp(sk.y, 0, kH - 1);
        for (int d = 0; d <= last; ++d) {
            const int64_t v = weaponDamage(r_, sk.seekWeapon.de, std::max(1, d));
            if (v <= 0) continue;
            const int x0 = std::clamp(cx - d, 0, kW - 1), x1 = std::clamp(cx + d, 0, kW - 1);
            const int y0 = std::clamp(cy - d, 0, kH - 1), y1 = std::clamp(cy + d, 0, kH - 1);
            auto add = [&](int x, int y) { map[static_cast<size_t>(y * kW + x)] += v; };
            if (d == 0) {
                add(x0, y0);
                continue;
            }
            for (int x = x0; x <= x1; ++x) {
                add(x, y0);
                if (y1 != y0) add(x, y1);
            }
            for (int y = y0 + 1; y < y1; ++y) {
                add(x0, y);
                if (x1 != x0) add(x1, y);
            }
        }
    }
    return map;
}

// Spec 04 §16.1: the attack map. For each ready weapon that has a target it
// can hit, the damage it would deal from each square, measured from the
// target's top-left square: in full, a fifth for Shields Only, Only Engines and
// Only Master Computers, nothing for push, pull and teleport; against a ship
// whose shields are no more than its regeneration, values below its emissive
// armor count 0 (unless the mover is a fighter or satellite group).
std::vector<int64_t> Battle::attackMap(int i, const std::vector<int>& targets) {
    std::vector<int64_t> map(static_cast<size_t>(kW * kH), 0);
    const Piece& a = pieces_[i];
    for (const Weapon& w : a.weapons) {
        if (w.kind() == WeaponKind::PointDefense) continue;
        int ready = 0;
        for (int n = 0; n < instances(i, w); ++n) ready += w.reload[static_cast<size_t>(n)] == 0 ? 1 : 0;
        if (ready <= 0) continue;
        if (w.type == DamageType::PushesTarget || w.type == DamageType::PullsTarget || w.type == DamageType::RandomTargetMovement) continue;
        const int t = planTarget(i, w, targets);
        if (t < 0) continue;
        const Piece& b = pieces_[t];
        const bool fifth = w.type == DamageType::ShieldsOnly || w.type == DamageType::OnlyEngines || w.type == DamageType::OnlyMasterComputers;
        int64_t emissive = 0;
        if (b.kind == Kind::Vehicle && !(a.kind == Kind::UnitGroup && (a.vtype == VehicleType::Fighter || a.vtype == VehicleType::Satellite))) {
            const Design& d = s_.design(b.unit.design);
            const int64_t regen = detail::componentSum(r_, s_, b.unit, AbilityKind::ShieldRegeneration) + detail::hullSum(r_, d, AbilityKind::ShieldRegeneration);
            if (b.sh.current <= regen)
                emissive = std::max(detail::componentBest(r_, s_, b.unit, AbilityKind::EmissiveArmor), bestValue1(r_.hullAbilities(d.hull), AbilityKind::EmissiveArmor));
        }
        const int64_t n = int64_t{ready} * firedTogether(i, w);
        std::array<int64_t, kRangeTable + 1> byRange{};
        for (int d = 1; d <= kRangeTable; ++d) {
            int64_t v = weaponDamage(r_, w.de, d);
            if (v < emissive) v = 0;
            v *= n;
            if (fifth) v /= 5;
            byRange[static_cast<size_t>(d)] = v;
        }
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const int d = cheb(x, y, b.x, b.y);
                if (d >= 1 && d <= kRangeTable) map[static_cast<size_t>(y * kW + x)] += byRange[static_cast<size_t>(d)];
            }
    }
    return map;
}

// Spec 04 §16.1 "Choosing the square" for the range strategies. Returns the
// destination; (-1, -1) means Don't Get Hurt.
std::pair<int, int> Battle::rangeSquare(int i, MoveStrategy m, int t, const std::vector<int64_t>& danger, const std::vector<int64_t>& attack) {
    const Piece& p = pieces_[i];
    const int px = p.x, py = p.y;
    auto at = [](const std::vector<int64_t>& map, int x, int y) { return map[static_cast<size_t>(y * kW + x)]; };
    // Never a border row or column, nor a square with a piece on it (inferred: its own square may be kept).
    auto usable = [&](int x, int y) {
        if (x <= 0 || y <= 0 || x >= kW - 1 || y >= kH - 1) return false;
        const int o = occupant(x, y);
        return o < 0 || o == i;
    };
    auto toTarget = [&](int x, int y) { return t >= 0 ? cheb(x, y, pieces_[t].x, pieces_[t].y) : 0; };
    auto dangerNear = [&] {
        for (int y = std::max(0, py - 10); y <= std::min(kH - 1, py + 10); ++y)
            for (int x = std::max(0, px - 10); x <= std::min(kW - 1, px + 10); ++x)
                if (at(danger, x, y) > 0) return true;
        return false;
    };
    // The scan runs column by column; a full tie replaces the choice with a
    // 1-in-10 chance as the scan goes on (confirmed: binary; the scan order is inferred).
    struct Best {
        bool found = false;
        int x = -1, y = -1;
        std::array<int64_t, 3> key{};   // smaller is better
    };
    Rng& dice = planRng_[p.owner.value];
    auto scan = [&](auto&& keyOf, auto&& allowed, int tieOdds) {
        Best best;
        for (int x = 0; x < kW; ++x)
            for (int y = 0; y < kH; ++y) {
                if (!usable(x, y) || !allowed(x, y)) continue;
                const std::array<int64_t, 3> key = keyOf(x, y);
                if (!best.found || key < best.key) {
                    best = Best{true, x, y, key};
                } else if (key == best.key && dice.below(static_cast<uint64_t>(tieOdds)) == 0) {
                    best.x = x;
                    best.y = y;
                }
            }
        return best;
    };
    auto any = [](int, int) { return true; };
    auto fallback = [&]() -> std::pair<int, int> {
        // It can deal damage nowhere: stay put if there is danger within 10 squares
        // and it has no target; otherwise the least-danger square, the one farthest
        // from its target (without a target, the one nearest to itself).
        if (t < 0 && dangerNear()) return {px, py};
        const Best b = scan([&](int x, int y) -> std::array<int64_t, 3> {
            return {at(danger, x, y), t >= 0 ? -int64_t{toTarget(x, y)} : int64_t{cheb(x, y, px, py)}, 0};
        }, any, 10);
        return b.found ? std::pair{b.x, b.y} : std::pair{px, py};
    };
    auto optimal = [&]() -> std::pair<int, int> {
        // A square with no danger where it can deal damage: the most such damage, nearest its target.
        Best b = scan([&](int x, int y) -> std::array<int64_t, 3> { return {-at(attack, x, y), toTarget(x, y), 0}; },
                      [&](int x, int y) { return at(danger, x, y) == 0 && at(attack, x, y) > 0; }, 10);
        if (b.found) return {b.x, b.y};
        // The lowest 1000 × danger ÷ damage, nearest its target.
        b = scan([&](int x, int y) -> std::array<int64_t, 3> { return {1000 * at(danger, x, y) / at(attack, x, y), toTarget(x, y), 0}; },
                 [&](int x, int y) { return at(attack, x, y) > 0; }, 10);
        if (b.found) return {b.x, b.y};
        return fallback();
    };
    switch (m) {
        case MoveStrategy::OptimalRange: return optimal();
        case MoveStrategy::ShortRange: {
            // The most damage, ties to less danger; no 1-to-3-squares limit.
            const Best b = scan([&](int x, int y) -> std::array<int64_t, 3> { return {-at(attack, x, y), at(danger, x, y), 0}; },
                                [&](int x, int y) { return at(attack, x, y) > 0; }, 10);
            if (b.found) return {b.x, b.y};
            return fallback();
        }
        case MoveStrategy::PointBlank: {
            if (t < 0) return optimal();
            // The square next to the target's top-left square, one step toward the mover.
            const Piece& q = pieces_[t];
            return {q.x + sgn(px - q.x), q.y + sgn(py - q.y)};
        }
        case MoveStrategy::MaximumRange: {
            if (t < 0) return dangerNear() ? std::pair{px, py} : std::pair{-1, -1};
            bool ready = false;
            for (const Weapon& w : p.weapons) {
                if (w.kind() == WeaponKind::PointDefense) continue;
                for (int n = 0; n < instances(i, w); ++n) ready = ready || w.reload[static_cast<size_t>(n)] == 0;
            }
            if (!ready || at(attack, px, py) > 0) {
                // The ring at (the target's longest weapon range + its movement + 2)
                // from its top-left square: the least-danger square of it within
                // reach, else its nearest square, else Don't Get Hurt.
                const Piece& q = pieces_[t];
                int longest = 0;
                for (const Weapon& w : q.weapons)
                    if (w.kind() != WeaponKind::PointDefense) longest = std::max(longest, w.reach);
                const int radius = longest + computeMp(t) + 2;
                auto onRing = [&](int x, int y) { return cheb(x, y, q.x, q.y) == radius; };
                Best b = scan([&](int x, int y) -> std::array<int64_t, 3> { return {at(danger, x, y), 0, 0}; },
                              [&](int x, int y) { return onRing(x, y) && cheb(x, y, px, py) <= p.mp; }, 10);
                if (b.found) return {b.x, b.y};
                b = scan([&](int x, int y) -> std::array<int64_t, 3> { return {cheb(x, y, px, py), 0, 0}; }, onRing, 10);
                if (b.found) return {b.x, b.y};
                return {-1, -1};
            }
            // Where it can deal damage: within its movement, the farthest from the
            // target, else the farthest overall; ties nearer to itself, then a coin flip.
            Best b = scan([&](int x, int y) -> std::array<int64_t, 3> { return {-int64_t{toTarget(x, y)}, cheb(x, y, px, py), 0}; },
                          [&](int x, int y) { return at(attack, x, y) > 0 && cheb(x, y, px, py) <= p.mp; }, 2);
            if (b.found) return {b.x, b.y};
            b = scan([&](int x, int y) -> std::array<int64_t, 3> { return {-int64_t{toTarget(x, y)}, cheb(x, y, px, py), 0}; },
                     [&](int x, int y) { return at(attack, x, y) > 0; }, 2);
            if (b.found) return {b.x, b.y};
            return fallback();
        }
        default: return {-1, -1};
    }
}

// Spec 04 §16.1 Don't Get Hurt (confirmed: binary): only where pieces stand
// counts. Every square within its movement on both axes is a candidate; its
// score adds, for each square holding hostile pieces (obstacles and hostile
// seekers included), 10 × their number × the straight-line distance, and for
// each square holding only own pieces (the mover and own seekers included),
// 3 × their number × the straight-line distance, each truncated. The highest
// score wins, ties to the smallest column, then the smallest row. When the
// scan reaches a square holding hostile pieces, that square's own score so
// far is reset to 0.
std::pair<int, int> Battle::dontGetHurtSquare(int i) const {
    const Piece& p = pieces_[i];
    struct Holders {
        int hostile = 0, own = 0;
    };
    std::map<std::pair<int, int>, Holders> squares;   // by (column, row): the scan order
    for (const Piece& q : pieces_) {
        if (!q.alive) continue;
        const bool hostile = q.kind == Kind::Obstacle || (q.owner.valid() && q.owner != p.owner && detail::enemies(s_, p.owner, q.owner));
        if (hostile) ++squares[{q.x, q.y}].hostile;
        else if (q.owner == p.owner) ++squares[{q.x, q.y}].own;
    }
    const int reach = std::max(0, p.mp);
    int bestX = p.x, bestY = p.y;
    int64_t bestScore = -1;
    for (int x = std::max(0, p.x - reach); x <= std::min(kW - 1, p.x + reach); ++x)
        for (int y = std::max(0, p.y - reach); y <= std::min(kH - 1, p.y + reach); ++y) {
            int64_t score = 0;
            for (const auto& [sq, h] : squares) {
                const int dx = sq.first - x, dy = sq.second - y;
                if (h.hostile > 0) {
                    if (dx == 0 && dy == 0) score = 0;
                    score += weightedDistance(int64_t{10} * h.hostile, dx, dy);
                } else if (h.own > 0) {
                    score += weightedDistance(int64_t{3} * h.own, dx, dy);
                }
            }
            if (score > bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
            }
        }
    return {bestX, bestY};
}

MovePlan Battle::plan(int i) {
    MovePlan mv;
    const Strategy& S = strategyOf(i);
    std::tie(mv.mode, mv.target) = chooseMode(i, S);
    if (pieces_[i].mp <= 0) return mv;
    std::pair<int, int> dest{-1, -1};
    switch (mv.mode) {
        case MoveStrategy::DontGetHurt: break;
        case MoveStrategy::DropTroops: {
            if (mv.target < 0) break;
            // While its planet still has guns and the side has an armed escort (a
            // piece with guns and a range strategy in effect), the carrier waits;
            // otherwise it goes to the planet's Point Blank square and lands.
            bool escort = false;
            if (pieces_[mv.target].guns)
                for (size_t k = 0; k < pieces_.size() && !escort; ++k)
                    if (pieces_[k].alive && pieces_[k].owner == pieces_[i].owner && static_cast<int>(k) != i && pieces_[k].guns &&
                        pieces_[k].kind != Kind::Planet && isRangeStrategy(strategyInEffect(static_cast<int>(k))))
                        escort = true;
            if (escort) break;
            const Piece& q = pieces_[mv.target];
            dest = {q.x + sgn(pieces_[i].x - q.x), q.y + sgn(pieces_[i].y - q.y)};
            break;
        }
        case MoveStrategy::BoardEnemyShips:
        case MoveStrategy::Ram:
            if (mv.target < 0) break;
            mv.path = pathToward(i, mv.target);
            return mv;
        default: {
            const std::vector<int> targets = sortedTargets(i, S);
            if (dangerFor_ != pieces_[i].owner) buildDanger(pieces_[i].owner);   // (inferred) a piece acting on its own
            dest = rangeSquare(i, mv.mode, mv.target, dangerFor(i), attackMap(i, targets));
            break;
        }
    }
    // Don't Get Hurt: the fallback of Drop Troops, Maximum Weapons Range, Board and Ram.
    if (dest.first < 0) dest = dontGetHurtSquare(i);
    if (dest != std::pair{pieces_[i].x, pieces_[i].y}) mv.path = pathToSquare(i, dest.first, dest.second);
    return mv;
}

// Rams, boarding and drones close on their target: greedy steps to a square
// next to its footprint (inferred).
std::vector<std::pair<int, int>> Battle::pathToward(int i, int t) const {
    std::vector<std::pair<int, int>> path;
    int x = pieces_[i].x, y = pieces_[i].y;
    for (int left = pieces_[i].mp; left > 0; --left) {
        const int cur = std::max(gap(x, 1, pieces_[t].x, pieces_[t].size), gap(y, 1, pieces_[t].y, pieces_[t].size));
        if (cur <= 1) break;
        int bx = x, by = y, best = cur;
        for (const auto& [dx, dy] : kDirs) {
            const int nx = x + dx, ny = y + dy;
            if (!isFree(nx, ny, i) || std::find(path.begin(), path.end(), std::pair{nx, ny}) != path.end()) continue;
            const int d = std::max(gap(nx, 1, pieces_[t].x, pieces_[t].size), gap(ny, 1, pieces_[t].y, pieces_[t].size));
            if (d < best) {
                best = d;
                bx = nx;
                by = ny;
            }
        }
        if (bx == x && by == y) break;
        x = bx;
        y = by;
        path.emplace_back(x, y);
    }
    return path;
}

// Steps toward a square: when the next square is taken, the piece tries the
// other squares around it that bring it closer, then stops (spec 04 §5).
std::vector<std::pair<int, int>> Battle::pathToSquare(int i, int tx, int ty) const {
    std::vector<std::pair<int, int>> path;
    int x = pieces_[i].x, y = pieces_[i].y;
    for (int left = pieces_[i].mp; left > 0; --left) {
        const int cur = std::max(std::abs(x - tx), std::abs(y - ty));
        if (cur == 0) break;
        int bx = x, by = y, best = cur;
        for (const auto& [dx, dy] : kDirs) {
            const int nx = x + dx, ny = y + dy;
            if (!isFree(nx, ny, i)) continue;
            const int d = std::max(std::abs(nx - tx), std::abs(ny - ty));
            if (d < best) {
                best = d;
                bx = nx;
                by = ny;
            }
        }
        if (bx == x && by == y) break;
        x = bx;
        y = by;
        path.emplace_back(x, y);
    }
    return path;
}

void Battle::followLeader(int i, bool logMoves) {
    // Members move toward their formation slot around the leader's square, turned
    // by the leader's facing, with their own movement points (spec 04 §5).
    const int l = leaderOf(i);
    if (l < 0 || !pieces_[i].hasSlot) return;
    const Piece& leader = pieces_[l];
    const auto [dx, dy] = rotateSlot(leader.facing, pieces_[i].slotDx, pieces_[i].slotDy);
    const int tx = std::clamp(leader.x + dx, 0, kW - 1);
    const int ty = std::clamp(leader.y + dy, 0, kH - 1);
    const std::vector<std::pair<int, int>> path = pathToSquare(i, tx, ty);
    if (logMoves) logMove(i, path);
    walk(i, path);
}

void Battle::logMove(int i, const std::vector<std::pair<int, int>>& path) {
    if (!logging(i) || path.empty()) return;
    TacticalOrder o{TacticalOrder::Kind::Move, pieces_[i].owner, i};
    o.alone = true;
    for (const auto& [x, y] : path) o.path.push_back(Square{static_cast<int16_t>(x), static_cast<int16_t>(y)});
    logOrder(std::move(o));
}

void Battle::droneAct(int i) {
    // Drones pick their own ship, planet or satellite targets and ram them (spec 04 §10.7).
    int t = pieces_[i].droneTarget;
    const bool valid = t >= 0 && combatant(t) && pieces_[t].owner != pieces_[i].owner && detail::enemies(s_, pieces_[i].owner, pieces_[t].owner) &&
                       (maskOf(t) & kDroneTargets) && pieces_[t].owner == pieces_[i].droneTargetOwner;
    if (!valid) {
        // A temporary target until the battle ends; one that changed owner is dropped (confirmed: binary).
        t = -1;
        for (int c : sortedTargets(i, strategyOf(i)))
            if (maskOf(c) & kDroneTargets) {
                t = c;
                break;
            }
        pieces_[i].droneTarget = t;
        if (t >= 0) pieces_[i].droneTargetOwner = pieces_[t].owner;
    }
    if (t < 0) {
        fire(i);
        return;
    }
    walk(i, pathToward(i, t));
    if (combatant(i) && combatant(t) && dist(i, t) <= 1 && pieces_[i].mp > 0) ram(i, t);
    if (combatant(i)) fire(i);   // drones also fire any weapons they carry
    if (combatant(i)) pieces_[i].mp = 0;
}

int Battle::boardTarget(int i) const {
    // Enemy ships and bases, those with shields down first, then the nearest.
    int best = -1;
    std::tuple<int, int, int> bestKey{};
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& b = pieces_[k];
        const int j = static_cast<int>(k);
        if (!b.alive || b.kind != Kind::Vehicle || !detail::enemies(s_, pieces_[i].owner, b.owner)) continue;
        const std::tuple<int, int, int> key{b.sh.current > 0 ? 1 : 0, dist(i, j), j};
        if (best < 0 || key < bestKey) {
            best = j;
            bestKey = key;
        }
    }
    return best;
}

int Battle::troopTarget(int i) const {
    const Piece& a = pieces_[i];
    auto usable = [&](int j) {
        const Piece& b = pieces_[j];
        return b.alive && b.kind == Kind::Planet && detail::enemies(s_, a.owner, b.owner) && !contestedBy(b, a.owner);
    };
    // A hostile colony named by one of the ship's own Attack orders, else the most populous (confirmed: binary).
    for (const Order& o : a.unit.orders)
        if (o.kind == OrderKind::Attack && o.object.valid())
            for (size_t k = 0; k < pieces_.size(); ++k)
                if (pieces_[k].kind == Kind::Planet && pieces_[k].object == o.object && usable(static_cast<int>(k))) return static_cast<int>(k);
    int best = -1;
    int64_t bestPop = -1;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        if (!usable(static_cast<int>(k))) continue;
        int64_t pop = 0;
        for (const PopulationGroup& g : pieces_[k].population) pop += g.millions;
        if (pop > bestPop) {
            bestPop = pop;
            best = static_cast<int>(k);
        }
    }
    return best;
}

void Battle::board(int i, int t) {
    // Capture Ship: both ships or bases, adjacent, the target's shields at 0 (confirmed: binary).
    const Piece& b = pieces_[t];
    if (b.kind != Kind::Vehicle || !b.alive || pieces_[i].kind != Kind::Vehicle || dist(i, t) > 1 || b.sh.current > 0) return;
    const int64_t offense = detail::componentSum(r_, s_, pieces_[i].unit, AbilityKind::BoardingAttack);
    if (offense <= 0) return;
    int crew = 0;
    {
        const Design& d = s_.design(b.unit.design);
        for (size_t e = 0; e < d.entries.size(); ++e)
            if (entryIntact(r_, s_, b.unit, e) && hasAbility(r_.componentAbilities(d.entries[e].component), AbilityKind::ShipCrewQuarters)) ++crew;
    }
    // Boarding parties defend as well as attack; each crew quarters adds 4 (confirmed: binary).
    const int64_t defense = detail::componentSum(r_, s_, b.unit, AbilityKind::BoardingDefense) + 4 * crew +
                            detail::componentSum(r_, s_, b.unit, AbilityKind::BoardingAttack);
    if (logging(i)) logOrder(TacticalOrder{TacticalOrder::Kind::Capture, pieces_[i].owner, i, t});
    pieces_[i].fired = true;
    event(Ev::Fire, i, t);
    if (offense > defense) {
        if (detail::hasIntactComponent(r_, s_, b.unit, AbilityKind::SelfDestruct)) {
            // The self-destruct device: both ships take 10000 Normal damage instead (confirmed: binary).
            note(std::format("{} self-destructed rather than be captured", label(t)));
            applyHit(i, t, DamageType::Normal, kSelfDestruct);
            if (combatant(i)) applyHit(t, i, DamageType::Normal, kSelfDestruct);
            return;
        }
        capture(t, i, true);
        applyHit(-1, i, DamageType::OnlyBoardingParties, kBoardingSpend);   // the boarding parties are spent
        applyHit(-1, t, DamageType::OnlySecurityStations, kBoardingSpend);
        return;
    }
    note(std::format("{} repelled boarders from {}", label(t), label(i)));
    applyHit(-1, i, DamageType::OnlyBoardingParties, kBoardingSpend);
    applyHit(-1, t, DamageType::OnlySecurityStations, rng_.rangeInt(1, 5));
}

void Battle::ram(int i, int t) {
    // Spec 04 §10.3 (confirmed: binary).
    if (logging(i)) logOrder(TacticalOrder{TacticalOrder::Kind::Ram, pieces_[i].owner, i, t});
    const Piece& a = pieces_[i];
    const Piece& b = pieces_[t];
    const bool drone = a.vtype == VehicleType::Drone && a.kind == Kind::UnitGroup;
    const bool shipTarget = b.kind == Kind::Vehicle;
    const DesignId targetDesign = b.unit.design;
    int64_t dealt = xmath::pctTrunc(ramHitPoints(i), cs_.ramSourcePercent);
    int64_t taken = xmath::pctTrunc(ramHitPoints(t), cs_.ramTargetPercent);
    if (b.kind == Kind::Planet) {
        dealt /= 4;
        taken = kImmovable;
    }
    if (drone) taken = kImmovable;
    // Warheads: the rammer's that can hit the target and all of the target's; both explode.
    int64_t warheads = 0;
    std::vector<std::pair<DamageType, int64_t>> droneWarheads;
    const uint8_t mask = maskOf(t);
    auto collect = [&](const Piece& p, bool own) {
        if (p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) return;
        // A ship's intact warheads; a unit group's, every unit of each design.
        const std::vector<UnitStack> designs = p.kind == Kind::UnitGroup ? p.stacks : std::vector<UnitStack>{{p.unit.design, 1}};
        for (const UnitStack& st : designs) {
            if (st.count <= 0) continue;
            const Design& d = s_.design(st.design);
            const int copies = st.count;
            for (size_t e = 0; e < d.entries.size(); ++e) {
                const ruleset::Component& c = r_.component(d.entries[e].component);
                if (c.weapon.kind != WeaponKind::Warhead || (p.kind == Kind::Vehicle && !entryIntact(r_, s_, p.unit, e))) continue;
                const DamageType type = parseDamageType(c.weapon.damageType);
                if (own && !(parseWeaponTargets(c.weapon.targets) & mask)) continue;
                const int64_t value = weaponLargestDamage(r_, d.entries[e]);
                if (own && drone)
                    for (int n = 0; n < copies; ++n) droneWarheads.emplace_back(type, value);
                if (warheadExplodes(type)) warheads += value * copies;
            }
        }
    };
    collect(a, true);
    collect(b, false);
    pieces_[i].fired = true;
    event(Ev::Fire, i, t);
    note(std::format("{} rammed {}", label(i), label(t)));
    // The blow has the rammer as its attacker: its empire's damage modifier applies (§8).
    auto blow = [&](int64_t value) { return xmath::pctRound(value, 100 + damageBonus(pieces_[i].owner)); };
    if (drone) {
        // A drone strikes with each warhead as its own hit, then with its bulk (confirmed: binary).
        for (const auto& [type, value] : droneWarheads) {
            if (!combatant(t)) break;
            const int64_t hit = blow(value);
            event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, hit)));
            applyHit(i, t, type, hit);
        }
        if (combatant(t)) {
            const int64_t hit = blow(dealt);
            event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, hit)));
            applyHit(i, t, DamageType::Normal, hit);
        }
    } else {
        const int64_t hit = blow(dealt + warheads);
        event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, hit)));
        applyHit(i, t, DamageType::Normal, hit);
    }
    const bool targetDestroyed = !combatant(t) || (pieces_[t].kind == Kind::Obstacle && pieces_[t].colonyLost);
    if (!combatant(i)) return;
    pieces_[i].mp = 0;
    // A ram kill counts twice: the rammer's design gets a ship target's hull
    // tonnage once more (confirmed: binary).
    if (targetDestroyed && shipTarget && targetDesign.valid()) creditTonnage(i, designTonnage(r_, s_.design(targetDesign)));
    // The recoil has no attacker: no damage modifier, and nobody is credited if the rammer dies.
    applyHit(-1, i, DamageType::SkipsAllShields, taken + warheads);
    if (targetDestroyed && combatant(i)) gainExperience(i, kShipKillTenths);   // a surviving rammer's crew: +1.0 more
}

void Battle::dropTroops(int i, int t) {
    // A ship drops every troop unit aboard, of whatever design, for its owner,
    // onto an adjacent hostile colony not contested by another empire's troops;
    // the ground combat is fought at once (confirmed: binary).
    Piece& planet = pieces_[t];
    if (planet.kind != Kind::Planet || !planet.alive || dist(i, t) > 1 || !hostileTo(i, t) || contestedBy(planet, pieces_[i].owner)) return;
    const EmpireId attacker = pieces_[i].owner;
    const bool invaded = std::any_of(planet.landed.begin(), planet.landed.end(), [](const UnitStack& u) { return u.count > 0; });
    int landed = 0;
    std::vector<UnitStack> dropped;
    for (UnitStack& st : pieces_[i].unit.cargo.units) {
        if (st.count <= 0 || !isTroopDesign(r_, s_, st.design)) continue;
        dropped.push_back(st);
        landed += st.count;
        st.count = 0;
    }
    if (landed <= 0) return;
    if (logging(i)) logOrder(TacticalOrder{TacticalOrder::Kind::DropTroops, attacker, i, t});
    std::erase_if(pieces_[i].unit.cargo.units, [](const UnitStack& u) { return u.count <= 0; });
    Piece& pl = pieces_[t];
    std::erase_if(pl.landed, [](const UnitStack& u) { return u.count <= 0; });
    detail::joinUnits(pl.landed, dropped);
    pl.invader = attacker;
    if (!invaded || pl.militia < 0) pl.militia = militiaCount(cs_, pl.population);
    troopsLanded_[attacker.value] += landed;
    event(Ev::Launch, i, t, landed);
    note(std::format("{} landed {} troops on {}", label(i), landed, pl.name));

    detail::GroundFight fight;
    fight.attacker = attacker;
    fight.defender = pl.owner;
    fight.invaders = &pl.landed;
    fight.cargo = &pl.unit.cargo;
    fight.population = &pl.population;
    fight.militia = &pl.militia;
    for (uint32_t f : pl.facilities) fight.groundDefensePercent += sumValue1(r_.facilityAbilities(f), AbilityKind::PlanetChangeGroundDefense);
    for (const auto& a : s_.galaxy.object(pl.object).abilities)
        if (parseAbilityKind(a.type) == AbilityKind::PlanetChangeGroundDefense) fight.groundDefensePercent += a.number1();
    // The record for the Ground Combat window: both sides as the fight begins.
    GroundCombat gc;
    gc.round = static_cast<uint8_t>(std::clamp(round_, 0, 255));
    gc.planetPiece = static_cast<uint32_t>(t);
    gc.troopShip = static_cast<uint32_t>(i);
    gc.planet = pl.object;
    gc.attacker = attacker;
    gc.defender = pl.owner;
    for (const PopulationGroup& g : pl.population) gc.population += g.millions;
    gc.facilities = pl.facilities;
    gc.militia = std::max(0, pl.militia);
    std::vector<size_t> attIndex, defIndex;
    for (size_t k = 0; k < pl.landed.size(); ++k)
        if (pl.landed[k].count > 0) {
            attIndex.push_back(k);
            gc.attackers.push_back(pl.landed[k]);
        }
    for (size_t k = 0; k < pl.unit.cargo.units.size(); ++k)
        if (pl.unit.cargo.units[k].count > 0) {
            defIndex.push_back(k);
            gc.defenders.push_back(pl.unit.cargo.units[k]);
        }
    const detail::GroundOutcome o = detail::fightGround(r_, s_, cs_, fight, rng_);
    Piece& after = pieces_[t];
    for (size_t k : attIndex) gc.attackersLeft.push_back(after.landed[k]);
    for (size_t k : defIndex) gc.defendersLeft.push_back(after.unit.cargo.units[k]);
    gc.militiaLeft = std::max(0, after.militia);
    gc.rounds = o.rounds;
    gc.captured = o.captured;
    rec_.grounds.push_back(std::move(gc));
    groundReports_.push_back(std::format("Ground combat on {}: {} rounds; invaders lost {} of {} troops, defenders {} units and {} militia{}.",
                                         after.name, o.rounds, o.attackersLost, o.attackersAtStart, o.defendersLost, o.militiaLost,
                                         o.captured ? "; the planet fell" : o.attackersGone ? "; the invasion failed" : ""));
    if (o.attackersGone) {
        after.landed.clear();
        after.invader = {};
    }
    if (!o.captured) {
        afterDamage(t);
        return;
    }
    // The planet's piece changes sides at once; the surviving invaders join its cargo.
    after.owner = attacker;
    after.unit.owner = attacker;
    after.capturedBy = attacker;
    after.militia = -1;
    detail::joinUnits(after.unit.cargo.units, after.landed);
    after.landed.clear();
    after.invader = {};
    if (after.isLeader) dissolve(t);
    buildPlanetWeapons(pieces_[t]);
    planetShields(pieces_[t], false);
    refreshStats(t);
    event(Ev::Captured, t, i, static_cast<int>(attacker.value));
    note(std::format("{} fell to {} troops", pieces_[t].name, s_.empire(attacker).name));
}

// ---- The turn sequence (spec 04 §4) ------------------------------------------------------------------

void Battle::beginRound() {
    if (round_ > 1) startRound();
    acted_.assign(pieces_.size(), 0);
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (pieces_[k].kind == Kind::Seeker) acted_[k] = 1;
}

void Battle::endPhase() {
    ++phaseIndex_;
    dangerFor_ = {};   // the next side builds its own danger map
    // The battle's end is checked only after a phase (confirmed: binary); in the
    // strategic window only after a whole combat turn, as for every battle
    // without player sides (inferred, spec 04 §19.2 Q70).
    stage_ = !strategic_ && over() ? Stage::Finished : Stage::Between;
}

bool Battle::isPlayer(EmpireId e) const { return std::find(players_.begin(), players_.end(), e) != players_.end(); }

void Battle::setPlayers(std::vector<EmpireId> players, std::optional<std::vector<EmpireId>> release) {
    players_ = std::move(players);
    release_ = std::move(release);
    strategic_ = players_.empty();
}

void Battle::advance() {
    // The counter starts at 1 and the battle ends when it reaches the setting:
    // one turn fewer than `Number Of Space Combat Turns` (confirmed: binary). It
    // also ends as soon as no two empires that still have pieces are hostile.
    while (stage_ == Stage::Between) {
        if (!roundOpen_) {
            if (round_ >= cs_.spaceTurns) {
                round_ = lastRound();
                stage_ = Stage::Finished;
                return;
            }
            beginRound();
            roundOpen_ = true;
            phaseIndex_ = 0;
        }
        if (phaseIndex_ >= order_.size()) {
            roundOpen_ = false;
            if (strategic_ && over()) {
                stage_ = Stage::Finished;
                return;
            }
            ++round_;
            continue;
        }
        const EmpireId e = order_[phaseIndex_];
        if (!hasPieces(e)) {
            ++phaseIndex_;
            continue;
        }
        phaseEmpire_ = e;
        if (isPlayer(e) && !autoAll_) {
            startPlayerPhase();   // the side's drones and seekers move, then the player takes over
            return;
        }
        phase(e);
        endPhase();
        if (stage_ != Stage::Between || !autoAll_ || !isPlayer(e)) continue;
        // With Auto on, play pauses after the phase of the last player's empire in each combat turn.
        bool later = false;
        for (size_t k = phaseIndex_; k < order_.size(); ++k)
            if (isPlayer(order_[k]) && hasPieces(order_[k])) later = true;
        if (!later) {
            stage_ = Stage::Paused;
            return;
        }
    }
}

void Battle::run() {
    players_.clear();
    autoAll_ = false;
    strategic_ = true;
    advance();
}

// ---- Results ----------------------------------------------------------------------------------------------------

void Battle::finish() {
    const std::string sector = detail::sectorName(s_, where_);
    // Empires that fought someone (the others were only present).
    std::vector<EmpireId> fighting;
    for (EmpireId e : empires_)
        for (EmpireId o : empires_)
            if (o != e && detail::enemies(s_, e, o)) {
                fighting.push_back(e);
                break;
            }
    std::map<uint32_t, Result> results;
    for (EmpireId e : fighting) {
        const bool mine = hasPieces(e);
        bool enemy = false;
        for (EmpireId o : empires_)
            if (o != e && detail::enemies(s_, e, o) && hasPieces(o)) enemy = true;
        results[e.value] = mine && !enemy ? Result::Win : (!mine && enemy ? Result::Loss : Result::Stalemate);
    }
    for (EmpireId e : fighting) {
        const Result res = results[e.value];
        const char* word = res == Result::Win ? "victory" : res == Result::Loss ? "defeat" : "stalemate";
        rec_.summary.push_back(std::format("{}: {}", s_.empire(e).name, word));
    }
    for (const std::string& line : groundReports_) rec_.summary.push_back(line);

    // Fleet experience earned during the battle.
    for (const auto& [f, exp] : fleetExp_)
        if (Fleet* fl = s_.fleet(FleetId{f})) {
            fl->experience = exp.first;
            fl->experienceTenths = exp.second;
        }

    // Per-empire losses for logs, mood and design statistics. A capture changes
    // no design statistic: the victim's design records no loss (spec 04 §15).
    struct Tally {
        std::vector<std::string> lost, destroyed, taken, captured;
        int shipsLost = 0, unitsLost = 0, unitsKilled = 0;
    };
    std::map<uint32_t, Tally> tally;
    for (const Piece& p : pieces_) {
        if (p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) continue;
        Tally& own = tally[p.startOwner.value];
        if (p.kind == Kind::UnitGroup) {
            own.unitsLost += p.unitsLost;
            continue;
        }
        if (!p.alive) {
            own.lost.push_back(p.name);
            ++own.shipsLost;
            ++s_.design(p.unit.design).lost;
        } else if (p.captured && p.owner != p.startOwner) {
            own.taken.push_back(p.name);
            ++own.shipsLost;
            tally[p.owner.value].captured.push_back(p.name);
        }
    }
    for (EmpireId e : fighting)
        for (const Piece& p : pieces_) {
            if ((p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) || p.startOwner == e || !detail::enemies(s_, e, p.startOwner)) continue;
            if (p.kind == Kind::UnitGroup) tally[e.value].unitsKilled += p.unitsLost;
            else if (!p.alive) tally[e.value].destroyed.push_back(p.name);
        }

    // Write vehicles back: damage, losses, capture, supply, cargo, experience.
    // Combat does not clear orders (spec 03 §6.3); a ship that changed owner loses them (spec 04 §12).
    for (Piece& p : pieces_) {
        if ((p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) || !p.source.valid()) continue;
        Vehicle* v = s_.vehicle(p.source);
        if (!v) continue;
        if (!p.alive) {
            v->count = 0;
            v->mixed.clear();
            continue;
        }
        if (p.kind == Kind::Vehicle) detail::restoreRegeneratingArmor(r_, s_, p.unit, kRegenerationCap);   // (history 1.79)
        v->damage = p.unit.damage;
        v->supply = p.unit.supply;
        std::erase_if(p.unit.cargo.units, [](const UnitStack& u) { return u.count <= 0; });
        v->cargo = p.unit.cargo;
        v->experience = p.unit.experience;
        v->experienceTenths = p.unit.experienceTenths;
        if (p.kind == Kind::Vehicle) v->count = p.unit.count;
        if (p.owner != v->owner) {
            if (Fleet* f = s_.fleet(v->fleet)) {
                std::erase(f->members, v->id);
                if (f->leader == v->id) f->leader = f->members.empty() ? VehicleId{} : f->members.front();
            }
            v->fleet = {};
            v->owner = p.owner;
            v->orders.clear();
            v->repeatOrders = false;
            v->targetVehicle = {};
            v->targetObject = {};
        }
    }

    // Planets: cargo, landed troops, population, facilities, plague, conditions, lost and captured colonies.
    for (Piece& p : pieces_) {
        if (p.kind != Kind::Planet && !p.colonyLost) continue;
        Colony* c = s_.colony(p.object);
        if (!c) continue;
        if (p.popKilled > 0) {
            ctx_.mood(p.startOwner, "1M Population Killed", where_.system, p.object, static_cast<int>(std::min<int64_t>(p.popKilled, INT_MAX)));
            ctx_.log(p.startOwner, LogCategory::Combat, std::format("{} bombarded", p.name),
                     std::format("{}M of our people were killed in the battle at {}.", p.popKilled, sector), where_);
        }
        if (p.colonyLost) {
            ctx_.log(p.startOwner, LogCategory::Combat, std::format("{} lost", p.name),
                     std::format("Our colony on {} was wiped out in the battle at {}.", p.name, sector), where_);
            // Its population died out: the colony is removed, the planet loses
            // value and the owner gets Homeworld Lost or Any Planet Lost (spec 02 §2).
            economy::colonyDiesOut(ctx_, p.object, std::format("wiped out in the battle at {}", sector));
            continue;
        }
        std::erase_if(p.unit.cargo.units, [](const UnitStack& u) { return u.count <= 0; });
        c->cargo = p.unit.cargo;
        std::erase_if(p.landed, [](const UnitStack& u) { return u.count <= 0; });
        c->landedTroops = p.landed;
        c->invader = p.landed.empty() ? EmpireId{} : p.invader;
        std::erase_if(p.population, [](const PopulationGroup& g) { return g.millions <= 0; });
        c->population = p.population;
        // Facilities lost in the battle are removed now, each once (OpenSE4 does
        // not copy the original's stale count of destroyed facilities, spec 04 §19.1).
        std::vector<uint32_t> kept;
        for (size_t f = 0; f < p.facilities.size(); ++f)
            if (!p.facilityLost[f]) kept.push_back(p.facilities[f]);
        c->facilities = std::move(kept);
        c->militia = p.militia;
        c->plagueLevel = std::max(c->plagueLevel, p.plague);
        if (p.conditionsLost > 0) {
            SpaceObject& obj = s_.galaxy.object(p.object);
            obj.conditions = static_cast<int>(std::clamp<int64_t>(obj.conditions - p.conditionsLost, 0, economy::kConditionsMax));   // 0-1.5 in hundredths
        }
        if (p.capturedBy.valid() && c->owner != p.capturedBy) detail::capturePlanet(ctx_, *c, p.capturedBy);
        if (invaders(r_, s_, *c).empty()) detail::endInvasion(*c, false);
    }

    // Carriers recover the fighter and satellite groups they launched in this
    // battle, ships first, then planets; the rest stay in space as the separate
    // groups they are (confirmed: binary).
    struct Spawn {
        EmpireId owner;
        std::vector<UnitStack> stacks;
        VehicleId target;
    };
    std::vector<Spawn> spawns;
    std::vector<std::vector<UnitStack>> left(pieces_.size());   // launched groups: units not recovered yet
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (pieces_[k].kind == Kind::UnitGroup && pieces_[k].alive)
            for (const UnitStack& st : pieces_[k].stacks)
                if (st.count > 0) left[k].push_back(st);
    for (const bool planets : {false, true})
        for (size_t c = 0; c < pieces_.size(); ++c) {
            const Piece& h = pieces_[c];
            if (!h.alive || (planets ? h.kind != Kind::Planet : h.kind != Kind::Vehicle)) continue;
            Cargo* cargo = nullptr;
            int64_t room = 0;
            if (h.kind == Kind::Vehicle && h.source.valid()) {
                Vehicle* v = s_.vehicle(h.source);
                if (!v || v->count <= 0) continue;
                room = vehicleCargoCapacity(r_, s_, *v) - cargoSpaceUsed(r_, s_, v->cargo);
                cargo = &v->cargo;
            } else if (h.kind == Kind::Planet) {
                Colony* col = s_.colony(h.object);
                if (!col) continue;
                room = colonyCargoCapacity(r_, s_, *col) - cargoSpaceUsed(r_, s_, col->cargo);
                cargo = &col->cargo;
            }
            if (!cargo) continue;
            for (size_t k = 0; k < pieces_.size(); ++k) {
                const Piece& u = pieces_[k];
                if (u.kind != Kind::UnitGroup || !u.launched || u.carrier != static_cast<int>(c) || u.owner != h.owner) continue;
                if (u.vtype != VehicleType::Fighter && u.vtype != VehicleType::Satellite) continue;
                for (UnitStack& st : left[k]) {
                    const int64_t tonnage = std::max(1, r_.hull(s_.design(st.design).hull).tonnage);
                    const int n = static_cast<int>(std::min<int64_t>(st.count, std::max<int64_t>(0, room) / tonnage));
                    if (n <= 0) continue;
                    detail::joinUnits(cargo->units, std::vector<UnitStack>{{st.design, n}});
                    st.count -= n;
                    room -= int64_t{n} * tonnage;
                }
            }
        }
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& u = pieces_[k];
        if (u.kind != Kind::UnitGroup) continue;
        if (u.launched) {
            std::erase_if(left[k], [](const UnitStack& st) { return st.count <= 0; });
            if (u.alive && !left[k].empty()) {
                const VehicleId target = u.droneTarget >= 0 && pieces_[u.droneTarget].alive ? pieces_[u.droneTarget].source : VehicleId{};
                spawns.push_back({u.owner, left[k], target});
            }
        } else if (Vehicle* v = s_.vehicle(u.source); v && u.alive) {
            setGroupStacks(s_, *v, u.stacks);   // the units of each design that are left
            v->damage.assign(s_.design(v->design).entries.size(), 0);
        }
    }

    // Mood events (Happiness.txt triggers) and logs for the empires that fought.
    for (EmpireId e : fighting) {
        const Result res = results[e.value];
        const std::string outcome = res == Result::Win ? "Win" : res == Result::Loss ? "Loss" : "Stalemate";
        ctx_.mood(e, "Battle in System - " + outcome, where_.system);
        for (const Piece& p : pieces_)
            if ((p.kind == Kind::Planet || p.colonyLost) && p.startOwner == e) ctx_.mood(e, "Battle in Sector - " + outcome, where_.system, p.object);
        const Tally& t = tally[e.value];
        if (t.shipsLost > 0) {
            ctx_.mood(e, "Any Ship Lost", {}, {}, t.shipsLost);
            ctx_.mood(e, "Ship Lost in System", where_.system, {}, t.shipsLost);
        }
        auto list = [](const std::vector<std::string>& names) {
            if (names.empty()) return std::string("none");
            std::string out;
            const size_t shown = std::min<size_t>(names.size(), 8);
            for (size_t i = 0; i < shown; ++i) out += (i ? ", " : "") + names[i];
            if (names.size() > shown) out += std::format(" and {} more", names.size() - shown);
            return out;
        };
        const char* headline = res == Result::Win ? "Victory" : res == Result::Loss ? "Defeat" : "Stalemate";
        std::string text = std::format("{}. Our losses: {}{}. Enemy losses: {}{}.", headline,
                                       list(t.lost), t.unitsLost ? std::format(" (and {} units)", t.unitsLost) : std::string{},
                                       list(t.destroyed), t.unitsKilled ? std::format(" (and {} units)", t.unitsKilled) : std::string{});
        if (!t.taken.empty()) text += std::format(" Taken: {}.", list(t.taken));
        if (!t.captured.empty()) text += std::format(" Captured: {}.", list(t.captured));
        if (const auto it = troopsLanded_.find(e.value); it != troopsLanded_.end()) text += std::format(" {} troops landed.", it->second);
        ctx_.log(e, LogCategory::Combat, std::format("Battle at {}", sector), std::move(text), where_);
    }

    // Each participant learns (or sees again) the designs it fought.
    for (EmpireId e : empires_) {
        Knowledge& known = s_.empire(e).knowledge;
        for (const Piece& p : pieces_) {
            if (p.startOwner == e || p.kind == Kind::Seeker || p.kind == Kind::Planet || p.kind == Kind::Obstacle || !p.unit.design.valid()) continue;
            const std::vector<UnitStack> designs = p.kind == Kind::UnitGroup ? p.stacks : std::vector<UnitStack>{{p.unit.design, 1}};
            for (const UnitStack& st : designs)
                if (s_.design(st.design).owner != e) seeDesign(known, st.design, s_.turn);
        }
    }

    if (!cs_.createReplay) rec_.events.clear();
    s_.combats.push_back(std::move(rec_));
    if (std::find(ctx_.battleSites.begin(), ctx_.battleSites.end(), where_) == ctx_.battleSites.end()) ctx_.battleSites.push_back(where_);

    // Last: launched units left in space (invalidates vehicle references), each
    // group as the separate group it is, with every design it holds (confirmed: binary).
    for (const Spawn& sp : spawns) {
        const Design& d = s_.design(sp.stacks.front().design);
        Vehicle v;
        v.owner = sp.owner;
        v.design = sp.stacks.front().design;
        v.name = d.name;
        v.location = where_;
        setGroupStacks(s_, v, sp.stacks);
        v.supply = vehicleSupplyCapacity(r_, s_, v);
        v.builtTurn = s_.turn;
        v.targetVehicle = sp.target;
        s_.addVehicle(std::move(v));
    }
}

} // namespace detail

namespace {

// Whether a human empire has a vehicle or colony in the sector (only then can a battle there ask).
bool humanPresent(const GameState& s, Location where) {
    auto human = [&](EmpireId e) { return e.valid() && e.index() < s.empires.size() && s.empire(e).alive && s.empire(e).kind == PlayerKind::Human; };
    for (const Vehicle& v : s.vehicles)
        if (v.location == where && v.count > 0 && human(v.owner)) return true;
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* c = s.colony(o); c && human(c->owner)) return true;
    return false;
}

// `entering` null: the vehicles that moved in this turn (the fallback of
// detail::enteringGroups); empty: nobody entered, so no mine strikes.
void resolve(TurnContext& ctx, Location where, const std::span<const VehicleId>* entering) {
    // A turn-based game with tactical combat asks the human sides (turn.hpp):
    // keep the game as the battle begins, in case the answer is missing.
    TurnContext::Battles* ask = ctx.battles && ctx.battles->answers ? ctx.battles : nullptr;
    std::shared_ptr<GameState> before;
    if (ask && ask->next >= ask->answers->size() && humanPresent(ctx.state, where)) before = std::make_shared<GameState>(ctx.state);
    Rng rng = ctx.state.rng.fork();
    // Mines strike first, then the battle check runs (confirmed: binary).
    if (!entering) detail::resolveMines(ctx, where, {}, rng);
    else if (!entering->empty()) detail::resolveMines(ctx, where, *entering, rng);
    detail::Battle battle(ctx, where, rng);
    if (!battle.setup()) return;
    if (ask) {
        // One question per battle for every human empire in it, hostile or not (confirmed: binary).
        std::vector<EmpireId> humans;
        for (EmpireId e : battle.empires())
            if (ctx.state.empire(e).alive && ctx.state.empire(e).kind == PlayerKind::Human) humans.push_back(e);
        if (!humans.empty()) {
            if (ask->next >= ask->answers->size()) {
                BattleQuestion q;
                q.where = where;
                if (entering) q.entering = std::vector<VehicleId>(entering->begin(), entering->end());
                q.humans = std::move(humans);
                q.participants = battle.empires();
                q.state = std::move(before);
                q.index = ask->next;
                throw game::detail::BattleQuestionRaised{std::move(q)};
            }
            const BattleAnswer& answer = (*ask->answers)[ask->next++];
            std::vector<EmpireId> players;
            for (EmpireId e : answer.tactical)
                if (std::find(humans.begin(), humans.end(), e) != humans.end()) players.push_back(e);
            battle.setPlayers(std::move(players));
            battle.play(answer.orders);
            battle.finish();
            return;
        }
    }
    battle.run();
    battle.finish();
}

} // namespace

void resolveSpaceCombat(TurnContext& ctx, Location where, std::span<const VehicleId> entering) { resolve(ctx, where, &entering); }

void resolveSpaceCombat(TurnContext& ctx, Location where) { resolve(ctx, where, nullptr); }

} // namespace opense4::game::combat
