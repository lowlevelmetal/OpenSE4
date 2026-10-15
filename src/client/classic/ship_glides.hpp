#pragma once

// Ship movement animation for the system map (docs/spec/06 §2.4 "Moves as
// they are made", confirmed: binary). With "animate ship movement in the
// system window" on, each step of a group the player sees in the shown system
// is shown as the original shows its player's own moves: first the turn to
// the new heading, 5° a frame with 10 ms after each frame, during which
// nothing is drawn: the group stays on its old square as it was, at its old
// heading (sliding() is false); then the slide to its new square, 1 px a
// frame along the longer axis with 1 ms after each (the waits of §1.10.3),
// the mini at its new heading from the first frame. The group is one sprite,
// its first vehicle's mini (MovementStep::lead), the other members hidden
// until the step is over. After each step the game pauses for `System Ship
// Movement Delay Milliseconds`, which the original reads as seconds (stock
// 0). The frames come by the time elapsed, whatever the display's refresh
// rate, at the original's waits divided by the player's speed (MovementPace,
// movement_pace.hpp). Mines striking the group (MovementStep::Kind) show a
// small explosion over its square, with or without the animation
// (MineBlast).
//
// The original animates only the moves made for the human player whose turn
// it is, in a turn-based game (enqueue's `originals`); the others, and moves
// found without their steps, only with OpenSE4's option to animate them too
// (Animated): otherwise they take no time, in their turn.
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
// found elsewhere without a step (a network game's new state of other
// players' moves; an event) moves there in one straight glide, the moves of
// one change grouped by where they start and end and queued in the game's
// vehicle order (inferred). When another system is shown, the glides under
// way end there, and the queue goes on. Headless, tested in
// tests/test_client_logic.cpp.

#include "client/classic/movement_pace.hpp"
#include "core/math.hpp"
#include "game/state.hpp"
#include "game/turn.hpp"

#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic {

// The small explosion drawn over a square when mines strike the group during
// the human player's own move (spec 06 §2.4, confirmed: binary), with the
// animation option on or off: eight 36x36 frames of a strip of
// `General.bmp`, the one at y 64 when the mines destroyed no member of the
// group and the one at y 100 when they destroyed one or more, each frame held
// a tenth of a second; then the square as it was before the strike, held a
// tenth of a second more; then its sound starts and the game waits half a
// second; then the square is redrawn without the members destroyed. Drawn
// in the square's sprite place, never scaled, its black keyed (the original
// keys the cell's own background colour: inferred). Its picture, waits and
// sound in this one place.
struct MineBlast {
    static constexpr std::string_view kPicture = "Pictures/Game/General.bmp";
    static constexpr int kStripY = 64;         // no member destroyed
    static constexpr int kStripYLost = 100;    // one or more destroyed
    static constexpr int kFrameSize = 36;      // frame n (0-based) at x = 36 n
    static constexpr int kFrames = 8;
    static constexpr double kSecondsAfterFrame = 0.1;   // after each frame, and after the square is put back
    static constexpr double kWaitAfterSound = 0.5;
    static constexpr std::string_view kSound = "boom1";
    static constexpr int stripY(bool lost) { return lost ? kStripYLost : kStripY; }
    static constexpr double soundAt() { return double(kFrames + 1) * kSecondsAfterFrame; }
    static constexpr double duration() { return soundAt() + kWaitAfterSound; }
    // The frame drawn `elapsed` seconds after it began, or none (before it,
    // and once the square is put back).
    static std::optional<int> frameAt(double elapsed) {
        if (elapsed < 0.0) return std::nullopt;
        const int frame = int(std::floor(elapsed / kSecondsAfterFrame + 1e-9));
        if (frame < kFrames) return frame;
        return std::nullopt;
    }
};

