#include "client/classic/screens/combat_logic.hpp"

#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace opense4::client::classic {

namespace {

using PieceKind = game::CombatPiece::Kind;

const ruleset::VehicleSize* hullOf(const game::Rules& r, const game::GameState& s, game::DesignId d) {
    if (!d.valid() || d.index() >= s.designs.size()) return nullptr;
    const uint32_t hull = s.design(d).hull;
    return hull < r.data().vehicleSizes.size() ? &r.hull(hull) : nullptr;
}

std::string objectName(const game::GameState& s, game::ObjectId o) {
    return o.valid() && o.index() < s.galaxy.objects.size() ? s.galaxy.object(o).name : std::string("Planet");
}

} // namespace

// ---- Strategic Combat: the forces list -----------------------------------------------------------------

void CombatForces::reset() {
    sides_.clear();
    highest_.clear();
    order_.clear();
}

void CombatForces::update(const game::Rules& r, const game::GameState& s, const game::CombatRecord& record,
                          const std::vector<CombatPlayback::Piece>& pieces) {
    std::map<std::pair<uint32_t, std::string>, int> now;
    for (size_t i = 0; i < record.pieces.size() && i < pieces.size(); ++i) {
        const game::CombatPiece& rp = record.pieces[i];
        const CombatPlayback::Piece& p = pieces[i];
        if (rp.kind == PieceKind::Seeker || rp.kind == PieceKind::Obstacle || rp.kind == PieceKind::Planet) continue;
        const bool alive = p.onMap && !p.destroyed && !p.neutral && p.owner.valid();
        const ruleset::VehicleSize* hull = hullOf(r, s, rp.design);
        const std::string name = hull ? hull->name : std::string("Unknown");
        // A launched group appears on its first event; units still in cargo do not count.
        const bool shown = p.onMap || p.destroyed || p.captured;
        if (!shown) continue;
        const std::pair<uint32_t, std::string> key{p.owner.valid() ? p.owner.value : rp.owner.value, name};
        const std::pair<uint32_t, std::string> startKey{rp.owner.value, name};
        if (std::find(order_.begin(), order_.end(), startKey) == order_.end()) order_.push_back(startKey);
        if (!alive) continue;
        if (std::find(order_.begin(), order_.end(), key) == order_.end()) order_.push_back(key);
        now[key] += rp.kind == PieceKind::UnitGroup ? std::max(0, p.units) : 1;
    }
    for (const auto& key : order_) {
        int& high = highest_[key];
        high = std::max(high, now[key]);
    }
    sides_.clear();
    for (game::EmpireId e : record.participants) {
        ForceSide side;
        side.empire = e;
        for (const auto& key : order_) {
            if (key.first != e.value) continue;
            ForceRow row;
            row.name = key.second;
            row.current = now[key];
            row.lost = highest_[key] - row.current;
            side.rows.push_back(std::move(row));
        }
        size_t planets = 0;
        for (size_t i = 0; i < record.pieces.size() && i < pieces.size() && planets < kForcePlanets; ++i) {
            const game::CombatPiece& rp = record.pieces[i];
            if (rp.kind != PieceKind::Planet || rp.owner != e) continue;
            const CombatPlayback::Piece& p = pieces[i];
            const bool stands = !p.destroyed && !p.neutral && p.owner == e;
            ForceRow row;
            row.name = rp.name.empty() ? objectName(s, rp.planet) : rp.name;
            row.planet = true;
            row.current = stands ? 1 : 0;
            row.lost = stands ? 0 : 1;
            side.rows.push_back(std::move(row));
            ++planets;
        }
        sides_.push_back(std::move(side));
    }
}

