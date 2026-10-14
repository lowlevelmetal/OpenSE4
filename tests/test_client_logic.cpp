// Logic behind the classic client's research, intelligence, diplomacy and
// log windows (client/classic/screens/empire_logic.hpp).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "client/classic/screens/empire_logic.hpp"
#include "client/classic/movement_pace.hpp"
#include "client/classic/ship_glides.hpp"
#include "client/classic/turn_start.hpp"
#include "game/commands.hpp"
#include "game/diplomacy.hpp"
#include "game/score.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <set>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using namespace opense4::client::classic;

namespace {

bool has(const std::vector<MessageType>& v, MessageType t) { return std::find(v.begin(), v.end(), t) != v.end(); }

} // namespace

TEST_CASE("client logic: message types follow the treaty and game options") {
    // The original's list (spec 06 §4.2, confirmed: binary).
    GameOptions o;
    const auto war = sendableMessageTypes(Treaty::War, o);
    CHECK(has(war, MessageType::ProposeTreaty));
    CHECK(has(war, MessageType::Surrender));
    CHECK_FALSE(has(war, MessageType::DeclareWar));
    CHECK_FALSE(has(war, MessageType::BreakTreaty));
    // Trade and every request are offered at war too.
    CHECK(has(war, MessageType::ProposeTrade));
    CHECK(has(war, MessageType::RequestSupport));
    CHECK(has(war, MessageType::RequestMakePeace));
    // Grant independence is OpenSE4's (the original never lists it).
    CHECK(has(war, MessageType::GrantIndependence));

    const auto none = sendableMessageTypes(Treaty::None, o);
    CHECK(has(none, MessageType::DeclareWar));
    CHECK_FALSE(has(none, MessageType::BreakTreaty));  // nothing to break
    // Break Treaty only from Non-Aggression up: not under Non-Intercourse.
    CHECK_FALSE(has(sendableMessageTypes(Treaty::NonIntercourse, o), MessageType::BreakTreaty));
    CHECK(has(sendableMessageTypes(Treaty::NonAggression, o), MessageType::BreakTreaty));

    const auto trade = sendableMessageTypes(Treaty::TradeAlliance, o);
    CHECK(has(trade, MessageType::BreakTreaty));
    CHECK(has(trade, MessageType::ProposeTrade));
    CHECK(has(trade, MessageType::Gift));
    CHECK(trade.front() == MessageType::General);
    // Replies are never composed from scratch.
    for (MessageType t : {MessageType::AcceptTreaty, MessageType::RefuseTrade, MessageType::CounterTreaty, MessageType::AcceptDemand})
        CHECK_FALSE(has(trade, t));

    // Gifts off: no Gift or Tribute, but the requests for them stay.
    o.allowGifts = false;
    const auto noGifts = sendableMessageTypes(Treaty::TradeAlliance, o);
    CHECK_FALSE(has(noGifts, MessageType::Gift));
    CHECK_FALSE(has(noGifts, MessageType::Tribute));
    CHECK(has(noGifts, MessageType::DemandGift));
    CHECK(has(noGifts, MessageType::DemandTribute));

    // Surrender and the demand for it only with Allow Surrender.
    o.allowSurrender = false;
    const auto noSurrender = sendableMessageTypes(Treaty::War, o);
    CHECK_FALSE(has(noSurrender, MessageType::Surrender));
    CHECK_FALSE(has(noSurrender, MessageType::DemandSurrender));
    CHECK(has(sendableMessageTypes(Treaty::War, GameOptions{}), MessageType::DemandSurrender));

    // A subjugated race proposes no treaty; Break Treaty and Declare War stay (spec 05 §3.2).
    const auto subject = sendableMessageTypes(Treaty::Subjugation, GameOptions{}, true);
    CHECK_FALSE(has(subject, MessageType::ProposeTreaty));
    CHECK(has(subject, MessageType::BreakTreaty));
    CHECK(has(subject, MessageType::DeclareWar));
}

TEST_CASE("client logic: message parameters, answers and counters") {
    CHECK(messageNeeds(MessageType::ProposeTreaty).treaty);
    CHECK(messageNeeds(MessageType::ProposeTrade).offer);
    CHECK(messageNeeds(MessageType::ProposeTrade).request);
    CHECK(messageNeeds(MessageType::Gift).offer);
    CHECK_FALSE(messageNeeds(MessageType::Gift).request);
    // A request for a gift or tribute carries what it asks for (spec 06 §4.2).
    for (MessageType t : {MessageType::DemandGift, MessageType::DemandTribute}) {
        CHECK(messageNeeds(t).request);
        CHECK_FALSE(messageNeeds(t).offer);
        CHECK_FALSE(tradePackage(t));
    }
    CHECK(tradePackage(MessageType::ProposeTrade));
    CHECK(tradePackage(MessageType::CounterTrade));
    CHECK_FALSE(tradePackage(MessageType::Gift));
    CHECK(messageNeeds(MessageType::DemandRemoveShips).system);
    CHECK(messageNeeds(MessageType::DemandLeavePlanet).planet);
    CHECK(messageNeeds(MessageType::GrantIndependence).ownPlanet);
    CHECK(messageNeeds(MessageType::RequestMakePeace).thirdEmpire);
    const MessageNeeds general = messageNeeds(MessageType::General);
    CHECK_FALSE((general.treaty || general.offer || general.request || general.system || general.planet || general.thirdEmpire));

    CHECK(answerable(MessageType::ProposeTreaty));
    CHECK(answerable(MessageType::Tribute));
    CHECK(answerable(MessageType::DemandStopSabotage));
    CHECK_FALSE(answerable(MessageType::General));
    CHECK_FALSE(answerable(MessageType::DeclareWar));
    CHECK_FALSE(answerable(MessageType::AcceptTrade));

    CHECK(counterType(MessageType::ProposeTreaty) == MessageType::CounterTreaty);
    CHECK(counterType(MessageType::CounterTrade) == MessageType::CounterTrade);
    CHECK(counterType(MessageType::Gift) == MessageType::General);

    // Non-Intercourse to Partnership, the current treaty included; never War or None.
    const auto treaties = proposableTreaties(Treaty::NonAggression);
    auto lists = [](const std::vector<Treaty>& v, Treaty t) { return std::find(v.begin(), v.end(), t) != v.end(); };
    CHECK(lists(treaties, Treaty::NonAggression));
    CHECK_FALSE(lists(treaties, Treaty::War));
    CHECK_FALSE(lists(treaties, Treaty::None));
    CHECK(treaties.front() == Treaty::NonIntercourse);
    CHECK(treaties.back() == Treaty::Partnership);
    CHECK(treaties.size() == 8);
    CHECK(proposableTreaties(Treaty::War) == treaties);
    // The package's Treaty tab: War included, None and the current one not.
    const auto tab = packageTreaties(Treaty::NonAggression);
    CHECK(lists(tab, Treaty::War));
    CHECK_FALSE(lists(tab, Treaty::None));
    CHECK_FALSE(lists(tab, Treaty::NonAggression));
    CHECK(lists(tab, Treaty::Partnership));

    CHECK(defaultMessageText(MessageType::ProposeTreaty, Treaty::TradeAlliance).find("Trade Alliance") != std::string::npos);
    CHECK(toneName(0) == "Pleading");
    CHECK(toneName(2) == "Demanding");
}

