#include "client/classic/reports.hpp"

#include "client/script/items.hpp"

#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/status_icons.hpp"

#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

const ImVec4 kDim = kDimText;

// Reading text: the report's body scrolls, so the Text size setting enlarges it.
void wrapped(UiContext& ui, const std::string& text) {
    const ReadingText reading(ui.painter());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "%s", text.c_str());
    ImGui::PopTextWrapPos();
}

void title(UiContext& ui, const std::string& text) {
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopFont();
}

void cargoList(UiContext& ui, const game::Cargo& c, int64_t capacity) {
    const game::GameState& s = ui.state();
    labelValue(ui, "Capacity", std::format("{} / {} kT", game::cargoSpaceUsed(ui.rules(), s, c), capacity));
    if (c.empty()) {
        ImGui::TextColored(kDim, "Empty");
        return;
    }
    for (const auto& p : c.population) {
        const std::string& race = p.race.valid() ? s.empire(p.race).race.name : std::string("Unknown");
        image(ui, ui.art.populationMini(p.race.valid() ? s.empire(p.race).race.style : ""), {20, 20});
        ImGui::SameLine();
        ImGui::Text("%s population: %sM", race.c_str(), formatNumber(p.millions).c_str());
    }
    for (const auto& u : c.units) {
        const game::Design& d = s.design(u.design);
        ImGui::Text("%d x %s", u.count, d.name.c_str());
    }
}

void abilityList(const std::vector<game::ParsedAbility>& list) {
    if (list.empty()) {
        ImGui::TextColored(kDim, "No special abilities");
        return;
    }
    for (const auto& a : list) {
        if (a.kind == game::AbilityKind::AITag) continue;
        const std::string name = a.kind == game::AbilityKind::Unknown ? a.raw : std::string(game::identifier(a.kind));
        if (a.value1 != 0 || a.value2 != 0) ImGui::BulletText("%s (%lld, %lld)", name.c_str(), static_cast<long long>(a.value1),
                                                              static_cast<long long>(a.value2));
        else if (!a.text1.empty() && a.text1 != "0") ImGui::BulletText("%s (%s)", name.c_str(), a.text1.c_str());
        else ImGui::BulletText("%s", name.c_str());
    }
}

// Status icons (docs/spec/06 §4.4) in a row, in the order they are drawn.
void statusRow(UiContext& ui, const std::vector<int>& cells) {
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, 0);
        image(ui, ui.art.statusIcon(cells[i] + 1), {20, 20});
    }
}

// Draws at fixed places in the current window, in frame pixels from its top-left
// (the report panel's classic layout).
class Pen {
public:
    explicit Pen(UiContext& ui) : ui_(ui), origin_(ImGui::GetWindowPos()), dl_(ImGui::GetWindowDrawList()) {}

