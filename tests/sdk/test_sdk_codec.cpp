// The SDK's command codec (src/sdk/codec.hpp): every command, order and
// tactical order kind round trips, decoding errors name the bad field, and a
// decoded command changes the game exactly as the original does.

#include "command_samples.hpp"
#include "engine_fixture.hpp"
#include "sdk/sdk_test_util.hpp"

#include "game/ai.hpp"
#include "game/commands.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "sdk/codec.hpp"
#include "sdk/names.hpp"
#include "sdk/value_io.hpp"

#include <doctest/doctest.h>

#include <cctype>
#include <set>
#include <utility>

using namespace opense4;
using namespace opense4::game;
using opense4::script::Value;
using opense4::script::ValueList;
using opense4::script::ValueMap;
using namespace opense4::sdktest;

// Converts to any field type; only declared, for unevaluated contexts (as in test_serialize.cpp).
namespace sdk_codec_test {
struct AnyField {
    template <class T>
    operator T() const;
};
} // namespace sdk_codec_test

namespace {

using sdk_codec_test::AnyField;

template <class T, class... A>
consteval size_t fieldCount() {
    if constexpr (requires { T{A{}..., AnyField{}}; }) return fieldCount<T, A..., AnyField>();
    else return sizeof...(A);
}

template <class T>
size_t codecFields() {
    sdk::detail::FieldCounter c;
    T value{};
    fields(c, value);
    return c.count;
}

#define CHECK_CODEC_FIELDS(T)                                                                                                       \
    CHECK_MESSAGE(codecFields<T>() == fieldCount<T>(), #T " declares " << fieldCount<T>() << " fields but its fields() in "        \
                                                                            "src/sdk/value_io.hpp lists "                         \
                                                                         << codecFields<T>())

template <size_t... I>
void checkCommandFieldCounts(std::index_sequence<I...>) {
    (
        [] {
            using T = std::variant_alternative_t<I, Command>;
            const Command c{T{}};
            const Value v = sdk::encodeCommand(c);
            CHECK_MESSAGE(v.size() == fieldCount<T>() + 1, sdk::commandKindName(c) << " declares " << fieldCount<T>()
                                                                                   << " fields; its encoding has " << v.size()
                                                                                   << " keys besides the kind");
            CHECK(sdk::commandKindNames()[I] == sdk::snakeCase(commandName(c)));
        }(),
        ...);
}

// Exact equality of commands: their save-format encodings.
std::vector<uint8_t> bytesOf(const Command& c) { return serializeOrders(EmpireOrders{EmpireId{0u}, 0, {c}}); }

Value map(std::initializer_list<std::pair<std::string, Value>> entries) { return Value(ValueMap(entries)); }
Value list(std::initializer_list<Value> items) { return Value(ValueList(items)); }

std::string decodeError(const Value& v) {
    auto d = sdk::decodeCommand(v);
    return d ? std::string("(accepted)") : d.error().text();
}

template <class E>
void checkNames() {
    std::set<std::string_view> seen;
    for (std::string_view n : sdk::enumNames<E>()) {
        CHECK_MESSAGE(seen.insert(n).second, "the name " << n << " is used twice");
        CHECK_FALSE(n.empty());
        for (char ch : n) CHECK_MESSAGE((std::islower(static_cast<unsigned char>(ch)) || ch == '_'), n << " is not lower_snake_case");
        const auto back = sdk::parseEnum<E>(n);
        REQUIRE(back);
        CHECK(sdk::enumName(*back) == n);
    }
}

} // namespace

TEST_CASE("sdk codec: every command kind round trips exactly") {
    const Rules& r = test::engineRules();
    const EmpireOrders orders = test::everyCommandSample(r);
    std::set<size_t> kinds;
    for (const Command& c : orders.commands) {
        kinds.insert(c.index());
        INFO(sdk::commandKindName(c));
        const Value v = sdk::encodeCommand(c);
        CHECK(at(v, "kind").asString() == sdk::commandKindName(c));
        const auto decoded = sdk::decodeCommand(v);
        REQUIRE_MESSAGE(decoded.has_value(), (decoded ? std::string{} : decoded.error().text()));
        CHECK(decoded->index() == c.index());
        CHECK(sdk::encodeCommand(*decoded) == v);
        CHECK(bytesOf(*decoded) == bytesOf(c));
    }
    CHECK_MESSAGE(kinds.size() == sdk::kCommandKinds, "everyCommandSample misses a command kind");

    // A whole turn, and a list of commands.
    const Value turn = sdk::encodeEmpireOrders(orders);
    CHECK(intAt(turn, "empire") == 1);
    CHECK(intAt(turn, "turn") == 42);
    const auto back = sdk::decodeEmpireOrders(turn);
    REQUIRE_MESSAGE(back.has_value(), (back ? std::string{} : back.error().text()));
    CHECK(serializeOrders(*back) == serializeOrders(orders));
    const auto listed = sdk::decodeCommands(sdk::encodeCommands(orders.commands));
    REQUIRE(listed.has_value());
    CHECK(listed->size() == orders.commands.size());
}

