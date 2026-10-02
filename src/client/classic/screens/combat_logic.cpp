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

namespace {

// Above 100000, in thousands with "K" (spec 06 §1.10.1; truncated, inferred).
std::string supplyNumber(int64_t v) { return v > 100000 ? std::format("{}K", v / 1000) : std::format("{}", v); }

} // namespace

std::vector<std::pair<std::string, std::string>> pieceReportLines(const game::Rules& r, const game::GameState& s,
                                                                  const std::vector<game::combat::TacticalPiece>& pieces, int piece) {
    std::vector<std::pair<std::string, std::string>> out;
    if (piece < 0 || size_t(piece) >= pieces.size()) return out;
    const game::combat::TacticalPiece& p = pieces[size_t(piece)];
    const bool planet = p.kind == PieceKind::Planet;
    const bool seeker = p.kind == PieceKind::Seeker;
    const bool drone = p.kind == PieceKind::UnitGroup && p.type == ruleset::VehicleType::Drone;
    const bool satellites = p.kind == PieceKind::UnitGroup && p.type == ruleset::VehicleType::Satellite;
    if (planet) out.emplace_back("Population", std::format("{}M", p.population));
    else out.emplace_back("Movement", std::format("{}/{}", p.movement, p.movementMax));
    out.emplace_back("Shields", std::format("{}/{}", p.shields, p.shieldsMax));
    // Taken against the full hit points the overkill limit counts, for every kind of piece.
    out.emplace_back("Damage", std::format("{}/{}", std::max<int64_t>(0, p.fullHitPoints - p.hitPoints), p.fullHitPoints));
    std::string supply;
    if (seeker) supply = "None";
    else if (planet || satellites) supply = "Never";
    else if (p.unlimitedSupply) supply = "Endless";
    else supply = std::format("{}/{}", supplyNumber(p.supply), supplyNumber(p.supplyCapacity));
    out.emplace_back("Supply", supply);
    out.emplace_back("Max Targets", std::format("{}", p.budget));
    if (drone) {
        const bool known = p.droneTarget >= 0 && size_t(p.droneTarget) < pieces.size() && pieces[size_t(p.droneTarget)].alive;
        out.emplace_back("Target", known ? pieces[size_t(p.droneTarget)].name : std::string("None"));
    } else {
        // Fleet groups are numbered like the groups formed in the window (spec 04 §3 step 5).
        std::string group = "None";
        if (p.group >= 0) group = std::format("Group {} - {}", p.group, p.isLeader ? "Leader" : "Wingman");
        out.emplace_back("Combat Group", group);
        const auto& formations = r.data().formations;
        out.emplace_back("Formation", p.formation >= 0 && size_t(p.formation) < formations.size() ? formations[size_t(p.formation)].name
                                                                                                    : std::string("None"));
    }
    if (planet) {
        int plague = p.plague;
        if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size())
            if (const game::Colony* colony = s.colony(p.planet)) plague = std::max(plague, colony->plagueLevel);
        if (plague > 0) out.emplace_back("Conditions", std::format("Plague {}", plague));
    }
    return out;
}

int dropTroopsColony(const game::combat::TacticalBattle& b, int piece) {
    const auto& pieces = b.pieces();
    if (piece < 0 || size_t(piece) >= pieces.size()) return -1;
    const game::combat::TacticalPiece& ship = pieces[size_t(piece)];
    int last = -1;
    for (size_t j = 0; j < pieces.size(); ++j) {
        const game::combat::TacticalPiece& q = pieces[j];
        if (q.alive && q.kind == PieceKind::Planet && q.owner.valid() && q.owner != ship.owner && b.distance(piece, int(j)) <= 1) last = int(j);
    }
    return last;
}

