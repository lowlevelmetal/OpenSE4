// Knowledge-aware pathfinding (spec 03 §6.2, spec 01 §8).
//
// Routes are found on a small graph: the start, the goals and every usable
// warp point are nodes. Walking between two sectors of one system costs the
// king-move (Chebyshev) distance; systems with obstacles (tagged minefields)
// or known hazards are searched sector by sector instead, and in a system
// with a destructive centre a leg follows the centre's cost map as moving
// groups do (spec 03 §6.2). A warp jump costs one step. Costs are compared as
// (steps, hazard sectors entered, straightness), so hazards and zig-zags only
// ever break ties.

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <climits>
#include <limits>
#include <map>
#include <queue>
#include <tuple>

namespace opense4::game::movement {

namespace {

using detail::inSystem;
using detail::rawBest;
using detail::rawSum;

constexpr int64_t kStep = 1'000'000'000'000;  // one movement point
constexpr int64_t kHazard = 1'000'000;        // entering a known hazard sector
constexpr int64_t kStraight = 10;
constexpr int64_t kDiagonal = 14;
constexpr int64_t kInf = std::numeric_limits<int64_t>::max();
constexpr int kCells = kSystemSize * kSystemSize;
constexpr int kWarpPointsConsidered = 10;

int cell(Sector s) { return s.y * kSystemSize + s.x; }
Sector sectorOf(int c) { return Sector{c % kSystemSize, c / kSystemSize}; }

int64_t chebyshevCost(Sector a, Sector b) {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    const int diag = std::min(dx, dy);
    return int64_t{std::max(dx, dy)} * kStep + diag * kDiagonal + (std::max(dx, dy) - diag) * kStraight;
}

// Walks from a to b, diagonals first (the canonical obstacle-free path).
void straightWalk(std::vector<Location>& out, SystemId sys, Sector a, Sector b) {
    int x = a.x, y = a.y;
    while (x != b.x || y != b.y) {
        x += (b.x > x) - (b.x < x);
        y += (b.y > y) - (b.y < y);
        out.push_back({sys, Sector{x, y}});
    }
}

struct Grid {
    std::array<int64_t, kCells> cost{};
    std::array<int16_t, kCells> prev{};
};

// The steps of a group from `a` to `b` in a system with a destructive centre:
// the cost map's choice each step, the greedy step toward `b` when no square
// around the group was reached (spec 03 §6.2). Empty when a == b.
std::vector<Sector> centreLeg(const detail::CentreCostMap& map, Sector a, Sector b) {
    std::vector<Sector> out;
    for (int guard = 0; a != b && guard < kCells; ++guard) {
        const auto next = detail::centreStep(map, a);
        a = next && *next != a ? *next : Sector{a.x + (b.x > a.x) - (b.x < a.x), a.y + (b.y > a.y) - (b.y < a.y)};
        out.push_back(a);
    }
    return out;
}

class Router {
public:
    Router(const GameState& s, EmpireId e, RouteOptions options) : s_(s), e_(e), options_(options) {
        if (knowing()) {
            // The empire's Ship Movement options decide what is avoided (spec 03 §6.2).
            const Empire& emp = s.empire(e);
            if (emp.avoidTaggedMinefields) {
                tagged_ = emp.taggedMinefields;
                std::sort(tagged_.begin(), tagged_.end());
            }
            if (emp.avoidRestrictedSystems) {
                avoided_ = emp.systemsToAvoid;
                std::sort(avoided_.begin(), avoided_.end());
            }
        }
    }

    std::optional<NearestPath> run(Location from, std::span<const Location> goals) {
        if (!valid(from)) return std::nullopt;
        from_ = from;
        goals_.assign(goals.begin(), goals.end());
        goalIndex_.clear();
        goalSystems_.clear();
        for (size_t i = 0; i < goals_.size(); ++i) {
            goalIndex_.emplace(goals_[i], i);  // the earlier goal wins ties
            if (valid(goals_[i])) goalSystems_.push_back(goals_[i].system);
        }
        std::sort(goalSystems_.begin(), goalSystems_.end());
        goalSystems_.erase(std::unique(goalSystems_.begin(), goalSystems_.end()), goalSystems_.end());

        const int start = node(from);
        dist_[static_cast<size_t>(start)] = 0;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
        open.push({0, rank(from), start});
        while (!open.empty()) {
            const auto [d, unused, u] = open.top();
            open.pop();
            if (d != dist_[static_cast<size_t>(u)]) continue;
            const Location at = locs_[static_cast<size_t>(u)];
            if (const auto g = goalIndex(at)) return reconstruct(start, u, *g);
            expand(u, at, open);
        }
        return std::nullopt;
    }

private:
    // Equal costs: goals first, the earlier goal before later ones.
    using Entry = std::tuple<int64_t, size_t, int>;
    size_t rank(Location l) const { return goalIndex(l).value_or(goals_.size()); }

