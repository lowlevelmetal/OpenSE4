#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace opense4::client::classic::shipui {

namespace {

ImU32 u32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

void drawSprite(ImDrawList* dl, const Sprite& s, ImVec2 p0, ImVec2 p1, ImU32 tint = IM_COL32_WHITE) {
    if (!s) return;
    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), p0, p1, ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y), tint);
}

// Centers the next modal popup in the frame at a fixed frame size.
void placePopup(UiContext& ui, Vec2 size) {
    ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ui.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
}

constexpr ImGuiWindowFlags kPopupFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

bool escapePressed() { return ImGui::IsKeyPressed(ImGuiKey_Escape, false); }

} // namespace

// ---- Defaults ---------------------------------------------------------------------------------

game::VehicleId firstOwnShip(const UiContext& ui) {
    const game::GameState& s = ui.state();
    game::VehicleId any;
    for (const game::Vehicle& v : s.vehicles) {
        if (v.owner != ui.session.player()) continue;
        if (!isUnitVehicle(ui.rules(), s, v)) return v.id;
        if (!any.valid()) any = v.id;
    }
    return any;
}

std::optional<game::ObjectId> homeworld(const UiContext& ui) {
    std::optional<game::ObjectId> first;
    for (const auto& c : ui.state().colonies) {
        if (!c || c->owner != ui.session.player()) continue;
        if (c->homeworld) return c->planet;
        if (!first) first = c->planet;
    }
    return first;
}

const game::Vehicle* ownVehicle(const UiContext& ui, game::VehicleId id) {
    const game::Vehicle* v = ui.state().vehicle(id);
    return v && v->owner == ui.session.player() ? v : nullptr;
}

const game::Colony* ownColony(const UiContext& ui, game::ObjectId planet) {
    const game::Colony* c = planet.valid() ? ui.state().colony(planet) : nullptr;
    return c && c->owner == ui.session.player() ? c : nullptr;
}

std::vector<const game::Vehicle*> ownVehiclesAt(const UiContext& ui, game::Location where) {
    std::vector<const game::Vehicle*> out;
    for (const game::Vehicle& v : ui.state().vehicles)
        if (v.owner == ui.session.player() && v.location == where) out.push_back(&v);
    return out;
}

std::vector<const game::Colony*> ownColoniesAt(const UiContext& ui, game::Location where) {
    std::vector<const game::Colony*> out;
    for (game::ObjectId id : game::planetsAt(ui.state(), where))
        if (const game::Colony* c = ownColony(ui, id)) out.push_back(c);
    return out;
}

void pickLocationForOrder(UiContext& ui, OrderOwner owner, game::Order order, std::string prompt, bool immediate) {
    ClassicSession* session = &ui.session;
    ui.requests.pickPrompt = std::move(prompt);
    ui.requests.pickLocation = [session, owner, order, immediate](game::Location where) mutable {
        order.location = where;
        const game::GameState& s = session->state();
        session->issue(immediate ? withImmediate(s, owner, order) : withAppended(s, owner, order));
    };
}

// ---- Text -------------------------------------------------------------------------------------

std::string describeOrder(const UiContext& ui, const game::Order& o, game::DesignId design) {
    using K = game::OrderKind;
    const game::GameState& s = ui.state();
    const std::string name(game::displayName(o.kind));
    const auto at = [&] { return o.location.system.valid() ? " at " + sectorName(s, o.location, ui.session.player()) : std::string(); };
    const auto amount = [&](const char* none) {
        const std::string item = o.design.valid() && o.design.index() < s.designs.size() ? s.design(o.design).name : std::string(none);
        return o.amount < 0 ? "all " + item : std::format("{} {}", o.amount, item);
    };
    const auto targetName = [&]() { return objectName(s, o.object, ui.session.player()); };
    switch (o.kind) {
        case K::LoadCargo:
        case K::DropCargo: return std::format("{}: {}{}", name, amount("population"), at());
        case K::LaunchUnits:
        case K::RecoverUnits: return std::format("{}: {}{}", name, amount("units"), at());
        case K::Attack:
            if (const game::Vehicle* t = s.vehicle(o.vehicle)) return std::format("{} {}", name, t->name);
            if (!targetName().empty()) return std::format("{} {}", name, targetName());
            return name;
        case K::UseComponent:
            if (design.valid() && design.index() < s.designs.size() && o.amount >= 0 &&
                static_cast<size_t>(o.amount) < s.design(design).entries.size())
                return std::format("{}: {}", name, ui.rules().component(s.design(design).entries[static_cast<size_t>(o.amount)].component).name);
            return name;
        case K::StellarManipulation: {
            const auto action = static_cast<game::StellarAction>(std::clamp(o.amount, 0, static_cast<int>(game::StellarAction::Count) - 1));
            std::string out = std::format("{}: {}", name, stellarInfo(action).name);
            if (action == game::StellarAction::OpenWarpPoint && o.location.system.valid())
                out += " to " + s.galaxy.system(o.location.system).name;
            else if (!targetName().empty()) out += " (" + targetName() + ")";
            return out;
        }
        case K::MoveToWaypoint: {
            const auto& wps = ui.me().waypoints;
            if (o.amount >= 0 && static_cast<size_t>(o.amount) < wps.size() && wps[static_cast<size_t>(o.amount)].set) {
                const game::Waypoint& w = wps[static_cast<size_t>(o.amount)];
                return std::format("{} {} ({})", name, w.name, sectorName(s, w.location, ui.session.player()));
            }
            return std::format("{} {}", name, o.amount);
        }
        default: return orderText(s, o, ui.session.player());
    }
}

