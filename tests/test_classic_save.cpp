// The original's saved games (docs/spec/08): the container's test vector,
// value encodings, OpenSE4 games through export and import in both turn
// styles, errors, and (opt-in) the original's own saves:
//
//   OPENSE4_CLASSIC_DATA=auto OPENSE4_ORIGINAL_SAVES=<folder> ./opense4_tests -tc="*classic save*"
//
// No save of the original is a fixture: the opt-in tests read the player's
// own files.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "game/classic_save.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::game::classic;

namespace {

std::vector<uint8_t> encodeOrFail(const ClassicSave& s) {
    auto bytes = encodeClassicSave(s);
    REQUIRE_MESSAGE(bytes.has_value(), (bytes ? std::string{} : bytes.error()));
    return std::move(*bytes);
}

// A played game of the test rules: the first empire human, two computer
// players, a few ships at every home, `turns` turns played.
GameState playedGame(bool simultaneous, int turns, uint64_t seed = 11) {
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(seed, 3, 14, /*allHuman=*/false);
    s.options.simultaneous = simultaneous;
    test::addHomeShips(s, r);
    for (int t = 0; t < turns && !s.gameOver; ++t) processTurn(r, s, {});
    if (!simultaneous) resumeTurnBased(r, s);
    return s;
}

ClassicSave exportOrFail(const Rules& r, const GameState& s, uint64_t keySeed = 3) {
    ConversionReport report;
    auto save = exportClassicSave(r, s, report, {keySeed, "test"});
    REQUIRE_MESSAGE(save.has_value(), (save ? std::string{} : save.error()));
    return std::move(*save);
}

GameState importOrFail(const Rules& r, const ClassicSave& save, ConversionReport* out = nullptr) {
    ConversionReport report;
    auto s = importClassicSave(r, save, report);
    REQUIRE_MESSAGE(s.has_value(), (s ? std::string{} : s.error()));
    if (out) *out = report;
    return std::move(*s);
}

// The vehicle at each slot.
std::map<uint32_t, const Vehicle*> bySlot(const GameState& s) {
    std::map<uint32_t, const Vehicle*> out;
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0) out[v.slot] = &v;
    return out;
}
std::map<uint32_t, const SpaceObject*> objectsBySlot(const GameState& s) {
    std::map<uint32_t, const SpaceObject*> out;
    for (const StarSystem& sys : s.galaxy.systems)
        for (ObjectId o : sys.objects) out[s.galaxy.object(o).slot] = &s.galaxy.object(o);
    return out;
}

uint32_t slotOfObject(const GameState& s, ObjectId o) { return o.valid() ? s.galaxy.object(o).slot : UINT32_MAX; }
uint32_t slotOfVehicle(const GameState& s, VehicleId v) {
    const Vehicle* x = v.valid() ? s.vehicle(v) : nullptr;
    return x ? x->slot : UINT32_MAX;
}
// A fleet as (owner, its position among the owner's fleets).
std::pair<uint32_t, size_t> fleetKey(const GameState& s, FleetId f) {
    if (!f.valid()) return {UINT32_MAX, 0};
    const Fleet* fleet = s.fleet(f);
    if (!fleet) return {UINT32_MAX, 1};
    size_t n = 0;
    for (const Fleet& x : s.fleets) {
        if (x.owner != fleet->owner) continue;
        if (x.id == f) break;
        ++n;
    }
    return {fleet->owner.value, n};
}

// Two orders mean the same in both states (ids through slots; what the
// format cannot carry, spec 08 §7.6, compared as the format carries it).
void checkOrder(const Rules& r, const GameState& a, const Order& x, const GameState& b, const Order& y) {
    INFO("order kind " << static_cast<int>(x.kind));
    CHECK(x.kind == y.kind);
    if (x.kind != y.kind) return;
    CHECK(slotOfObject(a, x.object) == slotOfObject(b, y.object));
    CHECK(slotOfVehicle(a, x.vehicle) == slotOfVehicle(b, y.vehicle));
    CHECK(x.from == y.from);
    CHECK(x.to == y.to);
    switch (x.kind) {
        case OrderKind::LoadCargo:
        case OrderKind::DropCargo:
        case OrderKind::LaunchUnits:
        case OrderKind::RecoverUnits:
            // By cargo kind in the original: a design of the same kind.
            CHECK(x.design.valid() == y.design.valid());
            if (x.design.valid() && y.design.valid()) CHECK(r.hull(a.design(x.design).hull).type == r.hull(b.design(y.design).hull).type);
            break;
        case OrderKind::Colonize: break;   // its colonists are loaded when given, in both games
        case OrderKind::JoinFleet: CHECK(fleetKey(a, FleetId{static_cast<uint32_t>(x.amount)}) == fleetKey(b, FleetId{static_cast<uint32_t>(y.amount)})); break;
        default:
            CHECK(x.design == y.design);
            CHECK(x.amount == y.amount);
            if (!x.vehicle.valid() && !x.object.valid()) CHECK(x.location == y.location);
            break;
    }
}