    ImVec2 at(float x, float y) const { return {origin_.x + ui_.px(x), origin_.y + ui_.px(y)}; }
    void sprite(const Sprite& s, float x, float y, float w, float h) const {
        if (!s) return;
        const ImVec2 a = at(x, y), b = at(x + w, y + h);
        dl_->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), a, b, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
    }
    // Cut short at the report's right edge (whole under the pointer).
    void text(float x, float y, ImU32 color, std::string_view t) const {
        const TextFit f = fitText(ui_.painter(), ImGui::GetFont(), ImGui::GetFontSize() / ui_.k(), t, ui_.px(kRight - x));
        dl_->AddText(ImGui::GetFont(), f.size, snap(at(x, y)), color, f.text.c_str());
        script::reportFit(f.text, at(x, y), ImVec2(at(x, y).x + f.extent.x, at(x, y).y + f.extent.y), f.extent.x > ui_.px(kRight - x) + 0.5f);
        if (f.cut && ImGui::IsMouseHoveringRect(at(x, y), at(kRight, y + 14)) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            ImGui::SetTooltip("%.*s", int(t.size()), t.data());
    }
    void text(float x, float y, ImVec4 color, std::string_view t) const { text(x, y, ImGui::GetColorU32(color), t); }
    void rightAligned(float x, float y, ImU32 color, std::string_view t) const {
        const float w = ImGui::CalcTextSize(t.data(), t.data() + t.size()).x / ui_.k();
        text(x - w, y, color, t);
    }
    void centered(ImFont* font, float size, float x, float y, ImU32 color, std::string_view t) const {
        ImGui::PushFont(font, ui_.fontPx(size));
        const float w = ImGui::CalcTextSize(t.data(), t.data() + t.size()).x / ui_.k();
        text(x - w * 0.5f, y, color, t);
        ImGui::PopFont();
    }
    void wrapped(ImFont* font, float size, float x, float y, float width, ImU32 color, std::string_view t) const {
        ImGui::PushFont(font, ui_.fontPx(size));
        dl_->AddText(ImGui::GetFont(), ImGui::GetFontSize(), snap(at(x, y)), color, t.data(), t.data() + t.size(), ui_.px(width));
        ImGui::PopFont();
    }
    // Three amounts in the resource colours, each followed by its icon.
    void resourceRow(std::array<int64_t, 3> v, std::array<float, 3> xs, float y, const char* suffix) const {
        static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
        static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
        for (size_t i = 0; i < 3; ++i) {
            const std::string t = std::format("{}{}", v[i], suffix);
            text(xs[i], y, imColor(kColors[i]), t);
            const float w = ImGui::CalcTextSize(t.c_str()).x / ui_.k();
            sprite(ui_.art.icon16(kIcons[i]), xs[i] + w + 1, y - 2, 14, 14);
        }
    }

private:
    static constexpr float kRight = 286.0f;   // the report's right edge
    static ImVec2 snap(ImVec2 p) { return {std::floor(p.x + 0.5f), std::floor(p.y + 0.5f)}; }
    UiContext& ui_;
    ImVec2 origin_;
    ImDrawList* dl_;
};

} // namespace

bool knownVehicle(const UiContext& ui, const game::Vehicle& v) {
    if (v.owner == ui.session.player()) return true;
    const auto& vis = ui.me().knowledge.visibleVehicles;
    return std::find(vis.begin(), vis.end(), v.id) != vis.end();
}

Sprite vehicleMini(UiContext& ui, const game::Vehicle& v) {
    const game::GameState& s = ui.state();
    const std::string& style = v.owner.valid() ? s.empire(v.owner).race.style : std::string{};
    return ui.art.shipMini(style, ui.rules().hull(s.design(v.design).hull));
}

Sprite vehiclePortrait(UiContext& ui, const game::Vehicle& v) {
    const game::GameState& s = ui.state();
    const std::string& style = v.owner.valid() ? s.empire(v.owner).race.style : std::string{};
    return ui.art.shipPortrait(style, ui.rules().hull(s.design(v.design).hull));
}

Sprite objectSprite(UiContext& ui, const game::SpaceObject& o) { return ui.art.planet(ui.rules().data().sectorObjectTypes[o.sectorType].picture); }

std::string objectName(const game::GameState& s, game::ObjectId id, game::EmpireId viewer) {
    if (!id.valid() || id.index() >= s.galaxy.objects.size()) return {};
    const game::SpaceObject& o = s.galaxy.object(id);
    if (o.kind == game::ObjectKind::WarpPoint && viewer.valid()) return game::sight::warpPointName(s, viewer, id);
    return o.name;
}

std::string sectorName(const game::GameState& s, game::Location where, game::EmpireId viewer) {
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return "-";
    for (game::ObjectId id : s.galaxy.system(where.system).objects) {
        const game::SpaceObject& o = s.galaxy.object(id);
        if (o.sector == where.sector && o.kind != game::ObjectKind::Star) return objectName(s, id, viewer);
    }
    return std::format("{} ({}, {})", s.galaxy.system(where.system).name, where.sector.x, where.sector.y);
}

