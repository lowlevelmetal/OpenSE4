# The computer-player protocol

How the engine and a script computer player talk (docs/MODDING_SDK.md, section 6). The same
messages are used in two places:

- **in the game**: as `script::Value` maps passed to and from the MicroPython runtime;
- **by external bots**: as JSON over a local connection.

The engine side is in `src/sdk/` and the Python side in `python/opense4/`. Neither side
may depend on anything not written here. Changing a message means changing this page,
`api` and both sides together.

All names are lower_snake_case. Ids, enums and records follow docs/sdk/view.md, and
commands follow docs/sdk/commands.md.

## 1. Players in a mod

A mod declares its computer players in `mod.toml`:

```toml
[[ai.players]]
name = "Admiral"                 # unique within the mod; shown in the setup screens
module = "admiral"               # a module or package under the mod's ai/ folder
class = "Admiral"                # an opense4.ai.Player subclass in that module
description = "Plays the classic economy and its own war."
```

- **The player's code:** the engine adds the files of the mod's `ai/` folder to the
  interpreter at its root, so `ai/admiral.py` is the module `admiral` and
  `ai/fleet/admiral.py` the module `fleet.admiral`. `module` names it that way, and a
  mod's modules import one another by those names. The `opense4` package
  (`python/opense4`, docs/sdk/python-api.md) is there too; its dispatcher answers the
  requests.
- **Choosing a player:** a game names one with the controller
  `{"kind": "script", "mod": "<mod id>", "player": "<name>"}` on an empire. That is
  `EmpireSetup::controller` and the empire's state. The setup screens and the server's
  setup files offer it, and `--ai=<mod id>:<name>` gives it to every computer empire.
- **Other controllers:**
  - `{"kind": "builtin"}`: the classic AI, the default;
  - `{"kind": "external", "slot": <n>}`: an external bot connected to the host.

## 2. Sessions

- **One session per engine call** that needs the player: a simultaneous turn, a
  turn-based player's turn, or a battle replayed with an answer.
- **The `Player` object** is created at the session's first request and kept until its
  end. Attributes set on `self` survive between requests within the same session, and are
  lost after it.
- **Sessions are per empire.** One interpreter may serve the sessions of several empires
  at once (a simultaneous turn asks each of them in turn); each has its own `Player`.
- **`self.memory`** is a `Value`. It is given to the player when the session starts and
  read back after every request. The engine stores it in the empire's state (saved, sent
  and checksummed), with a size limit (default 1 MiB serialized). Only `memory` survives
  between sessions.

## 3. Requests (engine → player)

Every request is a map:

| Field | Type | Meaning |
|---|---|---|
| `api` | int | 1 |
| `call` | string | One of the calls below |
| `empire` | int | The empire played |
| `turn` | int | The game turn |
| `seed` | int | A random seed for this request, from the game's seed, turn, empire and call; the same every time the request is replayed |
| `view` | map or null | The empire's view (docs/sdk/view.md): fair, or whole when the game allows it. Given for the planning calls; null for the others unless stated |
| `player` | map | Only in a session's first request: the player to make. A mod's: `{mod, name, module, class}`, its `[[ai.players]]` entry (the dispatcher imports `module` and makes an instance of `class`); an external bot's: `{slot}` (the bot makes its own) |
| `memory` | any | Only in a session's first request: the stored memory (null the first time) |
| `args` | map | Per call, below |

The calls:

| Call | When | `args` | Answer |
|---|---|---|---|
| `politics` | Start of the empire's turn, before messages are delivered (the classic Politics minister's moment) | `{}` | commands |
| `orders` | Start of the empire's turn, after politics and delivery | `{}` | commands |
| `economy` | End of the empire's turn, before income, research and construction | `{}` | commands |
| `colony_type` | A colony is founded | `{colony, planet, vehicle, choices: [names]}` | a colony type name, or null for the classic choice |
| `enter_sector` | A group's move would enter a sector with enemies | `{vehicles: [ids], sector: {system, x, y}, enemies: [empire ids]}` | true, false, or null (enter) |
| `decloak` | A cloaked vehicle or colony needs to decloak for an order or an attack | `{object, reason: "order" or "attack"}` | true, false, or null (the classic minister) |
| `battle_round` | Each round of a space battle the empire fights | `{battle}`: the battle's state (section 5) | `{orders: [tactical orders]}`, or null to let the strategies decide |
| `end_session` | Before the session ends | `{}` | nothing; the last chance to update `memory` |

