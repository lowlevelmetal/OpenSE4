#include "client/classic/frontend.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/pointers.hpp"

#include "client/app_settings.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/file_dialog.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/screens/setup_widgets.hpp"
#include "client/script/items.hpp"
#include "client/classic/learn_content.hpp"
#include "client/classic/settings.hpp"
#include "client/settings_window.hpp"
#include "datafile/datafile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <string_view>

#ifndef OPENSE4_CLIENT_VERSION
#define OPENSE4_CLIENT_VERSION "0.0.0"
#endif

namespace opense4::client::classic {

// The intro picture stretched over the whole window, as the original shows it.
void introBackground(MenuContext& ctx) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a{0, 0}, b = ImGui::GetIO().DisplaySize;
    if (Sprite bg = ctx.art.introPicture())
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
        // at the left and the loading state at the right. OpenSE4's own entries
        // sit in a small row at the top right, apart from the original's layout.
        // Places are the 1024x768 frame's, kept at the same distance from the
        // frame's bottom in the 800x600 layout.
        const Painter p = ctx.painter();
        const float left = ctx.map.left, right = ctx.map.right;
        const float dy = frameH() - 768.0f;
        ImGui::GetBackgroundDrawList()->AddRectFilled(ctx.at({left, 695 + dy}), ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y),
                                                      IM_COL32_BLACK);
        ImGui::SetNextWindowPos(ctx.at({left, 672 + dy}));
        ImGui::SetNextWindowSize(ctx.size({right - left, 96}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("##intro", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoBackground)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::PushFont(ctx.fonts.small, p.fontPx(kSmallSize));
            dl->AddText(ctx.at({left + 10, 682 + dy}), IM_COL32(220, 220, 220, 255), "Version: OpenSE4 " OPENSE4_CLIENT_VERSION);
            // At its right the original's loading progress, done once the data
            // files have loaded (spec 07 session 5): "Loading:" and "Complete".
            dl->AddText(ctx.at({right - (1024 - 713), 682 + dy}), IM_COL32(220, 220, 220, 255), "Loading:");
            dl->AddText(ctx.at({right - (1024 - 814), 682 + dy}), IM_COL32(220, 220, 220, 255), "Complete");
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
            ImVec2 tutorialMin, tutorialMax;
            for (size_t i = 0; i < entries.size(); ++i) {
                ImGui::SetCursorScreenPos(ctx.at({left + 12 + float(i % kColumns) * (w + 5), 700 + dy + float(i / kColumns) * 30}));
                if (classicButton(p, entries[i].label, {w, 26}, 0, false, entries[i].go != nullptr) && entries[i].go) entries[i].go();
                if (std::string_view(entries[i].label) == "Tutorial") {
                    tutorialMin = ImGui::GetItemRectMin();
                    tutorialMax = ImGui::GetItemRectMax();
                }
            }
            if (!ctx.error.empty()) dl->AddText(ctx.at({left + 12, 660 + dy}), IM_COL32(255, 128, 100, 255), ctx.error.c_str());
            if (!error_.empty()) dl->AddText(ctx.at({left + 12, 644 + dy}), IM_COL32(255, 128, 100, 255), error_.c_str());
            // OpenSE4's own: until a first lesson is started, a hint points new
            // players at Tutorial (docs/LEARNING.md), which pulses.
            if (ctx.learn && !ctx.learn->library.tutorials.empty() && !lessonsStarted() && settings().learnDone.empty())
                tutorialHint(ctx, tutorialMin, tutorialMax, !ctx.error.empty() || !error_.empty());
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        // OpenSE4's own entries, which the original does not have: multiplayer,
        // its settings and the manual.
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
    static void tutorialHint(MenuContext& ctx, ImVec2 buttonMin, ImVec2 buttonMax, bool errorShown) {
        const Painter p = ctx.painter();
        const float pulse = 0.6f + 0.4f * std::sin(float(ctx.time) * 4.0f);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const float pad = ctx.px(2);
        fg->AddRect(ImVec2(buttonMin.x - pad, buttonMin.y - pad), ImVec2(buttonMax.x + pad, buttonMax.y + pad), imColor(0xffd040, pulse), 0.0f,
                    std::max(2.0f, ctx.px(2.5f)));
        // The note above the band of buttons, over the picture.
        constexpr const char* kText = "New to the game? Tutorial (outlined below) starts guided lessons\nthat teach it step by step, in about ten minutes each.";
        ImGui::PushFont(ctx.fonts.regular, p.fontPx(kTextSize));
        const ImVec2 text = ImGui::CalcTextSize(kText);
        const ImVec2 inner(ctx.px(8), ctx.px(5));
        const float bottom = buttonMin.y - ctx.px(errorShown ? 98.0f : 52.0f);   // clear of the version line and the error lines
        const ImVec2 a(buttonMin.x, bottom - text.y - 2 * inner.y), b(buttonMin.x + text.x + 2 * inner.x, bottom);
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(a, b, imColor(0x101c40, 0.92f));
        bg->AddRect(a, b, imColor(0xffd040, 0.9f), 0.0f, std::max(1.0f, ctx.px(1.5f)));
        bg->AddText(ImVec2(a.x + inner.x, a.y + inner.y), IM_COL32_WHITE, kText);
        ImGui::PopFont();
        // Input scripts see whether it shows (drawn over the picture, outside the band's window).
        ImGui::PushClipRect(a, b, false);
        script::reportItem("tutorial-hint", a, b);
        ImGui::PopClipRect();
    }

    void resume(MenuContext& ctx, const std::filesystem::path& file) {
        const BusyPointer busy;  // the Hourglass while it loads (spec 06 §5.8)
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

// Quick Start's picker in the setup frame (spec 07 session 5): "Select Empire"
// and a two-line hint, the races of Settings.txt's Quick Start Style list in
// pages of eight, two columns of four filled column by column, each a 128×128
// portrait with the empire's name and the race's description beside it;
// arrows at the frame's right turn a whole page; Begin Game and Cancel.
class QuickStartScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        if (styles_.empty() && ctx.rules) styles_ = setup::quickStartStyles(*ctx.rules);
        setup::SetupArea a(ctx, "##quick", "Select Empire", setup::Decoration::None);
        if (!a.open()) return;
        const auto& presets = ctx.rules->racePresets();
        a.heading({1, 43}, "Choose the empire you will lead");
        a.heading({1, 59}, "by clicking its portrait.");
        constexpr size_t kPerPage = 8;
        const size_t pages = std::max<size_t>(1, (styles_.size() + kPerPage - 1) / kPerPage);
        page_ = std::min(page_, pages - 1);
        for (size_t k = 0; k < kPerPage; ++k) {
            const size_t at = page_ * kPerPage + k;
            if (at >= styles_.size()) break;
            const size_t index = styles_[at];
            const ruleset::RacePreset& p = presets[index];
            // The left column first: styles 1-4, then 5-8.
            const Vec2 frame{k < 4 ? 230.0f : 506.0f, 14 + 132.0f * float(k % 4)};
            a.place(frame);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::InvisibleButton("##portrait", a.size({128, 128}))) chosen_ = static_cast<int>(index);
            const bool twice = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            ImGui::PopID();
            const std::string name = empireName(p);
            script::reportItem(name);   // input scripts find a race by its empire's name
            a.picture(ctx.art.racePortrait(p.folder), frame, frame + Vec2{128, 128});
            const bool selected = static_cast<int>(index) == chosen_;
            a.box(frame, frame + Vec2{127, 127}, selected ? 0xffdc5a : setup::kBoxRgb);
            const float nameH = a.textWrapped(frame + Vec2{134, 3}, name, 134, setup::kWhite);
            a.textWrapped(frame + Vec2{134, 5 + nameH}, p.description, 132, setup::kExplainRgb, setup::Face::Small);
            if (twice) {
                begin(ctx, index);
                return;
            }
        }
        if (a.arrow("##pageup", {766, 9}, true, page_ > 0)) --page_;
        if (a.arrow("##pagedown", {766, 516}, false, page_ + 1 < pages)) ++page_;
        if (!error_.empty()) a.status(error_, setup::kBad);
        // Begin Game is lit before a portrait is chosen (observed); without one it
        // only asks for a choice (ours).
        if (a.beginButton("Begin Game")) {
            if (chosen_ >= 0) {
                begin(ctx, static_cast<size_t>(chosen_));
                return;
            }
            error_ = "Click a portrait to choose the empire you will lead.";
        }
        if (a.cancelButton()) ctx.go(FrontId::Intro);
    }

private:
    // The empire's name as the race file writes it: Empire Name and Empire Type.
    static std::string empireName(const ruleset::RacePreset& p) {
        const std::string& name = p.empireName.empty() ? p.name : p.empireName;
        return p.empireType.empty() ? name : name + " " + p.empireType;
    }

    void begin(MenuContext& ctx, size_t preset) {
        auto setup = quickStartSetup(*ctx.rules, ctx.rules->racePresets()[preset].folder, ctx.seed);
        auto session = startLocalGame(ctx.rules, setup, quickStartExtras());
        if (!session) {
            error_ = session.error();
            return;
        }
        newGameStarted(setup.options.simultaneous);
        ctx.startGame(std::move(*session));
    }

    std::vector<size_t> styles_;
    size_t page_ = 0;
    int chosen_ = -1;
    std::string error_;
};

class SettingsFrontScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        // The same 780×475 classic window as the in-game Settings, centred on
        // the frame as every dialog is (spec 06 §2.1.1): at 800x600 too.
        const Painter p = ctx.painter();
        const Vec2 min{std::floor((frameW() - 780.0f) * 0.5f), std::floor((frameH() - 475.0f) * 0.5f)};
        const Rect r{min, min + Vec2{780, 475}};
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

// The original's Load Game dialog over the intro picture (screens/file_dialog.hpp),
// the same one the Game Menu's Load opens.
class LoadGameScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        const FileDialog::Result r = dialog_.draw(ctx.painter(), "##load");
        if (r == FileDialog::Result::Cancelled) {
            ctx.go(FrontId::Intro);
        } else if (r == FileDialog::Result::Chosen) {
            const std::filesystem::path path = dialog_.chosen().path;
            const BusyPointer busy;
            auto session = ClassicSession::load(ctx.rules, path);
            if (session) {
                restoreHistoryFrom(path);
                if (ctx.loadedFromIntro) ctx.loadedFromIntro();
                ctx.startGame(std::move(*session));
            } else {
                dialog_.setError(session.error());
            }
        }
    }

private:
    FileDialog dialog_{"Load Game", "Save Game Name", ".gam", loadGameDirectory(), savesDir()};
};

} // namespace

Painter MenuContext::painter() const { return {art, fonts, map, fbScale, appSettings().graphics.textScale}; }

std::expected<std::unique_ptr<ClassicSession>, std::string> startLocalGame(std::shared_ptr<const game::Rules> rules, const game::GameSetup& setup,
                                                                          const game::StartExtras& extras) {
    auto state = game::createGame(*rules, setup, extras);
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

game::GameSetup quickStartSetup(const game::Rules& rules, std::string_view playerPreset, uint64_t seed, std::optional<int> opponents) {
    return setup::quickStartGame(rules, playerPreset, seed, opponents);
}

game::StartExtras quickStartExtras() {
    game::StartExtras extras;
    extras.designMinisterRun.push_back(game::EmpireId{0u});  // the player, empire 0 of quickStartSetup
    return extras;
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
