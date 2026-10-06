#include "client/classic/screens/setup_empire.hpp"

#include "client/classic/screens/list_widgets.hpp"
#include "client/script/items.hpp"
#include "datafile/datafile.hpp"
#include "game/economy.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>

namespace opense4::client::classic::setup {

namespace {

constexpr std::array<const char*, static_cast<size_t>(EmpirePage::Count)> kPageTitles{
    "General", "Environment", "Culture", "Characteristics", "Advanced Traits", "Description"};

std::string squash(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

std::string signedPercent(int v) { return v == 0 ? std::string("0") : std::format("{:+}%", v); }

// The racial effect each characteristic feeds (spec 02 §8.1, §8.2), as the
// Characteristics page names it after the value (our wording).
constexpr std::array<const char*, game::kCharacteristics> kEffects{
    "Ground Combat", "Research",        "Intelligence", "Environmental Resistance", "Reproduction",
    "Happiness",     "Ship Attack",     "Ship Defense", "Trade",                    "Mineral Production",
    "Organic Production", "Radioactive Production", "Space Yard Rate", "Repair", "Maintenance"};

// The word for a characteristic's level beside its value; the original shows
// "Average" at 100 %, the other words are ours (inferred).
const char* levelWord(int v) {
    if (v <= 60) return "Very Poor";
    if (v <= 80) return "Poor";
    if (v < 100) return "Below Average";
    if (v == 100) return "Average";
    if (v < 120) return "Above Average";
    if (v < 140) return "Good";
    return "Excellent";
}

int indexOf(const std::vector<std::string>& list, std::string_view v) {
    for (size_t i = 0; i < list.size(); ++i)
        if (datafile::keysEqual(list[i], v)) return static_cast<int>(i);
    return -1;
}

} // namespace

const char* pageTitle(EmpirePage p) { return kPageTitles[static_cast<size_t>(p)]; }

std::optional<EmpirePage> empirePageFromName(std::string_view name) {
    const std::string want = squash(name);
    if (want.empty()) return std::nullopt;
    for (size_t i = 0; i < kPageTitles.size(); ++i)
        if (squash(kPageTitles[i]) == want) return static_cast<EmpirePage>(i);
    for (size_t i = 0; i < kPageTitles.size(); ++i)
        if (squash(kPageTitles[i]).find(want) != std::string::npos) return static_cast<EmpirePage>(i);
    return std::nullopt;
}

EmpireEditor::EmpireEditor(std::shared_ptr<const game::Rules> rules, EmpireDraft draft, int racialPoints, bool isNew)
    : rules_(std::move(rules)), draft_(std::move(draft)), racialPoints_(racialPoints), isNew_(isNew) {
    const auto& all = rules_->racePresets();
    for (size_t i = 0; i < all.size(); ++i)
        if (!all[i].neutral) presets_.push_back(i);
    for (size_t k = 0; k < presets_.size(); ++k)
        if (datafile::keysEqual(all[presets_[k]].folder, draft_.race.style)) presetPos_ = k;
    surfaces_ = surfacesInSetupOrder(*rules_);
    atmospheres_ = atmospheresInSetupOrder(*rules_);
    designFiles_ = designNameFiles(*rules_);
    ministerStyles_ = ministerStyleChoices(*rules_);
    offersPlayers_ = offersComputerPlayers(*rules_);
}

const ruleset::RacePreset* EmpireEditor::preset() const { return presetOf(rules(), draft_.setup); }

void EmpireEditor::chooseStyle(size_t position) {
    // One click moves one style; only the pictures change (spec 07 session 5).
    if (presets_.empty()) return;
    presetPos_ = std::min(position, presets_.size() - 1);
    const ruleset::RacePreset& p = rules().racePresets()[presets_[presetPos_]];
    draft_.race.style = p.folder;
    draft_.setup.preset = p.folder;
}

void EmpireEditor::resetToTier(int tier) {
    // Preset Build (OpenSE4): the race, names and leader of the style's preset.
    if (const ruleset::RacePreset* p = preset()) {
        EmpireDraft next = draftFromPreset(rules(), *p, tier);
        next.setup.kind = draft_.setup.kind;
        next.setup.passwordHash = draft_.setup.passwordHash;
        next.setup.email = draft_.setup.email;
        next.setup.color = draft_.setup.color;
        next.setup.ministerStyle = draft_.setup.ministerStyle;
        next.setup.useRaceMinisterStyle = draft_.setup.useRaceMinisterStyle;
        next.setup.experience = draft_.setup.experience;
        next.setup.controller = draft_.setup.controller;
        draft_ = std::move(next);
    }
}

void EmpireEditor::pointsLine(SetupArea& a, float y) {
    // "Racial Points Available:" and left/budget right-aligned near x 783.
    const int left = racialPointsLeft(rules(), draft_.race, racialPoints_);
    a.heading({571, y}, "Racial Points Available:");
    a.textRight({783, y}, std::format("{}/{}", left, racialPoints_), left < 0 ? 0xff6050 : kWhite);
}

void EmpireEditor::openPicker(Pick what) {
    const auto& names = rules().data().names;
    picking_ = what;
    switch (what) {
        case Pick::EmpireName: pickRows_ = names.empireNames; picker_.open("Select Empire Name", "Empire Names", pickRows_); break;
        case Pick::EmpireType: pickRows_ = names.empireTypes; picker_.open("Select Empire Type", "Empire Types", pickRows_); break;
        case Pick::EmperorTitle: pickRows_ = names.emperorTitles; picker_.open("Select Emperor Title", "Emperor Titles", pickRows_); break;
        case Pick::EmperorName: pickRows_ = names.emperorNames; picker_.open("Select Emperor Name", "Emperor Names", pickRows_); break;
        case Pick::DesignNames: pickRows_ = designFiles_; picker_.open("Select Design Name File", "Files", pickRows_); break;
        case Pick::MinisterStyle: pickRows_ = ministerStyles_; picker_.open("Select Minister Style", "Styles", pickRows_); break;
        case Pick::None: break;
    }
}

void EmpireEditor::pickerResult(int row) {
    if (row < 0 || static_cast<size_t>(row) >= pickRows_.size()) return;
    const std::string& v = pickRows_[static_cast<size_t>(row)];
    switch (picking_) {
        case Pick::EmpireName: draft_.setup.name = v; break;
        case Pick::EmpireType: draft_.setup.empireType = v; break;
        case Pick::EmperorTitle: draft_.setup.leaderTitle = v; break;
        case Pick::EmperorName: draft_.setup.leaderName = v; break;
        case Pick::DesignNames: draft_.race.designNameFile = v; break;
        case Pick::MinisterStyle: pickMinisterStyle(draft_.setup, v); break;
        case Pick::None: break;
    }
    picking_ = Pick::None;
}

EmpireEditor::Result EmpireEditor::draw(MenuContext& ctx) {
    Result result = Result::Editing;
    SetupArea a(ctx, "Empire Setup###empiresetup", "Empire Setup", Decoration::EmpireSetup);
    if (!a.open()) return result;

    switch (page_) {
        case EmpirePage::General: pageGeneral(a); break;
        case EmpirePage::Environment: pageEnvironment(a); break;
        case EmpirePage::Culture: pageCulture(a); break;
        case EmpirePage::Characteristics: pageCharacteristics(a); break;
        case EmpirePage::Traits: pageTraits(a); break;
        case EmpirePage::Description: pageDescription(a); break;
        case EmpirePage::Count: break;
    }
    if (picker_.isOpen()) pickerResult(picker_.draw(ctx));
    if (players_.isOpen()) {
        std::optional<game::Controller> own = ownComputerPlayer(draft_.setup);
        PlayerPicker::Options o;
        o.title = "Computer Player";
        o.question = std::format("Who plays {}? The game's choice is {}.", draft_.setup.name.empty() ? std::string("this empire") : draft_.setup.name,
                                 gameChoice_);
        o.gameChoiceRow = true;
        o.gameChoice = gameChoice_;
        if (players_.draw(ctx, rules(), own, o)) setOwnComputerPlayer(draft_.setup, own);
    }

    for (size_t i = 0; i < kPageTitles.size(); ++i)
        if (a.pageButton(static_cast<int>(i), kPageTitles[i], page_ == static_cast<EmpirePage>(i))) page_ = static_cast<EmpirePage>(i);
    if (!error_.empty()) a.status(error_, kBad);

    if (a.beginButton(isNew_ ? "Create Empire" : "Save Empire")) {
        EmpireDraft d = draft_;
        if (!password_.empty()) d.setup.passwordHash = hashPassword(password_);
        d.setup.email = game::cleanEmail(d.setup.email);
        auto done = finishDraft(rules(), d, racialPoints_);
        if (done) {
            result_ = std::move(*done);
            result = Result::Created;
        } else {
            error_ = done.error();
        }
    }
    if (a.cancelButton()) result = Result::Cancelled;
    return result;
}

// ---- General ------------------------------------------------------------------------------------

void EmpireEditor::pageGeneral(SetupArea& a) {
    const auto& names = rules().data().names;
    // The first four rows: an edit box and a ▽ button that opens a picker.
    struct Row {
        const char* label;
        float y;
        std::string* value;
        Pick pick;
        const std::vector<std::string>* list;
    };
    const std::array<Row, 4> rows{{
        {"Empire Name", 21, &draft_.setup.name, Pick::EmpireName, &names.empireNames},
        {"Empire Type", 45, &draft_.setup.empireType, Pick::EmpireType, &names.empireTypes},
        {"Emperor Title", 69, &draft_.setup.leaderTitle, Pick::EmperorTitle, &names.emperorTitles},
        {"Emperor Name", 93, &draft_.setup.leaderName, Pick::EmperorName, &names.emperorNames},
    }};
    for (const Row& r : rows) {
        a.heading({231, r.y}, r.label);
        ImGui::PushID(r.label);
        a.edit("##edit", {406, r.y - 5}, {561, r.y + 14}, *r.value);
        if (a.dropButton("##pick", {566, r.y - 5}, !r.list->empty())) openPicker(r.pick);
        ImGui::PopID();
    }
    a.heading({231, 117}, "Password");
    a.edit("##password", {406, 112}, {585, 131}, password_, ImGuiInputTextFlags_Password);
    if (!draft_.setup.passwordHash.empty() && password_.empty()) a.text({590, 117}, "(set)", kExplainRgb, Face::Small);
    a.heading({231, 141}, "Email");
    a.edit("##email", {406, 136}, {585, 155}, draft_.setup.email);

    // The style strip: left arrow, the race portrait, the ship of the same
    // style, right arrow; the style's folder in brackets under it.
    a.heading({231, 165}, "Race Portrait \\ Ship Style");
    if (a.sideArrow("##prevstyle", {406, 160}, {422, 288}, true, presetPos_ > 0)) chooseStyle(presetPos_ - 1);
    a.picture(a.ctx().art.racePortrait(draft_.race.style), {422, 160}, {550, 288}, true);
    Sprite ship;
    for (const auto& v : rules().data().vehicleSizes)
        if (v.type == ruleset::VehicleType::Ship && !ship) ship = a.ctx().art.shipPortrait(draft_.race.style, v);
    a.picture(ship, {550, 160}, {678, 288}, true);
    if (a.sideArrow("##nextstyle", {678, 160}, {694, 288}, false, presetPos_ + 1 < presets_.size())) chooseStyle(presetPos_ + 1);
    const std::string folder = std::format("[{}]", draft_.race.style);
    a.text({550 - a.textWidth(folder) * 0.5f, 297}, folder);

    a.heading({231, 317}, "Design Name File");
    a.text({406, 317}, draft_.race.designNameFile.empty() ? std::string("(none)") : draft_.race.designNameFile);
    if (a.dropButton("##designnames", {566, 312}, !designFiles_.empty())) openPicker(Pick::DesignNames);
    a.heading({231, 341}, "Minister Style");
    a.text({406, 341}, draft_.setup.ministerStyle.empty() ? std::string("(the race's own)") : draft_.setup.ministerStyle,
           draft_.setup.useRaceMinisterStyle ? kDimTextRgb : kWhite);
    if (a.dropButton("##ministerstyle", {566, 336}, !draft_.setup.useRaceMinisterStyle && !ministerStyles_.empty())) openPicker(Pick::MinisterStyle);

    // Computer Controlled: dim in the original; OpenSE4 lets a listed empire
    // be a computer player (the label says which).
    a.heading({231, 365}, "Computer Controlled");
    bool computer = draft_.setup.kind == game::PlayerKind::Computer;
    if (a.checkBox("##computer", {408, 362}, computer ? "Controlled by Computer" : "Controlled by Player", computer))
        draft_.setup.kind = computer ? game::PlayerKind::Computer : game::PlayerKind::Human;
    // OpenSE4's own, when the game's mods offer computer players: who plays
    // this computer empire (the game's choice unless it has its own).
    if (offersPlayers_ && computer) {
        a.heading({231, 413}, "Computer Player");
        const std::optional<game::Controller> own = ownComputerPlayer(draft_.setup);
        const std::string shown = own ? computerPlayerName(rules(), *own) : std::string("The game's choice");
        a.text({406, 413}, elided(shown, a.px(155)));   // the body face is the window's
        script::reportItem("Computer player: " + shown, a.at({406, 408}), a.at({561, 427}));   // input scripts read it
        if (a.dropButton("##computerplayer", {566, 408})) players_.open();
    }
    a.heading({231, 389}, "Use Race Minister Style");
    bool raceStyle = draft_.setup.useRaceMinisterStyle;
    if (a.checkBox("##racestyle", {408, 386}, raceStyle ? "Using Race Style" : "Using Selected Style", raceStyle))
        setUseRaceMinisterStyle(draft_.setup, raceStyle);
    a.heading({231, 437}, "Experience Points");
    a.text({405, 437}, std::to_string(draft_.setup.experience));
    a.heading({231, 461}, "Race Age");
    a.text({405, 461}, game::economy::raceAge(draft_.setup.experience));

    // OpenSE4's own: the style's preset builds and the race's name.
    if (const ruleset::RacePreset* p = preset(); p && !p->tiers.empty()) {
        a.heading({604, 21}, "Preset Build");
        const std::vector<int> costs = tierCosts(rules(), *p);
        std::vector<std::string> builds;
        int matching = -1;
        for (size_t t = 0; t < costs.size(); ++t) {
            builds.push_back(std::format("Build {} ({} pts)", t + 1, costs[t]));
            if (matching < 0 && sameRace(draft_.race, game::raceFromPreset(rules(), *p, static_cast<int>(t)))) matching = static_cast<int>(t);
        }
        int chosen = matching;
        if (a.lampList("##builds", {604, 32}, {783, 32 + 18.0f * float(builds.size()) + 1}, chosen, std::span<const std::string>(builds)))
            resetToTier(chosen);
    }
    a.heading({604, 117}, "Race Name");
    a.edit("##racename", {604, 136}, {783, 155}, draft_.race.name);
}

// ---- Environment ---------------------------------------------------------------------------------

void EmpireEditor::pageEnvironment(SetupArea& a) {
    a.heading({231, 21}, "Atmosphere Breathed");
    int atm = indexOf(atmospheres_, draft_.race.atmosphere);
    const float atmBottom = std::max(115.0f, 16 + 18.0f * float(atmospheres_.size()) + 1);
    if (a.lampList("##atm", {406, 16}, {585, atmBottom}, atm, std::span<const std::string>(atmospheres_)))
        draft_.race.atmosphere = atmospheres_[static_cast<size_t>(atm)];
    a.heading({231, 145}, "Home Planet Type");
    int surf = indexOf(surfaces_, draft_.race.nativeSurface);
    const float surfTop = std::max(140.0f, atmBottom + 25);
    if (a.lampList("##surf", {406, surfTop}, {585, surfTop + std::max(59.0f, 18.0f * float(surfaces_.size()) + 1)}, surf,
                   std::span<const std::string>(surfaces_)))
        draft_.race.nativeSurface = surfaces_[static_cast<size_t>(surf)];

    // OpenSE4's own: the homeworld's picture and what the choice means.
    const auto pic = planetPicture(rules(), draft_.race.nativeSurface, draft_.race.atmosphere);
    a.picture(pic ? a.ctx().art.planetPortrait(*pic) : Sprite{}, {620, 16}, {748, 144}, true);
    a.textWrapped({604, 160},
                  "The homeworld has this atmosphere and planet type. Other planets with this atmosphere can be colonized "
                  "directly; the rest need domes, which the game settings may forbid.",
                  180);
}

// ---- Culture ------------------------------------------------------------------------------------

void EmpireEditor::pageCulture(SetupArea& a) {
    const auto& cultures = rules().data().cultures;
    a.heading({231, 21}, "Culture");
    // A lamp list with each culture's description indented under its name.
    a.box({232, 36}, {783, 487});
    a.place({233, 37});
    const Painter p = a.painter();
    beginList(p, "##cultures", a.size({550, 450}), 18.0f, ImGuiChildFlags_None, false);
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* body = a.ctx().fonts.regular;
    const float bodySize = p.fontPx(kTextSize);
    for (uint32_t i = 0; i < cultures.size(); ++i) {
        const ruleset::Culture& c = cultures[i];
        const float descW = rowW - p.px(36);
        const float descH = c.description.empty() ? 0.0f : body->CalcTextSizeA(bodySize, FLT_MAX, descW, c.description.c_str()).y;
        const float rowH = p.px(18) + descH + p.px(4);
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##culture", ImVec2(rowW, rowH))) draft_.race.culture = i;
        ImGui::PopID();
        script::reportItem(c.name);
        const bool on = draft_.race.culture == i;
        if (on)
            if (Sprite grid = a.ctx().art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(rowW / p.k()), 18, false))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(grid.tex.value)), r0, {r0.x + rowW, r0.y + p.px(18)},
                             {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
        if (Sprite s = a.ctx().art.region("Pictures/Game/General.bmp", 178 + 13 * (on ? 1 : 0), 0, 13, 13))
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), {r0.x + p.px(2), r0.y + p.px(2.5f)},
                         {r0.x + p.px(15), r0.y + p.px(15.5f)}, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
        dl->AddText(body, bodySize, {r0.x + p.px(20), r0.y + p.px(1)}, IM_COL32_WHITE, c.name.c_str());
        if (!c.description.empty())
            dl->AddText(body, bodySize, {r0.x + p.px(33), r0.y + p.px(18)}, imColor(kExplainRgb), c.description.c_str(), nullptr, descW);
    }
    endList(p);
    if (a.button({234, 500}, {535, 525}, "Compare Culture Modifiers")) ImGui::OpenPopup("Compare Culture Modifiers");
    compareCulturesPopup(a);
}

