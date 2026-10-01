#pragma once

// Tactical combat (docs/spec/04 §3, §4, §16; the windows are spec 06 §1.6):
// a battle stepped one empire phase at a time, where the pieces of a
// player's side follow the player's orders and every other side follows its
// strategies. Strategic resolution (combat::resolveSpaceCombat) runs the same
// turn sequence with every side on its strategies, so the rules are
// identical and only control differs (spec 04 §3 step 1).
//
// The turn sequence (spec 04 §4). A combat turn runs the empires' phases in
// the order drawn at setup. Every phase starts with the side's drones and
// seekers (always computer-controlled). A computer side's other pieces then
// act by their strategies, each launching as it acts. A player's phase then
// stops for orders: they run at once, in any number and order; a piece may
// move while it has movement points and fire each weapon whose reload counter
// is 0. EndPhase ("End Turn") ends the phase; unused movement points are
// lost, and idle pieces do not fire by themselves. Drones a player launches
// first act at the start of the side's next phase.
//
// Auto (piece -1) is one toggle for the whole battle and every empire: from
// the next phase on every empire follows its strategies, and play pauses
// after the phase of the last player's empire in each combat turn (EndPhase
// goes on). Releasing it gives the players back their sides. Resolve Combat
// hands every empire to its strategies until the battle ends. OpenSE4
// extensions (spec 04 §19.1): Auto with a piece makes that piece act by its
// strategy at once; AutoPhase lets the strategies play the rest of the
// player's phase. The battle's end is checked only after a phase.
//
// Orders are validated (spec 04 §5, §6, §10.3, §10.4, §11, §12) and refused
// with a reason; a refused order changes nothing. Everything is
// deterministic: the same battle start and the same accepted orders give the
// same battle, which is how a turn-based game applies a tactical battle
// fought in the client (the accepted orders are the battle's script, see
// turn.hpp BattleAnswer).

#include "game/combat.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace opense4::game::combat {

namespace detail {
class Battle;
}

// One square of the combat map.
struct Square {
    int16_t x = 0, y = 0;
    bool operator==(const Square&) const = default;
};

struct TacticalOrder {
    enum class Kind : uint8_t {
        Move,            // `piece` toward (x, y), or along `path` (adjacent squares) when it is not empty
        Fire,            // `piece` fires at `target`: `weapon` (-1: every enabled weapon), `instance` (-1: every ready one)
        ToggleWeapon,    // `piece`'s `weapon` (-1: all) on or off (`on`)
        Launch,          // "Launch Units": `piece` launches `count` units of `design`; `group` is the window
                         // session: units of one kind launched from the piece in one session share a group
                         // (drones: one each) (spec 04 §10.4)
        LaunchFighters,  // "Launch Fighters in Groups": `count` fighters of `design` in groups of `group` (5-50)
        DropTroops,      // `piece` lands its troops on the adjacent colony of another empire that comes last in
                         // piece order, whatever the treaty (spec 04 §11; `target` is ignored)
        Ram,             // `piece` rams the adjacent `target` (spec 04 §10.3)
        Capture,         // `piece` boards the adjacent ship `target` (spec 04 §12)
        SetLeader,       // `piece` leads combat group `group` (0-9) in `formation` (Formations.txt index)
        SetMember,       // `piece` joins combat group `group` with the next member number: its place is the
                         // position with that number in the formation of whoever leads the group
        ClearGroup,      // `piece` leaves its group (its members keep the number)
        ClearAllGroups,  // every piece of the side leaves its group
        Auto,            // `piece` acts by its strategy now; -1: the battle's Auto toggle, set to `on`
        AutoPhase,       // the strategies play the rest of this phase (an OpenSE4 extension)
        EndPhase,        // "End Turn": the side's phase ends (or, paused by Auto, play goes on)
        ResolveCombat,   // every empire follows its strategies to the end of the battle
    };
    Kind kind = Kind::EndPhase;
    EmpireId empire;              // the side giving it: the empire whose phase it is
    int piece = -1;
    int target = -1;
    int weapon = -1;              // index into the piece's weapon list
    int instance = -1;            // a satellite, drone or platform stack fires each unit's weapon on its own
    int x = -1, y = -1;
    std::vector<Square> path;
    DesignId design;
    int count = 0;
    int group = 0;
    int formation = -1;           // SetLeader
    bool on = true;
    bool alone = false;           // Move of a group leader: the members stay (the strategies' moves)
    bool operator==(const TacticalOrder&) const = default;
};
// Group sizes of "Launch Fighters in Groups" (spec 04 §10.4).
inline constexpr std::array<int, 8> kFighterGroupSizes{5, 8, 10, 15, 20, 30, 40, 50};
std::string_view identifier(TacticalOrder::Kind k);

