# A first computer player

In this tutorial you write a computer player that takes one decision away from the classic
AI, where its colony ships go, test it, and measure it in the arena. Then you read a
complete player, the example [mods/examples/small-ai](../../../../mods/examples/small-ai/)
(Pioneer), that makes every decision of its own. The chapter behind it is
[Computer players](../computer-players.md).

## 1. Make the mod

```sh
opense4-sdk new ai settler --id=me.settler --name=Settler
```

The template holds a player, `Prospector` in `ai/player.py`, that sends idle scouts
exploring. Replace it: delete `ai/player.py`, and declare your player in `mod.toml`:

```toml
[[ai.players]]
name = "Settler"
module = "settler"          # ai/settler.py
class = "Settler"
description = "Sends each colony ship to the best planet it can reach; the classic AI does the rest."
```

## 2. The player

`ai/settler.py`:

```python
"""Settler: the classic AI, but it chooses where its colony ships go."""

from opense4 import ai, cmd, order


def size_score(rules, planet, breathes):
    """The population a colony on the planet could hold (domed when we cannot breathe there)."""
    for size in rules.planet_sizes:
        if size.name == planet.size and size.physical_type == "Planet":
            return size.max_population if breathes else size.max_population_domed
    return 0


class Settler(ai.Player):
    def orders(self, view, orders):
        claimed = self.memory.setdefault("claimed", {})      # {planet id (text): ship id}
        # Forget the claims of ships that are gone and planets that have a colony now.
        for key in list(claimed):
            planet = view.planet(int(key))
            if view.vehicle(claimed[key]) is None or (planet is not None and planet.colony_owner_id is not None):
                del claimed[key]

        home = view.system(view.my.home_system)
        jumps = view.galaxy.distances(home) if home is not None else {}
        sent = []
        for ship in view.my.idle_vehicles:
            figures = ship.design.figures if ship.design is not None else None
            if figures is None or not figures.colonize:
                continue                                      # not a colony ship
            best = None
            for planet in view.planets:
                if (planet.kind != "planet" or planet.surface not in figures.colonize
                        or planet.colony_owner_id is not None or str(planet.id) in claimed
                        or planet.system_id not in jumps):
                    continue
                breathes = planet.atmosphere == view.me.race.atmosphere
                score = size_score(view.rules, planet, breathes) // (1 + jumps[planet.system_id])
                if best is None or score > best[0]:
                    best = (score, planet)
            if best is None:
                continue
            planet = best[1]
            if self.query("colonize_problem", vehicle=ship, planet=planet).problem:
                continue                                      # the engine knows a reason it cannot
            claimed[str(planet.id)] = ship.id
            orders.add(cmd.give(ship, [order.colonize(planet)]))
            self.note(ship, "to settle " + planet.name)
            sent.append(ship.id)

        # Everything else is the classic AI's, less its orders for the ships we sent.
        for command in ai.builtin.orders(view):
            if command.get("vehicle") not in sent:
                orders.add(command)
```

Line by line:

- **`orders(view, orders)`** is called at the start of each turn. It overrides the classic
  AI's orders; `politics`, `economy`, the colony types and the battles stay the classic AI's,
  because Settler does not define them.
- **`view.my.idle_vehicles`** are our vehicles with nothing to do. A ship's design's
  `figures.colonize` lists the surfaces it can settle (empty for every other ship).
- **`view.planets`** are the planets of the systems we know. Each has a `size`, `surface`,
  `atmosphere`, and `colony_owner_id` (None when nobody has settled it that we know of).
- **`view.galaxy.distances(home)`** gives the warp jumps from home to every system we can
  reach, worked out in Python without asking the engine.
- **`view.rules.planet_sizes`** is the data set's PlanetSize table: how many people a
  planet of each size holds, under domes or not.
- **`self.query("colonize_problem", ...)`** asks the engine whether anything stops this ship
  settling that planet (its rules, not ours): an empty answer means it can.
- **`order.colonize(planet)`** alone is enough: the game puts a Move To in front of it.
- **`self.memory`** is saved with the game. `claimed` keeps two colony ships from heading
  for the same planet across turns; keys are text, values ids.
- **`self.note(ship, ...)`** shows in the client's AI notes view (`Ctrl+Shift+N`).
- **`ai.builtin.orders(view)`** is what the classic ministers would order now; we keep all
  of it but the orders for ships we sent.

## 3. Test it

