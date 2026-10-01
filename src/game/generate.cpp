#include "game/generate.hpp"

#include "datafile/datafile.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <map>
#include <set>

namespace opense4::game {

namespace {

using datafile::keysEqual;
using xmath::Ext;

bool isAny(std::string_view s) { return s.empty() || keysEqual(s, "Any"); }
bool isNone(std::string_view s) { return s.empty() || keysEqual(s, "None"); }

// "Gas" and "Gas Giant" name the same planet type.
bool surfaceEqual(std::string_view a, std::string_view b) {
    auto norm = [](std::string_view s) { return keysEqual(s, "Gas") ? std::string_view("Gas Giant") : s; };
    return keysEqual(norm(a), norm(b));
}

bool isRuins(const ruleset::Ability& a) { return keysEqual(a.type, "Ancient Ruins") || keysEqual(a.type, "Ancient Ruins Unique"); }

int draw(Rng& rng, int n) { return n <= 1 ? 0 : static_cast<int>(rng.below(static_cast<uint64_t>(n))); }  // R(n)

// ---- Geometry -----------------------------------------------------------------------------------------
//
// The original's arctangents are reproduced in the x87 extended format of
// xmath.hpp: the series below is accurate to far better than the distance of
// any result we round from a half-integer (tests/test_generate.cpp checks the
// margins), so the rounded results equal the original's.

// π to 63 bits.
const Ext& pi() {
    static const Ext v = Ext(0x6487ED5110B4611A) / Ext(int64_t{1} << 61);
    return v;
}

// atan(x) for |x| ≤ 0.43, by its Taylor series.
Ext atanSeries(Ext x) {
    const Ext x2 = x * x;
    Ext term = x;
    Ext sum = x;
    for (int k = 1; k <= 40; ++k) {
        term = term * x2;
        const Ext t = term / Ext(2 * k + 1);
        sum = (k % 2 != 0) ? sum - t : sum + t;
    }
    return sum;
}

// atan(x) for x ≥ 0: atan(x) = π/2 − atan(1/x) above 1, and π/4 + atan((x − 1)/(x + 1)) above 0.4.
Ext atanExt(Ext x) {
    if (x > Ext(1)) return pi() / Ext(2) - atanExt(Ext(1) / x);
    if (x * Ext(5) > Ext(2)) return pi() / Ext(4) + atanSeries((x - Ext(1)) / (x + Ext(1)));
    return atanSeries(x);
}

// t(u) = atan(u · π / 180) for u = 0..45: the outline function's arctangent of
// an angle taken in radians (a quirk of the original, spec 01 §3.5).
const std::array<Ext, 46>& outlineAtans() {
    static const std::array<Ext, 46> table = [] {
        std::array<Ext, 46> t{};
        for (int u = 0; u <= 45; ++u) t[static_cast<size_t>(u)] = atanExt(Ext(u) * pi() / Ext(180));
        return t;
    }();
    return table;
}

int64_t isqrt(int64_t n) {
    int64_t lo = 0, hi = 3'037'000'499;  // floor(sqrt(INT64_MAX))
    while (lo < hi) {
        const int64_t mid = lo + (hi - lo + 1) / 2;
        if (mid * mid <= n) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

} // namespace

int galaxyDistance(GalaxyPos a, GalaxyPos b) {
    const int64_t dx = a.x - b.x, dy = a.y - b.y;
    const int64_t n = dx * dx + dy * dy;
    const int64_t s = isqrt(n);
    // √n ≥ s + ½  ⟺  n > s² + s for integers; √n is never exactly s + ½.
    return static_cast<int>(s + (n > s * s + s ? 1 : 0));
}

int galaxyBearing(GalaxyPos from, GalaxyPos to) {
    const int dx = std::abs(to.x - from.x), dy = std::abs(to.y - from.y);
    if (dy == 0) return dx == 0 ? 0 : to.x > from.x ? 90 : 270;
    int a = static_cast<int>((atanExt(Ext(dx) / Ext(dy)) * Ext(180) / pi()).round());
    if (from.x <= to.x && from.y < to.y) a = 180 - a;       // below-right or straight below
    else if (to.x < from.x && to.y > from.y) a += 180;       // below-left
    else if (to.x < from.x && to.y < from.y) a = 360 - a;    // above-left
    return a == 360 ? 0 : a;
}

int bearingDifference(int a, int b) {
    if ((a > 270 && b < 90) || (b > 270 && a < 90)) return 360 - std::max(a, b) + std::min(a, b);
    return std::abs(a - b);
}

OutlinePoint squareOutline(int bearing, int twiceR) {
    int a = bearing % 360;
    if (a < 0) a += 360;
    if (bearing == 360) a = 360;
    const Ext r = Ext(twiceR) / Ext(2);
    const auto& t = outlineAtans();
    // round(r ± r·t(u)), half to even.
    auto off = [&](int u, bool plus) {
        const Ext rt = r * t[static_cast<size_t>(std::clamp(u, 0, 45))];
        return static_cast<int>((plus ? r + rt : r - rt).round());
    };
    const int edge = twiceR - 1;
    if (a < 45) return {off(a, true), 0};
    if (a < 90) return {edge, off(90 - a, false)};
    if (a < 135) return {edge, off(a - 90, true)};
    if (a < 180) return {off(180 - a, true), edge};
    if (a < 225) return {off(a - 180, false), edge};
    if (a < 270) return {0, off(270 - a, true)};
    if (a < 315) return {0, off(a - 270, false)};
    return {off(360 - a, false), 0};
}

Sector warpEdgeSector(int a, std::span<const PlacedWarpPoint> existing) {
    const OutlinePoint p = squareOutline(a, kSystemSize);  // r = 6.5
    // The coordinate that runs along the edge, and the direction of the nudge.
    bool alongX = true;
    int sign = 1;
    if (a >= 315 || a <= 45) {
        alongX = true;
        sign = 1;
    } else if (a < 135) {
        alongX = false;
        sign = 1;
    } else if (a <= 215) {
        alongX = true;
        sign = -1;
    } else {
        alongX = false;
        sign = -1;
    }
    const Sector outline{p.x, p.y};
    for (const PlacedWarpPoint& w : existing)
        if (w.sector == outline && a < w.bearing) {
            sign = -sign;
            break;
        }
    int x = p.x, y = p.y;
    int& c = alongX ? x : y;
    if (c % 2 != 0) c += sign;  // c ± ½ rounded half to even: odd values move to the even neighbour
    return {x, y};
}

Sector warpInwardSector(int a, int inward) {
    const OutlinePoint p = squareOutline(a, kSystemSize);
    int x = p.x, y = p.y;
    if (a <= 45 || a >= 315) y += inward;       // top edge: down
    else if (a <= 135) x -= inward;             // right edge: left
    else if (a <= 225) y -= inward;             // bottom edge: up
    else x += inward;                           // left edge: right
    return {x, y};
}

// ---- Helpers shared with setup and stellar manipulation ---------------------------------------------------

int sizeOrdinal(std::string_view s) {
    static constexpr std::array<std::string_view, 5> kSizes{"Tiny", "Small", "Medium", "Large", "Huge"};
    for (size_t i = 0; i < kSizes.size(); ++i)
        if (keysEqual(s, kSizes[i])) return static_cast<int>(i) + 1;
    return 0;
}

namespace {

// The PlanetSize record a SectType's `Planet Size` names, for a planet or an asteroid field.
const ruleset::PlanetSize* planetSizeRecord(const ruleset::Ruleset& rs, ObjectKind kind, std::string_view name) {
    const std::string_view physical = kind == ObjectKind::Asteroids ? "Asteroids" : "Planet";
    for (const auto& ps : rs.planetSizes)
        if (keysEqual(ps.physicalType, physical) && keysEqual(ps.name, name)) return &ps;
    for (const auto& ps : rs.planetSizes)
        if (keysEqual(ps.name, name)) return &ps;
    return nullptr;
}

int recordSize(const ruleset::Ruleset& rs, ObjectKind kind, std::string_view planetSizeName) {
    if (const ruleset::PlanetSize* ps = planetSizeRecord(rs, kind, planetSizeName)) return sizeOrdinal(ps->stellarSize);
    return sizeOrdinal(planetSizeName);
}

bool constructedRecord(const ruleset::Ruleset& rs, ObjectKind kind, std::string_view planetSizeName) {
    const ruleset::PlanetSize* ps = planetSizeRecord(rs, kind, planetSizeName);
    return ps && ps->constructed;
}

} // namespace

int stellarSizeOf(const ruleset::Ruleset& rs, const SpaceObject& obj) { return recordSize(rs, obj.kind, obj.size); }

std::vector<uint32_t> naturalSectorTypes(const ruleset::Ruleset& rs, ObjectKind kind, int size, std::string_view surface,
                                         std::string_view atmosphere) {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < rs.sectorObjectTypes.size(); ++i) {
        const ruleset::SectorObjectType& st = rs.sectorObjectTypes[i];
        if (parseObjectKind(st.physicalType) != kind) continue;
        if (kind == ObjectKind::Planet || kind == ObjectKind::Asteroids) {
            if (constructedRecord(rs, kind, st.planetSize)) continue;
            if (size > 0 && recordSize(rs, kind, st.planetSize) != size) continue;
            if (!surface.empty() && !surfaceEqual(st.planetPhysicalType, surface)) continue;
            if (!atmosphere.empty() && !keysEqual(st.planetAtmosphere, atmosphere)) continue;
        }
        out.push_back(i);
    }
    return out;
}

void applySectorType(const ruleset::Ruleset& rs, SpaceObject& obj, uint32_t index) {
    const ruleset::SectorObjectType& st = rs.sectorObjectTypes[index];
    obj.sectorType = index;
    if (obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) {
        obj.size = st.planetSize;
        obj.surface = st.planetPhysicalType;
        obj.atmosphere = st.planetAtmosphere;
    } else if (obj.kind == ObjectKind::Star || obj.kind == ObjectKind::DestroyedStar) {
        obj.size = st.starSize;
        obj.starAge = st.starAge;
        obj.starColor = st.starColor;
        obj.starLuminosity = st.starLuminosity;
    } else if (obj.kind == ObjectKind::Storm) {
        obj.size = st.stormSize;
    }
}

Conditions rollConditions(bool asteroids, Rng& rng) {
    // R[0,10] / 10 + 0.5 (0.5, 0.6, ... 1.5); asteroid fields get half of that.
    // Computed in the x87 format and stored as a double (conditions.hpp).
    Ext c = Ext(rng.rangeInt(0, 10)) / Ext(10) + Ext(1) / Ext(2);
    if (asteroids) c = c / Ext(2);
    return Conditions::of(c);
}

void rollNaturalValues(const ruleset::Ruleset& rs, SpaceObject& obj, bool finite, Rng& rng) {
    const bool asteroids = obj.kind == ObjectKind::Asteroids;
    const std::string prefix = asteroids ? "Asteroids Value" : "Planet Value";
    const std::string unit = finite ? "Resources" : "Percent";
    const int64_t lo = rs.settings.integer(std::format("{} Low {}", prefix, unit), finite ? 5000 : 50);
    const int64_t hi = rs.settings.integer(std::format("{} High {}", prefix, unit), finite ? 50000 : 150);
    for (int& v : obj.value) v = static_cast<int>(rng.range(std::min(lo, hi), std::max(lo, hi)));
    obj.conditions = rollConditions(asteroids, rng);
}

std::vector<Sector> emptySectors(const Galaxy& g, SystemId sys) {
    std::array<bool, kSystemSize * kSystemSize> used{};
    for (ObjectId o : g.system(sys).objects) {
        const Sector s = g.object(o).sector;
        if (s.valid()) used[static_cast<size_t>(s.y * kSystemSize + s.x)] = true;
    }
    std::vector<Sector> out;
    for (int n = 0; n < kSystemSize * kSystemSize; ++n)
        if (!used[static_cast<size_t>(n)]) out.push_back(Sector{n % kSystemSize, n / kSystemSize});
    return out;
}

std::string romanNumeral(int n) {
    static constexpr std::pair<int, const char*> kTable[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"},
                                                              {90, "XC"},  {50, "L"},   {40, "XL"}, {10, "X"},   {9, "IX"},
                                                              {5, "V"},    {4, "IV"},   {1, "I"}};
    std::string out;
    for (const auto& [v, s] : kTable)
        while (n >= v) {
            out += s;
            n -= v;
        }
    return out;
}

namespace {

// The value of the Roman numeral I..XXX that ends a name ("Xyz IV" -> 4), 0 if
// there is none; larger numerals are not recognised (spec 01 §5.6).
int trailingNumeral(std::string_view name) {
    const size_t space = name.rfind(' ');
    if (space == std::string_view::npos) return 0;
    const std::string_view word = name.substr(space + 1);
    if (word.empty() || word.find_first_not_of("IVX") != std::string_view::npos) return 0;
    for (int n = 1; n <= 30; ++n)
        if (romanNumeral(n) == word) return n;
    return 0;
}

// Sector number y·13 + x (spec 01 §4.1).
size_t sectorIndex(Sector s) { return static_cast<size_t>(s.y * kSystemSize + s.x); }

// The number of sectors of a system that hold a planet (asteroid fields do not count).
int sectorsWithPlanet(const Galaxy& g, SystemId sys) {
    std::array<bool, kSystemSize * kSystemSize> used{};
    for (ObjectId o : g.system(sys).objects) {
        const SpaceObject& obj = g.object(o);
        if (obj.kind == ObjectKind::Planet && obj.sector.valid()) used[sectorIndex(obj.sector)] = true;
    }
    return static_cast<int>(std::count(used.begin(), used.end(), true));
}

} // namespace

std::array<std::optional<ObjectId>, kSystemSize * kSystemSize> firstPlanetPerSector(const Galaxy& g, SystemId sys) {
    std::array<std::optional<ObjectId>, kSystemSize * kSystemSize> first{};
    for (ObjectId o : g.system(sys).objects) {
        const SpaceObject& obj = g.object(o);
        if (obj.kind == ObjectKind::Planet && obj.sector.valid() && !first[sectorIndex(obj.sector)]) first[sectorIndex(obj.sector)] = o;
    }
    return first;
}

int nextPlanetNumeral(const Galaxy& g, SystemId sys) {
    // Planets only (not asteroid fields), the first planet of each sector, and
    // only the numerals I to XXX (spec 01 §5.6, confirmed: binary).
    int highest = 0;
    for (const auto& planet : firstPlanetPerSector(g, sys))
        if (planet) highest = std::max(highest, trailingNumeral(g.object(*planet).name));
    return highest + 1;
}

std::vector<PlacedWarpPoint> placedWarpPoints(const Galaxy& g, SystemId sys) {
    std::vector<PlacedWarpPoint> out;
    for (ObjectId id : g.warpPoints(sys)) {
        const SpaceObject& wp = g.object(id);
        int bearing = 0;
        if (wp.destination.valid() && wp.destination.index() < g.objects.size())
            bearing = galaxyBearing(g.system(sys).position, g.system(g.object(wp.destination).system).position);
        out.push_back({wp.sector, bearing});
    }
    return out;
}

std::vector<int> warpJumps(const Galaxy& g, SystemId from) {
    std::vector<int> jumps(g.systems.size(), -1);
    if (!from.valid() || from.index() >= g.systems.size()) return jumps;
    std::vector<SystemId> queue{from};
    jumps[from.index()] = 0;
    for (size_t i = 0; i < queue.size(); ++i)
        for (SystemId n : g.neighbors(queue[i]))
            if (n.index() < jumps.size() && jumps[n.index()] < 0) {
                jumps[n.index()] = jumps[queue[i].index()] + 1;
                queue.push_back(n);
            }
    return jumps;
}

int maxSystemCount(const ruleset::Ruleset& rs) {
    return static_cast<int>(std::clamp<int64_t>(rs.settings.integer("Maximum Number Of Systems", 100), 1, 255));
}

std::pair<int, int> systemCountRange(const ruleset::Ruleset& rs, QuadrantSize size) {
    const int q = maxSystemCount(rs) / 5;
    switch (size) {
        case QuadrantSize::Small: return {q, 2 * q - 1};
        case QuadrantSize::Medium: return {2 * q, 4 * q - 1};
        case QuadrantSize::Large: return {4 * q, 5 * q - 1};
    }
    return {2 * q, 4 * q - 1};
}

namespace {

enum class Placement { Random, Diffuse, Grid, Clusters, Spiral };

class Generator {
public:
    Generator(const ruleset::Ruleset& rs, const QuadrantOptions& opt, Rng& rng) : rs_(rs), opt_(opt), rng_(rng) {}

