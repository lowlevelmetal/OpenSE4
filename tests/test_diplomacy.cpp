// Diplomacy: messages, treaties, packages, surrender, contact, trade and
// tariffs (docs/spec/05 §3).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/commands.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/score.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

#include <format>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};

TurnContext context(GameState& s) { return turnContext(politicsRules(), s); }

// Every living empire's treaty step, in empire order (spec 05 §8 step 6 of each
// empire's end-of-turn processing).
void treatySteps(TurnContext& ctx) {
    for (size_t i = 0; i < ctx.state.empires.size(); ++i)
        if (ctx.state.empires[i].alive) diplomacy::treatyStep(ctx, EmpireId{i});
}

int tradePct(const GameState& s, EmpireId a, EmpireId b) { return diplomacy::tradePercent(politicsRules(), s, a, b); }

void nextTurn(GameState& s) {
    ++s.turn;
    for (Empire& e : s.empires)
        for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
}

MessageId send(GameState& s, EmpireId from, EmpireId to, MessageType type, Treaty treaty = Treaty::None,
               std::vector<PackageItem> offer = {}, std::vector<PackageItem> request = {}) {
    DiplomaticMessage m;
    m.to = to;
    m.type = type;
    m.treaty = treaty;
    m.offer = std::move(offer);
    m.request = std::move(request);
    const CommandResult res = apply(politicsRules(), s, from, cmd::SendMessage{m});
    REQUIRE_MESSAGE(res.ok, res.error);
    return s.messages.back().id;
}

// A message the player's window would not send (a break treaty at war, a
// surrender with the option off), as a minister or computer player may.
MessageId sendUnchecked(GameState& s, EmpireId from, EmpireId to, MessageType type) {
    DiplomaticMessage m;
    m.to = to;
    m.type = type;
    const CommandResult res = apply(politicsRules(), s, from, cmd::SendMessage{m, true});
    REQUIRE_MESSAGE(res.ok, res.error);
    return s.messages.back().id;
}

MessageId answer(GameState& s, EmpireId who, MessageId id, bool accept) {
    const CommandResult res = apply(politicsRules(), s, who, cmd::AnswerMessage{id, accept, {}});
    REQUIRE_MESSAGE(res.ok, res.error);
    return s.messages.back().id;
}

const DiplomaticMessage* message(const GameState& s, MessageId id) {
    for (const auto& m : s.messages)
        if (m.id == id) return &m;
    return nullptr;
}

// The log entry a delivered message made (spec 06 §7 Q42): titled "Message".
const LogEntry* messageEntry(const GameState& s, EmpireId e, MessageId id) {
    for (const LogEntry& l : s.empire(e).log)
        if (l.message == id) return &l;
    return nullptr;
}

PackageItem resources(int64_t m, int64_t o, int64_t r) {
    PackageItem p;
    p.kind = PackageItem::Kind::Resources;
    p.resources = {m, o, r};
    return p;
}

PackageItem tech(ruleset::TechAreaId a) {
    PackageItem p;
    p.kind = PackageItem::Kind::Technology;
    p.tech = a;
    return p;
}

PackageItem treatyItem(Treaty t) {
    PackageItem p;
    p.kind = PackageItem::Kind::Treaty;
    p.treaty = t;
    return p;
}

// Proposes, delivers, answers and delivers again: the full two-turn round trip.
void agree(GameState& s, EmpireId from, EmpireId to, Treaty t) {
    TurnContext ctx = context(s);
    const MessageId id = send(s, from, to, MessageType::ProposeTreaty, t);
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, to, id, true);
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
}

} // namespace

TEST_CASE("diplomacy: messages arrive next turn; a treaty takes effect on acceptance") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    const MessageId id = send(s, kA, kB, MessageType::ProposeTreaty, Treaty::TradeAlliance);
    CHECK_FALSE(message(s, id)->delivered);
    CHECK_FALSE(apply(politicsRules(), s, kB, cmd::AnswerMessage{id, true, {}}).ok);  // not arrived yet

    diplomacy::deliverMessages(ctx);
    CHECK(message(s, id)->delivered);
    CHECK_FALSE(message(s, id)->answered);
    REQUIRE(messageEntry(s, kB, id));
    CHECK(messageEntry(s, kB, id)->title == "Message");
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::None);

    nextTurn(s);
    answer(s, kB, id, true);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::None);  // the reply is still in transit
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::TradeAlliance);
    CHECK(s.empire(kB).relation(kA).treaty == Treaty::TradeAlliance);
    CHECK(s.empire(kA).relation(kB).treatyTurn == 1);
    CHECK(message(s, id)->answered);
    CHECK(hasMood(ctx, kA, "New Treaty Trade"));
    CHECK(hasMood(ctx, kB, "New Treaty Trade"));
    // An accepted proposal makes no treaty entry: its "Message" is all (spec 06 §7 Q70).
    CHECK_FALSE(hasLog(s, kA, "New Treaty"));
    CHECK_FALSE(hasLog(s, kB, "New Treaty"));
    CHECK(hasLog(s, kA, "Message"));

    // Trade starts at 1 % after the signing turn and grows to the maximum.
    CHECK(tradePct(s, kA, kB) == 0);
    treatySteps(ctx);
    CHECK(tradePct(s, kA, kB) == 1);
    for (int i = 0; i < 30; ++i) treatySteps(ctx);
    CHECK(tradePct(s, kA, kB) == 20);
    CHECK(tradePct(s, kB, kA) == 20);
    CHECK(s.empire(kA).relation(kB).tradeTurns == 31);  // the counter itself is not capped
}

TEST_CASE("diplomacy: a delivered message is an ordinary Politics entry whose Goto opens Empires") {
    // Spec 06 §4.1, §7 Q41-Q42 (confirmed: binary): "Message", category
    // Politics, naming the sender and quoting the message; made when it
    // arrives, among the other entries in the order they were made.
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    DiplomaticMessage m;
    m.to = kB;
    m.type = MessageType::General;
    m.text = "Greetings";
    REQUIRE(apply(politicsRules(), s, kA, cmd::SendMessage{m}).ok);
    const MessageId id = s.messages.back().id;
    addLog(s, kB, LogCategory::Misc, "Before");
    diplomacy::deliverMessages(ctx);
    addLog(s, kB, LogCategory::Misc, "After");
    const auto& log = s.empire(kB).log;
    REQUIRE(log.size() >= 3);
    const LogEntry* entry = messageEntry(s, kB, id);
    REQUIRE(entry);
    CHECK(entry->title == "Message");
    CHECK(entry->category == LogCategory::Politics);
    CHECK(entry->target == LogGoto::Empires);
    CHECK(entry->text.find(s.empire(kA).name) != std::string::npos);
    CHECK(entry->text.find("\"Greetings\"") != std::string::npos);
    CHECK(log[log.size() - 3].title == "Before");
    CHECK(&log[log.size() - 2] == entry);
    CHECK(log.back().title == "After");
    // A plain entry's Goto: its location; a Research entry's: the Research window.
    CHECK(log.back().target == LogGoto::Location);
    CHECK(addLog(s, kB, LogCategory::Research, "New Tech Level")->target == LogGoto::Research);
}

TEST_CASE("diplomacy: Goto targets of treaties, trades and contact") {
    // Spec 06 §7 Q41: treaties enacted or lost open Empires; technology
    // received opens Research; resources received open Empires.
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    const MessageId pact = send(s, kA, kB, MessageType::Gift, Treaty::None, {treatyItem(Treaty::TradeAlliance)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, pact, true);
    diplomacy::deliverMessages(ctx);
    const LogEntry* treaty = findLog(s, kA, "Treaty Enacted");
    REQUIRE(treaty);
    CHECK(treaty->target == LogGoto::Empires);
    nextTurn(s);

    const auto beams = techArea(r, "Test Beams");
    s.empire(kA).techLevels[beams.index()] = 4;
    s.empire(kA).stockpile = {5000, 5000, 5000};
    const MessageId gift = send(s, kA, kB, MessageType::Gift, Treaty::None, {tech(beams)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, gift, true);
    diplomacy::deliverMessages(ctx);
    const LogEntry* techGift = findLog(s, kB, "Technology Received");
    REQUIRE(techGift);
    CHECK(techGift->target == LogGoto::Research);
    REQUIRE(findLog(s, kA, "Technology Transfered"));
    CHECK(findLog(s, kA, "Technology Transfered")->target == LogGoto::Research);
    nextTurn(s);
    const MessageId cash = send(s, kA, kB, MessageType::Gift, Treaty::None, {resources(10, 0, 0)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, cash, true);
    diplomacy::deliverMessages(ctx);
    const LogEntry* last = nullptr;
    for (const LogEntry& l : s.empire(kB).log)
        if (l.title == "Resources Received") last = &l;
    REQUIRE(last);
    CHECK(last->target == LogGoto::Empires);

    diplomacy::declareWar(ctx, kA, kB);
    REQUIRE(findLog(s, kB, "War Declared"));
    CHECK(findLog(s, kB, "War Declared")->target == LogGoto::Empires);
}

TEST_CASE("diplomacy: refusals, counter-proposals and forged acceptances") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);

    MessageId id = send(s, kA, kB, MessageType::ProposeTreaty, Treaty::Partnership);
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, false);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::None);
    CHECK(hasLog(s, kA, "Message"));

    // B counters A's proposal; A accepts the counter.
    nextTurn(s);
    id = send(s, kA, kB, MessageType::ProposeTreaty, Treaty::Partnership);
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    DiplomaticMessage counter;
    counter.to = kA;
    counter.type = MessageType::CounterTreaty;
    counter.treaty = Treaty::NonAggression;
    counter.inReplyTo = id;
    REQUIRE(apply(politicsRules(), s, kB, cmd::SendMessage{counter}).ok);
    const MessageId counterId = s.messages.back().id;
    diplomacy::deliverMessages(ctx);
    CHECK(message(s, id)->answered);
    nextTurn(s);
    answer(s, kA, counterId, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::NonAggression);

    // An Accept Treaty enacts the treaty it names, proposed or not (spec 05
    // §3.4, confirmed: binary); here a minister's, which no window checks.
    nextTurn(s);
    DiplomaticMessage forged;
    forged.to = kA;
    forged.type = MessageType::AcceptTreaty;
    forged.treaty = Treaty::Partnership;
    forged.inReplyTo = MessageId{12345u};
    REQUIRE(apply(politicsRules(), s, kB, cmd::SendMessage{forged, true}).ok);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::Partnership);
}