// Communicate opened from a portrait answers the newest message from that
// empire (spec 06 §4.2, confirmed: binary).
TEST_CASE("client logic: the portrait's reply types answer the newest message in its window") {
    CHECK(replyTypes(MessageType::ProposeTreaty) ==
          std::vector<MessageType>{MessageType::AcceptTreaty, MessageType::RefuseTreaty, MessageType::CounterTreaty});
    CHECK(replyTypes(MessageType::CounterTreaty) == replyTypes(MessageType::ProposeTreaty));
    CHECK(replyTypes(MessageType::ProposeTrade) ==
          std::vector<MessageType>{MessageType::AcceptTrade, MessageType::RefuseTrade, MessageType::CounterTrade});
    CHECK(replyTypes(MessageType::Gift) == std::vector<MessageType>{MessageType::AcceptGift, MessageType::RefuseGift});
    CHECK(replyTypes(MessageType::Tribute) == replyTypes(MessageType::Gift));
    CHECK(replyTypeName(MessageType::AcceptGift, MessageType::Tribute) == "Accept Tribute");
    CHECK(replyTypeName(MessageType::RefuseGift, MessageType::Tribute) == "Refuse Tribute");
    CHECK(replyTypeName(MessageType::AcceptGift, MessageType::Gift) == std::string(displayName(MessageType::AcceptGift)));
    for (MessageType d : {MessageType::DemandGift, MessageType::DemandSurrender, MessageType::RequestAttackPlanet, MessageType::DemandStopAttacks})
        CHECK(replyTypes(d) == std::vector<MessageType>{MessageType::AcceptDemand, MessageType::RefuseDemand});
    CHECK(replyTypes(MessageType::General).empty());
    CHECK(replyTypes(MessageType::DeclareWar).empty());
    CHECK(replyAccepts(MessageType::AcceptTrade) == true);
    CHECK(replyAccepts(MessageType::RefuseDemand) == false);
    CHECK_FALSE(replyAccepts(MessageType::CounterTrade).has_value());
    CHECK_FALSE(replyAccepts(MessageType::ProposeTreaty).has_value());

    GameState s;
    s.empires.resize(3);
    for (uint32_t i = 0; i < 3; ++i) s.empires[i].id = EmpireId{i};
    const EmpireId low{0u}, me{1u}, high{2u};
    s.turn = 10;
    auto message = [&](EmpireId from, uint32_t dated, MessageType type) {
        DiplomaticMessage m;
        m.id = MessageId{static_cast<uint32_t>(s.messages.size() + 1)};
        m.from = from;
        m.to = me;
        m.type = type;
        m.delivered = true;
        m.dated = dated;
        s.messages.push_back(m);
        return m.id;
    };
    // After the date − 2: dated 9 or 10, not 8.
    message(high, 8, MessageType::Gift);
    CHECK(replyableMessage(s, me, high) == nullptr);
    const MessageId nine = message(high, 9, MessageType::ProposeTrade);
    REQUIRE(replyableMessage(s, me, high) != nullptr);
    CHECK(replyableMessage(s, me, high)->id == nine);
    CHECK(offerView(replyableMessage(s, me, high)) == OfferView::Trade);
    // Only the newest counts, whatever its type.
    const MessageId ten = message(high, 10, MessageType::General);
    CHECK(replyableMessage(s, me, high)->id == ten);
    CHECK(offerView(replyableMessage(s, me, high)) == OfferView::None);
    CHECK(replyableMessage(s, high, me) == nullptr);   // nothing from us to them
    // In a simultaneous game, from a lower player number: after the date − 1.
    message(low, 9, MessageType::Tribute);
    CHECK(replyableMessage(s, me, low) != nullptr);
    s.options.simultaneous = true;
    CHECK(replyableMessage(s, me, low) == nullptr);
    CHECK(replyableMessage(s, me, high) != nullptr);   // a higher number keeps two turns
    message(low, 10, MessageType::Tribute);
    CHECK(offerView(replyableMessage(s, me, low)) == OfferView::Tribute);
}

// GitHub issue #4: a demand left unanswered kept Empires' "Inbox: 1 waiting"
// for the ten turns OpenSE4 keeps a message, though only the Log's Send
// Reply answers a message and its entry leaves the Log at the end of the
// player's next turn (spec 05 §3.4 "Log lifetime", spec 06 §4.1).
TEST_CASE("client logic: a message waits for a reply only while the Log lists it (issue 4)") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const EmpireId me{0u}, them{1u};
    setContact(s, me, them);
    TurnContext ctx = turnContext(r, s);
    auto send = [&](MessageType type) {
        DiplomaticMessage m;
        m.to = me;
        m.type = type;
        m.treaty = Treaty::NonAggression;
        PackageItem cash;   // a request for a gift names what it wants (spec 06 §4.2)
        cash.kind = PackageItem::Kind::Resources;
        cash.resources = {1000, 0, 0};
        if (type == MessageType::DemandGift) m.request = {cash};
        REQUIRE(apply(r, s, them, cmd::SendMessage{m}).ok);
        const MessageId id = s.messages.back().id;
        diplomacy::deliverMessages(ctx);
        s.empire(them).relation(me).messageSentThisTurn = false;
        return id;
    };
    // A demand arrives: it waits, and the Log lists its entry (Send Reply).
    const MessageId demand = send(MessageType::DemandGift);
    CHECK(messagesAwaitingReply(s, me, them) == 1);
    CHECK(messagesAwaitingReply(s, them, me) == 0);
    // Still at the player's next turn, as the Log still lists it.
    ++s.turn;
    CHECK(messagesAwaitingReply(s, me, them) == 1);
    // Not answered: the player's end-of-turn processing drops the entry from
    // the Log; the chance to reply has passed, though the message is kept.
    empireEndOfTurn(ctx, me, false);
    const auto kept = std::find_if(s.messages.begin(), s.messages.end(), [&](const DiplomaticMessage& m) { return m.id == demand; });
    REQUIRE(kept != s.messages.end());
    CHECK_FALSE(kept->answered);
    for (const LogEntry& l : s.empire(me).log) CHECK(l.message != demand);
    CHECK(messagesAwaitingReply(s, me, them) == 0);
    ++s.turn;
    CHECK(messagesAwaitingReply(s, me, them) == 0);

    // An answered message and one that takes no answer never wait.
    const MessageId proposal = send(MessageType::ProposeTreaty);
    CHECK(messagesAwaitingReply(s, me, them) == 1);
    REQUIRE(apply(r, s, me, cmd::AnswerMessage{proposal, false, {}}).ok);
    CHECK(messagesAwaitingReply(s, me, them) == 0);
    send(MessageType::General);
    CHECK(messagesAwaitingReply(s, me, them) == 0);
}

