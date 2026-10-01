#pragma once

// Ship movement animation for the system map. A visible ship whose square in the
// shown system changes glides from where it was drawn to its new square, instead of
// jumping there. Ships that warp in or out, and every ship when the view changes to
// another system, simply appear in their new place.

#include "core/math.hpp"
#include "game/state.hpp"

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

// The movement log of a simultaneous game (docs/spec/06 §2.7, Ctrl+P/O/I/U).
// Our engine keeps no day-by-day log, so the replay moves every vehicle the
// player saw in a straight line from where it was before the turn was
// processed to where it is now, in ten steps of one day (inferred).
class MovementReplay {
public:
    static constexpr int kDays = 10;
    static constexpr double kSecondsPerDay = 0.3;

    // A new turn: where the vehicles were before it was processed.
    void newTurn(std::map<game::VehicleId, game::Location> before);
    bool available() const { return !before_.empty(); }
    // Rewind: everything back at its start, held there.
    void rewind();
    // One day further (from the end: starts again at the first day).
    void step();
    // Plays from the current point (from the start when at the end).
    void play(double now);
    // Call once per frame; ends the replay when it is done.
    void update(double now);
    bool active() const { return active_; }
    // How far the replay is, 0 (start of the turn) .. 1 (now).
    double progress(double now) const;
    // Where to draw a vehicle during the replay, in sector units, when it moved
    // within `shown`; nullopt draws it at its place (or it was not seen before).
    std::optional<Vec2> position(game::VehicleId v, game::Location now, game::SystemId shown, double time) const;

private:
    std::map<game::VehicleId, game::Location> before_;
    bool active_ = false;
    bool playing_ = false;
    double day_ = kDays;        // days shown when not playing
    double playStart_ = 0.0;    // time play() began
    double playFrom_ = 0.0;     // days shown when play() began
};

} // namespace opense4::client::classic
