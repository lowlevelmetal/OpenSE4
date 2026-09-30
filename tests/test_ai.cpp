// Computer player and ministers (docs/spec/05 §7): the commands the AI
// produces. Movement and combat are not part of this branch, so a small fake
// world below moves ships to their destinations (and, for the long games,
// finishes queue items and research faster) to walk the AI through a game.

#include "engine_fixture.hpp"

#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/ai_planner.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
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

GameState computerGame(uint64_t seed, int computers, int neutrals, int systems) {
    GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = systems;
    for (int i = 0; i < computers + neutrals; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = i < computers ? PlayerKind::Computer : PlayerKind::Neutral;
        setup.empires.push_back(std::move(e));
    }
    auto g = createGame(engineRules(), setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    return std::move(*g);
}

void exploreEverything(GameState& s) {
    for (Empire& e : s.empires) {
        e.knowledge.explored.assign(s.galaxy.systems.size(), 1);
        e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 1);
    }
}

// Stand-in for movement (not merged on this branch): vehicles jump to where
// their orders lead, colony ships found colonies, and everyone stays in contact
// so the computers talk to each other.
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
        o << e.name << " state " << e.aiState << "/" << e.aiTurnsInState << " research";
        for (const auto& p : e.research) o << " " << p.area.value;
        o << " anger";
        for (const auto& rel : e.relations) o << " " << rel.anger;
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
        o << (c->queue.emergency ? " E" : "") << "\n";
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

} // namespace

// ---- Data ----------------------------------------------------------------------------------------

TEST_CASE("ai: state names and lists") {
    ai::AiState st;
    REQUIRE(ai::parseAiState("Defend (Short Term)", st));
    CHECK(st == ai::AiState::DefendShortTerm);
    CHECK(ai::displayName(ai::AiState::SecureHoldings) == "Secure Holdings After Attack");
    const auto mask = ai::parseStateList("Exploration, Attack,Not Connected, Bogus");
    CHECK(mask == (ai::maskOf(ai::AiState::Exploration) | ai::maskOf(ai::AiState::Attack) | ai::maskOf(ai::AiState::NotConnected)));
    Treaty t;
    REQUIRE(ai::parseTreatyName("Trade and Research Alliance", t));
    CHECK(t == Treaty::TradeResearchAlliance);
    CHECK(ai::angerKeyName(MessageType::Gift) == "Give Gift");
}

TEST_CASE("ai: built-in profile covers the tables") {
    const ai::AiProfile& p = ai::builtinProfile();
    CHECK(p.design("Attack Ship"));
    CHECK(p.design("Colony (Ice)"));
    CHECK(p.design("Scout"));
    for (size_t i = 0; i < ai::kAiStates; ++i) CHECK(p.vehicleQueue(static_cast<ai::AiState>(i)));
    CHECK(p.facilityQueue(ai::AiState::Attack, "Homeworld"));
    CHECK_FALSE(p.planetTypes.empty());
    CHECK(p.speech.pool("Send Propose Treaty"));
    CHECK(p.sources == std::vector<std::string>{"built-in"});
    // Rules without an install use the built-in profile.
    CHECK(&ai::profileFor(engineRules(), "Anything") == &p);
}

namespace {

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
};

} // namespace