// Every field the format carries, compared between an OpenSE4 game and the
// same game after export and import (spec 08 §6). Objects and vehicles are
// matched by slot, fleets by owner and position.
void checkCarried(const Rules& r, const GameState& a, const GameState& b) {
    CHECK(a.turn == b.turn);
    CHECK(a.seed % 9999 == b.seed);
    CHECK(a.options.simultaneous == b.options.simultaneous);
    if (!a.options.quadrantType.empty()) CHECK(a.options.quadrantType == b.options.quadrantType);
    CHECK(a.options.eventFrequency == b.options.eventFrequency);
    CHECK(a.options.maxEventSeverity == b.options.maxEventSeverity);
    CHECK(a.options.techCost == b.options.techCost);
    CHECK(a.options.maxShipsPerPlayer == b.options.maxShipsPerPlayer);
    CHECK(a.options.maxUnitsPerPlayer == b.options.maxUnitsPerPlayer);
    CHECK(a.options.allowSurrender == b.options.allowSurrender);
    CHECK(a.options.victory.years == b.options.victory.years);
    CHECK(a.options.victory.yearsValue == b.options.victory.yearsValue);
    CHECK(a.peacefulTurns == b.peacefulTurns);
    CHECK(a.gameOver == b.gameOver);
    if (!a.options.simultaneous) CHECK(a.playerTurn.empire == b.playerTurn.empire);

    // Galaxy.
    REQUIRE(a.galaxy.systems.size() == b.galaxy.systems.size());
    for (size_t i = 0; i < a.galaxy.systems.size(); ++i) {
        const StarSystem& x = a.galaxy.systems[i];
        const StarSystem& y = b.galaxy.systems[i];
        INFO("system " << x.name);
        CHECK(x.name == y.name);
        CHECK(x.position == y.position);
        CHECK(x.type == y.type);
        CHECK(x.physicalType == y.physicalType);
        CHECK(x.objects.size() == y.objects.size());
    }
    const auto oa = objectsBySlot(a), ob = objectsBySlot(b);
    REQUIRE(oa.size() == ob.size());
    for (const auto& [slot, x] : oa) {
        INFO("object slot " << slot);
        REQUIRE(ob.contains(slot));
        const SpaceObject* y = ob.at(slot);
        CHECK(x->kind == y->kind);
        CHECK(x->system == y->system);
        CHECK(x->sector == y->sector);
        CHECK(x->sectorType == y->sectorType);
        CHECK(x->size == y->size);
        CHECK(x->atmosphere == y->atmosphere);
        if (x->kind == ObjectKind::Planet || x->kind == ObjectKind::Asteroids) {
            CHECK(x->name == y->name);
            CHECK(x->conditions == y->conditions);
            CHECK(x->value == y->value);
        }
        if (x->kind == ObjectKind::WarpPoint) CHECK(slotOfObject(a, x->destination) == slotOfObject(b, y->destination));
        const Colony* ca = a.colony(x->id);
        const Colony* cb = b.colony(y->id);
        CHECK((ca != nullptr) == (cb != nullptr));
        if (!ca || !cb) continue;
        CHECK(ca->owner == cb->owner);
        CHECK(ca->colonyType == cb->colonyType);
        CHECK(ca->population == cb->population);
        CHECK(ca->anger == cb->anger);
        std::vector<uint32_t> fa = ca->facilities, fb = cb->facilities;
        std::sort(fa.begin(), fa.end());
        std::sort(fb.begin(), fb.end());
        CHECK(fa == fb);   // grouped per kind in the file
        CHECK(ca->cargo.population == cb->cargo.population);
        CHECK(ca->cargo.units == cb->cargo.units);
        REQUIRE(ca->queue.items.size() == cb->queue.items.size());
        for (size_t k = 0; k < ca->queue.items.size(); ++k) {
            CHECK(ca->queue.items[k].kind == cb->queue.items[k].kind);
            CHECK(ca->queue.items[k].design == cb->queue.items[k].design);
            CHECK(ca->queue.items[k].facility == cb->queue.items[k].facility);
            CHECK(ca->queue.items[k].count == cb->queue.items[k].count);
        }
        if (!ca->queue.items.empty()) CHECK(ca->queue.items.front().spent == cb->queue.items.front().spent);
        CHECK(ca->queue.onHold == cb->queue.onHold);
        CHECK(ca->queue.repeat == cb->queue.repeat);
        CHECK(ca->queue.emergency == cb->queue.emergency);
        CHECK(ca->queue.autoWaypoint == cb->queue.autoWaypoint);
        CHECK(ca->plagueLevel == cb->plagueLevel);
        CHECK(ca->atmosphereTurns == cb->atmosphereTurns);
        CHECK(ca->minister == cb->minister);
        CHECK(ca->homeworld == cb->homeworld);
        CHECK(ca->cloaked == cb->cloaked);
        CHECK(ca->invader == cb->invader);
        CHECK(ca->repeatOrders == cb->repeatOrders);
        REQUIRE(ca->orders.size() == cb->orders.size());
        for (size_t k = 0; k < ca->orders.size(); ++k) checkOrder(r, a, ca->orders[k], b, cb->orders[k]);
    }

    // Empires.
    REQUIRE(a.empires.size() == b.empires.size());
    for (size_t i = 0; i < a.empires.size(); ++i) {
        const Empire& x = a.empires[i];
        const Empire& y = b.empires[i];
        INFO("empire " << x.name);
        CHECK(x.name == y.name);
        CHECK(x.empireType == y.empireType);
        CHECK(x.leaderTitle == y.leaderTitle);
        CHECK(x.leaderName == y.leaderName);
        CHECK(x.email == y.email);
        CHECK(x.kind == y.kind);
        CHECK(x.neutral == y.neutral);
        CHECK(x.alive == y.alive);
        CHECK(x.race.style == y.race.style);
        CHECK(x.race.traits == y.race.traits);
        CHECK(x.race.characteristics == y.race.characteristics);
        CHECK(x.race.culture == y.race.culture);
        CHECK(x.race.happinessModel == y.race.happinessModel);
        CHECK(x.race.nativeSurface == y.race.nativeSurface);
        CHECK(x.race.atmosphere == y.race.atmosphere);
        CHECK(x.race.demeanor == y.race.demeanor);
        CHECK(x.race.designNameFile == y.race.designNameFile);
        CHECK(x.racialPointsSpent == y.racialPointsSpent);
        CHECK(x.stockpile == y.stockpile);
        CHECK(x.researchPool == y.researchPool);
        CHECK(x.intelPool == y.intelPool);
        CHECK(x.techLevels == y.techLevels);
        REQUIRE(x.research.size() == y.research.size());
        for (size_t k = 0; k < x.research.size(); ++k) {
            CHECK(x.research[k].area == y.research[k].area);
            CHECK(x.research[k].progress == y.research[k].progress);
        }
        CHECK(x.researchEvenly == y.researchEvenly);
        CHECK(x.repeatResearch == y.repeatResearch);
        CHECK(x.uniqueAreasUnlocked == y.uniqueAreasUnlocked);
        REQUIRE(x.intel.size() == y.intel.size());
        for (size_t k = 0; k < x.intel.size(); ++k) {
            CHECK(x.intel[k].project == y.intel[k].project);
            CHECK(x.intel[k].target == y.intel[k].target);
            CHECK(x.intel[k].progress == y.intel[k].progress);
        }
        for (size_t j = 0; j < a.empires.size(); ++j) {
            if (j == i) continue;
            const Relation& p = x.relations[j];
            const Relation& q = y.relations[j];
            CHECK(p.contact == q.contact);
            if (p.contact) CHECK(p.treaty == q.treaty);
            CHECK(p.dominant == q.dominant);
            CHECK(p.tradeTurns == q.tradeTurns);
            CHECK(p.anger == q.anger);
            CHECK(p.turnsSinceWar == q.turnsSinceWar);
        }
        for (size_t k = 0; k < x.knowledge.explored.size(); ++k)
            if (x.knowledge.explored[k]) CHECK(y.knowledge.explored[k]);
        CHECK(x.knowledge.notes == y.knowledge.notes);
        CHECK(x.homeSystem == y.homeSystem);
        CHECK(x.homeSector == y.homeSector);
        std::vector<SystemId> ca = x.claimedSystems, cb = y.claimedSystems;
        std::sort(ca.begin(), ca.end());
        std::sort(cb.begin(), cb.end());
        CHECK(ca == cb);
        CHECK(x.systemsToAvoid == y.systemsToAvoid);
        CHECK(x.taggedMinefields == y.taggedMinefields);
        for (size_t w = 0; w < x.waypoints.size(); ++w) {
            CHECK(x.waypoints[w].set == y.waypoints[w].set);
            if (x.waypoints[w].set) CHECK(x.waypoints[w].location == y.waypoints[w].location);
        }
        CHECK(x.designTypes == y.designTypes);
        CHECK(x.colonyTypes == y.colonyTypes);
        CHECK(x.repairPriorities == y.repairPriorities);
        CHECK(x.designs == y.designs);
        REQUIRE(x.strategies.size() == y.strategies.size());
        for (size_t k = 0; k < x.strategies.size(); ++k) {
            const combat::Strategy p = combat::parseStrategy(x.strategies[k]);
            const combat::Strategy q = combat::parseStrategy(y.strategies[k]);
            CHECK(p.name == q.name);
            CHECK(p.primary == q.primary);
            CHECK(p.secondary == q.secondary);
            CHECK(p.targeting == q.targeting);
            CHECK(p.typePriority == q.typePriority);
            CHECK(p.dontFireOn == q.dontFireOn);
            CHECK(p.fighterLaunchGroup == q.fighterLaunchGroup);
            CHECK(p.dronesPerTarget == q.dronesPerTarget);
            CHECK(p.damagePercentShip == q.damagePercentShip);
            CHECK(p.damageUntilWeaponsGone == q.damageUntilWeaponsGone);
        }
        REQUIRE(x.log.size() == y.log.size());
        for (size_t k = 0; k < x.log.size(); ++k) {
            CHECK(x.log[k].turn == y.log[k].turn);
            CHECK(x.log[k].category == y.log[k].category);
            CHECK(x.log[k].title == y.log[k].title);
            CHECK(x.log[k].text == y.log[k].text);
            CHECK(x.log[k].location == y.log[k].location);
            CHECK(x.log[k].target == y.log[k].target);
        }
        CHECK(x.experience == y.experience);
        CHECK(x.aiState == y.aiState);
        CHECK(x.aiTurnsInState == y.aiTurnsInState);
        CHECK(x.aiMemory.targets == y.aiMemory.targets);
        CHECK(x.aiMemory.staging == y.aiMemory.staging);
        CHECK(x.aiMemory.secured == y.aiMemory.secured);
        CHECK(x.aiMemory.defend == y.aiMemory.defend);
        CHECK(x.aiMemory.afterAttack == y.aiMemory.afterAttack);
        CHECK(x.aiMinimalChanges == y.aiMinimalChanges);
        CHECK((x.aiDifficulty < 0 ? kDifficultyMedium : x.aiDifficulty) == y.aiDifficulty);   // not yet assigned: Medium
        CHECK(x.ministerStyle == y.ministerStyle);
        CHECK(x.useRaceMinisterStyle == y.useRaceMinisterStyle);
        CHECK(x.ministersForNewVehicles == y.ministersForNewVehicles);
        CHECK((x.ministerAll ? kAllMinisters : x.ministers) == y.ministers);
        CHECK(x.clearOrdersOnEncounter == y.clearOrdersOnEncounter);
        CHECK(x.avoidTaggedMinefields == y.avoidTaggedMinefields);
        CHECK(x.avoidRestrictedSystems == y.avoidRestrictedSystems);
        CHECK(x.chooseColonyType == y.chooseColonyType);
        InterfaceOptions ux = x.interfaceOptions, uy = y.interfaceOptions;
        // Not carried: the sort keys (spec 08 §11.1 Q4) and the log's place.
        ux.planetsSort = ux.coloniesSort = ux.shipsSort = ux.queuesSort = {};
        ux.logPosition = ux.logScroll = 0;
        CHECK(ux == uy);
    }

    // Designs.
    REQUIRE(a.designs.size() == b.designs.size());
    for (size_t i = 0; i < a.designs.size(); ++i) {
        const Design& x = a.designs[i];
        const Design& y = b.designs[i];
        INFO("design " << x.name);
        CHECK(x.name == y.name);
        CHECK(x.designType == y.designType);
        CHECK(x.hull == y.hull);
        CHECK(x.entries == y.entries);
        CHECK(x.strategy == y.strategy);
        CHECK(x.obsolete == y.obsolete);
        CHECK(x.createdTurn == y.createdTurn);
        CHECK(x.templateName == y.templateName);
        CHECK(x.everBuilt == y.everBuilt);
        CHECK(x.built == y.built);
        CHECK(x.lost == y.lost);
        CHECK(x.scrapped == y.scrapped);
        CHECK(x.enemyTonnageDestroyed == y.enemyTonnageDestroyed);
    }
    for (size_t i = 0; i < a.empires.size(); ++i) {
        std::vector<SeenDesign> sa = a.empires[i].knowledge.seenDesigns, sb = b.empires[i].knowledge.seenDesigns;
        CHECK(sa.size() == sb.size());
        for (size_t k = 0; k < std::min(sa.size(), sb.size()); ++k) {
            CHECK(sa[k].design == sb[k].design);
            CHECK(sa[k].turn == sb[k].turn);
        }
    }

    // Vehicles, by slot.
    const auto va = bySlot(a), vb = bySlot(b);
    REQUIRE(va.size() == vb.size());
    for (const auto& [slot, x] : va) {
        INFO("vehicle " << x->name << " at slot " << slot);
        REQUIRE(vb.contains(slot));
        const Vehicle* y = vb.at(slot);
        CHECK(x->owner == y->owner);
        CHECK(x->design == y->design);
        CHECK(x->name == y->name);
        CHECK(x->location == y->location);
        CHECK(x->count == y->count);
        CHECK(x->mixed == y->mixed);
        CHECK(x->supply == y->supply);
        CHECK(x->movement == y->movement);
        CHECK(x->experience == y->experience);
        CHECK(x->experienceTenths == y->experienceTenths);
        CHECK(x->status == y->status);
        CHECK(x->minister == y->minister);
        CHECK(x->heading == y->heading);
        CHECK(x->cargo.population == y->cargo.population);
        CHECK(x->cargo.units == y->cargo.units);
        CHECK(fleetKey(a, x->fleet) == fleetKey(b, y->fleet));
        // Only destroyed parts are carried (spec 08 §7.6).
        const Design& d = a.design(x->design);
        REQUIRE(x->damage.size() == y->damage.size());
        for (size_t p = 0; p < x->damage.size(); ++p) {
            const int structure = entryStructure(r, d, p);
            CHECK((x->damage[p] >= structure && x->damage[p] > 0) == (y->damage[p] >= structure && y->damage[p] > 0));
        }
        CHECK(x->repeatOrders == y->repeatOrders);
        REQUIRE(x->orders.size() == y->orders.size());
        for (size_t k = 0; k < x->orders.size(); ++k) checkOrder(r, a, x->orders[k], b, y->orders[k]);
        CHECK(x->queue.items.size() == y->queue.items.size());
    }

    // Fleets.
    REQUIRE(a.fleets.size() == b.fleets.size());
    for (size_t i = 0; i < a.fleets.size(); ++i) {
        const Fleet& x = a.fleets[i];
        const Fleet& y = b.fleets[i];
        INFO("fleet " << x.name);
        CHECK(x.owner == y.owner);
        CHECK(x.name == y.name);
        CHECK(x.location == y.location);
        CHECK(x.formation == y.formation);
        CHECK(x.strategy == y.strategy);
        CHECK(x.experience == y.experience);
        CHECK(x.experienceTenths == y.experienceTenths);
        CHECK(x.minister == y.minister);
        CHECK(slotOfVehicle(a, x.leader) == slotOfVehicle(b, y.leader));
        std::vector<uint32_t> ma, mb;
        for (VehicleId m : x.members) ma.push_back(slotOfVehicle(a, m));
        for (VehicleId m : y.members) mb.push_back(slotOfVehicle(b, m));
        std::sort(ma.begin(), ma.end());
        std::sort(mb.begin(), mb.end());
        CHECK(ma == mb);
    }

    // Timed events and unanswered messages.
    REQUIRE(a.pendingEvents.size() == b.pendingEvents.size());
    for (size_t i = 0; i < a.pendingEvents.size(); ++i) {
        CHECK(a.pendingEvents[i].eventType == b.pendingEvents[i].eventType);
        CHECK(a.pendingEvents[i].fireTurn == b.pendingEvents[i].fireTurn);
        CHECK(a.pendingEvents[i].empire == b.pendingEvents[i].empire);
    }
    size_t open = 0;
    for (const DiplomaticMessage& m : a.messages) open += m.delivered && !m.answered;
    size_t openB = 0;
    for (const DiplomaticMessage& m : b.messages) openB += m.delivered && !m.answered;
    CHECK(open == openB);
}