TEST_CASE("sdk codec: every field of every command and of what it carries is encoded") {
    checkCommandFieldCounts(std::make_index_sequence<std::variant_size_v<Command>>{});
    CHECK_CODEC_FIELDS(Order);
    CHECK_CODEC_FIELDS(Sector);
    CHECK_CODEC_FIELDS(QueueItem);
    CHECK_CODEC_FIELDS(cmd::QueueTarget);
    CHECK_CODEC_FIELDS(Design);
    CHECK_CODEC_FIELDS(DesignEntry);
    CHECK_CODEC_FIELDS(ResearchProject);
    CHECK_CODEC_FIELDS(IntelProjectOrder);
    CHECK_CODEC_FIELDS(PackageItem);
    CHECK_CODEC_FIELDS(DiplomaticMessage);
    CHECK_CODEC_FIELDS(Waypoint);
    CHECK_CODEC_FIELDS(ruleset::CombatStrategy);
    CHECK_CODEC_FIELDS(InterfaceOptions);
    CHECK_CODEC_FIELDS(PopulationGroup);
    CHECK_CODEC_FIELDS(UnitStack);
    CHECK_CODEC_FIELDS(Cargo);
    CHECK_CODEC_FIELDS(VictoryConditions);
    CHECK_CODEC_FIELDS(EmpireOrders);
    CHECK_CODEC_FIELDS(combat::TacticalOrder);
    CHECK_CODEC_FIELDS(combat::Square);
    // Location and Resources spell out their parts: a location's sector as x
    // and y, the three resources by name.
    CHECK(codecFields<Location>() == 3);
    CHECK(codecFields<Resources>() == 3);
}

TEST_CASE("sdk codec: the names of enumerations are unique lower_snake_case words") {
    checkNames<OrderKind>();
    checkNames<StellarAction>();
    checkNames<Resource>();
    checkNames<Treaty>();
    checkNames<MessageType>();
    checkNames<PackageItem::Kind>();
    checkNames<QueueItem::Kind>();
    checkNames<cmd::DemandList>();
    checkNames<EncounterClear>();
    checkNames<Minister>();
    checkNames<combat::TacticalOrder::Kind>();
    checkNames<VehicleStatus>();
    checkNames<ObjectKind>();
    checkNames<PlayerKind>();
    checkNames<LogCategory>();
    checkNames<LogGoto>();
    checkNames<Mood>();
    checkNames<ruleset::VehicleType>();
    checkNames<ruleset::WeaponKind>();
    checkNames<Characteristic>();
    checkNames<SightType>();
    checkNames<Aggregation>();
    checkNames<economy::ConditionsBand>();
    checkNames<CombatPiece::Kind>();
    CHECK(sdk::enumName(OrderKind::MoveToWaypoint) == "move_to_waypoint");
    CHECK(sdk::enumName(Treaty::TradeResearchAlliance) == "trade_research_alliance");
    CHECK(sdk::snakeCase("QueueReplaceFacility") == "queue_replace_facility");
    CHECK_FALSE(sdk::commandKindIndex("SetOrders").has_value());   // kinds are lower_snake_case only
    CHECK(sdk::commandKindIndex("set_orders") == size_t{0});
}

TEST_CASE("sdk codec: every order kind round trips, alone and in an order list") {
    for (size_t k = 0; k < static_cast<size_t>(OrderKind::Count); ++k) {
        const auto kind = static_cast<OrderKind>(k);
        INFO(sdk::enumName(kind));
        Order o;
        o.kind = kind;
        o.location = {SystemId{static_cast<uint32_t>(k % 5)}, Sector{static_cast<int>(k % 13), 12 - static_cast<int>(k % 13)}};
        o.object = ObjectId{static_cast<uint32_t>(100 + k)};
        o.vehicle = k % 2 ? VehicleId{static_cast<uint32_t>(7 + k)} : VehicleId{};
        o.design = k % 3 ? DesignId{static_cast<uint32_t>(k)} : DesignId{};
        o.amount = kind == OrderKind::StellarManipulation ? static_cast<int>(StellarAction::CreateBlackHole)
                   : kind == OrderKind::LoadCargo         ? -1
                                                          : static_cast<int>(k * 1000);
        if (kind == OrderKind::ConvertResources) {
            o.from = static_cast<uint8_t>(Resource::Organics);
            o.to = static_cast<uint8_t>(Resource::Radioactives);
        }
        const Value v = sdk::encodeOrder(o);
        CHECK(at(v, "kind").asString() == sdk::enumName(kind));
        const auto back = sdk::decodeOrder(v);
        REQUIRE_MESSAGE(back.has_value(), (back ? std::string{} : back.error().text()));
        CHECK(*back == o);

        const Command c = cmd::SetOrders{VehicleId{3u}, {}, {o, o}, true, {}};
        const auto viaCommand = sdk::decodeCommand(sdk::encodeCommand(c));
        REQUIRE(viaCommand.has_value());
        CHECK(std::get<cmd::SetOrders>(*viaCommand).orders == std::vector<Order>{o, o});
    }
    // Convert Resources names its resources; a value no resource has (an order
    // that does nothing) stays a number.
    Order convert{OrderKind::ConvertResources};
    convert.from = 1;
    convert.to = 9;
    const Value v = sdk::encodeOrder(convert);
    CHECK(at(v, "from_resource") == Value("organics"));
    CHECK(at(v, "to_resource") == Value(9));
    CHECK(*sdk::decodeOrder(v) == convert);
}

