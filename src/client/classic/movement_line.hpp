#pragma once

// The movement line of the system panel (docs/spec/06 §2.4 "Movement lines",
// confirmed: binary): which object has one, and what the panel draws for the
// part of its planned route (game::movement::planRoute) that lies in the
// system shown, as Windows draws it with a 1 px pen. Headless, tested in
// tests/test_client_logic.cpp; main_window.cpp draws the marks.

#include "game/movement.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace opense4::client::classic {

// The line's colour (pure blue) and the numbers' (white), as 0xRRGGBB.
inline constexpr uint32_t kMovementLineRgb = 0x0000ff;
inline constexpr uint32_t kMovementNumberRgb = 0xffffff;

struct PixelPoint {
    int x = 0, y = 0;
    bool operator==(const PixelPoint&) const = default;
};

// What the panel draws, in drawing order.
struct LineMark {
    enum class Kind { Ring, Segment, Number };
    Kind kind = Kind::Ring;
    PixelPoint at;    // a ring's or a number's centre; a segment's start
    PixelPoint to;    // a segment's end (not drawn itself)
    int number = 0;   // a number's value
};

// The marks for the route's points that lie in `shown` (spec 06 §2.4,
// "Drawing"), `centre` giving a sector's centre C in frame pixels. In route
// order: each point but the start gets a ring; a point after an earlier one
// drawn in this system gets the line from that one, then that one's number
// (the route point just before); the last point, and one whose next point
// lies in another system, gets its own number. Where the original joins the
// last square before leaving the system to the first after coming back, we
// break the line (the spec allows it), so no square gets two numbers.
std::vector<LineMark> movementLineMarks(const game::movement::PlannedRoute& route, game::SystemId shown,
                                        const std::function<PixelPoint(game::Sector)>& centre);

// The pixels a Windows line with a 1 px pen sets from `a` toward `b`: every
// pixel of the line but the end point `b` itself (Bresenham's choice, which is
// Windows' for the straight and diagonal lines between adjacent sectors).
std::vector<PixelPoint> linePixels(PixelPoint a, PixelPoint b);

// The pixels of the ring around C: a Windows ellipse with a 1 px pen on the
// box C − (4,4) to C + (4,4), right and bottom edges excluded, so 8 px across
// from C − 4 to C + 3. The exact pixel pattern is inferred (spec 06 §7 Q56).
std::span<const PixelPoint> ringOffsets();

// The object of the viewer's whose movement line the panel shows: the one
// whose report is open, if it is one of the viewer's ships, bases, fighter or
// drone groups, or a fleet of the viewer's, and it has at least one order.
struct LineSubject {
    game::VehicleId vehicle;   // set for a vehicle
    game::FleetId fleet;       // set for a fleet
    bool operator==(const LineSubject&) const = default;
};
std::optional<LineSubject> movementLineSubject(const game::Rules& r, const game::GameState& s, game::EmpireId viewer,
                                               std::optional<game::VehicleId> reportVehicle, std::optional<game::FleetId> reportFleet);

// The route of a subject, with a display-only generator seeded from the
// subject and the date: the same line on every machine and every frame,
// and never a number from the game's own sequence.
game::movement::PlannedRoute movementLineRoute(const game::Rules& r, const game::GameState& s, const LineSubject& subject);

} // namespace opense4::client::classic
