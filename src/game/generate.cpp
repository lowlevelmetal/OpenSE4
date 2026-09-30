#include "game/generate.hpp"

#include "datafile/datafile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>
#include <set>

namespace opense4::game {

namespace {

using datafile::keysEqual;
constexpr double kPi = std::numbers::pi;

bool isAny(std::string_view s) { return s.empty() || keysEqual(s, "Any"); }
bool isNone(std::string_view s) { return s.empty() || keysEqual(s, "None"); }

// "Gas" and "Gas Giant" name the same planet type.
bool surfaceEqual(std::string_view a, std::string_view b) {
    auto norm = [](std::string_view s) { return keysEqual(s, "Gas") ? std::string_view("Gas Giant") : s; };
    return keysEqual(norm(a), norm(b));
}

std::string roman(int n) {
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

int chebyshev(GalaxyPos a, GalaxyPos b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
double euclid(GalaxyPos a, GalaxyPos b) { return std::hypot(double(a.x - b.x), double(a.y - b.y)); }

double gaussian(Rng& rng) {
    const double u1 = std::max(double(rng.unit()), 1e-7);
    const double u2 = double(rng.unit());
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
}

// Weighted pick among (weight) entries; weights are "tenths of a percent" but
// need not sum to 1000.
template <class Weights>
std::optional<size_t> weightedIndex(const Weights& weights, Rng& rng) {
    int64_t total = 0;
    for (int w : weights) total += std::max(0, w);
    if (total <= 0) return std::nullopt;
    int64_t roll = rng.range(0, total - 1);
    for (size_t i = 0; i < weights.size(); ++i) {
        roll -= std::max(0, weights[i]);
        if (roll < 0) return i;
    }
    return weights.size() - 1;
}

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
        if (quadrant->systemTypeChances.empty()) return std::unexpected(std::format("Quadrant type '{}' lists no system types.", quadrant->name));
        const int64_t maxSystems = std::min<int64_t>(255, rs_.settings.integer("Maximum Number Of Systems", 255));
        if (opt_.systemCount < 1 || opt_.systemCount > maxSystems)
            return std::unexpected(std::format("The number of systems must be between 1 and {}.", maxSystems));

        out_.galaxy.quadrantType = quadrant->name;
        placeSystems(*quadrant);
        chooseSystemTypes(*quadrant);
        for (StarSystem& sys : out_.galaxy.systems) instantiate(sys);
        if (!opt_.noWarpPoints) buildWarpNetwork(*quadrant);
        nameObjects();
        return std::move(out_);
    }

private:
    void warn(std::string message) { out_.warnings.push_back(std::move(message)); }

    // ---- system placement --------------------------------------------------------
    void placeSystems(const ruleset::QuadrantType& q) {
        const int count = opt_.systemCount;
        const int spacing = std::max(1, q.minDistanceBetweenSystems + 1);  // Chebyshev distance between systems
        // Grid sized so the systems fill roughly a third of the available room.
        const double area = double(count) * spacing * spacing * 3.0;
        int width = std::max(8, int(std::ceil(std::sqrt(area * 4.0 / 3.0))));
        int height = std::max(6, int(std::ceil(width * 0.75)));

        for (int attempt = 0; attempt < 30; ++attempt) {
            std::vector<GalaxyPos> points = placeOnce(q.systemPlacement, count, spacing, width, height);
            if (static_cast<int>(points.size()) == count) {
                out_.galaxy.width = width;
                out_.galaxy.height = height;
                for (size_t i = 0; i < points.size(); ++i) {
                    StarSystem sys;
                    sys.id = SystemId{i};
                    sys.position = points[i];
                    out_.galaxy.systems.push_back(std::move(sys));
                }
                return;
            }
            width = width * 11 / 10 + 1;  // too crowded for this layout: grow and retry
            height = height * 11 / 10 + 1;
        }
        warn("System placement could not honor the minimum distance; systems may be closer than requested.");
        out_.galaxy.width = width;
        out_.galaxy.height = height;
        for (int i = 0; i < count; ++i) {
            StarSystem sys;
            sys.id = SystemId{i};
            sys.position = {int(rng_.below(uint64_t(width))), int(rng_.below(uint64_t(height)))};
            out_.galaxy.systems.push_back(std::move(sys));
        }
    }

    std::vector<GalaxyPos> placeOnce(const std::string& placement, int count, int spacing, int w, int h) {
        std::vector<GalaxyPos> pts;
        auto fits = [&](GalaxyPos p, int minDist) {
            if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return false;
            return std::none_of(pts.begin(), pts.end(), [&](GalaxyPos q) { return chebyshev(p, q) < minDist; });
        };
        auto sample = [&](auto&& candidate, int minDist) {
            for (int tries = 0; tries < 4000 && static_cast<int>(pts.size()) < count; ++tries) {
                const GalaxyPos p = candidate();
                if (fits(p, minDist)) pts.push_back(p);
            }
        };
        const GalaxyPos center{w / 2, h / 2};

        if (keysEqual(placement, "Grid")) {
            // A regular lattice covering the quadrant; take `count` of its points.
            const int step = std::max(spacing, int(std::floor(std::sqrt(double(w) * h / count))));
            std::vector<GalaxyPos> lattice;
            for (int y = step / 2; y < h; y += step)
                for (int x = step / 2; x < w; x += step) lattice.push_back({x, y});
            if (static_cast<int>(lattice.size()) < count) return {};
            rng_.shuffle(lattice);
            lattice.resize(size_t(count));
            std::sort(lattice.begin(), lattice.end());
            return lattice;
        }
        if (keysEqual(placement, "Spiral")) {
            const int arms = 2 + int(rng_.below(3));
            const double radius = std::min(w, h) / 2.0 - 1.0;
            const double offset = rng_.unit() * 2.0 * kPi;
            sample(
                [&] {
                    const double t = rng_.unit();
                    const int arm = int(rng_.below(uint64_t(arms)));
                    const double angle = offset + arm * 2.0 * kPi / arms + t * 3.0 * kPi;
                    const double r = radius * (0.08 + 0.92 * t);
                    return GalaxyPos{center.x + int(std::lround(std::cos(angle) * r + gaussian(rng_) * spacing)),
                                     center.y + int(std::lround(std::sin(angle) * r * 0.8 + gaussian(rng_) * spacing))};
                },
                spacing);
            return pts;
        }
        if (keysEqual(placement, "Clusters")) {
            const int clusters = std::max(2, count / 8);
            std::vector<GalaxyPos> centers;
            for (int tries = 0; tries < 2000 && static_cast<int>(centers.size()) < clusters; ++tries) {
                const GalaxyPos c{int(rng_.below(uint64_t(w))), int(rng_.below(uint64_t(h)))};
                if (std::all_of(centers.begin(), centers.end(), [&](GalaxyPos o) { return chebyshev(c, o) >= spacing * 5; }))
                    centers.push_back(c);
            }
            if (centers.empty()) centers.push_back(center);
            sample(
                [&] {
                    const GalaxyPos c = centers[rng_.below(centers.size())];
                    return GalaxyPos{c.x + int(std::lround(gaussian(rng_) * spacing * 1.6)),
                                     c.y + int(std::lround(gaussian(rng_) * spacing * 1.6))};
                },
                spacing);
            return pts;
        }
        const auto uniform = [&] { return GalaxyPos{int(rng_.below(uint64_t(w))), int(rng_.below(uint64_t(h)))}; };
        if (keysEqual(placement, "Diffuse")) {
            // Poisson-disc style: prefer twice the spacing, fall back to the minimum.
            sample(uniform, spacing * 2);
            sample(uniform, spacing);
            return pts;
        }
        if (!keysEqual(placement, "Random") && !warnedPlacement_) {
            warn(std::format("Unknown system placement '{}'; using Random.", placement));
            warnedPlacement_ = true;
        }
        sample(uniform, spacing);
        return pts;
    }

    // ---- system types and names ----------------------------------------------------
    void chooseSystemTypes(const ruleset::QuadrantType& q) {
        std::vector<int> weights;
        for (const auto& [type, chance] : q.systemTypeChances) weights.push_back(chance);
        std::vector<std::string> names = rs_.names.systemNames;
        rng_.shuffle(names);
        int fallback = 0;
        for (StarSystem& sys : out_.galaxy.systems) {
            const size_t pick = weightedIndex(weights, rng_).value_or(0);
            sys.type = q.systemTypeChances[pick].first;
            const ruleset::SystemType& t = rs_.systemTypes[sys.type.index()];
            sys.physicalType = t.physicalType;
            sys.abilities = t.abilities;
            if (!names.empty()) {
                sys.name = names.back();
                names.pop_back();
            } else {
                sys.name = std::format("System {}", ++fallback);
            }
        }
    }

    // ---- objects inside a system --------------------------------------------------------
    std::vector<Sector> occupied(const StarSystem& sys) const {
        std::vector<Sector> out;
        for (ObjectId id : sys.objects) out.push_back(out_.galaxy.object(id).sector);
        return out;
    }

    // Random sector satisfying `pred`, preferring free ones.
    std::optional<Sector> pickSector(const StarSystem& sys, auto&& pred) {
        const std::vector<Sector> taken = occupied(sys);
        std::vector<Sector> free, any;
        for (int y = 0; y < kSystemSize; ++y)
            for (int x = 0; x < kSystemSize; ++x) {
                const Sector s{x, y};
                if (!pred(s)) continue;
                any.push_back(s);
                if (std::find(taken.begin(), taken.end(), s) == taken.end()) free.push_back(s);
            }
        const auto& pool = free.empty() ? any : free;
        if (pool.empty()) return std::nullopt;
        return pool[rng_.below(pool.size())];
    }

    Sector resolvePosition(const StarSystem& sys, const std::string& spec, const std::vector<Sector>& placedByIndex) {
        const Sector center{kSystemCenter, kSystemCenter};
        auto number = [](std::string_view s) -> std::optional<int> {
            while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
            if (auto v = datafile::parseInteger(s)) return int(*v);
            return std::nullopt;
        };
        auto after = [&](std::string_view prefix) -> std::optional<std::string_view> {
            if (spec.size() >= prefix.size() && keysEqual(std::string_view(spec).substr(0, prefix.size()), prefix))
                return std::string_view(spec).substr(prefix.size());
            return std::nullopt;
        };
        if (auto rest = after("Ring")) {
            if (auto k = number(*rest)) {
                if (*k <= 1) return center;
                const int d = std::min(*k - 1, kSystemCenter);
                if (auto found = pickSector(sys, [&](Sector s) { return chebyshev(s, center) == d; })) return *found;
            }
        } else if (auto rest2 = after("Circle Radius")) {
            if (auto r = number(*rest2)) {
                const auto pred = [&](Sector s) {
                    return std::lround(std::hypot(double(s.x - center.x), double(s.y - center.y))) == *r;
                };
                if (auto s = pickSector(sys, pred)) return *s;
            }
        } else if (auto rest3 = after("Coord")) {
            const std::string_view r = *rest3;
            const size_t comma = r.find(',');
            if (comma != std::string_view::npos) {
                const auto x = number(r.substr(0, comma));
                const auto y = number(r.substr(comma + 1));
                if (x && y && Sector(*x, *y).valid()) return Sector{*x, *y};
            }
        } else if (auto rest4 = after("Same As")) {
            if (auto n = number(*rest4); n && *n >= 1 && *n <= static_cast<int>(placedByIndex.size())) return placedByIndex[size_t(*n - 1)];
        }
        warn(std::format("System type of '{}': unrecognized position '{}'; placed randomly.", sys.name, spec));
        return pickSector(sys, [&](Sector s) { return s != center; }).value_or(center);
    }

    // SectType records of a kind that satisfy the template's constraints.
    std::vector<uint32_t> sectorCandidates(ObjectKind kind, const ruleset::SystemObjectTemplate& t, int relax) const {
        std::vector<uint32_t> out;
        for (uint32_t i = 0; i < rs_.sectorObjectTypes.size(); ++i) {
            const ruleset::SectorObjectType& st = rs_.sectorObjectTypes[i];
            if (parseObjectKind(st.physicalType) != kind) continue;
            auto ok = [&](int level, std::string_view want, std::string_view have, bool surface = false) {
                if (relax >= level || isAny(want)) return true;
                return surface ? surfaceEqual(want, have) : keysEqual(want, have);
            };
            switch (kind) {
                case ObjectKind::Planet:
                case ObjectKind::Asteroids: {
                    const bool constructed = std::any_of(rs_.planetSizes.begin(), rs_.planetSizes.end(), [&](const auto& ps) {
                        return ps.constructed && keysEqual(ps.name, st.planetSize);
                    });
                    if (constructed) continue;  // ringworlds etc. are built, never generated
                    if (!ok(1, t.atmosphere, st.planetAtmosphere) || !ok(2, t.composition, st.planetPhysicalType, true) ||
                        !ok(3, t.size, st.planetSize))
                        continue;
                    break;
                }
                case ObjectKind::Star:
                case ObjectKind::DestroyedStar:
                    if (!ok(1, t.luminosity, st.starLuminosity) || !ok(1, t.age, st.starAge) || !ok(2, t.color, st.starColor) ||
                        !ok(3, t.size, st.starSize))
                        continue;
                    break;
                case ObjectKind::Storm:
                    if (!ok(3, t.size, st.stormSize)) continue;
                    break;
                default: break;
            }
            out.push_back(i);
        }
        return out;
    }

    void rollStellarAbility(SpaceObject& obj, const std::string& typeName) {
        if (isNone(typeName)) return;
        const auto id = rs_.findStellarAbilityType(typeName);
        if (!id) return;  // reported by the ruleset loader
        const ruleset::StellarAbilityType& sat = rs_.stellarAbilityTypes[id->index()];
        // One roll in [0,1000): at most one ability per object (spec 01 §5.2).
        int64_t roll = rng_.range(0, 999);
        for (const auto& [chance, ability] : sat.possibleAbilities) {
            roll -= chance;
            if (roll < 0) {
                const bool ruins = keysEqual(ability.type, "Ancient Ruins") || keysEqual(ability.type, "Ancient Ruins Unique");
                if (!(ruins && opt_.noRuins)) obj.abilities.push_back(ability);
                return;
            }
        }
    }

    void rollPlanetValues(SpaceObject& obj) {
        const bool asteroids = obj.kind == ObjectKind::Asteroids;
        const std::string prefix = asteroids ? "Asteroids Value" : "Planet Value";
        const std::string unit = opt_.finiteResources ? "Resources" : "Percent";
        const int64_t lo = rs_.settings.integer(std::format("{} Low {}", prefix, unit), opt_.finiteResources ? 5000 : 50);
        const int64_t hi = rs_.settings.integer(std::format("{} High {}", prefix, unit), opt_.finiteResources ? 50000 : 150);
        for (int& v : obj.value) v = int(rng_.range(std::min(lo, hi), std::max(lo, hi)));
        obj.conditions = int(rng_.range(0, 100));  // distribution unknown (spec 01 §14 Q15)
    }

    void applySectorType(SpaceObject& obj, uint32_t index) {
        const ruleset::SectorObjectType& st = rs_.sectorObjectTypes[index];
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

    ObjectId addObject(StarSystem& sys, SpaceObject obj) {
        obj.id = ObjectId{out_.galaxy.objects.size()};
        obj.system = sys.id;
        sys.objects.push_back(obj.id);
        out_.galaxy.objects.push_back(std::move(obj));
        return out_.galaxy.objects.back().id;
    }

    void instantiate(StarSystem& sys) {
        const ruleset::SystemType& type = rs_.systemTypes[sys.type.index()];
        std::vector<Sector> placed;
        for (const ruleset::SystemObjectTemplate& t : type.objects) {
            const auto kind = parseObjectKind(t.physicalType);
            if (!kind || *kind == ObjectKind::WarpPoint) {
                warn(std::format("System type '{}': unsupported object type '{}'.", type.name, t.physicalType));
                placed.push_back(Sector{});
                continue;
            }
            SpaceObject obj;
            obj.kind = *kind;
            obj.sector = resolvePosition(sys, t.position, placed);
            placed.push_back(obj.sector);

            std::vector<uint32_t> candidates;
            for (int relax = 0; relax <= 4 && candidates.empty(); ++relax) {
                candidates = sectorCandidates(*kind, t, relax);
                if (relax > 0 && !candidates.empty() && !warnedRelax_.contains(type.name)) {
                    warn(std::format("System type '{}': no sector type matches all constraints of a {}; some were relaxed.",
                                     type.name, displayName(*kind)));
                    warnedRelax_.insert(type.name);
                }
            }
            if (candidates.empty()) {
                warn(std::format("No sector type exists for a {} (system type '{}'); object skipped.", displayName(*kind), type.name));
                continue;
            }
            applySectorType(obj, candidates[rng_.below(candidates.size())]);
            rollStellarAbility(obj, t.stellarAbilityType);
            if (obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) rollPlanetValues(obj);
            addObject(sys, std::move(obj));
        }
    }

    // ---- warp network -------------------------------------------------------------------
    struct Edge {
        uint32_t a, b;
        double length;
    };

    static double bearing(GalaxyPos from, GalaxyPos to) {
        return std::atan2(double(to.y - from.y), double(to.x - from.x)) * 180.0 / kPi;
    }
    static double angleBetween(double a, double b) {
        double d = std::fmod(std::abs(a - b), 360.0);
        return d > 180.0 ? 360.0 - d : d;
    }

    void buildWarpNetwork(const ruleset::QuadrantType& q) {
        auto& systems = out_.galaxy.systems;
        const size_t n = systems.size();
        const int cap = q.maxWarpPointsPerSystem > 0 ? q.maxWarpPointsPerSystem : 99;
        const double minAngle = q.minAngleBetweenWarpPoints;

        // Candidate links: each system's nearest neighbours, shortest first.
        std::set<std::pair<uint32_t, uint32_t>> seen;
        std::vector<Edge> candidates;
        for (uint32_t i = 0; i < n; ++i) {
            std::vector<uint32_t> order;
            for (uint32_t j = 0; j < n; ++j)
                if (j != i) order.push_back(j);
            std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
                const double dx = euclid(systems[i].position, systems[x].position);
                const double dy = euclid(systems[i].position, systems[y].position);
                return dx != dy ? dx < dy : x < y;
            });
            for (size_t k = 0; k < std::min<size_t>(6, order.size()); ++k) {
                const auto key = std::minmax(i, order[k]);
                if (seen.insert(key).second) candidates.push_back({key.first, key.second, euclid(systems[key.first].position, systems[key.second].position)});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Edge& x, const Edge& y) { return x.length < y.length; });

        std::vector<std::vector<uint32_t>> links(n);
        auto angleOk = [&](uint32_t from, uint32_t to) {
            const double b = bearing(systems[from].position, systems[to].position);
            return std::all_of(links[from].begin(), links[from].end(), [&](uint32_t other) {
                return angleBetween(b, bearing(systems[from].position, systems[other].position)) >= minAngle;
            });
        };
        std::vector<Edge> accepted;
        for (const Edge& e : candidates) {
            if (links[e.a].size() >= size_t(cap) || links[e.b].size() >= size_t(cap)) continue;
            if (!angleOk(e.a, e.b) || !angleOk(e.b, e.a)) continue;
            links[e.a].push_back(e.b);
            links[e.b].push_back(e.a);
            accepted.push_back(e);
        }

        if (opt_.allWarpPointsConnected && n > 1) {
            // Join components with their shortest connecting links, ignoring caps.
            std::vector<uint32_t> parent(n);
            for (uint32_t i = 0; i < n; ++i) parent[i] = i;
            std::function<uint32_t(uint32_t)> root = [&](uint32_t x) { return parent[x] == x ? x : parent[x] = root(parent[x]); };
            for (const Edge& e : accepted) parent[root(e.a)] = root(e.b);
            for (;;) {
                std::optional<Edge> best;
                for (uint32_t i = 0; i < n; ++i)
                    for (uint32_t j = i + 1; j < n; ++j) {
                        if (root(i) == root(j)) continue;
                        const double d = euclid(systems[i].position, systems[j].position);
                        if (!best || d < best->length) best = Edge{i, j, d};
                    }
                if (!best) break;
                parent[root(best->a)] = root(best->b);
                links[best->a].push_back(best->b);
                links[best->b].push_back(best->a);
                accepted.push_back(*best);
            }
        }

        // Natural warp points use the ordinary (not "Unusual") warp point art.
        std::vector<uint32_t> warpTypes, unusualTypes;
        for (uint32_t i = 0; i < rs_.sectorObjectTypes.size(); ++i) {
            const auto& st = rs_.sectorObjectTypes[i];
            if (parseObjectKind(st.physicalType) != ObjectKind::WarpPoint) continue;
            (st.unusual ? unusualTypes : warpTypes).push_back(i);
        }
        if (warpTypes.empty()) warpTypes = unusualTypes;
        if (warpTypes.empty()) {
            warn("No 'Warp Point' sector types exist; warp points use sector type 0.");
            warpTypes.push_back(0);
        }

        for (const Edge& e : accepted) {
            const ObjectId wa = addWarpPoint(systems[e.a], systems[e.b].position, warpTypes);
            const ObjectId wb = addWarpPoint(systems[e.b], systems[e.a].position, warpTypes);
            out_.galaxy.object(wa).destination = wb;
            out_.galaxy.object(wb).destination = wa;
        }
    }

