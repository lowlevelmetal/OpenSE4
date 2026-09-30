#include "client/classic/screens/setup_empire.hpp"

#include "client/classic/screens/setup_widgets.hpp"
#include "datafile/datafile.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
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

ImVec4 signColor(int v) { return v > 0 ? kGood : v < 0 ? kBad : kDim; }

void pointsText(int left, int budget) {
    ImGui::TextColored(left < 0 ? kBad : kGood, "%d", left);
    ImGui::SameLine();
    ImGui::TextColored(kDim, "of %d racial points left", budget);
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
        if (datafile::keysEqual(all[presets_[k]].folder, draft_.setup.preset)) presetPos_ = k;
    surfaces_ = planetSurfaces(*rules_);
    atmospheres_ = atmospheres(*rules_);
    designFiles_ = designNameFiles(*rules_);
}

const ruleset::RacePreset* EmpireEditor::preset() const { return presetOf(rules(), draft_.setup); }

void EmpireEditor::choosePreset(size_t position) {
    if (presets_.empty()) return;
    presetPos_ = position % presets_.size();
    const ruleset::RacePreset& p = rules().racePresets()[presets_[presetPos_]];
    EmpireDraft next = draftFromPreset(rules(), p, bestTierWithin(rules(), p, racialPoints_));
    next.setup.kind = draft_.setup.kind;
    next.setup.passwordHash = draft_.setup.passwordHash;
    next.setup.color = draft_.setup.color;
    draft_ = std::move(next);
}

void EmpireEditor::resetToTier(int tier) {
    if (const ruleset::RacePreset* p = preset()) {
        draft_.setup.presetTier = tier;
        draft_.race = game::raceFromPreset(rules(), *p, tier);
    }
}

void EmpireEditor::pointsLine(MenuContext& ctx) {
    heading(ctx, "Racial Points");
    ImGui::SameLine(ctx.px(150));
    pointsText(racialPointsLeft(rules(), draft_.race, racialPoints_), racialPoints_);
}

