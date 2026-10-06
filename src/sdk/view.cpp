#include "sdk/view.hpp"

#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/intel.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/redact.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "sdk/parts.hpp"

#include <algorithm>

namespace opense4::sdk {

Perspective::Perspective(const game::Rules& r, const game::GameState& s, game::EmpireId empire, ViewOptions options)
    : rules_(&r), state_(&s), empire_(empire), options_(options) {
    if (!options_.whole) {
        redacted_ = std::make_shared<const game::GameState>(game::redactForEmpire(r, s, empire));
        state_ = redacted_.get();
    }
}

const game::Empire* Perspective::me() const {
    return empire_.valid() && empire_.index() < state_->empires.size() ? &state_->empire(empire_) : nullptr;
}

namespace detail {

namespace {

using game::Colony;
using game::Design;
using game::DesignId;
using game::Empire;
using game::EmpireId;
using game::Fleet;
using game::ObjectId;
using game::SpaceObject;
using game::StarSystem;
using game::SystemId;
using game::Vehicle;

class ViewBuilder {
public:
    explicit ViewBuilder(const Perspective& p)
        : r_(p.rules()), s_(p.state()), me_(p.empire()), my_(p.me()), whole_(p.whole()), want_(s_.designs.size(), 0),
          seen_(s_.designs.size(), 0) {
        if (my_)
            for (const game::SeenDesign& d : my_->knowledge.seenDesigns) mark(seen_, d.design);
    }

    Value build() {
        // Entities first: they name the designs the view must hold.
        Value colonyList = colonies();
        Value vehicleList = vehicles();
        Value fleetList = fleets();
        Value designList = designs();
        return Map(16)("api", num(kApiVersion))("empire", id(me_))("whole", Value(whole_))("game", gameValue())("my", myEmpire())(
                   "empires", empires())("systems", systems())("objects", objects())("colonies", std::move(colonyList))(
                   "vehicles", std::move(vehicleList))("fleets", std::move(fleetList))("designs", std::move(designList))(
                   "messages", messages())("log", log())("battles", battles())
            .done();
    }

private:
    const game::Rules& r_;
    const game::GameState& s_;
    EmpireId me_;
    const Empire* my_;
    bool whole_;
    std::vector<uint8_t> want_;   // per DesignId: the view lists it
    std::vector<uint8_t> seen_;   // per DesignId: a foreign design the empire knows

    static void mark(std::vector<uint8_t>& set, DesignId d) {
        if (d.valid() && d.index() < set.size()) set[d.index()] = 1;
    }
    bool mine(EmpireId owner) const { return my_ && owner == me_; }
    // Its owner's private details are shown.
    bool full(EmpireId owner) const { return whole_ || mine(owner); }
    // The system's contents are shown.
    bool shown(SystemId sys) const { return whole_ || (my_ && my_->hasExplored(sys)); }
    bool validDesign(DesignId d) const { return d.valid() && d.index() < s_.designs.size(); }

    // ---- The game ---------------------------------------------------------------------------------------

    Value gameValue() const {
        const bool turnBased = game::turnBased(s_);
        const EmpireId active = turnBased ? game::activePlayer(s_) : EmpireId{};
        return Map(12)("turn", num(s_.turn))("date", Value(game::score::dateText(s_.turn)))("year", num(s_.year()))(
                   "turn_style", Value(turnBased ? "turn_based" : "simultaneous"))("player_turn", id(active))(
                   "player_turn_started", Value(turnBased && s_.playerTurn.empire.valid() && s_.playerTurn.started))(
                   "game_over", Value(s_.gameOver))("winner", id(s_.winner))("peaceful_turns", num(s_.peacefulTurns))(
                   "options", options())("victory", enc(s_.options.victory))
            .done();
    }

