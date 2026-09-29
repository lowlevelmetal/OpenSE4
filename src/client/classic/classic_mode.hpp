#pragma once

// The classic-rules client: the new engine (src/game) driven by a classic
// data set, presented in the classic main-window layout (a fixed 1024×768
// frame, scaled to fit). Art comes from the player's own installed copy.
//
// Milestone 1: quadrant generation and browsing (galaxy panel, system panel,
// object reports). See docs/PARITY_PLAN.md.

#include "assets/assets.hpp"
#include "client/mode.hpp"
#include "game/generate.hpp"

#include <map>
#include <memory>
#include <optional>

namespace opense4::client {

struct ClassicOptions {
    std::string installDir;  // empty = auto-detect
    uint64_t seed = 1;
    int systemCount = 0;     // 0 = 40
    int empireCount = 4;
    std::string quadrantType;
};

class ClassicMode final : public Mode {
public:
    static std::unique_ptr<ClassicMode> create(const Platform& platform, const ClassicOptions& options, std::string& error);
    ~ClassicMode() override;

    bool update(const FrameState& fs) override;
    void render(gfx::Renderer2D& renderer, const FrameState& fs) override;
    Color clearColor() const override { return Color::hex(0x000000); }

private:
    explicit ClassicMode(const Platform& platform) : platform_(platform) {}

    struct Empire {
        std::string name;
        Color color;
        game::ObjectId homeworld;
    };

    // Frame (1024×768) <-> framebuffer mapping, recomputed each frame.
    struct FrameMapping {
        float scale = 1.0f;
        Vec2 offset;
        Vec2 toFb(Vec2 p) const { return offset + p * scale; }
        Vec2 fromFb(Vec2 p) const { return (p - offset) / scale; }
    };

    gfx::TextureId loadTexture(const std::string& key, std::initializer_list<std::string_view> candidates, bool colorKey);
    gfx::TextureId systemBackground(const game::StarSystem& sys);
    gfx::TextureId portrait(const game::SpaceObject& obj);
    void drawFrame(gfx::Renderer2D& r);
    void drawSystemPanel(gfx::Renderer2D& r, double time);
    void drawGalaxyPanel(gfx::Renderer2D& r);
    void drawText(const FrameState& fs);
    void reportPanel(const FrameState& fs);
    void handleInput(const FrameState& fs);
    std::vector<game::ObjectId> objectsAt(game::Sector s) const;

    Platform platform_;
    ruleset::Ruleset rules_;
    assets::InstallFiles files_;
    game::Galaxy galaxy_;
    std::vector<Empire> empires_;
    std::vector<std::string> warnings_;

    game::SystemId shownSystem_;
    std::optional<game::Sector> selectedSector_;
    std::optional<game::ObjectId> selectedObject_;

    std::map<std::string, gfx::TextureId> textures_;
    gfx::TextureId planetSheet_;
    int sheetWidth_ = 0, sheetHeight_ = 0;
    FrameMapping mapping_;
};

} // namespace opense4::client
