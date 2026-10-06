# External bots, the arena, the training environment and the mod tools

This page is for people who write computer players for OpenSE4 and want to run them
outside the game, measure them against each other, or train them. The plan behind it is
[docs/MODDING_SDK.md](../MODDING_SDK.md) (sections 6.3, 6.4 and 11); writing a player is
[python-api.md](python-api.md), and the messages underneath are
[ai-protocol.md](ai-protocol.md).

| You want to | Use |
|---|---|
| Run a player as a program of its own, with numpy, torch or anything else | an **external bot**: `python -m opense4.bot`, or `opense4-sdk bot` |
| Play against your own bot, alone or with friends | the client's or the dedicated server's `--bot-port`, and `--ai=external:N` |
| Find out which of two players is better | the **arena**: `opense4-sdk arena` |
| Train a machine-learning player turn by turn | the **training environment**: `opense4.env` |
| Check a mod's players before you share it | `opense4-sdk test` |
| Try a mod in the game | `opense4-sdk run` |

## External bots

An external bot is an ordinary Python program that plays one or more empires of a game
over a connection to it. It uses the same `opense4.ai.Player` class as a player that runs
inside the game, so a player can be developed and trained outside and shipped in a mod
later, unchanged. Outside the game it runs on CPython 3.10 or newer, with any library and
no budget; the game gives it a time limit per request instead.

### Running a bot

```sh
python -m opense4.bot admiral:Admiral --path mymod/ai --port 6722 --slot 0
```

- `admiral:Admiral` is a module and a class, an `opense4.ai.Player` subclass. `--path`
  puts folders in front of Python's module search path, as the game puts a mod's `ai/`
  folder at the root (default: the current folder), so a mod's `ai/` folder runs as a bot
  as it is.
- `--host` and `--port` say where the game listens for bots, `--slot` which external slot
  to play (without it, the first free one), and the token, the game's secret, comes from
  the environment variable `OPENSE4_BOT_TOKEN` (or `--token`, which other users of the
  computer can see in the process list). The game's tools that start bots themselves (the
  arena) set `OPENSE4_BOT_HOST`, `OPENSE4_BOT_PORT`, `OPENSE4_BOT_TOKEN` and
  `OPENSE4_BOT_SLOT`, which the bot reads when the options are not given.
- `--reconnect` connects again whenever the game ends a connection, for hosts that run
  once per turn (play by e-mail below). Stop the bot with Ctrl+C.
- `--wait=SEC` keeps trying to connect while the game is not listening yet (default 30).

The bot needs OpenSE4's `opense4` package on its path. In a copy of the source code that
is the `python` folder (`PYTHONPATH=python`). A release builds the package into its
programs: `opense4-sdk python` writes it out and prints the folder to put on
`PYTHONPATH`, and `opense4-sdk bot` does both and starts the bot with the same options:

```sh
OPENSE4_BOT_TOKEN=3f2a... opense4-sdk bot admiral:Admiral --path mymod/ai --port 6722
```

`--python=EXE` (or `OPENSE4_PYTHON`) chooses the Python to run; by default `python3`
(`python` on Windows).

From Python, `opense4.external.run(Admiral, port=6722, token="3f2a...", slot=0)` does
what the command does and returns once the game is over. `external.serve(connection,
Admiral)` answers requests on any `external.Connection`, as tests do.

### Where bots play

A computer empire whose controller is `external:N` is played by the bot connected to
slot N. Several empires may share a slot: their bot then plays each of them, with a
player object per empire (`self.empire_id` says which). A slot without a bot, a bot
that leaves, or a request it does not answer in time fails the request, and the classic
AI decides that one thing (ai-protocol.md §7). After three failures in a game turn, the
classic AI plays the empire for the rest of the turn.

| Where | How |
|---|---|
| The dedicated server | `--ai=external:N` for every computer empire, `--ai=external` for a bot of its own for each (slots 0, 1, ...), or `ai = "external:N"` in a setup file's `[[empire]]`. `--bot-port=N` (default 6722), `--bot-token`, `--bot-bind`, `--bot-timeout`. A new game starts once every player is ready and every external slot has its bot. Simultaneous and turn-based games, with human players over the network beside the bots (docs/MULTIPLAYER.md, "Bots on a host"). |
| Play by e-mail | `opense4-server pbem new` and `pbem process` take the same `--bot-...` options and wait `--bot-wait=SEC` (default 30) for the game's bots before the turn is played. A bot started with `--reconnect` stays between runs. |
| The game client | `opense4 --ai=external:0 --bot-port=6722` plays a quick game, or any new game, against your bot, and the bots play the computer empires of a network game you host from the client too. The log says where they connect and with which token. |
| The arena | `--ai=external:COMMAND`: the arena starts a bot for each game it plays (below). |
| The training environment | the environment itself is the bot of empire 0 (below). |