std::string orderText(const game::GameState& s, const game::Order& o, game::EmpireId viewer) {
    using game::OrderKind;
    const std::string name(game::displayName(o.kind));
    switch (o.kind) {
        case OrderKind::MoveTo: return std::format("{} {}", name, sectorName(s, o.location, viewer));
        case OrderKind::Warp:
        case OrderKind::Colonize:
            return o.object.valid() && o.object.index() < s.galaxy.objects.size() ? std::format("{} {}", name, objectName(s, o.object, viewer))
                                                                                    : name;
        case OrderKind::Attack:
            if (const game::Vehicle* t = s.vehicle(o.vehicle)) return std::format("{} {}", name, t->name);
            return name;
        case OrderKind::MoveToWaypoint: return std::format("{} {}", name, o.amount);
        default: return name;
    }
}

std::string ordersSummary(const game::GameState& s, const game::Vehicle& v, game::EmpireId viewer) {
    if (v.orders.empty()) return "No orders";
    std::string out = orderText(s, v.orders.front(), viewer);
    if (v.orders.size() > 1) out += std::format(" (+{})", v.orders.size() - 1);
    return out;
}

std::string groupDesigns(const game::GameState& s, const game::Vehicle& v, size_t shown) {
    if (v.mixed.empty()) {
        const std::string& name = s.design(v.design).name;
        return v.count > 1 ? std::format("{} x{}", name, v.count) : name;
    }
    // A unit group that mixes designs: each design with its units.
    std::string out;
    for (size_t i = 0; i < v.mixed.size() && i < shown; ++i)
        out += std::format("{}{} x{}", i ? ", " : "", s.design(v.mixed[i].design).name, v.mixed[i].count);
    if (v.mixed.size() > shown) out += std::format(" and {} more designs", v.mixed.size() - shown);
    return out;
}

std::string vehicleSummary(const UiContext& ui, const game::Vehicle& v) {
    const game::GameState& s = ui.state();
    std::string out = groupDesigns(s, v, 2);
    if (v.owner != ui.session.player() && v.owner.valid()) out += " (" + s.empire(v.owner).name + ")";
    return out;
}

ReportTab reportTabs(UiContext& ui, ReportTab current, bool planet, bool cargo) {
    // The image tabs of TabBtns.bmp: 72×30 cells; columns Detail, Comps, Cargo, Ability, Facil, Descr, Race, Tech;
    // rows normal, hover, selected, (unused), disabled.
    struct TabCell {
        ReportTab tab;
        int column;
        const char* label;
    };
    const std::array<TabCell, 4> tabs{{{ReportTab::Detail, 0, "Detail"},
                                       {planet ? ReportTab::Facilities : ReportTab::Components, planet ? 4 : 1, planet ? "Facil" : "Comps"},
                                       {ReportTab::Cargo, 2, "Cargo"},
                                       {ReportTab::Abilities, 3, "Ability"}}};
    ReportTab chosen = current;
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].tab == ReportTab::Cargo && !cargo) continue;
        if (i > 0) ImGui::SameLine(0, 0);
        ImGui::PushID(int(i));
        if (reportTab(ui, tabs[i].column, tabs[i].label, tabs[i].tab == current)) chosen = tabs[i].tab;
        ImGui::PopID();
    }
    return chosen;
}

bool reportTab(UiContext& ui, int column, const char* label, bool selected) {
    const bool clicked = ImGui::InvisibleButton("tab", ui.size({72, 30}));
    script::reportItem(label);   // input scripts find a tab by its label
    const int row = selected ? 2 : ImGui::IsItemHovered() ? 1 : 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (Sprite cell = ui.art.region("Pictures/Game/Buttons/TabBtns.bmp", column * 72, row * 30, 72, 30, false))
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(cell.tex.value)), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), {cell.uv.min.x, cell.uv.min.y},
                     {cell.uv.max.x, cell.uv.max.y});
    else
        drawFitted(ui.painter(), dl, ui.fonts.bold, kTitleSize, ImGui::GetItemRectMin(), ui.px(72), imColor(selected ? 0xffffff : palette::kButton), label, 0.5f,
                   ui.px(30));
    return clicked;
}

