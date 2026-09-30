// Designs (F3) and Create Design (docs/spec/06 §1.2, docs/spec/03 §4).
//
// Designs lists the empire's ship and unit designs and the enemy designs it
// has seen, with a detail pane (figures from computeDesignStats, then the
// components or, with Stats\Strategy on, the service record and default
// strategy). Create / Copy / Edit / Upgrade open Create Design:
//   ScreenArgs::text   "new", "new-unit", "copy", "edit" or "upgrade"
//   ScreenArgs::design the template for copy / edit / upgrade
//   ScreenArgs::index  the hull of a new design (optional)
// Editing never changes a design in place: like the classic game, the result
// is saved as a new design and the original stays as it was.

#include "client/classic/screens/design_tools.hpp"
#include "client/classic/screens/item_reports.hpp"
#include "client/classic/screens/screens.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>

namespace opense4::client::classic {

namespace {

using Kind = ItemRef::Kind;

void title(UiContext& ui, std::string_view text) {
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopFont();
}

// A label in the classic blue with its value `column` frame pixels further right.
void field(UiContext& ui, const char* label, const std::string& value, float column, ImVec4 color = ImVec4(1, 1, 1, 1)) {
    ImGui::TextColored(kBlueText, "%s", label);
    ImGui::SameLine(ui.px(column));
    ImGui::TextColored(color, "%s", value.c_str());
}

// A 128 px hull portrait on a solid, framed backdrop (the item is the picture, for click tests).
void portrait(UiContext& ui, const Sprite& s) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 q(p.x + ui.px(128), p.y + ui.px(128));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, IM_COL32_BLACK);
    dl->AddRect(p, q, imColor(palette::kFrame));
    image(ui, s, {128, 128});
}

std::string sizeText(const game::DesignStats& st) { return std::format("{} / {} kT", st.tonnageUsed, st.tonnageMax); }

std::string shieldsText(const game::DesignStats& st) {
    return st.phasedShields > 0 ? std::format("{} + {} phased", st.shields, st.phasedShields) : std::to_string(st.shields);
}

std::string weaponsText(const game::DesignStats& st) {
    return st.weapons > 0 ? std::format("{} (range {})", st.weapons, st.maxWeaponRange) : std::string("None");
}

// Vehicles of a design in service: in space (unit groups count each unit) and carried as cargo.
int inService(const game::GameState& s, game::DesignId id) {
    int n = 0;
    auto cargo = [&](const game::Cargo& c) { n += c.unitCount(id); };
    for (const game::Vehicle& v : s.vehicles) {
        if (v.design == id) n += v.count;
        cargo(v.cargo);
    }
    for (const auto& c : s.colonies)
        if (c) cargo(c->cargo);
    return n;
}

std::string_view mountCode(const game::Rules& r, int32_t mount) {
    if (mount < 0 || static_cast<size_t>(mount) >= r.data().weaponMounts.size()) return {};
    return r.data().weaponMounts[static_cast<size_t>(mount)].code;
}

