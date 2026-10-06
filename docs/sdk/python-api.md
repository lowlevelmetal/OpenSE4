# The `opense4` Python package

The package computer players are written with (docs/MODDING_SDK.md, section 6). The
same code runs in two places:

- **in the game**, on the MicroPython runtime built into OpenSE4, inside its sandbox
  ([runtime.md](runtime.md) lists what that Python has and lacks);
- **as an external bot**, on CPython 3.10 or newer, with any library.

It lives in `python/opense4/`. Its parts:

| Module | What it is |
|---|---|
| `opense4.ai` | `Player`, the class a computer player extends, and what its callbacks receive |
| `opense4.builtin` (`ai.builtin`) | The classic computer player as a library |
| `opense4.view` | The view of the game as typed objects ([view.md](view.md) is its schema) |
| `opense4.galaxy` | The warp map of a view: jumps, routes, distances |
| `opense4.rules` | The rules view as typed objects; the hooks of the rules tier (a later step) |
| `opense4.cmd`, `opense4.order`, `opense4.tactical` | Commands, orders and tactical orders ([commands.md](commands.md)) |
| `opense4.rng` | Random numbers that are the same on every runtime |
| `opense4.enums` | Every enumeration's names |
| `opense4.services` | What a player may ask the engine during a call |
| `opense4.external` | External bots |
| `opense4.testing` | Playing a player without the game, for tests |

The engine's side of the conversation is [ai-protocol.md](ai-protocol.md); a player
never needs to read or write its messages itself.

## A first computer player

`opense4-sdk new ai my-ai` makes a mod with one computer player. Its `mod.toml`
declares it:

```toml
[[ai.players]]
name = "Prospector"
module = "player"            # ai/player.py
class = "Prospector"
description = "Keeps the classic economy and sends its idle scouts exploring."
```

and `ai/player.py` holds it:

```python
from opense4 import ai, cmd, order


class Prospector(ai.Player):
    def orders(self, view, orders):
        self.memory["turns"] = self.memory.get("turns", 0) + 1
        sent = []
        for ship in view.my.idle_vehicles:
            figures = ship.design.figures if ship.design is not None else None
            if ship.type != "ship" or figures is None or figures.weapons or figures.colonize:
                continue
            orders.add(cmd.give(ship, [order.explore()]))
            self.note(ship, "exploring")
            sent.append(ship.id)
        for command in ai.builtin.orders(view):          # the classic ministers do the rest
            if command.get("vehicle") not in sent:
                orders.add(command)
```

Every decision a player does not override stays the classic AI's: this one keeps the
classic diplomacy, economy, colony types and battles.

The engine adds the files of a mod's `ai/` folder at the root of the interpreter, so
`ai/player.py` is the module `player`, and the mod's modules import one another by those
names (`import tactics` for `ai/tactics.py`).

## Players (`opense4.ai`)

### The callbacks

| Method | When | It gets | It gives | Left out |
|---|---|---|---|---|
| `politics(view, orders)` | Start of the turn, before messages are delivered | the view; `orders` to add commands to | commands | the classic Politics minister |
| `orders(view, orders)` | Start of the turn, after politics | the same | commands | the classic ministers' orders |
| `economy(view, orders)` | End of the turn, before income, research and construction | the same | commands | the classic economy |
| `colony_type(view, question)` | A colony is founded | `question.planet`, `question.vehicle`, `question.choices` | one of the choices, or None | None: the classic choice |
| `enter_sector(view, question)` | A move would enter a sector with enemies | `question.vehicles`, `question.sector`, `question.enemies` | True, False, or None (enter) | None |
| `decloak(view, question)` | A cloaked vehicle or colony must decloak to act | `question.object_id`, `question.vehicle`, `question.reason` | True, False, or None (the classic minister) | None |
| `battle_round(battle, orders)` | Each round of a space battle | the battle (`BattleState`); `orders` for tactical orders | tactical orders | none: the strategies decide |
| `end_session()` | Before the session ends | | | nothing |

- **Planning calls** (`politics`, `orders`, `economy`) give commands by adding them to
  `orders` (`orders.add(c)`, `orders.extend(cs)`); a method may also return a list of
  commands, which comes after them. The commands are applied in order, each checked as a
  human player's.
