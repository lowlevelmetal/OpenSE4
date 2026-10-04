// Scrap \ Analyze \ Mothball \ Self-Destruct (order G, docs/spec/06 §1.3,
// spec 03 §14-15, spec 02 §6): the own vehicles in a sector, toggled into a
// selection, with what scrapping, unmothballing and the other actions would
// give or cost. With an own colony in the sector a Facilities tab lists its
// facilities for Scrap Facilities.

#include "client/classic/reports.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/scrap.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

std::string plural(int n, std::string_view one, std::string_view many) { return std::format("{} {}", n, n == 1 ? one : many); }

class ScrapScreen final : public Screen {
public:
    explicit ScrapScreen(const ScreenArgs& args) : planet_(args.planet), facilities_(!args.vehicle.valid() && args.planet.valid()) {
        if (args.vehicle.valid()) selected_.push_back(args.vehicle);
        // A tagged group opens the window on its sector with nothing selected (spec 06 §7 Q52).
        if (!args.vehicle.valid() && !args.planet.valid() && args.location) where_ = args.location;
    }

    bool draw(UiContext& ui) override {
        locate(ui);
        Dialog d(ui, facilities_ ? "Scrap Facilities###scrap" : "Scrap \\ Analyze \\ Mothball \\ Self-Destruct###scrap", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Colony* colony = where_ ? coloniesHere(ui) : nullptr;
        if (!colony) facilities_ = false;
        // Only listed vehicles stay selected: none in a fleet, none cloaked
        // (spec 03 §15, §19 Q74).
        std::erase_if(selected_, [&](game::VehicleId id) {
            const game::Vehicle* v = ownVehicle(ui, id);
            return !v || !where_ || v->location != *where_ || !listed(*v);
        });
        if (colony) std::erase_if(slots_, [&](size_t i) { return i >= colony->facilities.size(); });

        d.beginContent();
        if (!where_) ImGui::TextColored(kDim, "You have no ships.");
        else if (facilities_) facilitiesContent(ui, *colony);
        else vehiclesContent(ui);

        d.beginButtons();
        if (colony) {
            if (d.tab("Vehicles", !facilities_)) facilities_ = false;
            if (d.tab("Facilities", facilities_)) facilities_ = true;
            d.spacer();
        }
        if (facilities_) facilityButtons(ui, d, *colony);
        else vehicleButtons(ui, d);
        if (d.close()) return false;
        popups(ui);
        return d.keepOpen();
    }

private:
    void locate(UiContext& ui) {
        if (where_) return;
        if (!selected_.empty())
            if (const game::Vehicle* v = ownVehicle(ui, selected_.front())) where_ = v->location;
        if (!where_ && ownColony(ui, planet_)) where_ = game::locationOf(ui.state().galaxy, planet_);
        if (!where_)
            if (const game::Vehicle* v = ownVehicle(ui, firstOwnShip(ui))) {
                where_ = v->location;
                selected_.push_back(v->id);
            }
    }

    const game::Colony* coloniesHere(const UiContext& ui) {
        const auto colonies = ownColoniesAt(ui, *where_);
        if (colonies.empty()) return nullptr;
        for (const game::Colony* c : colonies)
            if (c->planet == planet_) return c;
        planet_ = colonies.front()->planet;
        return colonies.front();
    }

    // The window lists only own vehicles in the sector that are in no fleet
    // and not cloaked (spec 03 §15, §19 Q74, confirmed: binary).
    static bool listed(const game::Vehicle& v) { return !v.fleet.valid() && v.status != game::VehicleStatus::Cloaked; }

    // The selected vehicles in the window's list order: the actions go to
    // them one after another in that order (spec 03 §15).
    std::vector<const game::Vehicle*> selection(const UiContext& ui) const {
        std::vector<const game::Vehicle*> out;
        if (!where_) return out;
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_))
            if (listed(*v) && isSelected(v->id)) out.push_back(v);
        return out;
    }

    bool isSelected(game::VehicleId id) const { return std::find(selected_.begin(), selected_.end(), id) != selected_.end(); }

    // ---- Vehicles ----

    void vehiclesContent(UiContext& ui) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        ImGui::TextColored(kDim, "Location: %s", sectorName(s, *where_, ui.session.player()).c_str());
        const float listW = ImGui::GetContentRegionAvail().x * 0.46f;
        const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
        beginPanel(ui, "##vehicles", "Selected Vehicles", ImVec2(listW, h));
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_)) {
            if (!listed(*v)) continue;
            RowStyle st;
            st.lamp = isSelected(v->id) ? Lamp::On : Lamp::Off;
            std::string detail = groupDesigns(s, *v, 2);
            if (v->status == game::VehicleStatus::Mothballed) detail += ", mothballed";
            const RowClick c = row(ui, static_cast<int>(v->id.value), isUnitVehicle(r, s, *v) ? unitMini(ui, *v) : vehicleMini(ui, *v),
                                   v->count > 1 ? std::format("{} (x{})", v->name, v->count) : v->name, detail, st);
            if (c.left) {
                if (isSelected(v->id)) std::erase(selected_, v->id);
                else selected_.push_back(v->id);
            }
            if (c.right) report_.vehicle(v->id);
        }
        endPanel(ui, "Click a vehicle to toggle it.");
        ImGui::SameLine(0, ui.px(20));

        const auto sel = selection(ui);
        const ScrapWindowState state = scrapWindowState(r, s, ui.session.player(), sel);
        game::Resources value, unmothball;
        int mothballed = 0, normal = 0, cloaked = 0;
        for (const game::Vehicle* v : sel) {
            value += scrapValue(r, s, *v);
            if (v->status == game::VehicleStatus::Mothballed) {
                ++mothballed;
                unmothball += unmothballCost(r, s, *v);
            } else if (v->status == game::VehicleStatus::Cloaked) {
                ++cloaked;
            } else {
                ++normal;
            }
        }
        const int n = static_cast<int>(sel.size());
        const char* status = n == 0 ? "-" : mothballed == n ? "Mothballed" : cloaked == n ? "Cloaked" : normal == n ? "Normal" : "Mixed";
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        ImGui::Separator();
        constexpr float col = 170;
        ImGui::TextColored(kLabelBlue, "Scrap Value");
        ImGui::SameLine(ui.px(col));
        resources(ui, value, true);
        // The last selected vehicle's word (spec 03 §15).
        labelValue(ui, "Research Potential", std::string(state.researchPotential), col);
        labelValue(ui, "Status", status, col);
        ImGui::TextColored(kLabelBlue, "Cost to Unmothball");
        ImGui::SameLine(ui.px(col));
        resources(ui, unmothball, true);
        labelValue(ui, "Can Self-Destruct", n == 0 ? "-" : state.canSelfDestruct ? "Yes" : "No", col);
        labelValue(ui, "Can Be Fired On", n == 0 ? "-" : state.canBeFiredOn ? "Yes" : "No", col);
        labelValue(ui, "Space Yard In Sector", game::scrapYardAt(r, s, ui.session.player(), *where_) ? "Yes" : "No", col);
        ImGui::Dummy(ImVec2(0, ui.px(16)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(kDim, "%s",
                           s.options.simultaneous
                               ? "Scrapping, analyzing, retrofitting and mothballing need a space yard in the sector. Each action becomes "
                                 "the vehicle's only order and is carried out when the vehicle first acts in the turn."
                               : "Scrapping, analyzing, retrofitting and mothballing need a space yard in the sector. Each action is "
                                 "carried out at once.");
        ImGui::Dummy(ImVec2(0, ui.px(8)));
        status_.draw(ui);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    // Each button is lit only when every selected vehicle qualifies (spec 06
    // §1.3, spec 03 §15, confirmed: binary).
    void vehicleButtons(UiContext& ui, Dialog& d) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const auto sel = selection(ui);
        const ScrapWindowState state = scrapWindowState(r, s, ui.session.player(), sel);
        if (d.button("Scrap", state.scrap)) {
            game::Resources value;
            for (const game::Vehicle* v : sel) value += scrapValue(r, s, *v);
            ask(ui, Action::Scrap,
                std::format("Scrap {} for their raw materials? The empire gets back {} minerals, {} organics and {} radioactives.",
                            plural(static_cast<int>(sel.size()), "vehicle", "vehicles"), formatNumber(value.v[0]), formatNumber(value.v[1]),
                            formatNumber(value.v[2])));
        }
        if (d.button("Analyze", state.analyze))
            ask(ui, Action::Analyze, "Take the selected vehicles apart to study their technology? They are lost, and nothing is refunded.");
        if (d.button("Retrofit", state.retrofit)) openRetrofit(ui, sel);
        if (d.button("Mothball", state.mothball)) perform(ui, Action::Mothball);
        if (d.button("Unmothball", state.unmothball)) perform(ui, Action::Unmothball);
        if (d.button("Self-Destruct", state.selfDestruct)) ask(ui, Action::SelfDestruct, "Order the selected vehicles to destroy themselves?");
        if (d.button("Fire On", state.fireOn))
            ask(ui, Action::FireOn, "Order our own armed ships here to fire on the selected vehicles and destroy them?");
    }

    void openRetrofit(UiContext& ui, const std::vector<const game::Vehicle*>& sel) {
        const game::GameState& s = ui.state();
        retrofitDesigns_.clear();
        std::vector<ListPicker::Item> items;
        const game::Vehicle& first = *sel.front();
        const uint32_t hull = s.design(first.design).hull;
        for (game::DesignId id : ui.me().designs) {
            const game::Design& d = s.design(id);
            if (d.hull != hull || id == first.design) continue;
            game::Resources cost;
            const std::string why = game::retrofitProblem(ui.rules(), s, ui.session.player(), first, id, &cost);
            std::string detail = why.empty() ? std::format("Cost {} / {} / {} each", formatNumber(cost.v[0]), formatNumber(cost.v[1]),
                                                           formatNumber(cost.v[2]))
                                             : why;
            if (d.obsolete) detail += " (obsolete)";
            items.push_back({d.name, detail, why.empty(), designMini(ui, id)});
            retrofitDesigns_.push_back(id);
        }
        retrofit_.open("Retrofit To", std::move(items));
    }

    // ---- Facilities ----

    void facilitiesContent(UiContext& ui, const game::Colony& c) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        ImGui::TextColored(kDim, "Planet: %s", s.galaxy.object(c.planet).name.c_str());
        const float listW = ImGui::GetContentRegionAvail().x * 0.52f;
        const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
        beginPanel(ui, "##facilities", "Facilities", ImVec2(listW, h));
        for (size_t i = 0; i < c.facilities.size(); ++i) {
            const ruleset::Facility& f = r.facility(c.facilities[i]);
            const bool on = std::find(slots_.begin(), slots_.end(), i) != slots_.end();
            RowStyle st;
            st.lamp = on ? Lamp::On : Lamp::Off;
            const game::Resources v = facilityScrapValue(r, s, c, i);
            const RowClick rc = row(ui, static_cast<int>(i), ui.art.facility(f.picture), f.name,
                                    std::format("Scrap value {} / {} / {}", formatNumber(v.v[0]), formatNumber(v.v[1]), formatNumber(v.v[2])), st);
            if (rc.left) {
                if (on) std::erase(slots_, i);
                else slots_.push_back(i);
            }
        }
        if (c.facilities.empty()) ImGui::TextColored(kDim, "No facilities.");
        endPanel(ui, "Click a facility to toggle it.");
        ImGui::SameLine(0, ui.px(20));
        game::Resources value;
        for (size_t i : slots_) value += facilityScrapValue(r, s, c, i);
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        ImGui::Separator();
        constexpr float col = 150;
        labelValue(ui, "Facilities", std::format("{} / {}", c.facilities.size(), game::facilitySlots(r, s, c)), col);
        labelValue(ui, "Selected", std::to_string(slots_.size()), col);
        ImGui::TextColored(kLabelBlue, "Scrap Value");
        ImGui::SameLine(ui.px(col));
        resources(ui, value, true);
        ImGui::Dummy(ImVec2(0, ui.px(16)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui.px(300));
        ImGui::TextColored(kDim, "Scrapped facilities return part of their cost at once, more with resource reclamation in the sector.");
        ImGui::Dummy(ImVec2(0, ui.px(8)));
        status_.draw(ui);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    void facilityButtons(UiContext& ui, Dialog& d, const game::Colony& c) {
        // The facility check list never asks, whatever "confirm scrapping" says (spec 06 §7 Q46).
        if (d.button("Scrap Facilities", !slots_.empty())) perform(ui, Action::ScrapFacilities);
        (void)c;
        if (d.button("Select All", !c.facilities.empty())) {
            slots_.clear();
            for (size_t i = 0; i < c.facilities.size(); ++i) slots_.push_back(i);
        }
        if (d.button("Select None", !slots_.empty())) slots_.clear();
    }

    // ---- Popups ----

    enum class Action { Scrap, Analyze, Mothball, Unmothball, SelfDestruct, FireOn, ScrapFacilities };

    // Scrap, Analyze, Self-Destruct and Fire On ask a "Confirm Action" Yes/No
    // first while the Empire Options' "confirm scrapping" is on; otherwise
    // they act at once. Retrofit, Mothball, Unmothball and the facility check
    // lists never ask (spec 06 §7 Q46).
    void ask(UiContext& ui, Action a, std::string text) {
        if (ui.options().confirmScrap) {
            confirmAction_ = a;
            confirm_.open("Confirm Action", std::move(text));
        } else {
            perform(ui, a);
        }
    }

    // Gives the action to every selected vehicle, one after another in list
    // order. A turn-based game carries each out at once, testing it again
    // (so one can change the next: the last armed vehicle cannot be fired
    // on); a simultaneous game leaves it as each vehicle's only order (spec 03
    // §15). Vehicles that fail are left alone.
    void act(UiContext& ui, game::ScrapAction a, game::DesignId design = {}) {
        std::vector<game::VehicleId> ids;
        for (const game::Vehicle* v : selection(ui)) ids.push_back(v->id);
        int done = 0;
        std::string why;
        for (game::VehicleId id : ids) {
            const game::CommandResult res = ui.session.issue(scrapCommand(a, id, design));
            if (res.ok) ++done;
            else if (why.empty()) why = res.error;
        }
        if (done == 0) {
            if (!why.empty()) status_.error(why);
            return;
        }
        static constexpr std::string_view kDone[] = {"scrapped", "analyzed", "mothballed", "back in service", "retrofitted", "destroyed", "destroyed"};
        static constexpr std::string_view kLater[] = {"be scrapped", "be analyzed", "be mothballed", "be unmothballed", "be retrofitted",
                                                      "self-destruct", "be fired on"};
        const size_t i = static_cast<size_t>(a);
        const std::string vehicles = plural(done, "vehicle", "vehicles");
        status_.ok(ui.state().options.simultaneous ? std::format("{} will {} when {} first {} this turn.", vehicles, kLater[i], done == 1 ? "it" : "they",
                                                                  done == 1 ? "acts" : "act")
                                                   : std::format("{} {}.", vehicles, kDone[i]));
    }

    static game::Command scrapCommand(game::ScrapAction a, game::VehicleId id, game::DesignId design) {
        switch (a) {
            case game::ScrapAction::Scrap: return game::cmd::Scrap{id, {}, -1};
            case game::ScrapAction::Analyze: return game::cmd::Analyze{id};
            case game::ScrapAction::Mothball: return game::cmd::Mothball{id, true};
            case game::ScrapAction::Unmothball: return game::cmd::Mothball{id, false};
            case game::ScrapAction::Retrofit: return game::cmd::Retrofit{id, design};
            case game::ScrapAction::SelfDestruct: return game::cmd::SelfDestruct{id};
            case game::ScrapAction::FireOn: return game::cmd::FireOn{id};
        }
        return game::cmd::Scrap{id, {}, -1};
    }

    void perform(UiContext& ui, Action a) {
        switch (a) {
            case Action::Scrap: act(ui, game::ScrapAction::Scrap); break;
            case Action::Analyze: act(ui, game::ScrapAction::Analyze); break;
            case Action::Mothball: act(ui, game::ScrapAction::Mothball); break;
            case Action::Unmothball: act(ui, game::ScrapAction::Unmothball); break;
            case Action::SelfDestruct: act(ui, game::ScrapAction::SelfDestruct); break;
            case Action::FireOn: act(ui, game::ScrapAction::FireOn); break;
            case Action::ScrapFacilities: {
                const game::Colony* c = ownColony(ui, planet_);
                if (!c) break;
                std::vector<size_t> slots = slots_;
                std::sort(slots.rbegin(), slots.rend());  // highest slot first keeps the others valid
                int done = 0;
                for (size_t i : slots)
                    if (status_.issue(ui, game::cmd::Scrap{{}, planet_, static_cast<int32_t>(i)})) ++done;
                slots_.clear();
                if (done > 0) status_.ok(std::format("{} facilit{} scrapped.", done, done == 1 ? "y" : "ies"));
                break;
            }
        }
    }

    void popups(UiContext& ui) {
        if (confirm_.draw(ui)) perform(ui, confirmAction_);
        if (auto i = retrofit_.draw(ui); i && *i < retrofitDesigns_.size()) act(ui, game::ScrapAction::Retrofit, retrofitDesigns_[*i]);
        report_.draw(ui);
    }

    std::optional<game::Location> where_;
    game::ObjectId planet_;
    bool facilities_ = false;
    std::vector<game::VehicleId> selected_;
    std::vector<size_t> slots_;
    Status status_;
    Confirm confirm_;
    Action confirmAction_ = Action::Scrap;
    ListPicker retrofit_;
    std::vector<game::DesignId> retrofitDesigns_;
    ReportPopup report_;
};

// ---- Abandon Planet -------------------------------------------------------------------------------

// The Abandon Planet order (Ctrl+A, spec 06 §1.3, §2.8; spec 02 §5): the
// player confirms; the order is refused when more people live there than
// Settings allow; otherwise, when the planet has facilities, the player
// chooses whether to scrap them for a refund or leave them for a later owner.
class AbandonPlanetScreen final : public Screen {
public:
    explicit AbandonPlanetScreen(const ScreenArgs& args) : planet_(args.planet) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        const game::Colony* c = ownColony(ui, planet_);
        if (!c && step_ != Step::Notice) return false;
        const std::string name = ui.state().galaxy.object(planet_).name;
        switch (step_) {
            case Step::Confirm:
                if (const auto a = yesNoBox(ui, "Abandon Planet###abandon1",
                                            std::format("Abandon {}? Its people leave.", name))) {
                    if (!*a) return false;
                    const int64_t limit = ui.rules().setting("Maximum Population For Abandon Planet Order", 50);
                    if (c->totalPopulation() > limit) {
                        notice_ = std::format("{} cannot be abandoned: more than {}M people live there.", name, limit);
                        step_ = Step::Notice;
                    } else if (!c->facilities.empty()) {
                        step_ = Step::Scrap;
                    } else {
                        abandon(ui, false);
                    }
                }
                break;
            case Step::Scrap:
                if (const auto a = yesNoBox(ui, "Abandon Planet###abandon2",
                                            std::format("Scrap the {} facilit{} on {} first? Scrapping returns part of their cost now; "
                                                        "otherwise they stay, and the planet stays your colony with nobody living there.",
                                                        c->facilities.size(), c->facilities.size() == 1 ? "y" : "ies", name)))
                    abandon(ui, *a);
                break;
            case Step::Notice:
                if (noticeBox(ui, "Abandon Planet###abandon3", notice_)) return false;
                break;
            case Step::Done: break;
        }
        return step_ != Step::Done;
    }

private:
    enum class Step { Confirm, Scrap, Notice, Done };

    void abandon(UiContext& ui, bool scrap) {
        if (scrap) {
            // Highest slot first, so the others keep their places.
            const game::Colony* c = ownColony(ui, planet_);
            for (size_t i = c ? c->facilities.size() : 0; i-- > 0;)
                if (!ui.session.issue(game::cmd::Scrap{{}, planet_, static_cast<int32_t>(i)}).ok) break;
        }
        const game::CommandResult r = ui.session.issue(game::cmd::AbandonPlanet{planet_});
        if (r.ok) {
            step_ = Step::Done;
        } else {
            notice_ = r.error;
            step_ = Step::Notice;
        }
    }

    // A small message box in the middle of the frame (spec 06 §3.4 keys).
    static std::optional<bool> yesNoBox(UiContext& ui, const char* id, const std::string& text) {
        std::optional<bool> answer;
        if (!beginBox(ui, id)) return answer;
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::Spacing();
        const std::optional<bool> key = yesNoKey();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Yes", ImVec2(w, ui.px(26))) || key == true) answer = true;
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(w, ui.px(26))) || key == false) answer = false;
        ImGui::End();
        return answer;
    }

    static bool noticeBox(UiContext& ui, const char* id, const std::string& text) {
        if (!beginBox(ui, id)) return false;
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::Spacing();
        const bool ok = ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey();
        ImGui::End();
        return ok;
    }

    static bool beginBox(UiContext& ui, const char* id) {
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({420, 0}), ImGuiCond_Always);
        const bool open = ImGui::Begin(id, nullptr,
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags | ui.windowFlags());
        if (!open) {
            ImGui::End();
            return false;
        }
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
        ui.tagWindow(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
        return true;
    }

    game::ObjectId planet_;
    Step step_ = Step::Confirm;
    std::string notice_;
};

} // namespace

std::unique_ptr<Screen> makeScrap(const ScreenArgs& args) { return std::make_unique<ScrapScreen>(args); }
std::unique_ptr<Screen> makeAbandonPlanet(const ScreenArgs& args) { return std::make_unique<AbandonPlanetScreen>(args); }

} // namespace opense4::client::classic
