#include "client/classic/screens/colony_widgets.hpp"

#include "game/query.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>

namespace opense4::client::classic {

namespace {

ImU32 rgb(uint32_t c, float alpha = 1.0f) {
    return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, static_cast<int>(alpha * 255.0f));
}

void drawSprite(ImDrawList* dl, const Sprite& s, ImVec2 min, ImVec2 max, ImU32 tint = IM_COL32_WHITE) {
    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), min, max, ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y),
                 tint);
}

// Space inside a table row after the cell padding.
float rowInner(UiContext& ui) { return ui.px(kRowHeight) - 2.0f * ImGui::GetStyle().CellPadding.y; }

void centerY(UiContext& ui, float itemHeight) {
    const float off = (rowInner(ui) - itemHeight) * 0.5f;
    if (off > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + off);
}

bool escapePressed() { return ImGui::IsKeyPressed(ImGuiKey_Escape, false); }

} // namespace

bool lampButton(Dialog& d, UiContext& ui, const char* label, bool on, bool enabled, const char* tooltip) {
    const bool clicked = d.button(label, enabled, on);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) itemTooltip(tooltip);
    const float s = ui.px(11);
    const ImVec2 p0(mn.x + ui.px(7), (mn.y + mx.y - s) * 0.5f), p1(p0.x + s, p0.y + s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // General.bmp holds 13x13 lamps: blue, green, red, grey from x = 178.
    if (Sprite lamp = ui.art.region("Pictures/Game/General.bmp", on ? 191 : 217, 0, 13, 13))
        drawSprite(dl, lamp, p0, p1, enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110));
    else
        dl->AddCircleFilled(ImVec2(p0.x + s * 0.5f, p0.y + s * 0.5f), s * 0.45f, on ? IM_COL32(40, 200, 60, 255) : IM_COL32(90, 90, 90, 255));
    return clicked;
}

void itemTooltip(const char* text) {
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ---- Quadrant map ------------------------------------------------------------------------------

std::optional<game::SystemId> quadrantMap(UiContext& ui, const char* id, Vec2 size, const std::vector<uint8_t>& marked,
                                          std::optional<game::SystemId> highlight) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 sz = ui.size(size);
    ImGui::InvisibleButton(id, sz);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(2, 5, 12, 255));
    dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), rgb(0x2c4f9e));

    const float pad = ui.px(6);
    const float cell = std::min((sz.x - 2 * pad) / float(std::max(1, g.width)), (sz.y - 2 * pad) / float(std::max(1, g.height)));
    const ImVec2 origin(p0.x + (sz.x - cell * float(g.width)) * 0.5f, p0.y + (sz.y - cell * float(g.height)) * 0.5f);
    auto pos = [&](const game::StarSystem& sys) {
        return ImVec2(origin.x + (float(sys.position.x) + 0.5f) * cell, origin.y + (float(sys.position.y) + 0.5f) * cell);
    };
    for (int x = 0; x <= g.width; x += 2)
        dl->AddLine(ImVec2(origin.x + float(x) * cell, origin.y), ImVec2(origin.x + float(x) * cell, origin.y + float(g.height) * cell),
                    rgb(0x10224a, 0.8f));
    for (int y = 0; y <= g.height; y += 2)
        dl->AddLine(ImVec2(origin.x, origin.y + float(y) * cell), ImVec2(origin.x + float(g.width) * cell, origin.y + float(y) * cell),
                    rgb(0x10224a, 0.8f));

    const auto& known = me.knowledge.knownWarpLink;
    for (const game::SpaceObject& o : g.objects) {
        if (o.kind != game::ObjectKind::WarpPoint || !o.destination.valid() || o.destination < o.id) continue;
        if (o.id.index() < known.size() && known[o.id.index()])
            dl->AddLine(pos(g.system(o.system)), pos(g.system(g.object(o.destination).system)), rgb(0x4868a8, 0.9f), ui.px(1));
    }

    // Presence: own colour, another empire's colour, or several (as the main window's galaxy panel).
    std::vector<std::vector<game::EmpireId>> presence(g.systems.size());
    auto mark = [&](game::SystemId sys, game::EmpireId e) {
        auto& list = presence[sys.index()];
        if (std::find(list.begin(), list.end(), e) == list.end()) list.push_back(e);
    };
    for (const game::Vehicle& v : s.vehicles)
        if (v.owner.valid() && knownVehicle(ui, v)) mark(v.location.system, v.owner);
    for (const auto& c : s.colonies)
        if (c && (c->owner == me.id || me.hasExplored(g.object(c->planet).system))) mark(g.object(c->planet).system, c->owner);

    const float r = std::max(ui.px(2.2f), std::min(ui.px(3.5f), cell * 0.35f));
    std::optional<game::SystemId> under;
    float best = ui.px(9) * ui.px(9);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const game::StarSystem& sys : g.systems) {
        const ImVec2 p = pos(sys);
        const auto& who = presence[sys.id.index()];
        ImU32 c = me.hasExplored(sys.id) ? rgb(0xc8d0dc) : rgb(0x505866);
        if (who.size() == 1) c = empireColor(s, who.front());
        const bool isMarked = sys.id.index() < marked.size() && marked[sys.id.index()];
        if (who.size() > 1) dl->AddTriangleFilled(ImVec2(p.x, p.y - r * 1.3f), ImVec2(p.x - r * 1.2f, p.y + r), ImVec2(p.x + r * 1.2f, p.y + r), IM_COL32_WHITE);
        else if (isMarked) dl->AddCircleFilled(p, r, c);
        else dl->AddCircle(p, r, c, 0, ui.px(1.2f));
        if (hovered) {
            const float dx = mouse.x - p.x, dy = mouse.y - p.y;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                under = sys.id;
            }
        }
    }
    if (highlight && highlight->index() < g.systems.size()) {
        const ImVec2 p = pos(g.system(*highlight));
        const float pulse = 0.75f + 0.25f * float(std::sin(ui.time * 5.0));
        dl->AddCircle(p, r + ui.px(4), IM_COL32(255, 208, 64, int(255 * pulse)), 0, ui.px(1.6f));
        dl->AddCircleFilled(p, ui.px(1.6f), IM_COL32(255, 208, 64, 255));
    }
    if (under) {
        const game::StarSystem& sys = g.system(*under);
        ImGui::SetTooltip("%s", me.hasExplored(sys.id) ? sys.name.c_str() : "Unexplored system");
    }
    return under;
}

