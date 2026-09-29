#include "client/ui/hud.hpp"

#include "client/palette.hpp"
#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"

#include <imgui.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace opense4::client {

namespace {

using sim::ResourceType;

constexpr ImVec4 kMineralsColor{0.74f, 0.82f, 0.92f, 1.0f};
constexpr ImVec4 kOrganicsColor{0.48f, 0.86f, 0.48f, 1.0f};
constexpr ImVec4 kRadioactivesColor{0.96f, 0.80f, 0.32f, 1.0f};
constexpr ImVec4 kResearchColor{0.56f, 0.74f, 1.0f, 1.0f};
constexpr ImVec4 kGood{0.50f, 0.92f, 0.60f, 1.0f};
constexpr ImVec4 kBad{1.0f, 0.50f, 0.45f, 1.0f};
constexpr ImVec4 kMuted{0.55f, 0.62f, 0.70f, 1.0f};

ImVec4 resourceColor(ResourceType r) {
    switch (r) {
        case ResourceType::Minerals: return kMineralsColor;
        case ResourceType::Organics: return kOrganicsColor;
        case ResourceType::Radioactives: return kRadioactivesColor;
    }
    return kMineralsColor;
}

ImVec4 toImVec4(Color c) { return ImVec4(c.r, c.g, c.b, c.a); }
ImVec4 empireColor(const sim::GameState& s, sim::EmpireId e) { return toImVec4(palette::empireColor(s.empire(e).color)); }

std::string fmtNum(int64_t v) {
    std::string digits = std::to_string(v < 0 ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return v < 0 ? "-" + out : out;
}

std::string str(std::string_view sv) { return std::string(sv); }

// "120 M  40 O  10 R" with each resource in its color; skips zeros.
void costText(const sim::Resources& cost) {
    bool first = true;
    for (ResourceType r : sim::kAllResources) {
        if (cost[r] == 0) continue;
        if (!first) ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(resourceColor(r), "%s %c", fmtNum(cost[r]).c_str(), sim::displayName(r)[0]);
        first = false;
    }
    if (first) ImGui::TextDisabled("free");
}

std::string costString(const sim::Resources& cost) {
    std::string out;
    for (ResourceType r : sim::kAllResources)
        if (cost[r] != 0) out += std::format("{}{} {}", out.empty() ? "" : ", ", fmtNum(cost[r]), sim::displayName(r));
    return out.empty() ? "free" : out;
}

void heading(ImFont* font, const std::string& text, ImVec4 color = ImVec4(0.92f, 0.96f, 1.0f, 1.0f), float scale = 1.25f) {
    ImGui::PushFont(font, ImGui::GetStyle().FontSizeBase * scale);
    ImGui::TextColored(color, "%s", text.c_str());
    ImGui::PopFont();
}

void labelValue(const char* label, const std::string& value, ImVec4 color = ImVec4(0.86f, 0.91f, 0.96f, 1.0f)) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    ImGui::TextColored(color, "%s", value.c_str());
}

std::string locationText(const sim::GameState& s, sim::Location loc) {
    return std::format("{} ({}, {})", s.system(loc.system).name, loc.sector.x, loc.sector.y);
}

std::string orderText(const sim::GameState& s, const sim::Ship& ship) {
    const int speed = std::max(1, s.statsOf(ship).speed);
    const int turns = static_cast<int>((ship.path.size() + static_cast<size_t>(speed) - 1) / static_cast<size_t>(speed));
    switch (ship.order.type) {
        case sim::OrderType::Move:
            return std::format("Moving to {} - {} move{}, {} turn{}", locationText(s, ship.order.destination), ship.path.size(),
                               ship.path.size() == 1 ? "" : "s", turns, turns == 1 ? "" : "s");
        case sim::OrderType::Colonize:
            if (ship.path.empty()) return std::format("Colonizing {} at the end of this turn", s.planet(ship.order.planet).name);
            return std::format("Colonizing {} - arrives in {} turn{}", s.planet(ship.order.planet).name, turns, turns == 1 ? "" : "s");
        case sim::OrderType::None: break;
    }
    return "Awaiting orders";
}

ImVec4 eventColor(sim::EventKind k) {
    switch (k) {
        case sim::EventKind::Exploration: return ImVec4(0.55f, 0.80f, 1.0f, 1.0f);
        case sim::EventKind::Movement: return ImVec4(0.70f, 0.78f, 0.86f, 1.0f);
        case sim::EventKind::Colonization: return kGood;
        case sim::EventKind::Construction: return ImVec4(0.95f, 0.85f, 0.45f, 1.0f);
        case sim::EventKind::Research: return kResearchColor;
        case sim::EventKind::Combat: return kBad;
        case sim::EventKind::Info: break;
    }
    return ImVec4(0.86f, 0.91f, 0.96f, 1.0f);
}

} // namespace

