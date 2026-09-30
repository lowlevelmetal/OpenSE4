#include "client/prototype_mode.hpp"

#include "client/app_settings.hpp"
#include "client/settings_window.hpp"

#include "client/palette.hpp"
#include "core/log.hpp"

#include <imgui.h>
#include <SDL3/SDL.h>

#include <format>

namespace opense4::client {

std::unique_ptr<PrototypeMode> PrototypeMode::create(const Platform& platform, const PrototypeOptions& options, std::string& error) {
    auto content = sim::loadContent(options.dataDir);
    if (!content) {
        error = "The game data could not be loaded:\n";
        for (const std::string& e : content.error()) error += "\n" + e;
        return nullptr;
    }
    std::unique_ptr<PrototypeMode> mode(new PrototypeMode(platform));
    mode->hud_.rendererInfo = platform.rendererInfo;
    sim::GameSetup setup = options.setup;
    setup.galaxy.sectorRadius = content->rules.sectorRadius;
    if (setup.galaxy.systemCount <= 0) setup.galaxy.systemCount = content->rules.defaultSystemCount;
    if (!mode->startGame(std::move(*content), setup, error)) return nullptr;

    if (options.autoTurns > 0) mode->session_->autoplay(options.autoTurns);
    if (options.startInSystemView) {
        const sim::Planet& home = mode->session_->state().planet(mode->session_->playerEmpire().homeworld);
        mode->openSystem(home.system);
        mode->selection_ = Selection::ofPlanet(home);
    }
    return mode;
}

Vec2 PrototypeMode::viewport() const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(platform_.window, &w, &h);
    return {static_cast<float>(w), static_cast<float>(h)};
}

bool PrototypeMode::startGame(sim::Content content, const sim::GameSetup& setup, std::string& error) {
    auto game = sim::createGame(content, setup);
    if (!game) {
        error = "Could not create the game: " + game.error();
        return false;
    }
    session_ = std::make_unique<GameSession>(std::move(content), std::move(*game));
    const sim::GameState& s = session_->state();
    galaxy_.fitToGalaxy(s, viewport());
    screen_ = Screen::Galaxy;
    const sim::Planet& home = s.planet(session_->playerEmpire().homeworld);
    selection_ = Selection::ofSystem(home.system);
    log::info("New game: seed {}, {} systems, {} empires, playing the {}", setup.galaxy.seed, s.systems.size(), s.empires.size(),
              session_->playerEmpire().name);
    return true;
}

void PrototypeMode::openSystem(sim::SystemId id) {
    if (screen_ != Screen::System || system_.system() != id) system_.open(session_->state(), id, viewport());
    screen_ = Screen::System;
    if (selection_.system != id) selection_ = Selection::ofSystem(id);
}

// Ships move and die between frames; keep the selection pointing at reality.
void PrototypeMode::syncSelection() {
    if (selection_.kind != Selection::Kind::Ship) return;
    const sim::Ship* ship = session_->state().findShip(selection_.ship);
    if (!ship || !session_->canSee(*ship)) selection_ = {};
    else selection_.system = ship->location.system;
}

void PrototypeMode::handle(const NavRequest& nav, bool& running) {
    if (nav.quit) running = false;
    if (nav.endTurn) {
        session_->endTurn();
        if (session_->newEventCount() > 0) hud_.showLog = true;
        session_->setStatus(std::format("Turn {} begins.", session_->state().turn));
    }
    if (nav.nextIdleShip) {
        // Cycle through idle ships in id order, starting after the current selection.
        const auto idle = session_->idleShips();
        if (!idle.empty()) {
            const sim::ShipId current = selection_.kind == Selection::Kind::Ship ? selection_.ship : sim::ShipId{};
            auto it = std::find_if(idle.begin(), idle.end(), [&](const sim::Ship* s) { return !current.valid() || s->id > current; });
            const sim::Ship* next = it != idle.end() ? *it : idle.front();
            openSystem(next->location.system);
            selection_ = Selection::ofShip(*next);
            system_.centerOn(next->location.sector);
        } else {
            session_->setStatus("All ships have orders.");
        }
    }
    if (nav.openSystem) openSystem(*nav.openSystem);
    if (nav.focus) {
        openSystem(nav.focus->system);
        system_.centerOn(nav.focus->sector);
    }
    if (nav.toGalaxy && screen_ == Screen::System) {
        screen_ = Screen::Galaxy;
        galaxy_.focus(session_->state().system(system_.system()).position);
    }
    if (nav.newGame) {
        sim::Content content = session_->content();
        sim::GameSetup setup = *nav.newGame;
        setup.galaxy.sectorRadius = content.rules.sectorRadius;
        std::string error;
        if (!startGame(std::move(content), setup, error)) session_->setStatus(error);
    }
}

bool PrototypeMode::update(const FrameState& fs) {
    const ImGuiIO& io = ImGui::GetIO();
    syncSelection();
    ViewContext ctx{*session_, selection_, *platform_.fonts, fs.frame, fs.fbScale, fs.uiScale, fs.time, fs.dt};

    NavRequest nav;
    hud_.fps = io.Framerate;
    hud_.draw(ctx, nav, screen_ == Screen::System);

    if (!io.WantCaptureKeyboard && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        if (!io.KeyAlt && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) nav.endTurn = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (screen_ == Screen::System) nav.toGalaxy = true;
            else selection_ = {};
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) nav.toGalaxy = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) nav.nextIdleShip = true;
    }

    // Settings (Ctrl+, by default): graphics and controls.
    if (appSettings().controls.bindings.pressed(Action::Settings)) settingsOpen_ = !settingsOpen_;
    if (settingsOpen_ && platform_.app) {
        ImGui::SetNextWindowSize(ImVec2(720 * fs.uiScale, 560 * fs.uiScale), ImGuiCond_Appearing);
        if (ImGui::Begin("Settings", &settingsOpen_, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::BeginTabBar("##settings")) {
                if (ImGui::BeginTabItem("Graphics")) {
                    graphicsSettingsPage(settingsState_, *platform_.app, fs.uiScale);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Controls")) {
                    controlsSettingsPage(settingsState_, fs.uiScale);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    if (screen_ == Screen::Galaxy) galaxy_.update(ctx, nav);
    else system_.update(ctx, nav);
    bool running = true;
    handle(nav, running);

    // A new game replaces the session; rebuild the context before drawing labels.
    ViewContext drawCtx{*session_, selection_, *platform_.fonts, fs.frame, fs.fbScale, fs.uiScale, fs.time, fs.dt};
    if (screen_ == Screen::Galaxy) galaxy_.drawLabels(drawCtx);
    else system_.drawLabels(drawCtx);
    return running;
}

void PrototypeMode::render(gfx::Renderer2D& renderer, const FrameState& fs) {
    ViewContext ctx{*session_, selection_, *platform_.fonts, fs.frame, fs.fbScale, fs.uiScale, fs.time, fs.dt};
    const Camera2D& cam = screen_ == Screen::Galaxy ? galaxy_.camera : system_.camera;
    starfield_.draw(renderer, fs.frame, cam.center, cam.zoom, fs.time);
    if (screen_ == Screen::Galaxy) galaxy_.draw(ctx, renderer);
    else system_.draw(ctx, renderer);
}

} // namespace opense4::client