### Security

- **This computer only, by default.** The game listens for bots on 127.0.0.1. With
  `--bot-bind=0.0.0.0` (or another address) bots on other computers can connect, but the
  connection is not encrypted: only the token keeps others out, and anyone who can watch
  the network can read the game's requests, which hold the views of the bots' empires.
- **The token.** Every bot must present the game's token when it connects; a wrong one is
  refused and the connection closed. The game makes a new random token for each run
  unless it is given one (`--bot-token`, or `OPENSE4_BOT_TOKEN` in its environment), and
  writes it in its log.
- A bot is a program you start on purpose. Mods never start bots, and a bot is outside the
  game's sandbox, with whatever rights you give it.

### Time

A bot has the host's time per request: `--bot-timeout=SEC`, else the server's turn timer
(`--turn-timeout`), else 60 seconds. The game's own work during the request (the
services a bot asks) does not count. A bot that answers late has failed that request; the
game has moved on, and drops the late answer when it comes (each request has an id, and
the bot's messages carry it). A service call that arrives after the game has given up
raises `opense4.external.RequestAbandoned` in the player's code, which ends that call.

### Replays and the journal

The game records every answer a player gives (ai-protocol.md §8). Whatever replays a
turn, plays a call again after a battle shown in a window, or repairs a player's copy of a
network game takes the answers from there and never asks a bot twice, so a bot need not
be deterministic. A turn-based play-by-e-mail game file that is between two players' turns
when its turn files are written keeps the computer players' turns played then, so that
processing the orders does not play them again.

### Other languages

Nothing in the connection is Python's: it is one line of JSON per message over TCP, and a
bot in any language can speak it. ai-protocol.md §10 has the messages; the requests and
responses are those of the rest of that page.

## The arena

```sh
opense4-sdk arena --mod=mymod --ai=mymod.id:Admiral --ai=builtin --games=200 --turns=150
```

The arena plays games between computer players without a window, several at a time,
each game in a process of its own (`--jobs=N`, by default the computer's cores, at most
four), with the same turn processing as the game and the server. It needs your installed
game (or `--data=DIR`, `--classic-dir=DIR`) and the mods the players come from (`--mod`,
`--mods-dir`).

Each `--ai` is one player:

| `--ai=` | Who |
|---|---|
| `builtin` | the classic AI |
| `mod.id:Player` | a player a mod declares |
| `external:COMMAND` | an external bot: the arena runs COMMAND with the system's shell for each game the bot plays in, with `OPENSE4_BOT_HOST`, `_PORT`, `_TOKEN` and `_SLOT` in its environment and `PYTHONPATH` reaching OpenSE4's `opense4` package: `--ai="external:python3 -m opense4.bot mine:Mine --path bots"` |
| `NAME=...` | any of these, named NAME in the report |

**The games.** Game i (from 0) has the seed `--seed` + i, so a run of `--games=N
--seed=S` is the same as two runs that split it. A game has one empire per `--ai`
(`--empires=N` for more: the players take the seats in turn), and by default game i
moves every player i seats on, so that over a run each player plays each seat and each
race (`--no-swap` keeps them in place). The races are drawn from each game's seed unless
`--race` names them; `--systems`, `--quadrant-size`, `--quadrant` and `--turn-based`
shape the game, or `--setup=FILE.toml` takes the options and empires of a server
[setup file](../MULTIPLAYER.md#setup-files). A game ends after `--turns`, at a victory,
or when one empire is left. Its winner is the victory's, the last one standing, or the
best score at the end.

**What it writes** (in `--out=DIR`, default `./arena`):

| File | What |
|---|---|
| `report.json` | Everything below, and each game's summary |
| `report.csv` | One line per player: games, wins, win rate, mean and median final score, mean colonies, systems, tech levels, research and ships, battles won, lost and drawn, eliminations, requests, failures, fallbacks, the player's time per turn in milliseconds, Elo, and the most its player used in any game: time in one turn (milliseconds), bytecodes in one planning request and in one other request |
| `games.csv` | One line per game and seat: the seed, the player, the empire and race, won or not, the final figures, the checksum and the saved game |
| `over_time.csv` | Each player's mean score, colonies, systems, ships, tech levels, research, and battles won and lost, turn by turn |
| `games/game-NNNN.gam` | The game's final state: the game opens it (with the mods it was played with) |
| `games/game-NNNN.json`, `.result.json`, `.log` | What the game's process played, what came of it (turn by turn), its log; `-bots/bot-N.log` is each bot's output |

A battle counts as won for a side that keeps pieces while every other side lost all of
its own, lost for one that lost all of its pieces while another kept some, and drawn
otherwise. Failures are requests that failed (an exception, a budget run out, a wrong
answer, no answer in time); fallbacks are the decisions the classic AI made instead,
failures and the requests skipped after three failures in a turn. The player's time is
measured, not counted: it is never part of a game. The bytecodes a request used are what
it counted against its budget (a planning call's 200 million and any other call's 5
million by default, [ai-protocol.md](ai-protocol.md) §7), so the peaks say how close a
player came to its limits; external bots have no budget, and their peaks stay 0.

**Ratings.** Each player has an Elo rating in the report, from 1500 and the games of the
run in order: every two seats of different players are a match, the winner ahead, then
the better final score. `--ratings=FILE` keeps them across runs: the arena starts from the
file's ratings and writes them back.

**Playing a game again.** `opense4-sdk arena --replay=DIR/games/game-0003.json` plays
that game again in the same way and compares its final checksum with the recorded one:
the same, unless an external bot answered differently or the data or mods changed.

An example: eight games of a hundred turns on the installed game between the test
fixture's `Captain` (`tests/fixtures/mods/ai-fixture`: a player written with the `opense4`
package that keeps the classic ministers but for the Patrol minister, chooses its colony
types by a fixed rule and fires at the weakest enemy in range in battle) and the classic
AI, four games at a time in a debug build:

```text
$ opense4-sdk arena --mod=tests/fixtures/mods/ai-fixture --ai=test.ai-fixture:Captain --ai=builtin \
      --games=8 --turns=100 --seed=1 --jobs=4
game 1/8, seed 1: builtin (Sergetti) won by score after 100 turns (10.5 s)
...
game 8/8, seed 8: test.ai-fixture:Captain (Terran) won by score after 100 turns (13.5 s)

player                  games  wins   win%     score    median colonies systems  techs  ships   battles  elim   fail/fb  ms/turn     elo
test.ai-fixture:Captain     8     1  12.5%     73952     67522     15.1     4.9   32.5    7.1   15/63       0    0/0       49.67  1437.0
builtin                     8     7  87.5%    134019    127541     27.2     7.2   40.0   14.0   64/23       0    0/0        0.00  1563.0

8 games in 30.9 s
```

Its colony types, chosen without regard to the planets, leave `Captain` behind from
about turn 40 on (`over_time.csv`: a mean score of 47,494 against 71,003 at turn 50), and
its battles go badly. Every game of the run played again (`--replay`) to the same checksum.

## The training environment

`opense4.env` plays one empire of a game step by step, for programs that learn to play:

```python
from opense4 import env

game = env.Game(seed=7, opponents=["builtin"], turns=200, builtin=("economy",))
view = game.reset()
done = False
while not done:
    commands = my_policy(view)                      # a list of commands (opense4.cmd)
    view, reward, done, info = game.step(commands)
game.close()
```

- **One step is one game turn** of empire 0. Its commands are applied where a player
  gives its orders (the `orders` call), each checked as a human player's; those the game
  refuses come back in the next step's `info["refused"]`.
- **The reward** is the change in the empire's score over the turn (the score of a turn
  not yet played counts as 0). Compute any other reward from the views.