namespace {

// A human player who opens the report of a foreign vehicle its long-range
// scanners reach learns the designs the report dates (spec 05 §8 "Design
// knowledge", open question 43): the command is given once a turn, when it
// would date one; in a simultaneous game never for a unit group, whose
// report does not reach the host.
void noteForeignReport(UiContext& ui, const game::Vehicle& v) {
    const game::GameState& s = ui.state();
    const game::EmpireId me = ui.session.player();
    if (!me.valid() || me.index() >= s.empires.size() || s.empire(me).kind != game::PlayerKind::Human) return;
    if (ui.session.waitingForOthers() || (ui.session.turnBased() && !ui.session.myTurn())) return;  // no orders now
    if (!game::sight::scannerReaches(ui.rules(), s, me, v)) return;
    if (s.options.simultaneous && game::isUnitType(game::vehicleType(ui.rules(), s, v))) return;
    for (game::DesignId d : game::sight::reportDesigns(ui.rules(), s, v))
        if (s.design(d).owner != me && game::designSeenTurn(s.empire(me).knowledge, d) != std::optional<uint32_t>(s.turn)) {
            ui.session.issue(game::cmd::OpenVehicleReport{v.id});
            return;
        }
}

} // namespace

void vehicleReport(UiContext& ui, const game::Vehicle& v, ReportTab tab) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::Design& d = s.design(v.design);
    const bool own = v.owner == ui.session.player();
    if (!own && v.owner.valid()) {
        const game::VehicleId id = v.id;
        noteForeignReport(ui, v);
        if (!s.vehicle(id)) return;  // a turn-based command may change the game
    }
    if (Sprite flag = ui.art.flag(v.owner.valid() ? s.empire(v.owner).race.style : "")) {
        image(ui, flag, {26, 18});
        ImGui::SameLine();
    }
    title(ui, v.name);
    switch (tab) {
        case ReportTab::Detail: {
            image(ui, vehiclePortrait(ui, v), {110, 110});
            ImGui::SameLine();
            ImGui::BeginGroup();
            labelValue(ui, "Class", v.mixed.empty() ? d.name : std::format("{} designs", v.mixed.size()), 70);
            labelValue(ui, "Size", r.hull(d.hull).name, 70);
            if (v.owner.valid() && !own) labelValue(ui, "Owner", s.empire(v.owner).name, 70);
            if (v.count > 1) labelValue(ui, "Units", std::to_string(v.count), 70);
            // A group that mixes designs: how many units of each (spec 03 §12).
            for (const game::UnitStack& st : v.mixed)
                labelValue(ui, "", std::format("{} x{} ({})", s.design(st.design).name, st.count, r.hull(s.design(st.design).hull).name), 70);
            if (v.status != game::VehicleStatus::Normal)
                labelValue(ui, "Status", v.status == game::VehicleStatus::Mothballed ? "Mothballed" : "Cloaked", 70);
            ImGui::EndGroup();
            // The status icons under the portrait, for own vehicles only.
            if (own) statusRow(ui, vehicleStatusCells(r, s, v));
            const int structure = game::vehicleStructure(r, s, v);
            const int damage = std::min(structure, game::vehicleDamageTaken(s, v));
            labelValue(ui, "Movement", std::format("{} / {}", v.movement, game::vehicleMaxMovement(r, s, v)));
            labelValue(ui, "Damage", std::format("{} / {} ({}%)", damage, structure, structure > 0 ? damage * 100 / structure : 0));
            if (own) {
                labelValue(ui, "Supplies", game::vehicleHasUnlimitedSupply(r, s, v)
                                               ? std::string("Endless")
                                               : std::format("{} / {}", formatNumber(v.supply), formatNumber(game::vehicleSupplyCapacity(r, s, v))));
                labelValue(ui, "Experience", game::combat::experienceLabel(v.experience, v.experienceTenths));
                if (const game::Fleet* f = s.fleet(v.fleet)) labelValue(ui, "Fleet", f->name);
                labelValue(ui, "Location", sectorName(s, v.location, ui.session.player()));
                // A ship or base with a working space yard: its queue's first item and time (spec 06 §7 Q48).
                if (workingVehicleYard(r, s, v) || !v.queue.items.empty()) {
                    const game::cmd::QueueTarget target{{}, v.id};
                    labelValue(ui, "Under Construction", underConstructionText(r, s, v.queue), 140);
                    labelValue(ui, "Time Remaining",
                               timeRemainingText(r, s, v.owner, target, v.queue, game::economy::constructionRate(r, s, v.owner, target)), 140);
                }
                heading(ui, "Orders");
                if (v.orders.empty()) ImGui::TextColored(kDim, "None");
                for (const auto& o : v.orders) ImGui::BulletText("%s", orderText(s, o, ui.session.player()).c_str());
                if (v.repeatOrders) ImGui::TextColored(kDim, "(repeating)");
            }
            break;
        }
        case ReportTab::Components:
        case ReportTab::Facilities: {
            // A group that mixes designs lists each design's parts (its units are whole).
            for (const game::UnitStack& st : v.mixed) {
                const game::Design& sd = s.design(st.design);
                heading(ui, std::format("{} x{}", sd.name, st.count).c_str());
                for (const game::DesignEntry& e : sd.entries) {
                    const auto& c = r.component(e.component);
                    image(ui, ui.art.component(c.picture), {24, 24});
                    ImGui::SameLine();
                    ImGui::TextUnformatted((e.mount >= 0 ? r.data().weaponMounts[static_cast<size_t>(e.mount)].shortName + " " + c.name : c.name).c_str());
                }
            }
            for (size_t i = 0; i < d.entries.size() && v.mixed.empty(); ++i) {
                const auto& c = r.component(d.entries[i].component);
                const bool intact = game::entryIntact(r, s, v, i);
                image(ui, ui.art.component(c.picture), {24, 24}, intact ? Color{1, 1, 1, 1} : Color{1, 0.3f, 0.3f, 0.8f});
                ImGui::SameLine();
                std::string label = c.name;
                if (d.entries[i].mount >= 0) label = r.data().weaponMounts[static_cast<size_t>(d.entries[i].mount)].shortName + " " + label;
                if (intact) ImGui::TextUnformatted(label.c_str());
                else ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s (destroyed)", label.c_str());
            }
            break;
        }
        case ReportTab::Cargo:
            if (own) cargoList(ui, v.cargo, game::vehicleCargoCapacity(r, s, v));
            else ImGui::TextColored(kDim, "Unknown");
            break;
        case ReportTab::Abilities: abilityList(game::vehicleAbilities(r, s, v)); break;
    }
}

