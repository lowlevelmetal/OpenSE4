// Strategic space combat (docs/spec/04 §3-§12, §14-§16): the combat map,
// start boxes and formations, strategy-driven movement and targeting, direct
// fire, seekers, point defense, launched units, planets, boarding, ramming,
// troop drops with their ground combat, and the battle's results, replay
// record and logs.
//
// Every piece works on a copy of its vehicle or colony; the results are
// written back once the battle ends. Randomness comes from a fork of
// GameState::rng; pieces act in a stable order. No floating point: the
// percentages the original applies in floating point go through xmath.

#include "game/combat.hpp"

#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <format>
#include <map>
#include <tuple>

namespace opense4::game::combat {

namespace {

using detail::ShieldState;
using ruleset::VehicleType;
using ruleset::WeaponKind;
using Kind = CombatPiece::Kind;
using Ev = CombatEvent::Kind;

constexpr int kW = kCombatMapWidth;
constexpr int kH = kCombatMapHeight;
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
constexpr int kRangeTable = kCombatMapWidth;     // longest range the movement logic considers
constexpr int64_t kPlanetSizeRank = 1'000'000;   // planets rank as the largest targets
constexpr uint8_t kDroneTargets = kTargetShips | kTargetPlanets | kTargetSatellites;

// Facings (spec 03 §10): 0 up, 1 right, 2 down, 3 left, then the diagonals.
constexpr std::array<std::pair<int, int>, 8> kFacing{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {-1, -1}, {1, 1}, {-1, 1}}};
constexpr std::array<std::pair<int, int>, 8> kDirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}}};
// Boxes of several empires starting in the middle, beside the centre (inferred order).
constexpr std::array<std::pair<int, int>, 8> kBeside{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}, {1, -1}, {-1, 1}}};

int sgn(int v) { return (v > 0) - (v < 0); }
int gap(int a0, int aSize, int b0, int bSize) { return std::max({0, a0 - (b0 + bSize - 1), b0 - (a0 + aSize - 1)}); }
bool onMap(int x, int y) { return x >= 0 && y >= 0 && x < kW && y < kH; }

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

struct Weapon {
    size_t entry = 0;        // design entry index (planets: in the platform design)
    DesignEntry de;
    const ruleset::Component* comp = nullptr;
    DamageType type = DamageType::Normal;
    uint8_t targets = 0;
    int reloadRate = 1;
    int reach = 0;           // longest range with damage (seekers: travel)
    int perUnit = 1;         // fighter groups: identical entries on one unit, fired together
    int stack = -1;          // planets: the platform stack in the planet's cargo
    std::vector<int> reload; // one counter per instance (0 = ready)

    WeaponKind kind() const { return comp->weapon.kind; }
};

struct Piece {
    Kind kind = Kind::Vehicle;
    EmpireId owner, startOwner;
    VehicleId source;                 // the vehicle this piece stands for (invalid for launched units)
    ObjectId object;                  // planets and obstacles
    Vehicle unit;                     // working copy: one ship, or a unit group (count units, no partial damage)
    VehicleType vtype = VehicleType::Ship;
    std::string name;
    int x = 0, y = 0, size = 1;
    int facing = 0;
    bool alive = true;
    bool mothballed = false;
    ShieldState sh;
    int64_t pool = 0;                 // damage too small to destroy anything (spec 04 §9.1)
    int64_t shieldPool = 0;           // unit groups: Shields Only damage (spec 04 §9.4)
    int64_t regenPool = 0;            // organic armor (spec 04 §9.3)
    int mp = 0;
    int reach = 0;                    // movement points at the start of the combat turn
    std::vector<Weapon> weapons;
    std::vector<int> engaged;         // distinct targets engaged this combat turn
    int budget = 1;
    int offense = 0, defense = 0;     // offense includes the system bonus
    bool alwaysHit = false;
    bool armed = false;
    int64_t strength = 0;
    std::array<int64_t, kRangeTable + 1> firepower{};
    TargetCategory category = TargetCategory::Ships;
    FleetId fleet;                    // for fleet experience while the ship stays in it
    uint32_t designStrategy = 0, fleetStrategy = 0;
    int leader = -1;
    bool isLeader = false;
    int slotDx = 0, slotDy = 0;       // formation offset before turning to the leader's facing
    bool slotFixed = false;           // an offset that does not turn with the leader
    bool arrived = false;             // moved into the sector this turn: an attacker's piece
    int boxDx = 0, boxDy = 0;         // start box direction
    // Seekers.
    int seekTarget = -1, launcher = -1, travelled = 0, speed = 0, members = 1, launchRound = 0;
    int64_t hp = 0;
    Weapon seekWeapon;
    // Launched units and drones.
    int carrier = -1;
    bool launched = false;
    int droneTarget = -1;
    EmpireId droneTargetOwner;
    // Planets.
    std::vector<PopulationGroup> population;
    std::vector<uint32_t> facilities;
    size_t facilitiesStart = 0;
    int militia = -1;
    int64_t popKilled = 0, hpStart = 0;
    bool colonyLost = false;
    EmpireId capturedBy;              // planets taken by troops during the battle
    int plague = 0;
    int64_t conditionsLost = 0;       // hundredths of the conditions scale (inferred)
    // Bookkeeping.
    bool fired = false, damaged = false, captured = false, pushed = false;
    int unitsLost = 0, startCount = 1;
};

enum class Result : uint8_t { Win, Loss, Stalemate };

struct MovePlan {
    MoveStrategy mode = MoveStrategy::DontGetHurt;
    int target = -1;
    std::vector<std::pair<int, int>> path;
};

class Battle {
public:
    Battle(TurnContext& ctx, Location where, Rng& rng)
        : r_(ctx.rules), s_(ctx.state), ctx_(ctx), where_(where), rng_(rng), cs_(loadSettings(ctx.rules)) {
        occ_.assign(static_cast<size_t>(kW * kH), -1);
    }

    bool setup();
    void run();
    void finish();

private:
    // ---- Setup.
    void addVehiclePiece(const Vehicle& v);
    void addPlanetPiece(const Colony& c);
    void addObstaclePiece(ObjectId o);
    Weapon makeWeapon(const DesignEntry& de, size_t entry) const;
    void buildWeapons(Piece& p) const;
    void buildPlanetWeapons(Piece& p) const;
    void place();
    std::pair<int, int> randomIn(const std::array<int, 4>& box, int size);
    std::pair<int, int> freeNear(int cx, int cy, int size, int self) const;
    bool fits(int x, int y, int size, int self) const;
    void occupy(int i);
    void vacate(int i);
    int addPiece(Piece p);

    // ---- Per-round state.
    void startRound();
    void refreshPiece(int i);
    void refreshCombatValues(int i);
    void refreshStats(int i);
    void afterDamage(int i);
    int computeMp(int i) const;
    void planetShields(Piece& p, bool fill) const;
    bool hasPieces(EmpireId e) const;
    bool over() const;
    const Strategy& strategy(EmpireId e, uint32_t index) const;
    uint32_t strategyIndex(int i) const;
    const Strategy& strategyOf(int i) const { return strategy(pieces_[i].owner, strategyIndex(i)); }
    bool combatant(int i) const;
    int bestCrewExperience(EmpireId e) const;
    int fleetExp(const Piece& p) const;

    // ---- Queries.
    int dist(int a, int b) const;          // range distance: nearest footprint squares
    int aimDist(int a, int b) const;       // aim distance: top-left squares
    int distAt(int x, int y, int b) const;
    std::pair<int, int> centreOf(int i) const;
    bool isFree(int x, int y, int self) const;
    uint8_t maskOf(int i) const;
    TargetCategory categoryFor(int j, EmpireId viewer) const;
    int damagePercent(int j) const;
    int64_t sizeOf(int j) const;
    int64_t hitPoints(int j) const;
    int64_t planetHp(const Piece& p) const;
    bool invaderStack(const Piece& p, size_t k) const;
    bool hasSupply(int i) const;
    int instances(int i, const Weapon& w) const;
    int firedTogether(int i, const Weapon& w) const;
    bool canAffect(DamageType type, int t, int att) const;
    bool canMove(int att, int t) const;
    int damageBonus(EmpireId e) const;
    int hitChance(int i, const Weapon& w, int t) const;
    int64_t exposureAt(int i, int x, int y, bool reach) const;
    int nearestThreat(int i, int x, int y) const;
    int seekerDistance(int i, int x, int y) const;
    int64_t ourDamage(int i, int t, int d) const;
    bool hasTroops(int i) const;
    bool contestedBy(const Piece& planet, EmpireId e) const;
    bool overkill(int i, int t, bool seeker) const;

    // ---- Targeting.
    std::vector<int> sortedTargets(int i, const Strategy& S);
    int pickTarget(int i, const Weapon& w, const std::vector<int>& targets);
    int64_t incomingSeekerDamage(int t) const;

    // ---- Actions.
    void phase(EmpireId e);
    void act(int i);
    void fire(int i);
    void shoot(int i, size_t wi, size_t k, int t);
    void launchSeeker(int i, const Weapon& w, int t, int count);
    void applyHit(int att, int t, DamageType type, int64_t damage);
    void shipHit(int att, int t, DamageType type, int64_t damage);
    void groupHit(int att, int t, DamageType type, int64_t damage);
    void seekerHit(int att, int t, DamageType type, int64_t damage);
    void planetHit(int att, int t, DamageType type, int64_t damage);
    int64_t cargoHit(int att, int t, DamageType type, int64_t pool, bool platforms);
    void populationLoss(int att, int t, int64_t millions);
    void facilityLoss(int t);
    void forcedMove(int t, int att, int64_t squares, bool push);
    void randomMove(int t);
    void kill(int t, int att);
    void creditKill(int att, int victim);
    void capture(int t, int capturer, bool boarding);
    void dissolve(int leader);
    void pdReact(int mover);
    void moveSeekers(EmpireId e);
    void expire(int i);
    void launchUnits(EmpireId e);
    bool spawnUnit(int carrier, DesignId design, int count, uint32_t strategyIndex);

