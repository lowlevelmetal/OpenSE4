#include "client/classic/frontend.hpp"

#include "datafile/datafile.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

void background(MenuContext& ctx) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a = ctx.at({0, 0}), b = ctx.at({kFrameW, kFrameH});
    if (Sprite bg = ctx.art.imageAny({"Pictures/Game/Screens/1024X768/Intro.bmp", "Pictures/Game/Screens/800X600/Intro.bmp"}, false))
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(bg.tex.value)), a, b, ImVec2(bg.uv.min.x, bg.uv.min.y), ImVec2(bg.uv.max.x, bg.uv.max.y));
    else dl->AddRectFilled(a, b, IM_COL32(2, 4, 12, 255));
}

bool beginPanel(MenuContext& ctx, const char* id, Rect r) {
    ImGui::SetNextWindowPos(ctx.at(r.min));
    ImGui::SetNextWindowSize(ctx.size(r.size()));
    ImGui::PushFont(ctx.fonts.medium, 15.0f * ctx.k());
    return ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
}

void endPanel() {
    ImGui::End();
    ImGui::PopFont();
}

class IntroScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        if (beginPanel(ctx, "##intro", Rect{{212, 560}, {812, 700}})) {
            const ImVec2 bs = ctx.size({138, 34});
            if (ImGui::Button("Quick Start", bs)) ctx.go(FrontId::QuickStart);
            ImGui::SameLine();
            if (ImGui::Button("New Game", bs)) ctx.go(FrontId::GameSetup);
            ImGui::SameLine();
            if (ImGui::Button("Load Game", bs)) ctx.go(FrontId::LoadGame);
            ImGui::SameLine();
            if (ImGui::Button("Multiplayer", bs)) ctx.go(FrontId::Multiplayer);
            if (ImGui::Button("Quit Game", bs)) ctx.quit();
            ImGui::SameLine();
            ImGui::TextDisabled("OpenSE4 classic engine - data: %s", ctx.rules->data().dataDir.parent_path().filename().string().c_str());
            if (!ctx.error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", ctx.error.c_str());
        }
        endPanel();
    }
};

class QuickStartScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        const auto& presets = ctx.rules->racePresets();
        if (beginPanel(ctx, "##quick", Rect{{112, 90}, {912, 690}})) {
            ImGui::TextUnformatted("Pick Empire");
            ImGui::Separator();
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

class LoadGameScreen final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        background(ctx);
        if (!scanned_) scan();
        if (beginPanel(ctx, "##load", Rect{{212, 120}, {812, 660}})) {
            ImGui::TextUnformatted("Load Game");
            ImGui::Separator();
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
        case FrontId::Multiplayer: return makeMultiplayerScreen();
    }
    return nullptr;
}

} // namespace opense4::client::classic
