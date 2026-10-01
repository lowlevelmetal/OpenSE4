#include "client/classic/frontend.hpp"

#include "client/app_settings.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/settings_window.hpp"
#include "datafile/datafile.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <functional>

#ifndef OPENSE4_CLIENT_VERSION
#define OPENSE4_CLIENT_VERSION "0.0.0"
#endif

namespace opense4::client::classic {

// The intro picture stretched over the whole window, as the original shows it.
void introBackground(MenuContext& ctx) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a{0, 0}, b = ImGui::GetIO().DisplaySize;
    if (Sprite bg = ctx.art.imageAny({"Pictures/Game/Screens/1024X768/Intro.bmp", "Pictures/Game/Screens/800X600/Intro.bmp"}, false))
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(bg.tex.value)), a, b, ImVec2(bg.uv.min.x, bg.uv.min.y), ImVec2(bg.uv.max.x, bg.uv.max.y));
    else dl->AddRectFilled(a, b, IM_COL32(2, 4, 12, 255));
}

namespace {

void background(MenuContext& ctx) { introBackground(ctx); }

// A classic window (pipe frame, title strip) with its content area as the ImGui window.
bool beginPanel(MenuContext& ctx, const char* id, Rect r, const char* title = nullptr) {
    ImGui::SetNextWindowPos(ctx.at(r.min));
    ImGui::SetNextWindowSize(ctx.size(r.size()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ctx.size({16, title ? 38.0f : 12.0f}));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool open = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    if (open) drawWindowFrame(ctx.painter(), ImGui::GetWindowDrawList(), r, title, 0);
    return open;
}

void endPanel() { ImGui::End(); }

class IntroScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        // The original's intro (docs/spec/06 §1.1, §7 Q9): a black band along the
        // bottom with two rows of four buttons across the full width, the version
        // at the left and the data set at the right. OpenSE4's own entries sit
        // in a small row at the top right, apart from the original's layout.
        const Painter p = ctx.painter();
        const float left = ctx.map.left, right = ctx.map.right;
        ImGui::GetBackgroundDrawList()->AddRectFilled(ctx.at({left, 695}), ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y),
                                                      IM_COL32_BLACK);
        ImGui::SetNextWindowPos(ctx.at({left, 672}));
        ImGui::SetNextWindowSize(ctx.size({right - left, 96}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("##intro", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoBackground)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::PushFont(ctx.fonts.small, p.fontPx(kSmallSize));
            dl->AddText(ctx.at({left + 10, 682}), IM_COL32(220, 220, 220, 255), "Version: OpenSE4 " OPENSE4_CLIENT_VERSION);
            const std::string data = "Data: " + ctx.rules->data().dataDir.parent_path().filename().string();
            dl->AddText(ctx.at({right - 12 - ImGui::CalcTextSize(data.c_str()).x / ctx.k(), 682}), IM_COL32(220, 220, 220, 255), data.c_str());
            ImGui::PopFont();
            struct Entry {
                const char* label;
                std::function<void()> go;
            };
            // Resume Game loads the last game saved on this machine. The original's
            // Tutorial and Scenario open our Learn window (docs/LEARNING.md) on its
            // Tutorials and Training tabs.
            const std::filesystem::path last = settings().lastSavedGame;
            std::error_code ec;
            const bool canResume = !last.empty() && std::filesystem::is_regular_file(last, ec);
            const std::array<Entry, 8> entries{{
                {"Quick Start", [&] { ctx.go(FrontId::QuickStart); }},
                {"New Game", [&] { ctx.go(FrontId::GameSetup); }},
                {"Resume Game", canResume ? std::function<void()>([&] { resume(ctx, last); }) : nullptr},
                {"Load Game", [&] { ctx.go(FrontId::LoadGame); }},
                {"Tutorial", [&] { ctx.go(FrontId::Learn); }},
                {"Scenario", [&] { ctx.go(FrontId::LearnTraining); }},
                {"Credits", [&] { ctx.go(FrontId::Credits); }},
                {"Quit Game", [&] { ctx.quit(); }},
            }};
            constexpr size_t kColumns = 4;
            const float w = (right - left - 24 - 5 * float(kColumns - 1)) / float(kColumns);
            for (size_t i = 0; i < entries.size(); ++i) {
                ImGui::SetCursorScreenPos(ctx.at({left + 12 + float(i % kColumns) * (w + 5), 700 + float(i / kColumns) * 30}));
                if (classicButton(p, entries[i].label, {w, 26}, 0, false, entries[i].go != nullptr) && entries[i].go) entries[i].go();
            }
            if (!ctx.error.empty()) dl->AddText(ctx.at({left + 12, 660}), IM_COL32(255, 128, 100, 255), ctx.error.c_str());
            if (!error_.empty()) dl->AddText(ctx.at({left + 12, 644}), IM_COL32(255, 128, 100, 255), error_.c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        // OpenSE4's own entries: multiplayer, its settings and the manual.
        ImGui::SetNextWindowPos(ctx.at({right - 12 - 3 * 112 - 2 * 4, 10}));
        ImGui::SetNextWindowSize(ctx.size({3 * 112 + 2 * 4, 24}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ctx.px(4), 0));
        if (ImGui::Begin("##intro-extras", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                       ImGuiWindowFlags_NoBackground)) {
            const std::array<std::pair<const char*, FrontId>, 3> extras{
                {{"Multiplayer", FrontId::Multiplayer}, {"Settings", FrontId::Settings}, {"Manual", FrontId::Manual}}};
            for (size_t i = 0; i < extras.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                if (classicButton(p, extras[i].first, {112, 22})) ctx.go(extras[i].second);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

private:
    void resume(MenuContext& ctx, const std::filesystem::path& file) {
        auto session = ClassicSession::load(ctx.rules, file);
        if (!session) {
            error_ = session.error();
            return;
        }
        restoreHistoryFrom(file);
        if (ctx.loadedFromIntro) ctx.loadedFromIntro();
        ctx.startGame(std::move(*session));
    }
    std::string error_;
};

// Credits, in our own words: OpenSE4, its licence and contributors, what it
// is built with, and where the game's own files come from.
class CreditsScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        if (beginPanel(ctx, "##credits", Rect{{212, 100}, {812, 668}}, "Credits")) {
            const Painter p = ctx.painter();
            ImGui::BeginChild("##text", ImVec2(0, -ctx.px(42)));
            ImGui::PushTextWrapPos(0.0f);
            auto section = [&](const char* title) {
                ImGui::Spacing();
                heading(p, title);
            };
            ImGui::TextUnformatted("OpenSE4 " OPENSE4_CLIENT_VERSION);
            ImGui::TextColored(kDimText, "An open-source engine for Space Empires IV Deluxe.");
            section("Licence");
            ImGui::TextUnformatted("OpenSE4 is free software under the GNU General Public License, version 3 or any later version. "
                                   "It comes with no warranty. The licence text is in the LICENSE file.");
            section("Made by");
            ImGui::TextUnformatted("Matthew Geiger and the OpenSE4 contributors.");
            section("Built with");
            ImGui::TextUnformatted("SDL 3, Dear ImGui, the Vulkan headers, volk, Vulkan Memory Allocator, toml++, stb_image, "
                                   "dr_mp3 and miniupnpc, each under its own licence (see THIRD_PARTY_NOTICES.txt in the release).");
            section("Fonts");
            ImGui::TextUnformatted("Noto Sans, under the SIL Open Font License. In a game, the bitmap fonts are read from your installed copy.");
            section("The game's own files");
            ImGui::TextUnformatted("The rules data, pictures, sounds, music and fonts you see and hear come from your own installed "
                                   "copy of Space Empires IV Deluxe, read where they are. None of them is part of OpenSE4.");
            ImGui::TextColored(kDimText, "Space Empires is a trademark of its owner. OpenSE4 is an independent project, not affiliated with, "
                                         "endorsed by or sponsored by Strategy First or Malfador Machinations.");
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            if (classicButton(p, "Back", {140, 28}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ctx.go(FrontId::Intro);
        }
        endPanel();
    }
};

// Tiles the star field over the whole window (the empire picker's backdrop).
void starfield(MenuContext& ctx) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled({0, 0}, size, IM_COL32_BLACK);
    const Sprite sky = ctx.art.image("Pictures/Game/Screens/1024X768/Starmap.bmp", false);
    if (!sky) return;
    const float tw = ctx.px(sky.size.x), th = ctx.px(sky.size.y);
    for (float y = 0; y < size.y; y += th)
        for (float x = 0; x < size.x; x += tw)
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(sky.tex.value)), {x, y}, {x + tw, y + th}, {sky.uv.min.x, sky.uv.min.y},
                         {sky.uv.max.x, sky.uv.max.y});
}

class QuickStartScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        // The original's picker: a title and hint at the left, a framed two-column
        // list of portraits with each race's name and description, and the
        // Begin Game and Cancel buttons below it.
        starfield(ctx);
        const Painter p = ctx.painter();
        const auto& presets = ctx.rules->racePresets();
        ImGui::SetNextWindowPos(ctx.at({0, 0}));
        ImGui::SetNextWindowSize(ctx.size({kFrameW, kFrameH}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("##quick", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                 ImGuiWindowFlags_NoBackground)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::PushFont(ctx.fonts.bold, p.fontPx(kTitleSize));
            dl->AddText(ctx.at({113, 84}), IM_COL32_WHITE, "Select Empire");
            ImGui::PopFont();
            ImGui::SetCursorPos(ctx.size({111, 126}));
            ImGui::PushTextWrapPos(ctx.px(280));
            ImGui::TextColored(kLabelBlue, "Choose the empire you will lead by clicking its portrait.");
            ImGui::PopTextWrapPos();

            const Rect box{{328, 85}, {912, 632}};
            drawWindowFrame(p, dl, box, nullptr, 0);
            ImGui::SetCursorPos(ctx.size({338, 95}));
            ImGui::BeginChild("##races", ctx.size({566, 530}));
            int col = 0;
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].neutral) continue;
                if (col++ % 2) ImGui::SameLine(ctx.px(277));
                ImGui::PushID(int(i));
                ImGui::BeginGroup();
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const ImVec2 b{a.x + ctx.px(128), a.y + ctx.px(128)};
                if (ImGui::InvisibleButton("##portrait", ctx.size({128, 128}))) chosen_ = int(i);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) begin(ctx, i);
                ImDrawList* cl = ImGui::GetWindowDrawList();
                if (Sprite portrait = ctx.art.racePortrait(presets[i].folder))
                    cl->AddImage(ImTextureRef(static_cast<ImTextureID>(portrait.tex.value)), a, b, {portrait.uv.min.x, portrait.uv.min.y},
                                 {portrait.uv.max.x, portrait.uv.max.y});
                const bool selected = int(i) == chosen_;
                cl->AddRect(a, b, selected ? IM_COL32(255, 220, 90, 255) : imColor(palette::kFrame), 0.0f, selected ? 2.0f : 1.0f);
                ImGui::SameLine(0, ctx.px(5));
                ImGui::BeginGroup();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ctx.px(138));
                const std::string name = presets[i].empireType.empty() ? presets[i].name : presets[i].name + " " + presets[i].empireType;
                ImGui::TextUnformatted(name.c_str());
                ImGui::PushFont(ctx.fonts.small, p.fontPx(kSmallSize));
                ImGui::TextUnformatted(presets[i].description.c_str());
                ImGui::PopFont();
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::EndGroup();
                ImGui::PopID();
                if (col % 2 == 0) ImGui::Dummy(ctx.size({0, 2}));
            }
            ImGui::EndChild();

            ImGui::SetCursorPos(ctx.size({608, 645}));
            if (classicButton(p, "Begin Game", {148, 26}, 0, false, chosen_ >= 0)) begin(ctx, size_t(chosen_));
            ImGui::SetCursorPos(ctx.size({761, 645}));
            if (classicButton(p, "Cancel", {148, 26}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ctx.go(FrontId::Intro);
            if (!error_.empty()) dl->AddText(ctx.at({328, 680}), IM_COL32(255, 128, 100, 255), error_.c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    void begin(MenuContext& ctx, size_t preset) {
        auto setup = quickStartSetup(*ctx.rules, ctx.rules->racePresets()[preset].folder, ctx.seed);
        auto session = startLocalGame(ctx.rules, setup);
        if (session) ctx.startGame(std::move(*session));
        else error_ = session.error();
    }

    int chosen_ = -1;
    std::string error_;
};

class SettingsFrontScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        // The same 780×475 classic window as the in-game Settings.
        const Painter p = ctx.painter();
        const Rect r{{122, 146}, {902, 621}};
        ImGui::SetNextWindowPos(ctx.at(r.min));
        ImGui::SetNextWindowSize(ctx.size(r.size()));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            drawWindowFrame(p, ImGui::GetWindowDrawList(), r, "Settings", 180);
            ImGui::SetCursorPos(ctx.size({15, 35}));
            ImGui::BeginChild("##page", ctx.size({556, 431}));
            switch (page_) {
                case 0:
                    if (ctx.app) graphicsSettingsPage(state_, *ctx.app, ctx.k());
                    break;
                case 1: controlsSettingsPage(state_, ctx.k()); break;
                default: soundSettingsPage(ctx.k()); break;
            }
            ImGui::EndChild();
            static constexpr std::array<const char*, 3> kPages{"Graphics", "Controls", "Sound"};
            for (int i = 0; i < 3; ++i) {
                ImGui::SetCursorPos(ctx.size({585, 35 + 31 * float(i)}));
                if (classicButton(p, kPages[size_t(i)], {180, 28}, 1, page_ == i)) page_ = i;
            }
            for (int i = 3; i < 13; ++i) {
                ImGui::SetCursorPos(ctx.size({585, 35 + 31 * float(i)}));
                emptySlot(p, {180, 28});
            }
            ImGui::SetCursorPos(ctx.size({585, 438}));
            if (classicButton(p, "Back", {180, 28}) || (!state_.capturing && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) ctx.go(FrontId::Intro);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    int page_ = 0;
    SettingsPanelState state_;
};

class LoadGameScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        if (!scanned_) scan();
        if (beginPanel(ctx, "##load", Rect{{212, 120}, {812, 660}}, "Load Game")) {
            ImGui::BeginChild("##saves", ImVec2(0, -ctx.px(46)));
            if (saves_.empty()) ImGui::TextDisabled("No saved games in %s", savesDir().string().c_str());
            for (const auto& [name, path] : saves_)
                if (ImGui::Selectable(name.c_str())) {
                    auto session = ClassicSession::load(ctx.rules, path);
                    if (session) {
                        restoreHistoryFrom(path);
                        if (ctx.loadedFromIntro) ctx.loadedFromIntro();
                        ctx.startGame(std::move(*session));
                    } else {
                        error_ = session.error();
                    }
                }
            ImGui::EndChild();
            if (ImGui::Button("Cancel", ctx.size({140, 34}))) ctx.go(FrontId::Intro);
            if (!error_.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
            }
        }
        endPanel();
    }

private:
    void scan() {
        scanned_ = true;
        std::error_code ec;
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
        for (const auto& e : std::filesystem::directory_iterator(savesDir(), ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".gam") files.emplace_back(e.last_write_time(ec), e.path());
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [time, path] : files) saves_.emplace_back(path.stem().string(), path);
    }
    bool scanned_ = false;
    std::vector<std::pair<std::string, std::filesystem::path>> saves_;
    std::string error_;
};

} // namespace