TEST_CASE("client logic: treaty codes are distinct") {
    std::set<std::string_view> codes;
    for (int i = 0; i < int(Treaty::Count); ++i) codes.insert(treatyCode(static_cast<Treaty>(i)));
    CHECK(codes.size() == size_t(Treaty::Count));
}

TEST_CASE("client logic: contacts and treaty visibility") {
    GameState s = newEngineGame(5, 4, 12);
    const EmpireId me{0u}, a{1u}, b{2u}, c{3u};
    CHECK(contactedEmpires(s, me).empty());
    s.empire(me).relation(a).contact = true;
    s.empire(me).relation(c).contact = true;
    s.empire(c).alive = false;
    CHECK(contactedEmpires(s, me) == std::vector<EmpireId>{a});

    CHECK(treatyVisibleTo(s, me, me, b));
    CHECK_FALSE(treatyVisibleTo(s, me, a, b));
    s.empire(me).relation(a).treaty = s.empire(a).relation(me).treaty = Treaty::Partnership;
    CHECK(treatyVisibleTo(s, me, a, b));  // allies share what they know
    // From Non-Aggression up (spec 05 §3.2, confirmed: binary); Non-Intercourse is not enough.
    s.empire(me).relation(a).treaty = s.empire(a).relation(me).treaty = Treaty::NonAggression;
    CHECK(treatyVisibleTo(s, me, a, b));
    CHECK(treatyVisibleTo(s, me, b, a));
    s.empire(me).relation(a).treaty = s.empire(a).relation(me).treaty = Treaty::NonIntercourse;
    CHECK_FALSE(treatyVisibleTo(s, me, a, b));
}

namespace {

void holdTreaty(GameState& s, EmpireId a, EmpireId b, Treaty t, bool aDominant = false) {
    for (auto [x, y] : {std::pair{a, b}, std::pair{b, a}}) {
        Relation& rel = s.empire(x).relation(y);
        rel.contact = true;
        rel.treaty = t;
        rel.dominant = (t == Treaty::Subjugation || t == Treaty::Protectorate) && (x == a) == aDominant;
    }
}

} // namespace

// The Treaty Grid's cells and the Empires window's lines (spec 06 §1.5,
// confirmed: binary).
TEST_CASE("client logic: the Treaty Grid's cells, the treaty roles and the status line") {
    GameState s = newEngineGame(5, 4, 12);
    const EmpireId me{0u}, a{1u}, b{2u}, c{3u};
    holdTreaty(s, me, a, Treaty::NonAggression);
    holdTreaty(s, a, b, Treaty::Subjugation, true);   // a dominates b
    holdTreaty(s, b, c, Treaty::TradeAlliance);
    holdTreaty(s, me, c, Treaty::None);

    CHECK(treatyCode(Treaty::War) == "War");
    CHECK(treatyCode(Treaty::Subjugation) == "Sub");
    CHECK(treatyCode(Treaty::Protectorate) == "Pro");
    CHECK(treatyCode(Treaty::Partnership) == "P");
    CHECK(treatyCode(Treaty::None).empty());

    CHECK(treatyGridCell(s, me, me, a) == "NA");
    CHECK(treatyGridCell(s, me, a, me) == "NA");
    CHECK(treatyGridCell(s, me, me, c).empty());   // None: blank
    CHECK(treatyGridCell(s, me, me, me).empty());
    // a holds Non-Aggression with us, so its treaties show, with the roles in (row, column) order.
    CHECK(treatyGridCell(s, me, a, b) == "Sub (D, S)");
    CHECK(treatyGridCell(s, me, b, a) == "Sub (S, D)");
    // b and c: we hold Non-Aggression or better with neither.
    CHECK(treatyGridCell(s, me, b, c).empty());
    CHECK(treatyGridCell(s, me, a, c).empty());   // no contact
    holdTreaty(s, me, c, Treaty::War);
    CHECK(treatyGridCell(s, me, c, me) == "War");

    CHECK(treatyRoles(s.empire(a).relation(b)) == "(We Dominate Them)");
    CHECK(treatyRoles(s.empire(b).relation(a)) == "(They Dominate Us)");
    holdTreaty(s, a, b, Treaty::Protectorate, false);   // b protects a
    CHECK(treatyRoles(s.empire(a).relation(b)) == "(They Protect Us)");
    CHECK(treatyRoles(s.empire(b).relation(a)) == "(We Protect Them)");
    CHECK(treatyRoles(s.empire(me).relation(a)).empty());

    // The status line: Message Sent first, then the newest message from them.
    CHECK(empireStatusLine(s, me, a).empty());
    s.turn = 10;
    auto from = [&](EmpireId x, MessageType type, Treaty treaty = Treaty::None) {
        DiplomaticMessage m;
        m.id = MessageId{static_cast<uint32_t>(s.messages.size() + 1)};
        m.from = x;
        m.to = me;
        m.type = type;
        m.treaty = treaty;
        m.delivered = true;
        m.dated = s.turn;
        s.messages.push_back(m);
    };
    from(a, MessageType::ProposeTrade);
    CHECK(empireStatusLine(s, me, a) == "Offered Trade");
    from(a, MessageType::Tribute);
    CHECK(empireStatusLine(s, me, a) == "Offered Tribute");
    from(a, MessageType::ProposeTreaty, Treaty::NonAggression);   // the treaty we hold: nothing to say
    CHECK(empireStatusLine(s, me, a).empty());
    from(a, MessageType::CounterTreaty, Treaty::TradeAlliance);
    CHECK(empireStatusLine(s, me, a) == "Proposed Treaty: Trade Alliance");
    s.empire(me).relation(a).messageSentThisTurn = true;
    CHECK(empireStatusLine(s, me, a) == "Message Sent");
    s.turn = 13;   // out of the window
    s.empire(me).relation(a).messageSentThisTurn = false;
    CHECK(empireStatusLine(s, me, a).empty());
}

