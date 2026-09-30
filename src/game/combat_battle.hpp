#pragma once

// The space battle (docs/spec/04 §3-§16), shared by strategic resolution
// (combat_space.cpp) and tactical combat (combat_tactical.cpp). Internal to
// the combat sources and their tests; see tactical.hpp for the public API.
//
// Every piece works on a copy of its vehicle or colony; the results are
// written back once the battle ends (finish). Randomness comes from the Rng
// the battle is given (a fork of GameState::rng); pieces act in a stable
// order. No floating point.

#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/tactical.hpp"

#include <array>
#include <format>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace opense4::game::combat::detail {

inline constexpr int kW = kCombatMapWidth;
inline constexpr int kH = kCombatMapHeight;
inline constexpr int kRangeTable = kCombatMapWidth;   // longest range the movement logic considers
inline constexpr int kGroups = 10;                     // tactical combat groups 0-9 (spec 04 §5)

// Launch kinds: the per-turn launch counts of a carrier (spec 04 §4, §10).
enum LaunchKind : size_t { kLaunchFighters = 0, kLaunchSatellites = 1, kLaunchDrones = 2 };

inline int sgn(int v) { return (v > 0) - (v < 0); }
inline bool onMap(int x, int y) { return x >= 0 && y >= 0 && x < kW && y < kH; }

struct Weapon {
    size_t entry = 0;        // design entry index (planets: in the platform design)
    DesignEntry de;
    const ruleset::Component* comp = nullptr;
    DamageType type = DamageType::Normal;
    uint8_t targets = 0;
    int reloadRate = 1;
    int reach = 0;           // longest range with damage (seekers: travel)
    int perUnit = 1;         // fighter groups: identical entries on one unit, fired together
    int stack = -1;          // planets: the platform stack in the planet's cargo; unit groups: the design (Piece::stacks)
    // Fighter groups: every design with this weapon, as (stack, identical entries
    // on one unit); all of them fire together as one shot (spec 04 §6).
    std::vector<std::pair<int, int>> shares;
    std::vector<int> reload; // one counter per instance (0 = ready)
    bool enabled = true;     // tactical: fires with the piece's Fire order (spec 06 §1.6 weapon toggles)

    ruleset::WeaponKind kind() const { return comp->weapon.kind; }
};

struct Piece {
    CombatPiece::Kind kind = CombatPiece::Kind::Vehicle;
    EmpireId owner, startOwner;
    VehicleId source;                 // the vehicle this piece stands for (invalid for launched units)
    ObjectId object;                  // planets and obstacles
    Vehicle unit;                     // working copy: one ship, or a unit group (count units, no partial damage)
    // Unit groups: their designs and the units left of each, in a fixed order
    // (weapons refer to them by index); `unit` follows them (setGroupStacks).
    std::vector<UnitStack> stacks;
    ruleset::VehicleType vtype = ruleset::VehicleType::Ship;
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
    int group = -1;                   // leaders of tactical groups: the group number 0-9
    bool tacticalGroup = false;       // leaders: a group a player formed (no fleet strategy)
    int slotDx = 0, slotDy = 0;       // formation offset before turning to the leader's facing
    bool slotFixed = false;           // an offset that does not turn with the leader
    bool arrived = false;             // moved into the sector this turn: an attacker's piece
    int boxDx = 0, boxDy = 0;         // start box direction
    std::array<int, 3> launchedNow{}; // units launched this combat turn, by LaunchKind
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
    int64_t popKilled = 0, hpStart = 0;   // hpStart: planets and unit groups, hit points at the start
    bool colonyLost = false;
    EmpireId capturedBy;              // planets taken by troops during the battle
    int plague = 0;
    // Bookkeeping.
    bool fired = false, damaged = false, captured = false, pushed = false;
    int unitsLost = 0, startCount = 1;
    int64_t tonnageStart = 0;         // unit groups: the units' total hull tonnage at the start (empire experience)
};

struct MovePlan {
    MoveStrategy mode = MoveStrategy::DontGetHurt;
    int target = -1;
    std::vector<std::pair<int, int>> path;
};

class Battle {
public:
    using Kind = CombatPiece::Kind;
    using Ev = CombatEvent::Kind;