TEST_CASE("ai: data files are read race first, then Ai/Default") {
    TempTree t("lookup");
    t.write("Ai/Default_AI_Anger.txt", "Per Attack Location := 7\nRegular Decrease := -9\nReceive Declare War := 33\n");
    t.write("Ai/Default_AI_Research.txt",
            "AI State := Exploration, Infrastructure\nTech Area Name := Test Beams\nTech Area Level := 3\nTech Area Min Percent := 50\n"
            "AI State := Attack\nTech Area Name := Test Armor\nTech Area Level := 9999\nTech Area Min Percent := 25\n");
    t.write("Ai/Default_AI_Construction_Vehicles.txt",
            "AI State := Exploration\nNum Queue Entries := 2\nEntry 1 Type := Attack Ship\nEntry 1 Planet Per Item := 10\n"
            "Entry 1 Must Have At Least := 3\nEntry 2 Type := Colonizer\nEntry 2 Planet Per Item := 0\nEntry 2 Must Have At Least := 1\n");
    t.write("Ai/Default_AI_Speech.txt", "Number of Send Declare War := 2\nSend Declare War 1 := Line one.\nSend Declare War 2 := Line two.\n"
                                        "Number of Mega Evil Declarations := 1\nMega Evil Declaration 1 := Beware.\n");
    t.write("Ai/Default_AI_Politics.txt", "Highest Allowed Treaty := Trade Alliance\nPropose Treaty Type Count := 1\n"
                                          "Propose Treaty Type 1 := Non-Aggression\nPropose Treaty Type 1 Anger Level Below Computed := 4\n"
                                          "Will Accept From Enemy Leave planet := True\n");
    t.write("Ai/Aggressive/Aggressive_AI_Anger.txt", "Regular Decrease := -1\n");
    t.write("Pictures/Races/Testian/Testian_AI_General.txt", "Name := Testian\nRace Opt 1 Num Characteristics := 0\n");
    t.write("Pictures/Races/Testian/Testian_AI_Anger.txt", "Regular Decrease := -4\n");
    t.write("Pictures/Races/Testian/Testian_AI_Settings.txt", "Personality Group := 3\nTurns to Wait until next attack := 11\n");
    t.write("Pictures/Races/Testian/testian_ai_construction_vehicles.txt", "AI State := Attack\nNum Queue Entries := 0\n");

    const ai::AiProfile race = ai::loadProfile(t.root, "Testian");
    CHECK(race.anger.regularDecrease == -4);        // race file wins
    CHECK(race.anger.perAttackLocation == ai::builtinProfile().anger.perAttackLocation);  // race file has no value: built-in
    CHECK(race.settings.personalityGroup == 3);
    CHECK(race.settings.turnsBetweenAttacks == 11);
    REQUIRE(race.research.size() == 2);             // from Ai/Default
    CHECK(race.research[0].area == "Test Beams");
    CHECK(race.research[0].states == (ai::maskOf(ai::AiState::Exploration) | ai::maskOf(ai::AiState::Infrastructure)));
    CHECK(race.research[1].level == 9999);
    // Construction tables are global: the race folder's copy is ignored.
    REQUIRE(race.vehicles.size() == 1);
    CHECK(race.vehicles[0].entries.size() == 2);
    CHECK(race.vehicles[0].entries[0].mustHave == 3);
    REQUIRE(race.speech.pool("Send Declare War"));
    CHECK(race.speech.pool("Send Declare War")->size() == 2);
    CHECK(race.speech.pool("Mega Evil Declarations")->front() == "Beware.");
    CHECK(race.politics.highestAllowedTreaty == Treaty::TradeAlliance);
    REQUIRE(race.politics.proposeTypes.size() == 1);
    CHECK(race.politics.proposeTypes[0] == std::pair{Treaty::NonAggression, 4});
    CHECK(race.politics.demands[static_cast<size_t>(MessageType::DemandLeavePlanet)].acceptFromEnemy);
    CHECK(race.sources.size() >= 5);

    const ai::AiProfile other = ai::loadProfile(t.root, "Nobody");
    CHECK(other.anger.regularDecrease == -9);       // falls back to Ai/Default
    CHECK(other.anger.receive[static_cast<size_t>(MessageType::DeclareWar)] == 33);

    const ai::AiProfile minister = ai::loadProfile(t.root, "Testian", "Aggressive");
    CHECK(minister.anger.regularDecrease == -1);    // the minister style's own file
    CHECK(minister.settings.personalityGroup == 3); // no style file: the race's

    // Rules with this root cache one profile per race.
    Rules rules{buildEngineRuleset(), t.root};
    const ai::AiProfile& cached = ai::profileFor(rules, "Testian");
    CHECK(&cached == &ai::profileFor(rules, "testian"));
    CHECK(cached.anger.regularDecrease == -4);
}