    bool knowing() const { return e_.valid() && e_.index() < s_.empires.size(); }
    bool valid(Location l) const { return l.system.valid() && l.system.index() < s_.galaxy.systems.size() && l.sector.valid(); }

    // May the route pass through this system (its warp points are known)?
    bool routable(SystemId sys) const {
        if (!knowing() || s_.options.omnipresent || sys == from_.system) return true;
        return s_.empire(e_).hasExplored(sys);
    }
    bool avoided(SystemId sys) const {
        if (sys == from_.system) return false;
        if (std::binary_search(goalSystems_.begin(), goalSystems_.end(), sys)) return false;
        return std::binary_search(avoided_.begin(), avoided_.end(), sys);
    }
    bool isGoal(Location l) const { return goalIndex_.contains(l); }
    std::optional<size_t> goalIndex(Location l) const {
        const auto it = goalIndex_.find(l);
        if (it == goalIndex_.end()) return std::nullopt;
        return it->second;
    }

    // A warp point the route may jump through: still in place, linked and the
    // link known to the empire. The one-way flag is never read: every link
    // works both ways (spec 01 §8, confirmed: binary).
    bool usableWarp(ObjectId w) const {
        const SpaceObject& wp = s_.galaxy.object(w);
        if (wp.kind != ObjectKind::WarpPoint || !wp.destination.valid() || !inSystem(s_.galaxy, w)) return false;
        if (!inSystem(s_.galaxy, wp.destination)) return false;
        return !knowing() || sight::knowsWarpLink(s_, e_, w);
    }

    bool tagged(Location l) const { return !tagged_.empty() && std::binary_search(tagged_.begin(), tagged_.end(), l); }
    // In a system, tagged sectors are not entered unless they are the goal
    // (the greedy step takes its target square untested).
    bool obstacle(Location l) const { return tagged(l) && !isGoal(l) && l != from_; }
    // A warp link whose warp-point sector on either side is tagged is not used,
    // even at the start or the goal, unless a Mine Sweeper leads (spec 03 §6.2, confirmed: binary).
    bool blockedLink(Location l) const { return tagged(l) && !options_.sweeper; }

    // Systems with a destructive centre: legs follow its cost map, which
    // ignores tagged minefields, storms and hostiles (spec 03 §6.2).
    bool centred(SystemId sys) {
        auto it = centred_.find(sys);
        if (it == centred_.end()) it = centred_.emplace(sys, detail::destructiveCentre(s_, sys) > 0).first;
        return it->second;
    }
    const std::vector<Sector>& centreSteps(SystemId sys, Sector a, Sector b) {
        const auto key = std::make_tuple(sys, cell(a), cell(b));
        auto it = centreLegs_.find(key);
        if (it != centreLegs_.end()) return it->second;
        const auto mapKey = std::make_pair(sys, cell(b));
        auto m = centreMaps_.find(mapKey);
        if (m == centreMaps_.end()) m = centreMaps_.emplace(mapKey, detail::centreCostMap(b, detail::centreZone(s_, sys))).first;
        return centreLegs_.emplace(key, centreLeg(m->second, a, b)).first->second;
    }

