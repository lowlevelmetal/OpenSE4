// Colony orders: Convert Resources (docs/spec/02 §5.6) and the colony's own
// order list (spec 03 §8, §12); colony (planetary) cloaking (spec 01 §6.9).

#include "movement_fixture.hpp"

#include "game/combat.hpp"
#include "game/commands.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;

namespace {

Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

cmd::SetOrders colonyOrders(ObjectId planet, std::vector<Order> orders, bool repeat = false) {
    cmd::SetOrders c;
    c.planet = planet;
    c.orders = std::move(orders);
    c.repeat = repeat;
    return c;
}

std::vector<Order> convert(Resource from, Resource to, int64_t amount) { return economy::conversionOrders(from, to, amount); }

int logged(const World& w, EmpireId e, std::string_view title) {
    return static_cast<int>(std::count_if(w.s.empire(e).log.begin(), w.s.empire(e).log.end(), [&](const LogEntry& l) { return l.title == title; }));
}

} // namespace

TEST_CASE("colony orders: a conversion yields trunc(amount × (100 − L) / 100) in exact integers (spec 02 §5.6)") {
    CHECK(economy::conversionGain(1000, 30) == 700);
    CHECK(economy::conversionGain(65000, 0) == 65000);
    CHECK(economy::conversionGain(999, 30) == 699);
    // The exact formula, also where the original's floating point comes out 1 lower (OpenSE4 choice).
    CHECK(economy::conversionGain(100, 16) == 84);
    CHECK(economy::conversionGain(50, 22) == 39);
    CHECK(economy::conversionGain(1000, 100) == 0);
    CHECK(economy::conversionGain(1000, 120) == -200);  // a loss above 100 takes from the target
    // A window line becomes (amount div 65,000) + 1 orders of at most 65,000.
    const auto one = convert(Resource::Minerals, Resource::Organics, 1000);
    REQUIRE(one.size() == 1);
    CHECK(one[0].kind == OrderKind::ConvertResources);
    CHECK(one[0].amount == 1000);
    CHECK(one[0].from == static_cast<uint8_t>(Resource::Minerals));
    CHECK(one[0].to == static_cast<uint8_t>(Resource::Organics));
    const auto exact = convert(Resource::Minerals, Resource::Organics, 130000);
    REQUIRE(exact.size() == 3);
    CHECK(exact[0].amount == 65000);
    CHECK(exact[1].amount == 65000);
    CHECK(exact[2].amount == 0);  // an exact multiple ends with an order for 0
    const auto big = convert(Resource::Radioactives, Resource::Minerals, 70001);
    REQUIRE(big.size() == 2);
    CHECK(big[1].amount == 5001);
}

TEST_CASE("colony orders: Convert Resources runs on day 1 of a simultaneous movement phase, is logged, and reads the loss then") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {6, 6});
    w.colony(home, kA, 1000, {"Mv Converter", "Mv Lossy Converter"});
    Resources& bank = w.s.empire(kA).stockpile;
    bank = {10000, 10000, 10000};
    REQUIRE(apply(r, w.s, kA, colonyOrders(home, convert(Resource::Minerals, Resource::Organics, 2000))).ok);
    CHECK(bank == Resources{10000, 10000, 10000});  // nothing until the movement phase
    w.move();
    // The highest loss counts: 50 %.
    CHECK(bank == Resources{8000, 11000, 10000});
    CHECK(w.s.colony(home)->orders.empty());
    CHECK(logged(w, kA, "Resources Converted") == 1);

    // The amount is cut to the stock; source and target may be the same.
    bank = {300, 0, 0};
    std::vector<Order> list = convert(Resource::Minerals, Resource::Radioactives, 1000);
    const auto same = convert(Resource::Organics, Resource::Organics, 1000);  // nothing held: nothing happens
    list.insert(list.end(), same.begin(), same.end());
    REQUIRE(apply(r, w.s, kA, colonyOrders(home, list)).ok);
    w.move();
    CHECK(bank == Resources{0, 0, 150});
    // The loss is read when the order runs: with the converters scrapped it converts without loss.
    w.s.colony(home)->orders = convert(Resource::Radioactives, Resource::Minerals, 100);
    w.s.colony(home)->facilities.clear();
    w.move();
    CHECK(bank == Resources{100, 0, 50});
    // Nothing happens with a resource that is not one of the three.
    Order odd = convert(Resource::Minerals, Resource::Organics, 50).front();
    odd.to = 7;
    w.s.colony(home)->orders = {odd};
    w.move();
    CHECK(bank == Resources{100, 0, 50});
    CHECK(w.s.colony(home)->orders.empty());
}