    // ---- Movement.
    MovePlan plan(int i);
    std::pair<MoveStrategy, int> chooseMode(int i, const Strategy& S);
    int desiredRange(int i, int t, MoveStrategy m) const;
    std::vector<std::pair<int, int>> pathToward(int i, int t, int range, bool avoidFire, bool& blocked) const;
    std::vector<std::pair<int, int>> pathToSquare(int i, int tx, int ty) const;
    std::vector<std::pair<int, int>> pathDontGetHurt(int i) const;
    void walk(int i, const std::vector<std::pair<int, int>>& path);
    void moveTo(int i, int x, int y);
    void step(int i, int x, int y);
    void followLeader(int i);
    void droneAct(int i);
    void board(int i, int t);
    void ram(int i, int t);
    void dropTroops(int i, int t);
    int boardTarget(int i) const;
    int troopTarget(int i) const;

    // ---- Records.
    void event(Ev k, int piece, int target, int amount = 0, uint32_t component = 0);
    void note(std::string line) { rec_.summary.push_back(std::format("Turn {}: {}", round_, std::move(line))); }
    std::string label(int i) const;

    const Rules& r_;
    GameState& s_;
    TurnContext& ctx_;
    Location where_;
    Rng& rng_;
    CombatSettings cs_;
    CombatRecord rec_;
    std::vector<Piece> pieces_;
    std::vector<char> acted_;
    std::vector<EmpireId> empires_;
    std::vector<EmpireId> order_;                                     // phase order, drawn once
    std::vector<EmpireId> defenders_;
    std::vector<int> occ_;
    mutable std::map<uint32_t, std::vector<Strategy>> strategies_;   // parsed lazily
    std::map<uint32_t, bool> holdFire_;
    std::map<uint32_t, int> troopsLanded_;
    std::map<uint32_t, int> combatBonus_, damageBonus_, shieldBonus_;   // system totals at the start
    std::map<uint32_t, std::pair<int, int>> fleetExp_;                // fleet -> (whole, tenths)
    std::map<std::pair<uint32_t, int>, int64_t> assigned_;            // (empire, target) -> direct damage this turn
    std::vector<std::string> groundReports_;
    int round_ = 1;
    int satelliteCap_ = 100;
    int interference_ = 0;
    int disruption_ = 0;
};

// ---- Setup ----------------------------------------------------------------------------------------

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
    const Design& d = s_.design(p.unit.design);
    const bool fighters = p.kind == Kind::UnitGroup && p.vtype == VehicleType::Fighter;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const ruleset::Component& c = r_.component(d.entries[i].component);
        if (!c.isWeapon() || c.weapon.kind == WeaponKind::Warhead) continue;
        if (fighters) {
            // A fighter group fires identical weapons (same part and mount) together (confirmed: binary).
            auto same = std::find_if(p.weapons.begin(), p.weapons.end(), [&](const Weapon& w) { return w.de == d.entries[i]; });
            if (same != p.weapons.end()) {
                ++same->perUnit;
                continue;
            }
        }
        Weapon w = makeWeapon(d.entries[i], i);
        // Satellite and drone groups fire each weapon of each unit on its own.
        w.reload.assign(p.kind == Kind::UnitGroup && !fighters ? static_cast<size_t>(std::max(1, p.unit.count)) : 1, 0);
        p.weapons.push_back(std::move(w));
    }
}

void Battle::buildPlanetWeapons(Piece& p) const {
    p.weapons.clear();
    // Every weapon of every platform is a separate weapon of the planet (confirmed: binary).
    for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
        const UnitStack& st = p.unit.cargo.units[k];
        if (st.count <= 0 || invaderStack(p, k) || r_.hull(s_.design(st.design).hull).type != VehicleType::WeaponPlatform) continue;
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
    rec_.pieces.push_back(CombatPiece{q.kind, q.owner, q.source, q.object, big ? DesignId{} : q.unit.design, q.name,
                                      static_cast<int16_t>(q.x), static_cast<int16_t>(q.y)});
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
    p.arrived = detail::arrivedThisTurn(s_, v);
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
    p.facilitiesStart = c.facilities.size();
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

void Battle::planetShields(Piece& p, bool fill) const {
    // Facility generators (and Planet - Shield Generation); platform shield parts do not add (confirmed: binary).
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
    const int bonus = shieldBonus_.count(p.owner.value) ? shieldBonus_.at(p.owner.value) : 0;
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
    interference_ = detail::sensorInterference(s_, where_);
    disruption_ = detail::shieldDisruption(s_, where_);
    satelliteCap_ = static_cast<int>(r_.setting("Maximum Satellites Per Player Per Sector", 100));

    const detail::Forces forces = detail::battleForces(r_, s_, where_);
    if (!forces.battle) return false;
    empires_ = forces.empires;
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
        if (p.kind == Kind::Vehicle)
            detail::refreshShields(r_, s_, p.unit, shieldBonus_[p.owner.value], disruption_, p.sh, true);
        else if (p.kind == Kind::Planet) planetShields(p, true);
        if (p.kind == Kind::Planet) p.hpStart = planetHp(p);
        refreshPiece(static_cast<int>(i));
    }
    place();

    std::vector<Piece> built = std::move(pieces_);
    pieces_.clear();
    acted_.clear();
    for (Piece& p : built) addPiece(std::move(p));
    for (size_t i = 0; i < pieces_.size(); ++i) occupy(static_cast<int>(i));

    // The phase order is drawn once: defenders first, then attackers, each in a random order (confirmed: binary).
    std::vector<EmpireId> att;
    order_ = defenders_;
    for (EmpireId e : empires_)
        if (std::find(defenders_.begin(), defenders_.end(), e) == defenders_.end()) att.push_back(e);
    rng_.shuffle(order_);
    rng_.shuffle(att);
    order_.insert(order_.end(), att.begin(), att.end());

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

std::pair<int, int> Battle::freeNear(int cx, int cy, int size, int self) const {
    cx = std::clamp(cx, 0, kW - size);
    cy = std::clamp(cy, 0, kH - size);
    for (int rad = 0; rad < std::max(kW, kH); ++rad)
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != rad) continue;
                if (fits(cx + dx, cy + dy, size, self)) return {cx + dx, cy + dy};
            }
    return {-1, -1};
}

