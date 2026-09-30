#include "client/classic/classic_mode.hpp"

#include "client/classic/settings.hpp"

#include "core/log.hpp"

#include <imgui.h>

#include <format>

namespace opense4::client {

using namespace classic;

std::unique_ptr<ClassicMode> ClassicMode::create(const Platform& platform, const ClassicOptions& options, std::string& error) {
    const auto dataDir = ruleset::findInstalledDataDir(options.installDir);
    if (!dataDir) {
        error = options.installDir.empty()
                    ? "Classic mode needs an installed copy of the classic game, and none was found in your Steam libraries.\n"
                      "Pass --classic-dir=<game directory> to point at it (see docs/SETUP.md)."
                    : std::format("No classic data set found at {}.", options.installDir);
        return nullptr;
    }
    auto loaded = ruleset::loadRuleset(*dataDir);
    if (!loaded.ruleset || !loaded.diagnostics.errors.empty()) {
        error = std::format("The data set at {} has errors:\n", dataDir->string());
        for (size_t i = 0; i < std::min<size_t>(loaded.diagnostics.errors.size(), 15); ++i) error += "\n" + loaded.diagnostics.errors[i];
        return nullptr;
    }
    const std::filesystem::path gameRoot = dataDir->parent_path();

    std::unique_ptr<ClassicMode> mode(new ClassicMode(platform));
    mode->options_ = options;
    mode->rules_ = std::make_shared<const game::Rules>(std::move(*loaded.ruleset), gameRoot);
    mode->art_ = std::make_unique<Art>(*platform.device, assets::InstallFiles(gameRoot));
    log::info("Classic data set: {} ({} components, {} race presets)", dataDir->string(), mode->rules_->data().components.size(),
              mode->rules_->racePresets().size());
    applyClassicStyle();

    if (auto front = frontScreenByName(options.openWindow)) {
        mode->front_ = std::move(front);  // automation: --open=<front-end screen>
    } else if (options.skipIntro) {
        std::string race = options.race;
        if (race.empty())
            for (const auto& p : mode->rules_->racePresets())
                if (!p.neutral) {
                    race = p.folder;
                    break;
                }
        game::GameSetup setup = quickStartSetup(*mode->rules_, race, options.seed, std::max(0, options.empireCount - 1));
        if (options.systemCount > 0) setup.options.systemCount = options.systemCount;
        setup.options.quadrantType = options.quadrantType;
        auto session = startLocalGame(mode->rules_, setup);
        if (!session) {
            error = session.error();
            return nullptr;
        }
        mode->startGame(std::move(*session));
        // Automation: the computer plays every empire for a while.
        mode->session_->simulateTurns(options.autoTurns);
        if (!options.openWindow.empty()) {
            const auto id = screenFromName(options.openWindow);
            if (!id) {
                error = std::format("Unknown window '{}'", options.openWindow);
                return nullptr;
            }
            mode->openScreen(*id, {});
        }
    } else {
        mode->front_ = makeFrontScreen(FrontId::Intro);
    }
    return mode;
}

ClassicMode::~ClassicMode() {
    screens_.clear();
    ui_.reset();
    session_.reset();
    art_.reset();
}

void ClassicMode::startGame(std::unique_ptr<ClassicSession> session) {
    screens_.clear();
    session_ = std::move(session);
    ui_ = std::make_unique<UiContext>(*session_, *art_, *platform_.fonts);
    ui_->opener = [this](ScreenId id, ScreenArgs args) { pendingOpen_.emplace_back(id, std::move(args)); };
    session_->onNewTurn = [this] { openLogOnTurn_ = true; };
    main_ = MainWindow{};
    main_.reset(*ui_);
    front_.reset();
}

void ClassicMode::openScreen(ScreenId id, ScreenArgs args) {
    // A window that is already open comes to the front instead of opening twice
    // (windows with arguments are replaced so they show the new target).
    for (auto it = screens_.begin(); it != screens_.end(); ++it)
        if (it->first == id) {
            screens_.erase(it);
            break;
        }
    if (auto screen = makeScreen(id, args)) screens_.emplace_back(id, std::move(screen));
}

void ClassicMode::endTurn() {
    if (!session_ || session_->waitingForOthers()) return;
    screens_.clear();
    session_->endTurn();
}

bool ClassicMode::update(const FrameState& fs) {
    const float fw = float(fs.frame.width), fh = float(fs.frame.height);
    mapping_.scale = std::min(fw / kFrameW, fh / kFrameH);
    mapping_.offset = {(fw - kFrameW * mapping_.scale) * 0.5f, (fh - kFrameH * mapping_.scale) * 0.5f};

    if (!session_) {
        MenuContext ctx{rules_, *art_, *platform_.fonts, mapping_, fs.fbScale, fs.time, options_.seed, {}, {}, {}, frontError_};
        ctx.startGame = [this](std::unique_ptr<ClassicSession> s) { startGame(std::move(s)); };
        ctx.go = [this](FrontId id) { nextFront_ = id; };
        ctx.quit = [this] { quit_ = true; };
        if (front_) front_->draw(ctx);
        if (nextFront_ && !session_) {
            front_ = makeFrontScreen(*nextFront_);
            nextFront_.reset();
        }
        return !quit_;
    }

    UiContext& ui = *ui_;
    ui.map = mapping_;
    ui.fbScale = fs.fbScale;
    ui.time = fs.time;
    ui.dt = fs.dt;
    session_->poll();

    // Classic windows are modal: while one is open the main window takes no input.
    main_.update(ui, !screens_.empty());

    // Windows, oldest first; the newest draws on top.
    for (size_t i = 0; i < screens_.size();) {
        ImGui::PushID(int(i));
        const bool keep = screens_[i].second->draw(ui);
        ImGui::PopID();
        if (keep) ++i;
        else screens_.erase(screens_.begin() + std::ptrdiff_t(i));
    }
    for (auto& [id, args] : pendingOpen_) openScreen(id, std::move(args));
    pendingOpen_.clear();
    main_.applyRequests(ui);

    if (ui.requests.endTurn) {
        ui.requests.endTurn = false;
        if (settings().confirmEndTurn) confirmEndTurn_ = true;
        else endTurn();
    }
    if (confirmEndTurn_) {
        ImGui::SetNextWindowPos(ui.at({362, 330}));
        ImGui::SetNextWindowSize(ui.size({300, 110}));
        ImGui::PushFont(platform_.fonts->regular, 14.0f * ui.k());
        ImGui::Begin("End Turn", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextUnformatted("End the turn now?");
        if (ImGui::Button("End Turn", ui.size({120, 28})) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            confirmEndTurn_ = false;
            endTurn();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ui.size({120, 28})) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) confirmEndTurn_ = false;
        ImGui::End();
        ImGui::PopFont();
    }
    if (openLogOnTurn_) {
        openLogOnTurn_ = false;
        if (settings().showLogAtTurnStart && !ui.me().log.empty() && ui.me().log.back().turn + 1 >= ui.state().turn)
            openScreen(ScreenId::Log, {});
    }
    if (ui.requests.loadGame) {
        const std::filesystem::path file = *ui.requests.loadGame;
        ui.requests.loadGame.reset();
        auto loaded = ClassicSession::load(rules_, file);
        if (loaded) {
            startGame(std::move(*loaded));
            return true;
        }
        ScreenArgs args;
        args.text = loaded.error();
        openScreen(ScreenId::LoadGame, std::move(args));
    }
    if (ui.requests.quitToIntro) {
        ui.requests.quitToIntro = false;
        screens_.clear();
        ui_.reset();
        session_.reset();
        front_ = makeFrontScreen(FrontId::Intro);
        return true;
    }
    return !ui.requests.quitGame;
}

void ClassicMode::render(gfx::Renderer2D& r, const FrameState& fs) {
    const float fw = float(fs.frame.width), fh = float(fs.frame.height);
    const Vec2 topLeft = mapping_.fromFb({0, 0});
    const Vec2 bottomRight = mapping_.fromFb({fw, fh});
    r.begin(Mat4::ortho2D(topLeft.x, bottomRight.x, topLeft.y, bottomRight.y), fs.frame, mapping_.scale);
    if (session_ && ui_) main_.render(r, *ui_);
    else r.rect(Rect{{0, 0}, {kFrameW, kFrameH}}, Color::hex(0x000000));
    r.flush();
}

} // namespace opense4::client