TEST_CASE("ai: random computer players are drawn by personality group") {
    TempTree t("random");
    t.write("Pictures/Races/Alpha/Alpha_AI_General.txt", "Name := Alpha\nRace Opt 1 Num Characteristics := 0\n");
    t.write("Pictures/Races/Alpha/Alpha_AI_Settings.txt", "Personality Group := 2\n");
    t.write("Pictures/Races/Beta/Beta_AI_General.txt", "Name := Beta\nRace Opt 1 Num Characteristics := 0\n");
    t.write("Pictures/Races/Beta/Beta_AI_Settings.txt", "Personality Group := 0\n");
    t.write("Pictures/RaceNeutral/Gamma/Gamma_AI_General.txt", "Name := Gamma\n");
    ruleset::Ruleset data = buildEngineRuleset();
    data.settings.set("Random Player Personality Groups", "2");
    data.settings.set("Random Player Personality Group 1 Percent", "0");
    data.settings.set("Random Player Personality Group 2 Percent", "100");
    data.settings.set("Minimum Computer Player Medium Setting", "3");
    data.settings.set("Maximum Computer Player Medium Setting", "3");
    data.settings.set("Minimum Neutral Player Low Setting", "1");
    data.settings.set("Maximum Neutral Player Low Setting", "1");
    const Rules rules{std::move(data), t.root};
    REQUIRE(rules.racePresets().size() == 3);
    Rng rng(5);
    const auto computers = ai::randomComputerPresets(rules, 1, false, rng);
    REQUIRE(computers.size() == 3);
    CHECK(computers[0] == "Alpha");  // the only group-2 race; group 0 is drawn only when nothing else is left
    const auto neutrals = ai::randomComputerPresets(rules, 0, true, rng);
    CHECK(neutrals == std::vector<std::string>{"Gamma"});
}

TEST_CASE("ai: research follows the AI_Research table") {
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
    const auto cmds = ai::planTurn(rules, s, EmpireId{0u});
    const cmd::SetResearch* research = nullptr;
    for (const Command& c : cmds)
        if (auto* x = as<cmd::SetResearch>(c)) research = x;
    REQUIRE(research);
    // Beams and Armor each allow one other project (50 %): the third row waits.
    const auto beams = techArea(rules, "Test Beams"), armor = techArea(rules, "Test Armor");
    REQUIRE(research->queue.size() >= 2);
    CHECK(research->queue[0].area == beams);
    CHECK(research->queue[1].area == armor);
    // Further levels of the same areas soak up the rest of a turn's points,
    // funded in order.
    int64_t need = 0;
    int beamLevels = 0, armorLevels = 0;
    for (const ResearchProject& q : research->queue) {
        CHECK((q.area == beams || q.area == armor));
        int& n = q.area == beams ? beamLevels : armorLevels;
        need += research::levelCost(rules, s, q.area, s.empire(EmpireId{0u}).techLevel(q.area) + 1 + n++);
    }
    CHECK((need >= s.empire(EmpireId{0u}).economy.research || research->queue.size() == 12));
    CHECK_FALSE(research->evenly);
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

// ---- Specific decisions --------------------------------------------------------------------------

TEST_CASE("ai: scouts explore unexplored space") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, true);
    const EmpireId me{0u};
    const auto cmds = ai::planTurn(r, s, me);
    int scoutsOrdered = 0;
    for (const Command& c : cmds) {
        const auto* o = as<cmd::SetOrders>(c);
        if (!o || !o->vehicle.valid()) continue;
        const Vehicle* v = s.vehicle(o->vehicle);
        REQUIRE(v);
        if (s.design(v->design).designType != "Scout") continue;
        ++scoutsOrdered;
        REQUIRE_FALSE(o->orders.empty());
        const Order& last = o->orders.back();
        if (last.kind == OrderKind::Warp) {
            const SpaceObject& wp = s.galaxy.object(last.object);
            CHECK(s.empire(me).hasExplored(wp.system));
            CHECK_FALSE(s.empire(me).hasExplored(s.galaxy.object(wp.destination).system));
        } else {
            CHECK(last.kind == OrderKind::Explore);
        }
    }
    CHECK(scoutsOrdered == 2);
    // Two scouts do not take the same warp point.
    std::vector<ObjectId> targets;
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c); o && !o->orders.empty() && o->orders.back().kind == OrderKind::Warp)
            targets.push_back(o->orders.back().object);
    if (targets.size() == 2) CHECK(targets[0] != targets[1]);
}