    std::expected<Generated, std::string> run() {
        const ruleset::QuadrantType* quadrant = nullptr;
        for (const auto& q : rs_.quadrantTypes)
            if (opt_.quadrantType.empty() || keysEqual(q.name, opt_.quadrantType)) {
                quadrant = &q;
                break;
            }
        if (!quadrant) return std::unexpected(std::format("Unknown quadrant type '{}'.", opt_.quadrantType));
        if (quadrant->systemTypeChances.empty() || rs_.systemTypes.empty())
            return std::unexpected(std::format("Quadrant type '{}' lists no system types.", quadrant->name));
        const int maxSystems = maxSystemCount(rs_);
        if (opt_.systemCount < 0 || opt_.systemCount > maxSystems)
            return std::unexpected(std::format("The number of systems must be between 1 and {}.", maxSystems));

        // The pipeline of spec 01 §3.7 (confirmed: binary).
        const int count = opt_.systemCount > 0 ? opt_.systemCount : rollSystemCount();
        out_.galaxy.quadrantType = quadrant->name;
        out_.galaxy.width = kQuadrantWidth;
        out_.galaxy.height = kQuadrantHeight;
        std::vector<std::string> names = drawNames(count);
        placeSystems(*quadrant, count, names);
        if (!opt_.noWarpPoints) buildLinks(*quadrant);
        links_.resize(out_.galaxy.systems.size());
        for (StarSystem& sys : out_.galaxy.systems) {
            rollSystemType(*quadrant, sys);
            instantiate(sys);
            placeWarpPoints(sys);
        }
        for (const auto& [key, id] : ends_) out_.galaxy.object(id).destination = ends_.at({key.second, key.first});
        return std::move(out_);
    }

private:
    void warn(std::string message) { out_.warnings.push_back(std::move(message)); }

