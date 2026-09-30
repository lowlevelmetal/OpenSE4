// Intelligence window (docs/spec/06 §1.5, docs/spec/05 §2).

#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/design.hpp"
#include "game/intel.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

class IntelligenceScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Intelligence", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Empire& e = ui.me();
        const bool allowed = ui.state().options.allowIntel;

        d.beginContent();
        header(ui);
        if (!allowed) {
            ImGui::Spacing();
            wrappedText("Intelligence projects are disabled in this game (Game Settings: Allow Intelligence Projects).", kTextWarn);
        } else {
            const float bottom = ui.px(118);
            const float listH = ImGui::GetContentRegionAvail().y - bottom - ImGui::GetStyle().ItemSpacing.y;
            const float leftW = ImGui::GetContentRegionAvail().x * 0.42f;
            hovered_.reset();
            ImGui::BeginChild("##list", ImVec2(leftW, listH));
            projectList(ui);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##queue", ImVec2(0, listH));
            queue(ui);
            ImGui::EndChild();
            ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
            detail(ui);
            ImGui::EndChild();
        }

        d.beginButtons();
        projectPageButtons(d, page_);
        d.spacer();
        if (d.check("Repeat Projects", e.repeatIntel, allowed)) set(ui, e.intel, e.intelEvenly, !e.repeatIntel);
        if (d.check("Divide Evenly", e.intelEvenly, allowed)) set(ui, e.intel, !e.intelEvenly, e.repeatIntel);
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