// What the original lacks, by part of the state (spec 08 §7.6): the parts
// whose hashes may differ after OpenSE4 -> original -> OpenSE4.
const std::set<std::string> kLossyParts{
    "galaxy",       // objects gone from their systems are not saved, so ids close up
    "empires",      // colours, history, the AI's per-empire memory, passwords, log pictures
    "colonies",     // founding turns, facilities grouped per kind
    "vehicles",     // ids in slot order, partial damage, arrival and came-from records, built turns
    "fleets",       // ids per owner
    "messages",     // ids, undelivered messages
    "battles",      // combat records exist only as log text
    "counters",     // next ids, arrival stamps
    "random numbers",  // reseeded from the game's seed
    "player turn",  // moves and questions of the turn in progress
    "date and options",  // OpenSE4's own options (random players, system count)
    "events",       // the far end of an event's target
    "designs",      // "retrofitted" is derived from "built" in the original
};

} // namespace

// ---- The container (§2) -------------------------------------------------------------------------------------

TEST_CASE("classic save: the key table and key stream reproduce the spec's test vector") {
    const Keys keys = testVectorKeys();
    CHECK(keys.k[5] == secondSeed(keys.k[0]));
    const KeyRows rows = keyRows(keys, 5);
    CHECK(rows.r1 == std::vector<uint8_t>{137, 9, 115, 157, 241});
    CHECK(rows.r57 == std::vector<uint8_t>{243, 133, 153, 255, 121});
    CHECK(rows.r123 == std::vector<uint8_t>{243, 69, 217, 255, 57});
    // "1.95" takes the character keys 9, 115, 157, 241; the next number key is 241 × 121 × 57.
    CHECK(keySample(keys, "1.95", 1) == std::vector<uint32_t>{9, 115, 157, 241, 1'662'177});

    // The version string as written: tag 6, length 4, the characters.
    ClassicSave s;
    s.keys = keys;
    s.traitCount = 0;
    const std::vector<uint8_t> bytes = encodeOrFail(s);
    const std::vector<uint8_t> header{2, 3, 3, 0x09, 0x03, 2, 11, 2, 22, 3, 0xE1, 0x10, 4, 0x5F, 0xBC, 0, 0};
    REQUIRE(bytes.size() > header.size() + 6);
    CHECK(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(header.size())) == header);
    CHECK(std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(header.size()), bytes.begin() + static_cast<std::ptrdiff_t>(header.size() + 6)) ==
          std::vector<uint8_t>{0x06, 0x04, 0x38, 0x5D, 0xA4, 0xC4});
    CHECK(looksLikeClassicSave(bytes));
    auto back = decodeClassicSave(bytes, 0);
    REQUIRE_MESSAGE(back.has_value(), (back ? std::string{} : back.error()));
    CHECK(*back == s);
}

