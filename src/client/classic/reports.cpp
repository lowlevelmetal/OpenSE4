#include "client/classic/reports.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

const ImVec4 kDim = kDimText;

void wrapped(const std::string& text) {
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
    void text(float x, float y, ImU32 color, std::string_view t) const {
        dl_->AddText(ImGui::GetFont(), ImGui::GetFontSize(), snap(at(x, y)), color, t.data(), t.data() + t.size());
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

std::string sectorName(const game::GameState& s, game::Location where) {
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return "-";
    for (game::ObjectId id : s.galaxy.system(where.system).objects) {
        const game::SpaceObject& o = s.galaxy.object(id);
        if (o.sector == where.sector && o.kind != game::ObjectKind::Star) return o.name;
    }
    return std::format("{} ({}, {})", s.galaxy.system(where.system).name, where.sector.x, where.sector.y);
}

std::string orderText(const game::GameState& s, const game::Order& o) {
    using game::OrderKind;
    const std::string name(game::displayName(o.kind));
    switch (o.kind) {
        case OrderKind::MoveTo: return std::format("{} {}", name, sectorName(s, o.location));
        case OrderKind::Warp:
        case OrderKind::Colonize:
            return o.object.valid() && o.object.index() < s.galaxy.objects.size() ? std::format("{} {}", name, s.galaxy.object(o.object).name)
                                                                                    : name;
        case OrderKind::Attack:
            if (const game::Vehicle* t = s.vehicle(o.vehicle)) return std::format("{} {}", name, t->name);
            return name;
        case OrderKind::MoveToWaypoint: return std::format("{} {}", name, o.amount);
        default: return name;
    }
}

std::string ordersSummary(const game::GameState& s, const game::Vehicle& v) {
    if (v.orders.empty()) return "No orders";
    std::string out = orderText(s, v.orders.front());
    if (v.orders.size() > 1) out += std::format(" (+{})", v.orders.size() - 1);
    return out;
}

std::string vehicleSummary(const UiContext& ui, const game::Vehicle& v) {
    const game::GameState& s = ui.state();
    const game::Design& d = s.design(v.design);
    std::string out = v.count > 1 ? std::format("{} x{}", d.name, v.count) : d.name;
    if (v.owner != ui.session.player() && v.owner.valid()) out += " (" + s.empire(v.owner).name + ")";
    return out;
}

ReportTab reportTabs(UiContext& ui, ReportTab current, bool planet) {
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, 0);
        ImGui::PushID(int(i));
        const bool clicked = ImGui::InvisibleButton("tab", ui.size({72, 30}));
        ImGui::PopID();
        const bool selected = tabs[i].tab == current;
        const int row = selected ? 2 : ImGui::IsItemHovered() ? 1 : 0;
        if (Sprite cell = ui.art.region("Pictures/Game/Buttons/TabBtns.bmp", tabs[i].column * 72, row * 30, 72, 30, false))
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(cell.tex.value)), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                         {cell.uv.min.x, cell.uv.min.y}, {cell.uv.max.x, cell.uv.max.y});
        else
            dl->AddText(ImGui::GetItemRectMin(), imColor(selected ? 0xffffff : palette::kButton), tabs[i].label);
        if (clicked) chosen = tabs[i].tab;
    }
    return chosen;
}

