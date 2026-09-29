#include "sim/galaxy_gen.hpp"

#include "sim/names.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_set>

namespace opense4::sim {

namespace {

constexpr float kSpacing = 70.0f;  // preferred minimum distance between systems on the galaxy map

float gaussian(Rng& rng) {
    const float u1 = std::max(rng.unit(), 1e-7f);
    const float u2 = rng.unit();
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(kTau * u2);
}

template <class T, size_t N>
T weightedPick(Rng& rng, const std::array<std::pair<T, int>, N>& table) {
    int total = 0;
    for (const auto& [value, weight] : table) total += weight;
    int roll = static_cast<int>(rng.below(static_cast<uint64_t>(total)));
    for (const auto& [value, weight] : table) {
        if (roll < weight) return value;
        roll -= weight;
    }
    return table.back().first;
}

std::vector<Vec2> placeSystems(const GalaxySettings& g, Rng& rng) {
    const int n = std::max(2, g.systemCount);
    const float radius = std::sqrt(static_cast<float>(n) * kSpacing * kSpacing * 2.2f / kPi);

    const int arms = 2 + static_cast<int>(rng.below(3));
    std::vector<Vec2> clusterCenters;
    if (g.shape == GalaxyShape::Clusters) {
        const int k = std::max(3, n / 9);
        const float offset = rng.unit() * kTau;
        for (int i = 0; i < k; ++i) {
            const float angle = offset + static_cast<float>(i) * kTau / static_cast<float>(k) + rng.rangeF(-0.3f, 0.3f);
            clusterCenters.push_back(fromAngle(angle) * (radius * rng.rangeF(0.45f, 0.85f)));
        }
    }

    auto sample = [&]() -> Vec2 {
        switch (g.shape) {
            case GalaxyShape::Spiral: {
                if (rng.percent(18)) return fromAngle(rng.unit() * kTau) * (radius * 0.28f * std::sqrt(rng.unit()));
                const float t = rng.unit();
                const int arm = static_cast<int>(rng.below(static_cast<uint64_t>(arms)));
                const float angle = static_cast<float>(arm) * kTau / static_cast<float>(arms) + t * kPi * 1.5f;
                const Vec2 p = fromAngle(angle) * (radius * (0.15f + 0.85f * t));
                return p + Vec2{gaussian(rng), gaussian(rng)} * (radius * 0.07f);
            }
            case GalaxyShape::Elliptical: {
                const Vec2 d = fromAngle(rng.unit() * kTau) * (radius * std::sqrt(rng.unit()));
                return {d.x * 1.3f, d.y * 0.8f};
            }
            case GalaxyShape::Ring:
                return fromAngle(rng.unit() * kTau) * (radius * rng.rangeF(0.7f, 1.0f));
            case GalaxyShape::Clusters:
            case GalaxyShape::Count:
                break;
        }
        const Vec2 c = clusterCenters[rng.below(clusterCenters.size())];
        return c + Vec2{gaussian(rng), gaussian(rng)} * (radius * 0.15f);
    };

    std::vector<Vec2> points;
    float minDist = kSpacing;
    int attempts = 0;
    while (static_cast<int>(points.size()) < n) {
        const Vec2 p = sample();
        const bool ok = std::none_of(points.begin(), points.end(),
                                     [&](Vec2 q) { return lengthSq(p - q) < minDist * minDist; });
        if (ok) {
            points.push_back(p);
            attempts = 0;
        } else if (++attempts > 400) {
            minDist *= 0.9f;  // shape too crowded: relax spacing rather than loop forever
            attempts = 0;
        }
    }
    return points;
}

using Lane = std::pair<int, int>;

std::vector<Lane> buildLanes(const std::vector<Vec2>& pts, const GalaxySettings& g, Rng& rng) {
    const int n = static_cast<int>(pts.size());
    std::vector<Lane> lanes;
    std::vector<int> degree(static_cast<size_t>(n), 0);

    // Prim's algorithm on the complete graph gives the Euclidean MST: connected and crossing-free.
    {
        std::vector<float> best(static_cast<size_t>(n), std::numeric_limits<float>::max());
        std::vector<int> parent(static_cast<size_t>(n), -1);
        std::vector<bool> inTree(static_cast<size_t>(n), false);
        best[0] = 0.0f;
        for (int iter = 0; iter < n; ++iter) {
            int u = -1;
            for (int i = 0; i < n; ++i)
                if (!inTree[static_cast<size_t>(i)] && (u < 0 || best[static_cast<size_t>(i)] < best[static_cast<size_t>(u)])) u = i;
            inTree[static_cast<size_t>(u)] = true;
            if (const int p = parent[static_cast<size_t>(u)]; p >= 0) {
                lanes.emplace_back(std::min(p, u), std::max(p, u));
                ++degree[static_cast<size_t>(p)];
                ++degree[static_cast<size_t>(u)];
            }
            for (int v = 0; v < n; ++v) {
                const float d = distance(pts[static_cast<size_t>(u)], pts[static_cast<size_t>(v)]);
                if (!inTree[static_cast<size_t>(v)] && d < best[static_cast<size_t>(v)]) {
                    best[static_cast<size_t>(v)] = d;
                    parent[static_cast<size_t>(v)] = u;
                }
            }
        }
    }

    // Candidate extra lanes: each system's nearest neighbours, shortest first.
    std::set<Lane> candidates;
    constexpr int kNearest = 5;
    for (int i = 0; i < n; ++i) {
        std::vector<int> order;
        for (int j = 0; j < n; ++j)
            if (j != i) order.push_back(j);
        std::partial_sort(order.begin(), order.begin() + std::min<int>(kNearest, static_cast<int>(order.size())), order.end(),
                          [&](int a, int b) {
                              return lengthSq(pts[static_cast<size_t>(a)] - pts[static_cast<size_t>(i)]) <
                                     lengthSq(pts[static_cast<size_t>(b)] - pts[static_cast<size_t>(i)]);
                          });
        for (int k = 0; k < std::min<int>(kNearest, static_cast<int>(order.size())); ++k)
            candidates.emplace(std::min(i, order[static_cast<size_t>(k)]), std::max(i, order[static_cast<size_t>(k)]));
    }
    std::vector<Lane> sorted(candidates.begin(), candidates.end());
    std::stable_sort(sorted.begin(), sorted.end(), [&](const Lane& a, const Lane& b) {
        return lengthSq(pts[static_cast<size_t>(a.first)] - pts[static_cast<size_t>(a.second)]) <
               lengthSq(pts[static_cast<size_t>(b.first)] - pts[static_cast<size_t>(b.second)]);
    });

    std::set<Lane> existing(lanes.begin(), lanes.end());
    for (const Lane& lane : sorted) {
        const auto [a, b] = lane;
        if (existing.contains(lane)) continue;
        if (degree[static_cast<size_t>(a)] >= g.maxWarpsPerSystem || degree[static_cast<size_t>(b)] >= g.maxWarpsPerSystem) continue;
        if (!rng.percent(g.extraWarpPercent)) continue;
        const Vec2 pa = pts[static_cast<size_t>(a)];
        const Vec2 pb = pts[static_cast<size_t>(b)];
        const bool crosses = std::any_of(lanes.begin(), lanes.end(), [&](const Lane& o) {
            if (o.first == a || o.first == b || o.second == a || o.second == b) return false;
            return segmentsCross(pa, pb, pts[static_cast<size_t>(o.first)], pts[static_cast<size_t>(o.second)]);
        });
        if (crosses) continue;
        bool grazes = false;
        for (int k = 0; k < n && !grazes; ++k)
            if (k != a && k != b && distanceToSegment(pts[static_cast<size_t>(k)], pa, pb) < kSpacing * 0.35f) grazes = true;
        if (grazes) continue;
        lanes.push_back(lane);
        existing.insert(lane);
        ++degree[static_cast<size_t>(a)];
        ++degree[static_cast<size_t>(b)];
    }
    return lanes;
}

StarClass rollStar(Rng& rng) {
    static constexpr std::array<std::pair<StarClass, int>, 7> kTable{{
        {StarClass::Red, 30}, {StarClass::Orange, 20}, {StarClass::Yellow, 18}, {StarClass::White, 14},
        {StarClass::Blue, 8}, {StarClass::WhiteDwarf, 6}, {StarClass::Neutron, 4}}};
    return weightedPick(rng, kTable);
}

// Sector for a warp point in a system, on the ring just inside the border,
// in the direction of the destination system. Avoids occupied sectors.
SectorPos warpSector(Vec2 dir, int radius, const std::vector<SectorPos>& occupied) {
    const int ring = radius - 1;
    const float m = std::max(std::abs(dir.x), std::abs(dir.y));
    const Vec2 ideal = m > 0.0f ? dir / m * static_cast<float>(ring) : Vec2{static_cast<float>(ring), 0.0f};
    // Prefer the ring just inside the border; if it is full, take any free sector off the star.
    for (const int minRing : {ring - 1, 1}) {
        std::optional<SectorPos> best;
        float bestDist = std::numeric_limits<float>::max();
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                const SectorPos p{x, y};
                const int d = sectorDistance(p, {});
                if (d < minRing || (minRing > 1 && d > ring)) continue;
                if (std::find(occupied.begin(), occupied.end(), p) != occupied.end()) continue;
                const float dist = lengthSq(Vec2{static_cast<float>(x), static_cast<float>(y)} - ideal);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = p;
                }
            }
        }
        if (best) return *best;
    }
    // (2r+1)^2 - 1 sectors can't all hold warp points for sane radii; createGame enforces r >= 4.
    return SectorPos{radius, radius};
}

