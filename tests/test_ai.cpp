// Computer player and ministers (docs/spec/05 §7): the commands the AI
// produces and the memory its turn step keeps. A small fake world below moves
// ships to their destinations (and, for the long games, finishes queue items
// and research faster) to walk the AI through a game without the rest of the
// turn pipeline.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/ai_planner.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/generate.hpp"
#include "game/intel.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

template <class T>
const T* as(const Command& c) {
    return std::get_if<T>(&c);
}

template <class T>
int countOf(const std::vector<Command>& cmds) {
    int n = 0;
    for (const Command& c : cmds) n += std::holds_alternative<T>(c);
    return n;
}

// Applies commands one by one; returns the refusals.
std::vector<std::string> applyAll(const Rules& r, GameState& s, EmpireId e, const std::vector<Command>& cmds) {
    std::vector<std::string> failed;
    for (const Command& c : cmds) {
        const CommandResult res = apply(r, s, e, c);
        if (!res.ok) failed.push_back(std::format("{}: {}", commandName(c), res.error));
    }
    return failed;
}

std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += s + "; ";
    return out;
}

EmpireSetup computerSetup(std::string name) {
    EmpireSetup e;
    e.name = std::move(name);
    e.kind = PlayerKind::Computer;
    return e;
}

GameState computerGame(uint64_t seed, int computers, int neutrals, int systems, const Rules& r = engineRules()) {
    GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = systems;
    setup.options.simultaneous = true;  // written for simultaneous turns (new games are turn-based)
    for (int i = 0; i < computers + neutrals; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = i < computers ? PlayerKind::Computer : PlayerKind::Neutral;
        setup.empires.push_back(std::move(e));
    }
    auto g = createGame(r, setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    return std::move(*g);
}

void exploreEverything(GameState& s) {
    for (Empire& e : s.empires) {
        e.knowledge.explored.assign(s.galaxy.systems.size(), 1);
        e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 1);
    }
}

void meet(GameState& s, EmpireId a, EmpireId b) {
    s.empire(a).relation(b).contact = true;
    s.empire(b).relation(a).contact = true;
}

// Stand-in for movement: vehicles jump to where their orders lead, colony
// ships found colonies, and everyone stays in contact so the computers talk
// to each other.
void fakeMovement(const Rules& r, GameState& s) {
    for (Vehicle& v : s.vehicles) {
        const std::vector<Order> orders = v.orders;  // a fleet member's copy of its fleet's orders
        for (const Order& o : orders) {
            switch (o.kind) {
                case OrderKind::MoveTo:
                case OrderKind::Attack: v.location = o.location; break;
                case OrderKind::Warp: {
                    const SpaceObject& wp = s.galaxy.object(o.object);
                    if (!wp.destination.valid()) break;
                    v.location = locationOf(s.galaxy, wp.destination);
                    Empire& e = s.empire(v.owner);
                    e.knowledge.knownWarpLink.resize(s.galaxy.objects.size(), 0);
                    e.knowledge.knownWarpLink[o.object.index()] = 1;
                    e.knowledge.knownWarpLink[wp.destination.index()] = 1;
                    break;
                }
                case OrderKind::Colonize:
                    if (!s.colony(o.object)) {
                        Colony c;
                        c.planet = o.object;
                        c.owner = v.owner;
                        c.population.push_back({v.owner, 10});
                        c.foundedTurn = s.turn;
                        s.colonies[o.object.index()] = c;
                    }
                    v.count = 0;
                    break;
                default: break;
            }
        }
        v.orders.clear();
        fleetMemberMoved(s, v);
    }
    s.removeDeadVehicles();
    sight::updateKnowledge(r, s);
    for (Empire& e : s.empires)
        for (size_t i = 0; i < e.relations.size(); ++i) e.relations[i].contact = i != e.id.index();
}

// Fake movement plus a faster game: the first item of every queue finishes
// and the first research project gains a level every other turn.
void fakeProgress(const Rules& r, GameState& s) {
    for (auto& c : s.colonies) {
        if (!c || c->queue.items.empty() || c->totalPopulation() == 0) continue;
        const QueueItem item = c->queue.items.front();
        c->queue.items.erase(c->queue.items.begin());
        switch (item.kind) {
            case QueueItem::Kind::Vehicle:
                if (isUnitType(r.hull(s.design(item.design).hull).type)) {
                    c->cargo.units.push_back({item.design, item.count});
                } else {
                    const EmpireId owner = c->owner;
                    const Location at = locationOf(s.galaxy, c->planet);
                    movement::spawnVehicle(r, s, owner, item.design, at);
                }
                break;
            case QueueItem::Kind::Facility: c->facilities.push_back(item.facility); break;
            case QueueItem::Kind::Upgrade: {
                const int family = r.facility(item.facility).family;
                if (auto latest = r.latestFacilityOfFamily(s.empire(c->owner), family))
                    for (uint32_t& f : c->facilities)
                        if (r.facility(f).family == family) f = *latest;
                break;
            }
        }
    }
    if (s.turn % 2 == 0)
        for (Empire& e : s.empires)
            if (!e.research.empty()) {
                const auto a = e.research.front().area;
                if (e.techLevels[a.index()] < r.tech(a).maxLevel) ++e.techLevels[a.index()];
            }
    fakeMovement(r, s);
}

// A readable dump of everything the AI influences, for determinism checks.
std::string digest(const GameState& s) {
    std::ostringstream o;
    o << "turn " << s.turn << "\n";
    for (const Empire& e : s.empires) {
        o << e.name << " state " << e.aiState << "/" << e.aiTurnsInState << " timer " << e.aiMemory.afterAttack << " research";
        for (const auto& p : e.research) o << " " << p.area.value;
        o << " anger";
        for (const auto& rel : e.relations) o << " " << rel.anger << ":" << static_cast<int>(rel.treaty);
        o << " designs";
        for (DesignId d : e.designs) o << " [" << s.design(d).name << (s.design(d).obsolete ? "*" : "") << "]";
        o << " intel " << e.intel.size() << " strategies " << e.strategies.size() << " tech";
        for (int l : e.techLevels) o << " " << l;
        o << "\n";
    }
    for (const Vehicle& v : s.vehicles) {
        o << "v" << v.id.value << " " << v.owner.value << " " << v.design.value << " @" << v.location.system.value << ":"
          << int{v.location.sector.x} << "," << int{v.location.sector.y} << " f" << v.fleet.value << " orders";
        for (const Order& ord : v.orders) o << " " << static_cast<int>(ord.kind) << ":" << ord.object.value << ":" << ord.vehicle.value;
        o << "\n";
    }
    for (const Fleet& f : s.fleets) {
        o << "fleet " << f.id.value << " " << f.owner.value << " n" << f.members.size() << " orders";
        for (const Order& ord : fleetOrders(s, f)) o << " " << static_cast<int>(ord.kind) << ":" << ord.object.value;
        o << "\n";
    }
    for (const auto& c : s.colonies) {
        if (!c) continue;
        o << "colony " << c->planet.value << " " << c->owner.value << " " << c->colonyType << " f" << c->facilities.size() << " q";
        for (const QueueItem& q : c->queue.items) o << " " << static_cast<int>(q.kind) << ":" << q.design.value << ":" << q.facility;
        o << "\n";
    }
    o << "messages " << s.messages.size() << "\n";
    return o.str();
}

std::string runComputerGame(uint64_t seed, int turns, std::vector<std::string>* rejected) {
    const Rules& r = engineRules();
    GameState s = computerGame(seed, 3, 1, 14);
    std::string trace;
    for (int t = 0; t < turns; ++t) {
        std::vector<EmpireOrders> none;
        const TurnResult res = processTurn(r, s, none);
        if (rejected)
            for (const auto& [e, why] : res.rejected) rejected->push_back(std::format("turn {} empire {}: {}", t, e.value, why));
        fakeProgress(r, s);
        trace += digest(s);
    }
    return trace;
}

// A fake install in a scratch directory of its own (removed afterwards).
struct TempTree {
    test::TempDir dir;
    std::filesystem::path root;
    explicit TempTree(std::string_view tag) : dir(std::format("ai_test_{}", tag)), root(dir.path()) {}
    void write(const std::filesystem::path& rel, std::string_view body) const {
        std::filesystem::create_directories((root / rel).parent_path());
        std::ofstream f(root / rel, std::ios::binary);
        f << "Test file written by opense4 tests.\n*BEGIN*\n" << body << "\n*END*\n";
    }
    void writePlain(const std::filesystem::path& rel, std::string_view body) const {
        std::filesystem::create_directories((root / rel).parent_path());
        std::ofstream f(root / rel, std::ios::binary);
        f << body;
    }
};

// Every tech area at its maximum.
void researchEverything(const Rules& r, Empire& e) {
    for (size_t i = 0; i < e.techLevels.size(); ++i) e.techLevels[i] = r.data().techAreas[i].maxLevel;
}

DesignId addWarship(GameState& s, const Rules& r, EmpireId owner, std::string_view name) {
    const DesignId d = addTestDesign(s, r, owner, name, "Test Frigate",
                                     {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser"});
    s.design(d).designType = "Attack Ship";
    return d;
}

// A colony ship for the race's own planet type at the empire's homeworld (a
// new game gives no ships, spec 01 §3.6).
VehicleId addColonyShip(GameState& s, const Rules& r, EmpireId owner) {
    const std::string_view surface = s.empire(owner).race.nativeSurface;
    const std::string_view pod = surface == "Ice" ? "Test Ice Pod" : surface == "Rock" ? "Test Rock Pod" : "Test Gas Pod";
    const DesignId d = addTestDesign(s, r, owner, std::format("Colonizer {}", owner.value), "Test Frigate",
                                     {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Supply Pod", pod});
    return addTestVehicle(s, r, d, locationOf(s.galaxy, homeworld(s, owner).planet)).id;
}

} // namespace

// ---- Data ----------------------------------------------------------------------------------------

TEST_CASE("ai: state names match rows by substring, case-sensitive") {
    ai::AiState st;
    REQUIRE(ai::parseAiState("Defend (Short Term)", st));
    CHECK(st == ai::AiState::DefendShortTerm);
    CHECK(ai::displayName(ai::AiState::SecureHoldings) == "Secure Holdings After Attack");
    CHECK(ai::parseStateList("Exploration, Attack,Not Connected, Bogus") ==
          (ai::maskOf(ai::AiState::Exploration) | ai::maskOf(ai::AiState::Attack) | ai::maskOf(ai::AiState::NotConnected)));
    // "Attack" also occurs in the other two names (spec 05 §7.5).
    const auto secure = ai::parseStateList("Secure Holdings After Attack, Incursion");
    CHECK((secure & ai::maskOf(ai::AiState::Attack)) != 0);
    CHECK((secure & ai::maskOf(ai::AiState::SecureHoldings)) != 0);
    CHECK((ai::parseStateList("Prepare for Attack") & ai::maskOf(ai::AiState::Attack)) != 0);
    CHECK(ai::parseStateList("exploration") == 0);  // case-sensitive
    Treaty t;
    REQUIRE(ai::parseTreatyName("Trade and Research Alliance", t));
    CHECK(t == Treaty::TradeResearchAlliance);
    CHECK(ai::angerKeyName(MessageType::Gift) == "Give Gift");
}

TEST_CASE("ai: the fixed design and colony types") {
    CHECK(ai::aiDesignTypes().size() == 39);
    CHECK(ai::isAiDesignType("attack ship"));
    CHECK(ai::isAiDesignType("Colony (Gas)"));
    CHECK_FALSE(ai::isAiDesignType("Scout"));
    CHECK(ai::parseColonyType("Imperial Center") == ai::ColonyType::Homeworld);
    CHECK(ai::parseColonyType("Refining Colony") == ai::ColonyType::Refining);
    CHECK(ai::parseColonyType("Balanced") == ai::ColonyType::Count);
}

TEST_CASE("ai: built-in profile covers the tables") {
    const ai::AiProfile& p = ai::builtinProfile();
    CHECK(p.design("Attack Ship"));
    CHECK(p.design("Colony (Ice)"));
    CHECK_FALSE(p.design("Scout"));  // there is no scout design type
    for (ai::AiState s : {ai::AiState::Exploration, ai::AiState::Infrastructure, ai::AiState::PrepareForAttack, ai::AiState::Attack,
                          ai::AiState::SecureHoldings, ai::AiState::DefendShortTerm, ai::AiState::NotConnected})
        CHECK(p.vehicleQueue(s));
    // The last matching table wins: "Attack" also occurs in "Secure Holdings After Attack".
    CHECK(p.vehicleQueue(ai::AiState::Attack) == p.vehicleQueue(ai::AiState::SecureHoldings));
    CHECK(p.facilityQueue(ai::AiState::Attack, "Homeworld"));
    CHECK_FALSE(p.facilityQueue(ai::AiState::Attack, "Balanced"));  // no fallback row
    CHECK_FALSE(p.planetTypes.empty());
    CHECK(p.speech.pool("Send Propose Treaty"));
    CHECK(p.sources == std::vector<std::string>{"built-in"});
    CHECK(&ai::profileFor(engineRules(), "Anything") == &p);
}

TEST_CASE("ai: one lookup rule for all twelve tables") {
    TempTree t("lookup");
    t.write("Ai/Default_AI_Anger.txt", "Per Attack Location := 7\nRegular Decrease := -9\nReceive Declare War := 33\n");
    t.write("Ai/Default_AI_Research.txt",
            "AI State := Exploration, Infrastructure\nTech Area Name := Test Beams\nTech Area Level := 3\nTech Area Min Percent := 50\n"
            "AI State := Attack\nTech Area Name := Test Armor\nTech Area Level := 9999\nTech Area Min Percent := 25\n"
            "AI State := Attack\nTech Area Name := Test Shields\n");
    t.write("Ai/Default_AI_Construction_Vehicles.txt",
            "AI State := Exploration\nNum Queue Entries := 2\nEntry 1 Type := Attack Ship\nEntry 1 Planet Per Item := 10\n"
            "Entry 1 Must Have At Least := 3\nEntry 2 Type := Colonizer\nEntry 2 Planet Per Item := 0\nEntry 2 Must Have At Least := 1\n");
    t.write("Ai/Default_AI_Speech.txt", "Number of Send Declare War := 2\nSend Declare War 1 := Line one.\nSend Declare War 2 := Line two.\n"
                                        "Number of Mega Evil Declarations := 1\nMega Evil Declaration 1 := Beware.\n");
    t.write("Ai/Default_AI_Politics.txt", "Highest Allowed Treaty := Trade Alliance\nPropose Treaty Type Count := 1\n"
                                          "Propose Treaty Type 1 := Non-Aggression\nPropose Treaty Type 1 Anger Level Below Computed := 4\n"
                                          "Will Accept From Enemy Leave planet := True\n");
    t.write("Ai/Default_AI_Settings.txt", "Maximum Systems to Defend at a Time := 6\n");
    t.write("Ai/Aggressive/Aggressive_AI_Anger.txt", "Regular Decrease := -1\n");
    t.write("Pictures/Races/Testian/Testian_AI_General.txt", "Name := Testian\nRace Opt 1 Num Characteristics := 0\n");
    t.write("Pictures/Races/Testian/Testian_AI_Anger.txt", "Regular Decrease := -4\n");
    t.write("Pictures/Races/Testian/Testian_AI_Settings.txt", "Personality Group := 3\nTurns to Wait until next attack := 11\n");
    t.write("Pictures/Races/Testian/testian_ai_construction_vehicles.txt",
            "AI State := Attack\nNum Queue Entries := 1\nEntry 1 Type := Defense Base\nEntry 1 Must Have At Least := 1\n");

    const ai::AiProfile race = ai::loadProfile(t.root, "Testian");
    CHECK(race.anger.regularDecrease == -4);        // the race file
    CHECK(race.anger.perAttackLocation == ai::builtinProfile().anger.perAttackLocation);  // a missing key keeps its default
    CHECK(race.settings.personalityGroup == 3);
    CHECK(race.settings.turnsBetweenAttacks == 11);
    CHECK(race.settings.maxMaintenancePercent == 80);  // absent keys: the spec's defaults
    CHECK(race.settings.maxSystemsToDefend == 3);
    CHECK(race.settings.maxResearchPoints == 300000);
    REQUIRE(race.research.size() == 3);             // from Ai/Default
    CHECK(race.research[0].area == "Test Beams");
    CHECK(race.research[0].states == (ai::maskOf(ai::AiState::Exploration) | ai::maskOf(ai::AiState::Infrastructure)));
    CHECK(race.research[1].level == 9999);
    CHECK(race.research[2].level == 0);             // absent keys are 0
    CHECK(race.research[2].minPercent == 0);
    // A race folder may replace any table, construction included.
    REQUIRE(race.vehicles.size() == 1);
    REQUIRE(race.vehicles[0].entries.size() == 1);
    CHECK(race.vehicles[0].entries[0].type == "Defense Base");
    REQUIRE(race.speech.pool("Send Declare War"));
    CHECK(race.speech.pool("Send Declare War")->size() == 2);
    CHECK(race.speech.pool("Mega Evil Declarations")->front() == "Beware.");
    CHECK(race.politics.highestAllowedTreaty == Treaty::TradeAlliance);
    REQUIRE(race.politics.proposeTypes.size() == 1);
    CHECK(race.politics.proposeTypes[0] == std::pair{Treaty::NonAggression, 4});
    CHECK(race.politics.demands[static_cast<size_t>(MessageType::DemandLeavePlanet)].acceptFromEnemy);

    const ai::AiProfile other = ai::loadProfile(t.root, "Nobody");
    CHECK(other.anger.regularDecrease == -9);       // Ai/Default
    CHECK(other.anger.receive[static_cast<size_t>(MessageType::DeclareWar)] == 33);
    CHECK(other.settings.maxSystemsToDefend == 6);
    REQUIRE(other.vehicles.size() == 1);
    CHECK(other.vehicles[0].entries.size() == 2);

    // A minister style reads its own folder, then Ai/Default: never the race folder.
    const ai::AiProfile minister = ai::loadProfile(t.root, "Testian", "Aggressive");
    CHECK(minister.anger.regularDecrease == -1);
    CHECK(minister.settings.personalityGroup == 0);
    CHECK(minister.settings.maxSystemsToDefend == 6);
    REQUIRE(minister.vehicles.size() == 1);
    CHECK(minister.vehicles[0].entries.size() == 2);

    // Rules with this root cache one profile per race and style; an empire
    // with a minister style uses the style.
    Rules rules{buildEngineRuleset(), t.root};
    const ai::AiProfile& cached = ai::profileFor(rules, "Testian");
    CHECK(&cached == &ai::profileFor(rules, "testian"));
    CHECK(cached.anger.regularDecrease == -4);
    Empire e;
    e.race.style = "Testian";
    e.ministerStyle = "Aggressive";
    CHECK(ai::profileFor(rules, e).anger.regularDecrease == -1);
}

TEST_CASE("ai: random races fill the personality groups toward their shares") {
    TempTree t("random");
    for (std::string_view race : {"Alpha", "Beta", "Gamma", "Delta"}) {
        const int group = race == "Alpha" || race == "Beta" ? 1 : race == "Gamma" ? 2 : 0;
        t.write(std::format("Pictures/Races/{0}/{0}_AI_General.txt", race), std::format("Name := {}\n", race));
        t.write(std::format("Pictures/Races/{0}/{0}_AI_Settings.txt", race), std::format("Personality Group := {}\n", group));
    }
    t.write("Pictures/RaceNeutral/Omega/Omega_AI_General.txt", "Name := Omega\n");
    ruleset::Ruleset data = buildEngineRuleset();
    data.settings.set("Random Player Personality Groups", "2");
    data.settings.set("Random Player Personality Group 1 Percent", "50");
    data.settings.set("Random Player Personality Group 2 Percent", "50");
    data.settings.set("Minimum Computer Player Medium Setting", "5");
    data.settings.set("Maximum Computer Player Medium Setting", "5");
    data.settings.set("Minimum Neutral Player Low Setting", "1");
    data.settings.set("Maximum Neutral Player Low Setting", "1");
    const Rules rules{std::move(data), t.root};
    REQUIRE(rules.racePresets().size() == 5);
    Rng rng(5);
    // Empty game: both groups at 0 %, the lowest group wins. Then group 1 is at
    // 50 % of two empires, so group 2 (0 %) comes next; then group 1 again.
    const auto computers = ai::randomComputerPresets(rules, 1, false, rng);
    REQUIRE(computers.size() == 4);  // races are never drawn twice: Omega is neutral
    CHECK((computers[0] == "Alpha" || computers[0] == "Beta"));
    CHECK(computers[1] == "Gamma");
    CHECK((computers[2] == "Alpha" || computers[2] == "Beta"));
    CHECK(computers[2] != computers[0]);
    CHECK(computers[3] == "Delta");  // no qualifying group has a race left: any unused race
    const auto neutrals = ai::randomComputerPresets(rules, 0, true, rng);
    CHECK(neutrals == std::vector<std::string>{"Omega"});
    // A race already in the game is never drawn.
    const ruleset::RacePreset* pick = ai::pickRandomRace(rules, rng, false, {"Alpha", "Beta", "Gamma", "Delta"});
    CHECK(pick == nullptr);
}

TEST_CASE("ai: a random computer player's race uses the Race Opt of the racial-point level") {
    TempTree t("raceopt");
    t.write("Pictures/Races/Opto/Opto_AI_General.txt",
            "Name := Opto\n"
            "Race Opt 1 Num Characteristics := 2\nRace Opt 1 Characteristic 1 Type := Intelligence\nRace Opt 1 Characteristic 1 Amount := 250\n"
            "Race Opt 1 Characteristic 2 Type := Reproduction\nRace Opt 1 Characteristic 2 Amount := 300\n"
            "Race Opt 1 Num Advanced Traits := 2\nRace Opt 1 Adv Trait 1 := Day Eyes\nRace Opt 1 Adv Trait 2 := Night Eyes\n"
            "Race Opt 2 Num Characteristics := 1\nRace Opt 2 Characteristic 1 Type := Reproduction\nRace Opt 2 Characteristic 1 Amount := 300\n");
    ruleset::Ruleset data = buildEngineRuleset();
    data.settings.set("Characteristic Intelligence Pct Cost", "10");
    data.settings.set("Characteristic Reproduction Pct Cost", "10");
    const Rules rules{std::move(data), t.root};
    const ruleset::RacePreset* preset = findPreset(rules, "Opto");
    REQUIRE(preset);
    // 2000 points: Race Opt 1. Intelligence 250 costs 1500; Reproduction 300
    // would bring it to 3500 and goes back to 100. Day Eyes (400) fits, Night
    // Eyes (500) would pass 2000 and ends the list.
    Rng rng(3);
    const Race low = ai::randomPlayerRace(rules, *preset, 2000, rng);
    CHECK(low.characteristic(Characteristic::Intelligence) == 250);
    CHECK(low.characteristic(Characteristic::Reproduction) == 100);
    CHECK(low.traits.size() == 1);
    CHECK(racialPointCost(rules, low) <= 2000);
    // 3000 points: Race Opt 2.
    const Race mid = ai::randomPlayerRace(rules, *preset, 3000, rng);
    CHECK(mid.characteristic(Characteristic::Reproduction) == 300);
    CHECK(mid.characteristic(Characteristic::Intelligence) == 100);
    // No racial points: no set is used.
    const Race none = ai::randomPlayerRace(rules, *preset, 0, rng);
    CHECK(none.characteristic(Characteristic::Intelligence) == 100);
    CHECK(none.traits.empty());
}

// ---- Research and intelligence ----------------------------------------------------------------

TEST_CASE("ai: research follows AI_Research and stops at a share total of 100") {
    TempTree t("research");
    t.write("Ai/Default_AI_Research.txt",
            "AI State := Exploration\nTech Area Name := Test Beams\nTech Area Level := 4\nTech Area Min Percent := 50\n"
            "AI State := Exploration\nTech Area Name := Test Armor\nTech Area Level := 3\nTech Area Min Percent := 50\n"
            "AI State := Exploration\nTech Area Name := Test Economics\nTech Area Level := 3\nTech Area Min Percent := 25\n");
    Rules rules{buildEngineRuleset(), t.root};
    GameSetup setup;
    setup.seed = 5;
    setup.options.systemCount = 8;
    for (int i = 0; i < 2; ++i) setup.empires.push_back(computerSetup(std::format("E{}", i)));
    auto g = createGame(rules, setup);
    REQUIRE(g);
    GameState& s = *g;
    REQUIRE(s.empire(EmpireId{0u}).economy.research > 0);
    const auto cmds = ai::planTurn(rules, s, EmpireId{0u});
    const cmd::SetResearch* research = nullptr;
    for (const Command& c : cmds)
        if (auto* x = as<cmd::SetResearch>(c)) research = x;
    REQUIRE(research);
    // Beams (50) and Armor (50) reach 100: the third row waits.
    REQUIRE(research->queue.size() == 2);
    CHECK(research->queue[0].area == techArea(rules, "Test Beams"));
    CHECK(research->queue[1].area == techArea(rules, "Test Armor"));
    CHECK_FALSE(research->evenly);
    CHECK_FALSE(research->repeat);

    // Nothing new while 4 or more projects are queued and nothing happened last turn.
    Empire& e = s.empire(EmpireId{0u});
    e.research = {{techArea(rules, "Test Physics"), 0}, {techArea(rules, "Test Construction"), 0}, {techArea(rules, "Test Propulsion"), 0},
                  {techArea(rules, "Test Units"), 0}};
    e.researchEvenly = false;
    s.turn = 3;
    CHECK(countOf<cmd::SetResearch>(ai::planTurn(rules, s, EmpireId{0u})) == 0);
    // A research event last turn opens the queue again (the Min Percent of
    // rows for areas already queued counts toward the total).
    addLog(s, EmpireId{0u}, LogCategory::Research, "New Tech Level");
    s.turn = 4;
    const auto again = ai::planTurn(rules, s, EmpireId{0u});
    REQUIRE(countOf<cmd::SetResearch>(again) == 1);
    for (const Command& c : again)
        if (auto* x = as<cmd::SetResearch>(c)) CHECK(x->queue.size() == 6);
}

TEST_CASE("ai: research queues the first requirement of the first mine sweeper every fifth turn after meeting mines") {
    TempTree t("sweeping");
    t.write("Ai/Default_AI_Research.txt", "AI State := Exploration\nTech Area Name := Test Armor\nTech Area Level := 1\nTech Area Min Percent := 100\n");
    ruleset::Ruleset rs = buildEngineRuleset();
    for (auto& c : rs.components)
        if (c.name == "Test Mine Sweeper") c.requirements.push_back({*rs.findTechArea("Test Physics"), 5});  // a second requirement
    const Rules r{std::move(rs), t.root};
    GameSetup setup;
    setup.seed = 3;
    setup.options.systemCount = 8;
    setup.options.simultaneous = true;  // the dates below are a simultaneous game's
    for (int i = 0; i < 2; ++i) setup.empires.push_back(computerSetup(std::format("E{}", i)));
    auto g = createGame(r, setup);
    REQUIRE(g);
    GameState& s = *g;
    const EmpireId cpu{1u};
    Empire& e = s.empire(cpu);
    e.techLevels[techArea(r, "Test Units").index()] = 0;
    e.techLevels[techArea(r, "Test Physics").index()] = 0;
    e.aiMemory.metMinefield = true;
    e.research.clear();
    REQUIRE(e.researchPool > 0);
    auto queued = [&](const GameState& g2, std::string_view area) {
        for (const Command& c : ai::planTurn(r, g2, cpu))
            if (auto* x = as<cmd::SetResearch>(c))
                for (const ResearchProject& p : x->queue)
                    if (p.area == techArea(r, area)) return true;
        return false;
    };
    s.turn = 4;  // processed with date 5
    CHECK(queued(s, "Test Units"));
    CHECK_FALSE(queued(s, "Test Physics"));  // only the first requirement
    s.turn = 5;  // date 6: not a fifth turn
    CHECK_FALSE(queued(s, "Test Units"));
    // The gate reads the research pool.
    s.turn = 4;
    s.empire(cpu).researchPool = 0;
    CHECK(countOf<cmd::SetResearch>(ai::planTurn(r, s, cpu)) <= 1);
    CHECK_FALSE(queued(s, "Test Units"));
    // Without research rows there is no mine sweeping.
    GameState plain = computerGame(3, 2, 0, 8);
    plain.empire(cpu).techLevels[techArea(engineRules(), "Test Units").index()] = 0;
    plain.empire(cpu).aiMemory.metMinefield = true;
    plain.turn = 4;
    for (const Command& c : ai::planTurn(engineRules(), plain, cpu))
        if (auto* x = as<cmd::SetResearch>(c))
            for (const ResearchProject& p : x->queue) CHECK_FALSE(p.area == techArea(engineRules(), "Test Units"));
}

TEST_CASE("ai: intelligence targets the angriest empire below Non-Aggression") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 3, 10, false);
    const EmpireId cpu{1u}, a{0u}, b{2u};
    meet(s, cpu, a);
    meet(s, cpu, b);
    Empire& e = s.empire(cpu);
    researchEverything(r, e);
    e.economy.intelligence = 100000;
    e.relation(a).anger = 70;
    e.relation(b).anger = 90;
    e.relation(b).treaty = Treaty::NonAggression;  // a friend is never a target
    const auto cmds = ai::planTurn(r, s, cpu);
    const cmd::SetIntel* intel = nullptr;
    for (const Command& c : cmds)
        if (auto* x = as<cmd::SetIntel>(c)) intel = x;
    REQUIRE(intel);
    CHECK_FALSE(intel->queue.empty());
    CHECK(intel->queue.size() <= 10);
    for (const IntelProjectOrder& o : intel->queue) CHECK((o.target == a || !o.target.valid()));
    // Projects against a new friend are dropped.
    e.intel = intel->queue;
    e.relation(a).treaty = Treaty::TradeAlliance;
    e.relation(b).treaty = Treaty::TradeAlliance;
    const auto later = ai::planTurn(r, s, cpu);
    for (const Command& c : later)
        if (auto* x = as<cmd::SetIntel>(c))
            for (const IntelProjectOrder& o : x->queue) CHECK(o.target != a);
}

// ---- Commands are valid ------------------------------------------------------------------------

TEST_CASE("ai: every planned command is accepted by the rules") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(21, 3, 14, true);
    TurnOptions opts;
    opts.aiForMissing = false;
    for (int turn = 0; turn < 25; ++turn) {
        for (const Empire& e : s.empires) {
            const ai::PlanReport plan = ai::planTurnReport(r, s, e.id);
            const auto failed = applyAll(r, s, e.id, plan.commands);
            CHECK_MESSAGE(failed.empty(), "turn " << turn << " empire " << e.id.value << ": " << joined(failed));
        }
        std::vector<EmpireOrders> none;
        processTurn(r, s, none, opts);
        fakeProgress(r, s);
    }
    // The AI got somewhere: more colonies, facilities and designs than at the start.
    int colonies = 0;
    for (const auto& c : s.colonies) colonies += c.has_value();
    CHECK(colonies > 3);
    for (const Empire& e : s.empires) CHECK(e.designs.size() > 4);
}