// Select Package's lists (spec 05 §3.4, spec 06 §1.5, confirmed: binary).
TEST_CASE("client logic: Select Package lists what the original lists") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 3, 12);
    addHomeShips(s, r);
    const EmpireId me{0u}, them{1u}, third{2u};
    holdTreaty(s, me, them, Treaty::NonAggression);
    holdTreaty(s, them, third, Treaty::None);
    Knowledge& k = s.empire(me).knowledge;
    k.explored.assign(s.galaxy.systems.size(), 0);
    k.explored[0] = k.explored[2] = 1;
    using K = PackageItem::Kind;
    auto kinds = [](const std::vector<PackageItem>& v, K kind) { return std::all_of(v.begin(), v.end(), [&](const PackageItem& i) { return i.kind == kind; }); };
    auto anyFirst = [](const std::vector<PackageItem>& v) { return !v.empty() && isAnyItem(v.front()) && std::none_of(v.begin() + 1, v.end(), isAnyItem); };

    // Systems: every system we have explored, on both sides, no "Any".
    for (bool give : {true, false}) {
        const auto systems = packageChoices(r, s, me, them, PackageTab::Systems, give);
        REQUIRE(systems.size() == 2);
        CHECK(kinds(systems, K::System));
        CHECK(systems[0].system == SystemId{0u});
        CHECK(systems[1].system == SystemId{2u});
    }
    // Star charts: the same systems, with "Any" on the side we ask for.
    CHECK(packageChoices(r, s, me, them, PackageTab::StarCharts, true).size() == 2);
    const auto charts = packageChoices(r, s, me, them, PackageTab::StarCharts, false);
    CHECK(charts.size() == 3);
    CHECK(anyFirst(charts));
    CHECK(kinds(charts, K::StarChart));

    // Planets: ours; theirs in explored systems after "Any".
    const auto ours = packageChoices(r, s, me, them, PackageTab::Planets, true);
    REQUIRE(ours.size() == 1);
    CHECK(ours[0].planet == homeworld(s, me).planet);
    const SystemId theirHome = s.galaxy.object(homeworld(s, them).planet).system;
    k.explored[theirHome.index()] = 0;
    CHECK(packageChoices(r, s, me, them, PackageTab::Planets, false).size() == 1);   // "Any" alone
    k.explored[theirHome.index()] = 1;
    const auto theirs = packageChoices(r, s, me, them, PackageTab::Planets, false);
    REQUIRE(theirs.size() == 2);
    CHECK(anyFirst(theirs));
    CHECK(theirs[1].planet == homeworld(s, them).planet);

    // Ships: ours; theirs that we see, after "Any".
    const auto ships = packageChoices(r, s, me, them, PackageTab::Ships, true);
    CHECK(ships.size() == 3);
    CHECK(kinds(ships, K::Vehicle));
    CHECK(packageChoices(r, s, me, them, PackageTab::Ships, false).size() == 1);
    VehicleId seen;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == them && !seen.valid()) seen = v.id;
    k.visibleVehicles = {seen};
    CHECK(packageChoices(r, s, me, them, PackageTab::Ships, false).size() == 2);
    // Units: the ships carrying units only.
    CHECK(packageChoices(r, s, me, them, PackageTab::Units, true).empty());
    Vehicle* loaded = nullptr;
    for (Vehicle& v : s.vehicles)
        if (v.owner == me && !loaded) loaded = &v;
    REQUIRE(loaded);
    loaded->cargo.units = {UnitStack{loaded->design, 2}};
    const auto units = packageChoices(r, s, me, them, PackageTab::Units, true);
    REQUIRE(units.size() == 1);
    CHECK(units[0].vehicle == loaded->id);
    const auto askUnits = packageChoices(r, s, me, them, PackageTab::Units, false);
    REQUIRE(askUnits.size() == 1);   // "Any" alone: the ship we see carries none
    CHECK(isAnyItem(askUnits[0]));

    // Technology: only with technology trades; "Any" on the side we ask for.
    CHECK(packageTabEnabled(s.options, PackageTab::Technology));
    const auto techAsk = packageChoices(r, s, me, them, PackageTab::Technology, false);
    CHECK(anyFirst(techAsk));
    s.options.allowTechTrades = false;
    CHECK_FALSE(packageTabEnabled(s.options, PackageTab::Technology));
    CHECK(packageTabEnabled(s.options, PackageTab::Resources));
    CHECK(packageChoices(r, s, me, them, PackageTab::Technology, false).empty());
    s.options.allowTechTrades = true;

    // Treaty: every treaty but None and the current one, War included.
    const auto treaties = packageChoices(r, s, me, them, PackageTab::Treaty, true);
    CHECK(treaties.size() == size_t(Treaty::Count) - 2);
    CHECK(kinds(treaties, K::Treaty));
    CHECK(std::any_of(treaties.begin(), treaties.end(), [](const PackageItem& i) { return i.treaty == Treaty::War; }));

    // Comm channels: the empires we have met; asked for, those they have met and we have not. No "Any".
    CHECK(packageChoices(r, s, me, them, PackageTab::CommChannels, true).empty());
    const auto channels = packageChoices(r, s, me, them, PackageTab::CommChannels, false);
    REQUIRE(channels.size() == 1);
    CHECK(channels[0].empire == third);
    holdTreaty(s, me, third, Treaty::None);
    CHECK(packageChoices(r, s, me, them, PackageTab::CommChannels, false).empty());
    const auto give = packageChoices(r, s, me, them, PackageTab::CommChannels, true);
    REQUIRE(give.size() == 1);
    CHECK(give[0].empire == third);

    CHECK(packageChoices(r, s, me, them, PackageTab::Resources, true).empty());
    CHECK(packageTabName(PackageTab::StarCharts) == "Star Charts");
    CHECK(packageTabName(PackageTab::CommChannels) == "Comm Channels");
}

