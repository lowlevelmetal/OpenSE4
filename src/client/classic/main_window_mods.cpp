// The main window's part of the interface tier (docs/sdk/interface.md): the
// mods' orders from the order strip's free place, their arguments asked for
// one after another, the mods' report panels behind the report's MOD button,
// and the mods' keys.

#include "client/classic/main_window.hpp"

#include "client/audio.hpp"
#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/sector_view.hpp"
#include "client/script/items.hpp"

#include "game/query.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <format>

namespace opense4::client::classic {

namespace {

// A mod order's target as the report's thing names it.
std::optional<sdk::ModOrderTarget> targetFor(const game::GameState& s, game::EmpireId me, std::string_view appliesTo, const sdk::UiThing& thing) {
    if (appliesTo == "self") return sdk::ModOrderTarget{"self", -1};
    if (appliesTo == "vehicle" && thing.kind == "vehicle") return sdk::ModOrderTarget{"vehicle", thing.id};
    if (appliesTo == "fleet" && thing.kind == "fleet") return sdk::ModOrderTarget{"fleet", thing.id};
    if (appliesTo == "colony" && (thing.kind == "colony" || thing.kind == "object")) return sdk::ModOrderTarget{"colony", thing.id};
    if (appliesTo == "empire") {
        game::EmpireId owner;
        if (thing.kind == "empire") owner = game::EmpireId{static_cast<uint32_t>(thing.id)};
        else if (thing.kind == "vehicle") {
            if (const game::Vehicle* v = s.vehicle(game::VehicleId{static_cast<uint32_t>(thing.id)})) owner = v->owner;
        } else if (thing.kind == "colony" || thing.kind == "object") {
            if (const game::Colony* c = s.colony(game::ObjectId{static_cast<uint32_t>(thing.id)})) owner = c->owner;
        }
        if (owner.valid() && owner != me) return sdk::ModOrderTarget{"empire", static_cast<int64_t>(owner.value)};
    }
    return std::nullopt;
}

std::string choiceText(const opense4::script::Value& v) { return sdk::formatUiValue(v); }

} // namespace

// ---- The orders offered ----------------------------------------------------------------------------------

std::vector<MainWindow::ModOrderOffer> MainWindow::modOrderOffers(UiContext& ui) const {
    std::vector<ModOrderOffer> out;
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::EmpireId me = ui.session.player();
    if (s.mods.empty()) return out;
    auto add = [&](const sdk::ModOrderTarget& t) {
        for (sdk::ModOrderChoice& c : sdk::modOrders(r, s, me, t)) {
            std::string label = modOrderLabel(r, c.mod, c.order);
            out.push_back(ModOrderOffer{std::move(c), t, std::move(label)});
        }
    };
    // What is selected (a tagged group takes the empire's own orders only).
    if (tagged_.empty()) {
        if (const game::Vehicle* v = selectedVehicle(ui)) {
            if (v->owner == me) {
                if (fleet_ && s.fleet(*fleet_)) add({"fleet", static_cast<int64_t>(fleet_->value)});
                else add({"vehicle", static_cast<int64_t>(v->id.value)});
            } else if (v->owner.valid()) {
                add({"empire", static_cast<int64_t>(v->owner.value)});
            }
        } else if (object_) {
            if (const game::Colony* c = seenColony(r, s, me, *object_)) {
                if (c->owner == me) add({"colony", static_cast<int64_t>(object_->value)});
                else add({"empire", static_cast<int64_t>(c->owner.value)});
            }
        }
    }
    add({"self", -1});
    return out;
}

void MainWindow::openModOrders(UiContext& ui) {
    std::vector<ModOrderOffer> offers = modOrderOffers(ui);
    if (offers.empty()) return;
    const game::Rules& r = ui.rules();
    Chooser c;
    c.title = "Mod Orders";
    c.note = "Orders of the game's mods";
    std::string group;
    for (ModOrderOffer& o : offers) {
        // A heading per kind of target: the selection's, then the empire's own.
        const std::string h = o.target.kind == "self" ? std::string("Your empire") : o.target.kind == "empire" ? std::string("Their empire")
                                                                                  : std::string("The selection");
        if (h != group) {
            c.items.push_back({h, nullptr});
            group = h;
        }
        Choice item;
        item.label = o.label;
        item.tooltip = modOrderDescription(r, o.choice.mod, o.choice.order);
        item.icon = modOrderIcon(ui, o.choice.mod, o.choice.order.name);
        item.action = [this, &ui, offer = std::move(o)] { startModOrder(ui, offer); };
        c.items.push_back(std::move(item));
    }
    chooser_ = std::move(c);
}

// ---- One order, its arguments asked one after another ----------------------------------------------------

void MainWindow::startModOrder(UiContext& ui, ModOrderOffer offer) {
    ModOrderDraft d;
    d.steps = sdk::uiArgumentSteps(offer.choice.order, modUi(ui.rules()).ext.order(offer.choice.mod, offer.choice.order.name));
    // The questions in the mod's language.
    for (sdk::UiArgStep& step : d.steps)
        step.question = modText(ui.rules(), offer.choice.mod, std::format("order.{}.arg.{}", offer.choice.order.name, step.name), step.question);
    d.offer = std::move(offer);
    modOrder_ = std::move(d);
    nextModStep(ui);
}

void MainWindow::answerModStep(UiContext& ui, opense4::script::Value answer) {
    if (!modOrder_ || modOrder_->at >= modOrder_->steps.size()) return;
    modOrder_->answers.set(modOrder_->steps[modOrder_->at].name, std::move(answer));
    ++modOrder_->at;
    nextModStep(ui);
}

void MainWindow::nextModStep(UiContext& ui) {
    if (!modOrder_) return;
    ModOrderDraft& d = *modOrder_;
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::EmpireId me = ui.session.player();
    const std::string label = d.offer.label;
    if (d.at >= d.steps.size()) {
        // Every argument answered: the order goes as a command, checked as any other.
        const ModOrderDraft done = std::move(*modOrder_);
        modOrder_.reset();
        auto args = sdk::uiOrderArguments(done.offer.choice.order, done.answers);
        if (!args) {
            note(ui, std::format("{}: {}", label, args.error()));
            return;
        }
        const game::CommandResult res = ui.session.issue(sdk::modOrderCommand(done.offer.choice, done.offer.target, *args));
        note(ui, res.ok ? std::format("{}: given", label) : res.error);
        return;
    }
    const sdk::UiArgStep& step = d.steps[d.at];
    const std::string title = std::format("{}: {}", label, step.question);
    auto chooser = [&](std::vector<std::pair<std::string, opense4::script::Value>> items) {
        Chooser c;
        c.title = label;
        c.note = step.question;
        for (auto& [text, value] : items) {
            Choice item;
            item.label = text;
            item.chosen = !step.defaultValue.isNull() && value == step.defaultValue;
            item.action = [this, &ui, v = std::move(value)] { answerModStep(ui, v); };
            c.items.push_back(std::move(item));
        }
        if (c.items.empty()) {
            note(ui, std::format("{}: nothing to choose for {}", label, step.question));
            modOrder_.reset();
            return;
        }
        chooser_ = std::move(c);
    };
    switch (step.ask) {
        case sdk::UiArgStep::Ask::Number: {
            ModPrompt p;
            p.title = label;
            p.question = step.question;
            if (step.min || step.max)
                p.question += std::format(" ({} to {})", step.min ? std::to_string(*step.min) : "any", step.max ? std::to_string(*step.max) : "any");
            p.number = true;
            p.value = step.defaultValue.isInt() ? step.defaultValue.asInt() : step.min.value_or(0);
            modPrompt_ = std::move(p);
            return;
        }
        case sdk::UiArgStep::Ask::Text: {
            ModPrompt p;
            p.title = label;
            p.question = step.question;
            p.number = false;
            p.text = step.defaultValue.isString() ? step.defaultValue.asString() : std::string();
            modPrompt_ = std::move(p);
            return;
        }
        case sdk::UiArgStep::Ask::YesNo: return chooser({{"Yes", opense4::script::Value(true)}, {"No", opense4::script::Value(false)}});
        case sdk::UiArgStep::Ask::Choice: {
            std::vector<std::pair<std::string, opense4::script::Value>> items;
            for (const opense4::script::Value& v : step.choices) items.emplace_back(choiceText(v), v);
            return chooser(std::move(items));
        }
        case sdk::UiArgStep::Ask::Empire: {
            std::vector<std::pair<std::string, opense4::script::Value>> items;
            if (step.optional) items.emplace_back("(none)", opense4::script::Value());
            items.emplace_back(s.empire(me).name, opense4::script::Value(static_cast<int64_t>(me.value)));
            for (game::EmpireId e : knownEmpires(ui)) items.emplace_back(s.empire(e).name, opense4::script::Value(static_cast<int64_t>(e.value)));
            return chooser(std::move(items));
        }
        case sdk::UiArgStep::Ask::Design: {
            std::vector<std::pair<std::string, opense4::script::Value>> items;
            if (step.optional) items.emplace_back("(none)", opense4::script::Value());
            for (game::DesignId id : s.empire(me).designs)
                if (!s.design(id).obsolete) items.emplace_back(s.design(id).name, opense4::script::Value(static_cast<int64_t>(id.value)));
            return chooser(std::move(items));
        }
        case sdk::UiArgStep::Ask::Pick:
            // A system, stellar object, colony, vehicle or fleet: picked on the map.
            pick_ = Pick::Callback;
            pickPrompt_ = std::format("{}: pick {} on the map", label, step.question);
            pickCallback_ = [this, &ui](game::Location where) { pickModArgument(ui, where); };
            (void)r;
            return;
    }
}

void MainWindow::pickModArgument(UiContext& ui, game::Location where) {
    if (!modOrder_ || modOrder_->at >= modOrder_->steps.size()) return;
    const sdk::UiArgStep& step = modOrder_->steps[modOrder_->at];
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::EmpireId me = ui.session.player();
    std::vector<std::pair<std::string, opense4::script::Value>> found;
    if (step.type == "system") {
        if (where.system.valid()) found.emplace_back(s.galaxy.system(where.system).name, opense4::script::Value(static_cast<int64_t>(where.system.value)));
    } else if (step.type == "object" || step.type == "colony") {
        for (game::ObjectId id : shownStellarObjects(r, s, me, where.system, where.sector)) {
            if (step.type == "colony" && !seenColony(r, s, me, id)) continue;
            found.emplace_back(objectName(s, id, me), opense4::script::Value(static_cast<int64_t>(id.value)));
        }
    } else if (step.type == "vehicle" || step.type == "fleet") {
        for (const game::Vehicle* v : vehiclesAt(ui, where)) {
            if (step.type == "vehicle") {
                found.emplace_back(v->name, opense4::script::Value(static_cast<int64_t>(v->id.value)));
            } else if (const game::Fleet* f = s.fleet(v->fleet);
                       f && std::none_of(found.begin(), found.end(), [&](const auto& x) { return x.second == opense4::script::Value(static_cast<int64_t>(f->id.value)); })) {
                found.emplace_back(f->name, opense4::script::Value(static_cast<int64_t>(f->id.value)));
            }
        }
    }
    if (found.empty()) {
        note(ui, std::format("{}: no {} there", modOrder_->offer.label, step.type));
        modOrder_.reset();
        return;
    }
    if (found.size() == 1) {
        answerModStep(ui, found.front().second);
        return;
    }
    // Several there: which one.
    Chooser c;
    c.title = modOrder_->offer.label;
    c.note = step.question;
    for (auto& [text, value] : found) c.items.push_back({text, [this, &ui, v = value] { answerModStep(ui, v); }});
    chooser_ = std::move(c);
}

void MainWindow::drawModPrompt(UiContext& ui) {
    ModPrompt& p = *modPrompt_;
    const std::string title = p.title + "##modprompt";
    Dialog d(ui.painter(), title.c_str(), DialogSize::Prompt, 0.0f);
    bool ok = false, cancel = false;
    if (d.open()) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ui.promptWindow();
        d.beginContent();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kLabelBlue, "%s", p.question.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (p.appearing) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(ui.px(220));
        if (p.number) {
            int64_t v = p.value;
            if (ImGui::InputScalar("##modnumber", ImGuiDataType_S64, &v, nullptr, nullptr, "%lld", ImGuiInputTextFlags_EnterReturnsTrue)) ok = true;
            p.value = v;
        } else {
            char buffer[1024] = {};
            std::snprintf(buffer, sizeof buffer, "%s", p.text.c_str());
            if (ImGui::InputText("##modtext", buffer, sizeof buffer, ImGuiInputTextFlags_EnterReturnsTrue)) ok = true;
            p.text = buffer;
        }
        p.appearing = false;
        ImGui::Spacing();
        if (classicButton(ui, "OK", {100, 26})) ok = true;
        ImGui::SameLine();
        if (classicButton(ui, "Cancel", {100, 26}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cancel = true;
    }
    if (cancel) {
        audio().play("close");
        modPrompt_.reset();
        modOrder_.reset();
        return;
    }
    if (ok) {
        const opense4::script::Value answer = p.number ? opense4::script::Value(p.value) : opense4::script::Value(p.text);
        modPrompt_.reset();
        answerModStep(ui, answer);
    }
}

// ---- Keys ------------------------------------------------------------------------------------------------

void MainWindow::modHotkeys(UiContext& ui) {
    if (ImGui::GetIO().WantTextInput) return;
    for (const ModAction& a : modActions()) {
        if (!modActionPressed(a.id)) continue;
        const size_t first = a.id.find(':');
        const size_t second = a.id.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        const std::string mod = a.id.substr(0, first), kind = a.id.substr(first + 1, second - first - 1), name = a.id.substr(second + 1);
        if (kind == "order") {
            // The order for the selection (or the empire), its arguments asked as from the menu.
            std::vector<ModOrderOffer> offers = modOrderOffers(ui);
            auto it = std::find_if(offers.begin(), offers.end(), [&](const ModOrderOffer& o) { return o.choice.mod == mod && o.choice.order.name == name; });
            if (it == offers.end()) note(ui, std::format("{}: select what it is given to first", a.label));
            else startModOrder(ui, std::move(*it));
        } else if (kind == "panel") {
            modPage_ = !modPage_;
        } else if (kind == "page") {
            ScreenArgs args;
            args.text = std::format("mod-page:{}:{}", mod, name);
            ui.open(ScreenId::Empires, args);
        }
        return;
    }
}

// ---- The report's mod page --------------------------------------------------------------------------------

bool MainWindow::modReport(UiContext& ui, bool draw) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::EmpireId me = ui.session.player();
    if (!tagged_.empty() || modUi(r).ext.panels.empty()) return false;
    struct Part {
        std::vector<const sdk::UiPanel*> panels;
        sdk::UiThing thing;
    };
    std::vector<Part> parts;
    std::string title;   // what the report is about, as the classic report's title names it
    if (vehicle_) {
        const game::Vehicle* v = s.vehicle(*vehicle_);
        if (!v) return false;
        if (const game::Fleet* f = fleet_ ? s.fleet(*fleet_) : nullptr) {
            parts.push_back({modPanelsFor(ui, sdk::UiReport::Fleet, f->owner), {"fleet", static_cast<int64_t>(f->id.value)}});
            title = f->name;
        } else {
            parts.push_back({modPanelsFor(ui, sdk::UiReport::Ship, v->owner), {"vehicle", static_cast<int64_t>(v->id.value)}});
            title = v->name;
        }
    } else if (object_) {
        const game::SpaceObject& o = s.galaxy.object(*object_);
        if (o.kind != game::ObjectKind::Planet && o.kind != game::ObjectKind::Asteroids) return false;
        const game::Colony* c = seenColony(r, s, me, *object_);
        const game::EmpireId owner = c ? c->owner : game::EmpireId{};
        parts.push_back({modPanelsFor(ui, sdk::UiReport::Planet, owner), {"object", static_cast<int64_t>(object_->value)}});
        if (c) parts.push_back({modPanelsFor(ui, sdk::UiReport::Colony, owner), {"colony", static_cast<int64_t>(object_->value)}});
        title = o.name;
    } else if (!listMode_ && shown_.valid() && !emptyReport_ && ui.me().hasExplored(shown_)) {
        parts.push_back({modPanelsFor(ui, sdk::UiReport::System, {}), {"system", static_cast<int64_t>(shown_.value)}});
        title = s.galaxy.system(shown_).name + " System";
    }
    const bool any = std::any_of(parts.begin(), parts.end(), [](const Part& p) { return !p.panels.empty(); });
    if (!draw || !any) return any;
    // The title line, as the classic reports have it (the MOD button at its right end).
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    std::optional<std::pair<sdk::UiButton, sdk::UiThing>> clicked;
    for (size_t i = 0; i < parts.size(); ++i) {
        ImGui::PushID(int(i));
        if (auto b = drawModPanels(ui, parts[i].panels, parts[i].thing)) clicked = std::pair{*b, parts[i].thing};
        ImGui::PopID();
    }
    if (modValuesPending()) ImGui::TextColored(kDimText, "Working out the mods' values...");
    if (clicked && !inputBlocked_) {
        // A panel's button: the mod's order, given to what the report shows.
        const auto& [button, thing] = *clicked;
        const mods::Package* mod = nullptr;
        for (const mods::Package& p : sdk::gamePackages(r))
            if (p.id() == button.mod) mod = &p;
        const mods::ModOrderDecl* decl = mod ? mod->manifest.rules.order(button.order) : nullptr;
        const std::optional<sdk::ModOrderTarget> target = decl ? targetFor(s, me, decl->appliesTo, thing) : std::nullopt;
        std::vector<sdk::ModOrderChoice> choices = target ? sdk::modOrders(r, s, me, *target) : std::vector<sdk::ModOrderChoice>{};
        auto it = std::find_if(choices.begin(), choices.end(), [&](const sdk::ModOrderChoice& c) { return c.mod == button.mod && c.order.name == button.order; });
        if (it == choices.end()) {
            note(ui, std::format("{}: not an order you can give here", decl ? modOrderLabel(r, button.mod, *decl) : button.order));
        } else {
            audio().play("ordbtn");
            std::string label = modOrderLabel(r, it->mod, it->order);
            startModOrder(ui, ModOrderOffer{std::move(*it), *target, std::move(label)});
        }
    }
    return any;
}

bool MainWindow::modPageButton(UiContext& ui, Vec2 at) {
    // OpenSE4's own small button (no classic art): an outlined box with MOD,
    // lit while the report shows the mods' panels. In a child window of its
    // own over the report's body, as the up-arrow is.
    const Vec2 size{33, 21};
    ImGui::SetCursorScreenPos(ui.at(at));
    ImGui::BeginChild("##modPageButton", ui.size(size), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool clicked = ImGui::InvisibleButton("##modpage", ui.size(size));
    const bool hovered = ImGui::IsItemHovered();
    script::reportItem("Mod panels");   // input scripts press it by this name
    if (hovered) ImGui::SetTooltip("%s", modPage_ ? "Back to the report" : "The mods' panels about this");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ui.at(at), b = ui.at(at + size);
    dl->AddRectFilled(a, b, modPage_ ? imColor(0x1c2a4c) : IM_COL32(0, 0, 0, 220));
    const ImU32 line = imColor(modPage_ ? palette::kButtonHeld : hovered ? palette::kButtonHot : palette::kButton);
    dl->AddRect(a, b, line, 0.0f, std::max(1.0f, ui.k()));
    ImFont* font = ui.fonts.small ? ui.fonts.small : ImGui::GetFont();
    const float px = ui.fontPx(kSmallSize);
    const ImVec2 t = font->CalcTextSizeA(px, FLT_MAX, 0.0f, "MOD");
    dl->AddText(font, px, ImVec2(std::floor((a.x + b.x - t.x) * 0.5f), std::floor((a.y + b.y - t.y) * 0.5f)), modPage_ ? IM_COL32_WHITE : line, "MOD");
    ImGui::EndChild();
    if (clicked) audio().play("button");
    return clicked && !inputBlocked_;
}

} // namespace opense4::client::classic
