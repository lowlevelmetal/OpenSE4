// Research (F8) and Tech Tree windows (docs/spec/06 §1.2, docs/spec/05 §1).

#include "client/classic/screens/empire_logic.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/item_ref.hpp"
#include "client/classic/widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/pointers.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/research.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

using ruleset::TechAreaId;

// Tech areas grouped by their Group: the groups in alphabetical order, the
// areas in data order under each (spec 06 §1.11, §7 Q92, confirmed: binary).
std::vector<std::pair<std::string, std::vector<TechAreaId>>> byGroup(const game::Rules& r, const std::vector<TechAreaId>& areas) {
    return techAreasByGroup(r, areas);
}

std::string joined(const std::vector<std::string>& names, size_t limit = 0) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (limit && i == limit) {
            out += std::format(" and {} more", names.size() - limit);
            break;
        }
        out += (i ? ", " : "") + names[i];
    }
    return out;
}

// ---- Research ---------------------------------------------------------------------------------

class ResearchScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        const bool keep = drawDialog(ui);
        popup_.draw(ui);
        return keep;
    }

private:
    bool drawDialog(UiContext& ui) {
        Dialog d(ui, "Research", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Empire& e = ui.me();

        // The original's layout: points in the title strip, the full-width list of
        // areas, then the current projects in four boxes side by side.
        d.titleText(170, ImGui::GetColorU32(kTextBlue), "Research Points Available:");
        // Produced at the end of last turn, spent at the end of this one (spec 05 §1.1).
        const std::string points = std::to_string(game::research::availablePoints(ui.state(), e));
        d.titleText(356, IM_COL32_WHITE, points);
        d.titleIcon(360 + ImGui::CalcTextSize(points.c_str()).x / ui.k(), ui.art.icon16(Icon::Research));
        ui.tag("research:points", d.at({165, 6}), d.at({380 + ImGui::CalcTextSize(points.c_str()).x / ui.k(), 30}));   // for lessons
        // The original's places (spec 06 §7 Q92, confirmed: binary): the area
        // list at (15,56), 560×243; the four project boxes of the page side by
        // side from (15,330).
        d.beginContent(576);
        hovered_.reset();
        const ImU32 blue = imColor(palette::kLabel);
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {15, 39}, blue, "Research Areas");
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {400, 39}, blue, "Current Level");
        textRightAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {546, 39}, blue, "Cost");
        ui.tag("research:headings", d.at({15, 39}), d.at({575, 56}));   // for lessons: the columns' headings
        if (const Sprite icon = ui.art.icon16(Icon::Research))
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(icon.tex.value)), d.at({548, 39}), d.at({564, 55}),
                                                 {icon.uv.min.x, icon.uv.min.y}, {icon.uv.max.x, icon.uv.max.y});
        ImGui::SetCursorScreenPos(d.at({15, 56}));
        areaList(ui);
        ui.tagItem("research:areas");
        if (hovered_) {
            ImGui::SetNextWindowSize(ui.size({380, 0}));
            if (ImGui::BeginTooltip()) {
                detail(ui);
                ImGui::EndTooltip();
            }
        }
        ImGui::SetCursorScreenPos(d.at({17, 303}));
        status_.draw();
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {575, 305}, blue, "(click a research area to add it as a project)");
        // "1 Current Project", "N Current Projects" (confirmed: binary).
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {15, 316}, blue,
               std::format("{} Current Project{}", e.research.size(), e.research.size() == 1 ? "" : "s"));
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {575, 318}, blue, "(click a project to cancel it)");
        projects(ui, d);
        ui.tag("research:queue", d.at(kProjectBoxesAt), d.at(kProjectBoxesAt + Vec2{projectBoxesInUse(e.research.size(), page_), 130}));

        // The original's column (spec 06 §7 Q92, confirmed: binary): the three
        // pages, a gap, Repeat Projects and Divide Pts Evenly (slots 5 and 6),
        // Tech Tree in slot 12 only when the game lets players see the
        // complete tech tree (the only way to that window), Reorder Projects in
        // slot 13, Close.
        d.beginButtons();
        projectPageButtons(d, page_);
        d.spacer();
        if (d.check("Repeat Projects", e.repeatResearch)) set(ui, e.research, e.researchEvenly, !e.repeatResearch);
        ui.tagItem("research:repeat");
        if (d.check("Divide Pts Evenly", e.researchEvenly)) set(ui, e.research, !e.researchEvenly, e.repeatResearch);
        ui.tagItem("research:divide-evenly");
        for (int gap = 0; gap < 5; ++gap) d.spacer();
        if (ui.state().options.completeTechTree) {
            if (d.button("Tech Tree")) ui.open(ScreenId::TechTree);
            ui.tagItem("research:tech-tree");
        } else {
            d.spacer();
        }
        // The reorder window's rows are the areas' names (confirmed: binary).
        if (d.button("Reorder Projects", e.research.size() > 1)) {
            std::vector<std::string> rows;
            for (const auto& p : e.research) rows.push_back(ui.rules().tech(p.area).name);
            reorder_.open(std::move(rows));
        }
        if (auto order = reorder_.draw(ui)) {
            std::vector<game::ResearchProject> q;
            for (size_t i : *order) q.push_back(ui.me().research[i]);
            set(ui, q, ui.me().researchEvenly, ui.me().repeatResearch);
        }
        d.close();
        return d.keepOpen();
    }

    void set(UiContext& ui, std::vector<game::ResearchProject> queue, bool evenly, bool repeat) {
        game::cmd::SetResearch c;
        c.queue = std::move(queue);
        c.evenly = evenly;
        c.repeat = repeat;
        status_.result(ui.session.issue(std::move(c)));
    }

    bool queued(const game::Empire& e, TechAreaId a) const {
        return std::any_of(e.research.begin(), e.research.end(), [&](const game::ResearchProject& p) { return p.area == a; });
    }

    // A click on an area adds it; on a queued area, or with the queue full,
    // nothing happens (confirmed: binary).
    void add(UiContext& ui, TechAreaId a) {
        const game::Empire& e = ui.me();
        if (queued(e, a) || static_cast<int>(e.research.size()) >= kMaxProjects) return;
        auto q = e.research;
        q.push_back({a, 0});
        set(ui, q, e.researchEvenly, e.repeatResearch);
        if (status_.empty()) page_ = std::min(kMaxProjects / kProjectsPerPage - 1, static_cast<int>(q.size() - 1) / kProjectsPerPage);
    }

    void areaList(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Empire& e = ui.me();
        // Researchable areas and, in their places, the completed ones: dimmed,
        // with "Complete" as the cost, and a click does nothing (observed, spec 07
        // session 3). A queued area is green and still shows its next level's
        // cost (confirmed: binary). A right-click on a row opens the area's
        // report (confirmed: binary); the hover detail is ours.
        const std::vector<ResearchListArea> areas = researchListAreas(r, s, e);
        std::vector<TechAreaId> ids;
        for (const ResearchListArea& a : areas) ids.push_back(a.area);
        auto complete = [&](TechAreaId a) {
            return std::any_of(areas.begin(), areas.end(), [&](const ResearchListArea& x) { return x.area == a && x.complete; });
        };
        if (!beginListTable(ui, "##areaTable", 3, ImGuiTableFlags_None, ui.size({560, 243}), kListLineStep)) return;
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, ui.px(70));
        ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, ui.px(96));
        auto right = [](const std::string& text, ImVec4 color) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x);
            ImGui::TextColored(color, "%s", text.c_str());
        };
        for (const auto& [group, list] : byGroup(r, ids)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            heading(ui, group.empty() ? "Other" : group.c_str());
            for (TechAreaId a : list) {
                const ruleset::TechArea& t = r.tech(a);
                const int level = e.techLevel(a);
                const bool done = complete(a);
                const bool inQueue = queued(e, a);
                const ImVec4 color = done ? imColorV(palette::kDim) : inQueue ? kQueuedGreen : ImVec4(1, 1, 1, 1);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(a.index()));
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                if (ImGui::Selectable(t.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && !done) add(ui, a);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) hovered_ = a;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open({ItemRef::Kind::TechArea, a.value});
                ImGui::TableSetColumnIndex(1);
                right(std::to_string(level), color);
                ImGui::TableSetColumnIndex(2);
                right(done ? std::string("Complete") : std::to_string(game::research::levelCost(r, s, a, level + 1)), color);
                ImGui::PopID();
            }
        }
        if (areas.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kTextDim, "No technology area is known.");
        }
        endListTable(ui);
    }

    void projects(UiContext& ui, const Dialog& d) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Empire& e = ui.me();
        // What each project still needs and gets this turn (spec 05 §1.4).
        std::vector<int64_t> need;
        for (size_t i = 0; i < e.research.size(); ++i) {
            const game::ResearchProject& p = e.research[i];
            need.push_back(std::max<int64_t>(0, game::research::levelCost(r, s, p.area, e.techLevel(p.area) + 1) - p.progress));
        }
        const std::vector<int64_t> shares = game::research::allocate(game::research::availablePoints(s, e), need, e.researchEvenly);
        std::vector<ProjectBox> boxes(kProjectsPerPage);
        for (int slot = 0; slot < kProjectsPerPage; ++slot) {
            const size_t i = size_t(page_ * kProjectsPerPage + slot);
            if (i >= e.research.size()) continue;
            const game::ResearchProject& p = e.research[i];
            const int next = e.techLevel(p.area) + 1;
            const int64_t cost = game::research::levelCost(r, s, p.area, next);
            ProjectBox& b = boxes[size_t(slot)];
            b.name = r.tech(p.area).name;
            b.level = std::format("Research Level {}", next);
            b.remaining = need[i];
            b.perTurn = i < shares.size() ? shares[i] : 0;
            // truncate(points paid × 100 / level cost), within 0 and 100.
            b.percent = cost > 0 ? int(std::clamp<int64_t>(p.progress * 100 / cost, 0, 100)) : 0;
        }
        int over = -1, rightClicked = -1;
        const int clicked = projectBoxes(ui, d, kProjectBoxesAt, boxes, &over, &rightClicked);
        if (over >= 0) hovered_ = e.research[size_t(page_ * kProjectsPerPage + over)].area;
        // A right-click on a box opens its area's report (confirmed: binary).
        if (rightClicked >= 0) popup_.open({ItemRef::Kind::TechArea, e.research[size_t(page_ * kProjectsPerPage + rightClicked)].area.value});
        if (clicked >= 0) {
            // Clicking a project cancels it, asking first while the Empire
            // Options' "confirm deleting a research project" is on (spec 06 §1.9).
            const size_t i = size_t(page_ * kProjectsPerPage + clicked);
            removing_ = e.research[i].area;
            if (ui.options().confirmDeleteResearch)
                confirm_.open(std::format("Cancel the research project {} {}?", r.tech(*removing_).name, e.techLevel(*removing_) + 1));
            else cancelProject(ui);
        }
        if (confirm_.draw(ui)) cancelProject(ui);
    }

    void cancelProject(UiContext& ui) {
        const game::Empire& e = ui.me();
        if (!removing_) return;
        auto q = e.research;
        const auto it = std::find_if(q.begin(), q.end(), [&](const game::ResearchProject& p) { return p.area == *removing_; });
        removing_.reset();
        if (it == q.end()) return;
        q.erase(it);
        set(ui, q, e.researchEvenly, e.repeatResearch);
    }

    void detail(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Empire& e = ui.me();
        const std::optional<TechAreaId> a = hovered_ ? hovered_ : pinned_;
        if (!a) {
            ImGui::TextColored(kTextDim, "Point at a technology area to see what its next level brings.");
            return;
        }
        const ruleset::TechArea& t = r.tech(*a);
        const int level = e.techLevel(*a);
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(t.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "%s", t.group.c_str());
        ImGui::SameLine(ui.px(330));
        ImGui::TextColored(kTextBlue, "Level");
        ImGui::SameLine();
        ImGui::Text("%d of %d", level, t.maxLevel);
        if (level < t.maxLevel) {
            ImGui::SameLine(ui.px(470));
            ImGui::TextColored(kTextBlue, "Level %d costs", level + 1);
            ImGui::SameLine();
            ImGui::Text("%s RP", formatNumber(game::research::levelCost(r, s, *a, level + 1)).c_str());
        }
        if (!t.description.empty()) wrappedText(t.description, kTextDim);
        if (level < t.maxLevel) {
            const auto unlocks = unlockNames(r, *a, level + 1);
            ImGui::TextColored(kTextBlue, "Level %d unlocks:", level + 1);
            ImGui::SameLine();
            wrappedText(unlocks.empty() ? "Nothing new by itself (later levels or other areas may need it)." : joined(unlocks));
        } else {
            ImGui::TextColored(kTextGood, "Fully researched.");
        }
    }

    // A queued area's colour (inferred: the progress bar's green).
    static constexpr ImVec4 kQueuedGreen{0.0f, 1.0f, 0.0f, 1.0f};

    int page_ = 0;
    std::optional<TechAreaId> hovered_;
    std::optional<TechAreaId> pinned_;
    std::optional<TechAreaId> removing_;
    YesNoPrompt confirm_;
    ReorderPopup reorder_;
    StatusLine status_;
    ItemReportPopup popup_;
};