TEST_CASE("ai: an all-computer game runs 60 turns deterministically") {
    std::vector<std::string> rejected;
    const std::string a = runComputerGame(99, 60, &rejected);
    CHECK_MESSAGE(rejected.empty(), joined(rejected));
    const std::string b = runComputerGame(99, 60, nullptr);
    CHECK(a == b);
    const std::string c = runComputerGame(100, 5, nullptr);
    CHECK(a.substr(0, c.size()) != c);  // another seed plays differently
}

// ---- Exploration and colonization -----------------------------------------------------------------

TEST_CASE("ai: idle attack ships explore the frontier") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, true);
    const EmpireId me{0u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId warship = addWarship(s, r, me, "Picket");
    const VehicleId explorer = addTestVehicle(s, r, warship, home).id;
    const auto cmds = ai::planTurn(r, s, me);
    bool explored = false;
    std::vector<ObjectId> targets;
    for (const Command& c : cmds) {
        const auto* o = as<cmd::SetOrders>(c);
        if (!o || o->orders.empty() || o->orders.back().kind != OrderKind::Warp) continue;
        const SpaceObject& wp = s.galaxy.object(o->orders.back().object);
        CHECK(s.empire(me).hasExplored(wp.system));
        targets.push_back(o->orders.back().object);
        explored = explored || o->vehicle == explorer;
    }
    CHECK(explored);
    // With fewer explorers than free points, no two take the same one.
    std::sort(targets.begin(), targets.end());
    CHECK(std::adjacent_find(targets.begin(), targets.end()) == targets.end());
}

TEST_CASE("ai: a colony ship moves to the best target and colonizes it") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u};
    const Colony& home = homeworld(s, me);
    const SystemId homeSys = s.galaxy.object(home.planet).system;
    for (ObjectId o : s.galaxy.system(homeSys).objects) {
        SpaceObject& obj = s.galaxy.object(o);
        if (obj.kind == ObjectKind::Planet && !s.colony(o)) {
            obj.surface = s.empire(me).race.nativeSurface;
            break;
        }
    }
    addColonyShip(s, r, me);
    const auto cmds = ai::planTurn(r, s, me);
    bool found = false;
    for (const Command& c : cmds) {
        const auto* o = as<cmd::SetOrders>(c);
        if (!o || o->orders.empty() || o->orders.back().kind != OrderKind::Colonize) continue;
        found = true;
        const Vehicle* v = s.vehicle(o->vehicle);
        REQUIRE(v);
        const DesignStats st = computeDesignStats(r, &s.empire(me), s.design(v->design));
        const SpaceObject& target = s.galaxy.object(o->orders.back().object);
        CHECK(target.kind == ObjectKind::Planet);
        CHECK(s.colony(target.id) == nullptr);
        CHECK(st.canColonize(target.surface));
        CHECK(s.empire(me).hasExplored(target.system));
        CHECK(o->orders.back().location == locationOf(s.galaxy, target.id));
        // The Colonize order travels itself; a Move To in front would make it
        // take its colonists aboard at the target instead of at the yard.
        CHECK(o->orders.size() == 1);
    }
    CHECK(found);
}

TEST_CASE("ai: a colony ship takes colonists aboard where it starts, so the new colony is populated") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u};
    s.empire(me).kind = PlayerKind::Computer;
    const Colony& home = homeworld(s, me);
    const SystemId homeSys = s.galaxy.object(home.planet).system;
    // A settleable planet in the home system, away from the homeworld's sector.
    ObjectId target;
    for (ObjectId o : s.galaxy.system(homeSys).objects) {
        SpaceObject& obj = s.galaxy.object(o);
        if (obj.kind == ObjectKind::Planet && !s.colony(o) && obj.sector != s.galaxy.object(home.planet).sector) {
            obj.surface = s.empire(me).race.nativeSurface;
            target = o;
            break;
        }
    }
    REQUIRE(target.valid());
    TurnOptions opts;
    opts.aiForMissing = false;  // only the computer player acts
    std::vector<EmpireOrders> none;
    ObjectId founded;
    for (int t = 0; t < 20 && !founded.valid(); ++t) {
        processTurn(r, s, none, opts);
        for (const auto& c : s.colonies)
            if (c && c->owner == me && !c->homeworld) founded = c->planet;
    }
    REQUIRE(founded.valid());
    CHECK(s.colony(founded)->totalPopulation() > 0);
}

// ---- Construction ---------------------------------------------------------------------------------

TEST_CASE("ai: research, construction and designs on the first turn") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12, true);
    const EmpireId me{1u};
    const auto cmds = ai::planTurn(r, s, me);
    const cmd::SetResearch* research = nullptr;
    for (const Command& c : cmds)
        if (auto* x = as<cmd::SetResearch>(c)) research = x;
    REQUIRE(research);
    CHECK_FALSE(research->queue.empty());
    for (const ResearchProject& p : research->queue) {
        CHECK(r.techVisible(s, s.empire(me), p.area));
        CHECK(s.empire(me).techLevel(p.area) < r.tech(p.area).maxLevel);
    }
    // A new game gives no designs (spec 01 §3.6): the Design minister makes
    // them in the first turn, and Ship Construction queues from them.
    CHECK(s.empire(me).designs.empty());
    CHECK(countOf<cmd::CreateDesign>(cmds) >= 1);
    for (const Command& c : cmds)
        if (const auto* d = as<cmd::CreateDesign>(c)) {
            CHECK(ai::isAiDesignType(d->design.designType));  // never a scout
            CHECK(computeDesignStats(r, &s.empire(me), d->design).problems.empty());
        }
    CHECK(applyAll(r, s, me, cmds).empty());
    CHECK_FALSE(homeworld(s, me).queue.items.empty());
    // Then it rests while nothing is new (date 5, no research event), and
    // looks again every tenth date: turn 9 is processed with date 10.
    s.empire(me).techLevels[techArea(r, "Test Propulsion").index()] = 3;  // a better engine, unannounced
    s.turn = 4;
    CHECK(countOf<cmd::CreateDesign>(ai::planTurn(r, s, me)) == 0);
    s.turn = 9;
    CHECK(countOf<cmd::CreateDesign>(ai::planTurn(r, s, me)) >= 1);
}

TEST_CASE("ai: ship construction spends one turn of net income on queues under 5 turns") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12, false);
    const EmpireId cpu{1u};
    Empire& e = s.empire(cpu);
    const DesignId warship = addWarship(s, r, cpu, "Picket");
    e.designs.push_back(warship);
    const Resources perShip = computeDesignStats(r, &e, s.design(warship)).cost;
    // Net income of exactly one ship per turn, and a queue far faster than that.
    e.economy = {};
    e.economy.colonies = perShip;
    e.economy.maintenance = Resources{};
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 9);
    p.state = ai::AiState::Infrastructure;
    ai::detail::planShips(p);
    const Colony& home = *p.st.colony(homeworld(s, cpu).planet);
    int ships = 0;
    for (const QueueItem& q : home.queue.items) ships += q.kind == QueueItem::Kind::Vehicle;
    CHECK(ships >= 1);
    CHECK(ships <= 2);  // the budget is spent after what the first items take this turn

    // Over the soft maintenance cap only colony ships (and warp point openers) are built.
    GameState s2 = newEngineGame(3, 2, 12, false);
    Empire& e2 = s2.empire(cpu);
    e2.designs.push_back(addWarship(s2, r, cpu, "Picket"));
    e2.economy = {};
    e2.economy.colonies = Resources{100000, 100000, 100000};
    e2.economy.maintenance = Resources{95000, 0, 0};  // above 80 % and 90 %, not above 100 % of revenue
    ai::detail::Planner q(r, s2, cpu, ai::detail::Mode::Computer, 9);
    q.state = ai::AiState::Infrastructure;
    CHECK(q.overCap(0));
    CHECK(q.overCap(10));
    CHECK_FALSE(q.overCap(20));
    ai::detail::planShips(q);
    for (const auto& c : q.st.colonies)
        if (c && c->owner == cpu)
            for (const QueueItem& item : c->queue.items)
                if (item.kind == QueueItem::Kind::Vehicle) CHECK(q.info(item.design).role == ai::detail::Role::Colonizer);
}

TEST_CASE("ai: Colonizer entries build the colony-ship type of the first uncovered target") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12, false);
    exploreEverything(s);
    const EmpireId cpu{1u};
    Empire& e = s.empire(cpu);
    researchEverything(r, e);
    e.economy = {};
    e.economy.colonies = Resources{100000, 100000, 100000};
    std::map<std::string, DesignId> ships;
    for (std::string_view surface : {"Rock", "Ice", "Gas"}) {
        const std::string pod = std::format("Test {} Pod", surface);
        const DesignId d = addTestDesign(s, r, cpu, std::format("{} Settler", surface), "Test Frigate",
                                         {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Supply Pod", pod});
        s.design(d).designType = std::format("Colony ({})", surface);
        e.designs.push_back(d);
        ships[std::format("Colony ({})", surface)] = d;
    }
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 5);
    p.state = ai::AiState::Exploration;
    REQUIRE_FALSE(p.sit.colonyTargets.empty());
    std::vector<std::string> wanted;
    for (const ai::detail::ColonyTarget& t : p.sit.colonyTargets)
        if (t.settleable) wanted.push_back(ai::detail::colonyTypeName(s.galaxy.object(t.planet).surface));
    ai::detail::planShips(p);
    std::vector<std::string> built;
    for (const auto& c : p.st.colonies)
        if (c && c->owner == cpu)
            for (const QueueItem& q : c->queue.items)
                if (q.kind == QueueItem::Kind::Vehicle && p.info(q.design).role == ai::detail::Role::Colonizer) built.push_back(p.info(q.design).aiType);
    REQUIRE_FALSE(built.empty());
    // In queue order, each colony ship covers the next target.
    for (size_t i = 0; i < built.size() && i < wanted.size(); ++i) CHECK(built[i] == wanted[i]);
}

TEST_CASE("ai: facilities go only to colonies with an empty queue and a free slot") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(11, 2, 12, false);
    const EmpireId cpu{1u};
    Colony& home = homeworld(s, cpu);
    home.facilities.resize(home.facilities.size() - 3);
    s.turn = 1;
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 9);
    ai::detail::planFacilities(p, true);
    ai::detail::planFacilities(p, false);
    // One facility per pass, and the second pass finds the queue busy.
    int facilities = 0;
    for (const QueueItem& q : p.st.colony(home.planet)->queue.items) facilities += q.kind == QueueItem::Kind::Facility;
    CHECK(facilities == 1);
    // The first pass comes before ship construction, except on every fifth turn.
    auto facilitiesAdded = [&](uint32_t turn, int& ships) {
        GameState copy = s;
        copy.turn = turn;
        int added = 0;
        ships = 0;
        for (const Command& c : ai::planEconomyStep(r, copy, cpu))
            if (auto* q = as<cmd::QueueAdd>(c); q && q->target.planet == home.planet) {
                added += q->item.kind == QueueItem::Kind::Facility;
                ships += q->item.kind == QueueItem::Kind::Vehicle && added == 0;
            }
        return added;
    };
    int shipsFirst = 0;
    CHECK(facilitiesAdded(1, shipsFirst) == 1);
    CHECK(shipsFirst == 0);
    int shipsBefore = 0;
    const int onFifth = facilitiesAdded(5, shipsBefore);
    CHECK(onFifth == (shipsBefore > 0 ? 0 : 1));
}

TEST_CASE("ai: colony types at colonization") {
    TempTree t("planettypes");
    // Rows in file order; a 0 limit means no limit.
    t.write("Ai/Default_AI_Planet_Types.txt",
            "AI State := Exploration, Infrastructure\nPlanet Type := Research Compound\nMax Per System := 0\nPercent of Colonies := 0\n"
            "Maximum Total in Empire := 1\n"
            "AI State := Exploration, Infrastructure\nPlanet Type := Refining Colony\nRadioactives Value := 100\nMineral Value := 130\n"
            "AI State := Exploration, Infrastructure\nPlanet Type := Farming Colony\nOrganics Value := 120\nPercent of Colonies := 50\n"
            "AI State := Attack\nPlanet Type := Military Installation\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(11, 2, 0, 12, r);
    const EmpireId cpu{1u};
    Empire& e = s.empire(cpu);
    ObjectId planet;
    for (const SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::Planet && !s.colony(o.id)) {
            planet = o.id;
            break;
        }
    REQUIRE(planet.valid());
    SpaceObject& obj = s.galaxy.object(planet);
    e.stockpile = Resources{50000, 50000, 50000};
    e.economy = {};
    e.economy.colonies = Resources{10000, 10000, 10000};
    e.economy.research = 100;

    // Pre-rule 1: little minerals in store and a planet rich in them.
    e.stockpile[Resource::Minerals] = 1500;
    obj.value = {90, 40, 40};
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Mining Colony");
    e.stockpile[Resource::Minerals] = 50000;
    // Pre-rule 2: an organics deficit above 2000 on a planet with 50 or more organics.
    e.economy.maintenance = Resources{0, 13000, 0};
    obj.value = {60, 60, 60};
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Farming Colony");
    e.economy.maintenance = Resources{};

    // The first row that passes: a Research Compound (at most one in the empire).
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Research Compound");
    for (auto& c : s.colonies)
        if (c && c->owner == cpu) c->colonyType = "Research Compound";
    // Refining needs minerals of at least 130 (only thresholds above 100 count;
    // they are absolute minimums, not ratios).
    obj.value = {129, 60, 999};
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Mining Colony");  // no row passes
    obj.value = {130, 60, 10};
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Refining Colony");
    // Farming: organics of at least 120, and not more than 50 % of the colonies already.
    obj.value = {60, 120, 60};
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Farming Colony");
    // Two more colonies: one Farming of three is 33 %, two of three is 67 %.
    std::vector<ObjectId> extra;
    for (const SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::Planet && !s.colony(o.id) && o.id != planet && extra.size() < 2) extra.push_back(o.id);
    REQUIRE(extra.size() == 2);
    for (ObjectId o : extra) {
        Colony c;
        c.planet = o;
        c.owner = cpu;
        c.population.push_back({cpu, 10});
        c.colonyType = "Mining Colony";
        s.colonies[o.index()] = c;
    }
    s.colonies[extra[0].index()]->colonyType = "Farming Colony";
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Farming Colony");
    s.colonies[extra[1].index()]->colonyType = "Farming Colony";
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Mining Colony");
    // The row's state must match.
    e.aiState = static_cast<int>(ai::AiState::Attack);
    CHECK(ai::colonyTypeAtColonization(r, s, cpu, planet) == "Military Installation");
}

// ---- Designs --------------------------------------------------------------------------------------

TEST_CASE("ai: the designer takes the largest allowed hull and fills it by density") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 1, 8, true);
    Empire& e = s.empires[0];
    researchEverything(r, e);
    int made = 0;
    for (const ai::DesignTemplate& t : ai::builtinProfile().designs) {
        auto d = ai::detail::buildDesign(r, s, e, t);
        if (!d) continue;
        ++made;
        const DesignStats st = computeDesignStats(r, &e, *d);
        CHECK_MESSAGE(st.problems.empty(), t.name << ": " << (st.problems.empty() ? "" : st.problems.front()));
        CHECK(d->designType == t.designType);
        if (t.name == "Attack Ship") {
            CHECK(st.armed());
            CHECK(r.hull(d->hull).name == "Test Cruiser");  // the largest hull
            CHECK(st.engines >= t.minSpeed);
            CHECK(st.tonnageUsed == st.tonnageMax);         // the fill leaves no space
            CHECK(st.shields > 0);                          // shields at 1 per 300 kT
            // The newest weapon of the first family pick (Laser II outranks Laser).
            int laser2 = 0;
            for (const DesignEntry& en : d->entries) laser2 += r.component(en.component).name == "Test Laser II";
            CHECK(laser2 >= 1);
        }
        if (t.name == "Colony (Ice)") CHECK(st.canColonizeIce);
    }
    CHECK(made >= 10);
    // A tonnage window below the cruiser keeps the frigate.
    ai::DesignTemplate small = *ai::builtinProfile().design("Attack Ship");
    small.maxTonnage = 200;
    auto capped = ai::detail::buildDesign(r, s, e, small);
    REQUIRE(capped);
    CHECK(r.hull(capped->hull).name == "Test Frigate");
    // A template without a maximum tonnage never finds a hull.
    small.maxTonnage = 0;
    CHECK_FALSE(ai::detail::buildDesign(r, s, e, small));
    // "Spaces Per One" N gives floor(tonnage / N) copies, at least one: 10000 means one.
    ai::DesignTemplate one = *ai::builtinProfile().design("Attack Ship");
    one.misc = {{"Sensor Level", 10000}};
    one.shieldsSpacesPerOne = 0;
    auto withSensor = ai::detail::buildDesign(r, s, e, one);
    REQUIRE(withSensor);
    int sensors = 0;
    for (const DesignEntry& en : withSensor->entries) sensors += r.component(en.component).name == "Test Sensor";
    CHECK(sensors == 1);
    // No family pick with a researched weapon: the template gets no weapon.
    ai::DesignTemplate unarmed = *ai::builtinProfile().design("Attack Ship");
    unarmed.majorityFamilies = {999, 0, 0, 0, 0};
    auto d = ai::detail::buildDesign(r, s, e, unarmed);
    if (d) CHECK_FALSE(computeDesignStats(r, &e, *d).armed());
}

TEST_CASE("ai: a new design's strategy suits its type when the template's default is missing") {
    // Spec 05 §7.5 step 10 (confirmed: binary): unarmed types get Don't Get
    // Hurt, so they never ram through Optimal Weapons Range (spec 04 §16.1).
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 1, 8, true);
    Empire& e = s.empires[0];
    researchEverything(r, e);
    e.strategies.clear();
    for (const char* movement : {"Optimal Weapons Range", "Don't Get Hurt", "Drop Troops", "Board Enemy Ships", "Ram"})
        e.strategies.push_back({std::string("Test ") + movement, {{"Primary Movement Strategy", movement}}});
    auto primaryOf = [&](uint32_t i) { return e.strategies.at(i).settings.front().second; };
    auto expected = [](std::string_view type) -> std::string {
        auto has = [&](std::string_view part) { return type.find(part) != std::string_view::npos; };
        if (type == "Troop Transport" || type == "Troop") return "Drop Troops";
        if (type == "Boarding Ship") return "Board Enemy Ships";
        if (has("Drone") && !has("Carrier")) return "Ram";
        for (std::string_view k : {"Colony", "Warp Point", "Planet", "Star", "Storm", "Nebulae", "Black Hole", "Space Yard", "Mine", "Transport",
                                   "Carrier", "Layer"})
            if (has(k)) return "Don't Get Hurt";
        return "Optimal Weapons Range";
    };
    int checked = 0;
    for (ai::DesignTemplate t : ai::builtinProfile().designs) {
        t.defaultStrategy = "No Such Strategy";
        const auto d = ai::detail::buildDesign(r, s, e, t);
        if (!d) continue;
        ++checked;
        CHECK_MESSAGE(primaryOf(d->strategy) == expected(t.designType), t.designType);
        if (t.designType == "Population Transport" || t.designType.starts_with("Colony")) CHECK(primaryOf(d->strategy) == "Don't Get Hurt");
    }
    CHECK(checked >= 10);
    // A template default the empire has wins; "Ram" falls back to "Kamikaze".
    e.strategies.push_back({"Kamikaze", {{"Primary Movement Strategy", "Ram"}}});
    ai::DesignTemplate attack = *ai::builtinProfile().design("Attack Ship");
    attack.defaultStrategy = "Ram";
    const auto rammer = ai::detail::buildDesign(r, s, e, attack);
    REQUIRE(rammer);
    CHECK(e.strategies.at(rammer->strategy).name == "Kamikaze");
}

TEST_CASE("ai: new designs make every older design of their type obsolete") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 8, false);
    const EmpireId cpu{1u};
    // A hand-made attack ship of the same design type.
    const DesignId old = addWarship(s, r, cpu, "Old Picket");
    s.empire(cpu).designs.push_back(old);
    researchEverything(r, s.empire(cpu));  // a bigger hull and better parts
    s.turn = 9;  // processed with date 10
    const auto cmds = ai::planTurn(r, s, cpu);
    REQUIRE(applyAll(r, s, cpu, cmds).empty());
    CHECK(s.design(old).obsolete);
    int attackShips = 0;
    for (DesignId d : s.empire(cpu).designs)
        if (s.design(d).designType == "Attack Ship" && !s.design(d).obsolete) ++attackShips;
    CHECK(attackShips == 1);
    // Nothing more to improve: the next tenth turn designs nothing new of it.
    s.turn = 19;
    for (const Command& c : ai::planTurn(r, s, cpu))
        if (auto* d = as<cmd::CreateDesign>(c)) CHECK(d->design.designType != "Attack Ship");
    // Without a research event or a tenth turn the designer rests.
    s.turn = 20;
    CHECK(countOf<cmd::CreateDesign>(ai::planTurn(r, s, cpu)) == 0);
}

TEST_CASE("ai: design names come from the race's design-name file") {
    TempTree t("names");
    t.writePlain("Dsgnname/TESTNAMES.TXT", "Alder\r\nBirch\r\nCedar\r\n");
    Rules rules{buildEngineRuleset(), t.root};
    const auto& names = ai::designNameList(rules, "testnames.txt");
    REQUIRE(names.size() == 3);
    CHECK(names[1] == "Birch");
    GameSetup setup;
    setup.seed = 3;
    setup.options.systemCount = 8;
    setup.empires.push_back(computerSetup("Namer"));
    auto g = createGame(rules, setup);
    REQUIRE(g);
    GameState& s = *g;
    s.empire(EmpireId{0u}).race.designNameFile = "TestNames.txt";
    s.empire(EmpireId{0u}).designs.clear();  // no designs yet: the first name
    const auto cmds = ai::planTurn(rules, s, EmpireId{0u});
    std::vector<std::string> made;
    for (const Command& c : cmds)
        if (auto* d = as<cmd::CreateDesign>(c)) made.push_back(d->design.name);
    REQUIRE(made.size() >= 4);
    CHECK(made[0] == "Alder");
    CHECK(made[1] == "Birch");
    CHECK(made[2] == "Cedar");
    CHECK(made[3] == "Alder II");
}

// ---- Politics -------------------------------------------------------------------------------------

namespace {

MessageId deliver(GameState& s, EmpireId from, EmpireId to, MessageType type, Treaty treaty = Treaty::None) {
    DiplomaticMessage m;
    m.id = MessageId{s.nextMessageId++};
    m.from = from;
    m.to = to;
    m.type = type;
    m.treaty = treaty;
    m.sentTurn = s.turn;
    m.delivered = true;
    if (type == MessageType::Gift || type == MessageType::Tribute) {
        PackageItem item;
        item.resources = {1000, 0, 0};
        m.offer.push_back(item);
    }
    s.messages.push_back(m);
    return m.id;
}

// The date that puts a message in the AI's answer window now (spec 05 §7.4
// "Answer window"): in a simultaneous game the ministers' date − 2 (a
// player's message of the turn processed before), in a turn-based game the
// current date (sent earlier in this game turn).
uint32_t answerDate(const GameState& s) {
    const uint32_t date = ai::aiDate(s);
    if (!s.options.simultaneous) return date;
    return date >= 2 ? date - 2 : uint32_t{0xffffffffu};
}

// The message dated so that the AI answers it now.
GameState datedNow(const GameState& s, MessageId id) {
    GameState g = s;
    for (DiplomaticMessage& m : g.messages)
        if (m.id == id) {
            m.sentTurn = g.turn;
            m.dated = answerDate(g);
        }
    return g;
}

std::optional<bool> answerTo(const Rules& r, const GameState& s, EmpireId cpu, MessageId id) {
    for (const Command& c : ai::planTurn(r, datedNow(s, id), cpu))
        if (auto* a = as<cmd::AnswerMessage>(c); a && a->message == id) return a->accept;
    return std::nullopt;
}

// A message the AI sent in reply to `id`, if any.
std::optional<DiplomaticMessage> sentInReply(const Rules& r, const GameState& s, EmpireId cpu, MessageId id) {
    for (const Command& c : ai::planTurn(r, datedNow(s, id), cpu))
        if (auto* m = as<cmd::SendMessage>(c); m && m->message.inReplyTo == id) return m->message;
    return std::nullopt;
}

} // namespace

TEST_CASE("ai: treaties are accepted on a fixed threshold, with Minimum Anger Chance as a floor") {
    ruleset::Ruleset data = buildEngineRuleset();
    const Rules r{std::move(data)};
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    meet(s, human, cpu);
    s.turn = 60;  // past the first 50 turns
    const ai::PoliticsTable& pol = ai::builtinProfile().politics;
    // The built-in accept rule: base 55, floor 5; nobody is at war; the scores are
    // close, so neither the stronger nor the weaker amount applies.
    const MessageId treaty = deliver(s, human, cpu, MessageType::ProposeTreaty, Treaty::NonAggression);
    int accepted = 0, answered = 0;
    for (int anger : {0, 30, pol.accept.baseAnger - 1, pol.accept.baseAnger, 80, 100}) {
        s.empire(cpu).relation(human).anger = anger;
        // Answers are deterministic: every turn gives the same one, if any.
        std::optional<bool> first;
        for (uint32_t turn = 60; turn < 66; ++turn) {
            s.turn = turn;
            const auto a = answerTo(r, s, cpu, treaty);
            if (!a) continue;
            ++answered;
            if (!first) first = a;
            CHECK(*a == *first);
            accepted += *a;
            CHECK(*a == (anger < pol.accept.baseAnger));
        }
    }
    CHECK(answered > 0);
    CHECK(accepted > 0);
}

TEST_CASE("ai: the Minimum Anger Chance raises a low threshold") {
    TempTree t("floor");
    t.write("Ai/Default_AI_Politics.txt",
            "Accept Treaty Base Anger Level := 10\nAccept Treaty Anger Modifier Per Higher Treaty Level := -20\n"
            "Accept Treaty Minimum Anger Chance := 40\nAccept Treaty Anger Modifier Per Other Wars := 0\n"
            "Accept Treaty First 50 Turns Modifier := 0\nAccept Treaty Anger Modifier For Percent Stronger Player := 10000\n"
            "Accept Treaty Anger Modifier For Percent Weaker Player := 0\nTurns Since Last War Before Friendly Treaty := 0\n"
            "Accept Treaty Minimum Time From Last Treaty := 0\nDeclare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\n");
    Rules r{buildEngineRuleset(), t.root};
    GameSetup setup;
    setup.seed = 7;
    setup.options.systemCount = 8;
    setup.empires.push_back(computerSetup("A"));
    setup.empires.push_back(computerSetup("B"));
    auto g = createGame(r, setup);
    REQUIRE(g);
    GameState& s = *g;
    s.turn = 60;
    const EmpireId a{0u}, b{1u};
    meet(s, a, b);
    // Partnership: 10 - 20 x 3 = -50, raised to 40 by the floor. Anger 39 accepts, 40 refuses.
    const MessageId treaty = deliver(s, a, b, MessageType::ProposeTreaty, Treaty::Partnership);
    s.empire(b).relation(a).anger = 39;
    std::optional<bool> yes;
    for (uint32_t turn = 60; turn < 70 && !yes; ++turn) {
        s.turn = turn;
        yes = answerTo(r, s, b, treaty);
    }
    REQUIRE(yes);
    CHECK(*yes);
    s.empire(b).relation(a).anger = 40;
    std::optional<bool> no;
    for (uint32_t turn = 60; turn < 70 && !no; ++turn) {
        s.turn = turn;
        no = answerTo(r, s, b, treaty);
    }
    REQUIRE(no);
    CHECK_FALSE(*no);
}

TEST_CASE("ai: proposals send the last qualifying treaty, and nothing is given unprompted") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    meet(s, human, cpu);
    s.empire(cpu).relation(human).anger = 0;
    int proposals = 0;
    for (uint32_t turn = 0; turn < 80; ++turn) {
        s.turn = turn;
        for (const Command& c : ai::planTurn(r, s, cpu)) {
            const auto* m = as<cmd::SendMessage>(c);
            if (!m) continue;
            CHECK(m->message.type != MessageType::Gift);
            CHECK(m->message.type != MessageType::Tribute);
            CHECK(m->message.type != MessageType::DemandGift);
            CHECK(m->message.type != MessageType::DemandTribute);
            if (m->message.type != MessageType::ProposeTreaty) continue;
            ++proposals;
            // The built-in list is best first, so the last qualifying entry is one step up.
            CHECK(m->message.treaty == Treaty::NonAggression);
            CHECK_FALSE(m->message.text.empty());
        }
    }
    CHECK(proposals > 0);
    // At Non-Aggression the next step up is proposed.
    s.empire(cpu).relation(human).treaty = Treaty::NonAggression;
    s.empire(human).relation(cpu).treaty = Treaty::NonAggression;
    bool up = false;
    for (uint32_t turn = 100; turn < 200 && !up; ++turn) {
        s.turn = turn;
        for (const Command& c : ai::planTurn(r, s, cpu))
            if (auto* m = as<cmd::SendMessage>(c); m && m->message.type == MessageType::ProposeTreaty) {
                CHECK(m->message.treaty == Treaty::TradeAlliance);
                up = true;
            }
    }
    CHECK(up);
}

TEST_CASE("ai: gifts, tributes and demands are answered from the politics tables") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    meet(s, human, cpu);
    const MessageId gift = deliver(s, human, cpu, MessageType::Gift);
    s.empire(cpu).relation(human).anger = 0;
    std::optional<bool> a;
    for (uint32_t turn = 0; turn < 10 && !a; ++turn) {
        s.turn = turn;
        a = answerTo(r, s, cpu, gift);
    }
    REQUIRE(a);
    CHECK(*a);
    s.empire(cpu).relation(human).anger = 90;  // above Max Anger Level for Accept a Gift
    std::optional<bool> b;
    for (uint32_t turn = 0; turn < 10 && !b; ++turn) {
        s.turn = turn;
        b = answerTo(r, s, cpu, gift);
    }
    if (b) CHECK_FALSE(*b);

    // A request for a tribute from an empire that is not far ahead is refused
    // with a General message from the Response pool, not a verdict.
    s.messages.clear();
    s.empire(cpu).relation(human).anger = 0;
    const MessageId demand = deliver(s, human, cpu, MessageType::DemandTribute);
    std::optional<DiplomaticMessage> reply;
    for (uint32_t turn = 0; turn < 10 && !reply; ++turn) {
        s.turn = turn;
        CHECK_FALSE(answerTo(r, s, cpu, demand));
        reply = sentInReply(r, s, cpu, demand);
    }
    REQUIRE(reply.has_value());
    CHECK(reply->type == MessageType::General);
    CHECK_FALSE(reply->text.empty());
    const auto cmds = ai::planTurn(r, datedNow(s, demand), cpu);
    GameState applied = datedNow(s, demand);
    CHECK(applyAll(r, applied, cpu, cmds).empty());
}