    // ---- 1. Size (spec 01 §2.2) ---------------------------------------------------------------------------
    int rollSystemCount() {
        const int q = maxSystemCount(rs_) / 5;
        int count = 0;
        switch (opt_.size) {
            case QuadrantSize::Small: count = q + draw(rng_, q); break;
            case QuadrantSize::Medium: count = 2 * q + draw(rng_, 2 * q); break;
            case QuadrantSize::Large: count = 4 * q + draw(rng_, q); break;
        }
        return std::max(1, count);
    }

    // ---- 2. Names (spec 01 §3.4) --------------------------------------------------------------------------
    // A random start in the name list, then the first unused name from there.
    std::vector<std::string> drawNames(int count) {
        std::vector<std::string> pool;
        for (const std::string& n : rs_.names.systemNames)
            if (n.find_first_not_of(" \t\r\n") != std::string::npos) pool.push_back(n);
        std::vector<uint8_t> used(pool.size(), 0);
        std::vector<std::string> out;
        int fallback = 0;
        for (int i = 0; i < count; ++i) {
            if (static_cast<size_t>(i) < pool.size()) {
                const size_t start = static_cast<size_t>(draw(rng_, static_cast<int>(pool.size())));
                for (size_t k = 0; k < pool.size(); ++k) {
                    const size_t at = (start + k) % pool.size();
                    if (used[at]) continue;
                    used[at] = 1;
                    out.push_back(pool[at]);
                    break;
                }
            } else {
                // The original leaves these systems unnamed; we name them and say so (OpenSE4 choice).
                if (fallback == 0) warn(std::format("SystemNames.txt has {} names for {} systems; the rest get generated names.", pool.size(), count));
                out.push_back(std::format("System {}", ++fallback));
            }
        }
        return out;
    }

    // ---- 3. Placement (spec 01 §3.3) --------------------------------------------------------------------------
    void placeSystems(const ruleset::QuadrantType& q, int count, const std::vector<std::string>& names) {
        Placement how = Placement::Random;
        if (keysEqual(q.systemPlacement, "Diffuse")) how = Placement::Diffuse;
        else if (keysEqual(q.systemPlacement, "Grid")) how = Placement::Grid;
        else if (keysEqual(q.systemPlacement, "Clusters")) how = Placement::Clusters;
        else if (keysEqual(q.systemPlacement, "Spiral")) how = Placement::Spiral;
        else if (!keysEqual(q.systemPlacement, "Random"))
            warn(std::format("Unknown system placement '{}'; using Random.", q.systemPlacement));  // (OpenSE4 choice)
        // Two systems are too close when both |dx| and |dy| are at most D (a negative D is treated as 0).
        const int minDist = std::max(0, q.minDistanceBetweenSystems) + (how == Placement::Diffuse ? 2 : 0);
        // Clusters: width c and gap g of each cluster cell, by the system count.
        const int c = count > 150 ? 12 : count > 80 ? 10 : 7;
        const int g = count > 150 ? 3 : count > 80 ? 5 : 8;
        const int cell = c + g;
        const int cellsX = std::max(1, kQuadrantWidth / cell), cellsY = std::max(1, kQuadrantHeight / cell);
        const int perCluster = count / (cellsX * cellsY) + 1;

        // x and y in the spec's coordinates (1..67, 1..46) for system number i (from 1).
        auto propose = [&](int i) -> std::pair<int, int> {
            switch (how) {
                case Placement::Random:
                case Placement::Diffuse: {
                    const int x = draw(rng_, kQuadrantWidth);
                    return {x, draw(rng_, kQuadrantHeight)};
                }
                case Placement::Grid: {
                    const int x = 5 * draw(rng_, 13) + 2;
                    return {x, 5 * draw(rng_, 9) + 2};
                }
                case Placement::Clusters: {
                    const int k = i / perCluster;
                    const int column = k % cellsX, row = k / cellsX;
                    const int x = draw(rng_, c) + g / 2 + cell * column + 4;
                    return {x, draw(rng_, c) + g / 2 + cell * row + 1};
                }
                case Placement::Spiral: {
                    const int r = 6 + 3 * (i / 10);
                    const OutlinePoint o = squareOutline(draw(rng_, 360) + 1, 2 * r);
                    return {34 - r + o.x, 23 - r + o.y};
                }
            }
            return {1, 1};
        };

        auto& systems = out_.galaxy.systems;
        for (int i = 1; i <= count; ++i) {
            bool placed = false;
            for (int attempt = 0; attempt < 1001 && !placed; ++attempt) {
                auto [x, y] = propose(i);
                x = std::clamp(x, 1, kQuadrantWidth);
                y = std::clamp(y, 1, kQuadrantHeight);
                const GalaxyPos p{x - 1, y - 1};
                const bool tooClose = std::any_of(systems.begin(), systems.end(), [&](const StarSystem& o) {
                    return std::abs(o.position.x - p.x) <= minDist && std::abs(o.position.y - p.y) <= minDist;
                });
                if (tooClose) continue;
                StarSystem sys;
                sys.id = SystemId{systems.size()};
                sys.name = names[static_cast<size_t>(i - 1)];
                sys.position = p;
                systems.push_back(std::move(sys));
                placed = true;
            }
            if (!placed) {
                warn(std::format("Only {} of {} systems fit in the quadrant.", systems.size(), count));
                break;
            }
        }
    }