TEST_CASE("sdk codec: tactical orders round trip") {
    using K = combat::TacticalOrder::Kind;
    for (size_t k = 0; k <= static_cast<size_t>(K::ResolveCombat); ++k) {
        combat::TacticalOrder o;
        o.kind = static_cast<K>(k);
        o.empire = EmpireId{static_cast<uint32_t>(k % 3)};
        o.piece = static_cast<int>(k);
        o.target = static_cast<int>(k) - 1;
        o.weapon = 2;
        o.instance = -1;
        o.x = 10;
        o.y = 4;
        if (o.kind == K::Move) o.path = {{1, 2}, {2, 3}};
        o.design = DesignId{5u};
        o.count = 8;
        o.group = 3;
        o.formation = 1;
        o.on = k % 2 == 0;
        o.alone = k % 3 == 0;
        INFO(sdk::enumName(o.kind));
        const Value v = sdk::encodeTacticalOrder(o);
        CHECK(at(v, "kind").asString() == sdk::enumName(o.kind));
        const auto back = sdk::decodeTacticalOrder(v);
        REQUIRE_MESSAGE(back.has_value(), (back ? std::string{} : back.error().text()));
        CHECK(*back == o);
    }
    const auto bad = sdk::decodeTacticalOrder(map({{"kind", "fire"}, {"path", list({map({{"x", 1}, {"y", 99999}})})}}));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().text() == "path[0].y: 99999 is out of range (-32768 to 32767)");
}

TEST_CASE("sdk codec: decoding errors name the path to the bad value") {
    CHECK(decodeError(Value(3)) == "expected a command (a map with a kind)");
    CHECK(decodeError(map({{"vehicle", 1}})) == "kind: missing: every command names its kind");
    CHECK(decodeError(map({{"kind", 7}})) == "kind: expected a command kind name");
    CHECK(decodeError(map({{"kind", "SetOrders"}})) == "kind: 'SetOrders' is not a command kind");
    CHECK(decodeError(map({{"kind", "leave_fleet"}, {"vehical", 3}})) == "vehical: unknown field");
    CHECK(decodeError(map({{"kind", "leave_fleet"}, {"vehicle", "the red one"}})) == "vehicle: expected a vehicle id (or null)");
    CHECK(decodeError(map({{"kind", "leave_fleet"}, {"vehicle", -2}})) == "vehicle: expected a vehicle id (or null)");
    CHECK(decodeError(map({{"kind", "set_fleet_options"}, {"formation", -1}})) == "formation: -1 is out of range (0 to 4294967295)");
    CHECK(decodeError(map({{"kind", "mothball"}, {"mothball", 1}})) == "mothball: expected true or false");
    CHECK(decodeError(map({{"kind", "rename"}, {"name", 5}})) == "name: expected text");
    CHECK(decodeError(map({{"kind", "set_orders"}, {"orders", map({})}})) == "orders: expected a list");
    const Value orders = list({map({{"kind", "move_to"}}), map({{"kind", "sentry"}}), map({{"kind", "attack"}, {"object", "planet"}})});
    CHECK(decodeError(map({{"kind", "set_orders"}, {"orders", orders}})) == "orders[2].object: expected an object id (or null)");
    CHECK(decodeError(map({{"kind", "set_orders"}, {"orders", list({map({{"kind", "fly"}})})}})) == "orders[0].kind: 'fly' is not an order kind");
    CHECK(decodeError(map({{"kind", "set_orders"}, {"orders", list({map({{"kind", 3}})})}})) == "orders[0].kind: expected an order kind name");
    CHECK(decodeError(map({{"kind", "queue_add"}, {"target", map({{"planet", 4}, {"ship", 2}})}})) == "target.ship: unknown field");
    CHECK(decodeError(map({{"kind", "queue_add"}, {"item", map({{"spent", map({{"minerals", "lots"}})}})}})) ==
          "item.spent.minerals: expected a whole number");
    CHECK(decodeError(map({{"kind", "tag_minefield"}, {"location", map({{"system", 1}, {"x", 300}})}})) ==
          "location.x: 300 is out of range (-128 to 127)");
    CHECK(decodeError(map({{"kind", "set_ministers"}, {"areas", list({"research", "dancing"})}})) == "areas[1]: 'dancing' is not a minister");
    CHECK(decodeError(map({{"kind", "set_interface_options"}, {"options", map({{"planets_sort", list({1, 2})}})}})) ==
          "options.planets_sort: expected a list of 5");
    // Lists of commands name the command's place first.
    const auto many = sdk::decodeCommands(list({map({{"kind", "leave_fleet"}}), map({{"kind", "disband_fleet"}, {"fleet", true}})}));
    REQUIRE_FALSE(many.has_value());
    CHECK(many.error().text() == "[1].fleet: expected a fleet id (or null)");
    const auto turn = sdk::decodeEmpireOrders(map({{"empire", 0}, {"commands", list({map({{"kind", "nothing"}})})}}));
    REQUIRE_FALSE(turn.has_value());
    CHECK(turn.error().text() == "commands[0].kind: 'nothing' is not a command kind");
    CHECK(turn.error().path == "commands[0].kind");
}