TEST_CASE("ai: wars count only against empires above a quarter of our score") {
    const Rules& r = engineRules();
    auto answer = [&](bool weakThird) {
        GameState s = newEngineGame(7, 3, 12, false);
        const EmpireId human{0u}, cpu{1u}, third{2u};
        meet(s, human, cpu);
        meet(s, cpu, third);
        s.empire(cpu).relation(third).treaty = Treaty::War;
        s.empire(third).relation(cpu).treaty = Treaty::War;
        if (weakThird) {  // nothing left: a score of 0
            for (auto& c : s.colonies)
                if (c && c->owner == third) c.reset();
            std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.owner == third; });
            for (int& l : s.empire(third).techLevels) l = 0;
            s.empire(third).economy = {};
        }
        const auto scores = ai::politicalScores(r, s);
        CHECK((scores[third.index()] > scores[cpu.index()] / 4) == !weakThird);
        s.turn = 60;
        // Built-in accept rule: base 55, -10 per war. Anger 50 passes only without a counted war.
        s.empire(cpu).relation(human).anger = 50;
        s.empire(cpu).relation(third).anger = 50;
        const MessageId treaty = deliver(s, human, cpu, MessageType::ProposeTreaty, Treaty::NonAggression);
        std::optional<bool> a;
        for (uint32_t turn = 60; turn < 70 && !a; ++turn) {
            s.turn = turn;
            a = answerTo(r, s, cpu, treaty);
        }
        return a;
    };
    const auto strong = answer(false);
    const auto weak = answer(true);
    REQUIRE(strong);
    REQUIRE(weak);
    CHECK_FALSE(*strong);
    CHECK(*weak);
}

TEST_CASE("ai: trades are judged on item values") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    meet(s, human, cpu);
    s.empire(cpu).stockpile = Resources{50000, 50000, 50000};
    s.empire(cpu).relation(human).anger = 30;
    auto trade = [&](int64_t give, int64_t get) {
        s.messages.clear();
        DiplomaticMessage m;
        m.id = MessageId{s.nextMessageId++};
        m.from = human;
        m.to = cpu;
        m.type = MessageType::ProposeTrade;
        m.sentTurn = s.turn;
        m.delivered = true;
        PackageItem offer, request;
        offer.resources = {give, 0, 0};
        request.resources = {get, 0, 0};
        m.offer.push_back(offer);
        m.request.push_back(request);
        s.messages.push_back(m);
        std::optional<bool> a;
        for (uint32_t turn = 60; turn < 70 && !a; ++turn) {
            s.turn = turn;
            a = answerTo(r, s, cpu, m.id);
        }
        return a;
    };
    // Resources are worth 1 per 1000. An enemy must offer 120 % of what it asks.
    CHECK(trade(10000, 10000) == false);
    CHECK(trade(12000, 10000) == true);
    // A friend only 95 %: 10 >= trunc(10 x 95 %).
    s.empire(cpu).relation(human).treaty = Treaty::NonAggression;
    s.empire(human).relation(cpu).treaty = Treaty::NonAggression;
    CHECK(trade(10000, 10000) == true);
    CHECK(trade(8000, 10000) == false);
    // What the AI does not have is refused.
    CHECK(trade(900000, 60000) == false);
}

TEST_CASE("ai: an accepted demand is carried out half of the time, before the reply, even when the reply's pool is empty") {
    TempTree t("request");
    t.write("Ai/Default_AI_Politics.txt",
            "Score Percent To Accept Declare war on empire := 0\nWill Accept From Enemy Declare war on empire := True\n"
            "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    // No `Response ... YES ...` pool (as in the stock files): no Accept Demand reply is ever sent.
    t.write("Ai/Default_AI_Speech.txt", "Number of Send General Message := 1\nSend General Message 1 := Hello.\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 3, 0, 12, r);
    const EmpireId asker{0u}, cpu{1u}, third{2u};
    meet(s, asker, cpu);
    meet(s, cpu, third);
    meet(s, asker, third);
    int carried = 0, turns = 0;
    for (uint32_t turn = 1; turn < 41; ++turn) {
        s.turn = turn;
        s.messages.clear();
        const MessageId id = deliver(s, asker, cpu, MessageType::RequestDeclareWar);
        s.messages.back().thirdEmpire = third;
        const auto cmds = ai::planTurn(r, datedNow(s, id), cpu);
        ++turns;
        bool carriedNow = false;
        for (const Command& c : cmds) {
            if (const auto* d = as<cmd::CarryOutDemand>(c); d && d->demand == id) carriedNow = true;
            if (const auto* a = as<cmd::AnswerMessage>(c)) CHECK(a->message != id);  // the empty pool: no reply
        }
        carried += carriedNow;
    }
    CHECK(carried > 0);
    CHECK(carried < turns);
    // Carried out: one entry naming the third empire on the war list.
    GameState g = s;
    const MessageId id = g.messages.back().id;
    REQUIRE(apply(r, g, cpu, cmd::CarryOutDemand{id}).ok);
    CHECK(g.empire(cpu).relation(third).queuedWar == 1);
    REQUIRE(apply(r, g, cpu, cmd::CarryOutDemand{id}).ok);
    CHECK(g.empire(cpu).relation(third).queuedWar == 2);  // duplicates are kept
    CHECK_FALSE(apply(r, g, asker, cmd::CarryOutDemand{id}).ok);  // not the recipient
}

TEST_CASE("ai: demand-list entries are used up one per check, and promises name the empire the demand names") {
    TempTree t("lists");
    t.write("Ai/Default_AI_Politics.txt",
            "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 3, 0, 12, r);
    const EmpireId asker{0u}, cpu{1u}, third{2u};
    meet(s, asker, cpu);
    meet(s, cpu, third);
    for (uint32_t turn = 21; turn < 29; ++turn) {
        s.turn = turn;
        // Two war entries: the first check uses one, and the initiative's check
        // (in either branch) answers yes too: war is declared every time.
        GameState g = s;
        g.empire(cpu).relation(third).queuedWar = 2;
        const auto cmds = ai::planTurn(r, g, cpu);
        bool declared = false;
        for (const Command& c : cmds) {
            if (const auto* m = as<cmd::SendMessage>(c); m && m->message.to == third && m->message.type == MessageType::DeclareWar) declared = true;
            if (const auto* d = as<cmd::DecideWar>(c); d && d->target == third) declared = true;
        }
        CHECK(declared);
        REQUIRE(applyAll(r, g, cpu, cmds).empty());
        CHECK(g.empire(cpu).relation(third).queuedWar <= 1);
        // One entry is always used up, declared or not.
        GameState one = s;
        one.empire(cpu).relation(third).queuedWar = 1;
        REQUIRE(applyAll(r, one, cpu, ai::planTurn(r, one, cpu)).empty());
        CHECK(one.empire(cpu).relation(third).queuedWar == 0);
        // A peace entry forces a proposal and is used up even when no treaty
        // qualifies and nothing is sent.
        GameState peace = s;
        peace.empire(cpu).relation(third).queuedPeace = 1;
        peace.empire(cpu).relation(third).anger = 100;
        REQUIRE(applyAll(r, peace, cpu, ai::planTurn(r, peace, cpu)).empty());
        CHECK(peace.empire(cpu).relation(third).queuedPeace == 0);
    }
    // A check that stops earlier leaves the entries alone: already at war.
    s.turn = 21;
    s.empire(cpu).relation(third).treaty = s.empire(third).relation(cpu).treaty = Treaty::War;
    s.empire(cpu).relation(third).queuedWar = 1;
    s.empire(cpu).relation(third).queuedBreak = 1;
    REQUIRE(applyAll(r, s, cpu, ai::planTurn(r, s, cpu)).empty());
    CHECK(s.empire(cpu).relation(third).queuedWar == 1);
    CHECK(s.empire(cpu).relation(third).queuedBreak == 1);
    // "Stop hostile actions against" an empire: a promise about that empire,
    // not about the requester (spec 05 open question 47).
    const MessageId id = deliver(s, asker, cpu, MessageType::RequestStopHostilities);
    s.messages.back().thirdEmpire = third;
    REQUIRE(apply(r, s, cpu, cmd::CarryOutDemand{id}).ok);
    CHECK(s.empire(cpu).relation(third).promises == 1);
    CHECK(s.empire(cpu).relation(asker).promises == 0);
}

TEST_CASE("ai: an accepted gift request: concrete items go in, the gifts option is not read, an empty package is a refusal") {
    TempTree t("giftrequest");
    t.write("Ai/Default_AI_Politics.txt",
            "Score Percent To Accept Want a gift := 0\nWill Accept From Enemy Want a gift := True\nGift to Enemy Max Anger := 100\n"
            "Gift Value Base to Enemy := 1000\nGift Value to Enemy Per Percentage Greater Score := 0\n"
            "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 2, 0, 10, r);
    const EmpireId asker{0u}, cpu{1u};
    meet(s, asker, cpu);
    s.turn = 30;
    s.options.allowGifts = false;  // only limits what a human picks
    s.empire(cpu).stockpile = {5000, 5000, 5000};
    // A planet the AI does not own is a concrete item it cannot hand over:
    // it goes in anyway, adding no value; then resources up to the value.
    PackageItem planet;
    planet.kind = PackageItem::Kind::Planet;
    planet.planet = homeworld(s, asker).planet;
    PackageItem cash;
    cash.resources = {3000, 0, 0};
    const MessageId id = deliver(s, asker, cpu, MessageType::DemandGift);
    s.messages.back().request = {planet, cash};
    GameState g = datedNow(s, id);
    const auto cmds = ai::planTurn(r, g, cpu);
    std::optional<DiplomaticMessage> gift;
    for (const Command& c : cmds)
        if (const auto* m = as<cmd::SendMessage>(c); m && m->message.inReplyTo == id) gift = m->message;
    REQUIRE(gift);
    CHECK(gift->type == MessageType::Gift);
    REQUIRE(gift->offer.size() == 2);
    CHECK(gift->offer[0].kind == PackageItem::Kind::Planet);
    CHECK(applyAll(r, g, cpu, cmds).empty());
    // Accepted, its items move although gifts are off.
    TurnContext ctx{r, g, {}, {}, {}};
    diplomacy::deliverMessages(ctx);
    const MessageId sent = g.messages.back().id;
    const Resources before = g.empire(asker).stockpile;
    REQUIRE(apply(r, g, asker, cmd::AnswerMessage{sent, true, {}}).ok);
    diplomacy::deliverMessages(ctx);
    CHECK(g.empire(asker).stockpile[Resource::Minerals] == before[Resource::Minerals] + 3000);
    // A value of 0: the package stays empty and the answer is the General refusal.
    TempTree t2("giftrequest0");
    t2.write("Ai/Default_AI_Politics.txt",
             "Score Percent To Accept Want a gift := 0\nWill Accept From Enemy Want a gift := True\nGift to Enemy Max Anger := 100\n"
             "Gift Value Base to Enemy := 0\nGift Value to Enemy Per Percentage Greater Score := 0\n"
             "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    const Rules r0{buildEngineRuleset(), t2.root};
    std::optional<DiplomaticMessage> refusal;
    for (const Command& c : ai::planTurn(r0, datedNow(s, id), cpu))
        if (const auto* m = as<cmd::SendMessage>(c); m && m->message.inReplyTo == id) refusal = m->message;
    REQUIRE(refusal);
    CHECK(refusal->type == MessageType::General);
}

TEST_CASE("ai: the territory pass claims for exactly the empires whose Politics minister is on") {
    // Spec 06 §7 Q47 (confirmed: binary): every computer player, and a human
    // who turns that minister on; a human without it keeps its claims as set.
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, true);
    const EmpireId human{0u};
    const SystemId home = s.galaxy.object(homeworld(s, human).planet).system;
    const SystemId elsewhere = home == SystemId{0u} ? SystemId{1u} : SystemId{0u};
    TurnContext ctx{r, s, {}, {}, {}};
    Empire& e = s.empire(human);
    REQUIRE_FALSE(ai::ministerOn(e, Minister::Politics));   // off for a new human empire
    e.claimedSystems = {elsewhere};
    ai::updateAiState(ctx, human);
    ai::claimTerritory(ctx, human);
    CHECK(s.empire(human).claimedSystems == std::vector<SystemId>{elsewhere});
    // The minister claims as a computer player's does: the colony systems and their neighbours.
    s.empire(human).ministers |= ministerBit(Minister::Politics);
    ai::updateAiState(ctx, human);
    CHECK(s.empire(human).claimedSystems == std::vector<SystemId>{elsewhere});  // not in the state update
    ai::claimTerritory(ctx, human);
    const auto& claimed = s.empire(human).claimedSystems;
    CHECK(std::binary_search(claimed.begin(), claimed.end(), home));
    for (SystemId nb : s.galaxy.neighbors(home)) CHECK(std::binary_search(claimed.begin(), claimed.end(), nb));
    CHECK(claimed == ai::detail::computeTerritory(s, human));
}

TEST_CASE("ai: the demand lists are emptied at the start of turns whose date is a multiple of 10") {
    const Rules& r = engineRules();
    GameState s = computerGame(7, 2, 0, 10);
    const EmpireId cpu{1u}, other{0u};
    auto fill = [&] {
        Relation& rel = s.empire(cpu).relation(other);
        rel.queuedWar = rel.queuedBreak = rel.queuedPeace = rel.promises = 2;
        s.empire(cpu).aiMemory.avoid = {SystemId{0u}};
        s.empire(cpu).aiMemory.attackSystems = {SystemId{1u}};
    };
    TurnContext ctx{r, s, {}, {}, {}};
    fill();
    s.turn = 8;  // the ministers see 9
    ai::updateAiState(ctx, cpu);
    CHECK(s.empire(cpu).relation(other).queuedWar == 2);
    s.turn = 9;  // the ministers see 10
    ai::updateAiState(ctx, cpu);
    const Relation& rel = s.empire(cpu).relation(other);
    CHECK(rel.queuedWar == 0);
    CHECK(rel.queuedBreak == 0);
    CHECK(rel.queuedPeace == 0);
    CHECK(rel.promises == 0);
    CHECK(s.empire(cpu).aiMemory.avoid.empty());
    CHECK(s.empire(cpu).aiMemory.attackSystems.empty());
}

TEST_CASE("ai: a furious AI declares war, and the declaration sets anger to 100") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    meet(s, human, cpu);
    s.empire(cpu).relation(human).anger = 100;
    bool declared = false;
    for (uint32_t turn = 0; turn < 20 && !declared; ++turn) {
        s.turn = turn;
        for (const Command& c : ai::planTurn(r, s, cpu))
            if (auto* m = as<cmd::SendMessage>(c); m && m->message.to == human && m->message.type == MessageType::DeclareWar) declared = true;
    }
    CHECK(declared);
    // The AI step records the declaration.
    s.empire(cpu).relation(human).anger = 60;
    DiplomaticMessage war;
    war.id = MessageId{s.nextMessageId++};
    war.from = cpu;
    war.to = human;
    war.type = MessageType::DeclareWar;
    war.sentTurn = s.turn;
    s.messages.push_back(war);
    TurnContext ctx{r, s, {}, {}, {}};
    ai::updateAnger(ctx);
    // 100, then this turn's terms (decay at most) apply on top.
    CHECK(s.empire(cpu).relation(human).anger >= 100 + ai::builtinProfile().anger.regularDecrease - 1);
}

TEST_CASE("ai: a war declaration with an empty speech pool declares nothing, yet anger becomes 100") {
    // Spec 05 §7.5 AI_Speech: no "Send Declare War" lines at all.
    TempTree t("silentwar");
    t.write("Ai/Default_AI_Speech.txt", "Number of Send Refuse Treaty := 1\nSend Refuse Treaty 1 := No.\n");
    t.write("Ai/Default_AI_Politics.txt", "Declare War Base Anger Level := 10\nBreak Treaty Base Anger Level := 1000\n"
                                          "Propose Treaty Percent Chance Per Turn := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 2, 0, 10, r);
    const EmpireId other{0u}, cpu{1u};
    meet(s, other, cpu);
    const Treaty before = s.empire(cpu).relation(other).treaty;
    bool decided = false;
    for (uint32_t turn = 1; turn < 30 && !decided; ++turn) {
        s.turn = turn;
        s.empire(cpu).relation(other).anger = 60;
        const auto cmds = ai::planTurn(r, s, cpu);
        for (const Command& c : cmds) {
            if (const auto* m = as<cmd::SendMessage>(c)) CHECK(m->message.type != MessageType::DeclareWar);
            if (const auto* d = as<cmd::DecideWar>(c); d && d->target == other) decided = true;
        }
        if (decided) REQUIRE(applyAll(r, s, cpu, cmds).empty());
    }
    REQUIRE(decided);
    CHECK(s.empire(cpu).relation(other).anger == 100);
    CHECK(s.empire(cpu).relation(other).treaty == before);  // nothing was declared

    // The command itself: only toward another living empire already met.
    CHECK_FALSE(apply(r, s, cpu, cmd::DecideWar{cpu}).ok);
    CHECK_FALSE(apply(r, s, cpu, cmd::DecideWar{EmpireId{9u}}).ok);
    s.empire(other).relation(cpu).contact = true;
    s.empire(other).relation(cpu).anger = 10;
    REQUIRE(apply(r, s, other, cmd::DecideWar{cpu}).ok);
    CHECK(s.empire(other).relation(cpu).anger == 100);
}

// ---- Anger and the AI step ---------------------------------------------------------------------------

TEST_CASE("ai: only a successful operation whose culprit is named angers the victim") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 3, 0, 10);
    const EmpireId a{0u}, b{1u};
    meet(s, a, b);
    s.turn = 1;
    const auto& table = ai::builtinProfile().anger;
    const std::string culprit = effects::empireFullName(s.empire(b));
    auto logIntel = [&](std::string title, std::string text) {
        s.empire(a).log.push_back(LogEntry{s.turn, LogCategory::Intelligence, std::move(title), std::move(text), std::nullopt, {}});
    };
    // A blocked attempt and a counter-intelligence report name b but add nothing (spec 05 §7.3 term 3).
    logIntel("Counter Intelligence", std::format("Our counter-intelligence stopped an operation of the {}.", culprit));
    logIntel("Counter Intelligence", std::format("Our agents shut down the Sabotage operation of the {}.", culprit));
    s.empire(a).relation(b).anger = 50;
    TurnContext ctx{r, s, {}, {}, {}};
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease);
    CHECK_FALSE(s.empire(a).relation(b).spiedOnUs);
    // A success with the suspect line does.
    ++s.turn;
    logIntel("Sabotage", "A hostile intelligence operation struck us." + intel::suspectLine(culprit));
    CHECK(intel::namesCulprit(s, s.empire(a).log.back(), b));
    CHECK_FALSE(intel::namesCulprit(s, s.empire(a).log.back(), a));
    s.empire(a).relation(b).anger = 50;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == std::clamp(50 + table.intelligenceAgainstUs, 0, 100) + table.regularDecrease);
    CHECK(s.empire(a).relation(b).spiedOnUs);
}

TEST_CASE("ai: the political step at the start of a turn counts the turn processed before") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 3, 0, 10);
    const EmpireId a{0u}, b{1u};
    meet(s, a, b);
    s.turn = 5;
    const auto& table = ai::builtinProfile().anger;
    const std::string culprit = effects::empireFullName(s.empire(b));
    // A report of turn 4, read by a's political step at the start of turn 5
    // (spec 05 §7.1, §8 step 4).
    s.empire(a).log.push_back(
        LogEntry{4, LogCategory::Intelligence, "Sabotage", "A hostile intelligence operation struck us." + intel::suspectLine(culprit), std::nullopt, {}});
    TurnContext ctx{r, s, {}, {}, {}};
    s.empire(a).relation(b).anger = 50;
    ai::politicalStep(ctx, a, 4u);
    CHECK(s.empire(a).relation(b).anger == std::clamp(50 + table.intelligenceAgainstUs, 0, 100) + table.regularDecrease);
    // Only that turn counts; before the first processed turn nothing does.
    s.empire(a).relation(b).anger = 50;
    ai::politicalStep(ctx, a, 5u);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease);
    s.empire(a).relation(b).anger = 50;
    ai::politicalStep(ctx, a, std::nullopt);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease);
    CHECK(s.empire(b).relation(a).anger == 50);  // one empire's step changes only its own anger
}

TEST_CASE("ai: anger starts at 50 and every term is clamped to 0-100") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 3, 0, 10);
    const EmpireId a{0u}, b{1u}, c{2u};
    CHECK(s.empire(a).relation(b).anger == 50);
    const auto& table = ai::builtinProfile().anger;
    meet(s, a, b);
    meet(s, a, c);
    s.empire(a).relation(b).anger = 50;
    s.empire(a).relation(c).anger = 1;
    s.turn = 1;
    TurnContext ctx{r, s, {}, {}, {}};
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease);
    CHECK(s.empire(a).relation(c).anger == std::max(table.minimum, 0));  // clamped at 0 before the floor
    // Only empires in contact are updated.
    CHECK(s.empire(b).relation(c).anger == 50);

    // A message that arrived this turn: clamped at 100 before the decay.
    DiplomaticMessage war;
    war.id = MessageId{s.nextMessageId++};
    war.from = b;
    war.to = a;
    war.type = MessageType::DeclareWar;
    war.sentTurn = s.turn;
    war.delivered = true;
    s.messages.push_back(war);
    s.empire(a).relation(b).anger = 95;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 100 + table.regularDecrease);
    // Only the earliest message from an empire counts each turn.
    s.messages.clear();
    for (MessageType t : {MessageType::DeclareWar, MessageType::DemandSurrender}) {
        DiplomaticMessage m = war;
        m.id = MessageId{s.nextMessageId++};
        m.type = t;
        s.messages.push_back(m);
    }
    s.empire(a).relation(b).anger = 20;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 20 + table.receive[static_cast<size_t>(MessageType::DeclareWar)] + table.regularDecrease);
}

TEST_CASE("ai: intruders in our territory and a promise change anger") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId a{0u}, b{1u};
    meet(s, a, b);
    const auto& table = ai::builtinProfile().anger;
    s.turn = 1;
    const Location home = locationOf(s.galaxy, homeworld(s, a).planet);
    const DesignId probe =
        addTestDesign(s, r, b, "Probe", "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    addTestVehicle(s, r, probe, home);
    addTestVehicle(s, r, probe, home);
    sight::updateKnowledge(r, s);
    s.empire(a).relation(b).anger = 50;
    TurnContext ctx{r, s, {}, {}, {}};
    ai::updateAnger(ctx);
    // No treaty: Per No Treaty Ship for each of the two ships.
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease + 2 * table.perNoTreatyShip);
    // Claimed territory now includes the home system.
    CHECK(std::binary_search(s.empire(a).claimedSystems.begin(), s.empire(a).claimedSystems.end(), home.system));
    // At war: Per Enemy Ship. A promise to stop hostile actions: -20, once.
    s.empire(a).relation(b).treaty = Treaty::War;
    s.empire(a).relation(b).promises = 2;
    s.empire(a).relation(b).anger = 50;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease - 20 + 2 * table.perEnemyShip);
    CHECK(s.empire(a).relation(b).promises == 1);
    // Two promises: -20 on two turns (spec 05 open question 47).
    s.empire(a).relation(b).anger = 50;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease - 20 + 2 * table.perEnemyShip);
    CHECK(s.empire(a).relation(b).promises == 0);
    // Friends' ships do not count.
    s.empire(a).relation(b).treaty = Treaty::NonAggression;
    s.empire(a).relation(b).anger = 50;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease);
}

TEST_CASE("ai: anger term 2 counts the stellar manipulation reports of the empire's own log (spec 05 Q44)") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 3, 0, 10);
    const EmpireId a{0u}, b{1u}, c{2u};
    meet(s, a, b);
    meet(s, a, c);
    const auto& table = ai::builtinProfile().anger;
    s.turn = 1;
    s.empire(a).relation(b).anger = 50;
    s.empire(a).relation(c).anger = 50;
    // A report in our own log naming B; the same report in C's own log does
    // not count for us.
    const Location where = locationOf(s.galaxy, homeworld(s, a).planet);
    const LogEntry entry{s.turn, LogCategory::Events, "Planet Destroyed: Somewhere", movement::stellarReportText(s, b, "Breaker 1"), where, {}};
    s.empire(a).log.push_back(entry);
    s.empire(c).log.push_back(entry);
    TurnContext ctx{r, s, {}, {}, {}};
    ai::politicalStep(ctx, a, s.turn);
    const int expected = std::clamp(50 + 2 * table.defendingLost, 0, 100);
    CHECK(s.empire(a).relation(b).anger == std::max(std::clamp(expected + table.regularDecrease, 0, 100), table.minimum));
    CHECK(s.empire(a).relation(c).anger == std::max(std::clamp(50 + table.regularDecrease, 0, 100), table.minimum));
}

TEST_CASE("ai: a simultaneous political step counts the messages delivered since the previous one, step 2's included") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 3, 12, false);
    const EmpireId human{0u}, cpu{1u}, later{2u};
    meet(s, human, cpu);
    meet(s, later, cpu);
    s.turn = 5;
    const auto& table = ai::builtinProfile().anger;
    TurnContext ctx{r, s, {}, {}, {}};
    // A player's message of this turn's step 2 (dated 5) and a computer
    // player's message delivered after our previous step (dated 5 too).
    DiplomaticMessage demand;
    demand.to = cpu;
    demand.type = MessageType::DemandTribute;
    REQUIRE(apply(r, s, human, cmd::SendMessage{demand}).ok);
    DiplomaticMessage war = demand;
    war.type = MessageType::DemandSurrender;
    REQUIRE(apply(r, s, later, cmd::SendMessage{war}).ok);
    diplomacy::deliverMessages(ctx);
    s.empire(cpu).relation(human).anger = 30;
    s.empire(cpu).relation(later).anger = 30;
    ai::politicalStep(ctx, cpu, ai::simultaneousWindow(s, cpu));
    ai::recordPoliticalStep(s, cpu);
    auto expected = [&](int receive) { return std::max(std::clamp(30 + receive + table.regularDecrease, 0, 100), table.minimum); };
    const int fromHuman = s.empire(cpu).relation(human).anger;
    CHECK(fromHuman <= expected(table.receive[static_cast<size_t>(MessageType::DemandTribute)]));
    CHECK(fromHuman >= expected(table.receive[static_cast<size_t>(MessageType::DemandTribute)]) - 10);
    // Counted once: the next step does not count them again.
    ++s.turn;
    s.empire(cpu).relation(human).anger = 30;
    ai::politicalStep(ctx, cpu, ai::simultaneousWindow(s, cpu));
    CHECK(s.empire(cpu).relation(human).anger <= expected(0));
    // Without the delivery-based window, step 2's message was not counted in its own turn.
    s.turn = 5;
    s.empire(cpu).politicsMark = PoliticsMark{};
    s.empire(cpu).relation(human).anger = 30;
    ai::PoliticalWindow old = ai::simultaneousWindow(s, cpu);
    old.messagesByDelivery = false;
    ai::politicalStep(ctx, cpu, old);
    CHECK(s.empire(cpu).relation(human).anger < fromHuman);
}

TEST_CASE("ai: the Mega Evil Empire is judged per AI with the strict threshold") {
    ruleset::Ruleset data = buildEngineRuleset();
    data.settings.set("AI Mega Evil Empire Threshold Score Thousands", "0");
    data.settings.set("AI Computer Mega Evil Empire Score Percent", "150");
    data.settings.set("AI Human Mega Evil Empire Score Percent", "150");
    const Rules r{std::move(data)};
    GameState s = computerGame(4, 3, 0, 10, r);
    const EmpireId a{0u}, b{1u}, c{2u};
    std::vector<int64_t> scores{100, 100, 149};
    CHECK_FALSE(ai::megaEvilEmpire(r, scores, s, a).valid());  // 149 < 150
    scores = {100, 100, 150};
    CHECK(ai::megaEvilEmpire(r, scores, s, a) == c);
    CHECK(ai::megaEvilEmpire(r, scores, s, b) == c);
    // The evaluating empire is never its own MEE; it judges the next best.
    CHECK_FALSE(ai::megaEvilEmpire(r, scores, s, c).valid());
    // The evaluating empire counts too: 100 is not 1.5 times c's 150.
    scores = {100, 40, 150};
    CHECK_FALSE(ai::megaEvilEmpire(r, scores, s, c).valid());
    scores = {100, 40, 60};
    CHECK(ai::megaEvilEmpire(r, scores, s, c) == a);
    CHECK(ai::megaEvilEmpire(r, scores, s, b) == a);
    CHECK_FALSE(ai::megaEvilEmpire(r, scores, s, a).valid());  // c's 60 is not 1.5 times a's 100
    // The threshold is strict: a score equal to it does not qualify.
    ruleset::Ruleset high = buildEngineRuleset();
    high.settings.set("AI Mega Evil Empire Threshold Score Thousands", "1");
    high.settings.set("AI Computer Mega Evil Empire Score Percent", "100");
    const Rules r2{std::move(high)};
    CHECK_FALSE(ai::megaEvilEmpire(r2, std::vector<int64_t>{0, 0, 1000}, s, a).valid());
    CHECK(ai::megaEvilEmpire(r2, std::vector<int64_t>{0, 0, 1001}, s, a) == c);
}

TEST_CASE("ai: the AI state machine") {
    const Rules& r = engineRules();
    GameState s = computerGame(13, 2, 0, 12);
    exploreEverything(s);
    const EmpireId me{0u}, enemy{1u};
    // Exploration: no contact, nothing changes.
    CHECK(ai::nextState(r, s, me) == ai::AiState::Exploration);
    // Contact and no unexplored space next to our territory: Infrastructure.
    meet(s, me, enemy);
    CHECK(ai::nextState(r, s, me) == ai::AiState::Infrastructure);

    // Infrastructure: a hostile candidate planet starts Prepare for Attack.
    TurnContext ctx{r, s, {}, {}, {}};
    s.empire(me).aiState = static_cast<int>(ai::AiState::Infrastructure);
    s.empire(me).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(me).treaty = Treaty::War;
    s.turn = 3;
    ai::updateAnger(ctx);
    CHECK(s.empire(me).aiState == static_cast<int>(ai::AiState::PrepareForAttack));
    const SystemId enemyHome = s.galaxy.object(homeworld(s, enemy).planet).system;
    REQUIRE_FALSE(s.empire(me).aiMemory.targets.empty());
    CHECK(s.empire(me).aiMemory.targets.front() == enemyHome);
    CHECK(s.empire(me).aiMemory.staging.valid());
    CHECK(s.empire(me).aiMemory.staging != enemyHome);

    // Stronger at the staging system than the target: Attack.
    const DesignId warship = addWarship(s, r, me, "Hammer");
    const Location staging = {s.empire(me).aiMemory.staging, Sector{kSystemCenter, kSystemCenter}};
    for (int i = 0; i < 6; ++i) addTestVehicle(s, r, warship, staging);
    ai::updateAnger(ctx);
    CHECK(s.empire(me).aiState == static_cast<int>(ai::AiState::Attack));

    // Every target without hostile strength: Secure Holdings.
    for (auto& c : s.colonies)
        if (c && c->owner == enemy) c->owner = me;
    std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.owner == enemy; });
    ai::updateAnger(ctx);
    CHECK(s.empire(me).aiState == static_cast<int>(ai::AiState::SecureHoldings));
    CHECK(s.empire(me).aiMemory.secured == enemyHome);
}