    // ---- 4. The warp network (spec 01 §3.5) ----------------------------------------------------------------
    int bearing(uint32_t from, uint32_t to) {
        int& b = bearings_[static_cast<size_t>(from) * out_.galaxy.systems.size() + to];
        if (b < 0) b = galaxyBearing(out_.galaxy.systems[from].position, out_.galaxy.systems[to].position);
        return b;
    }

    void buildLinks(const ruleset::QuadrantType& q) {
        const auto& systems = out_.galaxy.systems;
        const uint32_t n = static_cast<uint32_t>(systems.size());
        links_.assign(n, {});
        bearings_.assign(static_cast<size_t>(n) * n, -1);
        std::vector<int> dist(static_cast<size_t>(n) * n, 0);
        for (uint32_t i = 0; i < n; ++i)
            for (uint32_t j = 0; j < n; ++j) dist[static_cast<size_t>(i) * n + j] = galaxyDistance(systems[i].position, systems[j].position);
        auto distance = [&](uint32_t a, uint32_t b) { return dist[static_cast<size_t>(a) * n + b]; };
        auto linked = [&](uint32_t a, uint32_t b) { return std::find(links_[a].begin(), links_[a].end(), b) != links_[a].end(); };
        auto link = [&](uint32_t a, uint32_t b) {
            links_[a].push_back(b);
            links_[b].push_back(a);
        };
        auto full = [&](uint32_t a) { return links_[a].size() >= static_cast<size_t>(kMaxWarpPoints); };
        const int minAngle = q.minAngleBetweenWarpPoints;
        // The bearing toward `to` keeps Min Angle from every link `from` already has.
        auto angleOk = [&](uint32_t from, uint32_t to) {
            const int b = bearing(from, to);
            return std::none_of(links_[from].begin(), links_[from].end(),
                                [&](uint32_t other) { return bearingDifference(b, bearing(from, other)) < minAngle; });
        };

        // K nearest systems considered; half as many without "all connected".
        const int k = opt_.allWarpPointsConnected ? q.maxWarpPointsPerSystem : std::max(1, q.maxWarpPointsPerSystem / 2);
        for (uint32_t i = 0; i < n; ++i) {
            std::vector<uint32_t> candidates;
            std::vector<uint8_t> taken(n, 0);
            for (int d = 1; d <= 68; ++d) {
                for (uint32_t j = 0; j < n; ++j)
                    if (j != i && !taken[j] && distance(i, j) == d) {
                        taken[j] = 1;
                        candidates.push_back(j);
                    }
                if (static_cast<int>(candidates.size()) >= k) break;
            }
            if (full(i)) continue;
            for (uint32_t j : candidates) {
                if (full(i)) break;
                if (linked(i, j) || full(j) || !angleOk(i, j) || !angleOk(j, i)) continue;
                link(i, j);
            }
        }

        if (!opt_.allWarpPointsConnected || n < 2) return;
        // Connectivity pass: join every system to the marked part, ignoring the angle rule.
        std::vector<uint8_t> marked(n, 0);
        auto flood = [&](uint32_t from) {
            std::vector<uint32_t> stack{from};
            marked[from] = 1;
            while (!stack.empty()) {
                const uint32_t at = stack.back();
                stack.pop_back();
                for (uint32_t next : links_[at])
                    if (!marked[next]) {
                        marked[next] = 1;
                        stack.push_back(next);
                    }
            }
        };
        flood(static_cast<uint32_t>(draw(rng_, static_cast<int>(n))));
        while (std::find(marked.begin(), marked.end(), 0) != marked.end()) {
            for (uint32_t b = 0; b < n; ++b) {
                if (marked[b]) continue;
                // A system that gets no link is marked without one. The original marks it
                // together with everything linked to it, looks for a partner only up to 68
                // squares away and can loop forever; we mark the system alone, search any
                // distance and always finish (OpenSE4 choice, spec 01 §3.5, §14 Q31).
                if (full(b)) {
                    marked[b] = 1;
                    continue;
                }
                std::optional<uint32_t> best;
                for (uint32_t c = 0; c < n; ++c)
                    if (marked[c] && !full(c) && (!best || distance(b, c) <= distance(b, *best))) best = c;  // ties: highest number
                if (!best) {
                    marked[b] = 1;
                    warn("A system could not be connected to the rest of the quadrant.");
                    continue;
                }
                link(b, *best);
                flood(b);
            }
        }
    }

    // ---- 5a. System type (spec 01 §3.4) -------------------------------------------------------------------
    void rollSystemType(const ruleset::QuadrantType& q, StarSystem& sys) {
        const auto& entries = q.systemTypeChances;
        int64_t total = 0;
        for (const auto& [type, chance] : entries) total += std::max(0, chance);
        uint32_t chosen = 0;  // all chances 0: the first SystemTypes record
        if (total > 0) {
            const int64_t r = rng_.range(1, 1000);
            int64_t passed = 0;
            for (size_t at = 0;; at = (at + 1) % entries.size()) {
                const int64_t chance = std::max(0, entries[at].second);
                if (passed <= r && r < passed + chance) {
                    chosen = lastWithName(static_cast<uint32_t>(entries[at].first.index()));
                    break;
                }
                passed += chance;
            }
        }
        sys.type = ruleset::SystemTypeId{chosen};
        const ruleset::SystemType& t = rs_.systemTypes[chosen];
        sys.physicalType = t.physicalType;
        sys.abilities = t.abilities;
    }

    // The last SystemTypes record with this record's name wins.
    uint32_t lastWithName(uint32_t index) const {
        const std::string& name = rs_.systemTypes[index].name;
        for (uint32_t i = static_cast<uint32_t>(rs_.systemTypes.size()); i-- > 0;)
            if (keysEqual(rs_.systemTypes[i].name, name)) return i;
        return index;
    }

