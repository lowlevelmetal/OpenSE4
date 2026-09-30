// Tactical combat (docs/spec/04 §3-§12, §16; tactical.hpp): a player's orders
// in its phase, validated against the combat rules and carried out through
// the same moves, shots, launches, rams and boardings the strategies use.

#include "game/tactical.hpp"

#include "game/combat_battle.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::game::combat {

std::string_view identifier(TacticalOrder::Kind k) {
    switch (k) {
        case TacticalOrder::Kind::Begin: return "Begin";
        case TacticalOrder::Kind::Move: return "Move";
        case TacticalOrder::Kind::Fire: return "Fire";
        case TacticalOrder::Kind::ToggleWeapon: return "ToggleWeapon";
        case TacticalOrder::Kind::Launch: return "Launch";
        case TacticalOrder::Kind::DropTroops: return "DropTroops";
        case TacticalOrder::Kind::Ram: return "Ram";
        case TacticalOrder::Kind::Capture: return "Capture";
        case TacticalOrder::Kind::SetLeader: return "SetLeader";
        case TacticalOrder::Kind::SetMember: return "SetMember";
        case TacticalOrder::Kind::ClearGroup: return "ClearGroup";
        case TacticalOrder::Kind::ClearAllGroups: return "ClearAllGroups";
        case TacticalOrder::Kind::Auto: return "Auto";
        case TacticalOrder::Kind::EndPhase: return "EndPhase";
        case TacticalOrder::Kind::ResolveCombat: return "ResolveCombat";
    }
    return "?";
}

