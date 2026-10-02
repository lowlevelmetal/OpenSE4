#pragma once

// Ship movement animation for the system map (docs/spec/06 §2.4, confirmed:
// binary). With "animate ship movement in the system window" on, a visible
// vehicle whose square in the shown system changes first turns its mini to
// its new heading, 5° a frame with 10 ms after each frame, then slides to its
// new square 1 px a frame along the longer axis with 1 ms after each (the
// waits of §1.10.3); a fleet's members slide as one sprite. After each step the
// game pauses for `System Ship Movement Delay Milliseconds`, which the original
// reads as seconds (stock 0). Every frame stays at least one display refresh
// (an OpenSE4 choice). Ships that warp in or out, and every ship when the view
// changes to another system, simply appear in their new place. The client
// sees the engine's moves only as they are made, so several steps made at once
// glide as one straight move, with the pause of every step (inferred).

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
    static constexpr int kDegreesPerTurnFrame = 5;
    static constexpr double kSecondsAfterTurnFrame = 0.010;
    static constexpr double kSecondsAfterSlideFrame = 0.001;

    struct Glide {
        Vec2 from, to;                      // in sector units: a square's centre is (x + 0.5, y + 0.5)
        double angle0 = 0.0, angle1 = 0.0;  // degrees clockwise from up, the shorter way round
        int turnFrames = 0, slideFrames = 0;
        double pause = 0.0;                 // seconds held on the new square afterwards
        double start = 0.0;                 // when it began (fleet members share it)
        // Progress: frames shown so far, when the last one was, and when the pause ends.
        int frame = 0;
        double lastFrameAt = 0.0;
        double pauseUntil = -1.0;
        bool done = false;
    };
    struct Seen {
        game::VehicleId id;
        game::Location location;
        int heading = 0;      // the engine's heading (game::Vehicle::heading)
        bool turns = true;    // the mini is drawn turned (engineless hulls, satellites and mines are not)
    };

    // Once per frame, with every vehicle the player can see: starts the glides
    // of the moves made since the last frame and plays each glide one frame
    // on. `enabled` off drops every glide (the setting). `cellPixels`: the
    // system panel's sector size in frame pixels (36 at 800x600, 50 at
    // 1024x768); `pauseSeconds`: the delay after each step.
    void track(double now, game::SystemId shown, bool enabled, std::span<const Seen> visible, float cellPixels = 50.0f,
               double pauseSeconds = 0.0);
    // Settings.txt `System Ship Movement Delay Milliseconds` as the pause in
    // seconds: the original reads the value as seconds (spec 06 §2.4,
    // confirmed: binary); 0 when it is 0 or less.
    static double stepPause(int64_t value) { return value > 0 ? double(value) : 0.0; }
    // The glide a vehicle is in, if any.
    const Glide* find(game::VehicleId v) const;
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