void fleetReport(UiContext& ui, const game::Fleet& f) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    title(ui, f.name);
    if (f.owner == ui.session.player()) statusRow(ui, fleetStatusCells(s, f));
    int mp = 1 << 30;
    int64_t supply = 0, capacity = 0;
    bool endless = !f.members.empty();
    for (game::VehicleId id : f.members)
        if (const game::Vehicle* v = s.vehicle(id)) {
            mp = std::min(mp, v->movement);
            // Fighter groups and members with unlimited supply are left out (spec 03 §9).
            if (game::vehicleHasUnlimitedSupply(r, s, *v)) continue;
            endless = false;
            if (game::vehicleType(r, s, *v) == ruleset::VehicleType::Fighter) continue;
            supply += v->supply;
            capacity += game::vehicleSupplyCapacity(r, s, *v);
        }
    if (f.members.empty()) mp = 0;
    labelValue(ui, "Movement", std::to_string(mp));
    labelValue(ui, "Supplies", endless ? std::string("Endless") : std::format("{} / {}", formatNumber(supply), formatNumber(capacity)));
    labelValue(ui, "Experience", game::combat::experienceLabel(f.experience, f.experienceTenths));
    if (f.formation < r.data().formations.size()) labelValue(ui, "Formation", r.data().formations[f.formation].name);
    const auto& strategies = s.empire(f.owner).strategies;
    if (f.strategy < strategies.size()) labelValue(ui, "Strategy", strategies[f.strategy].name);
    heading(ui, "Ships");
    for (game::VehicleId id : f.members)
        if (const game::Vehicle* v = s.vehicle(id)) {
            image(ui, vehicleMini(ui, *v), {20, 20});
            ImGui::SameLine();
            ImGui::Text("%s%s", v->name.c_str(), id == f.leader ? " (leader)" : "");
        }
    heading(ui, "Orders");
    const std::vector<game::Order>& orders = game::fleetOrders(s, f);  // the copies its members at its location hold
    if (orders.empty()) ImGui::TextColored(kDim, "None");
    for (const auto& o : orders) ImGui::BulletText("%s", orderText(s, o, ui.session.player()).c_str());
}