TEST_CASE("ai: the after-attack timer holds off the next attack") {
    TempTree t("timer");
    t.write("Ai/Default_AI_Settings.txt", "Turns to Wait until next attack := 3\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(13, 2, 0, 12, r);
    exploreEverything(s);
    const EmpireId me{0u}, enemy{1u};
    meet(s, me, enemy);
    s.empire(me).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(me).treaty = Treaty::War;
    s.empire(me).aiState = static_cast<int>(ai::AiState::Infrastructure);
    s.empire(me).aiMemory.afterAttack = 1;  // an attack just ended
    TurnContext ctx{r, s, {}, {}, {}};
    // The timer grows by one each turn; the next attack waits until it is above 3.
    for (int expected : {2, 3}) {
        ++s.turn;
        ai::updateAnger(ctx);
        CHECK(s.empire(me).aiState == static_cast<int>(ai::AiState::Infrastructure));
        CHECK(s.empire(me).aiMemory.afterAttack == expected);
    }
    ++s.turn;
    ai::updateAnger(ctx);
    CHECK(s.empire(me).aiState == static_cast<int>(ai::AiState::PrepareForAttack));
    CHECK(s.empire(me).aiMemory.afterAttack == 0);  // every state but Infrastructure resets it
}

TEST_CASE("ai: Not Connected when little is left to settle, explore or reach, over every link") {
    const Rules& r = engineRules();
    GameState s = computerGame(13, 2, 0, 12);
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    // Everything explored, no warp link known, no colony module, and our
    // ships already head for every frontier point.
    e.knowledge.explored.assign(s.galaxy.systems.size(), 1);
    e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 0);
    for (const auto& a : r.data().techAreas)
        if (a.name.find("Colonies") != std::string::npos) e.techLevels[r.data().findTechArea(a.name)->index()] = 0;
    std::vector<Order> everywhere;
    for (const SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::WarpPoint) {
            Order warp;
            warp.kind = OrderKind::Warp;
            warp.object = o.id;
            warp.location = locationOf(s.galaxy, o.id);
            everywhere.push_back(warp);
        }
    for (Vehicle& v : s.vehicles)
        if (v.owner == me) {
            v.orders = everywhere;
            break;
        }
    // The reachability counts every link, known or not: the galaxy is connected.
    CHECK(ai::nextState(r, s, me) != ai::AiState::NotConnected);
    // Cut every link: home reaches nothing, and the test holds.
    GameState cut = s;
    for (SpaceObject& o : cut.galaxy.objects)
        if (o.kind == ObjectKind::WarpPoint) o.destination = {};
    CHECK(ai::nextState(r, cut, me) == ai::AiState::NotConnected);
    const SystemId home = ai::detail::homeSystem(cut, me);
    const std::vector<int> jumps = ai::detail::jumpsOver(cut, home);
    CHECK(jumps[home.index()] == 0);
    for (size_t i = 0; i < jumps.size(); ++i)
        if (SystemId{i} != home) CHECK(jumps[i] == ai::detail::kUnreachable);
    // Back in a connected galaxy the test fails.
    s.empire(me).aiState = static_cast<int>(ai::AiState::NotConnected);
    CHECK(ai::nextState(r, s, me) == ai::AiState::Infrastructure);
}

TEST_CASE("ai: difficulty is per empire: random players get the chosen level, rebels the highest") {
    GameState s = computerGame(3, 3, 0, 8);
    s.options.aiDifficulty = kDifficultyHigh;
    s.options.randomAiPlayers = {0, 1, 0};
    CHECK(ai::difficultyOf(s, EmpireId{0u}) == kDifficultyMedium);
    CHECK(ai::difficultyOf(s, EmpireId{1u}) == kDifficultyHigh);
    s.turn = 0;
    TurnContext ctx{engineRules(), s, {}, {}, {}};
    ai::updateAnger(ctx);
    CHECK(s.empire(EmpireId{0u}).aiDifficulty == kDifficultyMedium);
    CHECK(s.empire(EmpireId{1u}).aiDifficulty == kDifficultyHigh);
    CHECK(ai::rebelDifficulty(s) == kDifficultyHigh);
    // An empire that appears later (a revolt) takes the highest level.
    Empire rebel = s.empire(EmpireId{2u});
    rebel.id = EmpireId{3u};
    rebel.aiDifficulty = -1;
    for (Empire& e : s.empires) e.relations.resize(4);
    rebel.relations.resize(4);
    s.empires.push_back(rebel);
    s.turn = 30;
    ai::updateAnger(ctx);
    CHECK(s.empire(EmpireId{3u}).aiDifficulty == kDifficultyHigh);
    // A human's ministers play at Medium.
    s.empire(EmpireId{0u}).kind = PlayerKind::Human;
    CHECK(ai::difficultyOf(s, EmpireId{0u}) == kDifficultyMedium);
}

TEST_CASE("ai: Low difficulty misses one hostile object in ten") {
    const Rules& r = engineRules();
    GameState s = computerGame(3, 2, 0, 8);
    int seen = 0;
    for (uint32_t i = 0; i < 1000; ++i) seen += ai::detail::notices(s, EmpireId{0u}, ai::detail::planetKey(ObjectId{i}));
    CHECK(seen == 1000);  // Medium notices everything
    s.empire(EmpireId{0u}).aiDifficulty = kDifficultyLow;
    seen = 0;
    for (uint32_t i = 0; i < 1000; ++i) seen += ai::detail::notices(s, EmpireId{0u}, ai::detail::planetKey(ObjectId{i}));
    CHECK(seen > 850);
    CHECK(seen < 950);
    // The same roll for every caller on the same turn.
    CHECK(ai::detail::notices(s, EmpireId{0u}, 42) == ai::detail::notices(s, EmpireId{0u}, 42));
    (void)r;
}

TEST_CASE("ai: mood labels") {
    CHECK(ai::moodLabel(0) == "Brotherly");
    CHECK(ai::moodLabel(10) == "Amiable");
    CHECK(ai::moodLabel(29) == "Receptive");
    CHECK(ai::moodLabel(35) == "Warm");
    CHECK(ai::moodLabel(40) == "Moderate");
    CHECK(ai::moodLabel(59) == "Moderate");
    CHECK(ai::moodLabel(60) == "Cool");
    CHECK(ai::moodLabel(75) == "Displeased");
    CHECK(ai::moodLabel(85) == "Angry");
    CHECK(ai::moodLabel(100) == "Murderous");
}

// ---- Modes and ministers -------------------------------------------------------------------------

TEST_CASE("ai: minimal-changes mode does nothing") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(9, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u};
    meet(s, me, EmpireId{1u});
    CHECK_FALSE(ai::planTurn(r, s, me, false).empty());
    CHECK(ai::planTurn(r, s, me, true).empty());
}

TEST_CASE("ai: neutral empires stay in their home system") {
    const Rules& r = engineRules();
    GameState s = computerGame(17, 2, 1, 14);
    const EmpireId neutral{2u};
    REQUIRE(s.empire(neutral).kind == PlayerKind::Neutral);
    const SystemId home = s.galaxy.object(homeworld(s, neutral).planet).system;
    exploreEverything(s);
    for (int turn = 0; turn < 20; ++turn) {
        const auto cmds = ai::planTurn(r, s, neutral);
        for (const Command& c : cmds)
            if (const auto* o = as<cmd::SetOrders>(c))
                for (const Order& ord : o->orders) {
                    CHECK(ord.kind != OrderKind::Warp);
                    CHECK(ord.kind != OrderKind::Explore);
                    if (ord.kind == OrderKind::MoveTo || ord.kind == OrderKind::Colonize) CHECK(ord.location.system == home);
                }
        CHECK(applyAll(r, s, neutral, cmds).empty());
        std::vector<EmpireOrders> none;
        TurnOptions opts;
        opts.aiForMissing = false;
        processTurn(r, s, none, opts);
        fakeProgress(r, s);
        exploreEverything(s);
    }
    for (const Vehicle& v : s.vehicles)
        if (v.owner == neutral) CHECK(v.location.system == home);
}

TEST_CASE("ai: ministers act only on what they were given") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(11, 2, 12, true);
    const EmpireId me{0u};
    CHECK(ai::ministerCommands(r, s, me).empty());  // nothing under minister control

    Colony& home = homeworld(s, me);
    home.minister = true;
    home.facilities.resize(home.facilities.size() - 2);  // room for the facility minister
    auto cmds = ai::ministerCommands(r, s, me);
    CHECK_FALSE(cmds.empty());
    for (const Command& c : cmds) {
        const auto* q = as<cmd::QueueAdd>(c);
        CHECK_MESSAGE(q, commandName(c));
        if (q) CHECK(q->target.planet == home.planet);
    }
    CHECK(applyAll(r, s, me, cmds).empty());

    // Switching the Facility Construction minister off stops it.
    GameState off = s;
    homeworld(off, me).queue.items.clear();
    off.empire(me).ministers &= ~ministerBit(Minister::FacilityConstruction);
    CHECK(ai::ministerCommands(r, off, me).empty());

    // A ship under the Exploration minister explores.
    const VehicleId scout = addTestVehicle(s, r, addWarship(s, r, me, "Picket"), locationOf(s.galaxy, home.planet)).id;
    s.vehicle(scout)->minister = true;
    cmds = ai::ministerCommands(r, s, me);
    bool ordered = false;
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c)) {
            CHECK(o->vehicle == scout);
            ordered = true;
        }
    CHECK(ordered);

    // A global minister takes over its whole area when switched on.
    s.empire(me).ministers |= ministerBit(Minister::Research);
    CHECK(countOf<cmd::SetResearch>(ai::ministerCommands(r, s, me)) == 1);
    s.empire(me).ministers &= ~ministerBit(Minister::Research);
    CHECK(countOf<cmd::SetResearch>(ai::ministerCommands(r, s, me)) == 0);
    // Full minister control includes research; computer empires get nothing here.
    s.empire(me).ministerAll = true;
    CHECK(countOf<cmd::SetResearch>(ai::ministerCommands(r, s, me)) == 1);
    s.empire(EmpireId{1u}).kind = PlayerKind::Computer;
    CHECK(ai::ministerCommands(r, s, EmpireId{1u}).empty());
}

// ---- Fleets, attack and defence -----------------------------------------------------------------------

TEST_CASE("ai: fleets follow the division table and attack the state's goal") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u}, enemy{1u};
    meet(s, me, enemy);
    s.empire(me).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(me).treaty = Treaty::War;
    s.empire(me).relation(enemy).anger = 60;
    const SystemId enemyHome = s.galaxy.object(homeworld(s, enemy).planet).system;
    s.empire(me).aiState = static_cast<int>(ai::AiState::Attack);
    s.empire(me).aiMemory.targets = {enemyHome};
    s.empire(me).aiMemory.staging = s.galaxy.object(homeworld(s, me).planet).system;
    s.turn = 40;
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId warship = addWarship(s, r, me, "Hammer");
    for (int i = 0; i < 8; ++i) addTestVehicle(s, r, warship, home);

    // Built-in divisions: up to 15 vehicles, two fleets, 40 % for defence:
    // fleet 1 attacks. One fleet is formed per turn, around the newest idle fit ship.
    auto cmds = ai::planTurn(r, s, me);
    CHECK(countOf<cmd::CreateFleet>(cmds) == 1);
    bool attack = false;
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c); o && o->fleet.valid())
            for (const Order& ord : o->orders)
                if (ord.kind == OrderKind::Attack && ord.object.valid()) {
                    const Colony* target = s.colony(ord.object);
                    REQUIRE(target);
                    CHECK(target->owner == enemy);
                    attack = true;
                }
    CHECK(attack);
    CHECK(applyAll(r, s, me, cmds).empty());
    // The fleet recruits up to trunc(vehicles x 60 % / 2).
    const Fleet* fleet = nullptr;
    for (const Fleet& f : s.fleets)
        if (f.owner == me) fleet = &f;
    REQUIRE(fleet);
    int vehicles = 0;
    for (const Vehicle& v : s.vehicles) vehicles += v.owner == me;
    CHECK(static_cast<int>(fleet->members.size()) <= vehicles * 60 / 100 / 2);
    CHECK(fleet->members.size() >= 2);
    // The second fleet the table wants comes next turn, and no third.
    s.turn = 41;
    auto next = ai::planTurn(r, s, me);
    CHECK(countOf<cmd::CreateFleet>(next) == 1);
    REQUIRE(applyAll(r, s, me, next).empty());
    s.turn = 42;
    CHECK(countOf<cmd::CreateFleet>(ai::planTurn(r, s, me)) == 0);
    // Too early in the game: no fleets, and existing ones are disbanded.
    s.turn = 10;
    CHECK(countOf<cmd::DisbandFleet>(ai::planTurn(r, s, me)) == 2);
}

TEST_CASE("ai: fleet roles: odd fleets attack while under the defence share") {
    TempTree t("fleets");
    t.write("Ai/Default_AI_Fleets.txt",
            "Fleets Num Divisions := 1\nFleets Div 1 Max Amount of Ships := 1000\nFleets Div 1 Max Amount of Planets := 0\n"
            "Fleets Div 1 Num Fleets := 4\nFleets Percentage of Ships For Fleets := 100\nFleets Dont Use For Num Turns := 0\n"
            "Percentage of Fleets to use for defense := 40\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(13, 2, 0, 12, r);
    exploreEverything(s);
    const EmpireId me{0u}, enemy{1u};
    meet(s, me, enemy);
    s.empire(me).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(me).treaty = Treaty::War;
    const SystemId enemyHome = s.galaxy.object(homeworld(s, enemy).planet).system;
    s.empire(me).aiState = static_cast<int>(ai::AiState::Attack);
    s.empire(me).aiMemory.targets = {enemyHome};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId warship = addWarship(s, r, me, "Hammer");
    s.empire(me).designs.push_back(warship);
    std::vector<FleetId> fleets;
    for (int i = 0; i < 4; ++i) {
        const VehicleId v = addTestVehicle(s, r, warship, home).id;
        REQUIRE(apply(r, s, me, cmd::CreateFleet{{}, {v}}).ok);
        fleets.push_back(s.fleets.back().id);
    }
    // 4 fleets wanted, 60 % for attack: fleets 1 and 3 attack ((i + 1) / 2 < 2.4), 2 and 4 defend.
    const auto cmds = ai::planTurn(r, s, me);
    std::vector<bool> attacking(4, false);
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c); o && o->fleet.valid())
            for (size_t i = 0; i < fleets.size(); ++i)
                if (o->fleet == fleets[i])
                    for (const Order& ord : o->orders) attacking[i] = attacking[i] || (ord.kind == OrderKind::Attack && ord.object.valid());
    CHECK(attacking[0]);
    CHECK_FALSE(attacking[1]);
    CHECK(attacking[2]);
    CHECK_FALSE(attacking[3]);
}

TEST_CASE("ai: a fleet whose leader is unfit is disbanded") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, true);
    const EmpireId me{0u};
    s.turn = 40;
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    // Fifteen components: bridge, life support, crew quarters, two engines, a laser and nine plates.
    const DesignId warship = addTestDesign(s, r, me, "Hammer", "Test Cruiser",
                                           {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine",
                                            "Test Laser", "Test Armor Plate", "Test Armor Plate", "Test Armor Plate", "Test Armor Plate",
                                            "Test Armor Plate", "Test Armor Plate", "Test Armor Plate", "Test Armor Plate", "Test Armor Plate"});
    s.design(warship).designType = "Attack Ship";
    const VehicleId lead = addTestVehicle(s, r, warship, home).id;
    REQUIRE(apply(r, s, me, cmd::CreateFleet{{}, {lead}}).ok);
    auto disbanded = [&] { return countOf<cmd::DisbandFleet>(ai::planTurn(r, s, me)) > 0; };
    CHECK_FALSE(disbanded());
    // round(0.3 x 15) is 4, not 5: the double nearest 0.3 is a hair below it.
    Vehicle& v = *s.vehicle(lead);
    for (size_t i = 6; i < 10; ++i) v.damage[i] = entryStructure(r, s.design(warship), i);
    CHECK_FALSE(disbanded());
    v.damage[10] = entryStructure(r, s.design(warship), 10);
    CHECK(disbanded());
    // A ship without a part it needs to operate is unfit too: no working
    // bridge, auxiliary control or Master Computer (spec 05 §7.5). Life
    // support, crew quarters and engines are not checked.
    for (size_t i = 6; i < 11; ++i) v.damage[i] = 0;
    v.damage[1] = entryStructure(r, s.design(warship), 1);  // life support
    v.damage[3] = entryStructure(r, s.design(warship), 3);  // an engine
    CHECK_FALSE(disbanded());
    v.damage[1] = v.damage[3] = 0;
    v.damage[0] = entryStructure(r, s.design(warship), 0);  // the bridge
    CHECK(disbanded());
}

TEST_CASE("ai: troop transports reload at the nearest colony with troops") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId troop = addTestDesign(s, r, me, "Grunt", "Test Troop Hull", {"Test Troop Rifle"});
    const DesignId lander = addTestDesign(s, r, me, "Lander", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay"});
    s.design(lander).designType = "Troop Transport";
    const VehicleId empty = addTestVehicle(s, r, lander, home).id;
    homeworld(s, me).cargo.units.push_back({troop, 5});
    const auto cmds = ai::planTurn(r, s, me);
    bool loading = false;
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c); o && o->vehicle == empty && !o->orders.empty())
            loading = o->orders.back().kind == OrderKind::LoadCargo && o->orders.back().location == home && o->orders.back().design == troop;
    CHECK(loading);
    CHECK(applyAll(r, s, me, cmds).empty());
}

TEST_CASE("ai: sweepers sweep hostile mine fields and drones hunt war enemies in range") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, false);
    exploreEverything(s);
    const EmpireId enemy{0u}, cpu{1u};
    meet(s, enemy, cpu);
    s.empire(cpu).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(cpu).treaty = Treaty::War;
    const Location home = locationOf(s.galaxy, homeworld(s, cpu).planet);
    const DesignId mine = addTestDesign(s, r, enemy, "Mine", "Test Mine Hull", {"Test Warhead"});
    const VehicleId field = addTestVehicle(s, r, mine, home).id;
    const DesignId raider = addWarship(s, r, enemy, "Raider");
    const VehicleId intruder = addTestVehicle(s, r, raider, home).id;
    const DesignId sweeperDesign = addTestDesign(s, r, cpu, "Sweeper", "Test Frigate",
                                                 {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Mine Sweeper"});
    s.design(sweeperDesign).designType = "Mine Sweeper";
    const VehicleId sweeper = addTestVehicle(s, r, sweeperDesign, home).id;
    const DesignId droneDesign = addTestDesign(s, r, cpu, "Drone", "Test Drone Hull", {"Test Engine", "Test Warhead"});
    s.design(droneDesign).designType = "Anti-Ship Drone";
    const VehicleId drone = addTestVehicle(s, r, droneDesign, home).id;
    sight::updateKnowledge(r, s);
    Empire& e = s.empire(cpu);
    for (VehicleId v : {field, intruder})
        if (std::find(e.knowledge.visibleVehicles.begin(), e.knowledge.visibleVehicles.end(), v) == e.knowledge.visibleVehicles.end())
            e.knowledge.visibleVehicles.push_back(v);
    std::sort(e.knowledge.visibleVehicles.begin(), e.knowledge.visibleVehicles.end());
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 3);
    REQUIRE(p.sit.enemyNearby.size() == 1);  // a mine field is "nearby", not "in territory"
    ai::detail::planMinesSatellitesDrones(p);
    const Vehicle* sw = p.st.vehicle(sweeper);
    REQUIRE(sw);
    REQUIRE_FALSE(sw->orders.empty());
    CHECK(sw->orders.back().kind == OrderKind::SweepMines);
    const Vehicle* d = p.st.vehicle(drone);
    REQUIRE(d);
    REQUIRE_FALSE(d->orders.empty());
    CHECK(d->orders.front().kind == OrderKind::Attack);
    CHECK(d->orders.front().vehicle == intruder);
}

TEST_CASE("ai: defenders answer a threat at home") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, true);
    const EmpireId me{0u}, enemy{1u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId raider = addTestDesign(s, r, enemy, "Raider", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser"});
    const VehicleId intruder = addTestVehicle(s, r, raider, home).id;
    const DesignId guard = addWarship(s, r, me, "Guard");
    const VehicleId mine = addTestVehicle(s, r, guard, home).id;
    // Supplies for many moves and a depot at home: the Resupply minister leaves it alone.
    s.vehicle(mine)->supply = 1'000'000;
    homeworld(s, me).facilities.push_back(facilityIndex(r, "Test Depot"));
    sight::updateKnowledge(r, s);

    CHECK(ai::nextState(r, s, me) == ai::AiState::DefendShortTerm);
    s.empire(me).aiState = static_cast<int>(ai::AiState::DefendShortTerm);
    s.empire(me).aiTurnsInState = 12;
    CHECK(ai::nextState(r, s, me) == ai::AiState::DefendShortTerm);  // Defend (Long Term) is never entered

    const auto cmds = ai::planTurn(r, s, me);
    bool engaged = false;
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c); o && o->vehicle == mine)
            engaged = !o->orders.empty() && o->orders.front().kind == OrderKind::Attack && o->orders.front().vehicle == intruder;
    CHECK(engaged);
    // Gone: back to Exploration while unexplored space borders our territory.
    s.vehicles.erase(std::remove_if(s.vehicles.begin(), s.vehicles.end(), [&](const Vehicle& v) { return v.id == intruder; }),
                     s.vehicles.end());
    sight::updateKnowledge(r, s);
    CHECK(ai::nextState(r, s, me) == ai::AiState::Exploration);
}

TEST_CASE("ai: the budget's revenue is production times the bonus plus income from others; tariffs paid stay in") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    REQUIRE(s.empire(cpu).kind == PlayerKind::Computer);
    s.options.aiBonus = 3;  // High: income × 5 after tariffs (spec 05 §8)
    s.turn = 1;
    TurnContext ctx{r, s, {}, {}, {}};
    diplomacy::makeContact(ctx, human, cpu);
    diplomacy::setTreaty(ctx, human, cpu, Treaty::Subjugation, true);
    economy::updateReports(r, s);
    const Resources tariffs = s.empire(cpu).economy.tariffsOut;
    REQUIRE(tariffs != Resources{});
    const Resources revenue = ai::detail::Planner(r, s, cpu, ai::detail::Mode::Computer, 9).revenue();
    const Resources before = s.empire(cpu).stockpile;
    economy::collectIncome(ctx, cpu);
    economy::collectTrade(ctx, cpu);
    // The income step banks (production - tariffs) × 5; the budget counts production × 5.
    CHECK(revenue - (s.empire(cpu).stockpile - before) == Resources{tariffs.v[0] * 5, tariffs.v[1] * 5, tariffs.v[2] * 5});
}

TEST_CASE("ai: a ship sent for supplies is not sent again while it is on its way") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId me{0u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    Location away = home;
    away.sector = Sector{home.sector.x == 0 ? 1 : 0, home.sector.y};  // another sector of the home system
    const DesignId tanker = addTestDesign(s, r, me, "Tanker", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Supply Pod"});
    const VehicleId ship = addTestVehicle(s, r, tanker, away).id;
    s.vehicle(ship)->supply = 0;
    auto resupplyOrders = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 9);
        ai::detail::planRepairAndResupply(p, false);
        std::vector<cmd::SetOrders> out;
        for (const Command& c : p.report().commands)
            if (const auto* o = as<cmd::SetOrders>(c); o && o->vehicle == ship) out.push_back(*o);
        return out;
    };
    const auto first = resupplyOrders(s);
    REQUIRE(first.size() == 1);
    REQUIRE(apply(r, s, me, first.front()).ok);
    REQUIRE_FALSE(s.vehicle(ship)->orders.empty());
    CHECK(s.vehicle(ship)->orders.front().kind == OrderKind::MoveTo);  // stored expanded (spec 03 §8)
    CHECK(resupplyOrders(s).empty());  // already on its way: not sent again
}

TEST_CASE("ai: computer player bonus helpers") {
    GameState s = newEngineGame(3, 2, 8, false);
    s.options.aiBonus = 2;
    CHECK(ai::incomeBonusFactor(s, EmpireId{0u}) == 1);  // humans get no bonus
    CHECK(ai::incomeBonusFactor(s, EmpireId{1u}) == 3);
    CHECK(ai::constructionBonusPercent(s, EmpireId{0u}) == 100);
    CHECK(ai::constructionBonusPercent(s, EmpireId{1u}) == 200);
}

// ---- Installed data (opt-in) ----------------------------------------------------------------------

namespace {

const Rules* installedRules() {
    static const std::unique_ptr<Rules> rules = []() -> std::unique_ptr<Rules> {
        const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
        if (!env) return nullptr;
        auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
        if (!dir) return nullptr;
        auto loaded = ruleset::loadRuleset(*dir);
        if (!loaded.ruleset) return nullptr;
        return std::make_unique<Rules>(std::move(*loaded.ruleset), dir->parent_path());
    }();
    return rules.get();
}

} // namespace

// ---- Minister settings, planet launches, chatter and unit cargo --------------------------------------

TEST_CASE("ai: the minister settings belong to the empire and are set by cmd::SetMinisters") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(11, 2, 12, true);
    const EmpireId me{0u}, other{1u};
    // One area switch at a time; the global Research minister then takes over research.
    REQUIRE(apply(r, s, me, cmd::SetMinisters{.areas = ministerBit(Minister::Research)}).ok);
    CHECK(s.empire(me).ministers == ministerBit(Minister::Research));
    CHECK(ai::ministerOn(s.empire(me), Minister::Research));
    CHECK_FALSE(ai::ministerOn(s.empire(me), Minister::Design));
    CHECK(countOf<cmd::SetResearch>(ai::ministerCommands(r, s, me)) == 1);
    CHECK_FALSE(apply(r, s, me, cmd::SetMinisters{.areas = uint32_t{1} << 30}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::SetMinisters{.style = std::string("../Default")}).ok);

    // Indiv. Ministers On flags everything we own and nothing else.
    const DesignId picket = addWarship(s, r, me, "Picket");
    const DesignId theirs = addWarship(s, r, other, "Theirs");
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    REQUIRE(apply(r, s, me, cmd::SetMinisters{.individual = true}).ok);
    for (const Vehicle& v : s.vehicles) CHECK(v.minister == (v.owner == me));
    for (const auto& c : s.colonies)
        if (c) CHECK(c->minister == (c->owner == me));

    // With the new-vehicle option, new vehicles start under minister control.
    CHECK_FALSE(movement::spawnVehicle(r, s, me, picket, home).minister);
    REQUIRE(apply(r, s, me, cmd::SetMinisters{.newVehicles = true}).ok);
    CHECK(movement::spawnVehicle(r, s, me, picket, home).minister);
    CHECK_FALSE(movement::spawnVehicle(r, s, other, theirs, home).minister);

    // Complete AI On and Off do all of the bulk buttons at once.
    REQUIRE(apply(r, s, me, cmd::SetMinisters{.completeAi = false}).ok);
    CHECK(s.empire(me).ministers == 0);
    CHECK_FALSE(s.empire(me).ministerAll);
    CHECK_FALSE(s.empire(me).ministersForNewVehicles);
    for (const Vehicle& v : s.vehicles)
        if (v.owner == me) CHECK_FALSE(v.minister);
    CHECK(ai::ministerCommands(r, s, me).empty());
    REQUIRE(apply(r, s, me, cmd::SetMinisters{.completeAi = true}).ok);
    CHECK(s.empire(me).ministers == kAllMinisters);
    CHECK(s.empire(me).ministerAll);
    CHECK(s.empire(me).ministersForNewVehicles);
    CHECK(homeworld(s, me).minister);
    CHECK_FALSE(homeworld(s, other).minister);
}

TEST_CASE("ai: the minister style picks the AI files unless the race's style is used") {
    TempTree t("style");
    t.write("Ai/Default_AI_Anger.txt", "Regular Decrease := -9\n");
    t.write("Ai/Aggressive/Aggressive_AI_Anger.txt", "Regular Decrease := -1\n");
    t.write("Ai/Defensive/Defensive_AI_Settings.txt", "Personality Group := 2\n");
    t.writePlain("Ai/Notes/readme.txt", "not a style folder");
    t.write("Pictures/Races/Testian/Testian_AI_Anger.txt", "Regular Decrease := -4\n");
    const Rules rules{buildEngineRuleset(), t.root};
    CHECK(ai::ministerStyles(rules) == std::vector<std::string>{"Aggressive", "Defensive"});
    CHECK(ai::ministerStyles(engineRules()).empty());  // no install

    GameState s = newEngineGame(3, 2, 12, true);
    const EmpireId me{0u};
    s.empire(me).race.style = "Testian";
    CHECK(ai::profileFor(rules, s.empire(me)).anger.regularDecrease == -4);  // no style: the race's files
    REQUIRE(apply(rules, s, me, cmd::SetMinisters{.style = std::string("Aggressive")}).ok);
    CHECK(ai::ministerStyleOf(s.empire(me)) == "Aggressive");
    CHECK(ai::profileFor(rules, s.empire(me)).anger.regularDecrease == -1);
    // Use Race Minister Style: the race's files again; the style choice is kept.
    REQUIRE(apply(rules, s, me, cmd::SetMinisters{.useRaceStyle = true}).ok);
    CHECK(ai::ministerStyleOf(s.empire(me)).empty());
    CHECK(s.empire(me).ministerStyle == "Aggressive");
    CHECK(ai::profileFor(rules, s.empire(me)).anger.regularDecrease == -4);
    // A style folder without the table falls back to Ai/Default, never to the race.
    REQUIRE(apply(rules, s, me, cmd::SetMinisters{.style = std::string("Defensive"), .useRaceStyle = false}).ok);
    CHECK(ai::profileFor(rules, s.empire(me)).anger.regularDecrease == -9);
    CHECK(ai::profileFor(rules, s.empire(me)).settings.personalityGroup == 2);
}

TEST_CASE("ai: a missed human turn is played with every minister on, the political step included") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId me{0u}, other{1u};
    s.empire(me).kind = PlayerKind::Human;
    meet(s, me, other);
    s.empire(me).ministers = ministerBit(Minister::Research);  // the player's Politics minister is off
    s.empire(me).relation(other).anger = 50;
    s.turn = 1;
    TurnContext ctx{r, s, {}, {}, {}};
    ai::politicalStep(ctx);
    CHECK(s.empire(me).relation(other).anger == 50);

    const ai::MinisterSettings saved = ai::standIn(s.empire(me));
    CHECK(ai::ministerOn(s.empire(me), Minister::Politics));
    ai::politicalStep(ctx);
    CHECK(s.empire(me).relation(other).anger == 50 + ai::builtinProfile().anger.regularDecrease);
    addTestVehicle(s, r, addWarship(s, r, me, "Picket"), locationOf(s.galaxy, homeworld(s, me).planet));
    CHECK_FALSE(ai::planOrders(r, s, me).empty());  // every minister acts on everything

    ai::restoreMinisters(s.empire(me), saved);
    CHECK(s.empire(me).ministers == ministerBit(Minister::Research));
    CHECK_FALSE(s.empire(me).ministerAll);
    CHECK_FALSE(ai::ministerOn(s.empire(me), Minister::Politics));
}

