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
        case TacticalOrder::Kind::Move: return "Move";
        case TacticalOrder::Kind::Fire: return "Fire";
        case TacticalOrder::Kind::ToggleWeapon: return "ToggleWeapon";
        case TacticalOrder::Kind::Launch: return "Launch";
        case TacticalOrder::Kind::LaunchFighters: return "LaunchFighters";
        case TacticalOrder::Kind::DropTroops: return "DropTroops";
        case TacticalOrder::Kind::Ram: return "Ram";
        case TacticalOrder::Kind::Capture: return "Capture";
        case TacticalOrder::Kind::SetLeader: return "SetLeader";
        case TacticalOrder::Kind::SetMember: return "SetMember";
        case TacticalOrder::Kind::ClearGroup: return "ClearGroup";
        case TacticalOrder::Kind::ClearAllGroups: return "ClearAllGroups";
        case TacticalOrder::Kind::Auto: return "Auto";
        case TacticalOrder::Kind::AutoPhase: return "AutoPhase";
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
    // Spec 04 §6: intact, reload counter 0, supplies, a hostile target of its set,
    // damage at the range. The damage type is not checked when firing by hand.
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

std::string Battle::checkLaunch(const TacticalOrder& o) const {
    if (std::string e = checkPiece(o, false); !e.empty()) return e;
    const Piece& p = pieces_[static_cast<size_t>(o.piece)];
    if (p.kind != Kind::Vehicle && p.kind != Kind::Planet) return "Only ships and planets launch units.";
    if (o.count <= 0) return "Nothing to launch.";
    if (!o.design.valid() || o.design.index() >= s_.designs.size() ||
        std::none_of(p.unit.cargo.units.begin(), p.unit.cargo.units.end(), [&](const UnitStack& u) { return u.design == o.design && u.count > 0; }))
        return "It carries no such units.";
    const int kind = launchKindOf(o.design);
    if (kind < 0) return "Only fighters, satellites and drones are launched in combat.";
    if (o.kind == OK::LaunchFighters) {
        // Launch Fighters in Groups: fighters only, in groups of 5 to 50 (spec 04 §10.4).
        if (kind != kLaunchFighters) return "Only fighters are launched in groups.";
        if (std::find(kFighterGroupSizes.begin(), kFighterGroupSizes.end(), o.group) == kFighterGroupSizes.end())
            return "Fighter groups hold 5, 8, 10, 15, 20, 30, 40 or 50.";
    } else if (o.group < 0) {
        return "No such launch window.";
    }
    if (launchLeft(o.piece)[static_cast<size_t>(kind)] <= 0) return "It cannot launch more of those this turn.";
    if (kind == kLaunchSatellites && satellitesPresent(p.owner) >= satelliteCap_) return "The satellite limit for this sector is reached.";
    return {};
}