void Hud::draw(ViewContext& ctx, NavRequest& nav, bool inSystemView) {
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) showHelp = !showHelp;
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) showResearch = !showResearch;
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) showLog = !showLog;
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) showDebug = !showDebug;

    topBar(ctx, nav);
    selectionPanel(ctx, nav, inSystemView);
    if (showResearch) researchWindow(ctx);
    if (showLog) logWindow(ctx, nav);
    if (showHelp) helpWindow();
    newGameDialog(ctx, nav);
    statusToast(ctx);
    if (showDebug) debugOverlay(ctx);
}

void Hud::topBar(ViewContext& ctx, NavRequest& nav) {
    const GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const sim::Empire& me = session.playerEmpire();
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * ctx.uiScale, 7.0f * ctx.uiScale));
    ImGui::Begin("##topbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar(2);

    if (ImGui::Button("Menu")) ImGui::OpenPopup("game_menu");
    if (ImGui::BeginPopup("game_menu")) {
        if (ImGui::MenuItem("New Game...")) openNewGame_ = true;
        if (ImGui::MenuItem("Research", "F2", showResearch)) showResearch = !showResearch;
        if (ImGui::MenuItem("Event Log", "F3", showLog)) showLog = !showLog;
        if (ImGui::MenuItem("Controls", "F1", showHelp)) showHelp = !showHelp;
        if (ImGui::MenuItem("Renderer Info", "F9", showDebug)) showDebug = !showDebug;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) nav.quit = true;
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 14.0f * ctx.uiScale);
    ImGui::PushFont(ctx.fonts.bold, 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(empireColor(s, me.id), "%s", me.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 10.0f * ctx.uiScale);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Turn %u", s.turn);

    for (ResourceType r : sim::kAllResources) {
        ImGui::SameLine(0.0f, 22.0f * ctx.uiScale);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(resourceColor(r), "%s", str(sim::displayName(r)).c_str());
        ImGui::SameLine();
        ImGui::Text("%s", fmtNum(me.stockpile[r]).c_str());
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(kMuted, "(+%s)", fmtNum(me.lastIncome[r]).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Income last turn");
    }
    ImGui::SameLine(0.0f, 22.0f * ctx.uiScale);
    ImGui::TextColored(kResearchColor, "Research");
    ImGui::SameLine();
    ImGui::Text("+%s", fmtNum(me.lastResearch).c_str());

    // Right-aligned buttons.
    const std::string logLabel = session.newEventCount() > 0 ? std::format("Log ({})###log", session.newEventCount()) : "Log###log";
    const ImGuiStyle& style = ImGui::GetStyle();
    auto buttonWidth = [&](const char* label) { return ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f; };
    const size_t idle = session.idleShips().size();
    const std::string idleLabel = std::format("Idle ships: {}###idle", idle);
    const float width = buttonWidth(idleLabel.c_str()) + buttonWidth("Research") + buttonWidth(logLabel.c_str()) +
                        buttonWidth("End Turn  >") + style.ItemSpacing.x * 3.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 20.0f, ImGui::GetWindowWidth() - style.WindowPadding.x - width));
    ImGui::BeginDisabled(idle == 0);
    if (ImGui::Button(idleLabel.c_str())) nav.nextIdleShip = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Select the next ship without orders (Tab)");
    ImGui::SameLine();
    if (ImGui::Button("Research")) showResearch = !showResearch;
    ImGui::SameLine();
    if (ImGui::Button(logLabel.c_str())) showLog = !showLog;
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.42f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.55f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.66f, 0.48f, 1.0f));
    if (ImGui::Button("End Turn  >")) nav.endTurn = true;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("End the turn (Enter)");

    topBarHeight = ImGui::GetWindowHeight();
    ImGui::End();
}