// ---- Tactical Combat ---------------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>> pieceReportLines(const game::Rules& r, const game::GameState& s,
                                                                  const std::vector<game::combat::TacticalPiece>& pieces, int piece) {
    std::vector<std::pair<std::string, std::string>> out;
    if (piece < 0 || size_t(piece) >= pieces.size()) return out;
    const game::combat::TacticalPiece& p = pieces[size_t(piece)];
    const bool planet = p.kind == PieceKind::Planet;
    const bool drone = p.kind == PieceKind::UnitGroup && p.type == ruleset::VehicleType::Drone;
    const game::Colony* colony = planet && p.planet.valid() && p.planet.index() < s.galaxy.objects.size() ? s.colony(p.planet) : nullptr;
    if (planet) out.emplace_back("Population", colony ? std::format("{}M", colony->totalPopulation()) : std::string("None"));
    else out.emplace_back("Movement", std::format("{}/{}", p.movement, p.movementMax));
    out.emplace_back("Shields", std::format("{}/{}", p.shields, p.shieldsMax));
    // Damage taken against the maximum: a ship's structure; otherwise the percentage the battle reports.
    std::string damage = std::format("{}%", p.damagePercent);
    if (p.kind == PieceKind::Vehicle && p.vehicle.valid())
        if (const game::Vehicle* v = s.vehicle(p.vehicle)) {
            const int64_t max = game::vehicleStructure(r, s, *v);
            if (max > 0) damage = std::format("{}/{}", std::clamp<int64_t>(max - p.hitPoints, 0, max), max);
        }
    out.emplace_back("Damage", damage);
    out.emplace_back("Supply", planet ? std::string("-") : !p.hasSupply ? std::string("None") : std::format("{}", p.supply));
    out.emplace_back("Max Targets", std::format("{}", p.budget));
    if (drone) {
        const bool known = p.seekTarget >= 0 && size_t(p.seekTarget) < pieces.size();
        out.emplace_back("Target", known ? pieces[size_t(p.seekTarget)].name : std::string("None"));
    } else {
        std::string group = "None";
        // A fleet's own combat group has no number (inferred: shown as "Fleet").
        const std::string number = p.group >= 0 ? std::format("Group {}", p.group) : std::string("Fleet");
        if (p.isLeader) group = number + " - Leader";
        else if (p.leader >= 0 || p.group >= 0) group = number + " - Wingman";
        out.emplace_back("Combat Group", group);
        std::string formation = "None";
        if (const game::Vehicle* v = p.vehicle.valid() ? s.vehicle(p.vehicle) : nullptr)
            if (const game::Fleet* f = s.fleet(v->fleet); f && f->formation < r.data().formations.size())
                formation = r.data().formations[f->formation].name;
        out.emplace_back("Formation", formation);
    }
    if (colony && colony->plagueLevel > 0) out.emplace_back("Conditions", std::format("Plague level {}", colony->plagueLevel));
    return out;
}

DropTarget dropTroopsTarget(const game::combat::TacticalBattle& b, int piece) {
    DropTarget out;
    const auto& pieces = b.pieces();
    if (piece < 0 || size_t(piece) >= pieces.size()) {
        out.problem = "Select a ship with troops first.";
        return out;
    }
    const game::combat::TacticalPiece& ship = pieces[size_t(piece)];
    for (size_t j = 0; j < pieces.size(); ++j) {
        const game::combat::TacticalPiece& q = pieces[j];
        if (!q.alive || q.kind != PieceKind::Planet || !q.owner.valid() || q.owner == ship.owner || b.distance(piece, int(j)) > 1) continue;
        // Refused when another empire's troops already fight on it (spec 06 §1.10.2).
        if (!q.landed.empty() && q.invader.valid() && q.invader != ship.owner) {
            if (out.problem.empty()) out.problem = "Another empire's troops are fighting there already.";
            continue;
        }
        game::combat::TacticalOrder o{game::combat::TacticalOrder::Kind::DropTroops, ship.owner, piece, int(j)};
        if (std::string why = b.check(o); !why.empty()) {
            if (out.problem.empty()) out.problem = why;
            continue;
        }
        out.planet = int(j);
        out.problem.clear();
        return out;
    }
    if (out.problem.empty()) out.problem = "No colony of another empire is adjacent.";
    return out;
}