// ---- Tables ------------------------------------------------------------------------------------

void cellText(UiContext& ui, const std::string& text, const ImVec4& color) {
    centerY(ui, ImGui::GetTextLineHeight());
    ImGui::TextColored(color, "%s", text.c_str());
}

void cellImage(UiContext& ui, const Sprite& s, float size) {
    centerY(ui, ui.px(size));
    image(ui, s, {size, size});
}

void cellResources(UiContext& ui, const game::Resources& r) {
    centerY(ui, std::max(ImGui::GetTextLineHeight(), ui.px(14)));
    resources(ui, r, true);
}

void cellProgress(UiContext& ui, float fraction, const std::string& caption, float width) {
    // Drawn by hand: a framed ImGui widget would shift the text baseline of the row's later cells.
    const float h = ui.px(15);
    centerY(ui, h);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = width > 0 ? ui.px(width) : ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg));
    dl->AddRectFilled(p, ImVec2(p.x + w * std::clamp(fraction, 0.0f, 1.0f), p.y + h), IM_COL32(51, 115, 217, 255));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_Border));
    const ImVec2 ts = ImGui::CalcTextSize(caption.c_str());
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), IM_COL32_WHITE, caption.c_str());
}

void statusIconRow(UiContext& ui, const std::vector<int>& icons, float size) {
    centerY(ui, ui.px(size));
    for (size_t i = 0; i < icons.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, ui.px(2));
        image(ui, ui.art.statusIcon(icons[i]), {size, size});
    }
    if (icons.empty()) ImGui::Dummy(ui.size({size, size}));
}