// ---- Tech Tree ---------------------------------------------------------------------------------
//
// The original's window (spec 06 §7 Q92, confirmed: binary): titled
// "Technology Tree", it lists every tech area of the data set, nothing
// hidden, under the groups' headings in alphabetical order; an area the
// empire can research now is white, any other grey (#606060). Tech Areas
// shows each area's requirements (columns Tech Name, Required Tech, Level),
// Tech Levels what each level makes available (Tech Name, Level, "Item
// [Number of Tech Dependencies]"). Export in slot 13 writes the view shown to
// TechAreas.txt or TechLevels.txt in the saves folder, one line per row with
// its cells joined by commas, then says where in "Export Successful". Ours:
// the places of the list and its columns; an area's description under the
// pointer.

class TechTreeScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        const bool keep = drawDialog(ui);
        exportNote(ui);
        return keep;
    }

private:
    bool drawDialog(UiContext& ui) {
        Dialog d(ui, "Technology Tree", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent(576);
        list(ui, d);
        d.beginButtons();
        if (d.tab("Tech Areas", !levels_)) levels_ = false;
        ui.tagTab("tech-areas", !levels_);
        if (d.tab("Tech Levels", levels_)) levels_ = true;
        ui.tagTab("tech-levels", levels_);
        for (int gap = 0; gap < 10; ++gap) d.spacer();
        if (d.button("Export")) exportView(ui);
        ui.tagItem("tech-tree:export");
        d.close();
        return d.keepOpen();
    }

    // The column headings at (16,39) over the list at (16,59), 557x403 (ours).
    void list(UiContext& ui, const Dialog& d) {
        const game::Rules& r = ui.rules();
        static constexpr ListColumn kAreaColumns[] = {{"Tech Name", 210, 0, false}, {"Required Tech", 0, 0, false}, {"Level", 110, 0, false}};
        static constexpr ListColumn kLevelColumns[] = {{"Tech Name", 210, 0, false}, {"Level", 60, 0, false},
                                                       {"Item [Number of Tech Dependencies]", 0, 0, false}};
        const std::span<const ListColumn> cols = levels_ ? std::span<const ListColumn>(kLevelColumns) : std::span<const ListColumn>(kAreaColumns);
        ImGui::SetCursorScreenPos(d.at({16, 39}));
        const float listW = ui.px(557);
        const float rowsW = listRowsWidth(ui, listW);
        listHeader(ui, levels_ ? "##levelhead" : "##areahead", cols, rowsW);
        const std::vector<float> edges = columnEdges(ui, cols, rowsW);
        // White: an area the empire can research now; grey otherwise.
        const std::vector<TechAreaId> open = researchableAreas(r, ui.state(), ui.me());
        std::vector<TechAreaId> all;
        for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) all.push_back(TechAreaId{i});
        ImGui::SetCursorScreenPos(d.at({16, 59}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, levels_ ? "##levels" : "##areas", ImVec2(listW, ui.px(403)), kRowH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        const float viewTop = ImGui::GetScrollY(), viewH = ImGui::GetWindowHeight();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float y = 0;
        std::optional<TechAreaId> hovered;
        auto inView = [&](float top) { return top + ui.px(kRowH) > viewTop && top < viewTop + viewH; };
        for (const auto& [group, areas] : techAreasByGroup(r, all)) {
            if (inView(y)) {
                ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
                dl->AddText({origin.x + ui.px(4), origin.y + y + ui.px(1)}, imColor(palette::kHeading), group.empty() ? "Other" : group.c_str());
                ImGui::PopFont();
            }
            y += ui.px(kRowH);
            for (TechAreaId a : areas) {
                const ImU32 color = std::find(open.begin(), open.end(), a) != open.end() ? IM_COL32_WHITE : imColor(palette::kDim);
                for (const TechTreeRow& row : levels_ ? techLevelRows(r, a) : techAreaRows(r, a)) {
                    if (inView(y)) {
                        const ImVec2 at{origin.x, origin.y + y};
                        if (ImGui::IsMouseHoveringRect(at, {at.x + w, at.y + ui.px(kRowH)}) && ImGui::IsWindowHovered()) hovered = a;
                        for (size_t c = 0; c < 3; ++c) {
                            dl->PushClipRect({at.x + edges[c], at.y}, {at.x + edges[c + 1] - ui.px(2), at.y + ui.px(kRowH)}, true);
                            dl->AddText({at.x + edges[c] + ui.px(4), at.y + ui.px(1)}, color, row.cells[c].c_str());
                            dl->PopClipRect();
                        }
                    }
                    y += ui.px(kRowH);
                }
            }
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(w, std::max(1.0f, y)));
        endList(ui);
        ImGui::PopStyleVar(2);
        ui.tagItem("tech-tree:list");
        if (hovered && !r.tech(*hovered).description.empty()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ui.px(360));
            ImGui::TextUnformatted(r.tech(*hovered).description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void exportView(UiContext& ui) {
        const BusyPointer busy;  // the Hourglass while it writes the file (spec 06 §5.8)
        const std::string text = techTreeExport(techTreeRows(ui.rules(), levels_));
        const std::filesystem::path dir = savesDir();
        const std::filesystem::path file = dir / (levels_ ? "TechLevels.txt" : "TechAreas.txt");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream out(file, std::ios::binary);
        out << text;
        out.close();
        if (out) note_ = {"Export Successful", std::format("Exported to {}.", file.string())};
        else note_ = {"Export Failed", std::format("Could not write {}.", file.string())};
    }

    // The box after Export.
    void exportNote(UiContext& ui) {
        if (!note_) return;
        const std::string id = note_->first + "###techexport";
        if (!ImGui::IsPopupOpen(id.c_str())) ImGui::OpenPopup(id.c_str());
        ImGui::SetNextWindowSize(ui.size({420, 0}));
        if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) {
            ImGui::TextWrapped("%s", note_->second.c_str());
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) {
                ImGui::CloseCurrentPopup();
                note_.reset();
            }
            ImGui::EndPopup();
        }
    }

    static constexpr float kRowH = 18.0f;   // the list's rows and headings (ours)
    bool levels_ = false;
    std::optional<std::pair<std::string, std::string>> note_;   // title, text
};

} // namespace

std::unique_ptr<Screen> makeResearch(const ScreenArgs&) { return std::make_unique<ResearchScreen>(); }
std::unique_ptr<Screen> makeTechTree(const ScreenArgs&) { return std::make_unique<TechTreeScreen>(); }

} // namespace opense4::client::classic