- **The game ends** (`done`) after `turns`, at a victory, or when the empire is
  destroyed. The last step's `info` holds the turn, whether a victory ended it, the
  winner and every empire's final score. `close()` (or leaving a `with` block) ends the
  engine at any time.
- **The rest of the turn.** `builtin=("politics", "economy")` lets the classic AI make
  those calls for the empire (by default the empire does nothing there: its construction
  and research queues stay as its commands set them); its colony types, battles and other
  questions are the classic AI's. `game.builtin_commands("orders")` gives what the classic
  AI would order now, a baseline to imitate or to start from, and `game.query(...)` runs a
  query (docs/sdk/view.md) on the state of the step under way; `view.rules` is the rules
  view.
- **Deterministic.** The same seed and the same commands give the same views, rewards and
  end. `reset(seed=None)` takes the constructor's seed the first time and one more each
  time after; `reset(seed=S)` any other.
- **The opponents** are `"builtin"` or players of mods (`"mod.id:Player"`, with the mod in
  `mods`); `setup=` takes a server setup file's options and empires, and `races`,
  `systems`, `quadrant_size` and `turn_based` shape the game as for the arena.
- **The engine** is `opense4-sdk env-host`, started as a child process for each game and
  reached over the external bots' connection on this computer, with a token of its own. It
  is found as `Game(sdk=...)`, else `OPENSE4_SDK`, else `opense4-sdk` on `PATH`; the game
  folder and mods are `data=`, `mods=` and `mods_dir=` (default: the installed game).
  `log=FILE` keeps its log.

