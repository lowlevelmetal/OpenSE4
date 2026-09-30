#include "client/classic/frontend.hpp"

#include "client/app_settings.hpp"
#include "client/classic/screens/screens.hpp"
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

namespace {

// The intro picture stretched over the whole window, as the original shows it.
void background(MenuContext& ctx) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a{0, 0}, b = ImGui::GetIO().DisplaySize;
    if (Sprite bg = ctx.art.imageAny({"Pictures/Game/Screens/1024X768/Intro.bmp", "Pictures/Game/Screens/800X600/Intro.bmp"}, false))
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(bg.tex.value)), a, b, ImVec2(bg.uv.min.x, bg.uv.min.y), ImVec2(bg.uv.max.x, bg.uv.max.y));
    else dl->AddRectFilled(a, b, IM_COL32(2, 4, 12, 255));
}

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
        // A black band along the bottom with two rows of four buttons across the
        // full width, the version at the left and the loading state at the right.
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
            const std::array<Entry, 8> entries{{
                {"Quick Start", [&] { ctx.go(FrontId::QuickStart); }},
                {"New Game", [&] { ctx.go(FrontId::GameSetup); }},
                {"Resume Game", nullptr},
                {"Load Game", [&] { ctx.go(FrontId::LoadGame); }},
                {"Multiplayer", [&] { ctx.go(FrontId::Multiplayer); }},
                {"Scenario", nullptr},
                {"Settings", [&] { ctx.go(FrontId::Settings); }},
                {"Quit Game", [&] { ctx.quit(); }},
            }};
            const float w = (right - left - 24 - 15) / 4;
            for (size_t i = 0; i < entries.size(); ++i) {
                ImGui::SetCursorScreenPos(ctx.at({left + 12 + float(i % 4) * (w + 5), 700 + float(i / 4) * 30}));
                if (classicButton(p, entries[i].label, {w, 26}, 0, false, entries[i].go != nullptr) && entries[i].go) entries[i].go();
            }
            if (!ctx.error.empty()) dl->AddText(ctx.at({left + 12, 660}), IM_COL32(255, 128, 100, 255), ctx.error.c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
};

class QuickStartScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        const auto& presets = ctx.rules->racePresets();
        if (beginPanel(ctx, "##quick", Rect{{112, 90}, {912, 690}}, "Pick Empire")) {
            ImGui::BeginChild("##races", ImVec2(0, -ctx.px(46)));
            int col = 0;
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].neutral) continue;
                ImGui::PushID(int(i));
                ImGui::BeginGroup();
                const Sprite portrait = ctx.art.racePortrait(presets[i].folder);
                const bool selected = int(i) == chosen_;
                if (portrait) {
                    if (ImGui::ImageButton("##p", ImTextureRef(static_cast<ImTextureID>(portrait.tex.value)), ctx.size({112, 112}),
                                           ImVec2(portrait.uv.min.x, portrait.uv.min.y), ImVec2(portrait.uv.max.x, portrait.uv.max.y),
                                           selected ? ImVec4(0.3f, 0.5f, 1.0f, 0.6f) : ImVec4(0, 0, 0, 0)))
                        chosen_ = int(i);
                } else if (ImGui::Button(presets[i].name.c_str(), ctx.size({112, 112}))) {
                    chosen_ = int(i);
                }
                ImGui::TextColored(selected ? ImVec4(1, 1, 0.6f, 1) : ImVec4(0.8f, 0.85f, 0.95f, 1), "%s", presets[i].name.c_str());
                ImGui::EndGroup();
                ImGui::PopID();
                if (++col % 6 != 0) ImGui::SameLine();
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(chosen_ < 0);
            if (ImGui::Button("Begin Game", ctx.size({140, 34}))) {
                auto setup = quickStartSetup(*ctx.rules, presets[size_t(chosen_)].folder, ctx.seed);
                auto session = startLocalGame(ctx.rules, setup);
                if (session) ctx.startGame(std::move(*session));
                else error_ = session.error();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ctx.size({140, 34}))) ctx.go(FrontId::Intro);
            if (!error_.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
            }
        }
        endPanel();
    }

private:
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
                    if (session) ctx.startGame(std::move(*session));
                    else error_ = session.error();
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
    setup.seed = seed;
    setup.options.systemCount = 40;
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
        case FrontId::Settings: return std::make_unique<SettingsFrontScreen>();
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
    return nullptr;
}

} // namespace opense4::client::classic
