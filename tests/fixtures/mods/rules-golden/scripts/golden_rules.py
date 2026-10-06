"""The rules tier's golden games (tests/sdk/test_sdk_rules_golden.cpp): a rule on
every colony, a declared ability given an effect, and an event of the mod's own."""

from opense4 import rules


@rules.on("colony_end_of_turn")
def overcrowding(game, colony, fx):
    """A colony past nine tenths of its room grows unhappy, and says so now and then."""
    room = colony.max_population
    if not room or colony.total_population * 10 <= room * 9:
        return
    fx.change_happiness(colony, -1)
    crowded = colony.mod_data.get("crowded", 0) + 1
    colony.mod_data["crowded"] = crowded
    if crowded % 5 == 1:
        fx.log(colony.owner, colony.planet.name + " is overcrowded.", title="Overcrowding")
    game.mod_data["crowded"] = game.mod_data.get("crowded", 0) + 1


@rules.on("empire_end_of_turn", step="supply", when="after")
def field_batteries(game, empire, step, when, fx):
    """Test Field Battery: a vehicle carrying it gains that much supply each turn."""
    charged = 0
    for raw in game.raw["vehicles"]:
        if raw["owner"] != empire.id or raw["supply"] is None:
            continue
        value = game.ability(raw["id"], "Test Field Battery", "vehicle")
        if value:
            fx.change_supply(raw["id"], value)
            charged += 1
    if charged:
        game.mod_data["charged"] = game.mod_data.get("charged", 0) + charged


@rules.event("dust_storm")
def dust_storm(game, event, fx):
    """A vehicle caught in a dust storm takes a little damage."""
    vehicle = event.target_object
    if vehicle is None:
        return
    fx.damage(vehicle, 3 + game.rng.below(5), "Caught in a dust storm.")
    game.mod_data["storms"] = game.mod_data.get("storms", 0) + 1