    Battle(TurnContext& ctx, Location where, Rng& rng);

    bool setup();
    // Every side by its strategies, to the end (strategic resolution).
    void run();
    void finish();

    // ---- Stepping (spec 04 §4; tactical.hpp) ---------------------------------------------------
    // Where the turn sequence is: between phases, or in a player's phase
    // (its launch step, or taking orders), or done.
    enum class Stage : uint8_t { Between, Launch, Orders, Finished };
    // The sides a player drives; the others follow their strategies.
    void setPlayers(std::vector<EmpireId> players) { players_ = std::move(players); }
    // Plays computer phases until a player's phase needs orders or the battle ends.
    void advance();
    std::string check(const TacticalOrder& o) const;
    // Validates and carries out one order of the player whose phase it is
    // (then the computer phases that follow). Returns why it was refused.
    std::string submit(const TacticalOrder& o);
    // Replays a script: its orders at the players' phases, in order; when it
    // runs out, the strategies play the players' sides.
    void play(std::span<const TacticalOrder> script);
    // Tests: the strategies' orders for player sides go here as explicit orders.
    void recordStrategies(std::vector<TacticalOrder>* out) { strategyLog_ = out; }

    // ---- Queries (tactical views) --------------------------------------------------------------
    Stage stage() const { return stage_; }
    EmpireId phaseEmpire() const { return stage_ == Stage::Launch || stage_ == Stage::Orders ? phaseEmpire_ : EmpireId{}; }
    int round() const { return round_; }
    int lastRound() const { return std::max(1, cs_.spaceTurns - 1); }
    const std::vector<Piece>& pieces() const { return pieces_; }
    const CombatRecord& record() const { return rec_; }
    const std::vector<EmpireId>& empires() const { return empires_; }
    const std::vector<EmpireId>& phaseOrder() const { return order_; }
    bool isPlayer(EmpireId e) const;
    bool over() const;
    bool acted(int i) const { return i >= 0 && static_cast<size_t>(i) < acted_.size() && acted_[static_cast<size_t>(i)]; }
    // The empires present that are hostile to another one present (the sides that fight).
    std::vector<EmpireId> fighting() const;
    bool combatant(int i) const;
    bool hostileTo(int i, int t) const;
    int dist(int a, int b) const;          // range distance: nearest footprint squares
    int aimDist(int a, int b) const;       // aim distance: top-left squares
    int occupant(int x, int y) const;
    int instances(int i, const Weapon& w) const;
    int firedTogether(int i, const Weapon& w) const;
    bool hasSupply(int i) const;
    int hitChance(int i, const Weapon& w, int t) const;
    int64_t hitPoints(int j) const;
    int damagePercent(int j) const;
    int maxMovement(int i) const { return computeMp(i); }
    std::array<int, 3> launchLeft(int i) const;
    bool hasTroops(int i) const;
    bool ownTroops(int i) const;
    bool invaderStack(const Piece& p, size_t k) const;
    std::vector<std::pair<int, int>> pathToSquare(int i, int tx, int ty) const;
    // Why weapon `wi` of piece i cannot fire at t now (empty: it can). Instance -1: any ready one.
    std::string fireProblem(int i, size_t wi, int instance, int t) const;

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
    void beginRound();
    void startRound();
    void refreshPiece(int i);
    void refreshCombatValues(int i);
    void refreshStats(int i);
    void afterDamage(int i);
    int computeMp(int i) const;
    void planetShields(Piece& p, bool fill) const;
    bool hasPieces(EmpireId e) const;
    const Strategy& strategy(EmpireId e, uint32_t index) const;
    uint32_t strategyIndex(int i) const;
    const Strategy& strategyOf(int i) const { return strategy(pieces_[i].owner, strategyIndex(i)); }
    int bestCrewExperience(EmpireId e) const;
    int fleetExp(const Piece& p) const;

