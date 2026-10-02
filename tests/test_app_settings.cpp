// Graphics/controls settings and key bindings (client/app_settings.hpp, client/input.hpp).

#include "client/app_settings.hpp"
#include "client/input.hpp"

#include <doctest/doctest.h>

#include <SDL3/SDL_keycode.h>

using namespace opense4::client;

TEST_CASE("settings: key chords print and parse") {
    const KeyChord f12{ImGuiKey_F12};
    CHECK(chordName(f12) == "F12");
    CHECK(parseChord("F12") == f12);
    const KeyChord ctrlShiftN{ImGuiKey_N, true, true, false};
    CHECK(chordName(ctrlShiftN) == "Ctrl+Shift+N");
    CHECK(parseChord(chordName(ctrlShiftN)) == ctrlShiftN);
    CHECK(parseChord("(none)")->empty());
    CHECK_FALSE(parseChord("Ctrl+NoSuchKey").has_value());
}

TEST_CASE("settings: default bindings are the classic hotkeys and conflicts are found") {
    const Bindings b;
    CHECK(b.chords(Action::EndTurn)[0] == KeyChord{ImGuiKey_F12});
    CHECK(b.chords(Action::MoveTo)[0] == KeyChord{ImGuiKey_M});
    CHECK(b.chords(Action::ClearOrders)[1] == KeyChord{ImGuiKey_Backspace});
    // Every default chord is used by exactly one action.
    for (const ActionInfo& a : actionInfos())
        for (const KeyChord& c : b.chords(a.action)) CHECK_FALSE(b.boundTo(c, a.action).has_value());
    CHECK(b.boundTo(KeyChord{ImGuiKey_M}, Action::Warp) == Action::MoveTo);
}

TEST_CASE("settings: the file round trip keeps every value") {
    AppSettings s;
    s.graphics.renderer = RendererChoice::OpenGL;
    s.graphics.displayMode = DisplayMode::Fullscreen;
    s.graphics.fullscreenWidth = 2560;
    s.graphics.fullscreenHeight = 1440;
    s.graphics.fullscreenRefresh = 144.0f;
    s.graphics.vsync = false;
    s.graphics.frameLimit = 120;
    s.graphics.widescreen = WidescreenLayout::Classic;
    s.graphics.sharpPixels = true;
    s.graphics.integerScaling = true;
    s.graphics.textScale = 1.25f;
    s.controls.rightClickMoves = false;
    s.controls.bindings.set(Action::MoveTo, 0, KeyChord{ImGuiKey_J, false, true, false});
    s.controls.bindings.set(Action::EndTurn, 1, {});
    std::string error;
    const AppSettings back = appSettingsFromToml(appSettingsToToml(s), &error);
    CHECK(error.empty());
    CHECK(back.graphics.renderer == RendererChoice::OpenGL);
    CHECK(back.graphics.displayMode == DisplayMode::Fullscreen);
    CHECK(back.graphics.fullscreenWidth == 2560);
    CHECK(back.graphics.fullscreenRefresh == doctest::Approx(144.0));
    CHECK_FALSE(back.graphics.vsync);
    CHECK(back.graphics.frameLimit == 120);
    CHECK(back.graphics.widescreen == WidescreenLayout::Classic);
    CHECK(back.graphics.sharpPixels);
    CHECK(back.graphics.integerScaling);
    CHECK(back.graphics.textScale == doctest::Approx(1.25));
    CHECK_FALSE(back.controls.rightClickMoves);
    CHECK(back.controls.bindings.chords(Action::MoveTo)[0] == KeyChord{ImGuiKey_J, false, true, false});
    CHECK(back.controls.bindings.chords(Action::EndTurn)[1].empty());
    // Garbage is reported, and the defaults are used.
    const AppSettings bad = appSettingsFromToml("this is [not toml", &error);
    CHECK_FALSE(error.empty());
    CHECK(bad.graphics.vsync);
}

TEST_CASE("input: AltGr counts as Alt, as SDL reports it on Windows") {

    CHECK(altGrAsAlt(SDL_KMOD_MODE) == (SDL_KMOD_MODE | SDL_KMOD_RALT));
    CHECK(altGrAsAlt(SDL_KMOD_MODE | SDL_KMOD_LSHIFT) == (SDL_KMOD_MODE | SDL_KMOD_RALT | SDL_KMOD_LSHIFT));
    CHECK(altGrAsAlt(SDL_KMOD_LCTRL) == SDL_KMOD_LCTRL);
    CHECK(altGrAsAlt(SDL_KMOD_RALT) == SDL_KMOD_RALT);
    CHECK(altGrAsAlt(SDL_KMOD_NONE) == SDL_KMOD_NONE);
}
