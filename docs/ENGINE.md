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
| `design.hpp` | Mounts, design validation, movement points, supply, cargo, and generated starting designs |
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

`processTurn` (`turn.cpp`) currently runs the phases below. The original's order,
checked in the executable, is in spec 05 §8. It differs: for example, it spends
research and intelligence points before income, and runs each empire's end-of-turn
steps one empire at a time. The engine still has to be brought in line with it.

1. **Orders.** Each human's `EmpireOrders` are applied. Computer empires, and humans
   who sent nothing, are played by `ai::planTurn`. Then ministers act.
2. **Diplomacy:** `diplomacy::deliverMessages`.
3. **Movement and space combat:** `movement::runMovementAndCombat`. Movement runs in 30
   phases, and `combat::resolveSpaceCombat` runs in any sector where hostiles meet.
4. **Ground combat and capture**, then colonization.
5. **Economy:** production, trade, maintenance, then construction.
6. **Research.** 7. **Intelligence.**
8. **Events.** 9. **Upkeep:** supply and repair.
10. **Sight, contact and trade.** 11. **AI anger.**
12. **Population:** growth, then mood from every event of the turn, riots and plague.
    The spec places it earlier. It runs last so that mood events raised by the later
    phases are not lost.
13. **End of turn:** statistics, score, victory, then the date advances.

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
