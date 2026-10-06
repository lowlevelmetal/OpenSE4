"""The economy: what to research, and what each colony builds.

Research: Pioneer wants better versions of the parts it builds with (colony modules
for every surface, engines, weapons, armour, bigger hulls, and the facilities that make
resources and research points). Each turn it ranks the areas it may research by how
many such things their next level brings for its cost, and queues the best few.

Construction:
- a colony with a space yard builds ships: scouts while there is something to explore,
  a colony ship for each good planet waiting (a few at a time), and warships, more of
  them as the empire grows or when enemies are near;
- every other colony with room builds facilities: a spaceport when its output cannot
  reach us, else the resource its planet is richest in, and every third facility a
  research centre.
"""

from opense4 import cmd

import parts as p

# What research is for: ability -> worth of a new part with it.
WANTED = {
    p.ENGINE: 4, p.ARMOR: 3, "Resource Generation - Minerals": 4, "Resource Generation - Organics": 3,
    "Resource Generation - Radioactives": 3, "Point Generation - Research": 4, "Spaceport": 2, "Space Yard": 2,
}
for _surface, _ability in p.COLONIZE.items():
    WANTED[_ability] = 30          # a new surface opens many planets to settle

RESEARCH_QUEUE = 4
# Each level is charged this many turns of research on top of its cost, so that levels
# that cost next to nothing do not always come first (classic-ai-research explains it).
OVERHEAD_TURNS = 5
# A ship queue holds at most this many items; a facility queue one.
SHIP_QUEUE = 3


def research_plan(view, parts):
    """The areas to research, best first."""
    rules = view.rules
    research = view.my.research
    levels = research.levels
    # What each area's next level brings: the wanted parts that need only it.
    worth = {}
    for kind, table in (("component", rules.components), ("facility", rules.facilities), ("hull", rules.hulls)):
        for record in table:
            missing = [q for q in record.requirements if q.area is not None and levels[q.area] < q.level]
            if len(missing) != 1 or missing[0].level != levels[missing[0].area] + 1:
                continue
            if kind == "hull":
                value = 5 if record.type == "ship" else 1
            elif kind == "component" and record.is_weapon:
                value = 3
            else:
                value = max([WANTED.get(a.name, 0) for a in record.abilities] + [0])
            area = missing[0].area
            worth[area] = worth.get(area, 0) + value
    tech_cost = view.game.options.tech_cost
    ranked = []
    for area in research.researchable:
        tech = rules.tech(area)
        level = levels[area] + 1
        cost = tech.level_cost * level * (level if tech_cost == 2 else 1)
        if tech_cost == 1:
            cost = max(cost, tech.level_cost * level * level // 2)
        cost += OVERHEAD_TURNS * research.income
        # Areas that bring nothing wanted still rank, by cost alone, after the others.
        ranked.append((-(worth.get(area, 0) * 1000000 // max(cost, 1)), cost, area))
    ranked.sort()
    return [area for _, _, area in ranked[:RESEARCH_QUEUE]]


def research(view, parts):
    """set_research for our plan, unless the queue already holds it."""
    plan = research_plan(view, parts)
    queued = [e.area for e in view.my.research.queue]
    # Keep areas with progress at the front, so no points are lost.
    started = [e.area for e in view.my.research.queue if e.progress > 0 and e.area in view.my.research.researchable]
    queue = started + [a for a in plan if a not in started]
    if not queue or queue == queued:
        return []
    return [cmd.set_research(queue[:max(RESEARCH_QUEUE, len(started))], evenly=False)]


def queued_designs(colonies):
    """{design id: count} of every ship being built in these queues."""
    counts = {}
    for colony in colonies:
        for item in (colony.queue.items if colony.queue is not None else []):
            if item.kind == "vehicle" and item.design_id is not None:
                counts[item.design_id] = counts.get(item.design_id, 0) + item.count
    return counts


def facility_for(view, parts, colony):
    """The facility this colony should build next, or None."""
    planet = colony.planet
    have = colony.facilities or []
    if colony.output is not None and not colony.output.connected:
        f = parts.facility_with("Spaceport")
        if f is not None:
            return f
    # More yards as the empire grows (one for every four colonies), on colonies with
    # people enough to build: each yard is a queue of its own for ships.
    yards = len([c for c in view.my.colonies if c.space_yard])
    if yards < 1 + len(view.my.colonies) // 4 and colony.total_population * 2 >= (colony.max_population or 0):
        f = parts.facility_with("Space Yard")
        if f is not None:
            return f
    if len(have) % 3 == 2:
        f = parts.facility_with("Point Generation - Research")
        if f is not None:
            return f
    value = planet.value if planet is not None else None
    order = [("minerals", "Resource Generation - Minerals"), ("organics", "Resource Generation - Organics"),
             ("radioactives", "Resource Generation - Radioactives")]
    if value is not None:
        order.sort(key=lambda o: -getattr(value, o[0]))
    for _, ability in order:
        f = parts.facility_with(ability)
        if f is not None:
            return f
    return None


def build_facilities(player, view, parts):
    """One facility at a time on each colony without a space yard that has room."""
    commands = []
    for colony in view.my.colonies:
        if colony.space_yard or colony.queue is None or colony.queue.items:
            continue
        if colony.facility_slots is not None and len(colony.facilities or []) >= colony.facility_slots:
            continue
        f = facility_for(view, parts, colony)
        if f is None:
            continue
        item = cmd.queue_item(kind="facility", facility=f.id)
        # The engine says whether the queue takes it (restrictions, population...).
        if player.query("queue_item_problem", planet=colony, item=item).problem:
            continue
        commands.append(cmd.queue_add(target=colony, item=item))
        player.note(colony, "building " + f.name)
    return commands


def rich(view):
    """Our stores are more than half full: what is not spent soon is lost to the cap."""
    stored = view.my.stored
    cap = view.my.economy.storage_capacity
    return all(getattr(stored, r) * 2 > getattr(cap, r) for r in ("minerals", "organics", "radioactives") if getattr(cap, r) > 0)


def build_ships(player, view, wanted):
    """Ships at our yards: `wanted` is [(design id, how many more)], most urgent first.
    When the stores are filling up, each item builds two at once."""
    commands = []
    yards = [c for c in view.my.colonies if c.space_yard and c.queue is not None]
    stored = view.my.stored
    count = 2 if rich(view) else 1
    for yard in yards:
        room = SHIP_QUEUE - len(yard.queue.items)
        for i in range(len(wanted)):
            design_id, more = wanted[i]
            if room <= 0:
                break
            if more <= 0 or design_id is None:
                continue
            design = view.design(design_id)
            # A design made this turn is not in this view yet: build it next turn.
            if design is None or design.figures is None:
                continue
            if not stored.covers(design.figures.cost):
                continue
            n = min(count, more)
            commands.append(cmd.queue_add(target=yard, item=cmd.queue_item(kind="vehicle", design=design_id, count=n)))
            wanted[i] = (design_id, more - n)
            room -= 1
    return commands
