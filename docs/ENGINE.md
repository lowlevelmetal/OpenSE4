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
| `state.hpp` | The whole game: empires (race, research, intel, relations, knowledge, lists, Empire Options), colonies, designs, vehicles, fleets, messages, pending events, combat records (with each piece's damage at the end, for the Log), options |
| `rules.hpp` | `Rules`: the loaded data set plus caches (parsed abilities, tech gates, settings with defaults, race presets) |
| `abilities.hpp` | The closed list of ability identifiers used by the data, parsed once |
| `design.hpp` | Mounts, design validation, unique design names and statistics, movement points, supply, cargo |
| `setup.hpp` | `createGame`: the quadrant, the empires and their races, homeworlds, starting technology, planets and pools (spec 01 §3, spec 02 §9). No empire gets a ship or a design of its own (spec 01 §3.6): only an empire file's designs, and what `StartExtras` asks for: the Quick Start player's one Design minister run (`ai::designMinisterRun`) and a tutorial's ships (`starting_ships`, an OpenSE4 lesson extension). `StartExtras` is never saved or sent |
| `orders.hpp` | Orders as they are given: Explore, Resupply, Repair and the composite orders expanded into simple ones (spec 03 §8) |
| `map_file.hpp` | Map files in our own text format ([MAPS.md](MAPS.md)): Save Map, and loaded maps with starting points |
| `query.hpp` | Read-only questions: what is where, space yards, capacities, hostility |
| `scrap.hpp` | The Scrap window's actions (spec 03 §15): each action's test and effect, Analyze's requirement pairs and research potential word, Fire On's armed test, the retrofit checks |

`GameState` is plain data. Every field is serialized (`serialize.hpp`), and
`stateChecksum` hashes the serialized bytes for desync detection.

- **One object list.** Stars, planets, asteroid fields, storms, warp points, ships, bases
  and unit groups each hold a slot of one object list (`SpaceObject::slot`,
  `Vehicle::slot`). A removed object leaves its slot free, and a new object of any kind
  takes the lowest free slot (`GameState::freeSlot`, `addObject`, `addVehicle`); a system
  lists its objects in slot order. `objectOrder` returns every object by slot, and
  whatever the rules do "in object order" follows it: the acting order of the movement
  phase, colonized planets included, where built units go, training sources and so on
  (spec 03 §6.3 step 5, §19 Q62). Stellar manipulation never changes an object in place: the
  replacement is a new object, added while the old one holds its slot, then the old one is
  removed (spec 03 §19 Q72).
- **Fleets** have no order list of their own (spec 03 §8, §9). Their orders are copies in
  the lists of the members at the fleet's location, mothballed ones included (`fleetOrders`,
  `fleetGroup`): orders given to the fleet or to any member are appended to each of those
  lists, never to an away member's own, and joining or leaving clears a vehicle's list. The
  location is the fleet's own record (`Fleet::location`): it follows whichever member moved
  last (`fleetMemberMoved`), and a fleet left with no member there is disbanded
  (`GameState::tidyFleets`, `leaveFleet`, `disbandFleet`).
- **Colonies** have an order list with Repeat (`Colony::orders`, `repeatOrders`): Launch and
  Recover Units, Use Facility and Convert Resources (spec 02 §5.6, spec 03 §8, §12). A colony
  can cloak (spec 01 §6.9): its cloak and sensor levels come from its facilities, stored and
  recalculated only at the moments the spec names (`sight::recalculateColony`), and
  `Colony::cloaked` survives every change of owner.

## Changing state

- **Players change state only through commands** (`commands.hpp`). There is one command
  per player action: orders, fleets, queues, designs, research, intelligence,
  messages, waypoints and so on. A few act at once in both turn styles, as the original's
  windows do: cargo transfer, Jettison Cargo (`cmd::JettisonCargo`) and a colony's Cloak and
  Decloak (`cmd::CloakColony`).
  - The Scrap window's seven actions on a vehicle (`cmd::Scrap`, `cmd::Analyze`,
    `cmd::Mothball`, `cmd::Retrofit`, `cmd::SelfDestruct`, `cmd::FireOn`; `scrap.hpp`) are
    carried out at once in a turn-based game and become the vehicle's only order in a
    simultaneous one, which movement carries out at its first action, testing again.
  - `game::apply` validates a command against the state and applies it, or rejects it
    with a reason.
  - A few changes are not player commands but the machine's or the host's: the Player
    Computer Control window (`ai::setComputerControl`: the computer-controlled mark with every
    minister and flag) and the network host's toggle (`ai::setComputerMark`: the mark alone).
  - One empire's commands for one turn form an `EmpireOrders`, the equivalent of the
    classic `.plr` file.