    Value options() const {
        const game::GameOptions& o = s_.options;
        Value allowed;   // null: every area
        if (!o.techAreasAllowed.empty()) {
            ValueList l;
            for (size_t i = 0; i < o.techAreasAllowed.size(); ++i)
                if (o.techAreasAllowed[i]) l.push_back(num(i));
            allowed = Value(std::move(l));
        }
        return Map(28)("quadrant_type", Value(o.quadrantType))("all_systems_seen", Value(o.allSystemsSeen))("omnipresent", Value(o.omnipresent))(
                   "finite_resources", Value(o.finiteResources))("event_frequency", num(o.eventFrequency))(
                   "max_event_severity", num(o.maxEventSeverity))("tech_cost", num(o.techCost))("start_tech_level", num(o.startTechLevel))(
                   "tech_areas_allowed", std::move(allowed))("starting_resources", enc(o.startingResources))("racial_points", num(o.racialPoints))(
                   "no_tactical_combat", Value(o.noTacticalCombat))("complete_tech_tree", Value(o.completeTechTree))(
                   "allow_gifts", Value(o.allowGifts))("allow_tech_trades", Value(o.allowTechTrades))("allow_intel", Value(o.allowIntel))(
                   "no_ruins", Value(o.noRuins))("only_breathable", Value(o.onlyBreathable))("only_home_type", Value(o.onlyHomeType))(
                   "team_mode", Value(o.teamMode))("allow_surrender", Value(o.allowSurrender))("score_display", num(o.scoreDisplay))(
                   "max_ships_per_player", num(o.maxShipsPerPlayer))("max_units_per_player", num(o.maxUnitsPerPlayer))(
                   "ai_difficulty", num(o.aiDifficulty))("ai_bonus", num(o.aiBonus))
            .done();
    }

    // ---- The empire's own affairs ---------------------------------------------------------------------------

    Value myEmpire() const {
        if (!my_) return Value();
        const Empire& e = *my_;
        return Map(32)("id", id(e.id))("stored", enc(e.stockpile))("score", e.history.empty() ? Value() : num(e.history.back().score))(
                   "economy", economyValue(e.economy))(
                   "maintenance_percent", num(game::economy::maintenancePercent(r_, e)))("research", research(e))("intel", intel(e))(
                   "ministers", ministers(e))("settings", settings(e))("ship_count", num(game::shipCount(r_, s_, e.id)))(
                   "unit_count", num(game::unitCount(r_, s_, e.id)))("experience", num(e.experience))(
                   "race_age", Value(game::economy::raceAge(e.experience)))("home_system", id(e.homeSystem))(
                   "home_sector", e.homeSystem.valid() ? enc(e.homeSector) : Value())("claimed_systems", enc(e.claimedSystems))(
                   "systems_to_avoid", enc(e.systemsToAvoid))("tagged_minefields", enc(e.taggedMinefields))("waypoints", waypoints(e))(
                   "notes", notes(e))("strategies", strategies(e))("design_types", enc(e.designTypes))("colony_types", enc(e.colonyTypes))(
                   "repair_priorities", enc(e.repairPriorities))("colony_type_choices", enc(e.colonyTypeChoices))("questions", questions())(
                   "ai_difficulty", num(e.aiDifficulty))
            .done();
    }

    Value research(const Empire& e) const {
        ValueList queue;
        for (size_t i = 0; i < e.research.size(); ++i) {
            const game::ResearchProject& p = e.research[i];
            const bool known = p.area.valid() && p.area.index() < r_.data().techAreas.size();
            const int level = e.techLevel(p.area) + 1;
            queue.push_back(Map(5)("area", id(p.area))("progress", num(p.progress))("level", num(level))(
                                "cost", known ? num(game::research::levelCost(r_, s_, p.area, level)) : Value())(
                                "eta", num(game::research::etaTurns(r_, s_, e, i)))
                                .done());
        }
        return Map(10)("points", num(e.researchPool))("income", num(e.economy.research))("evenly", Value(e.researchEvenly))(
                   "repeat", Value(e.repeatResearch))("queue", Value(std::move(queue)))("levels", enc(e.techLevels))(
                   "researchable", enc(game::research::researchable(r_, s_, e)))("unique_areas", enc(e.uniqueAreasUnlocked))(
                   "total_levels", num(game::research::totalLevels(r_, e)))("tech_percent", num(game::research::techPercent(r_, s_, e)))
            .done();
    }