namespace detail {

namespace {

using Kind = CombatPiece::Kind;
using OK = TacticalOrder::Kind;
using ruleset::VehicleType;
using ruleset::WeaponKind;

bool isDrone(const Piece& p) { return p.kind == Kind::UnitGroup && p.vtype == VehicleType::Drone; }

// Orders of the launch step: they come before the side's drones and seekers,
// as a computer side's launches do. Anything else ends the step.
bool launchStepOrder(OK k) { return k == OK::Launch || k == OK::ToggleWeapon; }

} // namespace

// ---- Checks -------------------------------------------------------------------------------------------

std::string Battle::checkPiece(const TacticalOrder& o, bool moving) const {
    if (o.piece < 0 || static_cast<size_t>(o.piece) >= pieces_.size()) return "No such piece.";
    const Piece& p = pieces_[static_cast<size_t>(o.piece)];
    if (!p.alive) return "That piece is gone.";
    if (p.kind == Kind::Seeker) return "Seekers fly on their own.";
    if (p.kind == Kind::Obstacle || p.owner != o.empire) return "That piece is not yours.";
    if (isDrone(p)) return "Drones act on their own.";
    if (p.mothballed) return "A mothballed ship cannot act.";
    if (moving && p.kind != Kind::Vehicle && p.kind != Kind::UnitGroup) return "Planets cannot move.";
    return {};
}

std::string Battle::fireProblem(int i, size_t wi, int instance, int t) const {
    const Piece& a = pieces_[static_cast<size_t>(i)];
    if (wi >= a.weapons.size()) return "No such weapon.";
    const Weapon& w = a.weapons[wi];
    // Spec 04 §6: intact, reload counter 0, supplies, a hostile target of its set, damage at the range.
    const int n = instances(i, w);
    if (n <= 0) return "The weapon is destroyed.";
    if (instance >= n) return "No such weapon.";
    bool ready = false;
    if (instance >= 0) ready = w.reload[static_cast<size_t>(instance)] == 0;
    else
        for (int k = 0; k < n; ++k) ready = ready || w.reload[static_cast<size_t>(k)] == 0;
    if (!ready) return "The weapon is reloading.";
    if (!hasSupply(i)) return "No supplies left to fire.";
    if (t < 0 || static_cast<size_t>(t) >= pieces_.size() || !combatant(t) || pieces_[static_cast<size_t>(t)].kind == Kind::Obstacle)
        return "No target there.";
    if (!hostileTo(i, t)) return "That is not an enemy.";
    if (!(w.targets & maskOf(t))) return "The weapon cannot target that.";
    if (w.kind() == WeaponKind::Seeking) {
        // A seeker needs its target within its travel (inferred: straight to the centre square, as the computer judges it).
        const auto [cx, cy] = centreOf(t);
        const auto [sx, sy] = centreOf(i);
        if (std::max(std::abs(cx - sx), std::abs(cy - sy)) > w.reach) return "Out of range.";
    } else if (weaponDamage(r_, w.de, dist(i, t)) <= 0) {
        return "Out of range.";
    }
    // The target budget (point-defense is outside it).
    if (w.kind() != WeaponKind::PointDefense && std::find(a.engaged.begin(), a.engaged.end(), t) == a.engaged.end() &&
        static_cast<int>(a.engaged.size()) >= a.budget)
        return a.budget == 1 ? std::string("It has engaged its one target this turn.")
                             : std::format("It has engaged its {} targets this turn.", a.budget);
    return {};
}

std::string Battle::check(const TacticalOrder& o) const {
    if (stage_ == Stage::Finished) return "The battle is over.";
    if (!playerPhase()) return "No side is giving orders now.";
    if (o.empire != phaseEmpire_) return "It is not that side's phase.";
    switch (o.kind) {
        case OK::Begin: return stage_ == Stage::Launch ? std::string{} : std::string("The phase has already begun.");
        case OK::EndPhase:
        case OK::ResolveCombat: return {};
        case OK::Auto: {
            if (o.piece < 0) return {};
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            if (acted(o.piece)) return "Its strategy has already acted this turn.";
            return {};
        }
        case OK::Move: {
            if (std::string e = checkPiece(o, true); !e.empty()) return e;
            const Piece& p = pieces_[static_cast<size_t>(o.piece)];
            if (p.mp <= 0) return "No movement left this turn.";
            if (!o.path.empty()) {
                int x = p.x, y = p.y;
                for (const Square& sq : o.path) {
                    if (!onMap(sq.x, sq.y) || std::max(std::abs(sq.x - x), std::abs(sq.y - y)) != 1) return "The path must go square by square.";
                    x = sq.x;
                    y = sq.y;
                }
                return {};
            }
            if (!onMap(o.x, o.y)) return "That is off the map.";
            if (p.x == o.x && p.y == o.y) return "It is already there.";
            if (pathToSquare(o.piece, o.x, o.y).empty()) return "The way is blocked.";
            return {};
        }
        case OK::Fire: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            if (o.weapon >= 0) return fireProblem(o.piece, static_cast<size_t>(o.weapon), o.instance, o.target);
            const Piece& p = pieces_[static_cast<size_t>(o.piece)];
            std::string first;
            bool selected = false;
            for (size_t wi = 0; wi < p.weapons.size(); ++wi) {
                if (!p.weapons[wi].enabled) continue;
                selected = true;
                std::string e = fireProblem(o.piece, wi, -1, o.target);
                if (e.empty()) return {};
                if (first.empty()) first = std::move(e);
            }
            return selected ? first : std::string("No weapon is selected.");
        }
        case OK::ToggleWeapon: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            if (o.weapon >= static_cast<int>(pieces_[static_cast<size_t>(o.piece)].weapons.size())) return "No such weapon.";
            return {};
        }
        case OK::Launch: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            const Piece& p = pieces_[static_cast<size_t>(o.piece)];
            if (p.kind != Kind::Vehicle && p.kind != Kind::Planet) return "Only ships and planets launch units.";
            if (o.count <= 0) return "Nothing to launch.";
            bool carried = false;
            for (size_t u = 0; u < p.unit.cargo.units.size(); ++u)
                if (p.unit.cargo.units[u].design == o.design && p.unit.cargo.units[u].count > 0 &&
                    !(p.kind == Kind::Planet && invaderStack(p, u)))
                    carried = true;
            if (!carried || !o.design.valid() || o.design.index() >= s_.designs.size()) return "It carries no such units.";
            const int kind = launchKindOf(o.design);
            if (kind < 0) return "Only fighters, satellites and drones are launched in combat.";
            if (launchLeft(o.piece)[static_cast<size_t>(kind)] <= 0) return "It cannot launch more of those this turn.";
            if (kind == kLaunchSatellites) {
                int present = 0;
                for (const Piece& q : pieces_)
                    if (q.alive && q.owner == p.owner && q.kind == Kind::UnitGroup && q.vtype == VehicleType::Satellite) present += q.unit.count;
                if (present >= satelliteCap_) return "The satellite limit for this sector is reached.";
            } else if (o.group <= 0) {
                return "Groups need at least one unit.";
            }
            return {};
        }
        case OK::DropTroops: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            if (pieces_[static_cast<size_t>(o.piece)].kind != Kind::Vehicle) return "Only ships drop troops.";
            if (!ownTroops(o.piece)) return "It carries no troops.";
            if (o.target < 0 || static_cast<size_t>(o.target) >= pieces_.size() || !combatant(o.target) ||
                pieces_[static_cast<size_t>(o.target)].kind != Kind::Planet || !hostileTo(o.piece, o.target))
                return "Troops land only on enemy planets.";
            if (dist(o.piece, o.target) > 1) return "The planet must be adjacent.";
            if (contestedBy(pieces_[static_cast<size_t>(o.target)], o.empire)) return "Another empire's troops are fighting there already.";
            return {};
        }
        case OK::Ram: {
            if (std::string e = checkPiece(o, true); !e.empty()) return e;
            if (pieces_[static_cast<size_t>(o.piece)].mp <= 0) return "No movement left to ram with.";
            if (o.target < 0 || static_cast<size_t>(o.target) >= pieces_.size() || !combatant(o.target) ||
                pieces_[static_cast<size_t>(o.target)].kind == Kind::Seeker || !hostileTo(o.piece, o.target))
                return "Only an enemy ship, unit group or planet can be rammed.";
            if (dist(o.piece, o.target) > 1) return "The target must be adjacent.";
            return {};
        }
        case OK::Capture: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            if (pieces_[static_cast<size_t>(o.piece)].kind != Kind::Vehicle) return "Only ships board.";
            if (detail::componentSum(r_, s_, pieces_[static_cast<size_t>(o.piece)].unit, AbilityKind::BoardingAttack) <= 0)
                return "It has no boarding parties.";
            if (o.target < 0 || static_cast<size_t>(o.target) >= pieces_.size() || !combatant(o.target) ||
                pieces_[static_cast<size_t>(o.target)].kind != Kind::Vehicle || !hostileTo(o.piece, o.target))
                return "Only enemy ships and bases can be captured.";
            if (dist(o.piece, o.target) > 1) return "The target must be adjacent.";
            if (pieces_[static_cast<size_t>(o.target)].sh.current > 0) return "Its shields must be down first.";
            return {};
        }
        case OK::SetLeader:
        case OK::SetMember: {
            if (std::string e = checkPiece(o, true); !e.empty()) return e;
            if (o.group < 0 || o.group >= kGroups) return "Groups are numbered 0 to 9.";
            if (o.kind == OK::SetMember) {
                for (size_t k = 0; k < pieces_.size(); ++k) {
                    const Piece& q = pieces_[k];
                    if (q.alive && q.owner == o.empire && q.isLeader && q.group == o.group)
                        return static_cast<int>(k) == o.piece ? std::string("It leads that group.") : std::string{};
                }
                return std::format("Group {} has no leader.", o.group);
            }
            return {};
        }
        case OK::ClearGroup: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            const Piece& p = pieces_[static_cast<size_t>(o.piece)];
            if (!p.isLeader && p.leader < 0) return "It is in no group.";
            return {};
        }
        case OK::ClearAllGroups: return {};
    }
    return "Unknown order.";
}