    // Known hazard sectors of a system (only explored systems are known).
    const std::vector<Sector>& hazards(SystemId sys) {
        auto it = hazards_.find(sys);
        if (it != hazards_.end()) return it->second;
        std::vector<Sector> out;
        if (routable(sys)) {
            const StarSystem& st = s_.galaxy.system(sys);
            for (ObjectId o : st.objects) {
                const SpaceObject& obj = s_.galaxy.object(o);
                if (rawBest(obj.abilities, AbilityKind::SectorDamage) > 0) out.push_back(obj.sector);
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        }
        return hazards_.emplace(sys, std::move(out)).first->second;
    }
    bool hazard(Location l) {
        const auto& h = hazards(l.system);
        return std::binary_search(h.begin(), h.end(), l.sector);
    }
    bool plain(SystemId sys) {
        if (!hazards(sys).empty()) return false;
        return std::none_of(tagged_.begin(), tagged_.end(), [&](const Location& t) { return t.system == sys && obstacle(t); });
    }

    const Grid& grid(SystemId sys, Sector from) {
        const auto key = std::make_pair(sys, cell(from));
        auto it = grids_.find(key);
        if (it != grids_.end()) return it->second;
        Grid g;
        g.cost.fill(kInf);
        g.prev.fill(-1);
        using Cell = std::pair<int64_t, int>;
        std::priority_queue<Cell, std::vector<Cell>, std::greater<>> open;
        g.cost[static_cast<size_t>(cell(from))] = 0;
        open.push({0, cell(from)});
        while (!open.empty()) {
            const auto [d, c] = open.top();
            open.pop();
            if (d != g.cost[static_cast<size_t>(c)]) continue;
            const Sector at = sectorOf(c);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const Sector next{at.x + dx, at.y + dy};
                    if (!next.valid() || obstacle({sys, next})) continue;
                    const int64_t nd = d + kStep + (dx != 0 && dy != 0 ? kDiagonal : kStraight) + (hazard({sys, next}) ? kHazard : 0);
                    const int nc = cell(next);
                    if (nd < g.cost[static_cast<size_t>(nc)]) {
                        g.cost[static_cast<size_t>(nc)] = nd;
                        g.prev[static_cast<size_t>(nc)] = static_cast<int16_t>(c);
                        open.push({nd, nc});
                    }
                }
        }
        return grids_.emplace(key, g).first->second;
    }

    int64_t legCost(SystemId sys, Sector a, Sector b) {
        if (a == b) return 0;
        if (centred(sys)) return static_cast<int64_t>(centreSteps(sys, a, b).size()) * kStep;
        if (plain(sys)) return chebyshevCost(a, b);
        return grid(sys, a).cost[static_cast<size_t>(cell(b))];
    }

    void appendLeg(std::vector<Location>& out, SystemId sys, Sector a, Sector b) {
        if (a == b) return;
        if (centred(sys)) {
            for (Sector step : centreSteps(sys, a, b)) out.push_back({sys, step});
            return;
        }
        if (plain(sys)) {
            straightWalk(out, sys, a, b);
            return;
        }
        const Grid& g = grid(sys, a);
        std::vector<Location> rev;
        for (int c = cell(b); c != cell(a) && c >= 0; c = g.prev[static_cast<size_t>(c)]) rev.push_back({sys, sectorOf(c)});
        out.insert(out.end(), rev.rbegin(), rev.rend());
    }

    int node(Location l) {
        auto [it, inserted] = index_.emplace(l, static_cast<int>(locs_.size()));
        if (inserted) {
            locs_.push_back(l);
            dist_.push_back(kInf);
            prev_.push_back(-1);
            via_.push_back(ObjectId{});
        }
        return it->second;
    }

    template <class Queue>
    void relax(int u, Location to, int64_t cost, ObjectId via, Queue& open) {
        if (cost == kInf) return;
        const int64_t nd = dist_[static_cast<size_t>(u)] + cost;
        const int v = node(to);
        if (nd < dist_[static_cast<size_t>(v)]) {
            dist_[static_cast<size_t>(v)] = nd;
            prev_[static_cast<size_t>(v)] = u;
            via_[static_cast<size_t>(v)] = via;
            open.push({nd, rank(to), v});
        }
    }

