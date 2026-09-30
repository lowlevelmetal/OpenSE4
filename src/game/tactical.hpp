#pragma once

// Tactical combat (docs/spec/04 §3, §4, §16; the windows are spec 06 §1.6):
// a battle stepped one empire phase at a time, where the pieces of a
// player's side follow the player's orders and every other side follows its
// strategies. Strategic resolution (combat::resolveSpaceCombat) runs the same
// turn sequence with every side on its strategies, so the rules are
// identical and only control differs (spec 04 §3 step 1).
//
// The turn sequence (spec 04 §4). A combat turn runs the empires' phases in
// the order drawn at setup. In a computer side's phase the side launches
// units, then its drones act, its seekers move, and its other pieces act by
// their strategies. A player's phase stops for orders:
//
//   launch step  Only Launch, weapon toggles and group orders are taken.
//                Launching here matches the computer's timing, so the new
//                drones act with the side's drones. Any other order (Begin
//                is the plain one) ends the step: the side's drones act and
//                its seekers move (drones and seekers are always
//                computer-controlled), then the order runs.
//   orders       Orders run at once, in any number and order. A piece may
//                move while it has movement points and fire each weapon
//                whose reload counter is 0. EndPhase ends the phase; unused
//                movement points are lost.
//
// Auto hands the rest of the phase to the strategies (with a piece: just that
// piece now); Resolve Combat hands them the side for the rest of the battle
// (spec 04 §4). Auto given in the launch step plays the whole phase as the
// computer would, so a player side on Auto from the start of every phase
// fights exactly like a strategic battle.
//
// Orders are validated (spec 04 §5, §6, §10.3, §11, §12) and refused with a
// reason; a refused order changes nothing. Everything is deterministic: the
// same battle start and the same accepted orders give the same battle, which
// is how a turn-based game applies a tactical battle fought in the client
// (the accepted orders are the battle's script, see turn.hpp BattleAnswer).

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
        Begin,           // ends the launch step: the side's drones act and its seekers move
        Move,            // `piece` toward (x, y), or along `path` (adjacent squares) when it is not empty
        Fire,            // `piece` fires at `target`: `weapon` (-1: every enabled weapon), `instance` (-1: every ready one)
        ToggleWeapon,    // `piece`'s `weapon` (-1: all) on or off (`on`)
        Launch,          // `piece` launches `count` units of `design` in groups of `group` (fighters and drones)
        DropTroops,      // `piece` lands its troops on the adjacent planet `target` (spec 04 §11)
        Ram,             // `piece` rams the adjacent `target` (spec 04 §10.3)
        Capture,         // `piece` boards the adjacent ship `target` (spec 04 §12)
        SetLeader,       // `piece` leads combat group `group` (0-9)
        SetMember,       // `piece` joins combat group `group`
        ClearGroup,      // `piece` leaves its group; a leader's group dissolves
        ClearAllGroups,  // every group of the side dissolves
        Auto,            // `piece` acts by its strategy now; -1: the strategies play the rest of the phase
        EndPhase,        // the side's phase ends
        ResolveCombat,   // the strategies play the side for the rest of the battle (and this phase)
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
    bool on = true;
    bool alone = false;           // Move of a group leader: the members stay (the strategies' moves)
    bool operator==(const TacticalOrder&) const = default;
};
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
    bool acted = false;           // the strategy acted for it this phase (Auto)
    int leader = -1;              // the group leader it follows (-1: none)
    bool isLeader = false;
    int group = -1;               // combat group number 0-9 (-1: none or a fleet's group)
    int seekTarget = -1, launcher = -1, carrier = -1;
    std::vector<TacticalWeapon> weapons;
    std::vector<UnitStack> cargo;  // units carried (planets: without invading troops)
    std::array<int, 3> launchLeft{};  // fighters, satellites, drones it may still launch this turn
    int64_t boardingAttack = 0;
    bool troops = false;          // carries troops of its own side
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
    bool launchStep() const;             // ... still in its launch step
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
