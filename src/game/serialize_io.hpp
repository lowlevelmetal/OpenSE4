#pragma once

// The binary archive behind the save format, order files and the network
// protocol (serialize.hpp is the public API). Every serialized type has one
//
//     template <class Ar> void io(Ar& ar, T& value)
//
// that lists its fields once; the same function writes, reads and hashes.
// Adding a field to a struct in state.hpp (or commands.hpp, setup.hpp) means
// adding it to that struct's io() below, and bumping kSaveVersion when the
// change makes older files unreadable. Struct io() functions list their
// members through fields(): tests/test_serialize.cpp compares that list with
// the struct's declared members and fails when a field was added to a struct
// but not here.
//
// Encoding: little-endian fixed-width integers (int is 32 bits), enums as
// their underlying type, bool as one byte (0 or 1), strings and lists as a
// u32 count followed by the elements, optionals as a presence byte, variants
// as a u8 alternative index followed by the alternative. Reading bounds-checks
// every count against the remaining input, stops at the first error and never
// throws; the result of a failed read must be discarded.

#include "core/hash.hpp"
#include "core/id.hpp"
#include "core/rng.hpp"
#include "game/commands.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/state.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace opense4::game::serial {

static_assert(sizeof(int) == 4, "the save format stores int as 32 bits");

inline constexpr size_t kMaxStringBytes = size_t{16} << 20;

// Appends the encoding to a byte vector.
class ByteWriter {
public:
    static constexpr bool kReading = false;
    explicit ByteWriter(std::vector<uint8_t>& out, uint32_t version = kSaveVersion) : out_(out), version_(version) {}

    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        out_.insert(out_.end(), p, p + size);
    }
    uint32_t version() const { return version_; }
    bool ok() const { return true; }
    void fail(std::string_view) {}

private:
    std::vector<uint8_t>& out_;
    uint32_t version_;
};

// Feeds the encoding to an FNV-1a hash without building it in memory.
class HashWriter {
public:
    static constexpr bool kReading = false;
    explicit HashWriter(uint32_t version = kSaveVersion) : version_(version) {}

    void bytes(const void* data, size_t size) { hash_.bytes(data, size); }
    uint32_t version() const { return version_; }
    bool ok() const { return true; }
    void fail(std::string_view) {}
    uint64_t value() const { return hash_.value(); }

private:
    Hasher hash_;
    uint32_t version_;
};

// Decodes from a byte span. The first problem is kept in error(); after it,
// every read yields zeros and lists come back empty.
class Reader {
public:
    static constexpr bool kReading = true;
    explicit Reader(std::span<const uint8_t> in, uint32_t version = kSaveVersion) : in_(in), version_(version) {}

    bool bytes(void* out, size_t size) {
        if (!error_.empty() || size > in_.size() - pos_) {
            if (error_.empty()) fail("unexpected end of data");
            std::fill_n(static_cast<uint8_t*>(out), size, uint8_t{0});
            return false;
        }
        std::copy_n(in_.data() + pos_, size, static_cast<uint8_t*>(out));
        pos_ += size;
        return true;
    }
    size_t remaining() const { return error_.empty() ? in_.size() - pos_ : 0; }
    size_t position() const { return pos_; }
    uint32_t version() const { return version_; }
    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    void fail(std::string_view message) {
        if (error_.empty()) error_ = message.empty() ? "invalid data" : std::string(message);
    }

private:
    std::span<const uint8_t> in_;
    size_t pos_ = 0;
    uint32_t version_;
    std::string error_;
};

template <class T>
concept Scalar = std::is_integral_v<T> || std::is_enum_v<T>;

// ---- Generic building blocks -------------------------------------------------------------