TEST_CASE("ai: a colony ship gets a valid colonize order") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u};
    // Make sure a suitable planet exists next door.
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
    }
    CHECK(found);
}

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
    const Colony& home = homeworld(s, me);
    CHECK_FALSE(home.queue.items.empty());
    CHECK(countOf<cmd::CreateDesign>(cmds) >= 1);
    for (DesignId d : s.empire(me).designs) {
        const DesignStats st = computeDesignStats(r, &s.empire(me), s.design(d));
        CHECK_MESSAGE(st.problems.empty(), s.design(d).name);
    }
}

TEST_CASE("ai: the designer makes valid designs for the built-in templates") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 1, 8, true);
    Empire& e = s.empires[0];
    for (size_t i = 0; i < e.techLevels.size(); ++i) e.techLevels[i] = r.data().techAreas[i].maxLevel;
    int made = 0;
    for (const ai::DesignTemplate& t : ai::builtinProfile().designs) {
        auto d = ai::detail::buildDesign(r, e, t, 0);
        if (!d) continue;
        ++made;
        const DesignStats st = computeDesignStats(r, &e, *d);
        CHECK_MESSAGE(st.problems.empty(), t.name << ": " << (st.problems.empty() ? "" : st.problems.front()));
        CHECK(st.movement >= t.minSpeed);
        if (t.name == "Attack Ship") {
            CHECK(st.armed());
            CHECK(r.hull(d->hull).name == "Test Cruiser");  // the biggest hull
        }
        if (t.name == "Colony (Ice)") CHECK(st.canColonizeIce);
    }
    CHECK(made >= 12);
    // A tonnage cap keeps early warships small.
    auto capped = ai::detail::buildDesign(r, e, *ai::builtinProfile().design("Attack Ship"), 200);
    REQUIRE(capped);
    CHECK(r.hull(capped->hull).name == "Test Frigate");
}

TEST_CASE("ai: messages are answered from the politics tables") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    s.empire(human).relation(cpu).contact = true;
    s.empire(cpu).relation(human).contact = true;
    auto deliver = [&](MessageType type, Treaty treaty = Treaty::None) {
        DiplomaticMessage m;
        m.id = MessageId{s.nextMessageId++};
        m.from = human;
        m.to = cpu;
        m.type = type;
        m.treaty = treaty;
        m.sentTurn = s.turn;
        m.delivered = true;
        if (type == MessageType::Gift) {
            PackageItem item;
            item.resources = {1000, 0, 0};
            m.offer.push_back(item);
        }
        s.messages.push_back(m);
        return m.id;
    };
    auto answerTo = [&](MessageId id) -> std::optional<bool> {
        for (const Command& c : ai::planTurn(r, s, cpu))
            if (auto* a = as<cmd::AnswerMessage>(c); a && a->message == id) return a->accept;
        return std::nullopt;
    };

    const MessageId treaty = deliver(MessageType::ProposeTreaty, Treaty::TradeAlliance);
    const MessageId gift = deliver(MessageType::Gift);
    s.empire(cpu).relation(human).anger = 0;
    CHECK(answerTo(treaty) == true);
    CHECK(answerTo(gift) == true);

    // Very angry: treaties and gifts are refused (allowing for the small
    // "minimum anger chance" of accepting anyway).
    s.empire(cpu).relation(human).anger = 150;
    int accepted = 0;
    for (uint32_t turn = 60; turn < 80; ++turn) {
        s.turn = turn;
        accepted += answerTo(treaty).value_or(false);
        CHECK(answerTo(gift) == false);
    }
    CHECK(accepted <= 4);

    // A demand from a much weaker empire is refused; the reply is valid.
    s.turn = 5;
    s.empire(cpu).relation(human).anger = 0;
    const MessageId demand = deliver(MessageType::DemandTribute);
    const auto reply = answerTo(demand);
    REQUIRE(reply.has_value());
    CHECK_FALSE(*reply);
    const auto cmds = ai::planTurn(r, s, cpu);
    CHECK(applyAll(r, s, cpu, cmds).empty());
    // Messages already answered are left alone.
    CHECK(countOf<cmd::AnswerMessage>(ai::planTurn(r, s, cpu)) == 0);
}