void planetReport(UiContext& ui, game::ObjectId planet, ReportTab tab) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::SpaceObject& o = s.galaxy.object(planet);
    // The colony only when the player sees it by the detection rule: an
    // unseen one's planet reports as uncolonized (spec 01 §6.9).
    const game::Colony* c = seenColony(r, s, ui.session.player(), planet);
    const bool own = c && c->owner == ui.session.player();
    const ruleset::SectorObjectType& type = r.data().sectorObjectTypes[o.sectorType];
    if (tab != ReportTab::Detail) {
        if (c)
            if (Sprite flag = ui.art.flag(s.empire(c->owner).race.style)) {
                image(ui, flag, {26, 18});
                ImGui::SameLine();
            }
        title(ui, o.name);
    }
    switch (tab) {
        case ReportTab::Detail: {
            // The classic layout, measured on the original (docs/spec/07 §UI): portrait at the
            // top left, name centred over the right column, label lines with the value
            // indented below, the description, then the colony block in two columns.
            Pen pen(ui);
            pen.sprite(ui.art.planetPortrait(type.picture), -5, -11, 128, 128);
            if (c) pen.sprite(ui.art.flag(s.empire(c->owner).race.style), -2, -6, 26, 18);
            pen.centered(ui.fonts.bold, kTitleSize, 153, -4, IM_COL32_WHITE, o.name);
            float y = 15;
            auto field = [&](const char* label, const std::string& value) {
                pen.text(126, y, kLabelBlue, label);
                pen.text(136, y + 15, IM_COL32_WHITE, value);
                y += 30;
            };
            field("Type", std::format("{} - {}", o.surface, o.size));
            field("Atmosphere", o.atmosphere);
            field("Conditions", std::string(game::economy::conditionsName(game::economy::conditionsBand(o.conditions))));
            pen.text(126, y, kLabelBlue, "Value");
            pen.resourceRow({o.value[0], o.value[1], o.value[2]}, {138, 194, 242}, y + 12, "%");
            // Status icons over the foot of the portrait: an own colony's, or the ruins on any planet.
            {
                const std::vector<int> cells = own ? colonyStatusCells(r, s, *c, game::economy::colonyOutput(r, s, *c).connected)
                                                   : planetStatusCells(r, s, ui.session.player(), planet);
                for (size_t i = 0; i < cells.size() && i < 6; ++i) pen.sprite(ui.art.statusIcon(cells[i] + 1), -3 + 20 * float(i), 96, 20, 20);
            }
            pen.wrapped(ui.fonts.small, kSmallSize, -1, 134, 286, imColor(palette::kHeading), type.description);
            if (!c) break;
            float row = 177;
            auto line = [&](const char* label, const std::string& value) {
                pen.text(0, row, kLabelBlue, label);
                pen.text(136, row, IM_COL32_WHITE, value);
                row += 14;
            };
            if (!own) {
                // A foreign colony shows its owner's flag and a Population line,
                // never its owner's name or colony type (spec 01 §6.9).
                line("Population", std::format("{}M", c->totalPopulation()));
                break;
            }
            const game::economy::ColonyOutput out = game::economy::colonyOutput(r, s, *c);
            line("Colony Type", c->colonyType);
            pen.sprite(ui.art.populationMini(s.empire(c->owner).race.style), 229, row - 4, 20, 20);
            line("Population", std::format("{}M/{}M", c->totalPopulation(), game::maxPopulation(r, s, *c)));
            line("Reproduction", std::format("{}% per year", out.reproductionPercent));
            line("Mood", std::string(game::economy::moodName(r, s, *c)));
            row += 8;
            pen.text(0, row, kLabelBlue, "Resource Production");
            pen.resourceRow({out.production.v[0], out.production.v[1], out.production.v[2]}, {139, 196, 249}, row, "");
            row += 14;
            pen.text(0, row, kLabelBlue, "Research");
            pen.rightAligned(163, row, IM_COL32_WHITE, std::to_string(out.research));
            pen.sprite(ui.art.icon16(Icon::Research), 165, row - 2, 14, 14);
            row += 14;
            pen.text(0, row, kLabelBlue, "Intelligence");
            pen.rightAligned(163, row, IM_COL32_WHITE, std::to_string(out.intelligence));
            pen.sprite(ui.art.icon16(Icon::Intelligence), 165, row - 2, 14, 14);
            row += 22;
            // The first item and its time (spec 06 §7 Q48).
            const game::cmd::QueueTarget target{c->planet, {}};
            line("Under Construction", underConstructionText(r, s, c->queue));
            line("Time Remaining",
                 timeRemainingText(r, s, c->owner, target, c->queue, game::economy::constructionRate(r, s, c->owner, target)));
            break;
        }
        case ReportTab::Facilities:
        case ReportTab::Components:
            if (!c) {
                ImGui::TextColored(kDim, "Not colonized");
                break;
            }
            labelValue(ui, "Facilities", std::format("{} / {}", c->facilities.size(), game::facilitySlots(r, s, *c)));
            for (uint32_t f : c->facilities) {
                image(ui, ui.art.facility(r.facility(f).picture), {24, 24});
                ImGui::SameLine();
                ImGui::TextUnformatted(r.facility(f).name.c_str());
            }
            break;
        case ReportTab::Cargo:
            if (own) cargoList(ui, c->cargo, game::colonyCargoCapacity(r, s, *c));
            else ImGui::TextColored(kDim, c ? "Unknown" : "Not colonized");
            break;
        case ReportTab::Abilities: {
            std::vector<game::ParsedAbility> list;
            if (own) list = game::colonyAbilities(r, s, *c);
            else
                for (const auto& a : o.abilities) list.push_back(game::parseAbility(a));
            abilityList(list);
            break;
        }
    }
}

