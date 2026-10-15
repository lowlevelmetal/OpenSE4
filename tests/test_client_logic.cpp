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
    // In a turn-based game, from a lower player number: after the date − 1
    // (spec 05 §3.4, confirmed: binary; spec 06 §1.5 had the turn style
    // backwards until 2026-10-14).
    message(low, 9, MessageType::Tribute);
    s.options.simultaneous = true;
    CHECK(replyableMessage(s, me, low) != nullptr);
    s.options.simultaneous = false;
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
    v.yearsTurns = 10;
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
    v.peaceTurns = 10;
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

    // The area report's later levels (ours, GitHub issue #11): from a level up
    // to the area's maximum, only the levels that bring something.
    const auto later = levelUnlocksFrom(r, physics, 3);
    REQUIRE_FALSE(later.empty());
    CHECK(later.front().level == 3);
    CHECK(named(later.front().items, "Test Cloaking", TechUnlock::Kind::TechArea));
    for (size_t i = 0; i < later.size(); ++i) {
        CHECK_FALSE(later[i].items.empty());
        CHECK(later[i].level <= r.tech(physics).maxLevel);
        if (i > 0) CHECK(later[i - 1].level < later[i].level);
    }
    const auto at4 = std::find_if(later.begin(), later.end(), [](const LevelUnlocks& l) { return l.level == 4; });
    REQUIRE(at4 != later.end());
    CHECK(named(at4->items, "Test Master Computer", TechUnlock::Kind::Component));
    CHECK(levelUnlocksFrom(r, physics, r.tech(physics).maxLevel + 1).empty());
    // Every level that brings something is listed, and no other.
    for (const auto area : {physics, techArea(r, "Test Missiles"), techArea(r, "Test Cloaking")}) {
        const auto all = levelUnlocksFrom(r, area, 1);
        for (int level = 1; level <= r.tech(area).maxLevel; ++level) {
            const bool listed = std::any_of(all.begin(), all.end(), [&](const LevelUnlocks& l) { return l.level == level; });
            CHECK(listed == !techUnlocks(r, area, level).empty());
        }
    }

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

}

TEST_CASE("client logic: the Tech Tree's rows and export") {
    // Spec 06 §7 Q92 (confirmed: binary): every area of the data set, under
    // the groups in alphabetical order, data order under each.
    ruleset::Ruleset rs = buildEngineRuleset();
    ruleset::RacialTrait mind;
    mind.name = "Test Mind Gift";
    mind.traitType = "Tech Area";
    mind.values = {"1"};
    rs.racialTraits.push_back(mind);
    rs.reindex();
    const Rules r(std::move(rs));
    const auto areaRows = techTreeRows(r, false);
    std::vector<std::string> groups;
    for (const TechTreeRow& row : areaRows)
        if (groups.empty() || groups.back() != r.tech(row.area).group) groups.push_back(r.tech(row.area).group);
    CHECK(groups == std::vector<std::string>{"Applied Science", "Theoretical Science", "Weapon Technology"});
    size_t areasListed = 0;
    for (size_t i = 0; i < areaRows.size(); ++i)
        if (i == 0 || areaRows[i].area != areaRows[i - 1].area) ++areasListed;
    CHECK(areasListed == r.data().techAreas.size());   // racial and unique areas too

    // Tech Areas: a row per requirement, the racial trait, a unique area's mark.
    auto rowsOf = [&](std::string_view name, bool levels) {
        return levels ? techLevelRows(r, techArea(r, name)) : techAreaRows(r, techArea(r, name));
    };
    const auto missiles = rowsOf("Test Missiles", false);
    REQUIRE(missiles.size() == 1);
    CHECK(missiles[0].cells == std::array<std::string, 3>{"Test Missiles", "Test Physics", "2"});
    const auto psionics = rowsOf("Test Psionics", false);
    REQUIRE(psionics.size() == 1);
    CHECK(psionics[0].cells == std::array<std::string, 3>{"Test Psionics", "Test Mind Gift", "Racial Trait"});
    const auto relics = rowsOf("Test Relics", false);
    REQUIRE(relics.size() == 1);
    CHECK(relics[0].cells == std::array<std::string, 3>{"Test Relics", "Unique Technology", "(Must Be Found)"});
    const auto physics = rowsOf("Test Physics", false);
    REQUIRE(physics.size() == 1);
    CHECK(physics[0].cells == std::array<std::string, 3>{"Test Physics", "", ""});

    // Tech Levels: every level; the items a level makes available, " [N]"
    // after one with more than one requirement; areas are not items.
    const auto levels = rowsOf("Test Physics", true);
    auto at = [&](int level) {
        std::vector<std::string> items;
        for (const TechTreeRow& row : levels)
            if (row.cells[1] == std::to_string(level)) items.push_back(row.cells[2]);
        return items;
    };
    CHECK(at(1).size() >= 1);
    CHECK(at(2) == std::vector<std::string>{""});   // only areas need Physics 2
    const auto four = at(4);
    CHECK(std::find(four.begin(), four.end(), "Test Master Computer") != four.end());
    CHECK(levels.back().cells[1] == "5");

    // The export: one line per row, the cells joined by commas.
    const std::string text = techTreeExport(missiles);
    CHECK(text == "Test Missiles,Test Physics,2\n");
    CHECK(techTreeExport(techTreeRows(r, true)).find("Test Physics,4,Test Master Computer\n") != std::string::npos);
}

