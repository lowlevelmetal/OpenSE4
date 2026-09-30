#include "client/classic/screens/item_reports.hpp"

#include "game/design.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace opense4::client::classic {

namespace {

using Kind = ItemRef::Kind;

// Treaties in our own words (docs/spec/05 §3.2), indexed by game::Treaty.
constexpr std::array<const char*, static_cast<size_t>(game::Treaty::Count)> kTreatyText{{
    "Open hostilities. Ships and planets of both empires fight wherever they meet.",
    "A cold peace: both empires agree to stay apart. Populations dislike it.",
    "The empires know each other but have agreed to nothing, so their ships still fight on contact.",
    "The first treaty that ends combat. Ships of both empires may share a sector.",
    "The subject pays part of its resources to its master every turn and may hold no other treaty. "
    "The master learns the subject's new designs and technology.",
    "The protected empire pays part of its resources to its protector, whose protection rests on its honour.",
    "Opens resource trade: each side earns a slowly growing share of what the other produces.",
    "Everything a trade alliance gives, plus a share of the other side's research.",
    "Everything above, plus resupply for ships at the ally's supply depots.",
    "The closest bond: adds a share of intelligence points, shared sight and star charts, and copies of enemy designs the partner scans.",
}};

void title(UiContext& ui, std::string_view text, float scale = 1.15f) {
    ImGui::PushFont(ui.fonts.bold, ImGui::GetFontSize() * scale);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

void wrapped(std::string_view text, ImVec4 color = kDimText) {
    if (text.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

float valueColumn(DetailStyle st) { return st == DetailStyle::Compact ? 80.0f : 100.0f; }
float pictureSize(DetailStyle st) { return st == DetailStyle::Full ? 128.0f : st == DetailStyle::Report ? 96.0f : 40.0f; }

// Label and value; inside a group (next to a picture) the value column is relative to the group.
void row(UiContext& ui, const char* label, const std::string& value, DetailStyle st) {
    ImGui::TextColored(kBlueText, "%s", label);
    ImGui::SameLine(ui.px(valueColumn(st)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

void costRow(UiContext& ui, const game::Resources& cost, DetailStyle st) {
    ImGui::TextColored(kBlueText, "Cost");
    ImGui::SameLine(ui.px(valueColumn(st)));
    resources(ui, cost, st == DetailStyle::Compact);
}

void section(UiContext& ui, const char* text) {
    ImGui::Spacing();
    heading(ui, text);
}

// Picture on the left, then a group for the headline rows; call ImGui::EndGroup() after.
void beginHeader(UiContext& ui, const Sprite& picture, DetailStyle st) {
    const float s = pictureSize(st);
    if (picture) {
        image(ui, picture, {s, s});
        ImGui::SameLine(0, ui.px(10));
    } else if (st != DetailStyle::Compact) {
        // Keep the layout even when the install lacks a picture.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + ui.px(s), p.y + ui.px(s)), IM_COL32(44, 79, 158, 255));
        ImGui::Dummy(ui.size({s, s}));
        ImGui::SameLine(0, ui.px(10));
    }
    ImGui::BeginGroup();
}

void abilityList(UiContext& ui, std::span<const ruleset::Ability> list, int shieldPercent) {
    bool any = false;
    for (const auto& a : list) {
        if (game::parseAbilityKind(a.type) == game::AbilityKind::AITag) continue;
        if (!any) section(ui, "Abilities");
        any = true;
        ImGui::Bullet();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(abilityText(a, shieldPercent).c_str());
        ImGui::PopTextWrapPos();
    }
}

std::string targetsText(const std::vector<std::string>& targets) {
    static constexpr std::array<std::pair<const char*, const char*>, 8> kWords{{{"Ftr", "Fighters"},
                                                                                {"Sat", "Satellites"},
                                                                                {"Drone", "Drones"},
                                                                                {"Mine", "Mines"},
                                                                                {"Trp", "Troops"},
                                                                                {"Troop", "Troops"},
                                                                                {"WeapPlat", "Weapon Platforms"},
                                                                                {"Seekers", "Seekers"}}};
    std::string out;
    for (const std::string& t : targets) {
        std::string word = t;
        for (const auto& [abbr, full] : kWords)
            if (t == abbr) word = full;
        if (!out.empty()) out += ", ";
        out += word;
    }
    return out.empty() ? std::string("-") : out;
}

void damageTable(UiContext& ui, const game::DesignEntry& entry) {
    const game::Rules& r = ui.rules();
    const int maxRange = std::min(20, game::weaponMaxRange(r, entry));
    if (maxRange <= 0) {
        ImGui::TextColored(kDimText, "No damage at any range");
        return;
    }
    constexpr int kPerRow = 10;
    for (int start = 1; start <= maxRange; start += kPerRow) {
        const int n = std::min(kPerRow, maxRange - start + 1);
        ImGui::PushID(start);
        if (ImGui::BeginTable("##dmg", n, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoHostExtendX)) {
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            for (int i = 0; i < n; ++i) {
                ImGui::TableNextColumn();
                ImGui::TextColored(kBlueText, "%d", start + i);
            }
            ImGui::TableNextRow();
            for (int i = 0; i < n; ++i) {
                ImGui::TableNextColumn();
                const int dmg = game::weaponDamageAtRange(r, entry, start + i);
                if (dmg > 0) ImGui::Text("%d", dmg);
                else ImGui::TextColored(kDimText, "-");
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}

void componentDetail(UiContext& ui, const ItemRef& item, DetailStyle st) {
    const game::Rules& r = ui.rules();
    const ruleset::Component& c = r.component(item.index);
    const game::DesignEntry entry{item.index, item.mount};
    const game::MountedComponent m = game::mounted(r, entry);
    Sprite pic = st == DetailStyle::Compact ? ui.art.component(c.picture) : ui.art.componentPortrait(c.picture);
    if (!pic) pic = ui.art.component(c.picture);
    beginHeader(ui, pic, st);
    title(ui, c.name);
    if (st != DetailStyle::Compact) {
        row(ui, "Group", c.generalGroup.empty() ? std::string("-") : c.generalGroup, st);
        if (item.mount >= 0) row(ui, "Mount", r.data().weaponMounts[static_cast<size_t>(item.mount)].longName, st);
        row(ui, "Size", std::format("{} kT", m.tonnage), st);
        row(ui, "Structure", std::to_string(m.structure), st);
        if (m.supplyUsed > 0) row(ui, "Supply Use", std::to_string(m.supplyUsed), st);
    }
    ImGui::EndGroup();
    if (st == DetailStyle::Compact) {
        if (item.mount >= 0) row(ui, "Mount", r.data().weaponMounts[static_cast<size_t>(item.mount)].longName, st);
        row(ui, "Size", std::format("{} kT", m.tonnage), st);
        row(ui, "Structure", std::to_string(m.structure), st);
        if (m.supplyUsed > 0) row(ui, "Supply Use", std::to_string(m.supplyUsed), st);
    }
    costRow(ui, m.cost, st);
    row(ui, "Vehicles", vehicleTypesText(c), st);
    if (c.maxPerVehicle > 0) row(ui, "Limit", std::format("{} per vehicle", c.maxPerVehicle), st);
    if (st != DetailStyle::Compact) row(ui, "Requires", requirementsText(r, c.requirements), st);
    if (c.isWeapon()) {
        section(ui, "Weapon");
        row(ui, "Type", std::string(weaponKindName(c.weapon.kind)), st);
        row(ui, "Targets", targetsText(c.weapon.targets), st);
        if (!c.weapon.damageType.empty()) row(ui, "Damage", c.weapon.damageType, st);
        row(ui, "Reload", std::format("{} turn{}", c.weapon.reloadRate, c.weapon.reloadRate == 1 ? "" : "s"), st);
        if (m.toHitModifier != 0) row(ui, "To Hit", std::format("{:+}%", m.toHitModifier), st);
        if (c.weapon.kind == ruleset::WeaponKind::Seeking && c.weapon.seekerSpeed > 0)
            row(ui, "Seeker", std::format("speed {}, resists {}", c.weapon.seekerSpeed, c.weapon.seekerDamageResistance), st);
        ImGui::TextColored(kBlueText, "Damage by range");
        damageTable(ui, entry);
    }
    abilityList(ui, c.abilities, m.shieldPercent);
    if (st != DetailStyle::Compact && !c.description.empty()) {
        ImGui::Spacing();
        wrapped(c.description);
    }
}

void facilityDetail(UiContext& ui, const ItemRef& item, DetailStyle st) {
    const game::Rules& r = ui.rules();
    const ruleset::Facility& f = r.facility(item.index);
    Sprite pic = st == DetailStyle::Compact ? ui.art.facility(f.picture) : ui.art.facilityPortrait(f.picture);
    if (!pic) pic = ui.art.facility(f.picture);
    beginHeader(ui, pic, st);
    title(ui, f.name);
    if (!f.group.empty()) row(ui, "Group", f.group, st);
    if (!f.restriction.empty() && f.restriction != "None") row(ui, "Limit", f.restriction, st);
    ImGui::EndGroup();
    costRow(ui, game::Resources::from(f.cost), st);
    row(ui, "Requires", requirementsText(r, f.requirements), st);
    abilityList(ui, f.abilities, 100);
    if (st != DetailStyle::Compact && !f.description.empty()) {
        ImGui::Spacing();
        wrapped(f.description);
    }
}

void hullDetail(UiContext& ui, const ItemRef& item, DetailStyle st) {
    const game::Rules& r = ui.rules();
    const ruleset::VehicleSize& h = r.hull(item.index);
    const std::string& style = ui.me().race.style;
    const Sprite pic = st == DetailStyle::Compact ? ui.art.shipMini(style, h) : ui.art.shipPortrait(style, h);
    beginHeader(ui, pic, st);
    title(ui, h.code.empty() ? h.name : std::format("{} ({})", h.name, h.code));
    row(ui, "Class", std::string(ruleset::displayName(h.type)), st);
    row(ui, "Tonnage", std::format("{} kT", h.tonnage), st);
    ImGui::EndGroup();
    costRow(ui, game::Resources::from(h.cost), st);
    if (st != DetailStyle::Compact) row(ui, "Requires", requirementsText(r, h.requirements), st);

    section(ui, "Design Rules");
    std::vector<std::string> rules;
    if (h.mustHaveBridge) rules.emplace_back("Needs a bridge");
    if (!h.canHaveAuxControl) rules.emplace_back("No auxiliary control");
    if (h.minLifeSupport > 0) rules.push_back(std::format("Life support: at least {}", h.minLifeSupport));
    if (h.minCrewQuarters > 0) rules.push_back(std::format("Crew quarters: at least {}", h.minCrewQuarters));
    if (h.usesEngines && h.maxEngines > 0) {
        rules.push_back(std::format("Engines: at most {}", h.maxEngines));
        if (h.enginesPerMove > 1) rules.push_back(std::format("{} engines per movement point", h.enginesPerMove));
    } else {
        rules.emplace_back("Cannot carry engines");
    }
    if (h.maxPercentFighterBays > 0) rules.push_back(std::format("Fighter bays: at least {}% of the hull", h.maxPercentFighterBays));
    if (h.maxPercentColonyModules > 0) rules.push_back(std::format("Colony modules: at least {}% of the hull", h.maxPercentColonyModules));
    if (h.maxPercentCargo > 0) rules.push_back(std::format("Cargo space: at least {}% of the hull", h.maxPercentCargo));
    for (const std::string& s : rules) ImGui::BulletText("%s", s.c_str());
    abilityList(ui, h.abilities, 100);
    if (st != DetailStyle::Compact && !h.description.empty()) {
        ImGui::Spacing();
        wrapped(h.description);
    }
}

void techDetail(UiContext& ui, const ItemRef& item, DetailStyle st) {
    const game::Rules& r = ui.rules();
    const ruleset::TechAreaId id{item.index};
    const ruleset::TechArea& t = r.tech(id);
    const int level = ui.me().techLevel(id);
    beginHeader(ui, ui.art.icon32(Icon::Research), DetailStyle::Compact);
    title(ui, t.name);
    ImGui::EndGroup();
    if (!t.group.empty()) row(ui, "Group", t.group, st);
    row(ui, "Level", std::format("{} of {}", level, t.maxLevel), st);
    if (level < t.maxLevel)
        row(ui, "Next Level", std::format("{} points", formatNumber(r.techLevelCost(id, level + 1, ui.state().options.techCostGrowth))), st);
    else row(ui, "Next Level", "Fully researched", st);
    row(ui, "Requires", requirementsText(r, t.requirements), st);
    if (!t.description.empty()) {
        ImGui::Spacing();
        wrapped(t.description);
    }
    if (level >= t.maxLevel || st == DetailStyle::Compact) return;

    // What the next level brings (items whose requirements name this level).
    auto needsNext = [&](std::span<const ruleset::TechRequirement> reqs) {
        return std::any_of(reqs.begin(), reqs.end(), [&](const auto& q) { return q.area == id && q.level == level + 1; });
    };
    std::vector<ItemRef> unlocks;
    const auto& data = r.data();
    for (uint32_t i = 0; i < data.components.size(); ++i)
        if (needsNext(data.components[i].requirements)) unlocks.push_back({Kind::Component, i});
    for (uint32_t i = 0; i < data.facilities.size(); ++i)
        if (needsNext(data.facilities[i].requirements)) unlocks.push_back({Kind::Facility, i});
    for (uint32_t i = 0; i < data.vehicleSizes.size(); ++i)
        if (needsNext(data.vehicleSizes[i].requirements)) unlocks.push_back({Kind::Hull, i});
    for (uint32_t i = 0; i < data.techAreas.size(); ++i)
        if (needsNext(data.techAreas[i].requirements)) unlocks.push_back({Kind::TechArea, i});
    for (uint32_t i = 0; i < data.intelProjects.size(); ++i)
        if (needsNext(data.intelProjects[i].requirements)) unlocks.push_back({Kind::IntelProject, i});
    section(ui, std::format("Level {} brings", level + 1).c_str());
    if (unlocks.empty()) ImGui::TextColored(kDimText, "Nothing new");
    for (const ItemRef& u : unlocks) {
        image(ui, itemIcon(ui, u), {20, 20});
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(itemName(ui, u).c_str());
    }
}

void intelDetail(UiContext& ui, const ItemRef& item, DetailStyle st) {
    const game::Rules& r = ui.rules();
    const ruleset::IntelProject& p = r.data().intelProjects[item.index];
    beginHeader(ui, st == DetailStyle::Compact ? ui.art.icon32(Icon::Intelligence) : ui.art.eventPicture(p.sourcePicture), st);
    title(ui, p.name);
    if (!p.group.empty()) row(ui, "Group", p.group, st);
    row(ui, "Cost", std::format("{} points", formatNumber(p.cost)), st);
    row(ui, "Requires", requirementsText(r, p.requirements), st);
    ImGui::EndGroup();
    if (!p.description.empty()) {
        ImGui::Spacing();
        wrapped(p.description);
    }
}

void treatyDetail(UiContext& ui, const ItemRef& item) {
    const auto t = static_cast<game::Treaty>(item.index);
    title(ui, game::displayName(t));
    ImGui::Spacing();
    wrapped(kTreatyText[item.index], ImVec4(0.85f, 0.88f, 0.95f, 1.0f));
    ImGui::Spacing();
    if (game::treatyIsHostile(t)) ImGui::TextColored(kWarnText, "Ships fight on contact.");
    else ImGui::TextColored(kGoodText, "Ships do not fight each other.");
}

char formationMark(size_t i) {
    if (i < 9) return static_cast<char>('1' + i);
    if (i < 9 + 26) return static_cast<char>('A' + (i - 9));
    if (i < 9 + 52) return static_cast<char>('a' + (i - 35));
    return '*';
}

void formationDiagram(UiContext& ui, const ruleset::Formation& f, float cellPx) {
    constexpr int kGrid = 19;
    const float c = ui.px(cellPx);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float size = c * kGrid;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(4, 9, 22, 255));
    for (int i = 0; i <= kGrid; ++i) {
        const float o = float(i) * c;
        const ImU32 col = i % 3 == 0 ? IM_COL32(40, 64, 120, 255) : IM_COL32(22, 36, 72, 255);
        dl->AddLine(ImVec2(p.x + o, p.y), ImVec2(p.x + o, p.y + size), col);
        dl->AddLine(ImVec2(p.x, p.y + o), ImVec2(p.x + size, p.y + o), col);
    }
    const float fontSize = std::min(ImGui::GetFontSize(), c * 0.95f);
    auto mark = [&](int x, int y, ImU32 fill, char label, ImU32 text) {
        if (x < 1 || y < 1 || x > kGrid || y > kGrid) return;
        const ImVec2 a(p.x + float(x - 1) * c + 1, p.y + float(y - 1) * c + 1);
        const ImVec2 b(a.x + c - 2, a.y + c - 2);
        dl->AddRectFilled(a, b, fill);
        const char s[2] = {label, 0};
        const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s);
        dl->AddText(ImGui::GetFont(), fontSize, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), text, s);
    };
    for (size_t i = 0; i < f.positions.size(); ++i)
        mark(f.positions[i].x, f.positions[i].y, IM_COL32(40, 80, 170, 255), formationMark(i), IM_COL32(220, 230, 255, 255));
    mark(f.leader.x, f.leader.y, IM_COL32(230, 190, 50, 255), 'L', IM_COL32(20, 20, 20, 255));
    ImGui::Dummy(ImVec2(size, size));
}

void formationDetail(UiContext& ui, const ItemRef& item, DetailStyle st) {
    const ruleset::Formation& f = ui.rules().data().formations[item.index];
    title(ui, f.name);
    row(ui, "Positions", std::to_string(f.positions.size()), st);
    if (!f.leader.designType.empty() && f.leader.designType != "Any") row(ui, "Leader", f.leader.designType, st);
    ImGui::Spacing();
    formationDiagram(ui, f, st == DetailStyle::Full ? 13.0f : st == DetailStyle::Report ? 11.0f : 8.0f);
    ImGui::TextColored(kDimText, "L = leader; the fleet faces up.");
    if (!f.description.empty()) {
        ImGui::Spacing();
        wrapped(f.description);
    }
}

bool validItem(const UiContext& ui, const ItemRef& item) {
    const auto& d = ui.rules().data();
    switch (item.kind) {
        case Kind::Component: return item.index < d.components.size();
        case Kind::Facility: return item.index < d.facilities.size();
        case Kind::Hull: return item.index < d.vehicleSizes.size();
        case Kind::TechArea: return item.index < d.techAreas.size();
        case Kind::IntelProject: return item.index < d.intelProjects.size();
        case Kind::Treaty: return item.index < static_cast<uint32_t>(game::Treaty::Count);
        case Kind::Formation: return item.index < d.formations.size();
        case Kind::None: break;
    }
    return false;
}

} // namespace

// ---- Items ----------------------------------------------------------------------------------

void itemDetail(UiContext& ui, const ItemRef& item, DetailStyle st) {
    if (!validItem(ui, item)) {
        ImGui::TextColored(kDimText, "Nothing selected");
        return;
    }
    ImGui::PushID(static_cast<int>(item.kind));
    ImGui::PushID(static_cast<int>(item.index));
    switch (item.kind) {
        case Kind::Component: componentDetail(ui, item, st); break;
        case Kind::Facility: facilityDetail(ui, item, st); break;
        case Kind::Hull: hullDetail(ui, item, st); break;
        case Kind::TechArea: techDetail(ui, item, st); break;
        case Kind::IntelProject: intelDetail(ui, item, st); break;
        case Kind::Treaty: treatyDetail(ui, item); break;
        case Kind::Formation: formationDetail(ui, item, st); break;
        case Kind::None: break;
    }
    ImGui::PopID();
    ImGui::PopID();
}

Sprite itemIcon(UiContext& ui, const ItemRef& item) {
    if (!validItem(ui, item)) return {};
    const game::Rules& r = ui.rules();
    switch (item.kind) {
        case Kind::Component: return ui.art.component(r.component(item.index).picture);
        case Kind::Facility: return ui.art.facility(r.facility(item.index).picture);
        case Kind::Hull: return ui.art.shipMini(ui.me().race.style, r.hull(item.index));
        case Kind::TechArea: return ui.art.icon16(Icon::Research);
        case Kind::IntelProject: return ui.art.icon16(Icon::Intelligence);
        case Kind::Formation: return ui.art.groupMini(ui.me().race.style, "Fleet");
        case Kind::Treaty:
        case Kind::None: break;
    }
    return {};
}

std::string itemName(const UiContext& ui, const ItemRef& item) {
    if (!validItem(ui, item)) return {};
    const game::Rules& r = ui.rules();
    switch (item.kind) {
        case Kind::Component: {
            const std::string mount = mountLabel(r, item.mount);
            return mount.empty() ? r.component(item.index).name : std::format("{} {}", mount, r.component(item.index).name);
        }
        case Kind::Facility: return r.facility(item.index).name;
        case Kind::Hull: return r.hull(item.index).name;
        case Kind::TechArea: return r.tech(ruleset::TechAreaId{item.index}).name;
        case Kind::IntelProject: return r.data().intelProjects[item.index].name;
        case Kind::Treaty: return std::string(game::displayName(static_cast<game::Treaty>(item.index)));
        case Kind::Formation: return r.data().formations[item.index].name;
        case Kind::None: break;
    }
    return {};
}

void ItemReportPopup::draw(UiContext& ui) {
    ImGui::PushFont(ui.fonts.regular, 14.0f * ui.k());
    if (request_) {
        ImGui::OpenPopup("##itemreport");
        request_ = false;
    }
    ImGui::SetNextWindowPos(ui.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ui.size({340, 60}), ui.size({340, 720}));
    if (ImGui::BeginPopup("##itemreport", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        itemDetail(ui, item_, DetailStyle::Report);
        ImGui::Spacing();
        ImGui::TextColored(kDimText, "Click to close");
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopFont();
}

// ---- Widgets --------------------------------------------------------------------------------

void drawSprite(const Sprite& s, ImVec2 min, ImVec2 max, ImU32 tint) {
    if (!s) return;
    ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), min, max, ImVec2(s.uv.min.x, s.uv.min.y),
                                         ImVec2(s.uv.max.x, s.uv.max.y), tint);
}

bool lampButton(UiContext& ui, Dialog& d, const char* label, bool lit, bool enabled) {
    const bool clicked = d.button(label, enabled, lit);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const float s = ui.px(11);
    const ImVec2 p(a.x + ui.px(7), (a.y + b.y - s) * 0.5f);
    // Indicator lamps in the General picture: green (lit) and grey (off).
    const Sprite lamp = ui.art.region("Pictures/Game/General.bmp", lit ? 190 : 216, 0, 13, 13);
    const ImU32 tint = enabled ? (lit ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255)) : IM_COL32(90, 90, 90, 255);
    if (lamp) drawSprite(lamp, p, ImVec2(p.x + s, p.y + s), tint);
    else
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.5f,
                                                    lit ? IM_COL32(60, 220, 80, 255) : IM_COL32(90, 90, 100, 255));
    return clicked;
}

RowResult itemRow(UiContext& ui, int id, const Sprite& icon, std::string_view text, bool selected, const RowStyle& style) {
    ImGui::PushID(id);
    const float lineH = ImGui::GetTextLineHeight();
    const bool twoLines = !style.sub.empty() || style.cost;
    const float h = std::max(ui.px(style.icon), twoLines ? lineH * 2.0f : lineH) + ui.px(4);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    RowResult res;
    res.clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, h));
    res.hovered = ImGui::IsItemHovered();
    res.rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    res.doubleClicked = res.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float is = ui.px(style.icon);
    const ImVec2 ip(p.x + ui.px(2), p.y + (h - is) * 0.5f);
    if (icon) drawSprite(icon, ip, ImVec2(ip.x + is, ip.y + is));
    if (!style.badge.empty()) {
        const ImVec2 ts = ImGui::CalcTextSize(style.badge.data(), style.badge.data() + style.badge.size());
        const ImVec2 bp(ip.x + is - ts.x - 1, ip.y + is - ts.y);
        dl->AddRectFilled(ImVec2(bp.x - 1, bp.y), ImVec2(bp.x + ts.x + 1, bp.y + ts.y), IM_COL32(0, 0, 0, 170));
        dl->AddText(bp, IM_COL32(255, 220, 60, 255), style.badge.data(), style.badge.data() + style.badge.size());
    }
    const float tx = ip.x + (icon || style.icon > 0 ? is + ui.px(6) : 0.0f);
    float clipRight = rmax.x - ui.px(4);
    if (!style.right.empty()) {
        const ImVec2 ts = ImGui::CalcTextSize(style.right.data(), style.right.data() + style.right.size());
        const float rx = rmax.x - ts.x - ui.px(6);
        dl->AddText(ImVec2(rx, p.y + (h - lineH) * 0.5f), ImGui::GetColorU32(kDimText), style.right.data(),
                    style.right.data() + style.right.size());
        clipRight = rx - ui.px(6);
    }
    dl->PushClipRect(rmin, ImVec2(std::max(rmin.x, clipRight), rmax.y), true);
    if (!twoLines) {
        dl->AddText(ImVec2(tx, p.y + (h - lineH) * 0.5f), style.color, text.data(), text.data() + text.size());
    } else {
        const float y0 = p.y + h * 0.5f - lineH;
        dl->AddText(ImVec2(tx, y0), style.color, text.data(), text.data() + text.size());
        float x = tx;
        if (!style.sub.empty()) {
            dl->AddText(ImVec2(x, y0 + lineH), ImGui::GetColorU32(kDimText), style.sub.data(), style.sub.data() + style.sub.size());
            x += ImGui::CalcTextSize(style.sub.data(), style.sub.data() + style.sub.size()).x + ui.px(8);
        }
        if (style.cost) {
            static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
            const float s = ui.px(12);
            for (size_t i = 0; i < 3; ++i) {
                if (style.cost->v[i] == 0) continue;
                const float iy = y0 + lineH + (lineH - s) * 0.5f;
                drawSprite(ui.art.icon16(kIcons[i]), ImVec2(x, iy), ImVec2(x + s, iy + s));
                x += s + ui.px(2);
                const std::string n = formatNumber(style.cost->v[i]);
                dl->AddText(ImVec2(x, y0 + lineH), ImGui::GetColorU32(kDimText), n.c_str());
                x += ImGui::CalcTextSize(n.c_str()).x + ui.px(6);
            }
        }
    }
    dl->PopClipRect();
    ImGui::PopID();
    return res;
}

void listHeading(UiContext& ui, std::string_view text) {
    ImGui::Dummy(ImVec2(0, ui.px(2)));
    ImGui::PushFont(ui.fonts.bold, ImGui::GetFontSize());
    ImGui::TextColored(kBlueText, "%.*s", static_cast<int>(text.size()), text.data());
    ImGui::PopFont();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ui.px(8), b.y),
                                        IM_COL32(44, 79, 158, 255));
}

