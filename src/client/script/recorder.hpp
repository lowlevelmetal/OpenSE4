#pragma once

// Records a session played by hand as an input script (--record-input, docs/
// BUILDING.md "Input scripts"), to start a script from: clicks, drags, the
// wheel, keys and typing, each click named by what lies under the pointer
// (a widget's label, a UI tag, a sector or a system, else a point of the
// frame), and the lesson's progress as wait-step lines so that the script
// waits where the player did.

#include "client/script/player.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace opense4::client::script {

class Recorder {
public:
    // `options`: the command line to write as the script's options line.
    Recorder(std::filesystem::path file, std::vector<std::string> options);

    // Each input event of the player's, before the mode sees it, with what
    // the frame drawn last showed; `seconds` is the time of the frame.
    void event(const SDL_Event& e, const Probe& probe, double seconds);
    // Once a frame, after the events: the lesson's progress.
    void frame(const Probe& probe, double seconds);
    // Writes the script; false (with `error`) when it cannot be written.
    bool save(std::string& error);
    const std::filesystem::path& file() const { return file_; }

    // The target a script would name for a point: the smallest labelled
    // widget or UI tag around it, a sector or a system, else the frame point.
    static std::string targetFor(ImVec2 p, const Probe& probe);

private:
    void line(std::string text, double seconds);
    // A wait-... line for the lesson's or the game's progress (after the click under way, if any).
    void progress(std::string text);
    void flushText(double seconds);

    std::filesystem::path file_;
    std::vector<std::string> options_;
    std::vector<std::string> lines_;
    std::vector<std::string> deferred_;
    double lastAction_ = -1;
    std::string text_;    // typed, not yet written
    // The press of a click or drag under way.
    struct Press {
        int button = 1;
        ImVec2 at;
        std::string target;
        bool moved = false;
    };
    std::optional<Press> press_;
    // The last click, which a second one at the same place makes a double click.
    struct LastClick {
        std::string target;
        ImVec2 at;
        double seconds = -10;
        size_t line = 0;
    };
    LastClick lastClick_;
    bool ctrl_ = false, shift_ = false, alt_ = false;
    size_t lessonStep_ = 0;
    std::string lessonResult_;
    std::string lessonSlug_;
    std::optional<uint32_t> turn_;
};

} // namespace opense4::client::script