// ---- Carrying orders out ---------------------------------------------------------------------------------

std::string Battle::submit(const TacticalOrder& o) {
    if (std::string why = check(o); !why.empty()) return why;
    execute(o);
    if (stage_ == Stage::Between) advance();
    return {};
}

void Battle::play(std::span<const TacticalOrder> script) {
    size_t next = 0;
    advance();
    while (stage_ != Stage::Finished) {
        if (!playerPhase()) {
            advance();
            continue;
        }
        // A refused order (which a recorded script never has) is skipped. When
        // the script runs out, the strategies play the players' sides.
        if (next < script.size()) submit(script[next++]);
        else submit(TacticalOrder{OK::ResolveCombat, phaseEmpire_});
    }
}

void Battle::runPrefix() {
    // Drones move and attack, then seekers (spec 04 §4: always computer-controlled).
    stage_ = Stage::Orders;
    phaseDrones(phaseEmpire_);
    moveSeekers(phaseEmpire_);
}

void Battle::autoRest() {
    // The strategies finish the phase: what the side may still launch, the
    // new drones, then every piece whose strategy has not acted yet.
    const EmpireId e = phaseEmpire_;
    launchUnits(e);
    phaseDrones(e);
    phasePieces(e);
}

void Battle::finishPlayerPhase() {
    // Unused movement points are lost at the end of the phase (spec 04 §5), and
    // every piece of the side has had its turn, as after a computer phase: a
    // ship that changes sides later in the combat turn does not act again.
    for (size_t k = 0; k < pieces_.size(); ++k)
        if (pieces_[k].alive && pieces_[k].owner == phaseEmpire_) {
            pieces_[k].mp = 0;
            acted_[k] = 1;
        }
    endPhase();
}

