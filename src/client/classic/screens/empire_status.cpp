// Empire Status (F11) and the windows opened from it: Empire Options,
// Ministers, Systems To Avoid, Waypoints, Strategies and Repair Priorities
// (docs/spec/06 §1.2, docs/spec/02 §5-§7 and §10, docs/spec/04 §16).

#include "client/classic/quadrant_map.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/script/items.hpp"
#include "client/classic/widgets.hpp"
#include "game/ai_data.hpp"
#include "game/setup.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <utility>

namespace opense4::client::classic {

namespace {

namespace cmd = game::cmd;

const ImVec4 kErrorText{1.0f, 0.5f, 0.45f, 1.0f};

// Shows a failed command's reason until the next command.
struct CommandStatus {
    std::string error;
    void issue(UiContext& ui, game::Command c) {
        const game::CommandResult r = ui.session.issue(std::move(c));
        error = r.ok ? std::string{} : r.error;
    }
    void draw() const {
        if (!error.empty()) ImGui::TextColored(kErrorText, "%s", error.c_str());
    }
};

// Frame on which a window of this group handed the input to the main window
// (Waypoints -> Set); Empire Status closes too so the main window is not blocked.
int gPickFrame = -10;

bool pickRequestedRecently() { return gPickFrame >= ImGui::GetFrameCount() - 1; }

// ---- Empire Status -------------------------------------------------------------------------

// The original's layout (spec 06 §7 Q95, confirmed: binary), in window
// coordinates: block headings in the large font, silver, at x 20; row labels
// in label blue at x 30; the three amounts right-aligned at x 380, 460 and
// 540 in the resource colours, written in full; the resource icons only after
// the amounts of From Our Colonies; a label-blue rule from x 330 to 570 over
// each Total; Net Resources Per Turn a labelled row of its own, a positive
// amount with "+".

constexpr std::array<Icon, 3> kResourceIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
constexpr std::array<uint32_t, 3> kResourceColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
constexpr std::array<float, 3> kAmountX{380, 460, 540};

void statusText(UiContext& ui, ImVec2 origin, ImFont* font, float size, float lead, Vec2 at, ImU32 color, std::string_view text) {
    ImGui::GetWindowDrawList()->AddText(font, ui.fontPx(size), ImVec2(std::floor(origin.x + ui.px(at.x)), std::floor(origin.y + ui.px(at.y + lead))), color,
                                        text.data(), text.data() + text.size());
}

// One row at `y`: the label, then the three amounts; `icons` puts each
// resource's icon after its amount, `sign` writes "+" before a positive one.
void amountRow(UiContext& ui, ImVec2 origin, float y, const char* label, const game::Resources& r, bool icons = false, bool sign = false) {
    statusText(ui, origin, ui.fonts.regular, kTextSize, kTextLead, {30, y}, imColor(palette::kLabel), label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < 3; ++i) {
        const std::string text = (sign && r.v[i] > 0 ? "+" : "") + formatNumber(r.v[i]);
        const float w = ImGui::CalcTextSize(text.c_str()).x / ui.k();
        statusText(ui, origin, ui.fonts.regular, kTextSize, kTextLead, {kAmountX[i] - w, y}, imColor(kResourceColors[i]), text);
        if (icons)
            if (const Sprite icon = ui.art.icon16(kResourceIcons[i]))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(icon.tex.value)), ImVec2(origin.x + ui.px(kAmountX[i] + 2), origin.y + ui.px(y)),
                             ImVec2(origin.x + ui.px(kAmountX[i] + 18), origin.y + ui.px(y + 16)), {icon.uv.min.x, icon.uv.min.y},
                             {icon.uv.max.x, icon.uv.max.y});
    }
}

class EmpireStatusScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        if (pickRequestedRecently()) return false;
        Dialog d(ui, "Empire Status", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Empire& me = ui.me();
        const game::EconomyReport& eco = me.economy;
        d.beginContent(576);
        const ImVec2 origin = d.at({0, 0});
        // From Our Colonies is the colonies' production total, which in the
        // original holds the income of abilities and the minimum-income floor
        // (spec 02 counts them in production) and, for a computer player, its
        // bonus (spec 06 §7 Q95): our engine keeps those apart (otherIncome)
        // and adds them here.
        const game::Resources colonies = eco.colonies + eco.otherIncome;
        const game::Resources income = colonies + eco.trade + eco.tariffsIn + eco.remoteMining;
        const game::Resources expenses = eco.tariffsOut + eco.maintenance + eco.construction;
        const ImU32 silver = imColor(palette::kHeading);
        statusText(ui, origin, ui.fonts.bold, kTitleSize, kTitleLead, {20, 40}, silver, "Resource Production Per Turn");
        amountRow(ui, origin, 60, "From Our Colonies", colonies, true);
        amountRow(ui, origin, 80, "From Trade", eco.trade);
        amountRow(ui, origin, 100, "From Tariffs", eco.tariffsIn);
        amountRow(ui, origin, 120, "From Remote Mining", eco.remoteMining);
        amountRow(ui, origin, 150, "Total", income);
        statusText(ui, origin, ui.fonts.bold, kTitleSize, kTitleLead, {20, 180}, silver, "Resource Expenses Per Turn");
        amountRow(ui, origin, 200, "Tariffs", eco.tariffsOut);
        amountRow(ui, origin, 220, "Maintenance Cost", eco.maintenance);
        amountRow(ui, origin, 240, "Construction Queue Usage", eco.construction);
        amountRow(ui, origin, 270, "Total", expenses);
        amountRow(ui, origin, 310, "Net Resources Per Turn", income - expenses, false, true);
        statusText(ui, origin, ui.fonts.bold, kTitleSize, kTitleLead, {20, 350}, silver, "Resources in Treasury");
        amountRow(ui, origin, 370, "Current Total", me.stockpile);
        amountRow(ui, origin, 390, "Maximum Resource Storage", eco.storageCap);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const float y : {145.0f, 265.0f})
            dl->AddRectFilled(ImVec2(origin.x + ui.px(330), origin.y + ui.px(y)), ImVec2(origin.x + ui.px(570), origin.y + ui.px(y + 1)),
                              imColor(palette::kLabel));
        ImGui::SetCursorScreenPos(d.at({30, 420}));
        status_.draw();
        ImGui::Dummy(ImVec2(0, 0));

        // The original's column: the six windows, Change Email in the 12th slot
        // and Change Password in the 13th (observed, spec 07 session 3).
        d.beginButtons();
        if (d.button("Empire Options")) ui.open(ScreenId::EmpireOptions);
        if (d.button("Ministers")) ui.open(ScreenId::Ministers);
        if (d.button("Systems To Avoid")) ui.open(ScreenId::SystemsToAvoid);
        if (d.button("Waypoints")) ui.open(ScreenId::Waypoints);
        if (d.button("Strategies")) ui.open(ScreenId::Strategies);
        if (d.button("Repair Priorities")) ui.open(ScreenId::RepairPriorities);
        for (int gap = 0; gap < 5; ++gap) d.spacer();
        // Change Email asks for the empire's address, holding the current one;
        // OK stores it, the password is kept (spec 06 §7 Q95, confirmed: binary).
        if (d.button("Change Email")) {
            email_ = me.email;
            ImGui::OpenPopup("Change Email");
        }
        emailPopup(ui);
        if (d.button("Change Password")) {
            password_.clear();
            repeat_.clear();
            passwordError_.clear();
            ImGui::OpenPopup("Change Password");
        }
        passwordPopup(ui);
        d.close();
        return d.keepOpen();
    }

