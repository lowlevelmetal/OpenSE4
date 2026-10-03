// Help (F1): the in-game encyclopedia (docs/spec/06 §1.2), in the original's
// layout (spec 07 session 5): "Items" over a lamp list of names in
// alphabetical order at (17,58), 271x404, the first chosen when a tab opens;
// "Item Details" over the detail box (296,58)-(573,462). Tabs list what the
// empire knows: researched components, weapon mounts, facilities and vehicle
// sizes, visible tech areas, available intelligence projects, plus the
// treaties, formations and the hotkey groups. The Weapons Report is a second
// mode of the same window: a damage-by-range grid of the known weapons.
// Ours: a Find box in the heading row over the list.
//
// Opening arguments (ScreenArgs::text): a tab name ("components",
// "weapmount", "facilities", "shipsizes", "unitsizes", "techareas", "treaties",
// "intelprojects", "formations", "hotkeys") with `index` selecting an item, or
// "weapons" for the Weapons Report with `index` as the initial weapon mount.

#include "client/app_settings.hpp"
#include "client/classic/data_export.hpp"
#include "client/classic/screens/design_tools.hpp"
#include "client/classic/screens/item_reports.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"

#include "game/design.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <format>
#include <utility>

namespace opense4::client::classic {

namespace {

using Kind = ItemRef::Kind;
using ruleset::WeaponKind;

enum class HelpTab : uint8_t {
    Components, WeapMounts, Facilities, ShipSizes, UnitSizes, TechAreas, Treaties, IntelProjects, Formations, Hotkeys, Count
};
constexpr size_t kTabCount = static_cast<size_t>(HelpTab::Count);
constexpr std::array<const char*, kTabCount> kTabLabels{"Components", "Weap Mount",     "Facilities", "Ship Sizes", "Unit Sizes",
                                                        "Tech Areas", "Treaties", "Intel Projects", "Formations", "Hotkeys"};

// The Hotkeys tab's groups (spec 07 session 5).
constexpr std::array<const char*, 6> kHotkeyGroups{"All Windows", "Main Window - Commands", "Main Window - Orders 1",
                                                   "Main Window - Orders 2", "Main Window - Selection", "Tactical Combat"};

// ---- Hotkeys (docs/spec/06 §3) ---------------------------------------------------------------
// The main window's keys as they are bound now (Settings -> Controls), in our
// own words. An order key works only while its button is lit.

struct Hotkey {
    std::string keys;
    std::string action;
};

std::vector<Hotkey> boundKeys(std::initializer_list<std::string_view> groups) {
    const Bindings& b = appSettings().controls.bindings;
    std::vector<Hotkey> out;
    for (std::string_view group : groups)
        for (const ActionInfo& a : actionInfos()) {
            if (a.group != group) continue;
            std::string keys;
            for (const KeyChord& c : b.chords(a.action))
                if (!c.empty()) keys += (keys.empty() ? "" : ", ") + chordName(c);
            if (keys.empty()) continue;
            out.push_back({std::move(keys), a.label});
        }
    return out;
}

// One row of the item list: a catalogue item, or (`special` from 0) a weapon
// mount or a hotkey group.
struct Entry {
    ItemRef ref;
    int special = -1;
    std::string name;
    bool operator==(const Entry& o) const { return ref == o.ref && special == o.special; }
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
        exportNotes(ui);
        return keep;
    }

private:
    bool drawDialog(UiContext& ui) {
        Dialog d(ui, weapons_ ? "Weapons Report###help" : "Help###help", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent(weapons_ ? 0.0f : 576.0f);
        if (weapons_) weaponsReport(ui);
        else topics(ui, d);
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
                    if (r.componentAvailable(me, i)) out.push_back({{Kind::Component, i}, -1, data.components[i].name});
                break;
            case HelpTab::WeapMounts:
                for (uint32_t i = 0; i < data.weaponMounts.size(); ++i)
                    if (r.mountAvailable(me, i)) out.push_back({{}, int(i), data.weaponMounts[i].longName});
                break;
            case HelpTab::Facilities:
                for (uint32_t i = 0; i < data.facilities.size(); ++i)
                    if (r.facilityAvailable(me, i)) out.push_back({{Kind::Facility, i}, -1, data.facilities[i].name});
                break;
            case HelpTab::ShipSizes:
            case HelpTab::UnitSizes:
                for (uint32_t i = 0; i < data.vehicleSizes.size(); ++i) {
                    const auto& h = data.vehicleSizes[i];
                    if (isUnitHull(h.type) != (tab_ == HelpTab::UnitSizes) || !r.hullAvailable(me, i)) continue;
                    out.push_back({{Kind::Hull, i}, -1, h.name});
                }
                break;
            case HelpTab::TechAreas:
                for (uint32_t i = 0; i < data.techAreas.size(); ++i)
                    if (r.techVisible(ui.state(), me, ruleset::TechAreaId{i})) out.push_back({{Kind::TechArea, i}, -1, data.techAreas[i].name});
                break;
            case HelpTab::Treaties:
                // The nine treaty types, without None.
                for (uint32_t i = 0; i < static_cast<uint32_t>(game::Treaty::Count); ++i)
                    if (static_cast<game::Treaty>(i) != game::Treaty::None)
                        out.push_back({{Kind::Treaty, i}, -1, std::string(game::displayName(static_cast<game::Treaty>(i)))});
                break;
            case HelpTab::IntelProjects:
                for (uint32_t i = 0; i < data.intelProjects.size(); ++i)
                    if (r.meets(me, data.intelProjects[i].requirements)) out.push_back({{Kind::IntelProject, i}, -1, data.intelProjects[i].name});
                break;
            case HelpTab::Formations:
                for (uint32_t i = 0; i < data.formations.size(); ++i) out.push_back({{Kind::Formation, i}, -1, data.formations[i].name});
                break;
            case HelpTab::Hotkeys:
                // The groups in their own order, not sorted.
                for (size_t i = 0; i < kHotkeyGroups.size(); ++i) out.push_back({{}, int(i), kHotkeyGroups[i]});
                return out;
            case HelpTab::Count: break;
        }
        // Only names, in alphabetical order (spec 07 session 5).
        std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
            return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
            });
        });
        return out;
    }

    void topics(UiContext& ui, const Dialog& d) {
        const size_t t = static_cast<size_t>(tab_);
        std::vector<Entry> all = entries(ui);
        std::vector<const Entry*> shown;
        for (const Entry& e : all)
            if (containsNoCase(e.name, filter_.data())) shown.push_back(&e);
        if (pendingSelect_) {
            for (const Entry& e : all)
                if (e.ref.valid() ? e.ref.index == *pendingSelect_ : e.special == int(*pendingSelect_)) selection_[t] = e;
            pendingSelect_.reset();
        }
        const bool selectionShown = std::any_of(shown.begin(), shown.end(), [&](const Entry* e) { return *e == selection_[t]; });
        if (!selectionShown && !shown.empty()) selection_[t] = *shown.front();

        const ImU32 blue = imColor(palette::kLabel);
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {17, 39}, blue, "Items");
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {295, 39}, blue, "Item Details");
        // Ours: the Find box, in the heading row beside "Items".
        ImGui::SetCursorScreenPos(d.at({62, 37}));
        ImGui::SetNextItemWidth(ui.px(226));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui.px(3), 0));
        ImGui::InputTextWithHint("##find", "Find...", filter_.data(), filter_.size());
        ImGui::PopStyleVar();

        // The list: 18 px rows, a lamp (green for the item shown) and the name.
        ImGui::SetCursorScreenPos(d.at({17, 58}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##items", ui.size({271, 404}), kRowH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        const Sprite green = ui.art.region("Pictures/Game/General.bmp", 190, 0, 13, 13);
        const Sprite blueLamp = ui.art.region("Pictures/Game/General.bmp", 177, 0, 13, 13);
        for (size_t i = 0; i < shown.size(); ++i) {
            const Entry& e = *shown[i];
            const bool selected = e == selection_[t];
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 a = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##row", ImVec2(w, ui.px(kRowH)))) selection_[t] = e;
            script::reportItem(e.name);   // input scripts find a row by its name
            if (e.ref.valid() && ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open(e.ref);
            ImGui::PopID();
            if (selected && scrollToSelection_) {
                ImGui::SetScrollHereY(0.3f);
                scrollToSelection_ = false;
            }
            if (const Sprite& lamp = selected ? green : blueLamp) drawSprite(lamp, {a.x + ui.px(3), a.y + ui.px(3)}, {a.x + ui.px(16), a.y + ui.px(16)});
            dl->PushClipRect(a, {a.x + w, a.y + ui.px(kRowH)}, true);
            dl->AddText({a.x + ui.px(21), a.y + ui.px(1 + kTextLead - 2)}, IM_COL32_WHITE, e.name.c_str());
            dl->PopClipRect();
        }
        scrollToSelection_ = false;
        if (shown.empty()) ImGui::TextColored(kDimText, all.empty() ? "Nothing known yet." : "No match.");
        endList(ui);
        ImGui::PopStyleVar(2);

        // The detail box.
        ImGui::SetCursorScreenPos(d.at({296, 58}));
        ImGui::PushStyleColor(ImGuiCol_Border, imColorV(palette::kFrameLight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##detail", ui.size({277, 404}), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (!shown.empty()) detail(ui, selection_[t]);
        ImGui::EndChild();
    }

    // ---- The detail box (spec 07 session 5) ----------------------------------------------------
    // A component or facility: the 128 px picture at its top left, the name in
    // the title font beside it (right-aligned for a vehicle size), a short
    // description in small grey type under the name, then the figures and
    // "Abilities" with a blue dot before each line. Tech areas, treaties and
    // intelligence projects: the name and a description. Formations: the grid.

    void detail(UiContext& ui, const Entry& e) {
        if (tab_ == HelpTab::Hotkeys) {
            hotkeyGroup(ui, e.special);
            return;
        }
        if (tab_ == HelpTab::WeapMounts) {
            mountDetail(ui, e.special);
            return;
        }
        const game::Rules& r = ui.rules();
        switch (e.ref.kind) {
            case Kind::Component: {
                const ruleset::Component& c = r.component(e.ref.index);
                Sprite pic = ui.art.componentPortrait(c.picture);
                if (!pic) pic = ui.art.component(c.picture);
                header(ui, pic, c.name, c.description, false);
                figure(ui, "Cost", {});
                costLine(ui, game::Resources::from(c.cost));
                figure(ui, "Size", std::format("{} kT", c.tonnage));
                figure(ui, "Damage Resistance", std::to_string(c.structure));
                figure(ui, "Supplies Used", std::to_string(c.supplyUsed));
                figure(ui, "Vehicle Types", vehicleTypesText(c));
                abilities(ui, c.abilities);
                break;
            }
            case Kind::Facility: {
                const ruleset::Facility& f = r.facility(e.ref.index);
                Sprite pic = ui.art.facilityPortrait(f.picture);
                if (!pic) pic = ui.art.facility(f.picture);
                header(ui, pic, f.name, f.description, false);
                figure(ui, "Cost", {});
                costLine(ui, game::Resources::from(f.cost));
                abilities(ui, f.abilities);
                break;
            }
            case Kind::Hull: {
                const ruleset::VehicleSize& h = r.hull(e.ref.index);
                header(ui, ui.art.shipPortrait(ui.me().race.style, h), h.name, h.description, true);
                figure(ui, "Cost", {});
                costLine(ui, game::Resources::from(h.cost));
                figure(ui, "Size", std::format("{} kT", h.tonnage));
                figure(ui, "Vehicle Type", std::string(ruleset::displayName(h.type)));
                abilities(ui, h.abilities);
                break;
            }
            case Kind::TechArea: {
                const ruleset::TechArea& a = r.tech(ruleset::TechAreaId{e.ref.index});
                nameAndText(ui, a.name, a.description);
                break;
            }
            case Kind::Treaty:
                nameAndText(ui, e.name, std::string(treatyDescription(static_cast<game::Treaty>(e.ref.index))));
                break;
            case Kind::IntelProject: {
                const ruleset::IntelProject& p = r.data().intelProjects[e.ref.index];
                nameAndText(ui, p.name, p.description);
                break;
            }
            case Kind::Formation:
                ImGui::SetCursorPos(ui.size({4, 4}));
                ImGui::BeginGroup();
                itemDetail(ui, e.ref, DetailStyle::Full);
                ImGui::EndGroup();
                break;
            case Kind::None: break;
        }
    }

    // The picture and the name, with the description beside the picture
    // (a vehicle size's name right-aligned); the figures start under it.
    void header(UiContext& ui, const Sprite& pic, const std::string& name, const std::string& description, bool nameRight) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        if (pic) drawSprite(pic, {o.x + ui.px(2), o.y + ui.px(2)}, {o.x + ui.px(130), o.y + ui.px(130)});
        // The name in the title font; a name too long for one line is
        // word-wrapped (ours: the original's long names are not described).
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        std::vector<std::string> lines;
        {
            std::string line;
            size_t i = 0;
            while (i < name.size()) {
                const size_t j = std::min(name.find(' ', i), name.size());
                const std::string word = name.substr(i, j - i);
                const std::string longer = line.empty() ? word : line + " " + word;
                if (!line.empty() && ImGui::CalcTextSize(longer.c_str()).x > ui.px(140)) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = longer;
                }
                i = j + 1;
            }
            if (!line.empty()) lines.push_back(line);
        }
        float y = 4.0f;
        dl->PushClipRect({o.x + ui.px(134), o.y}, {o.x + ui.px(276), o.y + ui.px(128)}, true);
        for (const std::string& l : lines) {
            const float nw = ImGui::CalcTextSize(l.c_str()).x;
            const float nx = nameRight ? std::max(o.x + ui.px(134), o.x + ui.px(273) - nw) : o.x + ui.px(134);
            dl->AddText({nx, o.y + ui.px(y + kTitleLead)}, IM_COL32_WHITE, l.c_str());
            y += 18.0f;
        }
        dl->PopClipRect();
        ImGui::PopFont();
        ImGui::SetCursorPos(ui.size({134, y + 4}));
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImGui::PushTextWrapPos(ui.px(273));
        ImGui::TextColored(imColorV(palette::kSecondary), "%s", description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        figureY_ = std::max(136.0f, ImGui::GetCursorPosY() / ui.k() + 4.0f);
    }

    void nameAndText(UiContext& ui, const std::string& name, const std::string& text) {
        ImGui::SetCursorPos(ui.size({4, 4}));
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopFont();
        ImGui::SetCursorPosX(ui.px(4));
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImGui::PushTextWrapPos(ui.px(271));
        ImGui::TextColored(imColorV(palette::kSecondary), "%s", text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }

    // A figure: the label in label blue at x 4, the value at x 120.
    void figure(UiContext& ui, const char* label, const std::string& value) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        dl->AddText({o.x + ui.px(4), o.y + ui.px(figureY_ + kTextLead - 3)}, imColor(palette::kLabel), label);
        if (!value.empty()) {
            dl->PushClipRect({o.x + ui.px(120), o.y}, {o.x + ui.px(276), o.y + ui.px(404)}, true);
            dl->AddText({o.x + ui.px(120), o.y + ui.px(figureY_ + kTextLead - 3)}, IM_COL32_WHITE, value.c_str());
            dl->PopClipRect();
        }
        figureY_ += 16.0f;
    }

    // The three amounts of a cost on the line under its label, each with its icon.
    void costLine(UiContext& ui, const game::Resources& cost) {
        static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
        static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        figureY_ -= 16.0f;   // on the label's line, from x 120
        float x = 120.0f;
        for (size_t i = 0; i < 3; ++i) {
            const std::string v = formatNumber(cost.v[i]);
            dl->AddText({o.x + ui.px(x), o.y + ui.px(figureY_ + kTextLead - 3)}, imColor(kColors[i]), v.c_str());
            x += ImGui::CalcTextSize(v.c_str()).x / ui.k() + 1.0f;
            drawSprite(ui.art.icon16(kIcons[i]), {o.x + ui.px(x), o.y + ui.px(figureY_)}, {o.x + ui.px(x + 14), o.y + ui.px(figureY_ + 14)});
            x += 18.0f;
        }
        figureY_ += 16.0f;
    }

    // "Abilities", then a blue dot before each line, "None" without any.
    void abilities(UiContext& ui, std::span<const ruleset::Ability> list) {
        figureY_ += 4.0f;
        figure(ui, "Abilities", {});
        ImGui::SetCursorPos(ui.size({4, figureY_}));
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto line = [&](const std::string& text) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled({at.x + ui.px(5), at.y + ImGui::GetTextLineHeight() * 0.5f}, ui.px(2.5f), imColor(palette::kLabel));
            ImGui::SetCursorPosX(ui.px(14));
            ImGui::PushTextWrapPos(ui.px(273));
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SetCursorPosX(ui.px(4));
        };
        if (list.empty()) line("None");
        for (const ruleset::Ability& a : list) line(abilityText(a));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopFont();
    }

    // A weapon mount: its code, cost, size, damage resistance, supply, the
    // weapon type it takes, damage, to-hit and range modifiers, the vehicle
    // type and the minimum size (spec 07 session 5).
    void mountDetail(UiContext& ui, int index) {
        const auto& mounts = ui.rules().data().weaponMounts;
        if (index < 0 || static_cast<size_t>(index) >= mounts.size()) return;
        const ruleset::WeaponMount& m = mounts[static_cast<size_t>(index)];
        nameAndText(ui, m.longName, m.description);
        figureY_ = ImGui::GetCursorPosY() / ui.k() + 6.0f;
        figure(ui, "Code", m.code);
        figure(ui, "Cost", std::format("{}%", m.costPercent));
        figure(ui, "Size", std::format("{}%", m.tonnagePercent));
        figure(ui, "Damage Resistance", std::format("{}%", m.structurePercent));
        figure(ui, "Supply", std::format("{}%", m.supplyPercent));
        figure(ui, "Weapon Type", m.weaponTypeRequirement.empty() ? std::string("Any") : m.weaponTypeRequirement);
        figure(ui, "Damage", std::format("{}%", m.damagePercent));
        figure(ui, "To Hit", std::format("{:+}%", m.toHitModifier));
        figure(ui, "Range", std::format("{:+}", m.rangeModifier));
        figure(ui, "Vehicle Type", m.vehicleType.empty() ? std::string("Any") : m.vehicleType);
        figure(ui, "Minimum Size", std::format("{} kT", m.minimumVehicleSize));
        ImGui::SetCursorPos(ui.size({4, figureY_}));
        ImGui::Dummy(ImVec2(0, 0));
    }

    // The keys of one hotkey group in two columns, the keys and what they do:
    // the main window's as bound now (Settings, Controls), in our own words.
    void hotkeyGroup(UiContext& ui, int group) {
        std::vector<Hotkey> keys;
        switch (group) {
            case 0:
                keys = {{"Esc, Enter", "Close a window whose bottom button is Close"},
                        {"Esc", "Close a window whose bottom button is Cancel"},
                        {"Y, N", "Answer a Yes/No question (Esc and Enter mean No)"},
                        {"Esc, Enter", "OK in a message box"},
                        {"T, S", "Tactical or Strategic, when a battle asks"}};
                // OpenSE4's lesson panel: its keys work over every window.
                for (Hotkey& k : boundKeys({"Lesson panel"})) keys.push_back(std::move(k));
                break;
            case 1: keys = boundKeys({"Windows"}); break;
            case 2:
            case 3: {
                const std::vector<Hotkey> orders = boundKeys({"Orders"});
                const size_t half = (orders.size() + 1) / 2;
                if (group == 2) keys.assign(orders.begin(), orders.begin() + std::ptrdiff_t(half));
                else {
                    keys.assign(orders.begin() + std::ptrdiff_t(half), orders.end());
                    keys.push_back({"Ctrl+0..9", "Move to waypoint 0..9"});
                    keys.push_back({"Alt+0..9", "Set waypoint 0..9 at the selected sector"});
                    for (Hotkey& k : boundKeys({"Movement log"})) keys.push_back(std::move(k));
                }
                break;
            }
            case 4:
                keys = boundKeys({"Selection", "Display"});
                keys.push_back({"Shift+click", "Tag a ship in the list (a fleet is tagged whole)"});
                keys.push_back({"Right click (galaxy)", "Open the Galaxy Map"});
                break;
            case 5:
                keys = {{"Alt+1..9", "Make the selected ship the leader of that group"},
                        {"Ctrl+1..9", "Make the selected ship a member of that group"},
                        {"Alt+0, Ctrl+0", "Clear the selected ship's group marks"},
                        {"L", "Launch units from the selected ship"},
                        {"T", "Drop troops on the adjacent colony"},
                        {"R", "Ram a ship (then click it)"},
                        {"C", "Capture a ship (then click it)"},
                        {"E", "End the combat turn"},
                        {"Space, Ctrl+N", "Next ship that can move"},
                        {"Ctrl+B", "Previous ship that can move"},
                        {"Ctrl+F, Ctrl+D", "Next, previous ship that can fire"},
                        {"Shift+A, Shift+C", "Select all weapons, clear the weapons"}};
                break;
            default: break;
        }
        ImGui::SetCursorPos(ui.size({4, 4}));
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(group >= 0 && size_t(group) < kHotkeyGroups.size() ? kHotkeyGroups[size_t(group)] : "");
        ImGui::PopFont();
        ImGui::SetCursorPosX(ui.px(4));
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingFixedFit, ImVec2(ui.px(269), 0))) {
            ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, ui.px(96));
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            for (const Hotkey& k : keys) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kLabelBlue, "%s", k.keys.c_str());
                ImGui::PopTextWrapPos();
                ImGui::TableNextColumn();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(k.action.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::PopFont();
            ImGui::EndTable();
        }
    }

    // The original's column: the ten tabs, a gap, Weapons Report (slot 12),
    // Manual (slot 13), Close (spec 07 session 5).
    void topicButtons(UiContext& ui, Dialog& d) {
        ImVec2 first, last;
        for (size_t i = 0; i < kTabCount; ++i) {
            if (lampButton(ui, d, kTabLabels[i], tab_ == static_cast<HelpTab>(i))) {
                if (tab_ != static_cast<HelpTab>(i)) selection_[i] = {};   // the first item, as the tab opens
                tab_ = static_cast<HelpTab>(i);
                scrollToSelection_ = true;
            }
            if (i == 0) first = ImGui::GetItemRectMin();
            last = ImGui::GetItemRectMax();
        }
        ui.tag("help:tabs", first, last);
        d.spacer();
        if (d.button("Weapons Report")) weapons_ = true;
        if (d.button("Manual")) ui.open(ScreenId::Manual);
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
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (!beginListTable(ui, "##weapons", kColumns, flags, ImVec2(0, 0), 26)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(kPicHeading, ImGuiTableColumnFlags_WidthFixed, ui.px(26));
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
        endListTable(ui);
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
        if (d.check("Only Latest", onlyLatest_)) onlyLatest_ = !onlyLatest_;
        if (lampButton(ui, d, "Dmg 1-10", page_ == 0)) page_ = 0;
        if (lampButton(ui, d, "Dmg 11-20", page_ == 1)) page_ = 1;
        d.spacer();
        // With Settings.txt `Allow Export of Weapon And Component Data` TRUE:
        // Weapons.txt, Comps.txt, WeaponFamilies.txt and CompFamilies.txt written
        // to the saves folder, each followed by its "Export Successful" message
        // naming the file (spec 06 §1.9, §7 Q83, confirmed: binary).
        if (exportAllowed(r.data().settings) && d.button("Export")) exportTables(ui);
        if (d.button("Help Topics")) weapons_ = false;
    }

    void exportTables(UiContext& ui) {
        exportQueue_.clear();
        const auto written = writeExportTables(savesDir(), "", weaponAndComponentTables(ui.rules()));
        if (!written) {
            exportQueue_.emplace_back("Export Failed", written.error());
            return;
        }
        for (const std::filesystem::path& file : *written)
            exportQueue_.emplace_back("Export Successful", std::format("Exported to {}.", file.string()));
    }

    // The export's messages, one after another.
    void exportNotes(UiContext& ui) {
        if (exportQueue_.empty()) return;
        const auto& [title, text] = exportQueue_.front();
        const std::string id = title + "###export";
        if (!ImGui::IsPopupOpen(id.c_str())) ImGui::OpenPopup(id.c_str());
        ImGui::SetNextWindowSize(ui.size({420, 0}));
        if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) {
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) {
                ImGui::CloseCurrentPopup();
                exportQueue_.pop_front();
            }
            ImGui::EndPopup();
        }
    }

    static constexpr float kRowH = 18.0f;   // the item list's rows
    HelpTab tab_ = HelpTab::Components;
    std::array<Entry, kTabCount> selection_{};
    float figureY_ = 136.0f;                 // the detail box's next figure line
    std::optional<uint32_t> pendingSelect_;
    bool scrollToSelection_ = false;
    std::array<char, 64> filter_{};
    ItemReportPopup popup_;

    bool weapons_ = false;
    WeaponKind kind_ = WeaponKind::None;
    int32_t mount_ = -1;
    bool onlyLatest_ = false;
    int page_ = 0;
    std::deque<std::pair<std::string, std::string>> exportQueue_;   // title, text
};

} // namespace

std::unique_ptr<Screen> makeHelp(const ScreenArgs& args) { return std::make_unique<HelpScreen>(args); }

} // namespace opense4::client::classic