    Value intel(const Empire& e) const {
        ValueList queue;
        for (const game::IntelProjectOrder& o : e.intel) {
            const bool known = o.project < r_.data().intelProjects.size();
            queue.push_back(Map(9)("project", num(o.project))("target", id(o.target))("target_planet", id(o.targetPlanet))(
                                "target_vehicle", id(o.targetVehicle))("third_empire", id(o.thirdEmpire))("target_tech", id(o.targetTech))(
                                "progress", num(o.progress))("cost", known ? num(r_.data().intelProjects[o.project].cost) : Value())(
                                "problem", Value(game::intel::orderProblem(r_, s_, e.id, o)))
                                .done());
        }
        return Map(6)("points", num(e.intelPool))("income", num(e.economy.intelligence))("evenly", Value(e.intelEvenly))(
                   "repeat", Value(e.repeatIntel))("defense", num(game::intel::defensePoints(r_, s_, e.id)))("queue", Value(std::move(queue)))
            .done();
    }

    static Value ministers(const Empire& e) {
        uint32_t areas = e.ministers;
        return Map(5)("areas", enc(MinisterAreas{&areas}))("style", Value(e.ministerStyle))("use_race_style", Value(e.useRaceMinisterStyle))(
                   "new_vehicles", Value(e.ministersForNewVehicles))("all", Value(e.ministerAll))
            .done();
    }

    static Value settings(const Empire& e) {
        return Map(7)("clear_orders_on_encounter", enc(e.clearOrdersOnEncounter))("avoid_tagged_minefields", Value(e.avoidTaggedMinefields))(
                   "avoid_restricted_systems", Value(e.avoidRestrictedSystems))("choose_colony_type", Value(e.chooseColonyType))(
                   "ai_minimal_changes", Value(e.aiMinimalChanges))("email", Value(e.email))("interface", enc(e.interfaceOptions))
            .done();
    }

    static Value waypoints(const Empire& e) {
        ValueList out;
        for (size_t i = 0; i < e.waypoints.size(); ++i) {
            const game::Waypoint& w = e.waypoints[i];
            out.push_back(Map(4)("slot", num(i))("name", Value(w.name))("set", Value(w.set))("location", w.set ? enc(w.location) : Value()).done());
        }
        return Value(std::move(out));
    }

    static Value notes(const Empire& e) {
        ValueList out;
        for (size_t i = 0; i < e.knowledge.notes.size(); ++i)
            if (!e.knowledge.notes[i].empty()) out.push_back(Map(2)("system", num(i))("note", Value(e.knowledge.notes[i])).done());
        return Value(std::move(out));
    }

    static Value strategies(const Empire& e) {
        ValueList out;
        for (size_t i = 0; i < e.strategies.size(); ++i)
            out.push_back(Map(3)("index", num(i))("name", Value(e.strategies[i].name))("settings", enc(e.strategies[i].settings)).done());
        return Value(std::move(out));
    }

    Value questions() const {
        ValueList out;
        if (s_.playerTurn.empire == me_)
            for (const game::EntryQuestion& q : s_.playerTurn.questions)
                out.push_back(Map(4)("vehicle", id(q.vehicle))("fleet", id(q.fleet))("location", enc(q.where))("tagged", enc(q.tagged)).done());
        return Value(std::move(out));
    }

    // ---- Empires ----------------------------------------------------------------------------------------------

    Value empires() const { return listOf(s_.empires, [&](const Empire& e) { return empire(e); }); }