TEST_CASE("ai: war and anger drive declarations and proposals") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 2, 12, false);
    const EmpireId human{0u}, cpu{1u};
    s.empire(human).relation(cpu).contact = true;
    s.empire(cpu).relation(human).contact = true;
    s.empire(cpu).relation(human).anger = 180;
    bool declared = false;
    for (const Command& c : ai::planTurn(r, s, cpu))
        if (auto* m = as<cmd::SendMessage>(c); m && m->message.to == human && m->message.type == MessageType::DeclareWar) declared = true;
    CHECK(declared);

    // Calm: sooner or later a treaty is proposed (never the current one).
    s.empire(cpu).relation(human).anger = 0;
    int proposals = 0;
    for (uint32_t turn = 0; turn < 60; ++turn) {
        s.turn = turn;
        for (const Command& c : ai::planTurn(r, s, cpu))
            if (auto* m = as<cmd::SendMessage>(c); m && m->message.type == MessageType::ProposeTreaty) {
                ++proposals;
                CHECK(m->message.treaty > s.empire(cpu).relation(human).treaty);
                CHECK_FALSE(m->message.text.empty());
            }
    }
    CHECK(proposals > 0);
}

TEST_CASE("ai: minimal mode only keeps things running") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(9, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u};
    s.empire(me).relation(EmpireId{1u}).contact = true;
    const auto full = ai::planTurn(r, s, me, false);
    const auto minimal = ai::planTurn(r, s, me, true);
    CHECK(minimal.size() < full.size());
    CHECK(countOf<cmd::SetResearch>(minimal) == 1);  // the queue was empty
    CHECK(countOf<cmd::QueueAdd>(minimal) >= 1);
    for (const Command& c : minimal) {
        CHECK_FALSE(std::holds_alternative<cmd::SetOrders>(c));
        CHECK_FALSE(std::holds_alternative<cmd::CreateDesign>(c));
        CHECK_FALSE(std::holds_alternative<cmd::SendMessage>(c));
        CHECK_FALSE(std::holds_alternative<cmd::AnswerMessage>(c));
        CHECK_FALSE(std::holds_alternative<cmd::Scrap>(c));
    }
    // With research and queues running, a minimal turn changes nothing.
    CHECK(applyAll(r, s, me, minimal).empty());
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
        for (const Command& c : cmds) {
            if (const auto* o = as<cmd::SetOrders>(c)) {
                for (const Order& ord : o->orders) {
                    CHECK(ord.kind != OrderKind::Warp);
                    CHECK(ord.kind != OrderKind::Explore);
                    if (ord.kind == OrderKind::MoveTo || ord.kind == OrderKind::Colonize) CHECK(ord.location.system == home);
                }
            }
            if (const auto* m = as<cmd::SendMessage>(c)) CHECK(m->message.type != MessageType::ProposeTreaty);
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

    // A scout under minister control explores.
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

    // Full minister control includes research; computer empires get nothing here.
    s.empire(me).ministerAll = true;
    CHECK(countOf<cmd::SetResearch>(ai::ministerCommands(r, s, me)) == 1);
    s.empire(EmpireId{1u}).kind = PlayerKind::Computer;
    CHECK(ai::ministerCommands(r, s, EmpireId{1u}).empty());
}

