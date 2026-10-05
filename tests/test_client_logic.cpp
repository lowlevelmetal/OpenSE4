// Logic behind the classic client's research, intelligence, diplomacy and
// log windows (client/classic/screens/empire_logic.hpp).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "client/classic/screens/empire_logic.hpp"
#include "client/classic/ship_glides.hpp"
#include "game/commands.hpp"
#include "game/diplomacy.hpp"
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
    GameOptions o;
    const auto war = sendableMessageTypes(Treaty::War, o);
    CHECK(has(war, MessageType::ProposeTreaty));
    CHECK(has(war, MessageType::Surrender));
    CHECK_FALSE(has(war, MessageType::DeclareWar));
    CHECK_FALSE(has(war, MessageType::BreakTreaty));
    CHECK_FALSE(has(war, MessageType::ProposeTrade));
    CHECK_FALSE(has(war, MessageType::RequestSupport));

    const auto none = sendableMessageTypes(Treaty::None, o);
    CHECK(has(none, MessageType::DeclareWar));
    CHECK_FALSE(has(none, MessageType::BreakTreaty));  // nothing to break

    const auto trade = sendableMessageTypes(Treaty::TradeAlliance, o);
    CHECK(has(trade, MessageType::BreakTreaty));
    CHECK(has(trade, MessageType::ProposeTrade));
    CHECK(has(trade, MessageType::Gift));
    // Replies are never composed from scratch.
    for (MessageType t : {MessageType::AcceptTreaty, MessageType::RefuseTrade, MessageType::CounterTreaty, MessageType::AcceptDemand})
        CHECK_FALSE(has(trade, t));

    o.allowGifts = false;
    const auto noGifts = sendableMessageTypes(Treaty::TradeAlliance, o);
    CHECK_FALSE(has(noGifts, MessageType::Gift));
    CHECK_FALSE(has(noGifts, MessageType::Tribute));
    CHECK_FALSE(has(noGifts, MessageType::DemandTribute));
}

TEST_CASE("client logic: message parameters, answers and counters") {
    CHECK(messageNeeds(MessageType::ProposeTreaty).treaty);
    CHECK(messageNeeds(MessageType::ProposeTrade).offer);
    CHECK(messageNeeds(MessageType::ProposeTrade).request);
    CHECK(messageNeeds(MessageType::Gift).offer);
    CHECK_FALSE(messageNeeds(MessageType::Gift).request);
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

    const auto treaties = proposableTreaties(Treaty::NonAggression);
    CHECK(std::find(treaties.begin(), treaties.end(), Treaty::NonAggression) == treaties.end());
    CHECK(std::find(treaties.begin(), treaties.end(), Treaty::War) == treaties.end());
    CHECK(std::find(treaties.begin(), treaties.end(), Treaty::None) != treaties.end());  // peace from war
    CHECK(std::find(treaties.begin(), treaties.end(), Treaty::Partnership) != treaties.end());

    CHECK(defaultMessageText(MessageType::ProposeTreaty, Treaty::TradeAlliance).find("Trade Alliance") != std::string::npos);
    CHECK(toneName(0) == "Pleading");
    CHECK(toneName(2) == "Demanding");
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

    const auto general = historyLines(r, s, me, EmpireId{}, false);
    REQUIRE(general.size() == 2);
    CHECK(general[0].text == "A storm formed");  // newest first, also within a turn
    CHECK(general[1].text == "A comet passed");
    CHECK(general[1].where == std::optional<Location>(Location{SystemId{0u}, Sector{3, 4}}));

    const auto ours = historyLines(r, s, me, me, false);
    REQUIRE(ours.size() == 2);
    CHECK(ours[0].text == "Colonized Somewhere");
    CHECK(ours[1].turn == 0);  // the founding comes last
    const auto theirs = historyLines(r, s, me, gone, false);
    REQUIRE(theirs.size() == 2);
    CHECK(theirs[0].text == "Treaty with the Gone ended");
    CHECK(theirs[0].turn == 9);
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

// ---- Ship movement animation (client/classic/ship_glides.hpp) ----

TEST_CASE("client logic: a ship that moves in the shown system turns, then glides there frame by frame (spec 06 §2.4)") {
    const SystemId sys{0u}, other{1u};
    const VehicleId ship{3u};
    ShipGlides g;
    // One call per displayed frame; `heading` is the engine's.
    auto frame = [&](double now, Location at, int heading = 0, SystemId shown = SystemId{0u}, bool enabled = true, double pause = 0.0) {
        const ShipGlides::Seen seen[] = {{ship, at, heading, true}};
        g.track(now, shown, enabled, seen, 50.0f, pause);
    };
    frame(0.0, {sys, Sector{2, 2}});
    CHECK(g.find(ship) == nullptr);   // first sight: nothing to animate

    frame(1.0, {sys, Sector{6, 2}}, 2);   // four squares east, now facing east
    const ShipGlides::Glide* glide = g.find(ship);
    REQUIRE(glide != nullptr);
    CHECK(glide->from == Vec2{2.5f, 2.5f});
    CHECK(glide->to == Vec2{6.5f, 2.5f});
    // A quarter turn: 18 frames of 5°; then 1 px a frame, 50 a square at 1024x768.
    CHECK(glide->turnFrames == 18);
    CHECK(glide->slideFrames == 200);
    CHECK(ShipGlides::angle(*glide) == doctest::Approx(5.0));
    CHECK(ShipGlides::position(*glide) == Vec2{2.5f, 2.5f});
    // One frame a display frame once its wait is over (10 ms after a turn frame).
    double t = 1.0;
    for (int i = 0; i < 17; ++i) frame(t += 0.0167, {sys, Sector{6, 2}}, 2);
    REQUIRE(g.find(ship));
    CHECK(ShipGlides::angle(*g.find(ship)) == doctest::Approx(90.0));
    CHECK(ShipGlides::position(*g.find(ship)) == Vec2{2.5f, 2.5f});
    frame(t += 0.0167, {sys, Sector{6, 2}}, 2);
    CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(2.5f + 1.0f / 50.0f));
    for (int i = 0; i < 99; ++i) frame(t += 0.0167, {sys, Sector{6, 2}}, 2);
    CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(4.5f));   // halfway after 100 slide frames
    for (int i = 0; i < 101; ++i) frame(t += 0.0167, {sys, Sector{6, 2}}, 2);
    CHECK(g.find(ship) == nullptr);   // done, no pause

    SUBCASE("a half-turn goes clockwise; the pause after each step is held on the new square") {
        frame(t += 0.0167, {sys, Sector{4, 2}}, 6, sys, true, 0.5);   // two squares west: a half-turn
        REQUIRE(g.find(ship));
        CHECK(g.find(ship)->angle1 == doctest::Approx(270.0));
        CHECK(g.find(ship)->turnFrames == 36);
        CHECK(g.find(ship)->pause == doctest::Approx(1.0));   // 0.5 s for each of the two steps
    }
    SUBCASE("a second move mid-glide carries on from where the ship is drawn") {
        frame(t += 0.0167, {sys, Sector{4, 2}}, 6);
        for (int i = 0; i < 40; ++i) frame(t += 0.0167, {sys, Sector{4, 2}}, 6);
        const Vec2 drawn = ShipGlides::position(*g.find(ship));
        frame(t += 0.0167, {sys, Sector{4, 8}}, 4);
        const ShipGlides::Glide* next = g.find(ship);
        REQUIRE(next != nullptr);
        CHECK(next->from.x == doctest::Approx(drawn.x));
        CHECK(next->to == Vec2{4.5f, 8.5f});
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
}
