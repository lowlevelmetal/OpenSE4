// Convert Resources (Ctrl+V, docs/spec/02 §5.6, spec 06 §1.3): on the left the
// resources to convert from, with the target buttons (Minerals when the window
// opens) and the step buttons below them; on the right the conversions, each
// with what it would yield at the colony's loss. OK gives each line as colony
// orders of at most 65,000; in a turn-based game the colony's list runs at
// once, in a simultaneous one on day 1 of the movement phase. Cancel gives none.

#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/economy.hpp"

#include <array>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};

size_t indexOf(game::Resource r) { return static_cast<size_t>(r); }

class ConvertResourcesScreen final : public Screen {
public:
    explicit ConvertResourcesScreen(const ScreenArgs& args) : planet_(args.planet) {}

    bool draw(UiContext& ui) override {
        // Opened with nothing (automation): the homeworld.
        if (!planet_.valid())
            if (auto home = homeworld(ui)) planet_ = *home;
        Dialog d(ui, screenTitle(ScreenId::ConvertResources), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const bool usable = canConvertAt(ui.rules(), s, ui.session.player(), planet_);
        d.beginContent();
        if (!usable) {
            ImGui::TextColored(kDim, "Select one of your colonies that can convert resources first.");
        } else {
            const game::Colony& colony = *s.colony(planet_);
            const int64_t loss = game::economy::conversionLoss(ui.rules(), colony);
            ImGui::TextColored(kDim, "%s", s.galaxy.object(planet_).name.c_str());
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
            ImGui::BeginGroup();
            sources(ui, ImVec2(w, h - ui.px(4 * 31)));
            controls(ui, w / ui.px(1.0f));
            ImGui::EndGroup();
            ImGui::SameLine();
            conversions(ui, loss, ImVec2(ImGui::GetContentRegionAvail().x, h));
            status_.draw(ui);
        }
        d.beginButtons();
        if (d.button("OK", usable && !window_.lines.empty()) && give(ui)) return false;
        if (d.close(true, "Cancel")) return false;
        return d.keepOpen();
    }

private:
    void sources(UiContext& ui, ImVec2 size) {
        beginPanel(ui, "##from", "Convert From Resource", size);
        for (game::Resource r : game::kResources)
            if (row(ui, static_cast<int>(indexOf(r)), ui.art.icon16(kIcons[indexOf(r)]), game::displayName(r), {}).left) window_.add(r);
        endPanel(ui, "Click a resource to convert from it.");
    }

    // The target buttons (exactly one down), then the step buttons; `width`
    // in frame pixels.
    void controls(UiContext& ui, float width) {
        for (game::Resource r : game::kResources)
            if (classicButton(ui, std::string(game::displayName(r)).c_str(), {width, 24}, 1, window_.target == r)) window_.target = r;
        const float half = width * 0.5f - 4.0f;
        if (classicButton(ui, "x 10000", {half, 24}, 2, window_.step == 10'000)) window_.press(10'000);
        ImGui::SameLine();
        if (classicButton(ui, "x 100000", {half, 24}, 2, window_.step == 100'000)) window_.press(100'000);
    }

    void conversions(UiContext& ui, int64_t loss, ImVec2 size) {
        beginPanel(ui, "##lines", std::format("Conversions (at {}% Loss of resources)", loss), size);
        for (size_t i = 0; i < window_.lines.size(); ++i) {
            const ConversionLine& l = window_.lines[i];
            const std::string title = std::format("{} {}", formatNumber(l.amount), game::displayName(l.from));
            const std::string yield = std::format("into {} {}", formatNumber(game::economy::conversionGain(l.amount, loss)), game::displayName(l.to));
            if (row(ui, static_cast<int>(i), ui.art.icon16(kIcons[indexOf(l.to)]), title, yield).left) {
                window_.remove(i);
                break;
            }
        }
        if (window_.lines.empty()) ImGui::TextColored(imColorV(kColors[indexOf(window_.target)]), "Nothing to convert yet.");
        endPanel(ui, "Click a conversion to take a step off it.");
    }

    // The lines as colony orders, appended to the colony's list.
    bool give(UiContext& ui) {
        const game::Colony* c = ui.state().colony(planet_);
        if (!c) return false;
        game::cmd::SetOrders cmd;
        cmd.planet = planet_;
        cmd.orders = c->orders;
        cmd.repeat = c->repeatOrders;
        const auto added = conversionOrders(window_.lines);
        cmd.orders.insert(cmd.orders.end(), added.begin(), added.end());
        return status_.issue(ui, cmd);
    }

    game::ObjectId planet_;
    ConversionWindow window_;
    Status status_;
};

} // namespace

std::unique_ptr<Screen> makeConvertResources(const ScreenArgs& args) { return std::make_unique<ConvertResourcesScreen>(args); }

} // namespace opense4::client::classic