void Battle::execute(const TacticalOrder& o) {
    const EmpireId e = phaseEmpire_;
    if (stage_ == Stage::Launch && !launchStepOrder(o.kind)) {
        if ((o.kind == OK::Auto && o.piece < 0) || o.kind == OK::ResolveCombat) {
            // The whole phase as a computer side plays it.
            if (o.kind == OK::ResolveCombat) std::erase(players_, e);
            phase(e);
            endPhase();
            return;
        }
        runPrefix();
        if (o.kind == OK::Begin) return;
        // The drones and seekers may have changed things: the order must still hold.
        if (o.kind != OK::EndPhase && !check(o).empty()) return;
    }
    switch (o.kind) {
        case OK::Begin: return;
        case OK::EndPhase: finishPlayerPhase(); return;
        case OK::ResolveCombat:
            std::erase(players_, e);
            autoRest();
            finishPlayerPhase();
            return;
        case OK::Auto:
            if (o.piece < 0) {
                autoRest();
                finishPlayerPhase();
            } else {
                act(o.piece);
            }
            return;
        case OK::Move: {
            std::vector<std::pair<int, int>> path;
            if (!o.path.empty())
                for (const Square& sq : o.path) path.emplace_back(sq.x, sq.y);
            else
                path = pathToSquare(o.piece, o.x, o.y);
            walk(o.piece, path);
            // The group follows its leader, each member to its place, with its own movement (spec 04 §5, inferred).
            if (!o.alone && pieces_[static_cast<size_t>(o.piece)].isLeader)
                for (size_t m = 0; m < pieces_.size(); ++m)
                    if (pieces_[m].alive && pieces_[m].leader == o.piece && pieces_[m].owner == e && pieces_[m].mp > 0 &&
                        pieces_[static_cast<size_t>(o.piece)].isLeader)
                        followLeader(static_cast<int>(m));
            return;
        }
        case OK::Fire: {
            // Weapons fire one at a time in design order (spec 04 §6), each ready instance on its own.
            const size_t count = pieces_[static_cast<size_t>(o.piece)].weapons.size();
            for (size_t wi = 0; wi < count; ++wi) {
                if (o.weapon >= 0 ? static_cast<int>(wi) != o.weapon : !pieces_[static_cast<size_t>(o.piece)].weapons[wi].enabled) continue;
                const int n = instances(o.piece, pieces_[static_cast<size_t>(o.piece)].weapons[wi]);
                for (int k = 0; k < n; ++k) {
                    if (o.instance >= 0 && k != o.instance) continue;
                    if (!combatant(o.piece) || !combatant(o.target)) return;
                    if (!fireProblem(o.piece, wi, k, o.target).empty()) continue;
                    shoot(o.piece, wi, static_cast<size_t>(k), o.target);
                }
            }
            return;
        }
        case OK::ToggleWeapon: {
            std::vector<Weapon>& weapons = pieces_[static_cast<size_t>(o.piece)].weapons;
            for (size_t wi = 0; wi < weapons.size(); ++wi)
                if (o.weapon < 0 || static_cast<int>(wi) == o.weapon) weapons[wi].enabled = o.on;
            return;
        }
        case OK::Launch: launchOrder(o.piece, o.design, o.count, o.group); return;
        case OK::DropTroops: dropTroops(o.piece, o.target); return;
        case OK::Ram: ram(o.piece, o.target); return;
        case OK::Capture: board(o.piece, o.target); return;
        case OK::SetLeader: setGroup(o.piece, o.group, true); return;
        case OK::SetMember: setGroup(o.piece, o.group, false); return;
        case OK::ClearGroup: leaveGroup(o.piece); return;
        case OK::ClearAllGroups:
            for (size_t k = 0; k < pieces_.size(); ++k)
                if (pieces_[k].owner == e && pieces_[k].isLeader) leaveGroup(static_cast<int>(k));
            return;
    }
}