    Value empire(const Empire& e) const {
        const bool self = mine(e.id);
        const bool related = my_ && !self && e.id.index() < my_->relations.size();
        const bool partner = related && game::treatySharesSight(my_->relation(e.id).treaty);
        const bool scored = whole_ || self || (my_ && game::score::scoreVisible(s_, me_, e.id));
        const bool open = whole_ || self;
        Value relation, angerTowardMe;
        if (related) {
            const game::Relation& rel = my_->relation(e.id);
            relation = Map(8)("contact", Value(rel.contact))("treaty", enc(rel.treaty))("dominant", Value(rel.dominant))(
                           "trade_turns", num(rel.tradeTurns))("treaty_turn", num(rel.treatyTurn))("last_war_turn", num(rel.lastWarTurn))(
                           "message_sent_this_turn", Value(rel.messageSentThisTurn))("anger", num(rel.anger))
                           .done();
            if (e.kind != game::PlayerKind::Human && me_.index() < e.relations.size()) angerTowardMe = num(e.relation(me_).anger);
        }
        ValueList treaties;
        for (size_t k = 0; k < e.relations.size(); ++k) {
            const EmpireId other{k};
            if (other == e.id || !e.relations[k].contact) continue;
            if (!whole_ && !(my_ && game::diplomacy::treatyVisible(s_, me_, e.id, other))) continue;
            treaties.push_back(Map(2)("empire", id(other))("treaty", enc(e.relations[k].treaty)).done());
        }
        const game::TurnStats* stats = scored && !e.history.empty() ? &e.history.back() : nullptr;
        return Map(22)("id", id(e.id))("name", Value(e.name))("empire_type", Value(e.empireType))("leader_title", Value(e.leaderTitle))(
                   "leader_name", Value(e.leaderName))("color", num(e.color))("kind", enc(e.kind))("neutral", Value(game::isNeutral(e)))(
                   "alive", Value(e.alive))("is_me", Value(self))("race", raceValue(e.race))("relation", std::move(relation))(
                   "anger_toward_me", std::move(angerTowardMe))("treaties", Value(std::move(treaties)))(
                   "score", stats ? num(stats->score) : Value())("stats", stats ? statsValue(*stats) : Value())(
                   "tech_levels", whole_ || self || partner ? enc(e.techLevels) : Value())("stored", open ? enc(e.stockpile) : Value())(
                   "economy", open ? economyValue(e.economy) : Value())("claimed_systems", enc(e.claimedSystems))(
                   "home_system", open ? id(e.homeSystem) : Value())
            .done();
    }

    // ---- The galaxy -----------------------------------------------------------------------------------------------

    Value systems() const {
        std::vector<ValueList> claimedBy(s_.galaxy.systems.size());
        for (const Empire& e : s_.empires)
            for (SystemId sys : e.claimedSystems)
                if (sys.valid() && sys.index() < claimedBy.size()) claimedBy[sys.index()].push_back(id(e.id));
        ValueList out;
        out.reserve(s_.galaxy.systems.size());
        for (const StarSystem& sys : s_.galaxy.systems) {
            const size_t i = sys.id.index();
            const bool show = shown(sys.id);
            const game::Knowledge* k = my_ ? &my_->knowledge : nullptr;
            auto flag = [&](const std::vector<SystemId>& list) {
                return my_ && std::find(list.begin(), list.end(), sys.id) != list.end();
            };
            out.push_back(
                Map(16)("id", id(sys.id))("name", show ? Value(sys.name) : Value())(
                    "position", Map(2)("x", num(sys.position.x))("y", num(sys.position.y)).done())(
                    "explored", Value(my_ && my_->hasExplored(sys.id)))("present", Value(k && i < k->present.size() && k->present[i] != 0))(
                    "last_seen", num(k && i < k->lastSeen.size() ? k->lastSeen[i] : 0u))("type", show ? id(sys.type) : Value())(
                    "physical_type", show ? Value(sys.physicalType) : Value())("abilities", show ? abilityEntries(sys.abilities) : Value())(
                    "objects", show ? enc(sys.objects) : Value())("avoid", Value(flag(my_ ? my_->systemsToAvoid : std::vector<SystemId>{})))(
                    "claimed", Value(flag(my_ ? my_->claimedSystems : std::vector<SystemId>{})))(
                    "note", Value(k && i < k->notes.size() ? k->notes[i] : std::string()))("claimed_by", Value(std::move(claimedBy[i])))
                    .done());
        }
        return Value(std::move(out));
    }

    Value objects() const {
        ValueList out;
        for (const StarSystem& sys : s_.galaxy.systems)
            if (shown(sys.id))
                for (ObjectId o : sys.objects) out.push_back(object(s_.galaxy.object(o)));
        return Value(std::move(out));
    }