TEST_CASE("colony orders: who may convert, at most 21 a run, and Repeat goes round the list (spec 02 §5.6)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId plain = w.planet(a, {2, 2});
    w.colony(plain, kA, 1000);
    // No converter: the order cannot be given; more than 65,000 in one order neither.
    CHECK_FALSE(apply(r, w.s, kA, colonyOrders(plain, convert(Resource::Minerals, Resource::Organics, 1000))).ok);
    const ObjectId home = w.planet(a, {6, 6});
    w.colony(home, kA, 1000, {"Mv Converter"});
    Order tooBig = convert(Resource::Minerals, Resource::Organics, 1000).front();
    tooBig.amount = 65001;
    CHECK_FALSE(apply(r, w.s, kA, colonyOrders(home, {tooBig})).ok);
    // The planet's own Resource Conversion lets it give the order, but only
    // facilities give the loss: it converts with none.
    const ObjectId rich = w.planet(a, {9, 9});
    w.s.galaxy.object(rich).abilities.push_back(ab(AbilityKind::ResourceConversion, 40));
    w.colony(rich, kA, 1000);
    Resources& bank = w.s.empire(kA).stockpile;
    bank = {1000, 0, 0};
    REQUIRE(apply(r, w.s, kA, colonyOrders(rich, convert(Resource::Minerals, Resource::Organics, 1000))).ok);
    w.move();
    CHECK(bank == Resources{0, 1000, 0});

    // 22 orders: 21 run in one run, the last waits for the next.
    bank = {100000, 0, 0};
    std::vector<Order> many;
    for (int i = 0; i < 22; ++i) many.push_back(convert(Resource::Minerals, Resource::Organics, 10).front());
    REQUIRE(apply(r, w.s, kA, colonyOrders(home, many)).ok);
    w.move();
    CHECK(bank[Resource::Minerals] == 100000 - 21 * 10);
    CHECK(w.s.colony(home)->orders.size() == 1);
    w.s.colony(home)->orders.clear();

    // Repeat: one order goes round its list, 21 times a run.
    bank = {100000, 0, 0};
    REQUIRE(apply(r, w.s, kA, colonyOrders(home, convert(Resource::Minerals, Resource::Organics, 100), true)).ok);
    CHECK(w.s.colony(home)->repeatOrders);
    w.move();
    CHECK(bank == Resources{100000 - 2100, 21 * 70, 0});
    CHECK(w.s.colony(home)->orders.size() == 1);
    // Clear Orders switches Repeat off.
    REQUIRE(apply(r, w.s, kA, colonyOrders(home, {}, false)).ok);
    CHECK_FALSE(w.s.colony(home)->repeatOrders);
    // Ships never get the order.
    const VehicleId ship = w.spawn(w.ship(kA, "Ship", 1), at(a, 6, 6));
    CHECK_FALSE(apply(r, w.s, kA, cmd::SetOrders{ship, {}, convert(Resource::Minerals, Resource::Organics, 10), false}).ok);
}