TEST_CASE("ai: computer players copy the four movement flags of AI_Settings each turn") {
    TempTree t("clearorders");
    t.write("Pictures/Races/Hasty/Hasty_AI_Settings.txt", "Clear orders on encounter enemy := True\n");
    t.write("Pictures/Races/Wary/Wary_AI_Settings.txt",
            "Clear orders on encounter enemy := True\nClear orders on encounter all := True\n"
            "Ships don't move through minefields := True\nShips don't move through restricted systems := True\n");
    const Rules rules{buildEngineRuleset(), t.root};
    GameState s = computerGame(4, 3, 0, 12);
    s.empire(EmpireId{0u}).race.style = "Hasty";
    s.empire(EmpireId{1u}).race.style = "Wary";
    s.empire(EmpireId{2u}).kind = PlayerKind::Human;
    s.empire(EmpireId{2u}).race.style = "Wary";
    s.empire(EmpireId{2u}).avoidTaggedMinefields = false;
    TurnContext ctx{rules, s, {}, {}, {}};
    ai::updateAiStates(ctx);
    CHECK(s.empire(EmpireId{0u}).clearOrdersOnEncounter == EncounterClear::Enemy);
    CHECK(s.empire(EmpireId{1u}).clearOrdersOnEncounter == EncounterClear::Any);
    CHECK(s.empire(EmpireId{2u}).clearOrdersOnEncounter == EncounterClear::Enemy);  // a human sets their own (on for a new empire)
    // Absent keys: the movement flags are off (spec 05 §7.5), whatever the empire had.
    CHECK_FALSE(s.empire(EmpireId{0u}).avoidTaggedMinefields);
    CHECK_FALSE(s.empire(EmpireId{0u}).avoidRestrictedSystems);
    CHECK(s.empire(EmpireId{1u}).avoidTaggedMinefields);
    CHECK(s.empire(EmpireId{1u}).avoidRestrictedSystems);
    CHECK_FALSE(s.empire(EmpireId{2u}).avoidTaggedMinefields);  // a human's own choice stays
    CHECK(s.empire(EmpireId{2u}).avoidRestrictedSystems);
}

TEST_CASE("ai: satellites and drones above the empire's kept shares are launched, each colony its whole stock") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, false);
    exploreEverything(s);
    const EmpireId enemy{0u}, cpu{1u};
    REQUIRE(s.empire(cpu).kind == PlayerKind::Computer);
    meet(s, enemy, cpu);
    s.empire(cpu).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(cpu).treaty = Treaty::War;
    const ObjectId planet = homeworld(s, cpu).planet;
    const Location home = locationOf(s.galaxy, planet);
    const DesignId sat = addTestDesign(s, r, cpu, "Sentinel", "Test Satellite Hull", {"Test Satellite Gun"});
    s.design(sat).designType = "Satellite";
    const DesignId recon = addTestDesign(s, r, cpu, "Watcher", "Test Satellite Hull", {"Test Sensor"});
    s.design(recon).designType = "Recon Satellite";
    const DesignId hunter = addTestDesign(s, r, cpu, "Hunter", "Test Drone Hull", {"Test Engine", "Test Warhead"});
    s.design(hunter).designType = "Anti-Ship Drone";
    const DesignId breaker = addTestDesign(s, r, cpu, "Breaker", "Test Drone Hull", {"Test Engine", "Test Warhead"});
    s.design(breaker).designType = "Anti-Planet Drone";
    homeworld(s, cpu).cargo.units = {{sat, 10}, {hunter, 10}, {breaker, 10}};
    const DesignId raider = addWarship(s, r, enemy, "Raider");
    const VehicleId intruder = addTestVehicle(s, r, raider, home).id;
    sight::updateKnowledge(r, s);
    Empire& e = s.empire(cpu);
    if (std::find(e.knowledge.visibleVehicles.begin(), e.knowledge.visibleVehicles.end(), intruder) == e.knowledge.visibleVehicles.end()) {
        e.knowledge.visibleVehicles.push_back(intruder);
        std::sort(e.knowledge.visibleVehicles.begin(), e.knowledge.visibleVehicles.end());
    }
    auto launches = [&](const GameState& g, ObjectId at) {
        ai::detail::Planner p(r, g, cpu, ai::detail::Mode::Computer, 3);
        ai::detail::planMinesSatellitesDrones(p);
        CHECK(p.dropped.empty());
        return p.st.colony(at)->orders;
    };

    // Satellites: 10 of 10 stored, 40 % kept: the excess (6) takes the whole
    // stock. Drones: an excess of 12, 6 a half; one ship target and one planet
    // target (the enemy homeworld), 3 drones each: the colony launches all its
    // drones, with no target.
    const std::vector<Order> all = launches(s, planet);
    REQUIRE(all.size() == 3);
    for (const Order& o : all) {
        CHECK(o.kind == OrderKind::LaunchUnits);
        CHECK(o.amount == -1);
        CHECK_FALSE(o.vehicle.valid());
        CHECK_FALSE(o.object.valid());
    }
    CHECK(all[0].design == sat);
    CHECK(all[1].design == hunter);
    CHECK(all[2].design == breaker);

    // Launched satellites still count toward the total: 6 in space and 4 in
    // cargo keep 4, so nothing more is launched.
    homeworld(s, cpu).cargo.units = {{sat, 4}};
    Vehicle& group = addTestVehicle(s, r, sat, home);
    group.count = 6;
    CHECK(launches(s, planet).empty());
    std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.id == group.id; });

    // Colony by colony in planet order: the first uses up the excess, a later
    // one launches only because it holds a Recon Satellite.
    std::optional<ObjectId> second;
    for (const SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::Planet && o.id > planet && !s.colony(o.id) && !second) second = o.id;
    REQUIRE(second);
    Colony extra;
    extra.planet = *second;
    extra.owner = cpu;
    extra.population = {{cpu, 100}};
    extra.cargo.units = {{sat, 1}};
    s.colonies[second->index()] = extra;
    homeworld(s, cpu).cargo.units = {{sat, 10}};
    CHECK(launches(s, planet).size() == 1);
    CHECK(launches(s, *second).empty());  // plain satellites: the excess is used up
    s.colony(*second)->cargo.units = {{recon, 1}};
    CHECK(launches(s, *second).size() == 1);

    // Nothing is launched at the unit limit.
    s.options.maxUnitsPerPlayer = 0;
    CHECK(launches(s, planet).empty());
}

TEST_CASE("ai: idle drones in range go after the targets, a set number per target") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, false);
    exploreEverything(s);
    const EmpireId enemy{0u}, cpu{1u};
    meet(s, enemy, cpu);
    s.empire(cpu).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(cpu).treaty = Treaty::War;
    const Location home = locationOf(s.galaxy, homeworld(s, cpu).planet);
    const DesignId raider = addWarship(s, r, enemy, "Raider");
    const VehicleId intruder = addTestVehicle(s, r, raider, home).id;
    const DesignId hunter = addTestDesign(s, r, cpu, "Hunter", "Test Drone Hull", {"Test Engine", "Test Warhead"});
    s.design(hunter).designType = "Anti-Ship Drone";
    std::vector<VehicleId> drones;
    for (int i = 0; i < 5; ++i) drones.push_back(addTestVehicle(s, r, hunter, home).id);
    sight::updateKnowledge(r, s);
    Empire& e = s.empire(cpu);
    if (std::find(e.knowledge.visibleVehicles.begin(), e.knowledge.visibleVehicles.end(), intruder) == e.knowledge.visibleVehicles.end()) {
        e.knowledge.visibleVehicles.push_back(intruder);
        std::sort(e.knowledge.visibleVehicles.begin(), e.knowledge.visibleVehicles.end());
    }
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 3);
    ai::detail::planMinesSatellitesDrones(p);
    int sent = 0;
    for (VehicleId d : drones)
        if (const Vehicle* v = p.st.vehicle(d); !v->orders.empty()) {
            CHECK(v->orders.front().kind == OrderKind::Attack);
            CHECK(v->orders.front().vehicle == intruder);
            ++sent;
        }
    CHECK(sent == p.prof.settings.antiShipDronesPerTarget);
}

TEST_CASE("ai: acknowledgements get a chatter reply from the response pools") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId a{0u}, b{1u};
    meet(s, a, b);
    s.empire(a).relation(b).treaty = s.empire(b).relation(a).treaty = Treaty::NonAggression;
    s.turn = 5;
    auto message = [&](EmpireId from, EmpireId to, MessageType type, uint32_t sent) {
        DiplomaticMessage m;
        m.id = MessageId{s.nextMessageId++};
        m.from = from;
        m.to = to;
        m.type = type;
        m.treaty = Treaty::NonAggression;
        m.sentTurn = sent;
        m.dated = sent;
        m.delivered = true;
        s.messages.push_back(m);
        return m.id;
    };
    // Acknowledgements are marked answered on delivery (they need no answer).
    // The answer window of a simultaneous turn: messages dated the ministers'
    // date (6) − 2.
    const MessageId proposal = message(a, b, MessageType::ProposeTreaty, 3);
    const MessageId stale = message(b, a, MessageType::RefuseTreaty, 1);    // outside the window
    const MessageId chatter = message(b, a, MessageType::General, 4);      // plain chatter gets no reply
    const MessageId accepted = message(b, a, MessageType::AcceptTreaty, 4); // the newest in the window
    s.messages.back().inReplyTo = proposal;
    for (DiplomaticMessage& m : s.messages) m.answered = true;

    auto replies = [&](const std::vector<Command>& cmds) {
        std::vector<DiplomaticMessage> out;
        for (const Command& c : cmds)
            if (const auto* send = as<cmd::SendMessage>(c); send && send->message.to == b && send->message.inReplyTo.valid())
                out.push_back(send->message);
        return out;
    };
    const auto cmds = ai::planTurn(r, s, a);
    const auto sent = replies(cmds);
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].inReplyTo == accepted);
    CHECK(sent[0].type == MessageType::General);
    CHECK(sent[0].text == std::format("{} welcomes the agreement.", s.empire(a).name));
    CHECK(applyAll(r, s, a, cmds).empty());
    for (const DiplomaticMessage& m : s.messages) CHECK(m.inReplyTo != stale);
    for (const DiplomaticMessage& m : s.messages) CHECK(m.inReplyTo != chatter);
    // Acknowledged once: the next turn's window holds nothing from b.
    for (Relation& rel : s.empire(a).relations) rel.messageSentThisTurn = false;
    ++s.turn;
    CHECK(replies(ai::planTurn(r, s, a)).empty());
}

TEST_CASE("ai: Construction_Units rows fill the cargo of full colonies whose queue is empty") {
    TempTree t("units");
    // The reserve record, then rows of Colony Type and entries with a maximum in kT (no AI State).
    t.write("Ai/Default_AI_Construction_Units.txt",
            "Percentage of Resources To Reserve For Unit Construction := 25\n"
            "Colony Type := Homeworld, Mining Colony\nNum Queue Entries := 3\n"
            "Entry 1 Type := Mine\nEntry 1 Maximum in kT := 500\nEntry 2 Type := satellite\nEntry 2 Maximum in kT := 500\n"
            "Entry 3 Type := Satellite\nEntry 3 Maximum in kT := 90000\n"
            "Colony Type := Farming Colony\nNum Queue Entries := 1\nEntry 1 Type := Satellite\nEntry 1 Maximum in kT := 10\n");
    // No ship list outside Attack, so only the units rows build.
    t.write("Ai/Default_AI_Construction_Vehicles.txt", "AI State := Attack\nNum Queue Entries := 1\nEntry 1 Type := Attack Ship\nEntry 1 Must Have At Least := 1\n");
    const Rules rules{buildEngineRuleset(), t.root};
    const ai::AiProfile prof = ai::loadProfile(t.root, "Nobody");
    CHECK(prof.unitsFile);
    CHECK(prof.unitReservePercent == 25);
    REQUIRE(prof.units.size() == 2);
    CHECK(prof.units[0].colonyType == "Homeworld, Mining Colony");
    REQUIRE(prof.units[0].entries.size() == 3);
    CHECK(prof.units[0].entries[2].type == "Satellite");
    CHECK(prof.units[0].entries[2].maxKt == 65'000);  // values above 65,000 read as 65,000
    // The last record whose Colony Type contains the colony's type, ignoring case.
    CHECK(prof.unitQueue("Homeworld") == &prof.units[0]);
    CHECK(prof.unitQueue("mining colony") == &prof.units[0]);
    CHECK(prof.unitQueue("Farming Colony") == &prof.units[1]);
    CHECK(prof.unitQueue("Resupply Base") == nullptr);
    CHECK(prof.unitQueue("") == nullptr);  // a colony without a type gets nothing

    GameState s = computerGame(8, 2, 0, 10, rules);
    const EmpireId me{0u};
    researchEverything(rules, s.empire(me));
    s.empire(me).stockpile = {1'000'000, 1'000'000, 1'000'000};
    const DesignId sat = addTestDesign(s, rules, me, "Sentinel", "Test Satellite Hull", {"Test Satellite Gun"});
    s.design(sat).designType = "Satellite";
    Colony& home = homeworld(s, me);
    home.queue.items.clear();
    home.cargo.units = {{sat, 1}};
    economy::updateReports(rules, s);
    const int slots = facilitySlots(rules, s, home);
    REQUIRE(slots > 0);
    home.facilities.resize(static_cast<size_t>(slots - 1), home.facilities.empty() ? 0u : home.facilities.front());
    {
        // A free facility slot: nothing.
        ai::detail::Planner p(rules, s, me, ai::detail::Mode::Computer, 3);
        REQUIRE(p.state == ai::AiState::Exploration);
        ai::detail::planShips(p);
        CHECK(p.st.colony(home.planet)->queue.items.empty());
    }
    // No free slot: one batch of as many as the queue finishes in one turn.
    home.facilities.resize(static_cast<size_t>(slots), home.facilities.empty() ? 0u : home.facilities.front());
    ai::detail::Planner p(rules, s, me, ai::detail::Mode::Computer, 3);
    ai::detail::planShips(p);
    // No mine design: the Mine entry is skipped; "satellite" names no type exactly.
    const ConstructionQueue& q = p.st.colony(home.planet)->queue;
    REQUIRE(q.items.size() == 1);
    CHECK(q.items[0].design == sat);
    const Resources rate = economy::constructionRate(rules, p.st, me, {home.planet, {}});
    const Resources each = computeDesignStats(rules, &s.empire(me), s.design(sat)).cost;
    int64_t batch = INT64_MAX;
    for (Resource k : kResources)
        if (each[k] > 0) batch = std::min(batch, rate[k] / each[k]);
    CHECK(q.items[0].count == std::max<int64_t>(1, batch));
    CHECK(p.dropped.empty());
}

TEST_CASE("ai: the units reserve is what the nearest acting empire's units step left, 0 without a units file") {
    TempTree t("reserve");
    t.write("Ai/Default_AI_Construction_Units.txt", "Percentage of Resources To Reserve For Unit Construction := 25\n");
    const Rules withFile{buildEngineRuleset(), t.root};
    GameState s = computerGame(8, 3, 0, 10, withFile);
    CHECK(ai::unitReserveLeft(withFile, s.empire(EmpireId{0u})) == 25);
    CHECK(ai::unitReserveLeft(engineRules(), s.empire(EmpireId{0u})) == 0);
    // An acting empire's units step sets it (to 0 without a units file), an
    // empire whose ministers do not act leaves it alone.
    TurnContext ctx{withFile, s, {}, {}, {}};
    empireEndOfTurn(ctx, EmpireId{0u}, true);
    CHECK(ctx.unitReserve == 25);
    GameState g = computerGame(8, 3, 0, 10);
    TurnContext plain{engineRules(), g, {}, {}, {}};
    plain.unitReserve = 25;
    empireEndOfTurn(plain, EmpireId{1u}, false);  // ministers not acting
    CHECK(plain.unitReserve == 25);
    empireEndOfTurn(plain, EmpireId{1u}, true);   // acting, no units file
    CHECK(plain.unitReserve == 0);
    // A human whose Ship Construction minister is off leaves it alone too.
    g.empire(EmpireId{2u}).kind = PlayerKind::Human;
    g.empire(EmpireId{2u}).ministerAll = false;
    g.empire(EmpireId{2u}).ministers = ministerBit(Minister::Research);
    plain.unitReserve = 25;
    empireEndOfTurn(plain, EmpireId{2u}, true);
    CHECK(plain.unitReserve == 25);
}

TEST_CASE("ai: a random player's planet type and atmosphere: every pair but a Gas Giant without atmosphere") {
    TempTree t("environment");
    t.write("Pictures/Races/Floater/Floater_AI_General.txt", "Name := Floater\nPlanet Type := Gas Giant\nAtmosphere := None\n");
    t.write("Pictures/Races/Digger/Digger_AI_General.txt", "Name := Digger\nPlanet Type := Ice\nAtmosphere := Hydrogen\n");
    const Rules rules{buildEngineRuleset(), t.root};
    const ruleset::RacePreset* floater = findPreset(rules, "Floater");
    REQUIRE(floater);
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Rng rng(seed);
        const Race redrawn = ai::randomPlayerRace(rules, *floater, 2000, rng);
        CHECK_FALSE((redrawn.nativeSurface == "Gas Giant" && redrawn.atmosphere == "None"));
        CHECK((redrawn.nativeSurface == "Rock" || redrawn.nativeSurface == "Ice" || redrawn.nativeSurface == "Gas Giant"));
        CHECK((redrawn.atmosphere == "None" || redrawn.atmosphere == "Methane" || redrawn.atmosphere == "Oxygen" ||
               redrawn.atmosphere == "Hydrogen" || redrawn.atmosphere == "Carbon Dioxide"));
    }
    // Any other pair is kept, whether the data set has such planets or not.
    const ruleset::RacePreset* digger = findPreset(rules, "Digger");
    REQUIRE(digger);
    Rng rng(9);
    const Race kept = ai::randomPlayerRace(rules, *digger, 2000, rng);
    CHECK(kept.nativeSurface == "Ice");
    CHECK(kept.atmosphere == "Hydrogen");
}

// ---- Settled rules (spec 05 §7, confirmed: binary) ------------------------------------------------

namespace {

// A free planet (no colony) of a system, other than `except`.
std::optional<ObjectId> freePlanetIn(const GameState& s, SystemId sys, ObjectId except = {}) {
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (o != except && s.galaxy.object(o).kind == ObjectKind::Planet && !s.colony(o)) return o;
    return std::nullopt;
}

Colony& addColony(GameState& s, ObjectId planet, EmpireId owner, std::vector<PopulationGroup> people) {
    Colony c;
    c.planet = planet;
    c.owner = owner;
    c.population = std::move(people);
    s.colonies[planet.index()] = c;
    return *s.colony(planet);
}

void destroyEntry(const Rules& r, GameState& s, VehicleId id, std::string_view component) {
    Vehicle& v = *s.vehicle(id);
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (r.component(d.entries[i].component).name == component) {
            v.damage[i] = entryStructure(r, d, i);
            return;
        }
    FAIL("no component " << component);
}

std::vector<Order> ordersOf(const ai::detail::Planner& p, VehicleId id) { return p.st.vehicle(id)->orders; }

} // namespace

TEST_CASE("ai: a queue takes vehicles while its backlog, in whole turns per item, is under 5") {
    TempTree t("backlog");
    t.write("Ai/Default_AI_Construction_Vehicles.txt",
            "AI State := Exploration\nNum Queue Entries := 1\nEntry 1 Type := Attack Ship\nEntry 1 Must Have At Least := 100\n");
    const Rules rules{buildEngineRuleset(), t.root};
    GameState s = computerGame(8, 2, 0, 10, rules);
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    e.designs.clear();
    const DesignId warship = addWarship(s, rules, me, "Picket");
    e.economy = {};
    e.economy.colonies = Resources{100'000'000, 100'000'000, 100'000'000};
    Colony& home = homeworld(s, me);
    home.queue.items.clear();
    ai::detail::Planner p(rules, s, me, ai::detail::Mode::Computer, 3);
    REQUIRE(p.state == ai::AiState::Exploration);
    ai::detail::planShips(p);
    const Resources rate = economy::constructionRate(rules, p.st, me, {home.planet, {}});
    const Resources cost = computeDesignStats(rules, &e, s.design(warship)).cost;
    int64_t turns = 0;
    for (Resource k : kResources)
        if (rate[k] > 0 && cost[k] > 0) turns = std::max(turns, (cost[k] + rate[k] - 1) / rate[k]);
    REQUIRE(turns >= 1);
    int64_t items = 0;
    for (const auto& c : p.st.colonies)
        if (c && c->owner == me)
            for (const QueueItem& q : c->queue.items) items += q.kind == QueueItem::Kind::Vehicle;
    // Placed while (items so far × turns) < 5: at most five one-turn items.
    CHECK(items == (5 + turns - 1) / turns);
}

TEST_CASE("ai: the ministers see the advanced date in simultaneous games, the unadvanced one in turn-based games") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12, true);
    const EmpireId me{1u};
    // Its first designs, then a better engine that no research event announced.
    ai::designMinisterRun(r, s, me);
    REQUIRE_FALSE(s.empire(me).designs.empty());
    s.empire(me).techLevels[techArea(r, "Test Propulsion").index()] = 3;
    s.turn = 9;
    s.options.simultaneous = true;
    CHECK(ai::aiDate(s) == 10);
    CHECK(countOf<cmd::CreateDesign>(ai::planTurn(r, s, me)) >= 1);  // the tenth date
    s.options.simultaneous = false;
    CHECK(ai::aiDate(s) == 9);
    CHECK(countOf<cmd::CreateDesign>(ai::planTurn(r, s, me)) == 0);
    s.turn = 10;
    CHECK(countOf<cmd::CreateDesign>(ai::planTurn(r, s, me)) >= 1);
}

TEST_CASE("ai: a transport delivers only where every carried race breathes, or to a domed colony hosting one of them") {
    ruleset::Ruleset rs = buildEngineRuleset();
    for (const auto& c : std::vector<ruleset::Component>(rs.components))
        if (c.name == "Test Cargo Bay") {
            ruleset::Component med = c;
            med.name = "Test Medical Bay";
            ruleset::Ability a;
            a.type = "Medical Bay";
            a.value1 = "10";
            med.abilities.push_back(a);
            rs.components.push_back(med);
        }
    const Rules r{std::move(rs), {}};
    GameState s = computerGame(21, 2, 0, 10, r);
    const EmpireId me{0u}, other{1u};
    s.empire(me).race.atmosphere = "Oxygen";
    s.empire(other).race.atmosphere = "Hydrogen";
    const Colony& home = homeworld(s, me);
    const ObjectId homePlanet = home.planet;
    const SystemId homeSys = s.galaxy.object(homePlanet).system;
    const auto a = freePlanetIn(s, homeSys);
    REQUIRE(a);
    const auto b = freePlanetIn(s, homeSys, *a);
    REQUIRE(b);
    s.galaxy.object(*a).atmosphere = "Oxygen";   // only our race breathes there
    s.galaxy.object(*b).atmosphere = "Methane";  // nobody breathes: domed
    addColony(s, *a, me, {{me, 1}});
    addColony(s, *b, me, {{other, 1}});
    const DesignId hauler = addTestDesign(s, r, me, "Hauler", "Test Transport Hull",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay", "Test Cargo Bay"});
    s.design(hauler).designType = "Population Transport";
    Vehicle& v = addTestVehicle(s, r, hauler, locationOf(s.galaxy, homePlanet));
    const VehicleId id = v.id;
    const int capacity = vehicleCargoCapacity(r, s, v);
    int64_t each = 1;
    for (;;) {
        v.cargo.population = {{me, each}, {other, each}};
        if (cargoSpaceUsed(r, s, v.cargo) * 2 > capacity) break;
        ++each;
    }
    REQUIRE(cargoSpaceUsed(r, s, v.cargo) <= capacity);
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 5);
    ai::detail::planTransports(p);
    const auto orders = ordersOf(p, id);
    REQUIRE_FALSE(orders.empty());
    CHECK(orders.back().kind == OrderKind::DropCargo);
    CHECK(orders.back().object == *b);  // not the undomed colony only one of the races breathes

    // An empty transport loads at a safe colony of 1000M or more whose atmosphere is wanted.
    s.vehicle(id)->cargo = {};
    REQUIRE(homeworld(s, me).totalPopulation() >= 1000);
    ai::detail::Planner q(r, s, me, ai::detail::Mode::Computer, 5);
    ai::detail::planTransports(q);
    const auto load = ordersOf(q, id);
    REQUIRE_FALSE(load.empty());
    CHECK(load.back().kind == OrderKind::LoadCargo);
    CHECK(load.back().location == locationOf(s.galaxy, homePlanet));

    // A transport with a Medical Bay gets no order.
    const DesignId medic = addTestDesign(s, r, me, "Medic", "Test Transport Hull",
                                         {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Medical Bay"});
    s.design(medic).designType = "Population Transport";
    const VehicleId medicId = addTestVehicle(s, r, medic, locationOf(s.galaxy, homePlanet)).id;
    ai::detail::Planner m(r, s, me, ai::detail::Mode::Computer, 5);
    ai::detail::planTransports(m);
    CHECK(ordersOf(m, medicId).empty());
}

TEST_CASE("ai: facility upgrades go on while what was queued is at most half the net income; queued old versions switch") {
    const Rules& r = engineRules();
    GameState s = computerGame(8, 2, 0, 10);
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    researchEverything(r, e);
    const uint32_t oldMine = facilityIndex(r, "Test Mine"), newMine = facilityIndex(r, "Test Mine II");
    Colony& home = homeworld(s, me);
    const ObjectId homePlanet = home.planet;
    home.facilities = {oldMine};
    home.queue.items.clear();
    const auto second = freePlanetIn(s, s.galaxy.object(homePlanet).system);
    REQUIRE(second);
    Colony& other = addColony(s, *second, me, {{me, 100}});
    other.facilities = {oldMine};
    QueueItem queued;
    queued.kind = QueueItem::Kind::Facility;
    queued.facility = oldMine;
    queued.spent = {10, 0, 0};
    other.queue.items = {queued};
    e.economy = {};  // a net income of 0
    s.turn = 4;      // date 5
    auto upgrades = [&](const ai::detail::Planner& p, ObjectId planet) {
        int n = 0;
        for (const QueueItem& q : p.st.colony(planet)->queue.items) n += q.kind == QueueItem::Kind::Upgrade;
        return n;
    };
    const ObjectId first = std::min(homePlanet, *second), later = std::max(homePlanet, *second);
    {
        ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 5);
        ai::detail::planFacilities(p, false);
        CHECK(upgrades(p, first) == 1);  // a zero budget still upgrades the first planet
        CHECK(upgrades(p, later) == 0);
    }
    e.economy.colonies = Resources{1'000'000, 1'000'000, 1'000'000};
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 5);
    ai::detail::planFacilities(p, false);
    CHECK(upgrades(p, first) == 1);
    CHECK(upgrades(p, later) == 1);
    // The queued old version switched to the newest in place, keeping what was
    // paid into it (spec 02 §6.6, spec 05 §7.5).
    const auto& items = p.st.colony(*second)->queue.items;
    REQUIRE_FALSE(items.empty());
    CHECK(items.front().kind == QueueItem::Kind::Facility);
    CHECK(items.front().facility == newMine);
    CHECK(items.front().spent == Resources{10, 0, 0});
    bool replaced = false;
    for (const Command& c : p.out) replaced = replaced || std::holds_alternative<cmd::QueueReplaceFacility>(c);
    CHECK(replaced);
}

TEST_CASE("commands: a queued facility switches to another level of its family in place") {
    const Rules& r = engineRules();
    GameState s = computerGame(8, 2, 0, 10);
    const EmpireId me{0u}, other{1u};
    researchEverything(r, s.empire(me));
    const uint32_t oldMine = facilityIndex(r, "Test Mine"), newMine = facilityIndex(r, "Test Mine II");
    Colony& home = homeworld(s, me);
    QueueItem queued;
    queued.kind = QueueItem::Kind::Facility;
    queued.facility = oldMine;
    queued.count = 2;
    queued.spent = {40, 5, 0};
    home.queue.items = {queued};
    const cmd::QueueTarget target{home.planet, {}};
    // Only an own planet's queued facility, to a researched level of the same family.
    CHECK_FALSE(apply(r, s, other, cmd::QueueReplaceFacility{target, 0, newMine}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::QueueReplaceFacility{target, 1, newMine}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::QueueReplaceFacility{target, 0, static_cast<uint32_t>(r.data().facilities.size())}).ok);
    for (uint32_t f = 0; f < r.data().facilities.size(); ++f)
        if (r.facility(f).family != r.facility(oldMine).family) {
            CHECK_FALSE(apply(r, s, me, cmd::QueueReplaceFacility{target, 0, f}).ok);
            break;
        }
    REQUIRE(apply(r, s, me, cmd::QueueReplaceFacility{target, 0, newMine}).ok);
    const QueueItem& now = homeworld(s, me).queue.items.front();
    CHECK(now.facility == newMine);
    CHECK(now.count == 2);
    CHECK(now.spent == Resources{40, 5, 0});
}