template <class Ar, Scalar T>
void io(Ar& ar, T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        uint8_t b = v ? 1 : 0;
        io(ar, b);
        if constexpr (Ar::kReading) {
            if (b > 1) ar.fail("invalid boolean");
            v = b == 1;
        }
    } else if constexpr (std::is_enum_v<T>) {
        using U = std::underlying_type_t<T>;
        U u = static_cast<U>(v);
        io(ar, u);
        if constexpr (Ar::kReading) {
            // Enums with a Count enumerator are range-checked.
            if constexpr (requires { T::Count; })
                if (u >= static_cast<U>(T::Count)) ar.fail("enum value out of range");
            v = static_cast<T>(u);
        }
    } else {
        using U = std::make_unsigned_t<T>;
        std::array<uint8_t, sizeof(T)> buf{};
        if constexpr (Ar::kReading) {
            ar.bytes(buf.data(), buf.size());
            U u = 0;
            for (size_t i = 0; i < sizeof(T); ++i) u = static_cast<U>(u | static_cast<U>(static_cast<U>(buf[i]) << (8 * i)));
            v = static_cast<T>(u);
        } else {
            const U u = static_cast<U>(v);
            for (size_t i = 0; i < sizeof(T); ++i) buf[i] = static_cast<uint8_t>(u >> (8 * i));
            ar.bytes(buf.data(), buf.size());
        }
    }
}

// Writes/reads a u32 element count; a count larger than the remaining input
// (every element takes at least one byte) is rejected before allocating.
template <class Ar>
bool ioCount(Ar& ar, size_t size, uint32_t& n) {
    n = static_cast<uint32_t>(size);
    io(ar, n);
    if constexpr (Ar::kReading) {
        if (n > ar.remaining()) {
            ar.fail("length out of range");
            return false;
        }
    }
    return ar.ok();
}

template <class Ar>
void io(Ar& ar, std::string& s) {
    uint32_t n = 0;
    if (!ioCount(ar, s.size(), n)) {
        s.clear();
        return;
    }
    if constexpr (Ar::kReading) {
        if (n > kMaxStringBytes) {
            ar.fail("string too long");
            s.clear();
            return;
        }
        s.resize(n);
    }
    ar.bytes(s.data(), n);
}

template <class Ar, class T, class A>
void io(Ar& ar, std::vector<T, A>& v) {
    uint32_t n = 0;
    if (!ioCount(ar, v.size(), n)) {
        v.clear();
        return;
    }
    if constexpr (Ar::kReading) {
        v.clear();
        if constexpr (std::is_same_v<T, uint8_t>) {
            v.resize(n);
            ar.bytes(v.data(), n);
        } else {
            v.reserve(std::min<size_t>(n, 4096));  // grows with the data actually present
            for (uint32_t i = 0; i < n && ar.ok(); ++i) io(ar, v.emplace_back());
        }
    } else {
        if constexpr (std::is_same_v<T, uint8_t>) ar.bytes(v.data(), v.size());
        else
            for (T& x : v) io(ar, x);
    }
}

template <class Ar, class T, size_t N>
void io(Ar& ar, std::array<T, N>& a) {
    for (T& x : a) io(ar, x);
}

template <class Ar, class T>
void io(Ar& ar, std::optional<T>& o) {
    bool has = o.has_value();
    io(ar, has);
    if constexpr (Ar::kReading) {
        if (has && ar.ok()) io(ar, o.emplace());
        else o.reset();
    } else if (has) {
        io(ar, *o);
    }
}

template <class Ar, class A, class B>
void io(Ar& ar, std::pair<A, B>& p) {
    io(ar, p.first);
    io(ar, p.second);
}

template <class Ar, class Tag>
void io(Ar& ar, Id<Tag>& id) {
    io(ar, id.value);
}

template <class Ar>
void io(Ar& ar, Rng& rng) {
    uint64_t st[4];
    for (int i = 0; i < 4; ++i) st[i] = rng.rawState()[i];
    for (uint64_t& x : st) io(ar, x);
    if constexpr (Ar::kReading) rng.setRawState(st);
}

template <class Ar, class V, size_t... I>
void readAlternative(Ar& ar, V& v, size_t index, std::index_sequence<I...>) {
    using Fn = void (*)(Ar&, V&);
    static constexpr Fn table[] = {[](Ar& a, V& x) { io(a, x.template emplace<I>()); }...};
    table[index](ar, v);
}

