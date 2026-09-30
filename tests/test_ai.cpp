// Computer player and ministers (docs/spec/05 §7): the commands the AI
// produces and the memory its turn step keeps. A small fake world below moves
// ships to their destinations (and, for the long games, finishes queue items
// and research faster) to walk the AI through a game without the rest of the
// turn pipeline.

#include "engine_fixture.hpp"

#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/ai_planner.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/events.hpp"
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
        std::vector<Order> orders = v.orders;
        if (const Fleet* f = s.fleet(v.fleet); f && !f->orders.empty()) orders = f->orders;
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
    }
    for (Fleet& f : s.fleets) f.orders.clear();
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
        for (const Order& ord : f.orders) o << " " << static_cast<int>(ord.kind) << ":" << ord.object.value;
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

struct TempTree {
    std::filesystem::path root;
    explicit TempTree(std::string_view tag) {
        root = std::filesystem::temp_directory_path() / std::format("opense4_ai_test_{}", tag);
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
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
    const Race low = ai::randomPlayerRace(rules, *preset, 2000);
    CHECK(low.characteristic(Characteristic::Intelligence) == 250);
    CHECK(low.characteristic(Characteristic::Reproduction) == 100);
    CHECK(low.traits.size() == 1);
    CHECK(racialPointCost(rules, low) <= 2000);
    // 3000 points: Race Opt 2.
    const Race mid = ai::randomPlayerRace(rules, *preset, 3000);
    CHECK(mid.characteristic(Characteristic::Reproduction) == 300);
    CHECK(mid.characteristic(Characteristic::Intelligence) == 100);
    // No racial points: no set is used.
    const Race none = ai::randomPlayerRace(rules, *preset, 0);
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

TEST_CASE("ai: research queues mine sweeping every fifth turn after meeting mines") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 8, false);
    const EmpireId cpu{1u};
    Empire& e = s.empire(cpu);
    e.techLevels[techArea(r, "Test Units").index()] = 0;
    e.aiMemory.metMinefield = true;
    s.turn = 5;
    const auto cmds = ai::planTurn(r, s, cpu);
    bool units = false;
    for (const Command& c : cmds)
        if (auto* x = as<cmd::SetResearch>(c))
            for (const ResearchProject& p : x->queue) units = units || p.area == techArea(r, "Test Units");
    CHECK(units);
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
    CHECK(applyAll(r, s, me, cmds).empty());
    CHECK_FALSE(homeworld(s, me).queue.items.empty());
    CHECK(countOf<cmd::CreateDesign>(cmds) >= 1);
    for (const Command& c : cmds)
        if (const auto* d = as<cmd::CreateDesign>(c)) {
            CHECK(ai::isAiDesignType(d->design.designType));  // never a scout
            CHECK(computeDesignStats(r, &s.empire(me), d->design).problems.empty());
        }
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
                                         {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", pod});
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

TEST_CASE("ai: new designs make every older design of their type obsolete") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 8, false);
    const EmpireId cpu{1u};
    // A hand-made attack ship of the same design type.
    const DesignId old = addWarship(s, r, cpu, "Old Picket");
    s.empire(cpu).designs.push_back(old);
    researchEverything(r, s.empire(cpu));  // a bigger hull and better parts
    s.turn = 10;
    const auto cmds = ai::planTurn(r, s, cpu);
    REQUIRE(applyAll(r, s, cpu, cmds).empty());
    CHECK(s.design(old).obsolete);
    int attackShips = 0;
    for (DesignId d : s.empire(cpu).designs)
        if (s.design(d).designType == "Attack Ship" && !s.design(d).obsolete) ++attackShips;
    CHECK(attackShips == 1);
    // Nothing more to improve: the next tenth turn designs nothing new of it.
    s.turn = 20;
    for (const Command& c : ai::planTurn(r, s, cpu))
        if (auto* d = as<cmd::CreateDesign>(c)) CHECK(d->design.designType != "Attack Ship");
    // Without a research event or a tenth turn the designer rests.
    s.turn = 21;
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

std::optional<bool> answerTo(const Rules& r, const GameState& s, EmpireId cpu, MessageId id) {
    for (const Command& c : ai::planTurn(r, s, cpu))
        if (auto* a = as<cmd::AnswerMessage>(c); a && a->message == id) return a->accept;
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

    // A demand from an empire that is not far ahead is refused; the reply is valid.
    s.messages.clear();
    s.empire(cpu).relation(human).anger = 0;
    const MessageId demand = deliver(s, human, cpu, MessageType::DemandTribute);
    std::optional<bool> reply;
    for (uint32_t turn = 0; turn < 10 && !reply; ++turn) {
        s.turn = turn;
        reply = answerTo(r, s, cpu, demand);
    }
    REQUIRE(reply.has_value());
    CHECK_FALSE(*reply);
    const auto cmds = ai::planTurn(r, s, cpu);
    CHECK(applyAll(r, s, cpu, cmds).empty());
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

TEST_CASE("ai: an accepted request is carried out half of the time and queues a war") {
    TempTree t("request");
    t.write("Ai/Default_AI_Politics.txt",
            "Score Percent To Accept Declare war on empire := 0\nWill Accept From Enemy Declare war on empire := True\n"
            "Declare War Base Anger Level := 1000\nBreak Treaty Base Anger Level := 1000\nPropose Treaty Percent Chance Per Turn := 0\n");
    const Rules r{buildEngineRuleset(), t.root};
    GameState s = computerGame(7, 3, 0, 12, r);
    const EmpireId asker{0u}, cpu{1u}, third{2u};
    meet(s, asker, cpu);
    meet(s, cpu, third);
    meet(s, asker, third);
    bool queued = false;
    int accepted = 0;
    for (uint32_t turn = 1; turn < 30 && !queued; ++turn) {
        s.turn = turn;
        s.messages.clear();
        DiplomaticMessage m;
        m.id = MessageId{s.nextMessageId++};
        m.from = asker;
        m.to = cpu;
        m.type = MessageType::RequestDeclareWar;
        m.thirdEmpire = third;
        m.sentTurn = turn;
        m.delivered = true;
        s.messages.push_back(m);
        const auto cmds = ai::planTurn(r, s, cpu);
        REQUIRE(applyAll(r, s, cpu, cmds).empty());
        for (const DiplomaticMessage& reply : s.messages) accepted += reply.from == cpu && reply.type == MessageType::AcceptDemand;
        for (Empire& e : s.empires)
            for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
        TurnContext ctx{r, s, {}, {}, {}};
        ai::updateAnger(ctx);
        queued = s.empire(cpu).relation(third).queuedWar;
    }
    CHECK(accepted > 0);
    REQUIRE(queued);
    // The queued war is declared at the next opportunity.
    bool declared = false;
    for (const Command& c : ai::planTurn(r, s, cpu))
        if (const auto* m = as<cmd::SendMessage>(c); m && m->message.to == third && m->message.type == MessageType::DeclareWar) declared = true;
    CHECK(declared);
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
    s.empire(a).relation(b).promise = true;
    s.empire(a).relation(b).anger = 50;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease - 20 + 2 * table.perEnemyShip);
    CHECK_FALSE(s.empire(a).relation(b).promise);
    // Friends' ships do not count.
    s.empire(a).relation(b).treaty = Treaty::NonAggression;
    s.empire(a).relation(b).anger = 50;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease);
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

TEST_CASE("ai: Not Connected when little is left to settle, explore or reach") {
    const Rules& r = engineRules();
    GameState s = computerGame(13, 2, 0, 12);
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    // Everything explored, no warp link known (so almost nothing is reachable),
    // no colony module, and our ships already head for every frontier point.
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
    CHECK(ai::nextState(r, s, me) == ai::AiState::NotConnected);
    // Knowing the links makes the galaxy reachable: the test fails.
    s.empire(me).aiState = static_cast<int>(ai::AiState::NotConnected);
    s.empire(me).knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 1);
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

    // A premade scout under the Exploration minister explores.
    VehicleId scout;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == me && s.design(v.design).designType == "Scout") scout = v.id;
    REQUIRE(scout.valid());
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
    // A ship without a part it needs to operate is unfit too.
    for (size_t i = 6; i < 11; ++i) v.damage[i] = 0;
    v.damage[1] = entryStructure(r, s.design(warship), 1);  // life support
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

TEST_CASE("ai: the budget's revenue is what the income step banks, bonus included, tariffs paid") {
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
    REQUIRE(s.empire(cpu).economy.tariffsOut != Resources{});
    const Resources revenue = ai::detail::Planner(r, s, cpu, ai::detail::Mode::Computer, 9).revenue();
    const Resources before = s.empire(cpu).stockpile;
    economy::collectIncome(ctx, cpu);
    economy::collectTrade(ctx, cpu);
    CHECK(s.empire(cpu).stockpile - before == revenue);
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