TEST_CASE("classic save: K6 is not read, only the selector's table value counts") {
    ClassicSave s;
    s.keys = drawKeys(42);
    CHECK(s.keys.k[0] >= 1);
    CHECK(s.keys.k[0] <= 10);
    CHECK(s.keys.k[5] == secondSeed(s.keys.k[0]));
    s.traitCount = 0;
    ClassicSave wrong = s;
    wrong.keys.k[5] = 12345;
    auto a = decodeClassicSave(encodeOrFail(s), 0);
    auto b = decodeClassicSave(encodeOrFail(wrong), 0);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    b->keys = a->keys;
    CHECK(*a == *b);
}

TEST_CASE("classic save: value encodings") {
    // Floats keep their 80-bit pattern; experience is held in tenths.
    for (int64_t tenths : {0, 1, 3, 10, 25, 499, 500}) {
        CHECK(float80Tenths(tenthsToFloat80(tenths)) == tenths);
        CHECK(fromFloat80(toFloat80(fromFloat80(tenthsToFloat80(tenths)))) == fromFloat80(tenthsToFloat80(tenths)));
    }
    // 0.1 + 0.1 + 0.1 as doubles: 0.30000000000000004, still 3 tenths.
    const xmath::Ext tenth = (xmath::Ext(1) / xmath::Ext(10)).roundedTo(xmath::kDoubleBits);
    const xmath::Ext three = ((tenth + tenth).roundedTo(xmath::kDoubleBits) + tenth).roundedTo(xmath::kDoubleBits);
    CHECK(float80Tenths(toFloat80(three)) == 3);
    // Conditions are a double widened to 80 bits: exact both ways.
    const Conditions c = Conditions::hundredths(73);
    CHECK(xmath::toDoubleBits(fromFloat80(toFloat80(xmath::fromDoubleBits(c.bits)))) == c.bits);
    // Text: Latin-1 in the file, UTF-8 in OpenSE4; other characters become '?'.
    CHECK(latin1ToUtf8("Caf\xE9") == "Café");
    CHECK(utf8ToLatin1("Café", nullptr) == "Caf\xE9");
    size_t replaced = 0;
    CHECK(utf8ToLatin1("Ω ship", &replaced) == "? ship");
    CHECK(replaced == 1);
    // Sets: capacity and words.
    BitSet b;
    b.set(3);
    b.set(40);
    CHECK(b.capacity == 41);
    CHECK(b.words.size() == 2);
    CHECK(b.test(3));
    CHECK(b.test(40));
    CHECK_FALSE(b.test(4));
    // Ability ids (Appendix A).
    CHECK(abilityName(7) == "Sector - Sight Obscuration");
    CHECK(abilityName(150) == "AI Tag 07");
    CHECK(abilityName(168) == "Generate Points Intelligence");
    CHECK(abilityId("supply storage") == 61);
    CHECK_FALSE(abilityId("No Such Ability").has_value());
    // Design type codes (§3.7).
    CHECK(designTypeCode("Attack Ship") == 1);
    CHECK(designTypeCode("Satellite") == 25);
    CHECK(designTypeCode("Fighter") == 28);
    CHECK(designTypeCode("Drone Carrier") == 39);
    CHECK_FALSE(designTypeCode("Scout").has_value());
    for (uint16_t code = 1; code <= 39; ++code) CHECK(designTypeCode(designTypeName(code)) == code);
}

