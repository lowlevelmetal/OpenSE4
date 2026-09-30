// Save format: round trips, every command type, corrupt/hostile input,
// checksum stability, save files, and the guard against struct fields that
// were added without being serialized.

#include "engine_fixture.hpp"

#include "core/rng.hpp"
#include "game/commands.hpp"
#include "game/query.hpp"
#include "game/serialize.hpp"
#include "game/serialize_io.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <set>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

// ---- Field guard -------------------------------------------------------------------------------
//
// fieldCount<T>() counts the members an aggregate declares; serializedFields
// counts the members T's io() passes to fields(). They differ when a field
// was added to a struct without adding it to serialize_io.hpp.

namespace opense4::game::serial {
struct FieldCounter {
    static constexpr bool kReading = false;
    size_t count = 0;
};
template <class... T>
void fields(FieldCounter& c, T&...) {
    c.count += sizeof...(T);
}
} // namespace opense4::game::serial

namespace {

struct AnyField {
    template <class T>
    operator T() const;
};

template <class T, class... A>
consteval size_t fieldCount() {
    if constexpr (requires { T{A{}..., AnyField{}}; }) return fieldCount<T, A..., AnyField>();
    else return sizeof...(A);
}

template <class T>
size_t serializedFields() {
    serial::FieldCounter c;
    T value{};
    io(c, value);
    return c.count;
}

#define CHECK_ALL_FIELDS(T)                                                                                                          \
    CHECK_MESSAGE(serializedFields<T>() == fieldCount<T>(), #T " declares " << fieldCount<T>() << " fields but its io() in "        \
                                                                               "serialize_io.hpp lists "                             \
                                                                            << serializedFields<T>())

template <size_t... I>
void checkCommandFields(std::index_sequence<I...>) {
    (
        [] {
            using T = std::variant_alternative_t<I, Command>;
            const Command c{T{}};
            CHECK_MESSAGE(serializedFields<T>() == fieldCount<T>(), "cmd::" << commandName(c) << " declares " << fieldCount<T>()
                                                                             << " fields but its io() in serialize_io.hpp lists "
                                                                             << serializedFields<T>());
        }(),
        ...);
}

} // namespace

TEST_CASE("serialize: every struct field is serialized") {
    CHECK_ALL_FIELDS(ruleset::Ability);
    CHECK_ALL_FIELDS(ruleset::CombatStrategy);
    CHECK_ALL_FIELDS(Location);
    CHECK_ALL_FIELDS(GalaxyPos);
    CHECK_ALL_FIELDS(SpaceObject);
    CHECK_ALL_FIELDS(StarSystem);
    CHECK_ALL_FIELDS(Galaxy);
    CHECK_ALL_FIELDS(StartingPoint);
    CHECK_ALL_FIELDS(QuadrantMap);
    CHECK_ALL_FIELDS(Race);
    CHECK_ALL_FIELDS(Waypoint);
    CHECK_ALL_FIELDS(ResearchProject);
    CHECK_ALL_FIELDS(IntelProjectOrder);
    CHECK_ALL_FIELDS(Relation);
    CHECK_ALL_FIELDS(LogEntry);
    CHECK_ALL_FIELDS(TurnStats);
    CHECK_ALL_FIELDS(EconomyReport);
    CHECK_ALL_FIELDS(Knowledge);
    CHECK_ALL_FIELDS(Empire);
    CHECK_ALL_FIELDS(PopulationGroup);
    CHECK_ALL_FIELDS(UnitStack);
    CHECK_ALL_FIELDS(Cargo);
    CHECK_ALL_FIELDS(QueueItem);
    CHECK_ALL_FIELDS(ConstructionQueue);
    CHECK_ALL_FIELDS(Colony);
    CHECK_ALL_FIELDS(DesignEntry);
    CHECK_ALL_FIELDS(Design);
    CHECK_ALL_FIELDS(Order);
    CHECK_ALL_FIELDS(Vehicle);
    CHECK_ALL_FIELDS(Fleet);
    CHECK_ALL_FIELDS(PackageItem);
    CHECK_ALL_FIELDS(DiplomaticMessage);
    CHECK_ALL_FIELDS(CombatEvent);
    CHECK_ALL_FIELDS(CombatPiece);
    CHECK_ALL_FIELDS(CombatRecord);
    CHECK_ALL_FIELDS(PendingEvent);
    CHECK_ALL_FIELDS(VictoryConditions);
    CHECK_ALL_FIELDS(GameOptions);
    CHECK_ALL_FIELDS(GameState);
    CHECK_ALL_FIELDS(EmpireOrders);
    CHECK_ALL_FIELDS(EmpireSetup);
    CHECK_ALL_FIELDS(GameSetup);
    CHECK_ALL_FIELDS(SaveInfo);
    checkCommandFields(std::make_index_sequence<std::variant_size_v<Command>>{});
}

