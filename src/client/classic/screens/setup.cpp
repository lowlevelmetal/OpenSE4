#include "client/classic/frontend.hpp"

namespace opense4::client::classic {

namespace {

class GameSetupPlaceholder final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        ImGui::SetNextWindowPos(ctx.at({312, 300}));
        ImGui::SetNextWindowSize(ctx.size({400, 140}));
        ImGui::Begin("New Game", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextDisabled("Game setup is not available yet. Use Quick Start.");
        if (ImGui::Button("Back")) ctx.go(FrontId::Intro);
        ImGui::End();
    }
};

} // namespace

std::unique_ptr<FrontScreen> makeGameSetupScreen() { return std::make_unique<GameSetupPlaceholder>(); }

} // namespace opense4::client::classic