std::pair<int, int> Battle::randomIn(const std::array<int, 4>& box, int size) {
    const int x0 = std::clamp(box[0], 0, kW - size), y0 = std::clamp(box[1], 0, kH - size);
    const int x1 = std::clamp(box[2] - size + 1, x0, kW - size), y1 = std::clamp(box[3] - size + 1, y0, kH - size);
    const int x = rng_.rangeInt(x0, x1);
    const int y = rng_.rangeInt(y0, y1);
    return {x, y};
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

void Battle::place() {
    // The start box: 6 squares for up to 20 pieces, 12 up to 40, 18 up to 60,
    // then 24, doubled along the edge it lies on (confirmed: binary).
    const size_t n = pieces_.size();
    const int side = n <= 20 ? 6 : n <= 40 ? 12 : n <= 60 ? 18 : 24;
    const int along = n > 60 ? 48 : side;
    auto span = [](int dir, int length, int centre, int extent) -> std::pair<int, int> {
        if (dir < 0) return {0, length - 1};
        if (dir > 0) return {extent - length, extent - 1};
        return {centre - length / 2, centre - length / 2 + length - 1};
    };
    auto edgeBox = [&](int dx, int dy) -> std::array<int, 4> {
        // Edge boxes are `along` long and `side` deep; corners are square (inferred).
        const int lx = dx == 0 && dy != 0 ? along : side, ly = dy == 0 && dx != 0 ? along : side;
        const auto [x0, x1] = span(dx, lx, kCentreX, kW);
        const auto [y0, y1] = span(dy, ly, kCentreY, kH);
        return {x0, y0, x1, y1};
    };
    auto besideBox = [&](int dx, int dy) -> std::array<int, 4> {
        auto axis = [&](int d, int c) -> std::pair<int, int> {
            if (d < 0) return {c - side, c - 1};
            if (d > 0) return {c + 1, c + side};
            return {c - side / 2, c - side / 2 + side - 1};
        };
        const auto [x0, x1] = axis(dx, kCentreX);
        const auto [y0, y1] = axis(dy, kCentreY);
        return {x0, y0, x1, y1};
    };
    const std::array<int, 4> centreBox = edgeBox(0, 0);

    // Empires with pieces that start in the middle each get a box beside the centre when there are several.
    std::vector<EmpireId> middle;
    for (const Piece& p : pieces_)
        if (p.owner.valid() && p.boxDx == 0 && p.boxDy == 0 && std::find(middle.begin(), middle.end(), p.owner) == middle.end())
            middle.push_back(p.owner);
    std::sort(middle.begin(), middle.end());
    auto boxOf = [&](const Piece& p) -> std::array<int, 4> {
        if (p.boxDx != 0 || p.boxDy != 0) return edgeBox(p.boxDx, p.boxDy);
        if (!p.owner.valid() || middle.size() <= 1) return centreBox;
        const size_t k = static_cast<size_t>(std::find(middle.begin(), middle.end(), p.owner) - middle.begin());
        const auto [bx, by] = kBeside[k % kBeside.size()];
        return besideBox(bx, by);
    };
    // Pieces face the centre: arrivals away from their edge, boxes beside the centre toward it (inferred).
    auto facingFor = [&](const Piece& p) {
        if (p.boxDx != 0 || p.boxDy != 0) return facingOf(-p.boxDx, -p.boxDy);
        if (!p.owner.valid() || middle.size() <= 1) return 0;
        const size_t k = static_cast<size_t>(std::find(middle.begin(), middle.end(), p.owner) - middle.begin());
        const auto [bx, by] = kBeside[k % kBeside.size()];
        return facingOf(-bx, -by);
    };

    std::vector<char> placed(n, 0);
    auto put = [&](size_t i, int x, int y) {
        const auto [px, py] = freeNear(x, y, pieces_[i].size, static_cast<int>(i));
        pieces_[i].x = std::max(0, px);
        pieces_[i].y = std::max(0, py);
        pieces_[i].facing = facingFor(pieces_[i]);
        occupy(static_cast<int>(i));
        placed[i] = 1;
    };
    auto putRandom = [&](size_t i) {
        const auto [x, y] = randomIn(boxOf(pieces_[i]), pieces_[i].size);
        put(i, x, y);
    };
    // Planets and obstacles first (confirmed: binary).
    for (size_t i = 0; i < n; ++i)
        if (pieces_[i].kind == Kind::Planet || pieces_[i].kind == Kind::Obstacle) putRandom(i);

    // Then fleet leaders; their members take formation slots (spec 03 §10).
    std::vector<FleetId> fleets;
    for (const Piece& p : pieces_)
        if (p.kind != Kind::Planet && p.kind != Kind::Obstacle && p.unit.fleet.valid() &&
            std::find(fleets.begin(), fleets.end(), p.unit.fleet) == fleets.end())
            fleets.push_back(p.unit.fleet);
    for (FleetId fid : fleets) {
        const Fleet* fleet = s_.fleet(fid);
        if (!fleet) continue;
        std::vector<size_t> members;
        for (VehicleId m : fleet->members)
            for (size_t i = 0; i < n; ++i)
                if (pieces_[i].source == m && pieces_[i].owner == fleet->owner && !placed[i]) members.push_back(i);
        if (members.empty()) continue;
        size_t leader = members.front();
        for (size_t m : members)
            if (pieces_[m].source == fleet->leader) leader = m;
        putRandom(leader);
        const Strategy& S = strategy(fleet->owner, fleet->strategy);
        const ruleset::Formation* formation =
            fleet->formation < r_.data().formations.size() ? &r_.data().formations[fleet->formation] : nullptr;
        size_t slot = 0;
        for (size_t m : members) {
            if (m == leader || S.breakFormation[static_cast<size_t>(pieces_[m].category)]) continue;
            // The slot's offset from the leader position, turned to the leader's facing.
            int dx = 0, dy = 0;
            if (formation) {
                if (slot >= formation->positions.size()) continue;   // beyond the positions: no formation place
                const auto& pos = formation->positions[slot++];
                dx = pos.x - formation->leader.x;
                dy = pos.y - formation->leader.y;
            }
            const auto [rx, ry] = rotateSlot(pieces_[leader].facing, dx, dy);
            put(m, std::clamp(pieces_[leader].x + rx, 0, kW - 1), std::clamp(pieces_[leader].y + ry, 0, kH - 1));
            if (!formation) {
                // (inferred) no formation record: keep the square found next to the leader, unturned.
                dx = pieces_[m].x - pieces_[leader].x;
                dy = pieces_[m].y - pieces_[leader].y;
                pieces_[m].slotFixed = true;
            }
            pieces_[m].leader = static_cast<int>(leader);
            pieces_[m].slotDx = dx;
            pieces_[m].slotDy = dy;
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
    if (p.isLeader || (p.leader >= 0 && pieces_[p.leader].alive && pieces_[p.leader].isLeader)) return p.fleetStrategy;
    return p.designStrategy;
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
    if (p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) return 0;
    if (p.mothballed || p.vtype == VehicleType::Satellite) return 0;
    // Half the system-map speed, rounded up (speed / 2 + 0.01, then Round), plus
    // the best Combat Movement part (confirmed: binary). The speed already holds
    // the 1-point override for missing supplies or command.
    const int speed = vehicleMaxMovement(r_, s_, p.unit);
    if (speed <= 0 && p.vtype == VehicleType::Base) return 0;
    return (std::max(0, speed) + 1) / 2 + static_cast<int>(detail::componentBest(r_, s_, p.unit, AbilityKind::CombatMovement));
}

void Battle::refreshCombatValues(int i) {
    Piece& p = pieces_[i];
    const int sys = combatBonus_.count(p.owner.value) ? combatBonus_.at(p.owner.value) : 0;
    p.alwaysHit = false;
    if (p.kind == Kind::Planet) {
        // Facilities and weapon platforms, family by family (inferred: one pool of families) + racial + the setting.
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
        for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
            const UnitStack& st = p.unit.cargo.units[k];
            if (st.count <= 0 || invaderStack(p, k) || r_.hull(s_.design(st.design).hull).type != VehicleType::WeaponPlatform) continue;
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
        p.offense = detail::unitOffense(r_, s_, p.unit) + sys;
        p.defense = detail::unitDefense(r_, s_, p.unit);
        p.alwaysHit = detail::hasIntactComponent(r_, s_, p.unit, AbilityKind::WeaponsAlwaysHit);
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
    else {
        const int multiplex = static_cast<int>(std::max<int64_t>(1, detail::componentBest(r_, s_, p.unit, AbilityKind::MultiplexTracking)));
        p.budget = p.kind == Kind::UnitGroup ? std::max(multiplex, p.unit.count) : multiplex;
    }
    refreshCombatValues(i);
    // Firepower by range, used to judge threats and ranges.
    p.firepower.fill(0);
    p.strength = 0;
    p.armed = false;
    for (const Weapon& w : p.weapons) {
        const int n = instances(i, w) * firedTogether(i, w);
        if (n <= 0) continue;
        p.armed = true;
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
    p.mp = computeMp(i);
    p.reach = p.mp;
    refreshStats(i);
}

void Battle::afterDamage(int i) {
    // A survivor's shields are capped at their new maximum, its movement at the new allowance (confirmed: binary).
    Piece& p = pieces_[i];
    if (!p.alive || p.kind == Kind::Obstacle || p.kind == Kind::Seeker) return;
    if (p.kind == Kind::Vehicle) detail::refreshShields(r_, s_, p.unit, shieldBonus_[p.owner.value], disruption_, p.sh, false);
    else if (p.kind == Kind::Planet) planetShields(p, false);
    p.mp = std::min(p.mp, computeMp(i));
    refreshStats(i);
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
            // design order; with none destroyed the pool empties (history 1.80).
            const int64_t organic = detail::componentSum(r_, s_, p.unit, AbilityKind::ArmorRegeneration);
            p.regenPool = std::min(kRegenerationCap, p.regenPool + organic);
            p.regenPool -= detail::restoreRegeneratingArmor(r_, s_, p.unit, p.regenPool);
            if (!detail::hasDestroyedRegeneratingArmor(r_, s_, p.unit)) p.regenPool = 0;
            detail::refreshShields(r_, s_, p.unit, shieldBonus_[p.owner.value], disruption_, p.sh, false);
        } else if (p.kind == Kind::Planet) {
            int64_t regen = 0;
            for (uint32_t f : p.facilities) regen += sumValue1(r_.facilityAbilities(f), AbilityKind::ShieldRegeneration);
            p.sh.current = static_cast<int>(std::min<int64_t>(p.sh.max, p.sh.current + regen));   // (inferred) facilities regenerate
        }
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

int Battle::distAt(int x, int y, int b) const {
    const Piece& q = pieces_[b];
    return std::max(gap(x, 1, q.x, q.size), gap(y, 1, q.y, q.size));
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

bool Battle::invaderStack(const Piece& p, size_t k) const {
    // Only troops invade; other stored units serve the planet's owner.
    const UnitStack& st = p.unit.cargo.units[k];
    if (!isTroopDesign(r_, s_, st.design)) return false;
    const EmpireId owner = s_.design(st.design).owner;
    return owner.valid() && owner != p.owner && detail::enemies(s_, owner, p.owner);
}

// Population × damage per population + the units in cargo − the pool (confirmed: binary).
int64_t Battle::planetHp(const Piece& p) const {
    int64_t hp = 0;
    for (const PopulationGroup& g : p.population) hp += g.millions * cs_.damagePerPopulation;
    for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
        const UnitStack& st = p.unit.cargo.units[k];
        if (st.count <= 0 || invaderStack(p, k)) continue;
        hp += int64_t{st.count} * detail::unitHitPoints(r_, s_.design(st.design), DamageType::Normal);
    }
    return std::max<int64_t>(0, hp - p.pool);
}

int64_t Battle::hitPoints(int j) const {
    const Piece& p = pieces_[j];
    switch (p.kind) {
        case Kind::Seeker: return std::max<int64_t>(0, p.hp * p.members - p.pool);
        case Kind::Planet: return planetHp(p);
        case Kind::UnitGroup: {
            const bool shielded = !(p.vtype == VehicleType::Fighter && p.unit.supply <= 0);
            return std::max<int64_t>(0, detail::unitHitPoints(r_, s_.design(p.unit.design), DamageType::Normal, shielded) * p.unit.count - p.pool);
        }
        case Kind::Vehicle: return detail::remainingStructure(r_, s_, p.unit);
        case Kind::Obstacle: return 0;
    }
    return 0;
}

int Battle::damagePercent(int j) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Seeker || p.kind == Kind::Obstacle) return 0;
    if (p.kind == Kind::Planet) return p.hpStart > 0 ? static_cast<int>(100 - planetHp(p) * 100 / p.hpStart) : 0;
    const Design& d = s_.design(p.unit.design);
    if (p.kind == Kind::Vehicle) {
        const int structure = std::max(1, detail::designStructure(r_, d));
        return (structure - detail::remainingStructure(r_, s_, p.unit)) * 100 / structure;
    }
    const int64_t per = std::max<int64_t>(1, detail::unitHitPoints(r_, d, DamageType::Normal));
    const int64_t total = per * std::max(1, p.startCount);
    return static_cast<int>(std::clamp<int64_t>((total - hitPoints(j)) * 100 / total, 0, 100));
}

int64_t Battle::sizeOf(int j) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Planet) return kPlanetSizeRank;
    if (p.kind == Kind::Seeker || p.kind == Kind::Obstacle) return 0;
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
        return p.vtype == VehicleType::Fighter ? 1 : std::min<int>(static_cast<int>(w.reload.size()), p.unit.count);
    }
    return entryIntact(r_, s_, p.unit, w.entry) ? 1 : 0;
}