TEST_CASE("ai: the current player at a battle counts it as Attacking, whatever the place") {
    TempTree t("attacking");
    t.write("Ai/Default_AI_Anger.txt",
            "Per Attack Location := 0\nPer No Treaty Ship := 0\nPer Enemy Ship := 0\nRegular Decrease := 0\nMega Evil Empire := 0\n"
            "Combat Attacking Won := 30\nCombat Defending Won := 7\nMinimum Anger := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(5, 3, 0, 10, r);
    const EmpireId me{0u}, enemy{1u};
    meet(s, me, enemy);
    s.empire(me).relation(enemy).treaty = s.empire(enemy).relation(me).treaty = Treaty::War;
    auto angerAfter = [&](EmpireId current, bool extras = false) {
        GameState g = s;
        g.turn = 3;
        g.empire(me).relation(enemy).anger = 50;
        CombatRecord rec;
        rec.turn = 2;
        rec.location = locationOf(g.galaxy, homeworld(g, me).planet);  // in our territory
        rec.participants = {me, enemy};
        rec.currentPlayer = current;
        CombatPiece ours, theirs;
        ours.owner = me;
        theirs.owner = enemy;
        rec.pieces = {ours, theirs};
        CombatEvent gone;
        gone.kind = CombatEvent::Kind::Destroyed;
        gone.piece = 1;
        rec.events = {gone};
        if (extras) {
            // A star (a neutral obstacle belongs to no empire) and an enemy ship we took:
            // still a victory (spec 04 §15 "The verdict").
            CombatPiece star, prize;
            star.kind = CombatPiece::Kind::Obstacle;
            prize.owner = enemy;
            rec.pieces.push_back(star);
            rec.pieces.push_back(prize);
            CombatEvent taken;
            taken.kind = CombatEvent::Kind::Captured;
            taken.piece = 3;
            taken.amount = static_cast<int32_t>(me.value);
            rec.events.push_back(taken);
        }
        g.combats = {rec};
        TurnContext ctx{r, g, {}, {}, {}};
        ai::politicalStep(ctx, me, 2u);
        return g.empire(me).relation(enemy).anger;
    };
    CHECK(angerAfter(me) == 80);                 // Attacking Won, in our own territory
    CHECK(angerAfter(EmpireId{2u}) == 57);       // the highest player number was current: Defending
    CHECK(angerAfter(me, true) == 80);           // the same verdict with an obstacle and a capture
}

TEST_CASE("ai: the strength rating counts Boarding Attack, carried fighters and half shields with their fraction") {
    ruleset::Ruleset rs = buildEngineRuleset();
    for (auto& c : rs.components)
        if (c.name == "Test Shield")
            for (auto& a : c.abilities) a.value1 = "15";
    const Rules r{std::move(rs), {}};
    GameState s = newEngineGame(3, 2, 8, true);
    const EmpireId me{0u};
    const Location at = locationOf(s.galaxy, homeworld(s, me).planet);
    auto rating = [&](std::initializer_list<std::string_view> parts, std::string_view hull = "Test Cruiser") {
        const DesignId d = addTestDesign(s, r, me, "Probe", hull, parts);
        return addTestVehicle(s, r, d, at).id;
    };
    const auto crew = {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"};
    (void)crew;
    // Boarding Attack 20 alone: 20.
    const VehicleId boarder = rating({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Boarding Party"});
    CHECK(ai::detail::vehicleRating(r, s, *s.vehicle(boarder)) == 20 * ai::detail::kStrengthScale);
    // A laser (best 12) and a shield of 15: 12 + 7.5.
    const VehicleId shielded = rating({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser", "Test Shield"});
    CHECK(ai::detail::vehicleRating(r, s, *s.vehicle(shielded)) == 195);
    // Shields alone count nothing.
    const VehicleId shieldOnly = rating({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Shield"});
    CHECK(ai::detail::vehicleRating(r, s, *s.vehicle(shieldOnly)) == 0);
    // An unarmed carrier: 3 fighters with a gun of best damage 6 add 18.
    const DesignId fighter = addTestDesign(s, r, me, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const VehicleId carrier = rating({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Fighter Bay"});
    s.vehicle(carrier)->cargo.units = {{fighter, 3}};
    CHECK(ai::detail::vehicleRating(r, s, *s.vehicle(carrier)) == 18 * ai::detail::kStrengthScale);
    // A destroyed weapon does not count.
    destroyEntry(r, s, shielded, "Test Laser");
    CHECK(ai::detail::vehicleRating(r, s, *s.vehicle(shielded)) == 0);
}

TEST_CASE("ai: territory and jumps use every warp link, known or not") {
    GameState s = computerGame(13, 2, 0, 12);
    const EmpireId me{0u};
    s.empire(me).knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 0);
    const SystemId home = ai::detail::homeSystem(s, me);
    const std::vector<SystemId> territory = ai::detail::computeTerritory(s, me);
    for (SystemId nb : s.galaxy.neighbors(home)) CHECK(std::binary_search(territory.begin(), territory.end(), nb));
    const std::vector<int> jumps = ai::detail::jumpsOver(s, home);
    for (SystemId nb : s.galaxy.neighbors(home)) CHECK(jumps[nb.index()] == 1);
}

TEST_CASE("ai: mine layers load at the nearest colony holding mines and lay them at a warp point toward another empire") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, false);
    exploreEverything(s);
    const EmpireId enemy{0u}, cpu{1u};
    meet(s, enemy, cpu);
    s.empire(cpu).relation(enemy).treaty = s.empire(enemy).relation(cpu).treaty = Treaty::War;
    researchEverything(r, s.empire(cpu));
    const ObjectId homePlanet = homeworld(s, cpu).planet;
    const Location home = locationOf(s.galaxy, homePlanet);
    const DesignId mine = addTestDesign(s, r, cpu, "Spike", "Test Mine Hull", {"Test Warhead"});
    s.design(mine).designType = "Mine";
    const DesignId layer = addTestDesign(s, r, cpu, "Sower", "Test Frigate",
                                         {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Mine Layer"});
    s.design(layer).designType = "Mine Layer";
    homeworld(s, cpu).cargo.units = {{mine, 20}};
    Location away = home;
    away.sector = Sector{home.sector.x == 0 ? 1 : 0, home.sector.y};
    const VehicleId id = addTestVehicle(s, r, layer, away).id;
    // An enemy ship beyond one of our home system's warp points.
    const auto wps = s.galaxy.warpPoints(home.system);
    REQUIRE_FALSE(wps.empty());
    const ObjectId gate = wps.front();
    const Location beyond = locationOf(s.galaxy, s.galaxy.object(gate).destination);
    addTestVehicle(s, r, addWarship(s, r, enemy, "Lurker"), beyond);
    {
        ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 3);
        ai::detail::planMinesSatellitesDrones(p);
        const auto orders = ordersOf(p, id);
        REQUIRE_FALSE(orders.empty());
        CHECK(orders.back().kind == OrderKind::LoadCargo);
        CHECK(orders.back().design == mine);
        CHECK(orders.back().location == home);
    }
    s.vehicle(id)->cargo.units = {{mine, 8}};
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 3);
    ai::detail::planMinesSatellitesDrones(p);
    const auto orders = ordersOf(p, id);
    REQUIRE(orders.size() >= 2);
    CHECK(orders.back().kind == OrderKind::LaunchUnits);
    CHECK(orders.back().design == mine);
    // The site is a warp point of our colony system whose far system holds another empire's object.
    const Location site = orders.back().location;
    bool atGate = false;
    for (ObjectId wp : s.galaxy.warpPoints(home.system)) {
        if (locationOf(s.galaxy, wp) != site) continue;
        const SystemId far = s.galaxy.object(s.galaxy.object(wp).destination).system;
        for (const Vehicle& v : s.vehicles) atGate = atGate || (v.owner == enemy && v.location.system == far);
        for (const auto& c : s.colonies) atGate = atGate || (c && c->owner == enemy && s.galaxy.object(c->planet).system == far);
    }
    CHECK(atGate);
    // At the unit limit layers do nothing.
    s.options.maxUnitsPerPlayer = 0;
    ai::detail::Planner full(r, s, cpu, ai::detail::Mode::Computer, 3);
    ai::detail::planMinesSatellitesDrones(full);
    CHECK(ordersOf(full, id).empty());
}

TEST_CASE("ai: an attack candidate's value is the foreign ratings in its sector plus the planet's defence") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, false);
    exploreEverything(s);
    const EmpireId enemy{0u}, cpu{1u};
    meet(s, enemy, cpu);
    s.empire(cpu).relation(enemy).treaty = s.empire(enemy).relation(cpu).treaty = Treaty::War;
    Colony& target = homeworld(s, enemy);
    const ObjectId planet = target.planet;
    const Location at = locationOf(s.galaxy, planet);
    target.facilities.push_back(facilityIndex(r, "Test Planet Shield"));  // 100: 20 points
    const DesignId troop = addTestDesign(s, r, enemy, "Grunt", "Test Troop Hull", {"Test Troop Rifle"});
    target.cargo.units = {{troop, 5}};  // 5 × 8
    std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.location == at; });
    addTestVehicle(s, r, addWarship(s, r, enemy, "Guard"), at);  // a laser: 12
    addTestVehicle(s, r, addWarship(s, r, cpu, "Visitor"), at);   // ours: not counted
    const int64_t shields = sumValue1(colonyAbilities(r, s, *s.colony(planet)), AbilityKind::PlanetShieldGeneration);
    const int64_t expected = 12 * 10 + shields * 10 / 5 + s.colony(planet)->totalPopulation() / 100 * 10 + 5 * 8 * 10;
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 3);
    bool found = false;
    for (const ai::detail::Candidate& c : p.sit.candidates)
        if (c.planet == planet) {
            found = true;
            CHECK(c.value == expected);
        }
    CHECK(found);
}

TEST_CASE("ai: defend entries are ordered by jumps, our population at stake, planet sectors, then the threat") {
    using ai::detail::DefendEntry;
    auto entry = [](int id, int jumps, int64_t pop, bool planet, int64_t threat) {
        DefendEntry d;
        d.owner = EmpireId{static_cast<uint32_t>(id)};
        d.jumps = jumps;
        d.ourMaxPopulation = pop;
        d.planetSector = planet;
        d.threat = threat;
        return d;
    };
    const std::vector<DefendEntry> base{entry(1, 1, 0, false, 50), entry(2, 0, 100, true, 30), entry(3, 0, 100, true, 10),
                                        entry(4, 0, 0, false, 5), entry(5, 0, 0, false, 70)};
    auto order = [&](bool weakest) {
        std::vector<DefendEntry> list = base;
        ai::detail::sortDefendEntries(list, weakest);
        std::vector<uint32_t> ids;
        for (const DefendEntry& d : list) ids.push_back(d.owner.value);
        return ids;
    };
    CHECK(order(false) == std::vector<uint32_t>{3, 2, 5, 4, 1});
    CHECK(order(true) == std::vector<uint32_t>{3, 2, 4, 5, 1});
}

TEST_CASE("ai: the 4-jump test starts on the 6th turn in the state and counts per target") {
    GameState s = computerGame(13, 2, 0, 12);
    exploreEverything(s);
    const EmpireId me{0u}, enemy{1u};
    meet(s, me, enemy);
    s.empire(me).relation(enemy).treaty = s.empire(enemy).relation(me).treaty = Treaty::War;
    s.vehicles.clear();
    const SystemId home = ai::detail::homeSystem(s, me);
    const SystemId enemyHome = ai::detail::homeSystem(s, enemy);
    const std::vector<int> fromHome = ai::detail::jumpsOver(s, home);
    REQUIRE(fromHome[enemyHome.index()] <= 4);
    AiMemory& m = s.empire(me).aiMemory;
    m.targets = {enemyHome};
    m.staging = home;
    s.empire(me).aiState = static_cast<int>(ai::AiState::PrepareForAttack);
    const Rules& r = engineRules();
    // Our strength near the target (our home colony, 1) is not above 3 × theirs (1).
    s.empire(me).aiTurnsInState = 4;
    CHECK(ai::nextState(r, s, me) == ai::AiState::PrepareForAttack);
    s.empire(me).aiTurnsInState = 5;
    CHECK(ai::nextState(r, s, me) == ai::AiState::Infrastructure);
    // Two points at home, and a second target near home: home counts once per target (4 > 3).
    const DesignId sat = addTestDesign(s, r, me, "Buoy", "Test Satellite Hull", {"Test Satellite Gun"});
    addTestVehicle(s, r, sat, {home, Sector{kSystemCenter, kSystemCenter}}).count = 1;
    std::optional<SystemId> second;
    for (size_t i = 0; i < fromHome.size() && !second; ++i)
        if (SystemId{i} != home && SystemId{i} != enemyHome && fromHome[i] <= 4) second = SystemId{i};
    REQUIRE(second);
    const std::vector<int> fromSecond = ai::detail::jumpsOver(s, *second);
    REQUIRE(fromSecond[home.index()] <= 4);
    CHECK(ai::nextState(r, s, me) == ai::AiState::Infrastructure);  // one target: 2 is not above 3
    m.targets = {enemyHome, *second};
    CHECK(ai::nextState(r, s, me) == ai::AiState::Attack);  // 2 + 2 > 3, and the staging system is stronger
}

TEST_CASE("ai: design names count the Design minister's designs and skip names any empire uses") {
    TempTree t("names2");
    t.writePlain("Dsgnname/SHORT.TXT", "Alder\r\nBirch\r\n");
    Rules rules{buildEngineRuleset(), t.root};
    GameState s = computerGame(3, 2, 0, 8, rules);
    const EmpireId me{0u}, other{1u};
    s.empire(me).race.designNameFile = "Short.txt";
    s.empire(me).designs.clear();
    // One design already made by the minister: the counter is 1.
    Design made;
    made.owner = me;
    made.name = "Old";
    made.designType = "Mine";
    made.templateName = "Mine";
    made.hull = 0;
    made.obsolete = true;
    addDesign(s, made);
    // Another empire uses "Alder II".
    Design taken;
    taken.owner = other;
    taken.name = "Alder II";
    addDesign(s, taken);
    s.turn = 9;
    std::vector<std::string> names;
    for (const Command& c : ai::planTurn(rules, s, me))
        if (auto* d = as<cmd::CreateDesign>(c)) {
            names.push_back(d->design.name);
            CHECK_FALSE(d->design.templateName.empty());
        }
    REQUIRE(names.size() >= 3);
    CHECK(names[0] == "Birch");     // position 2, beyond the counter
    CHECK(names[1] == "Birch II");  // position 4: "Alder II" (position 3) is in use
    CHECK(names[2] == "Alder III");
    // Without a file: "Design <counter + 1>".
    s.empire(me).race.designNameFile.clear();
    for (const Command& c : ai::planTurn(rules, s, me))
        if (auto* d = as<cmd::CreateDesign>(c)) {
            CHECK(d->design.name == "Design 2");
            break;
        }
}

TEST_CASE("ai: a Race Opt trait that does not fit is skipped and later ones are still tried") {
    TempTree t("raceopt2");
    t.write("Pictures/Races/Opto/Opto_AI_General.txt",
            "Name := Opto\n"
            "Race Opt 1 Num Characteristics := 2\nRace Opt 1 Characteristic 1 Type := Intelligence\nRace Opt 1 Characteristic 1 Amount := 255\n"
            "Race Opt 1 Characteristic 2 Type := Reproduction\nRace Opt 1 Characteristic 2 Amount := 105\n"
            "Race Opt 1 Num Advanced Traits := 2\nRace Opt 1 Adv Trait 1 := Night Eyes\nRace Opt 1 Adv Trait 2 := Day Eyes\n");
    ruleset::Ruleset data = buildEngineRuleset();
    data.settings.set("Characteristic Intelligence Pct Cost", "10");
    data.settings.set("Characteristic Reproduction Pct Cost", "10");
    const Rules rules{std::move(data), t.root};
    const ruleset::RacePreset* preset = findPreset(rules, "Opto");
    REQUIRE(preset);
    Rng rng(3);
    const Race race = ai::randomPlayerRace(rules, *preset, 2000, rng);
    // Intelligence 255 costs 1550 and Reproduction 105 another 50: 1600. Night
    // Eyes (500) does not fit and is skipped; Day Eyes (400) still fits.
    CHECK(race.characteristic(Characteristic::Intelligence) == 255);
    CHECK(race.characteristic(Characteristic::Reproduction) == 105);
    CHECK(racialPointCost(rules, race) <= 2000);
    REQUIRE(race.traits.size() == 1);
    CHECK(rules.data().racialTraits[race.traits.front()].name == "Day Eyes");
}

TEST_CASE("ai: designs of no AI type are typed by what they carry") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 1, 8, true);
    const EmpireId me{0u};
    auto typeOf = [&](std::string_view hull, std::initializer_list<std::string_view> parts, std::string label = {}) {
        const DesignId d = addTestDesign(s, r, me, "Probe", hull, parts);
        s.design(d).designType = std::move(label);
        return ai::detail::aiTypeOf(r, s.design(d), computeDesignStats(r, nullptr, s.design(d)));
    };
    CHECK(typeOf("Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"}) == "Attack Ship");
    CHECK(typeOf("Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay"}) ==
          "Population Transport");
    CHECK(typeOf("Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Boarding Party"}) ==
          "Boarding Ship");
    CHECK(typeOf("Test Cruiser", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Planet Maker"}) ==
          "Create Planet");
    CHECK(typeOf("Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Yard Module"}) == "Base Space Yard");
    CHECK(typeOf("Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Laser"}) == "Defense Base");
    CHECK(typeOf("Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Rock Pod"}) == "Colony (Rock)");
    // A label is one of the 39 names only when it matches exactly.
    CHECK(typeOf("Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"}, "Carrier") == "Carrier");
    CHECK(typeOf("Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"}, "carrier") == "Attack Ship");
}

TEST_CASE("ai: Allow Surrender gates the computer's answer and the Surrender message") {
    TempTree t("surrender");
    t.write("Ai/Default_AI_Politics.txt",
            "Score Percent To Accept Demand your surrender := 0\nWill Accept From Enemy Demand your surrender := True\n"
            "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 2, 0, 10, r);
    const EmpireId asker{0u}, cpu{1u};
    meet(s, asker, cpu);
    REQUIRE(s.options.allowSurrender);  // on by default
    const MessageId demand = deliver(s, asker, cpu, MessageType::DemandSurrender);
    std::optional<DiplomaticMessage> reply;
    for (uint32_t turn = 0; turn < 10 && !reply; ++turn) {
        s.turn = turn;
        reply = sentInReply(r, s, cpu, demand);
    }
    REQUIRE(reply);
    CHECK(reply->type == MessageType::Surrender);
    // A human's ministers never surrender: a General message instead.
    s.empire(cpu).kind = PlayerKind::Human;
    std::optional<DiplomaticMessage> refusal;
    for (uint32_t turn = 0; turn < 10 && !refusal; ++turn) {
        s.turn = turn;
        refusal = sentInReply(r, s, cpu, demand);
    }
    REQUIRE(refusal);
    CHECK(refusal->type == MessageType::General);
    s.empire(cpu).kind = PlayerKind::Computer;
    // With the option off the demand gets no answer at all...
    s.options.allowSurrender = false;
    for (uint32_t turn = 0; turn < 10; ++turn) {
        s.turn = turn;
        CHECK_FALSE(sentInReply(r, s, cpu, demand));
        CHECK_FALSE(answerTo(r, s, cpu, demand));
    }
    // ... and a Surrender message does nothing.
    GameState g = s;
    g.messages.clear();
    const MessageId gone = deliver(g, cpu, asker, MessageType::Surrender);
    for (DiplomaticMessage& m : g.messages)
        if (m.id == gone) m.delivered = false;
    TurnContext ctx{r, g, {}, {}, {}};
    auto owns = [&](EmpireId e) { return std::any_of(g.colonies.begin(), g.colonies.end(), [&](const auto& c) { return c && c->owner == e; }); };
    REQUIRE(owns(cpu));
    diplomacy::deliverMessages(ctx);
    CHECK(g.empire(cpu).alive);
    CHECK(owns(cpu));
    g.options.allowSurrender = true;
    const MessageId real = deliver(g, cpu, asker, MessageType::Surrender);
    for (DiplomaticMessage& m : g.messages)
        if (m.id == real) m.delivered = false;
    diplomacy::deliverMessages(ctx);
    // Everything passes; the empire is destroyed at its next destruction check (spec 05 §3.4).
    CHECK_FALSE(owns(cpu));
}

TEST_CASE("ai: colonization danger: any object seen or not, and one per empire beyond each warp point") {
    GameState s = computerGame(17, 3, 0, 14);
    exploreEverything(s);
    const EmpireId me{0u}, stranger{1u};
    const Rules& r = engineRules();
    researchEverything(r, s.empire(me));
    ai::detail::Planner before(r, s, me, ai::detail::Mode::Computer, 1);
    REQUIRE_FALSE(before.sit.colonyTargets.empty());
    // Take a target system T and a neighbour N where nobody else is present.
    std::optional<ai::detail::ColonyTarget> pick;
    std::optional<SystemId> quiet;
    auto nobodyElse = [&](SystemId sys) {
        for (const Vehicle& v : s.vehicles)
            if (v.owner != me && v.location.system == sys) return false;
        for (const auto& c : s.colonies)
            if (c && s.galaxy.object(c->planet).system == sys) return false;  // and no colony of ours
        return true;
    };
    for (const ai::detail::ColonyTarget& t : before.sit.colonyTargets) {
        if (!nobodyElse(t.system)) continue;
        for (SystemId nb : s.galaxy.neighbors(t.system))
            if (nb != t.system && nobodyElse(nb)) {
                pick = t;
                quiet = nb;
                break;
            }
        if (pick) break;
    }
    REQUIRE(pick);
    int links = 0;
    for (SystemId nb : s.galaxy.neighbors(pick->system)) links += nb == *quiet;
    // An unmet empire's ship, which we do not see, in the neighbour.
    const DesignId lurker = addWarship(s, r, stranger, "Lurker");
    addTestVehicle(s, r, lurker, {*quiet, Sector{0, 0}});
    ai::detail::Planner after(r, s, me, ai::detail::Mode::Computer, 1);
    for (const ai::detail::ColonyTarget& t : after.sit.colonyTargets)
        if (t.planet == pick->planet) CHECK(t.danger == pick->danger + links);
    // A second empire there adds 1 more per warp point; a second ship of the same one adds nothing.
    const EmpireId third{2u};
    addTestVehicle(s, r, lurker, {*quiet, Sector{1, 0}});
    addTestVehicle(s, r, addWarship(s, r, third, "Prowler"), {*quiet, Sector{0, 1}});
    ai::detail::Planner two(r, s, me, ai::detail::Mode::Computer, 1);
    for (const ai::detail::ColonyTarget& t : two.sit.colonyTargets)
        if (t.planet == pick->planet) CHECK(t.danger == pick->danger + 2 * links);
    std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.owner == third; });
    // In the target system itself: 5 more.
    addTestVehicle(s, r, lurker, {pick->system, Sector{0, 0}});
    ai::detail::Planner inside(r, s, me, ai::detail::Mode::Computer, 1);
    for (const ai::detail::ColonyTarget& t : inside.sit.colonyTargets)
        if (t.planet == pick->planet) CHECK(t.danger == pick->danger + links + 5);
    // A friend's ship there excludes nothing; a friend's colony there does.
    meet(s, me, stranger);
    s.empire(me).relation(stranger).treaty = s.empire(stranger).relation(me).treaty = Treaty::NonAggression;
    ai::detail::Planner friendlyShip(r, s, me, ai::detail::Mode::Computer, 1);
    bool listed = false;
    for (const ai::detail::ColonyTarget& t : friendlyShip.sit.colonyTargets) listed = listed || t.planet == pick->planet;
    CHECK(listed);
    const auto other = freePlanetIn(s, pick->system, pick->planet);
    if (other) {
        addColony(s, *other, stranger, {{stranger, 10}});
        ai::detail::Planner friendlyColony(r, s, me, ai::detail::Mode::Computer, 1);
        for (const ai::detail::ColonyTarget& t : friendlyColony.sit.colonyTargets) CHECK(t.system != pick->system);
    }
}

TEST_CASE("ai: a hostile empire's colony without population is a colonization target") {
    GameState s = computerGame(17, 2, 0, 12);
    exploreEverything(s);
    const Rules& r = engineRules();
    const EmpireId me{0u}, enemy{1u};
    researchEverything(r, s.empire(me));
    const SystemId enemyHome = ai::detail::homeSystem(s, enemy);
    const auto spare = freePlanetIn(s, enemyHome);
    REQUIRE(spare);
    addColony(s, *spare, enemy, {});  // no population
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 1);
    bool listed = false;
    for (const ai::detail::ColonyTarget& t : p.sit.colonyTargets)
        if (t.planet == *spare) {
            listed = true;
            CHECK(t.colonized);
        }
    CHECK(listed);
    // A populated one, or one of an empire at Non-Aggression, is not.
    s.colony(*spare)->population = {{enemy, 10}};
    ai::detail::Planner q(r, s, me, ai::detail::Mode::Computer, 1);
    for (const ai::detail::ColonyTarget& t : q.sit.colonyTargets) CHECK(t.planet != *spare);
}

TEST_CASE("ai: one-per-system facilities follow the fixed list") {
    TempTree t("onepersystem");
    t.write("Ai/Default_AI_Construction_Facilities.txt",
            "AI State := Exploration\nConstruction Queue Type := Mining Colony\nNum Queue Entries := 2\n"
            "Facility 1 Ability := Ship Training\nFacility 1 Amount := 1\n"
            "Facility 2 Ability := Planet - Shield Generation\nFacility 2 Amount := 1\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(8, 2, 0, 10, r);
    const EmpireId me{0u};
    researchEverything(r, s.empire(me));
    Colony& home = homeworld(s, me);
    const ObjectId homePlanet = home.planet;
    const auto second = freePlanetIn(s, s.galaxy.object(homePlanet).system);
    REQUIRE(second);
    Colony& c = addColony(s, *second, me, {{me, 500}});
    c.colonyType = "Mining Colony";
    auto firstQueued = [&](const GameState& g) -> std::string {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 2);
        ai::detail::planFacilities(p, false);
        const auto& items = p.st.colony(*second)->queue.items;
        return items.empty() ? std::string{} : r.facility(items.front().facility).name;
    };
    CHECK(firstQueued(s) == "Test Training Ground");
    // Ship Training is one per system: the homeworld's blocks it, Planet - Shield Generation is not on the list.
    homeworld(s, me).facilities.push_back(facilityIndex(r, "Test Training Ground"));
    homeworld(s, me).facilities.push_back(facilityIndex(r, "Test Planet Shield"));
    CHECK(firstQueued(s) == "Test Planet Shield");
}