TEST_CASE("diplomacy: only the newest recent message from an empire can be answered (spec 05 §3.4)") {
    GameState s = newPoliticsGame();
    s.options.simultaneous = false;  // turn-based: the window's one-turn case
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    TurnContext ctx = context(s);
    // C (a higher number than B) proposes, then sends a General message: only
    // the newest counts, whatever its type.
    const MessageId proposal = send(s, kC, kB, MessageType::ProposeTreaty, Treaty::NonAggression);
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    const MessageId chat = send(s, kC, kB, MessageType::General);
    diplomacy::deliverMessages(ctx);
    CHECK(diplomacy::newestMessage(s, kB, kC, s.turn)->id == chat);
    CommandResult res = apply(politicsRules(), s, kB, cmd::AnswerMessage{proposal, true, {}});
    CHECK_FALSE(res.ok);
    CHECK(res.error == "Only the newest recent message from that empire can be answered");
    // A counter-proposal needs a recent proposal as the newest message too.
    DiplomaticMessage counter;
    counter.to = kC;
    counter.type = MessageType::CounterTreaty;
    counter.treaty = Treaty::NonAggression;
    CHECK_FALSE(apply(politicsRules(), s, kB, cmd::SendMessage{counter}).ok);

    // A (a lower number than B) proposes: in a turn-based game its message is
    // recent only on the date it was sent (dated after the date − 1).
    nextTurn(s);
    const MessageId fromA = send(s, kA, kB, MessageType::ProposeTreaty, Treaty::NonAggression);
    diplomacy::deliverMessages(ctx);
    CHECK(diplomacy::newestMessage(s, kB, kA, s.turn)->id == fromA);
    nextTurn(s);
    CHECK(diplomacy::newestMessage(s, kB, kA, s.turn) == nullptr);
    CHECK_FALSE(apply(politicsRules(), s, kB, cmd::AnswerMessage{fromA, true, {}}).ok);
    // From a higher number, two dates: C's chat of two turns ago is gone too.
    CHECK(diplomacy::newestMessage(s, kB, kC, s.turn) == nullptr);
    // Simultaneous games have no one-turn case.
    s.options.simultaneous = true;
    CHECK(diplomacy::newestMessage(s, kB, kA, s.turn)->id == fromA);
    // An answer counts as the turn's message to that empire.
    REQUIRE(apply(politicsRules(), s, kB, cmd::AnswerMessage{fromA, false, {}}).ok);
    CHECK(s.empire(kB).relation(kA).messageSentThisTurn);
    CHECK(apply(politicsRules(), s, kB, cmd::AnswerMessage{fromA, true, {}}).error == "Only one message per empire per turn");
}

TEST_CASE("diplomacy: an accepted package is the proposer's newest offer, looked up by the processing's date test (spec 05 §3.4)") {
    GameState s = newPoliticsGame();   // simultaneous
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    s.empire(kA).stockpile = {0, 0, 0};
    s.empire(kB).stockpile = {9000, 0, 0};
    auto turnTo = [&](uint32_t t) {
        s.turn = t;
        for (Empire& e : s.empires)
            for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
    };
    // A player's gift, from its orders of turn 4: the host's processing of
    // turn 4 dates it 4. A accepts in its orders of turn 5; the processing of
    // the players' messages looks for an offer dated exactly 4: carried out.
    turnTo(4);
    MessageId gift = send(s, kB, kA, MessageType::Gift, Treaty::None, {resources(1000, 0, 0)});
    diplomacy::deliverMessages(ctx, std::nullopt, diplomacy::MessageLookup::PreviousTurn);
    turnTo(5);
    answer(s, kA, gift, true);
    diplomacy::deliverMessages(ctx, std::nullopt, diplomacy::MessageLookup::PreviousTurn);
    CHECK(s.empire(kA).stockpile[Resource::Minerals] == 1000);

    // A computer player's gift, sent at the start-of-turn step of turn 5's
    // processing: dated with the advanced date, 6. Accepted in the orders of
    // turn 6, the lookup for an offer dated exactly 5 finds nothing (medium
    // confidence); accepted again in the orders of turn 7 it is found.
    gift = sendUnchecked(s, kB, kA, MessageType::Gift);
    for (DiplomaticMessage& m : s.messages)
        if (m.id == gift) m.offer = {resources(1000, 0, 0)};
    diplomacy::deliverMessages(ctx, 6, diplomacy::MessageLookup::Exact);
    turnTo(6);
    answer(s, kA, gift, true);
    diplomacy::deliverMessages(ctx, std::nullopt, diplomacy::MessageLookup::PreviousTurn);
    CHECK(s.empire(kA).stockpile[Resource::Minerals] == 1000);
    turnTo(7);
    answer(s, kA, gift, true);
    diplomacy::deliverMessages(ctx, std::nullopt, diplomacy::MessageLookup::PreviousTurn);
    CHECK(s.empire(kA).stockpile[Resource::Minerals] == 2000);

    // A computer player accepting at the start-of-turn step (the advanced
    // date 9, exact mode): an offer dated exactly 9 − 2 = 7, a player's from
    // its orders of turn 7.
    turnTo(7);
    gift = send(s, kA, kB, MessageType::Gift, Treaty::None, {resources(500, 0, 0)});
    diplomacy::deliverMessages(ctx, std::nullopt, diplomacy::MessageLookup::PreviousTurn);
    turnTo(8);
    REQUIRE(apply(politicsRules(), s, kB, cmd::AnswerMessage{gift, true, {}, true}).ok);
    diplomacy::deliverMessages(ctx, 9, diplomacy::MessageLookup::Exact);
    CHECK(s.empire(kA).stockpile[Resource::Minerals] == 1500);

    // The packages carried out are the newest offer's, whatever the answer
    // copied; and a newer message of another kind leaves nothing to carry out.
    turnTo(10);
    const MessageId small = send(s, kB, kA, MessageType::Gift, Treaty::None, {resources(100, 0, 0)});
    diplomacy::deliverMessages(ctx, std::nullopt, diplomacy::MessageLookup::Window);
    turnTo(11);
    answer(s, kA, small, true);
    for (DiplomaticMessage& m : s.messages)
        if (!m.delivered) m.request = {resources(7000, 0, 0)};  // the reply's copy: ignored
    diplomacy::deliverMessages(ctx, std::nullopt, diplomacy::MessageLookup::Window);
    CHECK(s.empire(kA).stockpile[Resource::Minerals] == 1600);
}

TEST_CASE("diplomacy: break treaty and declare war are immediate") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    agree(s, kA, kB, Treaty::MilitaryAlliance);
    REQUIRE(s.empire(kA).relation(kB).treaty == Treaty::MilitaryAlliance);
    TurnContext ctx = context(s);

    send(s, kA, kB, MessageType::BreakTreaty);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kB).relation(kA).treaty == Treaty::None);
    // Only an accepted treaty raises a happiness event (spec 05 §3.2, confirmed: binary).
    CHECK_FALSE(hasMood(ctx, kA, "New Treaty None"));
    CHECK_FALSE(hasMood(ctx, kB, "New Treaty None"));

    nextTurn(s);
    send(s, kB, kA, MessageType::DeclareWar);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::War);
    CHECK(s.empire(kA).relation(kB).lastWarTurn == static_cast<int32_t>(s.turn));
    CHECK_FALSE(hasMood(ctx, kA, "New Treaty War"));
    CHECK_FALSE(hasMood(ctx, kB, "New Treaty War"));
    CHECK(hasLog(s, kA, "War Declared"));

    // The player's window offers Break Treaty only from Non-Aggression up
    // (spec 06 §4.2), but a Break Treaty processed at War ends the war: both
    // sides go to None (spec 05 §3.2, confirmed: binary).
    nextTurn(s);
    DiplomaticMessage breakWar;
    breakWar.to = kA;
    breakWar.type = MessageType::BreakTreaty;
    CHECK(apply(politicsRules(), s, kB, cmd::SendMessage{breakWar}).error == "There is no treaty to break");
    sendUnchecked(s, kB, kA, MessageType::BreakTreaty);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::None);
    CHECK(s.empire(kB).relation(kA).treaty == Treaty::None);
}

