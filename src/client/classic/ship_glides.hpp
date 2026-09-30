#pragma once

// Ship movement animation for the system map. A visible ship whose square in the
// shown system changes glides from where it was drawn to its new square, instead of
// jumping there. Ships that warp in or out, and every ship when the view changes to
// another system, simply appear in their new place.

#include "core/math.hpp"
#include "game/state.hpp"

#include <map>
#include <span>
#include <utility>

namespace opense4::client::classic {

class ShipGlides {
public:
    struct Glide {
        Vec2 from, to;       // in sector units: a square's centre is (x + 0.5, y + 0.5)
        double start = 0.0;  // seconds, the client's clock
        double duration = 0.0;
    };
    struct Seen {
        game::VehicleId id;
        game::Location location;
    };

    // Seconds a glide takes per square, and its shortest and longest time.
    static constexpr double kSecondsPerSquare = 0.12;
    static constexpr double kMinSeconds = 0.35;
    static constexpr double kMaxSeconds = 1.0;

    // Once per frame, with every vehicle the player can see. `enabled` off drops
    // every glide (the setting).
    void track(double now, game::SystemId shown, bool enabled, std::span<const Seen> visible);
    // The glide a vehicle is in at `now`, if any.
    const Glide* find(game::VehicleId v, double now) const;
    // Where a glide is at `now`, in sector units, eased in and out.
    static Vec2 position(const Glide& g, double now);

private:
    std::map<game::VehicleId, game::Location> lastSeen_;
    std::map<game::VehicleId, Glide> glides_;
    game::SystemId lastShown_;
};

} // namespace opense4::client::classic
