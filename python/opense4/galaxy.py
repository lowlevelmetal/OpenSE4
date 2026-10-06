"""The warp map of a view: which systems are linked, how many jumps apart they are,
and how long a route is, worked out in Python from the view alone (no engine call).

`view.galaxy` is the map of the links the empire knows, as its own ships route:

- a link is a warp point of a system whose contents we see, with a known far end
  (`destination_system`) that our routes may use (`link_known`); only the first ten
  warp points of a system count, as for the engine's routes;
- with the empire's option to avoid restricted systems on, its systems to avoid are
  never crossed (they may still be where a route starts or ends);
- with the option to avoid tagged minefields on, a link whose warp point sector on
  either side is tagged is not used.

`jumps` and `path` count warp jumps (breadth-first). `route_length` counts movement
points as the engine's routes do: one per sector moved, diagonals included, and one
per jump. It does not know the detours the engine makes around a system's
destructive centre or tagged minefields inside a system; the `path` query does
(docs/sdk/view.md, "Queries").

Systems may be given as ids or System objects; results name systems by their ids,
except `path`, which gives System objects.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Sequence, Tuple

from ._values import id_of, location_of

# Only the first ten warp points of a system carry routes (docs/sdk/view.md, "path").
WARP_POINTS_ROUTED = 10


def _chebyshev(ax: int, ay: int, bx: int, by: int) -> int:
    dx = ax - bx
    dy = ay - by
    if dx < 0:
        dx = -dx
    if dy < 0:
        dy = -dy
    return dx if dx > dy else dy


class Galaxy:
    """The warp map of one view (see the module's description)."""

    def __init__(self, view: Any, known_only: bool = True, avoid: Optional[bool] = None) -> None:
        """`known_only`: only links our routes may use (False: every link the view shows).
        `avoid`: honour the empire's Ship Movement options (None: as the empire has set them)."""
        self._view = view
        # system id -> [(warp point sector x, y, far system, far sector x, y), ...]
        self._links: Dict[int, List[Tuple[int, int, int, int, int]]] = {}
        self._avoided: Dict[int, bool] = {}
        my = view._d["my"]
        settings = my["settings"] if my is not None else None
        use_avoid = avoid if avoid is not None else bool(settings and settings["avoid_restricted_systems"])
        if use_avoid and my is not None:
            for s in my["systems_to_avoid"]:
                self._avoided[s] = True
        tagged = {}
        if my is not None and settings is not None and (avoid is None or avoid) and settings["avoid_tagged_minefields"]:
            for loc in my["tagged_minefields"]:
                tagged[(loc["system"], loc["x"], loc["y"])] = True
        objects = view._by_id("object")
        for sys in view._d["systems"]:
            ids = sys["objects"]
            if not ids:
                continue
            links = []
            warp_points = 0
            for oid in ids:
                o = objects.get(oid)
                if o is None:
                    continue
                d = o._d
                if d["kind"] != "warp_point":
                    continue
                warp_points += 1
                if warp_points > WARP_POINTS_ROUTED:
                    break
                far_system = d["destination_system"]
                if far_system is None or (known_only and not d["link_known"]):
                    continue
                far = objects.get(d["destination"]) if d["destination"] is not None else None
                fx, fy = (far._d["sector"]["x"], far._d["sector"]["y"]) if far is not None else (6, 6)
                sx, sy = d["sector"]["x"], d["sector"]["y"]
                if tagged and ((sys["id"], sx, sy) in tagged or (far_system, fx, fy) in tagged):
                    continue
                links.append((sx, sy, far_system, fx, fy))
            if links:
                self._links[sys["id"]] = links

    def _id(self, system: Any) -> int:
        if isinstance(system, dict):
            return system["system"]
        if hasattr(system, "_kind") and system._kind in ("location", "space_object", "vehicle", "fleet", "colony"):
            return location_of(system)["system"]
        return id_of(system, "system", "system")

    def _passable(self, system: int, start: int, goal: int) -> bool:
        return system == start or system == goal or system not in self._avoided

    def neighbours(self, system: Any) -> List[int]:
        """The systems one jump away, in the order of the warp points (each once)."""
        out: List[int] = []
        for link in self._links.get(self._id(system), ()):
            if link[2] not in out:
                out.append(link[2])
        return out

    def links(self, system: Any) -> List[Tuple[Tuple[int, int], int, Tuple[int, int]]]:
        """The system's usable links: ((x, y) of the warp point, far system id, (x, y) where it arrives)."""
        return [((l[0], l[1]), l[2], (l[3], l[4])) for l in self._links.get(self._id(system), ())]

    def distances(self, origin: Any, goal: Any = None) -> Dict[int, int]:
        """Jumps from `origin` to every system it reaches: {system id: jumps}."""
        start = self._id(origin)
        target = None if goal is None else self._id(goal)
        dist = {start: 0}
        frontier = [start]
        links = self._links
        while frontier:
            nxt = []
            for s in frontier:
                if s != start and not self._passable(s, start, target if target is not None else -1):
                    continue
                d = dist[s] + 1
                for link in links.get(s, ()):
                    far = link[2]
                    if far not in dist:
                        dist[far] = d
                        nxt.append(far)
            frontier = nxt
        return dist

    def jumps(self, origin: Any, destination: Any) -> Optional[int]:
        """The fewest warp jumps from one system to another, or None when no known route links them."""
        return self.distances(origin, destination).get(self._id(destination))

    def path(self, origin: Any, destination: Any) -> Optional[List[Any]]:
        """The systems on a route with the fewest jumps, both ends included, or None."""
        start = self._id(origin)
        goal = self._id(destination)
        prev: Dict[int, int] = {start: start}
        frontier = [start]
        while frontier and goal not in prev:
            nxt = []
            for s in frontier:
                if s != start and not self._passable(s, start, goal):
                    continue
                for link in self._links.get(s, ()):
                    far = link[2]
                    if far not in prev:
                        prev[far] = s
                        nxt.append(far)
            frontier = nxt
        if goal not in prev:
            return None
        chain = [goal]
        while chain[-1] != start:
            chain.append(prev[chain[-1]])
        chain.reverse()
        view = self._view
        return [view.resolve("system", s) for s in chain]

    def nearest(self, origin: Any, candidates: Sequence[Any]) -> Optional[Any]:
        """The candidate (a system, or anything in one: a location, planet, vehicle...) the
        fewest jumps away; the earlier one on a tie; None when none is reachable."""
        dist = self.distances(origin)
        best = None
        best_jumps = -1
        for c in candidates:
            j = dist.get(self._id(c))
            if j is not None and (best is None or j < best_jumps):
                best = c
                best_jumps = j
        return best

    def route_length(self, origin: Any, destination: Any) -> Optional[int]:
        """Movement points from one place to another over the known links (see the module's
        description), or None when no known route links them. Places are locations or what
        stands for one (a system means its centre)."""
        a = location_of(origin, "origin")
        b = location_of(destination, "destination")
        start, goal = a["system"], b["system"]
        if start == goal:
            return _chebyshev(a["x"], a["y"], b["x"], b["y"])
        # Dijkstra over (system, x, y) points: the start, warp point sectors and arrivals.
        best: Dict[Tuple[int, int, int], int] = {(start, a["x"], a["y"]): 0}
        open_: List[Tuple[int, Tuple[int, int, int]]] = [(0, (start, a["x"], a["y"]))]
        result: Optional[int] = None
        while open_:
            # the cheapest open point (lists stay small: a few per system)
            i_best = 0
            for i in range(1, len(open_)):
                if open_[i][0] < open_[i_best][0]:
                    i_best = i
            cost, point = open_.pop(i_best)
            if best.get(point, -1) != cost:
                continue
            if result is not None and cost >= result:
                break
            s, x, y = point
            if s == goal:
                total = cost + _chebyshev(x, y, b["x"], b["y"])
                if result is None or total < result:
                    result = total
                continue
            if s != start and not self._passable(s, start, goal):
                continue
            for sx, sy, far, fx, fy in self._links.get(s, ()):
                c = cost + _chebyshev(x, y, sx, sy) + 1
                p = (far, fx, fy)
                if c < best.get(p, c + 1):
                    best[p] = c
                    open_.append((c, p))
        return result
