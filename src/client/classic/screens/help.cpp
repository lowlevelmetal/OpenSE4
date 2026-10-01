// Help (F1): the in-game encyclopedia (docs/spec/06 §1.2). Tabs list what the
// empire knows — researched components, facilities and vehicle sizes,
// visible tech areas, available intelligence projects — plus treaties,
// formations and the main-window hotkeys. The Weapons Report is a second
// mode of the same window: a damage-by-range grid of the known weapons.
//
// Opening arguments (ScreenArgs::text): a tab name ("components",
// "facilities", "shipsizes", "unitsizes", "techareas", "treaties",
// "intelprojects", "formations", "hotkeys") with `index` selecting an item, or
// "weapons" for the Weapons Report with `index` as the initial weapon mount.

#include "client/classic/screens/design_tools.hpp"
#include "client/classic/screens/item_reports.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/design.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace opense4::client::classic {

namespace {

using Kind = ItemRef::Kind;
using ruleset::WeaponKind;

enum class HelpTab : uint8_t { Components, Facilities, ShipSizes, UnitSizes, TechAreas, Treaties, IntelProjects, Formations, Hotkeys, Count };
constexpr size_t kTabCount = static_cast<size_t>(HelpTab::Count);
constexpr std::array<const char*, kTabCount> kTabLabels{"Components", "Facilities",     "Ship Sizes", "Unit Sizes", "Tech Areas",
                                                        "Treaties",   "Intel Projects", "Formations", "Hotkeys"};

// ---- Hotkeys (docs/spec/06 §3), described in our own words -------------------------------------

struct Hotkey {
    const char* keys;
    const char* action;
};

constexpr std::array<Hotkey, 13> kWindowKeys{{
    {"F1", "Help: this encyclopedia"},
    {"Shift+F1", "Manual: the page about the window in front"},
    {"F2", "Game Menu: save, load, options"},
    {"F3", "Designs: create and manage designs"},
    {"F4", "Planets: every planet seen so far"},
    {"F5", "Colonies: your settled worlds"},
    {"F6", "Ships \\ Units: your vehicles and fleets"},
    {"F7", "Construction Queues: all build queues"},
    {"F8", "Research: pick what to study"},
    {"F9", "Empires: diplomacy and intelligence"},
    {"F10", "Log: news from the last turn"},
    {"F11", "Empire Status: budget and settings"},
    {"F12", "Finish your turn"},
}};

constexpr std::array<Hotkey, 14> kSelectionKeys{{
    {"Space, Ctrl+N", "Select the next ship"},
    {"Ctrl+B", "Go back to the previous ship"},
    {"Ctrl+F / Ctrl+D", "Cycle through fleets (forward / back)"},
    {"Ctrl+C / Ctrl+X", "Cycle through colonies (forward / back)"},
    {"Shift+click", "Tag a ship in the ship list"},
    {"Shift+A / Shift+C", "Tag all / clear tagged ships"},
    {"Ctrl+L", "Show or hide movement lines"},
    {"Ctrl+S", "Sound on or off"},
    {"Ctrl+H", "Show the tutorial or scenario text"},
    {"Ctrl+P", "Replay movement (simultaneous games)"},
    {"Ctrl+O", "Rewind the movement replay"},
    {"Ctrl+I", "Step the replay by one day"},
    {"Ctrl+U", "Replay following the selected ship"},
    {"Esc", "Cancel a location pick, clear the selection"},
}};

constexpr std::array<Hotkey, 34> kOrderKeys{{
    {"M", "Move to a sector"},
    {"Ctrl+0..9", "Move to waypoint 0..9"},
    {"Alt+0..9", "Set waypoint 0..9 at the selected sector"},
    {"W", "Warp through a warp point"},
    {"A", "Attack a target in the sector"},
    {"C", "Colonize a planet"},
    {"S", "Resupply at the nearest depot"},
    {"R", "Repair at the nearest yard"},
    {"Del, Backspace", "Clear orders"},
    {"F", "Fleet transfer"},
    {"Q", "Construction queue"},
    {"T", "Cargo transfer"},
    {"U", "Launch or recover units"},
    {"L / D", "Load / drop cargo at a location"},
    {"I / O", "Launch / recover units at a location"},
    {"Y", "Sentry"},
    {"E", "Explore"},
    {"P", "Patrol between picked points"},
    {"K", "Repeat orders on or off"},
    {"B", "Stellar manipulation"},
    {"V", "View orders"},
    {"G", "Scrap, analyze or mothball"},
    {"H", "Fleet formation and strategy"},
    {"N", "Rename"},
    {"J", "Jettison cargo"},
    {"Z / X", "Cloak / decloak"},
    {"Ctrl+M", "Sweep mines"},
    {"Ctrl+T / Ctrl+R", "Tag / untag a minefield"},
    {"Ctrl+A", "Abandon the planet"},
    {"Ctrl+V", "Convert resources"},
    {"Left click", "Select; in lists, act on the row"},
    {"Right click", "Report on the object or list row"},
    {"Right click (galaxy)", "Open the galaxy map"},
    {"Enter", "End the turn (or finish a patrol route)"},
}};

struct Entry {
    ItemRef ref;
    std::string name;
    std::string group;
    std::string right;
};

std::string squash(std::string_view in) {
    std::string out;
    for (char c : in)
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

class HelpScreen final : public Screen {
public:
    explicit HelpScreen(const ScreenArgs& args) {
        const std::string want = squash(args.text);
        if (want == "weapons" || want == "weaponsreport") {
            weapons_ = true;
            mount_ = args.index;
            return;
        }
        for (size_t i = 0; i < kTabCount; ++i)
            if (squash(kTabLabels[i]) == want) {
                tab_ = static_cast<HelpTab>(i);
                if (args.index >= 0) {
                    pendingSelect_ = static_cast<uint32_t>(args.index);
                    scrollToSelection_ = true;
                }
            }
    }

    bool draw(UiContext& ui) override {
        const bool keep = drawDialog(ui);
        popup_.draw(ui);
        return keep;
    }

private:
    bool drawDialog(UiContext& ui) {
        Dialog d(ui, weapons_ ? "Weapons Report###help" : "Help###help", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        if (weapons_) weaponsReport(ui);
        else if (tab_ == HelpTab::Hotkeys) hotkeys(ui);
        else topics(ui);
        d.beginButtons();
        if (weapons_) weaponButtons(ui, d);
        else topicButtons(ui, d);
        d.close();
        return d.keepOpen();
    }

    // ---- Topics ---------------------------------------------------------------------------

    std::vector<Entry> entries(UiContext& ui) const {
        const game::Rules& r = ui.rules();
        const game::Empire& me = ui.me();
        const auto& data = r.data();
        std::vector<Entry> out;
        switch (tab_) {
            case HelpTab::Components:
                for (uint32_t i = 0; i < data.components.size(); ++i)
                    if (r.componentAvailable(me, i))
                        out.push_back({{Kind::Component, i}, data.components[i].name, data.components[i].generalGroup,
                                       std::format("{} kT", data.components[i].tonnage)});
                break;
            case HelpTab::Facilities:
                for (uint32_t i = 0; i < data.facilities.size(); ++i)
                    if (r.facilityAvailable(me, i)) out.push_back({{Kind::Facility, i}, data.facilities[i].name, data.facilities[i].group, {}});
                break;
            case HelpTab::ShipSizes:
            case HelpTab::UnitSizes:
                for (uint32_t i = 0; i < data.vehicleSizes.size(); ++i) {
                    const auto& h = data.vehicleSizes[i];
                    if (isUnitHull(h.type) != (tab_ == HelpTab::UnitSizes) || !r.hullAvailable(me, i)) continue;
                    out.push_back({{Kind::Hull, i}, h.name, std::string(ruleset::displayName(h.type)), std::format("{} kT", h.tonnage)});
                }
                break;
            case HelpTab::TechAreas:
                for (uint32_t i = 0; i < data.techAreas.size(); ++i) {
                    const ruleset::TechAreaId id{i};
                    if (!r.techVisible(ui.state(), me, id)) continue;
                    out.push_back({{Kind::TechArea, i}, data.techAreas[i].name, data.techAreas[i].group,
                                   std::format("{}/{}", me.techLevel(id), data.techAreas[i].maxLevel)});
                }
                break;
            case HelpTab::Treaties:
                for (uint32_t i = 0; i < static_cast<uint32_t>(game::Treaty::Count); ++i)
                    out.push_back({{Kind::Treaty, i}, std::string(game::displayName(static_cast<game::Treaty>(i))), {}, {}});
                break;
            case HelpTab::IntelProjects:
                for (uint32_t i = 0; i < data.intelProjects.size(); ++i)
                    if (r.meets(me, data.intelProjects[i].requirements))
                        out.push_back({{Kind::IntelProject, i}, data.intelProjects[i].name, data.intelProjects[i].group,
                                       formatNumber(data.intelProjects[i].cost)});
                break;
            case HelpTab::Formations:
                for (uint32_t i = 0; i < data.formations.size(); ++i)
                    out.push_back({{Kind::Formation, i}, data.formations[i].name, {}, std::to_string(data.formations[i].positions.size())});
                break;
            case HelpTab::Hotkeys:
            case HelpTab::Count: break;
        }
        // Groups alphabetically (vehicle sizes by class); data order inside a group, which
        // follows each family's numerals and the hull sizes.
        if (tab_ == HelpTab::ShipSizes || tab_ == HelpTab::UnitSizes)
            std::stable_sort(out.begin(), out.end(), [&](const Entry& a, const Entry& b) {
                return data.vehicleSizes[a.ref.index].type < data.vehicleSizes[b.ref.index].type;
            });
        else std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.group < b.group; });
        return out;
    }

    float iconSize() const {
        switch (tab_) {
            case HelpTab::TechAreas:
            case HelpTab::IntelProjects: return 16.0f;
            case HelpTab::Treaties: return 0.0f;
            case HelpTab::Formations: return 20.0f;
            default: return 28.0f;
        }
    }

    void topics(UiContext& ui) {
        const size_t t = static_cast<size_t>(tab_);
        std::vector<Entry> all = entries(ui);
        std::vector<const Entry*> shown;
        for (const Entry& e : all)
            if (containsNoCase(e.name, filter_.data())) shown.push_back(&e);

        if (pendingSelect_) {
            for (const Entry& e : all)
                if (e.ref.index == *pendingSelect_) selection_[t] = e.ref;
            pendingSelect_.reset();
        }
        const bool selectionShown = std::any_of(shown.begin(), shown.end(), [&](const Entry* e) { return e->ref == selection_[t]; });
        if (!selectionShown && !shown.empty()) selection_[t] = shown.front()->ref;

        // Left: the item list with a find box.
        const float listW = ui.px(290);
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(listW);
        ImGui::InputTextWithHint("##find", "Find...", filter_.data(), filter_.size());
        ImGui::BeginChild("##items", ImVec2(listW, 0), ImGuiChildFlags_Borders);
        std::string lastGroup;
        for (size_t i = 0; i < shown.size(); ++i) {
            const Entry& e = *shown[i];
            if (!e.group.empty() && (i == 0 || e.group != lastGroup)) listHeading(ui, e.group);
            lastGroup = e.group;
            const bool selected = e.ref == selection_[t];
            RowStyle style;
            style.icon = iconSize();
            style.right = e.right;
            const RowResult row = itemRow(ui, static_cast<int>(i), itemIcon(ui, e.ref), e.name, selected, style);
            if (selected && scrollToSelection_) {
                ImGui::SetScrollHereY(0.3f);
                scrollToSelection_ = false;
            }
            if (row.clicked) selection_[t] = e.ref;
            if (row.rightClicked) popup_.open(e.ref);
        }
        scrollToSelection_ = false;
        if (shown.empty()) ImGui::TextColored(kDimText, all.empty() ? "Nothing known yet." : "No match.");
        ImGui::EndChild();
        ImGui::EndGroup();

        // Right: everything about the selected item.
        ImGui::SameLine();
        ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (!shown.empty()) itemDetail(ui, selection_[t], DetailStyle::Full);
        ImGui::EndChild();
    }

    void hotkeys(UiContext& ui) {
        auto table = [&](const char* id, const char* heading, std::span<const Hotkey> keys) {
            listHeading(ui, heading);
            if (ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, ui.px(130));
                ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
                for (const Hotkey& k : keys) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.45f, 1.0f), "%s", k.keys);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(k.action);
                }
                ImGui::EndTable();
            }
        };
        ImGui::TextColored(kDimText, "Keys of the main window. Hovering an order button also shows its key.");
        const float colW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginChild("##keysA", ImVec2(colW, 0), ImGuiChildFlags_Borders);
        table("##windows", "Windows", kWindowKeys);
        ImGui::Spacing();
        table("##selection", "Selection and Display", kSelectionKeys);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##keysB", ImVec2(0, 0), ImGuiChildFlags_Borders);
        table("##orders", "Orders and Mouse", kOrderKeys);
        ImGui::EndChild();
    }

    void topicButtons(UiContext& ui, Dialog& d) {
        ImVec2 first, last;
        for (size_t i = 0; i < kTabCount; ++i) {
            if (lampButton(ui, d, kTabLabels[i], tab_ == static_cast<HelpTab>(i))) {
                tab_ = static_cast<HelpTab>(i);
                scrollToSelection_ = true;
            }
            if (i == 0) first = ImGui::GetItemRectMin();
            last = ImGui::GetItemRectMax();
        }
        ui.tag("help:tabs", first, last);
        d.spacer();
        if (d.button("Weapons Report")) weapons_ = true;
    }

    // ---- Weapons Report ----------------------------------------------------------------------

    void weaponsReport(UiContext& ui) {
        const game::Rules& r = ui.rules();
        std::vector<uint32_t> list = knownWeapons(r, ui.me(), kind_, onlyLatest_);
        if (mount_ >= 0) std::erase_if(list, [&](uint32_t c) { return !mountFitsWeapon(r, static_cast<uint32_t>(mount_), c); });
        const int first = page_ * 10 + 1;

        const std::string mount = mount_ >= 0 ? r.data().weaponMounts[static_cast<size_t>(mount_)].longName : std::string("No mount");
        ImGui::TextColored(kBlueText, "%s", kind_ == WeaponKind::None ? "All weapons" : std::string(weaponKindName(kind_)).c_str());
        ImGui::SameLine();
        ImGui::TextColored(kDimText, "- %zu known, %s, damage at ranges %d to %d. Right-click a weapon for its report.", list.size(),
                           mount.c_str(), first, first + 9);

        constexpr int kColumns = 4 + 10;
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("##weapons", kColumns, flags)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ui.px(26));
        ImGui::TableSetupColumn("Weapon", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ui.px(44));
        ImGui::TableSetupColumn("Rld", ImGuiTableColumnFlags_WidthFixed, ui.px(28));
        std::array<std::string, 10> rangeLabels;
        for (int i = 0; i < 10; ++i) {
            rangeLabels[static_cast<size_t>(i)] = std::to_string(first + i);
            ImGui::TableSetupColumn(rangeLabels[static_cast<size_t>(i)].c_str(), ImGuiTableColumnFlags_WidthFixed, ui.px(30));
        }
        ImGui::TableHeadersRow();
        for (size_t row = 0; row < list.size(); ++row) {
            const uint32_t c = list[row];
            const ruleset::Component& comp = r.component(c);
            const game::DesignEntry entry{c, mount_ >= 0 ? mount_ : -1};
            ImGui::TableNextRow(ImGuiTableRowFlags_None, ui.px(26));
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(row));
            const bool clicked = ImGui::Selectable("##w", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, ui.px(24)));
            if (clicked || ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Component, c, entry.mount});
            ImGui::SameLine(0, 0);
            image(ui, ui.art.component(comp.picture), {24, 24});
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(comp.name.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", game::mounted(r, entry).tonnage);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", comp.weapon.reloadRate);
            for (int i = 0; i < 10; ++i) {
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const int dmg = game::weaponDamageAtRange(r, entry, first + i);
                if (dmg > 0) ImGui::Text("%d", dmg);
                else ImGui::TextColored(kDimText, "-");
            }
        }
        ImGui::EndTable();
        if (list.empty()) ImGui::TextColored(kDimText, "No known weapon matches.");
    }

    void weaponButtons(UiContext& ui, Dialog& d) {
        const game::Rules& r = ui.rules();
        static constexpr std::array<std::pair<WeaponKind, const char*>, 5> kKinds{{{WeaponKind::None, "All"},
                                                                                   {WeaponKind::DirectFire, "Direct Fire"},
                                                                                   {WeaponKind::Seeking, "Seeking"},
                                                                                   {WeaponKind::PointDefense, "Point-Defense"},
                                                                                   {WeaponKind::Warhead, "Warhead"}}};
        for (const auto& [kind, label] : kKinds)
            if (lampButton(ui, d, label, kind_ == kind)) kind_ = kind;
        d.spacer();
        if (lampButton(ui, d, "Weapon Mount", mount_ >= 0, !r.data().weaponMounts.empty())) ImGui::OpenPopup("##mounts");
        if (ImGui::BeginPopup("##mounts")) {
            if (ImGui::Selectable("No mount", mount_ < 0)) mount_ = -1;
            for (uint32_t m = 0; m < r.data().weaponMounts.size(); ++m) {
                if (!r.mountAvailable(ui.me(), m)) continue;
                const auto& wm = r.data().weaponMounts[m];
                if (ImGui::Selectable(std::format("{}##{}", wm.longName, m).c_str(), mount_ == static_cast<int32_t>(m)))
                    mount_ = static_cast<int32_t>(m);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Damage %d%%, size %d%%, cost %d%%, range %+d, to hit %+d%%", wm.damagePercent, wm.tonnagePercent,
                                      wm.costPercent, wm.rangeModifier, wm.toHitModifier);
            }
            ImGui::EndPopup();
        }
        if (lampButton(ui, d, "Only Latest", onlyLatest_)) onlyLatest_ = !onlyLatest_;
        if (lampButton(ui, d, "Dmg 1-10", page_ == 0)) page_ = 0;
        if (lampButton(ui, d, "Dmg 11-20", page_ == 1)) page_ = 1;
        d.spacer();
        if (d.button("Help Topics")) weapons_ = false;
    }

    HelpTab tab_ = HelpTab::Components;
    std::array<ItemRef, kTabCount> selection_{};
    std::optional<uint32_t> pendingSelect_;
    bool scrollToSelection_ = false;
    std::array<char, 64> filter_{};
    ItemReportPopup popup_;

    bool weapons_ = false;
    WeaponKind kind_ = WeaponKind::None;
    int32_t mount_ = -1;
    bool onlyLatest_ = false;
    int page_ = 0;
};

} // namespace

std::unique_ptr<Screen> makeHelp(const ScreenArgs& args) { return std::make_unique<HelpScreen>(args); }

} // namespace opense4::client::classic