void Hud::selectionPanel(ViewContext& ctx, NavRequest& nav, bool inSystemView) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float top = topBarHeight + 8.0f * ctx.uiScale;
    const float width = 350.0f * ctx.uiScale;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 8.0f * ctx.uiScale, vp->Pos.y + top));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, vp->Size.y - top - 8.0f * ctx.uiScale));
    ImGui::Begin("##selection", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    const Selection sel = ctx.selection;
    switch (sel.kind) {
        case Selection::Kind::System: systemDetails(ctx, nav, sel.system, inSystemView); break;
        case Selection::Kind::Planet: planetDetails(ctx, nav, sel.planet); break;
        case Selection::Kind::Ship: shipDetails(ctx, nav, sel.ship); break;
        case Selection::Kind::WarpPoint: warpPointDetails(ctx, nav, sel.warpPoint); break;
        case Selection::Kind::None: {
            const sim::GameState& s = ctx.session.state();
            const sim::Empire& me = ctx.session.playerEmpire();
            heading(ctx.fonts.bold, me.name, empireColor(s, me.id));
            ImGui::TextDisabled("%s", ctx.session.content().race(me.race).description.c_str());
            ImGui::Separator();
            labelValue("Colonies", std::to_string(sim::colonyCount(s, me.id)));
            labelValue("Ships", std::to_string(sim::shipCount(s, me.id)));
            labelValue("Population", fmtNum(sim::totalPopulation(s, me.id)) + " M");
            const int explored = static_cast<int>(std::count(me.explored.begin(), me.explored.end(), uint8_t{1}));
            labelValue("Explored", std::format("{} / {} systems", explored, s.systems.size()));
            ImGui::Separator();
            if (ImGui::Button("Go to homeworld")) nav.focus = s.planet(me.homeworld).location();
            ImGui::Spacing();
            ImGui::TextWrapped("Select a system, planet or ship to see details. Double-click a system to open its map.");
            break;
        }
    }
    ImGui::End();
}