    // ---- 5b. Objects (spec 01 §4.3, §5) -----------------------------------------------------------------
    bool occupied(const StarSystem& sys, Sector s) const {
        return std::any_of(sys.objects.begin(), sys.objects.end(), [&](ObjectId o) { return out_.galaxy.object(o).sector == s; });
    }

    Sector randomSector(const StarSystem& sys) {
        std::vector<Sector> free = emptySectors(out_.galaxy, sys.id);
        if (free.empty()) return Sector{draw(rng_, kSystemSize), draw(rng_, kSystemSize)};
        return free[static_cast<size_t>(draw(rng_, static_cast<int>(free.size())))];
    }

    static std::optional<int> numberAfter(std::string_view s, size_t from) {
        size_t i = from;
        while (i < s.size() && (s[i] < '0' || s[i] > '9')) ++i;
        if (i >= s.size()) return std::nullopt;
        int v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && v < 100000) v = v * 10 + (s[i++] - '0');
        return v;
    }

    static std::optional<size_t> findWord(std::string_view s, std::string_view word) {
        for (size_t i = 0; i + word.size() <= s.size(); ++i)
            if (keysEqual(s.substr(i, word.size()), word)) return i + word.size();
        return std::nullopt;
    }

    // A sector an earlier template entry of this system was placed on, a comet
    // or warp point entry included (spec 01 §4.2, §4.3, §14 Q43).
    bool claimed(Sector s) const { return std::find(claimed_.begin(), claimed_.end(), s) != claimed_.end(); }

    // `recorded`: the sector recorded for each template entry, (0, 0) until it is placed.
    Sector resolvePosition(const StarSystem& sys, const std::string& spec, const std::vector<Sector>& recorded) {
        const Sector center{kSystemCenter, kSystemCenter};
        // Recognised by the word it contains, in this order (confirmed: binary).
        if (auto at = findWord(spec, "Ring")) {
            size_t i = *at;
            while (i < spec.size() && (spec[i] < '0' || spec[i] > '9')) ++i;
            if (i < spec.size()) {
                int k = spec[i] - '0';  // a single digit
                if (k <= 1) return center;
                if (k > 7) {
                    warn(std::format("System '{}': position '{}' is beyond the grid; using Ring 7.", sys.name, spec));
                    k = 7;
                }
                // A side of the square ring k − 1 squares from the centre, then a place along it.
                const int side = 2 * k - 1, low = 7 - k;
                Sector last{low, low};
                for (int n = 0; n < 101; ++n) {
                    const bool horizontal = draw(rng_, 2) == 0;
                    const bool far = draw(rng_, 2) == 1;
                    const int along = low + draw(rng_, side);
                    const int fixed = far ? low + side - 1 : low;
                    last = horizontal ? Sector{along, fixed} : Sector{fixed, along};
                    if (!claimed(last)) break;
                }
                return last;
            }
        } else if (auto atCoord = findWord(spec, "Coord")) {
            const std::string_view rest = std::string_view(spec).substr(*atCoord);
            const size_t comma = rest.find(',');
            if (comma != std::string_view::npos) {
                const auto x = numberAfter(rest.substr(0, comma), 0);
                const auto y = numberAfter(rest.substr(comma + 1), 0);
                if (x && y && Sector(*x, *y).valid()) return Sector{*x, *y};
            }
        } else if (auto atSame = findWord(spec, "Same")) {
            // Entry N's sector, or (0, 0) when it was not placed (yet).
            if (auto n = numberAfter(spec, *atSame); n && *n >= 1 && static_cast<size_t>(*n) <= recorded.size())
                return recorded[static_cast<size_t>(*n - 1)];
            return Sector{0, 0};
        } else if (auto atCircle = findWord(spec, "Circle Radius")) {
            if (auto r = numberAfter(spec, *atCircle)) {
                // Unoccupied sectors whose distance from the centre, truncated, is R.
                std::vector<Sector> ring;
                for (int y = 0; y < kSystemSize; ++y)
                    for (int x = 0; x < kSystemSize; ++x) {
                        const int d2 = (x - kSystemCenter) * (x - kSystemCenter) + (y - kSystemCenter) * (y - kSystemCenter);
                        const Sector s{x, y};
                        if (*r * *r <= d2 && d2 < (*r + 1) * (*r + 1) && !claimed(s)) ring.push_back(s);
                    }
                if (ring.empty()) return Sector{0, 0};
                return ring[static_cast<size_t>(draw(rng_, static_cast<int>(ring.size())))];
            }
        }
        // No defined result in the original (OpenSE4 choice).
        warn(std::format("System '{}': unrecognized position '{}'; placed randomly.", sys.name, spec));
        return randomSector(sys);
    }

    // SectType records for a template object (spec 01 §5.1). With size and
    // atmosphere both Any, every natural record of the kind is a candidate and
    // the other constraints are ignored (a quirk of the original). `relax`
    // drops constraints when nothing matches (OpenSE4 choice).
    std::vector<uint32_t> sectorCandidates(ObjectKind kind, const ruleset::SystemObjectTemplate& t, int relax) const {
        const bool anything = isAny(t.size) && isAny(t.atmosphere);
        std::vector<uint32_t> out;
        for (uint32_t i : naturalSectorTypes(rs_, kind)) {
            const ruleset::SectorObjectType& st = rs_.sectorObjectTypes[i];
            if (!anything) {
                auto ok = [&](int level, std::string_view want, std::string_view have) { return relax >= level || isAny(want) || keysEqual(want, have); };
                bool match = true;
                switch (kind) {
                    case ObjectKind::Planet:
                    case ObjectKind::Asteroids: {
                        const int want = sizeOrdinal(t.size);
                        const bool sizeOk = relax >= 3 || isAny(t.size) || (want > 0 ? recordSize(rs_, kind, st.planetSize) == want : keysEqual(t.size, st.planetSize));
                        match = ok(1, t.atmosphere, st.planetAtmosphere) && (relax >= 2 || isAny(t.composition) || surfaceEqual(t.composition, st.planetPhysicalType)) &&
                                sizeOk;
                        break;
                    }
                    case ObjectKind::Star:
                    case ObjectKind::DestroyedStar:
                        match = ok(1, t.luminosity, st.starLuminosity) && ok(1, t.age, st.starAge) && ok(2, t.color, st.starColor) &&
                                ok(3, t.size, st.starSize);
                        break;
                    case ObjectKind::Storm: match = ok(3, t.size, st.stormSize); break;
                    default: break;
                }
                if (!match) continue;
            }
            out.push_back(i);
        }
        return out;
    }

    // One roll R[1,1000] against the running total of the chances: at most one
    // ability (spec 01 §5.2). Returns true when an ability was granted and kept.
    bool rollStellarAbility(SpaceObject& obj, const std::string& typeName) {
        if (isNone(typeName)) return false;
        const auto id = rs_.findStellarAbilityType(typeName);
        if (!id) return false;  // reported by the ruleset loader
        const ruleset::StellarAbilityType& sat = rs_.stellarAbilityTypes[id->index()];
        const int64_t r = rng_.range(1, 1000);
        int64_t total = 0;
        for (const auto& [chance, ability] : sat.possibleAbilities) {
            total += chance;
            if (total >= r) {
                if (isRuins(ability) && opt_.noRuins) return false;  // No Ruins discards the result, not the roll
                obj.abilities.push_back(ability);
                return true;
            }
        }
        return false;
    }