// ---- Combat Simulator ---------------------------------------------------------------------------

std::vector<SimulatorRow> simulatorRows(const game::Rules& r, const game::GameState& s, const game::combat::SimulatorSetup& setup) {
    using game::combat::SimulatorItem;
    std::vector<SimulatorRow> out;
    const std::vector<std::string> names = game::combat::simulatorItemNames(r, s, setup);
    auto unitType = [&](const SimulatorItem& i) -> std::optional<ruleset::VehicleType> {
        if (i.kind != SimulatorItem::Kind::Design) return std::nullopt;
        const ruleset::VehicleSize* hull = hullOf(r, s, i.design);
        if (!hull || !game::isUnitType(hull->type)) return std::nullopt;
        return hull->type;
    };
    for (int side = 0; side < int(setup.sides.size()); ++side) {
        std::map<ruleset::VehicleType, size_t> groups;   // the side's group row per unit kind
        for (size_t k = 0; k < setup.items.size(); ++k) {
            const SimulatorItem& i = setup.items[k];
            if (i.side != side || (i.kind == SimulatorItem::Kind::Planet && !game::combat::simulatorColony(s, i))) continue;
            const auto type = unitType(i);
            if (type && *type != ruleset::VehicleType::Drone) {
                if (auto it = groups.find(*type); it != groups.end()) {
                    out[it->second].items.push_back(k);
                    out[it->second].units += i.count;
                    continue;
                }
                groups.emplace(*type, out.size());
            }
            SimulatorRow row;
            row.side = side;
            row.items.push_back(k);
            row.design = i.design;
            row.object = i.kind == SimulatorItem::Kind::Planet ? i.planet : game::ObjectId{};
            row.units = type ? i.count : 0;
            row.name = k < names.size() ? names[k] : std::string("?");
            if (!type && i.kind == SimulatorItem::Kind::Design && i.count > 1) row.name += std::format(" (x{})", i.count);
            out.push_back(std::move(row));
        }
    }
    for (size_t k = 0; k < setup.items.size(); ++k) {
        const SimulatorItem& i = setup.items[k];
        if (i.kind != SimulatorItem::Kind::Planet || game::combat::simulatorColony(s, i)) continue;
        SimulatorRow row;
        row.items.push_back(k);
        row.object = i.planet;
        row.name = k < names.size() ? names[k] : std::string("?");
        out.push_back(std::move(row));
    }
    return out;
}

bool simulatorAdd(const game::Rules& r, const game::GameState& s, game::combat::SimulatorSetup& setup, game::combat::SimulatorItem item) {
    using game::combat::SimulatorItem;
    item.count = 1;
    if (item.kind == SimulatorItem::Kind::Planet) {
        for (const SimulatorItem& i : setup.items)
            if (i.kind == SimulatorItem::Kind::Planet && i.planet == item.planet) return false;
        setup.items.push_back(std::move(item));
        return true;
    }
    const ruleset::VehicleSize* hull = hullOf(r, s, item.design);
    if (hull && game::isUnitType(hull->type) && hull->type != ruleset::VehicleType::Drone)
        for (SimulatorItem& i : setup.items)
            if (i.kind == SimulatorItem::Kind::Design && i.design == item.design && i.side == item.side && i.cargo.empty() && i.fleet < 0) {
                if (i.count >= game::combat::kSimulatorMaxCount) return false;
                ++i.count;
                return true;
            }
    setup.items.push_back(std::move(item));
    return true;
}

void simulatorRemove(game::combat::SimulatorSetup& setup, const SimulatorRow& row) {
    std::vector<size_t> items = row.items;
    std::sort(items.rbegin(), items.rend());
    for (size_t k : items)
        if (k < setup.items.size()) setup.items.erase(setup.items.begin() + std::ptrdiff_t(k));
}

} // namespace opense4::client::classic
