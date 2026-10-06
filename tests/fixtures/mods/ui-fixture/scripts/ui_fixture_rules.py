"""The interface fixture's rules: three orders that keep what they did in the mod's data,
which the fixture's panels, columns and Empires page show."""

from opense4 import rules


@rules.order("mark")
def mark(game, order, fx):
    order.target.mod_data["mark"] = order.args["colour"]


@rules.order("beacon")
def beacon(game, order, fx):
    count = order.args["count"]
    fx.add_resources(order.empire, minerals=count * game.option("beacon_bonus"))
    order.empire.mod_data["beacons"] = order.empire.mod_data.get("beacons", 0) + count


@rules.order("survey")
def survey(game, order, fx):
    surveyed = order.target.mod_data.setdefault("surveyed", [])
    if order.args["system"] is not None:
        surveyed.append(game.system(order.args["system"]).name)


@rules.objective("lamplit")
def lamplit(game, objective, fx):
    game.mod_data["lamplit"] = objective.empire.id