    Value object(const SpaceObject& o) const {
        const bool body = o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids;
        const bool warp = o.kind == game::ObjectKind::WarpPoint;
        Value destination, destinationSystem, linkKnown;
        if (warp) {
            linkKnown = Value(whole_ || (my_ && game::sight::knowsWarpLink(s_, me_, o.id)));
            if (o.destination.valid() && o.destination.index() < s_.galaxy.objects.size()) {
                const SystemId to = s_.galaxy.object(o.destination).system;
                if (shown(to)) {
                    destination = id(o.destination);
                    destinationSystem = id(to);
                }
            }
        }
        const Colony* colony = s_.colony(o.id);
        return Map(22)("id", id(o.id))("kind", enc(o.kind))("system", id(o.system))("sector", enc(o.sector))(
                   "name", Value(warp && my_ ? game::sight::warpPointName(s_, me_, o.id) : o.name))("sector_type", num(o.sectorType))(
                   "abilities", abilityEntries(o.abilities))("size", Value(o.size))("surface", Value(o.surface))(
                   "atmosphere", Value(o.atmosphere))("conditions", body ? num(o.conditions.inHundredths()) : Value())(
                   "conditions_band", body ? enc(game::economy::conditionsBand(o.conditions)) : Value())(
                   "value", body ? Map(3)("minerals", num(o.value[0]))("organics", num(o.value[1]))("radioactives", num(o.value[2])).done()
                                 : Value())("star_age", Value(o.starAge))("star_color", Value(o.starColor))(
                   "star_luminosity", Value(o.starLuminosity))("destination", std::move(destination))(
                   "destination_system", std::move(destinationSystem))("link_known", std::move(linkKnown))(
                   "colony", colony ? id(colony->owner) : Value())
            .done();
    }

    // ---- Colonies ------------------------------------------------------------------------------------------------------

    Value colonies() {
        ValueList out;
        for (const auto& c : s_.colonies)
            if (c) out.push_back(colony(*c));
        return Value(std::move(out));
    }

    Value colony(const Colony& c) {
        const bool f = full(c.owner);
        for (const game::UnitStack& u : c.landedTroops) mark(want_, u.design);
        if (f) {
            for (const game::UnitStack& u : c.cargo.units) mark(want_, u.design);
            for (const game::QueueItem& q : c.queue.items) mark(want_, q.design);
        }
        Value destroyed, output;
        if (f) {
            destroyed = listOf(c.destroyedFacilities, [](const game::DestroyedFacilities& d) {
                return Map(2)("facility", num(d.facility))("count", num(d.count)).done();
            });
            const game::economy::ColonyOutput o = game::economy::colonyOutput(r_, s_, c);
            output = Map(10)("production", enc(o.production))("research", num(o.research))("intelligence", num(o.intelligence))(
                         "supply", num(o.supply))("connected", Value(o.connected))("blockaded", Value(o.blockaded))(
                         "reproduction_percent", num(o.reproductionPercent))("delivery_percent", num(o.deliveryPercent))(
                         "facilities_operating", num(o.facilitiesOperating))("mood", enc(o.mood))
                         .done();
        }
        auto own = [&](Value v) { return f ? std::move(v) : Value(); };
        return Map(32)("planet", id(c.planet))("owner", id(c.owner))("colony_type", Value(c.colonyType))("population", enc(c.population))(
                   "total_population", num(c.totalPopulation()))("max_population", f ? num(game::maxPopulation(r_, s_, c)) : Value())(
                   "anger", num(c.anger))("mood", enc(game::moodFromAnger(c.anger)))(
                   "emotionless", Value(game::economy::emotionless(r_, s_, c.owner)))("homeworld", Value(c.homeworld))(
                   "founded_turn", num(c.foundedTurn))("cloaked", Value(c.cloaked))("plague_level", num(c.plagueLevel))(
                   "militia", num(c.militia))("invader", id(c.invader))("landed_troops", enc(c.landedTroops))(
                   "facilities", f ? enc(c.facilities) : Value())("facility_slots", f ? num(game::facilitySlots(r_, s_, c)) : Value())(
                   "destroyed_facilities", std::move(destroyed))("cargo", f ? enc(c.cargo) : Value())(
                   "cargo_capacity", f ? num(game::colonyCargoCapacity(r_, s_, c)) : Value())(
                   "cargo_used", f ? num(game::cargoSpaceUsed(r_, s_, c.cargo)) : Value())(
                   "queue", f ? queueValue(r_, s_, c.owner, {c.planet, {}}, c.queue) : Value())("orders", f ? enc(c.orders) : Value())(
                   "repeat_orders", own(Value(c.repeatOrders)))("minister", own(Value(c.minister)))(
                   "atmosphere_turns", f ? num(c.atmosphereTurns) : Value())("cloak_levels", f ? enc(c.cloakLevels) : Value())(
                   "sensor_levels", f ? enc(c.sensorLevels) : Value())("breathable", own(Value(game::breathable(s_, c))))(
                   "space_yard", own(Value(game::colonyHasSpaceYard(r_, c))))("output", std::move(output))
            .done();
    }

