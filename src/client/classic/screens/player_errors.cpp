// Computer Player Errors (OpenSE4's own, docs/sdk/python-api.md "When a
// player fails"): the failures of this game's computer players of mods,
// newest first, each with the call it failed in, its error and its
// traceback, which can be selected and copied. The main window's notice opens
// it (Details); the game's log file (opense4.log) holds the same.

#include "client/classic/computer_players.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/session.hpp"
#include "client/script/items.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace opense4::client::classic {

namespace {

class PlayerErrorsScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::PlayerErrors), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const std::vector<sdk::PlayerFailure> all = playerFailures();
        d.beginContent();
        {
            const ReadingText reading(ui.painter());
            ImGui::BeginChild("##errors", ImVec2(0, 0));
            ImGui::PushTextWrapPos(0.0f);
            if (all.empty()) ImGui::TextDisabled("No computer player has failed in this game.");
            else
                ImGui::TextDisabled("When a computer player of a mod fails, the classic AI answers in its place. The newest first; the "
                                    "game's log file has them too: %s",
                                    (userDataDir() / "opense4.log").string().c_str());
            for (size_t i = all.size(); i-- > 0;) {
                const sdk::PlayerFailure& f = all[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(kLabelBlue, "%s", std::format("{}  {}, playing {}: {}", formatDate(f.turn), f.player, f.empireName, f.call).c_str());
                ImGui::TextUnformatted(f.error.c_str());
                script::reportItem("player-error:" + f.error, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                if (f.outForTurn) ImGui::TextDisabled("Its third failure of the turn: the classic AI played the rest of that turn.");
                if (!f.traceback.empty()) {
                    // Read-only, so that it can be selected and copied.
                    std::string text = f.traceback;
                    const auto lines = static_cast<float>(std::count(text.begin(), text.end(), '\n') + 1);
                    const float height = std::min(lines, 12.0f) * ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2 + 2;
                    ImGui::InputTextMultiline("##traceback", text.data(), text.size() + 1, ImVec2(-1, height), ImGuiInputTextFlags_ReadOnly);
                }
                ImGui::PopID();
            }
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }
        d.beginButtons();
        d.close();
        return d.keepOpen();
    }
};

} // namespace

std::unique_ptr<Screen> makePlayerErrors(const ScreenArgs&) { return std::make_unique<PlayerErrorsScreen>(); }

} // namespace opense4::client::classic