void generatePlanets(GameState& s, StarSystem& sys, std::vector<SectorPos> occupied, Rng& rng) {
    const int radius = s.sectorRadius();
    int count = 0;
    if (sys.star == StarClass::Neutron || sys.star == StarClass::WhiteDwarf) count = rng.rangeInt(0, 3);
    else if (!rng.percent(8)) count = rng.rangeInt(2, std::min(8, radius + 1));

    static constexpr std::array<std::pair<PlanetSurface, int>, 3> kSurfaces{{
        {PlanetSurface::Rock, 45}, {PlanetSurface::Ice, 25}, {PlanetSurface::Gas, 30}}};
    static constexpr std::array<std::pair<PlanetSize, int>, 5> kSolidSizes{{
        {PlanetSize::Tiny, 15}, {PlanetSize::Small, 25}, {PlanetSize::Medium, 30}, {PlanetSize::Large, 20}, {PlanetSize::Huge, 10}}};
    static constexpr std::array<std::pair<PlanetSize, int>, 3> kGasSizes{{
        {PlanetSize::Medium, 20}, {PlanetSize::Large, 45}, {PlanetSize::Huge, 35}}};
    static constexpr std::array<std::pair<Atmosphere, int>, 5> kRockAir{{
        {Atmosphere::None, 30}, {Atmosphere::Oxygen, 18}, {Atmosphere::CarbonDioxide, 22}, {Atmosphere::Methane, 15}, {Atmosphere::Hydrogen, 15}}};
    static constexpr std::array<std::pair<Atmosphere, int>, 4> kIceAir{{
        {Atmosphere::None, 35}, {Atmosphere::Methane, 25}, {Atmosphere::Hydrogen, 20}, {Atmosphere::CarbonDioxide, 20}}};
    static constexpr std::array<std::pair<Atmosphere, int>, 3> kGasAir{{
        {Atmosphere::Hydrogen, 55}, {Atmosphere::Methane, 30}, {Atmosphere::None, 15}}};

    // Planets sit on distinct "orbits" (distances from the star) for a tidy map.
    const float innermost = 1.4f;
    const float outermost = static_cast<float>(radius) - 1.5f;
    std::vector<PlanetId> created;
    for (int i = 0; i < count; ++i) {
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.5f;
        const float orbit = innermost + (outermost - innermost) * t + rng.rangeF(-0.3f, 0.3f);
        std::optional<SectorPos> spot;
        for (int attempt = 0; attempt < 24 && !spot; ++attempt) {
            const Vec2 p = fromAngle(rng.unit() * kTau) * orbit;
            const SectorPos sp{static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))};
            if (sp == SectorPos{} || !s.inBounds(sp)) continue;
            if (std::find(occupied.begin(), occupied.end(), sp) != occupied.end()) continue;
            spot = sp;
        }
        if (!spot) continue;
        occupied.push_back(*spot);

        Planet p;
        p.id = PlanetId{s.planets.size()};
        p.system = sys.id;
        p.sector = *spot;
        p.surface = weightedPick(rng, kSurfaces);
        switch (p.surface) {
            case PlanetSurface::Gas:
                p.size = weightedPick(rng, kGasSizes);
                p.atmosphere = weightedPick(rng, kGasAir);
                break;
            case PlanetSurface::Ice:
                p.size = weightedPick(rng, kSolidSizes);
                p.atmosphere = weightedPick(rng, kIceAir);
                break;
            default:
                p.size = weightedPick(rng, kSolidSizes);
                p.atmosphere = weightedPick(rng, kRockAir);
                break;
        }
        for (int& v : p.value) v = (rng.rangeInt(20, 150) + rng.rangeInt(20, 150)) / 2;
        created.push_back(p.id);
        s.planets.push_back(std::move(p));
    }

    // Name planets by distance from the star: "Rigel I", "Rigel II", ...
    std::sort(created.begin(), created.end(), [&](PlanetId a, PlanetId b) {
        const SectorPos pa = s.planet(a).sector;
        const SectorPos pb = s.planet(b).sector;
        const int da = pa.x * pa.x + pa.y * pa.y;
        const int db = pb.x * pb.x + pb.y * pb.y;
        return da != db ? da < db : a < b;
    });
    for (size_t i = 0; i < created.size(); ++i) s.planet(created[i]).name = sys.name + " " + romanNumeral(static_cast<int>(i) + 1);
    sys.planets = created;
    std::sort(sys.planets.begin(), sys.planets.end());
}

} // namespace