    // The generated galaxy fills the object list's first slots in creation
    // order (spec 03 §19 Q62).
    ObjectId addObject(StarSystem& sys, SpaceObject obj) {
        obj.id = ObjectId{out_.galaxy.objects.size()};
        obj.slot = static_cast<uint32_t>(obj.id.value);
        obj.system = sys.id;
        sys.objects.push_back(obj.id);
        out_.galaxy.objects.push_back(std::move(obj));
        return out_.galaxy.objects.back().id;
    }

    // The template's entries in order (spec 01 §4.2, §4.3, §5.6, §14 Q43,
    // confirmed: binary). Every entry, a comet or warp point entry included,
    // is placed: its position is drawn with its usual random numbers, its
    // sector is marked for the later Ring and Circle Radius entries and
    // recorded for Same As, and a SectType record is drawn for it. Comet and
    // warp point entries then make nothing and keep an empty name. Each
    // object is named as it is made (nameFor).
    void instantiate(StarSystem& sys) {
        const ruleset::SystemType& type = rs_.systemTypes[sys.type.index()];
        std::vector<Sector> recorded(type.objects.size(), Sector{0, 0});  // the original's list starts at (0, 0)
        std::vector<std::string> names(type.objects.size());
        claimed_.clear();
        numeral_ = beltNumeral_ = 0;
        for (size_t i = 0; i < type.objects.size(); ++i) {
            const ruleset::SystemObjectTemplate& t = type.objects[i];
            const auto kind = parseObjectKind(t.physicalType);
            if (!kind) {
                warn(std::format("System type '{}': unsupported object type '{}'.", type.name, t.physicalType));
                continue;
            }
            SpaceObject obj;
            obj.kind = *kind;
            obj.sector = resolvePosition(sys, t.position, recorded);
            recorded[i] = obj.sector;
            claimed_.push_back(obj.sector);

            if (*kind == ObjectKind::Comet || *kind == ObjectKind::WarpPoint) {
                // The record is drawn as for any entry: one number when there is
                // a candidate, and also with size and atmosphere both Any when
                // there is none (no Comet records exist in stock data).
                const std::vector<uint32_t> candidates = sectorCandidates(*kind, t, 0);
                if (!candidates.empty()) draw(rng_, static_cast<int>(candidates.size()));
                else if (isAny(t.size) && isAny(t.atmosphere)) rng_.next();
                continue;  // nothing is made; the entry keeps an empty name
            }

            std::vector<uint32_t> candidates;
            for (int relax = 0; relax <= 3 && candidates.empty(); ++relax) {
                candidates = sectorCandidates(*kind, t, relax);
                if (relax > 0 && !candidates.empty() && warnedRelax_.insert(type.name).second)
                    warn(std::format("System type '{}': no sector type matches all constraints of a {}; some were relaxed.", type.name,
                                     displayName(*kind)));
            }
            if (candidates.empty()) {
                warn(std::format("No sector type exists for a {} (system type '{}'); object skipped.", displayName(*kind), type.name));
                continue;
            }
            applySectorType(rs_, obj, candidates[static_cast<size_t>(draw(rng_, static_cast<int>(candidates.size())))]);
            rollStellarAbility(obj, t.stellarAbilityType);
            if (obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) rollNaturalValues(rs_, obj, opt_.finiteResources, rng_);
            obj.name = nameFor(sys, obj, i, recorded, names);
            names[i] = obj.name;
            addObject(sys, std::move(obj));
        }
    }

    // Names (spec 01 §5.4, §5.6, confirmed: binary), given as each object is
    // made: stars "System Star", storms "Storm"; asteroid fields "System
    // Asteroid Belt" plus their own numeral, counted apart from the planets'
    // ("Xyz II" beside "Xyz Asteroid Belt I"). A planet counts the template
    // entries recorded in its sector, itself included, where entries not yet
    // placed still count as (0, 0): alone there, it takes the system name and
    // the next numeral; otherwise the name the first entry recorded there had
    // (empty for a comet or warp point entry, or for itself) plus a letter, A
    // for the second, B for the third.
    std::string nameFor(const StarSystem& sys, const SpaceObject& o, size_t index, const std::vector<Sector>& recorded,
                        const std::vector<std::string>& names) {
        switch (o.kind) {
            case ObjectKind::Star:
            case ObjectKind::DestroyedStar: return sys.name + " Star";
            case ObjectKind::Storm: return "Storm";
            case ObjectKind::Asteroids: return std::format("{} Asteroid Belt {}", sys.name, romanNumeral(++beltNumeral_));
            case ObjectKind::Planet: {
                size_t count = 0, first = index;
                for (size_t k = 0; k < recorded.size(); ++k)
                    if (recorded[k] == o.sector) {
                        if (count == 0) first = k;
                        ++count;
                    }
                if (count <= 1) return std::format("{} {}", sys.name, romanNumeral(++numeral_));
                return std::format("{} {}", names[first], static_cast<char>('A' + std::min<size_t>(count - 2, 25)));
            }
            default: return sys.name;
        }
    }

    // ---- 5c. Warp points (spec 01 §3.5) ----------------------------------------------------------------
    std::optional<uint32_t> firstPlainWarpType() const {
        std::optional<uint32_t> any;
        for (uint32_t i = 0; i < rs_.sectorObjectTypes.size(); ++i) {
            if (parseObjectKind(rs_.sectorObjectTypes[i].physicalType) != ObjectKind::WarpPoint) continue;
            if (!rs_.sectorObjectTypes[i].unusual) return i;
            if (!any) any = i;
        }
        return any;
    }

    uint32_t warpType(bool withAbility) {
        std::vector<uint32_t> all;
        for (uint32_t i = 0; i < rs_.sectorObjectTypes.size(); ++i)
            if (parseObjectKind(rs_.sectorObjectTypes[i].physicalType) == ObjectKind::WarpPoint) all.push_back(i);
        if (all.empty()) {
            if (!warnedWarpType_) warn("No 'Warp Point' sector types exist; warp points use sector type 0.");
            warnedWarpType_ = true;
            return 0;
        }
        if (!withAbility) return firstPlainWarpType().value_or(all.front());
        // A random record, redrawn up to 100 times until an Unusual one comes up.
        uint32_t pick = all.front();
        for (int n = 0; n < 100; ++n) {
            pick = all[static_cast<size_t>(draw(rng_, static_cast<int>(all.size())))];
            if (rs_.sectorObjectTypes[pick].unusual) break;
        }
        return pick;
    }

    void placeWarpPoints(StarSystem& sys) {
        const uint32_t i = static_cast<uint32_t>(sys.id.index());
        const ruleset::SystemType& type = rs_.systemTypes[sys.type.index()];
        std::vector<PlacedWarpPoint> here;
        for (uint32_t j : links_[i]) {
            const int a = bearing(i, j);
            SpaceObject wp;
            wp.kind = ObjectKind::WarpPoint;
            wp.name = "Warp Point";  // the destination is added for viewers who explored it
            if (opt_.warpPointsAnywhere) {
                // Up to 4 squares inward from the edge, redrawn until the sector is empty;
                // the original never stops redrawing, we stop after 1,000 draws (OpenSE4 choice).
                wp.sector = warpInwardSector(a, rng_.rangeInt(0, 4));
                for (int n = 0; n < 1000 && occupied(sys, wp.sector); ++n) wp.sector = warpInwardSector(a, rng_.rangeInt(0, 4));
            } else {
                wp.sector = warpEdgeSector(a, here);
            }
            if (j < i) {
                // The far end was made first: both ends share its record and rolled ability.
                const SpaceObject& first = out_.galaxy.object(ends_.at({j, i}));
                wp.sectorType = first.sectorType;
                wp.abilities = first.abilities;
            } else {
                const bool granted = rollStellarAbility(wp, type.warpPointStellarAbilityType);
                wp.sectorType = warpType(granted);
            }
            ends_[{i, j}] = addObject(sys, std::move(wp));
            here.push_back({out_.galaxy.object(ends_[{i, j}]).sector, a});
        }
    }

