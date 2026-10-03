// Research (F8) and Tech Tree windows (docs/spec/06 §1.2, docs/spec/05 §1).

#include "client/classic/screens/empire_logic.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/pointers.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/research.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

using ruleset::TechAreaId;

// Tech areas in data order, grouped by their Group (in order of first appearance).
std::vector<std::pair<std::string, std::vector<TechAreaId>>> byGroup(const game::Rules& r, const std::vector<TechAreaId>& areas) {
    std::vector<std::pair<std::string, std::vector<TechAreaId>>> out;
    for (TechAreaId a : areas) {
        const std::string& g = r.tech(a).group;
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == g; });
        if (it == out.end()) {
            out.push_back({g, {}});
            it = out.end() - 1;
        }
        it->second.push_back(a);
    }
    return out;
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

// Areas worth showing in the Tech Tree: allowed in this game, and racial or
// unique areas only for empires that can have them.
bool treeShows(const game::Rules& r, const game::GameState& s, const game::Empire& e, TechAreaId a) {
    const auto& allowed = s.options.techAreasAllowed;
    if (!allowed.empty() && a.index() < allowed.size() && !allowed[a.index()]) return false;
    const ruleset::TechArea& t = r.tech(a);
    if (t.racialArea == 0 && t.uniqueArea == 0) return true;
    return e.techLevel(a) > 0 || r.techVisible(s, e, a);
}

// ---- Research ---------------------------------------------------------------------------------

class ResearchScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
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
        // The original's places (spec 06 §7 Q92, confirmed: binary): the area
        // list at (15,56), 560×243; the four project boxes of the page side by
        // side from (15,330).
        d.beginContent(576);
        hovered_.reset();
        const ImU32 blue = imColor(palette::kLabel);
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {15, 39}, blue, "Research Areas");
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {400, 39}, blue, "Current Level");
        textRightAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {546, 39}, blue, "Cost");
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
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {15, 316}, blue, std::format("{} Current Projects", e.research.size()));
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {575, 318}, blue, "(click a project to cancel it)");
        projects(ui, d);
        ui.tag("research:queue", d.at(kProjectBoxesAt), d.at(kProjectBoxesAt + Vec2{560, 130}));

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
        if (d.button("Reorder Projects", e.research.size() > 1)) {
            std::vector<std::string> rows;
            for (const auto& p : e.research) rows.push_back(std::format("{} {}", ui.rules().tech(p.area).name, e.techLevel(p.area) + 1));
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

private:
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

    void add(UiContext& ui, TechAreaId a) {
        const game::Empire& e = ui.me();
        if (queued(e, a)) {
            status_.set(std::format("{} is already being researched.", ui.rules().tech(a).name), true);
            return;
        }
        if (static_cast<int>(e.research.size()) >= kMaxProjects) {
            status_.set(std::format("At most {} projects can run at once.", kMaxProjects), true);
            return;
        }
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
        // session 3).
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
                const ImVec4 color = done ? imColorV(palette::kDim) : inQueue ? kTextDim : ImVec4(1, 1, 1, 1);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(a.index()));
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                if (ImGui::Selectable(t.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && !done) add(ui, a);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) hovered_ = a;
                ImGui::TableSetColumnIndex(1);
                right(std::to_string(level), color);
                ImGui::TableSetColumnIndex(2);
                right(done ? std::string("Complete") : inQueue ? std::string("Queued") : std::to_string(game::research::levelCost(r, s, a, level + 1)),
                      color);
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
        int over = -1;
        const int clicked = projectBoxes(ui, d, kProjectBoxesAt, boxes, &over);
        if (over >= 0) hovered_ = e.research[size_t(page_ * kProjectsPerPage + over)].area;
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

    int page_ = 0;
    std::optional<TechAreaId> hovered_;
    std::optional<TechAreaId> pinned_;
    std::optional<TechAreaId> removing_;
    YesNoPrompt confirm_;
    ReorderPopup reorder_;
    StatusLine status_;
};

// ---- Tech Tree ---------------------------------------------------------------------------------

class TechTreeScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Tech Tree", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        status_.draw();
        if (levels_) levelsView(ui);
        else areasView(ui);
        d.beginButtons();
        if (d.tab("Tech Areas", !levels_)) levels_ = false;
        ui.tagTab("tech-areas", !levels_);
        if (d.tab("Tech Levels", levels_)) levels_ = true;
        ui.tagTab("tech-levels", levels_);
        d.spacer();
        if (d.button("Export")) exportView(ui);
        d.close();
        return d.keepOpen();
    }

