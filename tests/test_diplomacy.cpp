// Diplomacy: messages, treaties, packages, surrender, contact, trade and
// tariffs (docs/spec/05 §3).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/commands.hpp"
#include "game/diplomacy.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <format>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};

TurnContext context(GameState& s) { return turnContext(politicsRules(), s); }

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
    CHECK(hasLog(s, kB, "Propose Treaty"));
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
    CHECK(hasLog(s, kA, "Accept Treaty"));

    // Trade starts at 1 % after the signing turn and grows to the maximum.
    CHECK(s.empire(kA).relation(kB).tradePercent == 0);
    diplomacy::advanceTrade(ctx);
    CHECK(s.empire(kA).relation(kB).tradePercent == 1);
    for (int i = 0; i < 30; ++i) diplomacy::advanceTrade(ctx);
    CHECK(s.empire(kA).relation(kB).tradePercent == 20);
    CHECK(s.empire(kB).relation(kA).tradePercent == 20);
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
    CHECK(hasLog(s, kA, "Refuse Treaty"));

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
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeAlliance);
    for (int i = 0; i < 5; ++i) diplomacy::advanceTrade(ctx);
    CHECK(s.empire(kA).relation(kB).tradePercent == 5);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Partnership);  // both trade level: kept
    CHECK(s.empire(kA).relation(kB).tradePercent == 5);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::NonAggression);  // below trade level: reset
    CHECK(s.empire(kA).relation(kB).tradePercent == 0);
    diplomacy::advanceTrade(ctx);
    CHECK(s.empire(kA).relation(kB).tradePercent == 0);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeResearchAlliance);
    diplomacy::advanceTrade(ctx);
    CHECK(s.empire(kB).relation(kA).tradePercent == 1);

    // Setting the same treaty again changes nothing and logs nothing.
    const size_t logs = s.empire(kA).log.size();
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeResearchAlliance);
    CHECK(s.empire(kA).log.size() == logs);
}

TEST_CASE("diplomacy: trade income, research and intelligence trade, tariffs") {
    const Rules& r = politicsRules();
    CHECK(diplomacy::tradeShare(10000, 10, 100, 0) == 1000);
    CHECK(diplomacy::tradeShare(10000, 10, 120, 10) == 1300);
    CHECK(diplomacy::tradeShare(10000, 20, 50, -60) == 0);
    CHECK(diplomacy::tradeShare(-5, 20, 100, 0) == 0);
    CHECK(diplomacy::tradeShare(10000, 0, 100, 0) == 0);

    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeAlliance);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Subjugation, true);
    for (int i = 0; i < 7; ++i) diplomacy::advanceTrade(ctx);
    s.empire(kA).race.characteristics[static_cast<size_t>(Characteristic::PoliticalSavvy)] = 130;

    // Whatever the economy produces, the income follows the formula.
    const auto genB = diplomacy::generated(r, s, kB);
    const auto genC = diplomacy::generated(r, s, kC);
    const Resources tariff = max(genC.resources, Resources{}).percent(40);
    CHECK(diplomacy::tariffsPaid(r, s, kC) == tariff);
    CHECK(diplomacy::tariffsPaid(r, s, kA) == Resources{});
    CHECK(diplomacy::tariffsReceived(r, s, kA) == tariff);
    Resources expected = tariff;
    for (Resource res : kResources) expected[res] += diplomacy::tradeShare(genB.resources[res], 7, 130, 0);
    CHECK(diplomacy::tradeIncome(r, s, kA) == expected);
    CHECK(diplomacy::researchTradeIncome(r, s, kA) == 0);  // Trade Alliance trades no research
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Partnership);
    CHECK(diplomacy::researchTradeIncome(r, s, kA) == diplomacy::tradeShare(genB.research, 7, 130, 0));
    CHECK(diplomacy::intelTradeIncome(r, s, kA) == diplomacy::tradeShare(genB.intelligence, 7, 130, 0));
    CHECK(diplomacy::intelTradeIncome(r, s, kC) == 0);
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

    send(s, kA, kB, MessageType::Surrender);
    diplomacy::deliverMessages(ctx);
    CHECK_FALSE(s.empire(kA).alive);
    CHECK(s.colony(homeA)->owner == kB);
    for (const Vehicle& v : s.vehicles) CHECK(v.owner != kA);
    CHECK(s.empire(kB).stockpile == bBefore + Resources{100, 200, 300});
    CHECK(s.empire(kB).techLevel(beams) == 7);
    CHECK(hasLog(s, kC, "Surrender"));

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

TEST_CASE("diplomacy: first contact and contact loss") {
    GameState s = newPoliticsGame();
    TurnContext ctx = context(s);
    // A sees one of B's ships; A is present where C has a colony.
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kB) {
            s.empire(kA).knowledge.visibleVehicles = {v.id};
            break;
        }
    const SystemId homeC = s.galaxy.object(homeworld(s, kC).planet).system;
    s.empire(kA).knowledge.present[homeC.index()] = 1;
    diplomacy::updateContacts(ctx);
    CHECK(diplomacy::inContact(s, kA, kB));
    CHECK(diplomacy::inContact(s, kB, kA));
    CHECK(diplomacy::inContact(s, kA, kC));
    CHECK_FALSE(diplomacy::inContact(s, kB, kC));
    CHECK(hasLog(s, kB, "First Contact"));
    CHECK(hasLog(s, kC, "First Contact"));

    // Nobody sees anybody and the warp network falls apart: contact is lost.
    diplomacy::setTreaty(ctx, kA, kB, Treaty::NonAggression);
    for (Empire& e : s.empires) {
        e.knowledge.visibleVehicles.clear();
        std::fill(e.knowledge.present.begin(), e.knowledge.present.end(), uint8_t{0});
    }
    diplomacy::updateContacts(ctx);
    CHECK(diplomacy::inContact(s, kA, kB));  // still linked by warp points
    for (SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::WarpPoint) o.destination = ObjectId{};
    diplomacy::updateContacts(ctx);
    CHECK_FALSE(diplomacy::inContact(s, kA, kB));
    CHECK(s.empire(kA).relation(kB).treaty == Treaty::None);
    CHECK(hasLog(s, kA, "Contact Lost"));
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
    s.empire(kB).knowledge.seenDesigns = {cDesign};
    const DesignId fresh = addTestDesign(s, r, kC, "Fresh Hull", "Test Frigate", {"Test Bridge"});
    diplomacy::updateContacts(ctx);
    CHECK(s.empire(kA).hasExplored(homeB));
    CHECK(std::binary_search(s.empire(kA).knowledge.seenDesigns.begin(), s.empire(kA).knowledge.seenDesigns.end(), cDesign));
    CHECK(std::binary_search(s.empire(kA).knowledge.seenDesigns.begin(), s.empire(kA).knowledge.seenDesigns.end(), fresh));
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
            for (Empire& e : s.empires) {
                e.economy.research = 2500;
                e.economy.intelligence = 1200;
            }
            processTurn(r, s, orders);
        }
        std::vector<std::string> trace;
        for (const Empire& e : s.empires) {
            for (const LogEntry& l : e.log) trace.push_back(std::format("{}:{}:{}", l.turn, l.title, l.text));
            for (const Relation& rel : e.relations) trace.push_back(std::format("{}/{}", static_cast<int>(rel.treaty), rel.tradePercent));
            trace.push_back(std::format("{} {} {}", e.stockpile[Resource::Minerals], research::totalLevels(e), e.history.size()));
        }
        return std::pair{trace, s.rng};
    };
    const auto a = play();
    const auto b = play();
    CHECK(a.first.size() > 100);
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}