// Scores, Comparisons and Victory Conditions (spec 05 §5, spec 06 §1.5, confirmed: binary).
TEST_CASE("client logic: the score figures, ranks, comparison rows and victory marks") {
    CHECK(scoreFigure(0) == "0.0K");
    CHECK(scoreFigure(12345) == "12.3K");
    CHECK(scoreFigure(12350) == "12.4K");
    CHECK(scoreFigure(999'949) == "999.9K");
    CHECK(scoreFigure(4'500'000) == "4.5M");
    CHECK(populationFigure(2500) == "2.5B");
    CHECK(populationFigure(1'200'000) == "1.2T");
    CHECK(rankText(1) == "1st");
    CHECK(rankText(2) == "2nd");
    CHECK(rankText(3) == "3rd");
    CHECK(rankText(4) == "4th");
    CHECK(rankText(11) == "11th");
    CHECK(rankText(21) == "21th");   // as the original writes it
    CHECK(denseRanks({50, 80, 50, 10}) == std::vector<int>{2, 1, 2, 3});
    CHECK(denseRanks({}).empty());

    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 3, 12);
    const EmpireId me{0u}, a{1u}, b{2u};
    // The rows of our statistics record only, by turn; a turn not recorded is a gap.
    auto row = [](EmpireId e, uint32_t turn, int64_t score) {
        StatisticsRow x;
        x.empire = e;
        x.stats.turn = turn;
        x.stats.score = score;
        return x;
    };
    s.empire(me).history.push_back(TurnStats{});
    s.empire(me).statistics = {row(me, 1, 10), row(a, 1, 20), row(me, 2, 11), row(me, 3, 12), row(a, 3, 22)};
    const auto rowsA = comparisonRows(s, me, a);
    REQUIRE(rowsA.size() == 2);
    CHECK(rowsA[0].turn == 1);
    CHECK(rowsA[1].turn == 3);
    CHECK(comparisonRows(s, me, me).size() == 3);
    CHECK(comparisonRows(s, me, b).empty());
    s.empire(a).alive = false;   // destroyed, but recorded: still listed
    CHECK(comparisonEmpires(s, me) == std::vector<EmpireId>{me, a});
    s.empire(a).alive = true;
    // A game saved before the record: the history while the score is visible.
    s.empire(me).statistics.clear();
    s.empire(b).history = {TurnStats{}, TurnStats{}};
    s.options.scoreDisplay = 2;
    CHECK(comparisonRows(s, me, b).size() == 2);
    s.options.scoreDisplay = 0;
    CHECK(comparisonRows(s, me, b).empty());

    // Victory Conditions' columns read Score Display one step off.
    s.options.scoreDisplay = 0;
    CHECK(victoryColumnVisible(s, me, me));
    CHECK_FALSE(victoryColumnVisible(s, me, a));
    holdTreaty(s, me, a, Treaty::NonAggression);
    CHECK(victoryColumnVisible(s, me, a));   // "own only" shows the Non-Aggression partners
    s.options.scoreDisplay = 1;
    CHECK(victoryColumnVisible(s, me, b));   // "partners" shows everyone
    s.empire(b).alive = false;
    CHECK_FALSE(victoryColumnVisible(s, me, b));
    s.empire(b).alive = true;

    // The marks: nothing in a row that is off; the years mark every empire once the date has come.
    VictoryConditions& v = s.options.victory;
    CHECK_FALSE(victoryRowOn(v, VictoryRow::Years));
    CHECK_FALSE(victoryMark(r, s, VictoryRow::Years, me));
    v.years = true;
    v.yearsValue = 1;
    s.turn = 9;
    CHECK_FALSE(victoryMark(r, s, VictoryRow::Years, me));
    s.turn = 10;
    CHECK(victoryMark(r, s, VictoryRow::Years, me));
    CHECK(victoryMark(r, s, VictoryRow::Years, a));
    v.score = true;
    v.scoreValue = 0;
    CHECK(victoryMark(r, s, VictoryRow::Score, me));
    v.scoreValue = game::score::empireScore(r, s, me) + 1;
    CHECK_FALSE(victoryMark(r, s, VictoryRow::Score, me));
    v.peace = true;
    v.peaceYears = 1;
    s.peacefulTurns = 9;
    CHECK_FALSE(victoryMark(r, s, VictoryRow::Peace, b));
    s.peacefulTurns = 10;
    CHECK(victoryMark(r, s, VictoryRow::Peace, b));
}

// The text a new message starts with (spec 06 §4.2, confirmed: binary).
TEST_CASE("client logic: a new message's text comes from the race's speech pools") {
    GameState s = newEngineGame(5, 2, 12);
    const EmpireId me{0u}, them{1u};
    CHECK(messageSpeechPool(s, me, them, MessageType::ProposeTreaty) == "Send Propose Treaty");
    CHECK(messageSpeechPool(s, me, them, MessageType::DemandGift) == "Send Want a gift");
    CHECK(messageSpeechPool(s, me, them, MessageType::AcceptGift, MessageType::Tribute) == "Send Accept Tribute");
    CHECK(messageSpeechPool(s, me, them, MessageType::RefuseGift, MessageType::Gift) == "Send Refuse Gift");
    // Accept or Refuse Demand: Friend from Non-Aggression up.
    CHECK(messageSpeechPool(s, me, them, MessageType::AcceptDemand, MessageType::DemandLeavePlanet) == "Response Enemy YES Leave planet");
    holdTreaty(s, me, them, Treaty::NonAggression);
    CHECK(messageSpeechPool(s, me, them, MessageType::RefuseDemand, MessageType::DemandLeavePlanet) == "Response Friend NO Leave planet");
    // A line of the pool, its tokens filled in.
    DiplomaticMessage draft;
    draft.to = them;
    draft.type = MessageType::ProposeTreaty;
    draft.treaty = Treaty::TradeAlliance;
    const std::string text = messageText(engineRules(), s, me, draft, MessageType::General, 3);
    CHECK(text.find("Trade Alliance") != std::string::npos);
    CHECK(text.find("[%") == std::string::npos);
    // Without a race to speak for, OpenSE4's own text.
    CHECK(messageText(engineRules(), s, EmpireId{}, draft, MessageType::General, 3) == defaultMessageText(MessageType::ProposeTreaty, Treaty::TradeAlliance));
}

TEST_CASE("client logic: package items and Any placeholders") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    PackageItem res;
    res.resources = Resources{2000, 0, 1000};
    CHECK(packageItemText(r, s, res) == "2,000 Minerals, 1,000 Radioactives");
    CHECK_FALSE(isAnyItem(res));

    PackageItem planet;
    planet.kind = PackageItem::Kind::Planet;
    CHECK(isAnyItem(planet));
    CHECK(packageItemText(r, s, planet) == "Any planet");
    planet.planet = homeworld(s, EmpireId{0u}).planet;
    CHECK_FALSE(isAnyItem(planet));
    CHECK(packageItemText(r, s, planet).find(s.galaxy.object(planet.planet).name) != std::string::npos);

    PackageItem tech;
    tech.kind = PackageItem::Kind::Technology;
    tech.tech = techArea(r, "Test Physics");
    CHECK(packageItemText(r, s, tech) == "Technology: Test Physics");
    CHECK_FALSE(packageHasAny({res, tech}));
    PackageItem channel;
    channel.kind = PackageItem::Kind::CommChannel;
    CHECK(packageHasAny({res, tech, channel}));
}

TEST_CASE("client logic: researchable areas without the research module") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    const Empire& e = s.empire(EmpireId{0u});
    const auto areas = researchableAreas(r, s, e);
    REQUIRE_FALSE(areas.empty());
    for (ruleset::TechAreaId a : areas) {
        CHECK(r.techVisible(s, e, a));
        CHECK(e.techLevel(a) < r.tech(a).maxLevel);
    }
    const auto contains = [&](std::string_view name) {
        return std::find(areas.begin(), areas.end(), techArea(r, name)) != areas.end();
    };
    CHECK(contains("Test Physics"));
    CHECK_FALSE(contains("Test Missiles"));      // needs Physics 2
    CHECK_FALSE(contains("Test Psionics"));      // racial
    CHECK_FALSE(contains("Test Rock Colonies"));  // complete for a rock race
}