void Hud::systemDetails(ViewContext& ctx, NavRequest& nav, sim::SystemId id, bool inSystemView) {
    GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const sim::StarSystem& sys = s.system(id);
    heading(ctx.fonts.bold, sys.name);
    ImGui::TextColored(toImVec4(palette::starColor(sys.star)), "%s", str(sim::displayName(sys.star)).c_str());
    if (!inSystemView) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Open Map").x - 8.0f);
        if (ImGui::SmallButton("Open Map")) nav.openSystem = id;
    }
    ImGui::Separator();

    if (!session.explored(id)) {
        ImGui::TextColored(kMuted, "Unexplored.");
        ImGui::TextWrapped("Send a ship here to survey its planets and warp points.");
    } else {
        ImGui::SeparatorText("Planets");
        if (sys.planets.empty()) ImGui::TextDisabled("None");
        for (sim::PlanetId pid : sys.planets) {
            const sim::Planet& p = s.planet(pid);
            ImGui::PushID(static_cast<int>(pid.value));
            const ImVec4 col = p.colony ? empireColor(s, p.colony->owner) : ImVec4(0.86f, 0.91f, 0.96f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            if (ImGui::Selectable(p.name.c_str(), false, ImGuiSelectableFlags_AllowOverlap)) {
                ctx.selection = Selection::ofPlanet(p);
                nav.focus = p.location();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
            ImGui::TextDisabled("%s %s", str(sim::displayName(p.size)).c_str(), str(sim::displayName(p.surface)).c_str());
            ImGui::SameLine(ImGui::GetFontSize() * 14.5f);
            const bool breathable = sim::canBreathe(session.playerEmpire(), p);
            ImGui::TextColored(breathable ? kGood : kMuted, "%s", str(sim::displayName(p.atmosphere)).c_str());
            ImGui::PopID();
        }
        ImGui::SeparatorText("Warp Points");
        for (sim::WarpPointId wid : sys.warpPoints) {
            const sim::WarpPoint& wp = s.warpPoint(wid);
            const sim::SystemId dest = s.warpPoint(wp.exit).system;
            const std::string label = std::format("To {}##wp{}", session.explored(dest) ? s.system(dest).name : "unexplored space", wid.value);
            if (ImGui::Selectable(label.c_str())) {
                ctx.selection = Selection::ofWarpPoint(wp);
                nav.focus = sim::Location{wp.system, wp.sector};
            }
        }
    }

    std::vector<const sim::Ship*> ships;
    for (const sim::Ship* ship : sim::shipsInSystem(s, id))
        if (session.canSee(*ship)) ships.push_back(ship);
    if (!ships.empty()) {
        ImGui::SeparatorText("Ships");
        for (const sim::Ship* ship : ships) {
            ImGui::PushStyleColor(ImGuiCol_Text, empireColor(s, ship->owner));
            if (ImGui::Selectable(std::format("{}##ship{}", ship->name, ship->id.value).c_str())) {
                ctx.selection = Selection::ofShip(*ship);
                nav.focus = ship->location;
            }
            ImGui::PopStyleColor();
        }
    }
}

void Hud::planetDetails(ViewContext& ctx, NavRequest& nav, sim::PlanetId id) {
    GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const sim::Content& c = session.content();
    const sim::Planet& p = s.planet(id);
    const sim::Empire& me = session.playerEmpire();

    heading(ctx.fonts.bold, p.name, p.colony ? empireColor(s, p.colony->owner) : ImVec4(0.92f, 0.96f, 1.0f, 1.0f));
    if (ImGui::SmallButton(std::format("{} system", s.system(p.system).name).c_str())) ctx.selection = Selection::ofSystem(p.system);
    ImGui::Separator();

    labelValue("Type", std::format("{} {}", sim::displayName(p.size), sim::displayName(p.surface)));
    const bool breathable = sim::canBreathe(me, p);
    labelValue("Atmosphere", std::format("{}{}", sim::displayName(p.atmosphere), breathable ? " (breathable)" : ""),
               breathable ? kGood : ImVec4(0.86f, 0.91f, 0.96f, 1.0f));
    ImGui::TextDisabled("Resources");
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    for (ResourceType r : sim::kAllResources) {
        ImGui::TextColored(resourceColor(r), "%c %d%%", sim::displayName(r)[0], p.value[sim::enumIndex(r)]);
        if (r != ResourceType::Radioactives) ImGui::SameLine();
    }
    labelValue("Facility slots", std::to_string(sim::facilitySlots(c, p)));

    if (!p.colony) {
        ImGui::SeparatorText("Uncolonized");
        labelValue("Max population", fmtNum(sim::maxPopulation(c, me, p)) + " M");
        // Offer the player's colony ships that could settle here.
        bool any = false;
        for (const sim::Ship& ship : s.ships) {
            if (ship.owner != me.id || !sim::colonizeProblem(s, ship, p).empty()) continue;
            if (!any) ImGui::TextDisabled("Send a colony ship:");
            any = true;
            if (ImGui::Button(std::format("{}##col{}", ship.name, ship.id.value).c_str())) session.issue(sim::cmd::Colonize{ship.id, p.id});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", orderText(s, ship).c_str());
        }
        if (!any) ImGui::TextWrapped("None of your colony ships can settle this %s world.", str(sim::displayName(p.surface)).c_str());
        return;
    }

    const sim::Colony& col = *p.colony;
    if (col.owner != me.id) {
        ImGui::SeparatorText("Colony");
        ImGui::TextColored(empireColor(s, col.owner), "Owned by the %s", s.empire(col.owner).name.c_str());
        return;
    }
    (void)nav;
    colonyManagement(ctx, p);
}

void Hud::colonyManagement(ViewContext& ctx, const sim::Planet& p) {
    GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const sim::Content& c = session.content();
    const sim::Empire& me = session.playerEmpire();
    const sim::Colony& col = *p.colony;

    ImGui::SeparatorText(me.homeworld == p.id ? "Homeworld" : "Colony");
    const int64_t maxPop = sim::maxPopulation(c, me, p);
    ImGui::TextDisabled("Population");
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    ImGui::ProgressBar(static_cast<float>(col.population) / static_cast<float>(std::max<int64_t>(1, maxPop)), ImVec2(-FLT_MIN, 0),
                       std::format("{} / {} M", fmtNum(col.population), fmtNum(maxPop)).c_str());

    const sim::ColonyOutput out = sim::colonyOutput(c, s, p);
    ImGui::TextDisabled("Output");
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    costText(out.resources);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(kResearchColor, "%s RP", fmtNum(out.research).c_str());
    labelValue("Efficiency", std::format("{}%", out.efficiencyPercent));

    ImGui::SeparatorText(std::format("Facilities {}/{}", col.facilities.size(), sim::facilitySlots(c, p)).c_str());
    std::map<uint32_t, int> counts;
    for (sim::FacilityIndex f : col.facilities) ++counts[f.value];
    if (counts.empty()) ImGui::TextDisabled("None yet - queue some below.");
    for (const auto& [f, n] : counts) {
        ImGui::BulletText("%s%s", n > 1 ? std::format("{}x ", n).c_str() : "", c.facility(sim::FacilityIndex{f}).name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.facility(sim::FacilityIndex{f}).description.c_str());
    }

    const bool yard = sim::hasSpaceYard(c, col);
    ImGui::SeparatorText(std::format("Construction - {}/turn", fmtNum(sim::constructionRate(c, col))).c_str());
    if (col.queue.empty()) ImGui::TextDisabled("Queue empty.");
    for (size_t i = 0; i < col.queue.size(); ++i) {
        const sim::ConstructionItem& item = col.queue[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string name = item.kind == sim::ConstructionKind::Ship ? s.design(item.design).name : c.facility(item.facility).name;
        const float progress = static_cast<float>(item.percentComplete()) / 100.0f;
        ImGui::ProgressBar(progress, ImVec2(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x, 0),
                           std::format("{}  {}%", name, item.percentComplete()).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cost: %s\nSpent: %s", costString(item.cost).c_str(), costString(item.spent).c_str());
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) session.issue(sim::cmd::CancelConstruction{p.id, static_cast<int>(i)});
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cancel (refunds spent resources)");
        ImGui::PopID();
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##buildship", yard ? "Build ship..." : "Build ship (needs a Space Yard)", ImGuiComboFlags_HeightLarge)) {
        for (sim::DesignId did : me.designs) {
            const sim::Design& d = s.design(did);
            if (d.obsolete) continue;
            ImGui::BeginDisabled(!yard || !d.stats.problems.empty());
            if (ImGui::Selectable(std::format("{}##d{}", d.name, did.value).c_str())) session.issue(sim::cmd::BuildShip{p.id, did});
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::Text("%s (%s hull)", d.name.c_str(), c.hull(d.hull).name.c_str());
                ImGui::TextDisabled("Speed %d, structure %d, %d weapons", d.stats.speed, d.stats.structure, d.stats.weaponCount);
                costText(d.stats.cost);
                ImGui::EndTooltip();
            }
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
            costText(d.stats.cost);
        }
        ImGui::EndCombo();
    }
    const bool slotsFree = sim::usedFacilitySlots(col) < sim::facilitySlots(c, p);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##buildfacility", slotsFree ? "Build facility..." : "Build facility (no free slots)", ImGuiComboFlags_HeightLarge)) {
        for (size_t i = 0; i < c.facilities.size(); ++i) {
            const sim::FacilityIndex f{i};
            if (!sim::isAvailable(c, me, f)) continue;
            ImGui::BeginDisabled(!slotsFree);
            if (ImGui::Selectable(std::format("{}##f{}", c.facility(f).name, i).c_str())) session.issue(sim::cmd::BuildFacility{p.id, f});
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", c.facility(f).description.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
            costText(c.facility(f).cost);
        }
        ImGui::EndCombo();
    }
}