std::string cargoSummary(const UiContext& ui, const game::Cargo& c) {
    const game::GameState& s = ui.state();
    std::string out;
    auto add = [&](const std::string& part) { out += out.empty() ? part : ", " + part; };
    for (const auto& p : c.population)
        add(std::format("{} {}M", p.race.valid() ? s.empire(p.race).race.name : std::string("Unknown"), formatNumber(p.millions)));
    for (const auto& u : c.units) add(std::format("{} x {}", u.count, s.design(u.design).name));
    return out.empty() ? std::string("Empty") : out;
}

std::string ownerName(const UiContext& ui, OrderOwner o) {
    if (o.planet.valid() && o.planet.index() < ui.state().galaxy.objects.size()) return ui.state().galaxy.object(o.planet).name;
    if (const game::Fleet* f = ui.state().fleet(o.fleet)) return f->name;
    if (const game::Vehicle* v = ui.state().vehicle(o.vehicle)) return v->name;
    return "-";
}

// ---- Pictures -----------------------------------------------------------------------------------

Sprite designMini(UiContext& ui, game::DesignId d) {
    const game::GameState& s = ui.state();
    if (!d.valid() || d.index() >= s.designs.size()) return {};
    const game::Design& design = s.design(d);
    const std::string& style = design.owner.valid() ? s.empire(design.owner).race.style : std::string{};
    return ui.art.shipMini(style, ui.rules().hull(design.hull));
}

Sprite unitMini(UiContext& ui, const game::Vehicle& v) {
    if (v.count > 1) {
        const char* group = nullptr;
        switch (game::vehicleType(ui.rules(), ui.state(), v)) {
            case ruleset::VehicleType::Fighter: group = "FighterGroup"; break;
            case ruleset::VehicleType::Satellite: group = "SatelliteGroup"; break;
            case ruleset::VehicleType::Mine: group = "MineGroup"; break;
            default: break;
        }
        if (group)
            if (Sprite s = ui.art.groupMini(ui.state().empire(v.owner).race.style, group)) return s;
    }
    return vehicleMini(ui, v);
}

Sprite fleetMini(UiContext& ui) { return ui.art.groupMini(ui.me().race.style, "Fleet"); }

Sprite colonySprite(UiContext& ui, game::ObjectId planet) {
    return objectSprite(ui, ui.state().galaxy.object(planet));
}

// ---- Status -----------------------------------------------------------------------------------

void Status::ok(std::string text) {
    text_ = std::move(text);
    error_ = false;
}

void Status::error(std::string text) {
    text_ = std::move(text);
    error_ = true;
}

bool Status::issue(UiContext& ui, game::Command c, std::string done) {
    const game::CommandResult r = ui.session.issue(std::move(c));
    if (!r.ok) error(r.error);
    else if (!done.empty()) ok(std::move(done));
    return r.ok;
}

void Status::draw(UiContext&) const {
    if (text_.empty()) return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(error_ ? kBad : kGood, "%s", text_.c_str());
    ImGui::PopTextWrapPos();
}

// ---- Lists ------------------------------------------------------------------------------------

