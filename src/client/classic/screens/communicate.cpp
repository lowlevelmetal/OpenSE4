// Communicate: compose political messages, build give/take packages (the
// Select Package editor) and answer received messages (docs/spec/06 §1.5,
// §4.2; docs/spec/05 §3.4).

#include "client/classic/reports.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "learn/ids.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <random>

namespace opense4::client::classic {

namespace {

using game::EmpireId;
using game::MessageType;
using game::PackageItem;
using game::Treaty;

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

const game::DiplomaticMessage* findMessage(const game::GameState& s, game::MessageId id) {
    if (!id.valid()) return nullptr;
    for (const auto& m : s.messages)
        if (m.id == id) return &m;
    return nullptr;
}

bool isReplyType(MessageType t) { return replyAccepts(t).has_value() || t == MessageType::CounterTreaty || t == MessageType::CounterTrade; }

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
            // Opened from a portrait, the window answers the newest message
            // from that empire, when it takes an answer (spec 06 §4.2,
            // confirmed: binary). Our engine answers a message once.
            if (!received)
                if (const game::DiplomaticMessage* m = replyableMessage(s, ui.session.player(), target_); m && answerable(m->type) && !m->answered)
                    portraitReply_ = m->id;
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
        offerPopup(ui);
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

    // The message the portrait's reply types answer, while it still can be.
    const game::DiplomaticMessage* portraitMessage(const UiContext& ui) const {
        const game::DiplomaticMessage* m = findMessage(ui.state(), portraitReply_);
        return m && !m->answered ? m : nullptr;
    }

    // The type of the message the draft answers, for its speech pool.
    MessageType answeredType(const UiContext& ui) const {
        const game::DiplomaticMessage* m = findMessage(ui.state(), replyTo_);
        return m ? m->type : MessageType::General;
    }

    // The draft's text from our race's speech (spec 06 §4.2, confirmed:
    // binary). The line is drawn from the window's own random source, never
    // the game's.
    void fillText(const UiContext& ui) {
        draft_.to = target_;
        draft_.text = messageText(ui.rules(), ui.state(), ui.session.player(), draft_, answeredType(ui), pick_);
        autoText_ = draft_.text;
    }

    void startAgain(UiContext& ui) {
        draft_ = {};
        draft_.type = MessageType::General;
        draft_.tone = 1;
        draft_.treaty = firstProposable(ui);
        replyTo_ = {};
        pick_ = textRng_();
        fillText(ui);
        status_.set({}, false);
    }

    Treaty firstProposable(const UiContext& ui) const {
        const Treaty current = relation(ui).treaty;
        // The next better treaty (peace from war is Non-Intercourse or better).
        for (Treaty t : proposableTreaties(current))
            if (t > current && t != Treaty::Subjugation && t != Treaty::Protectorate) return t;
        return proposableTreaties(current).front();
    }

    // `picked`: the player chose the type, so a type with a package opens
    // Select Package at once, on the side it fills first (spec 06 §4.2,
    // confirmed: binary).
    void setType(UiContext& ui, MessageType t, bool picked) {
        const bool ownText = draft_.text != autoText_;
        const MessageType before = draft_.type;
        draft_.type = t;
        if (const game::DiplomaticMessage* m = portraitMessage(ui); m && isReplyType(t)) {
            replyTo_ = m->id;
            if (t == MessageType::CounterTreaty) {
                const auto list = proposableTreaties(relation(ui).treaty);
                draft_.treaty = std::find(list.begin(), list.end(), m->treaty) != list.end() ? m->treaty : firstProposable(ui);
            }
            // A counter-trade starts from their packages, the sides swapped.
            if (t == MessageType::CounterTrade && before != t) {
                draft_.offer = m->request;
                draft_.request = m->offer;
            }
        } else if (!answering_.valid()) {
            replyTo_ = {};
        }
        const MessageNeeds n = messageNeeds(t);
        if (!n.offer) draft_.offer.clear();
        if (!n.request) draft_.request.clear();
        if (before != t) {
            pick_ = textRng_();
            status_.set({}, false);
        }
        if (!ownText) fillText(ui);
        if (picked && (n.offer || n.request)) {
            mode_ = Mode::Package;
            giveSide_ = n.offer;
        }
    }

