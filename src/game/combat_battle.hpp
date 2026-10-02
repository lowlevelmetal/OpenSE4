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
#include <optional>
#include <span>
#include <string>
#include <tuple>
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
    int range = 0;           // the largest range from 1 to 20 with damage, as the strategies see it (spec 04 §16)
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
    // Damage too small to destroy anything (spec 04 §9.1): a ship's or seeker's;
    // a unit group's pool P; a planet's is its stored units' pool (the planet keeps none).
    int64_t pool = 0;
    int64_t shieldPool = 0;           // unit groups and a planet's stored units: the shield pool Q (spec 04 §9.4)
    int64_t regenPool = 0;            // organic armor (spec 04 §9.3)
    int mp = 0;
    std::vector<Weapon> weapons;      // every weapon that fires (warheads never do)
    // Warheads: they get targets when the computer gives them out, count in
    // the attack map and the fire-first test, but never fire (spec 04 §16, §16.1).
    std::vector<Weapon> warheads;
    std::vector<int> engaged;         // distinct targets engaged this combat turn
    int budget = 1;
    int offense = 0, defense = 0;     // offense includes the system bonus
    bool alwaysHit = false;
    bool armed = false;               // an intact weapon (warheads are never in `weapons`)
    bool guns = false;                // an intact weapon other than point-defense (spec 04 §16)
    int64_t strength = 0;
    std::array<int64_t, kRangeTable + 1> firepower{};
    TargetCategory category = TargetCategory::Ships;
    FleetId fleet;                    // for fleet experience while the ship stays in it
    bool inFleet = false;             // it belongs to a fleet of its owner: in a group it uses the fleet's strategy
    uint32_t designStrategy = 0, fleetStrategy = 0;
    // Combat groups (spec 04 §3 step 5, §5): fleet groups and the groups a
    // player forms share one numbering per side. A leader holds the number and
    // the formation; a member holds only the number and its member number, and
    // its place is the position with that number in the formation of whichever
    // piece of its side leads the number at that moment.
    bool isLeader = false;
    int group = -1;                   // the group's number (leaders and members), -1: in no group
    int member = 0;                   // members: 1 + the highest member number held when it joined
    int formation = -1;               // leaders: Formations.txt index (-1: none)
    bool arrived = false;             // moved into the sector this turn: an attacker's piece
    bool warped = false;              // came through a warp point (another system)
    int boxDx = 0, boxDy = 0;         // the neighbouring sector it came from (start box)
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
    // The piece's number (spec 04 §10.7): its place in piece order at set-up;
    // a new piece takes one above the highest number present, so it can take
    // the number of a piece that has left.
    int number = 0;
    bool droneOrderTarget = false;    // the target its Attack pursuit names: any kind of piece (spec 03 §19 Q68)
    bool wasCloaked = false;          // cloaked when the battle began: it cloaks again afterwards if it can (spec 04 §2)
    int64_t tonnageHad = 0;           // unit groups: hull tonnage of every unit it had in the battle (spec 04 §15, spec 02 §9 experience)
    // Planets.
    std::vector<PopulationGroup> population;
    std::vector<uint32_t> facilities;   // every facility at the start: lost ones keep working until the end (spec 04 §11)
    std::vector<char> facilityLost;     // per entry of `facilities`: destroyed in this battle
    std::vector<UnitStack> landed;      // troops landed by `invader` (Colony::landedTroops)
    std::vector<UnitStack> cargoDead;   // stored units killed in this battle: they take cargo space until it ends (spec 02 §2)
    EmpireId invader;
    int militia = -1;
    int64_t popKilled = 0, hpStart = 0;   // hpStart: planets and unit groups, hit points at the start
    bool colonyLost = false;
    EmpireId capturedBy;              // planets taken by troops during the battle
    int plague = 0;
    // Bookkeeping.
    bool fired = false, captured = false;
    int unitsLost = 0, startCount = 1;
};

// One weapon of a piece as the computer gives out targets (spec 04 §16): a
// weapon or warhead of one unit, platform or ship, in design order.
struct Arm {
    bool warhead = false;
    size_t index = 0;      // into Piece::weapons, or Piece::warheads
    int instance = 0;      // the unit or platform of a satellite or drone group or a planet
    int target = -1;
};

// The targets a computer piece gives its weapons at one choice (spec 04 §16):
// the sorted candidates, the main target (the first of them) and each weapon's.
struct Targeting {
    int main = -1;
    std::vector<int> candidates;
    std::vector<Arm> arms;
};

// The overkill totals every piece carries (spec 04 §16 "Overkill limit"): the
// damage of every weapon given it as a target, and the part of that from
// seeking weapons.
struct OverkillTotal {
    int64_t all = 0;
    int64_t seeking = 0;
};