void Battle::launchOrder(int i, DesignId design, int count, int group) {
    const int kind = launchKindOf(design);
    const size_t ki = static_cast<size_t>(kind);
    int left = std::min(count, launchLeft(i)[ki]);
    if (kind == kLaunchSatellites) {
        // The per-sector satellite cap (spec 03 §12; inferred to hold in combat).
        int present = 0;
        for (const Piece& q : pieces_)
            if (q.alive && q.owner == pieces_[static_cast<size_t>(i)].owner && q.kind == Kind::UnitGroup && q.vtype == VehicleType::Satellite)
                present += q.unit.count;
        left = std::min(left, std::max(0, satelliteCap_ - present));
    }
    size_t u = 0;
    const std::vector<UnitStack>& units = pieces_[static_cast<size_t>(i)].unit.cargo.units;
    while (u < units.size() && !(units[u].design == design && units[u].count > 0 &&
                                 !(pieces_[static_cast<size_t>(i)].kind == Kind::Planet && invaderStack(pieces_[static_cast<size_t>(i)], u))))
        ++u;
    if (u >= units.size()) return;
    left = std::min(left, units[u].count);
    // Fighters and drones in groups of the chosen size; satellites in one group (inferred, spec 04 Q37).
    const int size = kind == kLaunchSatellites ? left : std::max(1, group);
    const uint32_t sIndex = strategyIndex(i);
    const size_t before = pieces_.size();
    while (left > 0) {
        const int n = std::min(size, left);
        if (!spawnUnit(i, design, n, sIndex)) break;
        pieces_[static_cast<size_t>(i)].unit.cargo.units[u].count -= n;
        pieces_[static_cast<size_t>(i)].launchedNow[ki] += n;
        left -= n;
    }
    refreshStats(i);
    // After the launch step, drones launched now act at once (drones are always computer-controlled).
    if (stage_ == Stage::Orders)
        for (size_t k = before; k < pieces_.size(); ++k)
            if (pieces_[k].alive && isDrone(pieces_[k]) && !acted_[k]) {
                acted_[k] = 1;
                droneAct(static_cast<int>(k));
            }
}

void Battle::leaveGroup(int i) {
    Piece& p = pieces_[static_cast<size_t>(i)];
    if (p.isLeader) dissolve(i);
    Piece& q = pieces_[static_cast<size_t>(i)];
    q.leader = -1;
    q.group = -1;
    q.tacticalGroup = false;
}