void beginPanel(UiContext& ui, const char* id, const std::string& caption, ImVec2 size) {
    ImGui::BeginGroup();
    if (!caption.empty()) ImGui::TextColored(kLabelBlue, "%s", caption.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.px(4), ui.px(4)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.35f));
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void endPanel(UiContext& ui, const char* footnote) {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (footnote) {
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImGui::TextColored(kDim, "%s", footnote);
        ImGui::PopFont();
    }
    (void)ui;
    ImGui::EndGroup();
}

RowClick row(UiContext& ui, int id, const Sprite& picture, std::string_view title, std::string_view detail, const RowStyle& style) {
    RowClick out;
    ImGui::PushID(id);
    const float h = ui.px(style.height);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImGuiSelectableFlags flags = style.enabled ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled;
    out.left = ImGui::Selectable("##row", style.selected, flags, ImVec2(0, h));
    out.hovered = ImGui::IsItemHovered();
    out.right = style.enabled && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = p0.x + ui.px(4 + style.indent);
    const float cy = p0.y + h * 0.5f;
    if (style.lamp != Lamp::None) {
        const ImU32 fill = style.lamp == Lamp::On ? IM_COL32(40, 210, 60, 255) : IM_COL32(40, 60, 150, 255);
        dl->AddCircleFilled(ImVec2(x + ui.px(6), cy), ui.px(5.5f), fill);
        dl->AddCircle(ImVec2(x + ui.px(6), cy), ui.px(5.5f), IM_COL32(140, 170, 255, 200));
        x += ui.px(18);
    }
    if (style.picture > 0) {
        const float ps = ui.px(style.picture);
        const ImVec2 a{x, cy - ps * 0.5f};
        if (picture) drawSprite(dl, picture, a, ImVec2(a.x + ps, a.y + ps), style.enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110));
        x += ps + ui.px(8);
    }
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const ImU32 textCol = style.enabled ? IM_COL32(235, 240, 255, 255) : IM_COL32(140, 150, 170, 255);
    if (detail.empty()) {
        dl->AddText(font, fs, ImVec2(x, cy - fs * 0.5f), textCol, title.data(), title.data() + title.size());
    } else {
        const float ds = fs * 0.86f;
        const float top = cy - (fs + ds + ui.px(1)) * 0.5f;
        dl->AddText(font, fs, ImVec2(x, top), textCol, title.data(), title.data() + title.size());
        dl->AddText(font, ds, ImVec2(x + ui.px(2), top + fs + ui.px(1)), u32(kDim), detail.data(), detail.data() + detail.size());
    }
    return out;
}

bool stepButtons(Dialog& d, Step& step) {
    bool changed = false;
    for (Step s : {Step::One, Step::Five, Step::Ten, Step::All})
        if (d.tab(stepLabel(s), step == s)) {
            changed = step != s;
            step = s;
        }
    return changed;
}

// ---- Mini map -----------------------------------------------------------------------------------

void miniMap(UiContext& ui, ImVec2 size, std::optional<game::SystemId> highlight, const std::vector<game::SystemId>& marked) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const ImVec2 p1{p0.x + size.x, p0.y + size.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(2, 5, 12, 255));
    dl->AddRect(p0, p1, IM_COL32(68, 106, 216, 255));
    const float pad = ui.px(8);
    const float sx = (size.x - 2 * pad) / float(std::max(1, g.width));
    const float sy = (size.y - 2 * pad) / float(std::max(1, g.height));
    for (int x = 0; x <= g.width; x += 2)
        dl->AddLine(ImVec2(p0.x + pad + float(x) * sx, p0.y + pad), ImVec2(p0.x + pad + float(x) * sx, p1.y - pad), IM_COL32(16, 34, 74, 200));
    for (int y = 0; y <= g.height; y += 2)
        dl->AddLine(ImVec2(p0.x + pad, p0.y + pad + float(y) * sy), ImVec2(p1.x - pad, p0.y + pad + float(y) * sy), IM_COL32(16, 34, 74, 200));
    auto pos = [&](const game::StarSystem& sys) {
        return ImVec2(p0.x + pad + (float(sys.position.x) + 0.5f) * sx, p0.y + pad + (float(sys.position.y) + 0.5f) * sy);
    };
    const auto& known = me.knowledge.knownWarpLink;
    for (const game::SpaceObject& o : g.objects) {
        if (o.kind != game::ObjectKind::WarpPoint || !o.destination.valid()) continue;
        const bool linkKnown = o.id.index() < known.size() && known[o.id.index()];
        const ImVec2 a = pos(g.system(o.system));
        const ImVec2 b = pos(g.system(g.object(o.destination).system));
        if (linkKnown) {
            if (o.destination < o.id) continue;
            dl->AddLine(a, b, IM_COL32(72, 104, 168, 230));
        } else if (me.hasExplored(o.system)) {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            dl->AddLine(a, ImVec2(a.x + dx * ui.px(6) / len, a.y + dy * ui.px(6) / len), IM_COL32(72, 104, 168, 230));
        }
    }
    const ImU32 own = empireColor(s, ui.session.player());
    for (const game::StarSystem& sys : g.systems) {
        const ImVec2 c = pos(sys);
        const bool mark = std::find(marked.begin(), marked.end(), sys.id) != marked.end();
        const ImU32 col = mark ? own : me.hasExplored(sys.id) ? IM_COL32(200, 208, 220, 255) : IM_COL32(80, 88, 102, 255);
        if (mark) dl->AddCircleFilled(c, ui.px(3.2f), col);
        else dl->AddCircle(c, ui.px(3.0f), col, 0, ui.px(1.2f));
        if (highlight && *highlight == sys.id) {
            dl->AddCircle(c, ui.px(7.5f), IM_COL32(255, 208, 64, 255), 0, ui.px(1.8f));
            if (me.hasExplored(sys.id)) dl->AddText(ImVec2(c.x + ui.px(9), c.y - ui.px(7)), IM_COL32(255, 230, 150, 255), sys.name.c_str());
        }
    }
}

