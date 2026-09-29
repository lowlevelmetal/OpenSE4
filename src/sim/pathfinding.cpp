#include "sim/pathfinding.hpp"

#include <algorithm>
#include <cstdint>
#include <queue>

namespace opense4::sim {

namespace {

// Primary cost is the number of moves; the secondary term (straight = 10,
// diagonal = 14) only breaks ties so paths don't zig-zag.
constexpr int64_t kMoveCost = 1'000'000;
constexpr int64_t kStraight = 10;
constexpr int64_t kDiagonal = 14;

class Graph {
public:
    Graph(const GameState& s, const Empire* knowledge)
        : state_(s), side_(s.sectorsPerSide()), perSystem_(side_ * side_),
          warpExit_(s.systems.size() * static_cast<size_t>(perSystem_), -1) {
        for (const WarpPoint& wp : s.warpPoints) {
            if (knowledge && !knowledge->hasExplored(wp.system)) continue;
            const WarpPoint& exit = s.warpPoint(wp.exit);
            warpExit_[static_cast<size_t>(node({wp.system, wp.sector}))] = node({exit.system, exit.sector});
        }
    }

    int32_t node(Location loc) const {
        const int r = state_.sectorRadius();
        return static_cast<int32_t>(loc.system.index()) * perSystem_ + (loc.sector.y + r) * side_ + (loc.sector.x + r);
    }

    Location location(int32_t n) const {
        const int r = state_.sectorRadius();
        const int local = n % perSystem_;
        return {SystemId{static_cast<uint32_t>(n / perSystem_)}, SectorPos{local % side_ - r, local / side_ - r}};
    }

    size_t size() const { return warpExit_.size(); }

    template <class Fn>
    void forEachNeighbor(int32_t n, Fn&& fn) const {
        const Location loc = location(n);
        const int r = state_.sectorRadius();
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int x = loc.sector.x + dx;
                const int y = loc.sector.y + dy;
                if (x < -r || x > r || y < -r || y > r) continue;
                fn(n + dy * side_ + dx, (dx != 0 && dy != 0) ? kDiagonal : kStraight);
            }
        }
        if (const int32_t exit = warpExit_[static_cast<size_t>(n)]; exit >= 0) fn(exit, kStraight);
    }

private:
    const GameState& state_;
    int side_;
    int perSystem_;
    std::vector<int32_t> warpExit_;
};

template <class Goal>
std::optional<std::vector<Location>> dijkstra(const GameState& s, Location from, const Empire* knowledge, Goal&& isGoal) {
    if (!from.system.valid() || from.system.index() >= s.systems.size() || !s.inBounds(from.sector)) return std::nullopt;
    const Graph graph(s, knowledge);
    std::vector<int64_t> cost(graph.size(), INT64_MAX);
    std::vector<int32_t> prev(graph.size(), -1);

    using Entry = std::pair<int64_t, int32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    const int32_t start = graph.node(from);
    cost[static_cast<size_t>(start)] = 0;
    open.push({0, start});

    while (!open.empty()) {
        const auto [c, n] = open.top();
        open.pop();
        if (c != cost[static_cast<size_t>(n)]) continue;  // stale entry
        const Location loc = graph.location(n);
        if (isGoal(loc)) {
            std::vector<Location> path;
            for (int32_t cur = n; cur != start; cur = prev[static_cast<size_t>(cur)]) path.push_back(graph.location(cur));
            std::reverse(path.begin(), path.end());
            return path;
        }
        graph.forEachNeighbor(n, [&](int32_t next, int64_t tieBreak) {
            const int64_t nc = c + kMoveCost + tieBreak;
            if (nc < cost[static_cast<size_t>(next)]) {
                cost[static_cast<size_t>(next)] = nc;
                prev[static_cast<size_t>(next)] = n;
                open.push({nc, next});
            }
        });
    }
    return std::nullopt;
}

} // namespace

std::optional<std::vector<Location>> findPath(const GameState& s, Location from, Location to, const Empire* knowledge) {
    if (!to.system.valid() || to.system.index() >= s.systems.size() || !s.inBounds(to.sector)) return std::nullopt;
    return dijkstra(s, from, knowledge, [&](Location l) { return l == to; });
}

std::optional<std::vector<Location>> findPathTo(const GameState& s, Location from,
                                                const std::function<bool(Location)>& goal, const Empire* knowledge) {
    return dijkstra(s, from, knowledge, goal);
}

std::vector<int> warpHopDistances(const GameState& s, SystemId from, const Empire* knowledge) {
    std::vector<int> dist(s.systems.size(), -1);
    std::queue<SystemId> queue;
    dist[from.index()] = 0;
    queue.push(from);
    while (!queue.empty()) {
        const SystemId cur = queue.front();
        queue.pop();
        if (knowledge && !knowledge->hasExplored(cur)) continue;  // its warp points are unknown
        for (WarpPointId wid : s.system(cur).warpPoints) {
            const SystemId next = s.warpPoint(s.warpPoint(wid).exit).system;
            if (dist[next.index()] < 0) {
                dist[next.index()] = dist[cur.index()] + 1;
                queue.push(next);
            }
        }
    }
    return dist;
}

} // namespace opense4::sim