// The empire's design-name list (Dsgnname/<file> in the install), if any.
std::vector<std::string> loadNameList(UiContext& ui) {
    const std::string& file = ui.me().race.designNameFile;
    if (file.empty()) return {};
    const auto path = ui.art.files().find("Dsgnname/" + file);
    if (!path) return {};
    std::ifstream in(*path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseNameList(text);
}

// ---- Designs -----------------------------------------------------------------------------------

enum class DesignTab : uint8_t { Ships, Units, EnemyShips, EnemyUnits };
constexpr std::array<const char*, 4> kDesignTabs{"Ship Designs", "Unit Designs", "Enemy Ship Dsgn", "Enemy Unit Dsgn"};

bool enemyTab(DesignTab t) { return t == DesignTab::EnemyShips || t == DesignTab::EnemyUnits; }
bool unitTab(DesignTab t) { return t == DesignTab::Units || t == DesignTab::EnemyUnits; }

class DesignsScreen final : public Screen {
public:
    explicit DesignsScreen(const ScreenArgs& args) : selected_(args.design) {}

    bool draw(UiContext& ui) override {
        if (!initialized_) init(ui);
        const bool keep = drawDialog(ui);
        popup_.draw(ui);
        return keep;
    }

private:
    void init(UiContext& ui) {
        initialized_ = true;
        if (!ui.me().designs.empty()) newest_ = ui.me().designs.back();
        if (selected_.valid() && selected_.index() < ui.state().designs.size()) tab_ = tabOf(ui, ui.state().design(selected_));
    }

    DesignTab tabOf(const UiContext& ui, const game::Design& d) const {
        const bool unit = isUnitHull(ui.rules().hull(d.hull).type);
        if (d.owner == ui.session.player()) return unit ? DesignTab::Units : DesignTab::Ships;
        return unit ? DesignTab::EnemyUnits : DesignTab::EnemyShips;
    }

    std::vector<game::DesignId> listFor(const UiContext& ui, DesignTab tab) const {
        const game::GameState& s = ui.state();
        std::vector<game::DesignId> out;
        const std::vector<game::DesignId> source = enemyTab(tab) ? game::seenDesignIds(ui.me().knowledge) : ui.me().designs;
        for (game::DesignId id : source) {
            if (id.index() >= s.designs.size()) continue;
            const game::Design& d = s.design(id);
            if (enemyTab(tab) && d.owner == ui.session.player()) continue;
            if (isUnitHull(ui.rules().hull(d.hull).type) != unitTab(tab)) continue;
            if (hideObsolete_ && d.obsolete) continue;
            out.push_back(id);
        }
        return out;
    }

    const game::Design* selectedOwn(const UiContext& ui) const {
        if (!selected_.valid() || selected_.index() >= ui.state().designs.size()) return nullptr;
        const game::Design& d = ui.state().design(selected_);
        return d.owner == ui.session.player() ? &d : nullptr;
    }

    bool drawDialog(UiContext& ui) {
        // A design made in Create Design (new designs get the highest ids): show it.
        const auto& own = ui.me().designs;
        if (!own.empty() && (!newest_.valid() || own.back() > newest_)) {
            if (newest_.valid()) {
                selected_ = own.back();
                tab_ = tabOf(ui, ui.state().design(selected_));
                note_ = std::format("Created {}.", ui.state().design(selected_).name);
                noteIsError_ = false;
            }
            newest_ = own.back();
        }

        const std::vector<game::DesignId> list = listFor(ui, tab_);
        if (std::find(list.begin(), list.end(), selected_) == list.end()) selected_ = list.empty() ? game::DesignId{} : list.front();

        Dialog d(ui, "Designs", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        designList(ui, list);
        ImGui::SameLine();
        ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (selected_.valid()) detail(ui, ui.state().design(selected_));
        else ImGui::TextColored(kDimText, enemyTab(tab_) ? "No enemy designs of this kind have been seen yet." : "No designs of this kind yet.");
        ImGui::EndChild();
        d.beginButtons();
        buttons(ui, d);
        d.close();
        return d.keepOpen();
    }

    void designList(UiContext& ui, const std::vector<game::DesignId>& list) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const float w = ui.px(240);  // the original's list is about 240 wide, the detail takes the rest
        ImGui::BeginGroup();
        ImGui::TextColored(kBlueText, "%s", kDesignTabs[static_cast<size_t>(tab_)]);
        ImGui::SameLine();
        ImGui::TextColored(kDimText, "(%zu)", list.size());
        const float noteH = note_.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        ImGui::BeginChild("##list", ImVec2(w, -noteH), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < list.size(); ++i) {
            const game::Design& d = s.design(list[i]);
            const ruleset::VehicleSize& hull = r.hull(d.hull);
            const std::string& style = d.owner.valid() ? s.empire(d.owner).race.style : std::string{};
            const std::string sub = enemyTab(tab_) ? std::format("{} - {}", hull.name, d.owner.valid() ? s.empire(d.owner).name : "?")
                                                   : std::format("{} - {}", hull.name, d.designType.empty() ? "No type" : d.designType);
            RowStyle style2;
            style2.icon = 32;
            style2.sub = sub;
            style2.right = d.obsolete ? "Obsolete" : "";
            if (d.obsolete) style2.color = IM_COL32(140, 150, 165, 255);
            const RowResult row = itemRow(ui, static_cast<int>(i), ui.art.shipMini(style, hull), d.name, list[i] == selected_, style2);
            if (row.clicked) {
                selected_ = list[i];
                note_.clear();
            }
        }
        ImGui::EndChild();
        if (!note_.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
            ImGui::TextColored(noteIsError_ ? kWarnText : ImVec4(1.0f, 0.86f, 0.45f, 1.0f), "%s", note_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
    }

    void detail(UiContext& ui, const game::Design& d) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const bool own = d.owner == ui.session.player();
        const game::DesignStats st = game::computeDesignStats(r, own ? &ui.me() : nullptr, d);
        const ruleset::VehicleSize& hull = r.hull(d.hull);
        const std::string& style = d.owner.valid() ? s.empire(d.owner).race.style : std::string{};

        if (!own) {
            image(ui, ui.art.flag(style), {26, 18});
            ImGui::SameLine();
        }
        // The original's detail: portrait, then the name with label lines and their
        // values indented below; then one column of statistics.
        portrait(ui, ui.art.shipPortrait(style, hull));
        hullReportOnClick(ui, d.hull);
        ImGui::SameLine(0, ui.px(8));
        ImGui::BeginGroup();
        title(ui, d.name);
        auto stacked = [&](const char* label, const std::string& value, ImVec4 color = ImVec4(1, 1, 1, 1)) {
            ImGui::TextColored(kBlueText, "%s", label);
            ImGui::Indent(ui.px(10));
            ImGui::TextColored(color, "%s", value.c_str());
            ImGui::Unindent(ui.px(10));
        };
        stacked("Size", std::format("{} ({}kT)", hull.name, hull.tonnage));
        if (own) {
            stacked("Design Type", d.designType.empty() ? std::string("-") : d.designType);
            stacked("Date Created", formatDate(d.createdTurn));
            if (d.obsolete) stacked("Status", "Obsolete", ImVec4(1.0f, 0.7f, 0.4f, 1.0f));
        } else if (d.owner.valid()) {
            stacked("Owner", s.empire(d.owner).name);
        }
        ImGui::EndGroup();

        constexpr float kCol = 128.0f;
        ImGui::TextColored(kBlueText, "Cost");
        ImGui::SameLine(ui.px(kCol));
        resources(ui, st.cost, true);
        field(ui, "Class", std::string(ruleset::displayName(hull.type)), kCol);
        field(ui, "Space", sizeText(st), kCol);
        field(ui, "Structure", std::to_string(st.structure), kCol);
        field(ui, "Movement", std::to_string(st.movement), kCol);
        field(ui, "Shields", shieldsText(st), kCol);
        field(ui, "Weapons", weaponsText(st), kCol);
        field(ui, "Cargo Space", std::to_string(st.cargoCapacity), kCol);
        field(ui, "Supply Capacity", std::to_string(st.supplyCapacity), kCol);
        if (own && !st.problems.empty()) {
            ImGui::Spacing();
            for (const std::string& p : st.problems) ImGui::TextColored(kWarnText, "! %s", p.c_str());
        }

        ImGui::Spacing();
        if (statsView_) serviceRecord(ui, d, own);
        else components(ui, d);
    }

    void hullReportOnClick(UiContext& ui, uint32_t hull) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click for the %s report", ui.rules().hull(hull).name.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Hull, hull});
    }

    void components(UiContext& ui, const game::Design& d) {
        const game::Rules& r = ui.rules();
        heading(ui, std::format("Components ({})", d.entries.size()).c_str());
        const auto groups = groupEntries(d.entries);
        for (size_t i = 0; i < groups.size(); ++i) {
            const EntryGroup& g = groups[i];
            const ruleset::Component& c = r.component(g.entry.component);
            const std::string mount = mountLabel(r, g.entry.mount);
            const std::string text = std::format("{} x {}{}{}", g.count, mount, mount.empty() ? "" : " ", c.name);
            const std::string right = std::format("{} kT", game::mounted(r, g.entry).tonnage * g.count);
            RowStyle style;
            style.icon = 24;
            style.right = right;
            style.badge = mountCode(r, g.entry.mount);
            const RowResult row = itemRow(ui, static_cast<int>(i), ui.art.component(c.picture), text, false, style);
            if (row.clicked || row.rightClicked) popup_.open({Kind::Component, g.entry.component, g.entry.mount});
        }
        if (groups.empty()) ImGui::TextColored(kDimText, "No components");
    }

    void serviceRecord(UiContext& ui, const game::Design& d, bool own) {
        const game::GameState& s = ui.state();
        heading(ui, "Service Record");
        if (!own) {
            ImGui::TextColored(kDimText, "Unknown for foreign designs.");
            return;
        }
        constexpr float kCol = 110.0f;
        field(ui, "Built", std::to_string(d.built), kCol);
        field(ui, "In service", std::to_string(inService(s, d.id)), kCol);
        field(ui, "Lost", std::to_string(d.lost), kCol);
        field(ui, "Kills", std::to_string(d.kills), kCol);
        field(ui, "Enemy tonnage", std::format("{} kT destroyed", d.enemyTonnageDestroyed), kCol);
        ImGui::Spacing();
        heading(ui, "Default Strategy");
        const auto& strategies = ui.me().strategies;
        const std::string current = d.strategy < strategies.size() ? strategies[d.strategy].name : std::string("Default");
        ImGui::SetNextItemWidth(ui.px(240));
        if (ImGui::BeginCombo("##strategy", current.c_str())) {
            for (uint32_t i = 0; i < strategies.size(); ++i)
                if (ImGui::Selectable(std::format("{}##{}", strategies[i].name, i).c_str(), i == d.strategy) && i != d.strategy) {
                    const game::CommandResult res = ui.session.issue(game::cmd::SetVehicleStrategy{d.id, i});
                    if (!res.ok) {
                        note_ = res.error;
                        noteIsError_ = true;
                    }
                }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDimText, "How ships of this design fight when they are not in a fleet; a fleet's own strategy takes precedence.");
        ImGui::PopTextWrapPos();
    }

    void buttons(UiContext& ui, Dialog& d) {
        for (size_t i = 0; i < kDesignTabs.size(); ++i)
            if (lampButton(ui, d, kDesignTabs[i], tab_ == static_cast<DesignTab>(i))) {
                tab_ = static_cast<DesignTab>(i);
                note_.clear();
            }
        d.spacer();
        const game::Design* own = selectedOwn(ui);
        auto openDesigner = [&](const char* mode) {
            ScreenArgs a;
            a.text = mode;
            if (own) a.design = own->id;
            ui.open(ScreenId::CreateDesign, a);
            note_.clear();
        };
        if (d.button("Create")) {
            ScreenArgs a;
            a.text = unitTab(tab_) ? "new-unit" : "new";
            ui.open(ScreenId::CreateDesign, a);
            note_.clear();
        }
        if (d.button("Copy", own != nullptr)) openDesigner("copy");
        if (d.button("Edit", own != nullptr)) openDesigner("edit");
        if (d.button("Upgrade", own != nullptr)) {
            std::vector<game::DesignEntry> entries = own->entries;
            if (upgradeEntries(ui.rules(), ui.me(), entries)) {
                openDesigner("upgrade");
            } else {
                note_ = std::format("{} already uses the newest components.", own->name);
                noteIsError_ = false;
            }
        }
        const bool obsolete = own && own->obsolete;
        if (d.button(obsolete ? "Make Current" : "Make Obsolete", own != nullptr)) {
            const game::CommandResult res = ui.session.issue(game::cmd::SetDesignObsolete{own->id, !obsolete});
            note_ = res.ok ? std::string{} : res.error;
            noteIsError_ = !res.ok;
        }
        if (lampButton(ui, d, "Hide Obsolete", hideObsolete_)) hideObsolete_ = !hideObsolete_;
        if (lampButton(ui, d, "Stats\\Strategy", statsView_)) statsView_ = !statsView_;
        if (d.button("Simulator")) {
            note_ = "The combat simulator is not available yet.";
            noteIsError_ = false;
        }
    }

    bool initialized_ = false;
    DesignTab tab_ = DesignTab::Ships;
    game::DesignId selected_;
    game::DesignId newest_;  // the newest own design seen, to notice designs just created
    bool hideObsolete_ = false;
    bool statsView_ = false;
    std::string note_;
    bool noteIsError_ = false;
    ItemReportPopup popup_;
};

// ---- Create Design -------------------------------------------------------------------------------

class CreateDesignScreen final : public Screen {
public:
    explicit CreateDesignScreen(const ScreenArgs& args) : args_(args) {}

    bool draw(UiContext& ui) override {
        if (!initialized_) init(ui);
        const bool keep = drawDialog(ui);
        popup_.draw(ui);
        return keep && !done_;
    }

private:
    void setName(std::string_view name) {
        name_.fill(0);
        std::copy_n(name.begin(), std::min(name.size(), name_.size() - 1), name_.begin());
    }

    std::string name() const {
        std::string n(name_.data());
        while (!n.empty() && n.back() == ' ') n.pop_back();
        const size_t first = n.find_first_not_of(' ');
        return first == std::string::npos ? std::string{} : n.substr(first);
    }

    std::string suggestName(const UiContext& ui) {
        std::string n = suggestDesignName(ui.state(), ui.me(), names_, nameCursor_);
        if (!n.empty()) return n;
        // No name list in the install: "<Hull> 1", "<Hull> 2", ...
        for (int i = 1;; ++i) {
            n = std::format("{} {}", ui.rules().hull(hull_).name, i);
            if (!designNameTaken(ui.state(), ui.me(), n)) return n;
        }
    }

    std::vector<uint32_t> availableHulls(const UiContext& ui) const {
        std::vector<uint32_t> out;
        const game::Rules& r = ui.rules();
        for (size_t t = 0; t < static_cast<size_t>(ruleset::VehicleType::Count); ++t)
            for (uint32_t i = 0; i < r.data().vehicleSizes.size(); ++i)
                if (static_cast<size_t>(r.hull(i).type) == t && (r.hullAvailable(ui.me(), i) || i == hull_)) out.push_back(i);
        return out;
    }

    void pickDesignType(const UiContext& ui) {
        // Prefer a design type named after the hull class (fighters, satellites, ...).
        const auto& types = ui.me().designTypes;
        designType_ = types.empty() ? std::string{} : types.front();
        const std::string_view cls = ruleset::displayName(ui.rules().hull(hull_).type);
        if (ui.rules().hull(hull_).type == ruleset::VehicleType::Ship) return;
        for (const std::string& t : types)
            if (containsNoCase(t, cls)) {
                designType_ = t;
                return;
            }
    }

    void init(UiContext& ui) {
        initialized_ = true;
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        names_ = loadNameList(ui);
        if (!names_.empty()) nameCursor_ = (me.designs.size() * 37 + s.turn * 11) % names_.size();

        if (args_.design.valid() && args_.design.index() < s.designs.size()) {
            const game::Design& t = s.design(args_.design);
            hull_ = t.hull;
            entries_ = t.entries;
            designType_ = t.designType;
            strategy_ = t.strategy;
            if (args_.text == "upgrade") {
                upgradeEntries(r, me, entries_);
                setName(nextVersionName(s, me, t.name));
                origin_ = std::format("Upgrade of {}", t.name);
            } else if (args_.text == "edit") {
                setName(t.name);
                origin_ = std::format("Editing {}: saved as a new design", t.name);
            } else {
                setName(suggestName(ui));
                origin_ = std::format("Copy of {}", t.name);
            }
            if (t.owner != me.id) {
                pickDesignType(ui);
                strategy_ = 0;
            }
        } else {
            const bool unit = args_.text == "new-unit";
            std::optional<uint32_t> pick;
            if (args_.index >= 0 && static_cast<size_t>(args_.index) < r.data().vehicleSizes.size()) pick = static_cast<uint32_t>(args_.index);
            for (uint32_t i = 0; i < r.data().vehicleSizes.size() && !pick; ++i)
                if (r.hullAvailable(me, i) && isUnitHull(r.hull(i).type) == unit) pick = i;
            for (uint32_t i = 0; i < r.data().vehicleSizes.size() && !pick; ++i)
                if (r.hullAvailable(me, i)) pick = i;
            hull_ = pick.value_or(0);
            pickDesignType(ui);
            setName(suggestName(ui));
        }
        mounts_ = hullMounts(r, me, hull_);
    }

    void setHull(const UiContext& ui, uint32_t hull) {
        if (hull == hull_) return;
        const bool classChanged = ui.rules().hull(hull).type != ui.rules().hull(hull_).type;
        hull_ = hull;
        mounts_ = hullMounts(ui.rules(), ui.me(), hull_);
        if (mount_ >= 0 && std::find(mounts_.begin(), mounts_.end(), static_cast<uint32_t>(mount_)) == mounts_.end()) mount_ = -1;
        if (classChanged) {
            group_.clear();
            if (!designTypeChosen_) pickDesignType(ui);
        }
        error_.clear();
    }

    std::vector<std::string> nameProblems(const UiContext& ui) const {
        std::vector<std::string> out;
        const std::string n = name();
        if (n.empty()) out.emplace_back("The design needs a name");
        else if (designNameTaken(ui.state(), ui.me(), n)) out.emplace_back("Another design already has this name");
        return out;
    }

    bool drawDialog(UiContext& ui) {
        Dialog d(ui, "Create Design", DialogSize::Tall);
        if (!d.open()) return d.keepOpen();
        const game::DesignStats st = game::computeDesignStats(ui.rules(), &ui.me(), hull_, entries_);
        d.beginContent();
        const float warningsH = ui.px(96);
        header(ui, st);
        ImGui::Spacing();
        lists(ui, st, ImGui::GetContentRegionAvail().y - warningsH - ImGui::GetStyle().ItemSpacing.y);
        warnings(ui, st);
        d.beginButtons();
        buttons(ui, d);
        cancel(ui, d);
        return d.keepOpen();
    }

    void header(UiContext& ui, const game::DesignStats& st) {
        const game::Rules& r = ui.rules();
        const ruleset::VehicleSize& hull = r.hull(hull_);
        portrait(ui, ui.art.shipPortrait(ui.me().race.style, hull));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click for the %s report", hull.name.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Hull, hull_});
        ImGui::SameLine(0, ui.px(12));

        // Pickers.
        ImGui::BeginGroup();
        constexpr float kLabel = 48.0f;
        const float comboW = ui.px(270);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kBlueText, "Size");
        ImGui::SameLine(ui.px(kLabel));
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##hull", std::format("{} ({} kT)", hull.name, hull.tonnage).c_str(), ImGuiComboFlags_HeightLarge)) {
            std::optional<ruleset::VehicleType> lastType;
            for (uint32_t h : availableHulls(ui)) {
                const ruleset::VehicleSize& vs = r.hull(h);
                if (vs.type != lastType) ImGui::SeparatorText(std::string(ruleset::displayName(vs.type)).c_str());
                lastType = vs.type;
                if (ImGui::Selectable(std::format("{} ({} kT)##{}", vs.name, vs.tonnage, h).c_str(), h == hull_)) setHull(ui, h);
            }
            ImGui::EndCombo();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kBlueText, "Type");
        ImGui::SameLine(ui.px(kLabel));
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##type", designType_.empty() ? "(none)" : designType_.c_str(), ImGuiComboFlags_HeightLarge)) {
            const auto& types = ui.me().designTypes;
            for (size_t i = 0; i < types.size(); ++i)
                if (ImGui::Selectable(std::format("{}##{}", types[i], i).c_str(), types[i] == designType_)) {
                    designType_ = types[i];
                    designTypeChosen_ = true;
                }
            ImGui::EndCombo();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kBlueText, "Name");
        ImGui::SameLine(ui.px(kLabel));
        const float suggestW = ui.px(72);
        ImGui::SetNextItemWidth(comboW - suggestW - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputText("##name", name_.data(), name_.size())) error_.clear();
        ImGui::SameLine();
        if (ImGui::Button("Suggest", ImVec2(suggestW, 0))) {
            setName(suggestName(ui));
            error_.clear();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next name from the empire's list of design names");
        if (!origin_.empty()) ImGui::TextColored(kDimText, "%s", origin_.c_str());
        ImGui::EndGroup();

        // Running totals.
        ImGui::SameLine(0, ui.px(18));
        ImGui::BeginGroup();
        constexpr float kCol = 84.0f;
        const bool over = st.tonnageUsed > st.tonnageMax;
        field(ui, "Space", std::format("{}  ({} free)", sizeText(st), std::max(0, st.tonnageMax - st.tonnageUsed)), kCol,
              over ? kWarnText : ImVec4(1, 1, 1, 1));
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = ui.px(5);
            const float frac = st.tonnageMax > 0 ? std::min(1.0f, float(st.tonnageUsed) / float(st.tonnageMax)) : 0.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(20, 32, 70, 255));
            dl->AddRectFilled(p, ImVec2(p.x + w * frac, p.y + h), over ? IM_COL32(220, 70, 60, 255) : IM_COL32(70, 130, 230, 255));
            ImGui::Dummy(ImVec2(w, h));
        }
        ImGui::TextColored(kBlueText, "Cost");
        ImGui::SameLine(ui.px(kCol));
        resources(ui, st.cost, true);
        field(ui, "Movement", std::to_string(st.movement), kCol);
        field(ui, "Supply", formatNumber(st.supplyCapacity), kCol);
        field(ui, "Cargo", std::format("{} kT", st.cargoCapacity), kCol);
        field(ui, "Shields", shieldsText(st), kCol);
        field(ui, "Structure", std::to_string(st.structure), kCol);
        field(ui, "Weapons", weaponsText(st), kCol);
        ImGui::EndGroup();
    }

    void lists(UiContext& ui, const game::DesignStats& st, float height) {
        const game::Rules& r = ui.rules();
        std::optional<ItemRef> hovered;
        bool overLists = false;
        const float headerH = ImGui::GetTextLineHeightWithSpacing();
        const float childH = std::max(ui.px(120), height - headerH);

        // Components on the design (click removes).
        ImGui::BeginGroup();
        ImGui::TextColored(kBlueText, "On Design (%zu)", entries_.size());
        ImGui::SameLine();
        ImGui::TextColored(kDimText, "- click to remove");
        ImGui::BeginChild("##on", ImVec2(ui.px(262), childH), ImGuiChildFlags_Borders);
        overLists = overLists || ImGui::IsWindowHovered();
        std::optional<size_t> remove;
        if (condensed_) {
            const auto groups = groupEntries(entries_);
            for (size_t i = 0; i < groups.size(); ++i) {
                const EntryGroup& g = groups[i];
                const ruleset::Component& c = r.component(g.entry.component);
                const std::string text = std::format("{} x {}", g.count, c.name);
                const std::string right = std::format("{} kT", game::mounted(r, g.entry).tonnage * g.count);
                RowStyle style;
                style.icon = 20;
                style.right = right;
                style.badge = mountCode(r, g.entry.mount);
                const RowResult row = itemRow(ui, static_cast<int>(i), ui.art.component(c.picture), text, false, style);
                if (row.clicked) remove = g.last;
                if (row.rightClicked) popup_.open({Kind::Component, g.entry.component, g.entry.mount});
                if (row.hovered) hovered = ItemRef{Kind::Component, g.entry.component, g.entry.mount};
            }
        } else {
            for (size_t i = 0; i < entries_.size(); ++i) {
                const game::DesignEntry& e = entries_[i];
                const ruleset::Component& c = r.component(e.component);
                const std::string right = std::format("{} kT", game::mounted(r, e).tonnage);
                const std::string mount = mountLabel(r, e.mount);
                RowStyle style;
                style.icon = 28;
                style.right = right;
                style.sub = mount;
                style.badge = mountCode(r, e.mount);
                const RowResult row = itemRow(ui, static_cast<int>(i), ui.art.component(c.picture), c.name, false, style);
                if (row.clicked) remove = i;
                if (row.rightClicked) popup_.open({Kind::Component, e.component, e.mount});
                if (row.hovered) hovered = ItemRef{Kind::Component, e.component, e.mount};
            }
        }
        if (entries_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kDimText, "Click components on the right to add them.");
            ImGui::PopTextWrapPos();
        }
        if (remove) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*remove));
            error_.clear();
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        // Components that can go on this hull (click adds, with the chosen mount where it applies).
        ImGui::SameLine();
        ImGui::BeginGroup();
        std::string filter = group_.empty() ? std::string("All types") : group_;
        if (mount_ >= 0) filter += " - " + r.data().weaponMounts[static_cast<size_t>(mount_)].longName;
        ImGui::TextColored(kBlueText, "Available");
        ImGui::SameLine();
        ImGui::TextColored(kDimText, "- %s", filter.c_str());
        ImGui::BeginChild("##available", ImVec2(ui.px(300), childH), ImGuiChildFlags_Borders);
        overLists = overLists || ImGui::IsWindowHovered();
        const std::vector<uint32_t> comps = designerComponents(r, ui.me(), hull_, group_, onlyLatest_);
        std::string lastGroup;
        for (size_t i = 0; i < comps.size(); ++i) {
            const uint32_t ci = comps[i];
            const ruleset::Component& c = r.component(ci);
            if (group_.empty() && !condensed_ && (i == 0 || c.generalGroup != lastGroup)) listHeading(ui, c.generalGroup);
            lastGroup = c.generalGroup;
            const game::DesignEntry entry{ci, mountFor(r, hull_, ci, mount_)};
            const game::MountedComponent m = game::mounted(r, entry);
            const int onDesign = static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e.component == ci; }));
            const bool fits = st.tonnageUsed + m.tonnage <= st.tonnageMax && (c.maxPerVehicle == 0 || onDesign < c.maxPerVehicle);
            const std::string size = std::format("{} kT", m.tonnage);
            RowStyle style;
            style.badge = mountCode(r, entry.mount);
            if (!fits) style.color = IM_COL32(135, 140, 155, 255);
            if (condensed_) {
                style.icon = 20;
                style.right = size;
            } else {
                style.icon = 36;
                style.sub = size;
                style.cost = &m.cost;
            }
            const RowResult row = itemRow(ui, static_cast<int>(i), ui.art.component(c.picture), c.name, false, style);
            if (row.clicked) {
                entries_.push_back(entry);
                error_.clear();
            }
            if (row.rightClicked) popup_.open({Kind::Component, ci, entry.mount});
            if (row.hovered) hovered = ItemRef{Kind::Component, ci, entry.mount};
        }
        if (comps.empty()) ImGui::TextColored(kDimText, "Nothing of this type fits this hull.");
        ImGui::EndChild();
        ImGui::EndGroup();

        if (hovered) hover_ = hovered;
        else if (!overLists) hover_.reset();

        // Detail of the hovered component, or of the hull.
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(kBlueText, "%s", hover_ ? "Component" : "Hull");
        ImGui::BeginChild("##hover", ImVec2(0, childH), ImGuiChildFlags_Borders);
        itemDetail(ui, hover_.value_or(ItemRef{Kind::Hull, hull_}), DetailStyle::Compact);
        ImGui::EndChild();
        ImGui::EndGroup();
    }

    void warnings(UiContext& ui, const game::DesignStats& st) {
        ImGui::BeginChild("##warnings", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (!error_.empty()) ImGui::TextColored(kWarnText, "Not created: %s", error_.c_str());
        std::vector<std::string> problems = nameProblems(ui);
        problems.insert(problems.end(), st.problems.begin(), st.problems.end());
        if (problems.empty() && error_.empty()) {
            ImGui::TextColored(kGoodText, "The design meets every rule and can be created.");
        } else {
            // Two columns keep a long list readable.
            if (ImGui::BeginTable("##problems", 2, ImGuiTableFlags_SizingStretchSame)) {
                for (const std::string& p : problems) {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kWarnText, "! %s", p.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    void buttons(UiContext& ui, Dialog& d) {
        const game::Rules& r = ui.rules();
        if (lampButton(ui, d, "Comp Type", !group_.empty())) ImGui::OpenPopup("##groups");
        if (ImGui::BeginPopup("##groups")) {
            if (ImGui::Selectable("All Types", group_.empty())) group_.clear();
            ImGui::Separator();
            for (const std::string& g : componentGroups(r, ui.me(), hull_))
                if (ImGui::Selectable(g.c_str(), datafile::keysEqual(g, group_))) group_ = g;
            ImGui::EndPopup();
        }
        if (lampButton(ui, d, "Weap Mount", mount_ >= 0, !mounts_.empty())) ImGui::OpenPopup("##mounts");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", mounts_.empty() ? "No weapon mount fits this hull" : "Mount for weapons added next");
        if (ImGui::BeginPopup("##mounts")) {
            if (ImGui::Selectable("No Mount", mount_ < 0)) mount_ = -1;
            for (uint32_t m : mounts_) {
                const auto& wm = r.data().weaponMounts[m];
                if (ImGui::Selectable(std::format("{}##{}", wm.longName, m).c_str(), mount_ == static_cast<int32_t>(m)))
                    mount_ = static_cast<int32_t>(m);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Damage %d%%, size %d%%, cost %d%%, range %+d, to hit %+d%%", wm.damagePercent, wm.tonnagePercent,
                                      wm.costPercent, wm.rangeModifier, wm.toHitModifier);
            }
            ImGui::EndPopup();
        }
        if (lampButton(ui, d, "Condensed View", condensed_)) condensed_ = !condensed_;
        if (lampButton(ui, d, "Only Latest", onlyLatest_)) onlyLatest_ = !onlyLatest_;
        d.spacer();
        if (d.button("Weapons Report")) {
            ScreenArgs a;
            a.text = "weapons";
            a.index = mount_;
            ui.open(ScreenId::Help, a);
        }
        if (d.button("Clear Design", !entries_.empty())) {
            entries_.clear();
            error_.clear();
        }
        d.spacer();
        if (d.button("Create Design")) create(ui);
    }

    void cancel(UiContext& ui, Dialog& d) {
        const float h = ui.px(26);
        const float y = ImGui::GetWindowHeight() - h - ImGui::GetStyle().WindowPadding.y;
        if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput &&
                            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, h)) || escape) d.requestClose();
    }

    void create(UiContext& ui) {
        game::Design d;
        d.name = name();
        d.designType = designType_;
        d.hull = hull_;
        d.entries = entries_;
        d.strategy = strategy_;
        const game::CommandResult res = ui.session.issue(game::cmd::CreateDesign{std::move(d)});
        if (res.ok) done_ = true;
        else error_ = res.error;
    }

    ScreenArgs args_;
    bool initialized_ = false;
    bool done_ = false;
    uint32_t hull_ = 0;
    std::string designType_;
    bool designTypeChosen_ = false;
    std::array<char, 64> name_{};
    std::vector<game::DesignEntry> entries_;
    uint32_t strategy_ = 0;
    std::string origin_;
    std::string error_;

    std::string group_;  // Comp Type filter; empty = all
    int32_t mount_ = -1;
    std::vector<uint32_t> mounts_;
    bool condensed_ = false;
    bool onlyLatest_ = true;
    std::optional<ItemRef> hover_;

    std::vector<std::string> names_;
    size_t nameCursor_ = 0;
    ItemReportPopup popup_;
};

} // namespace

std::unique_ptr<Screen> makeDesigns(const ScreenArgs& args) { return std::make_unique<DesignsScreen>(args); }
std::unique_ptr<Screen> makeCreateDesign(const ScreenArgs& args) { return std::make_unique<CreateDesignScreen>(args); }

} // namespace opense4::client::classic