TEST_CASE("sdk codec: missing fields keep their defaults, null ids are none") {
    const auto leave = sdk::decodeCommand(map({{"kind", "leave_fleet"}, {"vehicle", 12}}));
    REQUIRE(leave.has_value());
    CHECK(std::get<cmd::LeaveFleet>(*leave).vehicle == VehicleId{12u});

    const Value move = map({{"kind", "move_to"}, {"location", map({{"system", 2}, {"x", 3}, {"y", 4}})}});
    const auto orders = sdk::decodeCommand(map({{"kind", "set_orders"}, {"fleet", 5}, {"vehicle", Value()}, {"orders", list({move})}}));
    REQUIRE(orders.has_value());
    const auto& so = std::get<cmd::SetOrders>(*orders);
    CHECK(so.fleet == FleetId{5u});
    CHECK_FALSE(so.vehicle.valid());
    CHECK_FALSE(so.planet.valid());
    CHECK_FALSE(so.repeat);
    REQUIRE(so.orders.size() == 1);
    CHECK(so.orders[0].kind == OrderKind::MoveTo);
    CHECK(so.orders[0].location == Location{SystemId{2u}, Sector{3, 4}});
    CHECK_FALSE(so.orders[0].object.valid());

    // Optional fields: null leaves the setting as it is.
    const auto ministers = sdk::decodeCommand(map({{"kind", "set_ministers"}, {"areas", list({"research", "politics"})}, {"style", Value()}}));
    REQUIRE(ministers.has_value());
    const auto& sm = std::get<cmd::SetMinisters>(*ministers);
    CHECK(sm.areas == (ministerBit(Minister::Research) | ministerBit(Minister::Politics)));
    CHECK_FALSE(sm.style.has_value());
    CHECK_FALSE(sm.completeAi.has_value());
}

TEST_CASE("sdk codec: decoded commands change the game exactly as the originals do") {
    const Rules& r = test::engineRules();
    GameState a = test::newEngineGame(5, 3, 12, false);
    test::addHomeShips(a, r);
    GameState b = a;
    TurnOptions opts;
    opts.aiForMissing = false;
    int applied = 0;
    for (int round = 0; round < 4; ++round) {
        std::vector<std::pair<EmpireId, Command>> commands;
        for (const Command& c : test::busyOrders(r, a, EmpireId{0u}, round).commands) commands.emplace_back(EmpireId{0u}, c);
        for (const Empire& e : a.empires)
            for (const Command& c : ai::planTurnReport(r, a, e.id).commands) commands.emplace_back(e.id, c);
        for (const auto& [e, c] : commands) {
            const auto decoded = sdk::decodeCommand(sdk::encodeCommand(c));
            REQUIRE_MESSAGE(decoded.has_value(), (decoded ? std::string{} : decoded.error().text()));
            const CommandResult ra = apply(r, a, e, c);
            const CommandResult rb = apply(r, b, e, *decoded);
            CHECK(ra.ok == rb.ok);
            CHECK(ra.error == rb.error);
            applied += ra.ok;
        }
        REQUIRE(stateChecksum(a) == stateChecksum(b));
        processTurn(r, a, {}, opts);
        processTurn(r, b, {}, opts);
        REQUIRE(stateChecksum(a) == stateChecksum(b));
    }
    CHECK(applied > 40);
}
