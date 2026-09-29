#pragma once

// The complete, serializable state of one game. Plain data: rules live in
// Content, behavior lives in the free functions of turn/commands/queries.

#include "core/math.hpp"
#include "core/rng.hpp"
#include "sim/content.hpp"
#include "sim/types.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace opense4::sim {

struct StarSystem {
    SystemId id;
    std::string name;
    Vec2 position;  // galaxy-map coordinates; presentation only, never used by rules
    StarClass star = StarClass::Yellow;
    std::vector<PlanetId> planets;
    std::vector<WarpPointId> warpPoints;
};

// Warp points come in linked pairs: moving onto `exit` from this sector jumps
// the ship into the destination system.
struct WarpPoint {
    WarpPointId id;
    SystemId system;
    SectorPos sector;
    WarpPointId exit;
};

enum class ConstructionKind : uint8_t { Ship, Facility };

struct ConstructionItem {
    ConstructionKind kind = ConstructionKind::Ship;
    DesignId design;          // kind == Ship
    FacilityIndex facility;   // kind == Facility
    Resources cost;
    Resources spent;          // progress so far

    int percentComplete() const {
        const int64_t total = cost.total();
        return total > 0 ? static_cast<int>(spent.total() * 100 / total) : 100;
    }
};

struct Colony {
    EmpireId owner;
    int64_t population = 0;  // millions
    std::vector<FacilityIndex> facilities;
    std::vector<ConstructionItem> queue;
    uint32_t foundedTurn = 0;
};

struct Planet {
    PlanetId id;
    std::string name;
    SystemId system;
    SectorPos sector;
    PlanetSurface surface = PlanetSurface::Rock;
    Atmosphere atmosphere = Atmosphere::None;
    PlanetSize size = PlanetSize::Medium;
    std::array<int, kResourceCount> value{100, 100, 100};  // resource richness, percent
    std::optional<Colony> colony;

    Location location() const { return {system, sector}; }
};

// Derived numbers for a design; recomputed from content, never saved.
struct DesignStats {
    int tonnageUsed = 0;
    int tonnageMax = 0;
    int structure = 0;
    int speed = 0;
    int shields = 0;
    int supply = 0;
    int cargo = 0;
    int sensor = 0;
    int weaponCount = 0;
    int weaponDamage = 0;  // total per round
    int engines = 0;
    Resources cost;
    std::vector<PlanetSurface> colonizes;
    std::vector<std::string> problems;  // empty if the design is valid

    bool armed() const { return weaponCount > 0; }
    bool canColonize(PlanetSurface s) const { return std::find(colonizes.begin(), colonizes.end(), s) != colonizes.end(); }
};

struct Design {
    DesignId id;
    EmpireId owner;
    std::string name;
    std::string role;
    HullIndex hull;
    std::vector<ComponentIndex> components;
    DesignStats stats;  // cached; see computeDesignStats()
    bool obsolete = false;
    int built = 0;      // for naming ships "Scout 3"
};

enum class OrderType : uint8_t { None, Move, Colonize };

struct ShipOrder {
    OrderType type = OrderType::None;
    Location destination;  // Move and Colonize
    PlanetId planet;       // Colonize
};

struct Ship {
    ShipId id;
    EmpireId owner;
    DesignId design;
    std::string name;
    Location location;
    int movesLeft = 0;
    int damage = 0;             // structure lost; destroyed when >= stats.structure
    ShipOrder order;
    std::vector<Location> path;  // remaining steps, excluding the current location
};

struct Empire {
    EmpireId id;
    std::string name;
    RaceIndex race;
    uint32_t color = 0xffffff;
    bool ai = false;
    bool alive = true;
    PlanetSurface nativeSurface = PlanetSurface::Rock;
    Atmosphere breathes = Atmosphere::Oxygen;
    PlanetId homeworld;

    Resources stockpile;
    Resources lastIncome;
    int64_t lastResearch = 0;

    std::vector<int> techLevels;          // per TechIndex
    std::vector<int64_t> techProgress;    // points toward the next level, per TechIndex
    std::vector<TechIndex> researchQueue;
    int64_t unspentResearch = 0;          // accumulates while the queue is empty

    std::vector<uint8_t> explored;  // per SystemId
    std::vector<DesignId> designs;

    int techLevel(TechIndex t) const { return techLevels[t.index()]; }
    bool hasExplored(SystemId s) const { return explored[s.index()] != 0; }
};

enum class EventKind : uint8_t { Info, Exploration, Movement, Colonization, Construction, Research, Combat };

struct GameEvent {
    EventKind kind = EventKind::Info;
    EmpireId empire;           // recipient
    std::string text;
    std::optional<Location> location;
};

struct GalaxySettings {
    uint64_t seed = 1;
    int systemCount = 40;
    GalaxyShape shape = GalaxyShape::Spiral;
    int sectorRadius = 6;
    int extraWarpPercent = 35;   // chance to keep each candidate non-tree warp lane
    int maxWarpsPerSystem = 5;
};

struct GameState {
    uint32_t turn = 1;
    GalaxySettings galaxy;

    std::vector<StarSystem> systems;
    std::vector<Planet> planets;
    std::vector<WarpPoint> warpPoints;
    std::vector<Empire> empires;
    std::vector<Design> designs;
    std::vector<Ship> ships;  // sorted by id
    uint32_t nextShipId = 0;

    Rng rng;
    std::vector<GameEvent> events;  // produced by the most recent turn

    int sectorRadius() const { return galaxy.sectorRadius; }
    int sectorsPerSide() const { return 2 * galaxy.sectorRadius + 1; }
    bool inBounds(SectorPos p) const {
        const int r = galaxy.sectorRadius;
        return p.x >= -r && p.x <= r && p.y >= -r && p.y <= r;
    }

    StarSystem& system(SystemId id) { return systems[id.index()]; }
    const StarSystem& system(SystemId id) const { return systems[id.index()]; }
    Planet& planet(PlanetId id) { return planets[id.index()]; }
    const Planet& planet(PlanetId id) const { return planets[id.index()]; }
    WarpPoint& warpPoint(WarpPointId id) { return warpPoints[id.index()]; }
    const WarpPoint& warpPoint(WarpPointId id) const { return warpPoints[id.index()]; }
    Empire& empire(EmpireId id) { return empires[id.index()]; }
    const Empire& empire(EmpireId id) const { return empires[id.index()]; }
    Design& design(DesignId id) { return designs[id.index()]; }
    const Design& design(DesignId id) const { return designs[id.index()]; }

    Ship* findShip(ShipId id) {
        auto it = std::lower_bound(ships.begin(), ships.end(), id, [](const Ship& s, ShipId v) { return s.id < v; });
        return (it != ships.end() && it->id == id) ? &*it : nullptr;
    }
    const Ship* findShip(ShipId id) const { return const_cast<GameState*>(this)->findShip(id); }

    const DesignStats& statsOf(const Ship& s) const { return design(s.design).stats; }
};

} // namespace opense4::sim