template <class Ar, class... Ts>
void io(Ar& ar, std::variant<Ts...>& v) {
    static_assert(sizeof...(Ts) < 256, "variant index is stored in one byte");
    uint8_t index = static_cast<uint8_t>(v.index());
    io(ar, index);
    if constexpr (Ar::kReading) {
        if (index >= sizeof...(Ts)) {
            ar.fail("unknown variant alternative");
            return;
        }
        readAlternative(ar, v, index, std::index_sequence_for<Ts...>{});
    } else {
        std::visit([&](auto& x) { io(ar, x); }, v);
    }
}

template <class Ar, class... T>
void fields(Ar& ar, T&... v) {
    (io(ar, v), ...);
}

// ---- Rules data held in the state ----------------------------------------------------------

template <class Ar> void io(Ar& ar, ruleset::Ability& a) { fields(ar, a.type, a.description, a.value1, a.value2); }
template <class Ar> void io(Ar& ar, ruleset::CombatStrategy& c) { fields(ar, c.name, c.settings); }

// ---- types.hpp, galaxy.hpp ------------------------------------------------------------------

template <class Ar> void io(Ar& ar, Resources& r) { fields(ar, r.v); }
template <class Ar> void io(Ar& ar, Sector& s) { fields(ar, s.x, s.y); }
template <class Ar> void io(Ar& ar, Location& l) { fields(ar, l.system, l.sector); }
template <class Ar> void io(Ar& ar, GalaxyPos& p) { fields(ar, p.x, p.y); }
template <class Ar> void io(Ar& ar, Conditions& c) { fields(ar, c.bits); }

template <class Ar>
void io(Ar& ar, SpaceObject& o) {
    fields(ar, o.id, o.slot, o.kind, o.system, o.sector, o.sectorType, o.name, o.abilities, o.size, o.surface, o.atmosphere, o.conditions,
           o.value, o.starAge, o.starColor, o.starLuminosity, o.destination);
}

template <class Ar>
void io(Ar& ar, StarSystem& s) {
    fields(ar, s.id, s.name, s.position, s.type, s.physicalType, s.abilities, s.objects);
}

template <class Ar> void io(Ar& ar, Galaxy& g) { fields(ar, g.width, g.height, g.quadrantType, g.systems, g.objects); }
template <class Ar> void io(Ar& ar, StartingPoint& p) { fields(ar, p.system, p.sector, p.player); }

// ---- Empires -------------------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, Race& r) {
    fields(ar, r.name, r.style, r.biology, r.society, r.history, r.characteristics, r.traits, r.culture, r.happinessModel,
           r.nativeSurface, r.atmosphere, r.demeanor, r.designNameFile);
}

template <class Ar> void io(Ar& ar, Waypoint& w) { fields(ar, w.name, w.location, w.set); }
template <class Ar> void io(Ar& ar, ResearchProject& p) { fields(ar, p.area, p.progress); }

template <class Ar>
void io(Ar& ar, IntelProjectOrder& p) {
    fields(ar, p.project, p.target, p.targetPlanet, p.targetVehicle, p.thirdEmpire, p.targetTech, p.progress);
}

template <class Ar>
void io(Ar& ar, Relation& r) {
    fields(ar, r.contact, r.treaty, r.dominant, r.tradeTurns, r.treatyTurn, r.lastWarTurn, r.anger, r.messageSentThisTurn);
    fields(ar, r.turnsSinceWar, r.treatyAge, r.agedTreaty, r.promises, r.queuedWar, r.queuedBreak, r.queuedPeace, r.attackedUs,
           r.spiedOnUs, r.attackedIn, r.combatsThisTurn, r.combatsLastTurn);
}

template <class Ar>
void io(Ar& ar, AiMemory& m) {
    fields(ar, m.targets, m.staging, m.secured, m.defend, m.afterAttack, m.avoid, m.attackSystems, m.metMinefield, m.designsFought);
}

template <class Ar> void io(Ar& ar, PoliticsMark& m) { fields(ar, m.set, m.turn, m.battles, m.logs, m.nextMessage); }

template <class Ar> void io(Ar& ar, LogEntry& l) { fields(ar, l.turn, l.category, l.title, l.text, l.location, l.picture, l.target, l.message); }