TEST_CASE("client logic: intel project targets") {
    // The second list window a project asks for (spec 05 §2.1, confirmed: binary).
    ruleset::IntelProject p;
    auto kind = [&](const char* type) {
        p.type = type;
        return intelTargetKind(p);
    };
    CHECK(kind("Intelligence Defense") == IntelTarget::None);
    // The seven ship sabotage types (listed in an order of our own).
    for (const char* t : {"Ship - Orders Change", "Ship - Rebel", "Ship - Damage", "Ship - Cargo Damage", "Ship - Lose Supply",
                          "Ship - Experience Change", "Ship - Lose Movement"})
        CHECK(kind(t) == IntelTarget::Vehicle);
    for (const char* t : {"Planet - Info", "Planet - Facility Damage", "Planet - Population Rebel", "Planet - Value Change",
                          "Planet - Cargo Damage", "Planet - Population Riot", "Planet - Conditions Change", "Planet - Population Anger Change",
                          "Planet - Population Change"})
        CHECK(kind(t) == IntelTarget::Planet);
    CHECK(kind("Research - Steal") == IntelTarget::Tech);
    // The five Politics types.
    for (const char* t : {"Politics - Treaty Info", "Politics - Fake Messages", "Politics - Prevent Messages", "Politics - Disrupt Trade",
                          "Politics - Intercept Messages"})
        CHECK(kind(t) == IntelTarget::ThirdEmpire);
    // Only the empire: reports, points, deleting projects, design and log theft, and a mod's types.
    for (const char* t : {"Ship - Concentrations", "Ship - Construction Info", "Planet - Locations", "Points - Change", "Points - Steal",
                          "Research - Delete Project", "Intel - Delete Project", "System - Info", "Empire - Info", "Tech Level - Info",
                          "Ship Designs - Steal", "Unit Designs - Steal", "Log Reports - Steal", "A Mod's Own Type"})
        CHECK(kind(t) == IntelTarget::Empire);
}