Painter MenuContext::painter() const { return {art, fonts, map, fbScale, appSettings().graphics.textScale}; }

std::expected<std::unique_ptr<ClassicSession>, std::string> startLocalGame(std::shared_ptr<const game::Rules> rules, const game::GameSetup& setup) {
    auto state = game::createGame(*rules, setup);
    if (!state) return std::unexpected(state.error());
    game::EmpireId player;
    int humans = 0;
    for (const game::Empire& e : state->empires)
        if (e.kind == game::PlayerKind::Human) {
            if (!player.valid()) player = e.id;
            ++humans;
        }
    if (!player.valid()) return std::unexpected("A game needs at least one human player.");
    return std::make_unique<ClassicSession>(std::move(rules), std::move(*state), player, humans > 1 ? SessionKind::Hotseat : SessionKind::Local);
}

game::GameSetup quickStartSetup(const game::Rules& rules, std::string_view playerPreset, uint64_t seed, int opponents) {
    game::GameSetup setup;
    setup.seed = seed;  // every other setting keeps its default (a rolled Medium quadrant)
    game::EmpireSetup me;
    me.preset = std::string(playerPreset);
    me.kind = game::PlayerKind::Human;
    setup.empires.push_back(me);
    // Opponents: other non-neutral presets, picked deterministically from the seed.
    std::vector<std::string> pool;
    for (const auto& p : rules.racePresets())
        if (!p.neutral && !datafile::keysEqual(p.folder, playerPreset)) pool.push_back(p.folder);
    Rng rng(seed ^ 0x9e3779b97f4a7c15ull);
    rng.shuffle(pool);
    for (int i = 0; i < opponents && size_t(i) < pool.size(); ++i) {
        game::EmpireSetup e;
        e.preset = pool[size_t(i)];
        e.kind = game::PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    return setup;
}

std::unique_ptr<FrontScreen> makeFrontScreen(FrontId id) {
    switch (id) {
        case FrontId::Intro: return std::make_unique<IntroScreen>();
        case FrontId::QuickStart: return std::make_unique<QuickStartScreen>();
        case FrontId::GameSetup: return makeGameSetupScreen();
        case FrontId::LoadGame: return std::make_unique<LoadGameScreen>();
        case FrontId::Multiplayer: return makeMultiplayerScreen({});
        case FrontId::Pbem: return makePbemScreen();
        case FrontId::Settings: return std::make_unique<SettingsFrontScreen>();
        case FrontId::Learn: return makeLearnFrontScreen("tutorials");
        case FrontId::LearnTraining: return makeLearnFrontScreen("training");
        case FrontId::Manual: return makeLearnFrontScreen("manual:");
        case FrontId::Credits: return std::make_unique<CreditsScreen>();
    }
    return nullptr;
}

std::unique_ptr<FrontScreen> frontScreenByName(std::string_view name) {
    const size_t colon = name.find(':');
    const std::string_view screen = name.substr(0, colon);
    const std::string_view page = colon == std::string_view::npos ? std::string_view{} : name.substr(colon + 1);
    if (datafile::keysEqual(screen, "intro")) return makeFrontScreen(FrontId::Intro);
    if (datafile::keysEqual(screen, "settings")) return makeFrontScreen(FrontId::Settings);
    if (datafile::keysEqual(screen, "quickstart")) return makeFrontScreen(FrontId::QuickStart);
    if (datafile::keysEqual(screen, "setup") || datafile::keysEqual(screen, "newgame")) return makeGameSetupScreen(page);
    if (datafile::keysEqual(screen, "empiresetup")) return makeGameSetupScreen(std::string("empire:") + std::string(page));
    if (datafile::keysEqual(screen, "multiplayer")) return makeMultiplayerScreen(page);
    if (datafile::keysEqual(screen, "pbem")) return makePbemScreen(page);
    if (datafile::keysEqual(screen, "learn")) return makeLearnFrontScreen(page);
    if (datafile::keysEqual(screen, "credits")) return makeFrontScreen(FrontId::Credits);
    return nullptr;
}

} // namespace opense4::client::classic