void vehicleReport(UiContext& ui, const game::Vehicle& v, ReportTab tab) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::Design& d = s.design(v.design);
    const bool own = v.owner == ui.session.player();
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
            labelValue(ui, "Class", d.name, 70);
            labelValue(ui, "Size", r.hull(d.hull).name, 70);
            if (v.owner.valid() && !own) labelValue(ui, "Owner", s.empire(v.owner).name, 70);
            if (v.count > 1) labelValue(ui, "Units", std::to_string(v.count), 70);
            if (v.status != game::VehicleStatus::Normal)
                labelValue(ui, "Status", v.status == game::VehicleStatus::Mothballed ? "Mothballed" : "Cloaked", 70);
            ImGui::EndGroup();
            const int structure = game::vehicleStructure(r, s, v);
            const int damage = std::min(structure, game::vehicleDamageTaken(s, v));
            labelValue(ui, "Movement", std::format("{} / {}", v.movement, game::vehicleMaxMovement(r, s, v)));
            labelValue(ui, "Damage", std::format("{} / {} ({}%)", damage, structure, structure > 0 ? damage * 100 / structure : 0));
            if (own) {
                labelValue(ui, "Supplies", std::format("{} / {}", formatNumber(v.supply), formatNumber(game::vehicleSupplyCapacity(r, s, v))));
                labelValue(ui, "Experience", std::format("{}%", v.experience));
                if (const game::Fleet* f = s.fleet(v.fleet)) labelValue(ui, "Fleet", f->name);
                labelValue(ui, "Location", sectorName(s, v.location));
                heading(ui, "Orders");
                if (v.orders.empty()) ImGui::TextColored(kDim, "None");
                for (const auto& o : v.orders) ImGui::BulletText("%s", orderText(s, o).c_str());
                if (v.repeatOrders) ImGui::TextColored(kDim, "(repeating)");
            }
            break;
        }
        case ReportTab::Components:
        case ReportTab::Facilities: {
            for (size_t i = 0; i < d.entries.size(); ++i) {
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
    int mp = 1 << 30;
    int64_t supply = 0, capacity = 0;
    for (game::VehicleId id : f.members)
        if (const game::Vehicle* v = s.vehicle(id)) {
            mp = std::min(mp, v->movement);
            supply += v->supply;
            capacity += game::vehicleSupplyCapacity(r, s, *v);
        }
    if (f.members.empty()) mp = 0;
    labelValue(ui, "Movement", std::to_string(mp));
    labelValue(ui, "Supplies", std::format("{} / {}", formatNumber(supply), formatNumber(capacity)));
    labelValue(ui, "Experience", std::format("{}%", f.experience));
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
    if (f.orders.empty()) ImGui::TextColored(kDim, "None");
    for (const auto& o : f.orders) ImGui::BulletText("%s", orderText(s, o).c_str());
}

void planetReport(UiContext& ui, game::ObjectId planet, ReportTab tab) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::SpaceObject& o = s.galaxy.object(planet);
    const game::Colony* c = s.colony(planet);
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
            pen.wrapped(ui.fonts.small, kSmallSize, -1, 134, 286, imColor(palette::kHeading), type.description);
            if (!c) break;
            float row = 177;
            auto line = [&](const char* label, const std::string& value) {
                pen.text(0, row, kLabelBlue, label);
                pen.text(136, row, IM_COL32_WHITE, value);
                row += 14;
            };
            if (!own) {
                line("Owner", s.empire(c->owner).name);
                line("Colony Type", c->colonyType);
                line("Population", std::format("{}M", c->totalPopulation()));
                break;
            }
            const game::economy::ColonyOutput out = game::economy::colonyOutput(r, s, *c);
            line("Colony Type", c->colonyType);
            pen.sprite(ui.art.populationMini(s.empire(c->owner).race.style), 229, row - 4, 20, 20);
            line("Population", std::format("{}M/{}M", c->totalPopulation(), game::maxPopulation(r, s, *c)));
            line("Reproduction", std::format("{}% per year", out.reproductionPercent));
            line("Mood", std::string(game::displayName(game::moodFromAnger(c->anger))));
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
            std::string building = "None", remaining;
            if (!c->queue.items.empty()) {
                const game::QueueItem& q = c->queue.items.front();
                building = q.kind == game::QueueItem::Kind::Facility  ? r.facility(q.facility).name
                           : q.kind == game::QueueItem::Kind::Upgrade ? "Upgrade " + r.facility(q.facility).name
                                                                      : s.design(q.design).name;
            }
            line("Under Construction", building);
            line("Time Remaining", remaining);
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
    title(ui, sys.name);
    if (!ui.me().hasExplored(sysId)) {
        ImGui::TextColored(kDim, "Unexplored");
        return;
    }
    labelValue(ui, "System Type", type.name);
    labelValue(ui, "Location", std::format("{}, {}", sys.position.x, sys.position.y));
    wrapped(type.description);
    for (const auto& a : sys.abilities) ImGui::BulletText("%s", a.description.empty() ? a.type.c_str() : a.description.c_str());
}

void objectReport(UiContext& ui, game::ObjectId id) {
    const game::GameState& s = ui.state();
    const game::SpaceObject& o = s.galaxy.object(id);
    title(ui, o.name);
    image(ui, ui.art.planetPortrait(ui.rules().data().sectorObjectTypes[o.sectorType].picture), {96, 96});
    switch (o.kind) {
        case game::ObjectKind::Star:
        case game::ObjectKind::DestroyedStar:
            labelValue(ui, "Star", std::format("{}, {}", o.starColor, o.size));
            labelValue(ui, "Age", o.starAge);
            labelValue(ui, "Luminosity", o.starLuminosity);
            break;
        case game::ObjectKind::WarpPoint: {
            const bool known = o.id.index() < ui.me().knowledge.knownWarpLink.size() && ui.me().knowledge.knownWarpLink[o.id.index()];
            labelValue(ui, "Destination",
                       known && o.destination.valid() ? s.galaxy.system(s.galaxy.object(o.destination).system).name : std::string("Unknown"));
            if (o.oneWay) labelValue(ui, "Note", "One-way");
            break;
        }
        default: labelValue(ui, "Kind", std::string(game::displayName(o.kind))); break;
    }
    wrapped(ui.rules().data().sectorObjectTypes[o.sectorType].description);
    for (const auto& a : o.abilities) ImGui::BulletText("%s", a.description.empty() ? a.type.c_str() : a.description.c_str());
}

} // namespace opense4::client::classic