TEST_CASE("client logic: the Research list keeps completed areas") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    const Empire& e = s.empire(EmpireId{0u});
    const auto list = researchListAreas(r, s, e);
    const auto open = researchableAreas(r, s, e);
    const auto find = [&](std::string_view name) {
        return std::find_if(list.begin(), list.end(), [&](const ResearchListArea& x) { return x.area == techArea(r, name); });
    };
    // Every researchable area, not complete, and the completed ones besides, in data order.
    for (ruleset::TechAreaId a : open)
        CHECK(std::any_of(list.begin(), list.end(), [&](const ResearchListArea& x) { return x.area == a && !x.complete; }));
    REQUIRE(find("Test Rock Colonies") != list.end());
    CHECK(find("Test Rock Colonies")->complete);
    CHECK(find("Test Psionics") == list.end());  // racial, never researched
    CHECK(find("Test Missiles") == list.end());  // not yet open
    for (size_t i = 1; i < list.size(); ++i) CHECK(list[i - 1].area.index() < list[i].area.index());
}

TEST_CASE("client logic: what a tech level unlocks") {
    const Rules& r = engineRules();
    const auto physics = techArea(r, "Test Physics");
    const auto level2 = techUnlocks(r, physics, 2);
    auto named = [](const std::vector<TechUnlock>& v, std::string_view n, TechUnlock::Kind k) {
        return std::any_of(v.begin(), v.end(), [&](const TechUnlock& u) { return u.name == n && u.kind == k; });
    };
    CHECK(named(level2, "Test Missiles", TechUnlock::Kind::TechArea));
    CHECK(named(level2, "Test Shields", TechUnlock::Kind::TechArea));
    CHECK_FALSE(named(level2, "Test Master Computer", TechUnlock::Kind::Component));
    CHECK(named(techUnlocks(r, physics, 4), "Test Master Computer", TechUnlock::Kind::Component));
    CHECK_FALSE(unlockNames(r, physics, 4).empty());

    const auto deps = dependentAreas(r, physics);
    CHECK(std::find(deps.begin(), deps.end(), techArea(r, "Test Cloaking")) != deps.end());
    CHECK(requirementText(r, r.tech(techArea(r, "Test Missiles")).requirements) == "Test Physics 2");
    CHECK(requirementText(r, {}) == "None");
}

TEST_CASE("client logic: tech progress, ETA text and exports") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    const Empire& e = s.empire(EmpireId{0u});
    const auto [owned, total] = techProgress(r, s, e);
    CHECK(owned > 0);
    CHECK(owned < total);

    CHECK(etaText(-1) == "Never");
    CHECK(etaText(13) == "1.3 years");
    CHECK(etaText(4) == "0.4 years");

    const std::string areas = techAreasExport(r, s, e);
    CHECK(areas.find("Test Missiles") != std::string::npos);
    CHECK(areas.find("Requires:  Test Physics 2") != std::string::npos);
    const std::string levels = techLevelsExport(r, s, e);
    CHECK(levels.find("Test Master Computer [Component]") != std::string::npos);
}

TEST_CASE("client logic: intel project targets") {
    ruleset::IntelProject p;
    p.type = "Intelligence Defense";
    CHECK(intelTargetKind(p) == IntelTarget::None);
    p.type = "Planet - Population Change";
    CHECK(intelTargetKind(p) == IntelTarget::Planet);
    p.type = "Planet - Locations";
    CHECK(intelTargetKind(p) == IntelTarget::Empire);
    p.type = "Ship - Rebel";
    CHECK(intelTargetKind(p) == IntelTarget::Vehicle);
    p.type = "Ship - Concentrations";
    CHECK(intelTargetKind(p) == IntelTarget::Empire);
    p.type = "Politics - Fake Messages";
    CHECK(intelTargetKind(p) == IntelTarget::ThirdEmpire);
    p.type = "Points - Steal";
    CHECK(intelTargetKind(p) == IntelTarget::Empire);
}

TEST_CASE("client logic: statistics series and timeline") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    s.turn = 3;
    Empire& e = s.empire(EmpireId{0u});
    TurnStats a, b;
    a.turn = 1;
    a.planets = 1;
    a.systems = 1;
    a.techLevels = 10;
    a.ships = 4;
    b = a;
    b.turn = 2;
    b.planets = 3;
    b.techLevels = 11;
    b.ships = 2;
    e.history = {b, a};  // any order
    const auto series = statsSeries(r, s, e.id);
    REQUIRE(series.size() == 3);
    CHECK(series[0].turn == 1);
    CHECK(series[1].turn == 2);
    CHECK(series[2].turn == 3);  // the current turn is appended
    CHECK(metricValue(b, Metric::Planets) == 3);
    CHECK(metricName(Metric::TechLevels) == "Tech Levels");

    const auto events = statsEvents({a, b});
    auto mentions = [&](std::string_view text) {
        return std::any_of(events.begin(), events.end(), [&](const HistoryEvent& h) { return h.text.find(text) != std::string::npos; });
    };
    CHECK(mentions("Gained 2 planets (now 3)"));
    CHECK(mentions("Learned 1 tech level (now 11)"));
    CHECK(mentions("Lost 2 ships (now 2)"));
    CHECK_FALSE(mentions("system"));
}

TEST_CASE("client logic: History window lists") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 3, 12);
    const EmpireId me{0u}, met{1u}, gone{2u};
    s.turn = 9;
    s.empire(me).relation(met).contact = s.empire(met).relation(me).contact = true;
    s.empire(gone).alive = false;  // destroyed, but our record mentions it
    addHistory(s, me, gone, "Treaty with the Gone ended");
    s.turn = 12;
    addHistory(s, me, EmpireId{}, "A comet passed", Location{SystemId{0u}, Sector{3, 4}});
    addHistory(s, me, me, "Colonized Somewhere");
    addHistory(s, me, EmpireId{}, "A storm formed");
    addHistory(s, met, EmpireId{}, "Not ours");

    CHECK(historyEmpires(s, me) == std::vector<EmpireId>{me, met, gone});
    CHECK(historyEmpires(s, met) == std::vector<EmpireId>{me, met});

    // Oldest first, as the original reads its file (spec 06 §4.3, confirmed: binary).
    const auto general = historyLines(r, s, me, EmpireId{}, false);
    REQUIRE(general.size() == 2);
    CHECK(general[0].text == "A comet passed");  // in the record's order within a turn
    CHECK(general[1].text == "A storm formed");
    CHECK(general[0].where == std::optional<Location>(Location{SystemId{0u}, Sector{3, 4}}));

    const auto ours = historyLines(r, s, me, me, false);
    REQUIRE(ours.size() == 2);
    CHECK(ours[0].turn == 0);  // the founding comes first
    CHECK(ours[1].text == "Colonized Somewhere");
    const auto theirs = historyLines(r, s, me, gone, false);
    REQUIRE(theirs.size() == 2);
    CHECK(theirs[1].text == "Treaty with the Gone ended");
    CHECK(theirs[1].turn == 9);

    // The decade headings over the lines.
    CHECK(decadeHeading(0) == "2400 - 2410");
    CHECK(decadeHeading(99) == "2400 - 2410");
    CHECK(decadeHeading(100) == "2410 - 2420");
    CHECK(decadeHeading(1234) == "2520 - 2530");
}