TEST_CASE("ai: a ship that needs repair loses its orders and fleet every turn and seeks the nearest yard") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId me{0u};
    const Colony& home = homeworld(s, me);
    const Location yard = locationOf(s.galaxy, home.planet);
    REQUIRE(colonyHasSpaceYard(r, home));
    Location away = yard;
    away.sector = Sector{yard.sector.x == 0 ? 1 : 0, yard.sector.y};
    const DesignId warship = addWarship(s, r, me, "Lancer");
    const VehicleId hurt = addTestVehicle(s, r, warship, away).id;
    const VehicleId mate = addTestVehicle(s, r, warship, away).id;
    REQUIRE(apply(r, s, me, cmd::CreateFleet{"Pair", {mate, hurt}}).ok);
    destroyEntry(r, s, hurt, "Test Laser");  // strength 0
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
    ai::detail::planRepairAndResupply(p, true);
    CHECK_FALSE(p.st.vehicle(hurt)->fleet.valid());
    REQUIRE(ordersOf(p, hurt).size() == 1);
    CHECK(ordersOf(p, hurt).front().kind == OrderKind::MoveTo);
    CHECK(ordersOf(p, hurt).front().location == yard);
    CHECK(p.st.vehicle(mate)->fleet.valid());  // an undamaged ship stays

    // A colony ship with too many destroyed parts drops its Colonize order, and with no yard keeps no orders.
    GameState g = s;
    for (auto& c : g.colonies)
        if (c && c->owner == me) std::erase_if(c->facilities, [&](uint32_t f) { return hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard); });
    const DesignId settler = addTestDesign(g, r, me, "Settler", "Test Frigate",
                                           {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Rock Pod", "Test Supply Pod"});
    const VehicleId pod = addTestVehicle(g, r, settler, away).id;
    Order colonize;
    colonize.kind = OrderKind::Colonize;
    colonize.object = home.planet;
    colonize.location = yard;
    g.vehicle(pod)->orders = {colonize};
    for (std::string_view part : {"Test Rock Pod", "Test Supply Pod", "Test Crew Quarters"}) destroyEntry(r, g, pod, part);  // 3 > round(0.25 × 6)
    ai::detail::Planner q(r, g, me, ai::detail::Mode::Computer, 9);
    ai::detail::planRepairAndResupply(q, true);
    CHECK(ordersOf(q, pod).empty());
}

TEST_CASE("ai: resupply follows the supply distance; colony ships are exempt; a depot, else a colony, else home") {
    ruleset::Ruleset rs = buildEngineRuleset();
    for (auto& c : rs.components)
        if (c.name == "Test Engine") c.supplyUsed = 10;
    const Rules r{std::move(rs), {}};
    GameState s = computerGame(4, 2, 0, 10, r);
    const EmpireId me{0u};
    Colony& home = homeworld(s, me);
    const Location homeAt = locationOf(s.galaxy, home.planet);
    if (!movement::resupplyDepotAt(r, s, me, homeAt)) home.facilities.push_back(facilityIndex(r, "Test Depot"));
    // A ship one jump from home: its distance is 13 × 2 = 26 moves.
    const SystemId next = s.galaxy.neighbors(homeAt.system).front();
    const Location there{next, Sector{kSystemCenter, kSystemCenter}};
    const DesignId tanker = addTestDesign(s, r, me, "Tanker", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Supply Pod"});
    const VehicleId ship = addTestVehicle(s, r, tanker, there).id;
    const int64_t cost = movement::moveSupplyCost(r, s, *s.vehicle(ship));
    REQUIRE(cost > 0);
    auto sent = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 9);
        ai::detail::planRepairAndResupply(p, false);
        return p.st.vehicle(ship)->orders;
    };
    s.vehicle(ship)->supply = 26 * cost;  // exactly the distance: not sent
    CHECK(sent(s).empty());
    s.vehicle(ship)->supply = 26 * cost - 1;
    const auto orders = sent(s);
    REQUIRE(orders.size() == 1);
    CHECK(orders.front().location == homeAt);  // the depot
    // Colony ships are exempt, even at 0 supply.
    const DesignId settler = addTestDesign(s, r, me, "Settler", "Test Frigate",
                                           {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Rock Pod"});
    const VehicleId pod = addTestVehicle(s, r, settler, there).id;
    s.vehicle(pod)->supply = 0;
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
    ai::detail::planRepairAndResupply(p, false);
    CHECK(ordersOf(p, pod).empty());
    // No depot anywhere: the distance is 999,999 and the ship goes to our nearest colony.
    for (auto& c : s.colonies)
        if (c) std::erase_if(c->facilities, [&](uint32_t f) { return hasAbility(r.facilityAbilities(f), AbilityKind::SupplyGeneration); });
    s.vehicle(ship)->supply = 1000 * cost;
    const auto fallback = sent(s);
    REQUIRE(fallback.size() == 1);
    CHECK(fallback.front().location == homeAt);
}

TEST_CASE("ai: space yard ships seek damaged slow vehicles without a yard, else resupply") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId me{0u};
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    const SystemId next = s.galaxy.neighbors(homeAt.system).front();
    const DesignId yardShip = addTestDesign(s, r, me, "Tender", "Test Cruiser",
                                            {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine", "Test Yard Module"});
    s.design(yardShip).designType = "Space Yard Ship";
    Vehicle& tender = addTestVehicle(s, r, yardShip, {next, Sector{0, 0}});
    tender.movement = 2;
    const VehicleId tenderId = tender.id;
    const DesignId station = addTestDesign(s, r, me, "Post", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Laser"});
    const Location post{next, Sector{3, 3}};
    const VehicleId base = addTestVehicle(s, r, station, post).id;
    destroyEntry(r, s, base, "Test Laser");
    {
        ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
        ai::detail::planSpaceYardShips(p);
        REQUIRE(ordersOf(p, tenderId).size() == 1);
        CHECK(ordersOf(p, tenderId).front().location == post);
    }
    // Nothing to fix: the resupply orders (home).
    s.vehicle(base)->damage.assign(s.vehicle(base)->damage.size(), 0);
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
    ai::detail::planSpaceYardShips(p);
    REQUIRE(ordersOf(p, tenderId).size() == 1);
    CHECK(ordersOf(p, tenderId).front().location.system == homeAt.system);
}

TEST_CASE("ai: a Create Planet ship goes to the uncolonized asteroid field nearest home in a system with a star") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 12);
    exploreEverything(s);
    const EmpireId me{0u};
    researchEverything(r, s.empire(me));
    const SystemId home = ai::detail::homeSystem(s, me);
    // Make sure a field exists: the first free planet of a neighbouring system with a star.
    bool anyField = false;
    for (const SpaceObject& o : s.galaxy.objects) anyField = anyField || (o.kind == ObjectKind::Asteroids && !s.colony(o.id));
    if (!anyField)
        for (SystemId nb : s.galaxy.neighbors(home))
            if (const auto planet = freePlanetIn(s, nb); planet && !anyField) {
                s.galaxy.object(*planet).kind = ObjectKind::Asteroids;
                anyField = true;
            }
    const DesignId maker = addTestDesign(s, r, me, "Maker", "Test Cruiser",
                                         {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Planet Maker"});
    const VehicleId id = addTestVehicle(s, r, maker, locationOf(s.galaxy, homeworld(s, me).planet)).id;
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
    ai::detail::planStellarManipulation(p);
    const auto orders = ordersOf(p, id);
    const std::vector<int> jumps = ai::detail::jumpsOver(s, home);
    int best = ai::detail::kUnreachable;
    for (const SpaceObject& o : s.galaxy.objects) {
        if (o.kind != ObjectKind::Asteroids || s.colony(o.id)) continue;
        bool star = false;
        for (ObjectId x : s.galaxy.system(o.system).objects)
            star = star || s.galaxy.object(x).kind == ObjectKind::Star || s.galaxy.object(x).kind == ObjectKind::DestroyedStar;
        if (star) best = std::min(best, jumps[o.system.index()]);
    }
    if (best == ai::detail::kUnreachable) {
        CHECK(orders.empty());
        return;
    }
    REQUIRE_FALSE(orders.empty());
    const Order& act = orders.back();
    CHECK(act.kind == OrderKind::StellarManipulation);
    CHECK(act.amount == static_cast<int>(StellarAction::CreatePlanet));
    CHECK(s.galaxy.object(act.object).kind == ObjectKind::Asteroids);
    CHECK(jumps[s.galaxy.object(act.object).system.index()] == best);
}

TEST_CASE("ai: trade values of systems, ships and planets") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    exploreEverything(s);
    researchEverything(r, s.empire(cpu));
    ai::detail::Planner p(r, s, cpu, ai::detail::Mode::Computer, 1);
    // A system: 100,000 per planet we could colonize, only when the giver claims it.
    const SystemId sys = ai::detail::homeSystem(s, human);
    PackageItem system;
    system.kind = PackageItem::Kind::System;
    system.system = sys;
    int64_t settleable = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) settleable += ai::detail::canSettle(r, s, s.empire(cpu), s.galaxy.object(o));
    s.empire(human).claimedSystems.clear();
    ai::detail::Planner unclaimed(r, s, cpu, ai::detail::Mode::Computer, 1);
    CHECK(ai::detail::tradeItemValue(unclaimed, system, human, cpu) == 0);
    s.empire(human).claimedSystems = {sys};
    ai::detail::Planner claimed(r, s, cpu, ai::detail::Mode::Computer, 1);
    CHECK(ai::detail::tradeItemValue(claimed, system, human, cpu) == settleable * 100'000);
    // A ship: 100 × its scrap value; a quarter per resource without weapons.
    const Location at = locationOf(s.galaxy, homeworld(s, human).planet);
    const VehicleId armed = addTestVehicle(s, r, addWarship(s, r, human, "Armed"), at).id;
    const DesignId hull = addTestDesign(s, r, human, "Empty", "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    const VehicleId unarmed = addTestVehicle(s, r, hull, at).id;
    ai::detail::Planner ships(r, s, cpu, ai::detail::Mode::Computer, 1);
    PackageItem item;
    item.kind = PackageItem::Kind::Vehicle;
    item.vehicle = armed;
    CHECK(ai::detail::tradeItemValue(ships, item, human, cpu) == scrapRefund(r, s, *s.vehicle(armed)).total() * 100);
    item.vehicle = unarmed;
    const Resources scrap = scrapRefund(r, s, *s.vehicle(unarmed));
    CHECK(ai::detail::tradeItemValue(ships, item, human, cpu) == (scrap.v[0] / 4 + scrap.v[1] / 4 + scrap.v[2] / 4) * 100);
    s.vehicle(unarmed)->status = VehicleStatus::Mothballed;
    ai::detail::Planner moth(r, s, cpu, ai::detail::Mode::Computer, 1);
    CHECK(ai::detail::tradeItemValue(moth, item, human, cpu) == 0);
    // A capital: resources × 1000, people × 100, facilities × 1,000,000, cargo × 10,000 and 1,000,000,000.
    const Colony& capital = homeworld(s, human);
    const SpaceObject& obj = s.galaxy.object(capital.planet);
    int64_t cargo = capital.cargo.totalPopulation();
    for (const UnitStack& u : capital.cargo.units) cargo += u.count;
    const int64_t worth = (int64_t{obj.value[0]} + obj.value[1] + obj.value[2]) * 1000 + capital.totalPopulation() * 100 +
                          static_cast<int64_t>(capital.facilities.size()) * 1'000'000 + cargo * 10'000 + 1'000'000'000;
    PackageItem planet;
    planet.kind = PackageItem::Kind::Planet;
    planet.planet = capital.planet;
    CHECK(ai::detail::tradeItemValue(moth, planet, human, cpu) == worth / 100'000);
    CHECK(ai::detail::tradeItemValue(moth, planet, cpu, human) == 0);  // no longer the giver's
}

TEST_CASE("ai: only the newest message in the answer window is answered, and an empty pool sends nothing") {
    TempTree t("speechpool");
    t.write("Ai/Default_AI_Speech.txt", "Number of Send Refuse Treaty := 1\nSend Refuse Treaty 1 := No.\n");
    t.write("Ai/Default_AI_Politics.txt", "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 2, 0, 10, r);
    const EmpireId asker{0u}, cpu{1u};
    meet(s, asker, cpu);
    s.turn = 60;
    s.empire(cpu).relation(asker).anger = 0;  // a Non-Aggression offer passes
    const MessageId offer = deliver(s, asker, cpu, MessageType::ProposeTreaty, Treaty::NonAggression);
    for (uint32_t turn = 60; turn < 70; ++turn) {
        s.turn = turn;
        CHECK_FALSE(answerTo(r, s, cpu, offer));  // "Send Accept Treaty" is empty: nothing is sent
    }
    s.empire(cpu).relation(asker).anger = 100;  // now refused, and that pool has a line
    std::optional<bool> refused;
    for (uint32_t turn = 60; turn < 70 && !refused; ++turn) {
        s.turn = turn;
        refused = answerTo(r, s, cpu, offer);
    }
    REQUIRE(refused);
    CHECK_FALSE(*refused);
    // A newer General message in the window is the newest: nothing is answered.
    const MessageId chat = deliver(s, asker, cpu, MessageType::General);
    for (uint32_t turn = 60; turn < 70; ++turn) {
        s.turn = turn;
        GameState g = datedNow(s, offer);
        for (DiplomaticMessage& m : g.messages)
            if (m.id == chat) m.dated = answerDate(g);
        for (const Command& c : ai::planTurn(r, g, cpu)) CHECK_FALSE((as<cmd::AnswerMessage>(c) && as<cmd::AnswerMessage>(c)->message == offer));
    }
    // The window (simultaneous): exactly the ministers' date − 2. An older
    // offer is never answered, a newer one not yet.
    for (const int shift : {-1, 1}) {
        s.turn = 60;
        GameState g = datedNow(s, offer);
        g.messages.erase(std::remove_if(g.messages.begin(), g.messages.end(), [&](const DiplomaticMessage& m) { return m.id == chat; }),
                         g.messages.end());
        for (DiplomaticMessage& m : g.messages)
            if (m.id == offer) m.dated = static_cast<uint32_t>(static_cast<int>(m.dated) + shift);
        for (const Command& c : ai::planTurn(r, g, cpu)) CHECK_FALSE((as<cmd::AnswerMessage>(c) && as<cmd::AnswerMessage>(c)->message == offer));
    }
}

TEST_CASE("ai: the turn-based answer window holds what the other empire sent since our previous turn") {
    TempTree t("tbwindow");
    t.write("Ai/Default_AI_Politics.txt", "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 3, 0, 10, r);
    s.options.simultaneous = false;
    const EmpireId low{0u}, cpu{1u}, high{2u};
    meet(s, low, cpu);
    meet(s, high, cpu);
    s.turn = 60;  // the ministers' date in a turn-based game
    s.empire(cpu).relation(low).anger = 0;
    s.empire(cpu).relation(high).anger = 0;
    auto answered = [&](EmpireId from, uint32_t dated) {
        GameState g = s;
        const MessageId id = deliver(g, from, cpu, MessageType::ProposeTreaty, Treaty::NonAggression);
        for (DiplomaticMessage& m : g.messages)
            if (m.id == id) m.dated = dated;
        for (const Command& c : ai::planTurn(r, g, cpu))
            if (const auto* a = as<cmd::AnswerMessage>(c); a && a->message == id) return true;
        return false;
    };
    // A lower player number played before us this game turn: only this date.
    CHECK(answered(low, 60));
    CHECK_FALSE(answered(low, 59));
    // A higher one played after us in the game turn before: that date too.
    CHECK(answered(high, 60));
    CHECK(answered(high, 59));
    CHECK_FALSE(answered(high, 58));
}

TEST_CASE("ai: a vehicle-list entry first looks for a design made from the template of that name") {
    TempTree t("templates");
    t.write("Ai/Default_AI_Construction_Vehicles.txt",
            "AI State := Exploration\nNum Queue Entries := 2\nEntry 1 Type := Picket Template\nEntry 1 Must Have At Least := 1\n"
            "Entry 2 Type := Nonsense\nEntry 2 Must Have At Least := 5\n");
    const Rules rules{buildEngineRuleset(), t.root};
    GameState s = computerGame(8, 2, 0, 10, rules);
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    e.designs.clear();
    e.economy = {};
    e.economy.colonies = Resources{1'000'000, 1'000'000, 1'000'000};
    std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.owner == me; });
    const DesignId fromTemplate = addWarship(s, rules, me, "Templated");
    s.design(fromTemplate).templateName = "PICKET TEMPLATE";
    const DesignId newer = addWarship(s, rules, me, "Newer");
    s.design(newer).createdTurn = 5;
    homeworld(s, me).queue.items.clear();
    ai::detail::Planner p(rules, s, me, ai::detail::Mode::Computer, 3);
    ai::detail::planShips(p);
    const auto& items = p.st.colony(homeworld(s, me).planet)->queue.items;
    REQUIRE(items.size() == 1);  // one Attack Ship (the template's design); "Nonsense" builds nothing
    CHECK(items.front().design == fromTemplate);
    // Counted by the chosen design's type: an Attack Ship in service satisfies the entry.
    addTestVehicle(s, rules, newer, locationOf(s.galaxy, homeworld(s, me).planet));
    ai::detail::Planner q(rules, s, me, ai::detail::Mode::Computer, 3);
    ai::detail::planShips(q);
    CHECK(q.st.colony(homeworld(s, me).planet)->queue.items.empty());
}

// ---- Spec 05 open question 37: the logistics ministers (confirmed: binary) -----------------------

TEST_CASE("ai: transports deliver only when more than half full, never fall back to loading, and deliver after a load only in the same sector") {
    const Rules& r = engineRules();
    GameState s = computerGame(21, 2, 0, 10);
    const EmpireId me{0u};
    const ObjectId homePlanet = homeworld(s, me).planet;
    const Location homeAt = locationOf(s.galaxy, homePlanet);
    const SystemId homeSys = homeAt.system;
    const auto spare = freePlanetIn(s, homeSys);
    REQUIRE(spare);
    s.galaxy.object(*spare).atmosphere = s.empire(me).race.atmosphere;
    addColony(s, *spare, me, {{me, 1}});
    REQUIRE(homeworld(s, me).totalPopulation() >= 1000);
    const DesignId hauler = addTestDesign(s, r, me, "Hauler", "Test Transport Hull",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay", "Test Cargo Bay"});
    s.design(hauler).designType = "Population Transport";
    Vehicle& v = addTestVehicle(s, r, hauler, homeAt);
    const VehicleId id = v.id;
    const int capacity = vehicleCargoCapacity(r, s, v);
    REQUIRE(capacity > 2);
    auto plan = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 5);
        ai::detail::planTransports(p);
        return ordersOf(p, id);
    };
    // A few people aboard (not more than half full): load here at the
    // homeworld, then deliver what is aboard to the other colony.
    v.cargo.population = {{me, 1}};
    REQUIRE(cargoSpaceUsed(r, s, v.cargo) * 2 <= capacity);
    auto orders = plan(s);
    REQUIRE(orders.size() == 3);
    CHECK(orders[0].kind == OrderKind::LoadCargo);
    CHECK(orders[1].kind == OrderKind::MoveTo);
    CHECK(orders[2].kind == OrderKind::DropCargo);
    CHECK(orders[2].object == *spare);
    // Empty: the load only; nothing aboard is never delivered.
    s.vehicle(id)->cargo = {};
    orders = plan(s);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].kind == OrderKind::LoadCargo);
    // The source in another sector: a move and the load, no delivery.
    Location away = homeAt;
    away.sector = Sector{homeAt.sector.x == 0 ? 1 : 0, homeAt.sector.y};
    s.vehicle(id)->location = away;
    s.vehicle(id)->cargo.population = {{me, 1}};
    orders = plan(s);
    REQUIRE(orders.size() == 2);
    CHECK(orders[0].kind == OrderKind::MoveTo);
    CHECK(orders[1].kind == OrderKind::LoadCargo);
    // In another system: only a move to the source's sector.
    const SystemId next = s.galaxy.neighbors(homeSys).front();
    s.vehicle(id)->location = Location{next, Sector{kSystemCenter, kSystemCenter}};
    orders = plan(s);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].kind == OrderKind::MoveTo);
    CHECK(orders[0].location == homeAt);
    // More than half full with nowhere to deliver: no fall back to loading,
    // the resupply orders instead (here: home, the depot).
    s.vehicle(id)->location = homeAt;
    s.colonies[spare->index()].reset();
    int64_t each = 1;
    for (s.vehicle(id)->cargo.population = {{me, each}}; cargoSpaceUsed(r, s, s.vehicle(id)->cargo) * 2 <= capacity;)
        s.vehicle(id)->cargo.population = {{me, ++each}};
    orders = plan(s);
    CHECK(std::none_of(orders.begin(), orders.end(), [](const Order& o) { return o.kind == OrderKind::LoadCargo; }));
    CHECK(std::none_of(orders.begin(), orders.end(), [](const Order& o) { return o.kind == OrderKind::DropCargo; }));
}

TEST_CASE("ai: the Repair minister: mothballed ships lose orders and fleet, yards by system then object order, a yard ship is its own yard") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId me{0u};
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    Location away = homeAt;
    away.sector = Sector{homeAt.sector.x == 0 ? 1 : 0, homeAt.sector.y};
    const DesignId warship = addWarship(s, r, me, "Lancer");
    const VehicleId sleeper = addTestVehicle(s, r, warship, away).id;
    const VehicleId mate = addTestVehicle(s, r, warship, away).id;
    REQUIRE(apply(r, s, me, cmd::CreateFleet{"Pair", {mate, sleeper}}).ok);
    destroyEntry(r, s, sleeper, "Test Laser");
    s.vehicle(sleeper)->status = VehicleStatus::Mothballed;
    s.vehicle(sleeper)->orders = {ai::detail::moveOrder(homeAt)};
    {
        ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
        ai::detail::planRepairAndResupply(p, true);
        CHECK_FALSE(p.st.vehicle(sleeper)->fleet.valid());
        CHECK(ordersOf(p, sleeper).empty());
    }
    // A damaged yard ship with a working yard is its own nearest yard: it stays.
    const DesignId yardShip = addTestDesign(s, r, me, "Tender", "Test Cruiser",
                                            {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine",
                                             "Test Yard Module", "Test Laser"});
    s.design(yardShip).designType = "Attack Ship";
    const VehicleId tender = addTestVehicle(s, r, yardShip, away).id;
    destroyEntry(r, s, tender, "Test Laser");
    {
        ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
        ai::detail::planRepairAndResupply(p, true);
        CHECK(ordersOf(p, tender).empty());
    }
    // Two yards at the same travel distance: the one in the lower-numbered system wins.
    GameState g = s;
    for (auto& c : g.colonies)
        if (c && c->owner == me) std::erase_if(c->facilities, [&](uint32_t f) { return hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard); });
    exploreEverything(g);
    g.vehicles.erase(std::remove_if(g.vehicles.begin(), g.vehicles.end(), [&](const Vehicle& x) { return x.id == tender; }), g.vehicles.end());
    std::vector<SystemId> around = g.galaxy.neighbors(homeAt.system);
    std::sort(around.begin(), around.end());
    around.erase(std::unique(around.begin(), around.end()), around.end());
    if (around.size() >= 2) {
        const DesignId post = addTestDesign(g, r, me, "Dock", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Yard Module"});
        // The yard ships sit on the warp point that leads back home, so both are one jump away.
        auto backHome = [&](SystemId sys) {
            for (ObjectId wp : g.galaxy.warpPoints(sys))
                if (g.galaxy.object(wp).destination.valid() && g.galaxy.object(g.galaxy.object(wp).destination).system == homeAt.system)
                    return locationOf(g.galaxy, wp);
            return Location{sys, Sector{kSystemCenter, kSystemCenter}};
        };
        const Location high = backHome(around[1]), low = backHome(around[0]);
        addTestVehicle(g, r, post, high);  // created first: earlier in object order, but a higher system
        addTestVehicle(g, r, post, low);
        // A sector of the home system as far from one yard as from the other.
        std::optional<Location> start;
        for (int y = 0; y < kSystemSize && !start; ++y)
            for (int x = 0; x < kSystemSize && !start; ++x) {
                const Location at{homeAt.system, Sector{x, y}};
                const auto a = movement::findPath(r, g, me, at, high), b = movement::findPath(r, g, me, at, low);
                if (a && b && a->length == b->length) start = at;
            }
        REQUIRE(start);
        const VehicleId hurt = addTestVehicle(g, r, warship, *start).id;
        destroyEntry(r, g, hurt, "Test Laser");
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 9);
        ai::detail::planRepairAndResupply(p, true);
        REQUIRE(ordersOf(p, hurt).size() == 1);
        CHECK(ordersOf(p, hurt).front().location == low);
    }
}

TEST_CASE("ai: a fleet's supply totals leave out the members with unlimited supply") {
    ruleset::Ruleset rs = buildEngineRuleset();
    for (auto& c : rs.components)
        if (c.name == "Test Engine") c.supplyUsed = 10;
    const Rules r{std::move(rs), {}};
    GameState s = computerGame(4, 2, 0, 10, r);
    const EmpireId me{0u};
    researchEverything(r, s.empire(me));
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    const SystemId next = s.galaxy.neighbors(homeAt.system).front();
    const Location there{next, Sector{kSystemCenter, kSystemCenter}};
    const DesignId tanker = addTestDesign(s, r, me, "Tanker", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Supply Pod"});
    const DesignId reactor = addTestDesign(s, r, me, "Reactor", "Test Cruiser",
                                           {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Quantum Reactor"});
    const VehicleId ship = addTestVehicle(s, r, tanker, there).id;
    const VehicleId endless = addTestVehicle(s, r, reactor, there).id;
    REQUIRE(vehicleHasUnlimitedSupply(r, s, *s.vehicle(endless)));
    REQUIRE(apply(r, s, me, cmd::CreateFleet{"Pair", {ship, endless}}).ok);
    const FleetId fleet = s.fleets.back().id;
    const int64_t cost = movement::moveSupplyCost(r, s, *s.vehicle(ship));
    REQUIRE(cost > 0);
    // One jump from the depot: 26 moves. The reactor ship's own supply and
    // cost would lift the fleet above that; only the tanker counts.
    s.vehicle(ship)->supply = 26 * cost - 1;
    s.vehicle(endless)->supply = 1'000'000;
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
    ai::detail::planRepairAndResupply(p, false);
    CHECK_FALSE(fleetOrders(p.st, *p.st.fleet(fleet)).empty());
}

TEST_CASE("ai: a space yard ship counts itself as a yard, and one in a fleet is planned too") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 10);
    const EmpireId me{0u};
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    const SystemId next = s.galaxy.neighbors(homeAt.system).front();
    const DesignId yardShip = addTestDesign(s, r, me, "Tender", "Test Cruiser",
                                            {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine", "Test Yard Module"});
    s.design(yardShip).designType = "Space Yard Ship";
    const Location dock{next, Sector{3, 3}};
    Vehicle& tender = addTestVehicle(s, r, yardShip, dock);
    tender.movement = 2;
    const VehicleId tenderId = tender.id;
    const DesignId station = addTestDesign(s, r, me, "Post", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Laser"});
    const VehicleId base = addTestVehicle(s, r, station, dock).id;
    destroyEntry(r, s, base, "Test Laser");
    const DesignId warship = addWarship(s, r, me, "Escort");
    const VehicleId escort = addTestVehicle(s, r, warship, dock).id;
    REQUIRE(apply(r, s, me, cmd::CreateFleet{"Yard", {tenderId, escort}}).ok);
    // The base it reached is no longer a target: the yard ship itself is a
    // yard there. Nothing else to fix: the resupply orders (home).
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
    ai::detail::planSpaceYardShips(p);
    REQUIRE(ordersOf(p, tenderId).size() == 1);
    CHECK(ordersOf(p, tenderId).front().location.system == homeAt.system);
}

// ---- Spec 05 open question 37: Stellar Manipulation (confirmed: binary) ----------------------------

TEST_CASE("ai: an Open Warp Point ship gets the resupply orders while a frontier point is free or when no edge sector is empty") {
    ruleset::Ruleset rs = buildEngineRuleset();
    for (const auto& c : std::vector<ruleset::Component>(rs.components))
        if (c.name == "Test Cargo Bay") {
            ruleset::Component opener = c;
            opener.name = "Test Warp Opener";
            opener.abilities.clear();
            ruleset::Ability a;
            a.type = std::string(identifier(AbilityKind::OpenWarpPointDistance));
            a.value1 = "100000";
            opener.abilities.push_back(a);
            rs.components.push_back(opener);
        }
    const Rules r{std::move(rs), {}};
    GameState s = computerGame(4, 2, 0, 12, r);
    const EmpireId me{0u};
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    const SystemId next = s.galaxy.neighbors(homeAt.system).front();
    const DesignId opener = addTestDesign(s, r, me, "Opener", "Test Cruiser",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Warp Opener"});
    s.design(opener).designType = "Open Warp Point";
    const VehicleId id = addTestVehicle(s, r, opener, Location{next, Sector{kSystemCenter, kSystemCenter}}).id;
    auto plan = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 9);
        const bool freePoint = !p.sit.freeFrontier.empty();
        ai::detail::planStellarManipulation(p);
        return std::pair{freePoint, ordersOf(p, id)};
    };
    // A free frontier point: the resupply orders (home, the depot).
    {
        const auto [freePoint, orders] = plan(s);
        REQUIRE(freePoint);
        REQUIRE(orders.size() == 1);
        CHECK(orders.front().kind == OrderKind::MoveTo);
        CHECK(orders.front().location == homeAt);
    }
    // Every system explored but the last; ships of ours head for every
    // frontier point, so none is free. Every edge sector of every explored
    // system holds a vehicle: no draw succeeds, and the ship gets the
    // resupply orders again.
    const SystemId last{s.galaxy.systems.size() - 1};
    for (Empire& e : s.empires) {
        e.knowledge.explored.assign(s.galaxy.systems.size(), 1);
        e.knowledge.explored[last.index()] = 0;
        e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 1);
    }
    const DesignId marker = addWarship(s, r, me, "Marker");
    for (size_t i = 0; i + 1 < s.galaxy.systems.size(); ++i)
        for (ObjectId wp : s.galaxy.warpPoints(SystemId{i}))
            if (s.galaxy.object(wp).destination.valid() && s.galaxy.object(s.galaxy.object(wp).destination).system == last)
                addTestVehicle(s, r, marker, homeAt).orders = {ai::detail::moveOrder(locationOf(s.galaxy, wp))};
    {
        GameState g = s;
        for (size_t i = 0; i + 1 < g.galaxy.systems.size(); ++i)
            for (int k = 0; k < kSystemSize; ++k)
                for (Sector at : {Sector{k, 0}, Sector{k, kSystemSize - 1}, Sector{0, k}, Sector{kSystemSize - 1, k}})
                    addTestVehicle(g, r, marker, Location{SystemId{i}, at});
        const auto [freePoint, orders] = plan(g);
        REQUIRE_FALSE(freePoint);
        REQUIRE(orders.size() == 1);
        CHECK(orders.front().kind == OrderKind::MoveTo);
        CHECK(orders.front().location.system == homeAt.system);
    }
    // With room on the edges: a move to an empty edge sector, then the order.
    const auto [freePoint, orders] = plan(s);
    REQUIRE_FALSE(freePoint);
    REQUIRE_FALSE(orders.empty());
    const Order& open = orders.back();
    CHECK(open.kind == OrderKind::StellarManipulation);
    CHECK(open.amount == static_cast<int>(StellarAction::OpenWarpPoint));
    const Location at = orders.size() == 2 ? orders.front().location : s.vehicle(id)->location;
    CHECK((at.sector.x == 0 || at.sector.y == 0 || at.sector.x == kSystemSize - 1 || at.sector.y == kSystemSize - 1));
}

TEST_CASE("ai: a Destroy Black Hole ship seeks sector 36 of the system and is planned again every turn") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 12);
    exploreEverything(s);
    const EmpireId me{0u};
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    std::optional<SystemId> hole;
    for (const StarSystem& sys : s.galaxy.systems)
        if (sys.id != homeAt.system && !hole) hole = sys.id;
    REQUIRE(hole);
    s.galaxy.system(*hole).physicalType = "Black Hole";
    const DesignId fixer = addTestDesign(s, r, me, "Fixer", "Test Cruiser", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    s.design(fixer).designType = "Destroy Black Hole";
    const VehicleId id = addTestVehicle(s, r, fixer, homeAt).id;
    auto plan = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 9);
        ai::detail::planStellarManipulation(p);
        return ordersOf(p, id);
    };
    const auto seek = plan(s);
    REQUIRE(seek.size() == 1);
    CHECK(seek.front().kind == OrderKind::MoveTo);
    CHECK(seek.front().location == Location{*hole, Sector{10, 2}});
    // Still on its way next turn, now inside the system: the order at once.
    s.vehicle(id)->orders = seek;
    s.vehicle(id)->location = Location{*hole, Sector{1, 1}};
    const auto act = plan(s);
    REQUIRE(act.size() == 1);
    CHECK(act.front().kind == OrderKind::StellarManipulation);
    CHECK(act.front().amount == static_cast<int>(StellarAction::DestroyBlackHole));
}

TEST_CASE("ai: Close Warp Point needs a seen hostile empire where we have no presence; satellites are presence, mines are not") {
    const Rules& r = engineRules();
    GameState s = computerGame(4, 2, 0, 12);
    const EmpireId me{0u}, them{1u};
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    std::optional<ObjectId> gate;
    SystemId far;
    for (ObjectId wp : s.galaxy.warpPoints(homeAt.system)) {
        const ObjectId dest = s.galaxy.object(wp).destination;
        if (!dest.valid() || gate) continue;
        const SystemId sys = s.galaxy.object(dest).system;
        bool colonized = false;
        for (ObjectId o : s.galaxy.system(sys).objects) colonized = colonized || s.colony(o) != nullptr;
        if (!colonized && freePlanetIn(s, sys)) {
            gate = wp;
            far = sys;
        }
    }
    REQUIRE(gate);
    exploreEverything(s);
    addColony(s, *freePlanetIn(s, far), them, {{them, 100}});
    const DesignId closer = addTestDesign(s, r, me, "Closer", "Test Cruiser", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    s.design(closer).designType = "Close Warp Point";
    const VehicleId id = addTestVehicle(s, r, closer, homeAt).id;
    auto closes = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 9);
        ai::detail::planStellarManipulation(p);
        const auto orders = ordersOf(p, id);
        return !orders.empty() && orders.back().kind == OrderKind::StellarManipulation;
    };
    // The hostile colony we see in the explored far system is enough (only
    // the warp points into it qualify, so the pick is ours to check).
    {
        ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 9);
        ai::detail::planStellarManipulation(p);
        const auto orders = ordersOf(p, id);
        REQUIRE_FALSE(orders.empty());
        CHECK(orders.back().kind == OrderKind::StellarManipulation);
        CHECK(s.galaxy.object(s.galaxy.object(orders.back().object).destination).system != homeAt.system);
    }
    // A mine field of ours there is no presence.
    GameState mined = s;
    const DesignId mine = addTestDesign(mined, r, me, "Mine", "Test Mine Hull", {});
    addTestVehicle(mined, r, mine, Location{far, Sector{1, 1}});
    CHECK(closes(mined));
    // Every system next to home but this one blocked by our presence, and a
    // satellite group of ours in this one: no candidate left.
    GameState watched = s;
    const DesignId eye = addTestDesign(watched, r, me, "Eye", "Test Satellite Hull", {"Test Satellite Gun"});
    for (ObjectId wp : watched.galaxy.warpPoints(homeAt.system))
        if (const ObjectId dest = watched.galaxy.object(wp).destination; dest.valid())
            addTestVehicle(watched, r, eye, Location{watched.galaxy.object(dest).system, Sector{2, 2}});
    for (auto& c : watched.colonies)
        if (c && c->owner == me && c->planet != homeworld(watched, me).planet) c.reset();
    CHECK_FALSE(closes(watched));
}

// ---- Spec 05 open question 37: mine and satellite layers (confirmed: binary) ---------------------

TEST_CASE("ai: layer weights add up over the empires in the far system; the cap counts only the layer's kind") {
    ruleset::Ruleset rs = buildEngineRuleset();
    rs.settings.set("Maximum Mines Per Player Per Sector", "5");
    const Rules r{std::move(rs), {}};
    GameState s = computerGame(13, 3, 0, 12, r);
    const EmpireId me{0u}, war{1u}, stranger{2u};
    meet(s, me, war);
    s.empire(me).relation(war).treaty = s.empire(war).relation(me).treaty = Treaty::War;
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    // Clear every other empire out of the systems next to home, then put a
    // ship of each of the other two beyond one gate.
    const ObjectId gate = s.galaxy.warpPoints(home.system).front();
    const SystemId far = s.galaxy.object(s.galaxy.object(gate).destination).system;
    std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.owner != me; });
    for (auto& c : s.colonies)
        if (c && c->owner != me && s.galaxy.object(c->planet).system != s.galaxy.object(homeworld(s, war).planet).system &&
            s.galaxy.object(c->planet).system != s.galaxy.object(homeworld(s, stranger).planet).system)
            c.reset();
    for (EmpireId x : {war, stranger}) {
        const SystemId theirs = s.galaxy.object(homeworld(s, x).planet).system;
        REQUIRE(theirs != far);
        for (SystemId nb : s.galaxy.neighbors(home.system)) REQUIRE(nb != theirs);
    }
    addTestVehicle(s, r, addWarship(s, r, war, "Raider"), Location{far, Sector{4, 4}});
    addTestVehicle(s, r, addWarship(s, r, stranger, "Visitor"), Location{far, Sector{5, 5}});
    auto weights = [&](const GameState& g, bool mines) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 3);
        return ai::detail::layerCandidates(p, mines);
    };
    const Location at = locationOf(s.galaxy, gate);
    auto weightAt = [&](const std::vector<std::pair<Location, int>>& list) {
        int w = 0;
        for (const auto& [where, weight] : list)
            if (where == at) w += weight;
        return w;
    };
    // Mines: 7 at war + 4 for the stranger (not met: hostile); satellites 2 + 2.
    CHECK(weightAt(weights(s, true)) == 11);
    CHECK(weightAt(weights(s, false)) == 4);
    // The cap: our satellites at the far system's sector of the gate's number
    // do not stop a mine layer, our mines do.
    const Location capped{far, at.sector};
    GameState sats = s;
    addTestVehicle(sats, r, addTestDesign(sats, r, me, "Eye", "Test Satellite Hull", {"Test Satellite Gun"}), capped).count = 5;
    CHECK(weightAt(weights(sats, true)) == 11);
    CHECK(weightAt(weights(sats, false)) == 0);
    GameState mined = s;
    addTestVehicle(mined, r, addTestDesign(mined, r, me, "Spike", "Test Mine Hull", {}), capped).count = 5;
    CHECK(weightAt(weights(mined, true)) == 0);
    CHECK(weightAt(weights(mined, false)) == 4);
}