    template <class Queue>
    void expand(int u, Location at, Queue& open) {
        const SystemId sys = at.system;
        for (auto it = goalIndex_.lower_bound(Location{sys, Sector{0, 0}}); it != goalIndex_.end() && it->first.system == sys; ++it)
            if (valid(it->first)) relax(u, it->first, legCost(sys, at.sector, it->first.sector), {}, open);
        if (!routable(sys)) return;
        // Only the first 10 warp points of a system are considered (spec 03 §6.2, confirmed: binary).
        int warpPoints = 0;
        for (ObjectId w : s_.galaxy.system(sys).objects) {
            if (s_.galaxy.object(w).kind != ObjectKind::WarpPoint) continue;
            if (++warpPoints > kWarpPointsConsidered) break;
            if (!usableWarp(w)) continue;
            const SpaceObject& wp = s_.galaxy.object(w);
            if (wp.sector != at.sector) {
                relax(u, {sys, wp.sector}, legCost(sys, at.sector, wp.sector), {}, open);
                continue;
            }
            if (!options_.allowWarp) continue;
            const SpaceObject& far = s_.galaxy.object(wp.destination);
            if (avoided(far.system)) continue;
            const Location arrival{far.system, far.sector};
            // A link with a tagged minefield on either side is not used (spec 03 §6.2).
            if (blockedLink(at) || blockedLink(arrival)) continue;
            relax(u, arrival, kStep + (hazard(arrival) ? kHazard : 0), w, open);
        }
    }

    NearestPath reconstruct(int start, int goalNode, size_t goal) {
        std::vector<int> chain;
        for (int n = goalNode; n != -1; n = prev_[static_cast<size_t>(n)]) {
            chain.push_back(n);
            if (n == start) break;
        }
        std::reverse(chain.begin(), chain.end());
        NearestPath out;
        out.goal = goal;
        for (size_t i = 1; i < chain.size(); ++i) {
            const Location a = locs_[static_cast<size_t>(chain[i - 1])];
            const Location b = locs_[static_cast<size_t>(chain[i])];
            if (via_[static_cast<size_t>(chain[i])].valid()) out.path.steps.push_back(b);
            else appendLeg(out.path.steps, a.system, a.sector, b.sector);
        }
        out.path.length = static_cast<int>(out.path.steps.size());
        return out;
    }

    const GameState& s_;
    EmpireId e_;
    RouteOptions options_;
    Location from_{};
    std::vector<Location> goals_;
    std::map<Location, size_t> goalIndex_;
    std::vector<SystemId> goalSystems_;
    std::vector<Location> tagged_;
    std::vector<SystemId> avoided_;
    std::map<Location, int> index_;
    std::vector<Location> locs_;
    std::vector<int64_t> dist_;
    std::vector<int> prev_;
    std::vector<ObjectId> via_;
    std::map<SystemId, std::vector<Sector>> hazards_;
    std::map<std::pair<SystemId, int>, Grid> grids_;
    std::map<SystemId, bool> centred_;
    std::map<std::pair<SystemId, int>, detail::CentreCostMap> centreMaps_;
    std::map<std::tuple<SystemId, int, int>, std::vector<Sector>> centreLegs_;
};

} // namespace

bool leadsSweeperGroup(const GameState& s, const Vehicle& first) {
    if (!first.design.valid() || first.design.index() >= s.designs.size()) return false;
    return datafile::keysEqual(s.design(first.design).designType, "Mine Sweeper");
}

const Vehicle& sweeperOf(const GameState& s, const Vehicle& v) {
    if (!detail::followsFleetOrders(s, v)) return v;
    const Fleet* f = s.fleet(v.fleet);
    const Vehicle* first = &v;
    for (VehicleId id : f->members)
        if (const Vehicle* m = s.vehicle(id); m && detail::alive(*m) && m->location == v.location &&
                                               std::pair(m->slot, m->id) < std::pair(first->slot, first->id))
            first = m;
    return *first;
}

namespace detail {

int64_t destructiveCentre(const GameState& s, SystemId sys) {
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size()) return 0;
    return rawSum(s.galaxy.system(sys).abilities, AbilityKind::SystemDestructiveCenter);
}

int64_t centreZone(const GameState& s, SystemId sys) {
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size()) return 0;
    return rawSum(s.galaxy.system(sys).abilities, AbilityKind::SystemMovementTowardsCenter);
}