struct MovePlan {
    MoveStrategy mode = MoveStrategy::DontGetHurt;
    int target = -1;                 // what it drops troops on, boards or rams after the move
    int aim = -1;                    // the fire-first test's target (spec 04 §16.1)
    std::pair<int, int> dest{0, 0};  // the square chosen; (0, 0) when no plan is made
    std::vector<std::pair<int, int>> path;
};

// Where the battle is fought when it is not in the real game (the combat
// simulator, spec 04 §17): the location's interference and disruption, no
// system modifier totals, the strategy each side's planets use, and no
// cloaking again after the battle (spec 04 §2).
struct BattleOverrides {
    int interference = 0;
    int disruption = 0;
    std::vector<std::pair<EmpireId, uint32_t>> planetStrategies;   // (side, index into its strategies)
};

class Battle {
public:
    using Kind = CombatPiece::Kind;
    using Ev = CombatEvent::Kind;

    Battle(TurnContext& ctx, Location where, Rng& rng);

    // The simulator's location (before setup).
    void setOverrides(const BattleOverrides& o) { overrides_ = o; }
    // The battle check that decides whether it starts (spec 04 §2; before setup).
    void setCheck(BattleCheck c) { check_ = std::move(c); }
    bool setup();
    // Every side by its strategies, to the end (strategic resolution).
    void run();
    void finish();

    // ---- Stepping (spec 04 §4; tactical.hpp) ---------------------------------------------------
    // Where the turn sequence is: between phases, in a player's phase (taking
    // orders), paused by Auto after the last player's phase of a combat turn, or done.
    enum class Stage : uint8_t { Between, Orders, Paused, Finished };
    // The sides a player drives; the others follow their strategies. `release`:
    // who gets hand control back when Auto is released (default: the same sides).
    void setPlayers(std::vector<EmpireId> players, std::optional<std::vector<EmpireId>> release = std::nullopt);
    // Plays computer phases until a player's phase needs orders or the battle
    // ends; with `onePhase`, stops after the first phase it plays.
    void advance(bool onePhase = false);
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
    EmpireId phaseEmpire() const { return stage_ == Stage::Orders || stage_ == Stage::Paused ? phaseEmpire_ : EmpireId{}; }
    bool autoOn() const { return autoAll_; }
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
    bool hasUnitsAboard(int i) const;     // units of any kind in its cargo (a planet piece: its colony's)
    int leaderOf(int i) const;             // the piece i follows (-1: none)
    std::vector<std::pair<int, int>> pathToSquare(int i, int tx, int ty) const;
    // Why weapon `wi` of piece i cannot fire at t now (empty: it can). Instance -1: any ready one.
    std::string fireProblem(int i, size_t wi, int instance, int t) const;

    // A free square for a piece drawn at (x, y): hops, then growing squares
    // (spec 04 §3 step 4). `found` is false when none is free; the square is
    // then the last hop's.
    struct Settled {
        int x = 0, y = 0;
        bool found = false;
    };

    // ---- For the tests: the computer's choices as things stand, and pieces set by hand.
    Targeting targetsFor(int i, bool firing) { return chooseTargets(i, firing); }
    void droneTargetFor(int i) { chooseDroneTarget(i); }
    OverkillTotal totalFor(int t) { return totalOf(t); }
    MovePlan planFor(int i);
    std::vector<int64_t> attackMapFor(int i) { return attackMap(i, chooseTargets(i, false)); }
    uint32_t strategyIndexOf(int i) const { return strategyIndex(i); }
    Settled settleFor(int x, int y, int size) { return settle(x, y, size, -1); }
    Piece& piece(int i) { return pieces_[static_cast<size_t>(i)]; }
    void placeAt(int i, int x, int y);

private:
    // ---- Setup.
    void addVehiclePiece(const Vehicle& v);
    void addPlanetPiece(const Colony& c);
    void addObstaclePiece(ObjectId o);
    Weapon makeWeapon(const DesignEntry& de, size_t entry) const;
    void buildWeapons(Piece& p) const;
    void buildPlanetWeapons(Piece& p) const;
    void place();
    Settled settle(int x, int y, int size, int self);
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
    int shieldBonus(EmpireId e) const;

    // ---- Queries.
    std::pair<int, int> centreOf(int i) const;
    bool isFree(int x, int y, int self) const;
    uint8_t maskOf(int i) const;
    TargetCategory categoryFor(int j, EmpireId viewer) const;
    int64_t sizeOf(int j) const;
    int64_t planetHp(const Piece& p) const;
    int64_t ramHitPoints(int j) const;
    bool canAffect(DamageType type, int t, int att) const;
    bool canMove(int att, int t) const;
    int damageBonus(EmpireId e) const;
    bool contestedBy(const Piece& planet, EmpireId e) const;
    bool overkill(int t, int64_t given) const;   // the candidate's total has reached its limit
    Vehicle roster(const Piece& p) const;   // a unit group with every design it had (to-hit, tracking)
    int64_t boardingDefense(int t) const;
    int hullRank(int j) const;              // the place of a ship's hull in VehicleSize.txt
    bool automated(EmpireId e) const;       // its pieces follow their strategies now (spec 03 §10, §19 Q60)
    bool simulated() const { return overrides_.has_value(); }