std::string Battle::check(const TacticalOrder& o) const {
    if (stage_ == Stage::Finished) return "The battle is over.";
    if (!playerPhase()) return "No side is giving orders now.";
    if (o.empire != phaseEmpire_) return "It is not that side's phase.";
    if (stage_ == Stage::Paused && o.kind != OK::EndPhase && o.kind != OK::ResolveCombat && !(o.kind == OK::Auto && o.piece < 0))
        return "Play is paused: End Turn goes on.";
    switch (o.kind) {
        case OK::EndPhase:
        case OK::ResolveCombat:
        case OK::AutoPhase: return {};
        case OK::Auto: {
            if (o.piece < 0) return {};
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            if (acted(o.piece)) return "It has already had its turn.";
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
        case OK::Launch:
        case OK::LaunchFighters: return checkLaunch(o);
        case OK::DropTroops: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            if (pieces_[static_cast<size_t>(o.piece)].kind != Kind::Vehicle) return "Only ships drop troops.";
            if (!hasTroops(o.piece)) return "It carries no troops.";
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
            // Spec 04 §5 (confirmed: binary).
            if (std::string e = checkPiece(o, true); !e.empty()) return e;
            if (o.group < 0 || o.group >= kGroups) return "Groups are numbered 0 to 9.";
            const Piece& p = pieces_[static_cast<size_t>(o.piece)];
            if (p.isLeader || p.group >= 0 || leaderOf(o.piece) >= 0 || p.fleetMember) return "It already belongs to a group.";
            int leader = -1;
            for (size_t k = 0; k < pieces_.size(); ++k)
                if (pieces_[k].alive && pieces_[k].owner == o.empire && pieces_[k].isLeader && pieces_[k].group == o.group) leader = static_cast<int>(k);
            if (o.kind == OK::SetMember) return leader >= 0 ? std::string{} : std::format("Group {} has no leader.", o.group);
            if (leader >= 0) return std::format("Group {} already has a leader.", o.group);
            const size_t formations = r_.data().formations.size();
            if (formations > 0 && (o.formation < 0 || static_cast<size_t>(o.formation) >= formations)) return "Pick a formation for the group.";
            if (formations == 0 && o.formation >= 0) return "There are no formations.";
            return {};
        }
        case OK::ClearGroup: {
            if (std::string e = checkPiece(o, false); !e.empty()) return e;
            const Piece& p = pieces_[static_cast<size_t>(o.piece)];
            if (!p.isLeader && p.group < 0 && p.leader < 0 && !p.fleetMember) return "It is in no group.";
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

void Battle::startPlayerPhase() {
    // The side's drones move and attack, then its seekers, before the player
    // gets control (spec 04 §4: always computer-controlled).
    stage_ = Stage::Orders;
    phaseDrones(phaseEmpire_);
    moveSeekers(phaseEmpire_);
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
    switch (o.kind) {
        case OK::EndPhase:
            if (stage_ == Stage::Paused) stage_ = Stage::Between;   // End Turn goes on
            else finishPlayerPhase();
            return;
        case OK::ResolveCombat:
            // Every empire to its strategies until the battle ends (spec 04 §4).
            players_.clear();
            autoAll_ = false;
            if (stage_ == Stage::Paused) {
                stage_ = Stage::Between;
                return;
            }
            phasePieces(e);
            finishPlayerPhase();
            return;
        case OK::Auto:
            if (o.piece < 0) {
                // One toggle for every empire, from the next phase on (spec 04 §4).
                autoAll_ = o.on;
                if (!o.on && release_) players_ = *release_;
            } else {
                act(o.piece);   // an OpenSE4 extension: this piece acts by its strategy now
            }
            return;
        case OK::AutoPhase:
            phasePieces(e);   // an OpenSE4 extension: the strategies play the rest of the phase
            finishPlayerPhase();
            return;
        case OK::Move: {
            std::vector<std::pair<int, int>> path;
            if (!o.path.empty())
                for (const Square& sq : o.path) path.emplace_back(sq.x, sq.y);
            else
                path = pathToSquare(o.piece, o.x, o.y);
            walk(o.piece, path);
            // The group follows its leader, each member to its place, with its own movement (spec 04 §5).
            if (!o.alone && pieces_[static_cast<size_t>(o.piece)].isLeader)
                for (size_t m = 0; m < pieces_.size(); ++m)
                    if (pieces_[m].alive && leaderOf(static_cast<int>(m)) == o.piece && pieces_[m].owner == e && pieces_[m].mp > 0)
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
        case OK::Launch:
        case OK::LaunchFighters: launchOrder(o); return;
        case OK::DropTroops: dropTroops(o.piece, o.target); return;
        case OK::Ram: ram(o.piece, o.target); return;
        case OK::Capture: board(o.piece, o.target); return;
        case OK::SetLeader: setGroup(o.piece, o.group, true, o.formation); return;
        case OK::SetMember: setGroup(o.piece, o.group, false, -1); return;
        case OK::ClearGroup: leaveGroup(o.piece); return;
        case OK::ClearAllGroups:
            for (size_t k = 0; k < pieces_.size(); ++k)
                if (pieces_[k].owner == e && (pieces_[k].isLeader || pieces_[k].group >= 0 || pieces_[k].leader >= 0 || pieces_[k].fleetMember))
                    leaveGroup(static_cast<int>(k));
            return;
    }
}

// Spec 04 §10.4 (confirmed: binary). "Launch Units": units of one kind launched
// from the piece in one window session share the group made first, whatever
// their design; drones are one per group. "Launch Fighters in Groups": fighters
// in single-design groups of the chosen size. Every launch stays within the
// per-turn rate (and the satellite cap). Drones a player launches first act at
// the side's next phase.
void Battle::launchOrder(const TacticalOrder& o) {
    const int i = o.piece;
    const int kind = launchKindOf(o.design);
    const size_t ki = static_cast<size_t>(kind);
    int left = std::min(o.count, launchLeft(i)[ki]);
    if (kind == kLaunchSatellites) left = std::min(left, std::max(0, satelliteCap_ - satellitesPresent(pieces_[static_cast<size_t>(i)].owner)));
    size_t u = 0;
    const std::vector<UnitStack>& units = pieces_[static_cast<size_t>(i)].unit.cargo.units;
    while (u < units.size() && !(units[u].design == o.design && units[u].count > 0)) ++u;
    if (u >= units.size()) return;
    left = std::min(left, units[u].count);
    if (left <= 0) return;
    const uint32_t sIndex = strategyIndex(i);
    int launched = 0;
    auto spawned = [&](int idx, int n) {
        if (idx < 0) return false;
        if (isDrone(pieces_[static_cast<size_t>(idx)])) acted_[static_cast<size_t>(idx)] = 1;   // it first acts next phase
        launched += n;
        return true;
    };
    if (kind == kLaunchDrones) {
        for (int n = 0; n < left; ++n)
            if (!spawned(spawnUnit(i, o.design, 1, sIndex), 1)) break;
    } else if (o.kind == OK::LaunchFighters) {
        while (launched < left) {
            const int n = std::min(o.group, left - launched);
            if (!spawned(spawnUnit(i, o.design, n, sIndex), n)) break;
        }
    } else {
        const std::tuple<int, int, int> key{i, o.group, kind};
        const auto it = launchGroups_.find(key);
        if (it != launchGroups_.end() && pieces_[static_cast<size_t>(it->second)].alive) {
            joinUnit(it->second, o.design, left);
            launched = left;
        } else {
            const int idx = spawnUnit(i, o.design, left, sIndex);
            if (spawned(idx, left)) launchGroups_[key] = idx;
        }
    }
    pieces_[static_cast<size_t>(i)].unit.cargo.units[u].count -= launched;
    pieces_[static_cast<size_t>(i)].launchedNow[ki] += launched;
    refreshStats(i);
}

void Battle::leaveGroup(int i) {
    // Clear Group Assignment clears only that piece: members of a cleared leader
    // keep their number and follow whichever piece leads it later (spec 04 §5).
    // A fleet's members keep the fleet's group, with no leader to follow, as
    // when the leader leaves the formation (spec 03 §10).
    Piece& q = pieces_[static_cast<size_t>(i)];
    q.isLeader = false;
    q.leader = -1;
    q.group = -1;
    q.tacticalGroup = false;
    q.hasSlot = false;
    q.fleetMember = false;
}

void Battle::setGroup(int i, int group, bool asLeader, int formation) {
    Piece& p = pieces_[static_cast<size_t>(i)];
    auto& g = groups_[{p.owner.value, group}];
    p.group = group;
    if (asLeader) {
        // The leader picks the formation its members take their places in.
        p.isLeader = true;
        p.tacticalGroup = true;
        g.formation = formation;
        return;
    }
    // The member takes the next position of the leader's formation, turned by
    // the leader's facing (spec 04 §5); none left: it keeps no place (inferred).
    p.hasSlot = false;
    const ruleset::Formation* f = g.formation >= 0 && static_cast<size_t>(g.formation) < r_.data().formations.size()
                                      ? &r_.data().formations[static_cast<size_t>(g.formation)]
                                      : nullptr;
    if (f && static_cast<size_t>(g.nextSlot) < f->positions.size()) {
        const auto& pos = f->positions[static_cast<size_t>(g.nextSlot++)];
        p.slotDx = pos.x - f->leader.x;
        p.slotDy = pos.y - f->leader.y;
        p.hasSlot = true;
    }
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
    if (setup_.interference || setup_.disruption)
        battle_->setOverrides(detail::BattleOverrides{setup_.interference.value_or(0), setup_.disruption.value_or(0)});
    started_ = battle_->setup();
    if (started_) {
        battle_->setPlayers(setup_.players, setup_.release);
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
bool TacticalBattle::paused() const { return awaitingOrders() && battle_->stage() == detail::Battle::Stage::Paused; }
bool TacticalBattle::autoOn() const { return started_ && battle_->autoOn(); }
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
        v.leader = p.alive && p.kind != CombatPiece::Kind::Seeker && p.kind != CombatPiece::Kind::Obstacle ? battle_->leaderOf(i) : -1;
        v.isLeader = p.isLeader;
        v.group = p.group;
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
        if (p.kind == CombatPiece::Kind::UnitGroup)
            for (const UnitStack& st : p.stacks)
                if (st.count > 0) v.units.push_back(st);
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
        for (const UnitStack& st : p.unit.cargo.units)
            if (st.count > 0) v.cargo.push_back(st);
        for (const UnitStack& st : p.landed)
            if (st.count > 0) v.landed.push_back(st);
        if (!v.landed.empty()) v.invader = p.invader;
        if (p.alive) v.launchLeft = battle_->launchLeft(i);
        if (p.kind == CombatPiece::Kind::Vehicle) {
            v.boardingAttack = detail::componentSum(rules_, *state_, p.unit, AbilityKind::BoardingAttack);
            v.troops = battle_->hasTroops(i);
        }
        views_.push_back(std::move(v));
    }
}

} // namespace opense4::game::combat
