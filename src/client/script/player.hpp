#pragma once

// Plays an input script (script.hpp) against the running client, one frame
// at a time (docs/BUILDING.md "Input scripts"). At the start of each frame
// the app asks the player for that frame's input events, which it then
// handles exactly as the events of a player's own mouse and keyboard: the
// mode's filter (the tutorial input lock) first, then Dear ImGui. What the
// player looks at (UI tags, widgets, windows, the lesson, the game) comes
// from a Probe over the frame drawn last. Nothing here reads a clock: frame
// counts only.

#include "client/script/items.hpp"
#include "client/script/script.hpp"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opense4::client::script {

struct Box {
    ImVec2 min, max;
    bool valid() const { return max.x > min.x && max.y > min.y; }
    ImVec2 center() const { return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f); }
};

struct LessonInfo {
    std::string slug;
    bool tutorial = true;
    size_t step = 0;     // the step shown (1-based)
    size_t active = 0;   // the active step (1-based); one past the last once all are done
    size_t steps = 0;
    std::string result;  // "none", "done", "won", "lost"
    bool locked = false; // the tutorial input lock is on
};

// What the client showed in the frame drawn last, and its game.
class Probe {
public:
    virtual ~Probe() = default;
    // Geometry (ImGui units, the coordinates of input events).
    virtual std::vector<Box> tagBoxes(std::string_view name) const = 0;
    virtual std::vector<std::string> tagNames() const = 0;
    virtual const std::vector<Item>& items() const { return lastItems(); }
    virtual ImVec2 framePoint(float x, float y) const = 0;   // a point of the classic frame
    virtual float frameScale() const = 0;                     // ImGui units per frame pixel
    // The sectors of the system view (or the systems of the galaxy panel) a
    // target's query names, in order; empty with `error` when it is wrong.
    virtual std::vector<Box> sectors(const Target& t, std::string& error) const = 0;
    virtual std::vector<Box> systems(const Target& t, std::string& error) const = 0;
    // For the recorder: "sector:x,y" or "system:n" for a point on the
    // system view or the galaxy panel, else nothing.
    virtual std::string targetAt(ImVec2 p) const = 0;
    // State.
    virtual std::string screen() const = 0;                   // "game" or "front" (before a game)
    virtual std::vector<std::string> openWindows() const = 0; // window ids, oldest first
    virtual std::optional<LessonInfo> lesson() const = 0;
    virtual std::optional<uint32_t> turn() const = 0;
    virtual std::vector<std::string> logLines() const = 0;    // the player's log, "title: text"
    // A text field has the keyboard (typed text goes there).
    virtual bool typing() const = 0;
    // The tutorial input lock, described (for dump).
    virtual std::string lockDescription() const { return {}; }
    // A condition (docs/LEARNING.md "Conditions") over the game; "since"
    // counters count from `since`. Nothing (with `error`) without a game.
    virtual std::optional<bool> holds(const learn::Condition& c, const learn::Mark& since, std::string& error) const = 0;
    // Where the counters stand now, or where they stood when the game began.
    virtual learn::Mark mark(bool gameStart) const = 0;
    // The number a condition key stands at ("since" keys from `since`).
    virtual std::optional<int64_t> factValue(learn::Fact f, const learn::Mark& since) const = 0;
};

// One input event for the app to make into an SDL event.
struct InputEvent {
    enum class Kind : uint8_t { Motion, ButtonDown, ButtonUp, Wheel, KeyDown, KeyUp, Text };
    Kind kind = Kind::Motion;
    ImVec2 pos;                  // pointer events (ImGui units = window coordinates)
    int button = 1;              // SDL's numbers: 1 left, 2 middle, 3 right
    float wheel = 0;             // notches, positive up
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false, shift = false, alt = false;   // the modifiers held (key events)
    std::string text;            // Text
    bool decisive = false;       // the press or key whose fate (lock) the step checks
};

// What the mode did with an event (Mode::filterEvent).
enum class Verdict : uint8_t { Pass, Drop, PointerAway };

struct FrameOutput {
    std::vector<InputEvent> events;
    std::vector<std::filesystem::path> captures;   // save this frame's picture here
    std::vector<std::string> messages;             // progress and the failure, to print
};

class Player {
public:
    // `outputDir`: where relative screenshot names and the failure picture go.
    Player(Script script, std::filesystem::path outputDir);

    // The start of a frame: what the script does in it.
    FrameOutput tick(const Probe& probe);
    // What became of the events tick() gave, in order.
    void verdicts(std::span<const Verdict> v);

    bool finished() const { return finished_; }
    bool failed() const { return failed_; }
    const std::string& failure() const { return failure_; }
    const Script& script() const { return script_; }
    uint64_t frame() const { return frame_; }
    // The picture saved when a step fails.
    std::filesystem::path failureShot() const;

private:
    enum class Status : uint8_t { Running, Done, Failed };
    Status run(const Step& st, const Probe& probe, FrameOutput& out);
    Status pointerStep(const Step& st, const Probe& probe, FrameOutput& out);
    Status keyStep(const Step& st, FrameOutput& out);
    Status typeStep(const Step& st, FrameOutput& out);
    Status playQueue(const Step& st, FrameOutput& out);
    Status waitStep(const Step& st, const Probe& probe);
    Status check(const Step& st, const Probe& probe);
    Status fail(std::string why);

    struct Resolved {
        ImVec2 point;
        bool disabled = false;
    };
    std::optional<Resolved> resolve(const Target& t, const Probe& probe, std::string& why, Box* box = nullptr) const;
    // The rectangle a target names (its offset left out).
    bool resolveBox(const Target& t, const Probe& probe, Box& box, std::string& why) const;
    std::string context(const Probe& probe) const;

    Script script_;
    std::filesystem::path outputDir_;
    size_t step_ = 0;
    bool started_ = false;
    int stepFrames_ = 0;
    uint64_t frame_ = 0;
    bool finished_ = false;
    bool failed_ = false;
    std::string failure_;
    std::string why_;    // the last reason a wait went on (for its time-out message)

    // The frames of events still to send for the step under way.
    std::deque<std::vector<InputEvent>> queue_;
    bool queued_ = false;
    int settle_ = 0;
    // The press or key whose verdict decides, once known.
    std::optional<Verdict> decisive_;
    bool awaitingVerdict_ = false;
    std::vector<bool> sentDecisive_;   // which events of the frame sent last decide
    uint64_t lastPressFrame_ = 0;
    ImVec2 lastPressPos_;
    // Where the pointer step's events aim, and how often it had to aim again.
    std::optional<ImVec2> aimed_;
    int reaims_ = 0;
    learn::Mark mark_;
    // The loops under way: per repeat step, the passes made and where its condition counts from.
    struct Loop {
        int64_t passes = 0;
        learn::Mark since;
    };
    std::map<size_t, Loop> loops_;
};

} // namespace opense4::client::script