void generateGalaxy(GameState& s, Rng& rng) {
    const GalaxySettings& g = s.galaxy;
    const std::vector<Vec2> positions = placeSystems(g, rng);

    std::unordered_set<std::string> usedNames;
    for (size_t i = 0; i < positions.size(); ++i) {
        StarSystem sys;
        sys.id = SystemId{i};
        sys.position = positions[i];
        sys.star = rollStar(rng);
        do sys.name = generateStarName(rng);
        while (!usedNames.insert(sys.name).second);
        s.systems.push_back(std::move(sys));
    }

    const std::vector<Lane> lanes = buildLanes(positions, g, rng);
    std::vector<std::vector<SectorPos>> occupied(s.systems.size(), std::vector<SectorPos>{SectorPos{}});
    for (const auto& [a, b] : lanes) {
        StarSystem& sa = s.systems[static_cast<size_t>(a)];
        StarSystem& sb = s.systems[static_cast<size_t>(b)];
        const Vec2 dir = sb.position - sa.position;

        WarpPoint wa;
        wa.id = WarpPointId{s.warpPoints.size()};
        wa.system = sa.id;
        wa.sector = warpSector(dir, g.sectorRadius, occupied[static_cast<size_t>(a)]);
        WarpPoint wb;
        wb.id = WarpPointId{s.warpPoints.size() + 1};
        wb.system = sb.id;
        wb.sector = warpSector(-dir, g.sectorRadius, occupied[static_cast<size_t>(b)]);
        wa.exit = wb.id;
        wb.exit = wa.id;

        occupied[static_cast<size_t>(a)].push_back(wa.sector);
        occupied[static_cast<size_t>(b)].push_back(wb.sector);
        sa.warpPoints.push_back(wa.id);
        sb.warpPoints.push_back(wb.id);
        s.warpPoints.push_back(wa);
        s.warpPoints.push_back(wb);
    }

    for (StarSystem& sys : s.systems) generatePlanets(s, sys, occupied[sys.id.index()], rng);
}

} // namespace opense4::sim