TEST_CASE("ai: fleets form and attack a war enemy when strong enough") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u}, enemy{1u};
    s.empire(me).relation(enemy) = Relation{true, Treaty::War, false, 0, 0, 0, 60, false};
    s.empire(enemy).relation(me) = Relation{true, Treaty::War, false, 0, 0, 0, 60, false};
    s.empire(me).aiState = static_cast<int>(ai::AiState::Attack);
    s.turn = 40;
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId warship = addTestDesign(s, r, me, "Hammer", "Test Cruiser",
                                           {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine",
                                            "Test Supply Pod", "Test Laser", "Test Laser", "Test Laser", "Test Laser", "Test Armor Plate"});
    s.design(warship).designType = "Attack Ship";
    for (int i = 0; i < 8; ++i) addTestVehicle(s, r, warship, home);

    const auto cmds = ai::planTurn(r, s, me);
    CHECK(countOf<cmd::CreateFleet>(cmds) >= 1);
    bool attack = false;
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c))
            for (const Order& ord : o->orders)
                if (ord.kind == OrderKind::Attack && ord.object.valid()) {
                    const Colony* target = s.colony(ord.object);
                    REQUIRE(target);
                    CHECK(target->owner == enemy);
                    attack = true;
                }
    CHECK(attack);
    CHECK(applyAll(r, s, me, cmds).empty());
}