// ---- OpenSE4 -> original -> OpenSE4 ----------------------------------------------------------------------------

TEST_CASE("classic save: OpenSE4 games survive export and import, in both turn styles") {
    const Rules& r = test::engineRules();
    for (const bool simultaneous : {true, false}) {
        INFO("turn style: " << std::string(simultaneous ? "simultaneous" : "turn-based"));
        const GameState s = playedGame(simultaneous, 12);
        REQUIRE(validateState(s, &r).empty());

        // Export, encode, decode: the same values.
        const ClassicSave a = exportOrFail(r, s);
        const std::vector<uint8_t> bytes = encodeOrFail(a);
        CHECK(looksLikeClassicSave(bytes));
        auto decoded = decodeClassicSave(bytes, r.data().racialTraits.size());
        REQUIRE_MESSAGE(decoded.has_value(), (decoded ? std::string{} : decoded.error()));
        CHECK(*decoded == a);

        // Import: every field the format carries comes back.
        ConversionReport report;
        const GameState t = importOrFail(r, *decoded, &report);
        CHECK(validateState(t, &r).empty());
        checkCarried(r, s, t);
        const std::vector<std::string> parts = differingStateParts(statePartHashes(s), statePartHashes(t));
        for (const std::string& p : parts) {
            MESSAGE("part that differs after the round trip: " << p);
            CHECK_MESSAGE(kLossyParts.contains(p), "unexpected loss in " << p);
        }

        // A second round trip changes nothing: the export of the import is the export.
        const ClassicSave b = exportOrFail(r, t);
        const std::vector<std::string> diff = compareSaves(a, b);
        for (const std::string& d : diff) MESSAGE(d);
        CHECK(diff.empty());
        const GameState u = importOrFail(r, b);
        CHECK(differingStateParts(statePartHashes(t), statePartHashes(u)).empty());

        // The imported game plays on, deterministically.
        GameState p = t, q = t;
        for (int turn = 0; turn < 3; ++turn) {
            processTurn(r, p, {});
            processTurn(r, q, {});
            CHECK(validateState(p, &r).empty());
        }
        CHECK(stateChecksum(p) == stateChecksum(q));
    }
}

TEST_CASE("classic save: files on disk, and the original's own values the export computes") {
    const Rules& r = test::engineRules();
    const GameState s = playedGame(false, 6, 23);
    test::TempDir dir("classic_save");
    const std::filesystem::path file = dir.path() / "Exported.gam";
    ConversionReport report;
    auto written = writeClassicGame(r, s, file, report, {9, {}});
    REQUIRE_MESSAGE(written.has_value(), (written ? std::string{} : written.error()));
    CHECK_FALSE(report.notes.empty());

    auto bytes = readFileBytes(file);
    REQUIRE(bytes.has_value());
    auto save = decodeClassicSave(*bytes, r.data().racialTraits.size());
    REQUIRE(save.has_value());
    // The summary matches the body (§2.6).
    CHECK(save->summary.empires == static_cast<int>(save->empires.size()));
    CHECK(save->summary.date == save->prologue.date);
    CHECK(save->prologue.turnCounter == kTurnCounterBase + static_cast<int32_t>(s.turn));
    // Ids equal positions (§7.2).
    for (size_t i = 0; i < save->designs.size(); ++i) CHECK(size_t{save->designs[i].id} == i + 1);
    for (size_t i = 0; i < save->objects.size(); ++i) CHECK(size_t{save->objects[i].id} == i + 1);
    for (size_t i = 0; i < save->systems.size(); ++i) CHECK(size_t{save->systems[i].number} == i + 1);
    // The cached values, as OpenSE4 computes them.
    for (const Design& d : s.designs) {
        const DesignRecord& rec = save->designs[d.id.index()];
        const DesignStats st = computeDesignStats(r, nullptr, d);
        CHECK(int{rec.speed} == designMovement(r, d.hull, d.entries));
        CHECK(rec.cost[0] == st.cost.v[0]);
        CHECK(rec.parts.size() == d.entries.size());
        CHECK(rec.typeCode >= 1);
        CHECK(rec.typeCode <= 39);
    }
    for (const Vehicle& v : s.vehicles) {
        if (v.count <= 0) continue;
        const ObjectRecord& o = save->objects[v.slot];
        if (o.objectClass() != ObjectClass::Ship) continue;
        CHECK(int{o.maxMovement} == vehicleMaxMovement(r, s, v));
        CHECK(size_t{o.destroyedParts.capacity} == s.design(v.design).entries.size());
    }
    // The empty slots are blanks.
    for (const ObjectRecord& o : save->objects)
        if (o.objectClass() == ObjectClass::Storm && o.sectorType == 0) CHECK(o.blank());

    // And back, from the file.
    ConversionReport back;
    auto t = readClassicGame(r, file, back);
    REQUIRE_MESSAGE(t.has_value(), (t ? std::string{} : t.error()));
    checkCarried(r, s, *t);
}

