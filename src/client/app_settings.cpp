#include "client/app_settings.hpp"

#include "core/environment.hpp"
#include "core/log.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

namespace opense4::client {

namespace {

template <class E, size_t N>
E fromName(std::string_view name, const std::array<std::pair<E, const char*>, N>& table, E fallback) {
    for (const auto& [value, text] : table)
        if (name == text) return value;
    return fallback;
}

template <class E, size_t N>
const char* toName(E value, const std::array<std::pair<E, const char*>, N>& table) {
    for (const auto& [v, text] : table)
        if (v == value) return text;
    return table.front().second;
}

constexpr std::array<std::pair<RendererChoice, const char*>, 3> kRenderers{
    {{RendererChoice::Auto, "auto"}, {RendererChoice::Vulkan, "vulkan"}, {RendererChoice::OpenGL, "opengl"}}};
constexpr std::array<std::pair<DisplayMode, const char*>, 3> kModes{
    {{DisplayMode::Windowed, "windowed"}, {DisplayMode::Borderless, "borderless"}, {DisplayMode::Fullscreen, "fullscreen"}}};
constexpr std::array<std::pair<WidescreenLayout, const char*>, 2> kLayouts{
    {{WidescreenLayout::Extended, "extended"}, {WidescreenLayout::Classic, "classic"}}};
constexpr std::array<std::pair<LayoutChoice, const char*>, 3> kScreenLayouts{
    {{LayoutChoice::Auto, "auto"}, {LayoutChoice::Small800, "800x600"}, {LayoutChoice::Large1024, "1024x768"}}};

std::unique_ptr<AppSettings>& instance() {
    static std::unique_ptr<AppSettings> s;
    return s;
}

} // namespace

const char* displayName(RendererChoice r) {
    switch (r) {
        case RendererChoice::Auto: return "Automatic (Vulkan, else OpenGL)";
        case RendererChoice::Vulkan: return "Vulkan";
        case RendererChoice::OpenGL: return "OpenGL";
    }
    return "?";
}

const char* displayName(DisplayMode m) {
    switch (m) {
        case DisplayMode::Windowed: return "Windowed";
        case DisplayMode::Borderless: return "Borderless fullscreen";
        case DisplayMode::Fullscreen: return "Exclusive fullscreen";
    }
    return "?";
}

const char* displayName(WidescreenLayout w) {
    switch (w) {
        case WidescreenLayout::Extended: return "Extended (use the whole width)";
        case WidescreenLayout::Classic: return "Classic 4:3 (bars at the sides)";
    }
    return "?";
}

const char* displayName(LayoutChoice l) {
    switch (l) {
        case LayoutChoice::Auto: return "Automatic (800x600 on a desktop 800 wide or less)";
        case LayoutChoice::Small800: return "800x600";
        case LayoutChoice::Large1024: return "1024x768";
    }
    return "?";
}

std::filesystem::path userDataDirectory() {
    std::filesystem::path dir;
    if (const auto own = core::environment("OPENSE4_USER_DIR"); own && !own->empty()) {
        dir = *own;  // UTF-8, as narrow strings become paths everywhere
    } else if (char* pref = SDL_GetPrefPath("", "OpenSE4")) {
        dir = pref;
        SDL_free(pref);
    } else {
        dir = std::filesystem::current_path() / "userdata";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path appSettingsFile() { return userDataDirectory() / "settings.toml"; }

std::string appSettingsToToml(const AppSettings& s) {
    const GraphicsSettings& g = s.graphics;
    toml::table graphics;
    graphics.insert("renderer", toName(g.renderer, kRenderers));
    graphics.insert("display_mode", toName(g.displayMode, kModes));
    graphics.insert("window_width", g.windowWidth);
    graphics.insert("window_height", g.windowHeight);
    graphics.insert("fullscreen_width", g.fullscreenWidth);
    graphics.insert("fullscreen_height", g.fullscreenHeight);
    graphics.insert("fullscreen_refresh", double(g.fullscreenRefresh));
    graphics.insert("vsync", g.vsync);
    graphics.insert("frame_limit", g.frameLimit);
    graphics.insert("widescreen", toName(g.widescreen, kLayouts));
    graphics.insert("layout", toName(g.layout, kScreenLayouts));
    graphics.insert("sharp_pixels", g.sharpPixels);
    graphics.insert("integer_scaling", g.integerScaling);
    graphics.insert("text_scale", double(g.textScale));
    graphics.insert("show_fps", g.showFps);

    toml::table keys;
    for (const ActionInfo& info : actionInfos()) {
        const auto& chords = s.controls.bindings.chords(info.action);
        toml::array pair;
        pair.push_back(chordName(chords[0]));
        pair.push_back(chordName(chords[1]));
        keys.insert(info.key, std::move(pair));
    }
    toml::table controls;
    controls.insert("right_click_moves", s.controls.rightClickMoves);
    controls.insert("double_click_seconds", double(s.controls.doubleClickSeconds));
    controls.insert("keys", std::move(keys));
    if (!s.controls.modKeys.empty()) {
        toml::table modKeys;
        for (const auto& [id, chords] : s.controls.modKeys) {
            toml::array pair;
            pair.push_back(chords[0].empty() ? std::string() : chordName(chords[0]));
            pair.push_back(chords[1].empty() ? std::string() : chordName(chords[1]));
            modKeys.insert(id, std::move(pair));
        }
        controls.insert("mod_keys", std::move(modKeys));
    }

    toml::table root;
    root.insert("graphics", std::move(graphics));
    root.insert("controls", std::move(controls));
    std::ostringstream out;
    out << "# OpenSE4 settings (graphics, display, controls)\n" << root << "\n";
    return out.str();
}

AppSettings appSettingsFromToml(std::string_view text, std::string* error) {
    AppSettings s;
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error& e) {
        if (error) *error = std::string(e.description());
        return s;
    }
    GraphicsSettings& g = s.graphics;
    const toml::node_view gr = root["graphics"];
    g.renderer = fromName(gr["renderer"].value_or(std::string{}), kRenderers, g.renderer);
    g.displayMode = fromName(gr["display_mode"].value_or(std::string{}), kModes, g.displayMode);
    g.windowWidth = std::clamp(gr["window_width"].value_or(g.windowWidth), 640, 16384);
    g.windowHeight = std::clamp(gr["window_height"].value_or(g.windowHeight), 480, 16384);
    g.fullscreenWidth = std::max(0, gr["fullscreen_width"].value_or(0));
    g.fullscreenHeight = std::max(0, gr["fullscreen_height"].value_or(0));
    g.fullscreenRefresh = float(gr["fullscreen_refresh"].value_or(0.0));
    g.vsync = gr["vsync"].value_or(g.vsync);
    g.frameLimit = std::clamp(gr["frame_limit"].value_or(0), 0, 1000);
    g.widescreen = fromName(gr["widescreen"].value_or(std::string{}), kLayouts, g.widescreen);
    g.layout = fromName(gr["layout"].value_or(std::string{}), kScreenLayouts, g.layout);
    g.sharpPixels = gr["sharp_pixels"].value_or(g.sharpPixels);
    g.integerScaling = gr["integer_scaling"].value_or(g.integerScaling);
    g.textScale = std::clamp(float(gr["text_scale"].value_or(1.0)), 0.75f, 2.0f);
    g.showFps = gr["show_fps"].value_or(g.showFps);

    const toml::node_view co = root["controls"];
    s.controls.rightClickMoves = co["right_click_moves"].value_or(s.controls.rightClickMoves);
    s.controls.doubleClickSeconds = std::clamp(float(co["double_click_seconds"].value_or(0.30)), 0.1f, 1.0f);
    if (const toml::table* keys = co["keys"].as_table())
        for (const ActionInfo& info : actionInfos())
            if (const toml::array* pair = (*keys)[info.key].as_array())
                for (size_t slot = 0; slot < 2 && slot < pair->size(); ++slot)
                    if (auto name = (*pair)[slot].value<std::string>())
                        if (auto chord = parseChord(*name)) s.controls.bindings.set(info.action, int(slot), *chord);
    if (const toml::table* modKeys = co["mod_keys"].as_table())
        for (const auto& [id, node] : *modKeys)
            if (const toml::array* pair = node.as_array()) {
                std::array<KeyChord, 2> chords{};
                for (size_t slot = 0; slot < 2 && slot < pair->size(); ++slot)
                    if (auto name = (*pair)[slot].value<std::string>())
                        if (auto chord = parseChord(*name)) chords[slot] = *chord;
                s.controls.modKeys[std::string(id.str())] = chords;
            }
    return s;
}

AppSettings& appSettings() {
    auto& s = instance();
    if (!s) {
        s = std::make_unique<AppSettings>();
        std::ifstream in(appSettingsFile());
        if (in) {
            std::stringstream buf;
            buf << in.rdbuf();
            std::string error;
            *s = appSettingsFromToml(buf.str(), &error);
            if (!error.empty()) log::warn("Settings file {}: {}", appSettingsFile().string(), error);
        }
    }
    return *s;
}

bool saveAppSettings() {
    const std::filesystem::path file = appSettingsFile();
    std::ofstream out(file, std::ios::binary | std::ios::trunc);  // LF line ends on every platform
    if (!out) {
        log::warn("Could not write {}", file.string());
        return false;
    }
    out << appSettingsToToml(appSettings());
    return static_cast<bool>(out);
}

} // namespace opense4::client