TEST_CASE("colony orders: in a turn-based game Convert Resources runs as it is given, with no log entry") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.s.options.simultaneous = false;
    const ObjectId home = w.planet(a, {6, 6});
    w.colony(home, kA, 1000, {"Mv Converter"});
    w.spawn(w.ship(kB, "Keeper", 1), at(a, 12, 12));  // B stays alive
    resumeTurnBased(r, w.s);
    REQUIRE(w.s.playerTurn.empire == kA);
    Resources& bank = w.s.empire(kA).stockpile;
    bank = {5000, 0, 0};
    const TurnResult res = applyLive(r, w.s, kA, colonyOrders(home, convert(Resource::Minerals, Resource::Radioactives, 3000)));
    CHECK(res.rejected.empty());
    CHECK(bank == Resources{2000, 0, 2100});
    CHECK(w.s.colony(home)->orders.empty());
    CHECK(logged(w, kA, "Resources Converted") == 0);
}

// ---- Colony (planetary) cloaking (docs/spec/01 §6.9) ------------------------------------------------

namespace {

constexpr size_t kPsychic = static_cast<size_t>(SightType::Psychic);

// A system with B's populated colony at (6,6) and A's colony elsewhere in it.
struct CloakWorld {
    World w;
    SystemId a;
    ObjectId hidden;   // B's
    ObjectId mine;     // A's
    CloakWorld(std::initializer_list<std::string_view> facilities = {"Mv Planet Cloak"}) {
        a = w.system("A");
        hidden = w.planet(a, {6, 6});
        w.colony(hidden, kB, 1000, facilities);
        mine = w.planet(a, {1, 1});
        w.colony(mine, kA, 1000);
        w.exploreAll(kA);
        w.exploreAll(kB);
    }
    Colony& colony() { return *w.s.colony(hidden); }
    CommandResult cloak(bool on = true) { return apply(w.rules(), w.s, kB, cmd::CloakColony{hidden, on}); }
};

} // namespace

TEST_CASE("colony cloaking: cloak and sensor levels come from facilities only and are stored until a recalculation") {
    CloakWorld cw({"Mv Planet Cloak", "Mv Planet Eye"});
    const Rules& r = cw.w.rules();
    Colony& c = cw.colony();
    CHECK(c.cloakLevels == std::array<int, kSightTypes>{3, 3, 3, 3, 3});
    CHECK(c.sensorLevels[0] == 1);         // EM Active at least 1
    CHECK(c.sensorLevels[kPsychic] == 4);
    CHECK(sight::colonyCanCloak(c));
    // The planet's own abilities are not read.
    SpaceObject& planet = cw.w.s.galaxy.object(cw.hidden);
    planet.abilities.push_back(abText(AbilityKind::SensorLevel, "Gravitic", 5));
    planet.abilities.push_back(abText(AbilityKind::CloakLevel, "Gravitic", 6));
    sight::recalculateColony(r, c);
    CHECK(c.sensorLevels[static_cast<size_t>(SightType::Gravitic)] == 0);
    CHECK(c.cloakLevels[static_cast<size_t>(SightType::Gravitic)] == 3);
    CHECK(sight::sensorLevels(r, cw.w.s, kB, cw.a)[static_cast<size_t>(SightType::Gravitic)] == 0);
    // A level-1 cloak cannot cloak a colony.
    CloakWorld faint({"Mv Faint Cloak"});
    CHECK_FALSE(sight::colonyCanCloak(faint.colony()));
    CHECK_FALSE(faint.cloak().ok);
    // Facilities lost without a recalculation (the intelligence project
    // Planet - Facility Damage) leave the stored levels as they were.
    REQUIRE(cw.cloak().ok);
    c.facilities.clear();
    CHECK(sight::colonyCanCloak(c));
    CHECK(c.sensorLevels[kPsychic] == 4);
    // Loading a game recalculates: the colony can no longer cloak, so it decloaks, without a message.
    sight::recalculateColonies(r, cw.w.s);
    CHECK_FALSE(c.cloaked);
    CHECK(c.sensorLevels[kPsychic] == 0);
    CHECK(cw.w.s.empire(kB).log.empty());
}