TEST_CASE("diplomacy: subjugation breaks the subject's other treaties") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    setContact(s, kA, kC);
    agree(s, kB, kC, Treaty::TradeAlliance);
    TurnContext ctx = context(s);
    const MessageId id = send(s, kA, kB, MessageType::ProposeTreaty, Treaty::Subjugation);
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::Subjugation);
    CHECK(s.empire(kA).relation(kB).dominant);  // the proposer is the master
    CHECK_FALSE(s.empire(kB).relation(kA).dominant);
    CHECK(diplomacy::masterOf(s, kB) == kA);
    CHECK_FALSE(diplomacy::masterOf(s, kA).valid());
    CHECK(s.empire(kB).relation(kC).treaty == Treaty::None);
    CHECK(hasLog(s, kC, "Treaty Broken"));
    CHECK(hasMood(ctx, kA, "New Treaty Subjugated (Dom)"));
    CHECK(hasMood(ctx, kB, "New Treaty Subjugated (Sub)"));
    // The dropped treaty raises no happiness event (spec 05 §3.2, confirmed: binary).
    CHECK_FALSE(hasMood(ctx, kC, "New Treaty None"));

    // A proposal may name the dominant side.
    nextTurn(s);
    DiplomaticMessage offer;
    offer.to = kA;
    offer.type = MessageType::ProposeTreaty;
    offer.treaty = Treaty::Protectorate;
    offer.thirdEmpire = kA;
    REQUIRE(apply(politicsRules(), s, kC, cmd::SendMessage{offer}).ok);
    const MessageId pid = s.messages.back().id;
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kA, pid, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kC).treaty == Treaty::Protectorate);
    CHECK(s.empire(kA).relation(kC).dominant);
    CHECK(diplomacy::masterOf(s, kC) == kA);

    // A subjugated race's player can neither propose nor accept a treaty, not
    // even with its master; Break Treaty and Declare War stay (spec 05 §3.2,
    // confirmed: binary). A protectorate is not subjugated.
    CHECK(diplomacy::isSubjugated(s, kB));
    CHECK_FALSE(diplomacy::isSubjugated(s, kC));
    CHECK_FALSE(diplomacy::isSubjugated(s, kA));
    nextTurn(s);
    DiplomaticMessage propose;
    propose.to = kC;
    propose.type = MessageType::ProposeTreaty;
    propose.treaty = Treaty::NonAggression;
    CommandResult res = apply(politicsRules(), s, kB, cmd::SendMessage{propose});
    CHECK_FALSE(res.ok);
    CHECK(res.error == "Subjugated races cannot propose treaties");
    propose.to = kA;
    CHECK_FALSE(apply(politicsRules(), s, kB, cmd::SendMessage{propose}).ok);
    // A minister's (or a computer player's) message is not checked, as in the original.
    CHECK(apply(politicsRules(), s, kB, cmd::SendMessage{propose, true}).ok);
    // Nor can it accept one.
    const MessageId toSubject = send(s, kC, kB, MessageType::ProposeTreaty, Treaty::TradeAlliance);
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    res = apply(politicsRules(), s, kB, cmd::AnswerMessage{toSubject, true, {}});
    CHECK_FALSE(res.ok);
    CHECK(res.error == "Subjugated races cannot accept treaties");
    CHECK(apply(politicsRules(), s, kB, cmd::AnswerMessage{toSubject, false, {}}).ok);   // refusing is fine
    // Nothing in the engine stops a treaty itself: a package's treaty item still works.
    diplomacy::setTreaty(ctx, kC, kB, Treaty::MilitaryAlliance);
    CHECK(s.empire(kC).relation(kB).treaty == Treaty::MilitaryAlliance);
    diplomacy::setTreaty(ctx, kC, kB, Treaty::None);

    // Third-party treaties are visible to the empires we hold Non-Aggression or
    // better with (spec 05 §3.2, confirmed: binary).
    CHECK(diplomacy::treatyVisible(s, kC, kA, kC));
    CHECK(diplomacy::treatyVisible(s, kC, kA, kB));   // C's Protectorate with A is above Non-Aggression
    diplomacy::setTreaty(ctx, kC, kA, Treaty::None);
    CHECK_FALSE(diplomacy::treatyVisible(s, kC, kA, kB));
    diplomacy::setTreaty(ctx, kC, kA, Treaty::NonAggression);
    CHECK(diplomacy::treatyVisible(s, kC, kA, kB));  // C holds Non-Aggression with A

    CHECK(diplomacy::treatyTrigger(Treaty::Protectorate, false) == "New Treaty Protectorate (Sub)");
    CHECK(diplomacy::treatyTrigger(Treaty::NonIntercourse, false) == "New Treaty Non Intercourse");
    CHECK(diplomacy::treatyTrigger(Treaty::TradeResearchAlliance, false) == "New Treaty Trade and Research");
}

TEST_CASE("diplomacy: a package's Subjugation keeps the subject's other treaties and raises the treaty events (spec 05 §3.2)") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    setContact(s, kA, kC);
    agree(s, kB, kC, Treaty::TradeAlliance);
    TurnContext ctx = context(s);
    // B gives A a Subjugation: A, the receiver, is the master; B keeps its
    // Trade Alliance with C.
    diplomacy::executePackage(ctx, kB, kA, std::vector<PackageItem>{treatyItem(Treaty::Subjugation)});
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::Subjugation);
    CHECK(diplomacy::masterOf(s, kB) == kA);
    CHECK(s.empire(kB).relation(kC).treaty == Treaty::TradeAlliance);
    CHECK_FALSE(hasLog(s, kC, "Treaty Broken"));
    CHECK(hasMood(ctx, kA, "New Treaty Subjugated (Dom)"));
    CHECK(hasMood(ctx, kB, "New Treaty Subjugated (Sub)"));
    // War through a package's Treaty tab is the one way to "New Treaty War".
    diplomacy::executePackage(ctx, kC, kA, std::vector<PackageItem>{treatyItem(Treaty::War)});
    CHECK(s.empire(kA).relation(kC).treaty == Treaty::War);
    CHECK(hasMood(ctx, kA, "New Treaty War"));
    CHECK(hasMood(ctx, kC, "New Treaty War"));
}

TEST_CASE("diplomacy: trade percentage across treaty changes") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    // The counter grows toward every other living empire, whatever the treaty.
    treatySteps(ctx);
    CHECK(s.empire(kA).relation(kC).tradeTurns == 1);
    CHECK(tradePct(s, kA, kC) == 0);  // no trade treaty
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeAlliance);  // from None: the counter restarts
    CHECK(s.empire(kA).relation(kB).tradeTurns == 0);
    for (int i = 0; i < 5; ++i) treatySteps(ctx);
    CHECK(tradePct(s, kA, kB) == 5);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Partnership);  // both trade level: kept
    CHECK(tradePct(s, kA, kB) == 5);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::NonAggression);  // below trade level: reset
    CHECK(tradePct(s, kA, kB) == 0);
    treatySteps(ctx);
    CHECK(s.empire(kA).relation(kB).tradeTurns == 1);
    CHECK(tradePct(s, kA, kB) == 0);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeResearchAlliance);
    treatySteps(ctx);
    CHECK(tradePct(s, kB, kA) == 1);

    // Setting the same treaty again changes nothing and logs nothing.
    const size_t logs = s.empire(kA).log.size();
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeResearchAlliance);
    CHECK(s.empire(kA).log.size() == logs);
}

