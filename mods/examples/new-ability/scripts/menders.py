"""Field Repair: the effect of the ability data/menders.toml declares.

After each empire's repair step (the end of its turn, where docks and repair facilities
work), every vehicle of that empire that carries Field Repair mends that many of its
damaged components, wherever it is. The empire's Log says so, and the empire's mod
data counts the components mended in the whole game.
"""

from opense4 import rules

ABILITY = "Field Repair"


@rules.on("empire_end_of_turn", step="repair", when="after")
def field_repairs(game, empire, step, when, fx):
    mended = 0
    # The vehicles as maps: reading a few fields of many vehicles costs less this way
    # than through the typed records (docs/sdk/rules.md, "Reading the game").
    for raw in game.raw["vehicles"]:
        if raw["owner"] != empire.id or not raw["damage"]:
            continue
        crews = game.ability(raw["id"], ABILITY, "vehicle")
        if not crews:
            continue
        done = fx.repair(raw["id"], components=crews)
        if done:
            mended += done
            fx.log(empire, "Field crews mended {} component{} of {}.".format(done, "" if done == 1 else "s", raw["name"]),
                   title="Field repairs", location=raw["location"])
    if mended:
        empire.mod_data["mended"] = empire.mod_data.get("mended", 0) + mended