    // ---- Targeting (spec 04 §16).
    // The candidates of a choice, sorted by the strategy: within `range`
    // (range distance) when it is not negative, of categories in `only` when it
    // is not 0; the Damage Percent filters first, all of them when those leave none.
    std::vector<int> sortedTargets(int i, const Strategy& S, int range = -1, uint8_t only = 0);
    bool holdsFire(EmpireId e);
    std::vector<Arm> arms(int i) const;     // every weapon and warhead, in design order
    const Weapon& weaponOf(int i, const Arm& a) const;
    bool ready(int i, const Arm& a) const;
    bool reaches(int i, const Weapon& w, int t) const;
    // Targets for all weapons at once: when planning (no distance check) or
    // firing. `droneChoice`: the choice of a drone target, the only one that
    // is not ordinary (no first total is cleared, spec 04 §16).
    Targeting chooseTargets(int i, bool firing, bool droneChoice = false);
    // A drone group's drone target (spec 04 §10.7): chosen at setup, at its
    // launch, in its planning when the target left the battle, and when the
    // target changes owner.
    void chooseDroneTarget(int i);
    bool droneMayTake(int i, int t) const;
    int64_t droneWarheadDamage(int i) const;
    int pursuedPiece(int i) const;      // the piece its first order pursues (-1: none)
    void ownerChanged(int t);           // drones aimed at t choose again
    OverkillTotal& totalOf(int t);

    // ---- The phases (spec 04 §4).
    void phase(EmpireId e);          // a computer phase: drones, seekers, then the other pieces
    void phaseDrones(EmpireId e);
    void phasePieces(EmpireId e);
    void endPhase();                 // the next phase; the battle ends when no two hostile sides are left
    void beginPhase(EmpireId e);     // the side's danger map and the drones' carried totals
    void act(int i);
    void move(int i);                // a moving piece's plan, move, special action and fire (spec 04 §16.1)
    void fire(int i);
    void shoot(int i, size_t wi, size_t k, int t);
    void launchSeeker(int i, const Weapon& w, int t, int count);
    void applyHit(int att, int t, DamageType type, int64_t damage);
    // Records a Hit of `att` on `t`, applies it, and marks the event with
    // what it did (CombatEvent::flags, spec 06 §7 Q77).
    void recordHit(int att, int t, DamageType type, int64_t damage, uint32_t component = 0);
    // Marks the Hit event `at` with what applying it did to `t`, whose hit points were `before`.
    void markHit(size_t at, int t, int64_t before);
    void shipHit(int att, int t, DamageType type, int64_t damage);
    void groupHit(int att, int t, DamageType type, int64_t damage);
    void seekerHit(int att, int t, DamageType type, int64_t damage);
    void planetHit(int att, int t, DamageType type, int64_t damage);
    void cargoHit(int t, DamageType type, int64_t damage, bool platforms);
    void trimPlanetCargo(int t);     // cargo above the capacity goes after a hit (spec 02 §2, §13 Q54)
    void populationLoss(int att, int t, int64_t millions);
    void facilityLoss(int t);
    void loseFacility(int t, size_t entry);
    int intactFacilities(const Piece& p) const;
    void forcedMove(int t, int att, int64_t squares, bool push);
    void randomMove(int t);
    void kill(int t, int att);
    void gainExperience(int k, int tenths);
    void creditTonnage(int att, int64_t tonnage);
    void capture(int t, int capturer, bool boarding);
    void convertPlanet(int t, int converter);
    void dissolve(int leader);
    void dissolveByStrategy(int leader);   // the same, as the strategies' orders (logged for player sides)
    bool surrounded(int i) const;          // every square on the map around it is taken
    // Groups: the leader's formation, and a member's place in it (spec 04 §5).
    const ruleset::Formation* formationOf(int leader) const;
    bool hasPlace(int i) const;
    std::pair<int, int> placeOf(int i) const;
    void pdReact(int mover);
    void moveSeekers(EmpireId e);
    void expire(int i);
    // A computer carrier's or planet's launches (spec 04 §10.4, §10.7); returns the units launched.
    int launchFrom(int i);
    int spawnUnit(int carrier, DesignId design, int count);
    void joinUnit(int group, DesignId design, int count);
    // Unit groups: hit points of the units left (without the pool), and `unit` brought in line with `stacks`.
    int64_t groupHitPoints(const Piece& p) const;
    void syncGroup(Piece& p);
    int launchKindOf(DesignId design) const;   // LaunchKind, or -1 for units that are not launched
    int satellitesPresent(EmpireId e) const;

