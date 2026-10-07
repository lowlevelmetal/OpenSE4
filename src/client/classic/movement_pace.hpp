#pragma once

// The pace of the system window's ship movement animation (docs/spec/06 §2.4
// "Moves as they are made", §7 Q62, confirmed: binary): the original waits
// 10 ms after each 5° frame of a turn and 1 ms after each 1 px frame of a
// slide. OpenSE4 draws the frames by the time elapsed since the animation
// began, whatever the display's refresh rate: a frame is drawn as soon as the
// wait after the one before is over, several in one display frame when they
// come faster than the display. The waits are the original's divided by the
// player's speed (Options, under OpenSE4: "Ship movement speed"; 1 = the
// original's waits, the default). Used by the glides of moves as they are
// made (ship_glides.hpp) and by the movement log replay (movement_replay.hpp).
// Headless, tested in tests/test_client_logic.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace opense4::client::classic {

// The speeds the Options window offers, slowest first: each twice the one
// before. 1 plays the original's waits (the default).
inline constexpr std::array<double, 7> kMovementSpeeds{0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
inline constexpr size_t kOriginalMovementSpeed = 3;   // kMovementSpeeds[3] == 1

// The step of kMovementSpeeds nearest to `speed` (by ratio): a value read
// from the settings file, or anything else, comes to one the window offers.
inline size_t movementSpeedStep(double speed) {
    if (!(speed > 0.0)) return kOriginalMovementSpeed;   // also NaN
    size_t best = kOriginalMovementSpeed;
    for (size_t i = 0; i < kMovementSpeeds.size(); ++i)
        if (std::abs(std::log2(speed / kMovementSpeeds[i])) < std::abs(std::log2(speed / kMovementSpeeds[best]))) best = i;
    return best;
}

struct MovementPace {
    static constexpr int kDegreesPerTurnFrame = 5;
    static constexpr double kSecondsAfterTurnFrame = 0.010;
    static constexpr double kSecondsAfterSlideFrame = 0.001;

    double speed = 1.0;   // the waits are the original's divided by this

    double turnFrameSeconds() const { return kSecondsAfterTurnFrame / safeSpeed(); }
    double slideFrameSeconds() const { return kSecondsAfterSlideFrame / safeSpeed(); }

    // How long an animation of `turnFrames` frames of a turn and then
    // `slideFrames` frames of a slide lasts: until the wait after its last
    // frame is over.
    double duration(int turnFrames, int slideFrames) const {
        return double(std::max(0, turnFrames)) * turnFrameSeconds() + double(std::max(0, slideFrames)) * slideFrameSeconds();
    }

    // The frames drawn `elapsed` seconds after the animation began: the first
    // at once, each next one when the wait after the one before is over; at
    // most all of them (0 for an animation without frames).
    int framesDrawn(int turnFrames, int slideFrames, double elapsed) const {
        turnFrames = std::max(0, turnFrames);
        slideFrames = std::max(0, slideFrames);
        const int total = turnFrames + slideFrames;
        if (total == 0) return 0;
        // A frame due exactly now is drawn, whatever the rounding of the sums.
        const double t = std::max(0.0, elapsed) + kSlack;
        const double turning = double(turnFrames) * turnFrameSeconds();
        // Counted as doubles and capped before they become ints (a long wait would overflow).
        if (t < turning) return int(std::min(double(turnFrames), std::floor(t / turnFrameSeconds()) + 1.0));
        if (slideFrames == 0) return total;
        return turnFrames + int(std::min(double(slideFrames), std::floor((t - turning) / slideFrameSeconds()) + 1.0));
    }

private:
    static constexpr double kSlack = 1e-9;
    double safeSpeed() const { return speed > 0.0 ? speed : 1.0; }
};

} // namespace opense4::client::classic