    std::string typeLabel(const UiContext& ui, MessageType t) const {
        if (const game::DiplomaticMessage* m = portraitMessage(ui); m && isReplyType(t)) return replyTypeName(t, m->type);
        return std::string(game::displayName(t));
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
        std::vector<MessageType> types =
            sendableMessageTypes(relation(ui).treaty, s.options, game::diplomacy::isSubjugated(s, ui.session.player()));
        // The answers to the newest message, when opened from the portrait.
        if (const game::DiplomaticMessage* m = portraitMessage(ui)) {
            const std::vector<MessageType> replies = replyTypes(m->type);
            types.insert(types.begin(), replies.begin(), replies.end());
        }
        if (replyTo_.valid() && counterType(answeredType(ui)) != MessageType::General &&
            std::find(types.begin(), types.end(), counterType(answeredType(ui))) == types.end())
            types.insert(types.begin(), counterType(answeredType(ui)));
        if (std::find(types.begin(), types.end(), draft_.type) == types.end()) setType(ui, types.front(), false);
        if (ImGui::BeginCombo("##type", typeLabel(ui, draft_.type).c_str(), ImGuiComboFlags_HeightLarge)) {
            for (MessageType t : types) {
                if (ImGui::Selectable(typeLabel(ui, t).c_str(), t == draft_.type) && t != draft_.type) setType(ui, t, true);
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

        const game::DiplomaticMessage* answered = replyAccepts(draft_.type) ? portraitMessage(ui) : nullptr;
        if (n.offer || n.request) {
            ImGui::Spacing();
            const bool both = n.offer && n.request;
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ui.px(34);
            if (n.offer) {
                beginList(ui, "##give", ImVec2(both ? half : 0, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
                packageList(ui, "We give", draft_.offer, true);
                endList(ui);
            }
            if (n.request) {
                if (both) ImGui::SameLine();
                beginList(ui, "##take", ImVec2(0, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
                packageList(ui, "We ask for", draft_.request, true);
                endList(ui);
            }
        } else if (answered) {
            // An answer: what it answers.
            ImGui::TextColored(kTextDim, "In answer to their %s of %s.", std::string(game::displayName(answered->type)).c_str(),
                               formatDate(answered->sentTurn).c_str());
            if (!answered->offer.empty() || !answered->request.empty()) {
                auto offer = answered->offer, request = answered->request;
                const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                const float h = ImGui::GetContentRegionAvail().y - ui.px(34);
                beginList(ui, "##theygive", ImVec2(half, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
                packageList(ui, "They give", offer, false);
                endList(ui);
                ImGui::SameLine();
                beginList(ui, "##theyask", ImVec2(0, h), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
                packageList(ui, "They ask for", request, false);
                endList(ui);
            }
        } else {
            ImGui::TextColored(kTextDim, "This kind of message carries no package.");
        }
        status_.draw();
        ImGui::EndChild();
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
                if (ImGui::Selectable(std::string(game::displayName(t)).c_str(), t == draft_.treaty) && t != draft_.treaty) {
                    const bool auto_ = draft_.text == autoText_;
                    draft_.treaty = t;
                    if (auto_) fillText(ui);
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
                if (k != target_ && ImGui::Selectable(s.empire(k).name.c_str(), k == draft_.thirdEmpire)) {
                    const bool auto_ = draft_.text == autoText_;
                    draft_.thirdEmpire = k;
                    if (auto_) fillText(ui);
                }
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
        const bool auto_ = draft_.text == autoText_;
        const game::SystemId before = draft_.system;
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
        if (auto_ && draft_.system != before) fillText(ui);
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
        const bool auto_ = draft_.text == autoText_;
        const game::ObjectId before = draft_.planet;
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
        if (auto_ && draft_.planet != before) fillText(ui);
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

    // What the original's window would not send (spec 06 §4.2, confirmed:
    // binary); the engine checks the same (game::diplomacy::playerMessageProblem).
    std::string problem(const UiContext& ui) const {
        const game::GameState& s = ui.state();
        const MessageNeeds n = messageNeeds(draft_.type);
        if (relation(ui).messageSentThisTurn) return "Only one message per empire each turn.";
        if (const std::optional<bool> accept = replyAccepts(draft_.type)) {
            const game::DiplomaticMessage* m = portraitMessage(ui);
            if (!m) return "There is no message to answer.";
            if (*accept && draft_.type == MessageType::AcceptTreaty && game::diplomacy::isSubjugated(s, ui.session.player()))
                return "Subjugated races cannot accept treaties.";
            if (*accept && draft_.type == MessageType::AcceptTrade && packageHasAny(m->request))
                return "They left us to choose some items: counter-propose with real items to accept.";
            return {};
        }
        if (n.thirdEmpire && !draft_.thirdEmpire.valid()) return "Choose the empire this message is about.";
        if (n.system && !draft_.system.valid() && draft_.type != MessageType::RequestAttackEmpire) return "Choose a system.";
        if ((n.planet || n.ownPlanet) && !draft_.planet.valid()) return "Choose a planet.";
        if ((draft_.type == MessageType::ProposeTreaty || draft_.type == MessageType::CounterTreaty) &&
            game::diplomacy::isSubjugated(s, ui.session.player()))
            return "Subjugated races cannot propose treaties.";
        if ((draft_.type == MessageType::Gift || draft_.type == MessageType::Tribute) && draft_.offer.empty()) return "The package is empty.";
        if ((draft_.type == MessageType::DemandGift || draft_.type == MessageType::DemandTribute) && draft_.request.empty())
            return "The package is empty.";
        // A trade needs items on both sides, and what we give is never "Any".
        if (tradePackage(draft_.type) && (draft_.offer.empty() || draft_.request.empty())) return "A trade needs items on both sides.";
        if (tradePackage(draft_.type) && packageHasAny(draft_.offer))
            return "A trade offer cannot contain unspecified \"Any\" items: replace them with real ones.";
        return {};
    }

    void send(UiContext& ui) {
        if (const std::string p = problem(ui); !p.empty()) {
            status_.set(p, true);
            return;
        }
        // An answer from the portrait's list (spec 06 §4.2): the engine's answer to that message.
        if (const std::optional<bool> accept = replyAccepts(draft_.type)) {
            game::cmd::AnswerMessage c;
            c.message = portraitReply_;
            c.accept = *accept;
            c.text = draft_.text;
            const game::CommandResult r = ui.session.issue(std::move(c));
            status_.result(r);
            if (r.ok) closeNow_ = true;
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

    // Report, View Trade, View Gift, View Tribute, Edit Package, Edit Trade,
    // Start Again, Send Message (spec 06 §1.5, confirmed: binary); OpenSE4
    // adds Back To Message after a message opened from the Log.
    void composeButtons(UiContext& ui, Dialog& d, const game::DiplomaticMessage* received) {
        const MessageNeeds n = messageNeeds(draft_.type);
        if (d.button("Report")) openReport(ui);
        const OfferView view = offerView(replyableMessage(ui.state(), ui.session.player(), target_));
        if (d.button("View Trade", view == OfferView::Trade)) showOffer_ = true;
        if (d.button("View Gift", view == OfferView::Gift)) showOffer_ = true;
        if (d.button("View Tribute", view == OfferView::Tribute)) showOffer_ = true;
        const bool trade = tradePackage(draft_.type);
        if (d.button("Edit Package", (n.offer || n.request) && !trade)) {
            mode_ = Mode::Package;
            giveSide_ = n.offer;
        }
        if (d.button("Edit Trade", trade)) {
            mode_ = Mode::Package;
            giveSide_ = true;
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

    std::string choiceLabel(const UiContext& ui, PackageTab tab, bool give, const PackageItem& i) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const game::Empire& me = ui.me();
        using K = PackageItem::Kind;
        if (isAnyItem(i)) {
            switch (i.kind) {
                case K::Planet: return "Any planet (they choose)";
                case K::Technology: return "Any technology (they choose)";
                case K::Vehicle: return tab == PackageTab::Units ? "Any unit transport (they choose)" : "Any ship (they choose)";
                case K::StarChart: return "Any star chart (they choose)";
                default: return "Any (they choose)";
            }
        }
        switch (i.kind) {
            case K::System:
            case K::StarChart: return s.galaxy.system(i.system).name;
            case K::Planet: {
                const game::Colony* c = s.colony(i.planet);
                return std::format("{}{}", s.galaxy.object(i.planet).name, c && c->homeworld ? " (homeworld)" : "");
            }
            case K::Technology:
                return give ? std::format("{} (level {})", r.tech(i.tech).name, me.techLevel(i.tech))
                            : std::format("{} (we have {})", r.tech(i.tech).name, me.techLevel(i.tech));
            case K::Vehicle: {
                const game::Vehicle* v = s.vehicle(i.vehicle);
                if (!v) return "?";
                const std::string name = v->count > 1 ? std::format("{} x{}", v->name, v->count) : v->name;
                const std::string designs = v->mixed.empty() ? s.design(v->design).name : groupDesigns(s, *v, 2);
                if (tab != PackageTab::Units) return std::format("{} ({})", name, designs);
                int units = 0;
                for (const game::UnitStack& u : v->cargo.units) units += u.count;
                return std::format("{} ({}, {} unit{})", name, designs, units, units == 1 ? "" : "s");
            }
            case K::Treaty: return std::string(game::displayName(i.treaty));
            case K::CommChannel: return s.empire(i.empire).name;
            case K::Resources: break;
        }
        return packageItemText(r, s, i);
    }

    void packageEditor(UiContext& ui) {
        const game::GameState& s = ui.state();
        const MessageNeeds n = messageNeeds(draft_.type);
        if (!n.request) giveSide_ = true;
        if (!n.offer) giveSide_ = false;
        if (!packageTabEnabled(s.options, tab_)) tab_ = PackageTab::Resources;
        heading(ui, "Select Package");
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "%s: %s", typeLabel(ui, draft_.type).c_str(), std::string(packageTabName(tab_)).c_str());
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
            const auto list = packageChoices(ui.rules(), s, ui.session.player(), target_, tab_, giveSide_);
            if (list.empty()) ImGui::TextColored(kTextDim, "Nothing available.");
            for (size_t i = 0; i < list.size(); ++i) {
                ImGui::PushID(int(i));
                const bool present = !isAnyItem(list[i]) && std::any_of(side.begin(), side.end(), [&](const PackageItem& p) {
                    return sameItem(p, list[i]) && (p.kind != PackageItem::Kind::Treaty || p.treaty == list[i].treaty);
                });
                if (ImGui::Selectable(choiceLabel(ui, tab_, giveSide_, list[i]).c_str(), present) && !present) addItem(side, list[i]);
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

    void packageButtons(UiContext& ui, Dialog& d) {
        for (int t = 0; t < int(PackageTab::Count); ++t) {
            const auto tab = static_cast<PackageTab>(t);
            // Technology is dim when the game allows no technology trades.
            if (d.tab(std::string(packageTabName(tab)).c_str(), tab_ == tab, packageTabEnabled(ui.state().options, tab))) tab_ = tab;
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
            draft_.request = m->offer;   // "Any" items they asked for stay: Send asks for real ones
            pick_ = textRng_();
            fillText(ui);
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

    // View Trade, View Gift and View Tribute: the newest message from the
    // target (replyableMessage), which lit the button.
    void offerPopup(UiContext& ui) {
        if (showOffer_) {
            ImGui::OpenPopup("Their Offer");
            showOffer_ = false;
        }
        ImGui::SetNextWindowSize(ui.size({520, 440}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ui.at({(frameW() - 520) * 0.5f, (frameH() - 440) * 0.5f}), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal("Their Offer", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
            return;
        if (const game::DiplomaticMessage* m = replyableMessage(ui.state(), ui.session.player(), target_); m && offerView(m) != OfferView::None) {
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
            confirm_ = false;
            // -nd (spec 05 §9.2): no box, and the question counts as No.
            if (noDialogs()) return skippedBox("Are you sure?", "No");
            ImGui::OpenPopup("Are you sure?");
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
    game::MessageId answering_;      // opened from the Log's Send Reply
    game::MessageId portraitReply_;  // opened from the portrait: the newest message, which the reply types answer
    game::MessageId replyTo_;
    bool started_ = false;
    Mode mode_ = Mode::Compose;
    game::DiplomaticMessage draft_;
    std::string autoText_;           // the text we filled in, until the player edits it
    uint64_t pick_ = 0;              // which line of the speech pool
    std::minstd_rand textRng_{std::random_device{}()};   // the window's own, never GameState::rng
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