// ---- A game that exercised many features ------------------------------------------------------------

namespace {

Design warbirdDesign(const Rules& r) {
    Design d;
    d.name = "Warbird";
    d.designType = "Attack Ship";
    d.hull = hullIndex(r, "Test Frigate");
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser", "Test Armor Plate"})
        d.entries.push_back({componentIndex(r, c), -1});
    return d;
}

std::vector<VehicleId> vehiclesOf(const GameState& s, EmpireId e) {
    std::vector<VehicleId> out;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e) out.push_back(v.id);
    return out;
}

// Orders touching queues, fleets, designs, research, messages and lists.
EmpireOrders busyOrders(const Rules& r, const GameState& s, EmpireId me, int round) {
    EmpireOrders o{me, s.turn, {}};
    const EmpireId other{static_cast<uint32_t>((me.index() + 1) % s.empires.size())};
    const Empire& e = s.empire(me);
    ObjectId homePlanet;
    for (const auto& c : s.colonies)
        if (c && c->owner == me && c->homeworld) homePlanet = c->planet;
    const cmd::QueueTarget home{homePlanet, {}};
    const std::vector<VehicleId> ships = vehiclesOf(s, me);

    if (round == 0) {
        Design d = warbirdDesign(r);
        d.name += std::to_string(me.index());
        o.commands.push_back(cmd::CreateDesign{d});
        if (ships.size() >= 2) o.commands.push_back(cmd::CreateFleet{"Home Guard", {ships[0], ships[1]}});
        o.commands.push_back(cmd::SetStrategy{-1, {"Careful", {{"Break Off At", "50"}, {"Target", "Nearest"}}}, false});
        o.commands.push_back(cmd::SetRepairPriorities{{"Ships", "Bases"}});
        o.commands.push_back(cmd::SetDesignTypes{{"Attack Ship", "Scout", "Colony Ship"}});
    }
    if (!e.designs.empty()) {
        QueueItem item;
        item.design = e.designs.front();
        o.commands.push_back(cmd::QueueAdd{home, item, -1});
    }
    QueueItem lab;
    lab.kind = QueueItem::Kind::Facility;
    lab.facility = facilityIndex(r, "Test Lab");
    o.commands.push_back(cmd::QueueAdd{home, lab, 0});
    o.commands.push_back(cmd::QueueFlags{home, false, round % 2 == 1, false, -1});
    o.commands.push_back(cmd::SetResearch{{{techArea(r, "Test Beams"), 0}, {techArea(r, "Test Construction"), 0}}, round % 2 == 0, false});
    o.commands.push_back(cmd::SetIntel{{IntelProjectOrder{0, other, {}, {}, {}, {}, 0}}, true, false});

    DiplomaticMessage m;
    m.to = other;
    m.type = MessageType::General;
    m.text = "Greetings from empire " + std::to_string(me.index()) + ", round " + std::to_string(round);
    PackageItem gift;
    gift.resources = Resources{10, 5, 1};
    m.offer.push_back(gift);
    o.commands.push_back(cmd::SendMessage{m});

    Waypoint wp{"Rally " + std::to_string(round), {SystemId{0u}, Sector{2, 3}}, true};
    o.commands.push_back(cmd::SetWaypoint{round % 10, wp});
    o.commands.push_back(cmd::SetSystemNote{SystemId{0u}, "note " + std::to_string(round)});
    o.commands.push_back(cmd::SetSystemFlags{SystemId{1u}, true, round % 2 == 0});
    o.commands.push_back(cmd::TagMinefield{{SystemId{1u}, Sector{4, 4}}, true});

    if (ships.size() >= 3) {
        Order move;
        move.kind = OrderKind::MoveTo;
        move.location = {SystemId{static_cast<uint32_t>((round + 1) % s.galaxy.systems.size())}, Sector{6, 6}};
        o.commands.push_back(cmd::SetOrders{ships[2], {}, {move}, false});
        o.commands.push_back(cmd::Rename{ships[2], {}, {}, {}, "Wanderer " + std::to_string(round)});
    }
    return o;
}

