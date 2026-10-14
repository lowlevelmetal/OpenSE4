// Races On Planet (docs/spec/06 §1.4, §7 Q120, spec 03 §11, confirmed:
// binary): a right-click where an own colony's race pictures are on the
// Planet Report's Detail page, for any colony (another empire's too, though
// its page draws nothing there), lists the races living there, one row each:
// the race's picture, its name and its population. Read-only; Close (or Esc,
// Enter) closes it.

#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/script/items.hpp"

#include "game/query.hpp"

#include <array>
#include <format>

namespace opense4::client::classic {

namespace {

class RacesOnPlanetScreen final : public Screen {
public:
    explicit RacesOnPlanetScreen(const ScreenArgs& args) : planet_(args.planet) {}
    bool closesWithParent() const override { return true; }

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        const game::Colony* c = planet_.valid() ? s.colony(planet_) : nullptr;
        if (!c) return false;
        Dialog d(ui, screenTitle(ScreenId::RacesOnPlanet), DialogSize::Picker);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        ImGui::TextColored(kLabelBlue, "%s", s.galaxy.object(planet_).name.c_str());
        static constexpr std::array<ListColumn, 3> kColumns{{{kPicHeading, 40, 0, false}, {"Name", 0, 0, false}, {"Population", 110, 0, false}}};
        const float width = ImGui::GetContentRegionAvail().x;
        listHeader(ui, "##races-heading", kColumns, width);
        const std::vector<float> edges = columnEdges(ui, kColumns, width - ui.px(kListColumnW));
        beginList(ui.painter(), "##races", ImVec2(0, ImGui::GetContentRegionAvail().y - ui.px(4)));
        const float rowH = ui.px(kListRowH);
        for (const game::PopulationGroup& g : c->population) {
            if (g.millions <= 0) continue;
            const bool known = g.race.valid() && g.race.index() < s.empires.size();
            const std::string name = known ? s.empire(g.race).race.name : std::string("Unknown");
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(0, rowH));
            script::reportItem(name);   // input scripts: rows by race
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (const Sprite pic = ui.art.populationMini(known ? s.empire(g.race).race.style : "")) {
                const float side = ui.px(32);
                const ImVec2 p0{at.x + edges[0] + ui.px(2), at.y + (rowH - side) * 0.5f};
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(pic.tex.value)), p0, {p0.x + side, p0.y + side}, {pic.uv.min.x, pic.uv.min.y},
                             {pic.uv.max.x, pic.uv.max.y});
            }
            const float ty = at.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddText({at.x + edges[1] + ui.px(4), ty}, IM_COL32_WHITE, name.c_str());
            const std::string pop = std::format("{}M", formatNumber(g.millions));
            dl->AddText({at.x + edges[2] + ui.px(4), ty}, IM_COL32_WHITE, pop.c_str());
        }
        endList(ui.painter());
        d.beginButtons();
        if (d.close()) return false;
        return d.keepOpen();
    }

private:
    game::ObjectId planet_;
};

} // namespace

std::unique_ptr<Screen> makeRacesOnPlanet(const ScreenArgs& args) { return std::make_unique<RacesOnPlanetScreen>(args); }

} // namespace opense4::client::classic