TEST_CASE("diplomacy: trade income, research and intelligence trade, tariffs") {
    const Rules& r = politicsRules();
    // trunc(round(base × pct / 100) × F / 100) (spec 05 §3.3).
    CHECK(diplomacy::tradeShare(10000, 10, 100) == 1000);
    CHECK(diplomacy::tradeShare(10000, 10, 130) == 1300);
    CHECK(diplomacy::tradeShare(1234, 7, 130) == 111);   // round(86.38) = 86; trunc(111.8)
    CHECK(diplomacy::tradeShare(50, 5, 100) == 2);       // round(2.5) goes to the even 2
    CHECK(diplomacy::tradeShare(70, 5, 100) == 4);       // round(3.5) goes to the even 4
    CHECK(diplomacy::tradeShare(10000, 1, 53) == 52);    // trunc(100 × 53 %) in extended precision
    CHECK(diplomacy::tradeShare(10000, 20, -10) == 0);
    CHECK(diplomacy::tradeShare(-5, 20, 100) == 0);
    CHECK(diplomacy::tradeShare(10000, 0, 100) == 0);

    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeAlliance);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Subjugation, true);
    for (int i = 0; i < 7; ++i) treatySteps(ctx);
    s.empire(kA).race.characteristics[static_cast<size_t>(Characteristic::PoliticalSavvy)] = 130;

    // F adds Political Savvy, the race's Trade traits and the culture's Trade value.
    const int64_t f = diplomacy::tradeFactor(r, s.empire(kA));
    const ruleset::Culture* culture = r.culture(s.empire(kA).race);
    CHECK(f == 130 + (culture ? culture->trade : 0));

    // Whatever the economy produces, the income follows the formula.
    const auto genB = diplomacy::generated(r, s, kB);
    const auto genC = diplomacy::generated(r, s, kC);
    REQUIRE(genC.research > 0);
    // Tariffs: round(income × 40 %) on each of the five non-trade incomes.
    const economy::Production incomeC = economy::nonTradeIncome(r, s, kC);
    Resources tariff;
    for (Resource res : kResources) tariff[res] = xmath::pctRound(incomeC.resources[res], 40);
    CHECK(diplomacy::tariffsPaid(r, s, kC) == tariff);
    CHECK(diplomacy::tariffDue(r, s, kC).research == xmath::pctRound(incomeC.research, 40));
    CHECK(diplomacy::tariffDue(r, s, kC).intelligence == xmath::pctRound(incomeC.intelligence, 40));
    CHECK(diplomacy::tariffsPaid(r, s, kA) == Resources{});
    CHECK(diplomacy::tariffsReceived(r, s, kA) == tariff);
    Resources expected = tariff;
    for (Resource res : kResources) expected[res] += diplomacy::tradeShare(genB.resources[res], 7, f);
    CHECK(diplomacy::tradeIncome(r, s, kA) == expected);
    CHECK(diplomacy::researchTradeIncome(r, s, kA) == 0);  // Trade Alliance trades no research
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Partnership);
    CHECK(diplomacy::researchTradeIncome(r, s, kA) == diplomacy::tradeShare(genB.research, 7, f));
    CHECK(diplomacy::intelTradeIncome(r, s, kA) == diplomacy::tradeShare(genB.intelligence, 7, f));
    CHECK(diplomacy::intelTradeIncome(r, s, kC) == 0);
}

TEST_CASE("diplomacy: tariffs take the income floor and Generate Points too, never trade") {
    // Spec 05 §3.3: the tariff base is the whole non-trade income of each kind.
    ruleset::Ruleset rs = buildPoliticsRuleset();
    ruleset::Facility mint;
    mint.name = "Test Mint";
    mint.group = "Test";
    mint.family = 9000;
    mint.romanNumeral = 1;
    for (auto [kind, value] : {std::pair{AbilityKind::GeneratePointsMinerals, 1000}, std::pair{AbilityKind::GeneratePointsResearch, 300}}) {
        ruleset::Ability a;
        a.type = std::string(identifier(kind));
        a.value1 = std::to_string(value);
        mint.abilities.push_back(a);
    }
    rs.facilities.push_back(mint);
    const Rules r{std::move(rs)};
    GameState s = newPoliticsGame();
    setContact(s, kA, kC);
    setContact(s, kB, kC);
    TurnContext ctx = turnContext(r, s);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Protectorate, true);  // a protectorate may trade with others
    diplomacy::setTreaty(ctx, kB, kC, Treaty::TradeAlliance);
    for (int i = 0; i < 5; ++i) diplomacy::treatyStep(ctx, kC);
    Colony& home = homeworld(s, kC);
    home.facilities.push_back(static_cast<uint32_t>(r.data().facilities.size() - 1));
    const auto made = diplomacy::generated(r, s, kC);
    const economy::Production income = economy::nonTradeIncome(r, s, kC);
    CHECK(income.resources[Resource::Minerals] == made.resources[Resource::Minerals] + 1000);
    CHECK(income.research == made.research + 300);
    const auto due = diplomacy::tariffDue(r, s, kC);
    CHECK(due.resources[Resource::Minerals] == xmath::pctRound(made.resources[Resource::Minerals] + 1000, 20));
    CHECK(due.research == xmath::pctRound(made.research + 300, 20));
    // Trade income comes later and is never taxed: the master gets the tariff
    // alone at the subject's income step.
    REQUIRE(diplomacy::tradePercent(r, s, kC, kB) > 0);
    const Resources masterBefore = s.empire(kA).stockpile;
    economy::collectIncome(ctx, kC);
    CHECK(s.empire(kA).stockpile == masterBefore + due.resources);
}

TEST_CASE("diplomacy: a traded ship teaches its design and its cargo's") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    VehicleId scout;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kA) scout = v.id;
    const DesignId sat = addTestDesign(s, r, kA, "Cargo Sat", "Test Satellite Hull", {"Test Armor Plate"});
    s.vehicle(scout)->cargo.units = {{sat, 1}};
    PackageItem ship;
    ship.kind = PackageItem::Kind::Vehicle;
    ship.vehicle = scout;
    s.turn = 7;
    diplomacy::executePackage(ctx, kA, kB, std::vector<PackageItem>{ship});
    CHECK(designSeenTurn(s.empire(kB).knowledge, s.vehicle(scout)->design) == std::optional<uint32_t>(7));
    CHECK(designSeenTurn(s.empire(kB).knowledge, sat) == std::optional<uint32_t>(7));
}

TEST_CASE("diplomacy: trades move resources, technology, ships, charts and contacts") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    const auto beams = techArea(r, "Test Beams");
    s.empire(kA).techLevels[beams.index()] = 4;
    const int beamsB = s.empire(kB).techLevel(beams);
    s.empire(kA).stockpile = {5000, 5000, 5000};
    s.empire(kB).stockpile = {5000, 5000, 5000};
    VehicleId scout;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kA) scout = v.id;
    const SystemId homeA = s.galaxy.object(homeworld(s, kA).planet).system;

    PackageItem ship;
    ship.kind = PackageItem::Kind::Vehicle;
    ship.vehicle = scout;
    PackageItem chart;
    chart.kind = PackageItem::Kind::StarChart;
    chart.system = homeA;
    PackageItem comm;
    comm.kind = PackageItem::Kind::CommChannel;
    comm.empire = kC;
    const MessageId id = send(s, kA, kB, MessageType::ProposeTrade, Treaty::None, {resources(1000, 0, 0), tech(beams), ship, chart, comm},
                              {resources(0, 500, 0)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).stockpile == Resources{4000, 5500, 5000});
    CHECK(s.empire(kB).stockpile == Resources{6000, 4500, 5000});
    // A technology item gives exactly one level (spec 05 §1.5, confirmed: binary).
    CHECK(s.empire(kB).techLevel(beams) == beamsB + 1);
    CHECK(s.vehicle(scout)->owner == kB);
    CHECK(s.empire(kB).hasExplored(homeA));
    CHECK(diplomacy::inContact(s, kB, kC));
    // Comm channels set both sides to None without a first-contact entry (spec 06 §7 Q70).
    CHECK(s.empire(kB).relation(kC).treaty == Treaty::None);
    CHECK_FALSE(hasLog(s, kB, "First Contact"));
    CHECK(hasLog(s, kB, "Comm Channels Received"));
    CHECK(hasLog(s, kC, "Comm Channels Opened"));
    CHECK_FALSE(hasLog(s, kA, "Trade Completed"));
}

TEST_CASE("diplomacy: placeholders, missing items, limits") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    s.empire(kA).stockpile = {300, 0, 0};

    // An "Any" technology cannot be accepted.
    MessageId id = send(s, kA, kB, MessageType::ProposeTrade, Treaty::None, {resources(100, 0, 0)}, {tech({})});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    CHECK(hasLog(s, kA, "Trade Cancelled"));
    CHECK(s.empire(kA).stockpile == Resources{300, 0, 0});

    // Only what the giver holds moves; a vehicle that is gone is reported.
    nextTurn(s);
    PackageItem ghost;
    ghost.kind = PackageItem::Kind::Vehicle;
    ghost.vehicle = VehicleId{9999u};
    id = send(s, kA, kB, MessageType::Gift, Treaty::None, {resources(1000000, 0, 0), ghost});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).stockpile == Resources{0, 0, 0});
    // The vehicle that is gone is skipped without any entry (spec 06 §7 Q70).
    CHECK_FALSE(hasLog(s, kB, "Items Unavailable"));
    CHECK_FALSE(hasLog(s, kB, "Vehicle Received"));
    CHECK(hasLog(s, kB, "Resources Received"));

    // The package code tests no game option: technology passes even with
    // Allow Technology Gifts off (that only limits what a player composes).
    nextTurn(s);
    s.options.allowTechTrades = false;
    const auto armor = techArea(r, "Test Armor");
    s.empire(kA).techLevels[armor.index()] = 5;
    const int armorB = s.empire(kB).techLevel(armor);
    id = send(s, kA, kB, MessageType::Gift, Treaty::None, {tech(armor)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kB).techLevel(armor) == armorB + 1);
    CHECK(diplomacy::isPlaceholder(tech({})));
    CHECK_FALSE(diplomacy::isPlaceholder(resources(1, 2, 3)));
}