// ---- Report popup -------------------------------------------------------------------------------

void ReportPopup::vehicle(game::VehicleId id) {
    *this = {};
    vehicle_ = id;
    request_ = true;
}

void ReportPopup::fleet(game::FleetId id) {
    *this = {};
    fleet_ = id;
    request_ = true;
}

void ReportPopup::planet(game::ObjectId id) {
    *this = {};
    planet_ = id;
    request_ = true;
}

void ReportPopup::draw(UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Vehicle* v = s.vehicle(vehicle_);
    const game::Fleet* f = s.fleet(fleet_);
    const char* title = v ? "Ship Report" : f ? "Fleet Report" : "Planet Report";
    const std::string id = std::format("{}###shipui_report{}", title, static_cast<const void*>(this));
    if (request_) {
        ImGui::OpenPopup(id.c_str());
        request_ = false;
    }
    placePopup(ui, {400, 540});
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, kPopupFlags)) return;
    ImGui::BeginChild("##report", ImVec2(0, -ui.px(34)));
    if (v) vehicleReport(ui, *v, tab_);
    else if (f) fleetReport(ui, *f);
    else if (planet_.valid()) planetReport(ui, planet_, tab_);
    ImGui::EndChild();
    bool close = false;
    if (v || planet_.valid()) {
        const bool isPlanet = !v;
        ReportTab t = tab_;
        if (isPlanet && t == ReportTab::Components) t = ReportTab::Facilities;
        if (!isPlanet && t == ReportTab::Facilities) t = ReportTab::Components;
        tab_ = reportTabs(ui, t, isPlanet);
        ImGui::SameLine();
    }
    if (ImGui::Button("Close", ImVec2(-FLT_MIN, ui.px(22))) || escapePressed()) close = true;
    if (close || (!v && !f && !planet_.valid())) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---- Text prompt ----------------------------------------------------------------------------------

void TextPrompt::open(std::string title, std::string text, std::string initial) {
    title_ = std::move(title);
    text_ = std::move(text);
    const size_t n = std::min(initial.size(), sizeof(buffer_) - 1);
    std::copy_n(initial.begin(), n, buffer_);
    buffer_[n] = '\0';
    request_ = true;
    focus_ = true;
}

std::optional<std::string> TextPrompt::draw(UiContext& ui) {
    const std::string id = std::format("{}###shipui_prompt{}", title_, static_cast<const void*>(this));
    if (request_) {
        ImGui::OpenPopup(id.c_str());
        request_ = false;
    }
    placePopup(ui, {420, 170});
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, kPopupFlags)) return std::nullopt;
    std::optional<std::string> result;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text_.c_str());
    ImGui::PopTextWrapPos();
    if (focus_) {
        ImGui::SetKeyboardFocusHere();
        focus_ = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool enter = ImGui::InputText("##name", buffer_, sizeof(buffer_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(34)));
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const bool empty = buffer_[0] == '\0';
    ImGui::BeginDisabled(empty);
    const bool ok = ImGui::Button("OK", ImVec2(w, ui.px(24)));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(24))) || escapePressed();
    if ((ok || enter) && !empty) {
        result = std::string(buffer_);
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return result;
}