// A state with some of everything, after several processed turns.
GameState busyGame() {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 3, 12, false);
    for (int round = 0; round < 4; ++round) {
        std::vector<EmpireOrders> orders{busyOrders(r, s, EmpireId{0u}, round)};
        processTurn(r, s, orders);
    }
    // Things today's turn pipeline does not create yet.
    CombatRecord battle;
    battle.turn = s.turn;
    battle.location = {SystemId{2u}, Sector{1, 2}};
    battle.participants = {EmpireId{0u}, EmpireId{1u}};
    battle.pieces.push_back({CombatPiece::Kind::Vehicle, EmpireId{0u}, VehicleId{0u}, {}, DesignId{0u}, "Scout 1", -3, 4});
    battle.pieces.push_back({CombatPiece::Kind::Planet, EmpireId{1u}, {}, ObjectId{3u}, {}, "Planet", 5, -5});
    battle.events.push_back({CombatEvent::Kind::Fire, 2, 0, 1, 1, -1, 25, 7});
    battle.summary = {"A skirmish.", "Nobody won."};
    s.combats.push_back(battle);
    s.pendingEvents.push_back({3, EmpireId{1u}, ObjectId{2u}, {}, SystemId{1u}, s.turn + 5});
    addLog(s, EmpireId{0u}, LogCategory::Combat, "Battle", "Details", Location{SystemId{2u}, Sector{1, 2}}, "Battle1");
    s.empires[1].history.push_back({s.turn, 1234, Resources{1, 2, 3}, 5, 6, 7, 8, 9, 10, 11, 12, 13});
    s.empires[2].knowledge.notes.assign(s.galaxy.systems.size(), "unexplored");
    s.empires[2].race.traits = {1, 4};
    s.empires[0].passwordHash = "0123456789abcdef";
    s.options.victory.score = true;
    s.options.techAreasAllowed.assign(r.data().techAreas.size(), 1);
    s.winner = EmpireId{2u};
    s.peacefulTurns = 17;
    return s;
}

} // namespace

TEST_CASE("serialize: state round trip is byte-identical after several turns") {
    const Rules& r = engineRules();
    GameState s = busyGame();
    REQUIRE(s.turn == 4);
    CHECK_FALSE(s.fleets.empty());
    CHECK(s.designs.size() > s.empires.size() * 2);
    CHECK_FALSE(s.combats.empty());

    const std::vector<uint8_t> bytes = serializeState(s);
    auto loaded = deserializeState(bytes);
    REQUIRE_MESSAGE(loaded.has_value(), (loaded ? std::string{} : loaded.error()));
    CHECK(serializeState(*loaded) == bytes);
    CHECK(stateChecksum(*loaded) == stateChecksum(s));
    CHECK(loaded->rng == s.rng);
    CHECK(loaded->empires[0].passwordHash == "0123456789abcdef");
    CHECK(loaded->vehicles.size() == s.vehicles.size());
    CHECK(loaded->combats.back().summary.size() == 2);  // the battle added above

    // The envelope's checksum is stateChecksum.
    uint64_t sealed = 0;
    for (int i = 0; i < 8; ++i) sealed |= static_cast<uint64_t>(bytes[24 + static_cast<size_t>(i)]) << (8 * i);
    CHECK(sealed == stateChecksum(s));

    // Saving and loading does not change the game's future.
    for (int round = 4; round < 6; ++round) {
        std::vector<EmpireOrders> a{busyOrders(r, s, EmpireId{0u}, round)};
        std::vector<EmpireOrders> b{busyOrders(r, *loaded, EmpireId{0u}, round)};
        CHECK(serializeOrders(a[0]) == serializeOrders(b[0]));
        processTurn(r, s, a);
        processTurn(r, *loaded, b);
        CHECK(stateChecksum(s) == stateChecksum(*loaded));
    }
}