TEST_CASE("client logic: reordering a list") {
    std::vector<int> v{1, 2, 3, 4};
    CHECK(moveEntry(v, 3, 0));
    CHECK(v == std::vector<int>{4, 1, 2, 3});
    CHECK(moveEntry(v, 0, 99));  // clamped to the bottom
    CHECK(v == std::vector<int>{1, 2, 3, 4});
    CHECK_FALSE(moveEntry(v, 2, 2));
    CHECK_FALSE(moveEntry(v, 7, 0));
}

// ---- Ship movement animation (client/classic/ship_glides.hpp, movement_pace.hpp) ----

TEST_CASE("client logic: the movement animation's frames come by the time elapsed (spec 06 §2.4)") {
    // The original waits 10 ms after each 5° frame of a turn and 1 ms after
    // each 1 px frame of a slide (confirmed: binary): the default pace.
    const MovementPace original;
    CHECK(original.turnFrameSeconds() == doctest::Approx(0.010));
    CHECK(original.slideFrameSeconds() == doctest::Approx(0.001));
    CHECK(kMovementSpeeds[kOriginalMovementSpeed] == 1.0);
    // A quarter turn (18 frames) and one square at 1024x768 (50 frames): 0.18 s + 0.05 s.
    CHECK(original.duration(18, 50) == doctest::Approx(0.230));
    // The first frame at once, each next one when the wait after the one before is over.
    CHECK(original.framesDrawn(18, 50, 0.0) == 1);
    CHECK(original.framesDrawn(18, 50, 0.0099) == 1);
    CHECK(original.framesDrawn(18, 50, 0.010) == 2);
    CHECK(original.framesDrawn(18, 50, 0.1795) == 18);     // the turn's last frame
    CHECK(original.framesDrawn(18, 50, 0.180) == 19);      // the slide's first
    CHECK(original.framesDrawn(18, 50, 0.1905) == 29);
    CHECK(original.framesDrawn(18, 50, 5.0) == 68);        // all of them, however late
    CHECK(original.framesDrawn(0, 36, 0.0175) == 18);      // a slide alone at 800x600
    CHECK(original.framesDrawn(0, 0, 1.0) == 0);
    // Whatever the display's refresh rate: the same frame at the same time,
    // whether it is looked at 60 or 240 times a second.
    for (const double hz : {30.0, 60.0, 144.0, 240.0}) {
        int drawn = 0;
        for (double t = 0.0; t < 0.1; t += 1.0 / hz) drawn = original.framesDrawn(0, 50, t);
        CHECK(drawn == 50);
    }
    // The player's speed divides the waits.
    const MovementPace twice{2.0}, quarter{0.25};
    CHECK(twice.duration(18, 50) == doctest::Approx(0.115));
    CHECK(quarter.duration(18, 50) == doctest::Approx(0.920));
    CHECK(twice.framesDrawn(0, 50, 0.010) == 21);
    CHECK(quarter.framesDrawn(0, 50, 0.010) == 3);
    // Speeds read from the settings file come to the nearest step offered.
    CHECK(movementSpeedStep(1.0) == kOriginalMovementSpeed);
    CHECK(kMovementSpeeds[movementSpeedStep(3.0)] == 4.0);   // nearer 4 than 2 by ratio
    CHECK(kMovementSpeeds[movementSpeedStep(100.0)] == 8.0);
    CHECK(kMovementSpeeds[movementSpeedStep(0.01)] == 0.125);
    CHECK(movementSpeedStep(0.0) == kOriginalMovementSpeed);
    CHECK(movementSpeedStep(-2.0) == kOriginalMovementSpeed);
}

