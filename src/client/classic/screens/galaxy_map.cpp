// Galaxy Map: the enlarged quadrant map opened by right-clicking the galaxy
// panel (docs/spec/06 §2.6), with overlays, Goto System, distances, names
// and per-system player notes.

#include "client/classic/quadrant_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include <array>
#include <format>

namespace opense4::client::classic {

namespace {

const ImVec4 kLabelBlue{0.44f, 0.61f, 1.0f, 1.0f};

std::string noteOf(const game::Empire& me, game::SystemId sys) {
    const auto& notes = me.knowledge.notes;
    return sys.index() < notes.size() ? notes[sys.index()] : std::string{};
}

class GalaxyMapScreen final : public Screen {
public:
    explicit GalaxyMapScreen(std::optional<game::SystemId> current) : current_(current) {}

    bool draw(UiContext& ui) override {
        Dialog d(ui, "Galaxy Map", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        bool keep = true;
        d.beginContent();

        QuadrantMapOptions opt;
        opt.overlay = overlay_;
        opt.names = names_;
        opt.distances = distances_;
        opt.current = current_;
        if (editing_) opt.highlight.push_back(*editing_);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float noteHeight = 98;
        const QuadrantMapResult r = quadrantMap(ui, "##map", {avail.x / ui.k(), avail.y / ui.k() - noteHeight}, opt);
        if (r.clicked) {
            editing_ = *r.clicked;
            draft_ = noteOf(me, *r.clicked);
            focusNote_ = true;
        }
        if (r.hovered) hovered_ = r.hovered;

        // Notes: the system being edited, else the hovered one.
        ImGui::BeginChild("##notes", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (editing_) {
            ImGui::TextColored(kLabelBlue, "Notes on %s", s.galaxy.system(*editing_).name.c_str());
            if (focusNote_) {
                ImGui::SetKeyboardFocusHere();
                focusNote_ = false;
            }
            const float bw = ui.px(90);
            inputMultiline("##note", draft_, ImVec2(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x, ui.px(44)));
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::Button("Save Note", ImVec2(bw, ui.px(20)))) {
                const game::CommandResult res = ui.session.issue(game::cmd::SetSystemNote{*editing_, draft_});
                error_ = res.ok ? std::string{} : res.error;
                if (res.ok) editing_.reset();
            }
            if (ImGui::Button("Cancel", ImVec2(bw, ui.px(20)))) editing_.reset();
            ImGui::EndGroup();
        } else if (hovered_) {
            const game::StarSystem& sys = s.galaxy.system(*hovered_);
            ImGui::TextColored(kLabelBlue, "%s", sys.name.c_str());
            ImGui::SameLine();
            dimText(me.hasExplored(sys.id) ? std::format("at {}, {}", sys.position.x, sys.position.y).c_str() : "unexplored");
            const std::string note = noteOf(me, sys.id);
            if (note.empty()) dimText("No notes. Click the system to write some.");
            else ImGui::TextWrapped("%s", note.c_str());
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
            if (d.button(label, true, overlay_ == overlay)) overlay_ = overlay;
        d.spacer();
        if (d.button("Goto System")) {
            filter_.clear();
            ImGui::OpenPopup("Goto System");
        }
        if (auto sys = gotoPopup(ui)) {
            ui.requests.showSystem = *sys;
            keep = false;
        }
        if (d.button("Show Distances", true, distances_)) distances_ = !distances_;
        if (d.button("Show Names", true, names_)) names_ = !names_;
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
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ui.px(4);
        auto row = [&](auto&& symbol, const char* text) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            symbol(ImVec2{p.x + ui.px(8), p.y + h * 0.5f});
            ImGui::SetCursorScreenPos({p.x + ui.px(20), p.y});
            dimText(text);
        };
        const float t = ui.px(1.5f);
        switch (overlay_) {
            case MapOverlay::Presence: {
                const ImU32 own = empireColor(ui.state(), ui.me().id);
                row([&](ImVec2 c) { dl->AddCircle(c, r, IM_COL32(80, 88, 102, 255), 0, t); }, "Unexplored");
                row([&](ImVec2 c) { dl->AddCircle(c, r, IM_COL32(200, 208, 220, 255), 0, t); }, "Explored");
                row([&](ImVec2 c) { dl->AddCircle(c, r, own, 0, t); }, "Only us present");
                row([&](ImVec2 c) {
                    dl->AddTriangleFilled({c.x, c.y - r * 1.25f}, {c.x - r * 1.1f, c.y + r * 0.85f}, {c.x + r * 1.1f, c.y + r * 0.85f}, IM_COL32_WHITE);
                }, "Several empires");
                break;
            }
            case MapOverlay::Avoid: row([&](ImVec2 c) { dl->AddCircleFilled(c, r, IM_COL32(255, 208, 64, 255)); }, "Avoided"); break;
            case MapOverlay::AllyClaimed:
            case MapOverlay::EnemyClaimed:
                row([&](ImVec2 c) { dl->AddCircleFilled(c, r, IM_COL32(200, 208, 220, 255)); }, "Claimed (claimant's colour)");
                break;
            case MapOverlay::Spaceports:
            case MapOverlay::ResupplyDepots:
                row([&](ImVec2 c) { dl->AddCircleFilled(c, r, IM_COL32(80, 220, 90, 255)); },
                    overlay_ == MapOverlay::Spaceports ? "Colony with a spaceport" : "Colony with a depot");
                row([&](ImVec2 c) { dl->AddCircleFilled(c, r, IM_COL32(255, 208, 64, 255)); }, "Colony without");
                break;
        }
    }

    std::optional<game::SystemId> current_;
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

std::unique_ptr<Screen> makeGalaxyMap(const ScreenArgs& args) {
    std::optional<game::SystemId> current;
    if (args.location) current = args.location->system;
    return std::make_unique<GalaxyMapScreen>(current);
}

} // namespace opense4::client::classic
