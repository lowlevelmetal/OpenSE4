#pragma once

// Ship movement animation for the system map. A visible ship whose square in the
// shown system changes glides from where it was drawn to its new square, instead of
// jumping there. Ships that warp in or out, and every ship when the view changes to
// another system, simply appear in their new place.

#include "core/math.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>

namespace opense4::client::classic {

class ShipGlides {
public:
    struct Glide {
        Vec2 from, to;       // in sector units: a square's centre is (x + 0.5, y + 0.5)
        double start = 0.0;  // seconds, the client's clock
        double duration = 0.0;  // the move and every pause
        // Settings.txt `System Ship Movement Delay Milliseconds` (spec 06
        // §1.9, confirmed: binary): a pause after each one-square step. With
        // one, the glide goes square by square, each step eased and followed
        // by the pause; without, it is one eased slide.
        int squares = 1;
        double pause = 0.0;  // seconds after each step
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
    // every glide (the setting). `pauseSeconds`: the wait after each animated
    // one-square step (0: none).
    void track(double now, game::SystemId shown, bool enabled, std::span<const Seen> visible, double pauseSeconds = 0.0);
    // Settings.txt `System Ship Movement Delay Milliseconds`, in seconds (0 when it is 0 or less).
    static double stepPause(int64_t milliseconds) { return milliseconds > 0 ? double(milliseconds) / 1000.0 : 0.0; }
    // The glide a vehicle is in at `now`, if any.
    const Glide* find(game::VehicleId v, double now) const;
    // Where a glide is at `now`, in sector units, eased in and out.
    static Vec2 position(const Glide& g, double now);
    // The heading of a vehicle's mini (0..7, 45° steps clockwise from up,
    // docs/spec/06 §2.4): the bearing of its last move within a system; a warp
    // keeps it and a vehicle never seen moving faces up. The client follows the
    // moves it sees, so a whole turn's moves in a simultaneous game give one
    // bearing, and headings start up again after loading a game (inferred).
    int heading(game::VehicleId v) const;
    // Where every visible vehicle was drawn when this frame began.
    const std::map<game::VehicleId, game::Location>& lastSeen() const { return lastSeen_; }

private:
    std::map<game::VehicleId, game::Location> lastSeen_;
    std::map<game::VehicleId, Glide> glides_;
    std::map<game::VehicleId, int> headings_;
    game::SystemId lastShown_;
};

} // namespace opense4::client::classic