The view is null in `colony_type`, `enter_sector`, `decloak` and `battle_round`, which can
come many times a turn. A player keeps what it needs from the planning calls on `self`,
or asks queries.

## 4. Responses (player → engine)

| Field | Type | Meaning |
|---|---|---|
| `commands` | list | Commands to apply now, in order (planning calls only) |
| `answer` | any | The call's answer, as in the table above |
| `memory` | any | The player's memory after the call |
| `notes` | list | `{object: id, kind, text}` notes for the client's AI view. `kind` says what the id names: `vehicle`, `fleet`, `object` (a stellar object; a colony by its planet), `system`, `empire`, `design` or `message`. A note replaces the earlier notes on the same thing for this turn; an empty text only removes them |
| `log` | list | Lines for the game's log file (not the empire's in-game Log) |
| `error` | map or absent | `{type, message, traceback}`; the engine falls back to the classic AI for this request and logs it. An exception the player's code raises comes back this way, with the traceback as the runtime writes it (file and line of each call), and with no commands and a null answer |

**Commands:**
- Each command goes through `game::apply`, exactly as a human's.
- A refused command is reported back in the next request's `args.refused` as
  `[{index, command, reason}]` and in the log, and the rest still apply. Planning calls
  only.
- A player that wants to react within the same call uses the `apply` service (section 6).

## 5. The battle state

`battle_round` gets the battle as the empire sees it on its tactical screen:
- `round`, `rounds_max`;
- `pieces`: each with `id`, `owner`, `design`, `position {x, y}`, `facing`, `damage`, `shields`, `weapons` (with `range`, `reload`, `ready`), `speed` and `cloaked`;
- `objects`: planets and other objects in the battle.

The answer's orders are the tactical orders of docs/sdk/commands.md ("Tactical orders"),
for the empire's own pieces only.

## 6. Services (player → engine, during a request)

While a request is being handled, the player may ask the engine. In the game these are
native functions of the module `_opense4`, one per service, each taking one argument (the
map of the service's arguments below) and returning its result:
`_opense4.query({"name": "path", "args": {"vehicle": 31, "destination": 4}})`. A service
that refuses raises an exception in the script (a `ValueError` for a bad argument, with the
path to it, as commands are refused). For external bots they are messages on the same
connection.

| Service | Arguments | Result |
|---|---|---|
| `query` | `{name, args}` | A query of docs/sdk/view.md ("Queries"), evaluated now |
| `rules` | `{}` | The rules view (built once per session) |
| `builtin` | `{call: "politics", "orders" or "economy", ministers: [names] or null, skip: [names]}` | The commands the classic AI would give for this empire now, from the ministers named (null: all) less those skipped; the names are those of the `minister` enumeration (docs/sdk/commands.md). The commands are not applied. |
| `builtin_answer` | `{call, args}` | The classic answer to `colony_type`, `enter_sector` or `decloak` |
| `apply` | `{command}` | Applies one command now and returns `{ok, reason}` plus the changed parts of the view (planning calls only) |

**Budget:** services count against the player's budget (by their cost in engine work), so
asking cannot be used to escape the limits.

## 7. Limits and failures

- **Per request:** a bytecode budget and a memory heap (docs/sdk/runtime.md). The budget is
  set per game by the host:
  - defaults per planning call: 200 M bytecodes;
  - per mid-turn call: 5 M.
- **On an error, exhausted budget or exhausted memory:**
  - the engine logs the error with its traceback;
  - it uses the classic AI's answer for that request;
  - it keeps the session running.

  After three failures in one turn, the classic AI plays the empire for the rest of the
  turn.
- **External bots** have the host's turn timer instead of a budget. A bot that doesn't
  answer in time is treated like an error.

## 8. The journal

Every answer and every list of commands a player gives is recorded with the turn,
`{call, args-digest, response}`. Replays, play by e-mail and desync repair read the
journal and never ask a player twice. In-game players are deterministic anyway; external
bots need the journal.
