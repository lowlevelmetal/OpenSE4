# Budgets and performance

A computer player that runs inside the game runs on OpenSE4's own Python, in a sandbox
with limits. The limits keep a game moving whatever a mod does, and they are counted the
same way on every computer, so a game plays the same everywhere. This chapter says what
the limits are, what things cost, and how to write a player that stays well inside them.
The runtime itself is described in [The script runtime](../runtime.md); the measurements
of the package are in [python-api.md](../python-api.md#costs).

## The limits

| Limit | Default | Set by |
|---|---|---|
| A planning call (`politics`, `orders`, `economy`) | 200 million bytecodes | the game's Computer Player Limits (Game Setup); `ai_planning_budget` in a server setup file |
| Any other call (`colony_type`, `enter_sector`, `decloak`, `battle_round`, `end_session`) | 5 million bytecodes | `ai_call_budget` |
| A player's memory | 1 MiB, as JSON | `ai_memory_limit` |
| The heap, shared by the call's script players | 64 MiB | the session |
| Nested Python calls | 200 | the runtime |

**A budget counts bytecodes, not time.** One unit is one bytecode the script runs, plus
work done in C on its behalf in comparable units: each step of `sum()`, `sorted()`,
`list()` or a comprehension over a long list, each comparison while sorting, each
backtracking step of a regular expression. The count depends only on the script and its
inputs, so a player runs out at the same point on every computer. Simple code runs at
about 2.3 ns a bytecode in an optimized build on a desktop computer: 200 million bytecodes
are about half a second of work, far more than a player needs.

**Running out** raises `BudgetExceeded`, which no `try` can catch: the call ends, the engine
logs it, and the classic AI answers that call. Running out of memory, recursing too deep or
keeping a memory over its limit fail the call the same way. After three failures in one
turn, the classic AI plays the empire for the rest of the turn.

**External bots** have no budget: they run on your own CPython with any library, and the
game gives each request a time limit instead (60 seconds unless the host sets
`--bot-timeout`; [Testing and measuring](testing-and-measuring.md#external-bots)).

## What things cost

**The engine's services** count against the budget as if the script had run that many
bytecodes ([ai-protocol.md](../ai-protocol.md#6-services-player--engine-during-a-request)):

| Service | Cost in bytecodes |
|---|---|
| A query (`self.query(...)`) | 20,000, plus 20 per value of its answer |
| The rules view (`view.rules`, once per session) | 5 per value |
| The classic AI's commands (`ai.builtin.orders(view)` and the others) | 2,000,000, plus 20 per value of the commands |
| The classic answer to a question (`ai.builtin.answer()`) | 20,000 |
| Applying a command now (`self.apply(...)`) | 50,000, plus 5 per value of the new view and 20 per value of the answer |

So a planning call may ask some thousands of queries, but each `ai.builtin` call is
expensive: ask once per call and filter the list it gives.

**Reading the view.** The `opense4` package wraps the view lazily: each list is wrapped
the first time it is read, each record when it is reached, each field when it is asked. On
a view of 8,160 vehicles (much larger than a real game's), reading `view.my.vehicles` cost
335,000 bytecodes, and five fields of every vehicle through the objects 1.4 million, but
600,000 through the maps (`record.raw`) ([python-api.md](../python-api.md#costs) has the
whole table). A real game's view is smaller by an order of magnitude or two.

**The examples.** Measured by the arena in a debug build (which is several times slower
than a release), each player's time per game turn of its own, all its calls together, on
the classic data set in games of 80 turns:

| Player | Time a turn (debug build) | Of which |
|---|---|---|
| A player that overrides nothing | about 40 ms | three `ai.builtin` calls |
| [classic-ai-research](../../../mods/examples/classic-ai-research/) (the Scholar) | about 110 ms | the same, and its planner over every component, facility and hull |
| [small-ai](../../../mods/examples/small-ai/) (Pioneer) | about 115 ms | its own decisions, a few dozen queries, and `apply` for new designs |

## Writing a fast player

- **Work out once per call what you need many times.** Build a dict of what you look up
  (`{planet id: colony}`) instead of searching a list in a loop. The view's own lookups
  (`view.vehicle(id)`) build their index once per view.
- **Keep what lasts all session on `self`.** The rules view never changes during a game,
  so a table made from it once per session can be reused by every call of that session:
  the Scholar builds its list of everything that needs technology once (`self._items`).
  Never put it in `self.memory`, which is saved with the game.
- **One pass, not one per question.** The Scholar's planner first finds, in one pass over
  every component, facility and hull, which tech area each one waits for; scoring each
  area then costs a dictionary lookup. Scoring each area by walking every item would cost
  thirty times as much.
- **In a loop over thousands of things, read the maps.** `for v in view.raw["vehicles"]`
  and `v["owner"]` cost less than half as much as the typed records, which call a
  property for each field.
- **Ask the engine for a few candidates.** Sort cheaply in Python first, then ask
  `colonize_problem` or `path` for the best few (Pioneer asks for at most four planets a
  ship).
- **Keep the questions cheap.** `colony_type`, `enter_sector` and `battle_round` come many
  times a turn with 5 million bytecodes each: answer from what the planning calls kept on
  `self`. Leaving `battle_round` out lets the combat strategies fight, at no cost.
- **Keep memory small.** It is saved, sent to every player's computer and checksummed each
  turn. Ids and counts, not copies of the view.
- **Measure.** `opense4-sdk arena` reports each player's time a turn (`ms/turn`), and
  `opense4-sdk test` the time of each test and game ([Testing and measuring](testing-and-measuring.md)).
  `tests/sdk/python/sdk_bench.py` in OpenSE4's source measures the package itself.

## Deterministic by construction

In-game players are deterministic: the same request gives the same answer on every
computer, because the runtime has no clock, files, threads or randomness of its own, dict
and set order do not depend on memory addresses, and `self.random` is seeded from the
request ([runtime.md](../runtime.md#the-same-on-every-computer)). Every answer is also
recorded in the turn's journal, so a turn replayed never asks a player twice. External
bots need not be deterministic: the game records their answers.