- **Questions** come many times a turn and get no fresh view: their `view` is the latest
  one of the session (from its last planning call), or None. The question's own fields are
  read as attributes; ids are resolved in that view where it can (`question.planet` is a
  SpaceObject or None, `question.planet_id` the id).
- **Battle rounds** add tactical orders (`opense4.tactical`) for the player's own pieces;
  `orders.fire(piece, target)` and `orders.move(piece, x, y)` are shortcuts, and the side
  giving each order is filled in. `BattleState` has `round`, `rounds_max`, `pieces`,
  `my_pieces`, `enemy_pieces`, `piece(i)`, `in_range(piece, target)` and
  `weakest_enemy_in_range(piece)`; each `Piece` reads its fields as attributes and knows its
  `index`, the position tactical orders name.

### What a player has

Set before every call:

| Attribute | What |
|---|---|
| `self.empire_id` | The empire played |
| `self.turn`, `self.call` | The game turn and the call being answered |
| `self.seed`, `self.random` | The request's seed and an `opense4.rng.Random` seeded from it: a replayed request draws the same numbers |
| `self.view` | The session's latest view |
| `self.refused` | The commands of the previous planning call the game refused: `[{index, command, reason}]` |
| `self.memory` | What the player keeps between sessions |

**Sessions and memory.** The engine starts a session for each engine call that needs the
player (a simultaneous turn, a turn-based player's turn) and makes a new `Player` for it.
Attributes set on `self` last until the session ends; only `self.memory` lasts longer: it is
saved with the game, sent to the other players' computers and checksummed, up to a size
limit (1 MiB as JSON by default). Keep plain values in it: None, True and False, whole
numbers, text, lists and dicts with text keys (not the view's objects: their ids). The
first session starts with an empty dict, or with what the player's own `__init__` put in
`self.memory`.

**Notes and the log.** `self.note(thing, text)` attaches a note for the client's AI view
to a vehicle, fleet, colony, stellar object, system, empire, design or message (an object
of the view, or an id with `kind="vehicle"` and so on). A later note on the same thing in
the same turn replaces it. `self.log(text)` writes a line to the game's log file.

**Errors.** An exception in a callback ends that call: the engine logs it with its
traceback (file and line of each call) and the classic AI decides that one thing instead.
The session goes on. After three failures in one turn, the classic AI plays the empire for
the rest of the turn. Running out of budget or memory counts as a failure too.

### Asking the engine

During a call a player may ask the engine ([ai-protocol.md](ai-protocol.md), section 6);
what it asks counts against its budget.

- `self.query(name, **args)` runs a query of [view.md](view.md) ("Queries") and gives its
  result as a record: `self.query("path", vehicle=ship, destination=home).length`.
  Arguments may be the view's objects. An unknown query or argument is an error before the
  engine is asked; a refused one raises the engine's error (`ValueError: vehicle: no such
  vehicle in view`).
- `self.rules` is the rules view (`opense4.rules.Rules`), asked once per session; so is
  `view.rules`.
- `self.apply(command)` applies a command now, in a planning call, and returns the
  engine's answer (`result.ok`, `result.reason`). A command applied this way is not given
  again with the call's answer.

### The classic AI as a library (`ai.builtin`)

```python
orders.extend(ai.builtin.economy(view))                        # the classic economy
orders.extend(ai.builtin.orders(view, skip=["attack"]))        # every minister but Attack
research = ai.builtin.economy(view, ministers=["research"])    # one minister's commands
answer = ai.builtin.answer()                                   # the classic answer to this question
```

`politics`, `orders` and `economy` give the commands the classic ministers would give for
the empire now, as lists of command maps, applied only if the player adds them. `ministers`
names the ministers to ask (None: all) and `skip` those to leave out, by the names of the
`minister` enumeration (`opense4.enums.MINISTER`: `"research"`, `"attack"`,
`"exploration"`...). Filter them as lists: `cmd.without(cs, "set_research")`,
`[c for c in cs if c.get("vehicle") != flagship.id]`. `answer()` gives the classic answer
to the question being asked (`colony_type`, `enter_sector` or `decloak`).

## The view (`opense4.view`)

`view` is a `View`: the game as the empire knows it ([view.md](view.md)). Wrapping costs
nothing: each list is wrapped the first time it is read, each field read when asked.

### Conventions

- Every documented field is a property of the same name: `vehicle.name`,
  `colony.population`, `view.game.options.tech_cost`.
- **A field that holds an id gives the object** it names, and the id itself under the same
  name with `_id`: `vehicle.design` is a `Design`, `vehicle.design_id` its id;
  `vehicle.owner` an `Empire`, `vehicle.owner_id` its id. A list of ids gives a list of
  objects, and the ids under the singular name with `_ids`: `fleet.members` and
  `fleet.member_ids`, `system.objects` and `system.object_ids`. Two fields take other names:
  a stellar object's `colony` (its colony's owner) is `colony_owner`, and the view's own
  `empire` is `view.me` (an `Empire`) and `view.empire_id`.
- **Refs and indices stay numbers.** A `... ref` may name what the view does not list; an
  `... index` is a position in the rules view (`rules.hull(design.hull)`).
- Nested types are records too (`vehicle.location.x`, `colony.queue.items[0].done_in`);
  enumerations are their names (`"ship"`, `"war"`).
- `record.raw` is the map as the engine sent it and `record["field"]` one field of it.
- Objects with an id (empires, systems, stellar objects, colonies, vehicles, fleets,
  designs, messages) are equal when their ids are, and can be dict keys or set members. A
  colony's `id` is its planet's: commands name colonies by their planet.

### Lists and lookups

| Lists | Lookups (an id or an object; None when not listed) |
|---|---|
| `view.empires`, `view.systems`, `view.objects`, `view.colonies`, `view.vehicles`, `view.fleets`, `view.designs`, `view.messages`, `view.log`, `view.battles` | `view.empire(id)`, `view.system(id)`, `view.object(id)` (or `view.planet(id)`), `view.colony(planet)`, `view.vehicle(id)`, `view.fleet(id)`, `view.design(id)`, `view.message(id)` |

`view.game` is the game (turn, date, options; `game.simultaneous`), `view.my` our own
affairs, `view.me` our `Empire`.

### Ours, theirs and where things are

| | |
|---|---|
| `view.my.vehicles`, `.fleets`, `.colonies`, `.designs` | Ours |
| `view.my.ships`, `.bases`, `.units`, `.vehicles_of_type("mine", ...)` | Ours by vehicle type |
| `view.my.idle_vehicles`, `.idle_fleets` | Ours with nothing to do (outside fleets, in service, able to move, no orders) |
| `view.my.colonies_of_type("Mining", ...)`, `.designs_of_type("Scout", ...)`, `.design_named(name)` | By colony type, design type, name |
| `view.foreign_vehicles`, `view.foreign_colonies` | Other empires' that we see or know |
| `view.enemies`, `view.enemy_vehicles`, `view.enemy_colonies`, `view.is_enemy(e)`, `view.treaty_with(e)` | Empires we are at war with, and theirs |
| `view.explored_systems`, `view.unexplored_systems`, `view.planets`, `view.warp_points` | Places |
| `view.objects_in(system)`, `view.vehicles_in(system)`, `view.colonies_in(system)`, `view.vehicles_at(place)` | What is where |
| `system.planets`, `.warp_points`, `.colonies`, `.vehicles`, `.centre`; `planet.colony`, `.location`; `vehicle.system`, `.mine`, `.idle`, `.is_unit`; `fleet.vehicles`, `.system`; `colony.system`, `.location` | The same from the objects |
| `location.distance_to(place)`; `resources.total`, `.covers(cost)` | Small sums |

`opense4.view.record(kind, data, view)` wraps a map of any documented type ("path_result",
"queue"...) by hand.

### The warp map (`view.galaxy`)

Worked out in Python from the view, with no call to the engine:

| | |
|---|---|
| `g.neighbours(system)` | The systems one jump away |
| `g.links(system)` | Its usable warp links: ((x, y) of the warp point, far system, (x, y) of the arrival) |
| `g.jumps(a, b)` | The fewest jumps from a to b, or None |
| `g.distances(a)` | Jumps from a to every system it reaches: {system id: jumps} |
| `g.path(a, b)` | The systems of a route with the fewest jumps, or None |
| `g.nearest(origin, candidates)` | The candidate (a system, or anything in one) the fewest jumps away |
| `g.route_length(a, b)` | Movement points between two places, or None |

It uses what the empire's own ships route over: the warp points of the systems we see
whose far end we know and may use, the first ten of each system, and the empire's Ship
Movement options (its systems to avoid, its tagged minefields). `route_length` counts as
the engine's routes do, one movement point per sector (diagonals too) and one per jump;
it does not know the detours around a system's destructive centre, which the `path` query
does. The tests check both against the engine's routes. `Galaxy(view, known_only=False,
avoid=False)` makes the map of every link a view shows, without the options.

## The rules view (`opense4.rules`)

`view.rules` (or `self.rules`) is a `Rules`: `components`, `facilities`, `hulls`,
`mounts`, `techs`, `racial_traits`, `cultures`, `happiness_models`, `races`,
`planet_sizes`, `system_types`, `sector_types`, `abilities`, `formations`, `strategies`,
`intel_projects`. Records are found by index (`rules.component(i)`, `rules.hull(i)`,
`rules.tech(i)`...) or by name (`rules.component_named(name)`, `hull_named`, `tech_named`,
`facility_named`); `rules.aggregation(ability)` says how an ability's entries combine.
Components, facilities and hulls answer `has_ability(name)`, `ability_value(name)` and
`ability_values(name)`; a component knows `is_weapon`.

## Commands (`opense4.cmd`, `opense4.order`, `opense4.tactical`)

There is a constructor for every command kind, every order kind, every tactical order kind
and every type they carry, named as [commands.md](commands.md) names them:

```python
from opense4 import cmd, order

cmd.set_orders(fleet=fleet, orders=[order.move_to(target), order.attack(vehicle=enemy)])
cmd.queue_add(target=colony, item=cmd.queue_item(design=lancer, count=2))
cmd.set_research([4, 9], evenly=False)
cmd.send_message(cmd.message(to_empire=rival, type="propose_treaty", treaty="non_aggression"))
```

- **Fields are keyword arguments**, with the engine's defaults; each command is a plain
  map with every field (`{"kind": "set_orders", "vehicle": None, "fleet": 3, ...}`), ready
  for `orders.add`. Orders and tactical orders take the fields their kind uses, in the
  docs' order (`order.colonize(planet)`, `order.drop_cargo(design, amount)`,
  `tactical.fire(piece, target)`).
- **Ids or objects**: a vehicle id or a `Vehicle`, a planet id, `SpaceObject` or `Colony`.
  An object of the wrong kind is refused at once (`TypeError: set_orders.vehicle: expected
  a vehicle id or record, got Fleet`). A location may be a map, a `(system, x, y)` tuple, a
  system (its centre), a stellar object (its sector), or a vehicle or fleet (where it is). A
  construction queue may be a colony, a planet or a vehicle. A research queue may list tech
  areas by index. A stellar manipulation may be named (`order.stellar_manipulation(
  "create_storm", location=...)`). Enumeration names are checked.
- **Helpers**: `cmd.give(vehicle_fleet_or_colony, orders)` (set_orders for any of the
  three), `cmd.build(colony_or_vehicle, design_or_facility, count)`, `cmd.centre(system)`,
  `cmd.only(commands, *kinds)`, `cmd.without(commands, *kinds)`.

The engine checks every command as it checks a human player's; a refused one comes back in
`self.refused` of the next planning call.

## Random numbers (`opense4.rng`)

`Random(seed)` is PCG32 in plain whole-number arithmetic: the same seed gives the same
numbers in the game and under CPython, on every computer. `self.random` is one, seeded
from the request. It has `randint(a, b)`, `randrange(...)`, `randbelow(n)`, `choice(seq)`,
`shuffle(list)`, `sample(population, k)`, `choices(population, weights, k)`,
`chance(percent)`, `getrandbits(k)`, `random()`, `uniform(a, b)` and `next32()`; its
numbers are its own, not those of CPython's `random`. `getstate()` and `setstate()` keep a
generator going in `self.memory`.

## Enumerations (`opense4.enums`)

Every enumeration of [view.md](view.md) and [commands.md](commands.md) as a tuple of its
names: `enums.MINISTER`, `enums.TREATY`, `enums.ORDER_KIND`, `enums.VEHICLE_TYPE`... and
`enums.ALL` by name.

## Rules hooks (a later step)

```python
from opense4 import rules

@rules.on("colony_end_of_turn")
def overcrowding(game, colony, fx):
    ...
```

`rules.on(hook)` registers a function for one of `rules.HOOKS` (docs/MODDING_SDK.md,
section 7.1), and `rules.Effects` is the interface of `fx`. The engine does not call hooks
yet: the rules tier comes with a later step of the SDK.

## External bots (`opense4.external`)

An external bot is a program that plays through a connection to the game, with the same
`Player` class:

```python
from opense4 import external
external.run(connection, Admiral)    # answers the game's requests until it ends
```

`external.Connection` is what a connection provides (`receive`, `send`, `service`); the
connections themselves come with a later step of the SDK. A bot has the host's turn timer
instead of a budget, and need not be deterministic: the game records its answers.

## Testing a player (`opense4.testing`)

```python
from opense4 import testing
from player import Prospector

h = testing.Harness(Prospector, testing.FakeServices(builtin={"orders": []}))
r = h.call("orders", view=view_map)       # a view map, as the engine sends it
assert "error" not in r and r["commands"]
h.call("end_session")
```

`FakeServices` answers the services from prepared values (`rules`, `queries`, `builtin`,
`answers`, `apply`) and records what was asked; `Harness` plays a player through requests
as the engine does, keeping its memory from one session to the next.

## The same code in both places

Write for both runtimes: no `match`, no `{**a, ...}` (use `dict(a, k=v)`), ASCII-only
`upper()` and `lower()`, a small `re` ([runtime.md](runtime.md) lists the rest). Type hints
are fine: the runtime's `typing` accepts them. Results the game keeps (commands, answers,
memory) hold whole numbers, never floats.

## Costs

Planning calls get a budget of 200 million bytecodes, questions 5 million (the host may
change them). What the package itself costs, measured on a view of 480 systems, 4160
stellar objects, 160 colonies and 8160 vehicles (the engine fixture's whole view copied
forty times; tests/sdk/python/sdk_bench.py), in bytecodes, which are the same on every
computer, and in time on a desktop computer (an optimized build; in brackets, the same
view copied ten times, with 2040 vehicles):

| Each step after the one before | Bytecodes | Game's runtime | CPython 3.14 |
|---|---|---|---|
| Wrapping the view | 104 | 0.6 ms | under 0.01 ms |
| 1000 lookups by id (the first builds the index; no other vehicle is wrapped) | 398,000 | 2.6 ms (1.9 ms) | 1.0 ms |
| `view.my.vehicles` (only ours are wrapped) | 335,000 | 13.5 ms (0.7 ms) | 0.8 ms |
| `view.my.idle_vehicles`, `.ships`, `.units` | 271,000 | 1.5 ms (0.4 ms) | 0.7 ms |
| `view.vehicles`, every vehicle wrapped | 247,000 | 10.7 ms (0.5 ms) | 0.5 ms |
| Five fields of every vehicle, through the objects | 1,432,000 | 59 ms (5.5 ms) | 3.0 ms |
| The same five fields, through the maps (`record.raw`) | 588,000 | 2.9 ms (0.5 ms) | 1.2 ms |
| Jumps from home to every system | 257,000 | 1.5 ms (0.3 ms) | 0.8 ms |
| Importing the package's main modules (compiled once per process) | 27,000 | 2.2 ms | |

Each object the package makes is an allocation, and in the game's runtime making many
objects takes more time than its bytecodes suggest, the more so the more the heap holds:
the view four times larger costs ten to twenty times as much above. Lists and lookups wrap only
what they give back, and each thing once. In a loop over thousands of vehicles that only
reads a few fields, read the maps: `for v in view.raw["vehicles"]: ...`.

## For SDK developers

- `tools/gen_sdk_python.py` writes `enums.py`, `_records.py`, `cmd.py`, `order.py` and
  `tactical.py` from [view.md](view.md) and [commands.md](commands.md); run it after
  changing them. Defaults come from the tables' words ("-1 by default", "(the default)").
- `tests/sdk/python/test_sdk_*.py` are the package's tests, plain functions that run under
  the game's runtime, CPython and pytest. `tests/sdk/test_sdk_python.cpp` runs them in both
  runtimes with fixtures it makes from the engine (views, the rules view, the engine's
  routes, every command and type with its defaults, the docs' schema), decodes what they
  build with the engine's codec, compares what both runtimes publish, checks that the
  generated modules are up to date, plays computer players with the engine's services and
  measures the costs above. `OPENSE4_SDK_FIXTURES_OUT=file` keeps the fixtures, for
  `python3 tests/sdk/python/run_sdk_tests.py --fixtures file` or pytest with
  `OPENSE4_SDK_FIXTURES=file`.