// ---- Errors -----------------------------------------------------------------------------------------------------

TEST_CASE("classic save: damaged files and other data sets give clear messages") {
    const Rules& r = test::engineRules();
    const GameState s = playedGame(true, 4, 31);
    const std::vector<uint8_t> bytes = encodeOrFail(exportOrFail(r, s));
    const size_t traits = r.data().racialTraits.size();

    // Not a saved game of the original at all.
    const std::vector<uint8_t> text{'h', 'e', 'l', 'l', 'o'};
    CHECK_FALSE(looksLikeClassicSave(text));
    auto none = decodeClassicSave(text, traits);
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().find("not a Space Empires IV saved game") != std::string::npos);
    // OpenSE4's own saves are told apart by their header, and OpenSE4's loader
    // names a save of the original for what it is.
    CHECK_FALSE(looksLikeClassicSave(serializeSave(s, SaveInfo{})));
    auto asOurs = deserializeSave(bytes);
    REQUIRE_FALSE(asOurs.has_value());
    CHECK(asOurs.error().find("saved game of the original") != std::string::npos);

    // Cut short: the message names where reading stopped.
    const std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    auto truncated = decodeClassicSave(cut, traits);
    REQUIRE_FALSE(truncated.has_value());
    CHECK(truncated.error().find("ends in the middle") != std::string::npos);
    CHECK(truncated.error().find("byte") != std::string::npos);

    // Extra bytes after the last section.
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    auto tail = decodeClassicSave(longer, traits);
    REQUIRE_FALSE(tail.has_value());
    CHECK(tail.error().find("left after the last section") != std::string::npos);

    // Another version.
    ClassicSave old = exportOrFail(r, s);
    old.version = "1.50";
    auto version = decodeClassicSave(encodeOrFail(old), traits);
    REQUIRE_FALSE(version.has_value());
    CHECK(version.error().find("version 1.50") != std::string::npos);

    // A data set with another number of racial traits: the file reads only
    // with the count it was written with, and the message says so.
    test::TempDir dir("classic_save_errors");
    const std::filesystem::path file = dir.path() / "game.gam";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ruleset::Ruleset more = test::buildEngineRuleset();
    more.racialTraits.push_back(ruleset::RacialTrait{"Extra Trait", "added for the test", 0, "", 0, "None", {}, {}, {}});
    more.reindex();
    const Rules moreTraits(std::move(more));
    ConversionReport report;
    auto mismatch = readClassicGame(moreTraits, file, report);
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error().find(std::format("data set of {} racial traits", traits)) != std::string::npos);
    CHECK(mismatch.error().find(std::format("has {}", traits + 1)) != std::string::npos);

    // Another number of tech areas: refused, with the counts.
    ruleset::Ruleset techs = test::buildEngineRuleset();
    techs.techAreas.push_back(ruleset::TechArea{});
    techs.techAreas.back().name = "Extra Area";
    techs.reindex();
    const Rules moreTech(std::move(techs));
    auto tech = readClassicGame(moreTech, file, report);
    REQUIRE_FALSE(tech.has_value());
    CHECK(tech.error().find("tech areas") != std::string::npos);
    CHECK(tech.error().find("another data set") != std::string::npos);

    // A position past the end of a data file: refused, naming the record.
    ruleset::Ruleset fewer = test::buildEngineRuleset();
    fewer.vehicleSizes.resize(1);
    fewer.reindex();
    const Rules fewerHulls(std::move(fewer));
    auto hull = readClassicGame(fewerHulls, file, report);
    if (!s.designs.empty()) {
        REQUIRE_FALSE(hull.has_value());
        CHECK(hull.error().find("VehicleSize.txt") != std::string::npos);
        CHECK(hull.error().find("design") != std::string::npos);
    }
}

TEST_CASE("classic save: the export refuses what the format cannot hold") {
    const Rules& r = test::engineRules();
    GameState s = playedGame(true, 1, 41);
    for (int i = 0; i < 300; ++i) {
        StarSystem sys = s.galaxy.systems.front();
        sys.id = SystemId{s.galaxy.systems.size()};
        sys.objects.clear();
        s.galaxy.systems.push_back(sys);
    }
    ConversionReport report;
    auto save = exportClassicSave(r, s, report);
    REQUIRE_FALSE(save.has_value());
    CHECK(save.error().find("at most 255") != std::string::npos);
}

// ---- The original's own saves (opt-in) -----------------------------------------------------------------------------