EmpireEditor::Result EmpireEditor::draw(MenuContext& ctx) {
    Result result = Result::Editing;
    const bool escape = escapePressed();
    const std::string title = std::format("Empire Setup - {}###empiresetup", pageTitle(page_));
    SetupFrame frame(ctx, title.c_str());
    if (!frame.open()) return result;

    frame.beginContent();
    ImGui::BeginChild("##page", ImVec2(0, -ctx.px(30)));
    switch (page_) {
        case EmpirePage::General: pageGeneral(ctx); break;
        case EmpirePage::Environment: pageEnvironment(ctx); break;
        case EmpirePage::Culture: pageCulture(ctx); break;
        case EmpirePage::Characteristics: pageCharacteristics(ctx); break;
        case EmpirePage::Traits: pageTraits(ctx); break;
        case EmpirePage::Description: pageDescription(ctx); break;
        case EmpirePage::Count: break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (!error_.empty()) ImGui::TextColored(kBad, "%s", error_.c_str());
    else note("Pick a race style to load its preset, then adjust anything. The race costs racial points; the game sets the budget.");

    frame.beginButtons();
    for (size_t i = 0; i < kPageTitles.size(); ++i)
        if (frame.pageButton(kPageTitles[i], page_ == static_cast<EmpirePage>(i))) page_ = static_cast<EmpirePage>(i);
    ImGui::Dummy(ImVec2(0, ctx.px(12)));
    const int left = racialPointsLeft(rules(), draft_.race, racialPoints_);
    ImGui::TextColored(kLabelBlue, "Points left");
    ImGui::TextColored(left < 0 ? kBad : kGood, "%d", left);
    ImGui::SameLine();
    ImGui::TextColored(kDim, "/ %d", racialPoints_);
    if (left < 0) note("Over budget: Create Empire is refused.");

    frame.toBottom(2);
    if (frame.button(isNew_ ? "Create Empire" : "Save Empire")) {
        EmpireDraft d = draft_;
        if (!password_.empty()) d.setup.passwordHash = hashPassword(password_);
        auto done = finishDraft(rules(), d, racialPoints_);
        if (done) {
            result_ = std::move(*done);
            result = Result::Created;
        } else {
            error_ = done.error();
        }
    }
    if (frame.button("Cancel") || escape) result = Result::Cancelled;
    return result;
}

// ---- General ------------------------------------------------------------------------------------

void EmpireEditor::pageGeneral(MenuContext& ctx) {
    const auto& names = rules().data().names;
    const ruleset::RacePreset* p = preset();

    // Race style chooser: portrait between arrows, name, flag, preset builds.
    ImGui::BeginGroup();
    heading(ctx, "Race Style");
    const float portrait = ctx.px(168), arrowW = ctx.px(24), arrowH = ctx.px(57), gap = ctx.px(6);
    const ImVec2 start = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(start.x, start.y + (portrait - arrowH) * 0.5f));
    if (arrowButton(ctx, "##prev", true, {24, 57})) choosePreset(presetPos_ + presets_.size() - 1);
    ImGui::SetCursorPos(ImVec2(start.x + arrowW + gap, start.y));
    sprite(ctx.art.racePortrait(draft_.race.style), ImVec2(portrait, portrait));
    ImGui::SetCursorPos(ImVec2(start.x + arrowW + portrait + 2 * gap, start.y + (portrait - arrowH) * 0.5f));
    if (arrowButton(ctx, "##next", false, {24, 57})) choosePreset(presetPos_ + 1);
    ImGui::SetCursorPos(ImVec2(start.x, start.y + portrait + ImGui::GetStyle().ItemSpacing.y));
    ImGui::PushFont(ctx.fonts.bold, kTitleSize * ctx.k());
    ImGui::TextColored(kHighlight, "%s", draft_.race.name.c_str());
    ImGui::PopFont();
    sprite(ctx.art.flag(draft_.race.style), ctx.size({39, 27}));
    ImGui::SameLine();
    if (!presets_.empty()) ImGui::TextColored(kDim, "Style %zu of %zu", presetPos_ + 1, presets_.size());
    ImGui::Dummy(ImVec2(0, ctx.px(6)));
    if (p && !p->tiers.empty()) {
        heading(ctx, "Preset Build");
        const std::vector<int> costs = tierCosts(rules(), *p);
        for (size_t t = 0; t < costs.size(); ++t) {
            const bool matches = sameRace(draft_.race, game::raceFromPreset(rules(), *p, static_cast<int>(t)));
            bool on = matches;
            const std::string label = std::format("Build {}  ({} points)", t + 1, costs[t]);
            ImGui::PushID(static_cast<int>(t));
            if (lamp(ctx, label.c_str(), on, true) && !matches) resetToTier(static_cast<int>(t));
            ImGui::PopID();
        }
        bool any = false;
        for (size_t t = 0; t < costs.size(); ++t) any = any || sameRace(draft_.race, game::raceFromPreset(rules(), *p, static_cast<int>(t)));
        if (!any) ImGui::TextColored(kDim, "Custom race (edited)");
    }
    ImGui::EndGroup();

    // Identity and player.
    ImGui::SameLine(ctx.px(300));
    ImGui::BeginGroup();
    const float field = ctx.px(300);
    const float label = ctx.px(140);
    auto row = [&](const char* text) {  // SameLine offsets inside a group count from the group's left edge
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kLabelBlue, "%s", text);
        ImGui::SameLine(label);
    };
    heading(ctx, "Empire");
    row("Empire Name");
    inputWithSuggestions(ctx, "##ename", draft_.setup.name, names.empireNames, field);
    row("Empire Type");
    inputWithSuggestions(ctx, "##etype", draft_.setup.empireType, names.empireTypes, field);
    row("Leader Title");
    inputWithSuggestions(ctx, "##ltitle", draft_.setup.leaderTitle, names.emperorTitles, field);
    row("Leader Name");
    inputWithSuggestions(ctx, "##lname", draft_.setup.leaderName, names.emperorNames, field);
    row("Race Name");
    inputText("##rname", draft_.race.name, field);
    row("Design Names");
    ImGui::SetNextItemWidth(field);
    if (ImGui::BeginCombo("##dnames", draft_.race.designNameFile.empty() ? "(none)" : draft_.race.designNameFile.c_str())) {
        for (const std::string& f : designFiles_)
            if (ImGui::Selectable(f.c_str(), datafile::keysEqual(f, draft_.race.designNameFile))) draft_.race.designNameFile = f;
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0, ctx.px(8)));
    ImGui::TextColored(kHighlight, "%s %s", draft_.setup.name.c_str(), draft_.setup.empireType.c_str());
    ImGui::TextColored(kDim, "led by %s %s", draft_.setup.leaderTitle.c_str(), draft_.setup.leaderName.c_str());

    ImGui::Dummy(ImVec2(0, ctx.px(10)));
    heading(ctx, "Player");
    int kind = draft_.setup.kind == game::PlayerKind::Human ? 0 : 1;
    if (lampChoice(ctx, "##kind", kind, {"Human player", "Computer controlled"}))
        draft_.setup.kind = kind == 0 ? game::PlayerKind::Human : game::PlayerKind::Computer;
    ImGui::Dummy(ImVec2(0, ctx.px(4)));
    row("Password");
    inputText("##pw", password_, ctx.px(200), ImGuiInputTextFlags_Password);
    if (!draft_.setup.passwordHash.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kGood, "set");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            draft_.setup.passwordHash.clear();
            password_.clear();
        }
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + label + field);
    note("Optional. With several human players on one computer, each is asked for their password before their turn.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

// ---- Environment ---------------------------------------------------------------------------------

void EmpireEditor::pageEnvironment(MenuContext& ctx) {
    auto indexOf = [](const std::vector<std::string>& list, std::string_view v) {
        for (size_t i = 0; i < list.size(); ++i)
            if (datafile::keysEqual(list[i], v)) return static_cast<int>(i);
        return -1;
    };
    ImGui::BeginGroup();
    heading(ctx, "Atmosphere Breathed");
    note("The gas the race breathes; its homeworld has it too.");
    int atm = indexOf(atmospheres_, draft_.race.atmosphere);
    if (lampChoice(ctx, "##atm", atm, std::span<const std::string>(atmospheres_), true)) draft_.race.atmosphere = atmospheres_[size_t(atm)];
    ImGui::Dummy(ImVec2(0, ctx.px(12)));
    heading(ctx, "Home Planet Type");
    note("The kind of planet the race lives on and colonizes first.");
    int surf = indexOf(surfaces_, draft_.race.nativeSurface);
    if (lampChoice(ctx, "##surf", surf, std::span<const std::string>(surfaces_), true)) draft_.race.nativeSurface = surfaces_[size_t(surf)];
    ImGui::EndGroup();

    ImGui::SameLine(ctx.px(330));
    ImGui::BeginGroup();
    heading(ctx, "Homeworld");
    const auto pic = planetPicture(rules(), draft_.race.nativeSurface, draft_.race.atmosphere);
    sprite(pic ? ctx.art.planetPortrait(*pic) : Sprite{}, ctx.size({192, 192}));
    ImGui::TextColored(kHighlight, "%s, %s atmosphere", draft_.race.nativeSurface.c_str(), draft_.race.atmosphere.c_str());
    ImGui::Dummy(ImVec2(0, ctx.px(8)));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ctx.px(380));
    note("The start planet is converted to this type and atmosphere and keeps its size. Other planets with this atmosphere can be "
         "colonized directly; the rest need domes, which the game settings may forbid.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

// ---- Culture ------------------------------------------------------------------------------------

void EmpireEditor::pageCulture(MenuContext& ctx) {
    const auto& cultures = rules().data().cultures;
    heading(ctx, "Culture");
    note("A culture shifts the whole empire's output and abilities by these percentages. Click a row to choose it.");
    static constexpr std::array<const char*, 10> kColumns{"Prod", "Research", "Intel", "Trade", "Space", "Ground", "Happy", "Maint", "Yards", "Repair"};
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##cultures", 11, flags, ImVec2(0, ctx.px(400)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Culture", ImGuiTableColumnFlags_WidthStretch);
        for (const char* c : kColumns) ImGui::TableSetupColumn(c, ImGuiTableColumnFlags_WidthFixed, ctx.px(56));
        ImGui::TableHeadersRow();
        for (uint32_t i = 0; i < cultures.size(); ++i) {
            const ruleset::Culture& c = cultures[i];
            const std::array<int, 10> v{c.production, c.research, c.intelligence, c.trade, c.spaceCombat,
                                        c.groundCombat, c.happiness, c.maintenance, c.shipyardRate, c.repair};
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            const bool selected = draft_.race.culture == i;
            if (selected) ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
            if (ImGui::Selectable(c.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) draft_.race.culture = i;
            if (selected) ImGui::PopStyleColor();
            ImGui::PopID();
            for (int x : v) {
                ImGui::TableNextColumn();
                ImGui::TextColored(signColor(x), "%s", signedPercent(x).c_str());
            }
        }
        ImGui::EndTable();
    }
    if (draft_.race.culture < cultures.size()) {
        ImGui::Dummy(ImVec2(0, ctx.px(6)));
        const ruleset::Culture& c = cultures[draft_.race.culture];
        ImGui::TextColored(kHighlight, "%s", c.name.c_str());
        if (!c.description.empty()) ImGui::TextWrapped("%s", c.description.c_str());
    }
}

// ---- Characteristics -------------------------------------------------------------------------------

void EmpireEditor::pageCharacteristics(MenuContext& ctx) {
    pointsLine(ctx);
    note("100% is average. Each point above 100% costs the listed points per %; lowering one gives some back.");
    ImGui::Dummy(ImVec2(0, ctx.px(4)));
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH;
    if (ImGui::BeginTable("##chars", 6, flags)) {
        ImGui::TableSetupColumn("Characteristic", ImGuiTableColumnFlags_WidthFixed, ctx.px(190));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, ctx.px(270));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ctx.px(64));
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, ctx.px(62));
        ImGui::TableSetupColumn("Per %", ImGuiTableColumnFlags_WidthFixed, ctx.px(52));
        ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < game::kCharacteristics; ++i) {
            const auto c = static_cast<game::Characteristic>(i);
            const CharacteristicLimits l = characteristicLimits(rules(), c);
            int& v = draft_.race.characteristics[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(std::string(game::displayName(c)).c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##v", &v, l.min, l.max, "%d%%", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TableNextColumn();
            if (ImGui::Button("-", ImVec2(ctx.px(28), 0))) v = std::max(l.min, v - 5);
            ImGui::SameLine(0, ctx.px(4));
            if (ImGui::Button("+", ImVec2(ctx.px(28), 0))) v = std::min(l.max, v + 5);
            ImGui::TableNextColumn();
            const int cost = characteristicCost(rules(), c, v);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(cost > 0 ? kBad : cost < 0 ? kGood : kDim, "%s", cost == 0 ? "0" : std::format("{:+}", cost).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%lld", static_cast<long long>(rules().setting(std::format("Characteristic {} Pct Cost", game::displayName(c)), 0)));
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%d-%d%%", l.min, l.max);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, ctx.px(6)));
    if (preset() && ImGui::Button("Preset Values", ctx.size({140, 26}))) {
        const game::Race base = game::raceFromPreset(rules(), *preset(), draft_.setup.presetTier);
        draft_.race.characteristics = base.characteristics;
    }
    ImGui::SameLine();
    if (ImGui::Button("All 100%", ctx.size({140, 26}))) draft_.race.characteristics.fill(100);
}

// ---- Advanced Traits -------------------------------------------------------------------------------

void EmpireEditor::pageTraits(MenuContext& ctx) {
    const auto& traits = rules().data().racialTraits;
    pointsLine(ctx);
    note("Click a trait to add or drop it. Some traits need another trait first, and some cannot be combined.");
    ImGui::Dummy(ImVec2(0, ctx.px(4)));
    int hovered = -1;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##traits", 4, flags, ImVec2(0, ctx.px(420)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Trait", ImGuiTableColumnFlags_WidthFixed, ctx.px(240));
        ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, ctx.px(60));
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, ctx.px(140));
        ImGui::TableSetupColumn("Rules", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (uint32_t i = 0; i < traits.size(); ++i) {
            const ruleset::RacialTrait& t = traits[i];
            const bool has = hasTrait(draft_.race, i);
            const TraitCheck check = canAddTrait(rules(), draft_.race, i);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = has;
            if (lamp(ctx, t.name.c_str(), on, has || check.ok)) {
                if (has) removeTrait(rules(), draft_.race, i);
                else addTrait(rules(), draft_.race, i);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) hovered = static_cast<int>(i);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(has ? kHighlight : ImGui::GetStyle().Colors[ImGuiCol_Text], "%d", t.cost);
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s", t.traitType.c_str());
            ImGui::TableNextColumn();
            std::string rulesText;
            for (const auto& need : t.requiredTraits)
                if (!need.empty() && !datafile::keysEqual(need, "None")) rulesText += (rulesText.empty() ? "Needs " : ", ") + need;
            std::string excl;
            for (const auto& no : t.restrictedTraits)
                if (!no.empty() && !datafile::keysEqual(no, "None")) excl += (excl.empty() ? "Not with " : ", ") + no;
            if (!excl.empty()) rulesText += (rulesText.empty() ? "" : "; ") + excl;
            ImGui::TextColored(!has && !check.ok ? kBad : kDim, "%s", rulesText.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (hovered >= 0) hoveredTrait_ = hovered;
    if (hoveredTrait_ >= 0 && static_cast<size_t>(hoveredTrait_) < traits.size()) {
        const ruleset::RacialTrait& t = traits[static_cast<size_t>(hoveredTrait_)];
        ImGui::Dummy(ImVec2(0, ctx.px(4)));
        ImGui::TextColored(kHighlight, "%s", t.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kDim, "(%d points)", t.cost);
        if (!t.description.empty()) ImGui::TextWrapped("%s", t.description.c_str());
        const TraitCheck check = canAddTrait(rules(), draft_.race, static_cast<uint32_t>(hoveredTrait_));
        if (!hasTrait(draft_.race, static_cast<uint32_t>(hoveredTrait_)) && !check.ok) ImGui::TextColored(kBad, "%s", check.reason.c_str());
    } else {
        ImGui::Dummy(ImVec2(0, ctx.px(4)));
        note("Point at a trait to read what it does.");
    }
}

// ---- Description ---------------------------------------------------------------------------------

void EmpireEditor::pageDescription(MenuContext& ctx) {
    const auto& moods = rules().data().happinessModels;
    const float label = ctx.px(140);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kLabelBlue, "Demeanor");
    ImGui::SameLine(label);
    inputWithSuggestions(ctx, "##demeanor", draft_.race.demeanor, rules().data().names.demeanors, ctx.px(260));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kLabelBlue, "Happiness Type");
    ImGui::SameLine(label);
    ImGui::SetNextItemWidth(ctx.px(260));
    const char* current = draft_.race.happinessModel < moods.size() ? moods[draft_.race.happinessModel].name.c_str() : "";
    if (ImGui::BeginCombo("##mood", current)) {
        for (uint32_t i = 0; i < moods.size(); ++i)
            if (ImGui::Selectable(moods[i].name.c_str(), draft_.race.happinessModel == i)) draft_.race.happinessModel = i;
        ImGui::EndCombo();
    }
    if (draft_.race.happinessModel < moods.size() && !moods[draft_.race.happinessModel].description.empty()) {
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kDim, "%s", moods[draft_.race.happinessModel].description.c_str());
        ImGui::PopTextWrapPos();
    }
    note("Demeanor is flavour text. The happiness type decides what pleases or angers the population.");
    ImGui::Dummy(ImVec2(0, ctx.px(6)));
    const ImVec2 box(-FLT_MIN, ctx.px(118));
    heading(ctx, "Biology");
    inputMultiline("##bio", draft_.race.biology, box);
    heading(ctx, "Society");
    inputMultiline("##soc", draft_.race.society, box);
    heading(ctx, "History");
    inputMultiline("##hist", draft_.race.history, box);
}

} // namespace opense4::client::classic::setup