    ObjectId addWarpPoint(StarSystem& sys, GalaxyPos toward, const std::vector<uint32_t>& types) {
        const Sector center{kSystemCenter, kSystemCenter};
        SpaceObject wp;
        wp.kind = ObjectKind::WarpPoint;
        if (opt_.warpPointsAnywhere) {
            wp.sector = pickSector(sys, [&](Sector s) { return s != center; }).value_or(Sector{0, 0});
        } else {
            // On the outer edge, in the direction of the destination.
            const double dx = toward.x - sys.position.x;
            const double dy = toward.y - sys.position.y;
            const double m = std::max(std::abs(dx), std::abs(dy));
            const double ix = center.x + (m > 0 ? dx / m : 1.0) * kSystemCenter;
            const double iy = center.y + (m > 0 ? dy / m : 0.0) * kSystemCenter;
            const std::vector<Sector> taken = occupied(sys);
            std::optional<Sector> best;
            double bestDist = 1e9;
            for (int y = 0; y < kSystemSize; ++y)
                for (int x = 0; x < kSystemSize; ++x) {
                    const Sector s{x, y};
                    if (chebyshev(s, center) != kSystemCenter) continue;  // outer ring only
                    if (std::find(taken.begin(), taken.end(), s) != taken.end()) continue;
                    const double d = std::hypot(x - ix, y - iy);
                    if (d < bestDist) {
                        bestDist = d;
                        best = s;
                    }
                }
            wp.sector = best.value_or(Sector{int(std::lround(ix)), int(std::lround(iy))});
        }
        applySectorType(wp, types[rng_.below(types.size())]);
        wp.oneWay = rs_.sectorObjectTypes[wp.sectorType].warpPointOneWay;
        rollStellarAbility(wp, rs_.systemTypes[sys.type.index()].warpPointStellarAbilityType);
        return addObject(sys, std::move(wp));
    }

