"""opense4.galaxy against the engine: the warp map worked out from the view agrees with
the routes the engine's `path` query found between the systems we have explored."""

import support
from opense4.galaxy import Galaxy
from opense4.view import View


def test_jumps_and_lengths_agree_with_the_engines_routes():
    # Over what empire 0 knows: its home system in the fixture's own view; every system
    # in the explorer's, where its options (a system to avoid, a tagged minefield) shape
    # the routes. And over everything in a whole view, which the engine routes without
    # the empire's options.
    _agree(View(support.fixture("view")).galaxy, support.fixture("paths"), 0)
    explorer = View(support.fixture("explorer_view"))
    assert explorer.my.systems_to_avoid_ids and explorer.my.settings.avoid_restricted_systems
    _agree(explorer.galaxy, support.fixture("explorer_paths"), 20)
    _agree(Galaxy(View(support.fixture("whole_view")), avoid=False), support.fixture("whole_paths"), 20)
    # The options make a difference here.
    assert explorer.galaxy.jumps(0, 3) != Galaxy(explorer, avoid=False).jumps(0, 3)


def _agree(g, paths, at_least):
    assert len(paths) >= 1
    reachable = 0
    for p in paths:
        a, b = p["origin"], p["destination"]
        jumps = g.jumps(a, b)
        label = "{} -> {}: engine {}, ours jumps {}".format(a, b, p, jumps)
        assert (jumps is not None) == p["found"], label
        if not p["found"]:
            assert g.path(a, b) is None and g.route_length(a, b) is None, label
            continue
        reachable += 1
        # The engine's route is the shortest in movement points; ours counts the same way.
        assert g.route_length(a, b) == p["length"], label + ", ours length " + str(g.route_length(a, b))
        # Its jumps are never fewer than the fewest possible.
        assert jumps <= p["jumps"], label
        route = g.path(a, b)
        assert route[0].id == a and route[-1].id == b and len(route) == jumps + 1
        for x, y in zip(route, route[1:]):
            assert y.id in g.neighbours(x)
    assert reachable - len(set(p["origin"] for p in paths)) >= at_least, "the fixture should have routes between systems"


def test_neighbours_distances_and_nearest():
    view = View(support.fixture("view"))
    g = view.galaxy
    home = view.my.home_system_id
    dist = g.distances(home)
    assert dist[home] == 0
    for s, d in dist.items():
        assert g.jumps(home, s) == d
        if d > 0:
            assert any(dist.get(n) == d - 1 for n in dist if s in g.neighbours(n))
    for s in g.neighbours(home):
        assert dist[s] == 1
        assert any(far == s for (_, far, _) in g.links(home))
    far = max(dist, key=lambda s: (dist[s], -s))
    assert g.nearest(home, [view.system(far), view.system(home)]).id == home
    assert g.nearest(home, [far]) == far
    assert g.nearest(home, []) is None
    # Anything in a system stands for it.
    ship = view.my.ships[0]
    assert g.jumps(ship, ship.system) == 0
    assert g.route_length(ship, ship.location) == 0
    # A system we cannot reach.
    unexplored = support.fixture("facts")["unexplored"]
    if unexplored not in dist:
        assert g.jumps(home, unexplored) is None and g.path(home, unexplored) is None
