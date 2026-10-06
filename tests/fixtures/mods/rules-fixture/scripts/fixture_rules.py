"""The rules fixture: every hook notes its call and arguments in the game's mod data,
so the tests can check what the engine passed and when."""

from opense4 import rules


def note(game, hook, *args):
    data = game.mod_data
    calls = data.setdefault("calls", {})
    calls[hook] = calls.get(hook, 0) + 1
    last = data.setdefault("last", {})
    last[hook] = list(args)
    order = data.setdefault("order", [])
    if len(order) < 60 and (not order or order[-1] != hook):
        order.append(hook)


def oid(x):
    return None if x is None else x.id


@rules.on("new_game")
def new_game(game, setup, fx):
    note(game, "new_game", setup.seed, len(setup.empires), [e["name"] for e in setup.empires])


@rules.on("generate_galaxy")
def generate_galaxy(game, fx):
    note(game, "generate_galaxy", len(game.systems), len(game.empires))
    first = game.systems[0]
    fx.rename(first, "Fixture Prime")


@rules.on("after_galaxy")
def after_galaxy(game, fx):
    note(game, "after_galaxy", len(game.systems), len(game.empires), len(game.colonies))
    for e in game.empires:
        e.mod_data["founded"] = game.turn
    for c in game.colonies:
        c.mod_data["home"] = True


@rules.on("turn_start")
def turn_start(game, fx):
    note(game, "turn_start", game.turn)


@rules.on("orders_applied")
def orders_applied(game, empire, fx):
    note(game, "orders_applied", game.turn, oid(empire))


@rules.on("movement_day")
def movement_day(game, day, fx):
    note(game, "movement_day", day)


@rules.on("vehicle_entered_sector")
def entered(game, vehicle, location, fx):
    note(game, "vehicle_entered_sector", oid(vehicle), None if location is None else [location.system_id, location.x, location.y])


@rules.on("before_battle")
def before_battle(game, site, fx):
    note(game, "before_battle", [site.location.system_id, site.location.x, site.location.y], [e.id for e in site.empires])


@rules.on("after_battle")
def after_battle(game, battle, fx):
    note(game, "after_battle", battle.turn, [battle.location.system_id, battle.location.x, battle.location.y], len(battle.pieces))


@rules.on("vehicle_destroyed")
def destroyed(game, vehicle, cause, fx):
    note(game, "vehicle_destroyed", vehicle.id, vehicle.owner_id, cause)


@rules.on("empire_end_of_turn")
def end_step(game, empire, step, when, fx):
    note(game, "empire_end_of_turn", empire.id, step, when)
    if empire.id == 0:
        steps = game.mod_data.setdefault("steps", [])
        if len(steps) < 20:
            steps.append(step + ":" + when)


@rules.on("empire_end_of_turn", step="income", when="after")
def after_income(game, empire, step, when, fx):
    note(game, "income_after", empire.id, step, when)


@rules.on("colony_end_of_turn")
def colony_end(game, colony, fx):
    note(game, "colony_end_of_turn", colony.id, colony.owner_id)
    colony.mod_data["turns"] = colony.mod_data.get("turns", 0) + 1


@rules.on("colony_founded")
def founded(game, colony, fx):
    note(game, "colony_founded", oid(colony), None if colony is None else colony.owner_id)


@rules.on("vehicle_built")
def built(game, b, fx):
    note(game, "vehicle_built", oid(b.vehicle), oid(b.design), b.count, b.empire_id)
    if b.vehicle is not None:
        b.vehicle.mod_data["built"] = game.turn


@rules.on("tech_researched")
def researched(game, empire, tech, level, fx):
    note(game, "tech_researched", empire.id, tech.name, level)


@rules.on("treaty_changed")
def treaty(game, empire, other, new, old, fx):
    note(game, "treaty_changed", empire.id, other.id, new, old)


@rules.on("message_sent")
def message(game, msg, fx):
    note(game, "message_sent", msg.id, msg.from_empire_id, msg.to_empire_id)


@rules.on("event_fired")
def event_fired(game, event, fx):
    note(game, "event_fired", event.name, event.mod, event.empire_id)


@rules.on("check_victory")
def check_victory(game, fx):
    note(game, "check_victory", game.turn)


@rules.on("turn_end")
def turn_end(game, fx):
    note(game, "turn_end", game.turn)


# ---- Orders, events, projects, victory, objectives ----

@rules.order("bounty")
def bounty(game, order, fx):
    each = game.option("bounty")
    fx.add_resources(order.empire, minerals=each * order.args["times"])
    order.empire.mod_data["bounties"] = order.empire.mod_data.get("bounties", 0) + order.args["times"]


@rules.order("tag")
def tag(game, order, fx):
    order.target.mod_data["tag"] = order.args["note"]
    if order.args["about"] is not None:
        order.target.mod_data["about"] = order.args["about"]


@rules.order_check("refused")
def refuse(game, order):
    return "The fixture refuses this order."


@rules.order("refused")
def refused(game, order, fx):
    raise AssertionError("a refused order's effect ran")


@rules.event("windfall")
def windfall(game, event, fx):
    colony = event.target_object
    fx.add_resources(colony.owner, minerals=10)
    data = game.mod_data
    data["windfalls"] = data.get("windfalls", 0) + 1


@rules.intel_project("Mod - Test Theft")
def theft(game, project, fx):
    taken = fx.add_resources(project.target, minerals=-100)
    fx.add_resources(project.empire, minerals=-taken["minerals"])
    game.mod_data["thefts"] = game.mod_data.get("thefts", 0) + 1
    return True


@rules.victory("beacons")
def beacons(game):
    lit = game.mod_data.get("beacons")
    return None if lit is None else game.empire(lit)


@rules.objective("reward")
def reward(game, objective, fx):
    fx.add_resources(objective.empire, minerals=500)
    game.mod_data["rewarded"] = objective.empire.id