TEST_CASE("client logic: intelligence target lists and the box's target line") {
    // Spec 05 §2.1 (confirmed: binary): "Select Empire" without "Any", then a
    // second list by the project's kind, its "Any ..." row first.
    using effects::Effect;
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame(5, 4);
    const EmpireId a{0u}, b{1u}, c{2u}, d{3u};
    setContact(s, a, b);
    setContact(s, a, c);
    setContact(s, a, d);
    s.empire(d).alive = false;   // the dead are not listed
    auto order = [&](Effect e, EmpireId target = {}) {
        IntelProjectOrder o;
        o.project = projectFor(e);
        o.target = target;
        return o;
    };
    auto labels = [](const IntelTargetList& list) {
        std::vector<std::string> out;
        for (const IntelTargetRow& row : list.rows) out.push_back(row.label);
        return out;
    };

    // Stage 0: the living contacts, in empire order; the empire-only kinds end there.
    const IntelTargetList empires = intelTargetList(r, s, a, order(Effect::ShipDamage), 0);
    CHECK(empires.title == "Select Empire");
    CHECK(labels(empires) == std::vector<std::string>{s.empire(b).name, s.empire(c).name});
    CHECK(empires.rows[1].order.target == c);
    CHECK_FALSE(empires.last);
    CHECK(intelTargetList(r, s, a, order(Effect::PointsChange), 0).last);

    // Ships: the target's vehicles we see now, by name with the case ignored.
    std::vector<VehicleId> theirs;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == b && v.count > 0) theirs.push_back(v.id);
    REQUIRE(theirs.size() >= 3);
    s.vehicle(theirs[0])->name = "zeta";
    s.vehicle(theirs[1])->name = "Alpha";
    s.vehicle(theirs[2])->name = "beta";
    std::vector<VehicleId>& seen = s.empire(a).knowledge.visibleVehicles;
    seen = {theirs[0], theirs[1], theirs[2]};
    for (const Vehicle& v : s.vehicles)
        if (v.owner == c && v.count > 0) seen.push_back(v.id);   // another empire's: not listed
    const IntelTargetList ships = intelTargetList(r, s, a, order(Effect::ShipRebel, b), 1);
    CHECK(ships.title == "Select Ship");
    CHECK(labels(ships) == std::vector<std::string>{"Any Ship", "Alpha", "beta", "zeta"});
    CHECK_FALSE(ships.rows[0].order.targetVehicle.valid());
    CHECK(ships.rows[1].order.targetVehicle == theirs[1]);
    CHECK(ships.rows[1].order.target == b);
    CHECK(intelTargetLine(r, s, ships.rows[0].order) == "Any Ship");
    CHECK(intelTargetLine(r, s, ships.rows[3].order) == "zeta");

    // Planets: the target's colonies we see; none before we explore their system.
    const IntelTargetList blind = intelTargetList(r, s, a, order(Effect::PlanetInfo, b), 1);
    CHECK(blind.title == "Select Planet");
    CHECK(labels(blind) == std::vector<std::string>{"Any Planet"});
    ObjectId home;
    for (const auto& col : s.colonies)
        if (col && col->owner == b) home = col->planet;
    REQUIRE(home.valid());
    const SystemId homeSystem = s.galaxy.object(home).system;
    s.empire(a).knowledge.explored[homeSystem.index()] = 1;
    for (Vehicle& v : s.vehicles)
        if (v.owner == a && v.count > 0) {
            v.location = Location{homeSystem, s.galaxy.object(home).sector};   // a sensor in the system
            break;
        }
    const IntelTargetList planets = intelTargetList(r, s, a, order(Effect::PlanetInfo, b), 1);
    REQUIRE(planets.rows.size() >= 2);
    CHECK(planets.rows[0].label == "Any Planet");
    CHECK(std::any_of(planets.rows.begin(), planets.rows.end(), [&](const IntelTargetRow& row) { return row.order.targetPlanet == home; }));
    CHECK(intelTargetLine(r, s, planets.rows[1].order) == planets.rows[1].label);

    // Technology: the areas the spy can research now, in alphabetical order.
    const IntelTargetList areas = intelTargetList(r, s, a, order(Effect::ResearchSteal, b), 1);
    CHECK(areas.title == "Select Technology");
    REQUIRE(areas.rows.size() >= 3);
    CHECK(areas.rows[0].label == "Any Technology");
    for (size_t i = 2; i < areas.rows.size(); ++i) CHECK(areas.rows[i - 1].label <= areas.rows[i].label);
    const std::vector<ruleset::TechAreaId> open = researchableAreas(r, s, s.empire(a));
    CHECK(areas.rows.size() == open.size() + 1);
    for (size_t i = 1; i < areas.rows.size(); ++i)
        CHECK(std::find(open.begin(), open.end(), areas.rows[i].order.targetTech) != open.end());
    CHECK(intelTargetLine(r, s, areas.rows[0].order) == "Any Technology");

    // A third empire: our other living contacts, the target left out.
    const IntelTargetList third = intelTargetList(r, s, a, order(Effect::PoliticsDisruptTrade, b), 1);
    CHECK(third.title == "Select Empire");
    CHECK(labels(third) == std::vector<std::string>{"Any Empire", s.empire(c).name});
    CHECK(third.rows[1].order.thirdEmpire == c);
    CHECK(intelTargetLine(r, s, third.rows[1].order) == s.empire(c).name);

    // The empire-only kinds and defense: no target line.
    CHECK(intelTargetLine(r, s, order(Effect::PointsSteal, b)).empty());
    CHECK(intelTargetLine(r, s, order(Effect::IntelligenceDefense)).empty());
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
    SUBCASE("a second move found mid-glide waits for the first, then glides on from where it ended") {
        frame(t += 0.0167, {sys, Sector{4, 2}}, 6);   // a half-turn (0.36 s), then two squares (0.1 s)
        const double began = t;
        frame(t += 0.2, {sys, Sector{4, 2}}, 6);
        frame(t += 0.0167, {sys, Sector{4, 8}}, 4);   // six squares south
        REQUIRE(g.find(ship));
        CHECK(g.find(ship)->to == Vec2{4.5f, 2.5f});   // still the first move
        CHECK(g.heldAt(ship) == Location{sys, Sector{6, 2}});
        frame(began + 0.4605, {sys, Sector{4, 8}}, 4);
        const ShipGlides::Glide* next = g.find(ship);
        REQUIRE(next != nullptr);
        CHECK(next->from == Vec2{4.5f, 2.5f});
        CHECK(next->to == Vec2{4.5f, 8.5f});
        CHECK(next->start == doctest::Approx(began + 0.46));   // when the first was over
        CHECK(next->angle0 == doctest::Approx(270.0));        // from the heading the first left it with
    }
    SUBCASE("a second look in the same frame takes a change made since, and changes nothing else") {
        frame(t += 0.0167, {sys, Sector{7, 2}}, 2);
        REQUIRE(g.find(ship));
        const int drawn = g.find(ship)->frame;
        frame(t, {sys, Sector{7, 2}}, 2);
        CHECK(g.find(ship)->frame == drawn);
        frame(t, {sys, Sector{7, 3}}, 4);   // moved again at once: queued after the glide under way
        REQUIRE(g.find(ship));
        CHECK(g.find(ship)->to == Vec2{7.5f, 2.5f});
        CHECK(g.heldAt(ship) == Location{sys, Sector{6, 2}});
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

// The queue of an engine call's steps (spec 06 §2.4 "Moves as they are made"):
// three vehicles in system 0, a fleet of two and a lone ship, all the
// player's own and seen; the first frame sees them where they start.
struct GlideQueue {
    const SystemId sys{0u}, other{1u};
    const VehicleId a{1u}, mate{2u}, b{3u}, c{4u};
    ShipGlides g;
    std::vector<ShipGlides::Seen> before{{a, {sys, Sector{2, 2}}, 0, true, true, true},
                                         {mate, {sys, Sector{2, 2}}, 0, true, true, true},
                                         {b, {sys, Sector{6, 6}}, 0, true, true, true},
                                         {c, {other, Sector{1, 1}}, 0, true, true, true}};
    // Where the engine left them: the fleet two squares east, B one north, C one east elsewhere.
    std::vector<ShipGlides::Seen> after{{a, {sys, Sector{4, 2}}, 2, true, true, true},
                                        {mate, {sys, Sector{4, 2}}, 2, true, true, true},
                                        {b, {sys, Sector{6, 5}}, 0, true, true, true},
                                        {c, {other, Sector{2, 1}}, 2, true, true, true}};
    GlideQueue() { g.track(0.0, sys, true, before); }
    static MovementStep step(std::vector<VehicleId> members, Location from, Location to, int heading) {
        MovementStep st;
        st.members = std::move(members);
        st.lead = st.members.front();
        st.from = from;
        st.to = to;
        st.heading = heading;
        return st;
    }
    MovementStep fleet1() const { return step({a, mate}, {sys, Sector{2, 2}}, {sys, Sector{3, 2}}, 2); }
    MovementStep fleet2() const { return step({a, mate}, {sys, Sector{3, 2}}, {sys, Sector{4, 2}}, 2); }
    MovementStep north() const { return step({b}, {sys, Sector{6, 6}}, {sys, Sector{6, 5}}, 0); }
    MovementStep away() const { return step({c}, {other, Sector{1, 1}}, {other, Sector{2, 1}}, 2); }
    void frame(double now, ShipGlides::Order order = ShipGlides::Order::OneGroupAtATime, double pause = 0.0) {
        g.track(now, sys, true, after, 50.0f, pause, {}, order);
    }
};

TEST_CASE("client logic: an engine call's steps play one after another, each square its own turn and slide, one group after another") {
    GlideQueue q;
    const MovementStep steps[] = {q.fleet1(), q.fleet2(), q.north()};
    q.g.enqueue(steps);
    q.frame(1.0);
    // The fleet's first square: one sprite, its first vehicle's, turning a quarter (18 frames of 10 ms), then 50 of 1 ms.
    const ShipGlides::Glide* fleet = q.g.find(q.a);
    REQUIRE(fleet != nullptr);
    CHECK(q.g.find(q.mate) == fleet);
    CHECK(fleet->lead == q.a);
    CHECK(fleet->from == Vec2{2.5f, 2.5f});
    CHECK(fleet->to == Vec2{3.5f, 2.5f});
    CHECK(fleet->turnFrames == 18);
    CHECK(fleet->slideFrames == 50);
    CHECK(ShipGlides::angle(*fleet) == doctest::Approx(5.0));
    // B waits on its square, facing as it did, until the fleet's run is over.
    CHECK(q.g.find(q.b) == nullptr);
    CHECK(q.g.heldAt(q.b) == Location{q.sys, Sector{6, 6}});
    CHECK(q.g.heldHeading(q.b) == 0);
    CHECK(q.g.moving(q.b));
    CHECK(q.g.active());

    // 0.23 s later the second square begins, from where the first ended, with no turn.
    q.frame(1.2305);
    fleet = q.g.find(q.a);
    REQUIRE(fleet != nullptr);
    CHECK(fleet->from == Vec2{3.5f, 2.5f});
    CHECK(fleet->start == doctest::Approx(1.23));
    CHECK(fleet->turnFrames == 0);
    CHECK(ShipGlides::position(*fleet).x == doctest::Approx(3.5f + 1.0f / 50.0f));
    CHECK(q.g.heldAt(q.a) == Location{q.sys, Sector{3, 2}});
    CHECK(q.g.find(q.b) == nullptr);

    // Then B, when the fleet has arrived; the fleet is drawn where the game has it.
    q.frame(1.2805);
    CHECK(q.g.find(q.a) == nullptr);
    CHECK_FALSE(q.g.moving(q.a));
    REQUIRE(q.g.find(q.b) != nullptr);
    CHECK(q.g.find(q.b)->start == doctest::Approx(1.28));
    CHECK(ShipGlides::position(*q.g.find(q.b)).y == doctest::Approx(6.5f - 1.0f / 50.0f));
    q.frame(1.3305);
    CHECK_FALSE(q.g.active());
    CHECK_FALSE(q.g.moving(q.b));

    SUBCASE("the pause comes after each step") {
        GlideQueue p;
        p.g.enqueue(steps);
        p.frame(1.0, ShipGlides::Order::OneGroupAtATime, 0.5);
        p.frame(1.2305, ShipGlides::Order::OneGroupAtATime, 0.5);
        REQUIRE(p.g.find(p.a));
        CHECK(p.g.find(p.a)->from == Vec2{2.5f, 2.5f});   // arrived, pausing on the new square
        CHECK(ShipGlides::position(*p.g.find(p.a)).x == doctest::Approx(3.5f));
        p.frame(1.7305, ShipGlides::Order::OneGroupAtATime, 0.5);
        REQUIRE(p.g.find(p.a));
        CHECK(p.g.find(p.a)->from == Vec2{3.5f, 2.5f});
        CHECK(p.g.find(p.a)->start == doctest::Approx(1.73));
    }
    SUBCASE("several steps over in one frame: the next starts when the one before ended") {
        GlideQueue p;
        p.g.enqueue(steps);
        p.frame(1.0);
        p.frame(1.3005);   // both of the fleet's squares are over; B began at 1.28
        REQUIRE(p.g.find(p.b));
        CHECK(p.g.find(p.b)->start == doctest::Approx(1.28));
        CHECK(p.g.find(p.b)->frame == 21);
    }
}

TEST_CASE("client logic: with all at once every group starts together, each with its own steps in turn") {
    GlideQueue q;
    const MovementStep steps[] = {q.fleet1(), q.fleet2(), q.north()};
    q.g.enqueue(steps);
    q.frame(1.0, ShipGlides::Order::AllAtOnce);
    REQUIRE(q.g.find(q.a));
    REQUIRE(q.g.find(q.b));
    CHECK(q.g.find(q.a)->to == Vec2{3.5f, 2.5f});
    CHECK(q.g.find(q.b)->start == doctest::Approx(1.0));
    CHECK(q.g.all().size() == 2);
    q.frame(1.2305, ShipGlides::Order::AllAtOnce);
    REQUIRE(q.g.find(q.a));
    CHECK(q.g.find(q.a)->from == Vec2{3.5f, 2.5f});   // the fleet's second square after its first
    CHECK(q.g.find(q.a)->start == doctest::Approx(1.23));
    CHECK(q.g.find(q.b) == nullptr);                   // B arrived at 1.05
    q.frame(1.2805, ShipGlides::Order::AllAtOnce);
    CHECK_FALSE(q.g.active());
}

TEST_CASE("client logic: steps not shown take no time but keep their turn; a marker waits for the steps before it") {
    SUBCASE("another system, a jump, a vehicle not seen before") {
        GlideQueue q;
        q.after.push_back({VehicleId{9u}, {q.sys, Sector{9, 9}}, 0, true, true, false});   // a foreign ship, first seen now
        q.after[2].location = {q.other, Sector{0, 6}};                                    // B jumps out instead
        const MovementStep steps[] = {q.away(), GlideQueue::step({VehicleId{9u}}, {q.sys, Sector{9, 8}}, {q.sys, Sector{9, 9}}, 4),
                                      GlideQueue::step({q.b}, {q.sys, Sector{6, 6}}, {q.other, Sector{0, 6}}, 0), q.fleet1()};
        q.after[0].location = q.after[1].location = {q.sys, Sector{3, 2}};
        q.g.enqueue(steps);
        q.frame(1.0);
        // The fleet's step begins at once: the others were done in their turn, without a wait.
        REQUIRE(q.g.find(q.a));
        CHECK(q.g.find(q.a)->start == doctest::Approx(1.0));
        CHECK_FALSE(q.g.moving(q.b));
        CHECK_FALSE(q.g.moving(q.c));
        CHECK(q.g.find(VehicleId{9u}) == nullptr);
    }
    SUBCASE("a marker") {
        GlideQueue q;
        const MovementStep fleet[] = {q.fleet1(), q.fleet2()};
        const MovementStep lone[] = {q.north()};
        q.g.enqueue(fleet);
        q.g.enqueueMarker(7);
        q.g.enqueue(lone);
        q.frame(1.0);
        CHECK(q.g.takeReached().empty());
        q.frame(1.2805);   // the fleet's run is over: the marker is reached, and the queue stops there
        CHECK(q.g.takeReached() == std::vector<uint64_t>{7});
        CHECK(q.g.find(q.b) == nullptr);
        CHECK(q.g.active());
        q.frame(1.2805);   // called again (the view followed): B goes on from when the fleet arrived
        REQUIRE(q.g.find(q.b));
        CHECK(q.g.find(q.b)->start == doctest::Approx(1.28));
    }
    SUBCASE("skipped, or the animation off: everything at once, every marker reached") {
        for (const bool off : {false, true}) {
            CAPTURE(off);
            GlideQueue q;
            const MovementStep fleet[] = {q.fleet1(), q.fleet2()};
            q.g.enqueue(fleet);
            q.g.enqueueMarker(3);
            q.g.enqueueMarker(4);
            if (off) {
                q.g.track(1.0, q.sys, false, q.after);
            } else {
                q.frame(1.0);
                q.g.finish();
            }
            CHECK_FALSE(q.g.active());
            CHECK_FALSE(q.g.moving(q.a));
            CHECK(q.g.find(q.a) == nullptr);
            CHECK(q.g.takeReached() == std::vector<uint64_t>{3, 4});
        }
    }
    SUBCASE("another system shown: the glide under way ends, the queue goes on there") {
        GlideQueue q;
        const MovementStep steps[] = {q.fleet1(), q.away()};
        q.after[0].location = q.after[1].location = {q.sys, Sector{3, 2}};
        q.g.enqueue(steps);
        q.frame(1.0);
        REQUIRE(q.g.find(q.a));
        q.g.track(1.1, q.other, true, q.after);
        CHECK(q.g.find(q.a) == nullptr);
        REQUIRE(q.g.find(q.c));   // C's step, in the system now shown
        CHECK(q.g.find(q.c)->start == doctest::Approx(1.1));
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