TEST_CASE("colony cloaking: Cloak and Decloak act at once, cost nothing, keep the queue; Scrap Facilities recalculates") {
    CloakWorld cw({"Mv Planet Cloak", "Test Space Yard"});
    const Rules& r = cw.w.rules();
    Colony& c = cw.colony();
    QueueItem item;
    item.kind = QueueItem::Kind::Facility;
    item.facility = test::facilityIndex(r, "Test Mine");
    c.queue.items.push_back(item);
    const Resources bank = cw.w.s.empire(kB).stockpile;
    CHECK_FALSE(cw.cloak(false).ok);  // not cloaked
    REQUIRE(cw.cloak().ok);
    CHECK(c.cloaked);
    CHECK(c.queue.items.size() == 1);  // cloaking a colony keeps its queue
    CHECK(cw.w.s.empire(kB).stockpile == bank);
    CHECK(c.orders.empty());
    CHECK_FALSE(cw.cloak().ok);       // already cloaked
    CHECK_FALSE(apply(r, cw.w.s, kA, cmd::CloakColony{cw.hidden, false}).ok);  // not A's
    // Scrap Facilities recalculates: without its cloaking facility it decloaks.
    REQUIRE(apply(r, cw.w.s, kB, cmd::Scrap{{}, cw.hidden, 0}).ok);
    CHECK_FALSE(c.cloaked);
    CHECK_FALSE(sight::colonyCanCloak(c));
    // Decloak stays possible for a colony marked cloaked that cannot cloak (after a battle).
    c.cloaked = true;
    CHECK(cw.cloak(false).ok);
    // Neither order writes a log entry (the Decloak's contact check may: First Contact).
    for (const LogEntry& l : cw.w.s.empire(kB).log) CHECK(l.title == "First Contact");
}

TEST_CASE("colony cloaking: a cloaked colony hides behind its cloak levels; partners and the omnipresent view get no exception") {
    CloakWorld cw;
    const Rules& r = cw.w.rules();
    REQUIRE(cw.cloak().ok);
    // The planet's obscuration is the cloak level; A's colony (EM Active 1) does not see it.
    CHECK(sight::planetObscuration(r, cw.w.s, cw.hidden) == sight::SightVector{3, 3, 3, 3, 3});
    CHECK_FALSE(sight::canSeeColony(r, cw.w.s, kA, cw.hidden));
    CHECK_FALSE(sight::canSeePlanet(r, cw.w.s, kA, cw.hidden));  // nor is the planet drawn
    CHECK(sight::canSeeColony(r, cw.w.s, kB, cw.hidden));        // its owner always sees it
    CHECK(sight::canSeePlanet(r, cw.w.s, kB, cw.hidden));
    cw.w.s.options.omnipresent = true;
    CHECK_FALSE(sight::canSeeColony(r, cw.w.s, kA, cw.hidden));
    cw.w.s.options.omnipresent = false;
    // Sensors of level 3 in any type reveal it.
    const VehicleId eye = cw.w.spawn(cw.w.ship(kA, "Eye", 1, {"Mv Sensor 3"}), at(cw.a, 2, 2));
    CHECK(sight::canSeeColony(r, cw.w.s, kA, cw.hidden));
    CHECK(sight::canSeePlanet(r, cw.w.s, kA, cw.hidden));
    cw.w.v(eye).count = 0;
    cw.w.s.removeDeadVehicles();
    // Uncloaked, it is seen as before.
    REQUIRE(cw.cloak(false).ok);
    CHECK(sight::canSeeColony(r, cw.w.s, kA, cw.hidden));
    // A cloaked colony stays a full sensor source for its owner.
    REQUIRE(cw.cloak().ok);
    CHECK(sight::hasPresence(r, cw.w.s, kB, cw.a));
}