game::combat::TacticalOrder dropTroopsOrder(const game::combat::TacticalBattle& b, int piece) {
    game::combat::TacticalOrder o{game::combat::TacticalOrder::Kind::DropTroops, b.phaseEmpire(), piece};
    o.target = dropTroopsColony(b, piece);
    return o;
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
    // The Name column's lines: what each row holds, and its fleet.
    for (SimulatorRow& row : out) {
        row.lines = true;
        const SimulatorItem& first = setup.items[row.items.front()];
        row.unitsLine = unitType(first).has_value();
        std::vector<std::pair<game::DesignId, int>> counts;
        auto addUnits = [&](game::DesignId d, int n) {
            if (n <= 0) return;
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == d; });
            if (it != counts.end()) it->second += n;
            else counts.emplace_back(d, n);
        };
        int64_t people = 0;
        if (row.unitsLine) {
            for (size_t k : row.items) addUnits(setup.items[k].design, setup.items[k].count);
        } else if (first.kind == SimulatorItem::Kind::Planet) {
            if (first.replaceCargo) {
                for (const game::UnitStack& u : first.cargo) addUnits(u.design, u.count);
            } else if (const game::Colony* c = s.colony(first.planet)) {
                for (const game::UnitStack& u : c->cargo.units) addUnits(u.design, u.count);
            }
        } else {
            for (const game::UnitStack& u : first.cargo) addUnits(u.design, u.count);
            for (const game::combat::SimulatorPeople& p : first.people) people += p.millions;
        }
        for (const auto& [d, n] : counts) {
            if (!row.cargo.empty()) row.cargo += ", ";
            row.cargo += std::format("{} {}", n, d.valid() && d.index() < s.designs.size() ? s.design(d).name : std::string("?"));
        }
        if (people > 0) row.cargo += std::format("{}{}M people", row.cargo.empty() ? "" : ", ", people);
        if (first.fleet >= 0 && size_t(first.fleet) < setup.fleets.size()) row.fleet = setup.fleets[size_t(first.fleet)].name;
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
    // A ship takes its side's next serial number (spec 04 §17).
    game::combat::simulatorNumberShips(r, s, setup, item);
    setup.items.push_back(std::move(item));
    return true;
}

void simulatorRemove(game::combat::SimulatorSetup& setup, const SimulatorRow& row) {
    std::vector<size_t> items = row.items;
    std::sort(items.rbegin(), items.rend());
    for (size_t k : items)
        if (k < setup.items.size()) setup.items.erase(setup.items.begin() + std::ptrdiff_t(k));
}