void Battle::setGroup(int i, int group, bool asLeader) {
    const EmpireId e = pieces_[static_cast<size_t>(i)].owner;
    auto join = [&](int m, int lead) {
        Piece& p = pieces_[static_cast<size_t>(m)];
        p.leader = lead;
        // A member keeps the place it has now, relative to its leader (inferred).
        p.slotDx = p.x - pieces_[static_cast<size_t>(lead)].x;
        p.slotDy = p.y - pieces_[static_cast<size_t>(lead)].y;
        p.slotFixed = true;
    };
    if (!asLeader) {
        if (pieces_[static_cast<size_t>(i)].isLeader || pieces_[static_cast<size_t>(i)].leader >= 0) leaveGroup(i);
        for (size_t k = 0; k < pieces_.size(); ++k)
            if (pieces_[k].alive && pieces_[k].owner == e && pieces_[k].isLeader && pieces_[k].group == group) {
                join(i, static_cast<int>(k));
                return;
            }
        return;
    }
    // A member leaves its group first; a leader keeps its members and takes the number.
    if (!pieces_[static_cast<size_t>(i)].isLeader && pieces_[static_cast<size_t>(i)].leader >= 0) leaveGroup(i);
    // The group's earlier leader hands over: it and its members follow the new leader (inferred).
    for (size_t k = 0; k < pieces_.size(); ++k) {
        if (static_cast<int>(k) == i || !pieces_[k].alive || pieces_[k].owner != e || !pieces_[k].isLeader || pieces_[k].group != group) continue;
        std::vector<int> joiners{static_cast<int>(k)};
        for (size_t m = 0; m < pieces_.size(); ++m)
            if (pieces_[m].leader == static_cast<int>(k) && static_cast<int>(m) != i) joiners.push_back(static_cast<int>(m));
        leaveGroup(static_cast<int>(k));
        for (int m : joiners)
            if (pieces_[static_cast<size_t>(m)].alive) join(m, i);
    }
    Piece& p = pieces_[static_cast<size_t>(i)];
    p.isLeader = true;
    p.group = group;
    p.tacticalGroup = true;
}

} // namespace detail

// ---- TacticalBattle ------------------------------------------------------------------------------------------

struct TacticalBattle::Context {
    TurnContext turn;
    Rng rng;
};

TacticalBattle::TacticalBattle(const Rules& r, GameState state, Setup setup)
    : rules_(r), state_(std::make_unique<GameState>(std::move(state))), setup_(std::move(setup)) {
    ctx_ = std::make_unique<Context>(Context{TurnContext{rules_, *state_, {}, {}, {}}, Rng{}});
    // As resolveSpaceCombat: the battle's random numbers fork from the game's; mines strike first.
    ctx_->rng = state_->rng.fork();
    if (!setup_.entering) detail::resolveMines(ctx_->turn, setup_.where, {}, ctx_->rng);
    else if (!setup_.entering->empty()) detail::resolveMines(ctx_->turn, setup_.where, *setup_.entering, ctx_->rng);
    battle_ = std::make_unique<detail::Battle>(ctx_->turn, setup_.where, ctx_->rng);
    started_ = battle_->setup();
    if (started_) {
        battle_->setPlayers(setup_.players);
        battle_->advance();
    }
    refresh();
}

TacticalBattle::~TacticalBattle() = default;
TacticalBattle::TacticalBattle(TacticalBattle&&) noexcept = default;

