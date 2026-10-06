# Testing and measuring

Four tools tell you whether a mod works and how well a computer player plays:
`opense4-sdk test` runs a mod's tests and short games, `opense4-sdk run` starts the game
with it, the **arena** plays many headless games between players and reports who wins, and
**external bots** and the **training environment** run a player on ordinary CPython. The
reference is [Bots, the arena and the mod tools](../bots-and-arena.md).

## `opense4-sdk test`

```sh
opense4-sdk test mymod
```

1. **The mod's Python tests.** Each `test_` function of each `tests/test_*.py` module runs in
   the game's own Python, with the mod's `ai/`, `scripts/` and `tests/` folders at the root
   (so `import pioneer` finds `ai/pioneer.py`), the `opense4` package, and a budget of its
   own (`--budget=N` bytecodes, default a billion). A test fails by raising.
2. **A short game for each computer player** the mod declares, against the classic AI, on
   your installed game (`--data=DIR` for another): `--turns=N` turns (default 10) from
   `--seed=N` (default 1). Any failed request fails it, with the errors and tracebacks the
   game logged; so does any failure of the mod's rules functions.
3. **A mod with rules scripts and no player** plays one such game between two classic AIs,
   with its rules on.
4. **Each scenario** of the mod is started as the game starts it and played by the computer
   for as many turns; the output says which objectives were met and how it ended.

It prints a line per test and game and a summary, and exits with 0 when everything passed
and 1 otherwise, for a mod's own continuous integration. `--no-games` runs only the Python
tests (no data set needed), `--no-tests` only the games, `--turn-based` plays turn-based
games.

```text
Testing example.small-ai 1.0.0 (mods/examples/small-ai)
Python tests (tests/):
  ok    test_pioneer.test_designs_fit_their_hulls (281 ms)
  ...
Games (seed 1, 10 turns, simultaneous):
  ok    example.small-ai:Pioneer against builtin: 10 turns, 44 requests, no failures, 98.3 ms a turn (1.4 s)

5 passed, 0 failed, 0 skipped
```

### Writing tests

