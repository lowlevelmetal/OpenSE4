#pragma once

// The original prototype game (our own simplified rules, src/sim). Kept
// playable until the classic-rules engine (src/game) replaces it.

#include "client/game_session.hpp"
#include "client/mode.hpp"
#include "client/settings_window.hpp"
#include "client/ui/hud.hpp"
#include "client/views/galaxy_view.hpp"
#include "client/views/starfield.hpp"
#include "client/views/system_view.hpp"

#include <memory>

namespace opense4::client {

struct PrototypeOptions {
    sim::GameSetup setup;
    std::filesystem::path dataDir;
    bool startInSystemView = false;
    int autoTurns = 0;
};

class PrototypeMode final : public Mode {
public:
    // Returns nullptr (after logging why) if the game data can't be loaded.
    static std::unique_ptr<PrototypeMode> create(const Platform& platform, const PrototypeOptions& options, std::string& error);

    bool update(const FrameState& fs) override;
    void render(gfx::Renderer2D& renderer, const FrameState& fs) override;

private:
    explicit PrototypeMode(const Platform& platform) : platform_(platform) {}
    bool startGame(sim::Content content, const sim::GameSetup& setup, std::string& error);
    void handle(const NavRequest& nav, bool& running);
    void openSystem(sim::SystemId id);
    void syncSelection();
    Vec2 viewport() const;

    Platform platform_;
    bool settingsOpen_ = false;
    SettingsPanelState settingsState_;
    std::unique_ptr<GameSession> session_;
    Selection selection_;
    GalaxyView galaxy_;
    SystemView system_;
    Starfield starfield_{0x5eedu};
    Hud hud_;
    enum class Screen { Galaxy, System } screen_ = Screen::Galaxy;
};

} // namespace opense4::client
