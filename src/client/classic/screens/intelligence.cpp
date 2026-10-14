// Intelligence window (docs/spec/06 §1.5, docs/spec/05 §2).

#include "client/classic/screens/empire_logic.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/item_ref.hpp"
#include "client/classic/widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/design.hpp"
#include "game/research.hpp"
#include "game/events.hpp"
#include "game/intel.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace opense4::client::classic {

namespace {

// The Research window's layout (spec 07 session 5, Intelligence; spec 06 §7
// Q92): the points in the title strip, the project list at (15,56) 560×243
// under group headings in the large silver type, the projects as 14 px rows of
// small white type with the cost right-aligned, then the four project boxes
// of the page side by side from (15,330).
class IntelligenceScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        const bool keep = drawDialog(ui);
        popup_.draw(ui);
        return keep;
    }

private:
    bool drawDialog(UiContext& ui) {
        Dialog d(ui, "Intelligence", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Empire& e = ui.me();
        const bool allowed = ui.state().options.allowIntel;

        // The points available this turn (the pool) in the title strip, from x 169.
        d.titleText(169, imColor(palette::kLabel), "Intelligence Points Available:");
        const std::string points = formatNumber(e.intelPool);
        d.titleText(369, IM_COL32_WHITE, points);
        d.titleIcon(373 + ImGui::CalcTextSize(points.c_str()).x / ui.k(), ui.art.icon16(Icon::Intelligence));

        d.beginContent(576);
        hovered_.reset();
        const ImU32 blue = imColor(palette::kLabel);
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {15, 39}, blue, "Intelligence Projects");
        textRightAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {520, 39}, blue, "Cost");
        if (const Sprite icon = ui.art.icon16(Icon::Intelligence))
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(icon.tex.value)), d.at({522, 39}), d.at({538, 55}),
                                                 {icon.uv.min.x, icon.uv.min.y}, {icon.uv.max.x, icon.uv.max.y});
        ImGui::SetCursorScreenPos(d.at({15, 56}));
        projectList(ui, allowed);
        ui.tagItem("intelligence:projects");
        if (hovered_) {
            ImGui::SetNextWindowSize(ui.size({380, 0}));
            if (ImGui::BeginTooltip()) {
                detail(ui);
                ImGui::EndTooltip();
            }
        }
        ImGui::SetCursorScreenPos(d.at({17, 303}));
        if (!allowed) wrappedText("Intelligence projects are disabled in this game (Allow Intelligence Projects).", kTextWarn);
        else status_.draw();
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {575, 305}, blue, "(click intelligence project to add it as a current project)");
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {15, 316}, blue, std::format("{} Current Projects", e.intel.size()));
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {575, 318}, blue, "(click to cancel project)");
        queue(ui, d);
        ui.tag("intelligence:queue", d.at(kProjectBoxesAt), d.at(kProjectBoxesAt + Vec2{projectBoxesInUse(e.intel.size(), page_), 130}));
        targetPicker(ui);

        // The buttons: the three pages, a gap, Repeat Projects and Divide Pts
        // Evenly (slots 5 and 6), gaps, Reorder Projects (slot 13), Close.
        d.beginButtons();
        projectPageButtons(d, page_);
        d.spacer();
        if (d.check("Repeat Projects", e.repeatIntel, allowed)) set(ui, e.intel, e.intelEvenly, !e.repeatIntel);
        if (d.check("Divide Pts Evenly", e.intelEvenly, allowed)) set(ui, e.intel, !e.intelEvenly, e.repeatIntel);
        for (int gap = 0; gap < 6; ++gap) d.spacer();
        if (d.button("Reorder Projects", allowed && e.intel.size() > 1)) {
            std::vector<std::string> rows;
            for (const auto& p : e.intel) rows.push_back(orderLabel(ui, p));
            reorder_.open(std::move(rows));
        }
        if (auto order = reorder_.draw(ui)) {
            std::vector<game::IntelProjectOrder> q;
            for (size_t i : *order) q.push_back(ui.me().intel[i]);
            set(ui, q, ui.me().intelEvenly, ui.me().repeatIntel);
        }
        d.close();
        return d.keepOpen();
    }

    void set(UiContext& ui, std::vector<game::IntelProjectOrder> queue, bool evenly, bool repeat) {
        game::cmd::SetIntel c;
        c.queue = std::move(queue);
        c.evenly = evenly;
        c.repeat = repeat;
        status_.result(ui.session.issue(std::move(c)));
    }

    const ruleset::IntelProject& project(const UiContext& ui, uint32_t i) const { return ui.rules().data().intelProjects[i]; }

    std::string orderLabel(const UiContext& ui, const game::IntelProjectOrder& p) const {
        std::string out = project(ui, p.project).name;
        if (p.target.valid()) out += " vs " + ui.state().empire(p.target).name;
        return out;
    }

    // A click on a project adds it (spec 05 §2.1, confirmed: binary): with
    // twelve queued nothing happens; Intelligence Defense is added at once;
    // every other project first asks for the target empire in "Select
    // Empire", then, by its Type, for a ship, a planet, a technology area or
    // a third empire in a second list window starting with an "Any ..." row.
    // Cancel at either step adds nothing.
    void add(UiContext& ui, uint32_t index) {
        const game::Empire& e = ui.me();
        if (static_cast<int>(e.intel.size()) >= kMaxProjects) return;
        game::IntelProjectOrder o;
        o.project = index;
        if (intelTargetKind(project(ui, index)) == IntelTarget::None) {
            commit(ui, o);
            return;
        }
        picking_ = Picking{o, 0};
    }

    void commit(UiContext& ui, const game::IntelProjectOrder& o) {
        const game::Empire& e = ui.me();
        auto q = e.intel;
        q.push_back(o);
        set(ui, q, e.intelEvenly, e.repeatIntel);
        if (status_.empty()) page_ = std::min(kMaxProjects / kProjectsPerPage - 1, static_cast<int>(q.size() - 1) / kProjectsPerPage);
    }

    void projectList(UiContext& ui, bool allowed) {
        const game::Rules& r = ui.rules();
        const auto list = allowed ? availableIntelProjects(r, ui.me()) : std::vector<uint32_t>{};
        // The groups in alphabetical order (observed: Defense, General Espionage, ...).
        std::vector<std::string> groups;
        for (uint32_t i : list)
            if (std::find(groups.begin(), groups.end(), project(ui, i).group) == groups.end()) groups.push_back(project(ui, i).group);
        std::sort(groups.begin(), groups.end());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##projects", ui.size({560, 243}), kRowH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        for (const std::string& g : groups) {
            ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            dl->AddText(ImVec2(at.x + ui.px(2), at.y + ui.px(kTitleLead)), imColor(palette::kHeading), g.empty() ? "Other" : g.c_str());
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(w, ui.px(kHeadingH)));
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            for (uint32_t i : list) {
                const ruleset::IntelProject& p = project(ui, i);
                if (p.group != g) continue;
                ImGui::PushID(int(i));
                const ImVec2 row = ImGui::GetCursorScreenPos();
                // Named after the project (input scripts find it so); the text is drawn below.
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
                const bool clicked = ImGui::Selectable(std::format("{}##row", p.name).c_str(), false, ImGuiSelectableFlags_None, ImVec2(w, ui.px(kRowH)));
                ImGui::PopStyleColor();
                ui.tagOption("intelligence:projects", intelTargetKind(p) == IntelTarget::None ? "defense" : "other");   // for lessons
                ui.tagOption("intelligence:target", targetTag(intelTargetKind(p)));   // for input scripts: what it asks for
                if (clicked) add(ui, i);
                if (ImGui::IsItemHovered()) hovered_ = i;
                // A right-click opens the project's report (spec 05 §2.1, confirmed: binary).
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open({ItemRef::Kind::IntelProject, i});
                // The name at x 11 of the list, the cost right-aligned at x 525.
                dl->AddText(ImVec2(row.x + ui.px(9), row.y + ui.px(1 + kSmallLead)), IM_COL32_WHITE, p.name.c_str());
                const std::string cost = formatNumber(p.cost);
                dl->AddText(ImVec2(row.x + ui.px(523) - ImGui::CalcTextSize(cost.c_str()).x, row.y + ui.px(1 + kSmallLead)), IM_COL32_WHITE, cost.c_str());
                ImGui::PopID();
            }
            ImGui::PopFont();
        }
        endList(ui);
        ImGui::PopStyleVar(2);
    }

    // The name of a project's tag by what it asks for (`intelligence:target:<kind>`).
    static const char* targetTag(IntelTarget kind) {
        switch (kind) {
            case IntelTarget::None: return "none";
            case IntelTarget::Empire: return "empire";
            case IntelTarget::Planet: return "planet";
            case IntelTarget::Vehicle: return "ship";
            case IntelTarget::Tech: return "technology";
            case IntelTarget::ThirdEmpire: return "third-empire";
        }
        return "empire";
    }

    // The list windows after a click on a project that needs a target (spec
    // 05 §2.1, confirmed: binary), each with one "Name" column and Cancel:
    // "Select Empire", then by the project's Type a second list starting
    // with an "Any ..." row (intelTargetList).
    void targetPicker(UiContext& ui) {
        if (!picking_) return;
        const IntelTargetList list = intelTargetList(ui.rules(), ui.state(), ui.session.player(), picking_->order, picking_->stage);
        const std::string& title = list.title;
        const std::string id = std::string(title) + "###intelTarget";
        if (!ImGui::IsPopupOpen(id.c_str())) ImGui::OpenPopup(id.c_str());
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({340, 370}), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;
        bool cancel = false;
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        static constexpr ListColumn kName[] = {{"Name", 0, 0, false}};
        listHeader(ui, "##targethead", kName, listRowsWidth(ui, ImGui::GetContentRegionAvail().x));
        beginList(ui, "##targets", ImVec2(0, -footer), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        std::optional<game::IntelProjectOrder> picked;
        for (size_t i = 0; i < list.rows.size(); ++i) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(list.rows[i].label.c_str())) picked = list.rows[i].order;
            ui.tagOption("intelligence:pick", std::to_string(i + 1));   // for input scripts: the rows by place
            ImGui::PopID();
        }
        const bool done = picked.has_value();
        endList(ui);
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cancel = true;
        if (done || cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (cancel) {
            picking_.reset();
        } else if (done) {
            picking_->order = *picked;
            if (picking_->stage == 0 && !list.last) {
                picking_->stage = 1;
            } else {
                const game::IntelProjectOrder order = picking_->order;
                picking_.reset();
                commit(ui, order);
            }
        }
    }

    // The boxes of the shown page; a click on one cancels its project.
    void queue(UiContext& ui, const Dialog& d) {
        const game::Empire& e = ui.me();
        const game::GameState& s = ui.state();
        std::vector<int64_t> need;
        for (const game::IntelProjectOrder& o : e.intel) need.push_back(std::max<int64_t>(0, project(ui, o.project).cost - o.progress));
        const std::vector<int64_t> shares = game::research::allocate(e.intelPool, need, e.intelEvenly);
        std::vector<ProjectBox> boxes(kProjectsPerPage);
        for (int slot = 0; slot < kProjectsPerPage; ++slot) {
            const size_t i = size_t(page_ * kProjectsPerPage + slot);
            if (i >= e.intel.size()) continue;
            const game::IntelProjectOrder& o = e.intel[i];
            const ruleset::IntelProject& p = project(ui, o.project);
            ProjectBox& b = boxes[size_t(slot)];
            b.name = p.name;
            // Under the name the target picked, then "Against" and the
            // target empire's flag (spec 05 §2.1, confirmed: binary).
            b.target = intelTargetLine(ui.rules(), s, o);
            if (o.target.valid() && o.target.index() < s.empires.size()) b.againstStyle = s.empire(o.target).race.style;
            b.remaining = need[i];
            b.perTurn = i < shares.size() ? shares[i] : 0;
            b.percent = p.cost > 0 ? int(std::clamp<int64_t>(o.progress * 100 / p.cost, 0, 100)) : 0;
        }
        int over = -1, right = -1;
        const int clicked = projectBoxes(ui, d, kProjectBoxesAt, boxes, &over, &right);
        if (over >= 0) hovered_ = e.intel[size_t(page_ * kProjectsPerPage + over)].project;
        // A right-click opens the project's report (confirmed: binary).
        if (right >= 0) popup_.open({ItemRef::Kind::IntelProject, e.intel[size_t(page_ * kProjectsPerPage + right)].project});
        if (clicked >= 0) {
            // Asks first while the Empire Options' "confirm deleting an
            // intelligence project" is on (spec 06 §1.9).
            const size_t i = size_t(page_ * kProjectsPerPage + clicked);
            removing_ = i;
            removingOrder_ = e.intel[i];
            if (ui.options().confirmDeleteIntel) confirm_.open(std::format("Cancel the intelligence project {}?", orderLabel(ui, e.intel[i])));
            else cancelProject(ui);
        }
        if (confirm_.draw(ui)) cancelProject(ui);
    }

    void cancelProject(UiContext& ui) {
        const game::Empire& e = ui.me();
        // The project still at that place (nothing else changed it meanwhile).
        if (!removing_ || *removing_ >= e.intel.size() || e.intel[*removing_].project != removingOrder_.project ||
            e.intel[*removing_].target != removingOrder_.target) {
            removing_.reset();
            return;
        }
        auto q = e.intel;
        q.erase(q.begin() + std::ptrdiff_t(*removing_));
        removing_.reset();
        set(ui, q, e.intelEvenly, e.repeatIntel);
    }

    void detail(UiContext& ui) {
        const std::optional<uint32_t> i = hovered_ ? hovered_ : pinned_;
        if (!i) {
            ImGui::TextColored(kTextDim, "Point at a project to see what it does.");
            return;
        }
        const ruleset::IntelProject& p = project(ui, *i);
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "%s", p.group.c_str());
        ImGui::SameLine(ui.px(330));
        ImGui::TextColored(kTextBlue, "Cost");
        ImGui::SameLine();
        ImGui::Text("%s IP", formatNumber(p.cost).c_str());
        ImGui::SameLine(ui.px(470));
        ImGui::TextColored(kTextBlue, "Target");
        ImGui::SameLine();
        switch (intelTargetKind(p)) {
            case IntelTarget::None: ImGui::TextUnformatted("None (defense)"); break;
            case IntelTarget::Empire: ImGui::TextUnformatted("An empire"); break;
            case IntelTarget::Planet: ImGui::TextUnformatted("A planet of an empire"); break;
            case IntelTarget::Vehicle: ImGui::TextUnformatted("A ship of an empire"); break;
            case IntelTarget::Tech: ImGui::TextUnformatted("A technology area"); break;
            case IntelTarget::ThirdEmpire: ImGui::TextUnformatted("An empire and a third empire"); break;
        }
        if (!p.description.empty()) wrappedText(p.description, kTextDim);
    }

    // Rows of the project list: 14 px, under headings of the large type (inferred: 18 px).
    static constexpr float kRowH = 14.0f;
    static constexpr float kHeadingH = 18.0f;
    struct Picking {
        game::IntelProjectOrder order;
        int stage = 0;   // 0: the empire; 1: the specific target
    };

    int page_ = 0;
    std::optional<uint32_t> hovered_;
    std::optional<uint32_t> pinned_;
    std::optional<Picking> picking_;
    std::optional<size_t> removing_;
    game::IntelProjectOrder removingOrder_;
    YesNoPrompt confirm_;
    ReorderPopup reorder_;
    StatusLine status_;
    ItemReportPopup popup_;
};

} // namespace

std::unique_ptr<Screen> makeIntelligence(const ScreenArgs&) { return std::make_unique<IntelligenceScreen>(); }

} // namespace opense4::client::classic