RowEvents tableRow(UiContext& ui, int id, bool selected) {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ui.px(kRowHeight));
    ImGui::TableSetColumnIndex(0);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    RowEvents ev;
    ev.clicked = ImGui::Selectable("##row", selected,
                                   ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
                                   ImVec2(0, rowInner(ui)));
    ev.hovered = ImGui::IsItemHovered();
    ev.doubleClicked = ev.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ev.rightClicked = ev.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (ev.doubleClicked) ev.clicked = false;
    ImGui::PopID();
    ImGui::SetCursorScreenPos(start);
    return ev;
}

bool sortKeyLess(const SortKey& a, const SortKey& b) {
    if (a.index() != b.index()) return a.index() < b.index();
    if (const auto* x = std::get_if<int64_t>(&a)) return *x < std::get<int64_t>(b);
    const std::string& sa = std::get<std::string>(a);
    const std::string& sb = std::get<std::string>(b);
    return std::lexicographical_compare(sa.begin(), sa.end(), sb.begin(), sb.end(), [](char c1, char c2) {
        return std::tolower(static_cast<unsigned char>(c1)) < std::tolower(static_cast<unsigned char>(c2));
    });
}

void readSortSpecs(int& column, bool& ascending) {
    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs(); ss && ss->SpecsCount > 0) {
        column = static_cast<int>(ss->Specs[0].ColumnUserID);
        ascending = ss->Specs[0].SortDirection != ImGuiSortDirection_Descending;
        ss->SpecsDirty = false;
    }
}

// ---- Text helpers ------------------------------------------------------------------------------

std::string turnsText(int turns) {
    if (turns < 0) return "never";
    return turns == 1 ? "1 turn" : std::format("{} turns", turns);
}

std::string resourcesText(const game::Resources& r) {
    return std::format("{} / {} / {}", formatNumber(r.v[0]), formatNumber(r.v[1]), formatNumber(r.v[2]));
}

Sprite designSprite(UiContext& ui, game::DesignId id) {
    const game::GameState& s = ui.state();
    if (!id.valid() || id.index() >= s.designs.size()) return {};
    const game::Design& d = s.design(id);
    const std::string& style = d.owner.valid() ? s.empire(d.owner).race.style : std::string{};
    return ui.art.shipMini(style, ui.rules().hull(d.hull));
}

Sprite queueItemSprite(UiContext& ui, const game::QueueItem& item) {
    if (item.kind == game::QueueItem::Kind::Vehicle) return designSprite(ui, item.design);
    if (item.facility >= ui.rules().data().facilities.size()) return {};
    return ui.art.facility(ui.rules().facility(item.facility).picture);
}

// ---- Command results ---------------------------------------------------------------------------

bool StatusLine::issue(UiContext& ui, game::Command c) {
    const game::CommandResult r = ui.session.issue(std::move(c));
    if (!r.ok) fail(r.error);
    return r.ok;
}

void StatusLine::draw(UiContext&) const {
    if (text.empty()) return;
    ImGui::TextColored(error ? kTextWarn : kTextDim, "%s", text.c_str());
}

// ---- Popups ------------------------------------------------------------------------------------

bool beginModal(UiContext& ui, const char* id, Vec2 size) {
    ImGui::SetNextWindowPos(ui.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
    return ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
}

namespace {

// Bottom-aligned row of equal buttons in a popup; returns the index clicked or -1.
int popupButtons(UiContext& ui, std::initializer_list<std::pair<const char*, bool>> buttons) {
    const float h = ui.px(26);
    const float y = ImGui::GetWindowHeight() - h - ImGui::GetStyle().WindowPadding.y;
    if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - spacing * float(buttons.size() - 1)) / float(buttons.size());
    int clicked = -1, i = 0;
    for (const auto& [label, enabled] : buttons) {
        if (i > 0) ImGui::SameLine();
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(label, ImVec2(w, h))) clicked = i;
        ImGui::EndDisabled();
        ++i;
    }
    return clicked;
}

int scrapPercent(UiContext& ui, game::ObjectId planet) {
    const game::Rules& r = ui.rules();
    const int base = static_cast<int>(r.setting("Scrap Facility Percent Returned", 30));
    return std::max(base, game::reclamationPercentAt(r, ui.state(), ui.session.player(), game::locationOf(ui.state().galaxy, planet)));
}

} // namespace

void ReportPopup::openPlanet(game::ObjectId p) {
    planet_ = p;
    vehicle_.reset();
    pending_ = true;
    if (tab_ == ReportTab::Components) tab_ = ReportTab::Facilities;
}