- **Subsystems change state only during turn processing**, through a `TurnContext`. The
  context also carries transient per-turn data: mood events, battle sites and
  rejected commands.

## Turn order

`processTurn` (`turn.cpp`) follows the original's order of a simultaneous turn, spec 05 §8
(confirmed: binary). Empires are always taken in empire-number order, and destroyed
empires are skipped.

1. **Orders.** Each human's `EmpireOrders` are applied, in player order, and the colonies
   they name are recalculated (`coloniesNamed`, `diplomacy::recalculateColony`); then sight
   is recalculated everywhere. A human who sent nothing is played by the computer for this
   turn: all of its ministers are switched on and restored at the end of the turn. A player
   who ticked "AI should not make changes" only gets the bookkeeping.
2. **Messages:** `diplomacy::deliverMessages` processes the players' messages.
3. **Date.** `GameState::turn` stays the number the orders were given for until the end
   of the turn, so log entries and records carry it. The steps that depend on the date
   get `turn + 1`.
4. **Start of turn**, for each empire: `ai::updateAiState` (on dates that are multiples
   of 10 it first empties the Politics minister's demand lists), `ai::politicalStep` (it
   counts the turn processed before, whose battles `GameState::combats` still holds, and
   the messages delivered since the empire's previous step, `ai::simultaneousWindow`),
   then the Politics minister's claims are rewritten (`ai::claimTerritory`), and the
   ministers that act while orders are given (every minister for a computer player, the
   active ones for a human): the Politics minister alone first (`ai::planPoliticsOrders`),
   whose messages take effect at once and carry the advanced date
   (`DiplomaticMessage::dated`, which the answer window reads), then the others
   (`ai::planOrdersAfterPolitics`), which see the treaties it changed but plan with the
   claims the state update used, as the original's lists do (spec 05 §7.2). Their
   colonization targets stay in `TurnContext::aiColonyTargets` for the next economy step
   whose ministers run (the first empire's in a simultaneous turn reads the last empire's).
   Afterwards `ai::recordAiDecisions` notes what was decided.
5. **Movement and space combat** (`movement::runMovementAndCombat`). Over 30 days each
   vehicle, fleet and planet with orders acts, in object order, whenever its day counter
   reaches 1: the acting vehicle gets exactly 1 movement point and its list runs, orders
   that complete chaining into the next; a fleet acts through its first member with orders,
   and the members at its location carry that order out together. After each day every sector where an object carried out an order (any order,
   a waiting Sentry included) runs a battle check, and `combat::resolveSpaceCombat` fights
   where an empire with an uncloaked vehicle there sees a hostile object; a sector gets a
   second battle in a turn only when newcomers arrive or a survivor was damaged (spec 03
   §6.3, spec 04 §2). A Colonize order founds its colony like any order, on an
   acting day with movement left, so a colony can appear in any phase.
   `TurnOptions::movementDay` lets a caller watch the state after each day without
   changing it, and `TurnOptions::movementStep` each vehicle's every step as it is made
   (the client's movement log replay plays a turn again from its start with them,
   docs/spec/06 §7 Q51, Q62). Each step within a system turns the vehicle to its bearing
   (`Vehicle::heading`, `movement::headingFor`, saved with the game); a warp keeps it. Sight is then updated.
   **First contact** is checked in one system at a time, only at the moments spec 05
   §3.1 names (`diplomacy::firstContactIn`): a group's arrival through a warp point, any
   decloak (orders, ministers, battles, supply), a logged event, a package's planet or
   vehicle and a rebellion project; in every system when the game is created and after a
   surrender (`firstContactEverywhere`). Two empires meet when each detects the other in
   that system now and a warp path links their colonies; moves within a system never
   make contact. A colony's Decloak in a simultaneous game makes only the acting empire's
   side (`onlySide`, spec 01 §14 Q44); the next check in a system where both detect each
   other completes the pair.
6. **End-of-turn processing**, one empire at a time (`empireEndOfTurn`), each followed by
   that empire's destruction check (`score::checkDestruction`):
   1. the ministers' end-of-turn actions (`ai::planEconomyStep`: Design, Research,
      Intelligence and construction);
   2. the statistics row (`score::recordStatistics`), and for a human player the lines of
      its statistics, history and log text files in the original's layouts, handed out
      in `TurnResult::records` (the classic client writes them under `history/<game>/` in
      its user data folder: statistics and history appended, the log copy rewritten);
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
   16. each ship, base, fighter group and drone group records its current sector as the
      one it comes from (`Vehicle::cameFrom`);
   17. ground combat (`combat::runGroundCombat`): the fights on the empire's invaded
       colonies, or the hand-over of a friendly empire's troops; in a turn-based game on
       one machine a fight with a human side stops the call to be shown (see "Battles
       shown as they happen" below);
   18. the log keeps only this turn's entries; the long record of the History window is
      `Empire::historyEvents`, which is never pruned (`addHistory`).

7. **Design cleanup** when a new year starts (`movement::purgeObsoleteDesigns`), then,
   every turn, the **contact check** (`diplomacy::checkContacts`): an empire from whose
   colonies no warp path leads to a colony of an empire it has met returns to "no
   contact" with it, drops its intelligence projects against it and logs "Contact Lost".
8. **Victory check** (`score::checkVictory`).
9. **Event step:** hazards (`movement::runStellarHazards`), the timed events that are due
   (`events::fireDueEvents`), then one roll for a new event for the whole galaxy
   (`events::rollNewEvent`).
10. **End.** Per-turn flags are cleared, sight follows the events, the AI
    remembers the turn's battles and spies (`ai::rememberAiEvents`), the turn number
    advances and `economy::updateReports` projects next turn's income.

Mood events raised after an empire's happiness update (construction, ground combat, the
other empires' processing, events) wait in `GameState::pendingMood` for that empire's next
update. As in the original, no game file keeps them (spec 02 §4): they are not saved, sent
or hashed, so a loaded game starts with none, and a local simultaneous game drops them where
it stands for the original's reading of its game file before processing. The game's stockpile and research pool start at Starting Resources plus one turn
of production (colony output with the minimum-generation rule, nothing else), and the
intelligence pool at 0 (`research::openingPools`, called by `createGame`).

### Turn-based games

With the turn style set to turn-based (`GameOptions::simultaneous` off; spec 01 §2.2,
spec 05 §8 "Turn-based game"), players take their turns one after another in empire
order, and `GameState::playerTurn` records whose turn it is (`turn_based.cpp`, API in
`turn.hpp`):

1. **Start of the player's turn**: a human's destruction check (`score::checkDestruction`);
   the start-of-turn step as in step 4 above (`ai::updateAiState`, `ai::politicalStep`
   counting everything since the empire's previous step, `Empire::politicsMark`;
   `ai::claimTerritory`; the Politics minister, then the other ministers, whose orders are
   given but not yet carried out; `ai::recordAiDecisions`); then its vehicles regain their movement
   (`movement::startTurn(ctx, empire)`) and every group carries out its order list, the
   ministers' new orders included, at most 21 orders each; a computer player's
   destruction check comes last.
2. **The player's orders execute as they are given** (`applyLive`). `movement::runLive`
   carries out the orders of the vehicles, fleets or planets a command set, action after
   action until each has spent its movement points, waits or fails. Only three things run
   a battle check (spec 04 §2, `combat::BattleCheck`): a movement step (a warp jump
   included), after the mines there have struck, which fights at once, fails the order
   and clears the group's whole list (a pursuit's step only stops it for this run and
   keeps its orders, as mines, storms and turbulence do, spec 04 §19.2 Q76); the Attack
   order, in the sector its target was in when it was given (or where the group stands),
   which decloaks nobody but the Ship Cloaking minister's vehicles and is then used up;
   and a drone group's pursuit (a Seek) at its target, which attacks every time the list
   runs and stays (a pursuit without a drone pursuing there waits and spends nothing).
   The check is one-directional: the group's owner must see a
   hostile object there, or, for a wholly cloaked group, another empire must see it. A
   human is first asked whether to enter a sector with visible enemies, and answers with
   `cmd::EnterSector`. Colony ships that reach their planet with movement left found the
   colony at once. Messages take effect when
   sent (`diplomacy::deliverMessages`), and sight follows every move (first contact only at the moments of step 5).