    // ---- Queries.
    int distAt(int x, int y, int b) const;
    std::pair<int, int> centreOf(int i) const;
    bool isFree(int x, int y, int self) const;
    uint8_t maskOf(int i) const;
    TargetCategory categoryFor(int j, EmpireId viewer) const;
    int64_t sizeOf(int j) const;
    int64_t planetHp(const Piece& p) const;
    bool canAffect(DamageType type, int t, int att) const;
    bool canMove(int att, int t) const;
    int damageBonus(EmpireId e) const;
    int64_t exposureAt(int i, int x, int y, bool reach) const;
    int nearestThreat(int i, int x, int y) const;
    int seekerDistance(int i, int x, int y) const;
    int64_t ourDamage(int i, int t, int d) const;
    bool contestedBy(const Piece& planet, EmpireId e) const;
    bool overkill(int i, int t, bool seeker) const;

    // ---- Targeting.
    std::vector<int> sortedTargets(int i, const Strategy& S);
    int pickTarget(int i, const Weapon& w, const std::vector<int>& targets);
    int64_t incomingSeekerDamage(int t) const;

    // ---- The phases (spec 04 §4).
    void phase(EmpireId e);          // a computer phase: launch, drones, seekers, the other pieces
    void phaseDrones(EmpireId e);
    void phasePieces(EmpireId e);
    void endPhase();                 // the next phase; the battle ends when no two hostile sides are left
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
    void creditDesignKills(int att, int kills, int64_t tonnage);
    void capture(int t, int capturer, bool boarding);
    void dissolve(int leader);
    void pdReact(int mover);
    void moveSeekers(EmpireId e);
    void expire(int i);
    void launchUnits(EmpireId e);
    bool spawnUnit(int carrier, DesignId design, int count, uint32_t strategyIndex);
    // Unit groups: hit points of the units left (without the pool), and `unit` brought in line with `stacks`.
    int64_t groupHitPoints(const Piece& p, DamageType type, bool shielded) const;
    void syncGroup(Piece& p);
    // The design credited with a kill by piece `att` (a unit group: the design that fired).
    DesignId killerDesign(int att) const;
    int launchKindOf(DesignId design) const;   // LaunchKind, or -1 for units that are not launched

    // ---- Movement.
    MovePlan plan(int i);
    std::pair<MoveStrategy, int> chooseMode(int i, const Strategy& S);
    int desiredRange(int i, int t, MoveStrategy m) const;
    std::vector<std::pair<int, int>> pathToward(int i, int t, int range, bool avoidFire, bool& blocked) const;
    std::vector<std::pair<int, int>> pathDontGetHurt(int i) const;
    void walk(int i, const std::vector<std::pair<int, int>>& path);
    void moveTo(int i, int x, int y);
    void step(int i, int x, int y);
    void followLeader(int i, bool logMoves = false);
    void logMove(int i, const std::vector<std::pair<int, int>>& path);
    void droneAct(int i);
    void board(int i, int t);
    void ram(int i, int t);
    void dropTroops(int i, int t);
    int boardTarget(int i) const;
    int troopTarget(int i) const;

    // ---- Player orders (combat_tactical.cpp).
    bool playerPhase() const { return stage_ == Stage::Launch || stage_ == Stage::Orders; }
    std::string checkPiece(const TacticalOrder& o, bool moving) const;
    void runPrefix();                // the end of the launch step: drones, then seekers
    void execute(const TacticalOrder& o);
    void autoRest();                 // the strategies play the rest of the player's phase
    void finishPlayerPhase();
    void launchOrder(int i, DesignId design, int count, int group);
    void setGroup(int i, int group, bool leader);
    void leaveGroup(int i);
    void logOrder(TacticalOrder o) const {
        if (strategyLog_) strategyLog_->push_back(std::move(o));
    }
    // Drones act on their own, so their moves are never orders.
    bool logging(int i) const {
        return strategyLog_ && i >= 0 && isPlayer(pieces_[i].owner) &&
               !(pieces_[i].kind == CombatPiece::Kind::UnitGroup && pieces_[i].vtype == ruleset::VehicleType::Drone);
    }

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
    int firingStack_ = -1;            // the unit group design whose weapon is being fired (kill credit)

    // The turn sequence.
    Stage stage_ = Stage::Between;
    bool roundOpen_ = false;
    size_t phaseIndex_ = 0;
    EmpireId phaseEmpire_;
    std::vector<EmpireId> players_;
    std::vector<TacticalOrder>* strategyLog_ = nullptr;
};

} // namespace opense4::game::combat::detail