    // ---- Vehicles and fleets -------------------------------------------------------------------------------------------

    Value vehicles() {
        ValueList out;
        out.reserve(s_.vehicles.size());
        for (const Vehicle& v : s_.vehicles) out.push_back(vehicle(v));
        return Value(std::move(out));
    }

    Value vehicle(const Vehicle& v) {
        const bool f = full(v.owner);
        const bool hasDesign = validDesign(v.design);
        const std::vector<game::UnitStack> stacks = game::groupStacks(v);
        for (const game::UnitStack& u : stacks) mark(want_, u.design);
        if (f) {
            for (const game::UnitStack& u : v.cargo.units) mark(want_, u.design);
            for (const game::QueueItem& q : v.queue.items) mark(want_, q.design);
        }
        const bool d = f && hasDesign;   // own figures that need the design
        Value queue;
        if (d && (game::vehicleHasSpaceYard(r_, s_, v) || !v.queue.items.empty())) queue = queueValue(r_, s_, v.owner, {{}, v.id}, v.queue);
        return Map(32)("id", id(v.id))("owner", id(v.owner))("design", id(v.design))("name", Value(v.name))(
                   "type", hasDesign ? enc(game::vehicleType(r_, s_, v)) : Value())("location", enc(v.location))("count", num(v.count))(
                   "stacks", enc(stacks))("status", enc(v.status))("built_turn", num(v.builtTurn))(
                   "structure", hasDesign ? num(game::vehicleStructure(r_, s_, v)) : Value())("damage", num(game::vehicleDamageTaken(s_, v)))(
                   "component_damage", enc(v.damage))("experience", num(v.experience))("supply", f ? num(v.supply) : Value())(
                   "supply_capacity", d ? num(game::vehicleSupplyCapacity(r_, s_, v)) : Value())(
                   "unlimited_supply", d ? Value(game::vehicleHasUnlimitedSupply(r_, s_, v)) : Value())(
                   "movement", f ? num(v.movement) : Value())("max_movement", d ? num(game::vehicleMaxMovement(r_, s_, v)) : Value())(
                   "orders", f ? enc(v.orders) : Value())("repeat_orders", f ? Value(v.repeatOrders) : Value())(
                   "fleet", f ? id(v.fleet) : Value())("cargo", f ? enc(v.cargo) : Value())(
                   "cargo_capacity", d ? num(game::vehicleCargoCapacity(r_, s_, v)) : Value())(
                   "cargo_used", f ? num(game::cargoSpaceUsed(r_, s_, v.cargo)) : Value())("minister", f ? Value(v.minister) : Value())(
                   "immobile_until", f ? num(v.immobileUntil) : Value())("queue", std::move(queue))
            .done();
    }

    Value fleets() const {
        ValueList out;
        for (const Fleet& fl : s_.fleets) {
            if (!full(fl.owner)) continue;
            const Vehicle* leader = game::fleetLeader(s_, fl);
            out.push_back(Map(13)("id", id(fl.id))("owner", id(fl.owner))("name", Value(fl.name))("members", enc(fl.members))(
                              "leader", leader ? id(leader->id) : Value())("chosen_leader", id(fl.leader))("location", enc(fl.location))(
                              "formation", num(fl.formation))("strategy", num(fl.strategy))("experience", num(fl.experience))(
                              "minister", Value(fl.minister))("speed", num(game::movement::fleetSpeed(r_, s_, fl)))(
                              "orders", enc(game::fleetOrders(s_, fl)))("repeat_orders", Value(game::fleetRepeats(s_, fl)))
                              .done());
        }
        return Value(std::move(out));
    }

    // ---- Designs -------------------------------------------------------------------------------------------------------