void ReportPopup::openVehicle(game::VehicleId v) {
    vehicle_ = v;
    planet_.reset();
    pending_ = true;
    if (tab_ == ReportTab::Facilities) tab_ = ReportTab::Components;
}

void ReportPopup::draw(UiContext& ui) {
    const char* id = planet_ ? "Planet Report###colreport" : "Ship Report###colreport";
    if (pending_) {
        ImGui::OpenPopup(id);
        pending_ = false;
    }
    if (!beginModal(ui, id, {360, 500})) return;
    const game::Vehicle* v = vehicle_ ? ui.state().vehicle(*vehicle_) : nullptr;
    const float footer = ui.px(26) * 2 + ImGui::GetStyle().ItemSpacing.y * 2;
    ImGui::BeginChild("##body", ImVec2(0, -footer));
    if (planet_) planetReport(ui, *planet_, tab_);
    else if (v) vehicleReport(ui, *v, tab_);
    else ImGui::TextColored(kTextDim, "No longer known");
    ImGui::EndChild();
    tab_ = reportTabs(ui, tab_, planet_.has_value());
    if (ImGui::Button("Close", ImVec2(-FLT_MIN, ui.px(26))) || escapePressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ScrapFacilitiesPopup::open(game::ObjectId planet) {
    planet_ = planet;
    checked_.clear();
    pending_ = true;
}

void ScrapFacilitiesPopup::draw(UiContext& ui, StatusLine& status) {
    const char* id = "Select Facilities###scrapfacilities";
    if (pending_) {
        ImGui::OpenPopup(id);
        pending_ = false;
    }
    if (!beginModal(ui, id, {480, 560})) return;
    const game::Rules& r = ui.rules();
    const game::Colony* c = ui.state().colony(planet_);
    if (!c || c->owner != ui.session.player()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    checked_.resize(c->facilities.size(), 0);
    const int pct = scrapPercent(ui, planet_);
    heading(ui, ui.state().galaxy.object(planet_).name.c_str());
    ImGui::TextColored(kTextDim, "Pick the facilities to scrap. Each returns %d%% of its cost at once.", pct);
    const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 3 + ImGui::GetTextLineHeightWithSpacing();
    game::Resources refund;
    int count = 0;
    if (ImGui::BeginTable("##facilities", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                          ImVec2(0, -footer))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ui.px(26));
        ImGui::TableSetupColumn("Facility", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Refund", ImGuiTableColumnFlags_WidthFixed, ui.px(170));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < c->facilities.size(); ++i) {
            const ruleset::Facility& f = r.facility(c->facilities[i]);
            const game::Resources back = game::Resources::from(f.cost).percent(pct);
            const RowEvents ev = tableRow(ui, static_cast<int>(i), checked_[i] != 0);
            if (ev.clicked || ev.doubleClicked) checked_[i] = checked_[i] ? 0 : 1;
            cellImage(ui, ui.art.facility(f.picture));
            ImGui::TableSetColumnIndex(1);
            cellText(ui, f.name, checked_[i] ? kTextHighlight : ImVec4(1, 1, 1, 1));
            ImGui::TableSetColumnIndex(2);
            cellText(ui, resourcesText(back), kTextDim);
            if (checked_[i]) {
                refund += back;
                ++count;
            }
        }
        if (c->facilities.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(kTextDim, "This colony has no facilities.");
        }
        ImGui::EndTable();
    }
    ImGui::Text("Selected: %d   Refund: %s", count, resourcesText(refund).c_str());
    const int b = popupButtons(ui, {{"Select All", !checked_.empty()}, {"Clear", count > 0}, {"Scrap", count > 0}, {"Cancel", true}});
    if (b == 0) std::fill(checked_.begin(), checked_.end(), uint8_t{1});
    if (b == 1) std::fill(checked_.begin(), checked_.end(), uint8_t{0});
    if (b == 2) {
        int done = 0;
        for (size_t i = checked_.size(); i-- > 0;)
            if (checked_[i] && status.issue(ui, game::cmd::Scrap{{}, planet_, static_cast<int32_t>(i)})) ++done;
        if (done == count) status.info(std::format("Scrapped {} facilit{} on {}", done, done == 1 ? "y" : "ies", ui.state().galaxy.object(planet_).name));
        ImGui::CloseCurrentPopup();
    }
    if (b == 3 || escapePressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ScrapTypePopup::open(std::vector<game::ObjectId> colonies) {
    colonies_ = std::move(colonies);
    chosen_.reset();
    pending_ = true;
}

void ScrapTypePopup::draw(UiContext& ui, StatusLine& status) {
    const char* id = "Scrap Facility Types###scraptypes";
    if (pending_) {
        ImGui::OpenPopup(id);
        pending_ = false;
    }
    if (!beginModal(ui, id, {520, 560})) return;
    const game::Rules& r = ui.rules();
    const game::GameState& s = ui.state();
    const game::EmpireId me = ui.session.player();
    struct TypeRow {
        int count = 0;
        int colonies = 0;
        game::Resources refund;
    };
    std::map<uint32_t, TypeRow> types;
    int colonyCount = 0;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != me) continue;
        if (!colonies_.empty() && std::find(colonies_.begin(), colonies_.end(), c->planet) == colonies_.end()) continue;
        ++colonyCount;
        const int pct = scrapPercent(ui, c->planet);
        std::map<uint32_t, int> here;
        for (uint32_t f : c->facilities) ++here[f];
        for (const auto& [f, n] : here) {
            TypeRow& t = types[f];
            t.count += n;
            ++t.colonies;
            for (int k = 0; k < n; ++k) t.refund += game::Resources::from(r.facility(f).cost).percent(pct);
        }
    }
    ImGui::TextColored(kTextDim, colonies_.empty() ? "Every facility of the chosen type on all %d colonies is scrapped."
                                                   : "Every facility of the chosen type on the %d selected colonies is scrapped.",
                       colonyCount);
    const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
    if (ImGui::BeginTable("##types", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter, ImVec2(0, -footer))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ui.px(26));
        ImGui::TableSetupColumn("Facility", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, ui.px(110));
        ImGui::TableSetupColumn("Refund", ImGuiTableColumnFlags_WidthFixed, ui.px(130));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& [f, t] : types) {
            const RowEvents ev = tableRow(ui, static_cast<int>(f), chosen_ == f);
            if (ev.clicked || ev.doubleClicked) chosen_ = f;
            cellImage(ui, ui.art.facility(r.facility(f).picture));
            ImGui::TableSetColumnIndex(1);
            cellText(ui, r.facility(f).name);
            ImGui::TableSetColumnIndex(2);
            cellText(ui, std::format("{} on {} colon{}", t.count, t.colonies, t.colonies == 1 ? "y" : "ies"), kTextDim);
            ImGui::TableSetColumnIndex(3);
            cellText(ui, resourcesText(t.refund), kTextDim);
        }
        ImGui::EndTable();
    }
    if (chosen_ && !types.contains(*chosen_)) chosen_.reset();
    const int b = popupButtons(ui, {{"Scrap All Of Type", chosen_.has_value()}, {"Cancel", true}});
    if (b == 0 && chosen_) {
        const auto commands = scrapFacilityType(s, me, *chosen_, colonies_);
        const std::string name = r.facility(*chosen_).name;
        int done = 0;
        for (const auto& c : commands) done += status.issue(ui, c) ? 1 : 0;
        if (done == static_cast<int>(commands.size())) status.info(std::format("Scrapped {} x {}", done, name));
        ImGui::CloseCurrentPopup();
    }
    if (b == 1 || escapePressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ConfirmPopup::open(std::string question) {
    question_ = std::move(question);
    pending_ = true;
}

bool ConfirmPopup::draw(UiContext& ui) {
    const char* id = "Confirm###confirm";
    if (pending_) {
        ImGui::OpenPopup(id);
        pending_ = false;
    }
    if (!beginModal(ui, id, {420, 170})) return false;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(question_.c_str());
    ImGui::PopTextWrapPos();
    const int b = popupButtons(ui, {{"Yes", true}, {"No", true}});
    if (b >= 0 || escapePressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return b == 0;
}

} // namespace opense4::client::classic
