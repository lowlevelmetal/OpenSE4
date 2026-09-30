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

### Turn-based games

With the turn style set to turn-based (`GameOptions::simultaneous` off; spec 01 §2.2,
spec 05 §8 "Turn-based game"), players take their turns one after another in empire
order, and `GameState::playerTurn` records whose turn it is (`turn_based.cpp`, API in
`turn.hpp`):

1. **Start of the player's turn**: the empire's destruction check (`score::checkDestruction`);
   its vehicles regain their movement (`movement::startTurn(ctx, empire)`) and first carry
   on with their order lists; then its start-of-turn step as in step 4 above
   (`ai::updateAiState`, `ai::politicalStep` counting the previous game turn,
   `ai::planOrders` for a computer player or active ministers, `ai::recordAiDecisions`).
2. **The player's orders execute as they are given** (`applyLive`). `movement::runLive`
   carries out the orders of the vehicles, fleets or planets a command set, action after
   action until each has spent its movement points, waits or fails. A group that steps
   into a sector where combat is possible fights there at once (mines strike first) and
   its order fails; a human is first asked whether to enter a sector with visible enemies,
   and answers with `cmd::EnterSector`. An order carried out in a sector (cargo, launches,
   an attack on its target) offers it to combat without failing. Colony ships that reach
   their planet with movement left found the colony at once. Messages take effect when
   sent (`diplomacy::deliverMessages`), and sight and contact follow every move.
3. **End of the player's turn** (`endPlayerTurn`): `empireEndOfTurn`, then the next living
   empire's turn starts. Computer players take their turns the same way, one after
   another (`resumeTurnBased`).
4. **After the last player** the date advances, then the design cleanup (a new year), the
   victory check and the event step run, the per-turn flags are cleared and the AI
   remembers the turn, as in steps 7 to 10. `GameState::combats` keeps the battles of the
   game turn in progress and of the one before, so each empire's political step counts
   every battle of the previous game turn exactly once.

In a turn-based game the end-of-turn processing sees the unadvanced date
(`economy::processingTurn`). `processTurn` on a turn-based game plays the rest of the
game turn: each player's `EmpireOrders` are carried out at its turn, and a human without
orders is played by the computer as in a simultaneous turn. The per-turn records a live
move needs across commands (steps made, emergency movement, units launched) are in
`GameState::playerTurn`, so a game saved in the middle of a turn goes on the same. So
are the Attack Sector questions still open. `LiveOptions::computerPlays` has the computer
play a human's turn, or the rest of it, as a stand-in; network and e-mail hosts use it
for players who are away. Network and play-by-e-mail games run the same calls on the
host (MULTIPLAYER.md).

### Tactical combat and the combat simulator

The space battle (`combat_battle.hpp`, `combat_space.cpp`) runs its turn sequence (spec 04
§4) as a state machine: `advance()` plays computer phases and stops at a player's phase;
orders for that phase are checked and carried out one at a time
(`combat_tactical.cpp`). Strategic resolution is the same sequence with every side on its
strategies, so the rules are identical and only control differs (spec 04 §3).

`combat::TacticalBattle` (`tactical.hpp`) is the public face. It fights a battle on its
own copy of the game, forking the random numbers and striking with mines exactly as
`resolveSpaceCombat` does:

1. **Begin**: `TacticalBattle(rules, state, {where, entering, players})`.
2. **Query**: `pieces()` (position, shields, damage, movement, weapons with reload
   counters, cargo, groups), `record()` (the replay record as it grows), `round()`,
   `phaseEmpire()`, `pathTo`, `hitChance`, `damageAt`, `fireProblem`.
3. **Orders** for the side whose phase it is: `check(order)` says why an order would be
   refused, `submit(order)` carries it out and plays the computer phases that follow.
   Orders: Move (to a square, or along a path), Fire (one weapon, or every enabled
   weapon), ToggleWeapon, Launch (in groups), DropTroops, Ram, Capture, SetLeader,
   SetMember, ClearGroup, ClearAllGroups, Auto, EndPhase, ResolveCombat, Begin.
4. **Finish**: `finish()` lets the strategies play what is left and applies the results
   to the battle's copy with the same code as a strategic battle.

The accepted orders are the battle's `script()`: the same start and script give the same
battle. Tests check that a player side on Auto, the strategies' orders given by hand, and
the replayed script all give the strategic battle, bit for bit.

In a turn-based game (`turn.hpp`, "Tactical combat in turn-based games") the calls that
play the game take the battles' answers (`BattleAnswer`: the tactical sides and their
script). A battle with a human side and no answer stops the call: the state is left as
it was, and `TurnResult::battle` returns the question with a copy of the game as the
battle begins. The client fights it in its window (or answers Strategic) and makes the
same call again with the answer; the call replays deterministically to the battle and
fights it with the script, so the game gets exactly the battle the player saw.

The combat simulator (`simulator.hpp`) builds a sandbox copy of the game: one virtual
empire per side (a copy of the player's, at war with the others), the chosen designs,
seen enemy designs and sample planets in an empty new system, cargo, fleets, strategies,
and the sides the computer controls. `startSimulation` returns the `TacticalBattle`;
the real game is never changed.

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
- In a turn-based game the host carries out each command of the player whose turn it is
  as it arrives and sends that player its new view. Everyone gets their view when the
  turn passes on.
- `src/net` carries this over TCP with UPnP port mapping. `opense4-server` hosts
  headless or processes PBEM turn files.

See [MULTIPLAYER.md](MULTIPLAYER.md).

## The client

`src/client/classic` presents the engine in the classic layout: a 1024×768 frame
scaled to the window, drawn with the art from the player's install.

| Part | Role |
|---|---|
| `session.*` | Rules, state and local player. Its `issue()` records commands (in a turn-based game it carries them out at once through `game::applyLive`, or sends them to the host of a network game, and keeps the battle to show), and it runs the End Turn flow for local, hotseat and network games. In local and hotseat turn-based games it holds the battle that waits for Tactical or Strategic, and the tactical battle being fought, and makes the engine call again with the answers |
| `art.*` | Pictures from the install, cached as textures |
| `ui.*` | The frame mapping, `UiContext`, the modal window stack, and the classic dialog layout |
| `main_window.*` | Status bar, command buttons, order strip, system, report and galaxy panels, and hotkeys |
| `reports.*` | Ship, planet, fleet and system reports |
| `screens/*` | One file per group of windows (designs, planets, queues, research, empires, log, ...). `combat_map.*` draws the combat map for the Combat Replay and Tactical Combat windows; `tactical.cpp` holds Tactical Combat with its Orders and Options windows; `simulator.cpp` the Combat Simulator |
| `frontend.*` | Intro, quick start, game setup, load, and the multiplayer lobby |

## Tests

The engine tests use `tests/engine_fixture.cpp`, a complete invented rules set. Every
subsystem has its own test file. `test_integration.cpp` runs whole games.

Tests against your installed data are opt-in:

```sh
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests
```