int Battle::firedTogether(int i, const Weapon& w) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::UnitGroup && p.vtype == VehicleType::Fighter) return p.unit.count * w.perUnit;
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

bool Battle::canAffect(DamageType type, int t, int att) const {
    const Piece& b = pieces_[t];
    const detail::DamageRule rule = detail::damageRule(type);
    switch (b.kind) {
        case Kind::Obstacle: return false;
        case Kind::Seeker: return rule.structural && !rule.shieldsOnly && !isSpecialEffect(type);
        case Kind::Planet:
            if (isPlanetOnlyDamage(type)) return true;
            if (!rule.structural || isSpecialEffect(type)) return false;   // (inferred) no reload, conversion or moves on planets
            if (rule.shieldsOnly) return b.sh.current > 0;
            return !rule.only || (*rule.only == detail::Layer::Weapons && b.armed);
        case Kind::UnitGroup:
            if (isPlanetOnlyDamage(type) || type == DamageType::CrewConversion) return false;
            if (type == DamageType::PushesTarget || type == DamageType::PullsTarget) return canMove(att, t);
            if (type == DamageType::IncreaseReloadTime || type == DamageType::DisruptReloadTime) return b.armed;
            if (rule.only) return detail::canAffectVehicle(r_, s_, b.unit, b.sh, type);
            return true;
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

// Damage hostile pieces could deal to a piece at (x, y). With `reach`, they first move twice their speed (inferred, spec 04 §19.1).
int64_t Battle::exposureAt(int i, int x, int y, bool reach) const {
    int64_t total = 0;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& h = pieces_[k];
        if (!h.alive || h.kind == Kind::Seeker || h.kind == Kind::Obstacle || !h.armed || !detail::enemies(s_, h.owner, pieces_[i].owner))
            continue;
        const int d = distAt(x, y, static_cast<int>(k)) - (reach ? 2 * h.reach : 0);
        total += h.firepower[static_cast<size_t>(std::clamp(d, 1, kRangeTable))];
    }
    return total;
}

int Battle::seekerDistance(int i, int x, int y) const {
    int best = kW * 2;
    for (const Piece& sk : pieces_)
        if (sk.alive && sk.kind == Kind::Seeker && sk.seekTarget == i) best = std::min(best, std::max(std::abs(sk.x - x), std::abs(sk.y - y)));
    return best;
}

int Battle::nearestThreat(int i, int x, int y) const {
    int best = kW * 2;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& h = pieces_[k];
        if (!h.alive || h.kind == Kind::Seeker || h.kind == Kind::Obstacle || !h.armed || !detail::enemies(s_, h.owner, pieces_[i].owner))
            continue;
        best = std::min(best, distAt(x, y, static_cast<int>(k)));
    }
    return best;
}

int64_t Battle::ourDamage(int i, int t, int d) const {
    int64_t total = 0;
    for (const Weapon& w : pieces_[i].weapons) {
        if (w.kind() == WeaponKind::PointDefense || !(w.targets & maskOf(t))) continue;
        total += int64_t{weaponDamage(r_, w.de, d)} * instances(i, w) * firedTogether(i, w) / w.reloadRate;
    }
    return total;
}

bool Battle::hasTroops(int i) const {
    for (const UnitStack& u : pieces_[i].unit.cargo.units)
        if (u.count > 0 && isTroopDesign(r_, s_, u.design)) return true;
    return false;
}