TEST_CASE("colony cloaking: first contact needs the cloak-aware test; Decloak runs the contact check at once") {
    CloakWorld cw;
    const Rules& r = cw.w.rules();
    REQUIRE(cw.cloak().ok);
    TurnContext ctx{r, cw.w.s, {}, {}, {}};
    sight::updateKnowledge(r, cw.w.s);
    diplomacy::updateContacts(ctx);
    // B sees A's colony, A does not see B's: no mutual detection.
    CHECK_FALSE(cw.w.s.empire(kA).relation(kB).contact);
    REQUIRE(cw.cloak(false).ok);
    CHECK(cw.w.s.empire(kA).relation(kB).contact);
    CHECK(cw.w.s.empire(kB).relation(kA).contact);
}

TEST_CASE("colony cloaking: a colonizer that cannot see the colony finds no planet to colonize") {
    CloakWorld cw;
    const Rules& r = cw.w.rules();
    const DesignId settlerDesign = cw.w.design(kA, "Settler", "Test Frigate", {"Test Bridge", "Test Rock Pod"});
    const VehicleId settler = cw.w.spawn(settlerDesign, at(cw.a, 6, 6));
    CHECK(movement::colonizeProblem(r, cw.w.s, cw.w.v(settler), cw.hidden) == "The planet is already colonized");
    REQUIRE(cw.cloak().ok);
    CHECK(movement::colonizeProblem(r, cw.w.s, cw.w.v(settler), cw.hidden) == "There is no planet here to colonize");
}

TEST_CASE("colony cloaking: a cloaked colony's yard does not work, but it builds facilities at its yard's rate; its counter moves twice") {
    CloakWorld cw({"Mv Planet Cloak", "Test Space Yard"});
    const Rules& r = cw.w.rules();
    Colony& c = cw.colony();
    REQUIRE(cw.cloak().ok);
    CHECK(colonyHasSpaceYard(r, c));
    CHECK_FALSE(colonyHasWorkingYard(r, c));
    CHECK_FALSE(spaceYardAt(r, cw.w.s, kB, locationOf(cw.w.s.galaxy, cw.hidden)));
    // A ship item is dropped from its queue; a facility item builds.
    const DesignId ship = cw.w.ship(kB, "Hull", 1);
    QueueItem shipItem;
    shipItem.design = ship;
    QueueItem mineItem;
    mineItem.kind = QueueItem::Kind::Facility;
    mineItem.facility = test::facilityIndex(r, "Test Mine");
    c.queue.items = {shipItem, mineItem};
    const Resources rate = economy::constructionRate(r, cw.w.s, kB, cmd::QueueTarget{cw.hidden, {}});
    c.cloaked = false;
    CHECK(economy::constructionRate(r, cw.w.s, kB, cmd::QueueTarget{cw.hidden, {}}) == rate);  // the yard's rate counts
    c.cloaked = true;
    TurnContext ctx{r, cw.w.s, {}, {}, {}};
    cw.w.s.empire(kB).stockpile = {100000, 100000, 100000};
    economy::runConstruction(ctx, kB);
    CHECK(c.queue.items.empty());  // the ship item went, the mine was built
    CHECK(std::count(c.facilities.begin(), c.facilities.end(), test::facilityIndex(r, "Test Mine")) == 1);
    // The one-yard limit still refuses a second yard.
    QueueItem yard;
    yard.kind = QueueItem::Kind::Facility;
    yard.facility = test::facilityIndex(r, "Test Space Yard");
    CHECK_FALSE(queueItemProblem(r, cw.w.s, kB, cmd::QueueTarget{cw.hidden, {}}, yard).empty());
    // The emergency counter moves twice a turn.
    c.queue.items.clear();
    c.queue.emergency = true;
    c.queue.emergencyTurns = 0;
    economy::runConstruction(ctx, kB);
    CHECK(c.queue.emergencyTurns == 2);
    // Scrapping and mothballing through it are refused.
    const VehicleId hull = cw.w.spawn(ship, at(cw.a, 6, 6));
    CHECK_FALSE(apply(r, cw.w.s, kB, cmd::Mothball{hull, true}).ok);
    REQUIRE(cw.cloak(false).ok);
    CHECK(apply(r, cw.w.s, kB, cmd::Mothball{hull, true}).ok);
}