TEST_CASE("diplomacy: a technology item gives one level, only in an area the receiver can see (spec 05 §1.5)") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    const auto physics = techArea(r, "Test Physics");
    const auto missiles = techArea(r, "Test Missiles");  // needs Test Physics 2
    const auto armor = techArea(r, "Test Armor");
    s.empire(kA).techLevels[physics.index()] = 3;
    s.empire(kA).techLevels[missiles.index()] = 4;
    s.empire(kA).techLevels[armor.index()] = 6;
    s.empire(kB).techLevels[physics.index()] = 1;
    s.empire(kB).techLevels[missiles.index()] = 0;
    s.empire(kB).techLevels[armor.index()] = 6;
    diplomacy::executePackage(ctx, kA, kB, std::vector<PackageItem>{tech(missiles), tech(armor), tech(physics)});
    // Missiles: B does not meet its requirement, so nothing; armor: B is not
    // behind; physics: one level, not the giver's 3.
    CHECK(s.empire(kB).techLevel(missiles) == 0);
    CHECK(s.empire(kB).techLevel(armor) == 6);
    CHECK(s.empire(kB).techLevel(physics) == 2);
    CHECK(hasLog(s, kB, "New Tech Level"));
    int useless = 0;
    for (const LogEntry& l : s.empire(kB).log)
        if (l.title == "Technology Received" && (l.text.ends_with("cannot make use of it yet.") || l.text.ends_with("no more advanced than ours.")))
            ++useless;
    CHECK(useless == 2);
    // Now that B meets the requirement, Missiles gives one level too.
    diplomacy::executePackage(ctx, kA, kB, std::vector<PackageItem>{tech(missiles)});
    CHECK(s.empire(kB).techLevel(missiles) == 1);
}

TEST_CASE("diplomacy: surrender and independence") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    TurnContext ctx = context(s);
    const auto beams = techArea(r, "Test Beams");
    s.empire(kA).techLevels[beams.index()] = 7;
    s.empire(kA).stockpile = {100, 200, 300};
    const Resources bBefore = s.empire(kB).stockpile;
    const ObjectId homeA = homeworld(s, kA).planet;

    // With Allow Surrender off the message does nothing (spec 05 §3.4).
    s.options.allowSurrender = false;
    DiplomaticMessage giveUp;
    giveUp.to = kB;
    giveUp.type = MessageType::Surrender;
    CHECK(apply(politicsRules(), s, kA, cmd::SendMessage{giveUp}).error == "Surrender is not allowed in this game");
    sendUnchecked(s, kA, kB, MessageType::Surrender);
    diplomacy::deliverMessages(ctx);
    CHECK(s.colony(homeA)->owner == kA);
    CHECK(s.empire(kA).stockpile == Resources{100, 200, 300});
    s.options.allowSurrender = true;
    nextTurn(s);

    // A design A knows whose owner can build it is learned too.
    const DesignId cDesign = s.empire(kC).designs.front();
    seeDesign(s.empire(kA).knowledge, cDesign, 0);
    const DesignId aDesign = s.empire(kA).designs.front();
    const SystemId homeSystemA = s.galaxy.object(homeA).system;
    const SystemId homeSystemC = s.galaxy.object(homeworld(s, kC).planet).system;
    s.empire(kA).knowledge.explored[homeSystemC.index()] = 1;
    s.empire(kA).researchPool = 900;
    // A's ships in a fleet; a plague on A's homeworld, and B a No Plagues race.
    std::vector<VehicleId> shipsA;
    const auto firstA = std::find_if(s.vehicles.begin(), s.vehicles.end(), [](const Vehicle& v) { return v.owner == kA; });
    REQUIRE(firstA != s.vehicles.end());
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kA && v.location == firstA->location) shipsA.push_back(v.id);
    REQUIRE_FALSE(shipsA.empty());
    Fleet fleet;
    fleet.owner = kA;
    fleet.members = shipsA;
    fleet.leader = shipsA.front();
    fleet.location = s.vehicle(shipsA.front())->location;
    const FleetId fleetA = s.addFleet(fleet).id;
    for (VehicleId v : shipsA) s.vehicle(v)->fleet = fleetA;
    s.colony(homeA)->plagueLevel = 2;
    s.empire(kB).race.traits.push_back(traitIndex(r, "Test Machine Folk"));
    send(s, kA, kB, MessageType::Surrender);
    diplomacy::deliverMessages(ctx);
    // Each ship leaves its fleet as it changes owner: B gets them in no fleet
    // (spec 05 §3.4, confirmed: binary); A's fleet is left empty.
    for (VehicleId v : shipsA) CHECK_FALSE(s.vehicle(v)->fleet.valid());
    for (const Fleet& f : s.fleets) CHECK(f.owner != kB);
    // A No Plagues race cures a plagued colony it receives at once.
    CHECK(s.colony(homeA)->plagueLevel == 0);
    // Everything passes, but the empire lives on, owning nothing, until its
    // next destruction check (spec 05 §6).
    CHECK(s.empire(kA).alive);
    CHECK(s.colony(homeA)->owner == kB);
    CHECK(s.colony(homeA)->homeworld);  // only the owner changes: a capital stays one (spec 02 §2, confirmed: binary)
    for (const Vehicle& v : s.vehicles) CHECK(v.owner != kA);
    CHECK(s.empire(kB).stockpile == bBefore + Resources{100, 200, 300});
    CHECK(s.empire(kA).stockpile == Resources{});
    CHECK(s.empire(kA).researchPool == 900);  // research points do not pass
    // Exactly one level where the recipient is behind.
    CHECK(s.empire(kB).techLevel(beams) == 2);
    // The systems of the objects become explored; nothing else of A's map.
    CHECK(s.empire(kB).hasExplored(homeSystemA));
    CHECK_FALSE(s.empire(kB).hasExplored(homeSystemC));
    // Designs: A's own and those A knew, dated now.
    CHECK(designSeenTurn(s.empire(kB).knowledge, aDesign) == std::optional<uint32_t>(s.turn));
    CHECK(designSeenTurn(s.empire(kB).knowledge, cDesign) == std::optional<uint32_t>(s.turn));
    // The empires in contact with either side are told.
    CHECK(hasLog(s, kA, "Surrender"));
    CHECK(hasLog(s, kB, "Surrender"));
    CHECK(hasLog(s, kC, "Surrender"));
    score::checkDestruction(ctx, kA);
    CHECK_FALSE(s.empire(kA).alive);
    CHECK_FALSE(s.empire(kB).relation(kA).contact);

    // Grant independence: the sender abandons one of its planets.
    nextTurn(s);
    DiplomaticMessage m;
    m.to = kC;
    m.type = MessageType::GrantIndependence;
    m.planet = homeA;
    REQUIRE(apply(r, s, kB, cmd::SendMessage{m}).ok);
    diplomacy::deliverMessages(ctx);
    CHECK(s.colony(homeA) == nullptr);
    CHECK(hasLog(s, kC, "Independence Granted"));
}

TEST_CASE("diplomacy: first contact runs in one system, needs mutual detection there and a warp path") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    TurnContext ctx = context(s);
    const SystemId homeB = s.galaxy.object(homeworld(s, kB).planet).system;
    const SystemId homeC = s.galaxy.object(homeworld(s, kC).planet).system;
    VehicleId shipOfA;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kA) shipOfA = v.id;
    REQUIRE(diplomacy::warpLinked(s, kA, kB));
    REQUIRE(diplomacy::warpLinked(s, kB, kA));
    REQUIRE_FALSE(diplomacy::inContact(s, kA, kB));

    // A's ship sits at B's home: they detect each other there, but the check
    // runs only in the system it is given (spec 05 §3.1).
    s.vehicle(shipOfA)->location = locationOf(s.galaxy, homeworld(s, kB).planet);
    REQUIRE(sight::canSeeVehicle(r, s, kB, *s.vehicle(shipOfA)));
    REQUIRE(sight::canSeeColony(r, s, kA, homeworld(s, kB).planet));
    diplomacy::firstContactIn(ctx, homeC);
    CHECK_FALSE(diplomacy::inContact(s, kA, kB));

    // Both detect each other in B's home system, but no warp path links them: none.
    std::vector<ObjectId> links;
    for (SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::WarpPoint) {
            links.push_back(o.destination);
            o.destination = ObjectId{};
        }
    CHECK_FALSE(diplomacy::warpLinked(s, kA, kB));
    diplomacy::firstContactIn(ctx, homeB);
    CHECK_FALSE(diplomacy::inContact(s, kA, kB));

    // With the warp links back: contact, both ways, in that system only.
    {
        size_t i = 0;
        for (SpaceObject& o : s.galaxy.objects)
            if (o.kind == ObjectKind::WarpPoint) o.destination = links[i++];
    }
    diplomacy::firstContactIn(ctx, homeB);
    CHECK(diplomacy::inContact(s, kA, kB));
    CHECK(diplomacy::inContact(s, kB, kA));
    CHECK_FALSE(diplomacy::inContact(s, kA, kC));
    CHECK(hasLog(s, kA, "First Contact"));
    CHECK(hasLog(s, kB, "First Contact"));

    // C's home holds nothing of A's: the check there finds no pair; the
    // galaxy-wide form (game creation, surrender) checks every system.
    diplomacy::firstContactEverywhere(ctx);
    CHECK_FALSE(diplomacy::inContact(s, kA, kC));
    CHECK_FALSE(diplomacy::inContact(s, kB, kC));

    // Nobody sees anybody any more: the contact check keeps contact while the path exists.
    diplomacy::setTreaty(ctx, kA, kB, Treaty::NonAggression);
    for (Empire& e : s.empires) {
        e.knowledge.visibleVehicles.clear();
        std::fill(e.knowledge.present.begin(), e.knowledge.present.end(), uint8_t{0});
    }
    diplomacy::checkContacts(ctx);
    CHECK(diplomacy::inContact(s, kA, kB));
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::NonAggression);

    // Destruction ends contact too.
    diplomacy::forgetEmpire(s, kB);
    CHECK_FALSE(diplomacy::inContact(s, kA, kB));
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::None);
    (void)r;
}

