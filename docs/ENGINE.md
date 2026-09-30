# The classic engine

`src/game` is the rules engine that plays Space Empires IV Deluxe from the player's own
data files. It is headless, deterministic, and implemented from the specs in
[spec/](spec/). This page is the map of the code. The rules themselves are in the specs.

```
ruleset (typed data files) ─> game::Rules ─┐
                                           ├─> game::processTurn(rules, state, orders)
player / AI / network ─> game::Command ────┘         │
                                                     └─> new GameState (+ logs, combat records)
```

## Data and state

| File | What it holds |
|---|---|
| `types.hpp` | Ids, `Resources`, treaties, moods, sight types, characteristics |
| `galaxy.hpp` | Systems, space objects, warp links, `Location` (system + sector) |
| `state.hpp` | The whole game: empires (race, research, intel, relations, knowledge, lists), colonies, designs, vehicles, fleets, messages, pending events, combat records, options |
| `rules.hpp` | `Rules`: the loaded data set plus caches (parsed abilities, tech gates, settings with defaults, race presets) |
| `abilities.hpp` | The closed list of ability identifiers used by the data, parsed once |
| `design.hpp` | Mounts, design validation, unique design names and statistics, movement points, supply, cargo, and generated starting designs |
| `orders.hpp` | Orders as they are given: Explore, Resupply, Repair and the composite orders expanded into simple ones (spec 03 §8) |
| `map_file.hpp` | Map files in our own text format ([MAPS.md](MAPS.md)): Save Map, and loaded maps with starting points |
| `query.hpp` | Read-only questions: what is where, space yards, capacities, hostility |

`GameState` is plain data. Every field is serialized (`serialize.hpp`), and
`stateChecksum` hashes the serialized bytes for desync detection.

## Changing state

- **Players change state only through commands** (`commands.hpp`). There is one command
  per player action: orders, fleets, queues, designs, research, intelligence,
  messages, waypoints and so on.
  - `game::apply` validates a command against the state and applies it, or rejects it
    with a reason.
  - One empire's commands for one turn form an `EmpireOrders`, the equivalent of the
    classic `.plr` file.
- **Subsystems change state only during turn processing**, through a `TurnContext`. The
  context also carries transient per-turn data: mood events, battle sites and
  rejected commands.

## Turn order

`processTurn` (`turn.cpp`) follows the original's order of a simultaneous turn, spec 05 §8
(confirmed: binary). Empires are always taken in empire-number order, and destroyed
empires are skipped.

1. **Orders.** Each human's `EmpireOrders` are applied, in player order. A human who sent
   nothing is played by the computer for this turn: all of its ministers are switched on
   and restored at the end of the turn. A player who ticked "AI should not make changes"
   only gets the bookkeeping.
2. **Messages:** `diplomacy::deliverMessages` processes the players' messages.
3. **Date.** `GameState::turn` stays the number the orders were given for until the end
   of the turn, so log entries and records carry it. The steps that depend on the date
   get `turn + 1`.
4. **Start of turn**, for each empire: `ai::updateAiState`, `ai::politicalStep` (it counts
   the turn processed before, whose battles `GameState::combats` still holds), then the
   ministers that act while orders are given (`ai::planOrders`: every minister for a
   computer player, the active ones for a human). Their messages take effect at once.
   Afterwards `ai::recordAiDecisions` notes what was decided.
5. **Movement and space combat** (`movement::runMovementAndCombat`). Over 30 days each
   vehicle, fleet and planet with orders carries out one order whenever its day counter
   reaches 1, and `combat::resolveSpaceCombat` runs in sectors where something acted and
   hostiles meet (spec 03 §6.3). Colony ships waiting at their planets then found their
   colonies (`movement::runColonization`), and sight and first contact are updated.
