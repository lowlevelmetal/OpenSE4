#pragma once

// Ship movement animation for the system map (docs/spec/06 §2.4 "Moves as
// they are made", confirmed: binary). With "animate ship movement in the
// system window" on, each step of a group the player sees in the shown system
// is shown as the original shows its player's own moves: the group's mini
// first turns to its new heading, 5° a frame with 10 ms after each frame, then
// slides to its new square 1 px a frame along the longer axis with 1 ms after
// each (the waits of §1.10.3). The group is one sprite, its first vehicle's
// mini, the other members hidden until the step is over. After each step the
// game pauses for `System Ship Movement Delay Milliseconds`, which the
// original reads as seconds (stock 0). The frames come by the time elapsed,
// whatever the display's refresh rate, at the original's waits divided by
// the player's speed (MovementPace, movement_pace.hpp).
//
// The steps come from the engine as it made them (game::MovementStep, through
// the session: enqueue()) and play as a queue, in that order. With "one group
// at a time" (OpenSE4's option, the default, settings.hpp) each step starts
// when the one before has ended, so a group's run is over before the next
// group moves, as in the original; with "all at once" every group starts at
// once and plays its own steps one after another. A vehicle waiting for its
// steps is drawn where they start (heldAt()). Steps in another system, steps
// of vehicles the player does not see, and warp jumps take no time: the
// vehicles simply appear where they arrive, in their turn. Markers in the
// queue stand for the view following the player's own groups (spec 06 §2.7):
// one is reached when the steps before it are over (takeReached()). A vehicle
// found elsewhere without a step (a network game's new state, which has no
// steps; an event) moves there in one straight glide, the moves of one change
// grouped by where they start and end and queued in the game's vehicle order
// (inferred). When another system is shown, the glides under way end there,
// and the queue goes on. Headless, tested in tests/test_client_logic.cpp.

#include "client/classic/movement_pace.hpp"
#include "core/math.hpp"
#include "game/state.hpp"
#include "game/turn.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace opense4::client::classic {

class ShipGlides {
public:
    static constexpr int kDegreesPerTurnFrame = MovementPace::kDegreesPerTurnFrame;

    // How the steps of different groups follow each other (Options, under
    // OpenSE4: "Ship movement", settings.hpp).
    enum class Order {
        OneGroupAtATime,   // each step after the one before, as the original shows them (the default)
        AllAtOnce,         // every group at once, each group's steps one after another; markers at once
    };

    struct Glide {
        std::vector<game::VehicleId> members;   // the members the player sees, drawn as one sprite
        game::VehicleId lead;                   // the one whose mini is drawn: the group's first vehicle
        Vec2 from, to;                          // in sector units: a square's centre is (x + 0.5, y + 0.5)
        double angle0 = 0.0, angle1 = 0.0;      // degrees clockwise from up, the shorter way round
        int turnFrames = 0, slideFrames = 0;
        double pause = 0.0;                     // seconds held on the new square afterwards
        double start = 0.0;                     // when its first frame was drawn
        MovementPace pace;                      // the speed when it began, kept to its end
        int frame = 1;                          // frames drawn so far, 1 to turnFrames + slideFrames
    };
    // Every vehicle of the game, each frame.
    struct Seen {
        game::VehicleId id;
        game::Location location;
        int heading = 0;        // the engine's heading (game::Vehicle::heading)
        bool turns = true;      // the mini is drawn turned (engineless hulls, satellites and mines are not)
        bool visible = true;    // the player sees it now
        bool own = false;       // the player's own: its steps are shown even if the last frame was another player's view
    };

    // The steps an engine call made, in order: they play after those queued
    // before them, from the next track().
    void enqueue(std::span<const game::MovementStep> steps);
    // A marker after them: reached, in its turn, once the steps queued before
    // it are over (with "all at once", at the next track()).
    void enqueueMarker(uint64_t id);