private:
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

    void header(UiContext& ui) {
        const game::Empire& e = ui.me();
        if (Sprite icon = ui.art.icon32(Icon::Intelligence)) {
            image(ui, icon, {24, 24});
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        heading(ui, "Intelligence Points");
        ImGui::SameLine();
        ImGui::Text("%s available, %s per turn", formatNumber(e.intelPool).c_str(), formatNumber(e.economy.intelligence).c_str());
        ImGui::SameLine(ui.px(330));
        ImGui::TextColored(kTextBlue, "Defense");
        ImGui::SameLine();
        ImGui::Text("%s", formatNumber(game::intel::defensePoints(ui.rules(), ui.state(), e.id)).c_str());
        ImGui::SameLine(ui.px(470));
        ImGui::TextColored(kTextBlue, "Projects");
        ImGui::SameLine();
        ImGui::Text("%zu of %d", e.intel.size(), kMaxProjects);
        ImGui::SameLine(ui.px(610));
        ImGui::TextColored(kTextDim, "%s", e.intelEvenly ? "Divided evenly" : "In queue order");
        status_.draw();
        ImGui::Separator();
    }

    void add(UiContext& ui, uint32_t index) {
        const game::Empire& e = ui.me();
        if (static_cast<int>(e.intel.size()) >= kMaxProjects) {
            status_.set(std::format("At most {} projects can run at once.", kMaxProjects), true);
            return;
        }
        game::IntelProjectOrder o;
        o.project = index;
        const IntelTarget kind = intelTargetKind(project(ui, index));
        if (kind != IntelTarget::None) {
            const auto known = knownEmpires(ui);
            if (known.empty()) {
                status_.set("This project needs a target empire, and we have not met anyone yet.", true);
                return;
            }
            o.target = known.front();
            if (kind == IntelTarget::ThirdEmpire)
                for (game::EmpireId k : known)
                    if (k != o.target) {
                        o.thirdEmpire = k;
                        break;
                    }
        }
        auto q = e.intel;
        q.push_back(o);
        set(ui, q, e.intelEvenly, e.repeatIntel);
        if (status_.empty()) page_ = std::min(kMaxProjects / kProjectsPerPage - 1, static_cast<int>(q.size() - 1) / kProjectsPerPage);
    }

    void projectList(UiContext& ui) {
        const game::Rules& r = ui.rules();
        heading(ui, "Projects");
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "(click to add)");
        const auto list = availableIntelProjects(r, ui.me());
        if (list.empty()) {
            ImGui::TextColored(kTextDim, "No projects are available yet.");
            return;
        }
        // Grouped by Group, in order of first appearance.
        std::vector<std::string> groups;
        for (uint32_t i : list)
            if (std::find(groups.begin(), groups.end(), project(ui, i).group) == groups.end()) groups.push_back(project(ui, i).group);
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (!ImGui::BeginTable("##projects", 2, flags, ImVec2(0, 0))) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Cost (IP)", ImGuiTableColumnFlags_WidthFixed, ui.px(72));
        ImGui::TableHeadersRow();
        for (const std::string& g : groups) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kTextWarn, "%s", g.empty() ? "Other" : g.c_str());
            for (uint32_t i : list) {
                const ruleset::IntelProject& p = project(ui, i);
                if (p.group != g) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(i));
                if (ImGui::Selectable(p.name.c_str(), pinned_ && *pinned_ == i, ImGuiSelectableFlags_SpanAllColumns)) add(ui, i);
                if (ImGui::IsItemHovered()) hovered_ = i;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) pinned_ = i;
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(formatNumber(p.cost).c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    // Known planets of `owner` (colonies in systems we have explored).
    std::vector<game::ObjectId> knownPlanets(const UiContext& ui, game::EmpireId owner) const {
        std::vector<game::ObjectId> out;
        const game::GameState& s = ui.state();
        for (const auto& c : s.colonies)
            if (c && c->owner == owner && ui.me().hasExplored(s.galaxy.object(c->planet).system)) out.push_back(c->planet);
        return out;
    }

    std::vector<game::VehicleId> knownVehicles(const UiContext& ui, game::EmpireId owner) const {
        std::vector<game::VehicleId> out;
        for (game::VehicleId id : ui.me().knowledge.visibleVehicles)
            if (const game::Vehicle* v = ui.state().vehicle(id); v && v->owner == owner) out.push_back(id);
        return out;
    }

    // Target pickers for one queued project; returns true when the order changed.
    bool targets(UiContext& ui, game::IntelProjectOrder& o) {
        const game::GameState& s = ui.state();
        const IntelTarget kind = intelTargetKind(project(ui, o.project));
        if (kind == IntelTarget::None) {
            ImGui::TextColored(kTextDim, "Protects our empire while it runs.");
            return false;
        }
        bool changed = false;
        const auto known = knownEmpires(ui);
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(w);
        const std::string empireName = o.target.valid() ? s.empire(o.target).name : std::string("Choose an empire");
        if (ImGui::BeginCombo("##target", empireName.c_str())) {
            for (game::EmpireId k : known)
                if (ImGui::Selectable(s.empire(k).name.c_str(), k == o.target) && k != o.target) {
                    o.target = k;
                    o.targetPlanet = {};
                    o.targetVehicle = {};
                    if (o.thirdEmpire == k) o.thirdEmpire = {};
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (kind == IntelTarget::Empire) return changed;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (kind == IntelTarget::Planet) {
            const std::string label = o.targetPlanet.valid() ? s.galaxy.object(o.targetPlanet).name : std::string("Any planet");
            if (ImGui::BeginCombo("##planet", label.c_str())) {
                if (ImGui::Selectable("Any planet", !o.targetPlanet.valid())) {
                    changed = o.targetPlanet.valid();
                    o.targetPlanet = {};
                }
                for (game::ObjectId p : knownPlanets(ui, o.target))
                    if (ImGui::Selectable(s.galaxy.object(p).name.c_str(), p == o.targetPlanet) && p != o.targetPlanet) {
                        o.targetPlanet = p;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
        } else if (kind == IntelTarget::Vehicle) {
            const game::Vehicle* tv = o.targetVehicle.valid() ? s.vehicle(o.targetVehicle) : nullptr;
            const std::string label = tv ? tv->name : std::string("Any ship");
            if (ImGui::BeginCombo("##vehicle", label.c_str())) {
                if (ImGui::Selectable("Any ship", !o.targetVehicle.valid())) {
                    changed = o.targetVehicle.valid();
                    o.targetVehicle = {};
                }
                for (game::VehicleId id : knownVehicles(ui, o.target)) {
                    const game::Vehicle* v = s.vehicle(id);
                    const std::string name = std::format("{} ({})", v->name, s.design(v->design).name);
                    if (ImGui::Selectable(name.c_str(), id == o.targetVehicle) && id != o.targetVehicle) {
                        o.targetVehicle = id;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (kind == IntelTarget::ThirdEmpire) {
            const std::string label = o.thirdEmpire.valid() ? s.empire(o.thirdEmpire).name : std::string("Choose a third empire");
            if (ImGui::BeginCombo("##third", label.c_str())) {
                for (game::EmpireId k : known)
                    if (k != o.target && ImGui::Selectable(s.empire(k).name.c_str(), k == o.thirdEmpire) && k != o.thirdEmpire) {
                        o.thirdEmpire = k;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
        }
        return changed;
    }

    void queue(UiContext& ui) {
        const game::Empire& e = ui.me();
        heading(ui, std::format("Current Projects {}-{}", page_ * kProjectsPerPage + 1, (page_ + 1) * kProjectsPerPage).c_str());
        const float slotH = (ImGui::GetContentRegionAvail().y - 3 * ImGui::GetStyle().ItemSpacing.y) / kProjectsPerPage;
        std::optional<size_t> remove;
        std::optional<std::pair<size_t, game::IntelProjectOrder>> change;
        for (int slot = 0; slot < kProjectsPerPage; ++slot) {
            const size_t i = size_t(page_ * kProjectsPerPage + slot);
            ImGui::PushID(slot);
            ImGui::BeginChild("##slot", ImVec2(0, slotH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
            if (i >= e.intel.size()) {
                ImGui::TextColored(kTextDim, "%zu.", i + 1);
                ImGui::SameLine();
                ImGui::TextColored(kTextDim, "Empty: click a project to add it.");
            } else {
                game::IntelProjectOrder o = e.intel[i];
                const ruleset::IntelProject& p = project(ui, o.project);
                ImGui::Text("%zu.", i + 1);
                ImGui::SameLine();
                ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
                ImGui::TextUnformatted(p.name.c_str());
                ImGui::PopFont();
                if (ImGui::IsItemHovered()) hovered_ = o.project;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ui.px(64));
                if (ImGui::SmallButton("Remove")) remove = i;
                if (targets(ui, o)) change = std::pair{i, o};
                const float frac = p.cost > 0 ? float(double(o.progress) / double(p.cost)) : 0.0f;
                progressBar(ui, frac, std::format("{} / {} IP", formatNumber(o.progress), formatNumber(p.cost)));
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
        if (remove) {
            auto q = e.intel;
            q.erase(q.begin() + std::ptrdiff_t(*remove));
            set(ui, q, e.intelEvenly, e.repeatIntel);
        } else if (change) {
            auto q = e.intel;
            q[change->first] = change->second;
            set(ui, q, e.intelEvenly, e.repeatIntel);
        }
    }

    void detail(UiContext& ui) {
        const std::optional<uint32_t> i = hovered_ ? hovered_ : pinned_;
        if (!i) {
            ImGui::TextColored(kTextDim, "Point at a project to see what it does. Targets are chosen in the project slots.");
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
            case IntelTarget::ThirdEmpire: ImGui::TextUnformatted("An empire and a third empire"); break;
        }
        if (!p.description.empty()) wrappedText(p.description, kTextDim);
    }

    int page_ = 0;
    std::optional<uint32_t> hovered_;
    std::optional<uint32_t> pinned_;
    ReorderPopup reorder_;
    StatusLine status_;
};

} // namespace

std::unique_ptr<Screen> makeIntelligence(const ScreenArgs&) { return std::make_unique<IntelligenceScreen>(); }

} // namespace opense4::client::classic