SimulatorSandbox simulatorSandbox(const game::Rules& r, const game::GameState& real, const game::combat::SimulatorSetup& setup, int side, bool cargo) {
    SimulatorSandbox out;
    out.sim = game::combat::buildSimulation(r, real, setup);
    out.sideIndex = std::clamp(side, 0, std::max(0, int(out.sim.sides.size()) - 1));
    out.side = out.sim.sides.empty() ? game::EmpireId{} : out.sim.sides[size_t(out.sideIndex)];
    out.cargo = cargo;
    game::GameState& state = out.sim.state;
    state.options.simultaneous = true;   // commands take effect at once; nobody's turn is played
    if (cargo) {
        // Side 1 plays every holder and the Storehouse: no owner is checked (§7 Q80).
        out.sideIndex = 0;
        out.side = out.sim.sides.empty() ? game::EmpireId{} : out.sim.sides.front();
        for (game::Vehicle& v : state.vehicles)
            if (v.location == out.sim.where && v.count > 0) v.owner = out.side;
        for (game::ObjectId o : out.sim.itemObjects)
            if (game::Colony* c = o.valid() ? state.colony(o) : nullptr) c->owner = out.side;
        // The left list: the Combat Vehicles list's rows, in its order.
        for (const SimulatorRow& row : simulatorRows(r, real, setup))
            for (size_t k : row.items) {
                if (k < out.sim.itemObjects.size() && out.sim.itemObjects[k].valid()) {
                    out.holders.push_back({{}, out.sim.itemObjects[k], row.side});
                    continue;
                }
                if (k >= out.sim.itemVehicles.size()) continue;
                for (game::VehicleId v : out.sim.itemVehicles[k]) {
                    const SimulatorCargoHolder h{v, {}, row.side};
                    if (std::find(out.holders.begin(), out.holders.end(), h) == out.holders.end()) out.holders.push_back(h);
                }
            }
        // The Storehouse: a copy of the player's first colony, systems in order.
        game::ObjectId first;
        for (const game::StarSystem& sys : real.galaxy.systems) {
            for (game::ObjectId o : sys.objects)
                if (const game::Colony* c = real.colony(o); c && c->owner == setup.viewer) {
                    first = o;
                    break;
                }
            if (first.valid()) break;
        }
        if (first.valid() && out.side.valid()) {
            game::SpaceObject obj = real.galaxy.object(first);
            obj.sector = out.sim.where.sector;
            obj.name = "Storehouse";
            ruleset::Ability storage;
            storage.type = std::string(game::identifier(game::AbilityKind::CargoStorage));
            storage.value1 = std::to_string(kStorehouseCargoStorage);
            obj.abilities.push_back(storage);
            const game::ObjectId planet = state.addObject(std::move(obj), out.sim.where.system);
            game::Colony c = *real.colony(first);
            c.planet = planet;
            c.owner = out.side;
            c.orders.clear();
            c.queue = {};
            c.homeworld = false;
            c.militia = -1;
            // 1000 of every unit design the player owns or has seen, sorted by
            // the owner's empire name, then the design name.
            std::vector<game::DesignId> designs;
            const game::Empire& viewer = real.empire(setup.viewer);
            for (game::DesignId d : viewer.designs) designs.push_back(d);
            for (const game::SeenDesign& seen : viewer.knowledge.seenDesigns) designs.push_back(seen.design);
            std::erase_if(designs, [&](game::DesignId d) {
                const ruleset::VehicleSize* hull = hullOf(r, real, d);
                return !hull || !game::isUnitType(hull->type);
            });
            auto ownerName = [&](game::DesignId d) {
                const game::EmpireId o = real.design(d).owner;
                return o.valid() && o.index() < real.empires.size() ? real.empire(o).name : std::string{};
            };
            std::sort(designs.begin(), designs.end(), [&](game::DesignId a, game::DesignId b) {
                const std::string oa = ownerName(a), ob = ownerName(b);
                if (oa != ob) return oa < ob;
                if (real.design(a).name != real.design(b).name) return real.design(a).name < real.design(b).name;
                return a < b;
            });
            designs.erase(std::unique(designs.begin(), designs.end()), designs.end());
            for (game::DesignId d : designs) {
                auto it = std::find_if(c.cargo.units.begin(), c.cargo.units.end(), [&](const game::UnitStack& u) { return u.design == d; });
                if (it != c.cargo.units.end()) it->count += kStorehouseUnits;
                else c.cargo.units.push_back({d, kStorehouseUnits});
            }
            // Side 1's people on top of the copy's own.
            c.population.push_back({out.side, kStorehousePeople});
            state.colonies[planet.index()] = std::move(c);
            out.storehouse = planet;
            out.anchorPlanet = planet;
        }
    }
    if (!out.anchorPlanet.valid())
        for (const std::vector<game::VehicleId>& vehicles : out.sim.itemVehicles)
            for (game::VehicleId v : vehicles)
                if (!out.anchorVehicle.valid() && state.vehicle(v) && state.vehicle(v)->owner == out.side &&
                    !game::isUnitType(game::vehicleType(r, state, *state.vehicle(v))))
                    out.anchorVehicle = v;
    return out;
}