bool Battle::contestedBy(const Piece& planet, EmpireId e) const {
    for (size_t k = 0; k < planet.unit.cargo.units.size(); ++k) {
        const UnitStack& u = planet.unit.cargo.units[k];
        if (u.count <= 0 || !invaderStack(planet, k)) continue;
        if (s_.design(u.design).owner != e) return true;
    }
    return false;
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

std::vector<int> Battle::sortedTargets(int i, const Strategy& S) {
    struct Candidate {
        int idx = 0;
        bool fresh = true;
        int priority = 0;
        std::array<int64_t, 4> keys{};
    };
    std::vector<Candidate> list;
    const Piece& a = pieces_[i];
    const bool hold = holdFire_.count(a.owner.value) > 0;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const int j = static_cast<int>(k);
        const Piece& b = pieces_[k];
        if (!combatant(j) || j == i || b.owner == a.owner || !detail::enemies(s_, a.owner, b.owner)) continue;
        const TargetCategory cat = categoryFor(j, a.owner);
        if (S.dontFireOn[static_cast<size_t>(cat)]) continue;
        if (hold && b.kind == Kind::Planet && !b.armed) continue;
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
        if (!combatant(t) || !(w.targets & maskOf(t))) continue;
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
    launchUnits(e);
    // Drones move and attack first, then seekers, then everything else (confirmed: binary).
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (pieces_[k].alive && pieces_[k].owner == e && pieces_[k].kind == Kind::UnitGroup && pieces_[k].vtype == VehicleType::Drone &&
            !acted_[k]) {
            acted_[k] = 1;
            droneAct(static_cast<int>(k));
        }
    moveSeekers(e);
    auto ready = [&](size_t k) {
        return !acted_[k] && pieces_[k].alive && pieces_[k].owner == e && pieces_[k].kind != Kind::Seeker && pieces_[k].kind != Kind::Obstacle;
    };
    for (size_t k = 0; k < pieces_.size(); ++k) {
        if (!ready(k)) continue;
        const Piece& p = pieces_[k];
        // Group members act right after their leader.
        if (p.leader >= 0 && !acted_[static_cast<size_t>(p.leader)] && pieces_[p.leader].alive && pieces_[p.leader].isLeader &&
            pieces_[p.leader].owner == e)
            continue;
        act(static_cast<int>(k));
        if (pieces_[k].isLeader)
            for (size_t m = 0; m < pieces_.size(); ++m)
                if (pieces_[m].leader == static_cast<int>(k) && ready(m)) act(static_cast<int>(m));
    }
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (ready(k)) act(static_cast<int>(k));
}

void Battle::act(int i) {
    acted_[static_cast<size_t>(i)] = 1;
    if (pieces_[i].mothballed) return;
    if (pieces_[i].kind == Kind::Planet) {
        fire(i);
        return;
    }
    const Piece& p = pieces_[i];
    if (p.leader >= 0 && pieces_[p.leader].alive && pieces_[p.leader].isLeader && pieces_[p.leader].owner == p.owner) {
        followLeader(i);
        if (pieces_[i].alive) fire(i);
        pieces_[i].mp = 0;
        return;
    }
    // The computer fires before moving only when the move takes it farther from
    // its target; otherwise it moves first (confirmed: binary). One move per phase.
    const MovePlan mv = plan(i);
    const int t = mv.target;
    bool fireFirst = false;
    if (t >= 0 && !mv.path.empty()) fireFirst = distAt(mv.path.back().first, mv.path.back().second, t) > dist(i, t);
    if (fireFirst) fire(i);
    if (!pieces_[i].alive) return;
    walk(i, mv.path);
    if (!pieces_[i].alive) return;
    if (t >= 0 && pieces_[t].alive && dist(i, t) <= 1) {
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
    if (!fireFirst) fire(i);
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
    const auto [x, y] = centreOf(i);   // a planet launches from its centre square
    // A seeker of the same empire, weapon and target on that square takes the new one in (confirmed: binary).
    for (size_t k = 0; k < pieces_.size(); ++k) {
        Piece& sk = pieces_[k];
        if (sk.alive && sk.kind == Kind::Seeker && sk.owner == pieces_[i].owner && sk.seekTarget == t && sk.seekWeapon.de == w.de &&
            sk.x == x && sk.y == y && sk.travelled == 0) {
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
            if (b.kind == Kind::Vehicle && detail::canAffectVehicle(r_, s_, b.unit, b.sh, type) && att >= 0 && rng_.rangeInt(1, 100) <= damage)
                capture(t, att, false);
            return;
        case DamageType::IncreaseReloadTime:
        case DamageType::DisruptReloadTime:
            if (b.kind == Kind::Vehicle || b.kind == Kind::UnitGroup) {
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
    afterDamage(t);
}

void Battle::groupHit(int att, int t, DamageType type, int64_t damage) {
    // Spec 04 §9.4: the hit goes into the group's pool; up to 20 times a unit
    // design is picked, and a unit dies when the pool covers its hit points.
    if (isPlanetOnlyDamage(type)) return;
    Piece& b = pieces_[t];
    const detail::DamageRule rule = detail::damageRule(type);
    if (rule.shieldsOnly) {
        b.shieldPool += damage;
        return;
    }
    const Design& d = s_.design(b.unit.design);
    if (rule.armorSpecials) {
        // (inferred) emissive armor of the unit design acts as on a ship.
        int64_t emissive = bestValue1(r_.hullAbilities(d.hull), AbilityKind::EmissiveArmor);
        for (const DesignEntry& e : d.entries) emissive = std::max(emissive, bestValue1(r_.componentAbilities(e.component), AbilityKind::EmissiveArmor));
        if (emissive > 0) {
            if (damage <= emissive) return;
            damage -= emissive;
        }
    }
    b.damaged = true;
    const int64_t before = b.pool;
    b.pool += damage;
    const bool shielded = !(b.vtype == VehicleType::Fighter && b.unit.supply <= 0);
    const int64_t hp = detail::unitHitPoints(r_, d, type, shielded);
    const int64_t shieldPart = hp - detail::designStructure(r_, d);
    int kills = 0;
    for (int n = 0; n < 20 && b.unit.count > 0; ++n) {
        const int64_t soak = std::min(b.shieldPool, shieldPart);   // Shields Only damage makes kills easier (inferred)
        if (b.pool < hp - soak) break;
        b.pool -= hp - soak;
        b.shieldPool -= soak;
        --b.unit.count;
        ++kills;
    }
    if (!rule.hullDamaging) b.pool = std::min(b.pool, before);   // other types count for this hit only
    if (kills > 0) {
        b.unitsLost += kills;
        s_.design(b.unit.design).lost += kills;
        int k = att;
        if (k >= 0 && pieces_[k].kind == Kind::Seeker) k = pieces_[k].launcher;
        if (k >= 0 && pieces_[k].kind != Kind::Planet && pieces_[k].unit.design.valid()) s_.design(pieces_[k].unit.design).kills += kills;
    }
    if (b.unit.count <= 0) kill(t, att);
    else if (kills > 0) afterDamage(t);
}

void Battle::seekerHit(int att, int t, DamageType type, int64_t damage) {
    // A hit that (with the pool) reaches a seeker's hit points destroys one member (confirmed: binary).
    const detail::DamageRule rule = detail::damageRule(type);
    if (!rule.structural || rule.shieldsOnly || isSpecialEffect(type)) return;   // a seeker has no shields to drain
    Piece& b = pieces_[t];
    const int64_t total = b.pool + damage;
    if (total < b.hp) {
        b.pool = total;
        return;
    }
    b.pool = 0;
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
                // D × 0.1 of the 0-1.5 conditions scale; SpaceObject::conditions counts hundredths of it (inferred).
                p.conditionsLost += rem * 10;
                return;
            case DamageType::OnlyResupplyDepots:
            case DamageType::OnlySpaceports: {
                const AbilityKind k = type == DamageType::OnlySpaceports ? AbilityKind::Spaceport : AbilityKind::SupplyGeneration;
                // The first such facility found (confirmed: binary).
                auto it = std::find_if(p.facilities.begin(), p.facilities.end(), [&](uint32_t f) { return hasAbility(r_.facilityAbilities(f), k); });
                if (it != p.facilities.end()) p.facilities.erase(it);
                afterDamage(t);
                return;
            }
            default: return;
        }
    }
    if (!rule.structural || isSpecialEffect(type)) return;   // (inferred) planets cannot be moved; no reload effects
    if (rule.only) {
        // (inferred) of the "Only" types, Only Weapons reaches the weapon platforms; the rest do nothing to planets.
        if (*rule.only != detail::Layer::Weapons) return;
        const int64_t before = p.pool;
        const int64_t left = cargoHit(att, t, type, p.pool + damage, true);
        pieces_[t].pool = std::min(left, before);
        afterDamage(t);
        return;
    }
    // Hull-damaging: the pool joins the hit, then shields.
    int64_t d = damage;
    if (rule.hullDamaging) {
        d += p.pool;
        p.pool = 0;
    }
    d = detail::absorbShields(p.sh, d, rule);
    if (rule.shieldsOnly || d <= 0) return;
    p.damaged = true;
    // Weapon platforms take it first, otherwise other stored units, then the population (confirmed: binary).
    bool platforms = false, others = false;
    for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
        const UnitStack& st = p.unit.cargo.units[k];
        if (st.count <= 0 || invaderStack(p, k)) continue;
        (r_.hull(s_.design(st.design).hull).type == VehicleType::WeaponPlatform ? platforms : others) = true;
    }
    if (platforms || others) pieces_[t].pool = cargoHit(att, t, type, d, platforms);
    bool unitsLeft = false;
    for (size_t k = 0; k < pieces_[t].unit.cargo.units.size(); ++k)
        if (pieces_[t].unit.cargo.units[k].count > 0 && !invaderStack(pieces_[t], k)) unitsLeft = true;
    if (!unitsLeft) {
        // The whole hit, however much the units took (confirmed: binary).
        pieces_[t].pool = 0;
        populationLoss(att, t, std::max<int64_t>(1, d / cs_.damagePerPopulation));
        if (pieces_[t].alive && !pieces_[t].colonyLost && rng_.below(3) == 0) facilityLoss(t);
    }
    afterDamage(t);
}

// The planet's stored units as one unit group (spec 04 §9.4); returns the pool left.
int64_t Battle::cargoHit(int att, int t, DamageType type, int64_t pool, bool platforms) {
    Piece& p = pieces_[t];
    int kills = 0;
    for (int n = 0; n < 20; ++n) {
        std::vector<size_t> stacks;
        for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
            const UnitStack& st = p.unit.cargo.units[k];
            if (st.count <= 0 || invaderStack(p, k)) continue;
            if ((r_.hull(s_.design(st.design).hull).type == VehicleType::WeaponPlatform) == platforms) stacks.push_back(k);
        }
        if (stacks.empty()) break;
        UnitStack& st = p.unit.cargo.units[stacks[rng_.below(stacks.size())]];
        const int64_t hp = detail::unitHitPoints(r_, s_.design(st.design), type);
        if (pool < hp) continue;
        pool -= hp;
        --st.count;
        ++s_.design(st.design).lost;
        ++kills;
    }
    int k = att;
    if (k >= 0 && pieces_[k].kind == Kind::Seeker) k = pieces_[k].launcher;
    if (kills > 0 && k >= 0 && pieces_[k].kind != Kind::Planet && pieces_[k].unit.design.valid()) s_.design(pieces_[k].unit.design).kills += kills;
    return pool;
}

void Battle::populationLoss(int att, int t, int64_t millions) {
    Piece& p = pieces_[t];
    while (millions > 0) {
        auto largest = std::max_element(p.population.begin(), p.population.end(),
                                        [](const PopulationGroup& a, const PopulationGroup& b) { return a.millions < b.millions; });
        if (largest == p.population.end() || largest->millions <= 0) break;
        const int64_t n = std::min(millions, largest->millions);
        largest->millions -= n;
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
    creditKill(att, t);
    if (p.isLeader) dissolve(t);
    p.kind = Kind::Obstacle;
    p.owner = {};
    p.weapons.clear();
    p.armed = false;
}

void Battle::facilityLoss(int t) {
    // Facilities fall so that at most (hit points) ÷ (starting hit points ÷ facilities) remain (confirmed: binary).
    Piece& p = pieces_[t];
    if (p.facilitiesStart == 0 || p.facilities.empty()) return;
    const int64_t per = p.hpStart / static_cast<int64_t>(p.facilitiesStart);
    if (per <= 0) return;
    const int64_t allowed = planetHp(p) / per;
    while (static_cast<int64_t>(p.facilities.size()) > allowed && !p.facilities.empty())
        p.facilities.erase(p.facilities.begin() + static_cast<std::ptrdiff_t>(rng_.below(p.facilities.size())));   // (inferred) at random
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

void Battle::creditKill(int att, int victim) {
    // Experience only for kills: +1.0 for a ship, base or planet, +0.1 for a whole
    // unit group or a seeker; seekers credit their launcher (confirmed: binary; history 1.87).
    if (att < 0) return;
    int k = att;
    if (pieces_[k].kind == Kind::Seeker) k = pieces_[k].launcher;
    if (k < 0) return;
    const Piece& v = pieces_[victim];
    const bool big = v.kind == Kind::Vehicle || v.kind == Kind::Planet || v.colonyLost;
    if (big && pieces_[k].kind != Kind::Planet && pieces_[k].unit.design.valid() && v.kind == Kind::Vehicle)
        ++s_.design(pieces_[k].unit.design).kills;
    Piece& killer = pieces_[k];
    if (killer.kind != Kind::Vehicle || !killer.alive) return;   // unit groups and planets gain no experience
    detail::addExperience(killer.unit.experience, killer.unit.experienceTenths, big ? kShipKillTenths : kUnitKillTenths);
    if (killer.fleet.valid() && !killer.captured && rng_.below(4) == 0) {
        auto& [whole, tenths] = fleetExp_[killer.fleet.value];
        detail::addExperience(whole, tenths, 1);   // the fleet's 1-in-4 chance of +0.1
    }
    for (size_t j = 0; j < pieces_.size(); ++j)
        if (pieces_[j].alive && pieces_[j].owner == killer.owner && pieces_[j].kind == Kind::Vehicle) refreshCombatValues(static_cast<int>(j));
}

void Battle::kill(int t, int att) {
    Piece& b = pieces_[t];
    if (!b.alive) return;
    b.alive = false;
    vacate(t);
    event(Ev::Destroyed, t, att >= 0 ? att : t);
    creditKill(att, t);
    if (b.isLeader) dissolve(t);
    if (b.kind != Kind::Seeker && b.kind != Kind::Planet) note(std::format("{} destroyed", label(t)));
}

void Battle::dissolve(int leader) {
    pieces_[leader].isLeader = false;
    for (Piece& p : pieces_)
        if (p.leader == leader) p.leader = -1;
}

void Battle::capture(int t, int capturer, bool boarding) {
    // The ship changes owner at once (confirmed: binary). Boarding also costs its
    // crew experience and adds the captured-ship reload; conversion does neither.
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
    pieces_[t].designStrategy = 0;   // (inferred) the captor's first strategy
    pieces_[t].fleetStrategy = 0;
    pieces_[t].droneTarget = -1;
    detail::refreshShields(r_, s_, pieces_[t].unit, shieldBonus_[newOwner.value], disruption_, pieces_[t].sh, false);
    refreshStats(t);
    event(Ev::Captured, t, capturer, static_cast<int>(newOwner.value));
    // (inferred, spec 04 §19.1 Q27) a capture counts as a kill for the captor's design.
    int k = capturer;
    if (k >= 0 && pieces_[k].kind != Kind::Planet && pieces_[k].unit.design.valid()) ++s_.design(pieces_[k].unit.design).kills;
    note(std::format("{} {} by {}", pieces_[t].name, boarding ? "captured" : "converted", s_.empire(newOwner).name));
}

void Battle::pdReact(int mover) {
    // Point-defense fires outside the budget, once per reload cycle, at categories in its set (confirmed: binary).
    auto shootPd = [&](int j, int target) {
        for (size_t wi = 0; wi < pieces_[j].weapons.size(); ++wi) {
            for (size_t k = 0;; ++k) {
                if (!combatant(target) || !combatant(j) || !hasSupply(j)) return;
                const Weapon& w = pieces_[j].weapons[wi];
                if (w.kind() != WeaponKind::PointDefense || k >= static_cast<size_t>(instances(j, w))) break;
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

void Battle::launchUnits(EmpireId e) {
    const size_t n = pieces_.size();
    for (size_t k = 0; k < n; ++k) {
        const int i = static_cast<int>(k);
        if (!pieces_[k].alive || pieces_[k].owner != e || pieces_[k].mothballed) continue;
        const Kind kind = pieces_[k].kind;
        if (kind != Kind::Vehicle && kind != Kind::Planet) continue;
        if (pieces_[k].unit.cargo.units.empty()) continue;
        int fighters = 0, satellites = 0, drones = 0;
        if (kind == Kind::Planet) {
            // Up to 100 of each kind per combat turn (confirmed: binary).
            fighters = satellites = drones = kPlanetLaunch;
        } else {
            fighters = static_cast<int>(detail::componentSum(r_, s_, pieces_[k].unit, AbilityKind::LaunchRecoverFighters));
            satellites = static_cast<int>(detail::componentSum(r_, s_, pieces_[k].unit, AbilityKind::LaunchRecoverSatellites));
            drones = static_cast<int>(detail::componentSum(r_, s_, pieces_[k].unit, AbilityKind::LaunchDrones));
        }
        if (fighters + satellites + drones <= 0) continue;
        const uint32_t sIndex = strategyIndex(i);
        const Strategy& S = strategy(e, sIndex);
        for (size_t u = 0; u < pieces_[k].unit.cargo.units.size(); ++u) {
            const UnitStack st = pieces_[k].unit.cargo.units[u];
            if (st.count <= 0 || (kind == Kind::Planet && invaderStack(pieces_[k], u))) continue;
            int* rate = nullptr;
            int group = 1;
            switch (r_.hull(s_.design(st.design).hull).type) {
                case VehicleType::Fighter:
                    rate = &fighters;
                    group = std::max(1, S.fighterLaunchGroup);   // the strategy's group size (confirmed: binary)
                    break;
                case VehicleType::Satellite: {
                    rate = &satellites;
                    group = st.count;   // (inferred) one group per launch
                    // The per-sector satellite cap limits launches (spec 03 §12; inferred to hold in combat).
                    int present = 0;
                    for (const Piece& q : pieces_)
                        if (q.alive && q.owner == e && q.kind == Kind::UnitGroup && q.vtype == VehicleType::Satellite) present += q.unit.count;
                    satellites = std::min(satellites, std::max(0, satelliteCap_ - present));
                    break;
                }
                case VehicleType::Drone:
                    rate = &drones;
                    group = std::max(1, S.dronesPerTarget);
                    break;
                default: break;
            }
            while (rate && *rate > 0 && pieces_[k].unit.cargo.units[u].count > 0) {
                const int count = std::min({group, *rate, pieces_[k].unit.cargo.units[u].count});
                if (!spawnUnit(i, st.design, count, sIndex)) break;
                pieces_[k].unit.cargo.units[u].count -= count;
                *rate -= count;
            }
        }
        refreshStats(i);
    }
}

bool Battle::spawnUnit(int carrier, DesignId design, int count, uint32_t strategyIndex) {
    const auto [cx, cy] = centreOf(carrier);
    const auto [x, y] = freeNear(cx, cy, 1, -1);
    if (x < 0) return false;
    const Design& d = s_.design(design);
    Piece u;
    u.kind = Kind::UnitGroup;
    u.owner = u.startOwner = pieces_[carrier].owner;
    u.unit.owner = u.owner;
    u.unit.design = design;
    u.unit.count = count;
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
    buildWeapons(u);
    const int idx = addPiece(std::move(u));
    occupy(idx);
    refreshPiece(idx);   // launched units get their full movement at once (history 1.55, 1.71)
    event(Ev::Launch, idx, carrier, count);
    return true;
}

// ---- Movement (spec 04 §5, §16) -----------------------------------------------------------------------------

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

std::pair<MoveStrategy, int> Battle::chooseMode(int i, const Strategy& S) {
    const Piece& p = pieces_[i];
    // The secondary strategy applies when the primary is impossible (spec 04 §16).
    for (MoveStrategy m : {S.primary, S.secondary}) {
        switch (m) {
            case MoveStrategy::DontGetHurt: return {m, -1};
            case MoveStrategy::DropTroops:
                if (hasTroops(i))
                    if (const int t = troopTarget(i); t >= 0) return {m, t};
                break;
            case MoveStrategy::BoardEnemyShips:
                if (p.kind == Kind::Vehicle && detail::componentSum(r_, s_, p.unit, AbilityKind::BoardingAttack) > 0)
                    if (const int t = boardTarget(i); t >= 0) return {m, t};
                break;
            case MoveStrategy::Ram:
                if (p.mp > 0) {
                    for (int t : sortedTargets(i, S))
                        if (pieces_[t].kind != Kind::Seeker) return {m, t};
                }
                break;
            default: {
                // Range strategies need a weapon that can engage the target.
                uint8_t reach = 0;
                for (const Weapon& w : p.weapons)
                    if (w.kind() != WeaponKind::PointDefense && instances(i, w) > 0) reach |= w.targets;
                if (reach)
                    for (int t : sortedTargets(i, S))
                        if (maskOf(t) & reach) return {m, t};
                break;
            }
        }
    }
    return {MoveStrategy::DontGetHurt, -1};   // (inferred) nothing else to do: keep out of range
}

int Battle::desiredRange(int i, int t, MoveStrategy m) const {
    int range = 1;
    int maxRange = 0;
    for (const Weapon& w : pieces_[i].weapons)
        if (w.kind() != WeaponKind::PointDefense && (w.targets & maskOf(t)) && instances(i, w) > 0) maxRange = std::max(maxRange, w.reach);
    maxRange = std::clamp(maxRange, 1, kRangeTable);
    switch (m) {
        case MoveStrategy::MaximumRange: range = maxRange; break;
        case MoveStrategy::OptimalRange: {
            // The distance with the best ratio of damage dealt to damage taken.
            int64_t bestOurs = 0, bestTheirs = 0;
            for (int d = 1; d <= maxRange; ++d) {
                const int64_t ours = ourDamage(i, t, d);
                if (ours <= 0) continue;
                const int64_t theirs = pieces_[t].firepower[static_cast<size_t>(d)];
                const int64_t lhs = ours * (bestTheirs + 1), rhs = bestOurs * (theirs + 1);
                if (bestOurs == 0 || lhs > rhs || (lhs == rhs && ours >= bestOurs)) {
                    bestOurs = ours;
                    bestTheirs = theirs;
                    range = d;
                }
            }
            break;
        }
        case MoveStrategy::ShortRange: {
            // 1-3 squares, least exposure.
            int64_t bestTheirs = -1, bestOurs = 0;
            for (int d = 1; d <= 3; ++d) {
                const int64_t ours = ourDamage(i, t, d);
                if (ours <= 0) continue;
                const int64_t theirs = pieces_[t].firepower[static_cast<size_t>(d)];
                if (bestTheirs < 0 || theirs < bestTheirs || (theirs == bestTheirs && ours >= bestOurs)) {
                    bestTheirs = theirs;
                    bestOurs = ours;
                    range = d;
                }
            }
            break;
        }
        default: range = 1; break;
    }
    // Close one extra square on planets (history 1.60).
    if (pieces_[t].kind == Kind::Planet) range = std::max(1, range - 1);
    return range;
}

MovePlan Battle::plan(int i) {
    MovePlan mv;
    const Strategy& S = strategyOf(i);
    std::tie(mv.mode, mv.target) = chooseMode(i, S);
    if (pieces_[i].mp <= 0) return mv;
    bool blocked = false;
    switch (mv.mode) {
        case MoveStrategy::DontGetHurt: mv.path = pathDontGetHurt(i); break;
        case MoveStrategy::DropTroops:
        case MoveStrategy::BoardEnemyShips:
        case MoveStrategy::Ram: mv.path = pathToward(i, mv.target, 1, false, blocked); break;
        default:
            mv.path = pathToward(i, mv.target, desiredRange(i, mv.target, mv.mode),
                                 mv.mode == MoveStrategy::OptimalRange || mv.mode == MoveStrategy::ShortRange, blocked);
            break;
    }
    // A leader blocked in its movement dissolves its group (history 1.03).
    if (blocked && pieces_[i].isLeader) dissolve(i);
    return mv;
}

std::vector<std::pair<int, int>> Battle::pathToward(int i, int t, int range, bool avoidFire, bool& blocked) const {
    // Greedy steps toward the wanted range. When the best next square is taken,
    // up to four other squares around it are tried, then the piece stops (confirmed: binary).
    std::vector<std::pair<int, int>> path;
    int x = pieces_[i].x, y = pieces_[i].y;
    for (int left = pieces_[i].mp; left > 0; --left) {
        const int cur = std::abs(distAt(x, y, t) - range);
        const int64_t curExp = avoidFire ? exposureAt(i, x, y, false) : 0;
        struct Option {
            int score;
            int64_t exposure;
            int seekerGap;   // negated distance to the nearest seeker coming for us
            int order;
            int nx, ny;
        };
        std::vector<Option> options;
        for (size_t d = 0; d < kDirs.size(); ++d) {
            const int nx = x + kDirs[d].first, ny = y + kDirs[d].second;
            if (!onMap(nx, ny)) continue;
            const int score = std::abs(distAt(nx, ny, t) - range);
            const int64_t exp = avoidFire ? exposureAt(i, nx, ny, false) : 0;
            if (score > cur || (score == cur && exp >= curExp)) continue;   // no better than staying
            options.push_back({score, exp, -seekerDistance(i, nx, ny), static_cast<int>(d), nx, ny});
        }
        if (options.empty()) break;
        // Among equally good squares the computer keeps away from seekers aimed at it (history 1.60; inferred tie-break).
        std::sort(options.begin(), options.end(), [](const Option& a, const Option& b) {
            return std::tie(a.score, a.exposure, a.seekerGap, a.order) < std::tie(b.score, b.exposure, b.seekerGap, b.order);
        });
        bool moved = false;
        for (size_t o = 0; o < options.size() && o < 5; ++o) {
            if (!isFree(options[o].nx, options[o].ny, i)) continue;
            bool again = false;   // a square already on the path is not free either
            for (const auto& sq : path) again = again || sq == std::pair{options[o].nx, options[o].ny};
            if (again) continue;
            x = options[o].nx;
            y = options[o].ny;
            path.emplace_back(x, y);
            moved = true;
            break;
        }
        if (!moved) {
            blocked = true;
            break;
        }
    }
    return path;
}

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

std::vector<std::pair<int, int>> Battle::pathDontGetHurt(int i) const {
    // Stay where no enemy can fire on us, even after it moves (spec 04 §14, §16; the look-ahead is inferred).
    // Search every square reachable this turn: least exposure, then room to keep evading (away from the map
    // edge), then farthest from the nearest threat, then fewest steps (inferred).
    const Piece& p = pieces_[i];
    std::vector<std::pair<int, int>> path;
    if (p.mp <= 0 || exposureAt(i, p.x, p.y, true) == 0) return path;
    const int reach = std::min(p.mp, kW);
    std::vector<int> from(static_cast<size_t>(kW * kH), -2);   // -2 unseen, -1 start, else previous square
    std::vector<int> depth(static_cast<size_t>(kW * kH), 0);
    std::vector<int> queue{p.y * kW + p.x};
    from[static_cast<size_t>(queue.front())] = -1;
    auto room = [](int x, int y) { return std::min({x, y, kW - 1 - x, kH - 1 - y, 3}); };
    int best = queue.front();
    std::tuple<int64_t, int, int> bestKey{exposureAt(i, p.x, p.y, true), -room(p.x, p.y), -nearestThreat(i, p.x, p.y)};
    for (size_t head = 0; head < queue.size(); ++head) {
        const int sq = queue[head];
        const int x = sq % kW, y = sq / kW;
        if (head > 0) {
            const std::tuple<int64_t, int, int> key{exposureAt(i, x, y, true), -room(x, y), -nearestThreat(i, x, y)};
            if (key < bestKey) {
                best = sq;
                bestKey = key;
            }
        }
        if (depth[static_cast<size_t>(sq)] >= reach) continue;
        for (const auto& [dx, dy] : kDirs) {
            const int nx = x + dx, ny = y + dy;
            if (!isFree(nx, ny, i)) continue;
            const int next = ny * kW + nx;
            if (from[static_cast<size_t>(next)] != -2) continue;
            from[static_cast<size_t>(next)] = sq;
            depth[static_cast<size_t>(next)] = depth[static_cast<size_t>(sq)] + 1;
            queue.push_back(next);
        }
    }
    for (int sq = best; from[static_cast<size_t>(sq)] != -1; sq = from[static_cast<size_t>(sq)]) path.emplace_back(sq % kW, sq / kW);
    std::reverse(path.begin(), path.end());
    return path;
}

void Battle::followLeader(int i) {
    // Members move toward their formation slot, turned to the leader's facing (spec 03 §10; spec 04 §5, inferred).
    const Piece& leader = pieces_[pieces_[i].leader];
    const auto [dx, dy] = pieces_[i].slotFixed ? std::pair{pieces_[i].slotDx, pieces_[i].slotDy}
                                               : rotateSlot(leader.facing, pieces_[i].slotDx, pieces_[i].slotDy);
    const int tx = std::clamp(leader.x + dx, 0, kW - 1);
    const int ty = std::clamp(leader.y + dy, 0, kH - 1);
    walk(i, pathToSquare(i, tx, ty));
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
    bool blocked = false;
    walk(i, pathToward(i, t, 1, false, blocked));
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
    // The planet the ship was ordered to take, otherwise the most populous (history 1.59).
    for (const Order& o : a.unit.orders)
        if (o.object.valid())
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
    const Piece& a = pieces_[i];
    const Piece& b = pieces_[t];
    const bool drone = a.vtype == VehicleType::Drone && a.kind == Kind::UnitGroup;
    int64_t dealt = xmath::pctTrunc(hitPoints(i), cs_.ramSourcePercent);
    int64_t taken = xmath::pctTrunc(hitPoints(t), cs_.ramTargetPercent);
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
        const Design& d = s_.design(p.unit.design);
        const int copies = p.kind == Kind::UnitGroup ? p.unit.count : 1;
        for (size_t e = 0; e < d.entries.size(); ++e) {
            const ruleset::Component& c = r_.component(d.entries[e].component);
            if (c.weapon.kind != WeaponKind::Warhead || !entryIntact(r_, s_, p.unit, e)) continue;
            const DamageType type = parseDamageType(c.weapon.damageType);
            if (own && !(parseWeaponTargets(c.weapon.targets) & mask)) continue;
            const int64_t value = weaponLargestDamage(r_, d.entries[e]);
            if (own && drone)
                for (int n = 0; n < copies; ++n) droneWarheads.emplace_back(type, value);
            if (warheadExplodes(type)) warheads += value * copies;
        }
    };
    collect(a, true);
    collect(b, false);
    pieces_[i].fired = true;
    event(Ev::Fire, i, t);
    note(std::format("{} rammed {}", label(i), label(t)));
    if (drone) {
        // A drone strikes with each warhead as its own hit, then with its bulk (confirmed: binary).
        for (const auto& [type, value] : droneWarheads) {
            if (!combatant(t)) break;
            event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, value)));
            applyHit(i, t, type, value);
        }
        if (combatant(t)) {
            event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, dealt)));
            applyHit(i, t, DamageType::Normal, dealt);
        }
    } else {
        event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, dealt + warheads)));
        applyHit(i, t, DamageType::Normal, dealt + warheads);
    }
    if (!combatant(i)) return;
    pieces_[i].mp = 0;
    applyHit(combatant(t) ? t : -1, i, DamageType::SkipsAllShields, taken + warheads);
}

