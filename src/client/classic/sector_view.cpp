#include "client/classic/sector_view.hpp"

#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <string_view>

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

} // namespace opense4::client::classic
