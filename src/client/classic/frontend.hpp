#pragma once

// Screens shown before a game is running: intro, quick start, game and
// empire setup, loading, and the multiplayer lobby (docs/spec/06 §1.1, §1.7).

#include "client/classic/art.hpp"
#include "client/classic/session.hpp"
#include "client/classic/ui.hpp"
#include "client/view_context.hpp"
#include "game/setup.hpp"

#include <functional>
#include <memory>
#include <string_view>

namespace opense4::client::classic {

enum class FrontId { Intro, QuickStart, GameSetup, LoadGame, Multiplayer };

struct MenuContext {
    std::shared_ptr<const game::Rules> rules;
    Art& art;
    const Fonts& fonts;
    FrameMapping map;
    float fbScale = 1.0f;
    double time = 0.0;
    uint64_t seed = 1;

    // Starts a game (the mode switches to the main window).
    std::function<void(std::unique_ptr<ClassicSession>)> startGame;
    std::function<void(FrontId)> go;
    std::function<void()> quit;
    std::string error;  // shown by the intro screen

    float k() const { return map.scale / fbScale; }
    ImVec2 at(Vec2 framePos) const {
        const Vec2 p = map.toFb(framePos) / fbScale;
        return {p.x, p.y};
    }
    ImVec2 size(Vec2 frameSize) const { return {frameSize.x * k(), frameSize.y * k()}; }
    float px(float v) const { return v * k(); }
};

class FrontScreen {
public:
    virtual ~FrontScreen() = default;
    virtual void draw(MenuContext& ctx) = 0;
};

std::unique_ptr<FrontScreen> makeFrontScreen(FrontId id);
// Automation (--open=NAME): a front-end screen by name: intro, quickstart,
// setup[:page] (Game Setup page, e.g. setup:players), empiresetup[:page]
// (Empire Setup for a new empire, e.g. empiresetup:traits), multiplayer.
// Returns nullptr if NAME is not a front-end screen.
std::unique_ptr<FrontScreen> frontScreenByName(std::string_view name);

// Creates a local game from a setup; on failure returns the reason.
std::expected<std::unique_ptr<ClassicSession>, std::string> startLocalGame(std::shared_ptr<const game::Rules> rules, const game::GameSetup& setup);

// Quick start: the player's race plus random computer opponents.
game::GameSetup quickStartSetup(const game::Rules& rules, std::string_view playerPreset, uint64_t seed, int opponents = 4);

// Game Setup / Empire Setup windows (setup.cpp in screens/), Multiplayer lobby (multiplayer.cpp).
// `startPage`: a Game Setup page name, or "empire[:page]" to open Empire Setup at once.
std::unique_ptr<FrontScreen> makeGameSetupScreen(std::string_view startPage = {});
std::unique_ptr<FrontScreen> makeMultiplayerScreen();

} // namespace opense4::client::classic
