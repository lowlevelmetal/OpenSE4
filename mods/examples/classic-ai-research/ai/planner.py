"""A research planner: which tech areas to research next, and in what order.

The planner is a plain function of plain values, so it can be tested without a game
(tests/test_planner.py) and read on its own. ai/scholar.py feeds it from the view.

The rule, in short: every area we may research now is worth what its next levels
unlock (components, facilities, hulls, weapon mounts, and other tech areas that become
researchable), divided by what those levels cost. Areas already under way come first,
so that no progress is wasted; then the best values. Points go to the queue in order.
"""

# How much each kind of thing a level unlocks is worth when nothing more is known of it:
# a hull opens new designs, a component or facility one new part, a tech area more
# research later. ai/scholar.py gives items that matter more a worth of their own.
WORTH = {"hull": 10, "component": 3, "facility": 3, "mount": 2, "tech": 2}

# Levels looked ahead: a level that unlocks nothing may lead to one that does. Each
# level further away counts for less (the divisor grows by one a level).
LOOK_AHEAD = 3

# The research queue holds at most 12 areas (docs/sdk/commands.md, set_research).
QUEUE_LENGTH = 8

# Every level is charged this many turns of research income on top of its cost. Without
# it, levels that cost next to nothing would always come first, however little they
# bring; with it, a cheap level is worth what it brings, and an expensive one what it
# brings for its cost.
OVERHEAD_TURNS = 5


def level_cost(level_cost_base, level, tech_cost):
    """The research points level `level` of an area costs, for the game's tech cost option
    (0 low, 1 medium, 2 high), as docs/spec/05 section 1.3 gives it."""
    if tech_cost == 0:
        cost = level_cost_base * level
    elif tech_cost == 2:
        cost = level_cost_base * level * level
    else:
        cost = max(level_cost_base * level, level_cost_base * level * level // 2)
    return min(cost, 2000000000)


def unlocks(levels, items):
    """What each tech area unlocks, level by level: {area: [(level, worth), ...]}.

    Only items that lack one area count: an item that also lacks another area is not
    unlocked by this one alone. Made once a turn, in one pass over the items, so that
    scoring every area costs little (docs/sdk/guide/performance.md)."""
    by_area = {}
    for item in items:
        kind, requirements = item[0], item[1]
        worth = item[2] if len(item) > 2 else WORTH[kind]
        area = None
        level = 0
        for a, l in requirements:
            if a is None or levels[a] >= l:
                continue                     # met already
            if area is None:
                area = a
            elif a != area:
                area = -1                    # lacks two areas: neither unlocks it alone
                break
            level = max(level, l)
        if area is not None and area >= 0:
            by_area.setdefault(area, []).append((level, worth))
    return by_area


def score(area, level, max_level, base_cost, unlocked, tech_cost, overhead=0):
    """Worth of the next LOOK_AHEAD levels of an area at `level` (in thousandths), per
    million research points they cost, each level `overhead` points more."""
    worth = 0
    cost = 0
    for step in range(1, LOOK_AHEAD + 1):
        if level + step > max_level:
            break
        cost += level_cost(base_cost, level + step, tech_cost) + overhead
        # The best thing a level brings counts in full, the others a third: ten small
        # upgrades are not worth ten new things.
        found = sorted((w for l, w in unlocked if l == level + step), reverse=True)
        gained = found[0] + sum(found[1:]) // 3 if found else 0
        worth += gained * 1000 // step
    if cost == 0:
        return 0
    # A small base worth, so that areas that unlock nothing soon still rank by cost.
    return (worth + 100) * 1000000 // cost


def plan(levels, researchable, in_progress, techs, items, tech_cost, income=0, length=QUEUE_LENGTH):
    """The research queue: tech indices in order.

    levels:       our level in every tech area, by tech index
    researchable: the tech indices we may queue now
    in_progress:  the tech indices with points spent toward their next level
    techs:        every tech area: {"max_level", "level_cost"}, by tech index
    items:        (kind, [(area, level), ...]) for everything that needs technology, or
                  (kind, [(area, level), ...], worth) to give an item a worth of its own
    tech_cost:    the game's tech cost option
    income:       our research points a turn
    """
    allowed = set(researchable)
    started = [a for a in in_progress if a in allowed]
    by_area = unlocks(levels, items)
    scored = []
    for a in researchable:
        if a in started:
            continue
        value = score(a, levels[a], techs[a]["max_level"], techs[a]["level_cost"], by_area.get(a, []), tech_cost,
                      OVERHEAD_TURNS * income)
        scored.append((value, a))
    scored.sort(key=lambda s: (-s[0], s[1]))
    queue = started + [a for _, a in scored]
    return queue[:max(length, len(started))]
