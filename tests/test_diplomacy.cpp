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
    CHECK(hasLog(s, kA, "New Treaty"));
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
    agree(s, kA, kB, Treaty::TradeAlliance);
    const LogEntry* treaty = findLog(s, kA, "New Treaty");
    REQUIRE(treaty);
    CHECK(treaty->target == LogGoto::Empires);

    TurnContext ctx = context(s);
    const auto beams = techArea(r, "Test Beams");
    s.empire(kA).techLevels[beams.index()] = 4;
    s.empire(kA).stockpile = {5000, 5000, 5000};
    const MessageId gift = send(s, kA, kB, MessageType::Gift, Treaty::None, {tech(beams)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, gift, true);
    diplomacy::deliverMessages(ctx);
    const LogEntry* techGift = findLog(s, kB, "Gift Completed");
    REQUIRE(techGift);
    CHECK(techGift->target == LogGoto::Research);
    nextTurn(s);
    const MessageId cash = send(s, kA, kB, MessageType::Gift, Treaty::None, {resources(10, 0, 0)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, cash, true);
    diplomacy::deliverMessages(ctx);
    const LogEntry* last = nullptr;
    for (const LogEntry& l : s.empire(kB).log)
        if (l.title == "Gift Completed") last = &l;
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

    // An acceptance of something never proposed changes nothing.
    nextTurn(s);
    DiplomaticMessage forged;
    forged.to = kA;
    forged.type = MessageType::AcceptTreaty;
    forged.treaty = Treaty::Partnership;
    forged.inReplyTo = MessageId{12345u};
    REQUIRE(apply(politicsRules(), s, kB, cmd::SendMessage{forged}).ok);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::NonAggression);
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
    CHECK(hasMood(ctx, kB, "New Treaty None"));

    nextTurn(s);
    send(s, kB, kA, MessageType::DeclareWar);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::War);
    CHECK(s.empire(kA).relation(kB).lastWarTurn == static_cast<int32_t>(s.turn));
    CHECK(hasMood(ctx, kA, "New Treaty War"));
    CHECK(hasLog(s, kA, "War Declared"));

    // Breaking a war is not a way to make peace.
    nextTurn(s);
    send(s, kB, kA, MessageType::BreakTreaty);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::War);
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

    // The subject cannot sign anything else.
    diplomacy::setTreaty(ctx, kC, kB, Treaty::MilitaryAlliance);
    CHECK(s.empire(kC).relation(kB).treaty == Treaty::None);
    CHECK(hasLog(s, kB, "Treaty Not Possible"));

    // Third-party treaties are visible to allies only.
    CHECK(diplomacy::treatyVisible(s, kC, kA, kC));
    CHECK_FALSE(diplomacy::treatyVisible(s, kC, kA, kB));
    diplomacy::setTreaty(ctx, kC, kA, Treaty::MilitaryAlliance);
    CHECK(diplomacy::treatyVisible(s, kC, kA, kB));  // A is C's ally

    CHECK(diplomacy::treatyTrigger(Treaty::Protectorate, false) == "New Treaty Protectorate (Sub)");
    CHECK(diplomacy::treatyTrigger(Treaty::NonIntercourse, false) == "New Treaty Non Intercourse");
    CHECK(diplomacy::treatyTrigger(Treaty::TradeResearchAlliance, false) == "New Treaty Trade and Research");
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
    CHECK(s.empire(kB).techLevel(beams) == 4);
    CHECK(s.vehicle(scout)->owner == kB);
    CHECK(s.empire(kB).hasExplored(homeA));
    CHECK(diplomacy::inContact(s, kB, kC));
    CHECK(hasLog(s, kA, "Trade Completed"));
    CHECK(hasLog(s, kB, "First Contact"));
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
    CHECK(hasLog(s, kB, "Items Unavailable"));
    CHECK(hasLog(s, kB, "Gift Completed"));

    // Technology trades can be switched off.
    nextTurn(s);
    s.options.allowTechTrades = false;
    const auto armor = techArea(r, "Test Armor");
    s.empire(kA).techLevels[armor.index()] = 5;
    id = send(s, kA, kB, MessageType::Gift, Treaty::None, {tech(armor)});
    diplomacy::deliverMessages(ctx);
    nextTurn(s);
    answer(s, kB, id, true);
    diplomacy::deliverMessages(ctx);
    CHECK(s.empire(kB).techLevel(armor) == 1);
    CHECK(diplomacy::isPlaceholder(tech({})));
    CHECK_FALSE(diplomacy::isPlaceholder(resources(1, 2, 3)));
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
    send(s, kA, kB, MessageType::Surrender);
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
    send(s, kA, kB, MessageType::Surrender);
    diplomacy::deliverMessages(ctx);
    // Everything passes, but the empire lives on, owning nothing, until its
    // next destruction check (spec 05 §6).
    CHECK(s.empire(kA).alive);
    CHECK(s.colony(homeA)->owner == kB);
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
