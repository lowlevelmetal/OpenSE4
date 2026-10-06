"""The Frontier Charter's rules: what its objectives do, and a word to every empire at
the start. The objectives themselves (when they are met, who wins) are in
scenarios/frontier.toml; the engine checks them at each turn's victory check.
"""

from opense4 import rules


@rules.on("after_galaxy")
def announce(game, fx):
    """The game is made: every empire learns the terms of the Charter."""
    grant = game.option("grant")
    for empire in game.empires:
        fx.log(empire, "The Charter grants the frontier to the first empire that holds eight colonies. "
                       "A second colony earns {} minerals.".format(grant), title="The Frontier Charter")


@rules.objective("charter_grant")
def charter_grant(game, objective, fx):
    """The objective second_colony was met by `objective.empire`: pay the grant."""
    grant = game.option("grant")
    if grant:
        fx.add_resources(objective.empire, minerals=grant)
        fx.log(objective.empire, "The Charter pays {} minerals for our second colony.".format(grant), title="Charter grant")
    objective.empire.mod_data["granted"] = game.turn