TEST_CASE("serialize: orders round trip for every command type") {
    const Rules& r = engineRules();
    Order order{OrderKind::LoadCargo, {SystemId{3u}, Sector{1, 12}}, ObjectId{4u}, VehicleId{5u}, DesignId{6u}, -1};
    const cmd::QueueTarget yard{ObjectId{9u}, VehicleId{10u}};
    QueueItem item{QueueItem::Kind::Upgrade, DesignId{2u}, 17, 3, Resources{1, 2, 3}};
    Design design = warbirdDesign(r);
    design.obsolete = true;
    design.kills = 4;
    DiplomaticMessage message;
    message.id = MessageId{8u};
    message.from = EmpireId{0u};
    message.to = EmpireId{1u};
    message.type = MessageType::ProposeTrade;
    message.tone = 2;
    message.text = "Trade?";
    message.treaty = Treaty::TradeAlliance;
    PackageItem tech;
    tech.kind = PackageItem::Kind::Technology;
    tech.tech = ruleset::TechAreaId{3u};
    message.offer = {tech};
    PackageItem planet;
    planet.kind = PackageItem::Kind::Planet;
    planet.planet = ObjectId{12u};
    message.request = {planet};
    message.thirdEmpire = EmpireId{2u};
    message.inReplyTo = MessageId{7u};

    EmpireOrders orders{EmpireId{1u}, 42, {}};
    auto& c = orders.commands;
    c.push_back(cmd::SetOrders{VehicleId{1u}, FleetId{2u}, {order, Order{}}, true});
    c.push_back(cmd::CreateFleet{"Strike Group", {VehicleId{3u}, VehicleId{4u}}});
    c.push_back(cmd::JoinFleet{FleetId{5u}, VehicleId{6u}});
    c.push_back(cmd::LeaveFleet{VehicleId{7u}});
    c.push_back(cmd::DisbandFleet{FleetId{8u}});
    c.push_back(cmd::SetFleetOptions{FleetId{9u}, 2, 3});
    c.push_back(cmd::SetVehicleStrategy{DesignId{4u}, 5});
    c.push_back(cmd::Rename{VehicleId{1u}, FleetId{2u}, DesignId{3u}, ObjectId{4u}, "New Name \xE2\x9C\x93"});
    c.push_back(cmd::Scrap{VehicleId{11u}, ObjectId{12u}, 3});
    c.push_back(cmd::Mothball{VehicleId{13u}, false});
    c.push_back(cmd::SetMinister{VehicleId{14u}, ObjectId{15u}, true, false});
    c.push_back(cmd::QueueAdd{yard, item, 2});
    c.push_back(cmd::QueueRemove{yard, 4});
    c.push_back(cmd::QueueMove{yard, 1, 5});
    c.push_back(cmd::QueueSetCount{yard, 2, 9});
    c.push_back(cmd::QueueFlags{yard, true, true, true, 7});
    c.push_back(cmd::Retrofit{VehicleId{16u}, DesignId{17u}});
    c.push_back(cmd::SetColonyType{ObjectId{18u}, "Research"});
    c.push_back(cmd::AbandonPlanet{ObjectId{19u}});
    c.push_back(cmd::TransferCargo{VehicleId{20u}, ObjectId{21u}, VehicleId{22u}, ObjectId{23u}, DesignId{24u}, EmpireId{2u}, 1234567890123});
    c.push_back(cmd::CreateDesign{design});
    c.push_back(cmd::SetDesignObsolete{DesignId{25u}, false});
    c.push_back(cmd::DeleteDesign{DesignId{26u}});
    c.push_back(cmd::SetResearch{{{ruleset::TechAreaId{1u}, 50}, {ruleset::TechAreaId{2u}, 0}}, false, true});
    c.push_back(cmd::SetIntel{{{2, EmpireId{0u}, ObjectId{3u}, VehicleId{4u}, EmpireId{2u}, ruleset::TechAreaId{5u}, 99}}, false, true});
    c.push_back(cmd::SendMessage{message});
    c.push_back(cmd::AnswerMessage{MessageId{27u}, true, "Agreed."});
    c.push_back(cmd::SetWaypoint{3, Waypoint{"Alpha", {SystemId{1u}, Sector{0, 12}}, true}});
    c.push_back(cmd::SetWaypoint{4, std::nullopt});
    c.push_back(cmd::SetSystemFlags{SystemId{28u}, std::nullopt, false});
    c.push_back(cmd::SetSystemNote{SystemId{29u}, "Watch this one"});
    c.push_back(cmd::TagMinefield{{SystemId{30u}, Sector{7, 8}}, false});
    c.push_back(cmd::SetStrategy{2, {"Aggressive", {{"Break Off At", "10"}}}, true});
    c.push_back(cmd::SetRepairPriorities{{"Bases", "Ships", "Units"}});
    c.push_back(cmd::SetDesignTypes{{"Carrier"}});
    c.push_back(cmd::SetColonyTypes{{"Mining", "Farming"}});
    c.push_back(cmd::SetEmpireOptions{true, std::string("verifier")});
    c.push_back(cmd::SetEmpireOptions{std::nullopt, std::nullopt});
    c.push_back(cmd::SetEncounterOptions{EncounterClear::Any});

    std::set<size_t> kinds;
    for (const Command& cmd : c) kinds.insert(cmd.index());
    CHECK_MESSAGE(kinds.size() == std::variant_size_v<Command>, "add the new command type to this test");

    const std::vector<uint8_t> bytes = serializeOrders(orders);
    auto loaded = deserializeOrders(bytes);
    REQUIRE_MESSAGE(loaded.has_value(), (loaded ? std::string{} : loaded.error()));
    CHECK(loaded->empire == orders.empire);
    CHECK(loaded->turn == 42);
    REQUIRE(loaded->commands.size() == c.size());
    for (size_t i = 0; i < c.size(); ++i) CHECK(loaded->commands[i].index() == c[i].index());
    CHECK(serializeOrders(*loaded) == bytes);
    const auto& transfer = std::get<cmd::TransferCargo>(loaded->commands[19]);
    CHECK(transfer.amount == 1234567890123);
    const auto& sent = std::get<cmd::SendMessage>(loaded->commands[25]).message;
    CHECK(sent.request.front().planet == ObjectId{12u});
    CHECK(std::get<cmd::SetOrders>(loaded->commands[0]).orders.front() == order);
    CHECK(std::get<cmd::Rename>(loaded->commands[7]).name == "New Name \xE2\x9C\x93");
}