private:
    std::vector<TechAreaId> shownAreas(const UiContext& ui) const {
        std::vector<TechAreaId> out;
        for (uint32_t i = 0; i < ui.rules().data().techAreas.size(); ++i)
            if (treeShows(ui.rules(), ui.state(), ui.me(), TechAreaId{i})) out.push_back(TechAreaId{i});
        return out;
    }

    ImVec4 areaColor(const UiContext& ui, TechAreaId a) const {
        const int level = ui.me().techLevel(a);
        if (level >= ui.rules().tech(a).maxLevel) return kTextGood;
        if (ui.rules().techVisible(ui.state(), ui.me(), a)) return ImVec4(0.92f, 0.94f, 1.0f, 1.0f);
        return kTextDim;
    }

    void areasView(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::Empire& e = ui.me();
        heading(ui, "Tech Areas");
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "Prerequisites of every area. Green: complete, white: researchable, grey: locked. Click for levels.");
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        if (!beginListTable(ui, "##tree", 4, flags, ImVec2(0, 0), kListLineStep)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthFixed, ui.px(190));
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, ui.px(52));
        ImGui::TableSetupColumn("Requires", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Leads To", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const auto& [group, list] : byGroup(r, shownAreas(ui))) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kTextWarn, "%s", group.empty() ? "Other" : group.c_str());
            for (TechAreaId a : list) {
                const ruleset::TechArea& t = r.tech(a);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(a.index()));
                ImGui::PushStyleColor(ImGuiCol_Text, areaColor(ui, a));
                if (ImGui::Selectable(t.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = a;
                    levels_ = true;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && !t.description.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ui.px(360));
                    ImGui::TextUnformatted(t.description.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", e.techLevel(a), t.maxLevel);
                ImGui::TableSetColumnIndex(2);
                reqList(ui, t.requirements);
                ImGui::TableSetColumnIndex(3);
                std::string leads;
                for (TechAreaId dep : dependentAreas(r, a)) leads += (leads.empty() ? "" : ", ") + r.tech(dep).name;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kTextDim, "%s", leads.empty() ? "-" : leads.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopID();
            }
        }
        endListTable(ui);
    }

    // Requirements, each coloured by whether we meet it.
    void reqList(UiContext& ui, const std::vector<ruleset::TechRequirement>& reqs) {
        if (reqs.empty()) {
            ImGui::TextColored(kTextDim, "None");
            return;
        }
        ImGui::PushTextWrapPos(0.0f);
        std::string text;
        bool allMet = true;
        for (const auto& q : reqs) {
            if (!q.area.valid() || q.area.index() >= ui.rules().data().techAreas.size()) continue;
            text += std::format("{}{} {}", text.empty() ? "" : ", ", ui.rules().tech(q.area).name, q.level);
            allMet = allMet && ui.me().techLevel(q.area) >= q.level;
        }
        ImGui::TextColored(allMet ? kTextGood : kTextWarn, "%s", text.c_str());
        ImGui::PopTextWrapPos();
    }

    void levelsView(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Empire& e = ui.me();
        const auto areas = shownAreas(ui);
        if (areas.empty()) return;
        if (!selected_ || std::find(areas.begin(), areas.end(), *selected_) == areas.end()) selected_ = areas.front();

        beginList(ui, "##levelAreas", ImVec2(ui.px(220), 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        for (const auto& [group, list] : byGroup(r, areas)) {
            ImGui::TextColored(kTextWarn, "%s", group.empty() ? "Other" : group.c_str());
            for (TechAreaId a : list) {
                ImGui::PushID(int(a.index()));
                ImGui::PushStyleColor(ImGuiCol_Text, areaColor(ui, a));
                if (ImGui::Selectable(r.tech(a).name.c_str(), *selected_ == a)) selected_ = a;
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        endList(ui);
        ImGui::SameLine();
        ImGui::BeginChild("##levels", ImVec2(0, 0));
        const TechAreaId a = *selected_;
        const ruleset::TechArea& t = r.tech(a);
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(t.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "%s, our level %d of %d", t.group.c_str(), e.techLevel(a), t.maxLevel);
        if (!t.description.empty()) wrappedText(t.description, kTextDim);
        ImGui::TextColored(kTextBlue, "Requires");
        ImGui::SameLine();
        reqList(ui, t.requirements);
        ImGui::Spacing();
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (beginListTable(ui, "##levelTable", 3, flags, ImVec2(0, 0), kListLineStep)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, ui.px(48));
            ImGui::TableSetupColumn("Cost (RP)", ImGuiTableColumnFlags_WidthFixed, ui.px(86));
            ImGui::TableSetupColumn("Unlocks", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (int level = 1; level <= t.maxLevel; ++level) {
                const bool have = e.techLevel(a) >= level;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(have ? kTextGood : ImVec4(0.92f, 0.94f, 1.0f, 1.0f), "%d", level);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(formatNumber(game::research::levelCost(r, s, a, level)).c_str());
                ImGui::TableSetColumnIndex(2);
                const auto unlocks = techUnlocks(r, a, level);
                if (unlocks.empty()) ImGui::TextColored(kTextDim, "-");
                for (const TechUnlock& u : unlocks) {
                    ImGui::TextColored(kTextDim, "%s:", std::string(unlockKindName(u.kind)).c_str());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(u.name.c_str());
                }
            }
            endListTable(ui);
        }
        ImGui::EndChild();
    }

    void exportView(UiContext& ui) {
        const BusyPointer busy;  // the Hourglass while it writes the file (spec 06 §5.8)
        const std::string text = levels_ ? techLevelsExport(ui.rules(), ui.state(), ui.me()) : techAreasExport(ui.rules(), ui.state(), ui.me());
        const std::filesystem::path file = userDataDir() / (levels_ ? "tech_levels.txt" : "tech_areas.txt");
        std::ofstream out(file, std::ios::binary);
        out << text;
        if (out.good()) status_.set(std::format("Saved to {}", file.string()), false);
        else status_.set(std::format("Could not write {}", file.string()), true);
    }

    bool levels_ = false;
    std::optional<TechAreaId> selected_;
    StatusLine status_;
};

} // namespace

std::unique_ptr<Screen> makeResearch(const ScreenArgs&) { return std::make_unique<ResearchScreen>(); }
std::unique_ptr<Screen> makeTechTree(const ScreenArgs&) { return std::make_unique<TechTreeScreen>(); }

} // namespace opense4::client::classic