TEST_CASE("diplomacy: contact is lost when no warp path links the colonies (spec 05 §3.1)") {
    GameState s = newPoliticsGame();
    TurnContext ctx = context(s);
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeAlliance);
    s.empire(kA).relation(kB).anger = 37;
    s.empire(kB).relation(kA).anger = 64;
    IntelProjectOrder spyB;
    spyB.project = 0;
    spyB.target = kB;
    IntelProjectOrder spyC = spyB;
    spyC.target = kC;
    s.empire(kA).intel = {spyB, spyC};
    s.empire(kB).intel = {spyB, spyC};
    s.empire(kB).intel[0].target = kA;

    // A keeps its colonies linked to C's, but every warp point of B's home
    // system leads nowhere, and every warp point toward it too.
    const SystemId homeB = s.galaxy.object(homeworld(s, kB).planet).system;
    for (SpaceObject& o : s.galaxy.objects) {
        if (o.kind != ObjectKind::WarpPoint || !o.destination.valid()) continue;
        if (o.system == homeB || s.galaxy.object(o.destination).system == homeB) o.destination = ObjectId{};
    }
    REQUIRE(diplomacy::warpLinked(s, kA, kC));
    diplomacy::checkContacts(ctx);
    for (auto [x, y] : {std::pair{kA, kB}, std::pair{kB, kA}}) {
        const Relation& rel = s.empire(x).relation(y);
        CHECK_FALSE(rel.contact);
        CHECK(rel.treaty == Treaty::None);
        CHECK(hasLog(s, x, "Contact Lost"));
    }
    // The anger stays; only the projects against the lost empire go.
    CHECK(s.empire(kA).relation(kB).anger == 37);
    CHECK(s.empire(kB).relation(kA).anger == 64);
    REQUIRE(s.empire(kA).intel.size() == 1);
    CHECK(s.empire(kA).intel[0].target == kC);
    REQUIRE(s.empire(kB).intel.size() == 1);
    CHECK(s.empire(kB).intel[0].target == kC);
    CHECK(diplomacy::inContact(s, kA, kC));
    CHECK(std::any_of(s.empire(kA).historyEvents.begin(), s.empire(kA).historyEvents.end(),
                      [](const HistoryEntry& h) { return h.empire == kB && h.text.starts_with("Lost contact"); }));

    // The human's history file gets the line the turn after.
    nextTurn(s);
    const score::PlayerRecords rec = score::playerRecords(politicsRules(), s, kA);
    CHECK(std::any_of(rec.history.begin(), rec.history.end(), [](const std::string& l) { return l.find("Lost contact") != std::string::npos; }));
}

TEST_CASE("diplomacy: an empire without colonies loses every contact at the next check") {
    GameState s = newPoliticsGame();
    TurnContext ctx = context(s);
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    s.colonies[homeworld(s, kB).planet.index()].reset();
    REQUIRE(s.empire(kB).alive);
    diplomacy::checkContacts(ctx);
    CHECK_FALSE(diplomacy::inContact(s, kB, kA));
    CHECK_FALSE(diplomacy::inContact(s, kB, kC));
    CHECK_FALSE(diplomacy::inContact(s, kA, kB));
    CHECK_FALSE(diplomacy::inContact(s, kC, kB));
}

TEST_CASE("diplomacy: the turn runs the contact check after the design cleanup, in both turn styles") {
    for (const bool simultaneous : {true, false}) {
        CAPTURE(simultaneous);
        GameState s = newPoliticsGame();
        s.options.simultaneous = simultaneous;
        setContact(s, kA, kB);
        for (SpaceObject& o : s.galaxy.objects)
            if (o.kind == ObjectKind::WarpPoint) o.destination = ObjectId{};
        const uint32_t turn = s.turn;
        processTurn(politicsRules(), s, {}, {});
        CHECK(s.turn == turn + 1);
        CHECK_FALSE(diplomacy::inContact(s, kA, kB));
        CHECK_FALSE(diplomacy::inContact(s, kB, kA));
        const LogEntry* lost = findLog(s, kA, "Contact Lost");
        REQUIRE(lost != nullptr);
        CHECK(lost->turn == turn);
    }
}

TEST_CASE("diplomacy: the treaty step resets mismatched treaties and declaring war ignores the treaty") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeAlliance);
    s.empire(kB).relation(kA).treaty = Treaty::Partnership;  // the two records disagree
    diplomacy::treatyStep(ctx, kA);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::None);
    CHECK(s.empire(kB).relation(kA).treaty == Treaty::None);

    diplomacy::setTreaty(ctx, kA, kB, Treaty::Partnership);
    diplomacy::declareWar(ctx, kB, kA);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::War);
    CHECK(s.empire(kB).relation(kA).treaty == Treaty::War);
    CHECK(hasLog(s, kA, "War Declared"));
}

TEST_CASE("diplomacy: partnership shares maps and designs; masters see subject designs") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Partnership);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Subjugation, true);
    const SystemId homeB = s.galaxy.object(homeworld(s, kB).planet).system;
    const DesignId cDesign = s.empire(kC).designs.front();
    seeDesign(s.empire(kB).knowledge, cDesign, s.turn);
    const DesignId fresh = addTestDesign(s, r, kC, "Fresh Hull", "Test Frigate", {"Test Bridge"});
    diplomacy::treatyStep(ctx, kA);
    CHECK(s.empire(kA).hasExplored(homeB));
    CHECK(hasLog(s, kA, "New System Maps Available"));
    CHECK(knowsDesign(s.empire(kA).knowledge, cDesign));
    CHECK(knowsDesign(s.empire(kA).knowledge, fresh));
    CHECK_FALSE(s.empire(kC).hasExplored(homeB));
}

TEST_CASE("diplomacy: messages expire and die with their empires") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    const MessageId id = send(s, kA, kB, MessageType::General);
    diplomacy::deliverMessages(ctx);
    for (uint32_t i = 0; i < diplomacy::kMessageLifetime; ++i) {
        nextTurn(s);
        diplomacy::deliverMessages(ctx);
    }
    CHECK(message(s, id) != nullptr);
    nextTurn(s);
    diplomacy::deliverMessages(ctx);
    CHECK(message(s, id) == nullptr);

    send(s, kA, kC, MessageType::General);
    s.empire(kC).alive = false;
    diplomacy::deliverMessages(ctx);
    CHECK(s.messages.empty());
}

TEST_CASE("diplomacy: research, intelligence, messages and events through the turn pipeline are deterministic") {
    const Rules& r = politicsRules();
    auto play = [&] {
        GameState s = newPoliticsGame(44, 3, 14);
        s.options.eventFrequency = 3;
        for (Empire& e : s.empires)
            for (Empire& o : s.empires)
                if (e.id != o.id) e.relation(o.id).contact = true;
        const uint32_t projects = static_cast<uint32_t>(r.data().intelProjects.size());
        std::vector<std::string> trace;
        for (uint32_t t = 0; t < 30 && !s.gameOver; ++t) {
            std::vector<EmpireOrders> orders;
            for (const Empire& e : s.empires) {
                if (!e.alive) continue;
                EmpireOrders o{e.id, s.turn, {}};
                const EmpireId next{(e.id.index() + 1) % s.empires.size()};
                o.commands.push_back(cmd::SetResearch{{{techArea(r, "Test Beams"), 0}, {techArea(r, "Test Armor"), 0}}, t % 2 == 0, true});
                IntelProjectOrder p;
                p.project = (t * 3 + static_cast<uint32_t>(e.id.index())) % projects;
                p.target = next;
                o.commands.push_back(cmd::SetIntel{{p}, true, false});
                DiplomaticMessage m;
                m.to = next;
                m.type = t % 3 == 0 ? MessageType::ProposeTreaty : t % 3 == 1 ? MessageType::Gift : MessageType::General;
                m.treaty = t % 2 ? Treaty::TradeAlliance : Treaty::NonAggression;
                m.offer = {resources(100, 0, 0)};
                o.commands.push_back(cmd::SendMessage{m});
                for (const DiplomaticMessage& msg : s.messages)
                    if (msg.to == e.id && msg.delivered && !msg.answered) o.commands.push_back(cmd::AnswerMessage{msg.id, msg.id.value % 2 == 0, {}});
                orders.push_back(std::move(o));
            }
            for (Empire& e : s.empires) research::addToPools(e, 2500, 1200);
            processTurn(r, s, orders);
            // Each log keeps one turn's entries (spec 05 §3.4): trace every turn.
            for (const Empire& e : s.empires) {
                for (const LogEntry& l : e.log) trace.push_back(std::format("{}:{}:{}", l.turn, l.title, l.text));
                for (const Relation& rel : e.relations) trace.push_back(std::format("{}/{}", static_cast<int>(rel.treaty), rel.tradeTurns));
                trace.push_back(std::format("{} {} {}", e.stockpile[Resource::Minerals], research::totalLevels(r, e), e.history.size()));
            }
        }
        return std::pair{trace, s.rng};
    };
    const auto a = play();
    const auto b = play();
    CHECK(a.first.size() > 100);
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}