3. **End of the player's turn** (`endPlayerTurn`): `empireEndOfTurn`, then the next living
   empire's turn starts. Computer players take their turns the same way, one after
   another (`resumeTurnBased`).
4. **After the last player** the date advances, then the design cleanup (a new year), the
   contact check, the victory check and the event step run, the per-turn flags are cleared and the AI
   remembers the turn, as in steps 7 to 10. `GameState::combats` keeps the battles of the
   game turn in progress and of the one before, so each empire's political step counts
   every battle since its previous step exactly once.

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
3. **Orders** for the side whose phase it is (its drones and seekers have moved before
   it gets control): `check(order)` says why an order would be refused, `submit(order)`
   carries it out and plays the computer phases that follow. Orders: Move (to a square,
   or along a path), Fire (one weapon, or every enabled weapon), ToggleWeapon, Launch (a
   "Launch Units" window session), LaunchFighters (in groups of 5 to 50), DropTroops,
   Ram, Capture, SetLeader (with a formation), SetMember, ClearGroup, ClearAllGroups,
   Auto (one piece now, or the battle's toggle for every empire), AutoPhase, EndPhase,
   ResolveCombat (every empire to its strategies).
4. **Finish**: `finish()` lets the strategies play what is left and applies the results
   to the battle's copy with the same code as a strategic battle.

The accepted orders are the battle's `script()`: the same start and script give the same
battle. Tests check that a player side whose phases the strategies play (AutoPhase), the
strategies' orders given by hand, and the replayed script all give the same battle, bit
for bit, and the strategic battle unless a player side has a fleet: a player's side is
not automated, so a hit on its group's leader never dissolves the group as it does for
an automated side (spec 03 §19 Q60).

`Setup::stepped` sets up a battle without player sides and stops before combat turn 1;
`step()` then plays one empire phase at a time (with the end-of-turn upkeep and the end
check that follow it). The Strategic Combat window fights a battle this way while it
shows it; a battle stepped or fought at once comes out the same.

### Battles shown as they happen

On one machine the original stops whatever started a battle once the battle is set up,
before combat turn 1, shows it, and goes on only when its window is closed (spec 06
§1.10.5, §1.10.6). The engine does the same with answers given in advance
(`turn.hpp`, "Battles shown as they happen"): the calls that play the game
(`resumeTurnBased`, `applyLive`, `endPlayerTurn`, and `processTurn` with
`TurnOptions::battles`) take one `BattleAnswer` per stop, in the order the stops come
up. A stop whose answer is missing stops the call: the state is left as it was, and
`TurnResult::battle` returns the `BattleQuestion` with a copy of the game:

- **Choose** (turn-based, a battle with a human side): Tactical or Strategic; the answer
  names the tactical sides and their script;
- **Show** (turn-based with "No Tactical Combat", a battle with a human side; or a
  simultaneous game whose Settings show battles, every battle, computer-only ones
  included): the Strategic Combat window with Begin and Close; the strategies fight it;
- **Ground** (turn-based, the colony owner's end-of-turn ground combat with a human
  side): the engine fights it and hands over its record (`BattleQuestion::ground`).

The client shows it (it fights the battle in its window, by hand or by the strategies,
or plays the ground record round by round) and makes the same call again with the answer
added; the call replays deterministically to the stop and goes on, so the game gets
exactly the battle the player saw, and the reports, log entries and everything after the
battle come only after the window has closed. Nothing a window shows can change the game,
so the results are the same whether a battle is shown or not. Network and PBEM hosts and
automated runs pass no answers and never stop.

A battle's record (`CombatRecord`) holds its pieces, the events the replays play back
(moves, shots, hits, losses of units, launches, captures) and the ground combats fought
when troops landed (`GroundCombat`, for the Ground Combat window: both sides at the start,
the counts of every stack and the militia after every round, and the landing's place
among the events).

Units in space are held in one group per (owner, unit kind, sector) that mixes designs
(spec 03 §12): a `Vehicle` whose `mixed` list names each design and its count (empty when
it holds one design, which is then `design` × `count`). The group helpers of
`design.hpp` (`groupStacks`, `addGroupUnits`, `removeGroupUnits`, `setGroupStacks`)
keep `design` (the first stack's) and `count` (the total) in step, and
`vehicleAbilityTotal` sums an ability over every unit. In a battle the group is one piece
whose weapons refer to its design stacks.

The combat simulator (`simulator.hpp`) builds a sandbox copy of the game: one virtual
empire per side (a copy of the empire that owns the side's first item, its strategy list
included, at war with the others), the chosen designs, seen enemy designs and sample
planets in an empty new system, cargo, fleets with their strategies, and the sides the
computer controls. `startSimulation` returns the `TacticalBattle`;
the real game is never changed.

## Determinism

- **Turn resolution uses integer math only.** No floats, and no wall-clock time. The
  rules the original computes in floating point use `xmath::Ext`, an integer emulation of
  the x87 extended format (`xmath.hpp`). Galaxy generation is integer-only too.
- **Iteration order is fixed.** Containers are sorted by id, every sort whose result
  matters has a total order (ties broken by id or data order; `std::sort` leaves ties in
  an order that differs between standard libraries), and nothing depends on hash-map or
  directory order.
- **All randomness comes from `GameState::rng`**, or from streams forked from it. The
  AI derives its own stream from the seed, the turn and the empire. Displays that need a
  random choice use their own generator (`movement::planRoute`). One draw per statement:
  two calls that draw or change state are never the arguments of one call or the
  operands of one operator, whose order compilers choose differently.
- **One seed plus one set of orders gives the same next state on every platform.**
  Network games still send the host's state rather than the seed. The integration test
  plays full all-AI games twice and compares checksums every turn, and the golden test
  below pins the results across compilers.

## Same on every platform

Players on Linux and Windows play one game together, so both builds must compute the
same game and draw the same picture. The rules below keep it that way. Everything here
was checked on 2026-10-01: the Linux build (GCC), the Windows build (MinGW-w64) under
Wine, and a Clang 21 + libc++ build of the tests. CI builds and tests every push with
GCC, Clang, Apple Clang (macOS on ARM, libc++), MSVC (Visual Studio 2022 and 2026) and
MinGW-w64, and on ARM Linux with GCC: 64-bit (aarch64) natively and 32-bit (armhf) under
QEMU. The goldens matched on both from the start (2026-10-03: GCC 16 on aarch64 and
GCC 15 and Clang 22 on armhf, under QEMU, then CI). What differed on armhf was the data
set's identity, which network games compare: it hashed sizes at their own width.

- **Golden checksums.** `tests/test_determinism.cpp` plays a simultaneous and a
  turn-based game of four computer players for 100 turns (battles, frequent events,
  intelligence, diplomacy) and fights ten varied battles with a ground combat. It
  compares the state checksums with constants, and on a mismatch names the first turn
  and the part of the state that differs. CI runs it with every compiler above.
  After a deliberate rules change, print the new values with
  `OPENSE4_PRINT_GOLDEN=1 opense4_tests -tc="determinism*" -s`. With the player's own
  data, the same PBEM game played for 15 turns by the Linux and the Windows
  `opense4-server` gave identical states. Over encrypted connections, a Linux
  `opense4-server` hosted the Windows server's scripted client under Wine for four
  simultaneous turns (with a join password), and a Linux and a Windows client for three
  turn-based ones (2026-10-02): the host's desync check found every Windows copy of the
  game identical to the view it sent. By e-mail, the Windows build opened a turn file
  the Linux host had encrypted to its empire's password and sent orders the Linux host
  read and accepted: both make the same Argon2id keys. With signed turn files
  (2026-10-02, again under Wine, with a network game of each turn style first), the
  Windows build trusted the Linux host's PBEM key on the game's first turn file,
  recognized it on the next, refused a turn file signed by another key, and both
  turns' orders were accepted.
- **Engine code.** It follows the rules of the section above. Serialized and hashed
  values are fixed-width (`FixedWidthScalar` in `core/hash.hpp`). `size_t` is 32 bits
  on armhf and 64 elsewhere, so a size is hashed as 64 bits (`Hasher::addSize`) and
  stored as a u32 count; the macOS build rejects a `size_t` passed as it is, and `long`
  and `wchar_t` too. A random index into a container comes from `Rng::index`, the same
  draw as `below` as a `size_t`. Little-endian targets are asserted. GCC and Clang
  build with `-ffp-contract=off` (`CMakeLists.txt`), so that no compiler fuses a
  multiply and an add into one rounding where another rounds twice; ARM has fused
  multiply-add, x86_64's baseline does not. `xmath::Ext` is plain
  integer code with no path of its own for any compiler. `test_xmath.cpp` checks it
  against exact arithmetic in its own multi-word integers with every compiler, and
  against the x87 itself on x86 with GCC or Clang; golden checksums of its results
  tie the compilers to those x87 runs.
  Character classes are ASCII-only, never the C library's locale-dependent `isalpha` and
  the like.
- **Files.** Data files parse the same everywhere: CR is dropped and Windows-1252 is
  converted to UTF-8. Install files are found in any case, as on Windows
  (`ruleset::childIgnoringCase`, the `InstallFiles` index). A file the game wants but
  cannot find is logged once (`InstallFiles::noteMissing`); the stock install has none.
  Our own files have the same bytes on every platform:
  - saves, PBEM files and network messages use a little-endian archive;
  - the settings are TOML written in binary, with floats formatted alike by every
    compiler (`TOML_FLOAT_CHARCONV=0`);
  - the history files end their lines in CR LF.

  Paths are UTF-8 everywhere. The Windows programs carry a manifest with the UTF-8 code
  page (`packaging/windows/opense4.manifest`), and stb and dr_mp3 open UTF-8 or wide
  names. This was checked with an install under a folder with a non-ASCII name.
- **Picture.** Vulkan and OpenGL share the shader, blending, samplers and UNORM
  framebuffer; Vulkan never picks an sRGB-encoding swapchain when another format is
  offered. `tools/render_scenes.sh` renders ten scenes: the main window at both layouts,
  Planets, Research, Create Design, Tactical and Strategic Combat, the manual, a lesson
  step and a small window. They are bit-identical between the Linux build and the
  Windows build under Wine, with either renderer. Vulkan and OpenGL differ only by GPU
  rounding, at most 1/255 in antialiased or filtered pixels, plus about a dozen glyph
  edges of the manual's text (up to 21/255). Different GPUs round differently anyway.
  Screenshot runs use a fixed frame time and no pointer, so their pictures repeat
  exactly.
- **Display scaling.** The classic frame scales itself to the window, and the desktop's
  scale never scales its style or text again (`ClassicMode::restyle`). Without that, a
  Windows desktop at 125 % or 150 % made all classic text that much larger than on Linux.
  The automatic 800×600 / 1024×768 choice reads the desktop width in logical units, as
  the original sees it on a scaled Windows desktop. Lines drawn in frame units scale
  with the frame. A few ImGui lines in windows (map grids, rules) stay one framebuffer
  pixel at any scale, the same on every platform.
- **Input and timing.** AltGr counts as Alt everywhere (`altGrAsAlt`; Linux reported it
  as a plain key). A minimized window keeps a network game going (`Mode::background`;
  Windows reports minimizing, and the host dropped such a client after a minute).
  Nothing that changes the game depends on frame time or the wall clock; the tactical
  map scrolls by time, not per frame.
- **Not the same.** These differences remain:
  - the default window size (1600×900) is in pixels on Windows and in points on
    Wayland and macOS; the frame fills the window either way;
  - GPU rounding, as above;
  - MSVC and macOS builds are checked in CI only (Clang was also checked locally,
    with libc++).

## Multiplayer

- The host is authoritative. Clients apply their own commands locally so their windows
  update at once, and send them at End Turn.
- The host processes the turn when every human's orders have arrived, or on timeout,
  with the AI playing for anyone missing. It then sends everyone their view of the new
  state (`redactForEmpire(rules, state, empire)`), which leaves out what the player may
  not know, a colony hidden by its cloak included.
- In a turn-based game the host carries out each command of the player whose turn it is
  as it arrives and sends that player its new view. Everyone gets their view when the
  turn passes on.
- A play-by-e-mail host keeps the whole game and sends each player the same view as a
  turn file, signed with the host's key (which the player's game trusts per game from
  the first turn file on); the players' orders files come back signed with their
  passwords.
- `src/net` carries this over TCP, encrypted (Monocypher: X25519, XChaCha20-Poly1305,
  BLAKE2b, EdDSA), with UPnP port mapping. A player's copy that drifts from the host's
  view is detected (`game::statePartHashes` names the parts) and replaced.
  `opense4-server` hosts headless or processes PBEM turns.

See [MULTIPLAYER.md](MULTIPLAYER.md).

## Rendering

`src/gfx` is a deliberately small RHI: textures plus batches of transient 2D geometry,
drawn with one premultiplied-alpha uber shader (textured, SDF disc and ring, additive
glow, antialiased line).

- *Vulkan 1.3*: dynamic rendering, synchronization2, VMA, volk, two frames in flight.
  The loader is `dlopen`ed through SDL, so a missing Vulkan runtime is not fatal.
- *OpenGL 3.3 core*: the fallback, with the same shader source (`#ifdef VULKAN` for
  bindings) and the same blending and framebuffer format, so both backends look the
  same.
- The Dear ImGui UI is drawn through the same RHI (`ImGuiRenderer`, using ImGui 1.92's
  dynamic texture protocol), so the UI needs no per-backend code.

A 2D strategy game needs very little from the GPU: textures and streamed triangles. A
small RHI of our own covers that completely and keeps full control, where bgfx,
Diligent or SDL_GPU would add a large dependency. The interface (`gfx/device.hpp`) can
grow, for example with more pipelines or offscreen targets, without touching game code.

## The client

`src/client/app.cpp` is the shell. It finds the player's install first, and without one
explains where it looked and exits with an error. It then opens the window, the render
device (Vulkan, else OpenGL) and Dear ImGui, and runs the frame loop and screenshots.

`src/client/classic` presents the engine in the classic layouts: a 1024×768 or
800×600 frame scaled to the window, drawn with the art, raster fonts and mouse
pointers from the player's install (docs/spec/06 §2.1.1, §5.4, §5.8). The layout
follows the desktop width as in the original (800 px or less: 800×600); the Graphics
setting or `--layout` can force one. `src/assets` reads the install's files: pictures
(`assets.*`, with case-insensitive lookup and, for fonts and pointers, the mod folder
named by `Path.txt` first), Windows raster fonts (`winfont.*`) and cursors
(`wincursor.*`); `tiny_font.*` is OpenSE4's own small raster face for the map numbers,
which the original draws in the system's Small Fonts.

| Part | Role |
|---|---|
| `session.*` | Rules, state and local player. Its `issue()` records commands (in a turn-based game it carries them out at once through `game::applyLive`, or sends them to the host of a network game), and it runs the End Turn flow for local, hotseat and network games. In local and hotseat games it holds the battle (or ground fight) that stops the engine call to be shown, and the battle being fought in a window, and makes the engine call again with the answers; it lists the battles of network and PBEM games, shown afterwards |
| `art.*` | Pictures from the install, cached as textures: minis turned to their heading, the combat maps' tiled background, the layout's system backgrounds and intro picture, and each empire's colour from its race's swatch |
| `layout.*` | The two screen layouts: regions, frame strips, sector grid, order strip pages, status bar and title strip places, and how the layout is chosen (headless) |
| `pointers.*`, `pointer_rules.*` | The install's twelve `.cur` pointers as SDL cursors, the Hourglass while the program is busy (`BusyPointer`), and which pointer the tactical map shows (headless rules) |
| `ui.*` | The frame mapping, `UiContext`, the modal window stack, the classic dialog layout, and the keys of dialogs and prompts (spec 06 §3.4: `yesNoKey`, `okKey`, `YesNoPrompt`). `UiContext::options()` and `setOptions()` read and change the empire's Empire Options and window memories (`game::InterfaceOptions`, saved with the game, changed with `cmd::SetInterfaceOptions`) |
| `settings.*` | This computer's preferences: the Options window (Game Menu → Options: animation, sound, music steps, Fast Tactical Combat, movement lines), the Combat Options display switches, OpenSE4's effects volume and the last saved game (Resume Game), in `classic_settings.toml` |
| `facility_markers.*` | The facility letter markers the Empire Options can show on colonies in the system window |
| `main_window.*` | Status bar, command buttons, order strip with the hover hint, system, report and galaxy panels, tagging, the movement log replay's controls, and hotkeys |
| `order_rules.*`, `status_icons.*`, `map_style.*` | Headless rules the main window draws from (tested without a window): when each order button is lit, which status icons an object shows, and the colours and symbols of the maps, with each system's presence for the viewer (`map_style::presence`) |
| `quadrant_map.*` | The quadrant map inside windows (Galaxy Map, Systems To Avoid, Waypoints) |
| `ship_glides.*` | Ships turning and sliding to their new square, frame by frame, with the Settings.txt pause after each step (spec 06 §2.4) |
| `movement_line.*` | The movement line of the system panel (spec 06 §2.4): which object has one, and the rings, lines and turn numbers drawn for its route, worked out by `game::movement::planRoute` with the engine's own step rule and a display-only random source (headless) |
| `movement_replay.*` | The movement log of a simultaneous turn (recorded by playing the turn again from its start with the engine's movement-step and movement-day observers, one entry per vehicle and step, or rebuilt from the client's view) and its replay (Ctrl+P/I/O/U), each entry animated on its own |
| `sector_view.*` | What a sector of the system panel shows: which stellar objects the viewer sees (`shownStellarObjects`), the stellar object, one vehicle or the owners' flags, and the counts (headless) |
| `finale.*`, `data_export.*` | The ending window's choice of kind and pictures (Victory, Human Dead, Lose; once per occurrence), and the Weapons Report's export tables (headless) |
| `reports.*` | Ship, planet, fleet and system reports |
| `screens/*` | One file per group of windows (designs, planets, queues, research, empires, log, ...); `list_widgets.*` the lists' sortable headings and the arrow column every list scrolls with (spec 06 §1.11); `finale_screen.cpp` the ending window, `help.cpp` also the Weapons Report's Export button. `cargo_transfer.cpp` also holds Jettison Cargo, `convert_resources.cpp` Convert Resources; the Select Component and Select Facility pickers are the main window's. `combat_map.*` draws the combat map for the Combat Replay, Tactical Combat and Strategic Combat windows; `combat_logic.*` holds their headless logic (forces list, piece report lines and abilities, the Drop Troops order, simulator rows and the sandbox its transfer windows work on); `tactical.cpp` holds Tactical Combat with its Orders, Launch Units, Combat Options and Combat Piece Report windows; `combat_replay.cpp` Combat Replay and its options; `strategic_combat.cpp` Strategic Combat (also the Tactical/Strategic question; it fights a live battle for up to 10 ms a frame, as fast as frames allow) and Ground Combat (round by round); `simulator.cpp` the Combat Simulator; `settings_screen.cpp` the per-computer Options window and OpenSE4's Settings; `scrap.cpp` also the Abandon Planet questions |
| `frontend.*` | Intro, credits, quick start, game setup, load, and the multiplayer lobby |
| `screen_id.*` | The `ScreenId` of every window and the window ids lessons and manual links use |
| `learn_content.*`, `lesson_runner.*` | The learning content (built in, or from disk), its progress in the client settings, and the lesson being played: its panel, outlines and result |
| `screens/learn_screens.*`, `screens/markdown_view.*` | The Learn window and the manual viewer, in the front end and in a game, and the Markdown they draw |
| `classic_probe.cpp` | What input scripts see of the client (`script::Probe`): the UI tags and widgets of the frame drawn last, the main window's sectors and systems, the windows, the lesson and the game's counters |

`src/client/script` plays input scripts (docs/BUILDING.md "Input scripts"): `script.*`
is the format and its parser, `player.*` turns each step into the frame's input events and
checks over a `Probe` (headless, so the tests drive it with one of their own),
`sdl_input.*` makes those events into the SDL events a mouse and keyboard send, `items.*`
collects the widgets of each frame by label (Dear ImGui's item hooks, `imgui_item_hook.*`
in `src/third_party_config`, turned on only for a script or a recording) and
`recorder.*` writes a script from a session. The app handles a script's events exactly as
a player's: `Mode::filterEvent` (the tutorial input lock) first, then Dear ImGui.

### Learning to play

Tutorials, training games and the manual ([LEARNING.md](LEARNING.md)) are our own
content, built into the executable from `assets/learn`. `src/learn` is a headless
library for them:

- `markdown.*`: the manual's Markdown subset as blocks (headings with anchors, inline
  spans, lists, tables, tip boxes) and its links;
- `lesson.*`: the TOML files of tutorials and training games, with errors that name the
  file and line;
- `condition.*`: the conditions (`all`, `any`, `not` and a fixed set of keys), evaluated
  over the game state, the player's empire and client facts (open windows, the kind of
  selection, the commands given);
- `progress.*`: where the player is in a lesson and the rules that move it on;
- `library.*`: the content from its sources (built in, a folder, or both), search, and
  the check of every link, window id and UI tag;
- `ids.*`: the vocabularies lessons use (window ids, Help tabs, order kinds, command
  names, UI tags).

The client tags windows and widgets with their rectangles each frame
(`UiContext::tags`), so a lesson step can outline them. Lessons only read the game: the
player's commands still go through `ClassicSession::issue()`, which reports each one to
the lesson.

## Tests

The engine tests use `tests/engine_fixture.cpp`, a complete invented rules set. Every
subsystem has its own test file. `test_integration.cpp` runs whole games.

`test_learn.cpp` covers the learning system's parser, loaders, conditions and progress,
and checks every built-in lesson and manual page.

Tests against your installed data are opt-in:

```sh
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests
```