**Training a machine-learning player.** The environment has the shape most reinforcement
learning libraries expect: wrap it in their environment class (an observation made from
`view.raw`, an action decoded into commands), run several environments in parallel
processes for throughput, and keep the seeds of the games you evaluate on apart from
those you train on. Once trained, the policy becomes an ordinary `opense4.ai.Player`
whose `orders` calls it: it then plays as an external bot (it needs your libraries), in
the arena against the classic AI and other players, and in network games.

## Testing a mod: `opense4-sdk test`

```sh
opense4-sdk test mymod
```

1. **The mod's Python tests.** Each `test_` function of each `tests/test_*.py` module of
   the mod runs in the game's own Python ([runtime.md](runtime.md)), with the mod's `ai/`
   folder and `tests/` at the root (so `import admiral` finds `ai/admiral.py`), the
   `opense4` package, and a budget of its own (`--budget=N` bytecodes, default a billion).
   A test fails by raising; `opense4.testing.Skip` (or pytest's skip) skips it.
2. **A short game for each computer player** the mod declares, against the classic AI, on
   your installed game (`--data=DIR`), `--turns=N` turns (default 10) from `--seed=N`
   (default 1), simultaneous unless `--turn-based`. Any failed request fails it, with the
   errors and tracebacks the game logged.

The tests have `opense4.testing`: `Harness` plays a player through requests as the game
does, `FakeServices` answers the services from prepared values, and under `opense4-sdk
test` `game_view(empire=0)` and `game_rules()` give the view and the rules view of a new
game of the mod's data set (two computer empires from the seed), so a test can call its
player on real data:

```python
from opense4 import testing
from admiral import Admiral

def test_it_orders_something():
    h = testing.Harness(Admiral, testing.FakeServices(builtin={"orders": []}))
    r = h.call("orders", view=testing.game_view())
    assert "error" not in r and r["commands"]
```

The same tests run under pytest with CPython (`PYTHONPATH` with the `opense4` package and
the mod's `ai/` folder), where `game_view()` and `game_rules()` skip. `--no-games` runs
only the Python tests, which then need no data set; `--no-tests` only the games. The
command prints a line per test and game and a summary, and exits with 0 when everything
passed and 1 otherwise, for a mod's own continuous integration.

## Trying a mod: `opense4-sdk run`

```sh
opense4-sdk run mymod -- --quick-start=Terran
```

starts the game (`opense4` beside `opense4-sdk`, or `--client=EXE`) with the mod and the
mods it requires, and with `--ai=` for the mod's first computer player (or `--player=NAME`)
so that the computer empires of new games are its. What follows `--` goes to the game as
it is; `--data=DIR` becomes its `--classic-dir`.

## For SDK developers

| Part | Where |
|---|---|
| The connection, the host's side (`sdk::BotHost`, `sdk::ExternalBot`) | `src/sdk/bots.hpp` |
| A headless game with its figures (the arena's, `test`'s, the environment's) | `src/sdk/match.hpp` |
| Child processes (POSIX and Windows, ended with what they started) | `src/sdk/process.hpp` |
| `test`, `run`, `arena`, `env-host`, `bot`, `python` | `tools/sdk_test.cpp`, `tools/sdk_arena.cpp`, `tools/sdk_env.cpp`, `tools/sdk_common.cpp` |
| The bot's side, the command, the environment | `python/opense4/external.py`, `bot.py`, `env.py` |
| The bot options of the server and the client | `src/server/main.cpp`, `src/client/main.cpp` |

The tests: `tests/sdk/test_sdk_bots.cpp` (the connection by hand: the handshake and its
refusals, services during a request, a bot that is late, breaks off or sends what is not
JSON; external empires in a game and the journal; the same player in the game and as a
CPython bot, request by request), `tests/sdk/test_sdk_arena.cpp` (the arena, its report
and its determinism, an external bot in it, the environment, `test`, `run`, and bots on
the dedicated server beside a network player, in both turn styles), and
`tests/sdk/python/test_sdk_external.py` (the bot's side under CPython, against a host
written in Python). The tests that need CPython skip without Python 3.10 or newer; `sdk
run`'s needs the installed game (`OPENSE4_CLASSIC_DATA`).
