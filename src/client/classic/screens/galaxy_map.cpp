// Galaxy Map: the enlarged quadrant map opened by right-clicking the galaxy
// panel (docs/spec/06 §2.6), with overlays, Goto System, distances, names
// and per-system player notes.

#include "client/classic/map_style.hpp"
#include "client/classic/quadrant_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include <array>
#include <format>

namespace opense4::client::classic {

namespace {


std::string noteOf(const game::Empire& me, game::SystemId sys) {
    const auto& notes = me.knowledge.notes;
    return sys.index() < notes.size() ? notes[sys.index()] : std::string{};
}

class GalaxyMapScreen final : public Screen {
public:

    bool draw(UiContext& ui) override {
        Dialog d(ui, "Galaxy Map", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        bool keep = true;
        d.beginContent();

        // The window's map is 544×376 (§2.6); the hovered or edited system's notes go below it.
        QuadrantMapOptions opt;
        opt.overlay = overlay_;
        opt.names = names_;
        opt.distances = distances_;
        if (editing_) opt.highlight.push_back(*editing_);
        const QuadrantMapResult r = quadrantMap(ui, "##map", {544, 376}, opt);
        if (r.clicked) {
            editing_ = *r.clicked;
            draft_ = noteOf(me, *r.clicked);
            focusNote_ = true;
        }
        if (r.hovered) hovered_ = r.hovered;

        // Notes: the system being edited, else the hovered one.
        ImGui::BeginChild("##notes", ImVec2(ui.px(544), 0));
        if (editing_) {
            ImGui::TextColored(kLabelBlue, "Notes on %s", s.galaxy.system(*editing_).name.c_str());
            if (focusNote_) {
                ImGui::SetKeyboardFocusHere();
                focusNote_ = false;
            }
            const float bw = ui.px(70);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2 * (bw + ImGui::GetStyle().ItemSpacing.x));
            const bool enter = inputString("##note", draft_, 400, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Save", ImVec2(bw, 0)) || enter) {
                const game::CommandResult res = ui.session.issue(game::cmd::SetSystemNote{*editing_, draft_});
                error_ = res.ok ? std::string{} : res.error;
                if (res.ok) editing_.reset();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(bw, 0))) editing_.reset();
        } else if (hovered_) {
            const game::StarSystem& sys = s.galaxy.system(*hovered_);
            ImGui::TextColored(kLabelBlue, "%s", sys.name.c_str());
            ImGui::SameLine();
            const std::string note = noteOf(me, sys.id);
            if (note.empty()) dimText(me.hasExplored(sys.id) ? "No notes. Click the system to write some." : "Unexplored");
            else ImGui::TextUnformatted(note.c_str());
        } else {
            dimText("Point at a system to read its notes; click it to edit them.");
        }
        if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%s", error_.c_str());
        ImGui::EndChild();

        d.beginButtons();
        static constexpr std::array<std::pair<MapOverlay, const char*>, 6> kOverlays{{{MapOverlay::Presence, "Presence"},
                                                                                     {MapOverlay::Avoid, "Avoid"},
                                                                                     {MapOverlay::AllyClaimed, "Ally Claimed"},
                                                                                     {MapOverlay::EnemyClaimed, "Enemy Claimed"},
                                                                                     {MapOverlay::Spaceports, "Spaceports"},
                                                                                     {MapOverlay::ResupplyDepots, "Resupply Depots"}}};
        for (const auto& [overlay, label] : kOverlays)
            if (d.tab(label, overlay_ == overlay)) overlay_ = overlay;
        d.spacer();
        if (d.button("Goto System")) {
            filter_.clear();
            ImGui::OpenPopup("Goto System");
        }
        if (auto sys = gotoPopup(ui)) {
            ui.requests.showSystem = *sys;
            keep = false;
        }
        if (d.check("Show Distances", distances_)) distances_ = !distances_;
        if (d.check("Show Names", names_)) names_ = !names_;
        d.spacer();
        legend(ui);
        d.close();
        return keep && d.keepOpen();
    }

private:
    std::optional<game::SystemId> gotoPopup(UiContext& ui) {
        std::optional<game::SystemId> picked;
        ImGui::SetNextWindowSize(ui.size({300, 420}));
        if (!ImGui::BeginPopupModal("Goto System", nullptr, ImGuiWindowFlags_NoResize)) return picked;
        const game::Galaxy& g = ui.state().galaxy;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        inputString("##filter", filter_, 40);
        auto matches = [&](const std::string& name) {
            if (filter_.empty()) return true;
            auto lower = [](std::string x) {
                for (char& c : x) c = char(std::tolower(static_cast<unsigned char>(c)));
                return x;
            };
            return lower(name).find(lower(filter_)) != std::string::npos;
        };
        ImGui::BeginChild("##list", ImVec2(0, -ui.px(32)), ImGuiChildFlags_Borders);
        for (game::SystemId sys : exploredSystems(ui)) {
            const std::string& name = g.system(sys).name;
            if (!matches(name)) continue;
            ImGui::PushID(int(sys.index()));
            if (ImGui::Selectable(name.c_str())) {
                picked = sys;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return picked;
    }

    void legend(UiContext& ui) {
        // Our own key to the symbols (the original has none).
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ui.px(4);
        auto row = [&](auto&& symbol, const char* text) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            symbol(ImVec2{p.x + ui.px(8), p.y + h * 0.5f});
            ImGui::SetCursorScreenPos({p.x + ui.px(20), p.y});
            dimText(text);
        };
        auto ring = [&](ImU32 c) { return [&, c](ImVec2 at) { dl->AddCircle(at, r, c, 0, ui.px(1.5f)); }; };
        const ImU32 own = empireColor(ui.state(), ui.me().id);
        row(ring(imColor(map_style::kUnexplored)), "Unexplored");
        row(ring(imColor(map_style::kExplored)), "Explored");
        switch (overlay_) {
            case MapOverlay::Presence:
                row(ring(own), "Only us seen there");
                row([&](ImVec2 c) {
                    dl->AddTriangleFilled({c.x - r, c.y + r}, {c.x + r, c.y + r}, {c.x, c.y - r}, own);
                }, "Several empires");
                break;
            case MapOverlay::Avoid: row(ring(own), "Avoided"); break;
            case MapOverlay::AllyClaimed:
            case MapOverlay::EnemyClaimed:
                row(ring(own), "Claimed (the claimant's colour)");
                row([&](ImVec2 c) { dl->AddCircleFilled(c, r, imColor(map_style::kYellow)); }, "Several claimants");
                break;
            case MapOverlay::Spaceports:
            case MapOverlay::ResupplyDepots:
                row(ring(imColor(map_style::kGreen)), overlay_ == MapOverlay::Spaceports ? "Colony with a spaceport" : "Colony with a depot");
                row(ring(imColor(map_style::kYellow)), "Colony without");
                break;
        }
    }

    std::optional<game::SystemId> hovered_;
    std::optional<game::SystemId> editing_;
    std::string draft_;
    std::string filter_;
    std::string error_;
    bool focusNote_ = false;
    MapOverlay overlay_ = MapOverlay::Presence;
    bool distances_ = false;
    bool names_ = false;
};

} // namespace

std::unique_ptr<Screen> makeGalaxyMap(const ScreenArgs&) {
    // The window marks no current system (only the galaxy panel does, §2.6).
    return std::make_unique<GalaxyMapScreen>();
}

} // namespace opense4::client::classic