void Battle::dropTroops(int i, int t) {
    // A ship drops all its troops on an adjacent hostile colony not contested by
    // another empire; the ground combat is fought at once (confirmed: binary).
    Piece& planet = pieces_[t];
    if (planet.kind != Kind::Planet || !planet.alive || dist(i, t) > 1 || contestedBy(planet, pieces_[i].owner)) return;
    const EmpireId attacker = pieces_[i].owner;
    bool invaded = false;
    for (size_t k = 0; k < planet.unit.cargo.units.size(); ++k) invaded = invaded || (planet.unit.cargo.units[k].count > 0 && invaderStack(planet, k));
    int landed = 0;
    for (UnitStack& st : pieces_[i].unit.cargo.units) {
        if (st.count <= 0 || !isTroopDesign(r_, s_, st.design)) continue;
        const EmpireId troopOwner = s_.design(st.design).owner;
        if (troopOwner != attacker) continue;
        const int n = st.count;
        st.count = 0;
        landed += n;
        Piece& pl = pieces_[t];
        auto dst = std::find_if(pl.unit.cargo.units.begin(), pl.unit.cargo.units.end(), [&](const UnitStack& u) { return u.design == st.design; });
        if (dst != pl.unit.cargo.units.end()) dst->count += n;
        else pl.unit.cargo.units.push_back({st.design, n});
    }
    if (landed <= 0) return;
    std::erase_if(pieces_[i].unit.cargo.units, [](const UnitStack& u) { return u.count <= 0; });
    Piece& pl = pieces_[t];
    if (!invaded || pl.militia < 0) pl.militia = militiaCount(cs_, pl.population);
    troopsLanded_[attacker.value] += landed;
    event(Ev::Launch, i, t, landed);
    note(std::format("{} landed {} troops on {}", label(i), landed, pl.name));

    detail::GroundFight fight;
    fight.attacker = attacker;
    fight.defender = pl.owner;
    fight.cargo = &pl.unit.cargo;
    fight.population = &pl.population;
    fight.militia = &pl.militia;
    for (uint32_t f : pl.facilities) fight.groundDefensePercent += sumValue1(r_.facilityAbilities(f), AbilityKind::PlanetChangeGroundDefense);
    for (const auto& a : s_.galaxy.object(pl.object).abilities)
        if (parseAbilityKind(a.type) == AbilityKind::PlanetChangeGroundDefense) fight.groundDefensePercent += a.number1();
    const detail::GroundOutcome o = detail::fightGround(r_, s_, cs_, fight, rng_);
    groundReports_.push_back(std::format("Ground combat on {}: {} rounds; invaders lost {} of {} troops, defenders {} units and {} militia{}.",
                                         pl.name, o.rounds, o.attackersLost, o.attackersAtStart, o.defendersLost, o.militiaLost,
                                         o.captured ? "; the planet fell" : o.attackersGone ? "; the invasion failed" : ""));
    if (!o.captured) {
        afterDamage(t);
        return;
    }
    // The planet's piece changes sides at once.
    Piece& won = pieces_[t];
    won.owner = attacker;
    won.unit.owner = attacker;
    won.capturedBy = attacker;
    won.militia = -1;
    if (won.isLeader) dissolve(t);
    planetShields(won, false);
    refreshStats(t);
    event(Ev::Captured, t, i, static_cast<int>(attacker.value));
    note(std::format("{} fell to {} troops", won.name, s_.empire(attacker).name));
}

