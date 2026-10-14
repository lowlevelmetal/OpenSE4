// The text window of the original's scenarios, its tutorial among them
// (docs/spec/06 §1.7, §5.6): one page of the scenario's text file with its
// picture, browsed with Previous and Next through its series. The rules are
// classic_scenario.hpp's; the pages come from UiContext::scenarioText.

#include "client/audio.hpp"
#include "client/classic/classic_scenario.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace opense4::client::classic {

namespace {

class ScenarioTextScreen final : public Screen {
public:
    explicit ScenarioTextScreen(const ScreenArgs& args) : at_(args.index < 0 ? 0 : size_t(args.index)) {}

    bool draw(UiContext& ui) override {
        const ScenarioFiles* files = ui.scenarioText;
        if (!files || at_ >= files->pages.size()) return false;
        const ScenarioPage& page = files->pages[at_];
        // About 416x453 without a title bar, centred (confirmed: binary); the
        // places inside it beyond the picture's and the text's top are ours (inferred).
        const Vec2 size{416, 453};
        const Vec2 min{std::floor((frameW() - size.x) * 0.5f), std::floor((frameH() - size.y) * 0.5f)};
        auto at = [&](Vec2 p) { return ui.at(min + p); };
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        bool keep = true;
        const bool open = ImGui::Begin("Scenario Text", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                                     ImGuiWindowFlags_NoScrollbar | ui.windowFlags());
        ImGui::PopStyleVar(2);
        if (open) {
            if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
            ui.tagWindow(ui.at(min), ui.at(min + size));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            drawWindowFrame(ui.painter(), dl, Rect{min, min + size}, nullptr, 0);

            // The page's 128x128 picture at (10,10), from the scenario folder;
            // none when Image is empty or None. The title beside it, or across
            // the top without one.
            const Sprite picture = page.image.empty() ? Sprite{} : ui.art.image("Scenarios/" + page.image, false);
            if (picture) {
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(picture.tex.value)), at({10, 10}), at({138, 138}), {picture.uv.min.x, picture.uv.min.y},
                             {picture.uv.max.x, picture.uv.max.y});
                script::reportItem("scenario-picture", at({10, 10}), at({138, 138}));
            }
            const SeriesPlace place = placeInSeries(files->pages, at_);
            const std::string pageText = std::format("Page {} of {}", place.at, place.of);
            const float textTop = picture ? 160.0f : 36.0f;
            ImGui::PushFont(ui.fonts.regular, ui.fontPx(kTextSize));
            const float pageW = ImGui::CalcTextSize(pageText.c_str()).x;
            // "Page i of n" right-aligned just above the text.
            const ImVec2 pagePos{at({406, textTop - 18}).x - pageW, at({0, textTop - 18}).y};
            dl->AddText(pagePos, imColor(palette::kLabel), pageText.c_str());
            script::reportItem(std::format("scenario-page:{} of {}", place.at, place.of), pagePos,
                               ImVec2(pagePos.x + pageW, pagePos.y + ui.px(kTextCell)));
            script::reportItem(std::format("scenario-file-page:{}", at_ + 1), pagePos, ImVec2(pagePos.x + pageW, pagePos.y + ui.px(kTextCell)));
            ImGui::PopFont();
            ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
            if (picture) {
                ImGui::SetCursorScreenPos(at({148, 10}));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui.px(258));
                ImGui::TextUnformatted(page.title.c_str());
                ImGui::PopTextWrapPos();
            } else {
                drawFitted(ui.painter(), dl, ui.fonts.bold, kTitleSize, at({10, 10}), ui.px(396) - pageW - ui.px(8), IM_COL32_WHITE, page.title);
            }
            ImGui::PopFont();

            // The text: a scrolling list with one entry per paragraph, each
            // indented three spaces (confirmed: binary).
            ImGui::SetCursorScreenPos(at({10, textTop}));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({4, 4}));
            ImGui::BeginChild("##scenario-text", ui.size({396, 407 - textTop}), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            {
                const ReadingText reading(ui.painter());
                ImGui::PushTextWrapPos(0.0f);
                for (size_t i = 0; i < page.paragraphs.size(); ++i) {
                    if (i > 0) ImGui::Spacing();
                    const std::string paragraph = "   " + page.paragraphs[i];
                    ImGui::TextUnformatted(paragraph.c_str());
                }
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();

            // One row of three equal buttons: Previous, Close, Next.
            const std::optional<size_t> previous = previousPage(files->pages, at_);
            const std::optional<size_t> next = nextPage(files->pages, at_);
            auto button = [&](const char* label, float x, bool enabled) {
                ImGui::SetCursorScreenPos(at({x, 417}));
                const bool clicked = classicButton(ui, label, {126, 26}, 0, false, enabled);
                if (clicked) audio().play("button");
                return clicked && enabled;
            };
            if (button("Previous", 10, previous.has_value())) at_ = *previous;
            if (button("Close", 145, true)) keep = false;
            ui.tagItem("scenario-text:close");
            if (button("Next", 280, next.has_value())) at_ = *next;
            // Esc closes (spec 06 §3.4).
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
                keep = false;
        }
        ImGui::End();
        if (!keep) {
            // The page on screen is the one Ctrl+H and the T buttons show again.
            game::InterfaceOptions o = ui.options();
            o.scenarioPage = static_cast<uint16_t>(std::min<size_t>(at_ + 1, UINT16_MAX));
            ui.setOptions(o);
        }
        return keep;
    }

private:
    size_t at_;   // the page shown: its place in the file
};

} // namespace

std::unique_ptr<Screen> makeScenarioText(const ScreenArgs& args) { return std::make_unique<ScenarioTextScreen>(args); }

} // namespace opense4::client::classic
