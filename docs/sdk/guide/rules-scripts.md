# Rules scripts

Data patches change the numbers of the game; rules scripts change what happens. A rules
script is Python that the engine calls at fixed moments of a turn and on events, and that
changes the game through **effects**, engine functions that keep the game valid. With them
a mod gives a declared ability an effect, adds orders, events, intelligence projects, game
options and victory conditions, and makes scenarios; with data generators it builds data
records in Python.

This chapter is the guide's path through the rules tier, built on three example mods. The
complete reference is [Rules scripts](../rules.md): every hook, argument and effect is
there, and this chapter links to it rather than repeat it.

| Example | Shows | Tutorial |
|---|---|---|
| [weapon-line](../../../mods/examples/weapon-line/) | A data generator: a technology and six levels of a weapon, built in Python | [A weapon line from a generator](tutorials/weapon-line.md) |
| [new-ability](../../../mods/examples/new-ability/) | A declared ability, a component that carries it, and the hook that gives it its effect; mod data | [A new ability with an effect](tutorials/new-ability.md) |
| [scenario](../../../mods/examples/scenario/) | A scenario: a setup, objectives, an option of the mod's own, an objective's action, a hook at the start | [A scenario](tutorials/scenario.md) |

## Data, computer player or rules?

| You want | Use |
|---|---|
| Different numbers: costs, sizes, damage, chances, settings | A data patch ([The data files](data/README.md)) |
| Many similar records: a weapon line, a family of hulls | A data generator (`data/*.py`) |
| A new kind of effect: an ability nothing in the game reads yet, a hazard, a bonus | A declared ability and a rules hook |
| Something to happen each turn, or when something else happens | A rules hook |
| A new thing players can order | A mod order |
| A new random event, intelligence project, game option, way to win | `[[rules.events]]`, `[[rules.intel_projects]]`, `[[rules.options]]`, `[[rules.victory]]` |
| A prepared game with goals | A scenario (`scenarios/*.toml`) |
| How computer empires decide | A computer player ([Computer players](computer-players.md)) |

## A rules mod

```text
my-rules/
  mod.toml            [rules] and its declarations: options, orders, events, projects, victory
  scripts/my_rules.py the functions (their files are at the root: scripts/my_rules.py is the module my_rules)
  scenarios/*.toml    scenarios
  data/*.toml, *.py   data patches and generators (a declared ability, a component that carries it)
  tests/              tests that call the functions with stand-ins, run by opense4-sdk test
```

```python
from opense4 import rules


@rules.on("empire_end_of_turn", step="repair", when="after")
def field_repairs(game, empire, step, when, fx):
    ...
```

A function gets `game` (the whole game, as typed objects), the hook's own arguments, and
`fx`, the effects. It changes the game only through `fx` and through its mod data. Name your
modules after your mod: two mods with a module of the same name conflict, and the later one's
is left out.

## Choosing the moment

