#pragma once

// Ship movement animation for the system map (docs/spec/06 §2.4, confirmed:
// binary). With "animate ship movement in the system window" on, a visible
// vehicle whose square in the shown system changes first turns its mini to
// its new heading, 5° a frame with 10 ms after each frame, then slides to its
// new square 1 px a frame along the longer axis with 1 ms after each (the
// waits of §1.10.3); a fleet's members slide as one sprite. After each step the
// game pauses for `System Ship Movement Delay Milliseconds`, which the original
// reads as seconds (stock 0). The frames come by the time elapsed, whatever
// the display's refresh rate, at the original's waits divided by the player's
// speed (MovementPace, movement_pace.hpp). Ships that warp in or out, and
// every ship when the view changes to another system, simply appear in their
// new place (glides under way end there). The client sees the engine's moves
// only once an engine call has made them all, so several steps made at once
// glide as one straight move, with the pause of every step, and the moves of
// one call glide together (inferred; the original animates each group's steps
// one after another as they are made).

#include "client/classic/movement_pace.hpp"
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
    static constexpr int kDegreesPerTurnFrame = MovementPace::kDegreesPerTurnFrame;

    struct Glide {
        Vec2 from, to;                      // in sector units: a square's centre is (x + 0.5, y + 0.5)
        double angle0 = 0.0, angle1 = 0.0;  // degrees clockwise from up, the shorter way round
        int turnFrames = 0, slideFrames = 0;
        double pause = 0.0;                 // seconds held on the new square afterwards
        double start = 0.0;                 // when its first frame was drawn (fleet members share it)
        MovementPace pace;                  // the speed when it began, kept to its end
        int frame = 1;                      // frames drawn so far, 1 to turnFrames + slideFrames
    };
    struct Seen {
        game::VehicleId id;
        game::Location location;
        int heading = 0;      // the engine's heading (game::Vehicle::heading)
        bool turns = true;    // the mini is drawn turned (engineless hulls, satellites and mines are not)
    };

    // Once per frame, with every vehicle the player can see: starts the glides
    // of the moves made since the last call and brings each glide to the
    // frame due at `now`. `enabled` off drops every glide (the setting).
    // `cellPixels`: the system panel's sector size in frame pixels (36 at
    // 800x600, 50 at 1024x768); `pauseSeconds`: the delay after each step;
    // `pace`: the speed new glides take. A second call at the same time, after
    // the game changed, starts the glides of the new moves.
    void track(double now, game::SystemId shown, bool enabled, std::span<const Seen> visible, float cellPixels = 50.0f,
               double pauseSeconds = 0.0, MovementPace pace = {});
    // Settings.txt `System Ship Movement Delay Milliseconds` as the pause in
    // seconds: the original reads the value as seconds (spec 06 §2.4,
    // confirmed: binary); 0 when it is 0 or less.
    static double stepPause(int64_t value) { return value > 0 ? double(value) : 0.0; }
    // Glides (or the pauses after them) under way.
    bool active() const { return !glides_.empty(); }
    // Every glide ends at once: the vehicles are drawn on their squares (the
    // player skipped the animation, an OpenSE4 convenience).
    void finish() { glides_.clear(); }
    // The glide a vehicle is in, if any; all of them.
    const Glide* find(game::VehicleId v) const;
    const std::map<game::VehicleId, Glide>& all() const { return glides_; }
    // Where a glide is drawn now, in sector units, and its mini's angle.
    static Vec2 position(const Glide& g);
    static double angle(const Glide& g);
    // Where every visible vehicle was drawn when this frame began, and its heading then.
    const std::map<game::VehicleId, game::Location>& lastSeen() const { return lastSeen_; }
    const std::map<game::VehicleId, int>& lastHeadings() const { return lastHeadings_; }

private:
    std::map<game::VehicleId, game::Location> lastSeen_;
    std::map<game::VehicleId, int> lastHeadings_;
    std::map<game::VehicleId, Glide> glides_;
    game::SystemId lastShown_;
};

} // namespace opense4::client::classic
