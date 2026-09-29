#include "sim/setup.hpp"

#include "sim/galaxy_gen.hpp"
#include "sim/mutate.hpp"
#include "sim/names.hpp"
#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"

#include <algorithm>
#include <format>

namespace opense4::sim {

namespace {

// Picks `count` systems spread as far apart as possible on the warp network.
std::vector<SystemId> pickHomeSystems(const GameState& s, int count, Rng& rng) {
    const size_t n = s.systems.size();
    std::vector<SystemId> homes{SystemId{rng.below(n)}};
    std::vector<int> minDist = warpHopDistances(s, homes.front());
    while (static_cast<int>(homes.size()) < count) {
        int best = -1;
        std::vector<SystemId> ties;
        for (size_t i = 0; i < n; ++i) {
            const SystemId id{i};
            if (std::find(homes.begin(), homes.end(), id) != homes.end()) continue;
            if (minDist[i] > best) {
                best = minDist[i];
                ties.clear();
            }
            if (minDist[i] == best) ties.push_back(id);
        }
        const SystemId next = ties[rng.below(ties.size())];
        homes.push_back(next);
        const std::vector<int> d = warpHopDistances(s, next);
        for (size_t i = 0; i < n; ++i) minDist[i] = std::min(minDist[i], d[i]);
    }
    rng.shuffle(homes);
    return homes;
}

// Turns a planet of the home system into a fitting homeworld (creating one if needed).
PlanetId prepareHomeworld(GameState& s, SystemId sysId, const Empire& e, Rng& rng) {
    StarSystem& sys = s.system(sysId);
    PlanetId chosen;
    for (PlanetId pid : sys.planets)
        if (s.planet(pid).surface == e.nativeSurface) {
            chosen = pid;
            break;
        }
    if (!chosen && !sys.planets.empty()) chosen = sys.planets[rng.below(sys.planets.size())];
    if (!chosen) {
        // Any free sector off the star, preferring ones not hugging it.
        std::vector<SectorPos> free, cramped;
        const int r = s.sectorRadius();
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                const SectorPos p{x, y};
                if (p == SectorPos{}) continue;
                const bool taken = std::any_of(sys.warpPoints.begin(), sys.warpPoints.end(),
                                               [&](WarpPointId w) { return s.warpPoint(w).sector == p; });
                if (taken) continue;
                (sectorDistance(p, {}) >= 2 && sectorDistance(p, {}) <= r - 1 ? free : cramped).push_back(p);
            }
        if (free.empty()) free = cramped;
        if (free.empty()) return PlanetId{};
        Planet p;
        p.id = PlanetId{s.planets.size()};
        p.system = sysId;
        p.sector = free[rng.below(free.size())];
        p.name = sys.name + " " + romanNumeral(1);
        s.planets.push_back(p);
        sys.planets.push_back(p.id);
        chosen = p.id;
    }
    Planet& home = s.planet(chosen);
    home.surface = e.nativeSurface;
    home.atmosphere = e.breathes;
    home.size = PlanetSize::Large;
    home.value = {100, 100, 100};
    return chosen;
}

} // namespace