void systemReport(UiContext& ui, game::SystemId sysId) {
    const game::GameState& s = ui.state();
    const game::StarSystem& sys = s.galaxy.system(sysId);
    const ruleset::SystemType& type = ui.rules().data().systemTypes[sys.type.index()];
    if (!ui.me().hasExplored(sysId)) {
        title(ui, sys.name);
        ImGui::TextColored(kDim, "Unexplored");
        return;
    }
    // The original's places (spec 06 §1.4, confirmed: binary), in the report's
    // coordinates (Pen: 4 px left and 8 px up of the spec's): the type's 128x128
    // picture (Systems/<Background Bitmap>, §5.3) at the top left; "<Name>
    // System" in the Button face, right-aligned at the top, its box from x 120;
    // the type's description in grey from y 140, a box 42 px tall; the
    // system's abilities from y 197. Ours: the type and the location as label
    // lines beside the picture. Every text wraps: what does not fit the panel
    // scrolls with the lists' arrow column, from the description down.
    Pen pen(ui);
    const Painter p = ui.painter();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    auto at = [&](float x, float y) { return ImVec2(origin.x + ui.px(x), origin.y + ui.px(y)); };
    if (const Sprite picture = ui.art.systemPicture(type.backgroundBitmap)) pen.sprite(picture, -5, -11, 128, 128);
    ImFont* button = ui.fonts.bold ? ui.fonts.bold : ImGui::GetFont();
    ImFont* body = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
    ImFont* small = ui.fonts.small ? ui.fonts.small : body;
    drawFitted(p, dl, button, kTitleSize, at(116, -4 + kTitleLead), ui.px(282 - 116), IM_COL32_WHITE, sys.name + " System", 1.0f);
    float y = 15;
    for (const auto& [label, value] : {std::pair<const char*, std::string>{"System Type", type.name},
                                       std::pair<const char*, std::string>{"Location", std::format("{}, {}", sys.position.x, sys.position.y)}}) {
        drawFitted(p, dl, body, kTextSize, at(126, y), ui.px(286 - 126), ImGui::ColorConvertFloat4ToU32(kLabelBlue), label, 0.0f, ui.px(15));
        drawFitted(p, dl, body, kTextSize, at(136, y + 15), ui.px(286 - 136), IM_COL32_WHITE, value, 0.0f, ui.px(15));
        script::reportText(label, at(126, y), at(286, y + 30));
        y += 30;
    }
    std::vector<std::string> abilities;
    for (const auto& a : sys.abilities) abilities.push_back(a.description.empty() ? a.type : a.description);
    // Reading text, at the Text size setting's size. Does it all fit the original's places?
    const float panelBottom = ImGui::GetWindowHeight() / ui.k() - 2;
    const float descTop = 132, abilitiesTop = 189, left = -1, width = 287;
    const ImU32 grey = imColor(palette::kSecondary);
    const float descH = type.description.empty() ? 0.0f
                                                 : small->CalcTextSizeA(p.textPx(kSmallSize), FLT_MAX, ui.px(width), type.description.c_str()).y / ui.k();
    const float bullet = ImGui::GetFontSize() / ui.k() + ImGui::GetStyle().FramePadding.x * 2 / ui.k();
    float abilitiesH = 0;
    for (const std::string& a : abilities)
        abilitiesH += body->CalcTextSizeA(p.textPx(kTextSize), FLT_MAX, ui.px(width - bullet), a.c_str()).y / ui.k() + 2;
    const float listTop = std::max(abilitiesTop, descTop + descH + 4);
    if (descH <= 42 && listTop + abilitiesH <= panelBottom) {
        if (!type.description.empty())
            dl->AddText(small, p.textPx(kSmallSize), at(left, descTop + kSmallLead), grey, type.description.c_str(), nullptr, ui.px(width));
        ImGui::SetCursorScreenPos(at(left, listTop));
        ImGui::PushFont(body, p.textPx(kTextSize));
        for (const std::string& a : abilities) wrappedBullet(a);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 0));   // the cursor was placed: an item makes the room
        return;
    }
    // Too much for the panel: the description and the abilities scroll together.
    ImGui::SetCursorScreenPos(at(left - 1, descTop));
    const Painter list = [&] {
        Painter lp = p;
        lp.tagger = &ui;
        return lp;
    }();
    beginList(list, "##system-info", ImVec2(ui.px(width + 1), ui.px(std::max(40.0f, panelBottom - descTop))), kListLineStep, ImGuiChildFlags_None, false);
    if (!type.description.empty()) {
        ImGui::PushFont(small, p.textPx(kSmallSize));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(grey), "%s", type.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    ImGui::PushFont(body, p.textPx(kTextSize));
    for (const std::string& a : abilities) wrappedBullet(a);
    ImGui::PopFont();
    endList(list);
}