TEST_CASE("ai: the layers' star-destroyer flag: dates that are multiples of 20, designs seen within 20 turns, ours included") {
    ruleset::Ruleset rs = buildEngineRuleset();
    for (const auto& c : std::vector<ruleset::Component>(rs.components))
        if (c.name == "Test Cargo Bay") {
            ruleset::Component nova = c;
            nova.name = "Test Nova Charge";
            nova.abilities.clear();
            ruleset::Ability a;
            a.type = std::string(identifier(AbilityKind::CreateNebulae));
            nova.abilities.push_back(a);
            rs.components.push_back(nova);
        }
    const Rules r{std::move(rs), {}};
    GameState s = computerGame(4, 2, 0, 10, r);
    const EmpireId me{0u}, other{1u};
    const DesignId theirs = addTestDesign(s, r, other, "Nova", "Test Cruiser",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Nova Charge"});
    auto flag = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 3);
        return ai::detail::starDestroyerFlag(p);
    };
    s.turn = 39;  // simultaneous: the ministers see date 40
    REQUIRE(ai::aiDate(s) == 40);
    CHECK_FALSE(flag(s));
    seeDesign(s.empire(me).knowledge, theirs, 19);
    CHECK(flag(s));  // 39 - 19 = 20 turns old: at most 20, still recent
    s.empire(me).knowledge.seenDesigns.clear();
    seeDesign(s.empire(me).knowledge, theirs, 18);
    CHECK_FALSE(flag(s));  // 21 turns old
    s.empire(me).knowledge.seenDesigns.clear();
    seeDesign(s.empire(me).knowledge, theirs, 30);
    CHECK(flag(s));
    s.turn = 40;  // date 41: not a multiple of 20
    CHECK_FALSE(flag(s));
    // Our own design that fought counts too.
    s.turn = 39;
    s.empire(me).knowledge.seenDesigns.clear();
    const DesignId ours = addTestDesign(s, r, me, "Own Nova", "Test Cruiser",
                                        {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Nova Charge"});
    CHECK_FALSE(flag(s));
    s.empire(me).aiMemory.designsFought = {{ours, 35}};
    CHECK(flag(s));
    // The AI's memory of the turn dates our designs that fought in a battle.
    s.empire(me).aiMemory.designsFought.clear();
    CombatRecord battle;
    battle.turn = s.turn;
    battle.participants = {me, other};
    CombatPiece piece;
    piece.owner = me;
    piece.design = ours;
    battle.pieces = {piece};
    s.combats = {battle};
    TurnContext ctx{r, s, {}, {}, {}};
    ai::rememberAiEvents(ctx);
    REQUIRE(s.empire(me).aiMemory.designsFought.size() == 1);
    CHECK(s.empire(me).aiMemory.designsFought.front().design == ours);
    CHECK(s.empire(me).aiMemory.designsFought.front().turn == s.turn);
    CHECK(flag(s));
}

TEST_CASE("installed data set: AI files load and computer players play (opt-in)") {
    const Rules* r = installedRules();
    if (!r) return;
    REQUIRE_FALSE(r->racePresets().empty());
    const ai::AiProfile& def = ai::profileFor(*r, "No Such Race");
    CHECK(def.sources.size() >= 8);
    CHECK_FALSE(def.research.empty());
    CHECK(def.designs.size() >= 10);
    CHECK(def.facilities.size() >= 5);
    CHECK(def.vehicles.size() >= 3);
    CHECK(def.planetTypes.size() >= 3);
    CHECK(def.speech.pools.size() >= 50);
    CHECK_FALSE(def.strategies.empty());
    for (const ai::DesignTemplate& t : def.designs) CHECK(ai::isAiDesignType(t.designType));
    const ai::AiProfile& race = ai::profileFor(*r, r->racePresets().front().folder);
    CHECK(race.sources.size() >= def.sources.size());
    CHECK(ai::ministerStyles(*r).size() >= 3);  // the stock Aggressive, Defensive and Neutral folders

    GameSetup setup;
    setup.seed = 77;
    setup.options.systemCount = 24;
    for (size_t i = 0; i < 4; ++i) {
        EmpireSetup e;
        e.preset = r->racePresets()[(i * 5) % r->racePresets().size()].folder;
        e.kind = PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    auto game = createGame(*r, setup);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    GameState& s = *game;
    std::vector<std::string> rejected;
    for (int t = 0; t < 12; ++t) {
        std::vector<EmpireOrders> none;
        for (const auto& [e, why] : processTurn(*r, s, none).rejected) rejected.push_back(std::format("{}: {}", e.value, why));
        fakeProgress(*r, s);
    }
    CHECK_MESSAGE(rejected.empty(), joined(rejected));
    for (const Empire& e : s.empires) {
        INFO(e.name);
        CHECK_FALSE(e.research.empty());
        CHECK(e.designs.size() > 4);
        for (DesignId d : e.designs) {
            const DesignStats st = computeDesignStats(*r, &e, s.design(d));
            CHECK_MESSAGE(st.problems.empty(), s.design(d).name << ": " << (st.problems.empty() ? "" : st.problems.front()));
        }
    }
}

TEST_CASE("ai: the Destroy Planet minister skips every colony marked cloaked, even one it sees (spec 01 §6.9)") {
    ruleset::Ruleset rs = buildEngineRuleset();
    for (const ruleset::Component& c : std::vector<ruleset::Component>(rs.components))
        if (c.name == "Test Cargo Bay") {
            ruleset::Component breaker = c;
            breaker.name = "Test Planet Breaker";
            breaker.abilities.clear();
            ruleset::Ability a;
            a.type = std::string(identifier(AbilityKind::DestroyPlanetSize));
            a.value1 = "100";
            breaker.abilities.push_back(a);
            rs.components.push_back(breaker);
        }
    rs.reindex();
    const Rules r{std::move(rs), {}};
    GameState s = computerGame(4, 2, 0, 12, r);
    exploreEverything(s);
    const EmpireId me{0u}, them{1u};
    const Location homeAt = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId design = addTestDesign(s, r, me, "Breaker", "Test Cruiser",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Planet Breaker"});
    s.design(design).designType = "Destroy Planet";
    const VehicleId id = addTestVehicle(s, r, design, homeAt).id;
    auto plan = [&](const GameState& g) {
        ai::detail::Planner p(r, g, me, ai::detail::Mode::Computer, 9);
        ai::detail::planStellarManipulation(p);
        return ordersOf(p, id);
    };
    const auto orders = plan(s);
    REQUIRE_FALSE(orders.empty());
    CHECK(orders.back().kind == OrderKind::StellarManipulation);
    CHECK(s.colony(orders.back().object)->owner == them);
    // Every colony of the enemy marked cloaked: no target.
    GameState hidden = s;
    for (auto& c : hidden.colonies)
        if (c && c->owner == them) c->cloaked = true;
    CHECK(plan(hidden).empty());
}

// ---- Requests the computer starts (spec 05 §7.4 "Demands the AI starts", question 52) -----------

namespace {

// The rules of a computer that never declares war, breaks a treaty or
// proposes one, so its initiative goes to the requests; `extra` adds lines.
const char* kQuietPolitics =
    "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n";

void setBoth(GameState& s, EmpireId a, EmpireId b, Treaty t) {
    s.empire(a).relation(b).treaty = t;
    s.empire(b).relation(a).treaty = t;
    s.empire(a).relation(b).contact = s.empire(b).relation(a).contact = true;
}

// Every request `cpu` sends `x` over many turns.
std::vector<DiplomaticMessage> requestsOver(const Rules& r, const GameState& base, EmpireId cpu, EmpireId x, uint32_t turns = 60) {
    std::vector<DiplomaticMessage> out;
    for (uint32_t turn = 21; turn < 21 + turns; ++turn) {
        if (turn % 10 == 0) continue;  // keep the demand lists' clearing out of it
        GameState g = base;
        g.turn = turn;
        for (const Command& c : ai::planTurn(r, g, cpu))
            if (const auto* m = as<cmd::SendMessage>(c); m && m->message.to == x && m->message.type != MessageType::General) {
                CHECK(m->minister);  // never through the player's picker
                out.push_back(m->message);
            }
    }
    return out;
}

} // namespace

TEST_CASE("ai: step 3 asks to break with the lowest-numbered empire X holds at Trade Alliance and the AI at War or Non-Intercourse") {
    TempTree t("request3");
    t.write("Ai/Default_AI_Politics.txt", kQuietPolitics);
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 4, 0, 12, r);
    const EmpireId x{0u}, cpu{1u}, z2{2u}, z3{3u};
    setBoth(s, cpu, x, Treaty::TradeAlliance);
    setBoth(s, x, z2, Treaty::TradeAlliance);
    setBoth(s, x, z3, Treaty::TradeAlliance);
    setBoth(s, cpu, z2, Treaty::None);   // None does not count
    setBoth(s, cpu, z3, Treaty::War);
    const auto sent = requestsOver(r, s, cpu, x);
    bool any = false;
    for (const DiplomaticMessage& m : sent) {
        if (m.type != MessageType::RequestBreakTreaty && m.type != MessageType::RequestDeclareWar) continue;
        any = true;
        CHECK(m.type == MessageType::RequestBreakTreaty);   // not Military Alliance with X
        CHECK(m.thirdEmpire == z3);
    }
    CHECK(any);
    // At Military Alliance with X and at War with Z: X is asked to declare war on Z.
    setBoth(s, cpu, x, Treaty::MilitaryAlliance);
    bool war = false;
    for (const DiplomaticMessage& m : requestsOver(r, s, cpu, x))
        if (m.type == MessageType::RequestDeclareWar) {
            war = true;
            CHECK(m.thirdEmpire == z3);
        }
    CHECK(war);
}

TEST_CASE("ai: step 4 asks for peace with the lowest-numbered ally of the AI that X is at war with") {
    TempTree t("request4");
    t.write("Ai/Default_AI_Politics.txt", kQuietPolitics);
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 4, 0, 12, r);
    const EmpireId x{0u}, cpu{1u}, z2{2u}, z3{3u};
    setBoth(s, cpu, x, Treaty::MilitaryAlliance);
    setBoth(s, x, z2, Treaty::War);
    setBoth(s, x, z3, Treaty::War);
    setBoth(s, cpu, z2, Treaty::NonAggression);   // below Military Alliance
    setBoth(s, cpu, z3, Treaty::MilitaryAlliance);
    bool any = false;
    for (const DiplomaticMessage& m : requestsOver(r, s, cpu, x)) {
        if (m.type != MessageType::RequestMakePeace) continue;
        any = true;
        CHECK(m.thirdEmpire == z3);
    }
    CHECK(any);
}

TEST_CASE("ai: step 5 names the system and the highest-numbered other side of its newest battle lost while defending") {
    TempTree t("request5");
    t.write("Ai/Default_AI_Politics.txt", kQuietPolitics);
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 4, 0, 12, r);
    const EmpireId x{0u}, cpu{1u}, z2{2u}, z3{3u};
    setBoth(s, cpu, x, Treaty::MilitaryAlliance);
    s.turn = 21;
    auto battle = [&](uint32_t turn, SystemId where, std::vector<EmpireId> sides, EmpireId current, bool lost) {
        CombatRecord rec;
        rec.turn = turn;
        rec.location = {where, Sector{3, 3}};
        rec.currentPlayer = current;
        rec.participants = sides;
        for (EmpireId e : sides) {
            CombatPiece p;
            p.owner = e;
            rec.pieces.push_back(p);
        }
        if (lost) {
            CombatEvent ev;
            ev.kind = CombatEvent::Kind::Destroyed;
            ev.piece = 0;   // ours
            rec.events.push_back(ev);
        }
        return rec;
    };
    // An older defeat names z2; the newest, with z2 and z3 (neither met), names z3.
    s.combats = {battle(20, SystemId{4u}, {cpu, z2}, z2, true), battle(21, SystemId{5u}, {cpu, z2, z3}, z3, true)};
    // The battles keep their dates relative to the turn planned: only the
    // AI's random stream (seed, turn and empire) changes from run to run.
    auto attackRequests = [&](const GameState& g) {
        std::vector<DiplomaticMessage> out;
        for (uint32_t turn = 21; turn < 81; ++turn) {
            if (turn % 10 == 0) continue;
            GameState h = g;
            h.turn = turn;
            for (CombatRecord& rec : h.combats) rec.turn = turn - (21 - rec.turn);
            for (const Command& c : ai::planTurn(r, h, cpu))
                if (const auto* m = as<cmd::SendMessage>(c); m && m->message.to == x && m->message.type == MessageType::RequestAttackEmpire)
                    out.push_back(m->message);
        }
        return out;
    };
    const auto asked = attackRequests(s);
    REQUIRE_FALSE(asked.empty());
    for (const DiplomaticMessage& m : asked) {
        CHECK(m.thirdEmpire == z3);
        CHECK(m.system == SystemId{5u});
    }
    // A battle won, or one fought as the current player, is no defeat while defending.
    GameState won = s;
    won.combats = {battle(21, SystemId{5u}, {cpu, z3}, z3, false)};
    CHECK(attackRequests(won).empty());
    GameState attacking = s;
    attacking.combats = {battle(21, SystemId{5u}, {cpu, z3}, cpu, true)};
    CHECK(attackRequests(attacking).empty());
    // When the empire to name is X itself, nothing is asked.
    GameState self = s;
    self.combats = {battle(21, SystemId{5u}, {x, cpu}, x, true)};
    CHECK(attackRequests(self).empty());
}

TEST_CASE("ai: the chosen request's flag is tested last; a forbidden one ends steps 1-5 for the turn") {
    for (const bool allowed : {true, false}) {
        CAPTURE(allowed);
        TempTree t(allowed ? "requestflag1" : "requestflag0");
        t.write("Ai/Default_AI_Politics.txt",
                std::string(kQuietPolitics) + (allowed ? "" : "Will Send To Enemy Stop attacks in system := False\n"));
        const Rules r{buildEngineRuleset(), t.root};
        GameState s = computerGame(7, 2, 0, 12, r);
        const EmpireId x{0u}, cpu{1u};
        setBoth(s, cpu, x, Treaty::None);
        // Both attacks and spying logged: step 1 chooses "stop attacks" first.
        Relation& rel = s.empire(cpu).relation(x);
        rel.attackedUs = true;
        rel.attackedIn = SystemId{2u};
        rel.spiedOnUs = true;
        const auto sent = requestsOver(r, s, cpu, x);
        if (allowed) {
            REQUIRE_FALSE(sent.empty());
            for (const DiplomaticMessage& m : sent) CHECK(m.type == MessageType::DemandStopAttacks);
        } else {
            CHECK(sent.empty());   // no fall-through to "stop espionage" or later steps
        }
    }
}

TEST_CASE("ai: a Politics minister's request about a third empire is not checked; the player's own is") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 3, 12, true);
    const EmpireId human{0u}, other{1u}, stranger{2u};
    REQUIRE(s.empire(human).kind == PlayerKind::Human);
    meet(s, human, other);
    DiplomaticMessage m;
    m.to = other;
    m.type = MessageType::RequestBreakTreaty;
    m.thirdEmpire = stranger;   // not met
    CHECK_FALSE(apply(r, s, human, cmd::SendMessage{m}).ok);
    CHECK(apply(r, s, human, cmd::SendMessage{m, true}).ok);
}

// ---- The computer players' pace (spec 05 questions 53-56, confirmed: binary) ----------------------

namespace {

// The queue items of a design anywhere in the empire's queues.
int queuedOf(const GameState& s, EmpireId e, DesignId d) {
    int n = 0;
    for (const auto& c : s.colonies)
        if (c && c->owner == e)
            for (const QueueItem& q : c->queue.items) n += q.kind == QueueItem::Kind::Vehicle && q.design == d;
    return n;
}

} // namespace

TEST_CASE("ai: the exploration frontier holds the warp points into unexplored systems, whatever we know of the links") {
    const Rules& r = engineRules();
    GameState s = computerGame(13, 2, 0, 12);
    const EmpireId me{0u}, other{1u};
    // Every system explored and no link known: no frontier point (the engine
    // used to count every unknown link), so contact leads to Infrastructure.
    s.empire(me).knowledge.explored.assign(s.galaxy.systems.size(), 1);
    s.empire(me).knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 0);
    {
        ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 1);
        CHECK(p.sit.frontier.empty());
        CHECK(p.sit.freeFrontier.empty());
        CHECK_FALSE(p.sit.bordersUnexplored);
    }
    CHECK(ai::nextState(r, s, me) == ai::AiState::Exploration);  // no contact yet
    meet(s, me, other);
    CHECK(ai::nextState(r, s, me) == ai::AiState::Infrastructure);
    s.empire(me).aiState = static_cast<int>(ai::AiState::DefendShortTerm);
    CHECK(ai::nextState(r, s, me) == ai::AiState::Infrastructure);

    // One neighbour of home unexplored: the frontier is exactly the warp
    // points of explored systems that lead there, and it borders our territory.
    s.empire(me).aiState = static_cast<int>(ai::AiState::Exploration);
    const SystemId home = ai::detail::homeSystem(s, me);
    const SystemId beyond = s.galaxy.neighbors(home).front();
    s.empire(me).knowledge.explored[beyond.index()] = 0;
    std::vector<ObjectId> expected;
    for (size_t i = 0; i < s.galaxy.systems.size(); ++i)
        if (SystemId{i} != beyond)
            for (ObjectId wp : s.galaxy.warpPoints(SystemId{i}))
                if (const SpaceObject& o = s.galaxy.object(wp); o.destination.valid() && s.galaxy.object(o.destination).system == beyond)
                    expected.push_back(wp);
    REQUIRE_FALSE(expected.empty());
    std::sort(expected.begin(), expected.end());
    {
        ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 1);
        std::vector<ObjectId> got = p.sit.frontier;
        std::sort(got.begin(), got.end());
        CHECK(got == expected);
        CHECK(p.sit.bordersUnexplored);
    }
    CHECK(ai::nextState(r, s, me) == ai::AiState::Exploration);
    s.empire(me).aiState = static_cast<int>(ai::AiState::DefendShortTerm);
    CHECK(ai::nextState(r, s, me) == ai::AiState::Exploration);

    // An explorer heads for one of those points only.
    const VehicleId scout = addTestVehicle(s, r, addWarship(s, r, me, "Picket"), locationOf(s.galaxy, homeworld(s, me).planet)).id;
    ai::detail::Planner p(r, s, me, ai::detail::Mode::Computer, 1);
    ai::detail::planExploration(p);
    const std::vector<Order> orders = ordersOf(p, scout);
    REQUIRE_FALSE(orders.empty());
    CHECK(orders.back().kind == OrderKind::Warp);
    CHECK(std::binary_search(expected.begin(), expected.end(), orders.back().object));
}

TEST_CASE("ai: the territory is what the Politics minister claimed the turn before; its exclusions spare our colony systems") {
    const Rules& r = engineRules();
    GameState s = computerGame(13, 3, 0, 12);
    exploreEverything(s);
    const EmpireId me{0u}, rival{1u}, stranger{2u};
    const SystemId home = ai::detail::homeSystem(s, me);
    const SystemId rivalHome = ai::detail::homeSystem(s, rival);
    // A start of the game claims the home system only.
    CHECK(s.empire(me).claimedSystems == std::vector<SystemId>{home});

    // A colony of ours in another computer player's home system, and in a
    // system we agreed to leave: both are claimed. The exclusions apply to
    // the neighbour systems only.
    {
        GameState g = s;
        const auto spare = freePlanetIn(g, rivalHome);
        REQUIRE(spare);
        addColony(g, *spare, me, {{me, 100}});
        const SystemId avoided = g.galaxy.neighbors(home).front();
        g.empire(me).aiMemory.avoid = {rivalHome, avoided};
        const std::vector<SystemId> claim = ai::detail::computeTerritory(g, me);
        CHECK(std::binary_search(claim.begin(), claim.end(), rivalHome));
        CHECK(std::binary_search(claim.begin(), claim.end(), home));
        if (avoided != rivalHome) CHECK_FALSE(std::binary_search(claim.begin(), claim.end(), avoided));
        for (SystemId nb : g.galaxy.neighbors(rivalHome))
            if (nb != home && nb != avoided && nb != ai::detail::homeSystem(g, stranger))
                CHECK(std::binary_search(claim.begin(), claim.end(), nb));
        // Another computer player's home system next to one of ours is left out.
        for (SystemId nb : g.galaxy.neighbors(home))
            if (nb == ai::detail::homeSystem(g, stranger)) CHECK_FALSE(std::binary_search(claim.begin(), claim.end(), nb));
    }

    // A system Y away from every home, with room for two colonies.
    std::optional<SystemId> y;
    for (size_t i = 0; i < s.galaxy.systems.size() && !y; ++i) {
        const SystemId sys{i};
        bool someoneHome = false;
        for (const Empire& e : s.empires) someoneHome = someoneHome || ai::detail::homeSystem(s, e.id) == sys;
        const auto neighbours = s.galaxy.neighbors(home);
        if (someoneHome || std::find(neighbours.begin(), neighbours.end(), sys) != neighbours.end()) continue;
        const auto first = freePlanetIn(s, sys);
        if (first && freePlanetIn(s, sys, *first)) y = sys;
    }
    REQUIRE(y);
    const ObjectId ours = *freePlanetIn(s, *y);
    const ObjectId theirs = *freePlanetIn(s, *y, ours);
    // Our new colony there, and a populated colony of an empire we have not met.
    addColony(s, ours, me, {{me, 100}});
    addColony(s, theirs, stranger, {{stranger, 100}});
    {
        // The state update still uses the claims made before the colony was founded ...
        GameState g = s;
        TurnContext ctx{r, g, {}, {}, {}};
        ai::updateAiState(ctx, me);
        CHECK(g.empire(me).aiState == static_cast<int>(ai::AiState::Exploration));
        // ... the Politics minister then claims Y, and the next update finds the enemy there.
        ai::claimTerritory(ctx, me);
        CHECK(std::binary_search(g.empire(me).claimedSystems.begin(), g.empire(me).claimedSystems.end(), *y));
        ai::updateAiState(ctx, me);
        CHECK(g.empire(me).aiState == static_cast<int>(ai::AiState::DefendShortTerm));
    }
    // The same through the turn: Y counts from the second start-of-turn update.
    std::vector<EmpireOrders> none;
    processTurn(r, s, none);
    CHECK(s.empire(me).aiState == static_cast<int>(ai::AiState::Exploration));
    CHECK(std::binary_search(s.empire(me).claimedSystems.begin(), s.empire(me).claimedSystems.end(), *y));
    processTurn(r, s, none);
    CHECK(s.empire(me).aiState == static_cast<int>(ai::AiState::DefendShortTerm));
}

TEST_CASE("ai: mines, satellites, platforms and fighters take the first queue by backlog, free cargo, units held, size, production, rate") {
    TempTree t("unitqueue");
    t.write("Ai/Default_AI_Construction_Vehicles.txt",
            "AI State := Exploration\nNum Queue Entries := 1\nEntry 1 Type := Satellite\nEntry 1 Must Have At Least := 1\n");
    const Rules rules{buildEngineRuleset(), t.root};
    GameState s = computerGame(8, 2, 0, 10, rules);
    const EmpireId me{0u};
    researchEverything(rules, s.empire(me));
    s.empire(me).economy = {};
    s.empire(me).economy.colonies = Resources{100'000, 100'000, 100'000};
    const DesignId sat = addTestDesign(s, rules, me, "Sentinel", "Test Satellite Hull", {"Test Satellite Gun"});
    s.design(sat).designType = "Satellite";
    const DesignId mine = addTestDesign(s, rules, me, "Spike", "Test Mine Hull", {"Test Warhead"});
    s.design(mine).designType = "Mine";
    Colony& home = homeworld(s, me);
    const ObjectId homePlanet = home.planet;
    home.queue.items.clear();
    const auto second = freePlanetIn(s, s.galaxy.object(homePlanet).system);
    REQUIRE(second);
    addColony(s, *second, me, {{me, 500}});
    auto freeCargo = [&](const GameState& g, ObjectId planet) {
        const Colony& c = *g.colony(planet);
        return colonyCargoCapacity(rules, g, c) - cargoSpaceUsed(rules, g, c.cargo);
    };
    // Room for a whole batch (as many as the queue finishes in one turn).
    ruleset::Ability hold;
    hold.type = std::string(identifier(AbilityKind::CargoStorage));
    hold.value1 = "100000";
    s.galaxy.object(*second).abilities.push_back(hold);
    REQUIRE(freeCargo(s, *second) >= 100'000);
    // The homeworld's hold is full of mines (another kind): with both queues
    // empty, the colony with more free cargo space takes the satellites,
    // whatever the rates.
    const int64_t mineTons = designTonnage(rules, s.design(mine));
    REQUIRE(mineTons > 0);
    homeworld(s, me).cargo.units = {{mine, static_cast<int>(colonyCargoCapacity(rules, s, homeworld(s, me)) / mineTons)}};
    REQUIRE(freeCargo(s, homePlanet) < designTonnage(rules, s.design(sat)));
    {
        ai::detail::Planner p(rules, s, me, ai::detail::Mode::Computer, 3);
        REQUIRE(p.state == ai::AiState::Exploration);
        ai::detail::planShips(p);
        CHECK(p.st.colony(homePlanet)->queue.items.empty());
        REQUIRE(p.st.colony(*second)->queue.items.size() == 1);
        CHECK(p.st.colony(*second)->queue.items.front().design == sat);
    }
    // A smaller backlog comes first: the full homeworld is chosen, and as it
    // has no room for the batch nothing is placed anywhere.
    QueueItem busy;
    busy.kind = QueueItem::Kind::Vehicle;
    busy.design = sat;
    busy.count = 1000;
    s.colony(*second)->queue.items = {busy};
    {
        ai::detail::Planner p(rules, s, me, ai::detail::Mode::Computer, 3);
        ai::detail::planShips(p);
        CHECK(p.st.colony(homePlanet)->queue.items.empty());
        CHECK(p.st.colony(*second)->queue.items.size() == 1);
    }
}

TEST_CASE("ai: a Defense Base goes to the K-th queue of the empire's list, K the queues with a working yard") {
    TempTree t("defensebase");
    t.write("Ai/Default_AI_Construction_Vehicles.txt",
            "AI State := Exploration\nNum Queue Entries := 1\nEntry 1 Type := Defense Base\nEntry 1 Must Have At Least := 2\n");
    const Rules rules{buildEngineRuleset(), t.root};
    GameState s = computerGame(8, 2, 0, 10, rules);
    const EmpireId me{0u};
    researchEverything(rules, s.empire(me));
    s.empire(me).economy = {};
    s.empire(me).economy.colonies = Resources{100'000, 100'000, 100'000};
    const DesignId base =
        addTestDesign(s, rules, me, "Bastion", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Laser"});
    s.design(base).designType = "Defense Base";
    const ObjectId homePlanet = homeworld(s, me).planet;
    homeworld(s, me).queue.items.clear();
    const Location homeAt = locationOf(s.galaxy, homePlanet);
    auto plan = [&](const Rules& rr, const GameState& g) {
        ai::detail::Planner p(rr, g, me, ai::detail::Mode::Computer, 3);
        REQUIRE(p.state == ai::AiState::Exploration);
        ai::detail::planShips(p);
        return p.st;
    };
    // Only the homeworld: K is 1 and the first queue is the homeworld's.
    CHECK(queuedOf(plan(rules, s), me, base) == 2);

    // A colony without a yard ahead of the homeworld in the list (a lower
    // system, or earlier in the home system): K is still 1, so the bases go
    // there, and with no yard they are lost; nothing is queued anywhere.
    std::optional<ObjectId> before;
    for (size_t i = 0; i <= homeAt.system.index() && !before; ++i)
        for (ObjectId o : s.galaxy.system(SystemId{i}).objects) {
            if (o == homePlanet) break;
            if (s.galaxy.object(o).kind == ObjectKind::Planet && !s.colony(o)) {
                before = o;
                break;
            }
        }
    REQUIRE(before);
    addColony(s, *before, me, {{me, 500}});
    CHECK(queuedOf(plan(rules, s), me, base) == 0);

    // That colony gets a yard: K is 2, and the second queue of the list (the
    // homeworld) takes the base, although the homeworld already has a base and
    // the other yard none.
    s.colony(*before)->facilities.push_back(facilityIndex(rules, "Test Space Yard"));
    addTestVehicle(s, rules, base, homeAt);
    {
        const GameState g = plan(rules, s);
        CHECK(g.colony(homePlanet)->queue.items.size() == 1);
        CHECK(g.colony(*before)->queue.items.empty());
    }
    // The backlog test is made on that queue alone: a busy homeworld takes
    // nothing, and the free yard is not tried.
    QueueItem busy;
    busy.kind = QueueItem::Kind::Vehicle;
    busy.design = addWarship(s, rules, me, "Picket");
    busy.count = 1000;
    homeworld(s, me).queue.items = {busy};
    {
        const GameState g = plan(rules, s);
        CHECK(g.colony(*before)->queue.items.empty());
        CHECK(g.colony(homePlanet)->queue.items.size() == 1);
    }
    // Every yard colony counts three bases or more (those in its sector plus
    // every base queued): each base goes to a queue of the list drawn at random.
    homeworld(s, me).queue.items.clear();
    for (int i = 0; i < 3; ++i) addTestVehicle(s, rules, base, locationOf(s.galaxy, *before));
    addTestVehicle(s, rules, base, homeAt);
    addTestVehicle(s, rules, base, homeAt);
    TempTree more("defensebase2");
    more.write("Ai/Default_AI_Construction_Vehicles.txt",
               "AI State := Exploration\nNum Queue Entries := 1\nEntry 1 Type := Defense Base\nEntry 1 Must Have At Least := 9\n");
    const Rules moreRules{buildEngineRuleset(), more.root};
    const GameState g = plan(moreRules, s);
    CHECK(queuedOf(g, me, base) == 3);
}