TEST_CASE("diplomacy: a player's request about a third empire must name a living empire it has met, not itself or the recipient (spec 05 Q51)") {
    GameState s = newPoliticsGame(5, 4);
    const EmpireId kD{3u};
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    auto request = [&](MessageType type, EmpireId third) {
        DiplomaticMessage m;
        m.to = kB;
        m.type = type;
        m.thirdEmpire = third;
        return apply(politicsRules(), s, kA, cmd::SendMessage{m});
    };
    for (MessageType type : {MessageType::RequestStopHostilities, MessageType::RequestBreakTreaty, MessageType::RequestDeclareWar,
                             MessageType::RequestMakePeace, MessageType::RequestSupport, MessageType::RequestAttackEmpire}) {
        CHECK_FALSE(request(type, EmpireId{}).ok);    // none named
        CHECK_FALSE(request(type, kA).ok);            // the sender
        CHECK_FALSE(request(type, kB).ok);            // the recipient
        CHECK_FALSE(request(type, kD).ok);            // not met
        CHECK_FALSE(request(type, EmpireId{9u}).ok);  // no such empire
    }
    s.empire(kC).alive = false;
    CHECK_FALSE(request(MessageType::RequestBreakTreaty, kC).ok);  // no longer in the game
    s.empire(kC).alive = true;
    CHECK(request(MessageType::RequestBreakTreaty, kC).ok);
    // Other messages do not name one.
    nextTurn(s);
    CHECK(request(MessageType::General, EmpireId{}).ok);
    // A computer player's own messages do not go through the picker.
    nextTurn(s);
    s.empire(kA).kind = PlayerKind::Computer;
    CHECK(request(MessageType::RequestAttackEmpire, kD).ok);
}

TEST_CASE("diplomacy: a vehicle handed over in a package and a surrender run the first-contact check") {
    for (const bool surrender : {false, true}) {
        CAPTURE(surrender);
        GameState s = newPoliticsGame();
        setContact(s, kA, kB);
        TurnContext ctx = context(s);
        // A's scout waits at C's home; nobody has met C.
        VehicleId scout;
        for (const Vehicle& v : s.vehicles)
            if (v.owner == kA) scout = v.id;
        s.vehicle(scout)->location = locationOf(s.galaxy, homeworld(s, kC).planet);
        REQUIRE_FALSE(diplomacy::inContact(s, kB, kC));
        if (surrender) {
            send(s, kA, kB, MessageType::Surrender);
            diplomacy::deliverMessages(ctx);
        } else {
            PackageItem ship;
            ship.kind = PackageItem::Kind::Vehicle;
            ship.vehicle = scout;
            diplomacy::executePackage(ctx, kA, kB, std::vector<PackageItem>{ship});
        }
        REQUIRE(s.vehicle(scout)->owner == kB);
        // B's ship now sits at C's home: they meet there.
        CHECK(diplomacy::inContact(s, kB, kC));
        CHECK(hasLog(s, kC, "First Contact"));
        CHECK_FALSE(diplomacy::inContact(s, kA, kC));
    }
}

TEST_CASE("diplomacy: a completed package logs one entry per item, receiver first, then the acceptance (spec 06 §7 Q70)") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    const auto beams = techArea(r, "Test Beams");
    s.empire(kA).techLevels[beams.index()] = 3;
    s.empire(kA).stockpile = {5000, 5000, 5000};
    s.empire(kB).stockpile = {5000, 5000, 5000};
    const SystemId homeC = s.galaxy.object(homeworld(s, kC).planet).system;
    REQUIRE_FALSE(s.empire(kA).hasExplored(homeC));   // charts need no exploration by the giver
    const SystemId homeB = s.galaxy.object(homeworld(s, kB).planet).system;
    s.empire(kB).claimedSystems = {homeB};
    PackageItem chart;
    chart.kind = PackageItem::Kind::StarChart;
    chart.system = homeC;
    PackageItem claim;
    claim.kind = PackageItem::Kind::System;
    claim.system = homeB;
    PackageItem gone;
    gone.kind = PackageItem::Kind::Vehicle;
    gone.vehicle = VehicleId{9999u};
    const auto linksBefore = s.empire(kB).knowledge.knownWarpLink;
    // A proposes: it gives resources, a vanished ship and charts; it asks
    // for B's technology... B has none ahead, so the data is of no use, and
    // B's claim on its home system.
    const MessageId id = send(s, kA, kB, MessageType::ProposeTrade, Treaty::None, {resources(1000, 0, 0), gone, chart},
                              {tech(beams), claim});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    // A's entries (the proposer): its offered items first, then the requested
    // ones; the acceptance's "Message" comes last.
    std::vector<std::string> titlesA;
    for (const LogEntry& l : s.empire(kA).log)
        if (l.turn == s.turn) titlesA.push_back(l.title);
    CHECK(titlesA == std::vector<std::string>{"Resources Transfered", "Technology Received", "Message"});
    std::vector<std::string> titlesB;
    for (const LogEntry& l : s.empire(kB).log)
        if (l.turn == s.turn) titlesB.push_back(l.title);
    CHECK(titlesB == std::vector<std::string>{"Resources Received", "Starcharts Received", "Technology Transfered"});
    // Technology of no use says so; the claim passed without an entry.
    const LogEntry* useless = findLog(s, kA, "Technology Received");
    REQUIRE(useless);
    CHECK(useless->target == LogGoto::Research);
    CHECK(useless->text.ends_with("It is no more advanced than ours."));
    CHECK(std::binary_search(s.empire(kA).claimedSystems.begin(), s.empire(kA).claimedSystems.end(), homeB));
    CHECK(s.empire(kB).claimedSystems.empty());
    // The charts: explored, no warp links copied; Goto the system, no sector.
    CHECK(s.empire(kB).hasExplored(homeC));
    CHECK(s.empire(kB).knowledge.knownWarpLink == linksBefore);
    const LogEntry* charts = findLog(s, kB, "Starcharts Received");
    REQUIRE(charts);
    REQUIRE(charts->location);
    CHECK(charts->location->system == homeC);
    CHECK_FALSE(charts->location->sector.valid());
    CHECK_FALSE(hasLog(s, kA, "Starcharts Transfered"));
    CHECK(findLog(s, kB, "Resources Received")->target == LogGoto::Empires);
    CHECK(s.empire(kB).stockpile[Resource::Minerals] == 6000);
}

TEST_CASE("diplomacy: a package's planet, vehicle and treaty entries; the treaty writes a contact line in the history") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = context(s);
    // A second colony of A to give, and a ship.
    ObjectId spare;
    const SystemId homeA = s.galaxy.object(homeworld(s, kA).planet).system;
    for (ObjectId o : s.galaxy.system(homeA).objects)
        if (s.galaxy.object(o).kind == ObjectKind::Planet && !s.colony(o)) {
            Colony c;
            c.planet = o;
            c.owner = kA;
            c.population.push_back({kA, 5});
            s.colonies[o.index()] = c;
            spare = o;
            break;
        }
    REQUIRE(spare.valid());
    VehicleId ship;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kA) ship = v.id;
    PackageItem planet;
    planet.kind = PackageItem::Kind::Planet;
    planet.planet = spare;
    PackageItem vehicle;
    vehicle.kind = PackageItem::Kind::Vehicle;
    vehicle.vehicle = ship;
    const MessageId id = send(s, kA, kB, MessageType::Gift, Treaty::None, {planet, vehicle, treatyItem(Treaty::NonAggression)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.colony(spare)->owner == kB);
    CHECK(s.vehicle(ship)->owner == kB);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::NonAggression);
    const LogEntry* got = findLog(s, kB, "Planet Received");
    REQUIRE(got);
    CHECK(got->target == LogGoto::Location);
    CHECK(got->location == locationOf(s.galaxy, spare));
    REQUIRE(findLog(s, kA, "Planet Transfered"));
    REQUIRE(findLog(s, kB, "Vehicle Received"));
    CHECK(findLog(s, kB, "Vehicle Received")->location == s.vehicle(ship)->location);
    REQUIRE(findLog(s, kA, "Vehicle Transfered"));
    for (EmpireId e : {kA, kB}) {
        const LogEntry* enacted = findLog(s, e, "Treaty Enacted");
        REQUIRE(enacted);
        CHECK(enacted->target == LogGoto::Empires);
        CHECK_FALSE(hasLog(s, e, "New Treaty"));
        CHECK_FALSE(hasLog(s, e, "Gift Completed"));
    }
    // The history file's contact line, the turn after.
    nextTurn(s);
    const score::PlayerRecords rec = score::playerRecords(politicsRules(), s, kA);
    CHECK(std::any_of(rec.history.begin(), rec.history.end(),
                      [&](const std::string& l) { return l.find("First contact with the " + effects::empireFullName(s.empire(kB))) != std::string::npos; }));
    // A planet no longer the giver's is skipped without any entry.
    const MessageId again = send(s, kA, kB, MessageType::Gift, Treaty::None, {planet});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, again, true);
    diplomacy::deliverMessages(ctx);
    for (const LogEntry& l : s.empire(kB).log)
        if (l.turn == s.turn) CHECK(l.title != "Planet Received");
}

