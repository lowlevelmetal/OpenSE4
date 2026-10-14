// Galaxy Map: the enlarged quadrant map opened by right-clicking the galaxy
// panel (docs/spec/06 §2.6), with overlays, Goto System, distances, names
// and per-system player notes; and its destination mode, in which the
// Stellar Manipulation window's Open Warp Point picks the system to link to.

#include "client/classic/map_style.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/quadrant_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"

#include <array>
#include <format>
#include <functional>
#include <optional>

namespace opense4::client::classic {

namespace {


std::string noteOf(const game::Empire& me, game::SystemId sys) {
    const auto& notes = me.knowledge.notes;
    return sys.index() < notes.size() ? notes[sys.index()] : std::string{};
}

class GalaxyMapScreen final : public Screen {
public:
    explicit GalaxyMapScreen(const ScreenArgs& args) : pickSystem_(args.pickSystem) {
        if (args.location && args.location->system.valid()) origin_ = args.location->system;
    }

    bool draw(UiContext& ui) override {
        // The original's frame (spec 07 session 5): no box around the content
        // area; the map in a #647EC7 frame (22,43)-(565,418) of the window, its
        // grid every 8 px; under it, in small label-blue type right-aligned to
        // x 565, the hint; on the line under it the hovered system's notes.
        Dialog d(ui, "Galaxy Map", DialogSize::Large, 190.0f, false);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        const bool destination = static_cast<bool>(pickSystem_);
        if (!started_) {
            // Show Names, and the destination mode's Show Distances, start from
            // the empire's options (spec 06 §2.6, spec 08 §3.6.7).
            names_ = ui.options().galaxyMapNames;
            destinationDistances_ = ui.options().galaxyMapDistances;
            started_ = true;
        }
        bool keep = true;
        d.beginContent(576);

        QuadrantMapOptions opt;
        opt.overlay = destination ? MapOverlay::Presence : overlay_;
        opt.names = names_;
        opt.distances = destination ? destinationDistances_ : distances_;
        opt.frameColor = palette::kFrameLight;
        opt.nearestAnywhere = true;   // a click anywhere takes the nearest system, explored or not (§2.6)
        if (destination) opt.current = origin_;
        ImGui::SetCursorScreenPos(d.at({22, 43}));
        const QuadrantMapResult r = quadrantMap(ui, "##map", {544, 376}, opt);
        ui.tag("galaxy-map:map", d.at({22, 43}), d.at({566, 419}));   // for lessons
        hovered_ = r.hovered;
        if (r.clicked) {
            if (destination) {
                // The destination: the order is given and the window closes (§2.6).
                const std::function<void(game::SystemId)> pick = pickSystem_;
                if (pick) pick(*r.clicked);
                keep = false;
            } else {
                openNotes(ui, *r.clicked);
            }
        }
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {565, 427}, imColor(palette::kLabel),
                    destination ? "(click on a system to choose it as the destination)" : "(click on a system to set its player notes)");

        // The line under the hint: the hovered system's notes (ours: its name
        // before them when it is explored), or in destination mode what the
        // distances count from.
        ImGui::SetCursorScreenPos(d.at({22, 441}));
        ImGui::BeginChild("##notes", ui.size({544, 22}), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        if (destination) {
            ImGui::TextColored(kLabelBlue, "All distances are in light-years from the system under the pointer.");
        } else if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%s", error_.c_str());
        } else if (hovered_) {
            const game::StarSystem& sys = s.galaxy.system(*hovered_);
            const std::string note = noteOf(me, sys.id);
            if (me.hasExplored(sys.id)) {
                ImGui::TextColored(kLabelBlue, "%s", sys.name.c_str());
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(note.c_str());
            if (!note.empty()) script::reportItem("notes:" + note);   // for input scripts
        }
        ImGui::EndChild();

        // The original's column: Presence (chosen), Avoid, Ally Claimed, Enemy
        // Claimed, Spaceports, Resupply Depots, four gaps, Goto System (slot
        // 11), a gap, Show Names (slot 13), Close. Ours in the gaps: Show
        // Distances (slot 7) and a legend (slots 8 to 10). Destination mode:
        // the overlay buttons hidden, Show Distances in slot 12 and Cancel in
        // Close's place (§2.6).
        d.beginButtons();
        static constexpr std::array<std::pair<MapOverlay, const char*>, 6> kOverlays{{{MapOverlay::Presence, "Presence"},
                                                                                     {MapOverlay::Avoid, "Avoid"},
                                                                                     {MapOverlay::AllyClaimed, "Ally Claimed"},
                                                                                     {MapOverlay::EnemyClaimed, "Enemy Claimed"},
                                                                                     {MapOverlay::Spaceports, "Spaceports"},
                                                                                     {MapOverlay::ResupplyDepots, "Resupply Depots"}}};
        // Any button click writes Show Names back into the empire's options (§2.6).
        bool clicked = false;
        if (destination) {
            for (int i = 0; i < 7; ++i) d.spacer();
        } else {
            std::optional<ImVec2> overlaysMin;
            for (const auto& [overlay, label] : kOverlays) {
                if (d.tab(label, overlay_ == overlay)) {
                    overlay_ = overlay;
                    clicked = true;
                }
                if (!overlaysMin) overlaysMin = ImGui::GetItemRectMin();
            }
            if (d.check("Show Distances", distances_)) {
                distances_ = !distances_;
                clicked = true;
            }
            ui.tag("galaxy-map:overlays", *overlaysMin, ImGui::GetItemRectMax());   // for lessons: what the map shows
        }
        legend(ui, d.skipSlots(3), opt.overlay);
        if (d.button("Goto System")) {
            filter_.clear();
            ImGui::OpenPopup("Goto System");
            clicked = true;
        }
        if (auto sys = gotoPopup(ui)) {
            ui.requests.showSystem = *sys;
            keep = false;
        }
        if (destination) {
            if (d.check("Show Distances", destinationDistances_)) {
                destinationDistances_ = !destinationDistances_;
                clicked = true;
            }
        } else {
            d.spacer();
        }
        if (d.check("Show Names", names_)) {
            names_ = !names_;
            clicked = true;
        }
        if (destination ? d.close(true, "Cancel") : d.close()) clicked = true;
        if (clicked) saveOptions(ui);
        notesPopup(ui);
        return keep && d.keepOpen();
    }

private:
    // Show Names, and in destination mode Show Distances, kept with the
    // empire's options (spec 06 §2.6, spec 08 §3.6.7).
    void saveOptions(UiContext& ui) {
        game::InterfaceOptions o = ui.options();
        o.galaxyMapNames = names_;
        if (pickSystem_) o.galaxyMapDistances = destinationDistances_;
        ui.setOptions(o);
    }