// What the tactical window shows of one piece.
struct TacticalWeapon {
    uint32_t component = 0;       // Components.txt index
    size_t entry = 0;             // design entry (planets: in the platform design)
    ruleset::WeaponKind kind = ruleset::WeaponKind::DirectFire;
    std::vector<int> reload;      // one counter per instance; 0 = ready
    int reloadRate = 1;
    int instances = 0;            // intact instances that can fire (0: destroyed)
    int together = 1;             // weapons fired as one shot (fighter groups)
    int reach = 0;                // longest range with damage (seekers: travel)
    uint8_t targets = 0;          // TargetMask bits
    bool enabled = true;
};

struct TacticalPiece {
    CombatPiece::Kind kind = CombatPiece::Kind::Vehicle;
    EmpireId owner, startOwner;
    VehicleId vehicle;            // the vehicle it stands for (invalid for launched units)
    ObjectId planet;
    DesignId design;
    std::string name;
    ruleset::VehicleType type = ruleset::VehicleType::Ship;
    int x = 0, y = 0, size = 1;
    int facing = 0;               // 0 up, 1 right, 2 down, 3 left, 4 up-right, 5 up-left, 6 down-right, 7 down-left
    bool alive = true;
    bool mothballed = false;
    bool captured = false;
    int shields = 0, shieldsMax = 0;
    int64_t hitPoints = 0;        // what is left (ships: intact structure)
    int damagePercent = 0;
    int movement = 0, movementMax = 0;
    int64_t supply = 0;
    bool hasSupply = true;        // may fire and hold shields
    int count = 1;                // units in a group; seekers: members
    std::vector<UnitStack> units; // a unit group: its designs and the units left of each
    int budget = 1;               // targets it may engage this turn
    int engaged = 0;              // targets engaged this turn
    bool acted = false;           // it has had its turn this phase (its strategy acted, or it was launched now)
    int leader = -1;              // the group leader it follows (-1: none)
    bool isLeader = false;
    int group = -1;               // combat group number, a fleet group's too (-1: none; spec 04 §3 step 5, §5)
    int seekTarget = -1, launcher = -1, carrier = -1;
    std::vector<TacticalWeapon> weapons;
    std::vector<UnitStack> cargo;  // units carried (planets: their stored units)
    std::vector<UnitStack> landed; // planets: troops landed by `invader`, fighting on the ground
    EmpireId invader;
    std::array<int, 3> launchLeft{};  // fighters, satellites, drones it may still launch this turn
    int64_t boardingAttack = 0;
    bool troops = false;          // carries troops (they land for its owner)
};

// A battle that can be stepped (see the file comment). It works on its own
// copy of the game: the state just before the battle (mines strike first, as
// in resolveSpaceCombat). The copy's random numbers fork the same way, so
// with no player side the battle and its results equal resolveSpaceCombat's.
class TacticalBattle {
public:
    struct Setup {
        Location where;
        // The vehicles that just entered (mines strike them, spec 04 §10.6).
        // nullopt: those that moved in this turn (resolveSpaceCombat's
        // two-argument form); empty: nobody entered.
        std::optional<std::vector<VehicleId>> entering;
        std::vector<EmpireId> players;   // sides the player drives
        // The combat simulator (spec 04 §17): the sides that get hand control
        // back when Auto is released (only side 1), and the location's
        // interference and disruption with no system modifier totals.
        std::optional<std::vector<EmpireId>> release;
        std::optional<int> interference, disruption;
        // Who ran the battle check that started it (spec 04 §2); empty: a
        // simultaneous game's check (the simulator and tests).
        BattleCheck check;
        // The simulator: the strategy each side's planets use, an index into
        // that side's strategies (other planets use their empire's first one).
        std::vector<std::pair<EmpireId, uint32_t>> planetStrategies;
        // A battle without player sides that a window shows while it is fought
        // (the Strategic Combat window, spec 06 §1.10.5): it stops after its
        // set-up, before combat turn 1, and step() plays it one empire phase at
        // a time. Otherwise such a battle is fought to its end at once.
        bool stepped = false;
    };

