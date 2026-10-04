// Communicate: compose political messages, build give/take packages (the
// Select Package editor) and answer received messages (docs/spec/06 §1.5,
// §4.2; docs/spec/05 §3.4).

#include "client/classic/reports.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "learn/ids.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace opense4::client::classic {

namespace {

using game::EmpireId;
using game::MessageType;
using game::PackageItem;
using game::Treaty;

enum class PackageTab { Systems, Planets, Resources, Technology, Ships, Units, StarCharts, Treaty, CommChannels, Count };

const char* tabName(PackageTab t) {
    switch (t) {
        case PackageTab::Systems: return "Systems";
        case PackageTab::Planets: return "Planets";
        case PackageTab::Resources: return "Resources";
        case PackageTab::Technology: return "Technology";
        case PackageTab::Ships: return "Ships";
        case PackageTab::Units: return "Units";
        case PackageTab::StarCharts: return "Star Charts";
        case PackageTab::Treaty: return "Treaty";
        case PackageTab::CommChannels: return "Comm Channels";
        case PackageTab::Count: break;
    }
    return "";
}

bool sameItem(const PackageItem& a, const PackageItem& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case PackageItem::Kind::Resources: return true;  // merged
        case PackageItem::Kind::Technology: return a.tech == b.tech;
        case PackageItem::Kind::Planet: return a.planet == b.planet;
        case PackageItem::Kind::Vehicle: return a.vehicle == b.vehicle;
        case PackageItem::Kind::StarChart:
        case PackageItem::Kind::System: return a.system == b.system;
        case PackageItem::Kind::Treaty: return true;  // one treaty per side
        case PackageItem::Kind::CommChannel: return a.empire == b.empire;
    }
    return false;
}

void addItem(std::vector<PackageItem>& side, const PackageItem& item) {
    for (PackageItem& p : side)
        if (sameItem(p, item)) {
            if (item.kind == PackageItem::Kind::Resources) p.resources += item.resources;
            else if (item.kind == PackageItem::Kind::Treaty) p.treaty = item.treaty;
            // Any placeholders may repeat (ask for two planets of their choice).
            else if (isAnyItem(item)) side.push_back(item);
            return;
        }
    side.push_back(item);
}

struct Candidate {
    std::string label;
    PackageItem item;
};

const game::DiplomaticMessage* findMessage(const game::GameState& s, game::MessageId id) {
    if (!id.valid()) return nullptr;
    for (const auto& m : s.messages)
        if (m.id == id) return &m;
    return nullptr;
}

class CommunicateScreen final : public Screen {
public:
    explicit CommunicateScreen(const ScreenArgs& a) : target_(a.empire) {
        if (a.index >= 0) answering_ = game::MessageId{static_cast<uint32_t>(a.index)};
    }

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        // A copy: answering appends to GameState::messages, which may move it.
        std::optional<game::DiplomaticMessage> held;
        if (const game::DiplomaticMessage* m = findMessage(s, answering_); m && m->to == ui.session.player()) held = *m;
        const game::DiplomaticMessage* received = held ? &*held : nullptr;
        if (received) target_ = received->from;
        const auto known = knownEmpires(ui);
        if ((!target_.valid() || target_.index() >= s.empires.size() || target_ == ui.session.player()) && !known.empty()) target_ = known.front();
        const bool haveTarget = target_.valid() && target_.index() < s.empires.size() && target_ != ui.session.player();
        if (!started_ && haveTarget) {
            started_ = true;
            startAgain(ui);
            mode_ = received ? Mode::Received : Mode::Compose;
        }

        Dialog d(ui, "Communicate", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        if (!haveTarget) {
            ImGui::Dummy(ui.size({0, 40}));
            wrappedText("There is no one to talk to: we have not made contact with any other empire yet.", kTextDim);
            d.beginButtons();
            d.close();
            return d.keepOpen();
        }
        switch (mode_) {
            case Mode::Received:
                if (received) receivedView(ui, *received);
                else mode_ = Mode::Compose;
                break;
            case Mode::Compose: compose(ui); break;
            case Mode::Package: packageEditor(ui); break;
        }
        lastOfferPopup(ui);
        confirmPopup(ui);

        d.beginButtons();
        switch (mode_) {
            case Mode::Received: receivedButtons(ui, d, received); break;
            case Mode::Compose: composeButtons(ui, d, received); break;
            case Mode::Package: packageButtons(ui, d); break;
        }
        d.close();
        if (closeNow_) d.requestClose();
        return d.keepOpen();
    }

private:
    enum class Mode { Received, Compose, Package };