// ---- Hostile input ------------------------------------------------------------------------------------

namespace {

std::vector<uint8_t> payloadOf(const std::vector<uint8_t>& blob) { return {blob.begin() + kEnvelopeSize, blob.end()}; }

} // namespace

TEST_CASE("serialize: corrupt, truncated and incompatible input is rejected") {
    GameState s = newEngineGame(9, 2, 6);
    const std::vector<uint8_t> good = serializeState(s);
    REQUIRE(deserializeState(good).has_value());

    auto error = [](std::span<const uint8_t> b) {
        auto r = deserializeState(b);
        return r ? std::string("(accepted)") : r.error();
    };
    CHECK(error({}).find("truncated") != std::string::npos);
    CHECK(error(std::span(good).first(20)).find("truncated") != std::string::npos);
    CHECK(error(std::span(good).first(good.size() - 1)).find("truncated") != std::string::npos);

    std::vector<uint8_t> longer = good;
    longer.push_back(0);
    CHECK(error(longer).find("unexpected bytes") != std::string::npos);

    std::vector<uint8_t> magic = good;
    magic[0] = 'X';
    CHECK(error(magic) == "not an OpenSE4 game state");

    const std::vector<uint8_t> orders = serializeOrders(EmpireOrders{EmpireId{0u}, 1, {}});
    CHECK(error(orders) == "this is an OpenSE4 order list, not a game state");

    std::vector<uint8_t> newer = good;
    newer[8] = static_cast<uint8_t>(kSaveVersion + 1);
    CHECK(error(newer).find("newer version") != std::string::npos);
    std::vector<uint8_t> older = good;
    older[8] = 0;
    CHECK(error(older).find("old format") != std::string::npos);

    std::vector<uint8_t> flipped = good;
    flipped[good.size() / 2] ^= 0x40;
    CHECK(error(flipped).find("checksum") != std::string::npos);

    // A valid checksum over a lying payload: a huge list count.
    std::vector<uint8_t> payload = payloadOf(good);
    // GameOptions.quadrantType is the first string, after turn (4) and seed (8).
    payload[12] = payload[13] = payload[14] = payload[15] = 0xff;
    CHECK(error(wrapEnvelope("OSE4STAT", payload)).find("length out of range") != std::string::npos);

    // Every truncation point of the payload, resealed, fails cleanly.
    const std::vector<uint8_t> full = payloadOf(good);
    for (size_t cut = 0; cut < full.size(); cut += 1 + cut / 16) {
        const std::vector<uint8_t> part(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut));
        CHECK_FALSE(deserializeState(wrapEnvelope("OSE4STAT", part)).has_value());
    }
}