void EmpireEditor::compareCulturesPopup(SetupArea& a) {
    // Ours: the modifiers of every culture side by side (the original's
    // window is not described).
    const auto& cultures = rules().data().cultures;
    const Painter p = a.painter();
    ImGui::SetNextWindowSize(p.size({720, 420}), ImGuiCond_Always);
    ImGui::SetNextWindowPos(a.ctx().at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, p.size({10, 10}));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, p.size({6, 4}));
    if (ImGui::BeginPopupModal("Compare Culture Modifiers", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        static constexpr std::array<const char*, 10> kColumns{"Prod", "Research", "Intel", "Trade", "Space", "Ground", "Happy", "Maint", "Yards", "Repair"};
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (beginListTable(p, "##modifiers", 11, flags, ImVec2(0, -p.px(36)), kListLineStep)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Culture", ImGuiTableColumnFlags_WidthStretch);
            for (const char* c : kColumns) ImGui::TableSetupColumn(c, ImGuiTableColumnFlags_WidthFixed, p.px(52));
            ImGui::TableHeadersRow();
            for (uint32_t i = 0; i < cultures.size(); ++i) {
                const ruleset::Culture& c = cultures[i];
                const std::array<int, 10> v{c.production, c.research, c.intelligence, c.trade, c.spaceCombat,
                                            c.groundCombat, c.happiness, c.maintenance, c.shipyardRate, c.repair};
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(draft_.race.culture == i ? kHighlight : ImGui::GetStyle().Colors[ImGuiCol_Text], "%s", c.name.c_str());
                for (int x : v) {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(x > 0 ? kGood : x < 0 ? kBad : kDim, "%s", signedPercent(x).c_str());
                }
            }
            endListTable(p);
        }
        if (classicButton(p, "Close", {140, 26}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// ---- Characteristics -------------------------------------------------------------------------------

void EmpireEditor::pageCharacteristics(SetupArea& a) {
    a.heading({235, 21}, "Physical Characteristics");
    pointsLine(a, 21);
    a.heading({235, 321}, "Vocational Aptitudes");
    for (size_t i = 0; i < game::kCharacteristics; ++i) {
        const auto c = static_cast<game::Characteristic>(i);
        const float y = i < 9 ? 45 + 30.0f * float(i) : 345 + 30.0f * float(i - 9);
        const CharacteristicLimits l = characteristicLimits(rules(), c);
        int& v = draft_.race.characteristics[i];
        a.text({255, y + 4}, game::displayName(c));
        int64_t value = v;
        ImGui::PushID(static_cast<int>(i));
        // The buttons keep the value in its range (spec 02 §8.1); one click is 5 % (inferred).
        if (a.spin("##value", {406, y}, 71, value, 5, l.min, l.max, std::format("{}%", v))) v = static_cast<int>(value);
        ImGui::PopID();
        a.text({556, y + 4}, levelWord(v));
        const int effect = v - 100;
        a.textRight({790, y + 4}, std::format("({}% to {})", effect == 0 ? std::string("0") : std::format("{:+}", effect), kEffects[i]),
                    kExplainRgb, Face::Small);
    }
    // OpenSE4's own: back to the preset's values, or every one at 100 %.
    if (preset() && a.button({500, 316}, {640, 335}, "Preset Values")) {
        const game::Race base = game::raceFromPreset(rules(), *preset(), draft_.setup.presetTier);
        draft_.race.characteristics = base.characteristics;
    }
    if (a.button({646, 316}, {782, 335}, "All 100%")) draft_.race.characteristics.fill(100);
}

// ---- Advanced Traits -------------------------------------------------------------------------------

void EmpireEditor::pageTraits(SetupArea& a) {
    const auto& traits = rules().data().racialTraits;
    a.heading({231, 21}, "Racial Traits");
    pointsLine(a, 21);
    // A check list with a heading row, each trait's description indented under it.
    a.box({232, 36}, {783, 517});
    a.text({236, 41}, "Name", kHeadingRgb);
    a.text({686, 41}, "Cost", kHeadingRgb);
    a.place({233, 57});
    const Painter p = a.painter();
    beginList(p, "##traits", a.size({550, 460}), 18.0f, ImGuiChildFlags_None, false);
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* body = a.ctx().fonts.regular;
    const float bodySize = p.fontPx(kTextSize);
    const float lw = std::max(1.0f, std::floor(p.map.scale)) / p.fbScale;
    int hovered = -1;
    for (uint32_t i = 0; i < traits.size(); ++i) {
        const ruleset::RacialTrait& t = traits[i];
        const bool has = hasTrait(draft_.race, i);
        const TraitCheck check = canAddTrait(rules(), draft_.race, i);
        const bool enabled = has || check.ok;
        const float descW = rowW - p.px(36);
        const float descH = t.description.empty() ? 0.0f : body->CalcTextSizeA(bodySize, FLT_MAX, descW, t.description.c_str()).y;
        const float rowH = p.px(18) + descH + p.px(4);
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::InvisibleButton("##trait", ImVec2(rowW, rowH));
        ImGui::PopID();
        script::reportItem(t.name);
        if (ImGui::IsItemHovered()) hovered = static_cast<int>(i);
        if (clicked && enabled) {
            if (has) removeTrait(rules(), draft_.race, i);
            else addTrait(rules(), draft_.race, i);
        }
        const ImVec2 b0{r0.x + p.px(1), r0.y + p.px(0.5f)}, b1{b0.x + p.px(16), b0.y + p.px(17)};
        dl->AddRect(b0, b1, imColor(enabled ? kExplainRgb : kDimBoxRgb), 0.0f, lw);
        if (hasTrait(draft_.race, i))
            if (Sprite s = a.ctx().art.region("Pictures/Game/General.bmp", 191, 0, 13, 13))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), {b0.x + p.px(1.5f), b0.y + p.px(2)}, {b0.x + p.px(14.5f), b0.y + p.px(15)},
                             {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
        const ImU32 color = imColor(enabled ? kWhite : kDimTextRgb);
        dl->AddText(body, bodySize, {r0.x + p.px(20), r0.y + p.px(1)}, color, t.name.c_str());
        const std::string cost = std::to_string(t.cost);
        dl->AddText(body, bodySize, {r0.x + p.px(453), r0.y + p.px(1)}, color, cost.c_str());
        if (!t.description.empty())
            dl->AddText(body, bodySize, {r0.x + p.px(33), r0.y + p.px(18)}, imColor(kExplainRgb), t.description.c_str(), nullptr, descW);
    }
    endList(p);
    // OpenSE4's own: why a trait cannot be taken, under the pointer.
    if (hovered >= 0) hoveredTrait_ = hovered;
    if (hovered >= 0 && !hasTrait(draft_.race, static_cast<uint32_t>(hovered))) {
        const TraitCheck check = canAddTrait(rules(), draft_.race, static_cast<uint32_t>(hovered));
        if (!check.ok) ImGui::SetTooltip("%s", check.reason.c_str());
    }
}

// ---- Description ---------------------------------------------------------------------------------

void EmpireEditor::pageDescription(SetupArea& a) {
    a.heading({231, 21}, "Biological Description");
    a.editMultiline("##bio", {406, 16}, {775, 75}, draft_.race.biology);
    a.heading({231, 89}, "Society Description");
    a.editMultiline("##soc", {406, 84}, {775, 143}, draft_.race.society);
    a.heading({231, 157}, "General History");
    a.editMultiline("##hist", {406, 152}, {775, 211}, draft_.race.history);

    a.heading({231, 225}, "Demeanor");
    const auto& demeanors = rules().data().names.demeanors;
    int demeanor = indexOf(demeanors, draft_.race.demeanor);
    if (a.lampList("##demeanor", {406, 220}, {585, 379}, demeanor, std::span<const std::string>(demeanors), true, true))
        draft_.race.demeanor = demeanors[static_cast<size_t>(demeanor)];

    // Happiness Type: a lamp list with each type's description under its name.
    a.heading({231, 393}, "Happiness Type");
    const auto& moods = rules().data().happinessModels;
    a.box({406, 388}, {775, 527});
    a.place({407, 389});
    const Painter p = a.painter();
    beginList(p, "##moods", a.size({368, 138}), 18.0f, ImGuiChildFlags_None, false);
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* body = a.ctx().fonts.regular;
    const float bodySize = p.fontPx(kTextSize);
    for (uint32_t i = 0; i < moods.size(); ++i) {
        const auto& m = moods[i];
        const float descW = rowW - p.px(36);
        const float descH = m.description.empty() ? 0.0f : body->CalcTextSizeA(bodySize, FLT_MAX, descW, m.description.c_str()).y;
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##mood", ImVec2(rowW, p.px(18) + descH + p.px(4)))) draft_.race.happinessModel = i;
        ImGui::PopID();
        script::reportItem(m.name);
        const bool on = draft_.race.happinessModel == i;
        if (Sprite s = a.ctx().art.region("Pictures/Game/General.bmp", 178 + 13 * (on ? 1 : 0), 0, 13, 13))
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), {r0.x + p.px(2), r0.y + p.px(2.5f)},
                         {r0.x + p.px(15), r0.y + p.px(15.5f)}, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
        dl->AddText(body, bodySize, {r0.x + p.px(20), r0.y + p.px(1)}, IM_COL32_WHITE, m.name.c_str());
        if (!m.description.empty())
            dl->AddText(body, bodySize, {r0.x + p.px(33), r0.y + p.px(18)}, imColor(kExplainRgb), m.description.c_str(), nullptr, descW);
    }
    endList(p);
}

} // namespace opense4::client::classic::setup