    Value designs() {
        if (whole_) {
            for (const Empire& e : s_.empires)
                for (DesignId d : e.designs) mark(want_, d);
        } else if (my_) {
            for (DesignId d : my_->designs) mark(want_, d);
            for (const game::SeenDesign& d : my_->knowledge.seenDesigns) mark(want_, d.design);
        }
        // Designs of the foreign vehicles in view count as seen (as in redactForEmpire).
        for (const Vehicle& v : s_.vehicles)
            if (!full(v.owner))
                for (const game::UnitStack& u : game::groupStacks(v)) mark(seen_, u.design);
        ValueList out;
        for (size_t i = 0; i < want_.size(); ++i)
            if (want_[i]) out.push_back(design(s_.designs[i]));
        return Value(std::move(out));
    }

    Value design(const Design& d) const {
        const bool own = full(d.owner);
        const bool known = own || (d.id.index() < seen_.size() && seen_[d.id.index()]);
        Value seenTurn;
        if (!own && my_)
            if (const auto t = game::designSeenTurn(my_->knowledge, d.id)) seenTurn = num(*t);
        Value figures;
        if (known && d.hull < r_.data().vehicleSizes.size())
            figures = designFigures(r_, s_, game::computeDesignStats(r_, nullptr, d), d.hull, d.entries, own ? &d : nullptr);
        auto ifOwn = [&](Value v) { return own ? std::move(v) : Value(); };
        auto ifKnown = [&](Value v) { return known ? std::move(v) : Value(); };
        return Map(21)("id", id(d.id))("owner", id(d.owner))("known", Value(known))("name", ifKnown(Value(d.name)))(
                   "design_type", ifKnown(Value(d.designType)))("hull", num(d.hull))("picture", Value(d.picture))("entries", ifKnown(enc(d.entries)))(
                   "strategy", num(d.strategy))("obsolete", Value(d.obsolete))("created_turn", num(d.createdTurn))(
                   "template_name", ifKnown(Value(d.templateName)))("prototype", ifOwn(Value(game::designIsPrototype(d))))(
                   "built", ifOwn(num(d.built)))("lost", ifOwn(num(d.lost)))("scrapped", ifOwn(num(d.scrapped)))(
                   "enemy_tonnage_destroyed", ifOwn(num(d.enemyTonnageDestroyed)))("seen_turn", std::move(seenTurn))(
                   "figures", std::move(figures))
            .done();
    }

    // ---- Messages, the log and battles ------------------------------------------------------------------------------------

    Value messages() const {
        ValueList out;
        for (const game::DiplomaticMessage& m : s_.messages)
            if (whole_ || m.from == me_ || m.to == me_) out.push_back(enc(m));
        return Value(std::move(out));
    }

    Value log() const {
        if (!my_) return Value::emptyList();
        return listOf(my_->log, [](const game::LogEntry& l) {
            return Map(7)("turn", num(l.turn))("category", enc(l.category))("title", Value(l.title))("text", Value(l.text))(
                       "location", enc(l.location))("goto", enc(l.target))("message", id(l.message))
                .done();
        });
    }

    Value battles() const {
        ValueList out;
        for (const game::CombatRecord& c : s_.combats) {
            if (!whole_ && std::find(c.participants.begin(), c.participants.end(), me_) == c.participants.end()) continue;
            Value pieces = listOf(c.pieces, [](const game::CombatPiece& p) {
                return Map(9)("kind", enc(p.kind))("owner", id(p.owner))("vehicle", id(p.vehicle))("planet", id(p.planet))(
                           "design", id(p.design))("name", Value(p.name))("count", num(p.count))("damage", num(p.damage))(
                           "survivor", id(p.survivor))
                    .done();
            });
            out.push_back(Map(5)("turn", num(c.turn))("location", enc(c.location))("participants", enc(c.participants))(
                              "summary", enc(c.summary))("pieces", std::move(pieces))
                              .done());
        }
        return Value(std::move(out));
    }
};

} // namespace

} // namespace detail

script::Value buildView(const Perspective& p) { return detail::ViewBuilder(p).build(); }

script::Value buildView(const game::Rules& r, const game::GameState& s, game::EmpireId empire, ViewOptions options) {
    return buildView(Perspective(r, s, empire, options));
}

} // namespace opense4::sdk