TEST_CASE("serialize: states that refer to missing things are rejected") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(9, 2, 6);
    CHECK(validateState(s).empty());
    CHECK(validateState(s, &r).empty());
    CHECK(validateState(busyGame(), &r).empty());

    auto rejected = [](const GameState& bad) {
        auto back = deserializeState(serializeState(bad));
        return back ? std::string("(accepted)") : back.error();
    };
    GameState bad = s;
    bad.vehicles[0].design = DesignId{999u};
    CHECK(rejected(bad).find("inconsistent: vehicle") != std::string::npos);
    bad = s;
    bad.vehicles[0].damage.push_back(0);
    CHECK(rejected(bad).find("damage for") != std::string::npos);
    bad = s;
    std::swap(bad.vehicles[0], bad.vehicles[1]);
    CHECK(rejected(bad).find("not sorted") != std::string::npos);
    bad = s;
    bad.vehicles[0].location.system = SystemId{77u};
    CHECK(rejected(bad) != "(accepted)");
    bad = s;
    bad.empires[1].relations.pop_back();
    CHECK(rejected(bad).find("relations") != std::string::npos);
    bad = s;
    bad.colonies.push_back(std::nullopt);
    bad.colonies.resize(bad.galaxy.objects.size() + 1);
    CHECK(rejected(bad) != "(accepted)");
    bad = s;
    bad.winner = EmpireId{5u};
    CHECK(rejected(bad).find("winner") != std::string::npos);

    // Data-set indices need the rules.
    bad = s;
    bad.designs[0].hull = 5000;
    CHECK(validateState(bad).empty());
    CHECK(validateState(bad, &r).find("hull") != std::string::npos);
    bad = s;
    bad.empires[0].research.push_back({ruleset::TechAreaId{9999u}, 0});
    CHECK(validateState(bad, &r).find("tech area") != std::string::npos);
}