template <class Ar> void io(Ar& ar, HistoryEntry& h) { fields(ar, h.turn, h.empire, h.text, h.location); }

template <class Ar> void io(Ar& ar, SeenDesign& d) { fields(ar, d.design, d.turn); }

template <class Ar>
void io(Ar& ar, TurnStats& t) {
    fields(ar, t.turn, t.score, t.production, t.research, t.intelligence, t.techLevels, t.systems, t.planets, t.population, t.units,
           t.ships, t.bases);
}

template <class Ar>
void io(Ar& ar, EconomyReport& e) {
    fields(ar, e.colonies, e.trade, e.tariffsIn, e.remoteMining, e.otherIncome, e.tariffsOut, e.maintenance, e.construction,
           e.lostToStorage, e.undelivered, e.storageCap, e.research, e.intelligence);
}

template <class Ar>
void io(Ar& ar, Knowledge& k) {
    fields(ar, k.explored, k.present, k.knownWarpLink, k.lastSeen, k.visibleVehicles, k.seenDesigns, k.notes);
}

template <class Ar>
void io(Ar& ar, InterfaceOptions& o) {
    fields(ar, o.showLogAtTurnStart, o.confirmEndTurn, o.confirmScrap, o.confirmStellarManipulation, o.confirmDeleteResearch,
           o.confirmDeleteIntel, o.confirmDeleteFirstQueueItem, o.noteSimilarAbilities);
    fields(ar, o.skipUnderConstruction, o.skipDamaged, o.stopOncePerLocation, o.skipInFleets);
    fields(ar, o.warpPointNames, o.planetNames, o.colonizableMarkers, o.systemGrid, o.coordinateLocation, o.facilityMarkers);
    fields(ar, o.galaxyGridLines, o.galaxyWarpLines, o.latestConstructionOnly, o.latestComponentsOnly, o.autoClaimColonized);
    fields(ar, o.logFilter, o.logPosition, o.logScroll, o.planetsTab, o.planetsNoSysToAvoid, o.queuesTab, o.queuesShown,
           o.simulatorNoObsolete);
    fields(ar, o.planetsSort, o.coloniesSort, o.shipsSort, o.queuesSort);
    fields(ar, o.replayAnimate, o.replayFast, o.replayViewRect, o.replayGrid);
}

template <class Ar>
void io(Ar& ar, Empire& e) {
    fields(ar, e.id, e.name, e.empireType, e.leaderTitle, e.leaderName, e.race, e.color, e.kind, e.alive, e.passwordHash,
           e.racialPointsSpent);
    fields(ar, e.stockpile, e.economy);
    fields(ar, e.techLevels, e.research, e.researchEvenly, e.repeatResearch, e.uniqueAreasUnlocked, e.researchPool);
    fields(ar, e.intel, e.intelEvenly, e.repeatIntel, e.intelPool);
    fields(ar, e.relations, e.knowledge);
    fields(ar, e.homeSystem, e.homeSector, e.claimedSystems, e.systemsToAvoid, e.taggedMinefields, e.waypoints, e.designTypes, e.colonyTypes, e.strategies,
           e.repairPriorities, e.designs);
    fields(ar, e.log, e.historyEvents, e.history, e.experience);
    fields(ar, e.aiState, e.aiTurnsInState, e.aiMinimalChanges, e.aiMemory, e.aiDifficulty, e.ministerAll, e.ministers, e.ministerStyle,
           e.useRaceMinisterStyle, e.ministersForNewVehicles, e.clearOrdersOnEncounter, e.avoidTaggedMinefields, e.avoidRestrictedSystems);
    fields(ar, e.chooseColonyType, e.colonyTypeChoices, e.interfaceOptions);
    fields(ar, e.politicsMark);
}

// ---- Cargo, queues, colonies ------------------------------------------------------------------

template <class Ar> void io(Ar& ar, PopulationGroup& p) { fields(ar, p.race, p.millions); }
template <class Ar> void io(Ar& ar, UnitStack& u) { fields(ar, u.design, u.count); }
template <class Ar> void io(Ar& ar, Cargo& c) { fields(ar, c.population, c.units); }
template <class Ar> void io(Ar& ar, QueueItem& q) { fields(ar, q.kind, q.design, q.facility, q.count, q.spent); }