// ---- List picker ----------------------------------------------------------------------------------

void ListPicker::open(std::string title, std::vector<Item> items, int current) {
    title_ = std::move(title);
    items_ = std::move(items);
    current_ = current;
    request_ = true;
}

std::optional<size_t> ListPicker::draw(UiContext& ui) {
    const std::string id = std::format("{}###shipui_picker{}", title_, static_cast<const void*>(this));
    if (request_) {
        ImGui::OpenPopup(id.c_str());
        request_ = false;
    }
    placePopup(ui, {440, 480});
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, kPopupFlags)) return std::nullopt;
    std::optional<size_t> chosen;
    beginPanel(ui, "##items", "", ImVec2(0, -ui.px(34)));
    const bool pictures = std::any_of(items_.begin(), items_.end(), [](const Item& i) { return static_cast<bool>(i.picture); });
    for (size_t i = 0; i < items_.size(); ++i) {
        RowStyle st;
        st.height = items_[i].detail.empty() && !pictures ? 24.0f : 34.0f;
        st.picture = pictures ? 28.0f : 0.0f;
        st.selected = static_cast<int>(i) == current_;
        st.enabled = items_[i].enabled;
        if (row(ui, static_cast<int>(i), items_[i].picture, items_[i].label, items_[i].detail, st).left) chosen = i;
    }
    if (items_.empty()) ImGui::TextColored(kDim, "Nothing to choose from.");
    endPanel(ui);
    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(24))) || escapePressed()) ImGui::CloseCurrentPopup();
    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return chosen;
}

// ---- Type and amount ------------------------------------------------------------------------------

void TypeAmountPicker::open(std::string title, std::string text, std::vector<Choice> choices) {
    title_ = std::move(title);
    text_ = std::move(text);
    choices_ = std::move(choices);
    selected_ = 0;
    all_ = true;
    request_ = true;
}

std::optional<TypeAmountPicker::Result> TypeAmountPicker::draw(UiContext& ui) {
    const std::string id = std::format("{}###shipui_type{}", title_, static_cast<const void*>(this));
    if (request_) {
        ImGui::OpenPopup(id.c_str());
        request_ = false;
    }
    placePopup(ui, {440, 500});
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, kPopupFlags)) return std::nullopt;
    std::optional<Result> result;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text_.c_str());
    ImGui::PopTextWrapPos();
    beginPanel(ui, "##types", "", ImVec2(0, -ui.px(70)));
    for (size_t i = 0; i < choices_.size(); ++i) {
        RowStyle st;
        st.height = 32;
        st.picture = 26;
        st.selected = static_cast<int>(i) == selected_;
        if (row(ui, static_cast<int>(i), choices_[i].picture, choices_[i].label, {}, st).left) selected_ = static_cast<int>(i);
    }
    if (choices_.empty()) ImGui::TextColored(kDim, "Nothing to choose from.");
    endPanel(ui);
    ImGui::Checkbox("All", &all_);
    ImGui::SameLine(0, ui.px(24));
    ImGui::BeginDisabled(all_);
    ImGui::TextUnformatted("Amount");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ui.px(120));
    ImGui::InputInt("##amount", &amount_);
    amount_ = std::max(1, amount_);
    ImGui::EndDisabled();
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(choices_.empty());
    const bool ok = ImGui::Button("Pick Location", ImVec2(w, ui.px(24)));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(24))) || escapePressed();
    if (ok && static_cast<size_t>(selected_) < choices_.size()) {
        result = Result{choices_[static_cast<size_t>(selected_)].design, all_ ? -1 : amount_};
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return result;
}

// ---- Confirm ------------------------------------------------------------------------------------

void Confirm::open(std::string title, std::string text) {
    title_ = std::move(title);
    text_ = std::move(text);
    request_ = true;
}

bool Confirm::draw(UiContext& ui) {
    const std::string id = std::format("{}###shipui_confirm{}", title_, static_cast<const void*>(this));
    if (request_) {
        ImGui::OpenPopup(id.c_str());
        request_ = false;
    }
    placePopup(ui, {440, 190});
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, kPopupFlags)) return false;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(34)));
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const bool yes = ImGui::Button("Yes", ImVec2(w, ui.px(24)));
    ImGui::SameLine();
    const bool no = ImGui::Button("No", ImVec2(w, ui.px(24))) || escapePressed();
    if (yes || no) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return yes;
}

} // namespace opense4::client::classic::shipui
