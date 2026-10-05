#include "client/classic/sector_view.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <tuple>

namespace opense4::client::classic {

int stellarSizeRank(const game::Rules& r, const game::SpaceObject& o) {
    if (o.kind != game::ObjectKind::Planet) return 0;
    static constexpr std::array<std::string_view, 5> kSizes{"Tiny", "Small", "Medium", "Large", "Huge"};
    for (const ruleset::PlanetSize& p : r.data().planetSizes)
        if (p.name == o.size)
            for (size_t i = 0; i < kSizes.size(); ++i)
                if (p.stellarSize == kSizes[i]) return int(i) + 1;
    return 0;
}

std::vector<game::ObjectId> shownStellarObjects(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::SystemId sys,
                                                std::optional<game::Sector> sector) {
    std::vector<game::ObjectId> out;
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size() || !viewer.valid() || viewer.index() >= s.empires.size()) return out;
    if (!s.empire(viewer).hasExplored(sys)) return out;
    for (game::ObjectId id : s.galaxy.system(sys).objects) {
        if (sector && s.galaxy.object(id).sector != *sector) continue;
        if (game::sight::canSeePlanet(r, s, viewer, id)) out.push_back(id);
    }
    return out;
}

std::vector<game::ObjectId> colonizeCandidates(const game::GameState& s, game::EmpireId viewer, game::Location where) {
    std::vector<game::ObjectId> out;
    const game::SystemId sys = where.system;
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size() || !viewer.valid() || viewer.index() >= s.empires.size()) return out;
    if (!s.empire(viewer).hasExplored(sys)) return out;
    for (game::ObjectId id : s.galaxy.system(sys).objects) {
        const game::SpaceObject& o = s.galaxy.object(id);
        if (o.sector == where.sector && o.kind == game::ObjectKind::Planet) out.push_back(id);
    }
    std::stable_sort(out.begin(), out.end(),
                     [&](game::ObjectId a, game::ObjectId b) { return game::objectOrderKey(s, a) < game::objectOrderKey(s, b); });
    return out;
}

int flagStep(int owners, int cellHeight) {
    if (owners <= 0 || owners * 10 <= cellHeight) return 10;
    return cellHeight / owners;
}

SectorView sectorView(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, std::span<const game::ObjectId> objects,
                      std::span<const game::Vehicle* const> vehicles, int cellHeight) {
    SectorView out;
    // The stellar object: the first in list order, replaced by a later planet of a larger size.
    int best = -1;
    for (const game::ObjectId id : objects) {
        ++out.stellarCount;
        const int rank = stellarSizeRank(r, s.galaxy.object(id));
        if (!out.stellar || rank > best) {
            out.stellar = id;
            best = rank;
        }
    }

    // Vehicles per owner; objects without an owner are not counted.
    for (const game::Vehicle* v : vehicles) {
        if (!v->owner.valid()) continue;
        auto it = std::find_if(out.owners.begin(), out.owners.end(), [&](const SectorView::Owner& o) { return o.empire == v->owner; });
        if (it == out.owners.end()) out.owners.push_back({v->owner, 1});
        else ++it->count;
    }
    std::sort(out.owners.begin(), out.owners.end(), [](const SectorView::Owner& a, const SectorView::Owner& b) { return a.empire.value < b.empire.value; });
    if (out.owners.empty()) return out;

    if (out.owners.size() > 1 || out.stellar) {
        out.flags = true;
        out.flagStep = flagStep(int(out.owners.size()), cellHeight);
        return out;
    }

    // One owner, no stellar object: the largest vehicle, a ship before any unit group.
    const game::Vehicle* shown = nullptr;
    int shownSize = -1;
    bool shownIsUnit = false;
    for (const game::Vehicle* v : vehicles) {
        if (!v->owner.valid()) continue;
        const bool unit = game::isUnitType(game::vehicleType(r, s, *v));
        const int size = r.hull(s.design(v->design).hull).tonnage;
        if (!shown || (shownIsUnit && !unit) || (unit == shownIsUnit && size > shownSize)) {
            shown = v;
            shownSize = size;
            shownIsUnit = unit;
        }
    }
    out.sprite = shown->id;
    out.fleetIcon = shown->owner == viewer && !shownIsUnit && shown->fleet.valid();
    out.cloakRing = shown->status == game::VehicleStatus::Cloaked;
    const int objectCount = out.owners.front().count;
    if (objectCount > 1) {
        out.count = objectCount;
    } else if (shownIsUnit) {
        // A lone unit group shows its own unit count, in white.
        out.count = std::max(1, shown->count);
        out.unitCount = true;
    }
    return out;
}