    // The Notes window of a system (spec 06 §2.6, confirmed: binary): titled
    // with the system's name, or "Unexplored System" while the player has not
    // explored it (spec 01 §6.1: never named), a text box, OK and Cancel.
    // OK or Enter saves the text; Cancel or Esc leaves it.
    static constexpr const char* kNotesId = "###galaxy-notes";
    void openNotes(UiContext& ui, game::SystemId sys) {
        notesFor_ = sys;
        draft_ = noteOf(ui.me(), sys);
        error_.clear();
        focusNote_ = true;
        openNotes_ = true;
    }

    // Drawn from one place (after the button column), where it is also opened.
    void notesPopup(UiContext& ui) {
        if (!notesFor_) return;
        const game::StarSystem& sys = ui.state().galaxy.system(*notesFor_);
        const std::string title =
            std::format("Notes for {}{}", ui.me().hasExplored(sys.id) ? sys.name : std::string("Unexplored System"), kNotesId);
        if (openNotes_) {
            ImGui::OpenPopup(title.c_str());
            openNotes_ = false;
        }
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({420, 150}), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal(title.c_str(), nullptr,
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags)) {
            notesFor_.reset();
            return;
        }
        ui.promptWindow();
        if (focusNote_) {
            ImGui::SetKeyboardFocusHere();
            focusNote_ = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool enter = inputString("##note", draft_, 400, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(34)));
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const bool ok = ImGui::Button("OK", ImVec2(w, ui.px(24))) || enter;
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(24))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (ok) {
            const game::CommandResult res = ui.session.issue(game::cmd::SetSystemNote{*notesFor_, draft_});
            error_ = res.ok ? std::string{} : res.error;
        }
        if (ok || cancel) {
            ImGui::CloseCurrentPopup();
            notesFor_.reset();
        }
        ImGui::EndPopup();
    }

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
    void legend(UiContext& ui, ImVec2 at, MapOverlay overlay) {
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
        switch (overlay) {
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
                row(ring(imColor(map_style::kGreen)), overlay == MapOverlay::Spaceports ? "Colony with a spaceport" : "Colony with a depot");
                row(ring(imColor(map_style::kYellow)), "Colony without");
                break;
        }
        ImGui::PopFont();
    }

    std::function<void(game::SystemId)> pickSystem_;   // destination mode: the window that asked
    std::optional<game::SystemId> origin_;             // destination mode: the vehicle's system, marked
    std::optional<game::SystemId> hovered_;
    std::optional<game::SystemId> notesFor_;           // the Notes window's system
    std::string draft_;
    std::string filter_;
    std::string error_;
    bool focusNote_ = false;
    bool openNotes_ = false;
    bool started_ = false;
    MapOverlay overlay_ = MapOverlay::Presence;
    bool distances_ = false;              // ours, in the plain mode: each opening starts without
    bool destinationDistances_ = false;   // the destination mode's (an empire option)
    bool names_ = false;
};

} // namespace

std::unique_ptr<Screen> makeGalaxyMap(const ScreenArgs& args) {
    // The plain window marks no current system (only the galaxy panel does,
    // §2.6); the destination mode marks the origin.
    return std::make_unique<GalaxyMapScreen>(args);
}

} // namespace opense4::client::classic