template <class Ar>
void io(Ar& ar, ConstructionQueue& q) {
    fields(ar, q.items, q.onHold, q.repeat, q.emergency, q.emergencyTurns, q.slowTurns, q.autoWaypoint);
}

template <class Ar>
void io(Ar& ar, Colony& c) {
    fields(ar, c.planet, c.owner, c.colonyType, c.population, c.anger, c.facilities, c.cargo, c.queue, c.plagueLevel, c.atmosphereTurns,
           c.minister, c.homeworld, c.foundedTurn, c.militia, c.invader, c.landedTroops, c.orders, c.repeatOrders);
    fields(ar, c.cloaked, c.cloakLevels, c.sensorLevels);
}

// ---- Designs and vehicles -----------------------------------------------------------------------

template <class Ar> void io(Ar& ar, DesignEntry& d) { fields(ar, d.component, d.mount); }

template <class Ar>
void io(Ar& ar, Design& d) {
    fields(ar, d.id, d.owner, d.name, d.designType, d.hull, d.entries, d.strategy, d.obsolete, d.createdTurn, d.retrofitted, d.built, d.lost,
           d.enemyTonnageDestroyed);
    fields(ar, d.templateName, d.everBuilt);
}

template <class Ar> void io(Ar& ar, Order& o) { fields(ar, o.kind, o.location, o.object, o.vehicle, o.design, o.amount, o.from, o.to); }

template <class Ar>
void io(Ar& ar, Vehicle& v) {
    fields(ar, v.id, v.slot, v.owner, v.design, v.name, v.location, v.count, v.mixed, v.damage, v.supply, v.movement, v.orders, v.repeatOrders, v.fleet,
           v.cargo, v.experience, v.experienceTenths, v.status, v.minister, v.queue, v.targetVehicle, v.targetObject, v.builtTurn,
           v.immobileUntil, v.cameFrom, v.cameFromTurn, v.heading);
}

template <class Ar>
void io(Ar& ar, Fleet& f) {
    fields(ar, f.id, f.owner, f.name, f.members, f.leader, f.location, f.formation, f.strategy, f.experience, f.experienceTenths,
           f.minister);
}

// ---- Diplomacy ------------------------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, PackageItem& p) {
    fields(ar, p.kind, p.resources, p.tech, p.planet, p.vehicle, p.system, p.treaty, p.empire);
}

template <class Ar>
void io(Ar& ar, DiplomaticMessage& m) {
    fields(ar, m.id, m.from, m.to, m.sentTurn, m.type, m.tone, m.text, m.treaty, m.offer, m.request, m.thirdEmpire, m.system, m.planet,
           m.inReplyTo, m.delivered, m.answered, m.dated);
}

// ---- Combat records, events, options --------------------------------------------------------------

template <class Ar>
void io(Ar& ar, CombatEvent& e) {
    fields(ar, e.kind, e.round, e.piece, e.target, e.x, e.y, e.amount, e.component);
}

template <class Ar>
void io(Ar& ar, CombatPiece& p) {
    fields(ar, p.kind, p.owner, p.vehicle, p.planet, p.design, p.name, p.startX, p.startY, p.count, p.damage, p.survivor);
}

template <class Ar>
void io(Ar& ar, GroundRound& g) {
    fields(ar, g.attackers, g.defenders, g.militia);
}

template <class Ar>
void io(Ar& ar, GroundCombat& g) {
    fields(ar, g.round, g.planetPiece, g.troopShip, g.event, g.planet, g.attacker, g.defender, g.population, g.facilities, g.attackers,
           g.defenders, g.attackersLeft, g.defendersLeft, g.militia, g.militiaLeft, g.rounds, g.captured, g.perRound);
}

template <class Ar>
void io(Ar& ar, CombatRecord& c) {
    fields(ar, c.turn, c.location, c.participants, c.pieces, c.events, c.summary, c.grounds);
    fields(ar, c.currentPlayer);
}