The engine calls functions at **moments** (the start of a turn, each movement day, before
and after each step of an empire's end of turn, each colony's end of turn, the victory
check, the end of the turn) and on **events** (a colony founded, a vehicle built or lost, a
tech level reached, a treaty changed, a message, an event fired), each listed with its
arguments in [rules.md](../rules.md#hooks). Some choices:

| To | Hook |
|---|---|
| Change what a step of the end of turn did (income, repairs, supply, population) | `empire_end_of_turn` with that `step`, `when="after"` |
| Do something to every colony every turn | `colony_end_of_turn` |
| React to a new colony, ship, technology or treaty | `colony_founded`, `vehicle_built`, `tech_researched`, `treaty_changed` |
| Something about movement or a sector | `vehicle_entered_sector`, `movement_day` |
| Around battles | `before_battle`, `after_battle`, `vehicle_destroyed` |
| Shape a new game | `new_game` (options), `generate_galaxy` (the quadrant), `after_galaxy` (the empires are placed) |
| End the game | `check_victory`, or a `[[rules.victory]]` condition |

Events are delivered at the next safe point after they happen, never inside the engine's
own loops, so a function always sees a consistent game.

## Reading the game

`game` is the view of [view.md](../view.md) for the whole game ([rules.md](../rules.md#reading-the-game)):
`game.empires`, `game.colonies`, `game.vehicles`, lookups such as `game.colony(planet)`, the
rules view `game.rules`, the mod's options `game.option(name)`, the queries `game.query(...)`,
and `game.ability(thing, name, kind)`, an ability's value on a vehicle, colony, system,
component or hull, combined as the ability combines.

Reading costs budget: 5 bytecodes for each value the engine builds. In a loop over many
things, read the maps:

```python fragment
for raw in game.raw["vehicles"]:              # every vehicle, as maps
    if raw["owner"] != empire.id or not raw["damage"]:
        continue
    crews = game.ability(raw["id"], "Field Repair", "vehicle")
```

Random numbers come only from the game's own generator, `game.rng` (`below`, `range`,
`chance`, `choice`, `shuffle`): a game played again draws the same.

## Changing the game

Every change is an effect ([rules.md](../rules.md#effects)): `fx.add_resources`,
`fx.change_population`, `fx.change_happiness`, `fx.damage`, `fx.repair`, `fx.change_supply`,
`fx.create_vehicle`, `fx.add_facility`, `fx.set_treaty`, `fx.log`, `fx.fire_event`,
`fx.set_planet`, `fx.victory` and the rest. Each checks what it is given; a refusal raises in
your function (a `ValueError` saying why) and changes nothing. Numbers are whole numbers.

**Tell the players.** `fx.log(empire, text, title=...)` writes in that empire's Log, which
is how players learn what your rule did. Both the new-ability and scenario examples do.

## Mod data

A mod keeps data of its own on the game, an empire, a colony or a vehicle: `game.mod_data`,
`empire.mod_data`, `colony.mod_data`, `vehicle.mod_data`, each a dict of plain values that
only that mod sees. Change it in place; what it holds when the function returns is kept
(unless the function failed), saved with the game, sent to the players' computers and
checksummed ([rules.md](../rules.md#mod-data)).

```python fragment
# scripts/menders.py: how many components an empire's crews mended in the game.
empire.mod_data["mended"] = empire.mod_data.get("mended", 0) + mended
```

Module globals do not last: keep in mod data whatever must be remembered from one call to
the next. With `players_see_mod_data = true` in `[rules]`, a player's own empire, colonies
and vehicles carry the mod's data in its view, so computer players can read it.

## Declared abilities

The data files' abilities are a closed list; a mod adds a name with a data patch, puts it on
components, facilities, hulls or system types like any other, and gives it an effect in a
hook:

```toml
# data/menders.toml
[[abilities.declare]]
name = "Field Repair"
combine = "sum"          # several entries on one ship add up; or "max", "min"
```

The [new-ability](../../../mods/examples/new-ability/) example does exactly that, and the
tutorial [A new ability with an effect](tutorials/new-ability.md) walks through it.
Computer players read a declared ability as any other: in the rules view, on components
(`component.ability_value("Field Repair")`), and with the `abilities` query.

## Data generators

A generator is a Python file in `data/` whose `generate()` returns a patch, a dict shaped
like a `.toml` patch file, applied in its file's turn among the mod's patches
([rules.md](../rules.md#data-generators)). It runs when the data loads, in the sandbox, with
nothing from the game, and must give the same patch every time. Use it when records follow
a rule: the [weapon-line](../../../mods/examples/weapon-line/) example builds a tech area
and six levels of a weapon from a few formulas
([A weapon line from a generator](tutorials/weapon-line.md)). `opense4-sdk dump` writes the
records it makes.

## Scenarios

A scenario is a setup and objectives in `scenarios/<name>.toml`: the game's options, its
empires, the mod's options, and objectives written in the lessons' condition language, each
with an optional action and victory ([rules.md](../rules.md#scenarios)). The
[scenario](../../../mods/examples/scenario/) example is a race to settle the frontier, with a
grant for each empire's second colony ([A scenario](tutorials/scenario.md)).
`opense4-sdk test` starts each scenario of a mod and plays it for a few turns. Players start
one from the Learn window (the title screen's Scenario button), whose **Scenarios** tab
lists the scenarios of the mods in use; a dedicated server's setup file names one with
`scenario = "<mod id>:<name>"`.

## Orders, events, projects, options and victory conditions

Each is declared in `mod.toml` and given a function in a script. An invented example of
each, in the shape [rules.md](../rules.md) specifies:

```toml
[[rules.options]]              # a game option, set in the setup screens, setup files and scenarios
name = "tithe"
label = "Tithe on research (percent)"
type = "int"
min = 0
max = 50
default = 10

[[rules.orders]]               # an order players and computer players can give
name = "survey"
label = "Survey the system"
applies_to = "vehicle"

[[rules.events]]               # a random event, rolled each turn
name = "solar_flare"
label = "Solar flare"
chance = 5
target = "colony"

[[rules.victory]]              # a way to win
name = "archive"
label = "The great archive was completed."
```

```python
from opense4 import rules


@rules.order_check("survey")
def can_survey(game, order):
    if order.target.supply < 50:
        return "Not enough supply to survey."     # the refusal; None: it may be given


@rules.order("survey")
def survey(game, order, fx):
    fx.change_supply(order.target, -50)
    order.target.mod_data["surveyed"] = game.turn


@rules.event("solar_flare")
def solar_flare(game, event, fx):
    fx.change_happiness(event.target_object, -2)
    fx.log(event.target_object.owner, "A solar flare frightened " + event.target_object.planet.name + ".")


@rules.victory("archive")
def archive(game):
    for e in game.empires:
        if e.alive and e.mod_data.get("archive", 0) >= 100:
            return e                   # the winner; None: the game goes on
    return None
```

A computer player gives a mod's order as any command:
`cmd.mod_command(mod="me.surveys", name="survey", vehicle=ship)`.

## The same on every computer

Rules scripts are part of the game's rules: on a network or e-mail game they run on the
host, and every turn must resolve the same way everywhere. The runtime makes that the
default, as long as you:

- draw random numbers only from `game.rng` (Python's `random` is not there);
- give the game whole numbers (floats are refused);
- keep nothing in module globals between calls (use mod data);
- do not depend on `id()` or the order of sets of objects.

## Testing a rules mod

- `opense4-sdk check` imports the scripts, and checks that every declared order, event,
  project, victory condition and objective action has its function and the other way
  round.
- A rules function is a plain function: test it with small stand-ins for `game` and `fx`
  that have only what it uses ([Testing and measuring](testing-and-measuring.md#writing-tests)).
- `opense4-sdk test` then plays the mod's rules in a short game, and each of its scenarios,
  and fails on any failure of a rules function, with its traceback.

## Budgets and failures

A call of a function may run 50 million bytecodes, all of a mod's calls in one game turn 2
billion ([rules.md](../rules.md#budgets-and-failures)). A function that raises, runs out or
leaves bad mod data fails: what its effects did stands, its mod data changes do not, and
that hook of that mod is skipped for the rest of the turn; after three failures in a turn
the whole mod is off until the next. The host's log says which mod, function and why, with
the traceback.
