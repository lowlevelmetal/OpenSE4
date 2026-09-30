// Strategic space combat (docs/spec/04 §3-§12, §14-§16): the combat map,
// starting positions and formations, strategy-driven movement and targeting,
// direct fire, seekers, point defense, launched units, planets, boarding,
// ramming, troop drops, and the battle's results, replay record and logs.
//
// Every piece works on a copy of its vehicle or colony; the results are
// written back once the battle ends. Randomness comes from a fork of
// GameState::rng; pieces act in a stable order.

#include "game/combat.hpp"

#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <format>
#include <map>
#include <tuple>

namespace opense4::game::combat {

namespace {

using detail::HitOutcome;
using detail::ShieldState;
using ruleset::VehicleType;
using ruleset::WeaponKind;
using Kind = CombatPiece::Kind;
using Ev = CombatEvent::Kind;

constexpr int kGrid = kCombatGridSize;
constexpr int kCentre = kGrid / 2 - 1;          // top-left square of the first planet
constexpr int kStartRadius = 12;                 // (inferred) attackers start this far from the centre
constexpr int kPlanetTargets = 10;               // a planet engages up to 10 targets (spec 04 §6)
constexpr int kRangeTable = 64;                  // longest range the movement logic considers
constexpr int kShipKillExperience = 5;           // (inferred) per ship, base or planet destroyed or captured
constexpr int kUnitKillExperience = 1;           // (inferred) fighters, satellites and drones are worth less
constexpr int kBattleExperience = 1;             // (inferred) for firing in a battle
constexpr int64_t kPlanetSizeRank = 1'000'000;   // planets rank as the largest targets

constexpr std::array<std::pair<int, int>, 8> kDirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}}};
// Start anchors of the empires that hold no planet here, around the centre (inferred).
constexpr std::array<std::pair<int, int>, 8> kRing{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}, {1, -1}, {-1, 1}}};

int sgn(int v) { return (v > 0) - (v < 0); }
int gap(int a0, int aSize, int b0, int bSize) { return std::max({0, a0 - (b0 + bSize - 1), b0 - (a0 + aSize - 1)}); }
bool onGrid(int x, int y) { return x >= 0 && y >= 0 && x < kGrid && y < kGrid; }

struct Weapon {
    size_t entry = 0;        // design entry index in the carrying design
    DesignEntry de;
    const ruleset::Component* comp = nullptr;
    DamageType type = DamageType::Normal;
    uint8_t targets = 0;
    int reloadRate = 1;
    int reload = 0;          // combat turns until ready (0 = ready)
    int maxRange = 0;
    int multiplicity = 1;    // identical entries fired together (unit groups, platforms)
    int stack = -1;          // planets: the platform stack in the planet's cargo

    WeaponKind kind() const { return comp->weapon.kind; }
};

struct Piece {
    Kind kind = Kind::Vehicle;
    EmpireId owner, startOwner;
    VehicleId source;                 // the vehicle this piece stands for (invalid for launched units)
    ObjectId planet;
    Vehicle unit;                     // working copy: one ship, or the front member of a unit group
    VehicleType vtype = VehicleType::Ship;
    std::string name;
    int x = 0, y = 0, size = 1;
    bool alive = true;
    bool mothballed = false;
    bool usesSupply = false;
    ShieldState sh;
    int mp = 0;
    int reach = 0;                    // movement points at the start of the combat turn
    std::vector<Weapon> weapons;
    std::vector<int> engaged;         // distinct targets engaged this combat turn
    int budget = 1;
    int offense = 0, defense = 0;
    bool alwaysHit = false;
    bool armed = false;
    int64_t strength = 0;
    std::array<int64_t, kRangeTable + 1> firepower{};
    TargetCategory category = TargetCategory::Ships;
    int experience = 0;
    uint32_t designStrategy = 0, fleetStrategy = 0;
    int leader = -1;
    bool isLeader = false;
    int slotDx = 0, slotDy = 0;
    // Seekers.
    int seekTarget = -1, launcher = -1, travelled = 0, speed = 0, hp = 0, seekCount = 1, seekBonus = 0;
    Weapon seekWeapon;
    // Launched units and drones.
    int carrier = -1;
    bool launched = false;
    int droneTarget = -1;
    // Planets.
    std::vector<PopulationGroup> population;
    std::vector<std::vector<int>> stackDamage;   // front-unit damage per cargo stack
    int64_t popDamage = 0, popKilled = 0, popStart = 0, hpStart = 0;
    bool colonyLost = false;
    int plague = 0, conditionsLost = 0;
    std::vector<AbilityKind> facilityKills;
    // Bookkeeping.
    bool fired = false, damaged = false, captured = false, pushed = false;
    int shipKills = 0, unitKills = 0, unitsLost = 0, startCount = 1, dropsThisTurn = 0;
};

enum class Result : uint8_t { Win, Loss, Stalemate };

class Battle {
public:
    Battle(TurnContext& ctx, Location where, Rng& rng)
        : r_(ctx.rules), s_(ctx.state), ctx_(ctx), where_(where), rng_(rng), cs_(loadSettings(ctx.rules)) {
        occ_.fill(-1);
    }

    bool setup();
    void run();
    void finish();

private:
    // ---- Setup.
    void addVehiclePiece(const Vehicle& v);
    void addPlanetPiece(const Colony& c);
    Weapon makeWeapon(const DesignEntry& de, size_t entry) const;
    void buildWeapons(Piece& p) const;
    void place();
    void placeEmpire(EmpireId e, int ax, int ay, int fx, int fy, std::vector<char>& placed);
    std::pair<int, int> freeNear(int cx, int cy) const;
    void occupy(int i);
    void vacate(int i);
    int addPiece(Piece p);

    // ---- Per-round state.
    void startRound();
    void refreshPiece(int i);
    void refreshCombatValues(int i);
    int computeMp(int i) const;
    std::vector<EmpireId> phaseOrder();
    bool hasPieces(EmpireId e) const;
    bool over() const;
    bool anyoneCanAct() const;
    const Strategy& strategy(EmpireId e, uint32_t index) const;
    uint32_t strategyIndex(int i) const;
    const Strategy& strategyOf(int i) const { return strategy(pieces_[i].owner, strategyIndex(i)); }

    // ---- Queries.
    int dist(int a, int b) const;
    int distAt(int x, int y, int b) const;
    bool isFree(int x, int y, int self) const;
    uint8_t maskOf(int i) const;
    TargetCategory categoryFor(int j, EmpireId viewer) const;
    int damagePercent(int j) const;
    int64_t sizeOf(int j) const;
    int remainingOf(int j) const;
    int64_t planetHp(const Piece& p) const;
    bool invaderStack(const Piece& p, size_t k) const;
    bool hasSupply(int i) const;
    bool weaponUsable(int i, const Weapon& w) const;
    int shotCount(int i, const Weapon& w) const;
    bool canAffect(DamageType type, int t) const;
    int damageBonus(EmpireId e) const;
    int hitChance(int i, const Weapon& w, int t, int d) const;
    int64_t exposureAt(int i, int x, int y, bool reach) const;
    int nearestThreat(int i, int x, int y) const;
    int64_t ourDamage(int i, int t, int d) const;
    bool hasTroops(int i) const;
    bool contestedBy(const Piece& planet, EmpireId e) const;

    // ---- Targeting.
    std::vector<int> sortedTargets(int i, const Strategy& S);
    int pickTarget(int i, const Weapon& w, const std::vector<int>& targets);
    int64_t incomingSeekerDamage(int t) const;

    // ---- Actions.
    void act(int i);
    void fire(int i);
    void shoot(int i, size_t wi, int t);
    void launchSeeker(int i, const Weapon& w, int t, int count);
    void deliver(int att, int t, DamageType type, int damage, int table, int shots);
    void applyHit(int att, int t, DamageType type, int damage);
    void groupHit(int att, int t, DamageType type, int damage);
    void planetHit(int att, int t, DamageType type, int damage);
    int damageStacks(int att, int t, int damage, bool platforms);
    void populationDamage(int att, int t, int damage);
    void special(int att, int t, DamageType type, int value);
    void forcedMove(int t, int att, int squares, bool push);
    void kill(int t, int att, bool credit);
    void creditKill(int att, bool ship);
    void capture(int t, EmpireId newOwner, int capturer, bool boarding);
    void dissolve(int leader);
    void pdReact(int mover);
    void moveSeekers(EmpireId e);
    void launchUnits(EmpireId e);
    bool spawnUnit(int carrier, DesignId design, int count, uint32_t strategyIndex);

    // ---- Movement.
    void moveByStrategy(int i);
    std::pair<MoveStrategy, int> chooseMode(int i, const Strategy& S);
    int desiredRange(int i, int t, MoveStrategy m) const;
    void moveToward(int i, int t, int range, bool avoidFire);
    void moveDontGetHurt(int i);
    void followLeader(int i);
    void droneMove(int i);
    void moveTo(int i, int x, int y);
    void step(int i, int x, int y);
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
    std::vector<EmpireId> defenders_;
    std::array<int, kGrid * kGrid> occ_{};
    mutable std::map<uint32_t, std::vector<Strategy>> strategies_;   // parsed lazily
    std::map<uint32_t, bool> holdFire_;
    std::map<uint32_t, int> troopsLanded_;
    int round_ = 0;
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
    w.maxRange = weaponMaxRange(r_, de);
    return w;
}

void Battle::buildWeapons(Piece& p) const {
    p.weapons.clear();
    if (p.mothballed) return;
    const Design& d = s_.design(p.unit.design);
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const ruleset::Component& c = r_.component(d.entries[i].component);
        if (!c.isWeapon() || c.weapon.kind == WeaponKind::Warhead) continue;
        if (p.kind == Kind::UnitGroup) {
            // A group fires one combined shot per distinct weapon (spec 04 §8).
            auto same = std::find_if(p.weapons.begin(), p.weapons.end(), [&](const Weapon& w) { return w.de == d.entries[i]; });
            if (same != p.weapons.end()) {
                ++same->multiplicity;
                continue;
            }
        }
        p.weapons.push_back(makeWeapon(d.entries[i], i));
    }
}