    // ---- names --------------------------------------------------------------------------
    void nameObjects() {
        for (StarSystem& sys : out_.galaxy.systems) {
            int stars = 0, planets = 0, asteroids = 0, storms = 0, warps = 0;
            for (ObjectId id : sys.objects) {
                switch (out_.galaxy.object(id).kind) {
                    case ObjectKind::Star:
                    case ObjectKind::DestroyedStar: ++stars; break;
                    case ObjectKind::Asteroids: ++asteroids; break;
                    default: break;
                }
            }
            const int starCount = stars, asteroidCount = asteroids;
            stars = asteroids = 0;
            for (ObjectId id : sys.objects) {
                SpaceObject& o = out_.galaxy.object(id);
                switch (o.kind) {
                    case ObjectKind::Star:
                    case ObjectKind::DestroyedStar:
                        o.name = starCount > 1 ? std::format("{} {}", sys.name, char('A' + stars++)) : sys.name;
                        break;
                    case ObjectKind::Planet: o.name = std::format("{} {}", sys.name, roman(++planets)); break;
                    case ObjectKind::Asteroids:
                        o.name = asteroidCount > 1 ? std::format("{} Asteroids {}", sys.name, roman(++asteroids)) : sys.name + " Asteroids";
                        break;
                    case ObjectKind::Storm: o.name = std::format("{} Storm {}", sys.name, roman(++storms)); break;
                    case ObjectKind::WarpPoint: o.name = std::format("{} Warp Point {}", sys.name, ++warps); break;
                    default: o.name = sys.name; break;
                }
            }
        }
    }