template <class Ar>
void io(Ar& ar, MoodEvent& m) {
    fields(ar, m.empire, m.trigger, m.system, m.planet, m.count);
}

template <class Ar>
void io(Ar& ar, PendingEvent& e) {
    fields(ar, e.eventType, e.empire, e.object, e.vehicle, e.system, e.fireTurn);
}

template <class Ar>
void io(Ar& ar, VictoryConditions& v) {
    fields(ar, v.score, v.scoreValue, v.years, v.yearsValue, v.percentOfSecond, v.percentOfSecondValue, v.techPercent, v.techPercentValue,
           v.peace, v.peaceYears, v.delay, v.delayYears);
}

template <class Ar>
void io(Ar& ar, GameOptions& o) {
    fields(ar, o.quadrantType, o.systemCount, o.allWarpPointsConnected, o.noWarpPoints, o.warpPointsAnywhere, o.allSystemsSeen,
           o.omnipresent, o.finiteResources);
    fields(ar, o.eventFrequency, o.maxEventSeverity);
    fields(ar, o.techCost, o.startTechLevel, o.techAreasAllowed);
    fields(ar, o.startingResources, o.racialPoints, o.homePlanetValue, o.startingPlanets, o.sameSystemAllowed, o.evenlyDistributed);
    fields(ar, o.noTacticalCombat, o.allowGifts, o.allowTechTrades, o.allowIntel, o.noRuins, o.onlyBreathable, o.onlyHomeType,
           o.teamMode, o.scoreDisplay, o.maxShipsPerPlayer, o.maxUnitsPerPlayer, o.aiDifficulty, o.aiBonus, o.victory);
    fields(ar, o.simultaneous);
    fields(ar, o.randomAiPlayers);
    fields(ar, o.quadrantSize, o.allPlanetsSameSize);
    fields(ar, o.playersCanSaveMap, o.autosaveTurns);
    fields(ar, o.allowSurrender);
}

// ---- Turn-based games ---------------------------------------------------------------------------------

template <class Ar> void io(Ar& ar, TurnMoves& m) { fields(ar, m.vehicle, m.steps, m.bonus); }
template <class Ar> void io(Ar& ar, TurnLaunches& l) { fields(ar, l.vehicle, l.planet, l.kind, l.count); }
template <class Ar> void io(Ar& ar, EntryQuestion& q) { fields(ar, q.vehicle, q.fleet, q.where); }
template <class Ar> void io(Ar& ar, PlayerTurn& t) { fields(ar, t.empire, t.started, t.moves, t.launched, t.questions); }

// ---- The game -----------------------------------------------------------------------------------------

template <class Ar> void io(Ar& ar, LeftFacilities& l) { fields(ar, l.planet, l.facilities); }

template <class Ar>
void io(Ar& ar, GameState& s) {
    fields(ar, s.turn, s.seed, s.options, s.galaxy, s.colonies, s.empires, s.designs, s.vehicles, s.fleets, s.messages, s.pendingEvents,
           s.pendingMood, s.combats, s.nextVehicleId, s.nextFleetId, s.nextMessageId, s.peacefulTurns, s.gameOver, s.winner, s.rng,
           s.playerTurn, s.startingPoints, s.leftFacilities);
}

// ---- Commands (commands.hpp) -------------------------------------------------------------------------

