"""Defence: warships guard our systems, gather into fleets and strike back at war.

- An enemy ship seen in a system where we have a colony is attacked by the nearest
  idle warship or fleet.
- Otherwise idle warships go home and wait there (Sentry). Three or more waiting in the
  same place form a fleet.
- A fleet of four or more, while we are at war, attacks the nearest enemy colony we
  know of.
- Ships low on supply resupply first; badly damaged ones go for repair.
"""

from opense4 import cmd, order

FLEET_SIZE = 3
STRIKE_SIZE = 4


def damaged(ship):
    return ship.structure and ship.damage * 2 > ship.structure


def needs_care(player, ship):
    """resupply or repair orders for a ship that needs them, else None."""
    if damaged(ship):
        player.note(ship, "going for repairs")
        return cmd.give(ship, [order.repair()])
    supply, capacity = ship.supply or 0, ship.supply_capacity or 0
    if capacity > 0 and not ship.unlimited_supply and supply * 3 < capacity:
        player.note(ship, "resupplying")
        return cmd.give(ship, [order.resupply()])
    return None


def nearest(view, origin, things):
    """The thing the fewest warp jumps from `origin`, or None."""
    return view.galaxy.nearest(origin, things) if things else None


def guard(player, view, warships, fleets):
    commands = []
    ours = {c.system.id for c in view.my.colonies if c.system is not None}
    threats = [v for v in view.enemy_vehicles if v.system is not None and v.system.id in ours]
    home = view.system(view.my.home_system) if view.my.home_system is not None else None

    groups = list(fleets)
    for ship in warships:
        care = needs_care(player, ship)
        if care is not None:
            commands.append(care)
        else:
            groups.append(ship)

    # Threats first: the nearest free group for each.
    for enemy in threats:
        group = nearest(view, enemy, groups)
        if group is None:
            break
        groups.remove(group)
        commands.append(cmd.give(group, [order.attack(vehicle=enemy)]))
        player.note(group, "intercepting " + enemy.name)

    # Strike: a large fleet at war attacks the nearest enemy colony.
    targets = view.enemy_colonies
    for group in list(groups):
        if targets and getattr(group, "members", None) is not None and len(group.members) >= STRIKE_SIZE:
            target = nearest(view, group, targets)
            if target is not None:
                groups.remove(group)
                commands.append(cmd.give(group, [order.attack(object=target.id)]))
                player.note(group, "attacking " + target.planet.name if target.planet else "attacking")

    # The rest wait at home; those already there form a fleet.
    waiting = {}
    for group in groups:
        if home is None:
            break
        if group.location.system_id != home.id:
            commands.append(cmd.give(group, [order.move_to(home), order.sentry()]))
        elif getattr(group, "members", None) is None:        # a ship, not a fleet
            waiting.setdefault((group.location.x, group.location.y), []).append(group)
    for ships in waiting.values():
        if len(ships) >= FLEET_SIZE:
            commands.append(cmd.create_fleet(name="Home Guard " + str(view.game.turn), members=ships))
    return commands
