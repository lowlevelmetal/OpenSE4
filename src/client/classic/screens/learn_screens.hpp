#pragma once

// The Learn window (tabs Tutorials, Training, Manual) and the manual viewer
// (contents, search, back and forward, links), drawn the same way from the
// intro and during a game (docs/LEARNING.md). What differs is the host: in
// a game, starting a lesson replaces the game and window links open windows;
// in the front end, window links are dimmed.

#include "client/classic/learn_content.hpp"
#include "client/classic/screens/markdown_view.hpp"
#include "client/classic/ui.hpp"

#include <functional>
#include <string>
#include <vector>

namespace opense4::client::classic {

struct LearnHost {
    const LearnContent* content = nullptr;
    bool inGame = false;
    // A tutorial step locks the game: `window:` links stay shut (the lesson
    // decides which windows open).
    bool locked = false;
    // Starts a lesson or training game.
    std::function<void(learn::LessonKind, const std::string&)> start;
    // Shows the manual at "slug" or "slug#anchor".
    std::function<void(const std::string&)> openManual;
    // Follows a `window:` or `help:` link (games only).
    std::function<void(const learn::Link&)> follow;
};

class LearnView {
public:
    enum class Tab : uint8_t { Tutorials, Training, Manual };
    explicit LearnView(Tab tab = Tab::Tutorials) : tab_(tab) {}
    // "tutorials", "training", "manual"; anything else: Tutorials.
    static Tab tabFromName(std::string_view name);

    // Draws the window's content and buttons into `d`; returns false once it closes.
    bool draw(const Painter& p, Dialog& d, LearnHost& host);

private:
    void lessons(const Painter& p, LearnHost& host, learn::LessonKind kind);
    void contents(const Painter& p, LearnHost& host);

    Tab tab_;
    std::string selected_[3];   // per tab: a lesson slug, or "slug#anchor"
};

class ManualView {
public:
    explicit ManualView(std::string target = {}) { go(target); }

    bool draw(const Painter& p, Dialog& d, LearnHost& host);
    // Shows "slug" or "slug#anchor" (empty: the first page), as a new history entry.
    void go(const std::string& target);

private:
    void contents(const Painter& p, const learn::Library& lib);
    void follow(const std::string& target, LearnHost& host);

    std::vector<std::string> history_;
    size_t at_ = 0;
    std::string page_;        // the page shown ("" until resolved)
    MarkdownOptions options_;
    bool toTop_ = true;
    std::string query_;
};

} // namespace opense4::client::classic