void simulatorTakeBack(const game::Rules& r, const SimulatorSandbox& made, const game::GameState& sb, game::combat::SimulatorSetup& setup) {
    using game::combat::SimulatorFleet;
    using game::combat::SimulatorItem;
    auto realDesign = [&](game::DesignId d) {
        for (const auto& [copy, real] : made.sim.designCopies)
            if (copy == d) return real;
        return d;
    };
    auto vehicleOf = [&](size_t item) -> const game::Vehicle* {
        if (item >= made.sim.itemVehicles.size() || made.sim.itemVehicles[item].empty()) return nullptr;
        return sb.vehicle(made.sim.itemVehicles[item].front());
    };
    if (made.cargo) {
        // People of a side's virtual empire belong to that side; others keep their real race.
        auto peopleOf = [&](const std::vector<game::PopulationGroup>& groups) {
            std::vector<game::combat::SimulatorPeople> out;
            for (const game::PopulationGroup& g : groups) {
                if (g.millions <= 0) continue;
                const auto side = std::find(made.sim.sides.begin(), made.sim.sides.end(), g.race);
                game::combat::SimulatorPeople p;
                p.millions = g.millions;
                if (side != made.sim.sides.end()) p.side = int(side - made.sim.sides.begin());
                else p.race = g.race;
                out.push_back(p);
            }
            return out;
        };
        for (size_t k = 0; k < setup.items.size(); ++k) {
            SimulatorItem& item = setup.items[k];
            std::vector<game::UnitStack> units;
            if (item.kind == SimulatorItem::Kind::Planet) {
                const game::ObjectId o = k < made.sim.itemObjects.size() ? made.sim.itemObjects[k] : game::ObjectId{};
                const game::Colony* c = o.valid() ? sb.colony(o) : nullptr;
                if (!c) continue;
                for (const game::UnitStack& u : c->cargo.units)
                    if (u.count > 0) units.push_back({realDesign(u.design), u.count});
                item.cargo = std::move(units);
                item.replaceCargo = true;
                item.people = peopleOf(c->population);
                item.replacePeople = true;
                continue;
            }
            const game::Vehicle* v = vehicleOf(k);
            if (!v || game::isUnitType(game::vehicleType(r, sb, *v))) continue;
            for (const game::UnitStack& u : v->cargo.units)
                if (u.count > 0) units.push_back({realDesign(u.design), u.count});
            item.cargo = std::move(units);
            item.people = peopleOf(v->cargo.population);   // people moved aboard stay and fight (§7 Q80)
        }
        return;
    }
    // The side's fleets as the window left them; the other sides' stay.
    std::vector<SimulatorFleet> fleets;
    std::vector<int> remap(setup.fleets.size(), -1);
    for (size_t f = 0; f < setup.fleets.size(); ++f)
        if (setup.fleets[f].side != made.sideIndex) {
            remap[f] = int(fleets.size());
            fleets.push_back(setup.fleets[f]);
        }
    std::map<uint32_t, int> formed;   // sandbox fleet -> its index in `fleets`
    for (size_t k = 0; k < setup.items.size(); ++k) {
        SimulatorItem& item = setup.items[k];
        if (item.side != made.sideIndex) {
            item.fleet = item.fleet >= 0 && size_t(item.fleet) < remap.size() ? remap[size_t(item.fleet)] : -1;
            continue;
        }
        item.fleet = -1;
        const game::Vehicle* v = vehicleOf(k);
        if (!v || game::isUnitType(game::vehicleType(r, sb, *v)) || !v->fleet.valid()) continue;
        const game::Fleet* fl = sb.fleet(v->fleet);
        if (!fl || fl->owner != made.side) continue;
        auto it = formed.find(fl->id.value);
        if (it == formed.end()) {
            it = formed.emplace(fl->id.value, int(fleets.size())).first;
            fleets.push_back(SimulatorFleet{made.sideIndex, fl->name, fl->formation, fl->strategy});
        }
        item.fleet = it->second;
    }
    setup.fleets = std::move(fleets);
}

} // namespace opense4::client::classic