// What the original's Communicate window lets a player send (spec 06 §4.2,
// spec 05 §3.2, §3.4, confirmed: binary). The original checks it in the
// window only; the engine checks every message a player writes, so a network
// player cannot get round it, and lets the computer's and the ministers'
// messages through as the original does.
TEST_CASE("diplomacy: a player's messages are checked as the original's window checks them") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    auto attempt = [&](MessageType type, Treaty treaty = Treaty::None, std::vector<PackageItem> offer = {}, std::vector<PackageItem> request = {},
                       bool minister = false) {
        DiplomaticMessage m;
        m.to = kB;
        m.type = type;
        m.treaty = treaty;
        m.offer = std::move(offer);
        m.request = std::move(request);
        const CommandResult res = apply(r, s, kA, cmd::SendMessage{m, minister});
        if (res.ok) s.empire(kA).relation(kB).messageSentThisTurn = false;
        return res;
    };
    PackageItem cash;
    cash.resources = {1000, 0, 0};
    PackageItem anyPlanet;
    anyPlanet.kind = PackageItem::Kind::Planet;
    PackageItem chart;
    chart.kind = PackageItem::Kind::StarChart;
    chart.system = SystemId{0u};

    // Treaties: Non-Intercourse to Partnership, never War or None.
    CHECK(attempt(MessageType::ProposeTreaty, Treaty::None).error == "Propose a treaty from Non-Intercourse to Partnership");
    CHECK(attempt(MessageType::ProposeTreaty, Treaty::War).error == "Propose a treaty from Non-Intercourse to Partnership");
    CHECK(attempt(MessageType::ProposeTreaty, Treaty::NonIntercourse).ok);
    CHECK(attempt(MessageType::ProposeTreaty, Treaty::None, {}, {}, true).ok);   // a minister's is not checked
    // Break Treaty needs Non-Aggression or better.
    CHECK(attempt(MessageType::BreakTreaty).error == "There is no treaty to break");
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::NonIntercourse);
    CHECK_FALSE(attempt(MessageType::BreakTreaty).ok);

    // A trade needs items on both sides; what we give is never "Any"; we may ask for "Any".
    CHECK(attempt(MessageType::ProposeTrade, Treaty::None, {cash}).error == "A trade needs items on both sides");
    CHECK(attempt(MessageType::ProposeTrade, Treaty::None, {}, {chart}).error == "A trade needs items on both sides");
    CHECK(attempt(MessageType::ProposeTrade, Treaty::None, {anyPlanet}, {chart}).error ==
          "A trade offer cannot contain unspecified \"Any\" items");
    CHECK(attempt(MessageType::ProposeTrade, Treaty::None, {cash}, {anyPlanet}).ok);
    // Trades are allowed at war.
    diplomacy::setTreaty(ctx, kA, kB, Treaty::War);
    CHECK(attempt(MessageType::ProposeTrade, Treaty::None, {cash}, {chart}).ok);
    CHECK(attempt(MessageType::DeclareWar).error == "We are already at war");

    // A gift or tribute holds something; a request for one names what it wants, "Any" included.
    CHECK(attempt(MessageType::Gift).error == "The package is empty");
    CHECK(attempt(MessageType::Tribute, Treaty::None, {cash}).ok);
    CHECK(attempt(MessageType::DemandGift).error == "The package is empty");
    CHECK(attempt(MessageType::DemandTribute, Treaty::None, {}, {anyPlanet}).ok);
    // The requests do not depend on the gifts option.
    s.options.allowGifts = false;
    CHECK(attempt(MessageType::DemandGift, Treaty::None, {}, {cash}).ok);
    CHECK_FALSE(attempt(MessageType::Gift, Treaty::None, {cash}).ok);
    s.options.allowGifts = true;

    // Surrender and the demand for it need Allow Surrender.
    s.options.allowSurrender = false;
    CHECK(attempt(MessageType::Surrender).error == "Surrender is not allowed in this game");
    CHECK(attempt(MessageType::DemandSurrender).error == "Surrender is not allowed in this game");
    s.options.allowSurrender = true;
    CHECK(attempt(MessageType::DemandSurrender).ok);

    // A computer player's messages are not checked.
    s.empire(kA).kind = PlayerKind::Computer;
    CHECK(attempt(MessageType::Gift).ok);
}

// A subjugated race's player can accept no treaty; a minister's answer is
// not checked (spec 05 §3.2, confirmed: binary).
TEST_CASE("diplomacy: a minister's answer for a subjugated race is not checked") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Subjugation, true);
    REQUIRE(diplomacy::isSubjugated(s, kB));
    const MessageId id = sendUnchecked(s, kC, kB, MessageType::ProposeTreaty);
    s.messages.back().treaty = Treaty::NonAggression;
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    CHECK(apply(politicsRules(), s, kB, cmd::AnswerMessage{id, true, {}}).error == "Subjugated races cannot accept treaties");
    CHECK(apply(politicsRules(), s, kB, cmd::AnswerMessage{id, true, {}, true}).ok);
}

// The Empires window's Trade and Tariff tabs read the economy report's
// breakdown by empire (spec 06 §1.5, confirmed: binary).
TEST_CASE("diplomacy: the economy report gives the trade and tariffs empire by empire") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeResearchAlliance);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Subjugation, true);   // C pays A
    for (int i = 0; i < 5; ++i) treatySteps(ctx);
    const int64_t f = diplomacy::tradeFactor(r, s.empire(kA));
    const auto genB = diplomacy::generated(r, s, kB);
    const diplomacy::Generated fromB = diplomacy::tradeFrom(r, s, kA, kB);
    for (Resource res : kResources) CHECK(fromB.resources[res] == diplomacy::tradeShare(genB.resources[res], 5, f));
    CHECK(fromB.research == diplomacy::tradeShare(genB.research, 5, f));
    CHECK(fromB.intelligence == 0);   // only in a Partnership
    CHECK(diplomacy::tradeFrom(r, s, kA, kC).resources == Resources{});   // a subjugation trades nothing

    const EconomyReport a = economy::incomeReport(r, s, kA);
    auto entry = [](const EconomyReport& rep, EmpireId e) -> const PartnerIncome* {
        for (const PartnerIncome& p : rep.partners)
            if (p.empire == e) return &p;
        return nullptr;
    };
    const PartnerIncome* b = entry(a, kB);
    REQUIRE(b);
    CHECK(b->trade == max(fromB.resources, Resources{}));
    CHECK(b->research == fromB.research);
    const PartnerIncome* c = entry(a, kC);
    REQUIRE(c);
    CHECK(c->tariffReceived == diplomacy::tariffsPaid(r, s, kC));
    CHECK(c->tariffGiven == Resources{});
    // The subject's report: the tariff it gives its master.
    const EconomyReport subject = economy::incomeReport(r, s, kC);   // kept: entry points into it
    const PartnerIncome* master = entry(subject, kA);
    REQUIRE(master);
    CHECK(master->tariffGiven == subject.tariffsOut);
    CHECK(master->tariffReceived == Resources{});
}

// A human player's statistics record (spec 05 §5, confirmed: binary): at its
// end-of-turn processing, one row for every living empire whose score it may
// see then, its own included.
TEST_CASE("diplomacy: the statistics record holds the visible empires' rows only") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    s.options.scoreDisplay = 1;   // own and Non-Aggression or better
    diplomacy::setTreaty(ctx, kA, kB, Treaty::NonAggression);
    score::recordStatistics(ctx, kA);
    auto rowsOf = [&](EmpireId e) {
        return std::count_if(s.empire(kA).statistics.begin(), s.empire(kA).statistics.end(), [&](const StatisticsRow& x) { return x.empire == e; });
    };
    CHECK(rowsOf(kA) == 1);
    CHECK(rowsOf(kB) == 1);
    CHECK(rowsOf(kC) == 0);
    CHECK(s.empire(kA).statistics.front().stats.turn == s.turn);
    // The next turn the treaty is gone: no row for B, a gap.
    nextTurn(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::None);
    score::recordStatistics(ctx, kA);
    CHECK(rowsOf(kA) == 2);
    CHECK(rowsOf(kB) == 1);
    // A computer player keeps no record.
    s.empire(kC).kind = PlayerKind::Computer;
    score::recordStatistics(ctx, kC);
    CHECK(s.empire(kC).statistics.empty());
}