template <class Ar> void io(Ar& ar, cmd::SetOrders& c) { fields(ar, c.vehicle, c.fleet, c.orders, c.repeat, c.planet); }
template <class Ar> void io(Ar& ar, cmd::CreateFleet& c) { fields(ar, c.name, c.members); }
template <class Ar> void io(Ar& ar, cmd::JoinFleet& c) { fields(ar, c.fleet, c.vehicle); }
template <class Ar> void io(Ar& ar, cmd::LeaveFleet& c) { fields(ar, c.vehicle); }
template <class Ar> void io(Ar& ar, cmd::DisbandFleet& c) { fields(ar, c.fleet); }
template <class Ar> void io(Ar& ar, cmd::SetFleetOptions& c) { fields(ar, c.fleet, c.formation, c.strategy); }
template <class Ar> void io(Ar& ar, cmd::SetVehicleStrategy& c) { fields(ar, c.design, c.strategy); }
template <class Ar> void io(Ar& ar, cmd::Rename& c) { fields(ar, c.vehicle, c.fleet, c.design, c.planet, c.name); }
template <class Ar> void io(Ar& ar, cmd::Scrap& c) { fields(ar, c.vehicle, c.facilityPlanet, c.facilitySlot); }
template <class Ar> void io(Ar& ar, cmd::Mothball& c) { fields(ar, c.vehicle, c.mothball); }
template <class Ar> void io(Ar& ar, cmd::SetMinister& c) { fields(ar, c.vehicle, c.planet, c.empireWide, c.on); }
template <class Ar> void io(Ar& ar, cmd::QueueTarget& c) { fields(ar, c.planet, c.vehicle); }
template <class Ar> void io(Ar& ar, cmd::QueueAdd& c) { fields(ar, c.target, c.item, c.position); }
template <class Ar> void io(Ar& ar, cmd::QueueRemove& c) { fields(ar, c.target, c.index); }
template <class Ar> void io(Ar& ar, cmd::QueueMove& c) { fields(ar, c.target, c.from, c.to); }
template <class Ar> void io(Ar& ar, cmd::QueueSetCount& c) { fields(ar, c.target, c.index, c.count); }
template <class Ar> void io(Ar& ar, cmd::QueueFlags& c) { fields(ar, c.target, c.onHold, c.repeat, c.emergency, c.autoWaypoint); }
template <class Ar> void io(Ar& ar, cmd::QueueReplaceFacility& c) { fields(ar, c.target, c.index, c.facility); }
template <class Ar> void io(Ar& ar, cmd::Retrofit& c) { fields(ar, c.vehicle, c.design); }
template <class Ar> void io(Ar& ar, cmd::SetColonyType& c) { fields(ar, c.planet, c.colonyType); }
template <class Ar> void io(Ar& ar, cmd::AbandonPlanet& c) { fields(ar, c.planet); }

template <class Ar>
void io(Ar& ar, cmd::TransferCargo& c) {
    fields(ar, c.fromVehicle, c.fromPlanet, c.toVehicle, c.toPlanet, c.unitDesign, c.populationRace, c.amount);
}

template <class Ar> void io(Ar& ar, cmd::CreateDesign& c) { fields(ar, c.design); }
template <class Ar> void io(Ar& ar, cmd::EditDesign& c) { fields(ar, c.design, c.with); }
template <class Ar> void io(Ar& ar, cmd::SetDesignObsolete& c) { fields(ar, c.design, c.obsolete); }
template <class Ar> void io(Ar& ar, cmd::DeleteDesign& c) { fields(ar, c.design); }
template <class Ar> void io(Ar& ar, cmd::SetResearch& c) { fields(ar, c.queue, c.evenly, c.repeat); }
template <class Ar> void io(Ar& ar, cmd::SetIntel& c) { fields(ar, c.queue, c.evenly, c.repeat); }
template <class Ar> void io(Ar& ar, cmd::SendMessage& c) { fields(ar, c.message, c.minister); }
template <class Ar> void io(Ar& ar, cmd::AnswerMessage& c) { fields(ar, c.message, c.accept, c.text); }
template <class Ar> void io(Ar& ar, cmd::DecideWar& c) { fields(ar, c.target); }
template <class Ar> void io(Ar& ar, cmd::SetInterfaceOptions& c) { fields(ar, c.options); }
template <class Ar> void io(Ar& ar, cmd::CarryOutDemand& c) { fields(ar, c.demand); }
template <class Ar> void io(Ar& ar, cmd::UseDemandEntry& c) { fields(ar, c.list, c.about); }
template <class Ar> void io(Ar& ar, cmd::SetWaypoint& c) { fields(ar, c.slot, c.waypoint); }
template <class Ar> void io(Ar& ar, cmd::SetSystemFlags& c) { fields(ar, c.system, c.avoid, c.claim); }
template <class Ar> void io(Ar& ar, cmd::SetSystemNote& c) { fields(ar, c.system, c.note); }
template <class Ar> void io(Ar& ar, cmd::TagMinefield& c) { fields(ar, c.location, c.tagged); }
template <class Ar> void io(Ar& ar, cmd::SetStrategy& c) { fields(ar, c.index, c.strategy, c.remove); }
template <class Ar> void io(Ar& ar, cmd::SetRepairPriorities& c) { fields(ar, c.priorities); }
template <class Ar> void io(Ar& ar, cmd::SetDesignTypes& c) { fields(ar, c.designTypes); }
template <class Ar> void io(Ar& ar, cmd::SetColonyTypes& c) { fields(ar, c.colonyTypes); }
template <class Ar> void io(Ar& ar, cmd::SetEmpireOptions& c) { fields(ar, c.aiMinimalChanges, c.passwordHash, c.chooseColonyType); }
template <class Ar>
void io(Ar& ar, cmd::SetMinisters& c) {
    fields(ar, c.areas, c.style, c.useRaceStyle, c.newVehicles, c.individual, c.completeAi);
}
template <class Ar> void io(Ar& ar, cmd::SetEncounterOptions& c) {
    fields(ar, c.clearOrdersOnEncounter, c.avoidTaggedMinefields, c.avoidRestrictedSystems);
}