CentreCostMap centreCostMap(Sector target, int64_t zone) {
    // round(√n) to nearest; √n is never exactly halfway between integers.
    auto roundRoot = [](int n) {
        int k = 0;
        while ((k + 1) * (k + 1) <= n) ++k;
        return n > k * k + k ? k + 1 : k;
    };
    auto enter = [&](Sector q) {
        const int dx = q.x - kSystemCenter, dy = q.y - kSystemCenter;
        const bool inZone = zone > 0 && std::max(std::abs(dx), std::abs(dy)) <= zone;
        return int64_t{1} + (30 - roundRoot(dx * dx + dy * dy)) + (inZone ? 1000 : 0);
    };
    constexpr int64_t kSpread = 500;
    CentreCostMap cost;
    cost.fill(-1);
    using Cell = std::pair<int64_t, int>;
    std::priority_queue<Cell, std::vector<Cell>, std::greater<>> open;
    cost[static_cast<size_t>(cell(target))] = 1;
    open.push({1, cell(target)});
    while (!open.empty()) {
        const auto [d, c] = open.top();
        open.pop();
        if (d != cost[static_cast<size_t>(c)] || d > kSpread) continue;  // only squares costing at most 500 spread
        const Sector at = sectorOf(c);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Sector next{at.x + dx, at.y + dy};
                if ((dx == 0 && dy == 0) || !next.valid()) continue;
                const int64_t nd = d + enter(next);
                const size_t nc = static_cast<size_t>(cell(next));
                if (cost[nc] < 0 || nd < cost[nc]) {
                    cost[nc] = nd;
                    open.push({nd, cell(next)});
                }
            }
    }
    return cost;
}

std::optional<Sector> centreStep(const CentreCostMap& map, Sector at) {
    std::optional<Sector> best;
    int64_t bestCost = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Sector q{at.x + dx, at.y + dy};
            if (!q.valid()) continue;
            const int64_t c = map[static_cast<size_t>(cell(q))];
            if (c < 0) continue;
            // The cheapest; on a tie the lowest sector number (y × 13 + x).
            if (!best || c < bestCost || (c == bestCost && cell(q) < cell(*best))) {
                best = q;
                bestCost = c;
            }
        }
    return best;
}

} // namespace detail

std::optional<NearestPath> findPathToNearest(const Rules&, const GameState& s, EmpireId e, Location from,
                                             std::span<const Location> goals, RouteOptions options) {
    if (goals.empty()) return std::nullopt;
    // With the avoid option on, systems to avoid are never crossed; with no
    // route around them there is no route (spec 03 §6.2, confirmed: binary).
    Router router(s, e, options);
    return router.run(from, goals);
}

std::optional<Path> findPath(const Rules& r, const GameState& s, EmpireId e, Location from, Location to) {
    const Location goals[] = {to};
    auto p = findPathToNearest(r, s, e, from, goals);
    if (!p) return std::nullopt;
    return std::move(p->path);
}

int fleetSpeed(const Rules& r, const GameState& s, const Fleet& f) {
    // The lowest maximum among the members in the fleet's sector (spec 03 §9).
    const Vehicle* lead = detail::fleetLeader(s, f);
    if (!lead) return 0;
    int speed = -1;
    for (VehicleId id : f.members)
        if (const Vehicle* v = s.vehicle(id); v && detail::alive(*v) && v->location == lead->location) {
            const int mp = detail::turnMovement(r, s, *v);
            speed = speed < 0 ? mp : std::min(speed, mp);
        }
    return std::max(0, speed);
}

int etaTurns(const Rules& r, const GameState& s, const Vehicle& v, Location to) {
    if (v.location == to) return 0;
    int speed = vehicleMaxMovement(r, s, v);
    int held = detail::heldInPlace(s, v) ? static_cast<int>(v.immobileUntil - s.turn) : 0;
    if (detail::followsFleetOrders(s, v))
        if (const Fleet* f = s.fleet(v.fleet)) {
            speed = INT_MAX;
            for (VehicleId id : f->members)
                if (const Vehicle* m = s.vehicle(id); m && detail::alive(*m) && m->location == v.location) {
                    speed = std::min(speed, vehicleMaxMovement(r, s, *m));
                    if (detail::heldInPlace(s, *m)) held = std::max(held, static_cast<int>(m->immobileUntil - s.turn));
                }
        }
    // The moves that speed makes in a turn (a simultaneous game's day counter
    // loses some, spec 03 §6.3).
    const int moves = movesPerTurn(s, speed);
    if (moves <= 0) return -1;
    RouteOptions options;
    options.allowWarp = vehicleType(r, s, v) != ruleset::VehicleType::Fighter;
    options.sweeper = leadsSweeperGroup(s, sweeperOf(s, v));
    const Location goals[] = {to};
    const auto p = findPathToNearest(r, s, v.owner, v.location, goals, options);
    if (!p) return -1;
    return held + (p->path.length + moves - 1) / moves;
}

} // namespace opense4::game::movement