TEST_CASE("serialize: random corruption never crashes the reader") {
    GameState s = newEngineGame(13, 2, 6);
    const std::vector<uint8_t> state = payloadOf(serializeState(s));
    EmpireOrders o{EmpireId{0u}, 3, {}};
    o.commands.push_back(cmd::CreateFleet{"Fleet", {VehicleId{1u}, VehicleId{2u}}});
    o.commands.push_back(cmd::SetResearch{{{ruleset::TechAreaId{1u}, 5}}, true, false});
    o.commands.push_back(cmd::SetStrategy{-1, {"S", {{"a", "b"}}}, false});
    const std::vector<uint8_t> orders = payloadOf(serializeOrders(o));

    Rng rng(2024);
    int accepted = 0;
    for (int i = 0; i < 600; ++i) {
        const bool useState = i % 3 != 0;
        std::vector<uint8_t> p = useState ? state : orders;
        const int edits = rng.rangeInt(1, 6);
        for (int e = 0; e < edits; ++e) {
            const size_t at = rng.below(p.size());
            switch (rng.below(4)) {
                case 0: p[at] = static_cast<uint8_t>(rng.below(256)); break;
                case 1: p[at] = 0xff; break;
                case 2: p.erase(p.begin() + static_cast<std::ptrdiff_t>(at)); break;
                default: p.insert(p.begin() + static_cast<std::ptrdiff_t>(at), static_cast<uint8_t>(rng.below(256))); break;
            }
            if (p.empty()) p.push_back(0);
        }
        if (useState) accepted += deserializeState(wrapEnvelope("OSE4STAT", p)).has_value();
        else accepted += deserializeOrders(wrapEnvelope("OSE4ORDR", p)).has_value();
    }
    CHECK(accepted < 600);  // most corruptions are detected; none crash
}

// ---- Checksums -------------------------------------------------------------------------------------------

namespace {

// A small hand-built state, independent of galaxy generation.
GameState goldenState() {
    GameState g;
    g.turn = 12;
    g.seed = 0x1234;
    g.options.quadrantType = "Test Quadrant";
    g.galaxy.width = 20;
    g.galaxy.height = 15;
    StarSystem sys;
    sys.id = SystemId{0u};
    sys.name = "Aster";
    sys.position = {3, -4};
    sys.objects = {ObjectId{0u}};
    g.galaxy.systems.push_back(sys);
    SpaceObject planet;
    planet.id = ObjectId{0u};
    planet.system = SystemId{0u};
    planet.sector = Sector{2, 9};
    planet.name = "Aster I";
    planet.value = {50, 60, 70};
    planet.abilities.push_back({"Test Ability", "", "1", ""});
    g.galaxy.objects.push_back(planet);
    Empire e;
    e.id = EmpireId{0u};
    e.name = "Golden";
    e.stockpile = {100, 200, 300};
    e.techLevels = {1, 2, 3};
    e.relations.resize(1);
    e.strategies.push_back({"Default", {{"k", "v"}}});
    g.empires.push_back(e);
    Colony c;
    c.planet = ObjectId{0u};
    c.owner = EmpireId{0u};
    c.population.push_back({EmpireId{0u}, 250});
    g.colonies.emplace_back(c);
    Design d;
    d.id = DesignId{0u};
    d.owner = EmpireId{0u};
    d.name = "Probe";
    d.entries = {{1, -1}, {2, 3}};
    g.designs.push_back(d);
    Vehicle v;
    v.owner = EmpireId{0u};
    v.design = DesignId{0u};
    v.name = "Probe 1";
    v.damage = {0, 5};
    v.orders.push_back(Order{OrderKind::Explore, {}, {}, {}, {}, 0});
    g.addVehicle(v);
    g.rng.reseed(77);
    return g;
}

} // namespace

TEST_CASE("serialize: checksums are stable") {
    const GameState g = goldenState();
    GameState copy = g;
    CHECK(stateChecksum(copy) == stateChecksum(g));
    // Golden values: the format is the same on every platform and run. When
    // a field is added to a serialized struct these change: bump kSaveVersion
    // in serialize.hpp if older files can no longer be read, then paste the
    // new values printed below.
    constexpr uint64_t kGoldenChecksum = 0xdfef78446189fcffull;
    constexpr size_t kGoldenSize = 1587;
    CHECK_MESSAGE(stateChecksum(g) == kGoldenChecksum,
                  "save format changed: kGoldenChecksum = " << std::format("{:#x}", stateChecksum(g)) << "ull");
    CHECK_MESSAGE(serializeState(g).size() == kGoldenSize, "save format changed: kGoldenSize = " << serializeState(g).size());

    copy.rng.next();
    CHECK(stateChecksum(copy) != stateChecksum(g));
    copy = g;
    copy.empires[0].name = "Golden2";
    CHECK(stateChecksum(copy) != stateChecksum(g));
    copy = g;
    copy.colonies[0]->population[0].millions += 1;
    CHECK(stateChecksum(copy) != stateChecksum(g));
}