// ---- Text -------------------------------------------------------------------------------------

std::string_view weaponKindName(ruleset::WeaponKind k) {
    switch (k) {
        case ruleset::WeaponKind::DirectFire: return "Direct Fire";
        case ruleset::WeaponKind::Seeking: return "Seeking";
        case ruleset::WeaponKind::Warhead: return "Warhead";
        case ruleset::WeaponKind::PointDefense: return "Point-Defense";
        case ruleset::WeaponKind::None: break;
    }
    return "None";
}

std::string vehicleTypesText(const ruleset::Component& c) {
    if (!c.vehicleDescription.empty()) return c.vehicleDescription;
    std::string out;
    int n = 0;
    for (size_t t = 0; t < static_cast<size_t>(ruleset::VehicleType::Count); ++t) {
        const auto type = static_cast<ruleset::VehicleType>(t);
        if (!(c.vehicles & ruleset::maskOf(type))) continue;
        if (!out.empty()) out += ", ";
        out += ruleset::displayName(type);
        ++n;
    }
    if (n == static_cast<int>(ruleset::VehicleType::Count)) return "All";
    return out.empty() ? std::string("None") : out;
}

std::string requirementsText(const game::Rules& r, std::span<const ruleset::TechRequirement> reqs) {
    std::string out;
    for (const auto& q : reqs) {
        if (!q.area.valid() || q.area.index() >= r.data().techAreas.size()) continue;
        if (!out.empty()) out += ", ";
        out += std::format("{} {}", r.tech(q.area).name, q.level);
    }
    return out.empty() ? std::string("Nothing") : out;
}

std::string abilityText(const ruleset::Ability& a, int shieldPercent) {
    if (!a.description.empty()) {
        std::string text = a.description;
        static constexpr std::string_view kShield = "[%ShieldPointsGenerated]";
        for (size_t at = text.find(kShield); at != std::string::npos; at = text.find(kShield, at))
            text.replace(at, kShield.size(), std::to_string(a.number1() * shieldPercent / 100));
        return text;
    }
    std::string out = a.type;
    const auto meaningful = [](const std::string& v) { return !v.empty() && v != "0"; };
    if (meaningful(a.value1) && meaningful(a.value2)) out += std::format(" ({}, {})", a.value1, a.value2);
    else if (meaningful(a.value1)) out += std::format(" ({})", a.value1);
    return out;
}

std::string mountLabel(const game::Rules& r, int32_t mount) {
    if (mount < 0 || static_cast<size_t>(mount) >= r.data().weaponMounts.size()) return {};
    const auto& m = r.data().weaponMounts[static_cast<size_t>(mount)];
    return m.shortName.empty() ? m.longName : m.shortName;
}

bool containsNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [&](char a, char b) { return lower(a) == lower(b); }) !=
           haystack.end();
}

} // namespace opense4::client::classic