`tests/test_settler.py`, run in the game's own Python on a new game of your data set:

```python no-run
from opense4 import testing

from settler import Settler


def test_it_gives_orders_and_keeps_its_claims():
    h = testing.Harness(Settler, testing.FakeServices(
        rules=testing.game_rules(),
        queries={"colonize_problem": {"problem": ""}},
        builtin={"orders": []}))
    r = h.call("orders", view=testing.game_view())
    assert "error" not in r, r.get("error")
    assert all(c["kind"] == "set_orders" for c in r["commands"])
    assert isinstance(r["memory"]["claimed"], dict)
```

```sh
opense4-sdk check settler
opense4-sdk test settler --turns=20
```

```text
Python tests (tests/):
  ok    test_settler.test_it_gives_orders_and_keeps_its_claims (257 ms)
Games (seed 1, 20 turns, simultaneous):
  ok    me.settler:Settler against builtin: 20 turns, 84 requests, no failures, 43.7 ms a turn (1.6 s)
```

`test` played twenty turns of Settler against the classic AI and found no failure. An
exception in `orders` would have failed it, with the traceback.

## 4. Watch it

```sh
opense4-sdk run settler -- --quick-start=Terran --turns=30
```

starts a quick game whose computer empires Settler plays, the computer playing yours too
for 30 turns. Switch on the AI notes view (`Ctrl+Shift+N`) and look at the colony ships'
notes on the map.

## 5. Measure it

How good is it? Put it in the arena with the classic AI, and with a player that overrides
nothing (a `Player` subclass with no methods: every decision `ai.builtin`'s), which plays
exactly the classic AI's games ([Computer players](../computer-players.md#the-classic-ai-as-a-library)):

```sh
opense4-sdk arena --mod=settler --mod=plain --ai=me.settler:Settler --ai=me.plain:Plain --ai=builtin \
    --games=24 --turns=80 --seed=1
```

Twenty-four games of 80 turns on the classic data set, the three in each galaxy, each
playing every seat in turn (a debug build):

| Player | Wins | Mean score | Colonies |
|---|---|---|---|
| Settler | 9 | 120,380 | 19.0 |
| Plain (overrides nothing) | 6 | 103,896 | 19.2 |
| builtin | 9 | 108,758 | 21.0 |

Seats and races weigh a lot in so few games: Plain, which plays as the classic AI does,
won 6 to builtin's 9. To see what Settler's own decision is worth, compare it with the
classic AI in its very seat: the same games with the classic AI in all three seats
(`--ai=A=builtin --ai=B=builtin --ai=builtin`, the same seeds) give, seat by seat, what the
classic AI did where Settler played. Against that, Settler scored 4,800 more a game (95 %
interval −10,300 to +19,900) with 1.8 fewer colonies (−3.6 to 0.0): choosing targets by
size and distance alone is about as good as the classic Colonization minister, no better.
To do better, weigh what a planet yields and how safe it is; Pioneer's
[expansion.py](../../../../mods/examples/small-ai/ai/expansion.py) is a start.

## 6. Read a complete player

[mods/examples/small-ai](../../../../mods/examples/small-ai/) is Pioneer, a player that makes
every decision of its own but the battles. Read it in this order:

1. [ai/pioneer.py](../../../../mods/examples/small-ai/ai/pioneer.py): the three planning
   calls, and the three things it remembers.
2. [ai/parts.py](../../../../mods/examples/small-ai/ai/parts.py): choosing components,
   weapons, hulls and facilities by what they do, so it plays on any data set.
3. [ai/designs.py](../../../../mods/examples/small-ai/ai/designs.py): composing designs,
   checking them with the `design_figures` query, creating them with `apply` so they can be
   built the same turn.
4. [ai/expansion.py](../../../../mods/examples/small-ai/ai/expansion.py): the same
   colonization idea as Settler's, with resources and conditions in the score.
5. [ai/economy.py](../../../../mods/examples/small-ai/ai/economy.py): research, facilities,
   ships and yards.
6. [ai/defence.py](../../../../mods/examples/small-ai/ai/defence.py): warships and fleets.

Its README says how it does in the arena and where it is weak: few yards, few surfaces
settled, battles lost. Copy it and make it better:

```sh
opense4-sdk new --from-example small-ai my-ai --id=me.my-ai
```

Next: [Budgets and performance](../performance.md) and
[Testing and measuring](../testing-and-measuring.md).