template <class Ar> void io(Ar& ar, cmd::EnterSector& c) { fields(ar, c.vehicle, c.fleet, c.where, c.enter); }
template <class Ar> void io(Ar& ar, cmd::OpenVehicleReport& c) { fields(ar, c.vehicle); }
template <class Ar> void io(Ar& ar, cmd::JettisonCargo& c) { fields(ar, c.vehicle, c.planet, c.population, c.units); }
template <class Ar> void io(Ar& ar, cmd::CloakColony& c) { fields(ar, c.planet, c.cloak); }

template <class Ar> void io(Ar& ar, EmpireOrders& o) { fields(ar, o.empire, o.turn, o.commands); }

// ---- New-game setup (setup.hpp) ----------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, EmpireSetup& e) {
    fields(ar, e.name, e.empireType, e.leaderTitle, e.leaderName, e.preset, e.presetTier, e.customRace, e.color, e.kind, e.passwordHash,
           e.ministerStyle, e.useRaceMinisterStyle, e.experience, e.designs, e.strategies);
}

template <class Ar> void io(Ar& ar, QuadrantMap& m) { fields(ar, m.name, m.galaxy, m.startingPoints); }
template <class Ar> void io(Ar& ar, GameSetup& g) { fields(ar, g.seed, g.options, g.empires, g.map); }

// ---- Save file header (serialize.hpp) ------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, SaveInfo& i) {
    fields(ar, i.gameName, i.dataSet, i.turn, i.empires, i.gameId, i.players, i.masterPasswordVerifier);
}

// ---- Helpers -----------------------------------------------------------------------------------------

// Encodes `value` (appending to `out`).
template <class T>
void write(std::vector<uint8_t>& out, const T& value, uint32_t version = kSaveVersion) {
    ByteWriter w(out, version);
    io(w, const_cast<T&>(value));  // writers never modify
}

template <class T>
std::vector<uint8_t> encode(const T& value, uint32_t version = kSaveVersion) {
    std::vector<uint8_t> out;
    write(out, value, version);
    return out;
}

// Decodes a complete value: fails on errors and on unread trailing bytes.
template <class T>
bool decode(std::span<const uint8_t> bytes, T& out, std::string& error, uint32_t version = kSaveVersion) {
    Reader r(bytes, version);
    io(r, out);
    if (r.ok() && r.remaining() != 0) r.fail(std::string("unexpected data after the end"));
    if (!r.ok()) {
        error = r.error() + " (at byte " + std::to_string(r.position()) + ")";
        return false;
    }
    return true;
}

template <class T>
uint64_t hash(const T& value, uint32_t version = kSaveVersion) {
    HashWriter h(version);
    io(h, const_cast<T&>(value));
    return h.value();
}

} // namespace opense4::game::serial
