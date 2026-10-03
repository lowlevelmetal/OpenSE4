// Galaxy Map: the enlarged quadrant map opened by right-clicking the galaxy
// panel (docs/spec/06 §2.6), with overlays, Goto System, distances, names
// and per-system player notes.

#include "client/classic/map_style.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/quadrant_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include <array>
#include <format>
#include <optional>

namespace opense4::client::classic {

namespace {


std::string noteOf(const game::Empire& me, game::SystemId sys) {
    const auto& notes = me.knowledge.notes;
    return sys.index() < notes.size() ? notes[sys.index()] : std::string{};
}

class GalaxyMapScreen final : public Screen {
public:

    bool draw(UiContext& ui) override {
        // The original's frame (spec 07 session 5): no box around the content
        // area; the map in a #647EC7 frame (22,43)-(565,418) of the window, its
        // grid every 8 px; under it, in small label-blue type right-aligned to
        // x 565, the hint. Ours: the notes of the hovered or edited system on
        // the line under the hint.
        Dialog d(ui, "Galaxy Map", DialogSize::Large, 190.0f, false);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        bool keep = true;
        d.beginContent(576);

        QuadrantMapOptions opt;
        opt.overlay = overlay_;
        opt.names = names_;
        opt.distances = distances_;
        opt.frameColor = palette::kFrameLight;
        if (editing_) opt.highlight.push_back(*editing_);
        ImGui::SetCursorScreenPos(d.at({22, 43}));
        const QuadrantMapResult r = quadrantMap(ui, "##map", {544, 376}, opt);
        ui.tag("galaxy-map:map", d.at({22, 43}), d.at({566, 419}));   // for lessons
        if (r.clicked) {
            editing_ = *r.clicked;
            draft_ = noteOf(me, *r.clicked);
            focusNote_ = true;
        }
        if (r.hovered) hovered_ = r.hovered;
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {565, 427}, imColor(palette::kLabel), "(click on a system to set its player notes)");

        // Notes: the system being edited, else the hovered one (ours).
        ImGui::SetCursorScreenPos(d.at({22, 441}));
        ImGui::BeginChild("##notes", ui.size({544, 22}), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        if (editing_) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kLabelBlue, "%s", s.galaxy.system(*editing_).name.c_str());
            ImGui::SameLine();
            if (focusNote_) {
                ImGui::SetKeyboardFocusHere();
                focusNote_ = false;
            }
            const float bw = ui.px(60);
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
        } else if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%s", error_.c_str());
        } else if (hovered_) {
            const game::StarSystem& sys = s.galaxy.system(*hovered_);
            const std::string note = noteOf(me, sys.id);
            if (me.hasExplored(sys.id)) {
                ImGui::TextColored(kLabelBlue, "%s", sys.name.c_str());
                ImGui::SameLine();
            }
            if (note.empty()) dimText(me.hasExplored(sys.id) ? "No notes." : "Unexplored");
            else ImGui::TextUnformatted(note.c_str());
        }
        ImGui::EndChild();

        // The original's column: Presence (chosen), Avoid, Ally Claimed, Enemy
        // Claimed, Spaceports, Resupply Depots, four gaps, Goto System (slot
        // 11), a gap, Show Names (slot 13), Close. Ours in the gaps: Show
        // Distances (slot 7) and a legend (slots 8 to 10).
        d.beginButtons();
        static constexpr std::array<std::pair<MapOverlay, const char*>, 6> kOverlays{{{MapOverlay::Presence, "Presence"},
                                                                                     {MapOverlay::Avoid, "Avoid"},
                                                                                     {MapOverlay::AllyClaimed, "Ally Claimed"},
                                                                                     {MapOverlay::EnemyClaimed, "Enemy Claimed"},
                                                                                     {MapOverlay::Spaceports, "Spaceports"},
                                                                                     {MapOverlay::ResupplyDepots, "Resupply Depots"}}};
        std::optional<ImVec2> overlaysMin;
        for (const auto& [overlay, label] : kOverlays) {
            if (d.tab(label, overlay_ == overlay)) overlay_ = overlay;
            if (!overlaysMin) overlaysMin = ImGui::GetItemRectMin();
        }
        if (d.check("Show Distances", distances_)) distances_ = !distances_;
        ui.tag("galaxy-map:overlays", *overlaysMin, ImGui::GetItemRectMax());   // for lessons: what the map shows
        legend(ui, d.skipSlots(3));
        if (d.button("Goto System")) {
            filter_.clear();
            ImGui::OpenPopup("Goto System");
        }
        if (auto sys = gotoPopup(ui)) {
            ui.requests.showSystem = *sys;
            keep = false;
        }
        d.spacer();
        if (d.check("Show Names", names_)) names_ = !names_;
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
        beginList(ui, "##list", ImVec2(0, -ui.px(32)), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
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
        endList(ui);
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return picked;
    }

    // Our own key to the symbols (the original has none), in three empty
    // slots of the button column from `at`.
    void legend(UiContext& ui, ImVec2 at) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ui.px(4);
        float y = at.y + ui.px(4);
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        auto row = [&](auto&& symbol, const char* text) {
            const float h = ui.px(15);
            symbol(ImVec2{at.x + ui.px(10), y + h * 0.5f});
            dl->AddText({at.x + ui.px(22), y + ui.px(2 + kSmallLead)}, imColor(palette::kSecondary), text);
            y += h;
        };
        auto ring = [&](ImU32 c) { return [&, c](ImVec2 p) { dl->AddCircle(p, r, c, 0, ui.px(1.5f)); }; };
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
        ImGui::PopFont();
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