int Battle::addPiece(Piece p) {
    const int i = static_cast<int>(pieces_.size());
    pieces_.push_back(std::move(p));
    acted_.push_back(pieces_.back().kind == Kind::Seeker ? 1 : 0);
    const Piece& q = pieces_.back();
    rec_.pieces.push_back(CombatPiece{q.kind, q.owner, q.source, q.planet, q.kind == Kind::Planet ? DesignId{} : q.unit.design, q.name,
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
    p.name = v.name;
    p.mothballed = v.status == VehicleStatus::Mothballed;
    p.designStrategy = d.owner == v.owner ? d.strategy : 0;
    if (const Fleet* f = s_.fleet(v.fleet); f && f->owner == v.owner) p.fleetStrategy = f->strategy;
    p.startCount = v.count;
    p.experience = v.experience;
    p.usesSupply = detail::needsSupply(r_, s_, p.unit);
    buildWeapons(p);
    // Shields start full (spec 04 §3). Shield Modifier - System helps the owner's ships.
    p.sh.bonus = (p.kind == Kind::Vehicle ? detail::systemModifier(r_, s_, v.owner, where_.system, AbilityKind::ShieldModifierSystem) : 0) -
                 disruption_;
    detail::refreshShields(r_, s_, p.unit, p.sh, true);
    pieces_.push_back(std::move(p));
}

void Battle::addPlanetPiece(const Colony& c) {
    Piece p;
    p.kind = Kind::Planet;
    p.owner = p.startOwner = c.owner;
    p.planet = c.planet;
    p.unit.owner = c.owner;
    p.unit.location = where_;
    p.unit.cargo = c.cargo;
    p.name = s_.galaxy.object(c.planet).name;
    p.size = 2;
    p.population = c.population;
    p.popStart = c.totalPopulation();
    for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
        const UnitStack& st = p.unit.cargo.units[k];
        const Design& d = s_.design(st.design);
        p.stackDamage.emplace_back(d.entries.size(), 0);
        if (st.count <= 0 || r_.hull(d.hull).type != VehicleType::WeaponPlatform || invaderStack(p, k)) continue;
        // The planet's guns are its weapon platforms (spec 04 §11).
        for (size_t i = 0; i < d.entries.size(); ++i) {
            const ruleset::Component& comp = r_.component(d.entries[i].component);
            if (!comp.isWeapon() || comp.weapon.kind == WeaponKind::Warhead) continue;
            auto same = std::find_if(p.weapons.begin(), p.weapons.end(),
                                     [&](const Weapon& w) { return w.stack == static_cast<int>(k) && w.de == d.entries[i]; });
            if (same != p.weapons.end()) {
                ++same->multiplicity;
                continue;
            }
            Weapon w = makeWeapon(d.entries[i], i);
            w.stack = static_cast<int>(k);
            p.weapons.push_back(w);
        }
    }
    // Facilities do not work without population. (inferred) Platform shield parts do not add (spec 04 §19 Q16).
    int64_t shields = 0;
    if (p.popStart > 0) shields = sumValue1(colonyAbilities(r_, s_, c), AbilityKind::PlanetShieldGeneration);
    p.sh.maxNormal = static_cast<int>(std::max<int64_t>(0, shields - disruption_));
    p.sh.normal = p.sh.maxNormal;
    p.hpStart = planetHp(p);
    pieces_.push_back(std::move(p));
}

bool Battle::setup() {
    if (!where_.system.valid() || where_.system.index() >= s_.galaxy.systems.size()) return false;
    interference_ = detail::sensorInterference(s_, where_);
    disruption_ = detail::shieldDisruption(s_, where_);

    const detail::Forces forces = detail::battleForces(r_, s_, where_);
    if (forces.empires.size() < 2) return false;
    empires_ = forces.empires;
    // Everyone in the battle is decloaked until it ends (history 1.28).
    for (EmpireId e : empires_) {
        for (ObjectId o : forces.colonies)
            if (s_.colony(o)->owner == e) addPlanetPiece(*s_.colony(o));
        for (VehicleId id : forces.vehicles)
            if (s_.vehicle(id)->owner == e) addVehiclePiece(*s_.vehicle(id));
    }
    // (inferred) The defenders are the planet owners (spec 04 §4, §19 Q3).
    for (ObjectId o : forces.colonies) {
        const EmpireId e = s_.colony(o)->owner;
        if (std::find(defenders_.begin(), defenders_.end(), e) == defenders_.end()) defenders_.push_back(e);
    }
    // A Neural Combat Net fights at the best crew experience among its empire's ships here.
    std::map<uint32_t, int> best;
    for (const Piece& p : pieces_)
        if (p.kind == Kind::Vehicle) best[p.owner.value] = std::max(best[p.owner.value], p.unit.experience);
    for (Piece& p : pieces_)
        if (p.kind == Kind::Vehicle && detail::hasIntactComponent(r_, s_, p.unit, AbilityKind::CombatBestExperience))
            p.experience = best[p.owner.value];
    // Drones keep the target they were given.
    for (Piece& p : pieces_)
        if (p.vtype == VehicleType::Drone && p.unit.targetVehicle.valid())
            for (size_t j = 0; j < pieces_.size(); ++j)
                if (pieces_[j].source == p.unit.targetVehicle) p.droneTarget = static_cast<int>(j);

    for (size_t i = 0; i < pieces_.size(); ++i) refreshPiece(static_cast<int>(i));
    place();

    std::vector<Piece> built = std::move(pieces_);
    pieces_.clear();
    acted_.clear();
    for (Piece& p : built) addPiece(std::move(p));

    rec_.turn = s_.turn;
    rec_.location = where_;
    rec_.participants = empires_;
    std::string names;
    for (EmpireId e : empires_) names += (names.empty() ? "" : ", ") + s_.empire(e).name;
    rec_.summary.push_back(std::format("Battle at {} between {}.", detail::sectorName(s_, where_), names));
    return true;
}

std::pair<int, int> Battle::freeNear(int cx, int cy) const {
    cx = std::clamp(cx, 0, kGrid - 1);
    cy = std::clamp(cy, 0, kGrid - 1);
    for (int rad = 0; rad < kGrid; ++rad)
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != rad) continue;
                const int x = cx + dx, y = cy + dy;
                if (onGrid(x, y) && occ_[static_cast<size_t>(y * kGrid + x)] < 0) return {x, y};
            }
    return {-1, -1};
}

void Battle::occupy(int i) {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Seeker) return;
    for (int dy = 0; dy < p.size; ++dy)
        for (int dx = 0; dx < p.size; ++dx)
            if (onGrid(p.x + dx, p.y + dy)) occ_[static_cast<size_t>((p.y + dy) * kGrid + p.x + dx)] = i;
}

void Battle::vacate(int i) {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Seeker) return;
    for (int dy = 0; dy < p.size; ++dy)
        for (int dx = 0; dx < p.size; ++dx)
            if (onGrid(p.x + dx, p.y + dy) && occ_[static_cast<size_t>((p.y + dy) * kGrid + p.x + dx)] == i)
                occ_[static_cast<size_t>((p.y + dy) * kGrid + p.x + dx)] = -1;
}

void Battle::place() {
    // Planets take the centre (inferred layout, spec 04 §19 Q1), 2x2 squares each (history 1.60).
    int planets = 0;
    for (size_t i = 0; i < pieces_.size(); ++i) {
        if (pieces_[i].kind != Kind::Planet) continue;
        const int k = planets++;
        const int offset = ((k + 1) / 2) * 3 * (k % 2 ? 1 : -1);
        pieces_[i].x = std::clamp(kCentre + offset, 0, kGrid - 2);
        pieces_[i].y = kCentre;
        occupy(static_cast<int>(i));
    }
    // Defenders (planet owners) gather around their planet; everyone else starts on a ring.
    std::vector<EmpireId> order = defenders_;
    for (EmpireId e : empires_)
        if (std::find(order.begin(), order.end(), e) == order.end()) order.push_back(e);
    std::vector<char> placed(pieces_.size(), 0);
    for (size_t i = 0; i < pieces_.size(); ++i) placed[i] = pieces_[i].kind == Kind::Planet;
    int ring = 0;
    for (EmpireId e : order) {
        int ax = kCentre, ay = kCentre, fx = -1, fy = 0;
        auto own = std::find_if(pieces_.begin(), pieces_.end(), [&](const Piece& p) { return p.kind == Kind::Planet && p.owner == e; });
        if (own != pieces_.end()) {
            ax = own->x;
            ay = own->y;
        } else {
            const auto [dx, dy] = kRing[static_cast<size_t>(ring % 8)];
            const int radius = std::max(3, kStartRadius - 4 * (ring / 8));
            ++ring;
            ax = kCentre + dx * radius;
            ay = kCentre + dy * radius;
            // Face the centre along the dominant axis.
            if (std::abs(dx) >= std::abs(dy)) {
                fx = -sgn(dx);
                fy = 0;
            } else {
                fx = 0;
                fy = -sgn(dy);
            }
        }
        placeEmpire(e, ax, ay, fx, fy, placed);
    }
}

void Battle::placeEmpire(EmpireId e, int ax, int ay, int fx, int fy, std::vector<char>& placed) {
    auto put = [&](int i, int x, int y) {
        const auto [px, py] = freeNear(x, y);
        pieces_[i].x = px < 0 ? 0 : px;
        pieces_[i].y = py < 0 ? 0 : py;
        occupy(i);
        placed[static_cast<size_t>(i)] = 1;
    };
    // Rotate a formation offset (drawn facing up, toward lower y) to the facing.
    auto rotate = [&](int dx, int dy) -> std::pair<int, int> {
        if (fx == 1) return {-dy, dx};
        if (fx == -1) return {dy, -dx};
        if (fy == 1) return {-dx, -dy};
        return {dx, dy};
    };
    // Each fleet starts in its formation as one combat group (spec 04 §3).
    std::vector<FleetId> fleets;
    for (const Piece& p : pieces_)
        if (p.owner == e && p.kind != Kind::Planet && p.unit.fleet.valid() &&
            std::find(fleets.begin(), fleets.end(), p.unit.fleet) == fleets.end())
            fleets.push_back(p.unit.fleet);
    for (FleetId fid : fleets) {
        const Fleet* fleet = s_.fleet(fid);
        if (!fleet) continue;
        std::vector<int> members;
        for (VehicleId m : fleet->members)
            for (size_t i = 0; i < pieces_.size(); ++i)
                if (pieces_[i].source == m && pieces_[i].owner == e && !placed[i]) members.push_back(static_cast<int>(i));
        if (members.empty()) continue;
        int leader = members.front();
        for (int m : members)
            if (pieces_[m].source == fleet->leader) leader = m;
        put(leader, ax, ay);
        const Strategy& S = strategy(e, fleet->strategy);
        const ruleset::Formation* formation =
            fleet->formation < r_.data().formations.size() ? &r_.data().formations[fleet->formation] : nullptr;
        size_t slot = 0;
        for (int m : members) {
            if (m == leader) continue;
            if (S.breakFormation[static_cast<size_t>(pieces_[m].category)]) continue;   // placed on its own below
            int dx = 0, dy = 0;
            bool fixed = false;
            if (formation && slot < formation->positions.size()) {
                const auto& pos = formation->positions[slot++];
                std::tie(dx, dy) = rotate(pos.x - formation->leader.x, pos.y - formation->leader.y);
                fixed = true;
            }
            put(m, pieces_[leader].x + dx, pieces_[leader].y + dy);
            if (!fixed) {
                dx = pieces_[m].x - pieces_[leader].x;
                dy = pieces_[m].y - pieces_[leader].y;
            }
            pieces_[m].leader = leader;
            pieces_[m].slotDx = dx;
            pieces_[m].slotDy = dy;
            pieces_[leader].isLeader = true;
        }
    }
    for (size_t i = 0; i < pieces_.size(); ++i)
        if (pieces_[i].owner == e && !placed[i]) put(static_cast<int>(i), ax, ay);
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

int Battle::computeMp(int i) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet || p.kind == Kind::Seeker || p.mothballed) return 0;
    Vehicle tmp = p.unit;
    if (!p.usesSupply) tmp.supply = std::max<int64_t>(tmp.supply, 1);
    int mp = vehicleMaxMovement(r_, s_, tmp);
    if (mp > 0) {
        const bool starved = p.usesSupply && p.unit.supply <= 0;
        if (!starved && vehicleHasControl(r_, s_, tmp))
            mp += static_cast<int>(detail::componentBest(r_, s_, p.unit, AbilityKind::CombatMovement));
    }
    return mp;
}

