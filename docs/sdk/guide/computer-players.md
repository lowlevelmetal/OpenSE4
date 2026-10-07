# Computer players

A computer player is a Python class that makes an empire's decisions. It can take over
every decision the classic AI makes, or only some and leave the rest to the classic AI. The
same code runs inside the game, on OpenSE4's own Python, and as an external bot on
ordinary CPython.

This chapter goes from a minimal player to a full one. The reference pages are
[The `opense4` Python package](../python-api.md) (the package),
[The script view](../view.md) (every field a player reads),
[Script commands](../commands.md) (every command it gives) and
[The computer-player protocol](../ai-protocol.md) (what happens underneath). The
[API reference](../reference/README.md) lists every class and function. Two example mods
go with this chapter: [classic-ai-research](../../../mods/examples/classic-ai-research/)
(the classic AI with research of its own) and [small-ai](../../../mods/examples/small-ai/)
(Pioneer, a complete small player).

## How a player plays

A mod declares its players in `mod.toml`:

```toml
[[ai.players]]
name = "Pioneer"          # shown in the setup screens; the game names it example.small-ai:Pioneer
module = "pioneer"        # ai/pioneer.py
class = "Pioneer"         # an opense4.ai.Player subclass in it
description = "Explores, colonizes, builds facilities and defends what it has."
```

The files of the mod's `ai/` folder are at the root of the player's Python, so
`ai/pioneer.py` is the module `pioneer`, and the mod's modules import each other by those
names (`import parts` for `ai/parts.py`).