void objectReport(UiContext& ui, game::ObjectId id, const game::GameState* state) {
    const game::GameState& s = state ? *state : ui.state();
    const game::SpaceObject& o = s.galaxy.object(id);
    title(ui, objectName(s, id, ui.session.player()));
    image(ui, ui.art.planetPortrait(ui.rules().data().sectorObjectTypes[o.sectorType].picture), {96, 96});
    switch (o.kind) {
        case game::ObjectKind::Star:
        case game::ObjectKind::DestroyedStar:
            labelValue(ui, "Star", std::format("{}, {}", o.starColor, o.size));
            labelValue(ui, "Age", o.starAge);
            labelValue(ui, "Luminosity", o.starLuminosity);
            break;
        case game::ObjectKind::WarpPoint: {
            // The destination shows once the viewer has explored it (docs/spec/01 §5.4, §8).
            const game::SystemId dest = o.destination.valid() ? s.galaxy.object(o.destination).system : game::SystemId{};
            const bool known = dest.valid() && (s.options.omnipresent || ui.me().hasExplored(dest));
            labelValue(ui, "Destination", known ? s.galaxy.system(dest).name : std::string("Unknown"));
            break;
        }
        default: labelValue(ui, "Kind", std::string(game::displayName(o.kind))); break;
    }
    wrapped(ui, ui.rules().data().sectorObjectTypes[o.sectorType].description);
    const ReadingText reading(ui.painter());
    for (const auto& a : o.abilities) wrappedBullet(a.description.empty() ? a.type : a.description);
}

} // namespace opense4::client::classic