    const ruleset::Ruleset& rs_;
    const QuadrantOptions& opt_;
    Rng& rng_;
    Generated out_;
    bool warnedPlacement_ = false;
    std::set<std::string> warnedRelax_;
};

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

std::expected<std::vector<ObjectId>, std::string> placeHomeworlds(Galaxy& galaxy, const ruleset::Ruleset& rs,
                                                                  std::span<const EmpireStart> empires,
                                                                  const PlacementOptions& options, Rng& rng) {
    std::vector<SystemId> eligible;
    for (const StarSystem& sys : galaxy.systems)
        if (rs.systemTypes[sys.type.index()].empiresCanStartIn) eligible.push_back(sys.id);
    if (eligible.empty()) return std::unexpected("No system in this quadrant can host a homeworld.");
    if (!options.allowSameSystem && eligible.size() < empires.size())
        return std::unexpected(std::format("Only {} systems can host a homeworld, but {} empires need one.", eligible.size(), empires.size()));

    // Home systems: farthest-point selection when evenly distributed.
    std::vector<SystemId> homes;
    for (size_t i = 0; i < empires.size(); ++i) {
        std::vector<SystemId> pool;
        for (SystemId s : eligible)
            if (options.allowSameSystem || std::find(homes.begin(), homes.end(), s) == homes.end()) pool.push_back(s);
        if (homes.empty() || !options.evenlyDistributed) {
            homes.push_back(pool[rng.below(pool.size())]);
            continue;
        }
        double bestDist = -1.0;
        std::vector<SystemId> ties;
        for (SystemId s : pool) {
            double nearest = 1e18;
            for (SystemId h : homes) nearest = std::min(nearest, euclid(galaxy.system(s).position, galaxy.system(h).position));
            if (nearest > bestDist + 1e-9) {
                bestDist = nearest;
                ties.clear();
            }
            if (std::abs(nearest - bestDist) <= 1e-9) ties.push_back(s);
        }
        homes.push_back(ties[rng.below(ties.size())]);
    }

    const char* level = options.homeValue == HomeValue::Low ? "Low" : options.homeValue == HomeValue::High ? "High" : "Medium";
    const int homeValue = int(rs.settings.integer(std::format("Plr Planet Value {} {}", level, options.finiteResources ? "Resources" : "Percent"),
                                                  options.finiteResources ? 20000 : 100));

    std::vector<ObjectId> homeworlds;
    for (size_t i = 0; i < empires.size(); ++i) {
        StarSystem& sys = galaxy.system(homes[i]);
        const EmpireStart& e = empires[i];
        // Prefer a planet of the native type, then any planet not already a
        // homeworld; among those, the one whose size is closest to Medium, the
        // size of the observed stock homeworld (inferred, spec 01 §3.6).
        auto sizeDistance = [](const std::string& size) {
            static constexpr std::array<std::string_view, 5> kOrder{"Tiny", "Small", "Medium", "Large", "Huge"};
            for (size_t k = 0; k < kOrder.size(); ++k)
                if (keysEqual(size, kOrder[k])) return k > 2 ? int(k) - 2 : 2 - int(k);
            return 9;  // constructed or unknown sizes last
        };
        std::optional<ObjectId> chosen;
        for (int pass = 0; pass < 2 && !chosen; ++pass)
            for (ObjectId id : sys.objects) {
                const SpaceObject& o = galaxy.object(id);
                if (o.kind != ObjectKind::Planet || std::find(homeworlds.begin(), homeworlds.end(), id) != homeworlds.end()) continue;
                if (pass == 0 && !surfaceEqual(o.surface, e.surface)) continue;
                if (!chosen || sizeDistance(o.size) < sizeDistance(galaxy.object(*chosen).size)) chosen = id;
            }
        if (!chosen) {
            // Start-eligible but planetless (some stock layouts): create one.
            SpaceObject p;
            p.id = ObjectId{galaxy.objects.size()};
            p.kind = ObjectKind::Planet;
            p.system = sys.id;
            std::vector<Sector> free;
            for (int y = 0; y < kSystemSize; ++y)
                for (int x = 0; x < kSystemSize; ++x) {
                    const Sector s{x, y};
                    const int d = chebyshev(s, Sector{});
                    const bool taken = std::any_of(sys.objects.begin(), sys.objects.end(), [&](ObjectId o) { return galaxy.object(o).sector == s; });
                    if (d >= 2 && d <= 4 && !taken) free.push_back(s);
                }
            p.sector = free.empty() ? Sector{kSystemCenter + 2, kSystemCenter} : free[rng.below(free.size())];
            p.size = "Medium";
            p.name = std::format("{} {}", sys.name, "Prime");
            sys.objects.push_back(p.id);
            galaxy.objects.push_back(p);
            chosen = p.id;
        }
        SpaceObject& home = galaxy.object(*chosen);
        home.surface = e.surface;
        home.atmosphere = e.atmosphere;
        home.value = {homeValue, homeValue, homeValue};
        // Keep the size; pick a sector type whose picture matches the new surface and air.
        std::vector<uint32_t> matches;
        for (uint32_t t = 0; t < rs.sectorObjectTypes.size(); ++t) {
            const auto& st = rs.sectorObjectTypes[t];
            if (parseObjectKind(st.physicalType) == ObjectKind::Planet && keysEqual(st.planetSize, home.size) &&
                surfaceEqual(st.planetPhysicalType, e.surface) && keysEqual(st.planetAtmosphere, e.atmosphere))
                matches.push_back(t);
        }
        if (!matches.empty()) home.sectorType = matches[rng.below(matches.size())];
        homeworlds.push_back(*chosen);
    }
    return homeworlds;
}

} // namespace opense4::game
