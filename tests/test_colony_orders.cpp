// Colony orders: Convert Resources (docs/spec/02 §5.6) and the colony's own
// order list (spec 03 §8, §12).

#include "movement_fixture.hpp"

#include "game/commands.hpp"
#include "game/economy.hpp"
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