void Battle::refreshCombatValues(int i) {
    Piece& p = pieces_[i];
    const int sys = detail::systemModifier(r_, s_, p.owner, where_.system, AbilityKind::CombatModifierSystem);
    if (p.kind == Kind::Planet) {
        const Empire& e = s_.empire(p.owner);
        p.offense = cs_.planetOffense + detail::racialOffense(r_, e) + sys;
        p.defense = cs_.planetDefense + detail::racialDefense(r_, e) + sys;
        return;
    }
    if (p.kind == Kind::Seeker) {
        p.offense = 0;
        p.defense = cs_.seekerDefense;
        return;
    }
    const bool group = p.kind == Kind::UnitGroup;
    p.offense = detail::vehicleOffense(r_, s_, p.unit, group, p.experience) + sys;
    p.defense = detail::vehicleDefense(r_, s_, p.unit, group, p.experience) + sys;
    p.alwaysHit = detail::hasIntactComponent(r_, s_, p.unit, AbilityKind::WeaponsAlwaysHit);
}

void Battle::refreshPiece(int i) {
    Piece& p = pieces_[i];
    if (!p.alive || p.kind == Kind::Seeker) return;
    p.engaged.clear();
    p.pushed = false;
    p.dropsThisTurn = 0;
    p.mp = computeMp(i);
    p.reach = p.mp;
    // Target budget (spec 04 §6).
    if (p.kind == Kind::Planet) p.budget = kPlanetTargets;
    else if (p.vtype == VehicleType::Fighter || p.vtype == VehicleType::Drone) p.budget = 1;
    else {
        const int multiplex = static_cast<int>(std::max<int64_t>(1, detail::componentBest(r_, s_, p.unit, AbilityKind::MultiplexTracking)));
        p.budget = p.vtype == VehicleType::Satellite ? std::min(p.unit.count, multiplex) : multiplex;
    }
    refreshCombatValues(i);
    // Firepower by range, used to judge threats and ranges.
    p.firepower.fill(0);
    p.strength = 0;
    p.armed = false;
    for (const Weapon& w : p.weapons) {
        if (!weaponUsable(i, w)) continue;
        p.armed = true;
        const int shots = shotCount(i, w);
        int64_t best = 0;
        for (int d = 1; d <= kRangeTable; ++d) {
            const int64_t dmg = int64_t{weaponDamageAtRange(r_, w.de, d)} * shots / w.reloadRate;
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

void Battle::startRound() {
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const int i = static_cast<int>(k);
        Piece& p = pieces_[k];
        if (!p.alive || p.kind == Kind::Seeker) continue;
        for (Weapon& w : p.weapons) w.reload = std::max(0, w.reload - 1);
        if (round_ > 1 && p.kind != Kind::Planet && !p.mothballed) {
            // Shield regeneration (normal pool first, inferred), organic armor after it was hit (history 1.80).
            int regen = static_cast<int>(detail::componentSum(r_, s_, p.unit, AbilityKind::ShieldRegeneration) +
                                         detail::hullSum(r_, s_.design(p.unit.design), AbilityKind::ShieldRegeneration));
            const int n = std::min(regen, std::max(0, p.sh.maxNormal - p.sh.normal));
            p.sh.normal += n;
            regen -= n;
            p.sh.phased += std::min(regen, std::max(0, p.sh.maxPhased - p.sh.phased));
            if (p.damaged) {
                const Design& d = s_.design(p.unit.design);
                for (size_t e = 0; e < d.entries.size(); ++e) {
                    const auto ab = r_.componentAbilities(d.entries[e].component);
                    if (hasAbility(ab, AbilityKind::ArmorRegeneration) && entryIntact(r_, s_, p.unit, e))
                        p.unit.damage[e] = std::max(0, p.unit.damage[e] - static_cast<int>(sumValue1(ab, AbilityKind::ArmorRegeneration)));
                }
            }
        }
        refreshPiece(i);
    }
    // (inferred) An empire landing troops holds fire on planets whose guns are silenced (history 1.43).
    holdFire_.clear();
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& p = pieces_[k];
        if (!p.alive || p.kind != Kind::Vehicle || !hasTroops(static_cast<int>(k))) continue;
        const Strategy& S = strategyOf(static_cast<int>(k));
        if (S.primary == MoveStrategy::DropTroops || S.secondary == MoveStrategy::DropTroops) holdFire_[p.owner.value] = true;
    }
}

std::vector<EmpireId> Battle::phaseOrder() {
    // Random order each turn, but defenders always move before attackers (history 1.47).
    std::vector<EmpireId> def, att;
    for (EmpireId e : empires_) {
        if (!hasPieces(e)) continue;
        (std::find(defenders_.begin(), defenders_.end(), e) != defenders_.end() ? def : att).push_back(e);
    }
    rng_.shuffle(def);
    rng_.shuffle(att);
    def.insert(def.end(), att.begin(), att.end());
    return def;
}

bool Battle::hasPieces(EmpireId e) const {
    return std::any_of(pieces_.begin(), pieces_.end(), [&](const Piece& p) { return p.alive && p.owner == e && p.kind != Kind::Seeker; });
}

bool Battle::over() const {
    for (EmpireId a : empires_)
        for (EmpireId b : empires_)
            if (a < b && detail::enemies(s_, a, b) && hasPieces(a) && hasPieces(b)) return false;
    return true;
}

bool Battle::anyoneCanAct() const {
    // (inferred) Once nobody can hurt anybody, the remaining turns change nothing but the replay.
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& p = pieces_[k];
        const int i = static_cast<int>(k);
        if (!p.alive) continue;
        if (p.kind == Kind::Seeker) return true;
        if (p.mothballed) continue;
        if (p.armed && hasSupply(i)) return true;
        if (p.vtype == VehicleType::Drone && p.kind != Kind::Planet) return true;
        for (size_t u = 0; u < p.unit.cargo.units.size(); ++u) {
            const UnitStack& st = p.unit.cargo.units[u];
            if (st.count <= 0 || (p.kind == Kind::Planet && invaderStack(p, u))) continue;
            const VehicleType t = r_.hull(s_.design(st.design).hull).type;
            const bool planet = p.kind == Kind::Planet;
            auto launches = [&](AbilityKind ability) { return planet || detail::componentSum(r_, s_, p.unit, ability) > 0; };
            if (t == VehicleType::Fighter && launches(AbilityKind::LaunchRecoverFighters)) return true;
            if (t == VehicleType::Satellite && launches(AbilityKind::LaunchRecoverSatellites)) return true;
            if (t == VehicleType::Drone && launches(AbilityKind::LaunchDrones)) return true;
            if (t == VehicleType::Troop && p.kind == Kind::Vehicle && troopTarget(i) >= 0) return true;
        }
        if (p.kind == Kind::Vehicle) {
            if (detail::componentSum(r_, s_, p.unit, AbilityKind::BoardingAttack) > 0) return true;
            const Strategy& S = strategyOf(i);
            if ((S.primary == MoveStrategy::Ram || S.secondary == MoveStrategy::Ram) && p.reach > 0) return true;
        }
    }
    return false;
}

// ---- Queries ----------------------------------------------------------------------------------------

int Battle::dist(int a, int b) const {
    const Piece& p = pieces_[a];
    const Piece& q = pieces_[b];
    return std::max(gap(p.x, p.size, q.x, q.size), gap(p.y, p.size, q.y, q.size));
}

int Battle::distAt(int x, int y, int b) const {
    const Piece& q = pieces_[b];
    return std::max(gap(x, 1, q.x, q.size), gap(y, 1, q.y, q.size));
}

bool Battle::isFree(int x, int y, int self) const {
    if (!onGrid(x, y)) return false;
    const int o = occ_[static_cast<size_t>(y * kGrid + x)];
    return o < 0 || o == self;
}

uint8_t Battle::maskOf(int i) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet) return kTargetPlanets;
    if (p.kind == Kind::Seeker) return kTargetSeekers;
    return targetMaskOf(p.vtype);
}

TargetCategory Battle::categoryFor(int j, EmpireId viewer) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Seeker)
        return p.seekTarget >= 0 && pieces_[p.seekTarget].owner == viewer ? TargetCategory::SeekersOnUs : TargetCategory::SeekersOnOthers;
    return p.category;
}

int64_t Battle::planetHp(const Piece& p) const {
    int64_t hp = 0;
    for (const PopulationGroup& g : p.population) hp += g.millions * cs_.damagePerPopulation;
    for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
        const UnitStack& st = p.unit.cargo.units[k];
        if (st.count <= 0 || invaderStack(p, k)) continue;
        const Design& d = s_.design(st.design);
        int per = 0, front = 0;
        for (size_t e = 0; e < d.entries.size(); ++e) {
            const int structure = entryStructure(r_, d, e);
            per += structure;
            front += std::max(0, structure - (e < p.stackDamage[k].size() ? p.stackDamage[k][e] : 0));
        }
        hp += int64_t{per} * (st.count - 1) + front;
    }
    return hp;
}

int Battle::damagePercent(int j) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Seeker) return 0;
    if (p.kind == Kind::Planet) return p.hpStart > 0 ? static_cast<int>(100 - planetHp(p) * 100 / p.hpStart) : 0;
    const int structure = std::max(1, vehicleStructure(r_, s_, p.unit));
    const int lost = structure - detail::remainingStructure(r_, s_, p.unit);
    if (p.kind == Kind::Vehicle) return lost * 100 / structure;
    const int64_t total = int64_t{structure} * std::max(1, p.startCount);
    return static_cast<int>((int64_t{p.startCount - p.unit.count} * structure + lost) * 100 / total);
}

