#pragma once

// The movement log replay of a simultaneous game (docs/spec/06 §2.7, §7 Q51,
// confirmed: binary): the main window goes back to the start of the turn and
// plays its 30 movement days again, with Ctrl+P (all days), Ctrl+I (one day
// a press), Ctrl+O (back to the start) and Ctrl+U (each of the viewer's
// moving objects in turn, the view following it).
//
// The log: in a local or hotseat game the client keeps the state the turn
// started from and plays the turn again from it, recording each day as the
// engine leaves it (ClassicSession::replayLastTurn, MovementRecorder): the
// same moves, warp jumps, colonies founded and objects removed as the turn
// had. A network or PBEM client has only its own view, so it rebuilds a log
// from where it saw every vehicle before and after the turn (approximateLog;
// inferred). Headless, tested in tests/test_main_window.cpp.

#include "core/math.hpp"
#include "game/state.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace opense4::client::classic {

struct MovementLog {
    static constexpr int kDays = 30;
    struct Move {
        game::VehicleId id;
        game::Location from, to;   // another system: a warp jump
    };
    struct Day {
        std::vector<Move> moves;
        std::vector<std::pair<game::VehicleId, game::Location>> appeared;
        std::vector<game::VehicleId> removed;
        std::vector<std::pair<game::ObjectId, game::EmpireId>> colonies;  // founded, or changed owner
        std::vector<game::ObjectId> coloniesRemoved;
    };

    uint32_t turn = UINT32_MAX;   // the game turn the log leads to
    bool exact = false;           // played again from the turn's start (else rebuilt from the client's view)
    std::map<game::VehicleId, game::Vehicle> vehicles;   // every vehicle it mentions, as first seen
    std::map<game::VehicleId, game::Location> start;     // their places at day 0
    std::map<game::ObjectId, game::EmpireId> startColonies;
    std::vector<Day> days;        // days 1..30

    // The objects of `owner` that moved or jumped, in order of first appearance in the log.
    std::vector<game::VehicleId> movers(game::EmpireId owner) const;
};

// Records a log from the start state and the state after each day.
class MovementRecorder {
public:
    explicit MovementRecorder(const game::GameState& start);
    void day(int day, const game::GameState& s);
    MovementLog take(uint32_t turn);

private:
    MovementLog log_;
    std::map<game::VehicleId, game::Location> where_;
    std::map<game::ObjectId, game::EmpireId> colonies_;
};

// A log rebuilt from where the client saw each vehicle before the turn and
// where it sees it now: within a system a vehicle steps one sector at a time
// along a straight line, the steps spread evenly over the 30 days; a vehicle
// now in another system jumps there on day 15; one no longer seen is removed
// after day 30 (inferred).
MovementLog approximateLog(const std::map<game::VehicleId, game::Location>& before, const game::GameState& now,
                           const std::set<game::VehicleId>& seenNow, uint32_t turn);

class MovementReplay {
public:
    static constexpr int kDays = MovementLog::kDays;
    // The animation of a move in the shown system: the sprite turns 5° per
    // 10 ms, then slides 1 px per millisecond (§7 Q51).
    static constexpr double kSecondsPerTurnStep = 0.010;
    static constexpr double kSecondsPerPixel = 0.001;

    void setLog(std::shared_ptr<const MovementLog> log);
    const MovementLog* log() const { return log_.get(); }
    // A log of this turn exists (otherwise "Replay Unavailable").
    bool available(uint32_t turn) const { return log_ && log_->turn == turn; }

    // Ctrl+P: from the start, the 30 days in one go.
    void play();
    // Ctrl+I: the first press shows Day 0; each later press applies one more
    // day; the press after day 30 ends the replay.
    void step();
    // Ctrl+O: back to Day 0, waiting for steps.
    void rewind();
    // Ctrl+U: for each object, from the start all 30 days, the view following it.
    void playFollowing(std::vector<game::VehicleId> objects);
    void stop();

    struct Frame {
        double now = 0.0;
        game::SystemId shown;
        bool animate = false;            // "animate ship movement in the system window"
        float cellPixels = 50.0f;        // the system panel's sector size, frame pixels
        std::function<bool(game::VehicleId)> seen;   // the viewer sees it (else never animated)
        std::function<bool(game::VehicleId)> turns;  // its mini turns to its heading
    };
    // Once per frame: plays on (one day per frame, or the day's animations).
    void update(const Frame& f);

    bool active() const { return mode_ != Mode::Off; }
    int day() const { return day_; }
    // Ctrl+U: the object the view follows now.
    std::optional<game::VehicleId> following() const;

    // The vehicles at this point of the replay, at their places.
    const std::vector<game::Vehicle>& vehicles() const { return view_; }
    // A vehicle being animated: where it is drawn (in sector units: a
    // square's centre is (x + 0.5, y + 0.5)) and its angle (degrees clockwise from up).
    struct Motion {
        Vec2 at;
        double angle = 0.0;
    };
    std::optional<Motion> motion(game::VehicleId v, double now) const;
    // The heading (0..7) of a turning mini at this point of the replay.
    int heading(game::VehicleId v) const;
    // The owner of a planet's colony at this point (none: not a colony then).
    std::optional<game::EmpireId> colonyOwner(game::ObjectId planet) const;

private:
    enum class Mode { Off, Stepping, Playing, Following };
    struct Animation {
        std::vector<game::VehicleId> ids;   // a fleet moves as one
        game::Location from, to;
        double angle0 = 0.0, angle1 = 0.0;  // degrees, the shorter way round
        double turnTime = 0.0, slideTime = 0.0;
    };
    void reset();
    void applyDay(const Frame& f);
    void rebuildView();
    bool animating() const { return animIndex_ < anims_.size(); }

    std::shared_ptr<const MovementLog> log_;
    Mode mode_ = Mode::Off;
    int day_ = 0;
    std::vector<game::VehicleId> follow_;
    size_t followIndex_ = 0;
    std::map<game::VehicleId, game::Location> where_;
    std::map<game::VehicleId, int> headings_;
    std::map<game::ObjectId, game::EmpireId> colonies_;
    std::vector<Animation> anims_;
    size_t animIndex_ = 0;
    double animStart_ = 0.0;
    bool started_ = false;   // the animation clock is set
    int pending_ = 0;        // step presses not yet carried out
    std::vector<game::Vehicle> view_;
};

} // namespace opense4::client::classic