    const game::Empire& them(const UiContext& ui) const { return ui.state().empire(target_); }
    const game::Relation& relation(const UiContext& ui) const { return ui.me().relation(target_); }

    void startAgain(UiContext& ui) {
        draft_ = {};
        draft_.type = MessageType::General;
        draft_.tone = 1;
        draft_.treaty = firstProposable(ui);
        draft_.text = defaultMessageText(draft_.type, draft_.treaty);
        replyTo_ = {};
        status_.set({}, false);
    }

    Treaty firstProposable(const UiContext& ui) const {
        const Treaty current = relation(ui).treaty;
        // Peace first when at war, otherwise the next better treaty.
        if (current == Treaty::War) return Treaty::None;
        for (Treaty t : proposableTreaties(current))
            if (t > current && t != Treaty::Subjugation && t != Treaty::Protectorate) return t;
        return proposableTreaties(current).front();
    }

    void setType(MessageType t) {
        const bool keepText = draft_.text != defaultMessageText(draft_.type, draft_.treaty);
        draft_.type = t;
        const MessageNeeds n = messageNeeds(t);
        if (!n.offer) draft_.offer.clear();
        if (!n.request) draft_.request.clear();
        if (!keepText) draft_.text = defaultMessageText(t, draft_.treaty);
    }

    // ---- The target's portrait column -------------------------------------------------------------

    void targetColumn(UiContext& ui) {
        const game::Empire& e = them(ui);
        ImGui::BeginChild("##who", ImVec2(ui.px(150), 0));
        framedImage(ui, ui.art.racePortrait(e.race.style), {128, 128});
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click for the race report");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) openReport(ui);
        ImGui::Spacing();
        empireLabel(ui, e.id, true);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kTextDim, "%s %s", e.leaderTitle.c_str(), e.leaderName.c_str());
        ImGui::TextColored(kTextBlue, "Treaty");
        ImGui::TextUnformatted(std::string(game::displayName(relation(ui).treaty)).c_str());
        if (e.kind != game::PlayerKind::Human) {
            const int anger = e.relation(ui.session.player()).anger;
            ImGui::TextColored(kTextBlue, "Mood");
            ImGui::Text("%s", std::string(moodWord(anger)).c_str());
        }
        if (relation(ui).messageSentThisTurn) ImGui::TextColored(kTextWarn, "We have already sent them a message this turn.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::SameLine();
    }

    void openReport(UiContext& ui) {
        ScreenArgs a;
        a.empire = target_;
        ui.open(ScreenId::RaceReport, a);
    }

    // ---- Composing -------------------------------------------------------------------------------------

