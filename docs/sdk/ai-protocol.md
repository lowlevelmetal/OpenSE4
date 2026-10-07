# The computer-player protocol

How the engine and a script computer player talk (docs/MODDING_SDK.md, section 6). The same
messages are used in two places:

- **in the game**: as `script::Value` maps passed to and from the MicroPython runtime;
- **by external bots**: as JSON over a TCP connection, one message a line (section 10).

The engine side is in `src/sdk/` (`sdk/players.hpp`, with the engine's hooks in
`game/players.hpp`) and the Python side in `python/opense4/`. Neither side may depend on
anything not written here. Changing a message means changing this page, `api` and both
sides together.

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
classic_state = true             # the classic AI's bookkeeping runs for its empires (section 9)
```

- **`classic_state`** (true by default) keeps the classic AI's own bookkeeping running for
  the empires the player plays, so that `ai.builtin` and every callback the player leaves
  out see the state the classic AI would see (section 9). A player that makes every
  decision itself and never asks `ai.builtin` may set it to false to save the time.
- **The name** may not hold `:`; module and class are Python names (the module's parts
  separated by dots). `opense4-sdk check` finds the module under `ai/` (`admiral.py`, or
  `admiral/__init__.py`), the class in it (defined there, or taken there from another of
  the mod's modules with `from .fleet import Admiral`), files under `ai/` whose names
  Python cannot import, and files that do not compile; `opense4-sdk info` lists the
  players.
- **The player's code:** the engine adds the files of the mod's `ai/` folder to the
  interpreter at its root, so `ai/admiral.py` is the module `admiral` and
  `ai/fleet/admiral.py` the module `fleet.admiral`. `module` names it that way, and a
  mod's modules import one another by those names (`import helpers`), as when the folder
  runs as an external bot. The `opense4` package (`python/opense4`,
  docs/sdk/python-api.md) is there too; its dispatcher answers the requests. Two mods of
  one game that both have a module of the same name conflict: the later one's is left out
  (and logged).
- **Choosing a player:** a game names one with the controller
  `{"kind": "script", "mod": "<mod id>", "player": "<name>"}` on an empire. That is
  `EmpireSetup::controller` and `Empire::controller` (save format 9), written
  `<mod id>:<name>` in setup files and on the command line:
  - the setup model (`client/classic/screens/setup_model.hpp`): `NewGameSettings::computerPlayer`
    plays every computer empire that names no player of its own, random ones included;
    `computerPlayerChoices` lists what the game's mods offer;
  - the server's setup files: `ai = "<mod id>:<name>"` at the top for every computer
    empire, and in an `[[empire]]` for that one (docs/MULTIPLAYER.md, "Setup files");
  - `--ai=<mod id>:<name>` gives it to every computer empire of the quick game that
    `opense4 --quick-start` starts (games set up in the client choose in Game Setup), or of
    a hosted game (`opense4-server`, which also takes `--ai=N` for the number of computer
    empires).

  The mod must be one the game uses: a setup that names a player of another mod, or one
  its mod does not declare, is refused with a message that says so
  (`sdk::checkControllers`).
- **Other controllers:**
  - `{"kind": "builtin"}` (`builtin`): the classic AI, the default;
  - `{"kind": "external", "slot": <n>}` (`external:<n>`): the external bot connected to the
    host's slot n (section 10). Empires may share a slot: its bot plays each of them.
- **Which empires:** a controller plays its empire while the empire is computer-controlled
  (PlayerKind Computer or Neutral). A human empire's controller is not used; the computer
  plays a human who is away with the built-in AI, as before. The defaults above (the setup
  model's, a setup file's top `ai`, `--ai`) go to computer empires; a neutral empire gets
  a player only from its own entry.

## 2. Sessions

- **One session per engine call** that needs a player: a simultaneous turn
  (`processTurn`), a turn-based call (`resumeTurnBased`, `endPlayerTurn`, `applyLive`,
  `startHumanTurn`), or a battle a window shows (`TacticalBattle::Setup::scriptPlayers`).
  A call made again after it stopped for a battle answer is a new session (section 8).
- **Sessions are per empire** on the player's side: the dispatcher keeps one per empire
  asked in the engine call, each with its own `Player`, from the empire's first request
  (which carries `player`) to its `end_session`.
- **The interpreter:** a session starts nothing until a request must go to a player
  (one that has no answer waiting in the journal). Then it starts a thread with an 8 MiB
  stack and, on it, one interpreter (docs/sdk/runtime.md) holding the `opense4` package
  and the `ai/` files of every mod whose players the game uses. Every call into the
  interpreter runs on that thread, so the C stack the runtime needs is there whichever
  thread plays the turn. Several script empires share the interpreter; each has its own
  player object and memory. The interpreter is one per process: another session waits for
  it. A game with mods' rules scripts runs them in the same session and interpreter
  ([rules.md](rules.md), "Sessions").
- **The `Player` object** is made by the package's dispatcher from the `player` field of
  the empire's first request in the session, and kept until the session ends. Attributes
  set on `self` survive between requests within the session, and are lost after it; keep
  in `memory` what must last.
- **`self.memory`** is a `Value`. It is given to the player with its first request in the
  session and read back after every request. The engine stores it in the empire's state
  (`Empire::script.memory`, as JSON text: saved, sent and checksummed), with a size limit
  (`GameOptions::aiMemoryLimit`, 1 MiB by default, measured as JSON). Only `memory`
  survives between sessions. Other empires' views never hold it.

## 3. Requests (engine → player)

Every request is a map, its fields in this order:

| Field | Type | Meaning |
|---|---|---|
| `api` | int | 1 |
| `call` | string | One of the calls below |
| `empire` | int | The empire played |
| `turn` | int | The game turn (`GameState::turn`: in a simultaneous turn, the one the orders were given for) |
| `seed` | int | A random seed for this request (0 to 2⁶³−1), from the game's seed, the turn, the empire, the call, its arguments and how many such requests came before in the session; the same every time the request is made again |
| `view` | map or null | The empire's view (docs/sdk/view.md) for the planning calls: what it knows, or the whole game with the game option "computer players see everything" (`GameOptions::aiSeesEverything`); null for the other calls |
| `player` | map | Only in the empire's first request of the session: the player to make. A mod's: `{mod, name, module, class}`, its `[[ai.players]]` entry (the dispatcher imports `module` and makes an instance of `class`); an external bot's: `{slot}` (the bot makes its own). If making it fails, the dispatcher answers that empire's later requests in the session with the same error. |
| `memory` | any | Only in the empire's first request of the session: the stored memory (null the first time) |
| `args` | map | Per call, below. Every call's `args` may also hold `refused` (section 4). |

The calls, in the order a turn asks them:

| Call | When | `args` | Answer |
|---|---|---|---|
| `politics` | Start of the empire's turn, where the classic Politics minister acts (section 9) | `{}` | commands |
| `orders` | Start of the empire's turn, after politics and the delivery of its messages | `{}` | commands |
| `colony_type` | One of its colony ships founds a colony | `{colony, planet, vehicle, choices}`: the new colony (by its planet's id, as colonies are named), its planet's object id, the colony ship's vehicle id, and the empire's colony types (text). The colony already stands, with the classic choice as its type. | one of `choices`, or null for the classic choice |
| `enter_sector` | One of its groups is about to step into a sector holding objects of an empire it is hostile to and sees, in either turn style (groups of drones only, and groups whose members are all cloaked, always enter, as for a human) | `{vehicles: [ids], sector: {system, x, y}, enemies: [empire ids]}` | true, false, or null (enter). A group that declines stops before the sector with its order waiting, and is not asked about that sector again during the same movement run. |
| `decloak` | One of its cloaked colonies is about to carry out an order, or one of its cloaked ships is about to attack (turn-based Attack): where the classic Ship Cloaking minister lowers the cloak | `{object, vehicle, planet, reason}`: `object` the ship's vehicle id or the colony's planet id; `vehicle` and `planet` the same, one of them null, which says which it is; `reason` `"order"` or `"attack"` | true (decloak), false (stay cloaked: a colony is then not cloaked again after the order either), or null (the minister: decloak) |
| `battle_round` | Each combat turn of a space battle the empire's side fights, at the start of its phase, after its drones and seekers have moved | `{battle}`: the battle as the side sees it (section 5) | `{orders: [tactical orders]}`, or null to let its strategies play the phase |
| `economy` | End of the empire's turn, before income, research and construction | `{}` | commands |
| `end_session` | The session ends, for every empire asked in it (not one out for the turn, section 7) | `{}` | nothing; the last chance to update `memory` |

The view is null in the calls between the planning calls, which can come many times a
turn. A player keeps what it needs from the planning calls on `self`, or asks queries.

## 4. Responses (player → engine)

A response is a map; any other value, or an unknown field, fails the request.

| Field | Type | Meaning |
|---|---|---|
| `commands` | list | Commands to apply now, in order (planning calls only; a non-empty list in another call fails it) |
| `answer` | any | The call's answer, as in the table above. An answer that is not one the call takes (a colony type not in `choices`, a non-boolean for `enter_sector`) fails the request. |
| `memory` | any | The player's memory after the call; absent keeps it |
| `notes` | list | `{object, kind, text}` notes for the client's AI view (docs/sdk/python-api.md, "Watching a player think"): `object` an id or null, `kind` what it names: `vehicle`, `fleet`, `object` (a stellar object; a colony by its planet; the default), `system`, `empire`, `design` or `message`. A note replaces the earlier note on the same thing; an empty text only removes it. Kept in the empire's state (`Empire::aiNotes`) for the turn it was given and the next, never saved or sent. |
| `log` | list | Lines of text for the game's log file (opense4.log, not the empire's in-game Log) |
| `error` | map or absent | `{type, message, traceback}`: the request failed (section 7). An exception the player's code raises comes back this way, with the traceback as the runtime writes it (file and line of each call), and with no commands and a null answer. |

**Commands:**
- Each command is decoded (docs/sdk/commands.md) and goes through `game::apply`, exactly
  as a human's, then the moment does what it does with orders (a turn-based start of turn
  delivers messages at once; a takeover carries out moves at once).
- A refused command (one that does not decode, or that the rules refuse) is reported back
  in the empire's next request (whatever its call) in `args.refused` as
  `[{index, command, reason}]`, `index` its place in the list, and in the log; the rest
  still apply. Tactical orders a battle refuses are reported the same way.
- A player that wants to react within the same call uses the `apply` service (section 6).

## 5. The battle state

`battle_round` gets the battle as the side sees it on the tactical screen:

| Field | Type | Meaning |
|---|---|---|
| `location` | location | Where. |
| `round` | int | The combat turn, from 1. |
| `rounds_max` | int | The last combat turn the battle can have. |
| `phase_order` | list of empire id | The sides' phase order, drawn when the battle began. |
| `pieces` | list of battle_round_piece | Everything in the battle; a piece's `id` is its place in the list, which tactical orders name. |
| `objects` | list of battle_round_object | The planets (and obstacles) among the pieces, with what only they have. |

`battle_round_piece`:

| Field | Type | Meaning |
|---|---|---|
| `id` | int | Its place among the pieces. |
| `kind` | battle_piece_kind | Vehicle, planet, unit group, seeker or obstacle. |
| `owner`, `start_owner` | empire id, or null | Its side now, and when the battle began. |
| `vehicle`, `planet`, `design` | ref, or null | What it stands for. |
| `name` | text | Its name. |
| `type` | vehicle_type, or null | For vehicles and unit groups. |
| `position` | `{x, y}` | Its square. |
| `size` | int | Squares across. |
| `facing` | int | 0 up, 1 right, 2 down, 3 left, 4 up-right, 5 up-left, 6 down-right, 7 down-left. |
| `alive`, `mothballed`, `cloaked`, `captured` | bool | `cloaked`: cloaked when the battle began. |
| `damage` | int | Damage in percent. |
| `hit_points`, `full_hit_points` | int | What is left, and the whole. |
| `shields`, `shields_max` | int | Its shields. |
| `movement`, `speed` | int | Movement left this combat turn, and its full movement. |
| `supply`, `has_supply` | int, bool | Supply on board; whether it may fire. |
| `count` | int | Units in a group (seekers: members). |
| `acted` | bool | It has had its turn this phase. |
| `leader`, `is_leader`, `group`, `formation` | int, bool, int, int | Combat groups (-1: none). |
| `budget`, `engaged` | int | Targets it may engage this combat turn, and has. |
| `launch_left` | `{fighters, satellites, drones}` | What it may still launch this combat turn. |
| `seek_target`, `launcher`, `carrier`, `drone_target` | int | Pieces (-1: none). |
| `weapons` | list of battle_round_weapon | Its weapons, in the order tactical orders number them. |
| `cargo` | list of unit_stack | Units on board. |
| `troops`, `boarding_attack` | bool, int | Whether it carries troops; its boarding strength. |

`battle_round_weapon`: `index`, `component` (component index), `kind` (weapon_kind),
`range` (the longest range with damage; seekers: their travel), `reload` (one counter per
instance, 0 ready), `reload_rate`, `ready` (instances ready now), `instances`, `together`
(weapons fired as one shot), `enabled`.

`battle_round_object`: `piece`, `planet`, `population` (millions), `plague`, `invader`
(empire id, or null), `landed` (list of unit_stack: the invader's troops on the ground).

**The answer's orders** are the tactical orders of docs/sdk/commands.md ("Tactical
orders"), for the side's own pieces, checked and carried out one at a time as a player's
(`combat::TacticalBattle::check`): a refused one changes nothing and is reported. Pieces
given no order do nothing in the phase. `end_phase` ends the phase at once; `auto_phase`
lets the strategies play the rest of it; `resolve_combat` does that and hands the side to
its strategies for the rest of the battle (it is not asked again); `auto` with a piece
lets that piece act by its strategy now; the battle's Auto switch (`auto` with piece -1)
is refused: it belongs to the players at a window. Null, or no answer, lets the strategies
play the phase exactly as they would for the built-in AI.

`battle_round` is asked in the battles the engine fights. A battle a window shows on one
computer (section 8) asks the script sides only when the window's battle had their answers
(`BattleAnswer::decisions`); otherwise their strategies fight them there, as the window
showed. The combat simulator never asks.

## 6. Services (player → engine, during a request)

While a request is being handled, the player may ask the engine. In the game these are
native functions of the module `_opense4`, one per service, each taking one argument (the
map of the service's arguments below; `rules` takes `{}` or nothing) and returning its
result: `_opense4.query({"name": "path", "args": {"vehicle": 31, "destination": 4}})`. A
service that refuses raises an exception in the script (a `ValueError` for a bad argument,
with the path to it, as commands are refused; a `RuntimeError` when the service is not
for this moment). For external bots they are messages on the same connection
(`sdk::ServiceCall`).

| Service | Arguments | Result | Cost (bytecodes) |
|---|---|---|---|
| `query` | `{name, args}` | A query of docs/sdk/view.md ("Queries"), evaluated now on the view's own state | 20,000 + 20 per value of the result |
| `rules` | `{}` | The rules view (built once per session) | 5 per value |
| `builtin` | `{call: "politics", "orders" or "economy", ministers: [names] or null, skip: [names]}` | The commands the classic ministers would give for this empire now, from the ministers named (null: all) less those skipped; the names are those of the `minister` enumeration (docs/sdk/commands.md; the AI_Strategies join counts as `design`'s). They plan as the engine's own classic answer does (section 9): `politics` begins with the commands that set the movement options, systems to avoid and claims the classic AI writes directly (with the `politics` minister); `orders` plans with the claims and figures the empire's turn began with; `economy` with the units reserve, the colonization lists and those figures, as the classic economy step reads them. With every minister, exactly what the built-in AI plans from this state. The commands are not applied. | 2,000,000 + 20 per value of the result |
| `builtin_answer` | `{call, args}` | The classic answer to `colony_type` (`args.planet`: the planet's id), `enter_sector` (true), `decloak` (true) or `battle_round` (null) | 20,000 |
| `apply` | `{command}` | Applies one command now, as the call's own commands are (a mod's order excepted: it is refused here, as its rules cannot run while the player's script does; give it with the call's commands), and returns `{ok, reason, changed, removed}`: what changed in the view since the request's (or the last apply's): for each list of records with ids (`colonies` by planet) the records added or changed (`changed`) and the ids gone (`removed`), and any other part that differs, whole. Planning calls only. | 50,000 + 5 per value of the new view + 20 per value of the result |

**Budget:** each service counts its cost against the request's budget, as if the script had
run that many bytecodes (`script::Interpreter::charge`), so asking cannot be used to escape
the limits; a request that runs out of budget inside a service stops at its next bytecode.
Converting the view and the arguments into Python is not counted. External bots have no
budget.

## 7. Limits and failures

- **Per request:** a bytecode budget, from the game's options (save format 9; the server's
  setup files set them, docs/MULTIPLAYER.md):
  - a planning call: `GameOptions::aiPlanningBudget`, 200 M bytecodes by default;
  - any other call (end_session included): `GameOptions::aiCallBudget`, 5 M.
- **Per session:** one heap of 64 MiB for every script player of the call; the C stack
  limit is the runtime's 256 KiB (1 MiB in sanitizer builds), on the session's own thread.
- **A failed request:** an error the player reports (`error`), an exception, an exhausted
  budget, an exhausted heap, a memory over its size limit, a response of the wrong shape,
  an answer the call does not take, or a player that cannot be made (its mod or player is
  missing). Then:
  - the engine logs it with its traceback (opense4.log);
  - it uses the classic AI's answer for that request: the classic ministers for a
    planning call (what the `apply` service carried out before the failure stands),
    the classic colony type, entering, decloaking, the strategies;
  - nothing of the response counts but its `log` lines: neither its commands nor its
    memory nor its notes;
  - the session keeps running.

  Failures are counted per empire and game turn (`Empire::script`, saved): after three in
  one game turn the classic AI answers for that empire for the rest of the turn, and it
  gets no end_session. The next game turn starts afresh.
- **Who hears of it:** `sdk::PlayerSetup::failures` is told of each failure as it happens
  (`sdk::PlayerFailure`: the empire, its player, the game turn, the call, the error in one
  line, the traceback, and whether the classic AI now plays the rest of the turn), on the
  engine call's thread. A failure the journal gives again (section 8) is not told twice.
  The client shows them in a notice in the main window, with a window of their tracebacks.
- **External bots** have the host's time per request instead of a budget (section 10): a
  bot that doesn't answer in time, a slot with no bot connected, and a connection that
  breaks during the request are failures, as is a message that is not the protocol's JSON.
  The engine's side is `sdk::ExternalBot` (a request, with the services while the bot
  works on it), `sdk::BotHost` its connection.

## 8. The journal

Every request's response is recorded in the game state's journal (`GameState::journal`,
`game::DecisionJournal`) as a `JournalEntry`: the turn, the empire, the call, a digest of
the request (an FNV-1a hash of the call, the empire, the turn and the arguments without
`refused`; not the view nor the memory) and the response as the engine took it, as JSON:
the player's own fields, its `memory` only when it changed, and `applied`, the commands
its `apply` service carried out during the request. A failed request is recorded with its
`error`. The journal keeps the entries of the game turn in progress and the one before.

Answers waiting in `journal.replay` are given again, in order, instead of asking: the
next request takes the next entry when its turn, empire, call and digest match (what
`apply` carried out is carried out again first); the first request that differs drops all
that wait, and the players are asked from there on. They wait there:

- **A call made again after a battle stop.** On one computer a battle stops the call
  (docs/ENGINE.md, "Battles shown as they happen"): the game goes back to where the call
  began, and the answers given so far wait in `journal.replay`. The same call made again
  with the battle's answer is a new session: up to the battle, every answer comes from the
  journal; after it, the players are asked, a fresh player object getting the memory as the
  journal left it.
- **A battle a window shows.** The window's `combat::TacticalBattle` asks the script sides
  through the session it is given (`Setup::scriptPlayers`), and `TacticalBattle::decisions`
  holds their answers; `BattleAnswer::decisions` carries them into the call made again,
  where the battle gives them again. The client's Strategic Combat and Tactical Combat
  windows make that session (`game::makePlayers` on the battle's copy of the game) whenever
  the game has script or external players, so a battle fights their sides the same way
  shown or not; the window lets the session go (and with it the script runtime, one per
  program) before the call is made again.
- **A turn played again.** `game::replayJournal(again, played)` puts a played turn's
  answers before the same turn played again from its start: the client's movement replay
  asks nobody.

The journal is kept in memory only: it is not saved, sent or hashed, and other empires'
views never hold it. Network and play-by-e-mail games are played on their host alone,
which runs the script players (the mods are its to have); players' copies only receive
views. A host that plays a turn-based game file's computer turns once for the turn files
and again when it processes the orders asks the players again; in-game players are
deterministic, so the answers are the same. External bots need the journal; in-game players
are deterministic anyway (the tests check that a replay that asks again gets identical
answers). A turn-based game file whose computer players play when its turn files are
written (it was saved between two players' turns) is saved again then if any of its
empires has an external player, so that processing the orders does not ask that player
again.

## 9. The classic AI's own steps for an empire a player plays

For an empire its script or external player plays (`game::playedByController`), the
player answers the calls of section 3 in the classic ministers' place. Around those calls
the built-in AI also keeps state of its own: what it remembers and works out between its
decisions. Each of its steps is either **bookkeeping**, which changes only that state (the
classic ministers read it, nothing else in the game does), or an **action**, which touches
the game: other empires see it or the rules read it.

- **Bookkeeping runs** for the player's empire as for a computer empire (unless the player
  asks for none, below), so `ai.builtin` and the classic answer to every call the player
  leaves out plan from the state the built-in AI would have. A player that overrides
  nothing plays exactly the built-in AI's game: the SDK's tests play such a player beside
  the built-in AI, turn by turn in both turn styles, and compare the whole state
  (`tests/sdk/test_sdk_players_classic.cpp`).
- **Actions are the player's.** What the built-in AI writes into a computer empire directly
  is, for a player's empire, commands of the classic answer of the call where the built-in
  AI does it; the player gives them, filters them or leaves them out.

| Step of the built-in AI | When | Kind | For an empire a player plays |
|---|---|---|---|
| The start-of-turn figures (`ai::startOfTurnFigures`): net income and revenue | First thing in the empire's start of turn | Bookkeeping | Worked out; the classic `orders` and `economy` answers plan with them |
| The state update (`ai::updateAiState`): the demand lists and the systems to avoid or attack forgotten every ten turns, the AI state machine (state, turns in it, targets, staging, defended systems, the after-attack timer) | Start of turn | Bookkeeping | Runs |
| The four AI_Settings movement options and the systems it agreed to leave copied into the empire's options (part of the state update) | Start of turn | Action: they decide how its ships move | Commands at the start of the classic `politics` answer (`set_encounter_options`, `set_system_flags` with `avoid`), only where the empire's options differ |
| The political step (`ai::politicalStep`): anger toward every empire in contact, and its mark of what it counted | Start of turn | Bookkeeping (other players see anger only as the leader's mood in their Empires window) | Runs |
| The claims (`ai::claimTerritory`): its colony systems and their neighbours | First thing in the Politics minister's run | Action: other empires see them, and their classic AIs read them | Commands of the classic `politics` answer, after the options (`set_system_flags` with `claim`); the Politics minister then plans on the empire as they leave it |
| Accepted demands carried out half of the time, war decided without a declaration, demand-list entries used | The Politics minister | Actions | Already commands of the classic `politics` answer (`carry_out_demand`, `decide_war`, `use_demand_entry`) |
| The colonization lists the Orders ministers build (spec 05 §7.2) | Before the `orders` call, on the state the `politics` call left | Bookkeeping (the economy step reads their targets) | Built whatever the player answers; the classic `orders` answer plans with the claims and figures the turn began with |
| `ai::recordAiDecisions`: turns since war, treaty age, the attacks and spies noted for its demands (forgotten every ten turns), anger 100 toward an empire it declared war on | After the empire's orders (simultaneous: every empire's) | Bookkeeping | Runs, for the declarations the player sent too |
| The economy step's reads and the units reserve: the colonization lists, the start figures, the reserve its Ship Construction minister leaves (spec 05 §7.5) | The `economy` call | Bookkeeping | The classic `economy` answer plans with them; whoever answers, the lists and figures are used up after the call and the reserve is left as the classic step leaves it |
| `ai::rememberAiEvents`: battles fought and those in its territory, spies traced, mine fields met, its designs that fought | End of the game turn | Bookkeeping | Runs |
| The difficulty, the Computer Player Bonus on income and construction, its groups taking along others of its own with the same order, the Ship Cloaking minister (unless the player answers `decloak`), every rule of the game | | Rules | Apply, as to every computer player |

**`classic_state = false`.** A player whose `[[ai.players]]` entry sets it (section 1) gets
none of the bookkeeping: its empire's classic AI state stays as it was (the difficulty
excepted), and the classic ministers that `builtin` runs, or that answer a call the player
leaves out or fails, read it as it was. The classic `orders` answer then plans without
start figures, a classic answer sets and uses the lists and the units reserve as for any
computer player, and a player's own answer leaves them as an empire whose ministers do not
act leaves them. The actions are commands of the classic answers either way. It saves the
bookkeeping's time for a player that makes every decision itself: about 1.4 ms a turn per
empire in a debug build on the test fixtures' 14-system quadrant (the `sdk players bench`
test, run with `OPENSE4_SDK_BENCH=1`), more on larger maps. External bots keep the classic state.

## 10. The connection (external bots)

External bots (docs/sdk/bots-and-arena.md) reach the host over TCP: by default on the
host's computer only (127.0.0.1), on the port the host names (6722 unless told otherwise).
Each message is one line: a JSON object (script/json.hpp: UTF-8, whole numbers within 64
bits, no fractions, no repeated keys, at most 100 levels) and a line feed (a carriage
return before it is allowed; blank lines are skipped). Each message is a map whose first
key names its kind; requests, services and their answers also carry `id`.

| Direction | Message | When |
|---|---|---|
| bot → host | `{"hello": {"api": 1, "token": T, "slot": N or null, "name": text}}` | First, at once (within 10 seconds) |
| host → bot | `{"welcome": {"api": 1, "slot": N, "game": text, "timeout_ms": M}}` | The bot plays slot N; M is its time per request |
| host → bot | `{"refused": {"message": text}}` | Instead, then the host closes the connection: a wrong token, another api, a slot the game does not have, every slot taken, a slot whose bot is in the middle of a request |
| host → bot | `{"request": <request, section 3>, "id": I}` | A decision of one of the slot's empires |
| bot → host | `{"service": {"name": S, "args": <map>}, "id": I}` | During request I, any number of times (section 6) |
| host → bot | `{"result": <value>, "id": I}`, or `{"service_error": {"type": E, "message": text}, "id": I}` | The service's answer; E is the error the game would raise in its own runtime (`ValueError`, `TypeError`, `RuntimeError`) |
| bot → host | `{"response": <response, section 4>, "id": I}` | The end of request I |
| either | `{"bye": {"reason": text, ...}}` | Then the connection closes. The host's says how the game ended when it did: `turn`, `game_over`, `winner`, `won_by` and every empire's `scores` (`{empire, alive, score}`) |

- **The token** is the host's secret for this game (`--bot-token`, else the variable
  `OPENSE4_BOT_TOKEN` of the host's environment, else 32 random hexadecimal digits the
  host writes in its log), compared without telling how much of a wrong one matched.
- **Slots.** A `hello` with `slot` null takes the lowest slot of the game that has no
  bot. A bot that connects to a slot that has one replaces it, unless the old one is
  answering a request; the old one gets `bye` and its connection closes.
- **Ids.** Requests are numbered per connection. A request the bot does not answer within
  its time fails (section 7); the host goes on to its next request and drops any later
  message with the old id. A bot that receives a request while it waits for a service's
  result knows that the host gave up on the request it is working on.
- **Failures** of the connection: the bot closing it, a message longer than 64 MiB, or a
  write that cannot be completed in time end the connection and fail the request; a line
  that is not JSON, or not a map naming its kind, fails the request and keeps the
  connection. A bot may connect again at any time. The host's first request to a slot in
  each engine call carries `player` and `memory` (section 3); a bot that connects in the
  middle of an engine call it was already asked in gets its requests without them and
  fails them, until the next call.
- **Sessions** are those of section 2: a request with `player` starts the empire's
  session (an external bot's `player` is `{slot}`: the bot makes its own player), and
  `end_session` ends it.

