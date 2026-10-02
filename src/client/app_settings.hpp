#pragma once

// Preferences of this machine that are not about one game: graphics, display
// and controls. Stored in <user data>/settings.toml. Command-line options
// override them for one run without being saved.

#include "client/input.hpp"

#include <filesystem>
#include <string>

namespace opense4::client {

enum class RendererChoice { Auto, Vulkan, OpenGL };
enum class DisplayMode { Windowed, Borderless, Fullscreen };
// How the classic 4:3 screens use a wider window.
enum class WidescreenLayout { Extended, Classic };
// The classic game's screen layout (docs/spec/06 §2.1.1): chosen by the desktop
// width as the original does (800 px or less: 800x600), or forced either way
// (an OpenSE4 extension, for testing and for players with large screens).
enum class LayoutChoice { Auto, Small800, Large1024 };

struct GraphicsSettings {
    RendererChoice renderer = RendererChoice::Auto;  // applies when the game starts
    DisplayMode displayMode = DisplayMode::Windowed;
    int windowWidth = 1600;
    int windowHeight = 900;
    int fullscreenWidth = 0;     // exclusive fullscreen mode; 0 = the desktop mode
    int fullscreenHeight = 0;
    float fullscreenRefresh = 0.0f;
    bool vsync = true;
    int frameLimit = 0;          // frames per second without vsync; 0 = unlimited
    WidescreenLayout widescreen = WidescreenLayout::Extended;
    LayoutChoice layout = LayoutChoice::Auto;
    bool sharpPixels = false;    // nearest-neighbour scaling of the classic art
    bool integerScaling = false; // scale the classic screens by whole multiples only
    float textScale = 1.0f;      // text size in the classic windows
    bool showFps = false;
};

struct ControlSettings {
    Bindings bindings;
    bool rightClickMoves = true;      // right-click on a sector gives Move To (not in the classic game)
    float doubleClickSeconds = 0.30f;
};

struct AppSettings {
    GraphicsSettings graphics;
    ControlSettings controls;
};

AppSettings& appSettings();
bool saveAppSettings();
std::filesystem::path appSettingsFile();
// The folder for everything this computer keeps (settings, saves, history):
// SDL's preference folder (~/.local/share/OpenSE4 on Linux, %APPDATA%\OpenSE4
// on Windows), or the folder named by the environment variable
// OPENSE4_USER_DIR (the tests use a scratch folder; also a portable install).
// Created if missing.
std::filesystem::path userDataDirectory();

// TOML round trip (exposed for tests).
std::string appSettingsToToml(const AppSettings& s);
AppSettings appSettingsFromToml(std::string_view text, std::string* error = nullptr);

const char* displayName(RendererChoice r);
const char* displayName(DisplayMode m);
const char* displayName(WidescreenLayout w);
const char* displayName(LayoutChoice l);

} // namespace opense4::client
