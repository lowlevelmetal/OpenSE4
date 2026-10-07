# A scenario

A scenario is a prepared game: a setup (the galaxy's options, the empires) and objectives,
each of which can end the game or run an action of the mod's own. In this tutorial you read
the example [mods/examples/scenario](../../../../mods/examples/scenario/), **Frontier
Charter**: three empires race to settle the frontier; an empire's second colony earns a grant
from the Charter, and the first to hold eight colonies wins. The chapters behind it are
[Rules scripts](../rules-scripts.md#scenarios) and [rules.md](../../rules.md#scenarios).

```sh
opense4-sdk new --from-example scenario frontier --id=me.frontier
```

```text
frontier/
  mod.toml                the mod, and its option [[rules.options]] grant
  scenarios/frontier.toml the scenario: setup, options, objectives
  scripts/frontier.py     what its objectives do, and a hook at the start
  tests/test_frontier.py  its tests
```

## 1. An option of the mod's own

In `mod.toml`, the grant's size is a game option of the mod: players can set it in a game
started without the scenario, and the scenario sets it for its own.

```toml
[[rules.options]]
name = "grant"
label = "Charter grant (minerals)"
type = "int"
min = 0
max = 20000
default = 2000
```

## 2. The setup

[scenarios/frontier.toml](../../../../mods/examples/scenario/scenarios/frontier.toml)
starts with what kind of game it is:

```toml
title = "Frontier Charter"
summary = "The Charter grants the frontier to the first empire that holds eight colonies. ..."

[setup]
seed = 2026
turn_style = "simultaneous"
systems = 12
start_tech_level = 0

[[setup.empire]]
name = "Charter Settlers"
kind = "human"

[[setup.empire]]
name = "Lantern Combine"
kind = "computer"

[[setup.empire]]
name = "Drift Clans"
kind = "computer"

[options]            # the mod's own options, as this scenario wants them
grant = 3000
```

An empire may name its race (`preset`) and who plays it (`controller = "builtin"` or a mod's
player, `"<mod id>:<player>"`); left out, the race presets are taken in turn and the classic
AI plays the computer empires. Every setup key is listed in [rules.md](../../rules.md#scenarios).

## 3. The objectives

```toml
[[objective]]
name = "second_colony"
text = "Found a second colony and claim the Charter's grant"
when = { colonies = 2 }
action = "charter_grant"

[[objective]]
name = "frontier"
text = "Hold eight colonies"
when = { colonies = 8 }
victory = true
by_turn = 120
```

- **`when`** is a condition in the lessons' language ([LEARNING.md](../../../LEARNING.md#conditions)):
  a number means "at least", so `colonies = 2` holds once an empire has two colonies;
  `all`, `any` and `not` combine conditions.
- **`empire`** says whose objective it is: an empire's number, or every empire (the default).
  Each empire meets an objective once, in its own Log ("Objective met").
- **`action`** names a function of the mod's scripts to run when it is met.
- **`victory = true`** ends the game with that empire as the winner and the objective's text
  as the reason; `by_turn` closes it after that game turn.

## 4. The actions

[scripts/frontier.py](../../../../mods/examples/scenario/scripts/frontier.py):

```python
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
```

- **`@rules.objective(name)`** registers an objective's action; the scenario's `action` names
  it. `objective` has `name`, `text`, `empire` (who met it) and `scenario`.
- **`@rules.on("after_galaxy")`** runs once, when the game has been made: here, a word in
  every empire's Log. It runs in every game the mod is in, scenario or not; the option's
  value is the scenario's (3,000) or the game's.
- **`game.option("grant")`** reads the mod's own option.

## 5. Check and test

```sh
opense4-sdk check frontier              # the scenario reads; its options and actions are the mod's
opense4-sdk test frontier --turns=60
```

`check` reads every scenario of the mod: an unknown key, an option the mod does not
declare, or an action no function is registered for is an error.
[tests/test_frontier.py](../../../../mods/examples/scenario/tests/test_frontier.py) calls the
action with stand-ins for the game and the effects. Then `test` starts the scenario as the
game does and lets the computer play every empire, the human one too (on the classic data
set; each objective met is written `<objective>@<empire>`):

```text
Scenarios (60 turns each):
  ok    scenario frontier: 31 turns, objectives met: second_colony@1, second_colony@2, second_colony@0, frontier@1; it ended: Hold eight colonies (Lantern Combine won) (1.3 s)
```

To play it yourself, switch the mod on in the Mods window and press the title screen's
**Scenario** button: the Learn window's **Scenarios** tab lists Frontier Charter with its
summary, empires and objectives, and **Start Game** begins it, with you as its first human
empire. `opense4-sdk run frontier` starts the game with the mod, so the tab lists it there too.

## 6. Going further

- **Objectives for one empire**: `empire = 0` gives the human player goals of their own.
- **Shape the galaxy**: a `generate_galaxy` hook can rename systems, change planets
  (`fx.set_planet`) or replace the quadrant with a map of yours (`fx.replace_galaxy`,
  [rules.md](../../rules.md#effects)).
- **Starting conditions**: an `after_galaxy` hook can give each empire resources, ships
  (`fx.create_vehicle`) or technology (`fx.grant_tech`).