// Which moves the glides animate (spec 06 §2.4): the original animates only
// the moves made for the human player whose turn it is, in a turn-based game
// (ShipGlides::enqueue's `originals`); OpenSE4's option "Also animate other
// empires' and simultaneous turns' moves" (settings.hpp) the others too.
struct AnimatedMoves {
    bool others = true;     // steps enqueued as not the original's, and other players' moves found without steps
    bool ownFound = true;   // own vehicles' moves found without steps count as the original's (a turn-based game)
};

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
    // Mines striking a group, shown over its square (MineBlast).
    struct Blast {
        game::Location at;
        bool lost = false;          // the mines destroyed a member (MineBlast::stripY)
        double start = 0.0;
        std::optional<int> frame;   // the frame drawn now (none: not, or no longer)
    };
    using Animated = AnimatedMoves;
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
    // before them, from the next track(). `originals`: moves the original
    // animates (the human player's own, in its turn of a turn-based game);
    // others only with Animated::others. `provisional`: steps of a call that
    // a battle stopped, made before the battle while the game is still as
    // before the call (ClassicSession::ShownMove::provisional): once shown,
    // their vehicles stay drawn where they took them until release().
    void enqueue(std::span<const game::MovementStep> steps, bool originals = true, bool provisional = false);
    // The call that a battle stopped is over (made again with the answers):
    // the vehicles its steps left pinned are drawn where the game has them,
    // which is where they were pinned, or go on with the rest of its steps.
    void release();
    // A marker after them: reached, in its turn, once the steps queued before
    // it are over (with "all at once", at the next track()).
    void enqueueMarker(uint64_t id);

    // Once per frame, with every vehicle of the game: queues the moves of
    // visible vehicles found without steps since the last call, plays the
    // queue on to `now` and brings each glide to the frame due then. It stops
    // after a marker is reached, so that the caller can follow the view
    // (takeReached()) and call again. `enabled` off (the setting): every move
    // takes no time, in its turn, and only mines' explosions take theirs.
    // `cellPixels`: the system panel's sector size in frame pixels (36 at
    // 800x600, 50 at 1024x768); `pauseSeconds`: the delay after each step;
    // `pace`: the speed new glides take; `order`: how the groups follow each
    // other; `animated`: which moves are animated. A second call at the same
    // time, after the game changed, takes the new moves.
    void track(double now, game::SystemId shown, bool enabled, std::span<const Seen> vehicles, float cellPixels = 50.0f,
               double pauseSeconds = 0.0, MovementPace pace = {}, Order order = Order::OneGroupAtATime, Animated animated = {});
    // The markers reached since the last call, in order.
    std::vector<uint64_t> takeReached() { return std::exchange(reached_, {}); }
    // The explosions whose sound is due since the last call (MineBlast::kSound), in order.
    std::vector<game::Location> takeBooms() { return std::exchange(booms_, {}); }
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
    // The glide a vehicle is in now, if any, turning or sliding; all of
    // them, in the order they began. Only a sliding glide is drawn (its
    // pause included): while it turns its members are drawn on their
    // square as they were (heldAt(), heldHeading()).
    const Glide* find(game::VehicleId v) const;
    const std::vector<Glide>& all() const { return glides_; }
    static bool sliding(const Glide& g) { return g.frame > g.turnFrames; }
    // The glide a vehicle is drawn in now: its slide (or the pause after it).
    const Glide* drawnGliding(game::VehicleId v) const {
        const Glide* g = find(v);
        return g && sliding(*g) ? g : nullptr;
    }
    // The explosions shown now, in the order they began.
    const std::vector<Blast>& blasts() const { return blasts_; }
    // A vehicle whose steps are still to come (or under way): where it stands
    // in the animation and the heading it has there, until its last step is
    // over; then it is drawn where the game has it.
    std::optional<game::Location> heldAt(game::VehicleId v) const;
    std::optional<int> heldHeading(game::VehicleId v) const;
    bool moving(game::VehicleId v) const { return held_.contains(v); }
    // Where every visible vehicle was in the game when this frame began, and its heading then.
    const std::map<game::VehicleId, game::Location>& lastSeen() const { return lastSeen_; }
    const std::map<game::VehicleId, int>& lastHeadings() const { return lastHeadings_; }
    // Where a glide is drawn now, in sector units (on its old square while
    // it turns), and its mini's angle when it is drawn (the new heading).
    static Vec2 position(const Glide& g);
    static double angle(const Glide& g);

private:
    struct Step {
        std::vector<game::VehicleId> members;
        game::VehicleId lead;
        game::Location from, to;
        int heading = 0;                    // within a system: the heading the members take
        bool straight = false;              // a move seen without its steps: one glide, the pause of each square it spans
        bool others = false;                // not a move the original animates (Animated::others)
        bool provisional = false;           // made before a battle stopped its call (enqueue)
        bool shown = false;                 // played as a glide or an explosion (begin)
        bool mines = false;                 // mines striking the group at `to` (MineBlast)
        bool lost = false;                  // and destroying a member
        std::optional<uint64_t> marker;     // a marker, not a step
        double queued = -1.0;               // when it was queued (the first track() after enqueue)
    };
    struct Running {
        Step step;
        Glide glide;
        std::optional<Blast> blast;   // an explosion, not a glide
        bool boomed = false;          // its sound is due (takeBooms)
        double end = 0.0;             // the wait after its last frame and its pause are over
    };
    struct Held {
        game::Location at;
        int heading = 0;
        bool seenBefore = false;  // the player saw it before its moves
        int steps = 0;            // steps of it queued or under way
        bool pinned = false;      // held where a provisional step left it, until release()
        bool provisional = false; // a provisional step of it was queued: the game is not there yet (no move is found)
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
    struct Look {
        game::SystemId shown;
        bool enabled = true;
        float cellPixels = 50.0f;
        double pauseSeconds = 0.0;
        MovementPace pace;
        Animated animated;
    };
    bool startDue(const Look& look, const SeenMap& seen, Order order, bool& stop);
    // Starts one step at `start`: a glide, or done at once when nothing of it is shown.
    void begin(Step st, double start, const Look& look, const SeenMap& seen);
    void boom(Running& r);
    void rebuildGlides(double now);

    std::map<game::VehicleId, game::Location> lastSeen_;
    std::map<game::VehicleId, int> lastHeadings_;
    std::map<game::VehicleId, Was> lastAll_;                 // every vehicle of the game, last frame
    std::deque<Step> pending_;
    std::vector<Running> running_;
    std::vector<Glide> glides_;                              // running_'s glides as last drawn, for all()
    std::vector<Blast> blasts_;                              // running_'s explosions as last drawn, for blasts()
    std::vector<uint64_t> reached_;
    std::vector<game::Location> booms_;
    std::map<game::VehicleId, Held> held_;
    std::map<game::VehicleId, game::Location> queuedTo_;   // where a held vehicle's last queued step ends
    std::map<game::VehicleId, double> freeAt_;             // when its last step ended (all at once)
    double lastEnd_ = 0.0;                                  // when the last step ended (one group at a time)
    game::SystemId lastShown_;
};

} // namespace opense4::client::classic