TEST_CASE("colony cloaking: a battle decloaks the colony and it cloaks again afterwards, with no can-cloak test") {
    CloakWorld cw;
    const Rules& r = cw.w.rules();
    REQUIRE(cw.cloak().ok);
    // An armed ship of A whose sensors pierce the cloak: the day's check sees the colony.
    cw.w.spawn(cw.w.ship(kA, "Gunboat", 1, {"Mv Sensor 3", "Test Laser"}), at(cw.a, 6, 6));
    const Location there = locationOf(cw.w.s.galaxy, cw.hidden);
    REQUIRE(combat::combatPossible(r, cw.w.s, there));
    TurnContext ctx{r, cw.w.s, {}, {}, {}};
    combat::resolveSpaceCombat(ctx, there);
    REQUIRE(cw.w.s.combats.size() == 1);
    REQUIRE(cw.w.s.colony(cw.hidden));
    CHECK(cw.w.s.colony(cw.hidden)->cloaked);
    // Marked cloaked while it cannot cloak (as after a lost cloaking facility):
    // it cloaks again all the same.
    cw.colony().cloakLevels.fill(1);
    cw.w.s.combats.clear();
    TurnContext again{r, cw.w.s, {}, {}, {}};
    combat::resolveSpaceCombat(again, there);
    REQUIRE(cw.w.s.combats.size() == 1);
    CHECK(cw.w.s.colony(cw.hidden)->cloaked);
}

TEST_CASE("colony cloaking: a cloaked colony is not the uncloaked object a wholly cloaked group's check needs (spec 04 §2)") {
    CloakWorld cw({"Mv Planet Cloak", "Mv Planet Eye"});
    const Rules& r = cw.w.rules();
    const VehicleId sneak = cw.w.spawn(cw.w.ship(kA, "Sneak", 1, {"Mv Cloak"}), at(cw.a, 6, 6));
    cw.w.v(sneak).status = VehicleStatus::Cloaked;
    const Location there = locationOf(cw.w.s.galaxy, cw.hidden);
    // B's colony sees the cloaked ship (Psychic 4 against 3) and, uncloaked, watches.
    CHECK(combat::battleCheck(r, cw.w.s, there, combat::BattleCheck{{sneak}}));
    REQUIRE(cw.cloak().ok);
    CHECK_FALSE(combat::battleCheck(r, cw.w.s, there, combat::BattleCheck{{sneak}}));
}

TEST_CASE("colony cloaking: computer players' colonies decloak for their orders and cloak again afterwards if they can") {
    CloakWorld cw;
    const Rules& r = cw.w.rules();
    Order launch;
    launch.kind = OrderKind::LaunchUnits;
    launch.design = cw.w.design(kB, "Sat", "Test Satellite Hull", {"Test Satellite Gun"});
    launch.amount = -1;
    // A human's colony without the Ship Cloaking minister: untouched.
    cw.colony().orders = {launch};
    cw.w.move();
    CHECK_FALSE(cw.colony().cloaked);
    // A computer player's colony ends up cloaked, whether or not it was before.
    cw.w.s.empire(kB).kind = PlayerKind::Computer;
    cw.colony().orders = {launch};
    cw.w.move();
    CHECK(cw.colony().cloaked);
    // A human's colony under minister control with the Ship Cloaking minister on.
    cw.w.s.empire(kB).kind = PlayerKind::Human;
    cw.colony().cloaked = false;
    cw.colony().minister = true;
    cw.w.s.empire(kB).ministers = ministerBit(Minister::ShipCloaking);
    cw.colony().orders = {launch};
    cw.w.move();
    CHECK(cw.colony().cloaked);
    // One that cannot cloak stays uncloaked.
    cw.colony().cloaked = false;
    cw.colony().cloakLevels.fill(1);
    cw.colony().orders = {launch};
    cw.w.move();
    CHECK_FALSE(cw.colony().cloaked);
    (void)r;
}