std::expected<NewGame, std::string> createGame(const Content& c, const GameSetup& setup) {
    const int empireCount = setup.empireCount;
    if (empireCount < 1) return std::unexpected("At least one empire is required.");
    if (empireCount > static_cast<int>(c.races.size()))
        return std::unexpected(std::format("Only {} races are defined; cannot create {} empires.", c.races.size(), empireCount));
    if (setup.galaxy.systemCount < empireCount * 2)
        return std::unexpected(std::format("{} systems are too few for {} empires.", setup.galaxy.systemCount, empireCount));
    if (setup.galaxy.systemCount > kMaxSystems)
        return std::unexpected(std::format("At most {} star systems are supported.", kMaxSystems));
    if (setup.galaxy.sectorRadius < kMinSectorRadius || setup.galaxy.sectorRadius > kMaxSectorRadius)
        return std::unexpected(std::format("Sector radius must be between {} and {}.", kMinSectorRadius, kMaxSectorRadius));

    std::optional<RaceIndex> playerRace;
    if (!setup.allAi) {
        playerRace = c.findRace(setup.playerRace);
        if (!playerRace) return std::unexpected(std::format("Unknown race '{}'.", setup.playerRace));
    }

    NewGame ng;
    GameState& s = ng.state;
    s.galaxy = setup.galaxy;
    s.rng.reseed(setup.galaxy.seed);
    Rng gen = s.rng.fork();
    generateGalaxy(s, gen);

    // Race assignment: the player's choice first, the rest shuffled.
    std::vector<RaceIndex> races;
    if (playerRace) races.push_back(*playerRace);
    std::vector<RaceIndex> others;
    for (size_t i = 0; i < c.races.size(); ++i)
        if (!playerRace || RaceIndex{i} != *playerRace) others.push_back(RaceIndex{i});
    gen.shuffle(others);
    for (RaceIndex r : others)
        if (static_cast<int>(races.size()) < empireCount) races.push_back(r);

    const std::vector<SystemId> homes = pickHomeSystems(s, empireCount, gen);
    const Rules& rules = c.rules;

    for (int i = 0; i < empireCount; ++i) {
        const RaceDef& race = c.race(races[static_cast<size_t>(i)]);
        Empire e;
        e.id = EmpireId{static_cast<uint32_t>(i)};
        e.name = race.empireName;
        e.race = races[static_cast<size_t>(i)];
        e.color = race.color;
        e.ai = setup.allAi || i != 0;
        e.nativeSurface = race.nativeSurface;
        e.breathes = race.breathes;
        e.stockpile = rules.startingResources;
        e.techLevels.assign(c.techs.size(), 0);
        e.techProgress.assign(c.techs.size(), 0);
        for (const auto& req : rules.startingTechs) e.techLevels[req.tech.index()] = std::max(e.techLevels[req.tech.index()], req.level);
        for (const auto& req : race.bonusTechs) e.techLevels[req.tech.index()] = std::max(e.techLevels[req.tech.index()], req.level);
        e.explored.assign(s.systems.size(), 0);
        s.empires.push_back(std::move(e));
    }

    for (int i = 0; i < empireCount; ++i) {
        const EmpireId eid{static_cast<uint32_t>(i)};
        const RaceDef& race = c.race(s.empire(eid).race);
        const SystemId home = homes[static_cast<size_t>(i)];

        const PlanetId hw = prepareHomeworld(s, home, s.empire(eid), gen);
        if (!hw) return std::unexpected("No room for a homeworld; increase the sector radius.");
        foundColony(s, hw, eid, rules.homeworldPopulation);
        s.planet(hw).colony->facilities = rules.homeworldFacilities;
        s.empire(eid).homeworld = hw;
        markExplored(s, eid, home);

        for (const DesignTemplate& tpl : c.designTemplates) {
            std::vector<ComponentIndex> comps;
            bool ok = true;
            for (ComponentIndex ci : tpl.components) {
                if (ci.valid()) comps.push_back(ci);
                else if (race.colonyComponent.valid()) comps.push_back(race.colonyComponent);
                else ok = false;
            }
            if (ok) addDesign(s, c, eid, tpl.name, tpl.role, tpl.hull, comps);
        }

        const Location at = s.planet(hw).location();
        for (const StartingShip& start : rules.startingShips) {
            for (DesignId did : s.empire(eid).designs) {
                if (s.design(did).name != start.design) continue;
                for (int k = 0; k < start.count; ++k) spawnShip(s, eid, did, at);
                break;
            }
        }
    }

    if (!setup.allAi) {
        ng.player = EmpireId{0u};
        const Planet& hw = s.planet(s.empire(ng.player).homeworld);
        addEvent(s, ng.player, EventKind::Info,
                 std::format("The {} rises. Our homeworld {} awaits your orders.", s.empire(ng.player).name, hw.name),
                 hw.location());
    }
    return ng;
}

} // namespace opense4::sim
