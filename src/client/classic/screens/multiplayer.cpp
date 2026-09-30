#include "client/classic/frontend.hpp"

namespace opense4::client::classic {

namespace {

class MultiplayerPlaceholder final : public FrontScreen {
public:
    void draw(MenuContext& ctx) override {
        ImGui::SetNextWindowPos(ctx.at({312, 300}));
        ImGui::SetNextWindowSize(ctx.size({400, 140}));
        ImGui::Begin("Multiplayer", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextDisabled("Multiplayer is not available yet.");
        if (ImGui::Button("Back")) ctx.go(FrontId::Intro);
        ImGui::End();
    }
};

} // namespace

std::unique_ptr<FrontScreen> makeMultiplayerScreen() { return std::make_unique<MultiplayerPlaceholder>(); }

} // namespace opense4::client::classic