TEST_CASE("client logic: a ship that moves in the shown system turns, then glides there (spec 06 §2.4)") {
    const SystemId sys{0u}, other{1u};
    const VehicleId ship{3u};
    ShipGlides g;
    // One call per displayed frame; `heading` is the engine's.
    auto frame = [&](double now, Location at, int heading = 0, SystemId shown = SystemId{0u}, bool enabled = true, double pause = 0.0,
                     MovementPace pace = {}) {
        const ShipGlides::Seen seen[] = {{ship, at, heading, true}};
        g.track(now, shown, enabled, seen, 50.0f, pause, pace);
    };
    frame(0.0, {sys, Sector{2, 2}});
    CHECK(g.find(ship) == nullptr);   // first sight: nothing to animate
    CHECK_FALSE(g.active());

    frame(1.0, {sys, Sector{6, 2}}, 2);   // four squares east, now facing east
    const ShipGlides::Glide* glide = g.find(ship);
    REQUIRE(glide != nullptr);
    CHECK(g.active());
    CHECK(glide->from == Vec2{2.5f, 2.5f});
    CHECK(glide->to == Vec2{6.5f, 2.5f});
    // A quarter turn: 18 frames of 5°; then 1 px a frame, 50 a square at 1024x768.
    CHECK(glide->turnFrames == 18);
    CHECK(glide->slideFrames == 200);
    CHECK(ShipGlides::angle(*glide) == doctest::Approx(5.0));
    CHECK(ShipGlides::position(*glide) == Vec2{2.5f, 2.5f});
    // At the original's pace: 10 ms a turn frame, so the turn is over after
    // 0.18 s; then 1 ms a slide frame, so the four squares take 0.2 s.
    frame(1.1755, {sys, Sector{6, 2}}, 2);
    REQUIRE(g.find(ship));
    CHECK(ShipGlides::angle(*g.find(ship)) == doctest::Approx(90.0));
    CHECK(ShipGlides::position(*g.find(ship)) == Vec2{2.5f, 2.5f});
    frame(1.1805, {sys, Sector{6, 2}}, 2);
    CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(2.5f + 1.0f / 50.0f));
    frame(1.2795, {sys, Sector{6, 2}}, 2);
    CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(4.5f));   // halfway after 100 slide frames
    frame(1.3795, {sys, Sector{6, 2}}, 2);
    REQUIRE(g.find(ship));   // the last frame drawn, its wait not over yet
    CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(6.5f));
    frame(1.3805, {sys, Sector{6, 2}}, 2);
    CHECK(g.find(ship) == nullptr);   // done, no pause
    CHECK_FALSE(g.active());
    double t = 1.3805;

    SUBCASE("the player's speed: twice as fast, half the time") {
        frame(t += 0.01, {sys, Sector{8, 2}}, 2, sys, true, 0.0, MovementPace{2.0});   // two squares on, no turn
        REQUIRE(g.find(ship));
        CHECK(g.find(ship)->slideFrames == 100);
        frame(t + 0.0495, {sys, Sector{8, 2}}, 2);
        CHECK(g.find(ship));
        CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(8.5f));
        frame(t + 0.0505, {sys, Sector{8, 2}}, 2);   // the pace it began with, whatever is passed later
        CHECK(g.find(ship) == nullptr);
    }
    SUBCASE("skipped: every ship is on its square at once") {
        frame(t += 0.01, {sys, Sector{8, 2}}, 2);
        REQUIRE(g.active());
        g.finish();
        CHECK_FALSE(g.active());
        CHECK(g.find(ship) == nullptr);
        frame(t += 0.01, {sys, Sector{8, 2}}, 2);   // and it does not start again
        CHECK_FALSE(g.active());
    }
    SUBCASE("a half-turn goes clockwise; the pause after each step is held on the new square") {
        frame(t += 0.0167, {sys, Sector{4, 2}}, 6, sys, true, 0.5);   // two squares west: a half-turn
        REQUIRE(g.find(ship));
        CHECK(g.find(ship)->angle1 == doctest::Approx(270.0));
        CHECK(g.find(ship)->turnFrames == 36);
        CHECK(g.find(ship)->pause == doctest::Approx(1.0));   // 0.5 s for each of the two steps
        frame(t + 0.36 + 0.1 + 0.5, {sys, Sector{4, 2}}, 6);
        REQUIRE(g.find(ship));   // arrived, pausing
        CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(4.5f));
        frame(t + 0.36 + 0.1 + 1.001, {sys, Sector{4, 2}}, 6);
        CHECK(g.find(ship) == nullptr);
    }
    SUBCASE("a second move mid-glide carries on from where the ship is drawn") {
        frame(t += 0.0167, {sys, Sector{4, 2}}, 6);
        frame(t += 0.4, {sys, Sector{4, 2}}, 6);
        const Vec2 drawn = ShipGlides::position(*g.find(ship));
        frame(t += 0.0167, {sys, Sector{4, 8}}, 4);
        const ShipGlides::Glide* next = g.find(ship);
        REQUIRE(next != nullptr);
        CHECK(next->from.x == doctest::Approx(drawn.x));
        CHECK(next->to == Vec2{4.5f, 8.5f});
    }
    SUBCASE("a second look in the same frame starts the glides of a change made since, and changes nothing else") {
        frame(t += 0.0167, {sys, Sector{7, 2}}, 2);
        REQUIRE(g.find(ship));
        const int drawn = g.find(ship)->frame;
        frame(t, {sys, Sector{7, 2}}, 2);
        CHECK(g.find(ship)->frame == drawn);
        frame(t, {sys, Sector{7, 3}}, 4);   // moved again at once: a new glide from where it is drawn
        REQUIRE(g.find(ship));
        CHECK(g.find(ship)->to == Vec2{7.5f, 3.5f});
    }
    SUBCASE("warps, view changes and the setting do not glide") {
        frame(2.0e3, {other, Sector{6, 2}});                   // warped out
        CHECK(g.find(ship) == nullptr);
        frame(2.1e3, {sys, Sector{0, 0}});                     // warped back in
        CHECK(g.find(ship) == nullptr);
        frame(2.2e3, {sys, Sector{3, 0}}, 2, other);           // the player looks at another system
        CHECK(g.find(ship) == nullptr);
        frame(2.3e3, {sys, Sector{3, 0}});                     // back to this one: no move since
        frame(2.4e3, {sys, Sector{5, 0}}, 2, sys, false);      // animation switched off
        CHECK(g.find(ship) == nullptr);
    }
    SUBCASE("another system shown: the glides under way end") {
        frame(t += 0.0167, {sys, Sector{9, 2}}, 2);
        REQUIRE(g.active());
        frame(t += 0.0167, {sys, Sector{9, 2}}, 2, other);
        CHECK_FALSE(g.active());
    }
}

// ---- What a turn's start shows, and in which order (client/classic/turn_start.hpp) ----

TEST_CASE("client logic: a turn's start shows its moves first, then battles, endings, questions and the Log (spec 06 §2.7)") {
    TurnStartFacts f;
    // Nothing under way: everything may show; the main window does not wait.
    TurnStartGate g = turnStartGate(f);
    CHECK_FALSE(g.hold);
    CHECK(g.battles);
    CHECK(g.endings);
    CHECK(g.questions);
    CHECK(g.log);

    // A new turn's moves are being shown: nothing opens, the main window waits (a click skips).
    f.movesShowing = true;
    f.turnStarting = true;
    g = turnStartGate(f);
    CHECK(g.hold);
    CHECK_FALSE(g.battles);
    CHECK_FALSE(g.endings);
    CHECK_FALSE(g.questions);
    CHECK_FALSE(g.log);

    // Moves of an order given during the turn, with nothing waiting for them: the player plays on.
    f.turnStarting = false;
    g = turnStartGate(f);
    CHECK_FALSE(g.hold);
    CHECK_FALSE(g.log);
    // ... but a question or a message box they raised waits for them, and so does the main window.
    f.questionWaiting = true;
    CHECK(turnStartGate(f).hold);
    CHECK_FALSE(turnStartGate(f).questions);

    // The moves shown: the battles to watch come first ...
    f = {};
    f.battlesQueued = true;
    f.questionWaiting = true;
    g = turnStartGate(f);
    CHECK(g.battles);
    CHECK_FALSE(g.endings);
    CHECK_FALSE(g.questions);
    CHECK_FALSE(g.log);
    // ... a battle that stops the engine call holds the rest too ...
    f.battlesQueued = false;
    f.battleWaiting = true;
    g = turnStartGate(f);
    CHECK(g.battles);
    CHECK_FALSE(g.endings);
    CHECK_FALSE(g.log);
    // ... then the endings (the destruction check comes before the orders run) ...
    f.battleWaiting = false;
    f.endingOpen = true;
    g = turnStartGate(f);
    CHECK(g.endings);
    CHECK_FALSE(g.questions);
    CHECK_FALSE(g.log);
    // ... then the questions the player's own orders raise ...
    f.endingOpen = false;
    g = turnStartGate(f);
    CHECK(g.questions);
    CHECK_FALSE(g.log);
    // ... and the Log last, when the player gets the turn.
    f.questionWaiting = false;
    CHECK(turnStartGate(f).log);
}