void Hud::shipDetails(ViewContext& ctx, NavRequest& nav, sim::ShipId id) {
    GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const sim::Content& c = session.content();
    const sim::Ship* ship = s.findShip(id);
    if (!ship || !session.canSee(*ship)) {
        ctx.selection = {};
        return;
    }
    const sim::Design& d = s.design(ship->design);
    const sim::DesignStats& st = d.stats;
    const bool mine = ship->owner == session.player();

    heading(ctx.fonts.bold, ship->name, empireColor(s, ship->owner));
    ImGui::TextDisabled("%s-class %s", d.name.c_str(), c.hull(d.hull).name.c_str());
    if (!mine) ImGui::TextColored(empireColor(s, ship->owner), "%s", s.empire(ship->owner).name.c_str());
    ImGui::Separator();

    ImGui::TextDisabled("Location");
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    if (ImGui::SmallButton(locationText(s, ship->location).c_str())) nav.focus = ship->location;
    const int structure = st.structure - ship->damage;
    ImGui::TextDisabled("Structure");
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    ImGui::ProgressBar(static_cast<float>(structure) / static_cast<float>(std::max(1, st.structure)), ImVec2(-FLT_MIN, 0),
                       std::format("{} / {}", structure, st.structure).c_str());

    if (mine) {
        labelValue("Speed", std::format("{} (moves left {})", st.speed, ship->movesLeft));
        if (st.shields > 0) labelValue("Shields", std::to_string(st.shields));
        if (st.armed()) labelValue("Weapons", std::format("{} ({} dmg/round)", st.weaponCount, st.weaponDamage));
        if (!st.colonizes.empty()) {
            std::string surfaces;
            for (sim::PlanetSurface ps : st.colonizes) surfaces += (surfaces.empty() ? "" : ", ") + str(sim::displayName(ps));
            labelValue("Colonizes", surfaces, kGood);
        }
        if (st.supply > 0) labelValue("Supply", fmtNum(st.supply));

        ImGui::SeparatorText("Orders");
        ImGui::TextWrapped("%s", orderText(s, *ship).c_str());
        if (!ship->path.empty() || ship->order.type != sim::OrderType::None) {
            if (ImGui::Button("Stop")) session.issue(sim::cmd::StopShip{ship->id});
            ImGui::SameLine();
        }
        if (const sim::Planet* here = sim::planetAt(s, ship->location);
            here && !st.colonizes.empty() && sim::colonizeProblem(s, *ship, *here).empty() && ship->order.type != sim::OrderType::Colonize) {
            if (ImGui::Button(std::format("Colonize {}", here->name).c_str())) session.issue(sim::cmd::Colonize{ship->id, here->id});
        }
        ImGui::NewLine();
        ImGui::TextDisabled("Right-click the map to move%s.", st.colonizes.empty() ? "" : " or colonize");
    } else {
        labelValue("Armed", st.armed() ? "Yes" : "No", st.armed() ? kBad : kMuted);
    }

    // Other ships sharing the sector.
    std::vector<const sim::Ship*> others;
    for (const sim::Ship* other : sim::shipsAt(s, ship->location))
        if (other->id != ship->id && session.canSee(*other)) others.push_back(other);
    if (!others.empty()) {
        ImGui::SeparatorText("Also in this sector");
        for (const sim::Ship* other : others) {
            ImGui::PushStyleColor(ImGuiCol_Text, empireColor(s, other->owner));
            if (ImGui::Selectable(std::format("{}##o{}", other->name, other->id.value).c_str())) ctx.selection = Selection::ofShip(*other);
            ImGui::PopStyleColor();
        }
    }
}

