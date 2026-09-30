// Research (F8) and Tech Tree windows (docs/spec/06 §1.2, docs/spec/05 §1).

#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/research.hpp"

#include <algorithm>
#include <format>
#include <fstream>

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

        d.beginContent();
        header(ui);
        const float bottom = ui.px(112);
        const float listH = ImGui::GetContentRegionAvail().y - bottom - ImGui::GetStyle().ItemSpacing.y;
        const float leftW = ImGui::GetContentRegionAvail().x * 0.52f;
        hovered_.reset();
        ImGui::BeginChild("##areas", ImVec2(leftW, listH), ImGuiChildFlags_None);
        areaList(ui);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##projects", ImVec2(0, listH), ImGuiChildFlags_None);
        projects(ui);
        ImGui::EndChild();
        ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
        detail(ui);
        ImGui::EndChild();

        d.beginButtons();
        projectPageButtons(d, page_);
        d.spacer();
        if (d.check("Repeat Projects", e.repeatResearch)) set(ui, e.research, e.researchEvenly, !e.repeatResearch);
        if (d.check("Divide Pts Evenly", e.researchEvenly)) set(ui, e.research, !e.researchEvenly, e.repeatResearch);
        d.spacer();
        if (d.button("Tech Tree")) ui.open(ScreenId::TechTree);
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

    void header(UiContext& ui) {
        const game::Empire& e = ui.me();
        if (Sprite icon = ui.art.icon32(Icon::Research)) {
            image(ui, icon, {24, 24});
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        heading(ui, "Research Points");
        ImGui::SameLine();
        ImGui::Text("%s per turn", formatNumber(e.economy.research).c_str());
        const auto [owned, total] = techProgress(ui.rules(), ui.state(), e);
        ImGui::SameLine(ui.px(330));
        ImGui::TextColored(kTextBlue, "Projects");
        ImGui::SameLine();
        ImGui::Text("%zu of %d", e.research.size(), kMaxProjects);
        ImGui::SameLine(ui.px(470));
        ImGui::TextColored(kTextBlue, "Tech Levels");
        ImGui::SameLine();
        ImGui::Text("%d of %d", owned, total);
        ImGui::SameLine(ui.px(640));
        ImGui::TextColored(kTextDim, "%s", e.researchEvenly ? "Divided evenly" : "In queue order");
        status_.draw();
        ImGui::Separator();
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
        heading(ui, "Technology Areas");
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "(click to research, right-click for details)");
        const auto areas = researchableAreas(r, s, e);
        if (areas.empty()) {
            ImGui::TextColored(kTextDim, "Every technology area is fully researched.");
            return;
        }
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (!ImGui::BeginTable("##areaTable", 3, flags, ImVec2(0, 0))) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, ui.px(56));
        ImGui::TableSetupColumn("Next Level", ImGuiTableColumnFlags_WidthFixed, ui.px(88));
        ImGui::TableHeadersRow();
        for (const auto& [group, list] : byGroup(r, areas)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kTextWarn, "%s", group.empty() ? "Other" : group.c_str());
            for (TechAreaId a : list) {
                const ruleset::TechArea& t = r.tech(a);
                const int level = e.techLevel(a);
                const bool inQueue = queued(e, a);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(a.index()));
                if (inQueue) ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                const bool pinned = pinned_ && *pinned_ == a;
                if (ImGui::Selectable(t.name.c_str(), pinned, ImGuiSelectableFlags_SpanAllColumns)) add(ui, a);
                if (ImGui::IsItemHovered()) hovered_ = a;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) pinned_ = a;
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", level, t.maxLevel);
                ImGui::TableSetColumnIndex(2);
                if (inQueue) ImGui::TextUnformatted("Queued");
                else ImGui::TextUnformatted(formatNumber(game::research::levelCost(r, s, a, level + 1)).c_str());
                if (inQueue) ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    void projects(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Empire& e = ui.me();
        heading(ui, std::format("Current Projects {}-{}", page_ * kProjectsPerPage + 1, (page_ + 1) * kProjectsPerPage).c_str());
        const float slotH = (ImGui::GetContentRegionAvail().y - 3 * ImGui::GetStyle().ItemSpacing.y) / kProjectsPerPage;
        std::optional<size_t> remove;
        for (int slot = 0; slot < kProjectsPerPage; ++slot) {
            const size_t i = size_t(page_ * kProjectsPerPage + slot);
            ImGui::PushID(slot);
            ImGui::BeginChild("##slot", ImVec2(0, slotH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
            if (i >= e.research.size()) {
                ImGui::TextColored(kTextDim, "%zu.", i + 1);
                ImGui::SameLine();
                ImGui::TextColored(kTextDim, "Empty: click an area to add a project.");
            } else {
                const game::ResearchProject& p = e.research[i];
                const ruleset::TechArea& t = r.tech(p.area);
                const int next = e.techLevel(p.area) + 1;
                const int64_t cost = game::research::levelCost(r, s, p.area, next);
                ImGui::Text("%zu.", i + 1);
                ImGui::SameLine();
                ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
                ImGui::Text("%s %d", t.name.c_str(), next);
                ImGui::PopFont();
                if (ImGui::IsItemHovered()) hovered_ = p.area;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) pinned_ = p.area;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ui.px(64));
                if (ImGui::SmallButton("Remove")) remove = i;
                const float frac = cost > 0 ? float(double(p.progress) / double(cost)) : 0.0f;
                progressBar(ui, frac, std::format("{} / {} RP", formatNumber(p.progress), formatNumber(cost)));
                ImGui::TextColored(kTextBlue, "ETA");
                ImGui::SameLine();
                ImGui::TextUnformatted(etaText(game::research::etaTurns(r, s, e, i)).c_str());
                const auto unlocks = unlockNames(r, p.area, next);
                if (!unlocks.empty()) {
                    ImGui::SameLine(0, ui.px(16));
                    ImGui::TextColored(kTextBlue, "Unlocks");
                    ImGui::SameLine();
                    ImGui::TextColored(kTextDim, "%s", joined(unlocks, 2).c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", joined(unlocks).c_str());
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
        if (remove) {
            auto q = e.research;
            q.erase(q.begin() + std::ptrdiff_t(*remove));
            set(ui, q, e.researchEvenly, e.repeatResearch);
        }
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
        if (d.tab("Tech Levels", levels_)) levels_ = true;
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
        if (!ImGui::BeginTable("##tree", 4, flags, ImVec2(0, 0))) return;
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
        ImGui::EndTable();
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

        ImGui::BeginChild("##levelAreas", ImVec2(ui.px(220), 0), ImGuiChildFlags_Borders);
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
        ImGui::EndChild();
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
        if (ImGui::BeginTable("##levelTable", 3, flags, ImVec2(0, 0))) {
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
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    void exportView(UiContext& ui) {
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
