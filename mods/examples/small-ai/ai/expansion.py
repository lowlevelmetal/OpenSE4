"""Expansion: scouts explore, colony ships settle the best planets they can reach.

A colony ship's target is the planet that scores best for it: a planet of a surface
its colony module settles, with no colony on it, in an explored system, that no other
colony ship of ours is on its way to. The score prefers large planets, an atmosphere
our race breathes, good conditions and rich resources, and divides by the warp jumps
to get there. The engine has the last word: before sending a ship we ask it, with the
`colonize_problem` query, whether anything stops that ship settling that planet.
"""

from opense4 import cmd, order

# Asking the engine costs budget (20,000 bytecodes a query): a few candidates a ship.
CANDIDATES_ASKED = 4


def planet_capacity(rules, planet, breathes):
    """The population a colony on the planet could hold: its size's capacity, or the
    domed capacity when our race cannot breathe its atmosphere."""
    for size in rules.planet_sizes:
        if size.name == planet.size and size.physical_type == "Planet":
            return size.max_population if breathes else size.max_population_domed
    return 0


def score(rules, race, planet, jumps):
    breathes = planet.atmosphere == race.atmosphere
    capacity = planet_capacity(rules, planet, breathes)
    value = planet.value.total if planet.value is not None else 0       # percent of each resource, added up
    conditions = planet.conditions if planet.conditions is not None else 50
    return capacity * (100 + value) * (50 + conditions) // (100 * (jumps + 1) * (jumps + 1))


def targets(view, claimed):
    """Planets worth settling, as {surface: [(score, planet), ...]}, best first."""
    race = view.me.race
    home = view.system(view.my.home_system) if view.my.home_system is not None else None
    jumps = view.galaxy.distances(home) if home is not None else {}
    found = {}
    for planet in view.planets:
        if planet.kind != "planet" or planet.colony_owner_id is not None or str(planet.id) in claimed:
            continue
        system = view.system(planet.system_id)
        if system is None or not system.explored or system.id not in jumps:
            continue
        found.setdefault(planet.surface, []).append((score(view.rules, race, planet, jumps[system.id]), planet))
    for options in found.values():
        options.sort(key=lambda s: (-s[0], s[1].id))
    return found


def send_scouts(player, view, scouts):
    """Every idle scout explores the nearest system we have not explored."""
    if not view.unexplored_systems:
        return []
    commands = []
    for ship in scouts:
        if low_on_supply(ship):
            commands.append(cmd.give(ship, [order.resupply()]))
            player.note(ship, "resupplying")
        else:
            commands.append(cmd.give(ship, [order.explore()]))
            player.note(ship, "exploring")
    return commands


def send_colony_ships(player, view, ships, claimed):
    """Each idle colony ship gets the best planet it can settle. `claimed` maps a planet
    id (as text: memory keys are text) to the ship sent there; it lives in memory, so a
    ship keeps its target from turn to turn."""
    commands = []
    options = targets(view, claimed)
    for ship in ships:
        surfaces = ship.design.figures.colonize if ship.design is not None and ship.design.figures else []
        candidates = []
        for surface in surfaces:
            candidates += options.get(surface, [])
        candidates.sort(key=lambda s: (-s[0], s[1].id))
        for _, planet in candidates[:CANDIDATES_ASKED]:
            if str(planet.id) in claimed:
                continue
            problem = player.query("colonize_problem", vehicle=ship, planet=planet).problem
            if problem:
                continue
            claimed[str(planet.id)] = ship.id
            commands.append(cmd.give(ship, [order.colonize(planet)]))
            player.note(ship, "to settle " + planet.name)
            player.note(planet, "target of " + ship.name)
            break
    return commands


def forget_finished(view, claimed):
    """Drops the claims of ships that are gone (they settled, or were lost) and of
    planets that have a colony now."""
    for key in list(claimed):
        planet = view.planet(int(key))
        if view.vehicle(claimed[key]) is None or (planet is not None and planet.colony_owner_id is not None):
            del claimed[key]


def low_on_supply(ship):
    return (ship.supply_capacity or 0) > 0 and not ship.unlimited_supply and ship.supply * 4 < ship.supply_capacity