    // Once per frame, with every vehicle of the game: queues the moves of
    // visible vehicles found without steps since the last call, plays the
    // queue on to `now` and brings each glide to the frame due then. It stops
    // after a marker is reached, so that the caller can follow the view
    // (takeReached()) and call again. `enabled` off ends the queue at once
    // (the setting). `cellPixels`: the system panel's sector size in frame
    // pixels (36 at 800x600, 50 at 1024x768); `pauseSeconds`: the delay after
    // each step; `pace`: the speed new glides take; `order`: how the groups
    // follow each other. A second call at the same time, after the game
    // changed, takes the new moves.
    void track(double now, game::SystemId shown, bool enabled, std::span<const Seen> vehicles, float cellPixels = 50.0f,
               double pauseSeconds = 0.0, MovementPace pace = {}, Order order = Order::OneGroupAtATime);
    // The markers reached since the last call, in order.
    std::vector<uint64_t> takeReached() { return std::exchange(reached_, {}); }
    // Settings.txt `System Ship Movement Delay Milliseconds` as the pause in
    // seconds: the original reads the value as seconds (spec 06 §2.4,
    // confirmed: binary); 0 when it is 0 or less.
    static double stepPause(int64_t value) { return value > 0 ? double(value) : 0.0; }
    // Steps under way or waiting (or the pauses after them).
    bool active() const { return !running_.empty() || !pending_.empty(); }
    // Every step ends at once and every marker is reached: the vehicles are
    // drawn on their squares (the player skipped the animation, an OpenSE4
    // convenience).
    void finish();
    // The glide a vehicle is in now, if any; all of them, in the order they began.
    const Glide* find(game::VehicleId v) const;
    const std::vector<Glide>& all() const { return glides_; }
    // A vehicle whose steps are still to come (or under way): where it stands
    // in the animation and the heading it has there, until its last step is
    // over; then it is drawn where the game has it.
    std::optional<game::Location> heldAt(game::VehicleId v) const;
    std::optional<int> heldHeading(game::VehicleId v) const;
    bool moving(game::VehicleId v) const { return held_.contains(v); }
    // Where every visible vehicle was in the game when this frame began, and its heading then.
    const std::map<game::VehicleId, game::Location>& lastSeen() const { return lastSeen_; }
    const std::map<game::VehicleId, int>& lastHeadings() const { return lastHeadings_; }
    // Where a glide is drawn now, in sector units, and its mini's angle.
    static Vec2 position(const Glide& g);
    static double angle(const Glide& g);

private:
    struct Step {
        std::vector<game::VehicleId> members;
        game::VehicleId lead;
        game::Location from, to;
        int heading = 0;                    // within a system: the heading the members take
        bool straight = false;              // a move seen without its steps: one glide, the pause of each square it spans
        std::optional<uint64_t> marker;     // a marker, not a step
        double queued = -1.0;               // when it was queued (the first track() after enqueue)
    };
    struct Running {
        Step step;
        Glide glide;
        double end = 0.0;         // the wait after its last frame and its pause are over
    };
    struct Held {
        game::Location at;
        int heading = 0;
        bool seenBefore = false;  // the player saw it before its moves
        int steps = 0;            // steps of it queued or under way
    };
    struct Was {
        game::Location location;
        int heading = 0;
    };
    using SeenMap = std::map<game::VehicleId, const Seen*>;
    void hold(const Step& st);
    void complete(const Step& st);
    // Starts the steps whose time has come; true when something changed.
    // `stop` is set once a marker is reached.
    bool startDue(game::SystemId shown, bool enabled, const SeenMap& seen, float cellPixels, double pauseSeconds, MovementPace pace,
                  Order order, bool& stop);
    // Starts one step at `start`: a glide, or done at once when nothing of it is shown.
    void begin(Step st, double start, game::SystemId shown, bool enabled, const SeenMap& seen, float cellPixels, double pauseSeconds,
               MovementPace pace);
    void rebuildGlides();

    std::map<game::VehicleId, game::Location> lastSeen_;
    std::map<game::VehicleId, int> lastHeadings_;
    std::map<game::VehicleId, Was> lastAll_;                 // every vehicle of the game, last frame
    std::deque<Step> pending_;
    std::vector<Running> running_;
    std::vector<Glide> glides_;                              // running_'s glides as last drawn, for all()
    std::vector<uint64_t> reached_;
    std::map<game::VehicleId, Held> held_;
    std::map<game::VehicleId, game::Location> queuedTo_;   // where a held vehicle's last queued step ends
    std::map<game::VehicleId, double> freeAt_;             // when its last step ended (all at once)
    double lastEnd_ = 0.0;                                  // when the last step ended (one group at a time)
    game::SystemId lastShown_;
};

} // namespace opense4::client::classic