    void compose(UiContext& ui) {
        const game::GameState& s = ui.state();
        targetColumn(ui);
        ImGui::BeginChild("##compose", ImVec2(0, 0));
        const float labelW = ui.px(110);
        const float fieldW = ui.px(300);

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kTextBlue, "Message Type");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(fieldW);
        std::vector<MessageType> types = sendableMessageTypes(relation(ui).treaty, s.options);
        if (replyTo_.valid() && counterType(originalType(ui)) != MessageType::General) types.insert(types.begin(), counterType(originalType(ui)));
        if (std::find(types.begin(), types.end(), draft_.type) == types.end()) setType(types.front());
        if (ImGui::BeginCombo("##type", std::string(game::displayName(draft_.type)).c_str(), ImGuiComboFlags_HeightLarge)) {
            for (MessageType t : types) {
                if (ImGui::Selectable(std::string(game::displayName(t)).c_str(), t == draft_.type)) setType(t);
                ui.tagOption("communicate:message-type", learn::optionId(game::displayName(t)));   // for lessons
            }
            ImGui::EndCombo();
        }
        ui.tagItem("communicate:message-type");

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kTextBlue, "Tone");
        ImGui::SameLine(labelW);
        std::optional<ImVec2> tonesMin;
        for (int tone = 0; tone < 3; ++tone) {
            if (tone) ImGui::SameLine();
            if (ImGui::RadioButton(std::string(toneName(tone)).c_str(), draft_.tone == tone)) draft_.tone = tone;
            if (!tonesMin) tonesMin = ImGui::GetItemRectMin();
        }
        ui.tag("communicate:tone", *tonesMin, ImGui::GetItemRectMax());   // for lessons

        const MessageNeeds n = messageNeeds(draft_.type);
        if (n.treaty) treatyPicker(ui, labelW, fieldW);
        // For lessons: the message being written.
        ui.facts.draftMessageType = learn::optionId(game::displayName(draft_.type));
        ui.facts.draftTreaty = n.treaty ? learn::optionId(game::displayName(draft_.treaty)) : std::string{};
        if (n.thirdEmpire) thirdEmpirePicker(ui, labelW, fieldW);
        if (n.system) systemPicker(ui, labelW, fieldW, draft_.type == MessageType::RequestAttackEmpire);
        if (n.planet || n.ownPlanet) planetPicker(ui, labelW, fieldW, n.ownPlanet);

        ImGui::TextColored(kTextBlue, "Message");
        inputTextMultiline("##text", draft_.text, ImVec2(-FLT_MIN, ui.px(96)));
        ui.tagItem("communicate:text");

        if (n.offer || n.request) {
            ImGui::Spacing();
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ui.px(34);
            beginList(ui, "##give", ImVec2(n.request ? half : 0, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
            packageList(ui, "We give", draft_.offer, true);
            endList(ui);
            if (n.request) {
                ImGui::SameLine();
                beginList(ui, "##take", ImVec2(0, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
                packageList(ui, "We ask for", draft_.request, true);
                endList(ui);
            }
        } else {
            ImGui::TextColored(kTextDim, "This kind of message carries no package.");
        }
        status_.draw();
        ImGui::EndChild();
    }

    MessageType originalType(const UiContext& ui) const {
        const game::DiplomaticMessage* m = findMessage(ui.state(), replyTo_);
        return m ? m->type : MessageType::General;
    }

    void treatyPicker(UiContext& ui, float labelW, float fieldW) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kTextBlue, "Treaty");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(fieldW);
        const auto list = proposableTreaties(relation(ui).treaty);
        if (std::find(list.begin(), list.end(), draft_.treaty) == list.end()) draft_.treaty = firstProposable(ui);
        if (ImGui::BeginCombo("##treaty", std::string(game::displayName(draft_.treaty)).c_str())) {
            for (Treaty t : list) {
                if (ImGui::Selectable(std::string(game::displayName(t)).c_str(), t == draft_.treaty)) {
                    const bool defaultText = draft_.text == defaultMessageText(draft_.type, draft_.treaty);
                    draft_.treaty = t;
                    if (defaultText) draft_.text = defaultMessageText(draft_.type, t);
                }
                ui.tagOption("communicate:treaty", learn::optionId(game::displayName(t)));   // for lessons
            }
            ImGui::EndCombo();
        }
        ui.tagItem("communicate:treaty");
    }

    void thirdEmpirePicker(UiContext& ui, float labelW, float fieldW) {
        const game::GameState& s = ui.state();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kTextBlue, "About empire");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(fieldW);
        const std::string label = draft_.thirdEmpire.valid() ? s.empire(draft_.thirdEmpire).name : std::string("Choose an empire");
        if (ImGui::BeginCombo("##third", label.c_str())) {
            for (EmpireId k : knownEmpires(ui))
                if (k != target_ && ImGui::Selectable(s.empire(k).name.c_str(), k == draft_.thirdEmpire)) draft_.thirdEmpire = k;
            ImGui::EndCombo();
        }
    }

    void systemPicker(UiContext& ui, float labelW, float fieldW, bool optional) {
        const game::GameState& s = ui.state();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kTextBlue, "System");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(fieldW);
        const std::string label = draft_.system.valid() ? s.galaxy.system(draft_.system).name
                                                        : std::string(optional ? "Anywhere" : "Choose a system");
        if (ImGui::BeginCombo("##system", label.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (optional && ImGui::Selectable("Anywhere", !draft_.system.valid())) draft_.system = {};
            std::vector<game::SystemId> systems;
            for (const game::StarSystem& sys : s.galaxy.systems)
                if (ui.me().hasExplored(sys.id)) systems.push_back(sys.id);
            std::sort(systems.begin(), systems.end(),
                      [&](game::SystemId a, game::SystemId b) { return std::pair(s.galaxy.system(a).name, a) < std::pair(s.galaxy.system(b).name, b); });
            for (game::SystemId sys : systems)
                if (ImGui::Selectable(s.galaxy.system(sys).name.c_str(), sys == draft_.system)) draft_.system = sys;
            ImGui::EndCombo();
        }
    }

    // Planets for demands: the recipient's (Leave Planet), a third party's (Attack
    // Planet) or our own (Grant Independence).
    std::vector<game::ObjectId> planetChoices(const UiContext& ui, bool own) const {
        const game::GameState& s = ui.state();
        const EmpireId me = ui.session.player();
        std::vector<game::ObjectId> out;
        for (const auto& c : s.colonies) {
            if (!c) continue;
            if (own) {
                if (c->owner == me) out.push_back(c->planet);
                continue;
            }
            if (c->owner == me || !ui.me().hasExplored(s.galaxy.object(c->planet).system)) continue;
            const bool theirs = c->owner == target_;
            if (draft_.type == MessageType::DemandLeavePlanet ? theirs : !theirs) out.push_back(c->planet);
        }
        return out;
    }

    void planetPicker(UiContext& ui, float labelW, float fieldW, bool own) {
        const game::GameState& s = ui.state();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kTextBlue, own ? "Our planet" : "Planet");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(fieldW);
        const std::string label = draft_.planet.valid() ? s.galaxy.object(draft_.planet).name : std::string("Choose a planet");
        if (ImGui::BeginCombo("##planet", label.c_str(), ImGuiComboFlags_HeightLarge)) {
            const auto choices = planetChoices(ui, own);
            if (choices.empty()) ImGui::TextColored(kTextDim, "No suitable planet known");
            for (game::ObjectId p : choices) {
                const game::Colony* c = s.colony(p);
                const std::string name = c && c->owner != ui.session.player()
                                             ? std::format("{} ({})", s.galaxy.object(p).name, s.empire(c->owner).name)
                                             : s.galaxy.object(p).name;
                if (ImGui::Selectable(name.c_str(), p == draft_.planet)) draft_.planet = p;
            }
            ImGui::EndCombo();
        }
    }

    // A package side; clicking an item removes it when editable.
    void packageList(UiContext& ui, const char* title, std::vector<PackageItem>& items, bool editable) {
        heading(ui, title);
        if (items.empty()) {
            ImGui::TextColored(kTextDim, editable ? "Nothing yet (Edit Package)" : "Nothing");
            return;
        }
        std::optional<size_t> remove;
        for (size_t i = 0; i < items.size(); ++i) {
            ImGui::PushID(int(i));
            const std::string text = packageItemText(ui.rules(), ui.state(), items[i]);
            if (editable) {
                if (ImGui::Selectable(text.c_str())) remove = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to remove");
            } else {
                ImGui::BulletText("%s", text.c_str());
            }
            ImGui::PopID();
        }
        if (remove) items.erase(items.begin() + std::ptrdiff_t(*remove));
    }

    std::string problem(const UiContext& ui) const {
        const MessageNeeds n = messageNeeds(draft_.type);
        if (relation(ui).messageSentThisTurn) return "Only one message per empire each turn.";
        if (n.thirdEmpire && !draft_.thirdEmpire.valid()) return "Choose the empire this message is about.";
        if (n.system && !draft_.system.valid() && draft_.type != MessageType::RequestAttackEmpire) return "Choose a system.";
        if ((n.planet || n.ownPlanet) && !draft_.planet.valid()) return "Choose a planet.";
        if ((draft_.type == MessageType::Gift || draft_.type == MessageType::Tribute) && draft_.offer.empty()) return "The package is empty.";
        if (n.offer && n.request && draft_.offer.empty() && draft_.request.empty()) return "The package is empty.";
        return {};
    }

    void send(UiContext& ui) {
        if (const std::string p = problem(ui); !p.empty()) {
            status_.set(p, true);
            return;
        }
        const MessageNeeds n = messageNeeds(draft_.type);
        game::DiplomaticMessage m;
        m.to = target_;
        m.type = draft_.type;
        m.tone = draft_.tone;
        m.text = draft_.text;
        m.treaty = n.treaty ? draft_.treaty : relation(ui).treaty;
        if (n.offer) m.offer = draft_.offer;
        if (n.request) m.request = draft_.request;
        if (n.thirdEmpire) m.thirdEmpire = draft_.thirdEmpire;
        if (n.system) m.system = draft_.system;
        if (n.planet || n.ownPlanet) m.planet = draft_.planet;
        m.inReplyTo = replyTo_;
        game::cmd::SendMessage c;
        c.message = std::move(m);
        const game::CommandResult r = ui.session.issue(std::move(c));
        status_.result(r);
        if (r.ok) closeNow_ = true;
    }

    void composeButtons(UiContext& ui, Dialog& d, const game::DiplomaticMessage* received) {
        const MessageNeeds n = messageNeeds(draft_.type);
        if (d.button("Report")) openReport(ui);
        if (d.button("View Last Offer", lastOffer(ui) != nullptr)) showOffer_ = true;
        if (d.button("Edit Package", n.offer || n.request)) {
            mode_ = Mode::Package;
            giveSide_ = n.offer;
        }
        if (d.button("Start Again")) startAgain(ui);
        if (received && d.button("Back To Message")) mode_ = Mode::Received;
        d.spacer();
        const bool sendClicked = d.button("Send Message", !relation(ui).messageSentThisTurn);
        ui.tagItem("communicate:send");
        if (sendClicked) {
            if (draft_.type == MessageType::Surrender || draft_.type == MessageType::DeclareWar) confirm_ = true;
            else send(ui);
        }
    }

    // ---- Package editor ("Select Package") --------------------------------------------------------------

    std::vector<Candidate> candidates(const UiContext& ui, PackageTab tab, bool give) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const game::Empire& me = ui.me();
        const EmpireId owner = give ? me.id : target_;
        std::vector<Candidate> out;
        auto any = [&](PackageItem::Kind k, const char* label) {
            if (!give) {
                PackageItem i;
                i.kind = k;
                out.push_back({label, i});
            }
        };
        switch (tab) {
            case PackageTab::Systems:
                any(PackageItem::Kind::System, "Any system claim (they choose)");
                for (game::SystemId sys : s.empire(owner).claimedSystems) {
                    PackageItem i;
                    i.kind = PackageItem::Kind::System;
                    i.system = sys;
                    out.push_back({me.hasExplored(sys) ? s.galaxy.system(sys).name : std::string("Unexplored system"), i});
                }
                break;
            case PackageTab::Planets:
                any(PackageItem::Kind::Planet, "Any planet (they choose)");
                for (const auto& c : s.colonies) {
                    if (!c || c->owner != owner) continue;
                    if (!give && !me.hasExplored(s.galaxy.object(c->planet).system)) continue;
                    PackageItem i;
                    i.kind = PackageItem::Kind::Planet;
                    i.planet = c->planet;
                    out.push_back({std::format("{}{}", s.galaxy.object(c->planet).name, c->homeworld ? " (homeworld)" : ""), i});
                }
                break;
            case PackageTab::Resources: break;
            case PackageTab::Technology:
                if (!s.options.allowTechTrades) break;
                any(PackageItem::Kind::Technology, "Any technology (they choose)");
                for (uint32_t a = 0; a < r.data().techAreas.size(); ++a) {
                    const ruleset::TechAreaId id{a};
                    if (give && me.techLevel(id) <= 0) continue;
                    if (!give && !r.techVisible(s, me, id) && me.techLevel(id) == 0) continue;
                    PackageItem i;
                    i.kind = PackageItem::Kind::Technology;
                    i.tech = id;
                    out.push_back({give ? std::format("{} (level {})", r.tech(id).name, me.techLevel(id))
                                        : std::format("{} (we have {})", r.tech(id).name, me.techLevel(id)),
                                   i});
                }
                break;
            case PackageTab::Ships:
            case PackageTab::Units: {
                const bool units = tab == PackageTab::Units;
                any(PackageItem::Kind::Vehicle, units ? "Any unit (they choose)" : "Any ship (they choose)");
                auto consider = [&](const game::Vehicle& v) {
                    if (v.owner != owner || game::isUnitType(game::vehicleType(r, s, v)) != units) return;
                    PackageItem i;
                    i.kind = PackageItem::Kind::Vehicle;
                    i.vehicle = v.id;
                    const std::string name = v.count > 1 ? std::format("{} x{}", v.name, v.count) : v.name;
                    out.push_back({std::format("{} ({})", name, v.mixed.empty() ? s.design(v.design).name : groupDesigns(s, v, 2)), i});
                };
                if (give)
                    for (const game::Vehicle& v : s.vehicles) consider(v);
                else
                    for (game::VehicleId id : me.knowledge.visibleVehicles)
                        if (const game::Vehicle* v = s.vehicle(id)) consider(*v);
                break;
            }
            case PackageTab::StarCharts:
                any(PackageItem::Kind::StarChart, "Any star chart (they choose)");
                for (const game::StarSystem& sys : s.galaxy.systems) {
                    if (me.hasExplored(sys.id) != give) continue;
                    PackageItem i;
                    i.kind = PackageItem::Kind::StarChart;
                    i.system = sys.id;
                    out.push_back({give ? sys.name : std::format("Unexplored system at {}, {}", sys.position.x, sys.position.y), i});
                }
                break;
            case PackageTab::Treaty:
                for (Treaty t : proposableTreaties(relation(ui).treaty)) {
                    PackageItem i;
                    i.kind = PackageItem::Kind::Treaty;
                    i.treaty = t;
                    out.push_back({std::string(game::displayName(t)), i});
                }
                break;
            case PackageTab::CommChannels:
                any(PackageItem::Kind::CommChannel, "Any comm channel (they choose)");
                if (give)
                    for (EmpireId k : knownEmpires(ui))
                        if (k != target_) {
                            PackageItem i;
                            i.kind = PackageItem::Kind::CommChannel;
                            i.empire = k;
                            out.push_back({s.empire(k).name, i});
                        }
                break;
            case PackageTab::Count: break;
        }
        return out;
    }

    void packageEditor(UiContext& ui) {
        const MessageNeeds n = messageNeeds(draft_.type);
        if (!n.request) giveSide_ = true;
        if (!n.offer) giveSide_ = false;
        heading(ui, "Select Package");
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "%s: %s", std::string(game::displayName(draft_.type)).c_str(), tabName(tab_));
        ImGui::BeginDisabled(!n.offer);
        if (ImGui::RadioButton("We give", giveSide_)) giveSide_ = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!n.request);
        if (ImGui::RadioButton("We ask for", !giveSide_)) giveSide_ = false;
        ImGui::EndDisabled();
        ImGui::Separator();

        std::vector<PackageItem>& side = giveSide_ ? draft_.offer : draft_.request;
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const float h = ImGui::GetContentRegionAvail().y - ui.px(30);
        beginList(ui, "##available", ImVec2(half, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        heading(ui, giveSide_ ? "Ours to offer" : "Theirs to ask for");
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "(click to add)");
        if (tab_ == PackageTab::Resources) {
            resourceInput(ui, side);
        } else {
            const auto list = candidates(ui, tab_, giveSide_);
            if (list.empty())
                ImGui::TextColored(kTextDim, "%s", tab_ == PackageTab::Technology && !ui.state().options.allowTechTrades
                                                       ? "Technology trades are disabled in this game."
                                                       : "Nothing available.");
            for (size_t i = 0; i < list.size(); ++i) {
                ImGui::PushID(int(i));
                const bool present = !isAnyItem(list[i].item) && std::any_of(side.begin(), side.end(), [&](const PackageItem& p) {
                    return sameItem(p, list[i].item) && (p.kind != PackageItem::Kind::Treaty || p.treaty == list[i].item.treaty);
                });
                if (ImGui::Selectable(list[i].label.c_str(), present) && !present) addItem(side, list[i].item);
                ImGui::PopID();
            }
        }
        endList(ui);
        ImGui::SameLine();
        beginList(ui, "##current", ImVec2(0, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        packageList(ui, giveSide_ ? "We give" : "We ask for", side, true);
        endList(ui);
        ImGui::TextColored(kTextDim, "Items the other side is asked to choose (\"Any\") must be filled in before a trade can be accepted.");
    }

    void resourceInput(UiContext& ui, std::vector<PackageItem>& side) {
        static constexpr std::array<game::Resource, 3> kRes{game::Resource::Minerals, game::Resource::Organics, game::Resource::Radioactives};
        ImGui::TextColored(kTextDim, "In steps of 1,000.");
        for (size_t i = 0; i < 3; ++i) {
            ImGui::PushID(int(i));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(std::string(game::displayName(kRes[i])).c_str());
            ImGui::SameLine(ui.px(110));
            ImGui::SetNextItemWidth(ui.px(160));
            const int64_t step = 1000, fast = 10000;
            ImGui::InputScalar("##amount", ImGuiDataType_S64, &amounts_.v[i], &step, &fast);
            amounts_.v[i] = std::max<int64_t>(0, amounts_.v[i] / 1000 * 1000);
            ImGui::PopID();
        }
        if (giveSide_) {
            ImGui::TextColored(kTextDim, "We have:");
            ImGui::SameLine();
            resources(ui, ui.me().stockpile, true);
        }
        if (ImGui::Button("Add To Package", ImVec2(ui.px(160), ui.px(24))) && !amounts_.isZero()) {
            PackageItem i;
            i.kind = PackageItem::Kind::Resources;
            i.resources = amounts_;
            addItem(side, i);
            amounts_ = {};
        }
    }

    void packageButtons(UiContext&, Dialog& d) {
        for (int t = 0; t < int(PackageTab::Count); ++t) {
            const auto tab = static_cast<PackageTab>(t);
            if (d.tab(tabName(tab), tab_ == tab)) tab_ = tab;
        }
        d.spacer();
        if (d.button("Clear Package")) (giveSide_ ? draft_.offer : draft_.request).clear();
        if (d.button("Done")) mode_ = Mode::Compose;
    }

    // ---- Received messages ------------------------------------------------------------------------------

    void messageBody(UiContext& ui, const game::DiplomaticMessage& m) {
        const game::GameState& s = ui.state();
        labelValue(ui, "Type", std::string(game::displayName(m.type)));
        labelValue(ui, "Tone", std::string(toneName(m.tone)));
        labelValue(ui, "Sent", formatDate(m.sentTurn));
        const MessageNeeds n = messageNeeds(m.type);
        if (n.treaty || m.type == MessageType::AcceptTreaty || m.type == MessageType::RefuseTreaty)
            labelValue(ui, "Treaty", std::string(game::displayName(m.treaty)));
        if (m.thirdEmpire.valid() && m.thirdEmpire.index() < s.empires.size()) labelValue(ui, "About", s.empire(m.thirdEmpire).name);
        if (m.system.valid() && m.system.index() < s.galaxy.systems.size()) labelValue(ui, "System", s.galaxy.system(m.system).name);
        if (m.planet.valid() && m.planet.index() < s.galaxy.objects.size()) labelValue(ui, "Planet", s.galaxy.object(m.planet).name);
        ImGui::Spacing();
        ImGui::BeginChild("##text", ImVec2(0, ui.px(80)), ImGuiChildFlags_Borders);
        {
            const ReadingText reading(ui.painter());   // it scrolls
            wrappedText(m.text.empty() ? std::string("(no text)") : m.text);
        }
        ImGui::EndChild();
        if (!m.offer.empty() || !m.request.empty()) {
            auto offer = m.offer, request = m.request;
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            beginList(ui, "##offer", ImVec2(half, ui.px(120)), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
            packageList(ui, "They give", offer, false);
            endList(ui);
            ImGui::SameLine();
            beginList(ui, "##request", ImVec2(0, ui.px(120)), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
            packageList(ui, "They ask for", request, false);
            endList(ui);
        }
    }

    void receivedView(UiContext& ui, const game::DiplomaticMessage& m) {
        targetColumn(ui);
        ImGui::BeginChild("##received", ImVec2(0, 0));
        heading(ui, "Message received");
        messageBody(ui, m);
        if (m.answered) {
            ImGui::TextColored(kTextDim, "We have answered this message.");
        } else if (answerable(m.type)) {
            ImGui::TextColored(kTextBlue, "Our answer (optional)");
            inputTextMultiline("##answer", answerText_, ImVec2(-FLT_MIN, ui.px(60)));
            if (packageHasAny(m.request))
                ImGui::TextColored(kTextWarn, "They left us to choose some items: counter-propose with real items to accept.");
        } else {
            ImGui::TextColored(kTextDim, "This message needs no answer; you may reply with a message of your own.");
        }
        status_.draw();
        ImGui::EndChild();
    }

    void answer(UiContext& ui, const game::DiplomaticMessage& m, bool accept) {
        game::cmd::AnswerMessage c;
        c.message = m.id;
        c.accept = accept;
        c.text = answerText_;
        const game::CommandResult r = ui.session.issue(std::move(c));
        status_.result(r);
        if (r.ok) closeNow_ = true;
    }

    void receivedButtons(UiContext& ui, Dialog& d, const game::DiplomaticMessage* m) {
        if (!m) return;
        const bool open = answerable(m->type) && !m->answered;
        if (d.button("Accept", open && !packageHasAny(m->request))) answer(ui, *m, true);
        if (d.button("Refuse", open)) answer(ui, *m, false);
        const MessageType counter = counterType(m->type);
        if (d.button("Counter", open && counter != MessageType::General)) {
            startAgain(ui);
            replyTo_ = m->id;
            draft_.type = counter;
            draft_.treaty = m->treaty;
            draft_.offer = m->request;  // what they asked of us
            draft_.request = m->offer;
            std::erase_if(draft_.offer, [](const PackageItem& i) { return isAnyItem(i); });
            draft_.text = defaultMessageText(counter, draft_.treaty);
            mode_ = Mode::Compose;
        }
        if (d.button("Reply")) {
            startAgain(ui);
            replyTo_ = m->id;
            mode_ = Mode::Compose;
        }
        d.spacer();
        if (d.button("Report")) openReport(ui);
    }

    // The newest delivered message from the target that carried a package.
    const game::DiplomaticMessage* lastOffer(const UiContext& ui) const {
        const game::DiplomaticMessage* best = nullptr;
        for (const auto& m : ui.state().messages)
            if (m.from == target_ && m.to == ui.session.player() && m.delivered && (!m.offer.empty() || !m.request.empty()))
                if (!best || m.sentTurn >= best->sentTurn) best = &m;
        return best;
    }

    void lastOfferPopup(UiContext& ui) {
        if (showOffer_) {
            ImGui::OpenPopup("Last Offer");
            showOffer_ = false;
        }
        ImGui::SetNextWindowSize(ui.size({520, 440}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ui.at({(frameW() - 520) * 0.5f, (frameH() - 440) * 0.5f}), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal("Last Offer", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
            return;
        if (const game::DiplomaticMessage* m = lastOffer(ui)) {
            empireLabel(ui, m->from, true);
            messageBody(ui, *m);
        } else {
            ImGui::TextColored(kTextDim, "No offer.");
        }
        const float y = ImGui::GetWindowHeight() - ui.px(26) - ImGui::GetStyle().WindowPadding.y;
        if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void confirmPopup(UiContext& ui) {
        if (confirm_) {
            ImGui::OpenPopup("Are you sure?");
            confirm_ = false;
        }
        ImGui::SetNextWindowSize(ui.size({420, 170}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ui.at({(frameW() - 420) * 0.5f, (frameH() - 170) * 0.5f}), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal("Are you sure?", nullptr,
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags))
            return;
        wrappedText(draft_.type == MessageType::Surrender
                        ? std::format("Surrender our whole empire to the {}? This cannot be undone.", them(ui).name)
                        : std::format("Declare war on the {}? It takes effect at once.", them(ui).name));
        const float y = ImGui::GetWindowHeight() - ui.px(26) - ImGui::GetStyle().WindowPadding.y;
        if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        // Y means Yes; N, Esc and Enter mean No (spec 06 §3.4).
        const std::optional<bool> key = yesNoKey();
        if (ImGui::Button("Yes", ImVec2(w, ui.px(26))) || key == true) {
            ImGui::CloseCurrentPopup();
            send(ui);
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(w, ui.px(26))) || key == false) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    EmpireId target_;
    game::MessageId answering_;
    game::MessageId replyTo_;
    bool started_ = false;
    Mode mode_ = Mode::Compose;
    game::DiplomaticMessage draft_;
    std::string answerText_;
    PackageTab tab_ = PackageTab::Resources;
    bool giveSide_ = true;
    game::Resources amounts_;
    bool showOffer_ = false;
    bool confirm_ = false;
    bool closeNow_ = false;
    StatusLine status_;
};

} // namespace

std::unique_ptr<Screen> makeCommunicate(const ScreenArgs& args) { return std::make_unique<CommunicateScreen>(args); }

} // namespace opense4::client::classic