    // ---- Movement.
    MovePlan plan(int i, const Targeting& T);
    MoveStrategy strategyInEffect(int i);
    bool leavesFormation(int i);
    void buildDanger(EmpireId e);
    std::vector<int64_t> dangerFor(int i) const;
    std::vector<int64_t> attackMap(int i, const Targeting& T) const;
    // `t`: the main target; `pointBlank`: Point Blank's target (its last weapon's, else the main one).
    std::pair<int, int> rangeSquare(int i, MoveStrategy m, int t, int pointBlank, const std::vector<int64_t>& danger,
                                    const std::vector<int64_t>& attack);
    std::pair<int, int> dontGetHurtSquare(int i) const;
    std::pair<int, int> pointBlankSquare(int i, int t) const;
    std::pair<int, int> approachSquare(int i, int t) const;   // Ram: the free square nearest to it near the target
    void walk(int i, const std::vector<std::pair<int, int>>& path);
    void moveTo(int i, int x, int y);
    void step(int i, int x, int y);
    void followLeader(int i, bool logMoves = false);
    void logMove(int i, const std::vector<std::pair<int, int>>& path);
    void board(int i, int t);
    void ram(int i, int t);
    // Drop Troops (spec 04 §11): the colony a landing takes, why it is refused
    // (empty: it is not), and the landing with its ground combat.
    int landingColony(int i) const;
    std::string landingProblem(int i) const;
    void dropTroops(int i);
    EmpireId colonyHolder(const Piece& planet) const;   // the colony's owner during the battle
    int boardTarget(int i) const;
    int ramTarget(int i) const;
    int troopTarget(int i) const;

    // ---- Player orders (combat_tactical.cpp).
    bool playerPhase() const { return stage_ == Stage::Orders || stage_ == Stage::Paused; }
    std::string checkPiece(const TacticalOrder& o, bool moving) const;
    std::string checkLaunch(const TacticalOrder& o) const;
    void startPlayerPhase();         // the side's drones and seekers move, then the player takes over
    void execute(const TacticalOrder& o);
    void finishPlayerPhase();
    void launchOrder(const TacticalOrder& o);
    void setGroup(int i, int group, bool leader, int formation);
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
    std::optional<BattleOverrides> overrides_;
    BattleCheck check_;
    std::vector<Piece> pieces_;
    std::vector<char> acted_;
    std::vector<EmpireId> empires_;
    std::vector<EmpireId> order_;                                     // phase order, drawn once
    std::vector<EmpireId> defenders_;
    std::vector<int> occ_;
    mutable std::map<uint32_t, std::vector<Strategy>> strategies_;   // parsed lazily
    std::map<uint32_t, int> troopsLanded_;
    std::map<uint32_t, int> combatBonus_, damageBonus_, shieldBonus_;   // system totals at the start
    std::map<uint32_t, std::pair<int, int>> fleetExp_;                // fleet -> (whole, tenths)
    std::vector<std::string> groundReports_;
    // Each empire's own random numbers for its pieces' planning (ties among
    // squares), forked at setup, one stream for its drones (always moved by
    // the computer) and one for its other pieces: planning for one side never
    // shifts the dice of another, so a side's moves given as orders play out as
    // its strategies' do.
    std::map<std::pair<uint32_t, bool>, Rng> planRng_;
    std::vector<int64_t> danger_;                                     // the moving side's danger map (spec 04 §16.1)
    EmpireId dangerFor_;
    std::vector<OverkillTotal> overkill_;                             // per piece, kept between choices (spec 04 §16)
    // Launch Units window sessions: (piece, session, launch kind) -> the group made (spec 04 §10.4).
    std::map<std::tuple<int, int, int>, int> launchGroups_;
    int round_ = 1;
    int satelliteCap_ = 100;
    int interference_ = 0;
    int disruption_ = 0;

    // The turn sequence.
    Stage stage_ = Stage::Between;
    bool roundOpen_ = false;
    size_t phaseIndex_ = 0;
    EmpireId phaseEmpire_;
    std::vector<EmpireId> players_;
    std::optional<std::vector<EmpireId>> release_;
    bool autoAll_ = false;           // Auto: every empire by its strategies from the next phase on (spec 04 §4)
    bool strategic_ = true;          // no player sides: the end is checked after whole combat turns
    std::vector<TacticalOrder>* strategyLog_ = nullptr;
};

} // namespace opense4::game::combat::detail
