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

TEST_CASE("settings: the mods' keys take their suggestions only where nothing else has them") {
    // docs/sdk/interface.md "Key bindings".
    const Bindings b;
    const std::vector<ModAction> actions{
        {"a.mod:order:mark", "A mod", "Mark", "Ctrl+Shift+M"},
        {"a.mod:order:move", "A mod", "Move on", "M"},              // the game's Move To has M
        {"b.mod:page:beacons", "B mod", "Beacons", "Ctrl+Shift+M"}, // A mod's Mark came first
        {"b.mod:panel:x", "B mod", "X", "Ctrl+NoSuchKey"},
        {"b.mod:order:quiet", "B mod", "Quiet", ""},
    };
    ModKeyChoices chosen;
    std::vector<ModKeys> keys = resolveModKeys(b, chosen, actions);
    REQUIRE(keys.size() == 5);
    CHECK(keys[0].chords[0] == KeyChord{ImGuiKey_M, true, true, false});
    CHECK(keys[0].conflict.empty());
    CHECK(keys[1].chords[0].empty());   // not taken from the game's binding
    CHECK(keys[1].conflict == "M is the key of Move to");
    CHECK(b.chords(Action::MoveTo)[0] == KeyChord{ImGuiKey_M});   // which keeps it
    CHECK(keys[2].chords[0].empty());
    CHECK(keys[2].conflict == "Ctrl+Shift+M is the key of Mark");
    CHECK(keys[3].conflict.find("not a key") != std::string::npos);
    CHECK(keys[4].chords[0].empty());
    CHECK(keys[4].conflict.empty());
    // The player's choice wins, and frees the suggestion for the next mod.
    chosen["a.mod:order:mark"] = {KeyChord{ImGuiKey_F7, true, false, false}, KeyChord{}};
    keys = resolveModKeys(b, chosen, actions);
    CHECK(keys[0].chosen);
    CHECK(keys[0].chords[0] == KeyChord{ImGuiKey_F7, true, false, false});
    CHECK(keys[2].chords[0] == KeyChord{ImGuiKey_M, true, true, false});
    CHECK(keys[2].conflict.empty());
    // Who has a chord: the game's actions, then the mods'.
    CHECK(chordUser(b, chosen, actions, KeyChord{ImGuiKey_M}) == "Move to");
    CHECK(chordUser(b, chosen, actions, KeyChord{ImGuiKey_F7, true, false, false}) == "Mark");
    CHECK(chordUser(b, chosen, actions, KeyChord{ImGuiKey_F7, true, false, false}, "a.mod:order:mark").empty());
    CHECK(chordUser(b, chosen, actions, KeyChord{ImGuiKey_M}, {}, Action::MoveTo).empty());
}

TEST_CASE("settings: the mods' keys the player chose are kept in the file") {
    AppSettings s;
    s.controls.modKeys["a.mod:order:mark"] = {KeyChord{ImGuiKey_F7, true, false, false}, KeyChord{}};
    s.controls.modKeys["b.mod:page:beacons"] = {KeyChord{}, KeyChord{}};   // unbound on purpose
    const std::string text = appSettingsToToml(s);
    CHECK(text.find("mod_keys") != std::string::npos);
    const AppSettings back = appSettingsFromToml(text);
    CHECK(back.controls.modKeys == s.controls.modKeys);
    CHECK(appSettingsToToml(AppSettings{}).find("mod_keys") == std::string::npos);
}