// ---- Round loop --------------------------------------------------------------------------------------------

void Battle::run() {
    // The counter starts at 1 and the battle ends when it reaches the setting:
    // one turn fewer than `Number Of Space Combat Turns` (confirmed: binary).
    for (round_ = 1; round_ < cs_.spaceTurns; ++round_) {
        if (round_ > 1) startRound();
        acted_.assign(pieces_.size(), 0);
        for (size_t k = 0; k < pieces_.size(); ++k)
            if (pieces_[k].kind == Kind::Seeker) acted_[k] = 1;
        {
            // (inferred) An empire landing troops holds fire on planets whose guns are silenced (history 1.43).
            holdFire_.clear();
            for (size_t k = 0; k < pieces_.size(); ++k) {
                const Piece& p = pieces_[k];
                if (!p.alive || p.kind != Kind::Vehicle || !hasTroops(static_cast<int>(k))) continue;
                const Strategy& S = strategyOf(static_cast<int>(k));
                if (S.primary == MoveStrategy::DropTroops || S.secondary == MoveStrategy::DropTroops) holdFire_[p.owner.value] = true;
            }
        }
        for (EmpireId e : order_) {
            if (!hasPieces(e)) continue;
            phase(e);
            if (over()) return;
        }
    }
    round_ = std::max(1, cs_.spaceTurns - 1);
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

    // Per-empire losses for logs, mood and design statistics.
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
        } else if (p.captured) {
            own.taken.push_back(p.name);
            ++own.shipsLost;
            ++s_.design(p.unit.design).lost;   // (inferred) a captured ship counts as lost
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

    // Planets: cargo, population, facilities, plague, conditions, lost and captured colonies.
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
            ctx_.mood(p.startOwner, "Any Planet Lost");
            ctx_.log(p.startOwner, LogCategory::Combat, std::format("{} lost", p.name),
                     std::format("Our colony on {} was wiped out in the battle at {}.", p.name, sector), where_);
            s_.colonies[p.object.index()].reset();
            continue;
        }
        std::erase_if(p.unit.cargo.units, [](const UnitStack& u) { return u.count <= 0; });
        c->cargo = p.unit.cargo;
        std::erase_if(p.population, [](const PopulationGroup& g) { return g.millions <= 0; });
        c->population = p.population;
        c->facilities = p.facilities;
        c->militia = p.militia;
        c->plagueLevel = std::max(c->plagueLevel, p.plague);
        if (p.conditionsLost > 0) {
            SpaceObject& obj = s_.galaxy.object(p.object);
            obj.conditions = static_cast<int>(std::max<int64_t>(0, obj.conditions - p.conditionsLost));
        }
        if (p.capturedBy.valid() && c->owner != p.capturedBy) detail::capturePlanet(ctx_, *c, p.capturedBy);
        if (invaders(r_, s_, *c).empty()) c->militia = -1;
    }

    // Carriers recover the fighter and satellite groups they launched in this
    // battle, ships first, then planets; the rest stay in space (confirmed: binary).
    struct Spawn {
        EmpireId owner;
        DesignId design;
        int count = 0;
        VehicleId target;
    };
    std::vector<Spawn> spawns;
    std::vector<int> left(pieces_.size(), 0);
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (pieces_[k].kind == Kind::UnitGroup && pieces_[k].alive) left[k] = pieces_[k].unit.count;
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
                if (u.kind != Kind::UnitGroup || !u.launched || u.carrier != static_cast<int>(c) || left[k] <= 0 || u.owner != h.owner) continue;
                if (u.vtype != VehicleType::Fighter && u.vtype != VehicleType::Satellite) continue;
                const int64_t tonnage = std::max(1, r_.hull(s_.design(u.unit.design).hull).tonnage);
                const int n = static_cast<int>(std::min<int64_t>(left[k], std::max<int64_t>(0, room) / tonnage));
                if (n <= 0) continue;
                auto dst = std::find_if(cargo->units.begin(), cargo->units.end(), [&](const UnitStack& s) { return s.design == u.unit.design; });
                if (dst != cargo->units.end()) dst->count += n;
                else cargo->units.push_back({u.unit.design, n});
                left[k] -= n;
                room -= int64_t{n} * tonnage;
            }
        }
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& u = pieces_[k];
        if (u.kind != Kind::UnitGroup) continue;
        if (u.launched) {
            if (u.alive && left[k] > 0) {
                const VehicleId target = u.droneTarget >= 0 && pieces_[u.droneTarget].alive ? pieces_[u.droneTarget].source : VehicleId{};
                spawns.push_back({u.owner, u.unit.design, left[k], target});
            }
        } else if (Vehicle* v = s_.vehicle(u.source); v && u.alive) {
            v->count = u.unit.count;
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

    // Each participant learns the designs it fought.
    for (EmpireId e : empires_) {
        std::vector<DesignId>& seen = s_.empire(e).knowledge.seenDesigns;
        for (const Piece& p : pieces_) {
            if (p.startOwner == e || p.kind == Kind::Seeker || p.kind == Kind::Planet || p.kind == Kind::Obstacle || !p.unit.design.valid()) continue;
            if (s_.design(p.unit.design).owner == e) continue;
            seen.push_back(p.unit.design);
        }
        std::sort(seen.begin(), seen.end());
        seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    }

    if (!cs_.createReplay) rec_.events.clear();
    s_.combats.push_back(std::move(rec_));
    if (std::find(ctx_.battleSites.begin(), ctx_.battleSites.end(), where_) == ctx_.battleSites.end()) ctx_.battleSites.push_back(where_);

    // Last: new vehicles for units left in space (invalidates vehicle references).
    for (const Spawn& sp : spawns) {
        const Design& d = s_.design(sp.design);
        Vehicle v;
        v.owner = sp.owner;
        v.design = sp.design;
        v.name = d.name;
        v.location = where_;
        v.count = sp.count;
        v.damage.assign(d.entries.size(), 0);
        v.supply = vehicleSupplyCapacity(r_, s_, v);
        v.builtTurn = s_.turn;
        v.targetVehicle = sp.target;
        s_.addVehicle(std::move(v));
    }
}

} // namespace

void resolveSpaceCombat(TurnContext& ctx, Location where, std::span<const VehicleId> entering) {
    Rng rng = ctx.state.rng.fork();
    // Mines strike first, then the battle check runs (confirmed: binary).
    detail::resolveMines(ctx, where, entering, rng);
    Battle battle(ctx, where, rng);
    if (!battle.setup()) return;
    battle.run();
    battle.finish();
}

void resolveSpaceCombat(TurnContext& ctx, Location where) { resolveSpaceCombat(ctx, where, {}); }

} // namespace opense4::game::combat