TEST_CASE("ai: troop transports load troops and invade held enemy colonies") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, true);
    exploreEverything(s);
    const EmpireId me{0u}, enemy{1u};
    s.empire(me).relation(enemy).treaty = Treaty::War;
    s.empire(enemy).relation(me).treaty = Treaty::War;
    s.empire(me).aiState = static_cast<int>(ai::AiState::Attack);
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const Location target = locationOf(s.galaxy, homeworld(s, enemy).planet);
    const DesignId warship = addTestDesign(s, r, me, "Picket", "Test Frigate",
                                           {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser"});
    addTestVehicle(s, r, warship, target);  // we hold the enemy system
    const DesignId troop = addTestDesign(s, r, me, "Grunt", "Test Troop Hull", {"Test Troop Rifle"});
    const DesignId lander = addTestDesign(s, r, me, "Lander", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay"});
    s.design(lander).designType = "Troop Transport";
    const VehicleId loaded = addTestVehicle(s, r, lander, home).id;
    s.vehicle(loaded)->cargo.units.push_back({troop, 3});
    const VehicleId empty = addTestVehicle(s, r, lander, home).id;
    homeworld(s, me).cargo.units.push_back({troop, 5});
    sight::updateKnowledge(r, s);

    const auto cmds = ai::planTurn(r, s, me);
    bool dropped = false, loading = false;
    for (const Command& c : cmds) {
        const auto* o = as<cmd::SetOrders>(c);
        if (!o || o->orders.empty()) continue;
        const Order& ord = o->orders.front();
        if (o->vehicle == loaded) {
            dropped = ord.kind == OrderKind::DropCargo && ord.location == target && ord.design == troop;
        }
        if (o->vehicle == empty) loading = ord.kind == OrderKind::LoadCargo && ord.location == home && ord.design == troop;
    }
    CHECK(dropped);
    CHECK(loading);
    CHECK(applyAll(r, s, me, cmds).empty());
}

TEST_CASE("ai: defenders answer a threat at home") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(13, 2, 12, true);
    const EmpireId me{0u}, enemy{1u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId raider = addTestDesign(s, r, enemy, "Raider", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser"});
    const VehicleId intruder = addTestVehicle(s, r, raider, home).id;
    const DesignId guard = addTestDesign(s, r, me, "Guard", "Test Frigate",
                                         {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser"});
    const VehicleId mine = addTestVehicle(s, r, guard, home).id;
    sight::updateKnowledge(r, s);

    CHECK(ai::nextState(r, s, me) == ai::AiState::DefendShortTerm);
    s.empire(me).aiState = static_cast<int>(ai::AiState::DefendShortTerm);
    s.empire(me).aiTurnsInState = 12;
    CHECK(ai::nextState(r, s, me) == ai::AiState::DefendLongTerm);

    const auto cmds = ai::planTurn(r, s, me);
    bool engaged = false;
    for (const Command& c : cmds)
        if (const auto* o = as<cmd::SetOrders>(c); o && o->vehicle == mine)
            engaged = !o->orders.empty() && o->orders.front().kind == OrderKind::Attack && o->orders.front().vehicle == intruder;
    CHECK(engaged);
    // The home yard goes to emergency construction.
    bool emergency = false;
    for (const Command& c : cmds)
        if (const auto* f = as<cmd::QueueFlags>(c)) emergency = emergency || f->emergency;
    CHECK(emergency);
}

TEST_CASE("ai: anger decays, notices intruders, messages and the Mega Evil Empire") {
    ruleset::Ruleset data = buildEngineRuleset();
    data.settings.set("AI Mega Evil Empire Threshold Score Thousands", "0");
    data.settings.set("AI Computer Mega Evil Empire Score Percent", "150");
    data.settings.set("AI Human Mega Evil Empire Score Percent", "150");
    const Rules r{std::move(data)};
    GameSetup setup;
    setup.seed = 4;
    setup.options.systemCount = 10;
    for (int i = 0; i < 3; ++i) setup.empires.push_back(computerSetup(std::format("E{}", i)));
    auto g = createGame(r, setup);
    REQUIRE(g);
    GameState& s = *g;
    const EmpireId a{0u}, b{1u}, c{2u};
    const auto& table = ai::builtinProfile().anger;

    // Nobody stands out yet: plain decay down to the floor.
    REQUIRE_FALSE(ai::megaEvilEmpire(r, s).valid());
    s.empire(a).relation(b).anger = 50;
    s.empire(a).relation(c).anger = 1;
    TurnContext ctx{r, s, {}, {}, {}};
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger == 50 + table.regularDecrease);
    CHECK(s.empire(a).relation(c).anger == std::max(table.minimum, 1 + table.regularDecrease));

    // Two of b's ships inside a's claimed home system.
    const Location home = locationOf(s.galaxy, homeworld(s, a).planet);
    const DesignId probe =
        addTestDesign(s, r, b, "Probe", "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    addTestVehicle(s, r, probe, home);
    addTestVehicle(s, r, probe, home);
    sight::updateKnowledge(r, s);
    // And a declaration of war that arrived this turn.
    DiplomaticMessage war;
    war.from = b;
    war.to = a;
    war.type = MessageType::DeclareWar;
    s.turn += 1;
    war.sentTurn = s.turn;  // delivered this turn
    war.delivered = true;
    s.messages.push_back(war);
    s.empire(a).relation(b).anger = 50;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(b).anger ==
          50 + table.regularDecrease + 2 * table.perNoTreatyShip + table.receive[static_cast<size_t>(MessageType::DeclareWar)]);

    // c pulls far ahead: it becomes the Mega Evil Empire and everyone else resents it.
    for (int& level : s.empire(c).techLevels) level += 20;
    REQUIRE(ai::megaEvilEmpire(r, s) == c);
    s.messages.clear();
    s.empire(a).relation(c).anger = 10;
    ai::updateAnger(ctx);
    CHECK(s.empire(a).relation(c).anger == 10 + table.regularDecrease + table.megaEvilEmpire);
    CHECK(s.empire(c).relation(a).anger == std::max(table.minimum, table.regularDecrease));
}

TEST_CASE("ai: difficulty and bonus helpers") {
    GameState s = newEngineGame(3, 2, 8, false);
    s.options.aiBonus = 2;
    CHECK(ai::incomeBonusFactor(s, EmpireId{0u}) == 1);  // humans get no bonus
    CHECK(ai::incomeBonusFactor(s, EmpireId{1u}) == 3);
    CHECK(ai::constructionBonusPercent(s, EmpireId{0u}) == 100);
    CHECK(ai::constructionBonusPercent(s, EmpireId{1u}) == 200);
    CHECK(ai::moodLabel(0) == "Friendly");
    CHECK(ai::moodLabel(95) == "Furious");
    // An easy computer makes fewer plans than a hard one on the same turn.
    const Rules& r = engineRules();
    s.turn = 1;
    s.options.aiDifficulty = 0;
    const auto easy = ai::planTurn(r, s, EmpireId{1u});
    s.options.aiDifficulty = 3;
    const auto hard = ai::planTurn(r, s, EmpireId{1u});
    CHECK(countOf<cmd::QueueAdd>(easy) < countOf<cmd::QueueAdd>(hard));
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