int64_t Battle::sizeOf(int j) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Planet) return kPlanetSizeRank;
    if (p.kind == Kind::Seeker) return 0;
    return int64_t{r_.hull(s_.design(p.unit.design).hull).tonnage} * p.unit.count;
}

int Battle::remainingOf(int j) const {
    const Piece& p = pieces_[j];
    if (p.kind == Kind::Seeker) return p.hp;
    if (p.kind == Kind::Planet) return static_cast<int>(std::min<int64_t>(planetHp(p), INT_MAX / 2));
    return detail::remainingStructure(r_, s_, p.unit);
}

bool Battle::invaderStack(const Piece& p, size_t k) const {
    const UnitStack& st = p.unit.cargo.units[k];
    const EmpireId owner = s_.design(st.design).owner;
    return owner.valid() && owner != p.owner && detail::enemies(s_, owner, p.owner);
}

bool Battle::hasSupply(int i) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet) return true;   // (inferred) planets never run dry
    if (p.kind == Kind::Seeker) return false;
    return !p.usesSupply || p.unit.supply > 0;
}

bool Battle::weaponUsable(int i, const Weapon& w) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet) return w.stack >= 0 && p.unit.cargo.units[static_cast<size_t>(w.stack)].count > 0;
    if (p.kind == Kind::UnitGroup) return p.unit.count > 0;   // (inferred) the other members' guns are intact
    return entryIntact(r_, s_, p.unit, w.entry);
}

int Battle::shotCount(int i, const Weapon& w) const {
    const Piece& p = pieces_[i];
    if (p.kind == Kind::Planet) return w.stack >= 0 ? p.unit.cargo.units[static_cast<size_t>(w.stack)].count * w.multiplicity : 0;
    if (p.kind == Kind::UnitGroup) return p.unit.count * w.multiplicity;
    return 1;
}

bool Battle::canAffect(DamageType type, int t) const {
    const Piece& b = pieces_[t];
    const detail::DamageRule rule = detail::damageRule(type);
    if (b.kind == Kind::Seeker) return rule.structural;
    if (b.kind == Kind::Planet) {
        if (isPlanetOnlyDamage(type)) return true;
        if (!rule.structural) return false;
        if (rule.shieldsOnly) return b.sh.normal > 0;
        return !rule.only || (*rule.only == detail::Layer::Weapons && b.armed);
    }
    return detail::canAffectVehicle(r_, s_, b.unit, b.sh, type);
}

int Battle::damageBonus(EmpireId e) const {
    return detail::systemModifier(r_, s_, e, where_.system, AbilityKind::DamageModifierSystem);
}

int Battle::hitChance(int i, const Weapon& w, int t, int d) const {
    const int offense = pieces_[i].offense + mounted(r_, w.de).toHitModifier;
    return detail::toHitChance(cs_, d, offense, pieces_[t].defense, interference_);
}

// Damage hostile pieces could deal to a piece at (x, y). With `reach`, they first move: twice their speed,
// since the empire order is reshuffled every combat turn and an enemy may act twice before we move again.
int64_t Battle::exposureAt(int i, int x, int y, bool reach) const {
    int64_t total = 0;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& h = pieces_[k];
        if (!h.alive || h.kind == Kind::Seeker || !h.armed || !detail::enemies(s_, h.owner, pieces_[i].owner)) continue;
        const int d = distAt(x, y, static_cast<int>(k)) - (reach ? 2 * h.reach : 0);
        total += h.firepower[static_cast<size_t>(std::clamp(d, 1, kRangeTable))];
    }
    return total;
}

int Battle::nearestThreat(int i, int x, int y) const {
    int best = kGrid * 2;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& h = pieces_[k];
        if (!h.alive || h.kind == Kind::Seeker || !h.armed || !detail::enemies(s_, h.owner, pieces_[i].owner)) continue;
        best = std::min(best, distAt(x, y, static_cast<int>(k)));
    }
    return best;
}

int64_t Battle::ourDamage(int i, int t, int d) const {
    const Piece& p = pieces_[i];
    int64_t total = 0;
    for (const Weapon& w : p.weapons) {
        if (w.kind() == WeaponKind::PointDefense || !(w.targets & maskOf(t)) || !weaponUsable(i, w)) continue;
        total += int64_t{weaponDamageAtRange(r_, w.de, d)} * shotCount(i, w) / w.reloadRate;
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
        if (u.count <= 0 || !isTroopDesign(r_, s_, u.design) || !invaderStack(planet, k)) continue;
        if (s_.design(u.design).owner != e) return true;
    }
    return false;
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
        if (!b.alive || j == i || b.owner == a.owner || !detail::enemies(s_, a.owner, b.owner)) continue;
        const TargetCategory cat = categoryFor(j, a.owner);
        if (S.dontFireOn[static_cast<size_t>(cat)]) continue;
        if (hold && b.kind == Kind::Planet && !b.armed) continue;
        Candidate c;
        c.idx = j;
        const int pr = S.typePriority[static_cast<size_t>(cat)];
        c.priority = pr > 0 ? pr : 1000;
        // Switch to fresh targets once one has taken its share of damage (spec 04 §16).
        int threshold = S.damagePercentShip;
        if (b.kind == Kind::Planet) threshold = S.damagePercentPlanet;
        else if (b.vtype == VehicleType::Fighter) threshold = S.damagePercentFighters;
        else if (b.vtype == VehicleType::Satellite) threshold = S.damagePercentSatellites;
        const int dmgPct = damagePercent(j);
        c.fresh = (S.damageUntilWeaponsGone && b.armed) || dmgPct < threshold || b.kind == Kind::Seeker;
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
        const int travel = sk.travelled + dist(static_cast<int>(k), t);
        total += int64_t{weaponDamageAtRange(r_, sk.seekWeapon.de, std::max(1, travel))} * sk.seekCount;
    }
    return total;
}

int Battle::pickTarget(int i, const Weapon& w, const std::vector<int>& targets) {
    const Piece& a = pieces_[i];
    const bool pd = w.kind() == WeaponKind::PointDefense;
    const bool moves = w.type == DamageType::PushesTarget || w.type == DamageType::PullsTarget || w.type == DamageType::RandomTargetMovement;
    for (int t : targets) {
        const Piece& b = pieces_[t];
        if (!b.alive || !(w.targets & maskOf(t))) continue;
        const int d = dist(i, t);
        if (w.kind() == WeaponKind::Seeking) {
            // A seeker needs its target within travel range; enough seekers in flight move fire on (history 1.82).
            if (d > w.maxRange || incomingSeekerDamage(t) >= remainingOf(t)) continue;
        } else if (weaponDamageAtRange(r_, w.de, std::max(1, d)) <= 0) {
            continue;
        }
        const bool engaged = std::find(a.engaged.begin(), a.engaged.end(), t) != a.engaged.end();
        if (!pd && !engaged && static_cast<int>(a.engaged.size()) >= a.budget) continue;
        if (!canAffect(w.type, t)) continue;
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

void Battle::act(int i) {
    acted_[static_cast<size_t>(i)] = 1;
    if (pieces_[i].mothballed) return;
    if (pieces_[i].kind == Kind::Planet) {
        fire(i);
        return;
    }
    // Fire before moving away, move, then fire whatever is still ready (spec 04 §16).
    fire(i);
    if (!pieces_[i].alive) return;
    const Piece& p = pieces_[i];
    if (p.vtype == VehicleType::Drone) droneMove(i);
    else if (p.leader >= 0 && pieces_[p.leader].alive && pieces_[p.leader].isLeader && pieces_[p.leader].owner == p.owner) followLeader(i);
    else moveByStrategy(i);
    if (pieces_[i].alive) fire(i);
}

void Battle::fire(int i) {
    if (!pieces_[i].alive || pieces_[i].kind == Kind::Seeker || pieces_[i].mothballed || !hasSupply(i)) return;
    const Strategy& S = strategyOf(i);
    const std::vector<int> targets = sortedTargets(i, S);
    for (size_t wi = 0; wi < pieces_[i].weapons.size(); ++wi) {
        if (!pieces_[i].alive || !hasSupply(i)) return;
        const Weapon& w = pieces_[i].weapons[wi];
        if (w.reload > 0 || !weaponUsable(i, w)) continue;
        const int t = pickTarget(i, w, targets);
        if (t >= 0) shoot(i, wi, t);
    }
}

void Battle::shoot(int i, size_t wi, int t) {
    const Weapon w = pieces_[i].weapons[wi];
    const int shots = shotCount(i, w);
    if (shots <= 0) return;
    {
        Piece& a = pieces_[i];
        if (a.kind != Kind::Planet && a.usesSupply) {
            // A vehicle with zero supplies cannot fire (history 1.65).
            if (a.unit.supply <= 0) return;
            a.unit.supply = std::max<int64_t>(0, a.unit.supply - int64_t{detail::supplyPerShot(r_, s_, a.unit, w.de)} * shots);
        }
        a.weapons[wi].reload = w.reloadRate;
        a.fired = true;
        if (w.kind() != WeaponKind::PointDefense && std::find(a.engaged.begin(), a.engaged.end(), t) == a.engaged.end())
            a.engaged.push_back(t);
    }
    event(Ev::Fire, i, t, static_cast<int>(w.entry), w.de.component);
    if (w.kind() == WeaponKind::Seeking) {
        launchSeeker(i, w, t, shots);
        return;
    }
    const int d = std::max(1, dist(i, t));
    const int table = weaponDamageAtRange(r_, w.de, d);
    // Direct fire and point defense roll to hit (spec 04 §7).
    if (!(pieces_[i].alwaysHit && w.kind() == WeaponKind::DirectFire)) {
        const int chance = hitChance(i, w, t, d);
        if (rng_.rangeInt(1, 100) > chance) {
            event(Ev::Miss, i, t, 0, w.de.component);
            return;
        }
    }
    const int dmg = static_cast<int>(std::min<int64_t>(kMaxShotDamage, int64_t{table} * (100 + damageBonus(pieces_[i].owner)) / 100));
    event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, int64_t{dmg} * shots)), w.de.component);
    deliver(i, t, w.type, dmg, table, shots);
}

void Battle::launchSeeker(int i, const Weapon& w, int t, int count) {
    Piece sk;
    sk.kind = Kind::Seeker;
    sk.owner = sk.startOwner = pieces_[i].owner;
    sk.unit.owner = sk.owner;
    sk.unit.design = pieces_[i].kind == Kind::Planet ? DesignId{} : pieces_[i].unit.design;
    sk.name = w.comp->name;
    sk.x = pieces_[i].x;
    sk.y = pieces_[i].y;
    sk.seekTarget = t;
    sk.launcher = i;
    sk.speed = std::max(1, w.comp->weapon.seekerSpeed);
    sk.hp = std::max(1, w.comp->weapon.seekerDamageResistance);
    sk.seekWeapon = w;
    sk.seekCount = count;
    sk.seekBonus = damageBonus(sk.owner);
    sk.defense = cs_.seekerDefense;
    const int idx = addPiece(std::move(sk));
    event(Ev::Seeker, idx, t, count, w.de.component);
}

