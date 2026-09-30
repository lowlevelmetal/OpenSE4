#include "client/app.hpp"
#include "core/log.hpp"
#include "ruleset/ruleset.hpp"

#include <charconv>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

constexpr const char* kUsage = R"(OpenSE4 - an open-source engine reimplementation for Space Empires IV Deluxe
(requires your own copy of the game; not affiliated with its publishers)

Usage: opense4 [options]

By default, plays the classic game on your installed copy of Space Empires IV
Deluxe (see docs/SETUP.md). Without an install, or with --prototype, runs the
prototype game (our own simplified rules and content).

Rendering:
  --renderer=auto|vulkan|opengl   Graphics backend (default: auto = Vulkan, fall back to OpenGL)
  --validation                    Enable Vulkan validation layers / GL debug output
  --no-vsync                      Disable vertical sync
  --fullscreen                    Start in fullscreen (Alt+Enter toggles)
  --size=WxH                      Window size (default 1600x900)
  --no-audio                      No sound or music

New game:
  --seed=N                        Galaxy seed (default: random)
  --systems=N                     Number of star systems
  --empires=N                     Number of empires, including yours (default 4)
  --shape=spiral|elliptical|ring|clusters
  --race=KEY                      Your race (see data/races.toml; default human)

Classic rules (the default when an install is found; see docs/PARITY_PLAN.md):
  --classic                       Require the classic engine (fail if no install is found)
  --prototype                     Run the prototype game instead
  --classic-dir=DIR               Game directory of the installed classic game (default: auto-detect)
  --quadrant=NAME                 Quadrant type from the data set (default: the first one)
  --quick-start[=RACE]            Skip the intro: start a quick game as RACE (a Pictures/Races folder name)
  --open=WINDOW                   With a quick start, open a window at once (e.g. --open=designs)
                                  or start on a front-end screen: intro, quickstart, setup[:PAGE],
                                  empiresetup[:PAGE], multiplayer (e.g. --open=setup:players)

Paths:
  --data=DIR                      Game data directory (default: auto-detect)
  --assets=DIR                    Assets directory (default: auto-detect)

Automation:
  --screenshot=FILE.png           Render a few frames, save a screenshot and exit
  --frames=N                      Frame to capture (default 10)
  --view=galaxy|system            Start on the galaxy map or the home system
  --turns=N                       Let the AI play N turns for every empire (yours too) first

  --verbose                       Debug logging
  --help                          Show this help
)";

bool parseInt(std::string_view s, auto& out) {
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

} // namespace

int main(int argc, char** argv) {
    bool prototype = false;  // --prototype, or a prototype-only option was given
    using namespace opense4;
    client::AppOptions options;
    options.setup.galaxy.seed = 0;
    options.setup.galaxy.systemCount = 0;  // 0 = rules default

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
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
        } else if (key == "--fullscreen") {
            options.fullscreen = true;
        } else if (key == "--size") {
            const auto x = value.find('x');
            ok = x != std::string_view::npos && parseInt(value.substr(0, x), options.width) && parseInt(value.substr(x + 1), options.height);
        } else if (key == "--seed") {
            ok = parseInt(value, options.setup.galaxy.seed);
        } else if (key == "--systems") {
            ok = parseInt(value, options.setup.galaxy.systemCount);
        } else if (key == "--empires") {
            ok = parseInt(value, options.setup.empireCount);
        } else if (key == "--shape") {
            prototype = true;
            const auto shape = sim::parseGalaxyShape(value);
            ok = shape.has_value();
            if (shape) options.setup.galaxy.shape = *shape;
        } else if (key == "--race") {
            prototype = true;
            options.setup.playerRace = std::string(value);
        } else if (key == "--classic") {
            options.classic = true;
        } else if (key == "--no-audio") {
            options.noAudio = true;
        } else if (key == "--prototype") {
            prototype = true;
        } else if (key == "--classic-dir") {
            options.classicDir = std::string(value);
            options.classic = true;
        } else if (key == "--quick-start") {
            options.classicQuickStart = true;
            options.classic = true;
            if (!value.empty()) options.classicRace = std::string(value);
        } else if (key == "--open") {
            options.classicWindow = std::string(value);
            options.classicQuickStart = true;
            options.classic = true;
        } else if (key == "--quadrant") {
            options.quadrantType = std::string(value);
        } else if (key == "--data") {
            prototype = true;
            options.dataDir = std::string(value);
        } else if (key == "--assets") {
            options.assetsDir = std::string(value);
        } else if (key == "--screenshot") {
            options.screenshotPath = std::string(value);
        } else if (key == "--frames") {
            ok = parseInt(value, options.screenshotFrames);
        } else if (key == "--view") {
            prototype = true;
            options.startInSystemView = value == "system";
            ok = value == "system" || value == "galaxy";
        } else if (key == "--turns") {
            ok = parseInt(value, options.autoTurns);
        } else if (key == "--verbose") {
            log::setMinLevel(log::Level::Debug);
        } else {
            ok = false;
        }
        if (!ok) {
            std::fprintf(stderr, "Invalid argument: %s\n\n%s", argv[i], kUsage);
            return 2;
        }
    }

    // Classic mode is the default when the player's classic install can be found.
    if (!options.classic && !prototype) {
        if (ruleset::findInstalledDataDir(options.classicDir)) options.classic = true;
        else log::info("No installed classic game found (see docs/SETUP.md); starting the prototype.");
    }
    if (options.setup.galaxy.seed == 0) options.setup.galaxy.seed = static_cast<uint64_t>(std::time(nullptr));
    return client::App().run(options);
}
