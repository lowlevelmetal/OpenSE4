#include "client/classic/movement_line.hpp"

#include "core/rng.hpp"
#include "game/design.hpp"

#include <array>
#include <cstdlib>

namespace opense4::client::classic {

std::vector<LineMark> movementLineMarks(const game::movement::PlannedRoute& route, game::SystemId shown,
                                        const std::function<PixelPoint(game::Sector)>& centre) {
    std::vector<LineMark> out;
    const std::vector<game::Location>& points = route.points;
    bool previousDrawn = false;   // the point before this one was drawn (it lies in this system)
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].system != shown) {
            previousDrawn = false;
            continue;
        }
        const PixelPoint c = centre(points[i].sector);
        if (i > 0) out.push_back({LineMark::Kind::Ring, c, {}, 0});
        if (previousDrawn) {
            const PixelPoint from = centre(points[i - 1].sector);
            out.push_back({LineMark::Kind::Segment, from, c, 0});
            out.push_back({LineMark::Kind::Number, from, {}, route.turnOf(i - 1)});
        }
        previousDrawn = true;
        if (i + 1 == points.size() || points[i + 1].system != shown) out.push_back({LineMark::Kind::Number, c, {}, route.turnOf(i)});
    }
    return out;
}

std::vector<PixelPoint> linePixels(PixelPoint a, PixelPoint b) {
    std::vector<PixelPoint> out;
    const int dx = std::abs(b.x - a.x), dy = -std::abs(b.y - a.y);
    const int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
    int err = dx + dy;
    PixelPoint p = a;
    while (p != b) {
        out.push_back(p);
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            p.x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            p.y += sy;
        }
    }
    return out;
}

std::span<const PixelPoint> ringOffsets() {
    // 8 x 8, x and y from -4 to 3 (inferred: the usual pixels of an 8 px circle):
    //   ..####..
    //   .#....#.
    //   #......#   (four rows)
    //   .#....#.
    //   ..####..
    static constexpr std::array<PixelPoint, 20> kRing{{
        {-2, -4}, {-1, -4}, {0, -4}, {1, -4},
        {-3, -3}, {2, -3},
        {-4, -2}, {3, -2}, {-4, -1}, {3, -1}, {-4, 0}, {3, 0}, {-4, 1}, {3, 1},
        {-3, 2}, {2, 2},
        {-2, 3}, {-1, 3}, {0, 3}, {1, 3},
    }};
    return kRing;
}

std::optional<LineSubject> movementLineSubject(const game::Rules& r, const game::GameState& s, game::EmpireId viewer,
                                               std::optional<game::VehicleId> reportVehicle, std::optional<game::FleetId> reportFleet) {
    if (reportFleet) {
        const game::Fleet* f = s.fleet(*reportFleet);
        if (!f || f->owner != viewer || game::fleetOrders(s, *f).empty()) return std::nullopt;
        return LineSubject{{}, f->id};
    }
    if (!reportVehicle) return std::nullopt;
    const game::Vehicle* v = s.vehicle(*reportVehicle);
    if (!v || v->owner != viewer || v->orders.empty()) return std::nullopt;
    using ruleset::VehicleType;
    const VehicleType type = game::vehicleType(r, s, *v);
    if (type != VehicleType::Ship && type != VehicleType::Base && type != VehicleType::Fighter && type != VehicleType::Drone) return std::nullopt;
    return LineSubject{v->id, {}};
}

game::movement::PlannedRoute movementLineRoute(const game::Rules& r, const game::GameState& s, const LineSubject& subject) {
    const uint64_t id = subject.fleet.valid() ? (uint64_t{1} << 32) | subject.fleet.value : subject.vehicle.value;
    Rng display((uint64_t{s.turn} << 33) ^ id ^ 0x6d6f76656c696e65ull);
    if (subject.fleet.valid())
        if (const game::Fleet* f = s.fleet(subject.fleet)) return game::movement::planRoute(r, s, *f, display);
    if (const game::Vehicle* v = s.vehicle(subject.vehicle)) return game::movement::planRoute(r, s, *v, display);
    return {};
}

} // namespace opense4::client::classic
