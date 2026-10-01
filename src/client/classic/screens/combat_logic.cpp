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
    rows_.clear();
    sides_.clear();
    ready_ = false;
}

namespace {

std::optional<uint32_t> hullIndex(const game::Rules& r, const game::GameState& s, game::DesignId d) {
    if (!d.valid() || d.index() >= s.designs.size()) return std::nullopt;
    const uint32_t hull = s.design(d).hull;
    if (hull >= r.data().vehicleSizes.size()) return std::nullopt;
    return hull;
}

using HullCounts = std::map<std::pair<uint32_t, uint32_t>, int>;   // (empire, hull) -> units
using PlanetNames = std::map<uint32_t, std::vector<std::string>>;   // empire -> its colonized planets' names, piece order

void countPieces(const game::Rules& r, const game::GameState& s, const std::vector<game::combat::TacticalPiece>& pieces, HullCounts& hulls,
                 PlanetNames& planets) {
    for (const game::combat::TacticalPiece& p : pieces) {
        if (!p.alive || !p.owner.valid()) continue;
        if (p.kind == PieceKind::Vehicle) {
            if (const auto h = hullIndex(r, s, p.design)) hulls[{p.owner.value, *h}] += 1;
        } else if (p.kind == PieceKind::UnitGroup) {
            for (const game::UnitStack& st : p.units)
                if (const auto h = hullIndex(r, s, st.design); h && st.count > 0) hulls[{p.owner.value, *h}] += st.count;
        } else if (p.kind == PieceKind::Planet) {
            planets[p.owner.value].push_back(p.name.empty() ? objectName(s, p.planet) : p.name);
        }
    }
}

void countRecord(const game::Rules& r, const game::GameState& s, const game::CombatRecord& record, const std::vector<CombatPlayback::Piece>& pieces,
                 HullCounts& hulls, PlanetNames& planets) {
    for (size_t i = 0; i < record.pieces.size() && i < pieces.size(); ++i) {
        const game::CombatPiece& rp = record.pieces[i];
        const CombatPlayback::Piece& p = pieces[i];
        if (!p.onMap || p.destroyed || p.neutral || !p.owner.valid()) continue;
        if (rp.kind == PieceKind::Vehicle) {
            if (const auto h = hullIndex(r, s, rp.design)) hulls[{p.owner.value, *h}] += 1;
        } else if (rp.kind == PieceKind::UnitGroup) {
            if (const auto h = hullIndex(r, s, rp.design)) hulls[{p.owner.value, *h}] += std::max(0, p.units);
        } else if (rp.kind == PieceKind::Planet) {
            planets[p.owner.value].push_back(rp.name.empty() ? objectName(s, rp.planet) : rp.name);
        }
    }
}

} // namespace

void CombatForces::build(const HullCounts& atStart, const PlanetNames& planets) {
    rows_.clear();
    std::map<uint32_t, Side> byEmpire;
    for (const auto& [key, n] : atStart) {
        Side& side = byEmpire[key.first];
        side.empire = game::EmpireId{key.first};
        side.hulls.push_back(Hull{key.second, n});
    }
    for (const auto& [e, names] : planets) {
        Side& side = byEmpire[e];
        side.empire = game::EmpireId{e};
        for (size_t k = 0; k < names.size() && k < kForcePlanets; ++k) side.planets.push_back(names[k]);
    }
    for (auto& [e, side] : byEmpire) rows_.push_back(std::move(side));   // player-number order; hulls in VehicleSize order
    ready_ = true;
}

void CombatForces::apply(const game::Rules& r, const HullCounts& now, const PlanetNames& standing) {
    sides_.clear();
    for (Side& side : rows_) {
        ForceSide out;
        out.empire = side.empire;
        for (Hull& h : side.hulls) {
            const auto it = now.find({side.empire.value, h.hull});
            const int current = it == now.end() ? 0 : it->second;
            h.highest = std::max(h.highest, current);
            out.rows.push_back(ForceRow{r.hull(h.hull).name, false, current, h.highest - current});
        }
        const auto names = standing.find(side.empire.value);
        for (const std::string& name : side.planets) {
            const bool stands = names != standing.end() && std::find(names->second.begin(), names->second.end(), name) != names->second.end();
            out.rows.push_back(ForceRow{name, true, stands ? 1 : 0, stands ? 0 : 1});
        }
        sides_.push_back(std::move(out));
    }
}

void CombatForces::setup(const game::Rules& r, const game::GameState& s, const std::vector<game::combat::TacticalPiece>& pieces) {
    HullCounts hulls;
    PlanetNames planets;
    countPieces(r, s, pieces, hulls, planets);
    build(hulls, planets);
    apply(r, hulls, planets);
}

void CombatForces::count(const game::Rules& r, const game::GameState& s, const std::vector<game::combat::TacticalPiece>& pieces) {
    HullCounts hulls;
    PlanetNames planets;
    countPieces(r, s, pieces, hulls, planets);
    apply(r, hulls, planets);
}

void CombatForces::setup(const game::Rules& r, const game::GameState& s, const game::CombatRecord& record,
                         const std::vector<CombatPlayback::Piece>& atStart) {
    HullCounts hulls;
    PlanetNames planets;
    countRecord(r, s, record, atStart, hulls, planets);
    build(hulls, planets);
    apply(r, hulls, planets);
}

void CombatForces::count(const game::Rules& r, const game::GameState& s, const game::CombatRecord& record,
                         const std::vector<CombatPlayback::Piece>& pieces) {
    HullCounts hulls;
    PlanetNames planets;
    countRecord(r, s, record, pieces, hulls, planets);
    apply(r, hulls, planets);
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
    if (!hull) return false;
    // Designs fight with their real owner's strategy (spec 04 §17): the window
    // sets none per item.
    if (game::isUnitType(hull->type) && hull->type != ruleset::VehicleType::Drone)
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