6. **End-of-turn processing**, one empire at a time (`empireEndOfTurn`), each followed by
   that empire's destruction check (`score::checkDestruction`):
   1. the ministers' end-of-turn actions (`ai::planEconomyStep`: Design, Research,
      Intelligence and construction);
   2. the statistics row (`score::recordStatistics`);
   3. intelligence (`intel::intelStep`) and
   4. research (`research::researchStep`), each spending the pool the previous turn filled;
   5. income (`economy::collectIncome`): production, tariffs (the master receives its
      share at once), the computer bonus, and the research and intelligence pools
      (`research::addToPools`);
   6. treaties and trade (`diplomacy::treatyStep`, which pays trade through
      `economy::collectTrade`);
   7. maintenance, 8. planets, 9. happiness, 10. construction (`economy::payMaintenance`,
      `processPlanets`, `updateHappiness`, `runConstruction`);
   11. repair (`movement::repairEmpire`);
   12. foreign designs last seen more than 50 turns ago are forgotten
      (`sight::forgetOldDesigns`);
   13. supply (`movement::supplyEmpire`);
   14. the storage cap, 15. system-wide abilities and training (`economy::applyStorageCap`,
      `applySystemAbilities`, `movement::trainEmpire`);
   17. ground combat where the empire's troops invade (`combat::runGroundCombat`);
   18. the log keeps only this turn's entries; the long record of the History window is
      `Empire::historyEvents`, which is never pruned (`addHistory`).

   Step 16's per-object upkeep is part of the supply step.
7. **Design cleanup** when a new year starts (`movement::purgeObsoleteDesigns`).
8. **Victory check** (`score::checkVictory`).
9. **Event step:** hazards (`movement::runStellarHazards`), the timed events that are due
   (`events::fireDueEvents`), then one roll for a new event for the whole galaxy
   (`events::rollNewEvent`).
10. **End.** Per-turn flags are cleared, sight and contact follow the events, the AI
    remembers the turn's battles and spies (`ai::rememberAiEvents`), the turn number
    advances and `economy::updateReports` projects next turn's income.

Mood events raised after an empire's happiness update (construction, ground combat, the
other empires' processing, events) wait in `GameState::pendingMood` for that empire's next
update. The game's research pool starts at Starting Resources plus one turn of research,
and the intelligence pool at 0 (`research::openingPools`, called by `createGame`).

OpenSE4 resolves every game this way; the turn-based style (spec 05 §8, "Turn-based game")
is not implemented. `empireEndOfTurn` is the per-player processing it would call.

## Determinism

- **Turn resolution uses integer math only.** No floats, and no wall-clock time.
- **Iteration order is fixed.** Containers are sorted by id, and results never depend
  on hash-map order.
- **All randomness comes from `GameState::rng`**, or from streams forked from it. The
  AI derives its own stream from the seed, the turn and the empire.
- **One seed plus one set of orders gives the same next state on every platform.**
  Galaxy generation uses floating point, so multiplayer sends the generated state
  rather than the seed. The integration test plays full all-AI games twice and
  compares checksums every turn.

## Multiplayer

- The host is authoritative. Clients apply their own commands locally so their windows
  update at once, and send them at End Turn.
- The host processes the turn when every human's orders have arrived, or on timeout,
  with the AI playing for anyone missing. It then sends everyone the new state.
- `src/net` carries this over TCP with UPnP port mapping. `opense4-server` hosts
  headless or processes PBEM turn files.

See [MULTIPLAYER.md](MULTIPLAYER.md).

## The client

`src/client/classic` presents the engine in the classic layout: a 1024×768 frame
scaled to the window, drawn with the art from the player's install.

| Part | Role |
|---|---|
| `session.*` | Rules, state and local player. Its `issue()` records commands, and it runs the End Turn flow for local, hotseat and network games |
| `art.*` | Pictures from the install, cached as textures |
| `ui.*` | The frame mapping, `UiContext`, the modal window stack, and the classic dialog layout |
| `main_window.*` | Status bar, command buttons, order strip, system, report and galaxy panels, and hotkeys |
| `reports.*` | Ship, planet, fleet and system reports |
| `screens/*` | One file per group of windows (designs, planets, queues, research, empires, log, ...) |
| `frontend.*` | Intro, quick start, game setup, load, and the multiplayer lobby |

## Tests

The engine tests use `tests/engine_fixture.cpp`, a complete invented rules set. Every
subsystem has its own test file. `test_integration.cpp` runs whole games.

Tests against your installed data are opt-in:

```sh
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests
```