`opense4.testing` has what a test needs ([python-api.md](../python-api.md#testing-a-player-opense4testing)):

| | |
|---|---|
| `testing.game_view(empire=0)` | The view of a new game of the mod's data set (two computer empires, from `--seed`), as the engine sends it |
| `testing.game_rules()` | The rules view of the mod's data set, patches applied: wrap it in `opense4.rules.Rules` |
| `testing.Harness(Player, services)` | Plays a player through requests as the engine does, keeping its memory between sessions |
| `testing.FakeServices(rules=, queries=, builtin=, answers=, apply=)` | Prepared answers for the services, each a value or a function of the arguments; `calls` records what was asked |
| `testing.Skip` | Raise it to skip a test (as `game_view()` does where there is no game) |

**A data mod** tests the data it makes, through the rules view, as computer players see it:

```python
# mods/examples/new-hull/tests/test_wren.py
def test_the_hull_is_in_the_data_set():
    hull = rules.Rules(testing.game_rules()).hull_named("Wren Courier")
    assert hull is not None and hull.type == "ship" and hull.tonnage == 120
```

**A computer player** is played through requests with prepared services:

```python
# mods/examples/classic-ai-research/tests/test_scholar.py
def test_it_keeps_the_classic_economy_but_its_research():
    h = testing.Harness(Scholar, testing.FakeServices(rules=testing.game_rules(), builtin={"economy": classic_economy}))
    r = h.call("economy", view=testing.game_view())
    assert "error" not in r, r.get("error")
    assert "rename" in [c["kind"] for c in r["commands"]]       # the classic commands it keeps
```

Each response is a map: `commands`, `answer`, `memory`, `notes`, `log`, and `error` with a
traceback when the player raised. `FakeServices(apply=...)` can answer `self.apply` as the
engine would; the [small-ai](../../../mods/examples/small-ai/tests/test_pioneer.py) tests make
designs that way.

**Logic in plain functions** is the easiest to test: the Scholar's research planner takes
plain values and returns a list, so its tests build a small invented tech tree and need no
game at all ([test_planner.py](../../../mods/examples/classic-ai-research/tests/test_planner.py)).

**A rules function** is a plain function too: call it with small stand-ins for `game` and
`fx` that have what it uses, and check what it asked for
([test_menders.py](../../../mods/examples/new-ability/tests/test_menders.py)):

```python
class Effects:
    def __init__(self):
        self.repairs = []

    def repair(self, vehicle, components=None):
        self.repairs.append((vehicle, components))
        return components


fx = Effects()
menders.field_repairs(game, empire, "repair", "after", fx)
assert fx.repairs == [(1, 3)]
```

**Under CPython**, the same tests run with pytest when `PYTHONPATH` has the `opense4`
package (`opense4-sdk python` prints its folder) and the mod's `ai/` (and `scripts/`)
folders; `game_view()` and `game_rules()` skip there.

## Trying a mod in the game: `opense4-sdk run`

```sh
opense4-sdk run mymod -- --quick-start=Terran
```

starts the game with the mod and the mods it requires, with the mod's first computer player
(or `--player=NAME`) for the computer empires of new games. What follows `--` goes to the
game. To watch players play: switch on the AI notes view (Settings → Modding, or
`Ctrl+Shift+N`), choose the Quadrant page's "Omnipresent view of all systems" in Game Setup,
and end turns; `--turns=N` lets the computer play every empire, yours too, for N turns
first.

## The arena

```sh
opense4-sdk arena --mod=mods/examples/small-ai --ai=example.small-ai:Pioneer --ai=builtin \
    --games=8 --turns=80 --seed=1 --jobs=4
```

plays games between computer players without a window, several at a time (`--jobs`, at most
four by default), with the game's own turn processing, and reports who wins:

```text
player                   games  wins   win%     score    median colonies systems  techs  ships   battles  elim   fail/fb  ms/turn     elo
example.small-ai:Pioneer     8     3  37.5%     89725     79096      9.1     3.5   31.5   10.5    0/15       0    0/0      117.72  1472.5
builtin                      8     5  62.5%    110754    104673     24.2     6.2   32.5   13.0   15/0        0    0/0        0.00  1527.5
```

- **Each `--ai` is a player:** `builtin`, `<mod id>:<player>` (with the mod given by `--mod`),
  or `external:COMMAND` for a bot the arena starts for each game.
- **The games:** game *i* has the seed `--seed` + *i*, and moves every player one seat on, so
  that over a run each player plays each seat and race. A game ends after `--turns`, at a
  victory, or when one empire is left; its winner is the victory's, the last standing, or
  the best score.
- **The columns:** wins, mean and median final score, mean colonies, systems, tech levels and
  ships, battles won and lost, eliminations, failed requests and decisions the classic AI
  made instead (`fail/fb`), the player's time per turn, and an Elo rating.
- **What it writes** (`--out=DIR`, default `./arena`): `report.json` and `report.csv`,
  `games.csv` (one line per game and seat), `over_time.csv` (each player's figures turn by
  turn), and each game's saved game, which the client opens with the mods it was played
  with. `--replay=DIR/games/game-0003.json` plays a game again and compares its checksum.

**Measuring well.**

- **Play enough games.** Results vary a lot from seed to seed; four games tell you little,
  dozens tell you something. Keep the same `--seed` when comparing two versions of a player,
  so both meet the same galaxies.
- **Compare with the right baseline.** A player that overrides nothing, every decision made
  by `ai.builtin`, plays exactly the built-in AI's games (the classic AI's own bookkeeping
  runs for it: [Computer players](computer-players.md#the-classic-ai-as-a-library)), so
  `builtin` itself is the baseline: what your player wins or loses against it, its own
  decisions win or lose. A player with `classic_state = false` that still asks `ai.builtin`
  plays worse, because the classic ministers then read a classic state that never moves.
- **Look at `over_time.csv`** to see when a player falls behind (colonies by turn 30?
  ships by turn 60?), and at a game's saved game to see why.
- **Three players at once** (`--ai` three times) put each in the same galaxies: a direct
  comparison.

## External bots

An external bot is a player that runs as a program of its own, on CPython 3.10 or newer,
with any library (numpy, torch) and no budget, and plays through a connection to the game.
It is the same `Player` class, so the same code can be shipped in a mod later.

```sh
OPENSE4_BOT_TOKEN=3f2a... opense4-sdk bot pioneer:Pioneer --path mods/examples/small-ai/ai --port 6722
opense4 --quick-start --ai=external:0 --bot-port=6722      # a game whose computer empires the bot plays
```

`opense4-sdk bot` writes out the `opense4` package OpenSE4 is built with and starts the bot
with `python -m opense4.bot`; in a copy of the source code, `PYTHONPATH=python python3 -m
opense4.bot ...` does the same. The game listens on this computer only by default, and
every bot must give its token, which the game writes in its log. The dedicated server,
play-by-e-mail hosts and the arena take bots too ([bots-and-arena.md](../bots-and-arena.md#external-bots)).

Running your mod's player as a bot is also the easiest way to **debug** it: a `print` goes
to the terminal, and Python's debugger (`breakpoint()`) stops it mid-turn while the game
waits (up to its time limit per request, `--bot-timeout`).

## The training environment

`opense4.env` plays one empire of a game step by step, for players that learn:

```python
from opense4 import env

game = env.Game(seed=7, opponents=["builtin"], turns=200, builtin=("economy",))
view = game.reset()
done = False
while not done:
    commands = my_policy(view)                      # a list of commands (opense4.cmd)
    view, reward, done, info = game.step(commands)  # one game turn; reward: the change in score
game.close()
```

Each step is one game turn of empire 0: its commands are applied where a player gives its
orders, those the game refuses come back in the next step's `info["refused"]`, and the
reward is the change in the empire's score. `builtin=("politics", "economy")` lets the
classic AI make those calls; `game.builtin_commands("orders")` gives what it would order,
a baseline to imitate; `game.query(...)` asks a query. It is deterministic for a seed and
the commands given. Once trained, a policy becomes an ordinary `Player` whose `orders`
calls it, and plays as an external bot ([bots-and-arena.md](../bots-and-arena.md#the-training-environment)).

The environment is also a quick way to look at a data set from Python while you design a
player: start a game, look at `view.rules` and `view.my`, close it.