    TacticalBattle(const Rules& r, GameState state, Setup setup);
    ~TacticalBattle();
    TacticalBattle(TacticalBattle&&) noexcept;
    TacticalBattle(const TacticalBattle&) = delete;
    TacticalBattle& operator=(const TacticalBattle&) = delete;

    // False when no battle starts there (nobody hostile sees anyone).
    bool started() const { return started_; }
    const Setup& setup() const { return setup_; }

    // ---- Where the battle is ----------------------------------------------------------------
    int round() const;                   // combat turn, 1-based
    int lastRound() const;               // the last combat turn fought
    EmpireId phaseEmpire() const;        // whose phase waits for orders (invalid otherwise)
    bool awaitingOrders() const;         // a player's phase waits for orders
    bool paused() const;                 // ... paused by Auto after the last player's phase of a combat turn
    bool autoOn() const;                 // the battle's Auto toggle
    bool over() const;                   // no two hostile sides have pieces left
    bool finished() const;               // the last phase was played
    const std::vector<EmpireId>& participants() const;
    const std::vector<EmpireId>& phaseOrder() const;
    bool isPlayer(EmpireId e) const;
    bool hostile(EmpireId a, EmpireId b) const;

    // ---- Pieces (index = CombatRecord piece index) ------------------------------------------
    const std::vector<TacticalPiece>& pieces() const { return views_; }
    const CombatRecord& record() const;
    // The piece covering a square (-1: none; seekers are not counted).
    int pieceAt(int x, int y) const;
    // Squares a Move of the piece toward (x, y) would step through now.
    std::vector<Square> pathTo(int piece, int x, int y) const;
    // Range distance (nearest squares) between two pieces.
    int distance(int a, int b) const;
    // A weapon's chance to hit the target now (100: always hits or no roll), and its damage at the current range.
    int hitChance(int piece, int weapon, int target) const;
    int damageAt(int piece, int weapon, int target) const;
    // Why the weapon cannot fire at the target now (empty: it can).
    std::string fireProblem(int piece, int weapon, int target) const;

    // ---- Orders --------------------------------------------------------------------------------
    // Why the order would be refused (empty: it would be accepted).
    std::string check(const TacticalOrder& o) const;
    // Validates and carries out the order, then plays the computer phases that
    // follow until a player's phase needs orders or the battle ends. Returns
    // the reason for a refusal (empty: accepted).
    std::string submit(const TacticalOrder& o);
    // The accepted orders so far, in order: the battle's script.
    const std::vector<TacticalOrder>& script() const { return script_; }
    // Tests: the orders the strategies give for player sides (Auto, Resolve
    // Combat) are appended here as explicit orders.
    void recordStrategies(std::vector<TacticalOrder>* out);

    // A stepped battle (Setup::stepped): plays the next empire phase, with the
    // end-of-turn upkeep and the end check that follow it (spec 04 §4). False,
    // doing nothing, once the last phase was played. The same battle stepped
    // or fought at once comes out the same.
    bool step();

    // ---- The end ---------------------------------------------------------------------------------
    // Applies the results to the battle's own state (as resolveSpaceCombat
    // does): damage, losses, captures, experience, logs, the record. Plays any
    // phases left by the strategies first.
    void finish();
    bool applied() const { return applied_; }
    const GameState& state() const { return *state_; }

private:
    void refresh();

    const Rules& rules_;
    std::unique_ptr<GameState> state_;   // on the heap: the battle keeps references to it
    Setup setup_;
    struct Context;
    std::unique_ptr<Context> ctx_;
    std::unique_ptr<detail::Battle> battle_;
    std::vector<TacticalPiece> views_;
    std::vector<TacticalOrder> script_;
    bool started_ = false;
    bool applied_ = false;
};

} // namespace opense4::game::combat