private:
    // The original's input box (spec 06 §7 Q95): "Please Enter Email Address",
    // the current address in the field, OK and Cancel. Ours is drawn as our
    // other input boxes are.
    void emailPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({360, 0}));
        if (!ImGui::BeginPopupModal("Change Email", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextColored(kLabelBlue, "Please Enter Email Address");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##email", email_, game::kMaxEmailBytes, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("OK", ImVec2(w, ui.px(26))) || enter) {
            status_.issue(ui, game::cmd::SetEmail{email_});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void passwordPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({360, 0}));
        if (!ImGui::BeginPopupModal("Change Password", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped("Players must enter this password before playing a turn for %s. Leave both fields empty to remove it.",
                           ui.me().name.c_str());
        ImGui::Spacing();
        ImGui::TextColored(kLabelBlue, "New password");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        inputString("##pw", password_, 64, ImGuiInputTextFlags_Password);
        ImGui::TextColored(kLabelBlue, "Repeat it");
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputString("##pw2", repeat_, 64, ImGuiInputTextFlags_Password);
        if (!passwordError_.empty()) ImGui::TextColored(kErrorText, "%s", passwordError_.c_str());
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("OK", ImVec2(w, ui.px(26))) || (!ImGui::IsWindowAppearing() && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
            if (password_ != repeat_) {
                passwordError_ = "The two entries differ.";
            } else if (auto value = ui.session.empirePasswordValue(password_); !value) {
                passwordError_ = value.error();
            } else {
                status_.issue(ui, cmd::SetEmpireOptions{.passwordHash = std::move(*value)});
                password_.clear();
                repeat_.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    CommandStatus status_;
    std::string password_, repeat_, passwordError_;
    std::string email_;
};

// ---- Empire Options --------------------------------------------------------------------------

// The facility marker rows of System Display (spec 06 §1.9), our wording.
constexpr std::array<const char*, game::kFacilityMarkerGroups> kMarkerRows{{
    "Markers R / S / Y: resupply depots, spaceports, space yards",
    "Markers Ca / Cc / Cv: atmosphere, conditions and value changers",
    "Markers St / Ft: ship and fleet training",
    "Markers Rc / Rr: component repair, resource reclamation",
    "Markers Sst / Sft: system-wide ship and fleet training",
    "Markers Spv / Spc: system-wide value and conditions changes",
    "Markers Sph / Spa: system-wide happiness and population changes",
    "Markers Scm / Sdm: system-wide combat and damage modifiers",
    "Markers Srm / Ssm: system-wide reproduction and shield modifiers",
    "Markers Spp / Src: system-wide plague prevention, reduced maintenance",
    "Markers Sbe / Sbi: system-wide bad event and bad intelligence chances",
    "Marker Slr: system-wide long range scanners",
}};

// Every row belongs to the empire and is saved with the game (spec 06 §1.9,
// confirmed: binary): the InterfaceOptions through cmd::SetInterfaceOptions,
// the colony type picker through cmd::SetEmpireOptions, Ship Movement and
// Ship Orders through cmd::SetEncounterOptions.
class EmpireOptionsScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Empire Options", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Empire& me = ui.me();
        game::InterfaceOptions o = ui.options();
        d.beginContent();
        ImGui::TextColored(kLabelBlue, "Options In Use");
        beginList(ui, "##options", ImVec2(0, 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        heading(ui, "General Options");
        lampToggle(ui, "Show the log at the start of each turn", &o.showLogAtTurnStart);
        lampToggle(ui, "Confirm ending the turn", &o.confirmEndTurn);
        lampToggle(ui, "Confirm scrapping", &o.confirmScrap);
        lampToggle(ui, "Confirm stellar manipulation", &o.confirmStellarManipulation);
        lampToggle(ui, "Confirm deleting a research project", &o.confirmDeleteResearch);
        lampToggle(ui, "Confirm deleting an intelligence project", &o.confirmDeleteIntel);
        lampToggle(ui, "Confirm deleting the first item of a construction queue", &o.confirmDeleteFirstQueueItem);
        bool choose = me.chooseColonyType;
        if (lampToggle(ui, "Pick the colony type when a colony is founded", &choose))
            status_.issue(ui, cmd::SetEmpireOptions{.chooseColonyType = choose});
        lampToggle(ui, "Note when a similar system-wide ability already exists", &o.noteSimilarAbilities);
        ImGui::Spacing();
        heading(ui, "Next / Previous");
        lampToggle(ui, "Skip ships under construction", &o.skipUnderConstruction);
        lampToggle(ui, "Skip damaged ships", &o.skipDamaged);
        lampToggle(ui, "Stop only once per location", &o.stopOncePerLocation);
        lampToggle(ui, "Skip ships in fleets", &o.skipInFleets);
        ImGui::Spacing();
        heading(ui, "Ship Movement");
        bool minefields = me.avoidTaggedMinefields;
        if (lampToggle(ui, "Avoid minefields", &minefields)) status_.issue(ui, cmd::SetEncounterOptions{.avoidTaggedMinefields = minefields});
        bool restricted = me.avoidRestrictedSystems;
        if (lampToggle(ui, "Avoid restricted systems", &restricted))
            status_.issue(ui, cmd::SetEncounterOptions{.avoidRestrictedSystems = restricted});
        ImGui::Spacing();
        heading(ui, "Ship Orders");
        const game::EncounterClear clear = me.clearOrdersOnEncounter;
        bool onEnemy = clear != game::EncounterClear::Never;
        bool onAny = clear == game::EncounterClear::Any;
        if (lampToggle(ui, "Clear orders on entering a system with an enemy", &onEnemy))
            status_.issue(ui, cmd::SetEncounterOptions{onEnemy ? game::EncounterClear::Enemy : game::EncounterClear::Never});
        if (lampToggle(ui, "Clear orders on entering a system with any other empire", &onAny))
            status_.issue(ui, cmd::SetEncounterOptions{onAny ? game::EncounterClear::Any : game::EncounterClear::Enemy});
        ImGui::Spacing();
        heading(ui, "System Display");
        lampToggle(ui, "Warp point names", &o.warpPointNames);
        lampToggle(ui, "Planet names", &o.planetNames);
        lampToggle(ui, "Colonizable planets", &o.colonizableMarkers);
        lampToggle(ui, "System grid", &o.systemGrid);
        lampToggle(ui, "Coordinate location", &o.coordinateLocation);
        for (int i = 0; i < game::kFacilityMarkerGroups; ++i) {
            const uint16_t bit = uint16_t(1u << i);
            bool on = (o.facilityMarkers & bit) != 0;
            if (lampToggle(ui, kMarkerRows[size_t(i)], &on)) o.facilityMarkers = uint16_t(on ? o.facilityMarkers | bit : o.facilityMarkers & ~bit);
        }
        ImGui::Spacing();
        heading(ui, "Galaxy Display");
        lampToggle(ui, "Show grid lines", &o.galaxyGridLines);
        lampToggle(ui, "Show warp lines", &o.galaxyWarpLines);
        ImGui::Spacing();
        heading(ui, "Latest Items");
        lampToggle(ui, "Only the latest items for construction", &o.latestConstructionOnly);
        lampToggle(ui, "Only the latest components for designs", &o.latestComponentsOnly);
        ImGui::Spacing();
        heading(ui, "Politics");
        lampToggle(ui, "Claim every system we colonize", &o.autoClaimColonized);
        status_.draw();
        endList(ui);
        if (!ui.setOptions(o)) status_.error = "The options cannot be changed now.";
        d.beginButtons();
        d.close();
        return d.keepOpen();
    }

private:
    CommandStatus status_;
};

// ---- Ministers -------------------------------------------------------------------------------

// The minister switches and style are part of the empire (spec 02 §10, spec
// 05 §7.1): every change is a cmd::SetMinisters, so the host and the AI see it.
class MinistersScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Ministers", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Empire& me = ui.me();
        const bool completeAi = me.ministerAll;
        d.beginContent();
        ImGui::BeginGroup();
        lamp(ui, completeAi, 16);
        ImGui::SameLine();
        ImGui::TextUnformatted(completeAi ? "Complete AI is on: the computer runs the whole empire."
                                         : "Complete AI is off: the ministers below handle only their own areas.");
        ImGui::EndGroup();
        ImGui::Spacing();
        const float col = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        // Room below the lists for the new-vehicle switch, the style switch and picker, and a status line.
        const float below = ImGui::GetFrameHeightWithSpacing() * 5 + ImGui::GetStyle().ItemSpacing.y * 2;
        const float listHeight = std::max(ui.px(200), ImGui::GetContentRegionAvail().y - below);
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) ImGui::SameLine();
            beginList(ui, pass == 0 ? "##global" : "##individual", ImVec2(col, listHeight), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
            heading(ui, pass == 0 ? "Empire-wide ministers" : "Individual ministers");
            wrappedDim(pass == 0 ? "Each takes over its whole area." : "Each acts on ships and planets with the minister flag set.");
            ImGui::Spacing();
            for (size_t i = 0; i < game::kMinisters; ++i) {
                const auto m = static_cast<game::Minister>(i);
                if (game::isGlobalMinister(m) != (pass == 0)) continue;
                const uint32_t bit = game::ministerBit(m);
                bool on = (me.ministers & bit) != 0;
                if (lampToggle(ui, std::string(game::displayName(m)).c_str(), &on))
                    status_.issue(ui, cmd::SetMinisters{.areas = on ? (me.ministers | bit) : (me.ministers & ~bit)});
            }
            endList(ui);
        }
        ImGui::Spacing();
        bool newVehicles = me.ministersForNewVehicles;
        if (lampToggle(ui, "Put newly built vehicles and launched units under their individual ministers", &newVehicles))
            status_.issue(ui, cmd::SetMinisters{.newVehicles = newVehicles});
        // A Ministers option in the original (spec 06 §1.2.1); it used to sit in Empire Status.
        bool minimal = me.aiMinimalChanges;
        if (lampToggle(ui, "In a simultaneous game, the computer changes nothing when it plays a missed turn for us", &minimal))
            status_.issue(ui, cmd::SetEmpireOptions{.aiMinimalChanges = minimal});
        styleChoice(ui, me);
        status_.draw();

        d.beginButtons();
        if (d.button("Select All")) status_.issue(ui, cmd::SetMinisters{.areas = game::kAllMinisters});
        if (d.button("Select None")) status_.issue(ui, cmd::SetMinisters{.areas = 0u});
        if (d.button("Indiv. On")) status_.issue(ui, cmd::SetMinisters{.individual = true});
        if (d.button("Indiv. Off")) status_.issue(ui, cmd::SetMinisters{.individual = false});
        d.spacer();
        if (d.tab("Complete AI On", completeAi, !completeAi)) status_.issue(ui, cmd::SetMinisters{.completeAi = true});
        if (d.tab("Complete AI Off", !completeAi, completeAi)) status_.issue(ui, cmd::SetMinisters{.completeAi = false});
        d.close();
        return d.keepOpen();
    }

private:
    CommandStatus status_;

    // The personality set the ministers' AI files come from (spec 05 §7.2).
    // OpenSE4 lets the player change it during the game.
    void styleChoice(UiContext& ui, const game::Empire& me) {
        bool raceStyle = me.useRaceMinisterStyle;
        if (lampToggle(ui, "Ministers follow our race's own style", &raceStyle)) status_.issue(ui, cmd::SetMinisters{.useRaceStyle = raceStyle});
        const std::vector<std::string> styles = game::ai::ministerStyles(ui.rules());
        ImGui::BeginDisabled(me.useRaceMinisterStyle);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Minister style");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui.px(220));
        const std::string current = me.ministerStyle.empty() ? std::string("Race") : me.ministerStyle;
        if (ImGui::BeginCombo("##ministerStyle", current.c_str())) {
            if (ImGui::Selectable("Race", me.ministerStyle.empty())) status_.issue(ui, cmd::SetMinisters{.style = std::string{}});
            for (const std::string& st : styles)
                if (ImGui::Selectable(st.c_str(), st == me.ministerStyle)) status_.issue(ui, cmd::SetMinisters{.style = st});
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
    }
};

// ---- Systems To Avoid ------------------------------------------------------------------------

class SystemsToAvoidScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Systems To Avoid", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        const bool claimTab = overlay_ == MapOverlay::AllyClaimed;
        d.beginContent();
        dimText(claimTab ? "Click a system to claim it for us, or to give up our claim."
                 : me.avoidRestrictedSystems
                     ? "Click a system to mark it as one our ships avoid, or to clear the mark."
                     : "Click a system to mark it as one to avoid, or to clear the mark. Routes ignore the marks while the "
                       "Empire Options switch to avoid them is off.");
        QuadrantMapOptions opt;
        opt.overlay = overlay_;
        opt.names = true;
        opt.avoidRings = overlay_ != MapOverlay::Avoid;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const QuadrantMapResult r = quadrantMap(ui, "##map", {avail.x / ui.k(), avail.y / ui.k() - 48}, opt);
        if (r.clicked) {
            if (claimTab) {
                const bool claimed = std::binary_search(me.claimedSystems.begin(), me.claimedSystems.end(), *r.clicked);
                status_.issue(ui, cmd::SetSystemFlags{.system = *r.clicked, .claim = !claimed});
            } else {
                const bool avoided = std::binary_search(me.systemsToAvoid.begin(), me.systemsToAvoid.end(), *r.clicked);
                status_.issue(ui, cmd::SetSystemFlags{.system = *r.clicked, .avoid = !avoided});
            }
        }
        // Status line: the hovered system, else the totals.
        if (r.hovered) {
            const game::SystemId sys = *r.hovered;
            std::string line = s.galaxy.system(sys).name;
            if (std::binary_search(me.systemsToAvoid.begin(), me.systemsToAvoid.end(), sys)) line += "  -  avoided";
            for (const game::Empire& e : s.empires)
                if (e.alive && std::binary_search(e.claimedSystems.begin(), e.claimedSystems.end(), sys) &&
                    (e.id == me.id || me.relation(e.id).contact))
                    line += std::format("  -  claimed by {}", e.id == me.id ? std::string("us") : e.name);
            ImGui::TextUnformatted(line.c_str());
        } else {
            ImGui::TextUnformatted(std::format("{} systems avoided, {} claimed by us", me.systemsToAvoid.size(), me.claimedSystems.size()).c_str());
        }
        status_.draw();

        d.beginButtons();
        if (d.tab("Avoid", overlay_ == MapOverlay::Avoid)) overlay_ = MapOverlay::Avoid;
        if (d.tab("Presence", overlay_ == MapOverlay::Presence)) overlay_ = MapOverlay::Presence;
        if (d.tab("Ally Claimed", overlay_ == MapOverlay::AllyClaimed)) overlay_ = MapOverlay::AllyClaimed;
        if (d.tab("Enemy Claimed", overlay_ == MapOverlay::EnemyClaimed)) overlay_ = MapOverlay::EnemyClaimed;
        d.spacer();
        if (claimTab) {
            if (d.button("Release All Claims", !me.claimedSystems.empty())) {
                const std::vector<game::SystemId> list = me.claimedSystems;
                for (game::SystemId sys : list) status_.issue(ui, cmd::SetSystemFlags{.system = sys, .claim = false});
            }
        } else if (d.button("Clear All", !me.systemsToAvoid.empty())) {
            const std::vector<game::SystemId> list = me.systemsToAvoid;
            for (game::SystemId sys : list) status_.issue(ui, cmd::SetSystemFlags{.system = sys, .avoid = false});
        }
        d.close();
        return d.keepOpen();
    }

private:
    MapOverlay overlay_ = MapOverlay::Avoid;
    CommandStatus status_;
};