namespace {

std::string lowerName(std::string_view name) {
    std::string out(name);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The list's order of vehicles and fleet rows (spec 06 §2.5, confirmed: binary).
struct RowKey {
    bool inFleet = false;
    uint32_t fleet = 0;       // the fleet number; highest first
    size_t owner = 0;         // the owner's player number
    bool unit = false;        // unit groups after ships
    uint32_t hull = 0;        // VehicleSize.txt order
    std::string name;         // ignoring case
    bool operator<(const RowKey& o) const {
        if (inFleet != o.inFleet) return inFleet;
        if (fleet != o.fleet) return fleet > o.fleet;
        return std::tie(owner, unit, hull, name) < std::tie(o.owner, o.unit, o.hull, o.name);
    }
};

RowKey keyOf(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, const std::string& name) {
    RowKey k;
    k.inFleet = v.fleet.valid();
    k.fleet = v.fleet.valid() ? v.fleet.value : 0;
    k.owner = v.owner.valid() ? v.owner.index() : s.empires.size();
    k.unit = game::isUnitType(game::vehicleType(r, s, v));
    k.hull = v.design.valid() && v.design.index() < s.designs.size() ? s.design(v.design).hull : 0;
    k.name = lowerName(name);
    return k;
}

} // namespace

std::vector<SectorListRow> sectorListRows(const game::Rules& r, const game::GameState& s, game::EmpireId viewer,
                                          std::span<const game::ObjectId> objects, std::span<const game::Vehicle* const> vehicles) {
    std::vector<SectorListRow> out;
    std::vector<std::pair<std::string, game::ObjectId>> stellar;
    for (game::ObjectId id : objects) stellar.emplace_back(lowerName(s.galaxy.object(id).name), id);
    std::stable_sort(stellar.begin(), stellar.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [name, id] : stellar) out.push_back(SectorListRow{id, {}, {}});
    std::vector<std::pair<RowKey, SectorListRow>> rows;
    for (const game::Vehicle* v : vehicles) {
        const game::Fleet* f = v->owner == viewer && v->fleet.valid() ? s.fleet(v->fleet) : nullptr;
        if (!f) {
            rows.emplace_back(keyOf(r, s, *v, v->name), SectorListRow{{}, v->id, {}});
            continue;
        }
        // The viewer's fleet: one row, standing for its first member here in object order.
        auto same = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.second.fleet == f->id; });
        if (same == rows.end()) {
            rows.emplace_back(keyOf(r, s, *v, f->name), SectorListRow{{}, v->id, f->id});
        } else if (const game::Vehicle* first = s.vehicle(same->second.vehicle); !first || game::objectOrderKey(*v) < game::objectOrderKey(*first)) {
            same->second.vehicle = v->id;
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [key, row] : rows) out.push_back(row);
    return out;
}

std::vector<game::VehicleId> fleetReportMembers(const game::Rules& r, const game::GameState& s, const game::Fleet& f) {
    std::vector<std::pair<RowKey, game::VehicleId>> rows;
    for (game::VehicleId id : game::fleetMembersAt(s, f))
        if (const game::Vehicle* v = s.vehicle(id)) rows.emplace_back(keyOf(r, s, *v, v->name), id);
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<game::VehicleId> out;
    for (const auto& [key, id] : rows) out.push_back(id);
    return out;
}

} // namespace opense4::client::classic