namespace {

const Rules* installRules() {
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

std::vector<std::filesystem::path> gamFiles(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e.is_regular_file(ec) && ext == ".gam") out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The saves the tests read: OPENSE4_ORIGINAL_SAVES, and the installed
// game's SaveGame folder (spec 08 §8 item 3).
std::vector<std::filesystem::path> originalSaves(const Rules& r) {
    std::vector<std::filesystem::path> out;
    if (const char* env = std::getenv("OPENSE4_ORIGINAL_SAVES")) out = gamFiles(env);
    if (!r.gameRoot().empty())
        for (const auto& f : gamFiles(ruleset::childIgnoringCase(r.gameRoot(), "SaveGame")))
            if (auto bytes = readFileBytes(f); bytes && looksLikeClassicSave(*bytes)) out.push_back(f);
    return out;
}

// The differences between an original save and our export of its import
// that spec 08 explains (§7.3, §7.6): fields OpenSE4 does not hold, written
// with their defaults or derived again.
bool explained(const std::string& line) {
    static const std::vector<std::regex> kExplained = [] {
        std::vector<std::regex> out;
        for (const char* p : {
                 R"(^design \d+ > changed:)", R"(^object \d+ > changed:)", R"(^system \d+ > changed:)",   // player-file flags, off (§7.3)
                 R"(^object \d+ > day accumulator)",                                                          // zeroed (§7.3)
                 R"(^system \d+ > claimed:)",                                                                 // capacity varies (§3.5)
                 R"(^game options > allowed tech area)",                                                      // Quick Start lists the fixed areas too
                 R"(^game options > (game master password|cheat codes|computer players|game name|game code|turn code|program sum|replay counter|data checksum))",
                 R"(^prologue > (empire names \d+ > race folder|current player copy|hand-over|autosave))",
                 R"(^globals > (shown system|shown sector|current player))",                                // home of the current player (§7.3)
                 R"(^empire \d+ > (race folder|art folder|password|emblem folder|network name):)",            // §7.3, §7.4
                 R"(^empire \d+ > (maintenance percent|reproduction percent|unused|default formation|default strategy|planet strategy):)",   // unused copies (§3.6.1)
                 R"(^empire \d+ > computer player > (ship-name index|unused|drone name counter|enemy capability|incursion system))",
                 R"(^empire \d+ > computer player > (anger|turns since war)( #\d+)?: )",                    // own entry and absent players
                 R"(^empire \d+ > empire options > (turn end system|turn end sector|sort key|set queue tab|designs tab|politics tab|colonies tab|cargo transfer tab|units transfer tab|designs statistics view|designs hide obsolete|galaxy names|galaxy distances|pause|unused))",
                 R"(^empire \d+ > log entry)",                                                                 // generic entries (§7.3)
                 R"(^empire \d+ > queue template)",                                                            // not held (§3.6.6)
                 R"(^empire \d+ > intelligence > project \d+ > specific target)",                           // system targets (now any)
                 R"(^empire \d+ > fleet)",                                                                     // free slots close up
                 R"(^empire \d+ > fleets created)",
                 R"(^empire \d+ > strategy \d+ > break formation)",                                          // the seekers' flags
                 R"(^empire \d+ > race > culture:)",                                                          // 0 (none) becomes the first
                 R"(^timed event)",                                                                           // free slots are dropped
                 R"(^object \d+ > (colony > )?order \d+ > (kind: 10 != 12|kind: 11 != 13|target name:))",    // Seek lasts one phase; names shown again (§3.8.8)
                 R"(^object \d+ > colony > facility \d+ > destroyed:)",                                    // 0 between turns (§3.8.5)
                 R"(^object \d+ > (destination system|destination sector))",                                 // one-way links (§3.8.3)
                 R"(^launch)",
                 R"(^object( count)?: )",                                                                     // trailing free slots are not kept
                 R"(experience: (\d+) \(x87 [0-9a-f]+\) != \1 \()",                                          // tenths without the float noise
                 R"(cargo > (units follow: true != false|unit: 0 \(only in the first\)|unit count: 0 \(only in the first\)))",   // an empty unit list
                 R"(^object \d+ > fleet: )",                                                                 // fleet numbers close up over free slots
             })
            out.emplace_back(p);
        return out;
    }();
    return std::any_of(kExplained.begin(), kExplained.end(), [&](const std::regex& re) { return std::regex_search(line, re); });
}

// Spec 08 §8 item 4: invariants of every save examined.
void checkInvariants(const Rules& r, const ClassicSave& s) {
    CHECK(s.summary.empires == static_cast<int>(s.prologue.empireCount));
    for (size_t i = 0; i < s.empires.size(); ++i) {
        CHECK(size_t{s.empires[i].player} == i + 1);
        CHECK(s.empires[i].techLevels.size() == r.data().techAreas.size());
        CHECK(s.empires[i].traits.size() == r.data().racialTraits.size());
        for (size_t k = 0; k < s.empires[i].fleets.size(); ++k) CHECK(size_t{s.empires[i].fleets[k].number} == k + 1);
    }
    for (size_t i = 0; i < s.systems.size(); ++i) CHECK(size_t{s.systems[i].number} == i + 1);
    for (size_t i = 0; i < s.designs.size(); ++i) CHECK(size_t{s.designs[i].id} == i + 1);
    size_t oneWay = 0;
    for (size_t i = 0; i < s.objects.size(); ++i) {
        const ObjectRecord& o = s.objects[i];
        CHECK(size_t{o.id} == i + 1);
        if (o.blank()) continue;
        if (o.objectClass() == ObjectClass::Ship) {
            REQUIRE(o.design >= 1);
            REQUIRE(size_t{o.design} <= s.designs.size());
            CHECK(size_t{o.destroyedParts.capacity} == s.designs[o.design - 1u].parts.size());
            if (o.fleet) CHECK(size_t{o.fleet} <= s.empires[o.owner - 1u].fleets.size());
        }
        if (o.objectClass() == ObjectClass::WarpPoint) {
            // Warp points pair up, except the one-way links (§8).
            bool back = false;
            for (const ObjectRecord& f : s.objects)
                if (f.objectClass() == ObjectClass::WarpPoint && !f.blank() && f.system == o.destSystem && f.sector == o.destSector)
                    back = back || (f.destSystem == o.system && f.destSector == o.sector);
            oneWay += !back;
        }
    }
    CHECK(oneWay <= 2);
}

} // namespace

TEST_CASE("classic save: the original's saves import, play on and export again (opt-in)") {
    const Rules* r = installRules();
    if (!r) return;
    const std::vector<std::filesystem::path> files = originalSaves(*r);
    if (files.empty()) {
        MESSAGE("skipped: set OPENSE4_ORIGINAL_SAVES to a folder of the original's saved games");
        return;
    }
    const int turns = std::getenv("OPENSE4_ORIGINAL_SAVES_TURNS") ? std::atoi(std::getenv("OPENSE4_ORIGINAL_SAVES_TURNS")) : 2;
    for (const std::filesystem::path& file : files) {
        INFO(file.filename().string());
        auto bytes = readFileBytes(file);
        REQUIRE(bytes.has_value());
        // Decodes to the end (§8 item 3).
        auto save = decodeClassicSave(*bytes, r->data().racialTraits.size());
        REQUIRE_MESSAGE(save.has_value(), (save ? std::string{} : save.error()));
        checkInvariants(*r, *save);
        // The counts of spec 08 §8 for the two saves it describes.
        std::map<int, size_t> classes;
        size_t colonies = 0;
        for (const ObjectRecord& o : save->objects) {
            ++classes[o.cls];
            colonies += o.colony.has_value();
        }
        if (bytes->size() == 72'921) {
            CHECK(save->empires.size() == 5);
            CHECK(save->systems.size() == 56);
            CHECK(save->designs.size() == 56);
            CHECK(classes[4] == 550);
            CHECK(colonies == 5);
        } else if (bytes->size() == 646'255) {
            CHECK(save->empires.size() == 20);
            CHECK(save->systems.size() == 81);
            CHECK(save->designs.size() == 1421);
            CHECK(classes[5] == 940);
            CHECK(classes[10] == 271);
            CHECK(colonies == 404);
        }
        // Re-encoding with other keys gives the same values (§8 item 2).
        ClassicSave again = *save;
        again.keys = drawKeys(99);
        auto reread = decodeClassicSave(encodeOrFail(again), r->data().racialTraits.size());
        REQUIRE(reread.has_value());
        reread->keys = save->keys;
        CHECK(*reread == *save);

        // Imports; twice gives the same game.
        ConversionReport report;
        auto game = importClassicSave(*r, *save, report);
        REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
        CHECK(validateState(*game, r).empty());
        ConversionReport report2;
        auto twin = importClassicSave(*r, *save, report2);
        REQUIRE(twin.has_value());
        CHECK(stateChecksum(*game) == stateChecksum(*twin));

        // Exported again: equal but for what the spec explains.
        ConversionReport out;
        auto exported = exportClassicSave(*r, *game, out, {5, save->options.gameName});
        REQUIRE_MESSAGE(exported.has_value(), (exported ? std::string{} : exported.error()));
        // Free design slots (owner 0) keep old values nothing reads (§3.7).
        std::set<std::string> freeDesigns;
        for (const DesignRecord& d : save->designs)
            if (d.owner == 0) freeDesigns.insert(std::format("design {} > ", d.id));
        // Free object slots are written as blank storms (§3.8).
        for (const ObjectRecord& o : save->objects)
            if (o.blank()) freeDesigns.insert(std::format("object {} > ", o.id));
        size_t unexplained = 0;
        for (const std::string& d : compareSaves(*save, *exported, 100000)) {
            if (explained(d)) continue;
            if (const size_t gt = d.find(" > "); gt != std::string::npos && freeDesigns.contains(d.substr(0, gt + 3))) continue;
            if (unexplained++ < 40) MESSAGE("unexplained difference: " << d);
        }
        CHECK(unexplained == 0);
        // Import of the export is the import (§8 item 5).
        auto reimported = importClassicSave(*r, *exported, out);
        REQUIRE(reimported.has_value());
        for (const std::string& part : differingStateParts(statePartHashes(*game), statePartHashes(*reimported)))
            CHECK_MESSAGE((part == "empires" || part == "vehicles"), "differs after export and import: " << part);

        // Plays on without errors or desync.
        for (int t = 0; t < turns && !game->gameOver; ++t) {
            processTurn(*r, *game, {});
            processTurn(*r, *twin, {});
            const std::string problem = validateState(*game, r);
            CHECK_MESSAGE(problem.empty(), problem);
            CHECK(stateChecksum(*game) == stateChecksum(*twin));
        }
    }
}

// A long game of the original, played on (opt-in): OPENSE4_ORIGINAL_SAVE_PLAY
// names one saved game, which is imported, played for
// OPENSE4_ORIGINAL_SAVES_TURNS turns (5 by default), saved in OpenSE4's format
// and loaded again, and exported back to the original's format.
TEST_CASE("classic save: a game of the original is played on, saved, loaded and exported (opt-in)") {
    const Rules* r = installRules();
    const char* file = std::getenv("OPENSE4_ORIGINAL_SAVE_PLAY");
    if (!r || !file) return;
    const int turns = std::getenv("OPENSE4_ORIGINAL_SAVES_TURNS") ? std::atoi(std::getenv("OPENSE4_ORIGINAL_SAVES_TURNS")) : 5;
    ConversionReport report;
    auto game = readClassicGame(*r, file, report);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    for (const std::string& n : report.notes) MESSAGE("import note: " << n);
    const uint32_t start = game->turn;
    for (int t = 0; t < turns && !game->gameOver; ++t) {
        processTurn(*r, *game, {});
        const std::string problem = validateState(*game, r);
        CHECK_MESSAGE(problem.empty(), problem);
    }
    CHECK(game->turn == start + static_cast<uint32_t>(turns));
    size_t alive = 0, vehicles = 0, colonies = 0;
    for (const Empire& e : game->empires) alive += e.alive;
    for (const Vehicle& v : game->vehicles) vehicles += v.count > 0;
    for (const auto& c : game->colonies) colonies += c.has_value();
    MESSAGE(std::format("after {} turns: date {}, {} empires alive, {} vehicles, {} colonies", turns, describeDate(game->turn), alive, vehicles, colonies));

    // OpenSE4's own format, and back: the same game, which plays on the same.
    // No game file keeps the mood events waiting for the next update (spec
    // 02 §4): a loaded game starts without them, so the game here drops them too.
    game->pendingMood.clear();
    SaveInfo info;
    info.gameName = "imported";
    auto loaded = deserializeSave(serializeSave(*game, info));
    REQUIRE_MESSAGE(loaded.has_value(), (loaded ? std::string{} : loaded.error()));
    CHECK(stateChecksum(loaded->first) == stateChecksum(*game));
    GameState again = loaded->first;
    processTurn(*r, again, {});
    processTurn(*r, *game, {});
    CHECK(stateChecksum(again) == stateChecksum(*game));

    // Back to the original's format: it decodes, and imports again.
    ConversionReport out;
    auto exported = exportClassicSave(*r, *game, out, {17, "imported"});
    REQUIRE_MESSAGE(exported.has_value(), (exported ? std::string{} : exported.error()));
    for (const std::string& n : out.notes) MESSAGE("export note: " << n);
    auto bytes = encodeClassicSave(*exported);
    REQUIRE(bytes.has_value());
    MESSAGE("exported file: " << bytes->size() << " bytes");
    auto decoded = decodeClassicSave(*bytes, r->data().racialTraits.size());
    REQUIRE_MESSAGE(decoded.has_value(), (decoded ? std::string{} : decoded.error()));
    checkInvariants(*r, *decoded);
    ConversionReport back;
    auto reimported = importClassicSave(*r, *decoded, back);
    REQUIRE_MESSAGE(reimported.has_value(), (reimported ? std::string{} : reimported.error()));
    CHECK(validateState(*reimported, r).empty());
    if (const char* keep = std::getenv("OPENSE4_ORIGINAL_SAVE_EXPORT")) {
        auto written = writeFileAtomic(keep, *bytes);
        CHECK(written.has_value());
    }
}