A game names who plays each computer empire: the classic AI (`builtin`), a mod's player
(`<mod id>:<name>`), or an external bot (`external:<slot>`). Game Setup, Empire Setup,
Quick Start and the network lobby offer the players of the enabled mods
([docs/SETUP.md](../../SETUP.md#computer-players)); `opense4 --quick-start --ai=example.small-ai:Pioneer`
gives every computer empire of a quick game to Pioneer; `opense4-sdk run mymod` does that
for the mod's first player.

The engine asks the player at these moments of a turn
([ai-protocol.md](../ai-protocol.md), section 3):

| Method | When | It gives | Left out |
|---|---|---|---|
| `politics(view, orders)` | Start of the turn, before messages are delivered | commands (messages, answers) | the classic Politics minister |
| `orders(view, orders)` | Start of the turn, after politics | commands (orders, fleets...) | the classic ministers' orders |
| `economy(view, orders)` | End of the turn, before income, research and construction | commands (designs, research, queues...) | the classic economy |
| `colony_type(view, question)` | A colony is founded | one of `question.choices`, or None | the classic choice |
| `enter_sector(view, question)` | A move would enter a sector with enemies | True, False, or None (enter) | enter |
| `decloak(view, question)` | A cloaked ship or colony must decloak to act | True, False, or None | the classic minister |
| `battle_round(battle, orders)` | Each round of a space battle | tactical orders | the combat strategies |
| `end_session()` | Before the session ends | nothing | |

**Every method you leave out is the classic AI's.** A player that overrides nothing plays as
the classic AI's ministers would, and each method you write takes one decision over.

**A session** is one engine call: a simultaneous turn, or a turn-based player's turn. The
engine makes a new player object for each session, so attributes on `self` last until the
session ends; only `self.memory` lasts longer (below).

## A minimal player

```python
from opense4 import ai, cmd, order


class Wanderer(ai.Player):
    """Sends every idle unarmed ship exploring; the classic AI does everything else."""

    def orders(self, view, orders):
        sent = []
        for ship in view.my.idle_vehicles:
            figures = ship.design.figures if ship.design is not None else None
            if ship.type == "ship" and figures is not None and not figures.weapons and not figures.colonize:
                orders.add(cmd.give(ship, [order.explore()]))
                sent.append(ship.id)
        # The classic ministers' orders for everything else, less any for our explorers.
        for command in ai.builtin.orders(view):
            if command.get("vehicle") not in sent:
                orders.add(command)
```

`opense4-sdk new ai my-ai` makes a mod with a player much like this one. Planning calls add
commands to `orders` (`orders.add(c)`, `orders.extend(cs)`); the engine applies them in
order, each checked as a human player's would be.

## Reading the view

`view` is what the empire knows, as a human player of it would see it
([view.md](../view.md)): its own affairs in full, other empires' as far as it has seen
them. With the game option "computer players see everything" it is the whole game.

```python fragment
view.game.turn, view.game.date            # the game
view.me                                   # our Empire
view.my.colonies, view.my.ships, view.my.designs, view.my.fleets
view.my.idle_vehicles                     # ours with nothing to do
view.my.research.levels                   # our level in every tech area, by index
view.my.stored, view.my.economy.net       # the treasury, and the coming turn's income
view.planets, view.unexplored_systems     # places
view.enemy_vehicles, view.enemy_colonies  # what we see of empires we are at war with
view.galaxy.jumps(home, system)           # warp jumps, worked out in Python
view.rules.component(i)                   # the data set (the rules view)
```

The conventions ([python-api.md](../python-api.md#the-view-opense4view)):

- **Every field is a property** of the documented name: `vehicle.name`, `colony.population`.
- **A field that holds an id gives the object**, and the id itself under `<name>_id`:
  `vehicle.design` is a Design, `vehicle.design_id` its number. Lists of ids likewise
  (`fleet.members`, `fleet.member_ids`).
- **Rules records are indices** into the rules view: `design.hull` is a hull index,
  `view.rules.hull(design.hull)` the hull.
- **A colony's `id` is its planet's**: commands name colonies by their planet.
- Objects with ids are equal when their ids are, so they can be dict keys and set members.
- What the empire does not know is None: a foreign ship's orders, an unexplored system's
  name.

**The rules view** is the data set as the game loaded it, mods applied: `view.rules`
(an `opense4.rules.Rules`). Find records by index (`rules.component(i)`) or name
(`rules.component_named("...")`), and ask them what they do: `component.has_ability(name)`,
`component.ability_value(name)`, `component.weapon`, `hull.tonnage`. A player that chooses
parts by what they do, as Pioneer does, plays on any data set:

```python
# ai/parts.py in the small-ai example: the best component for a job, by its ability.
def best_with(self, ability, vehicle_type="ship"):
    best = None
    for c in self.components(vehicle_type):          # what our technology allows
        if c.has_ability(ability):
            key = (c.ability_value(ability), -c.tonnage, c.roman_numeral)
            if best is None or key > best[0]:
                best = (key, c)
    return None if best is None else best[1]
```

## Giving commands

`opense4.cmd` has a constructor for every command, `opense4.order` for every order
([commands.md](../commands.md)). Fields are keyword arguments with the engine's defaults,
and ids may be given as the view's objects:

```python fragment
from opense4 import cmd, order

orders.add(cmd.give(ship, [order.colonize(planet)]))                  # set_orders for a ship, fleet or colony
orders.add(cmd.give(fleet, [order.move_to(home), order.sentry()]))
orders.add(cmd.queue_add(target=colony, item=cmd.queue_item(kind="vehicle", design=design_id, count=2)))
orders.add(cmd.build(colony, facility))                               # a facility of the rules view
orders.add(cmd.set_research([4, 9], evenly=False))                    # tech areas by index
orders.add(cmd.create_fleet(name="Home Guard", members=ships))
orders.add(cmd.answer_message(message=m, accept=True))
```

- An order aimed somewhere else gets a Move To in front of it, as in the game:
  `order.colonize(planet)` alone sends a colony ship there and settles.
- A command the game refuses comes back in `self.refused` at the player's next planning
  call, with the engine's reason (`[{index, command, reason}]`); the commands after it still
  apply. Log them while you develop a player:

```python fragment
for refused in self.refused:
    self.log("refused {}: {}".format(refused["command"]["kind"], refused["reason"]))
```

## Asking the engine

The view answers most questions; the engine answers the rest during a call
([view.md](../view.md#queries)). Each query costs budget (20,000 bytecodes and a little
per value of its answer), so ask for a few candidates, not for everything:

| Query | Answers |
|---|---|
| `self.query("path", vehicle=ship, destination=system)` | A route: its steps, length in movement points, jumps, turns |
| `self.query("movement", vehicle=ship)` | Movement and how long its supply lasts |
| `self.query("design_figures", hull=h, entries=[...])` | A proposed design's figures, whether the designer accepts it, and why not |
| `self.query("colonize_problem", vehicle=ship, planet=p)` | Why this ship cannot settle that planet now (empty: it can) |
| `self.query("queue_item_problem", planet=colony, item=item)` | Why a queue cannot take an item |
| `self.query("queue_forecast", planet=colony)` | When each item of a queue will be done |
| `self.query("research_forecast", area=i, level=n)` | What reaching a level costs and how long it takes |
| `self.query("abilities", vehicle=v)` | Every ability of a thing, combined as the rules combine them |

Pioneer asks before it acts, so that the engine's rules decide and the player stays short:

```python fragment
# ai/designs.py: the designer's own checks, before the design is made.
figures = self.player.query("design_figures", hull=hull.id, entries=entries)
if not figures.valid:
    self.player.log("design refused by the designer: " + "; ".join(figures.problems))

# ai/expansion.py: whatever stops this ship settling this planet.
if player.query("colonize_problem", vehicle=ship, planet=planet).problem:
    continue
```

**Applying a command at once.** `self.apply(command)` applies one command now, in a
planning call, and answers `result.ok`, `result.reason` and what changed. Pioneer creates
its designs this way so that it can put them in a queue in the same call: the answer's
`changed["designs"]` holds the new design and its id. The view you were given does not
change; the next call's view has the new design.

## Memory, notes and the log

**Memory.** `self.memory` is the only thing that outlasts a session: it is saved with the
game, sent to the players' computers and checksummed, up to a limit (1 MiB as JSON by
default). Keep plain values in it: None, True, False, whole numbers, text, lists and dicts
with text keys. Keep ids, never the view's objects. Pioneer keeps three things:

```python fragment
self.memory["designs"]   # {"scout": design id, "warship": ..., "colony:Ice": ...}: each role's design
self.memory["marks"]     # {"scout": 3, ...}: how many designs of each role it has made (for names)
self.memory["claimed"]   # {"<planet id>": ship id}: which colony ship is on its way where
```

Everything else it works out from the view each turn. Keep memory small, and keep in it
only what the view cannot tell you again.

**Notes.** `self.note(thing, text)` attaches a note to a vehicle, fleet, colony, planet,
system, empire, design or message for the client's **AI notes view** (Settings → Modding,
or `Ctrl+Shift+N` in a game). It shows each note on the maps and in the reports, so you can
watch a player think. A later note on the same thing replaces it; a note lasts for the turn
it was given and the next.

**The log.** `self.log(text)` writes a line to `opense4.log` in OpenSE4's user folder, and
`opense4-sdk test` prints it. Pioneer logs a line a turn while you develop it.

**Random numbers.** `self.random` is a generator seeded from the request: a replayed request
draws the same numbers ([python-api.md](../python-api.md#random-numbers-opense4rng)). Python's
`random` module is not available in the game.

## The classic AI as a library

`ai.builtin` gives what the classic ministers would do for the empire now, as commands you
may keep, filter or change:

```python fragment
orders.extend(ai.builtin.economy(view))                         # the whole classic economy
orders.extend(ai.builtin.orders(view, skip=["attack"]))         # every minister but Attack
research = ai.builtin.economy(view, ministers=["research"])     # one minister's commands
answer = ai.builtin.answer()                                    # the classic answer to a question
```

The minister names are those of `opense4.enums.MINISTER` (`"research"`, `"design"`,
`"colonization"`, `"attack"`, `"exploration"`...). `cmd.only(commands, kinds...)` and
`cmd.without(commands, kinds...)` filter by command kind. Asking costs 2 million bytecodes
a call, so ask once per call and filter the list.

The [classic-ai-research](../../../mods/examples/classic-ai-research/) example takes one
decision over and keeps the rest:

```python fragment
class Scholar(ai.Player):
    def economy(self, view, orders):
        classic = ai.builtin.economy(view)                        # designs, research, intel, construction
        orders.extend(cmd.without(classic, "set_research"))       # all of it but the research queue
        queue = self.plan(view)                                   # ai/planner.py
        if queue:
            orders.add(cmd.set_research(queue, evenly=False))
```

Its planner is a plain function of plain values (`ai/planner.py`), so it is tested without a
game. It ranks tech areas by what their next levels unlock for what they cost.

**The classic AI's own state.** Between its decisions the built-in AI keeps state of its
own: its state machine, anger toward each empire, counters, the figures it prepares at the
start of a turn and the lists its economy step reads. That bookkeeping keeps running for an
empire a player plays ([ai-protocol.md](../ai-protocol.md), section 9), so `ai.builtin`
plans from the state the built-in AI would have, and a player that overrides nothing plays
exactly the built-in AI's game: in 48 games of 100 turns on the classic data set against
the built-in AI, every game was the very game the built-in AI plays against itself. Your
player's results against `builtin` therefore measure your own decisions.

What the built-in AI writes into its empire directly, its claims, the systems it agreed to
leave and its movement options, comes as commands at the start of `ai.builtin.politics()`
(`set_system_flags`, `set_encounter_options`). A player that writes its own politics decides
them; to keep the classic ones:

```python fragment
    def politics(self, view, orders):
        orders.extend(cmd.only(ai.builtin.politics(view), "set_system_flags", "set_encounter_options"))
        ...                                                     # your own diplomacy
```

A player that makes every decision itself and never asks `ai.builtin` can save the
bookkeeping's time with `classic_state = false` in its `[[ai.players]]` entry; the classic
ministers that still answer for it (a question it leaves out, a failed request) then read
the classic state as the player found it.

## A full player

The [small-ai](../../../mods/examples/small-ai/) example, Pioneer, overrides the three
planning calls and leaves the questions and battles to the classic AI. It is split by
concern, which keeps each part short enough to read:

```text
ai/pioneer.py     the Player: politics(), orders(), economy(), and its memory
ai/parts.py       what we can build: tech checks, the best part for a job
ai/designs.py     scout, colony ship and warship designs
ai/expansion.py   scouts explore, colony ships settle
ai/defence.py     warships guard, gather into fleets, strike back
ai/economy.py     research, facilities, ships
```

Its turn:

1. **politics**: accept proposals of peace and trade, refuse subjugation and the rest.
2. **orders**: sort the idle ships by the role of their design (Pioneer remembers which
   design is which role), then scouts explore, colony ships each take the best planet they
   can reach that no other ship of ours is heading for, warships intercept enemies in our
   systems or gather at home and form fleets.
3. **economy**: bring each role's design up to date when research gave better parts (a new
   design, the old one marked obsolete), then research toward better parts, a facility on
   each colony with room, and ships at the yards: scouts while there is something to
   explore, colony ships for the good planets waiting, warships as the empire grows.

Read it in that order. Each function is a few dozen lines, and each comment says why, not
what. Its README gives its arena results, and what it does badly: good places to start
your own.

## Questions and battles

The questions come many times a turn, with no fresh view: `view` is the session's latest
one (from its last planning call) or None. Answer quickly from what you kept on `self`.

```python
def colony_type(self, view, question):
    # question.planet, question.vehicle, question.choices (the data set's colony types)
    return next((c for c in question.choices if "Research" in c), None)

def enter_sector(self, view, question):
    # question.vehicles, question.sector, question.enemies
    return len(question.vehicles) >= 3          # only in strength; None or True: enter

def battle_round(self, battle, orders):
    for piece in battle.my_pieces:
        target = battle.weakest_enemy_in_range(piece)
        if target is not None:
            orders.fire(piece, target)          # pieces given no order do nothing this phase
```

`battle_round` is asked each round of every space battle the empire fights, which can be
many times a turn: keep it cheap, or leave it out and let the combat strategies fight, as
both examples do. Colony types are names of the data set (`view.my.colony_types`); they
steer the classic ministers.

## When something fails

An exception in a callback ends that call: the engine logs it with its traceback and the
classic AI decides that one thing instead. The session goes on. Running out of budget or
memory, a wrong answer, or memory over its limit fail a call too; after three failures in
one turn the classic AI plays the empire for the rest of the turn. The host's main window
shows a notice for each failure, with its traceback under **Details**, and `opense4.log`
has them all ([python-api.md](../python-api.md#when-a-player-fails)). `opense4-sdk test`
fails on any of them ([Testing and measuring](testing-and-measuring.md)).

## One player, two Pythons

The same player runs in the game and as an external bot, so write for both:

- no `match` statement, no `{**a, ...}` (write `dict(a, k=v)`), ASCII-only `upper()` and
  `lower()`, a small `re` ([runtime.md](../runtime.md#the-language));
- `typing` and `dataclasses` work in both; type hints are welcome;
- what you give the game (commands, answers, memory) holds whole numbers, never floats;
- no files, clock, threads or network in the game: a player decides from the view and its
  memory alone.

Next: [Budgets and performance](performance.md), then
[Testing and measuring](testing-and-measuring.md), and the tutorial
[A first computer player](tutorials/computer-player.md).