void Battle::deliver(int att, int t, DamageType type, int damage, int table, int shots) {
    if (isSpecialEffect(type)) {
        special(att, t, type, table);   // (inferred) once per shot, however many members fired
        return;
    }
    // Each member weapon hits separately, so emissive armor sees every one (history 1.76).
    for (int k = 0; k < shots && pieces_[t].alive; ++k) applyHit(att, t, type, damage);
}

void Battle::applyHit(int att, int t, DamageType type, int damage) {
    Piece& b = pieces_[t];
    if (!b.alive) return;
    if (b.kind == Kind::Seeker) {
        if (!detail::damageRule(type).structural) return;
        b.hp -= damage;
        if (b.hp <= 0) kill(t, att, false);
        return;
    }
    if (b.kind == Kind::Planet) {
        planetHit(att, t, type, damage);
        return;
    }
    if (isPlanetOnlyDamage(type)) return;
    groupHit(att, t, type, damage);
}

void Battle::groupHit(int att, int t, DamageType type, int damage) {
    while (damage > 0 && pieces_[t].alive) {
        Piece& b = pieces_[t];
        const HitOutcome o = detail::hitUnit(r_, s_, b.unit, b.sh, damage, type, rng_);
        if (o.structureDamage > 0) b.damaged = true;
        if (!o.destroyed) break;
        ++b.unitsLost;
        --b.unit.count;
        if (b.kind == Kind::UnitGroup) creditKill(att, false);
        if (b.unit.count <= 0) {
            kill(t, att, b.kind != Kind::UnitGroup);
            break;
        }
        // The next member of the group takes the rest (inferred, spec 04 §19 Q10).
        b.unit.damage.assign(s_.design(b.unit.design).entries.size(), 0);
        detail::refreshShields(r_, s_, b.unit, b.sh, true);
        damage = o.excess;
    }
}

void Battle::planetHit(int att, int t, DamageType type, int damage) {
    const detail::DamageRule rule = detail::damageRule(type);
    const bool planetOnly = isPlanetOnlyDamage(type);
    if (!rule.structural && !planetOnly) return;   // reload, push and conversion do nothing to planets
    // (inferred) Planet shields also stop the planet-only types first (spec 04 §19 Q16).
    const detail::DamageRule shieldRule = planetOnly ? detail::damageRule(DamageType::Normal) : rule;
    Piece& p = pieces_[t];
    int rem = detail::absorbShields(p.sh, damage, shieldRule);
    if (rem <= 0) return;
    p.damaged = true;
    switch (type) {
        case DamageType::PlagueLevel1: p.plague = std::max(p.plague, 1); return;
        case DamageType::PlagueLevel2: p.plague = std::max(p.plague, 2); return;
        case DamageType::PlagueLevel3: p.plague = std::max(p.plague, 3); return;
        case DamageType::PlagueLevel4: p.plague = std::max(p.plague, 4); return;
        case DamageType::PlagueLevel5: p.plague = std::max(p.plague, 5); return;
        case DamageType::OnlyPlanetPopulation: populationDamage(att, t, rem); return;
        case DamageType::OnlyPlanetConditions:
            // (inferred) one point of conditions per `Damage Points To Kill One Population`.
            p.conditionsLost += std::max(1, rem / cs_.damagePerPopulation);
            return;
        case DamageType::OnlyResupplyDepots: p.facilityKills.push_back(AbilityKind::SupplyGeneration); return;
        case DamageType::OnlySpaceports: p.facilityKills.push_back(AbilityKind::Spaceport); return;
        default: break;
    }
    if (rule.only && *rule.only != detail::Layer::Weapons) return;
    // Weapon platforms first, then other units in cargo, then population (spec 04 §11).
    rem = damageStacks(att, t, rem, true);
    if (rule.only) return;
    rem = damageStacks(att, t, rem, false);
    if (rem > 0) populationDamage(att, t, rem);
}

int Battle::damageStacks(int att, int t, int damage, bool platforms) {
    std::vector<size_t> candidates;
    while (damage > 0) {
        Piece& p = pieces_[t];
        candidates.clear();
        for (size_t k = 0; k < p.unit.cargo.units.size(); ++k) {
            const UnitStack& st = p.unit.cargo.units[k];
            if (st.count <= 0 || invaderStack(p, k)) continue;
            const bool platform = r_.hull(s_.design(st.design).hull).type == VehicleType::WeaponPlatform;
            if (platform == platforms) candidates.push_back(k);
        }
        if (candidates.empty()) break;
        const size_t k = candidates[rng_.below(candidates.size())];
        UnitStack& st = p.unit.cargo.units[k];
        Vehicle tmp;
        tmp.owner = p.owner;
        tmp.design = st.design;
        tmp.damage = p.stackDamage[k];
        tmp.supply = 1;
        ShieldState none;
        const HitOutcome o = detail::hitUnit(r_, s_, tmp, none, damage, DamageType::Normal, rng_);
        p.stackDamage[k] = tmp.damage;
        if (!o.destroyed) return 0;
        --st.count;
        ++s_.design(st.design).lost;
        p.stackDamage[k].assign(p.stackDamage[k].size(), 0);
        creditKill(att, false);
        damage = o.excess;
    }
    return damage;
}

void Battle::populationDamage(int att, int t, int damage) {
    Piece& p = pieces_[t];
    p.popDamage += damage;
    int64_t deaths = p.popDamage / cs_.damagePerPopulation;
    p.popDamage %= cs_.damagePerPopulation;
    while (deaths > 0) {
        auto largest = std::max_element(p.population.begin(), p.population.end(),
                                        [](const PopulationGroup& a, const PopulationGroup& b) { return a.millions < b.millions; });
        if (largest == p.population.end() || largest->millions <= 0) break;
        const int64_t n = std::min(deaths, largest->millions);
        largest->millions -= n;
        deaths -= n;
        p.popKilled += n;
    }
    int64_t left = 0;
    for (const PopulationGroup& g : p.population) left += g.millions;
    if (left <= 0) {
        // (inferred) A planet whose population is gone loses its colony.
        p.colonyLost = true;
        note(std::format("the colony on {} was wiped out", p.name));
        kill(t, att, true);
    }
}

void Battle::special(int att, int t, DamageType type, int value) {
    Piece& b = pieces_[t];
    if (b.kind == Kind::Planet || b.kind == Kind::Seeker || !b.alive) return;
    switch (type) {
        case DamageType::IncreaseReloadTime:
            if (detail::hasIntactComponent(r_, s_, b.unit, AbilityKind::MasterComputer)) return;
            [[fallthrough]];
        case DamageType::DisruptReloadTime:
            for (Weapon& w : b.weapons) w.reload += std::max(0, value);
            return;
        case DamageType::CrewConversion:
            // The table value is the percent chance (inferred).
            if (detail::canAffectVehicle(r_, s_, b.unit, b.sh, type) && rng_.percent(value)) capture(t, pieces_[att].owner, att, false);
            return;
        case DamageType::PushesTarget:
        case DamageType::PullsTarget:
            forcedMove(t, att, value, type == DamageType::PushesTarget);
            b.pushed = true;
            return;
        case DamageType::RandomTargetMovement:
            // (inferred) the table value is ignored; the target lands on a random free square.
            for (int attempt = 0; attempt < 32; ++attempt) {
                const int x = rng_.rangeInt(0, kGrid - 1), y = rng_.rangeInt(0, kGrid - 1);
                if (isFree(x, y, t)) {
                    moveTo(t, x, y);
                    break;
                }
            }
            pieces_[t].pushed = true;
            return;
        default: return;
    }
}

void Battle::forcedMove(int t, int att, int squares, bool push) {
    int dx = sgn(pieces_[t].x - pieces_[att].x), dy = sgn(pieces_[t].y - pieces_[att].y);
    if (dx == 0 && dy == 0) dx = 1;
    if (!push) {
        dx = -dx;
        dy = -dy;
    }
    // (inferred) stops at the map edge or an occupied square.
    for (int k = 0; k < squares; ++k) {
        const int nx = pieces_[t].x + dx, ny = pieces_[t].y + dy;
        if (!isFree(nx, ny, t)) break;
        moveTo(t, nx, ny);
    }
}

void Battle::creditKill(int att, bool ship) {
    if (att < 0) return;
    int k = att;
    if (pieces_[k].kind == Kind::Seeker) k = pieces_[k].launcher;   // seeker kills credit the launcher (history 1.87)
    if (k < 0) return;
    if (ship) ++pieces_[k].shipKills;
    else ++pieces_[k].unitKills;
}

void Battle::kill(int t, int att, bool credit) {
    Piece& b = pieces_[t];
    if (!b.alive) return;
    b.alive = false;
    if (b.kind == Kind::Vehicle && b.unitsLost == 0) b.unitsLost = 1;
    vacate(t);
    event(Ev::Destroyed, t, att >= 0 ? att : t);
    if (credit) creditKill(att, true);
    if (b.isLeader) dissolve(t);
    if (b.kind != Kind::Seeker && b.kind != Kind::Planet) note(std::format("{} destroyed", label(t)));
}

void Battle::dissolve(int leader) {
    pieces_[leader].isLeader = false;
    for (Piece& p : pieces_)
        if (p.leader == leader) p.leader = -1;
}

void Battle::capture(int t, EmpireId newOwner, int capturer, bool boarding) {
    Piece& b = pieces_[t];
    if (detail::hasIntactComponent(r_, s_, b.unit, AbilityKind::SelfDestruct)) {
        // A ship about to be taken blows itself up, and its boarders with it (spec 04 §12).
        note(std::format("{} self-destructed rather than be captured", label(t)));
        kill(t, capturer, true);
        if (boarding && capturer >= 0 && pieces_[capturer].alive) {
            pieces_[capturer].unit.count = 0;
            ++pieces_[capturer].unitsLost;
            kill(capturer, t, false);
        }
        return;
    }
    b.owner = newOwner;
    b.unit.owner = newOwner;
    b.captured = true;
    b.unit.experience = 0;   // (history 1.15)
    b.experience = 0;
    b.unit.fleet = {};
    for (Weapon& w : b.weapons) w.reload += cs_.capturedReload;
    if (b.isLeader) dissolve(t);
    pieces_[t].leader = -1;
    pieces_[t].designStrategy = 0;   // (inferred) the captor's first strategy
    pieces_[t].fleetStrategy = 0;
    pieces_[t].droneTarget = -1;
    refreshCombatValues(t);
    event(Ev::Captured, t, capturer >= 0 ? capturer : t, static_cast<int>(newOwner.value));
    creditKill(capturer, true);
    note(std::format("{} captured by {}", pieces_[t].name, s_.empire(newOwner).name));
}