int TacticalBattle::round() const { return battle_->round(); }
int TacticalBattle::lastRound() const { return battle_->lastRound(); }
EmpireId TacticalBattle::phaseEmpire() const { return started_ && !applied_ ? battle_->phaseEmpire() : EmpireId{}; }
bool TacticalBattle::awaitingOrders() const { return phaseEmpire().valid(); }
bool TacticalBattle::launchStep() const { return awaitingOrders() && battle_->stage() == detail::Battle::Stage::Launch; }
bool TacticalBattle::over() const { return !started_ || battle_->over(); }
bool TacticalBattle::finished() const { return !started_ || applied_ || battle_->stage() == detail::Battle::Stage::Finished; }
const std::vector<EmpireId>& TacticalBattle::participants() const { return battle_->empires(); }
const std::vector<EmpireId>& TacticalBattle::phaseOrder() const { return battle_->phaseOrder(); }
bool TacticalBattle::isPlayer(EmpireId e) const { return battle_->isPlayer(e); }
bool TacticalBattle::hostile(EmpireId a, EmpireId b) const { return a.valid() && b.valid() && a != b && detail::enemies(*state_, a, b); }

const CombatRecord& TacticalBattle::record() const {
    // Once applied, the record lives in the state's battle list (as resolveSpaceCombat leaves it).
    if (applied_ && !state_->combats.empty()) return state_->combats.back();
    return battle_->record();
}

int TacticalBattle::pieceAt(int x, int y) const { return started_ && !applied_ ? battle_->occupant(x, y) : -1; }

std::vector<Square> TacticalBattle::pathTo(int piece, int x, int y) const {
    std::vector<Square> out;
    if (!started_ || applied_ || piece < 0 || static_cast<size_t>(piece) >= views_.size() || !detail::onMap(x, y)) return out;
    for (const auto& [px, py] : battle_->pathToSquare(piece, x, y)) out.push_back(Square{static_cast<int16_t>(px), static_cast<int16_t>(py)});
    return out;
}

namespace {

bool validPiece(const std::vector<TacticalPiece>& views, int i) { return i >= 0 && static_cast<size_t>(i) < views.size(); }

} // namespace

int TacticalBattle::distance(int a, int b) const { return validPiece(views_, a) && validPiece(views_, b) && !applied_ ? battle_->dist(a, b) : 0; }

int TacticalBattle::hitChance(int piece, int weapon, int target) const {
    if (applied_ || !validPiece(views_, piece) || !validPiece(views_, target) || weapon < 0 ||
        static_cast<size_t>(weapon) >= battle_->pieces()[static_cast<size_t>(piece)].weapons.size())
        return 0;
    const detail::Piece& p = battle_->pieces()[static_cast<size_t>(piece)];
    const detail::Weapon& w = p.weapons[static_cast<size_t>(weapon)];
    if (w.kind() == ruleset::WeaponKind::Seeking) return 100;   // seekers never roll
    return p.alwaysHit ? 100 : battle_->hitChance(piece, w, target);
}

int TacticalBattle::damageAt(int piece, int weapon, int target) const {
    if (applied_ || !validPiece(views_, piece) || !validPiece(views_, target) || weapon < 0 ||
        static_cast<size_t>(weapon) >= battle_->pieces()[static_cast<size_t>(piece)].weapons.size())
        return 0;
    const detail::Piece& p = battle_->pieces()[static_cast<size_t>(piece)];
    const detail::Weapon& w = p.weapons[static_cast<size_t>(weapon)];
    return weaponDamage(rules_, w.de, battle_->dist(piece, target)) * battle_->firedTogether(piece, w);
}

std::string TacticalBattle::fireProblem(int piece, int weapon, int target) const {
    if (!started_ || applied_ || !validPiece(views_, piece) || weapon < 0) return "No such weapon.";
    if (!battle_->pieces()[static_cast<size_t>(piece)].alive) return "That piece is gone.";
    return battle_->fireProblem(piece, static_cast<size_t>(weapon), -1, target);
}

std::string TacticalBattle::check(const TacticalOrder& o) const {
    if (!started_ || applied_) return "The battle is over.";
    return battle_->check(o);
}

std::string TacticalBattle::submit(const TacticalOrder& o) {
    if (!started_ || applied_) return "The battle is over.";
    std::string why = battle_->submit(o);
    if (why.empty()) script_.push_back(o);
    refresh();
    return why;
}

void TacticalBattle::recordStrategies(std::vector<TacticalOrder>* out) { battle_->recordStrategies(out); }