void Hud::warpPointDetails(ViewContext& ctx, NavRequest& nav, sim::WarpPointId id) {
    const sim::GameState& s = ctx.session.state();
    const sim::WarpPoint& wp = s.warpPoint(id);
    const sim::WarpPoint& exit = s.warpPoint(wp.exit);
    const bool known = ctx.session.explored(exit.system);
    heading(ctx.fonts.bold, "Warp Point", toImVec4(palette::kWarp));
    ImGui::TextDisabled("In the %s system, sector (%d, %d)", s.system(wp.system).name.c_str(), wp.sector.x, wp.sector.y);
    ImGui::Separator();
    ImGui::TextWrapped("A stable fold in space. Ships entering this sector can jump to %s.",
                       known ? s.system(exit.system).name.c_str() : "an unexplored system");
    if (ImGui::Button(known ? std::format("Go to {}", s.system(exit.system).name).c_str() : "Look through")) {
        nav.focus = sim::Location{exit.system, exit.sector};
        ctx.selection = Selection::ofWarpPoint(exit);
    }
}

void Hud::researchWindow(ViewContext& ctx) {
    GameSession& session = ctx.session;
    const sim::Content& c = session.content();
    const sim::Empire& me = session.playerEmpire();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(780.0f * ctx.uiScale, 600.0f * ctx.uiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Research", &showResearch, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(kResearchColor, "%s research points per turn", fmtNum(me.lastResearch).c_str());
    if (me.unspentResearch > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "(%s unspent - queue something!)", fmtNum(me.unspentResearch).c_str());
    }

    // Queue: each entry is one level of a technology.
    ImGui::SeparatorText("Research Queue");
    std::vector<sim::TechIndex> queue = me.researchQueue;
    std::optional<std::vector<sim::TechIndex>> newQueue;
    std::map<uint32_t, int> seen;
    if (queue.empty()) ImGui::TextDisabled("Empty - add technologies from the list below.");
    for (size_t i = 0; i < queue.size(); ++i) {
        const sim::TechIndex t = queue[i];
        const int level = me.techLevel(t) + (++seen[t.value]);
        ImGui::PushID(static_cast<int>(i));
        const float fraction = seen[t.value] == 1 ? static_cast<float>(me.techProgress[t.index()]) /
                                                        static_cast<float>(c.tech(t).costForLevel(level))
                                                  : 0.0f;
        ImGui::ProgressBar(fraction, ImVec2(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 3.0f - 16.0f, 0),
                           std::format("{} {}  ({} RP)", c.tech(t).name, level, fmtNum(c.tech(t).costForLevel(level))).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) {
            std::swap(queue[i], queue[i - 1]);
            newQueue = queue;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 == queue.size());
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) {
            std::swap(queue[i], queue[i + 1]);
            newQueue = queue;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) {
            queue.erase(queue.begin() + static_cast<ptrdiff_t>(i));
            newQueue = queue;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Technologies");
    std::vector<std::string> categories;
    for (const sim::TechDef& t : c.techs)
        if (std::find(categories.begin(), categories.end(), t.category) == categories.end()) categories.push_back(t.category);

    if (ImGui::BeginTable("techs", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Technology", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Next level", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Queue").x + 16.0f);
        ImGui::TableHeadersRow();
        for (const std::string& category : categories) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushFont(ctx.fonts.bold, 0.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.95f, 1.0f), "%s", category.c_str());
            ImGui::PopFont();
            for (size_t i = 0; i < c.techs.size(); ++i) {
                const sim::TechDef& tech = c.techs[i];
                if (tech.category != category) continue;
                const sim::TechIndex t{i};
                const int level = me.techLevel(t);
                const int queued = static_cast<int>(std::count(me.researchQueue.begin(), me.researchQueue.end(), t));
                const bool maxed = level + queued >= tech.maxLevel;
                const bool unlocked = sim::meetsRequirements(me, tech.prerequisites);
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextColumn();
                ImGui::Indent(8.0f);
                ImGui::TextColored(unlocked ? ImVec4(0.9f, 0.94f, 1.0f, 1.0f) : kMuted, "%s", tech.name.c_str());
                ImGui::Unindent(8.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                    ImGui::TextUnformatted(tech.description.c_str());
                    for (const auto& req : tech.prerequisites)
                        ImGui::TextColored(me.techLevel(req.tech) >= req.level ? kGood : kBad, "Requires %s %d", c.tech(req.tech).name.c_str(),
                                           req.level);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d / %d", level, tech.maxLevel);
                ImGui::TableNextColumn();
                if (level < tech.maxLevel) ImGui::Text("%s RP", fmtNum(tech.costForLevel(level + 1)).c_str());
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (level >= tech.maxLevel) ImGui::TextColored(kGood, "Complete");
                else if (!unlocked) ImGui::TextColored(kMuted, "Locked");
                else if (me.techProgress[i] > 0)
                    ImGui::ProgressBar(static_cast<float>(me.techProgress[i]) / static_cast<float>(tech.costForLevel(level + 1)), ImVec2(-FLT_MIN, 0));
                else if (queued > 0) ImGui::TextColored(kResearchColor, "Queued x%d", queued);
                else ImGui::TextDisabled("Available");
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(maxed);
                if (ImGui::SmallButton("Queue")) {
                    queue = me.researchQueue;
                    queue.push_back(t);
                    newQueue = queue;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (newQueue) session.issue(sim::cmd::SetResearchQueue{*newQueue});
    ImGui::End();
}

void Hud::logWindow(ViewContext& ctx, NavRequest& nav) {
    GameSession& session = ctx.session;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 size(470.0f * ctx.uiScale, 320.0f * ctx.uiScale);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - size.x - 10.0f, vp->Pos.y + vp->Size.y - size.y - 10.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Event Log", &showLog)) {
        ImGui::End();
        return;
    }
    session.markEventsRead();
    const auto& log = session.log();
    if (log.empty()) ImGui::TextDisabled("Nothing to report.");
    uint32_t lastTurn = 0;
    for (size_t i = log.size(); i-- > 0;) {
        const LogEntry& entry = log[i];
        if (entry.turn != lastTurn) {
            ImGui::SeparatorText(std::format("Turn {}", entry.turn).c_str());
            lastTurn = entry.turn;
        }
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, eventColor(entry.event.kind));
        ImGui::PushTextWrapPos(0.0f);
        if (ImGui::Selectable(entry.event.text.c_str(), false) && entry.event.location) nav.focus = entry.event.location;
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (entry.event.location && ImGui::IsItemHovered()) ImGui::SetTooltip("Click to go there");
        ImGui::PopID();
    }
    ImGui::End();
}

void Hud::helpWindow() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Controls", &showHelp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    static constexpr std::pair<const char*, const char*> kBindings[] = {
        {"Left click", "Select (click again to cycle through a sector)"},
        {"Double click", "Open a system / follow a warp point"},
        {"Right click", "Move the selected ship, or colonize a planet"},
        {"Drag / middle drag", "Pan the map"},
        {"Mouse wheel", "Zoom"},
        {"WASD / arrows", "Pan the map"},
        {"Home", "Reset the view"},
        {"Escape / G", "Back to the galaxy map"},
        {"Tab", "Next idle ship"},
        {"Enter", "End turn"},
        {"F1 / F2 / F3", "Controls / Research / Event log"},
        {"F9", "Renderer info"},
        {"Alt+Enter", "Toggle fullscreen"},
    };
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg)) {
        for (const auto& [key, action] : kBindings) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.95f, 1.0f), "%s", key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(action);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void Hud::newGameDialog(ViewContext& ctx, NavRequest& nav) {
    const sim::Content& c = ctx.session.content();
    if (openNewGame_) {
        openNewGame_ = false;
        pendingSetup_ = sim::GameSetup{};
        pendingSetup_.galaxy = ctx.session.state().galaxy;
        pendingSetup_.galaxy.seed = SDL_GetTicksNS();
        pendingSetup_.empireCount = static_cast<int>(ctx.session.state().empires.size());
        ImGui::OpenPopup("New Game");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * ctx.uiScale, 0.0f));
    if (!ImGui::BeginPopupModal("New Game", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;

    sim::GalaxySettings& g = pendingSetup_.galaxy;
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.0f);
    ImGui::InputScalar("Seed", ImGuiDataType_U64, &g.seed);
    ImGui::SameLine();
    if (ImGui::SmallButton("Random")) g.seed = SDL_GetTicksNS();
    ImGui::SliderInt("Star systems", &g.systemCount, 12, 150);
    const int maxEmpires = static_cast<int>(c.races.size());
    ImGui::SliderInt("Empires", &pendingSetup_.empireCount, 2, maxEmpires);
    pendingSetup_.empireCount = std::clamp(pendingSetup_.empireCount, 2, std::min(maxEmpires, g.systemCount / 2));
    if (ImGui::BeginCombo("Galaxy shape", str(sim::displayName(g.shape)).c_str())) {
        for (int i = 0; i < static_cast<int>(sim::GalaxyShape::Count); ++i) {
            const auto shape = static_cast<sim::GalaxyShape>(i);
            if (ImGui::Selectable(str(sim::displayName(shape)).c_str(), shape == g.shape)) g.shape = shape;
        }
        ImGui::EndCombo();
    }
    raceChoice_ = std::clamp(raceChoice_, 0, maxEmpires - 1);
    if (ImGui::BeginCombo("Race", c.races[static_cast<size_t>(raceChoice_)].name.c_str())) {
        for (int i = 0; i < maxEmpires; ++i) {
            const sim::RaceDef& race = c.races[static_cast<size_t>(i)];
            if (ImGui::Selectable(std::format("{} - {}", race.name, race.empireName).c_str(), i == raceChoice_)) raceChoice_ = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", race.description.c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    const sim::RaceDef& race = c.races[static_cast<size_t>(raceChoice_)];
    ImGui::TextWrapped("%s", race.description.c_str());
    ImGui::TextDisabled("Native to %s worlds, breathes %s.", str(sim::displayName(race.nativeSurface)).c_str(),
                        str(sim::displayName(race.breathes)).c_str());

    ImGui::Separator();
    if (ImGui::Button("Start", ImVec2(120.0f * ctx.uiScale, 0))) {
        pendingSetup_.playerRace = race.key;
        nav.newGame = pendingSetup_;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f * ctx.uiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void Hud::statusToast(ViewContext& ctx) {
    const double age = static_cast<double>(SDL_GetTicks()) / 1000.0 - ctx.session.statusTime();
    if (age > 4.0 || ctx.session.statusMessage().empty()) return;
    const float alpha = static_cast<float>(std::min(1.0, (4.0 - age) / 0.5));
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y - 40.0f * ctx.uiScale), ImGuiCond_Always,
                            ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.9f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::Begin("##toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.55f, 1.0f), "%s", ctx.session.statusMessage().c_str());
    ImGui::End();
    ImGui::PopStyleVar();
}

void Hud::debugOverlay(ViewContext& ctx) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 10.0f, vp->Pos.y + topBarHeight + 10.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##debug", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    ImGui::TextUnformatted(rendererInfo.c_str());
    ImGui::Text("%.0f FPS (%.2f ms)", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
    ImGui::Text("Framebuffer %ux%u, scale %.2f", ctx.frame.width, ctx.frame.height, ctx.fbScale);
    ImGui::Text("Seed %llu, checksum %016llx", static_cast<unsigned long long>(ctx.session.state().galaxy.seed),
                static_cast<unsigned long long>(sim::stateChecksum(ctx.session.state())));
    ImGui::End();
}

} // namespace opense4::client
