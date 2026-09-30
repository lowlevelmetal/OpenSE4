#include "client/classic/reports.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

const ImVec4 kDim{0.55f, 0.62f, 0.72f, 1.0f};

void wrapped(const std::string& text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "%s", text.c_str());
    ImGui::PopTextWrapPos();
}

void title(UiContext& ui, const std::string& text) {
    ImGui::PushFont(ui.fonts.bold, ImGui::GetFontSize() * 1.15f);
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
    const std::array<std::pair<ReportTab, const char*>, 4> tabs{{{ReportTab::Detail, "Detail"},
                                                                 {planet ? ReportTab::Facilities : ReportTab::Components, planet ? "Facil" : "Comps"},
                                                                 {ReportTab::Cargo, "Cargo"},
                                                                 {ReportTab::Abilities, "Ability"}}};
    ReportTab chosen = current;
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, ui.px(2));
        const bool active = tabs[i].first == current;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.36f, 0.75f, 1));
        if (ImGui::Button(tabs[i].second, ImVec2(ui.px(64), ui.px(22)))) chosen = tabs[i].first;
        if (active) ImGui::PopStyleColor();
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
    if (c) {
        if (Sprite flag = ui.art.flag(s.empire(c->owner).race.style)) {
            image(ui, flag, {26, 18});
            ImGui::SameLine();
        }
    }
    title(ui, o.name);
    switch (tab) {
        case ReportTab::Detail: {
            image(ui, ui.art.planetPortrait(r.data().sectorObjectTypes[o.sectorType].picture), {96, 96});
            ImGui::SameLine();
            ImGui::BeginGroup();
            labelValue(ui, "Type", std::format("{} - {}", o.surface, o.size), 80);
            labelValue(ui, "Atmosphere", o.atmosphere, 80);
            labelValue(ui, "Conditions", std::format("{}%", o.conditions), 80);
            labelValue(ui, "Value", std::format("{}% / {}% / {}%", o.value[0], o.value[1], o.value[2]), 80);
            ImGui::EndGroup();
            if (!c) {
                wrapped(r.data().sectorObjectTypes[o.sectorType].description);
                break;
            }
            labelValue(ui, "Owner", s.empire(c->owner).name);
            labelValue(ui, "Colony Type", c->colonyType);
            labelValue(ui, "Population", std::format("{}M / {}M", formatNumber(c->totalPopulation()), formatNumber(game::maxPopulation(r, s, *c))));
            if (own) {
                const game::economy::ColonyOutput out = game::economy::colonyOutput(r, s, *c);
                labelValue(ui, "Reproduction", std::format("{}% per year", out.reproductionPercent));
                labelValue(ui, "Mood", std::string(game::displayName(game::moodFromAnger(c->anger))));
                ImGui::TextColored(ImVec4(0.44f, 0.61f, 1.0f, 1.0f), "Production");
                ImGui::SameLine(ui.px(110));
                resources(ui, out.production, true);
                labelValue(ui, "Research", formatNumber(out.research));
                labelValue(ui, "Intelligence", formatNumber(out.intelligence));
                if (!c->queue.items.empty()) {
                    const game::QueueItem& q = c->queue.items.front();
                    const std::string what = q.kind == game::QueueItem::Kind::Facility ? r.facility(q.facility).name
                                             : q.kind == game::QueueItem::Kind::Upgrade ? "Upgrade " + r.facility(q.facility).name
                                                                                        : s.design(q.design).name;
                    labelValue(ui, "Constructing", what);
                } else {
                    labelValue(ui, "Constructing", "Nothing");
                }
            }
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