// ---- Waypoints -------------------------------------------------------------------------------

std::string waypointName(const game::Waypoint& w, int slot) { return w.name.empty() ? std::format("Waypoint {}", slot) : w.name; }

class WaypointsScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Waypoints", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        const auto& wps = me.waypoints;
        const game::Waypoint& sel = wps[size_t(selected_)];
        d.beginContent();

        const float left = ui.px(330);
        ImGui::BeginChild("##left", ImVec2(left, 0), ImGuiChildFlags_None);
        heading(ui, "Waypoints");
        if (ImGui::BeginTable("##wps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ui.px(22));
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < int(wps.size()); ++i) {
                const game::Waypoint& w = wps[size_t(i)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                if (ImGui::Selectable(std::to_string(i).c_str(), selected_ == i, ImGuiSelectableFlags_SpanAllColumns)) selected_ = i;
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                if (w.set) ImGui::TextUnformatted(waypointName(w, i).c_str());
                else dimText("(not set)");
                ImGui::TableSetColumnIndex(2);
                if (w.set) ImGui::TextUnformatted(sectorName(s, w.location, ui.session.player()).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        heading(ui, "Ships heading there");
        beginList(ui, "##ships", ImVec2(0, ui.px(130)), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        int shown = 0;
        if (sel.set) {
            auto headingThere = [&](const std::vector<game::Order>& orders) {
                for (const game::Order& o : orders)
                    if ((o.kind == game::OrderKind::MoveToWaypoint && o.amount == selected_) ||
                        (o.kind == game::OrderKind::MoveTo && o.location == sel.location))
                        return true;
                return false;
            };
            for (const game::Fleet& f : s.fleets)
                if (f.owner == me.id && headingThere(game::fleetOrders(s, f))) {
                    ImGui::Text("%s (fleet, %zu ships)", f.name.c_str(), f.members.size());
                    ++shown;
                }
            for (const game::Vehicle& v : s.vehicles)
                if (v.owner == me.id && !v.fleet.valid() && headingThere(v.orders)) {
                    image(ui, vehicleMini(ui, v), {18, 18});
                    ImGui::SameLine();
                    ImGui::TextUnformatted(v.name.c_str());
                    ++shown;
                }
        }
        if (shown == 0) dimText("None");
        endList(ui);
        heading(ui, "Space yards sending new ships there");
        beginList(ui, "##yards", ImVec2(0, 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        int yards = 0;
        for (const auto& c : s.colonies)
            if (c && c->owner == me.id && c->queue.autoWaypoint == selected_) {
                ImGui::TextUnformatted(s.galaxy.object(c->planet).name.c_str());
                ++yards;
            }
        for (const game::Vehicle& v : s.vehicles)
            if (v.owner == me.id && v.queue.autoWaypoint == selected_) {
                ImGui::TextUnformatted(v.name.c_str());
                ++yards;
            }
        if (yards == 0) dimText("None");
        endList(ui);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginGroup();
        QuadrantMapOptions opt;
        opt.names = true;
        for (int i = 0; i < int(wps.size()); ++i)
            if (wps[size_t(i)].set) opt.tags.emplace_back(wps[size_t(i)].location.system, std::to_string(i));
        if (sel.set) opt.highlight.push_back(sel.location.system);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        quadrantMap(ui, "##map", {avail.x / ui.k(), avail.y / ui.k() - 24}, opt);
        if (sel.set) ImGui::Text("%s: %s", waypointName(sel, selected_).c_str(), sectorName(s, sel.location, ui.session.player()).c_str());
        else dimText("Set places a waypoint on a sector you pick in the main window.");
        status_.draw();
        ImGui::EndGroup();

        d.beginButtons();
        bool keep = true;
        if (d.button("Set")) {
            const int slot = selected_;
            std::string name = sel.set ? sel.name : std::format("Waypoint {}", slot);
            ui.requests.pickLocation = [session = &ui.session, opener = ui.opener, slot, name](game::Location where) {
                game::Waypoint w;
                w.name = name;
                w.location = where;
                w.set = true;
                session->issue(cmd::SetWaypoint{slot, w});
                if (opener) opener(ScreenId::Waypoints, ScreenArgs{.index = slot});
            };
            ui.requests.pickPrompt = std::format("Set {}: pick a sector", name);
            gPickFrame = ImGui::GetFrameCount();
            keep = false;
        }
        if (d.button("Delete", sel.set)) status_.issue(ui, cmd::SetWaypoint{selected_, std::nullopt});
        if (d.button("Rename", sel.set)) {
            renameDraft_ = waypointName(sel, selected_);
            ImGui::OpenPopup("Rename Waypoint");
        }
        renamePopup(ui);
        d.close();
        return keep && d.keepOpen();
    }

    explicit WaypointsScreen(int slot) : selected_(std::clamp(slot, 0, 9)) {}

private:
    void renamePopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({340, 0}));
        if (!ImGui::BeginPopupModal("Rename Waypoint", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        inputString("##name", renameDraft_, 40);
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if ((ImGui::Button("OK", ImVec2(w, ui.px(26))) || (!ImGui::IsWindowAppearing() && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) && !renameDraft_.empty()) {
            game::Waypoint wp = ui.me().waypoints[size_t(selected_)];
            wp.name = renameDraft_;
            status_.issue(ui, cmd::SetWaypoint{selected_, wp});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    int selected_ = 0;
    std::string renameDraft_;
    CommandStatus status_;
};

// ---- Strategies ------------------------------------------------------------------------------

// Value identifiers of the strategy fields (docs/spec/04 §16).
// Movement identifiers from the closest engagement to the most distant, with our descriptions.
constexpr std::array<const char*, 8> kMovement{"Point Blank",       "Short Weapons Range", "Optimal Weapons Range",     "Maximum Weapons Range",
                                               "Board Enemy Ships", "Ram",                 "Drop Troops (if carrying)", "Don't Get Hurt"};
constexpr std::array<const char*, 8> kMovementHelp{
    "Get as close as possible.",
    "Fight from 1 to 3 squares away while taking as little fire as possible.",
    "Take the square with the best ratio of damage dealt to damage taken.",
    "Keep at the range of our longest weapon.",
    "Close to an adjacent square and try to capture the target.",
    "Ram the target.",
    "Close on a planet and land troops; otherwise use the secondary strategy.",
    "Stay where no enemy can fire on us."};
// Targeting identifiers, grouped in opposite pairs.
constexpr std::array<const char*, 12> kTargeting{"Has Weapons", "Does Not Have Weapons", "Strongest", "Weakest", "Most Damaged", "Least Damaged",
                                                 "Nearest",     "Farthest",              "Largest",   "Smallest", "Fastest",      "Slowest"};

bool isTrue(std::string_view v) {
    std::string l(v);
    for (char& c : l) c = char(std::tolower(static_cast<unsigned char>(c)));
    return l == "true" || l == "yes" || l == "1";
}

int toInt(std::string_view v, int fallback = 0) {
    int out = fallback;
    while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
    std::from_chars(v.data(), v.data() + v.size(), out);
    return out;
}

std::string* valueOf(ruleset::CombatStrategy& st, std::string_view key) {
    for (auto& [k, v] : st.settings)
        if (k == key) return &v;
    return nullptr;
}

std::string& valueRef(ruleset::CombatStrategy& st, std::string_view key, std::string_view fallback) {
    if (std::string* v = valueOf(st, key)) return *v;
    st.settings.emplace_back(std::string(key), std::string(fallback));
    return st.settings.back().second;
}

// A combo over identifiers (keeps an unknown current value selectable).
template <size_t N>
bool identifierCombo(const char* id, std::string& value, const std::array<const char*, N>& options) {
    bool changed = false;
    if (ImGui::BeginCombo(id, value.c_str())) {
        bool known = false;
        for (const char* o : options) {
            known = known || value == o;
            if (ImGui::Selectable(o, value == o)) {
                changed = value != o;
                value = o;
            }
        }
        if (!known && !value.empty()) ImGui::Selectable(value.c_str(), true);
        ImGui::EndCombo();
    }
    return changed;
}

// A slider that commits only when released (one command per edit).
bool intSlider(const char* id, std::string& value, int lo, int hi, const char* format = "%d") {
    ImGui::PushID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID draftKey = ImGui::GetID("draft"), activeKey = ImGui::GetID("active");
    const int current = toInt(value);
    int v = storage->GetBool(activeKey, false) ? storage->GetInt(draftKey, current) : current;
    ImGui::SliderInt("##v", &v, lo, hi, format, ImGuiSliderFlags_AlwaysClamp);
    script::reportItem(id);   // input scripts find the slider by its setting
    storage->SetInt(draftKey, v);
    storage->SetBool(activeKey, ImGui::IsItemActive());
    const bool commit = ImGui::IsItemDeactivatedAfterEdit() && v != current;
    ImGui::PopID();
    if (commit) value = std::to_string(v);
    return commit;
}

bool boolField(UiContext& ui, const char* label, std::string& value) {
    bool on = isTrue(value);
    if (!lampToggle(ui, label, &on)) return false;
    value = on ? "True" : "False";
    return true;
}

void fieldLabel(UiContext& ui, const char* label, float width = 200) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kLabelBlue, "%s", label);
    ImGui::SameLine(ui.px(width));
}

class StrategiesScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Strategies", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        const auto& list = me.strategies;
        selected_ = std::clamp(selected_, 0, std::max(0, int(list.size()) - 1));
        d.beginContent();

        beginList(ui, "##list", ImVec2(ui.px(220), 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        heading(ui, "Strategies");
        for (int i = 0; i < int(list.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(list[size_t(i)].name.c_str(), selected_ == i)) selected_ = i;
            ImGui::PopID();
        }
        endList(ui);
        ImGui::SameLine();
        ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (list.empty()) {
            dimText("This empire has no strategies. Add one.");
        } else {
            ruleset::CombatStrategy st = list[size_t(selected_)];
            bool changed = false;
            // Name (committed when the field is left).
            if (nameFor_ != selected_ || !editingName_) {
                nameDraft_ = st.name;
                nameFor_ = selected_;
            }
            fieldLabel(ui, "Name", 80);
            ImGui::SetNextItemWidth(-FLT_MIN);
            inputString("##name", nameDraft_, 60);
            editingName_ = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit() && !nameDraft_.empty() && nameDraft_ != st.name) {
                st.name = nameDraft_;
                changed = true;
            }
            int designs = 0, fleets = 0;
            for (game::DesignId id : me.designs) designs += s.design(id).strategy == uint32_t(selected_) ? 1 : 0;
            for (const game::Fleet& f : s.fleets) fleets += f.owner == me.id && f.strategy == uint32_t(selected_) ? 1 : 0;
            dimText(std::format("Default strategy of {} designs, used by {} fleets", designs, fleets).c_str());
            ImGui::Separator();
            ImGui::BeginChild("##fields", ImVec2(0, 0), ImGuiChildFlags_None);
            switch (page_) {
                case 0: changed |= movementPage(ui, st); break;
                case 1: changed |= firingPage(ui, st); break;
                case 2: changed |= launchingPage(ui, st); break;
                default: changed |= formationPage(ui, st); break;
            }
            ImGui::EndChild();
            if (changed) status_.issue(ui, cmd::SetStrategy{selected_, st});
        }
        ImGui::EndChild();

        d.beginButtons();
        static constexpr std::array<const char*, 4> kPages{"Movement", "Firing", "Launching", "Formation"};
        for (int i = 0; i < 4; ++i)
            if (d.tab(kPages[size_t(i)], page_ == i)) page_ = i;
        d.spacer();
        if (d.button("Add")) {
            ruleset::CombatStrategy st = !ui.rules().data().combatStrategies.empty() ? ui.rules().data().combatStrategies.front()
                                         : !list.empty()                            ? list[size_t(selected_)]
                                                                                    : ruleset::CombatStrategy{};
            st.name = "New Strategy";
            status_.issue(ui, cmd::SetStrategy{-1, st});
            if (status_.error.empty()) selected_ = int(ui.me().strategies.size()) - 1;
        }
        if (d.button("Copy", !list.empty())) {
            ruleset::CombatStrategy st = list[size_t(selected_)];
            st.name = "Copy of " + st.name;
            status_.issue(ui, cmd::SetStrategy{-1, st});
            if (status_.error.empty()) selected_ = int(ui.me().strategies.size()) - 1;
        }
        if (d.button("Remove", list.size() > 1)) status_.issue(ui, cmd::SetStrategy{selected_, {}, true});
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        status_.draw();
        ImGui::PopTextWrapPos();
        d.close();
        return d.keepOpen();
    }

private:
    bool movementPage(UiContext& ui, ruleset::CombatStrategy& st) {
        bool changed = false;
        for (const char* key : {"Primary Movement Strategy", "Secondary Movement Strategy"}) {
            std::string& v = valueRef(st, key, kMovement[2]);
            fieldLabel(ui, key == std::string_view("Primary Movement Strategy") ? "Primary movement" : "Secondary movement", 170);
            ImGui::SetNextItemWidth(ui.px(250));
            changed |= identifierCombo(std::format("##{}", key).c_str(), v, kMovement);
            for (size_t i = 0; i < kMovement.size(); ++i)
                if (v == kMovement[i]) {
                    ImGui::SetCursorPosX(ui.px(170));
                    wrappedDim(kMovementHelp[i]);
                }
            ImGui::Spacing();
        }
        wrappedDim("Ships use the secondary strategy when the primary one is impossible, for example when a troop "
                   "strategy has no troops or no planet to land on.");
        return changed;
    }

    bool firingPage(UiContext& ui, ruleset::CombatStrategy& st) {
        bool changed = false;
        heading(ui, "Target choice");
        for (int i = 1; i <= 4; ++i) {
            const std::string key = std::format("Targeting Priority {}", i);
            fieldLabel(ui, std::format("Priority {}", i).c_str(), 110);
            ImGui::SetNextItemWidth(ui.px(200));
            changed |= identifierCombo(("##" + key).c_str(), valueRef(st, key, kTargeting[6]), kTargeting);
        }
        changed |= boolField(ui, "Rank targets by type before the priorities above", valueRef(st, "Use Type Priority First", "False"));
        ImGui::Spacing();
        heading(ui, "Target types");
        std::vector<std::string> categories;
        for (const auto& [k, v] : st.settings)
            if (k.starts_with("Type Priority ")) categories.push_back(k.substr(14));
        const int ranks = std::max(1, int(categories.size()));
        if (ImGui::BeginTable("##types", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("Rank (1 = first)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Never fire on", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableHeadersRow();
            for (const std::string& cat : categories) {
                ImGui::PushID(cat.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(cat.c_str());
                ImGui::TableSetColumnIndex(1);
                std::string& rank = valueRef(st, "Type Priority " + cat, "1");
                ImGui::SetNextItemWidth(ui.px(70));
                if (ImGui::BeginCombo("##rank", rank.c_str())) {
                    for (int r = 1; r <= ranks; ++r)
                        if (ImGui::Selectable(std::to_string(r).c_str(), toInt(rank) == r) && toInt(rank) != r) {
                            // The type that had this rank takes the old one, so ranks stay unique.
                            for (auto& [k, v] : st.settings)
                                if (k.starts_with("Type Priority ") && toInt(v) == r) v = rank;
                            rank = std::to_string(r);
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                ImGui::TableSetColumnIndex(2);
                changed |= boolField(ui, "", valueRef(st, "Dont Fire On " + cat, "False"));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        heading(ui, "Switching targets");
        wrappedDim("Move on to a fresh target once the current one has taken this share of its structure.");
        for (const char* what : {"Ship", "Planet", "Fighter Group", "Satellite Group"}) {
            const std::string key = std::format("Damage Percent Per {}", what);
            fieldLabel(ui, what, 150);
            ImGui::SetNextItemWidth(ui.px(220));
            changed |= intSlider(key.c_str(), valueRef(st, key, "100"), 0, 100, "%d%%");
        }
        changed |= boolField(ui, "Keep firing until the target has no working weapons", valueRef(st, "Damage Until All Weapons Gone", "False"));
        return changed;
    }

    bool launchingPage(UiContext& ui, ruleset::CombatStrategy& st) {
        bool changed = false;
        heading(ui, "Fighters");
        fieldLabel(ui, "Fighters per group", 170);
        ImGui::SetNextItemWidth(ui.px(220));
        changed |= intSlider("Fighters Launch Group Amount", valueRef(st, "Fighters Launch Group Amount", "10"), 1, 100);
        wrappedDim("How many fighters leave a carrier together as one group.");
        // Anything the other pages do not cover (e.g. fields added by a mod).
        bool header = false;
        for (auto& [k, v] : st.settings) {
            if (k.starts_with("Primary Movement") || k.starts_with("Secondary Movement") || k.starts_with("Targeting Priority") ||
                k.starts_with("Use Type Priority") || k.starts_with("Type Priority") || k.starts_with("Dont Fire On") ||
                k.starts_with("Damage ") || k.starts_with("Break Formation") || k == "Fighters Launch Group Amount")
                continue;
            if (!header) {
                ImGui::Spacing();
                heading(ui, "Other settings");
                header = true;
            }
            fieldLabel(ui, k.c_str(), 250);
            ImGui::SetNextItemWidth(ui.px(180));
            ImGui::PushID(k.c_str());
            std::string draft = v;
            inputString("##v", draft, 60, ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemDeactivatedAfterEdit() && draft != v) {
                v = draft;
                changed = true;
            }
            ImGui::PopID();
        }
        return changed;
    }

    bool formationPage(UiContext& ui, ruleset::CombatStrategy& st) {
        bool changed = false;
        heading(ui, "Leave the formation in combat");
        wrappedDim("Pieces of a lit type break away from their fleet's formation during a battle and fight on their own.");
        ImGui::Spacing();
        for (auto& [k, v] : st.settings)
            if (k.starts_with("Break Formation ")) changed |= boolField(ui, k.c_str() + 16, v);
        return changed;
    }

    int selected_ = 0;
    int page_ = 0;
    std::string nameDraft_;
    int nameFor_ = -1;
    bool editingName_ = false;
    CommandStatus status_;
};

// ---- Repair Priorities -----------------------------------------------------------------------

class RepairPrioritiesScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Repair Priorities", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Rules& rules = ui.rules();
        std::vector<std::string> order = ui.me().repairPriorities;
        std::vector<std::string> available;
        auto offer = [&](const std::string& g) {
            if (!g.empty() && std::find(order.begin(), order.end(), g) == order.end() &&
                std::find(available.begin(), available.end(), g) == available.end())
                available.push_back(g);
        };
        for (const std::string& g : rules.data().names.repairPriorities) offer(g);
        for (const ruleset::Component& c : rules.data().components) offer(c.generalGroup);
        bool changed = false;
        selected_ = std::min(selected_, int(order.size()) - 1);

        d.beginContent();
        wrappedDim("Damaged ships get their components fixed group by group in this order. Click a group on the left to add it; "
                   "groups not in the list are repaired last.");
        const float col = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        beginList(ui, "##available", ImVec2(col, 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        heading(ui, "Component groups");
        for (const std::string& g : available)
            if (ImGui::Selectable(g.c_str())) {
                order.push_back(g);
                selected_ = int(order.size()) - 1;
                changed = true;
            }
        if (available.empty()) dimText("Every group is in the list.");
        endList(ui);
        ImGui::SameLine();
        beginList(ui, "##order", ImVec2(0, 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        heading(ui, "Repair order");
        for (int i = 0; i < int(order.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(std::format("{}.  {}", i + 1, order[size_t(i)]).c_str(), selected_ == i)) selected_ = i;
            ImGui::PopID();
        }
        if (order.empty()) dimText("Empty: components are repaired in design order.");
        status_.draw();
        endList(ui);

        d.beginButtons();
        const bool hasSel = selected_ >= 0 && selected_ < int(order.size());
        if (d.button("Move Up", hasSel && selected_ > 0)) {
            std::swap(order[size_t(selected_)], order[size_t(selected_ - 1)]);
            --selected_;
            changed = true;
        }
        if (d.button("Move Down", hasSel && selected_ + 1 < int(order.size()))) {
            std::swap(order[size_t(selected_)], order[size_t(selected_ + 1)]);
            ++selected_;
            changed = true;
        }
        if (d.button("Remove", hasSel)) {
            order.erase(order.begin() + selected_);
            changed = true;
        }
        d.spacer();
        if (d.button("Remove All", !order.empty())) {
            order.clear();
            changed = true;
        }
        if (d.button("Default Order")) {
            order = rules.data().names.repairPriorities;
            changed = true;
        }
        if (changed) status_.issue(ui, cmd::SetRepairPriorities{order});
        d.close();
        return d.keepOpen();
    }

private:
    int selected_ = -1;
    CommandStatus status_;
};

} // namespace

std::unique_ptr<Screen> makeEmpireStatus(const ScreenArgs&) { return std::make_unique<EmpireStatusScreen>(); }
std::unique_ptr<Screen> makeEmpireOptions(const ScreenArgs&) { return std::make_unique<EmpireOptionsScreen>(); }
std::unique_ptr<Screen> makeMinisters(const ScreenArgs&) { return std::make_unique<MinistersScreen>(); }
std::unique_ptr<Screen> makeSystemsToAvoid(const ScreenArgs&) { return std::make_unique<SystemsToAvoidScreen>(); }
std::unique_ptr<Screen> makeWaypoints(const ScreenArgs& args) { return std::make_unique<WaypointsScreen>(std::max(0, args.index)); }
std::unique_ptr<Screen> makeStrategies(const ScreenArgs&) { return std::make_unique<StrategiesScreen>(); }
std::unique_ptr<Screen> makeRepairPriorities(const ScreenArgs&) { return std::make_unique<RepairPrioritiesScreen>(); }

} // namespace opense4::client::classic
