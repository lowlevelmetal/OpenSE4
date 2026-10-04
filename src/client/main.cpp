#include "client/app.hpp"
#include "client/script/script.hpp"
#include "core/log.hpp"

#include <charconv>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

// SDL's entry point: on Windows it provides WinMain for the windowed build and
// calls main() below; elsewhere it changes nothing.
#include <SDL3/SDL_main.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
// The windowed build has no console of its own. Started from a terminal, write
// --help, logs and errors there.
void attachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}
#endif

constexpr const char* kUsage = R"(OpenSE4 - an open-source engine reimplementation for Space Empires IV Deluxe
(requires your own copy of the game; not affiliated with its publishers)

Usage: opense4 [options]

Plays Space Empires IV Deluxe on your own installed copy of the game. The game is
found in your Steam libraries, or given with --classic-dir (see docs/SETUP.md).

Rendering:
  --renderer=auto|vulkan|opengl   Graphics backend (default: auto = Vulkan, fall back to OpenGL)
  --validation                    Enable Vulkan validation layers / GL debug output
  --no-vsync                      Disable vertical sync
  --fullscreen                    Start in fullscreen (Alt+Enter toggles)
  --size=WxH                      Window size (default 1600x900)
  --layout=auto|800x600|1024x768  The classic screen layout (default: auto, 800x600 on a desktop
                                  800 px wide or less, as the original; forcing one is OpenSE4's)
  --text-size=N                   Settings' Text size for this run, 0.75 to 1.5
  --no-audio                      No sound or music
  --audio                         Sound and music even in a script or screenshot run (to check
                                  the audio, e.g. with SDL_AUDIO_DRIVER=disk: docs/SETUP.md)

Game:
  --classic-dir=DIR               Game directory of your installed copy (default: auto-detect)
  --quick-start[=RACE]            Skip the intro: start a quick game as RACE (a Pictures/Races folder name)
  --seed=N                        Seed for new games (default: random)
  --systems=N                     Number of star systems in a quick game
  --empires=N                     Number of empires in a quick game, including yours: N - 1 computer
                                  players and no neutral empire (default: as Quick Start, random
                                  computer and neutral players rolled from the seed)
  --quadrant=NAME                 Quadrant type from the data set (default: the first one)
  --turn-style=simultaneous|turn-based
                                  Turn style of a quick game (default: turn-based)
  --open=WINDOW[:ARG]             With a quick start, open a window at once (e.g. --open=designs,
                                  --open=help:hotkeys for a Help tab; none: no window, not even the Log)
                                  or start on a front-end screen: intro, quickstart, setup[:PAGE],
                                  empiresetup[:PAGE], multiplayer, pbem[:GAME.gam] (e.g. --open=setup:players).
                                  tactical: a sample tactical battle (your warships against copies);
                                  simulator: the Combat Simulator with that battle set up;
                                  ground-combat: your troop transports land on your homeworld in a
                                  sample strategic battle (needs a troop design: add --turns=70)

Learning to play (see docs/LEARNING.md):
  --tutorial=SLUG                 Start a tutorial (a guided lesson) at once
  --training=SLUG                 Start a training game at once
  --manual[=SLUG[#SECTION]]       Open the manual (at a page)
  --learn-dir=DIR                 Read tutorials, training games and the manual from DIR only
  --lesson-check                  With --tutorial=SLUG:N: open the windows step N works in, report
                                  whether everything it highlights or allows is on screen, and
                                  exit with 1 if not (tools/check_lessons.py runs every step)
  --lesson-audit                  The same, and print what the input lock lets through and whether
                                  what the step's text names can be seen (check_lessons.py --audit)

Play by e-mail (see docs/MULTIPLAYER.md):
  --pbem=GAME.gam                 Open the game file the host sent and play your turn; End Turn
                                  saves your orders file (.plr) to send back
  --pbem-empire=N                 Your empire's number (default: the only one that can play now)
  --pbem-password=PW              Your empire's password
  --pbem-orders=DIR               Where to save the orders file (default: the game file's folder)
  --pbem-end-turn                 End the turn at once, write the orders file and quit
                                  (with --screenshot: quit after the screenshot)

Paths:
  --assets=DIR                    OpenSE4's own assets directory (default: auto-detect, else built in)

Input scripts (see docs/BUILDING.md "Input scripts"):
  --input-script=FILE             Play FILE's clicks, keys and checks through the game's own input (FILE's
                                  options line adds options); exits with 1 when a step fails, with a picture
  --script-output=DIR             Where a script's screenshots and failure picture go (default: the system's
                                  temporary folder, opense4-scripts)
  --record-input=FILE             Record what you do as an input script, to start one from

Automation:
  --screenshot=FILE.png           Render a few frames, save a screenshot and exit (with a script: the last frame)
  --frames=N                      Frame to capture (default 10)
  --turns=N                       Let the AI play N turns for every empire (yours too) first
  --select=moving|fleet|ID        Then select one of your vehicles in the main window: the first with a
                                  move order, your first fleet with orders, or the vehicle with that id

  --verbose                       Debug logging
  --help                          Show this help
)";

bool parseInt(std::string_view s, auto& out) {
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    attachParentConsole();
#endif
    using namespace opense4;
    client::AppOptions options;
    // Start from the saved settings; command-line options override them for this run.
    client::GraphicsSettings& saved = client::appSettings().graphics;
    options.renderer = saved.renderer == client::RendererChoice::Vulkan   ? client::AppOptions::Renderer::Vulkan
                       : saved.renderer == client::RendererChoice::OpenGL ? client::AppOptions::Renderer::OpenGL
                                                                          : client::AppOptions::Renderer::Auto;
    options.vsync = saved.vsync;
    options.width = saved.windowWidth;
    options.height = saved.windowHeight;

    // An input script's options line comes first; the command line can override it.
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (!arg.starts_with("--input-script=")) continue;
        std::vector<std::string> errors;
        auto script = client::script::loadScript(std::string(arg.substr(15)), errors);
        if (!script) {
            for (const std::string& e : errors) std::fprintf(stderr, "%s\n", e.c_str());
            return 2;
        }
        args = script->options;
        options.recordOptions = script->options;
        options.inputScript = std::move(*script);
    }
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
        const std::string_view a = argv[i];
        // What a recording's options line repeats: the game's own options.
        if (!a.starts_with("--record-input") && !a.starts_with("--screenshot") && !a.starts_with("--frames") && !a.starts_with("--input-script") &&
            !a.starts_with("--script-output"))
            options.recordOptions.emplace_back(a);
    }

    for (const std::string& argument : args) {
        const std::string_view arg = argument;
        const auto eq = arg.find('=');
        const std::string_view key = arg.substr(0, eq);
        const std::string_view value = eq == std::string_view::npos ? std::string_view{} : arg.substr(eq + 1);
        bool ok = true;

        if (key == "--help" || key == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (key == "--renderer") {
            if (value == "auto") options.renderer = client::AppOptions::Renderer::Auto;
            else if (value == "vulkan" || value == "vk") options.renderer = client::AppOptions::Renderer::Vulkan;
            else if (value == "opengl" || value == "gl") options.renderer = client::AppOptions::Renderer::OpenGL;
            else ok = false;
        } else if (key == "--validation") {
            options.validation = true;
        } else if (key == "--no-vsync") {
            options.vsync = false;
            saved.vsync = false;
        } else if (key == "--fullscreen") {
            options.fullscreen = true;
            saved.displayMode = client::DisplayMode::Borderless;
        } else if (key == "--size") {
            const auto x = value.find('x');
            ok = x != std::string_view::npos && parseInt(value.substr(0, x), options.width) && parseInt(value.substr(x + 1), options.height);
            saved.displayMode = client::DisplayMode::Windowed;
            saved.windowWidth = options.width;
            saved.windowHeight = options.height;
        } else if (key == "--layout") {
            // The classic screen layout for this run (an OpenSE4 extension; docs/spec/06 §2.1.1).
            if (value == "auto") saved.layout = client::LayoutChoice::Auto;
            else if (value == "800x600") saved.layout = client::LayoutChoice::Small800;
            else if (value == "1024x768") saved.layout = client::LayoutChoice::Large1024;
            else ok = false;
        } else if (key == "--text-size") {
            // Settings → Graphics → Text size for this run (0.75 to 1.5).
            const std::string text(value);
            char* end = nullptr;
            const float scale = std::strtof(text.c_str(), &end);
            ok = !text.empty() && end == text.c_str() + text.size() && scale >= 0.75f && scale <= 1.5f;
            if (ok) saved.textScale = scale;
        } else if (key == "--seed") {
            ok = parseInt(value, options.seed);
        } else if (key == "--systems") {
            ok = parseInt(value, options.systemCount);
        } else if (key == "--empires") {
            ok = parseInt(value, options.empireCount);
        } else if (key == "--classic") {
            // Accepted and ignored, so older command lines keep working.
        } else if (key == "--no-audio") {
            options.noAudio = true;
        } else if (key == "--audio") {
            options.audio = true;
        } else if (key == "--classic-dir") {
            options.installDir = std::string(value);
        } else if (key == "--quick-start") {
            options.quickStart = true;
            if (!value.empty()) options.race = std::string(value);
        } else if (key == "--open") {
            options.openWindow = std::string(value);
            options.quickStart = true;
        } else if (key == "--pbem") {
            options.pbemFile = std::string(value);
            ok = !value.empty();
        } else if (key == "--pbem-empire") {
            ok = parseInt(value, options.pbemEmpire) && options.pbemEmpire > 0;
        } else if (key == "--pbem-password") {
            options.pbemPassword = std::string(value);
        } else if (key == "--pbem-orders") {
            options.pbemOrdersDir = std::string(value);
        } else if (key == "--pbem-end-turn") {
            options.pbemEndTurn = true;
        } else if (key == "--tutorial") {
            options.tutorial = std::string(value);
            ok = !value.empty();
        } else if (key == "--training") {
            options.training = std::string(value);
            ok = !value.empty();
        } else if (key == "--manual") {
            options.manual = std::string(value);
        } else if (key == "--learn-dir") {
            options.learnDir = std::string(value);
            ok = !value.empty();
        } else if (key == "--lesson-check") {
            options.lessonCheck = true;
        } else if (key == "--lesson-audit") {
            options.lessonCheck = true;
            options.lessonAudit = true;
        } else if (key == "--quadrant") {
            options.quadrantType = std::string(value);
        } else if (key == "--turn-style") {
            ok = value == "simultaneous" || value == "turn-based";
            options.turnBased = value == "turn-based";
        } else if (key == "--assets") {
            options.assetsDir = std::string(value);
        } else if (key == "--screenshot") {
            options.screenshotPath = std::string(value);
        } else if (key == "--frames") {
            ok = parseInt(value, options.screenshotFrames);
        } else if (key == "--turns") {
            ok = parseInt(value, options.autoTurns);
        } else if (key == "--select") {
            options.select = std::string(value);
            ok = !value.empty();
        } else if (key == "--input-script") {
            ok = options.inputScript.has_value();
        } else if (key == "--script-output") {
            options.scriptOutput = std::string(value);
            ok = !value.empty();
        } else if (key == "--record-input") {
            options.recordInput = std::string(value);
            ok = !value.empty();
        } else if (key == "--verbose") {
            log::setMinLevel(log::Level::Debug);
        } else {
            ok = false;
        }
        if (!ok) {
            std::fprintf(stderr, "Invalid argument: %s\n\n%s", argument.c_str(), kUsage);
            return 2;
        }
    }

    // A script run is the same every time: a fixed seed unless one is given.
    // Otherwise the clock's, for local games only: a network game hosted here
    // draws its seed from the cryptographic random source unless one was
    // given (multiplayer.cpp), since its players must not guess it.
    options.seedGiven = options.seed != 0 || options.inputScript;
    if (options.seed == 0) options.seed = options.inputScript ? 1 : static_cast<uint64_t>(std::time(nullptr));
    // The log also goes to opense4.log in the user data folder: on Windows the
    // game has no console, so that file is where a player finds it.
    log::setFile(client::userDataDirectory() / "opense4.log");
    return client::App().run(options);
}