void TacticalBattle::finish() {
    if (!started_ || applied_) return;
    // Phases left over are played by the strategies (a script that runs out does the same).
    battle_->play({});
    refresh();   // the views keep the battle's last picture; applying the results tidies the pieces away
    battle_->finish();
    applied_ = true;
}

void TacticalBattle::refresh() {
    views_.clear();
    if (!started_) return;
    const std::vector<detail::Piece>& pieces = battle_->pieces();
    views_.reserve(pieces.size());
    for (size_t k = 0; k < pieces.size(); ++k) {
        const int i = static_cast<int>(k);
        const detail::Piece& p = pieces[k];
        TacticalPiece v;
        v.kind = p.kind;
        v.owner = p.owner;
        v.startOwner = p.startOwner;
        v.vehicle = p.source;
        v.planet = p.object;
        v.design = p.kind == CombatPiece::Kind::Planet || p.kind == CombatPiece::Kind::Obstacle ? DesignId{} : p.unit.design;
        v.name = p.name;
        v.type = p.vtype;
        v.x = p.x;
        v.y = p.y;
        v.size = p.size;
        v.facing = p.facing;
        v.alive = p.alive;
        v.mothballed = p.mothballed;
        v.captured = p.captured;
        v.shields = p.sh.current;
        v.shieldsMax = p.sh.max;
        v.acted = battle_->acted(i);
        v.leader = p.leader >= 0 && pieces[static_cast<size_t>(p.leader)].isLeader ? p.leader : -1;
        v.isLeader = p.isLeader;
        v.group = p.isLeader ? p.group : v.leader >= 0 ? pieces[static_cast<size_t>(v.leader)].group : -1;
        v.seekTarget = p.seekTarget;
        v.launcher = p.launcher;
        v.carrier = p.carrier;
        if (p.kind == CombatPiece::Kind::Seeker) {
            v.count = p.members;
            v.hitPoints = battle_->hitPoints(i);
            v.movement = v.movementMax = p.speed;
            views_.push_back(std::move(v));
            continue;
        }
        if (p.kind == CombatPiece::Kind::Obstacle) {
            views_.push_back(std::move(v));
            continue;
        }
        v.hitPoints = battle_->hitPoints(i);
        v.damagePercent = battle_->damagePercent(i);
        v.movement = p.mp;
        v.movementMax = p.alive ? battle_->maxMovement(i) : 0;
        v.supply = p.unit.supply;
        v.hasSupply = battle_->hasSupply(i);
        v.count = p.kind == CombatPiece::Kind::UnitGroup ? p.unit.count : 1;
        v.budget = p.budget;
        v.engaged = static_cast<int>(p.engaged.size());
        for (const detail::Weapon& w : p.weapons) {
            TacticalWeapon tw;
            tw.component = w.de.component;
            tw.entry = w.entry;
            tw.kind = w.kind();
            tw.reload = w.reload;
            tw.reloadRate = w.reloadRate;
            tw.instances = p.alive ? battle_->instances(i, w) : 0;
            tw.together = battle_->firedTogether(i, w);
            tw.reach = w.reach;
            tw.targets = w.targets;
            tw.enabled = w.enabled;
            v.weapons.push_back(std::move(tw));
        }
        for (size_t u = 0; u < p.unit.cargo.units.size(); ++u)
            if (p.unit.cargo.units[u].count > 0 && !(p.kind == CombatPiece::Kind::Planet && battle_->invaderStack(p, u)))
                v.cargo.push_back(p.unit.cargo.units[u]);
        if (p.alive) v.launchLeft = battle_->launchLeft(i);
        if (p.kind == CombatPiece::Kind::Vehicle) {
            v.boardingAttack = detail::componentSum(rules_, *state_, p.unit, AbilityKind::BoardingAttack);
            v.troops = battle_->ownTroops(i);
        }
        views_.push_back(std::move(v));
    }
}

} // namespace opense4::game::combat