void Battle::pdReact(int mover) {
    // Point defense fires on its own whenever a valid target comes in range (spec 04 §10.2).
    for (size_t k = 0; k < pieces_.size(); ++k) {
        if (!pieces_[mover].alive) return;
        const int j = static_cast<int>(k);
        const Piece& p = pieces_[k];
        if (!p.alive || j == mover || p.kind == Kind::Seeker || p.mothballed) continue;
        if (!detail::enemies(s_, p.owner, pieces_[mover].owner) || !hasSupply(j)) continue;
        for (size_t wi = 0; wi < pieces_[k].weapons.size(); ++wi) {
            const Weapon& w = pieces_[k].weapons[wi];
            if (w.kind() != WeaponKind::PointDefense || w.reload > 0 || !(w.targets & maskOf(mover)) || !weaponUsable(j, w)) continue;
            if (weaponDamageAtRange(r_, w.de, std::max(1, dist(j, mover))) <= 0 || !canAffect(w.type, mover)) continue;
            shoot(j, wi, mover);
            if (!pieces_[mover].alive || !hasSupply(j)) break;
        }
    }
}

void Battle::moveSeekers(EmpireId e) {
    // (inferred) An empire's seekers move at the end of its phase (spec 04 §4).
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const int i = static_cast<int>(k);
        if (pieces_[k].kind != Kind::Seeker || !pieces_[k].alive || pieces_[k].owner != e) continue;
        for (int stepNo = 0; stepNo < pieces_[k].speed && pieces_[k].alive; ++stepNo) {
            const int t = pieces_[k].seekTarget;
            const bool valid = t >= 0 && pieces_[t].alive && pieces_[t].owner != e && detail::enemies(s_, e, pieces_[t].owner);
            if (!valid || pieces_[k].travelled + 1 > pieces_[k].seekWeapon.maxRange) {
                // Expired: target gone or captured (history 1.04, 1.18), or out of range.
                pieces_[k].alive = false;
                event(Ev::Destroyed, i, i);
                break;
            }
            const Piece& target = pieces_[t];
            const int tx = std::clamp(pieces_[k].x, target.x, target.x + target.size - 1);
            const int ty = std::clamp(pieces_[k].y, target.y, target.y + target.size - 1);
            pieces_[k].x += sgn(tx - pieces_[k].x);
            pieces_[k].y += sgn(ty - pieces_[k].y);
            ++pieces_[k].travelled;
            event(Ev::Move, i, i);
            pdReact(i);
            if (!pieces_[k].alive) break;
            if (dist(i, t) == 0) {
                // Impact: table value at the distance travelled, no roll (spec 04 §10.1).
                const Weapon w = pieces_[k].seekWeapon;
                const int table = weaponDamageAtRange(r_, w.de, std::max(1, pieces_[k].travelled));
                const int dmg = static_cast<int>(std::min<int64_t>(kMaxShotDamage, int64_t{table} * (100 + pieces_[k].seekBonus) / 100));
                const int count = pieces_[k].seekCount;
                pieces_[k].alive = false;
                event(Ev::Hit, i, t, static_cast<int>(std::min<int64_t>(INT_MAX, int64_t{dmg} * count)), w.de.component);
                deliver(i, t, w.type, dmg, table, count);
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
            // (inferred) a planet launches up to one full group of each kind per combat turn (spec 04 §19 Q16).
            fighters = cs_.fighterGroup;
            satellites = cs_.satelliteGroup;
            drones = cs_.fighterGroup;
        } else {
            fighters = static_cast<int>(detail::componentSum(r_, s_, pieces_[k].unit, AbilityKind::LaunchRecoverFighters));
            satellites = static_cast<int>(detail::componentSum(r_, s_, pieces_[k].unit, AbilityKind::LaunchRecoverSatellites));
            drones = static_cast<int>(detail::componentSum(r_, s_, pieces_[k].unit, AbilityKind::LaunchDrones));
        }
        if (fighters + satellites + drones <= 0) continue;
        const uint32_t sIndex = strategyIndex(i);
        const Strategy& S = strategy(e, sIndex);
        const int fighterGroup = std::clamp(S.fighterLaunchGroup, 1, cs_.fighterGroup);
        for (size_t u = 0; u < pieces_[k].unit.cargo.units.size(); ++u) {
            const UnitStack st = pieces_[k].unit.cargo.units[u];
            if (st.count <= 0 || (kind == Kind::Planet && invaderStack(pieces_[k], u))) continue;
            int* rate = nullptr;
            int group = 1;
            switch (r_.hull(s_.design(st.design).hull).type) {
                case VehicleType::Fighter:
                    rate = &fighters;
                    group = fighterGroup;
                    break;
                case VehicleType::Satellite:
                    rate = &satellites;
                    group = cs_.satelliteGroup;
                    break;
                case VehicleType::Drone:
                    rate = &drones;
                    group = 1;   // (inferred) drones fly singly and pick their own targets
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
    }
}

bool Battle::spawnUnit(int carrier, DesignId design, int count, uint32_t strategyIndex) {
    const auto [x, y] = freeNear(pieces_[carrier].x, pieces_[carrier].y);
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
    u.unit.supply = supply;
    u.vtype = r_.hull(d.hull).type;
    u.name = d.name;
    u.x = x;
    u.y = y;
    u.carrier = carrier;
    u.launched = true;
    u.designStrategy = strategyIndex;
    u.startCount = count;
    u.usesSupply = detail::needsSupply(r_, s_, u.unit);
    buildWeapons(u);
    u.sh.bonus = -disruption_;
    detail::refreshShields(r_, s_, u.unit, u.sh, true);
    const int idx = addPiece(std::move(u));
    occupy(idx);
    refreshPiece(idx);   // launched units get their full movement at once (history 1.55, 1.71)
    event(Ev::Launch, idx, carrier, count);
    return true;
}

// ---- Movement (spec 04 §5, §16) -----------------------------------------------------------------------------

void Battle::moveTo(int i, int x, int y) {
    vacate(i);
    pieces_[i].x = x;
    pieces_[i].y = y;
    occupy(i);
    event(Ev::Move, i, i);
}

void Battle::step(int i, int x, int y) {
    moveTo(i, x, y);
    --pieces_[i].mp;
    const VehicleType t = pieces_[i].vtype;
    if (pieces_[i].kind == Kind::UnitGroup && (t == VehicleType::Fighter || t == VehicleType::Drone)) pdReact(i);
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
                    if (w.kind() != WeaponKind::PointDefense && weaponUsable(i, w)) reach |= w.targets;
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
        if (w.kind() != WeaponKind::PointDefense && (w.targets & maskOf(t)) && weaponUsable(i, w)) maxRange = std::max(maxRange, w.maxRange);
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

void Battle::moveByStrategy(int i) {
    const Strategy& S = strategyOf(i);
    const auto [mode, t] = chooseMode(i, S);
    switch (mode) {
        case MoveStrategy::DontGetHurt: moveDontGetHurt(i); return;
        case MoveStrategy::DropTroops:
            moveToward(i, t, 1, false);
            if (pieces_[i].alive && pieces_[t].alive && dist(i, t) <= 1) dropTroops(i, t);
            return;
        case MoveStrategy::BoardEnemyShips:
            moveToward(i, t, 1, false);
            if (pieces_[i].alive && pieces_[t].alive && dist(i, t) <= 1) board(i, t);
            return;
        case MoveStrategy::Ram:
            moveToward(i, t, 1, false);
            if (pieces_[i].alive && pieces_[t].alive && dist(i, t) <= 1 && pieces_[i].mp > 0) ram(i, t);
            return;
        default:
            moveToward(i, t, desiredRange(i, t, mode), mode == MoveStrategy::OptimalRange || mode == MoveStrategy::ShortRange);
            return;
    }
}

void Battle::moveToward(int i, int t, int range, bool avoidFire) {
    while (pieces_[i].alive && pieces_[i].mp > 0 && pieces_[t].alive) {
        const Piece& p = pieces_[i];
        const int cur = std::abs(distAt(p.x, p.y, t) - range);
        int bestX = p.x, bestY = p.y, bestScore = cur;
        int64_t bestExp = avoidFire ? exposureAt(i, p.x, p.y, false) : 0;
        bool blocked = false;
        for (const auto& [dx, dy] : kDirs) {
            const int nx = p.x + dx, ny = p.y + dy;
            if (!onGrid(nx, ny)) continue;
            const int score = std::abs(distAt(nx, ny, t) - range);
            if (!isFree(nx, ny, i)) {
                blocked = blocked || score < cur;
                continue;
            }
            const int64_t exp = avoidFire ? exposureAt(i, nx, ny, false) : 0;
            if (score < bestScore || (score == bestScore && exp < bestExp)) {
                bestX = nx;
                bestY = ny;
                bestScore = score;
                bestExp = exp;
            }
        }
        if (bestX == p.x && bestY == p.y) {
            // A leader blocked in its movement dissolves its group (history 1.03).
            if (cur > 0 && blocked && p.isLeader) dissolve(i);
            return;
        }
        step(i, bestX, bestY);
    }
}

void Battle::moveDontGetHurt(int i) {
    // Stay where no enemy can fire on us, even after it moves (spec 04 §14, §16; the look-ahead is inferred).
    // Search every square reachable this turn: least exposure, then room to keep evading (away from the map
    // edge), then farthest from the nearest threat, then fewest steps (inferred).
    const Piece& p = pieces_[i];
    if (p.mp <= 0 || exposureAt(i, p.x, p.y, true) == 0) return;
    const int reach = std::min(p.mp, kGrid);
    std::vector<int> from(kGrid * kGrid, -2);   // -2 unseen, -1 start, else previous square
    std::vector<int> depth(kGrid * kGrid, 0);
    std::vector<int> queue{p.y * kGrid + p.x};
    from[static_cast<size_t>(queue.front())] = -1;
    auto room = [](int x, int y) { return std::min({x, y, kGrid - 1 - x, kGrid - 1 - y, 3}); };
    int best = queue.front();
    std::tuple<int64_t, int, int> bestKey{exposureAt(i, p.x, p.y, true), -room(p.x, p.y), -nearestThreat(i, p.x, p.y)};
    for (size_t head = 0; head < queue.size(); ++head) {
        const int sq = queue[head];
        const int x = sq % kGrid, y = sq / kGrid;
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
            const int next = ny * kGrid + nx;
            if (from[static_cast<size_t>(next)] != -2) continue;
            from[static_cast<size_t>(next)] = sq;
            depth[static_cast<size_t>(next)] = depth[static_cast<size_t>(sq)] + 1;
            queue.push_back(next);
        }
    }
    std::vector<int> path;
    for (int sq = best; from[static_cast<size_t>(sq)] != -1; sq = from[static_cast<size_t>(sq)]) path.push_back(sq);
    for (auto it = path.rbegin(); it != path.rend() && pieces_[i].alive && pieces_[i].mp > 0; ++it) {
        if (!isFree(*it % kGrid, *it / kGrid, i)) break;
        step(i, *it % kGrid, *it / kGrid);
    }
}

void Battle::followLeader(int i) {
    // Members move toward their formation slot next to the leader (spec 04 §5).
    const Piece& leader = pieces_[pieces_[i].leader];
    const int tx = std::clamp(leader.x + pieces_[i].slotDx, 0, kGrid - 1);
    const int ty = std::clamp(leader.y + pieces_[i].slotDy, 0, kGrid - 1);
    while (pieces_[i].alive && pieces_[i].mp > 0) {
        const Piece& p = pieces_[i];
        const int cur = std::max(std::abs(p.x - tx), std::abs(p.y - ty));
        if (cur == 0) return;
        int bestX = p.x, bestY = p.y, best = cur;
        for (const auto& [dx, dy] : kDirs) {
            const int nx = p.x + dx, ny = p.y + dy;
            if (!isFree(nx, ny, i)) continue;
            const int d = std::max(std::abs(nx - tx), std::abs(ny - ty));
            if (d < best) {
                best = d;
                bestX = nx;
                bestY = ny;
            }
        }
        if (bestX == p.x && bestY == p.y) return;
        step(i, bestX, bestY);
    }
}

void Battle::droneMove(int i) {
    // Drones pick their own ship, planet or satellite targets and ram them (spec 04 §10.7).
    constexpr uint8_t kDroneTargets = kTargetShips | kTargetPlanets | kTargetSatellites;
    int t = pieces_[i].droneTarget;
    const bool valid = t >= 0 && pieces_[t].alive && pieces_[t].owner != pieces_[i].owner &&
                       detail::enemies(s_, pieces_[i].owner, pieces_[t].owner) && (maskOf(t) & kDroneTargets);
    if (!valid) {
        t = -1;
        for (int c : sortedTargets(i, strategyOf(i)))
            if (maskOf(c) & kDroneTargets) {
                t = c;
                break;
            }
        pieces_[i].droneTarget = t;
    }
    if (t < 0) return;
    moveToward(i, t, 1, false);
    if (pieces_[i].alive && pieces_[t].alive && dist(i, t) <= 1 && pieces_[i].mp > 0) ram(i, t);
}

int Battle::boardTarget(int i) const {
    // Enemy ships and bases, those with shields down first, then the nearest.
    int best = -1;
    std::tuple<int, int, int> bestKey{};
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& b = pieces_[k];
        const int j = static_cast<int>(k);
        if (!b.alive || b.kind != Kind::Vehicle || !detail::enemies(s_, pieces_[i].owner, b.owner)) continue;
        const std::tuple<int, int, int> key{b.sh.normal + b.sh.phased > 0 ? 1 : 0, dist(i, j), j};
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
                if (pieces_[k].planet == o.object && usable(static_cast<int>(k))) return static_cast<int>(k);
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
    Piece& b = pieces_[t];
    if (b.kind != Kind::Vehicle || !b.alive || dist(i, t) > 1 || b.sh.normal + b.sh.phased > 0) return;
    const int64_t offense = detail::componentSum(r_, s_, pieces_[i].unit, AbilityKind::BoardingAttack);
    if (offense <= 0) return;
    // Boarding parties defend as well as attack (spec 04 §12).
    const int64_t defense = detail::componentSum(r_, s_, b.unit, AbilityKind::BoardingDefense) +
                            detail::componentSum(r_, s_, b.unit, AbilityKind::BoardingAttack);
    // Every attempt uses up the attacker's boarding parties.
    {
        Piece& a = pieces_[i];
        const Design& d = s_.design(a.unit.design);
        for (size_t e = 0; e < d.entries.size(); ++e)
            if (hasAbility(r_.componentAbilities(d.entries[e].component), AbilityKind::BoardingAttack) && entryIntact(r_, s_, a.unit, e))
                a.unit.damage[e] = entryStructure(r_, d, e);
        a.fired = true;
    }
    event(Ev::Fire, i, t);
    // (inferred) a strict comparison, no roll (spec 04 §19 Q13).
    if (offense > defense) capture(t, pieces_[i].owner, i, true);
    else note(std::format("{} repelled boarders from {}", label(t), label(i)));
    if (pieces_[i].alive && vehicleDestroyed(r_, s_, pieces_[i].unit)) kill(i, -1, false);
}

void Battle::ram(int i, int t) {
    const Piece& a = pieces_[i];
    const uint8_t mask = maskOf(t);
    // Target damage: rammer's remaining structure plus matching warheads (history 1.58); the rammer takes
    // a share of the target's remaining structure (inferred mapping of the two settings).
    int64_t warheads = 0;
    const Design& d = s_.design(a.unit.design);
    for (size_t e = 0; e < d.entries.size(); ++e) {
        const ruleset::Component& c = r_.component(d.entries[e].component);
        if (c.weapon.kind == WeaponKind::Warhead && entryIntact(r_, s_, a.unit, e) && (parseWeaponTargets(c.weapon.targets) & mask))
            warheads += weaponDamageAtRange(r_, d.entries[e], 1);
    }
    const int targetDamage = static_cast<int>(std::min<int64_t>(INT_MAX / 2, int64_t{remainingOf(i)} * cs_.ramTargetPercent / 100 + warheads));
    const int rammerDamage = static_cast<int>(int64_t{remainingOf(t)} * cs_.ramSourcePercent / 100);
    {
        Piece& ra = pieces_[i];
        for (size_t e = 0; e < d.entries.size(); ++e)   // (inferred) warheads are used up
            if (r_.component(d.entries[e].component).weapon.kind == WeaponKind::Warhead) ra.unit.damage[e] = entryStructure(r_, d, e);
        ra.mp = 0;
        ra.fired = true;
    }
    event(Ev::Fire, i, t);
    event(Ev::Hit, i, t, targetDamage);
    note(std::format("{} rammed {}", label(i), label(t)));
    applyHit(i, t, DamageType::Normal, targetDamage);
    if (!pieces_[i].alive) return;
    if (pieces_[i].vtype == VehicleType::Drone) {
        // (inferred) a drone is spent by its ram.
        Piece& dr = pieces_[i];
        --dr.unit.count;
        ++dr.unitsLost;
        if (dr.unit.count <= 0) kill(i, -1, false);
        else dr.unit.damage.assign(d.entries.size(), 0);
        return;
    }
    applyHit(t, i, DamageType::Normal, rammerDamage);
    if (pieces_[i].alive && vehicleDestroyed(r_, s_, pieces_[i].unit)) kill(i, t, true);
}

void Battle::dropTroops(int i, int t) {
    Piece& planet = pieces_[t];
    if (planet.kind != Kind::Planet || !planet.alive || dist(i, t) > 1 || contestedBy(planet, pieces_[i].owner)) return;
    const int64_t limitAbility = detail::componentSum(r_, s_, pieces_[i].unit, AbilityKind::DropTroops);
    int limit = limitAbility > 0 ? static_cast<int>(limitAbility) - pieces_[i].dropsThisTurn : INT_MAX;
    int landed = 0;
    for (UnitStack& st : pieces_[i].unit.cargo.units) {
        if (limit <= 0) break;
        if (st.count <= 0 || !isTroopDesign(r_, s_, st.design)) continue;
        const EmpireId troopOwner = s_.design(st.design).owner;
        if (troopOwner == planet.owner || !detail::enemies(s_, troopOwner, planet.owner)) continue;
        const int n = std::min(st.count, limit);
        st.count -= n;
        limit -= n;
        landed += n;
        Piece& pl = pieces_[t];
        auto dst = std::find_if(pl.unit.cargo.units.begin(), pl.unit.cargo.units.end(), [&](const UnitStack& u) { return u.design == st.design; });
        if (dst != pl.unit.cargo.units.end()) dst->count += n;
        else {
            pl.unit.cargo.units.push_back({st.design, n});
            pl.stackDamage.emplace_back(s_.design(st.design).entries.size(), 0);
        }
    }
    if (landed <= 0) return;
    pieces_[i].dropsThisTurn += landed;
    troopsLanded_[pieces_[i].owner.value] += landed;
    event(Ev::Launch, i, t, landed);
    note(std::format("{} landed {} troops on {}", label(i), landed, pieces_[t].name));
}

// ---- Round loop --------------------------------------------------------------------------------------------

void Battle::run() {
    for (round_ = 1; round_ <= cs_.spaceTurns; ++round_) {
        startRound();
        acted_.assign(pieces_.size(), 0);
        for (size_t k = 0; k < pieces_.size(); ++k)
            if (pieces_[k].kind == Kind::Seeker) acted_[k] = 1;
        auto ready = [&](size_t k, EmpireId e) {
            return !acted_[k] && pieces_[k].alive && pieces_[k].owner == e && pieces_[k].kind != Kind::Seeker;
        };
        for (EmpireId e : phaseOrder()) {
            launchUnits(e);
            for (size_t k = 0; k < pieces_.size(); ++k) {
                if (!ready(k, e)) continue;
                const Piece& p = pieces_[k];
                // Group members act right after their leader.
                if (p.leader >= 0 && !acted_[static_cast<size_t>(p.leader)] && pieces_[p.leader].alive && pieces_[p.leader].isLeader &&
                    pieces_[p.leader].owner == e)
                    continue;
                act(static_cast<int>(k));
                if (pieces_[k].isLeader)
                    for (size_t m = 0; m < pieces_.size(); ++m)
                        if (pieces_[m].leader == static_cast<int>(k) && ready(m, e)) act(static_cast<int>(m));
            }
            for (size_t k = 0; k < pieces_.size(); ++k)
                if (ready(k, e)) act(static_cast<int>(k));
            moveSeekers(e);
        }
        if (over() || !anyoneCanAct()) break;
    }
    round_ = std::min(round_, cs_.spaceTurns);
}

// ---- Results ----------------------------------------------------------------------------------------------------

void Battle::finish() {
    const std::string sector = detail::sectorName(s_, where_);
    // Results per empire.
    std::map<uint32_t, Result> results;
    for (EmpireId e : empires_) {
        const bool mine = hasPieces(e);
        bool enemy = false;
        for (EmpireId o : empires_)
            if (o != e && detail::enemies(s_, e, o) && hasPieces(o)) enemy = true;
        results[e.value] = mine && !enemy ? Result::Win : (!mine && enemy ? Result::Loss : Result::Stalemate);
    }
    for (EmpireId e : empires_) {
        const Result res = results[e.value];
        const char* word = res == Result::Win ? "victory" : res == Result::Loss ? "defeat" : "stalemate";
        rec_.summary.push_back(std::format("{}: {}", s_.empire(e).name, word));
    }

    // Experience for survivors (spec 04 §15; the scale is inferred).
    std::map<uint32_t, int> fleetGain;
    for (Piece& p : pieces_) {
        if ((p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) || !p.alive || p.captured) continue;
        const int gain = (p.fired ? kBattleExperience : 0) + p.shipKills * kShipKillExperience + p.unitKills * kUnitKillExperience;
        if (gain <= 0) continue;
        p.unit.experience = std::min(kMaxCombatExperience, p.unit.experience + gain);
        if (p.kind == Kind::Vehicle && p.unit.fleet.valid()) fleetGain[p.unit.fleet.value] = std::max(fleetGain[p.unit.fleet.value], gain);
    }
    for (const auto& [f, gain] : fleetGain)
        if (Fleet* fl = s_.fleet(FleetId{f})) fl->experience = std::min(kMaxCombatExperience, fl->experience + gain);

    // Design statistics.
    for (const Piece& p : pieces_) {
        if (p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) continue;
        Design& d = s_.design(p.unit.design);
        d.lost += p.unitsLost + (p.captured && p.alive ? 1 : 0);   // (inferred) a captured ship counts as lost
        d.kills += p.shipKills + p.unitKills;
    }

    // Per-empire losses for logs and mood.
    struct Tally {
        std::vector<std::string> lost, destroyed, taken, captured;
        int shipsLost = 0, unitsLost = 0, unitsKilled = 0;
    };
    std::map<uint32_t, Tally> tally;
    for (const Piece& p : pieces_) {
        if (p.kind == Kind::Seeker) continue;
        if (p.kind == Kind::Planet) continue;
        Tally& own = tally[p.startOwner.value];
        if (p.kind == Kind::UnitGroup) {
            own.unitsLost += p.unitsLost;
            continue;
        }
        if (!p.alive) {
            own.lost.push_back(p.name);
            ++own.shipsLost;
        } else if (p.captured) {
            own.taken.push_back(p.name);
            ++own.shipsLost;
            tally[p.owner.value].captured.push_back(p.name);
        }
    }
    for (EmpireId e : empires_)
        for (const Piece& p : pieces_) {
            if (p.kind == Kind::Seeker || p.startOwner == e || !detail::enemies(s_, e, p.startOwner)) continue;
            if (p.kind == Kind::UnitGroup) tally[e.value].unitsKilled += p.unitsLost;
            else if (!p.alive) tally[e.value].destroyed.push_back(p.name);
        }

    // Write vehicles back: damage, losses, capture, supply, cargo, experience; fighting clears orders (spec 04 §2).
    for (Piece& p : pieces_) {
        if ((p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) || !p.source.valid()) continue;
        Vehicle* v = s_.vehicle(p.source);
        if (!v) continue;
        if (Fleet* f = s_.fleet(v->fleet)) f->orders.clear();
        v->orders.clear();
        if (!p.alive) {
            v->count = 0;
            continue;
        }
        detail::restoreRegeneratingArmor(r_, s_, p.unit);   // (history 1.79)
        v->damage = p.unit.damage;
        v->count = p.unit.count;
        v->supply = p.unit.supply;
        std::erase_if(p.unit.cargo.units, [](const UnitStack& u) { return u.count <= 0; });
        v->cargo = p.unit.cargo;
        v->experience = p.unit.experience;
        if (p.owner != v->owner) {
            if (Fleet* f = s_.fleet(v->fleet)) {
                std::erase(f->members, v->id);
                if (f->leader == v->id) f->leader = f->members.empty() ? VehicleId{} : f->members.front();
            }
            v->fleet = {};
            v->owner = p.owner;
            v->repeatOrders = false;
            v->targetVehicle = {};
            v->targetObject = {};
        }
    }

    // Planets: cargo, population, facilities, plague, conditions, lost colonies.
    for (Piece& p : pieces_) {
        if (p.kind != Kind::Planet) continue;
        Colony* c = s_.colony(p.planet);
        if (!c) continue;
        if (p.popKilled > 0) {
            ctx_.mood(p.startOwner, "1M Population Killed", where_.system, p.planet, static_cast<int>(std::min<int64_t>(p.popKilled, INT_MAX)));
            ctx_.log(p.startOwner, LogCategory::Combat, std::format("{} bombarded", p.name),
                     std::format("{}M of our people were killed in the battle at {}.", p.popKilled, sector), where_);
        }
        if (p.colonyLost) {
            ctx_.mood(p.startOwner, "Any Planet Lost");
            ctx_.log(p.startOwner, LogCategory::Combat, std::format("{} lost", p.name),
                     std::format("Our colony on {} was wiped out in the battle at {}.", p.name, sector), where_);
            s_.colonies[p.planet.index()].reset();
            continue;
        }
        std::erase_if(p.unit.cargo.units, [](const UnitStack& u) { return u.count <= 0; });
        c->cargo = p.unit.cargo;
        std::erase_if(p.population, [](const PopulationGroup& g) { return g.millions <= 0; });
        c->population = p.population;
        // (inferred) facilities fall with the population: the same share of them is destroyed (spec 04 §19 Q16).
        if (p.popKilled > 0 && p.popStart > 0 && !c->facilities.empty()) {
            int lose = static_cast<int>(static_cast<int64_t>(c->facilities.size()) * p.popKilled / p.popStart);
            while (lose-- > 0 && !c->facilities.empty())
                c->facilities.erase(c->facilities.begin() + static_cast<std::ptrdiff_t>(rng_.below(c->facilities.size())));
        }
        for (AbilityKind k : p.facilityKills) {
            std::vector<size_t> match;
            for (size_t f = 0; f < c->facilities.size(); ++f)
                if (hasAbility(r_.facilityAbilities(c->facilities[f]), k)) match.push_back(f);
            if (!match.empty()) c->facilities.erase(c->facilities.begin() + static_cast<std::ptrdiff_t>(match[rng_.below(match.size())]));
        }
        c->plagueLevel = std::max(c->plagueLevel, p.plague);
        if (p.conditionsLost > 0) {
            SpaceObject& obj = s_.galaxy.object(p.planet);
            obj.conditions = std::max(0, obj.conditions - p.conditionsLost);
        }
    }

    // Launched units land on a friendly carrier or planet with room; the rest stay in space (spec 04 §10.4).
    struct Spawn {
        EmpireId owner;
        DesignId design;
        int count = 0;
    };
    std::vector<Spawn> spawns;
    for (size_t k = 0; k < pieces_.size(); ++k) {
        const Piece& u = pieces_[k];
        if (u.kind != Kind::UnitGroup || !u.alive || u.unit.count <= 0) continue;
        const bool fighter = u.vtype == VehicleType::Fighter;
        const bool satellite = u.vtype == VehicleType::Satellite;
        if (!u.launched && !fighter) continue;   // deployed satellites and drones stay where they are
        int left = u.unit.count;
        if (fighter || satellite) {
            const AbilityKind bay = fighter ? AbilityKind::LaunchRecoverFighters : AbilityKind::LaunchRecoverSatellites;
            const int64_t tonnage = std::max(1, r_.hull(s_.design(u.unit.design).hull).tonnage);
            std::vector<int> order;
            if (u.carrier >= 0) order.push_back(u.carrier);
            for (size_t c = 0; c < pieces_.size(); ++c) order.push_back(static_cast<int>(c));
            std::vector<char> tried(pieces_.size(), 0);
            for (int c : order) {
                if (left <= 0) break;
                if (tried[static_cast<size_t>(c)]) continue;
                tried[static_cast<size_t>(c)] = 1;
                const Piece& h = pieces_[c];
                if (!h.alive || h.owner != u.owner) continue;
                Cargo* cargo = nullptr;
                int64_t room = 0;
                if (h.kind == Kind::Vehicle && h.source.valid()) {
                    Vehicle* v = s_.vehicle(h.source);
                    if (!v || v->count <= 0 || v->owner != u.owner || !detail::hasIntactComponent(r_, s_, *v, bay)) continue;
                    room = vehicleCargoCapacity(r_, s_, *v) - cargoSpaceUsed(r_, s_, v->cargo);
                    cargo = &v->cargo;
                } else if (h.kind == Kind::Planet) {
                    Colony* col = s_.colony(h.planet);
                    if (!col || col->owner != u.owner) continue;
                    room = colonyCargoCapacity(r_, s_, *col) - cargoSpaceUsed(r_, s_, col->cargo);
                    cargo = &col->cargo;
                }
                if (!cargo || room < tonnage) continue;
                const int n = static_cast<int>(std::min<int64_t>(left, room / tonnage));
                auto dst = std::find_if(cargo->units.begin(), cargo->units.end(), [&](const UnitStack& s) { return s.design == u.unit.design; });
                if (dst != cargo->units.end()) dst->count += n;
                else cargo->units.push_back({u.unit.design, n});
                left -= n;
            }
        }
        if (u.launched) {
            if (left > 0) spawns.push_back({u.owner, u.unit.design, left});
        } else if (Vehicle* v = s_.vehicle(u.source)) {
            v->count = left;
        }
    }

    // Mood events (Happiness.txt triggers) and logs.
    for (EmpireId e : empires_) {
        const Result res = results[e.value];
        const std::string outcome = res == Result::Win ? "Win" : res == Result::Loss ? "Loss" : "Stalemate";
        ctx_.mood(e, "Battle in System - " + outcome, where_.system);
        for (const Piece& p : pieces_)
            if (p.kind == Kind::Planet && p.startOwner == e) ctx_.mood(e, "Battle in Sector - " + outcome, where_.system, p.planet);
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
        if (!t.taken.empty()) text += std::format(" Taken by the enemy: {}.", list(t.taken));
        if (!t.captured.empty()) text += std::format(" Captured: {}.", list(t.captured));
        if (const auto it = troopsLanded_.find(e.value); it != troopsLanded_.end()) text += std::format(" {} troops landed.", it->second);
        ctx_.log(e, LogCategory::Combat, std::format("Battle at {}", sector), std::move(text), where_);
    }

    // Each participant learns the designs it fought.
    for (EmpireId e : empires_) {
        std::vector<DesignId>& seen = s_.empire(e).knowledge.seenDesigns;
        for (const Piece& p : pieces_) {
            if (p.startOwner == e || p.kind == Kind::Seeker || p.kind == Kind::Planet || !p.unit.design.valid()) continue;
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
        s_.addVehicle(std::move(v));
    }
}

} // namespace

void resolveSpaceCombat(TurnContext& ctx, Location where) {
    Rng rng = ctx.state.rng.fork();
    detail::resolveMines(ctx, where, rng);
    Battle battle(ctx, where, rng);
    if (!battle.setup()) return;
    battle.run();
    battle.finish();
}

} // namespace opense4::game::combat