// ---- Save files --------------------------------------------------------------------------------------------

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / ("opense4_test_" + std::to_string(Rng(reinterpret_cast<uintptr_t>(this)).next()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("serialize: save files") {
    const Rules& r = engineRules();
    TempDir dir;
    GameState s = busyGame();
    SaveInfo info;
    info.gameName = "Round Trip";
    info.dataSet = dataSetIdentity(r);
    info.gameId = 0xfeedbeef;
    info.players = {"alice", "", ""};
    info.masterPasswordVerifier = "abc";
    const auto file = dir.path / "saves" / "round_trip.gam";
    REQUIRE(saveGame(file, s, info).has_value());
    CHECK_FALSE(std::filesystem::exists(dir.path / "saves" / "round_trip.gam.tmp"));

    auto loaded = loadGame(file);
    REQUIRE_MESSAGE(loaded.has_value(), (loaded ? std::string{} : loaded.error()));
    CHECK(serializeState(loaded->first) == serializeState(s));
    const SaveInfo& back = loaded->second;
    CHECK(back.gameName == "Round Trip");
    CHECK(back.turn == s.turn);
    CHECK(back.empires.size() == s.empires.size());
    CHECK(back.empires[0] == s.empires[0].name);
    CHECK(back.gameId == 0xfeedbeef);
    CHECK(back.players[0] == "alice");
    CHECK(back.masterPasswordVerifier == "abc");
    CHECK(sameDataSet(back.dataSet, dataSetIdentity(r)));

    auto header = readSaveInfo(file);
    REQUIRE(header.has_value());
    CHECK(header->turn == s.turn);

    // Overwrite in place.
    std::vector<EmpireOrders> none;
    processTurn(r, s, none);
    REQUIRE(saveGame(file, s, info).has_value());
    CHECK(readSaveInfo(file)->turn == s.turn);

    // Damaged files report the file name and the problem.
    auto bytes = readFileBytes(file);
    REQUIRE(bytes.has_value());
    const auto cut = dir.path / "cut.gam";
    REQUIRE(writeFileAtomic(cut, std::span(*bytes).first(bytes->size() / 2)).has_value());
    auto bad = loadGame(cut);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().find("cut.gam") != std::string::npos);
    CHECK(bad.error().find("truncated") != std::string::npos);
    CHECK_FALSE(loadGame(dir.path / "missing.gam").has_value());

    const auto notSave = dir.path / "orders.gam";
    const auto ordersBytes = serializeOrders(EmpireOrders{});
    REQUIRE(writeFileAtomic(notSave, ordersBytes).has_value());
    auto wrongKind = loadGame(notSave);
    REQUIRE_FALSE(wrongKind.has_value());
    CHECK(wrongKind.error().find("order list, not a saved game") != std::string::npos);
}

TEST_CASE("serialize: data set identity") {
    const Rules& r = engineRules();
    const std::string id = dataSetIdentity(r);
    CHECK(id == dataSetIdentity(r));
    CHECK(id.find('#') != std::string::npos);
    CHECK(sameDataSet(id, "elsewhere/Data#" + id.substr(id.find('#') + 1)));
    CHECK_FALSE(sameDataSet(id, "x#0000000000000000"));

    ruleset::Ruleset changed = buildEngineRuleset();
    changed.components.back().name += " Mk2";
    const Rules other{std::move(changed)};
    CHECK_FALSE(sameDataSet(id, dataSetIdentity(other)));
}