    const ruleset::Ruleset& rs_;
    const QuadrantOptions& opt_;
    Rng& rng_;
    Generated out_;
    std::vector<std::vector<uint32_t>> links_;             // per system, in the order the links were made
    std::vector<int> bearings_;                            // cache, -1 = not computed
    std::map<std::pair<uint32_t, uint32_t>, ObjectId> ends_;  // (system, destination system) -> warp point
    std::set<std::string> warnedRelax_;
    bool warnedWarpType_ = false;
    std::vector<Sector> claimed_;   // instantiate: the sectors the system's template entries were placed on
    int numeral_ = 0;               // instantiate: the last planet numeral and asteroid belt numeral given
    int beltNumeral_ = 0;
};

bool startEligible(const Galaxy& g, const ruleset::Ruleset& rs, SystemId s) {
    const StarSystem& sys = g.system(s);
    return sys.type.index() < rs.systemTypes.size() && rs.systemTypes[sys.type.index()].empiresCanStartIn;
}

// A new natural planet of an empire's atmosphere and type (spec 01 §3.6),
// named with the numeral one above the number of sectors that hold a planet.
ObjectId createPlanet(Galaxy& g, const ruleset::Ruleset& rs, SystemId sysId, Sector where, std::string_view surface, std::string_view atmosphere,
                      int size, bool finite, Rng& rng) {
    std::vector<uint32_t> types = naturalSectorTypes(rs, ObjectKind::Planet, size, surface, atmosphere);
    if (types.empty()) types = naturalSectorTypes(rs, ObjectKind::Planet, 0, surface, atmosphere);  // (OpenSE4 choice) any size
    if (types.empty()) types = naturalSectorTypes(rs, ObjectKind::Planet);                           // any planet record
    StarSystem& sys = g.system(sysId);
    SpaceObject p;
    p.id = ObjectId{g.objects.size()};
    p.slot = static_cast<uint32_t>(p.id.value);  // made with the galaxy, before any vehicle (spec 03 §19 Q62)
    p.kind = ObjectKind::Planet;
    p.system = sysId;
    p.sector = where;
    if (!types.empty()) applySectorType(rs, p, types[static_cast<size_t>(draw(rng, static_cast<int>(types.size())))]);
    // The empire must be able to live there, whatever the data offers.
    p.surface = std::string(surface);
    p.atmosphere = std::string(atmosphere);
    rollNaturalValues(rs, p, finite, rng);
    // The system name and the numeral one above the number of sectors that
    // hold a planet; asteroid fields do not count (confirmed: binary).
    p.name = std::format("{} {}", sys.name, romanNumeral(sectorsWithPlanet(g, sysId) + 1));
    sys.objects.push_back(p.id);
    g.objects.push_back(std::move(p));
    return g.objects.back().id;
}

// The homeworld at a map's starting point (spec 01 §3.6, §12, confirmed:
// binary): the planet in that sector; one whose atmosphere is not the
// empire's becomes a random natural Planet record of the empire's atmosphere
// and planet type at the same size. Without a planet there, one is created
// as for random placement.
ObjectId homeAtPoint(Galaxy& g, const ruleset::Ruleset& rs, const StartingPoint& point, const EmpireStart& e, int homeSize,
                     const PlacementOptions& options, Rng& rng) {
    for (ObjectId id : g.system(point.system).objects) {
        SpaceObject& obj = g.object(id);
        if (obj.kind != ObjectKind::Planet || obj.sector != point.sector) continue;
        if (!keysEqual(obj.atmosphere, e.atmosphere)) {
            const std::string size = obj.size;
            std::vector<uint32_t> types = naturalSectorTypes(rs, ObjectKind::Planet, stellarSizeOf(rs, obj), e.surface, e.atmosphere);
            if (types.empty()) types = naturalSectorTypes(rs, ObjectKind::Planet, 0, e.surface, e.atmosphere);  // (OpenSE4 choice)
            if (!types.empty()) applySectorType(rs, obj, types[static_cast<size_t>(draw(rng, static_cast<int>(types.size())))]);
            // The size stays; the empire must be able to live there whatever the data offers.
            obj.size = size;
            obj.surface = e.surface;
            obj.atmosphere = e.atmosphere;
        }
        return id;
    }
    return createPlanet(g, rs, point.system, point.sector, e.surface, e.atmosphere, options.allPlanetsSameSize ? homeSize : 0,
                        options.finiteResources, rng);
}

} // namespace

std::string_view displayName(ObjectKind k) {
    static constexpr std::string_view kNames[] = {"Star", "Planet", "Asteroids", "Storm", "Warp Point", "Destroyed Star", "Comet"};
    return kNames[static_cast<size_t>(k)];
}

std::optional<ObjectKind> parseObjectKind(std::string_view t) {
    if (keysEqual(t, "Star") || keysEqual(t, "Sun")) return ObjectKind::Star;
    if (keysEqual(t, "Planet")) return ObjectKind::Planet;
    if (keysEqual(t, "Asteroids") || keysEqual(t, "Asteroid")) return ObjectKind::Asteroids;
    if (keysEqual(t, "Storm")) return ObjectKind::Storm;
    if (keysEqual(t, "Warp Point")) return ObjectKind::WarpPoint;
    if (keysEqual(t, "Destroyed Star")) return ObjectKind::DestroyedStar;
    if (keysEqual(t, "Comet")) return ObjectKind::Comet;
    return std::nullopt;
}

std::vector<ObjectId> Galaxy::warpPoints(SystemId sys) const {
    std::vector<ObjectId> out;
    for (ObjectId id : system(sys).objects)
        if (object(id).kind == ObjectKind::WarpPoint) out.push_back(id);
    return out;
}

std::vector<SystemId> Galaxy::neighbors(SystemId sys) const {
    std::vector<SystemId> out;
    for (ObjectId id : warpPoints(sys))
        if (object(id).destination.valid()) out.push_back(object(object(id).destination).system);
    return out;
}

std::expected<Generated, std::string> generateQuadrant(const ruleset::Ruleset& rs, const QuadrantOptions& options, Rng& rng) {
    return Generator(rs, options, rng).run();
}

int homePlanetSize(const ruleset::Ruleset& rs, HomeValue value, std::string_view surface, std::string_view atmosphere) {
    int size = value == HomeValue::Low ? 2 : value == HomeValue::High ? 4 : 3;  // Small, Medium, Large
    int smallest = std::numeric_limits<int>::max();
    for (uint32_t t : naturalSectorTypes(rs, ObjectKind::Planet, 0, surface, atmosphere)) {
        const int s = recordSize(rs, ObjectKind::Planet, rs.sectorObjectTypes[t].planetSize);
        if (s > 0) smallest = std::min(smallest, s);
    }
    if (smallest != std::numeric_limits<int>::max()) size = std::max(size, smallest);
    return size;
}

std::expected<std::vector<ObjectId>, std::string> placeHomeworlds(Galaxy& galaxy, const ruleset::Ruleset& rs,
                                                                  std::span<const EmpireStart> empires,
                                                                  const PlacementOptions& options, Rng& rng,
                                                                  std::vector<StartingPoint>* heldPoints) {
    if (galaxy.systems.empty()) return std::unexpected("The quadrant has no systems.");
    const int systemCount = static_cast<int>(galaxy.systems.size());
    const int perPlayer = systemCount / std::max<int>(1, static_cast<int>(empires.size()));
    // trunc(0.8 · (S div P)) and trunc(0.5 · (S div P)); the floating-point 0.8
    // lies just above 4/5, so these are exact.
    const std::array<int, 2> farEnough{4 * perPlayer / 5, perPlayer / 2};

    std::vector<ObjectId> homes(empires.size());  // invalid until placed
    std::vector<SystemId> homeSystems;
    std::vector<std::vector<int>> jumpsFrom;
    auto isHome = [&](ObjectId o) { return std::find(homes.begin(), homes.end(), o) != homes.end(); };
    auto isHomeSystem = [&](SystemId s) { return std::find(homeSystems.begin(), homeSystems.end(), s) != homeSystems.end(); };
    auto settle = [&](size_t player, ObjectId home) {
        homes[player] = home;
        homeSystems.push_back(galaxy.object(home).system);
        jumpsFrom.push_back(warpJumps(galaxy, homeSystems.back()));
    };

    // A map's starting points come first (confirmed: binary): each empire, in
    // player order, takes its specific point (the last one listed for it), else
    // a random remaining common point, which is then used up. The original does
    // not check whether an earlier empire took the same sector; we skip such a
    // point (OpenSE4 choice, spec 01 §14 Q36).
    auto onMap = [&](const StartingPoint& p) { return p.system.valid() && p.system.index() < galaxy.systems.size() && p.sector.valid(); };
    std::vector<std::optional<StartingPoint>> point(empires.size());
    std::vector<StartingPoint> common, usedCommon;
    for (const StartingPoint& p : options.startingPoints) {
        if (!onMap(p)) continue;
        if (p.player == kCommonStart) common.push_back(p);
        else if (p.player >= 0 && static_cast<size_t>(p.player) < empires.size()) point[static_cast<size_t>(p.player)] = p;
    }
    // The points the game keeps for Save Map (spec 01 §12): every specific
    // point, and the common points no player takes (worked out below).
    if (heldPoints) {
        heldPoints->clear();
        for (const StartingPoint& p : options.startingPoints)
            if (onMap(p)) heldPoints->push_back(p);
    }
    std::vector<Location> taken;
    auto unclaimed = [&](const StartingPoint& p) { return std::find(taken.begin(), taken.end(), Location{p.system, p.sector}) == taken.end(); };
    for (size_t i = 0; i < empires.size(); ++i) {
        if (point[i] && !unclaimed(*point[i])) point[i].reset();
        std::vector<StartingPoint> open;
        for (const StartingPoint& p : common)
            if (unclaimed(p)) open.push_back(p);
        if (!point[i] && !open.empty()) {
            const StartingPoint pick = open[static_cast<size_t>(draw(rng, static_cast<int>(open.size())))];
            point[i] = pick;
            usedCommon.push_back(pick);
            common.erase(std::find(common.begin(), common.end(), pick));
        }
        if (!point[i]) continue;
        taken.push_back({point[i]->system, point[i]->sector});
        const EmpireStart& e = empires[i];
        settle(i, homeAtPoint(galaxy, rs, *point[i], e, homePlanetSize(rs, options.homeValue, e.surface, e.atmosphere), options, rng));
    }
    if (heldPoints)
        for (const StartingPoint& used : usedCommon)
            if (auto it = std::find(heldPoints->begin(), heldPoints->end(), used); it != heldPoints->end()) heldPoints->erase(it);

    for (size_t player = 0; player < empires.size(); ++player) {
        if (homes[player].valid()) continue;
        const EmpireStart& e = empires[player];
        const int homeSize = homePlanetSize(rs, options.homeValue, e.surface, e.atmosphere);
        std::vector<ObjectId> pool;
        for (int attempt = 1; attempt <= 3 && pool.empty(); ++attempt) {
            for (const StarSystem& sys : galaxy.systems) {
                if (!startEligible(galaxy, rs, sys.id)) continue;
                if (!options.allowSameSystem && isHomeSystem(sys.id)) continue;
                if (attempt < 3 && options.evenlyDistributed) {
                    const int limit = farEnough[static_cast<size_t>(attempt - 1)];
                    const bool spread = std::all_of(jumpsFrom.begin(), jumpsFrom.end(), [&](const std::vector<int>& j) {
                        const int d = j[sys.id.index()];
                        return d < 0 || d > limit;  // no warp path counts as very far
                    });
                    if (!spread) continue;
                }
                for (ObjectId id : sys.objects) {
                    const SpaceObject& o = galaxy.object(id);
                    if (o.kind != ObjectKind::Planet || !keysEqual(o.atmosphere, e.atmosphere) || !surfaceEqual(o.surface, e.surface)) continue;
                    if (isHome(id)) continue;
                    if ((attempt < 3 || options.allPlanetsSameSize) && stellarSizeOf(rs, o) != homeSize) continue;
                    pool.push_back(id);
                }
            }
        }
        ObjectId home;
        if (!pool.empty()) {
            home = pool[static_cast<size_t>(draw(rng, static_cast<int>(pool.size())))];
        } else {
            // Nothing fits: a new homeworld in a random start-eligible system.
            std::optional<SystemId> target;
            for (int n = 0; n < 2000 && !target; ++n) {
                const SystemId s{static_cast<uint32_t>(draw(rng, systemCount))};
                if (startEligible(galaxy, rs, s) && !isHomeSystem(s)) target = s;
            }
            if (!target) target = SystemId{static_cast<uint32_t>(draw(rng, systemCount))};
            // Any sector without a planet: a star, storm, warp point or asteroid
            // field may be there (confirmed: binary). The original redraws until it
            // finds one; drawing from the list gives the same distribution.
            const auto planets = firstPlanetPerSector(galaxy, *target);
            std::vector<Sector> free;
            for (int n = 0; n < kSystemSize * kSystemSize; ++n)
                if (!planets[static_cast<size_t>(n)]) free.push_back(Sector{n % kSystemSize, n / kSystemSize});
            const Sector where = free.empty() ? Sector{draw(rng, kSystemSize), draw(rng, kSystemSize)}  // (OpenSE4 choice) never happens
                                              : free[static_cast<size_t>(draw(rng, static_cast<int>(free.size())))];
            home = createPlanet(galaxy, rs, *target, where, e.surface, e.atmosphere, options.allPlanetsSameSize ? homeSize : 0,
                                options.finiteResources, rng);
        }
        settle(player, home);
    }
    return homes;
}

ObjectId createStartingPlanet(Galaxy& g, const ruleset::Ruleset& rs, SystemId sys, Sector where, std::string_view surface,
                              std::string_view atmosphere, int size, bool finiteResources, Rng& rng) {
    return createPlanet(g, rs, sys, where, surface, atmosphere, size, finiteResources, rng);
}

} // namespace opense4::game
