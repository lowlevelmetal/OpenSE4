# The planner's tests, on a small invented tech tree: no game needed, so they run
# under CPython too (pytest, with the opense4 package and ai/ on PYTHONPATH).

import planner

# Four areas: 0 Optics and 1 Hulls at level 1, 2 Lenses at 0 (needs Optics 2), 3 Polish.
TECHS = [
    {"max_level": 5, "level_cost": 1000},   # 0 Optics
    {"max_level": 5, "level_cost": 1000},   # 1 Hulls
    {"max_level": 3, "level_cost": 4000},   # 2 Lenses
    {"max_level": 2, "level_cost": 9000},   # 3 Polish
]
LEVELS = [1, 1, 0, 0]
ITEMS = [
    ("component", [(0, 2)]),            # Optics 2 unlocks a component...
    ("component", [(0, 2), (1, 1)]),    # ...and another (Hulls 1 we have)
    ("tech", [(0, 2)]),                 # ...and makes Lenses researchable
    ("hull", [(1, 3)]),                 # Hulls 3: a hull, two levels away
    ("component", [(0, 3), (2, 1)]),    # lacks two areas: neither unlocks it alone
]


def test_level_costs_follow_the_tech_cost_option():
    assert planner.level_cost(1000, 3, 0) == 3000
    assert planner.level_cost(1000, 3, 1) == 4500
    assert planner.level_cost(1000, 1, 1) == 1000
    assert planner.level_cost(1000, 3, 2) == 9000


def test_what_each_area_unlocks():
    by_area = planner.unlocks(LEVELS, ITEMS)
    assert sorted(by_area[0]) == [(2, 2), (2, 3), (2, 3)]     # (level, worth): two components and a tech area
    assert by_area[1] == [(3, 10)]                           # a hull
    assert 2 not in by_area


def test_the_plan_puts_what_unlocks_most_for_its_cost_first():
    # Hulls first: its level 3 brings a hull (worth 10, halved for being two levels
    # away), more than Optics 2's two components and a tech area, for the same cost.
    # Polish brings nothing and costs much.
    queue = planner.plan(LEVELS, [0, 1, 3], [], TECHS, ITEMS, tech_cost=1)
    assert queue == [1, 0, 3]


def test_the_overhead_favours_what_brings_most():
    techs = [{"max_level": 1, "level_cost": 100}, {"max_level": 1, "level_cost": 20000}]
    items = [("component", [(0, 1)], 3), ("component", [(1, 1)], 40)]
    # Without income, cost alone decides: the cheap area brings less, but costs far less.
    assert planner.plan([0, 0], [0, 1], [], techs, items, tech_cost=1, income=0) == [0, 1]
    # With planner.OVERHEAD_TURNS turns of research charged on every level, worth decides.
    assert planner.plan([0, 0], [0, 1], [], techs, items, tech_cost=1, income=2000) == [1, 0]


def test_areas_under_way_stay_first():
    queue = planner.plan(LEVELS, [0, 1, 3], [3], TECHS, ITEMS, tech_cost=1)
    assert queue[0] == 3
    queue = planner.plan(LEVELS, [0, 1], [3], TECHS, ITEMS, tech_cost=1)
    assert 3 not in queue, "an area we may not research any more is dropped"


def test_the_queue_has_a_length_limit():
    assert len(planner.plan(LEVELS, [0, 1, 3], [], TECHS, ITEMS, tech_cost=1, length=2)) == 2
