# Rules scripts

The rules tier of the modding SDK (docs/MODDING_SDK.md, section 7): Python functions a
mod registers for the game's moments and events, which change the game through
effects that keep it valid. With them a mod gives its declared abilities an effect, adds
orders, events, intelligence projects, game options, victory conditions and scenarios,
and builds data records with generators.

Rules scripts run inside the game, on the script runtime ([runtime.md](runtime.md)), on
the computer that plays the turn (a network or e-mail game's host). They are part of the
game's rules: every player of a game has the same ones, and they resolve the same way on
every computer.

- Engine side: `src/game/hooks.hpp` (the moments and events, the call sites' interface),
  `src/sdk/rules_engine.*` (the session's rules), `src/sdk/rules_effects.cpp` (the
  effects), `src/sdk/rules.hpp` (orders, options and abilities for the rest of the
  program), `src/sdk/scenario.*`, `src/sdk/generators.cpp`.
- Python side: `opense4.rules` (the registry, `Game`, `Effects`) and
  `opense4._rules_engine` (the dispatcher the engine calls).
- Tests: `tests/sdk/test_sdk_rules*.cpp` with the fixture mods
  `tests/fixtures/mods/rules-fixture` and `rules-golden`, and
  `tests/sdk/python/test_sdk_rules.py`.

## A first rules mod

```text
crowding/
  mod.toml
  scripts/crowding.py
```

```toml
[mod]
id = "example.crowding"
name = "Crowding"
version = "1.0.0"
api = 1

[[rules.options]]
name = "limit"
label = "Crowding limit (percent of room)"
type = "int"
min = 50
max = 100
default = 90
```

```python
from opense4 import rules


@rules.on("colony_end_of_turn")
def overcrowding(game, colony, fx):
    room = colony.max_population
    if room and colony.total_population * 100 > room * game.option("limit"):
        fx.change_happiness(colony, -1)
        fx.log(colony.owner, colony.planet.name + " is overcrowded.", title="Overcrowding")
```

Every function a rules script registers gets `game`, the whole game as typed objects,
the hook's own arguments, and `fx`, the effects. It changes the game only through `fx`
(and its mod data, below).

## The mod's files

| | |
|---|---|
| `scripts/` | The rules scripts. Their files are added at the interpreter's root, as `ai/`'s are for computer players: `scripts/crowding.py` is the module `crowding`, and a mod's modules import one another by those names. Two mods (or a mod's `ai/` and `scripts/`) with a module of the same name conflict: the later one's is left out and logged, so name modules after the mod. |
| `[rules]` in mod.toml | What the scripts declare to the engine: the table below. |
| `scenarios/*.toml` | Scenarios (below). |
| `data/*.py` | Data generators (below). |

```toml
[rules]
players_see_mod_data = true    # computer players see the mod's data on their own things (default false)
modules = ["crowding"]         # the modules under scripts/ to import, in order (default: every module directly under scripts/, by name)
# [[rules.options]], [[rules.orders]], [[rules.events]], [[rules.intel_projects]], [[rules.victory]]: below
```

Unknown keys are errors, as everywhere in mod.toml. Names of options, orders, events and
victory conditions are lowercase letters, digits and `_`, starting with a letter.

A game runs a mod's rules when the mod is in the game's mod set (`GameState::mods`, the
mods the game was created with) and has a `scripts/` folder. A game without such mods
makes no rules session at all and plays exactly as without the SDK: its save files and
checksums (at format 8) are those of OpenSE4 before the SDK.

## Sessions

The engine calls rules functions from the same kind of session as computer players
([ai-protocol.md](ai-protocol.md), section 2), and from the same interpreter when a call
needs both:

- **One session per engine call**: a simultaneous turn, a turn-based call (each player's
  turn start, each command a player gives, each turn end), the creation of a game, and a
  mod order given outside such a call (the client's own copy of a simultaneous game).
- **The interpreter** starts the first time a function must run, on a thread of the
  session's own with an 8 MiB stack, with the `opense4` package, the computer players'
  `ai/` files and the rules mods' `scripts/` files. Then the rules modules are imported,
  in load order (each mod's in name order, or its `modules`), and their decorators
  register their functions with the mod they belong to.
- **What a mod registers** is learnt once per process (keyed by the mods' identities), so
  later sessions know whether any function waits for a moment without starting an
  interpreter: an engine call none of whose moments has a function costs nothing.
- **Nothing lasts in module globals** beyond a session, and how calls are grouped into
  sessions differs between a host processing a turn and a player's computer: keep what
  must last in mod data. A session may also hold computer players' objects; don't rely on
  `id()` numbers or on the order of sets of objects.

## Hooks

Two kinds of hooks:

- **Moments** run where the engine reaches them. Their effects apply at once.
- **Events** are noted where they happen, with what they need from the game as it was
  then, and delivered in order at the next safe point: the end of a movement day (or of a
  turn-based live run), the end of each step of an empire's end of turn, after the victory
  check, after the event step, after the turn end, and at the end of the engine call. So
  no rules function runs, and nothing is added to or removed from the game, inside the
  engine's own loops. An event raised while events are delivered (an effect that destroys
  a vehicle) is delivered in the same round; more than 100,000 in one round is taken for a
  loop and the rest are dropped (logged).

The functions of one hook run mod by mod in load order, each mod's in the order they
were registered. A mod's functions for a hook get one engine call: if one raises, the
others of that mod for that hook do not run that time.

| Hook | When | Function |
|---|---|---|
| `new_game` | A new game is made: first thing, before the quadrant. The options are set; the galaxy and the empires are not made yet. | `f(game, setup, fx)`: `setup.seed`, `setup.empires` (each `{name, kind, preset, controller}`). Effects: `set_option`, `set_mod_data` (the game's). |
| `generate_galaxy` | The quadrant is made (generated or from a map), before the empires are placed. | `f(game, fx)`. Effects: the galaxy's (`set_planet`, `rename`, `link_systems`, `replace_galaxy`). |
| `after_galaxy` | The game is made: empires, homeworlds, designs, pools. | `f(game, fx)` |
| `turn_start` | Simultaneous: the start of the turn, before the orders. Turn-based: once per game turn, when its first player's turn is about to start. | `f(game, fx)` |
| `orders_applied` | Simultaneous: every empire's orders are in (the players', and the computer players' start-of-turn orders); movement comes next. Turn-based: each player's turn is over (its orders were carried out as given), before its end-of-turn processing. | `f(game, empire, fx)`: `empire` None in a simultaneous turn, the player in a turn-based one. |
| `movement_day` | Simultaneous: after each of the 30 movement days, its battles fought. Turn-based: after each live run of a player's groups (its turn start's, and each command's that moves something), as day 0. | `f(game, day, fx)` |
| `vehicle_entered_sector` | Event: a vehicle stepped into a sector (each member of a moving group, warps included). | `f(game, vehicle, location, fx)` |
| `before_battle` | Where a battle check finds a possible battle, before the mines strike; a battle may still not follow. | `f(game, site, fx)`: `site.location`, `site.empires` (those with objects there). |
| `after_battle` | A battle was fought and its record made. | `f(game, battle, fx)`: the battle's record (as the view's `battles` hold it). |
| `vehicle_destroyed` | Event: a vehicle was lost. | `f(game, vehicle, cause, fx)`: the vehicle as it was (it is gone from `game`); `cause` `"battle"`, `"mines"`, `"hazard"` (storms, a system's centre, turbulence, no supply left, used up), `"event"`, `"maintenance"` or `"empire_destroyed"`. Scrapped, analyzed and self-destructed vehicles are not lost. |
| `empire_end_of_turn` | Before and after each of these steps of an empire's end of turn (docs/ENGINE.md, "Turn order"): `intelligence`, `research`, `income`, `maintenance`, `population`, `happiness`, `construction`, `repair`, `supply`, `ground_combat`. | `f(game, empire, step, when, fx)`; `when` `"before"` or `"after"`. `@rules.on("empire_end_of_turn", step="income", when="after")` narrows it. |
| `colony_end_of_turn` | Each colony of an empire, in planet order, after its end-of-turn steps. | `f(game, colony, fx)` |
| `colony_founded` | Event: a colony ship founded a colony. | `f(game, colony, fx)` |
| `vehicle_built` | Event: construction finished a ship or base, or units (which go into cargo). | `f(game, built, fx)`: `built.vehicle` (None for units), `design`, `count`, `colony` (where, or None for a space yard ship), `location`, `empire`. |
| `tech_researched` | Event: an empire reached a tech level (research, ruins, trades, a surrender, espionage, `fx.grant_tech`). | `f(game, empire, tech, level, fx)`: `tech` the rules view's tech area. |
| `treaty_changed` | Event: the treaty between two empires changed. | `f(game, empire, other, treaty, old_treaty, fx)` (treaty names) |
| `message_sent` | Event: a diplomatic message reached its empire. | `f(game, message, fx)` |
| `event_fired` | Event: an event took effect: a classic one (Events.txt) or a mod's. | `f(game, event, fx)`: `event.name` (the classic event's Type, or the mod event's name), `event.mod` (None for a classic one), `empire`, `colony`, `vehicle`, `location`. |
| `check_victory` | Each game turn, after the classic victory check, while the game goes on; the mods' victory conditions and the scenario's objectives follow. | `f(game, fx)`; `fx.victory` ends the game. |
| `turn_end` | The end of the game turn, after the event step, before its flags are cleared and the turn number advances. | `f(game, fx)` |

`game.turn` is the turn being processed throughout (`GameState::turn`).

## Reading the game

`game` is an `opense4.rules.Game`: the view of [view.md](view.md) for the whole game, as
if every empire's affairs were ours (`whole` is true, `game.me` is None), with the same
typed objects as a computer player's view ([python-api.md](python-api.md), "The view").
It is read from the engine a part at a time: `game.colonies` reads every colony,
`game.colony(planet)` that one only. What is read is read again after each effect.

| | |
|---|---|
| `game.empires`, `.systems`, `.objects`, `.colonies`, `.vehicles`, `.fleets`, `.designs`, `.messages`, `.battles` | The lists; vehicles destroyed but not yet removed are left out. |
| `game.empire(id)`, `.colony(planet)`, `.vehicle(id)`... | One thing, or None. |
| `game.turn`, `game.options`, `game.simultaneous` | The game. |
| `game.option(name)` | One of the mod's own options (below). |
| `game.mod_data`, `x.mod_data` | The mod's data (below). |
| `game.rng.below(n)`, `.range(a, b)`, `.chance(percent)`, `.choice(list)`, `.shuffle(list)` | The game's own random numbers (`GameState::rng`): each draw takes the engine's next number, so a game played again draws the same. Python's `random` is not available. |
| `game.ability(thing, name, kind=None)` | An ability's value on a vehicle (its design), design, colony (its facilities), system (its own and its objects'), component, facility or hull, combined as the ability combines; None when it does not carry it. An id needs its `kind`. |
| `game.query(name, **args)` | A query of view.md ("Queries") on the whole game, as plain values. |
| `game.rules` | The rules view (`opense4.rules.Rules`). |
| `rules.log(text)` | A line in the game's log file (opense4.log on the host), not an empire's Log. |

Reading costs budget: 5 bytecodes per value the engine builds. In a loop over many
things, read the maps (`game.raw["vehicles"]`) rather than wrap every record.

## Effects

`fx` is an `opense4.rules.Effects`: each method is one engine function that checks what
it is given and keeps the game valid. Objects may be given as the game's records or as
ids. A refused effect raises in the script (a `ValueError` saying why, a `TypeError` for
an argument of the wrong type, a `RuntimeError` for an effect not allowed at this
moment) and changes nothing. Numbers are whole numbers: a float cannot cross into the
engine. Each effect costs 1,000 bytecodes.

| Effect | Does | Refused |
|---|---|---|
| `add_resources(empire, minerals=0, organics=0, radioactives=0)` | Adds to (negative: takes from) an empire's stores, never below 0. Returns what changed, `{minerals, organics, radioactives}`. | no such living empire |
| `add_research(empire, points)`, `add_intelligence(empire, points)` | Adds to the pool the empire's research (intelligence) spends this turn; returns the pool. | |
| `grant_tech(empire, area, levels=1)` | Gives tech levels (as research does: its Log entries, `tech_researched`); returns the level. | no such area |
| `change_population(colony, millions, race=None)` | Adds people of a race (an empire; the owner's by default) up to the colony's room, or takes them; returns the change made. | the colony would have nobody left |
| `change_happiness(colony, change)` | Positive: happier (less anger), within the colony's range; returns its anger. | |
| `set_colony_type(colony, colony_type)` | | not one of the owner's colony types |
| `set_plague(colony, level)` | | |
| `damage(vehicle, amount, cause="Damaged.")` | Damages it as a hazard does (components in the engine's order, units of a group), with its Log entries; True when it is destroyed. | |
| `repair(vehicle, components=None)` | Repairs damaged components (all, or that many) in design order; returns how many. | |
| `change_supply(vehicle, amount)` | Within 0 and its capacity; returns its supply. | |
| `create_vehicle(empire, design, location, count=1)` | A new vehicle of one of the empire's designs (units: a group of `count`), as construction places one; returns the Vehicle. | another empire's design; the ship limit |
| `remove_vehicle(vehicle)` | Takes it out of the game (no loss is counted, no `vehicle_destroyed`). | |
| `add_facility(colony, facility)` | Returns its place in the colony's list. | no free facility slot |
| `remove_facility(colony, index)` | Returns which facility it was. | no such place |
| `set_treaty(empire, other, treaty)` | As a treaty signed (`treaty_changed`). | the empires have not met |
| `log(empire, text, title="", category="misc", location=None)` | An entry in the empire's Log. | |
| `fire_event(name, target=None)` | Fires one of the mod's events at a target (a record or `(kind, id)`), at the next safe point. | the mod declares no such event |
| `set_planet(planet, minerals=, organics=, radioactives=, conditions=, size=, surface=, atmosphere=)` | Changes a planet or asteroid field: resource values, conditions (hundredths, 0 to 150), size (a planet size the data set knows), surface, atmosphere. | not a planet |
| `rename(thing, name)` | A system, stellar object, vehicle or fleet. | |
| `link_systems(a, b)` | A new pair of warp points on the two systems' edges (sectors drawn from the game's generator); returns their ids. | linked already; no free edge sector |
| `replace_galaxy(map_text)` | `generate_galaxy` only: the quadrant is replaced by a map in OpenSE4's map format ([MAPS.md](../MAPS.md)); returns its warnings. | elsewhere; a map that does not read |
| `set_option(name, value)` | `new_game` only: one of the mod's options, or one of these classic ones: `event_frequency`, `max_event_severity`, `tech_cost`, `start_tech_level`, `quadrant_size`, `system_count`, `no_ruins`, `finite_resources`, `all_systems_seen`, `allow_intel`. | elsewhere; out of range |
| `victory(empire=None, reason="")` | Ends the game, with a winner or none; every living empire's Log says so, and `GameState::endReason` keeps the reason. | outside `check_victory`, `turn_end`, a victory condition or an objective's action |
| `set_mod_data(thing, value)` | The mod's data on a thing (None: the game). | not plain values; too large |

An effect applied before a function raises stands; only the mod data it changed is not
kept (below).

## Mod data

A mod keeps data of its own on the game, an empire, a colony or a vehicle:
`game.mod_data`, `empire.mod_data`, `colony.mod_data`, `vehicle.mod_data`. Each is a dict
of plain values (None, True and False, whole numbers, text, lists, dicts with text keys),
empty at first; a mod sees only its own. Change it in place: what it holds when the
function returns is kept, unless the function failed. `fx.set_mod_data(thing, value)`
replaces it at once.

- It is part of the game (save format 9): saved, sent to players' computers and
  checksummed, at most `GameOptions::modDataLimit` bytes as JSON per mod and thing (1 MiB
  by default; larger fails the function). An empty dict is no data.
- A vehicle's data goes with the vehicle; a colony's stays with the colony when it
  changes hands.
- **What players' computers get**: a network or e-mail game sends each player a copy of
  the game as that player sees it (`game::redactForEmpire`). It holds the data a mod keeps
  on that player's own empire, colonies and vehicles when the mod says
  `players_see_mod_data = true`, and nothing else of any mod's data. A computer player
  sees the same in its view: `view.my.mod_data`, and `empire.mod_data`,
  `colony.mod_data` and `vehicle.mod_data` of its own things, as `{mod id: value}`
  ([view.md](view.md), `my_empire`). Without that line no player sees any of it.

## Abilities

A mod declares an ability name in a data patch ([packages-and-data.md](packages-and-data.md),
"New ability names"), puts it on components, facilities, hulls and system types like any
other, and gives it an effect in a hook. The rules golden mod's field batteries:

```python
@rules.on("empire_end_of_turn", step="supply", when="after")
def field_batteries(game, empire, step, when, fx):
    for raw in game.raw["vehicles"]:
        if raw["owner"] == empire.id:
            value = game.ability(raw["id"], "Test Field Battery", "vehicle")
            if value:
                fx.change_supply(raw["id"], value)
```

Computer players read declared abilities like the game's own: in the rules view's
`abilities` (with how they combine), on components, facilities and hulls
(`component.ability_value(name)`), and with the `ability` query. The engine's
`sdk::abilityValue` answers for C++.

## Orders

A mod declares orders; a human gives them from the order strip's Mod Orders, a panel's
button or a key ([interface.md](interface.md), "Mod orders"), and so can computer players
and external bots. Each is one command kind,
`cmd::ModCommand` (`mod_command` in [commands.md](commands.md)), so it travels with a
turn's orders over the network, in e-mail games and in replays.

```toml
[[rules.orders]]
name = "overcharge"
label = "Overcharge shields"
description = "Doubles the shields for a turn, at a cost in supply."
applies_to = "vehicle"         # "self" (the empire; the default), "vehicle", "fleet" or "colony" of its own, or "empire" (another)

[[rules.orders.args]]
name = "power"
type = "int"                   # int, bool, text, or an id: empire, system, object, colony, vehicle, fleet, design
min = 1
max = 3
default = 1                    # without a default an int, bool or text argument must be given; an id may be None
```

```python
@rules.order_check("overcharge")
def can_overcharge(game, order):
    if order.target.supply < 10 * order.args["power"]:
        return "Not enough supply."        # None (or True): it may be given


@rules.order("overcharge")
def overcharge(game, order, fx):
    fx.change_supply(order.target, -10 * order.args["power"])
    order.target.mod_data["overcharged"] = game.turn
```

Giving one (`game::apply`, as any command):

1. The engine checks it against the declaration: the mod is one of the game's rules
   mods, the order is declared, it names exactly its target (the empire's own vehicle,
   fleet or colony, another living empire, or nothing for `self`), and its arguments are
   the declared ones, of their types and in their ranges, those left out taking their
   defaults.
2. The mod's check runs, if it registered one; a text it returns is the refusal.
3. The effect runs at once, in both turn styles, as a cargo transfer does; its events
   are delivered after it.

`order` has `mod`, `name`, `empire` (who gives it), `target`, `args` (every declared
argument). A refused order comes back with its reason, as any command's; a check or
effect that fails is a refusal too (and counts as a failure of the mod, below). An order
cannot be given through a computer player's `apply` service while its script runs: give
it with the call's commands.

- **Computer players**: `cmd.mod_command(mod="example.shields", name="overcharge",
  vehicle=ship, args={"power": 2})`; external bots send the same map as JSON.
- **The client** lists the orders an object takes with `sdk::modOrders(rules, state,
  empire, {"vehicle", id})` (`self`, `vehicle`, `fleet`, `colony`, `empire`), and builds
  one with `sdk::modOrderCommand(choice, target, args)`, which it gives as any command.
  Where they appear: the order strip's free place lights as **Mod Orders** while the
  selection (or the empire itself) takes any, a menu of them; a mod's panels can have a
  button for one; a mod can suggest a key for each. Their arguments are asked for one after
  another (a number, yes or no, text, a choice, a pick on the map); a mod's `ui/` gives them
  a picture, questions and lists of choices ([interface.md](interface.md)).
- In a simultaneous game the player's own computer applies the order to its copy when it
  is given, and the host again when it processes the turn.

## Events

```toml
[[rules.events]]
name = "dust_storm"
label = "Dust storm"
chance = 25                    # percent per game turn, 0 to 100
target = "vehicle"             # "empire", "colony", "vehicle", "system" or "none"
first_turn = 3                 # not rolled before this game turn
option = "storms"              # optional: a switch of the mod's options that turns it on
```

```python
@rules.event("dust_storm")
def dust_storm(game, event, fx):
    fx.damage(event.target_object, 3 + game.rng.below(5), "Caught in a dust storm.")
```

Each game turn, after the classic event step (the due timed events, then the galaxy's
one roll), each declared event of each rules mod in load order rolls its chance from the
event step's generator; one that comes up picks its target among the living empires, the
colonies of living empires, the vehicles or the systems, and its effect runs. Then every
mod's `event_fired` hears of it, as of a classic event. `fx.fire_event(name, target)`
fires one at once (at the next safe point). An event with no registered effect does not
roll.

## Intelligence projects

A mod adds projects to IntelProjects.txt with a data patch, giving them a Type of its own,
and declares that Type:

```toml
[[rules.intel_projects]]
type = "Mod - Data Theft"
```

```python
@rules.intel_project("Mod - Data Theft")
def data_theft(game, project, fx):
    taken = fx.add_resources(project.target, minerals=-project.amount)
    fx.add_resources(project.empire, minerals=-taken["minerals"])
    return True               # it took effect; False: it failed
```

Such a project is ordered, funded and defended against as the classic attacks are
(spec 05 §2): it needs contact with its target empire, its progress must reach its cost,
and counter-intelligence may stop it. When it goes ahead the function runs in place of
the classic effect; on True the project's own messages are sent (with the suspect roll),
on False the source logs a failure. `project` has `type`, `name`, `project` (its
index), `empire`, `target`, `target_planet`, `target_vehicle`, `third_empire`,
`target_tech` and `amount` (its Effect Amount).

## Game options

```toml
[[rules.options]]
name = "limit"
label = "Crowding limit (percent of room)"
description = "..."
type = "int"                   # or "bool": a switch (0 or 1); a switch has no min and max
min = 50
max = 100
default = 90
```

- The game keeps every declared option's value (`GameOptions::modOptions`, save format
  9): a new game fills in the defaults of those its setup does not set, in load order and
  declaration order, and keeps set values within their ranges.
- **Setting them**: Game Setup's Mod Options (its Game Settings page), Quick Start's Mod
  Options and the network lobby's (the host changes them, the others see them) show each
  with its value ([docs/SETUP.md](../SETUP.md) "Mods' options"); the headless setup model
  lists them with their values (`setup::modOptionRows`) and sets one by `"<mod id>:<name>"`
  (`setup::setModOption`); `opense4-server`'s setup files set them in
  `[options.mod."<mod id>"]` (docs/MULTIPLAYER.md, "Setup files"); a scenario in its
  `[options]`; `fx.set_option` in `new_game`. A mod's `text/` gives their labels in other
  languages.
- **Reading them**: `game.option(name)` (the mod's own), and `game.options.mod_options`
  in rules functions and computer players' views (`{mod, name, value}` each).

## Victory conditions

```toml
[[rules.victory]]
name = "relics"
label = "The relics were gathered."    # the reason the game ends with
option = "relic_victory"               # optional: a switch of the mod's options that turns it on
```

```python
@rules.victory("relics")
def relics(game):
    for e in game.empires:
        if e.alive and e.mod_data.get("relics", 0) >= 5:
            return e          # the winner (an Empire or its id); True: the game ends without one
    return None               # the game goes on
```

At each game turn's victory check, after the classic conditions and the mods'
`check_victory` hooks, while the game goes on, each declared condition that is switched
on is tested; the first that answers ends the game with its winner and its label as the
reason. A `check_victory` hook may also end it with `fx.victory(empire, reason)`.

## Scenarios

A scenario is a setup and objectives, in `scenarios/<name>.toml`:

```toml
title = "Outpost"
summary = "Found a second colony; the first to do it wins."

[setup]
seed = 11
turn_style = "simultaneous"    # or "turn_based" (the default)
systems = 10                   # also: quadrant_size, event_frequency, max_event_severity, tech_cost,
                               # start_tech_level, starting_planets, racial_points, ai_difficulty,
                               # no_ruins, finite_resources, all_systems_seen, allow_intel
[[setup.empire]]
name = "Wardens"
kind = "human"                 # human, computer or neutral
preset = ""                    # a race preset; default: the presets in turn
controller = "builtin"         # or "<mod id>:<player>"

[[setup.empire]]
name = "Drifters"
kind = "computer"

[options]                      # the mod's own options
bounty = 40

[[objective]]
name = "second_colony"
text = "Found a second colony"
empire = 0                     # whose: an empire's number, or "every" (the default)
when = { colonies = 2 }
victory = true                 # the empire that meets it wins
action = "reward"              # optional: @rules.objective("reward") runs when it is met
by_turn = 40                   # optional: it can no longer be met after this game turn
```

- `when` is a condition of the lessons' language ([LEARNING.md](../LEARNING.md),
  "Conditions"), with the same keys. What the game state answers works (`colonies`,
  `population`, `ships`, `turn`, `treaty`, `score`, `minerals`...); the keys about the
  client (open windows, selections, commands given) never hold, and
  `enemy_ships_destroyed` and `planets_captured` count from 0 at each check.
- `sdk::startScenario(rules, mod, name)` makes the game: the setup, the mod's options it
  sets, and the scenario recorded in the game (`GameState::scenario`, save format 9).
- **Starting one**: the Learn window's Scenarios tab (the title screen's Scenario button)
  lists the scenarios of the rules mods in use, with their summaries, empires and
  objectives, and Start Game begins one, played by its first human empire (a scenario
  without one is for computer players: the tab says so). A dedicated server's setup file
  starts one with `scenario = "<mod id>:<name>"`: its seed, setup options, empires and the
  mod's options it sets, before the file's own keys, which take their places (a network
  host's players join the lobby in place of its human empires; MULTIPLAYER.md "Setup
  files"). A mod's `text/` can give the title, summary and objectives in other languages.
- At each victory check, after the mods' victory conditions, each objective not yet met is
  tested for each of its empires (living ones, in empire order). One that holds is logged
  in that empire's Log ("Objective met"), recorded (`GameState::scenario.met`, as
  `<objective>@<empire>`), runs its action with `objective` (`name`, `text`, `action`,
  `empire`, `scenario`) and, with `victory`, ends the game with that empire as winner
  and the objective's text as the reason.

The fixture `tests/fixtures/mods/rules-fixture/scenarios/outpost.toml` is a complete
small scenario.

## Data generators

A generator is a Python file in a mod's `data/` whose `generate()` returns a patch, a
dict shaped like a `.toml` patch file ([packages-and-data.md](packages-and-data.md),
"Data patches"):

```python
# data/beam_line.py
def generate():
    adds = []
    for level in range(2, 13):
        adds.append({"name": "Needle Beam %d" % level, "copy_from": "Needle Beam",
                     "set": {"Tonnage Space Taken": 10 * level}})
    return {"components": {"add": adds}}
```

- It runs when the data set loads, in its file's turn among the mod's patches (by name),
  in the sandbox, in an interpreter of its own (64 MiB, 500 million bytecodes). It gets
  nothing from the game: it builds records from its own code.
- Its result is checked and applied exactly as a `.toml` patch; whole numbers only.
- The same file gives the same patch on every computer, and the file is part of the mod's
  identity, so the patch need not be hashed separately. `opense4-sdk dump` writes the
  data set it makes.
- Its file name must be a Python name (`beam_line.py`). A failure is a load error with the
  mod, the file and the traceback's lines:
  `mod example.beams: data/beam_line.py: generate() failed: Exception: ZeroDivisionError: ...`.

The client, `opense4-server` and `opense4-sdk` run generators; without the SDK
(`mods::defaultGeneratorRunner`) a generator is an error that says so.

## Budgets and failures

| Limit | Default | Set by |
|---|---|---|
| One call of a mod's function | 50 million bytecodes | `GameOptions::rulesHookBudget`; setup files' `rules_hook_budget` |
| All of one mod's calls in one game turn | 2,000 million bytecodes | `rulesTurnBudget`; `rules_turn_budget` |
| One mod's data on one thing | 1 MiB as JSON | `modDataLimit`; `mod_data_limit` |
| The interpreter's heap, shared with computer players | 64 MiB | the session |

A function **fails** when it raises, runs out of its call's budget or of memory, recurses
too deep, returns what its call does not take, or leaves mod data that is not plain or too
large. Then:

- the host's log (opense4.log) says which mod, function and why, with the traceback;
- what its effects did stands; the mod data it changed is not kept;
- that hook (or order, event, project, victory condition, objective action) of that mod
  is skipped for the rest of the game turn;
- after three failures of a mod in one game turn, all of that mod's functions are off for
  the rest of the turn (its orders are refused, saying so). The next game turn starts
  afresh.

A mod whose calls use up its turn's budget is not failing: its functions are skipped for
the rest of the turn, and the log says so. What the game keeps of this
(`GameState::modRules`: failures and the functions skipped this turn) is saved and
checksummed; the budget a turn has used so far is kept only in memory.

Budgets are safety limits, set far above what rules need. A call's count of bytecodes is
the same on every computer for the same script and arguments, but it can depend on what
ran before it in the same interpreter (a module's first import), so a game whose rules run
near their limits may be decided differently when played again in other engine calls.

## The same on every computer

Rules functions resolve the same way everywhere, as the engine does:

- the game's random numbers only (`game.rng`); the runtime has no clock, files, threads or
  network ([runtime.md](runtime.md));
- whole numbers into the game; dict order is insertion order, and set order of numbers and
  text the same everywhere;
- no state in module globals beyond a call (sessions differ between computers);
- the engine calls them in a fixed order: hooks by moment, mods in load order, functions
  in registration order, events in the order they happened.

`tests/sdk/test_sdk_rules_golden.cpp` plays 60 turns of a game with a rules mod in each
turn style and compares checksums with golden values that every platform CI builds must
reproduce.

## Checking a rules mod

`opense4-sdk check` also checks a mod's rules (`sdk::checkModRules`): the files under
`scripts/` have importable names, compile and import; every declared order, event,
intelligence project type and victory condition has its function, and every function
the scripts register for those is declared; the scenarios read, their options are the
mod's, and their actions are registered.

## Costs

Measured in an optimized build on a desktop x86_64 computer, on the engine's test game
(four computer empires, 14 systems, about 20 colonies after 30 turns;
`tests/sdk/test_sdk_rules_golden.cpp`, "what a rules mod costs a turn"):

| | A simultaneous turn |
|---|---|
| Without rules mods | 3.1 ms |
| A mod with one hook a turn (`turn_end`) | +2.5 ms: starting the interpreter and importing the package, once per engine call that runs rules |
| The rules golden mod (overcrowding on every colony, field batteries after every empire's supply step, a dust storm event) | +5.2 ms |

A hook call itself costs some tens of microseconds plus what its function does; an engine
call none of whose moments has a function costs nothing.

## For engine developers

- `game/hooks.hpp`: the hooks (`Hook`), their arguments (`HookArgs`), `RulesHooks` (the
  interface the SDK implements) and the call sites' shorthand (`runHook`, `hookWanted`,
  `deliverHooks`, `noteVehicleLost`). `TurnContext::hooks` is the call's; a call site
  asks `hookWanted` before building anything.
- The call sites: `turn.cpp` (the simultaneous turn and `empireEndOfTurn`),
  `turn_based.cpp`, `setup.cpp` (`createGame`), `movement.cpp` (days, steps),
  `combat_space.cpp` (battles, losses), `combat.cpp` (mines), `movement_util.cpp`
  (hazards), `events.cpp`, `economy.cpp` (maintenance), `economy_queue.cpp`,
  `movement_upkeep.cpp` (colonization), `research.cpp`, `diplomacy.cpp`, `score.cpp`,
  `intel.cpp`.
- `game::CallSession` sets up a call's session (players and hooks) on its context;
  `end()` delivers the waiting events and ends it. A battle stop or a fault leaves without
  `end()`: the call is put back and made again, and the hooks run again the same way.
- `game::applyModCommand` hands mod orders to the handler the SDK installs
  (`sdk::installPlayers`), which uses the rules of the session playing that state, or a
  session made for the order alone.
- Unmodded games do not change: they make no session, and hashed at save format 8 they
  give the checksums recorded before the SDK.
