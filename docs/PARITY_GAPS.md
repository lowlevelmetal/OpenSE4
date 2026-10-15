# Parity gaps: where the engine differs from the original

On 2026-09-29 every rule in specs 01–05 was checked against the original executable (see
[CLEANROOM.md](CLEANROOM.md)). The specs were corrected, and each checked rule is marked
"(confirmed: binary)". This list compares the classic engine (`src/game`) with those
corrected specs.

**How to fix an item:** read the spec section it names and implement from that text. Do
not work from any listing (CLEANROOM.md, "Code follows the spec"). File and line numbers
are from the check and will drift. Impact: **H** changes most games, **M** changes some
outcomes, **L** is an edge case.

## Cross-cutting

| Item | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Turn-based games (`turn_based.cpp`, `net/host.cpp`, `net/pbem.cpp`) | Played locally, hotseat, over the network and by e-mail. On different machines a host is in charge: over the network (an OpenSE4 extension) it carries out the commands of the player whose turn it is; by e-mail each player plays their turn on their own view (a turn file) and sends its commands (`.plr`), and the host carries them out on the whole game and sends the next player their turn file. A player who is away, out of time or without a `.plr` is played by the computer for that turn (spec 05 open question 33). The game client opens a PBEM turn file (Multiplayer, Play by E-mail, or `--pbem`), plays the player's turn in either style and writes the `.plr` at End Turn (spec 05 open question 36). A computer player's (or a minister's) orders of one planning pass are carried out together after the pass, not one at a time as issued. As in the original, nobody is asked Tactical or Strategic in a game played on different machines: the host resolves the battle (local and hotseat games stop at each battle they show). The host never stops, so a client shows those battles afterwards, where the original shows the player whose turn it is the Strategic Combat window before the battle (spec 06 §7 Q74) | Spec 05 §9.1: on different machines the save file passes from player to player, and TCP/IP is for simultaneous games only; spec 04 §2, §3 step 1; spec 06 §2.7 | M |
| Random generator after a load | A saved game keeps the generator's state, so a loaded game continues the same random sequence | Every load reseeds the generator from the game's stored seed, which never changes in play (spec 08 §3.4, §11.2, confirmed: binary) | L |
| Timed events (`events.cpp`) | Any number of scheduled events wait in `GameState::pendingEvents` | The list keeps fired events as free slots, and nothing is scheduled once it has five slots, free or not, so after five events were pending together no timed event is scheduled again (spec 08 §3.4, §11.2, confirmed: binary) | L |
| Minister switches of a new empire (`Empire::ministers`) | A new human empire starts with the 14 individual minister areas on | A new human empire, from Quick Start or Game Setup, starts with all 25 switches off (spec 08 §3.6.9, §11.2, observed) | L |

Since 2026-10-04 the original's saved games (spec 08) are read and written, closing the row
that said OpenSE4 could neither import nor export them: Load Game and `--load` import a
`.gam` of the original, Save Game's *Save for SE IV* and `opense4-convert` write one
(`game/classic_save.hpp`, docs/SETUP.md "Games of the original"). What a conversion still
approximates is spec 08 §7.6 and §12. Since 2026-10-13 files of versions 1.40 to 1.95 are
read with the original's version gates (spec 08 §5.2), and the original's tutorial and
scenarios play with their pages (spec 06 §5.6). An imported game, like every load in the original,
reseeds the random generator from the game's seed; OpenSE4's own saves keep the generator's
state (the row "Random generator after a load" above).

The seven rows the analyst's check of OpenSE4's exports in the original added on
2026-10-04 (spec 08 §9.1) were closed the same day: exports carry the data-set checksums
computed from the data files (spec 08 §3.2.1), so players sign in to a simultaneous game;
OpenSE4's log entries are written unread, and those imported from the original keep their
kind, read date, picture key, other empire, event fields and battle details
(`LogEntry::classic`); a
pursuing Attack is written as kind 11 and read back as an Attack, an Attack without a
target as kind 8; Launch and Recover name their unit kind in the cargo-kind numbering; a
turn-based game exported before the current player's turn has started gives that player's
vehicles the movement of the turn's start. On 2026-10-05 the row that followed from the
third check (spec 08 §9.2), that such an export did not continue the vehicles' orders, was
closed too: before writing, the export carries out OpenSE4's start of a human current
player's turn on a copy of the game (`startHumanTurn`), since the original never starts a
turn on loading; what is left open is spec 08 §11.1 question 22. The engine keeps each
colony's never-reset counts of destroyed facilities (`Colony::destroyedFacilities`): every
removal pass after sabotage or an event takes them off again (spec 08 §11.2), and the
original's saved games carry them both ways. Battles still remove what they destroyed
once, the choice of spec 04 §19.1, and OpenSE4 has no hazard damage to colonies.
The save format became 8 and the network protocol 6 then; the modding SDK made them 9 and 7
in 0.11.0 (docs/MULTIPLAYER.md "Protocol").

**Mods** are an OpenSE4 addition ([MODDING_SDK.md](MODDING_SDK.md)) and change only the
games played with them. A game without mods plays as it did before the SDK: no script
runs, and hashed at save format 8 it gives the checksums recorded before the SDK. The
original's own mods (replacement data files and pictures) load as packages, so they no
longer need a copied game folder. A game played with mods is written for the original
(Save for SE IV, `opense4-convert`) while everything it uses fits the original's format,
with a note that the original plays it the same only on the same data
(`opense4-sdk dump`); one that uses what the original cannot hold (ability names mods
declare, their computer players or rules scripts, a design's own picture the install does
not have) is refused with the reason (docs/sdk/guide/multiplayer-and-saves.md "Saving for
the original"). Hegemon, the computer player that comes with OpenSE4, is such a mod and
off by default; the classic AI stays the one this page compares.

## Economy and population (spec 02)

The rows found on 2026-09-29 were implemented that day. The economy's end-of-turn work is
one function per step of spec 02 §12 (`economy.hpp`), run inside each empire's end-of-turn
processing (`turn.cpp`, spec 05 §8). Conditions are the original's 64-bit double, kept as
its bit pattern and changed in x87 arithmetic everywhere (`conditions.hpp`: generation,
facilities, events, combat, stellar manipulation), and setup's racial point cost sums
`economy::characteristicPointCost`, which costs each characteristic as stored. On
2026-09-30 the engine's remaining guesses (spec 02 §13 items 33 and 37–50) and the other
open items of spec 02 were settled from the executable, and the rows found then were
implemented the same day (the opening pools of spec 02 §9 followed with the turn order).
Built units go to the holders in the game's object order (spec 02 §6.5), planets and ships
mixed: since 2026-10-01 stellar objects and vehicles share one list of object slots, where
a new object of any kind takes the lowest slot any kind left (spec 02 §13 Q52). A queued facility
switches to a newer level in place, keeping what was paid (`cmd::QueueReplaceFacility`,
spec 02 §6.6), from the Upgrade Facilities button and the computer's upgrades.

On 2026-09-30 the engine's own choices where the spec had been silent (spec 02 §13 items
51–56) were answered from the executable, and on 2026-10-01 the engine followed the answers:
the conditions bands compare the stored double with the doubles nearest their edges, a value
on an edge in the band above (`economy::conditionsBand`, Q51); the queue removal pass takes
only a yard ship's facility items and, on a colony without a working yard, its ship and base
items and its upgrades with nothing left, so an upgrade with nothing left on a colony with a
yard is paid in full and converts nothing, and a repeated space yard item stops once the
colony has a yard (`economy::itemObsolete`, `stillBuildable`, Q53). Q55 and Q56 needed no
change (Q56's command check stays an OpenSE4 choice). Q52 (the shared object slots) came with
the movement work and Q54 (the cargo trim after each qualifying hit) with the combat work,
both on 2026-10-01. No row remains.

Also on 2026-10-01 the engine got Convert Resources (§5.6): a colony order of at most 65,000,
split from the window's lines, run at once in a turn-based game and on day 1 in a simultaneous
one (at most 21 a run), with Repeat on colony lists, the loss read when it runs and the log
entry in simultaneous games. It computes the gain with the exact integer formula (OpenSE4
choice) and refuses a new conversion for a colony without a converter (OpenSE4 choice, as for
Q56). A cloaked colony keeps building at its normal rate; only its yard stops (§6.1), and its
emergency counter moves twice a turn (§6.4).

## Vehicles, movement and logistics (spec 03)

Design names are unique in the whole game (`uniqueDesignName`; the original allows duplicates
from turn and empire files, an OpenSE4 choice that stands, spec 03 §19 Q50), composite orders
are expanded when given (`orders.hpp`), and units in space are held in groups that mix designs
(`Vehicle::mixed` and the group helpers of `design.hpp`). On 2026-09-30 the engine was brought in
line with the rules of spec 03 §19 as settled from the executable: the stored-double day counter,
chained actions in object-slot order, ad-hoc groups, the Ship Orders options, launches and
recovery, hazard damage, repeat battles, Sweep Mines, repair and training sources, ruins, the
destructive-centre cost map, design editing and the smaller rules. Long-range scanning follows
§3.3: a design is learned only when a human opens the report of a vehicle the scanners reach
(`cmd::OpenVehicleReport`), a ranged scanner works from any own object within its reach,
and `Long Range Scanner - System` works from any own object in the system (colonies
without population too) and covers ships and bases, not unit groups (`sight.cpp`
`scannerReaches`).

On 2026-10-01 the answers to §19 Q61–Q71 found six rows (the combat rules of Q60 and Q68
belong with spec 04), and they were closed the same day:

- **One object list** (Q62, spec 02 §13 Q52; `GameState::freeSlot`, `addObject`,
  `objectOrder`). The engine numbered vehicles apart and put every planet before every
  vehicle. Now stars, planets, storms, warp points, ships, bases and unit groups share one
  list of slots, a new object of any kind takes the lowest slot any kind left, and colonized
  planets act on day 1 in their slot's place; movement, built units, training, remote mining,
  the colony steps and the computer's repair yards follow that order.
- **Fleet orders** (§8, §9, Q65; `fleetOrders`, `cmd::SetOrders`). The engine kept a fleet
  list apart from the members' lists. Now a fleet's orders are copies in the lists of its
  members at its location: orders given to it or to any member are appended to each copy,
  joining and leaving clear the vehicle's list, and the members at the location carry out
  the head order of the member that acts, each list moving on. The client and the computer
  players read and give fleet orders the same way.
- **Fleet location and speed** (§6.3 step 2, §9, Q61; `Fleet::location`,
  `GameState::tidyFleets`). The fleet followed its leader, and a member elsewhere used its
  own movement. Now the location is the fleet's own record, following whichever member
  moved last; every member gains day credit at the lowest movement among the members at the
  location, 0 when none is there; a fleet with nobody there is disbanded at once.
- **A stopped vehicle's later actions** (§6.3 step 4, Q63). The action ran with 0 movement
  points and set every member to 1. Now it gives the acting vehicle exactly 1 when its
  maximum is at least 1, and the other members keep theirs.
- **Turn-based Attack and pursuits** (§6.4, §8, Q69, Q71). The Attack decloaked every member,
  one with no sector recorded followed its target, and a pursuit without a drone spent 1
  movement point and supply at its target. Now the turn-based Attack goes to the sector its
  target was in when it was given (or stays where the group stands when none was recorded)
  and decloaks nobody but the Ship Cloaking minister's vehicles, which cloak again
  afterwards; a pursuit group without a drone pursuing there waits at the target and spends
  nothing.
- **Colonies "seen"** (§6.2, §6.4, §8, Q70; `sight::canSeeColony`). A planet nothing
  obscures counted as a seen colony without sensors. Now the detection rule of spec 01 §6.3
  applies, which needs sensors in the system.

On 2026-10-01 the answers to §19 Q72–Q76 were implemented:

- **Objects replaced, not changed** (Q72; `movement_stellar.cpp`). Create Planet, Destroy
  Planet and the shockwave add the new object while the old one holds its slot, then remove
  the old one; the shockwave is one pass in slot order over vehicles and stellar objects, the
  stars last; Create Planet with no planet record of the size still removes the field, pays
  and reports.
- **An away fleet member's own list** (Q73, Q76; `Group::holders`, `setFleetOrders`). Carrying
  out an order, and orders, Clear and Repeat given to an away member, change only the lists of
  the members at the fleet's location.
- **Mothballed fleet members** (Q74; `fleetGroup`). They are full members: copies, group, speed
  0 (movement orders wait). Mothball, scrap and retrofit refuse fleet members, and the Scrap
  window lists only vehicles in no fleet that are not cloaked.
- **A computer player's ad-hoc companions** (Q75). Each joins alone and keeps its list.

The same day the orders of §8 followed the spec: **Jettison Cargo** (`cmd::JettisonCargo` and
its window; the original window's two faults are not reproduced, an OpenSE4 choice), **Use
Component** (the group's first member only, no checks or log, never failing, cleared first in
a turn-based game; no Self-Destruct branch), and **Use Facility** (a colony order with no
effect, cleared first and run with the colony's list in a turn-based game). The engine's new
choices are §19 Q77 (Repeat after the turn-based clearing; which lists a minefield and the Ship
Orders options clear when a computer group has companions).

The executable settled Q77 later on 2026-10-01: the turn-based clearing switches Repeat off as
the engine does, and a minefield clears only the lists a failure clears, as the engine does.
The Ship Orders options after a warp now empty every member's list, companions included, and a
turn-based Move To they interrupt goes on only within the current run (`encounter()`), and a
drone group's Sentry ends at a tenth of `Supply Amount for Low Supply Warning` while it holds a
unit, as a fighter group's does (`sentry()`, spec 06 §7 Q61). No row remains.

Players' reports on 0.8.1, settled from the executable on 2026-10-04 (spec 07 session 6), were
implemented the same day:

- **Population between own colonies in one sector** (spec 03 §11, §19 Q79; `commands.cpp`).
  Cargo Transfer moves the clicked race from one colony's population to the other's, capped by
  the target's free population room, the source keeping 1M, at no cost, in both turn styles.
- **Colonize** (spec 03 §8; `movement.cpp` `colonize()`). The planet is checked only in its
  sector, in the game's order; a failure clears the list and is told in a message box titled
  "Colonize" to the human whose turn it is (`TurnResult::messages`, nothing logged), or in a
  simultaneous game by one "Unable to Colonize" entry from the Colonization Minister with the
  picture `OrdersNotCompleted`.

A planet hidden from its colonizer, settled from the executable and implemented on 2026-10-04
(spec 03 §19 Q80, spec 05 §7 Q78):

- **The Colonize sight test** (`movement.cpp` `colonize()`, `movement::colonizeProblem`): the
  detection rule for the ship's owner applied to the planet, colonized or not, with the sensors
  of the moment, the colonizing ship's counted, before the movement test (spec 03 §8 "Seen").
- **Nothing checked on the way**: the computer players' Colonize was already Load Cargo, Move
  To and Colonize once given (`cmd::SetOrders`); the branch for a Colonize away from its planet
  (lists set without that expansion) checks nothing before the planet's sector either.
- **The loop** at a hidden planet is the original's and is kept: the targets apply no sight
  test, and no rule of ours avoids it (spec 05 §7.5 "On the way and on arrival"). In the pace
  set-up of spec 07 no game changed, since no generated galaxy holds a hidden planet; spec 07
  "The Colonize sight test" measures a variant with hidden planets.

No row remains.

Players' reports on 0.9.0, settled from the executable on 2026-10-05 (spec 03 §19 Q81, Q82),
were implemented the same day:

- **Design Upgrade** (`client/classic/screens/design_tools.cpp` `upgradeEntries`,
  `Rules::componentUpgradeTarget`, `designs.cpp`): each entry becomes the last component of
  its family in Components.txt order that the owner has researched, any family, numerals
  ignored, mount kept, and the designer always opens (spec 03 §4.1). Facility upgrades keep
  the highest numeral (`latestFacilityOfFamily`, spec 02 §6.6).
- **Orders to tagged vehicles** (`MainWindow::giveOrder`, `cmd::OrderTagged`,
  `turn_based.cpp` `noteEffects`, `movement.cpp` `Mover::tagged`, `EntryQuestion::tagged`,
  `cmd::EnterSector::tagged`): one command appends the order to every tagged list; in a
  turn-based game the group then runs at once through the first one tagged, stepping while
  all have movement, asked once and fighting one battle; leftovers run vehicle by vehicle at
  the next turn's start (spec 03 §8 "Tagged vehicles"). The choices this left open are
  spec 03 §19 Q83.

No row remains.

The REA parity audit of 2026-10-11 found these about cargo and units, implemented on
2026-10-14 (spec 03 §8, §11, §12, spec 04 §10.4, §10.6, spec 06 §1.3, §1.4, §1.10.2):

- **Cargo orders by kind** (`Order::cargo`, save format 10; `movement_util.cpp`
  `CargoWhat`). Load Cargo, Drop Cargo, Launch Units Remotely and Recover Units Remotely
  name a kind of cargo and move every design of it; the main window asks for the place
  first and the kind after (`MainWindow::chooseCargo`), and a colony's Launch and Recover
  only for the kind. The original's saved games carry the kind both ways. OpenSE4's
  computer players still give these orders for one design (the classic AI's own choice),
  and orders saved before format 10 keep their design.
- **Launch and recovery warnings** (`movement::UnitTransfer`): a human owner's order gives
  "Launch Warning" or "Recover Warning" with the reasons, as a log entry or, while its
  turn-based turn is live, a message box (spec 03 §8 "Where the player reads why"); a
  launch that went well is no longer logged.
- **The window Launch \ Recover Units** (`launch_recover.cpp`, `launch_window.hpp`,
  `cmd::LaunchUnitsNow`, `cmd::RecoverUnitsNow`): a click acts at once on the holder clicked,
  with no order and no log, shows the four "Can Launch:" lines, lists every own colony, and
  says why in message boxes. The same window opens in a battle (`TacticalLaunchScreen`):
  only the selected piece, only the groups launched while it is open, which a click takes
  back aboard (`TacticalOrder::Kind::Recover`). Its Move One .. Move All and Transfer
  Cargo's steps are kept with the empire (`InterfaceOptions::unitsStep`, `cargoStep`; spec
  08 §3.6.7). OpenSE4's buttons Launch Remotely and Recover Remotely in the window and its
  list of orders given are gone with the orders; the I and O order buttons remain.
- **Mines in a battle** (`Battle::launchOrder`, `minesLaid_`): laid with that window at
  the layers' combat rate and within the sector's cap; they join the minefield when the
  battle ends.
- **Transfer Cargo**: every holder's cargo lines shown, holders without cargo space left
  out, the plague refusal in a message box. **Jettison Cargo**: a right-click on a line
  opens the design's or the race's report. **Races On Planet**: from the Planet Report's
  population.

What stays different, by choice (inferred where the audit was silent): the map's window
keeps the carriers on the left (spec 06 §7 Q119); a recovery in a battle leaves the
launches of the combat turn counted (spec 04 §19 Q97); Races On Planet opens from our
page's Population line and only for own colonies.

GitHub issue #13 (spec 03 §19 Q85, 2026-10-14): a ship that lost engines in a battle kept the
movement it had, so its report read "4 / 3", and in a turn-based game a later move counted
the steps already made against the new maximum, leaving it nothing. Now every recompute of the
maximum (parts destroyed or repaired, supply, cloaking) caps the movement left at the new
maximum alone, for every vehicle and in either turn style (`movement::capMovement`), and the
Ship Report shows it as "3/6". What stays different: the report draws no gauge beside the
figures.

## Combat (spec 04)

The combat engine follows the rules of spec 04 settled on 2026-09-30 (§2-§19.1): start
positions, unit group and seeker damage, mines, cargo lost at once, troops fighting for
the ship's owner and ground combat in the colony owner's step, planets, the strategies
with the danger and attack maps, launches, design statistics, the tactical window's
phases, Auto, Resolve Combat and groups, and the combat simulator. When battles happen
follows §2 too (`combat::BattleCheck`, `movement.cpp`): in turn-based games only a
movement step (a warp jump included, after the mines), the Attack order and a Seek order
at its target run a battle check, one-directional from the group that runs it; a battle
started by a step clears the group's whole list, the Attack is used up, a Seek stays and
attacks again every time its list runs, and other participants lose only a Sentry at the
head of their lists. In simultaneous games every sector where an object carried out an
order that day, a waiting Sentry included, is checked from the side of each empire with
an uncloaked vehicle there, and a sector fights again in the same turn only when
newcomers arrive or a survivor was damaged.

On 2026-09-30 the engine was brought in line with the answers of spec 04 §19.2 (Q57-Q77)
and with spec 03 §19 Q60 and Q68 and spec 02 §13 Q54. A computer piece gives its weapons
targets at every choice by the steps of §16 (the first B candidates in rounds, the
overkill totals from 0 with full hit points, push weapons spread, point-defense and
warheads included, a fighter group's single target), builds the attack map and chooses
its square as §16.1 says (`Battle::chooseTargets`, `attackMap`, `rangeSquare`); a
surrounded piece makes no plan and its group dissolves first; a carrier reached again
launches again in the same phase, and Anti-Planet Drones are left out of the batches
(`phasePieces`, `launchFrom`); Board, Ram and Drop Troops pick their targets and squares
by §16.1, and drone groups move by their strategies like any piece, their target read
from their first Attack order when the battle starts. Combat groups keep only member
numbers, places come from the current leader's formation, fleet groups are numbered with
the tactical groups, and a fleet ship uses the fleet's strategy in any group; the group
of an automated side dissolves when its leader, left without movement, survives a hit.
Start positions rank the empires in the middle by object order (`objectOrderKey`, the
slots of the one object list of spec 03 §19 Q62, planets and vehicles mixed) and hops stay
on the map. A
check that passes always fights, colonies are seen by current sensors, minefields never
see; a pursuit meeting a battle, mines, a storm or turbulence keeps its order. Mines
strike unit groups with fresh pools per warhead and credit per mine. A fighter group has
one supply pool, refilled at every launch into it; pieces cloaked at the start cloak again
afterwards if they can; seekers take every damage type but Shields Only and the reload
types; an unseen battle runs the next turn's upkeep before it ends; the simulator keeps
each side's copied strategy list; a planet hit past its shields trims its cargo at once.

The engine's own choices where the spec is silent are spec 04 §19.1. Combat reads a drone
group's first order and a troop ship's Attack orders from the vehicle's own list, which
holds a fleet member's copy of its fleet's orders (spec 03 §19 Q65). On 2026-10-01 the
questions of spec 04 §19.3 (Q78-Q86) and spec 06 §7 Q30-Q40 were settled from the
executable; Q78, Q79, Q81-Q83 and Q85 needed no change, and the six rows the others left
were closed the same day:

- **Drop Troops** (spec 04 §11, §13, §16.1; spec 06 §7 Q37; `Battle::landingColony`,
  `dropTroops`). No treaty is checked: the order names no planet and lands on the
  adjacent colony of another empire that comes last in piece order, refused only when a
  third empire's troops are landed there or the ship carries none; a computer piece with
  Drop Troops in effect tries the landing after every move it plans; the ground combat is
  fought at once whatever the treaty, and every landing resets the planet's piece
  (weapons ready, shields full, targets engaged cleared). The engine's choices are spec 04
  §19.4 Q87-Q89.
- **Drone targets and the overkill totals** (Q80; `Battle::chooseDroneTarget`). The drone
  target is the main target of the drone choice (the piece its pursuit names, else the
  first candidate below its limit that a drone may take, else the first), chosen at
  set-up, launch, when it left the battle and when it changed owner; every piece keeps its
  two totals between choices, cleared only by the rules of spec 04 §16, and a drone's
  warhead damage is added to its target's.
- **Range of the firing choice** (Q84): each weapon's range as the strategies see it, so
  never past 20 squares.
- **Battle verdict** (Q86; `Battle::finish`): every other empire's survivors count, whatever
  the treaty. The computer's anger judges battles by the same verdict (spec 05 §7.3), so its
  reading of the record no longer counts neutral obstacles as another side's survivors, and
  counts a piece taken in the battle for its captor (`ai_anger.cpp`).
- **Battles shown as they happen** (spec 06 §1.10.5, Q30-Q32; `turn.hpp`). On one machine a
  battle with a human side stops the engine call once it is set up, before combat turn 1
  (turn-based: Tactical or Strategic, or with No Tactical Combat the Strategic Combat
  window with Begin and Close; simultaneous, when the Settings show battles); the window
  fights it phase by phase, and the call resumes after Close, so the reports and the rest
  of the turn come then. The record keeps each ground combat's place among its events.
- **Ground combat records and the end-of-turn fight** (spec 06 §1.10.6, Q33, spec 05 §8
  step 17; `GroundCombat::perRound`, `runGroundCombat`). Records keep every stack's count
  after every round, the window shows them round by round (0.9 s, explosion, boom), and in
  a turn-based game on one machine the colony owner's fight with a human side stops the
  call, shown after a notice; both empires get log entries either way.

Games on different machines are the one difference left in how battles are shown: their
host fights every battle without stopping, and the client shows the battles afterwards
(spec 06 §7 Q74, the turn-based row under Cross-cutting, an OpenSE4 extension).

Spec 04 §19.4 Q87–Q89 and spec 06 §7 Q76–Q78 were settled from the executable later on
2026-10-01 (Q89 matches). The engine followed the same day: landings judged by the planet
piece's side, the refusals in their order with a silent one, planet pieces dropping their
colony's troops (Q87, Q88); piece numbers reused, so a drone can take a new piece without a
choice (§10.7); every simultaneous battle shown when the Settings ask, computer-only ones
included (Q76).

The step toward a square (spec 04 §5, found 2026-10-02) was implemented the same day
(`Battle::walkToward`): one step toward the destination on each axis, and when that square
is taken up to four random tries of the two squares beside it, each drawing from the
battle's own sequence; a strategy's move is recorded as a move toward its square, so the
strategic, tactical and replayed battles stay identical. Which square each draw names is
spec 04 question 90 (inferred). Over 120 games of the pace set-up the drawn battles of
turns 51–100 fell from 44 to 37 % (spec 07 "Pace after the scrap, cap and fleet rules").
This row remains:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Random numbers of a battle shown tactically (client drawing) | The display draws no random numbers | Spec 04 §19.1, spec 06 §7 Q77: a miss's direction and a planet's point are drawn from the battle's sequence, so a shown battle continues differently | M |
| A click on an enemy that does not fire (out of range, reloading, no supplies) | The reason is written on the map (ours); only a click on a piece that is not hostile gets the "Cannot Fire Weapons" box | Spec 04 §19.6 Q96: which of these reasons the original puts in a box was not traced | L |

The parity audit of 2026-10-11 (REA) brought the tactical window and the simulator in
line on 2026-10-14 (spec 04 §5, §6, §10.3, §12, §17, spec 06 §1.10.1–§1.10.4, §3.3): the
boarding outcome's message boxes (the record keeps a `Boarding` event), the weapon ticks
as the window's own (the engine's `ToggleWeapon` switch stays for script players), the
next and previous piece search and the selection at each phase's start, the target panel
that follows the pointer with its damage line (each hit's shield and structure damage is
recorded), the Orders menu always lit with the original's Ram and Capture refusals in
their order and boxes, enemy seekers and neutral obstacles rammed, a leader picking a new
formation for its own number, clicks, Auto and E before Begin, Center Map only for a piece
out of view, group identifiers on every piece, the weapon cells, the buttons after Resolve
Combat, and the simulator's Begin (one side, no sight test, a planet added again). The
details it left open are spec 04 §19.6 Q91–Q96. Launching and recovering in battle (the
Launch \ Recover Units window, mines laid) is in the units part of "Vehicles, movement and
logistics" above, its open question spec 04 §19.7 Q97.

## Research, intelligence, diplomacy, events, score, turn order (spec 05 §1–§6, §8–§9)

Every rule of this section follows the spec. On 2026-09-30 the engine's own choices (spec
05 open questions 38–46) were answered from the executable, and contact loss was found
(question 13, corrected). The rows found then were implemented on 2026-10-01:

- Contact is lost when no warp path leads from an empire's colonies to a colony of the
  other: checked once per game turn in both turn styles, after the design cleanup and before
  the victory check, it returns that side to "no contact", drops its intelligence projects
  against the other and logs "Contact Lost", which the human player's history file records;
  first contact needs the same path (`diplomacy::checkContacts`, `firstContactIn`,
  `warpReach`; Q13, §3.1).
- An "Any" political operation draws its third empire among the living empires the source
  has met, other than the source and the target, and fails without one (`effects::pickTarget`, Q38).
- Planet - Conditions Change sends its messages like any other effect (Q39).
- The players' files use the original's layouts: statistics with the date the end-of-turn
  processing sees, history lines dated as tenths with the contact-lost lines, and the log
  copy only when `Create Log Text Files for Players` is true, rewritten each turn with its
  header (`score::playerRecords`, Q40). Their folder and names stay an OpenSE4 choice.
- A rebel empire keeps its former owner's experience, minister style and AI state, plans and
  anger; its name and pictures are drawn as §2.3 says; design theft reads a built-at-least-once
  mark (`Design::everBuilt`, Q41). OpenSE4 stops drawing names after 1,000 draws and numbers
  the name (the original would draw for ever).
- Every empire records its home sector (`Empire::homeSector`), and High and Catastrophic
  events spare every empire's recorded location (Q42).
- A ship's or base's report dates only its own design; a unit group's report dates nothing
  in a simultaneous game (Q43). OpenSE4 skips it on the player's machine too, where the
  original dates it until the turn ends.
- Stellar manipulation reports go to every empire present in the system, and anger term 2
  counts them in the counting empire's own log (`movement::stellarReportNames`, Q44). The
  per-empire mark of what a turn-based step counted stays; opening the Log window marks
  nothing in OpenSE4.
- A simultaneous political step counts the messages delivered since the empire's previous
  step (`ai::simultaneousWindow`), so a player's message of step 2 counts in its own turn as
  question 24 states; it counted a turn late before.
- `Ship - Moved` draws one sector number and disbands the moved ship's fleet (§4; done
  with the movement work, 2026-10-01).

Q45 and Q46 needed no change. The "(inferred)" markers of these choices are gone (the third
empire draw, the silent Conditions Change, the log copy's default and the files' widths, the
rebel's pictures, the theft's built count, the capitals standing for home locations, the
report's cargo designs, the culprit's log in anger term 2), and the 21-order limit's is now
"(confirmed: binary)". On 2026-10-01 the answers to questions 50 and 51 followed: Destroy
Planet reports to the empires still present after its result, the shockwave to every empire
that lost an object there, mine fields included (`Manipulation::noteWitnesses`); a human
player's request about a third empire must name a living empire it has met, other than itself
and the recipient (`cmd::SendMessage`). The engine's choice is question 52: a computer
player's own requests are not checked.

Question 52 and spec 06 §7 Q70 were settled from the executable later on 2026-10-01, and the
engine followed the same day: the requests the computer starts are chosen first and only then is
their flag tested, a refusal ending steps 1-5 (`ai_diplomacy.cpp` `chooseRequest()`); step 2
rolls for ships, then for colonies, with the 1-in-3 roll per candidate; step 3 names the
lowest-numbered empire by both treaties; step 5 names the system and the highest-numbered other
side of the newest battle lost while defending. A Politics minister's messages are not checked
(`cmd::SendMessage::minister`). The engine's choice is question 58 (step 2's candidate order).
A completed package logs one entry per item, the receiver's first, with the titles and Goto
targets of spec 06 §7 Q70, and nothing for the package; invalid items are skipped silently; a
package treaty logs "Treaty Enacted", which writes a contact line in the history file, and an
accepted proposal only its "Message"; channels, technology, systems and star charts follow the
same answer (`diplomacy::executePackage`). No row remains.

The 2026-10-11 audit of the original's dialogs found the diplomacy rules below; the engine
follows them since 2026-10-14:

- A subjugated race (the subordinate side of a Subjugation; a Protectorate does not count)
  can neither propose nor accept a treaty, not even with its master; Break Treaty and Declare
  War stay (`diplomacy::isSubjugated`, spec 05 §3.2). Nothing stops a treaty itself any more:
  OpenSE4's earlier choice, which refused a subject's treaties with third empires when they
  were set, is gone, and a treaty in an accepted package takes effect. A subject's computer
  player and Politics minister propose no treaty, leave treaty proposals unanswered and,
  since 2026-10-14 (complete listing), refuse every trade, gift or tribute holding a treaty
  (`ai_diplomacy.cpp`, `Politician::subject`).
- The treaty grid shows the treaties of the empires we hold Non-Aggression or better with,
  not only our allies' (`diplomacy::treatyVisible`).
- A player's messages follow the original's window (`diplomacy::playerMessageProblem`, spec
  05 §3.4): proposals name Non-Intercourse to Partnership (never War or None), Break Treaty
  needs Non-Aggression or better, a trade has items on both sides and no "Any" item on the
  side we give, a gift or tribute holds items and a request for one names what it wants
  (which the computer player pays from, §7.4), Surrender and its demand need Allow Surrender.
- A human player's statistics record (`Empire::statistics`, save format 10) keeps the rows
  the original's statistics file gets, for the Comparisons window (spec 05 §5).

What remains different, on purpose:

| Where | Engine now | Original | Impact |
|---|---|---|---|
| A human player's messages and answers (`cmd::SendMessage`, `cmd::AnswerMessage` without `minister`) | Refused in the command when the original's window would not send them (a subjugated race's treaty, an empty trade side, an answer to a message that is not the newest recent one, a second message to an empire in a turn, and so on) | Checked in the Communicate window only | None in a game on one computer; a network player cannot get round the window |
| Grant independence (`diplomacy.cpp` `grantIndependence`) | An OpenSE4 extension: offered, and the sender's colony is abandoned for the recipient to settle | Never offered; its message does nothing (spec 05 §3.4) | L |

## Galaxy, setup and sight (spec 01, spec 02 §9)

Generation, empire placement, starting planets and stockpile, the setup option lists,
racial point costs, sight and stellar manipulation follow the specs as settled on
2026-09-30 (`generate.cpp`, `setup.cpp`, `sight.cpp`, `movement_stellar.cpp`). Every
starting planet is a capital of colony type "Homeworld", and the home system is recorded
when the game is created (`Empire::homeSystem`). A new game and Quick Start are
turn-based. Maps are saved and loaded in our own format ([MAPS.md](MAPS.md),
`map_file.hpp`) with starting points placed first; the game keeps the points a later Save
Map writes (`GameState::startingPoints`). Since 2026-10-14 the original's map files and
empire files are read and written beside ours (spec 08 §13, §14; `classic_files.hpp`):
Load Map and Add Existing list both kinds, the install's `Maps` and `Empires` folders
included, Add Existing makes the original's technology and racial-point checks at once,
Edit asks for an empire's password, Save To File asks "Save Designs" and writes a copy with
computer control off, and both Save Map windows, Save To File and the Game Menu's Save
Empire offer *Save for SE IV*. Our empire files keep what the original's do (the password's
check, the e-mail address, the minister switches, the Empire Options and window memories,
the name lists), and a new game gives it to the empire; what remains different is in the
table after this paragraph. The autosave choices are applied in local and hotseat games of either turn style (after a
simultaneous turn is processed; in a turn-based game, since 2026-10-14, at the start of the
first human's turn of each game turn, before that turn starts, the file marked as an autosave
so that loading it opens the Next Player window: spec 06 §6.1), into files named after the
last digit of the turn count, and can be changed during the game (Empire Options). Spec 01 §14 Q41–Q43 were answered from the executable; the engine follows them,
and since 2026-10-01 a comet or warp point entry of a system template is placed like any
other entry (its position and a record drawn, its sector marked and recorded for `Same As`)
and keeps an empty name for the letters of later planets there (Q43).

| Where | Engine now | Original | Impact |
|---|---|---|---|
| Passwords of the original's empire files (`classic_files.cpp`) | Read as OpenSE4's check of the trimmed lower-case password, so Edit (an exact comparison) and the game take it typed in lower case; *Save for SE IV* writes none (OpenSE4 cannot recover the text) | The plain text, compared exactly by Edit and trimmed and in lower case in the game (spec 08 §3.6.1, §13.4) | L |
| What an empire file brings besides (`classic_files.cpp`) | The queue templates (OpenSE4 keeps them per computer), the computer players' memory and difficulty and the source game's player number and home are left out | The whole empire record goes into the new game (spec 08 §13.2, §13.4) | L |
| The original's file pickers (`screens/setup.cpp`, `game_menu.cpp`) | Add Existing lists OpenSE4's `empires` folder (both kinds of file) and the install's `Empires` folder together; *Save for SE IV* takes a folder and a file name, lower-cases only the file name, and has no New Empire File or New Map File button | Load Empire lists the Empires folder (the mod's when it has one); the Save Empire and Save Map pickers have New Empire File / New Map File and lower-case the whole path (spec 08 §13.4, §13.5, §14.3) | L |
| Begin Game after a loaded or generated map | OpenSE4's game seed is the setup's | Not known which seed the original keeps (spec 08 §11.1 Q27) | L |

Colony (planetary) cloaking (§6.9) came on 2026-10-01: cloak and sensor levels from facilities
alone, stored and recalculated only at the moments the spec names; Cloak and Decloak at once
(`cmd::CloakColony`), Decloak running the first-contact check; the cloak levels in detection,
first contact, the battle checks, Sentry, stellar manipulation, Colonize, the computer players'
choices, the Planets window, the map and the intelligence picker; a cloaked colony's yard does
not work; battles decloak colonies and cloak them again; the computer players' colony orders
decloak and cloak again. The engine's choices (how a simultaneous host receives a mid-turn
Cloak, which loads recalculate, upgrades) are §14 Q44, settled from the executable later on
2026-10-01, and followed the same day: a Decloak in a simultaneous game makes only the acting
empire's side of a first contact (`diplomacy::firstContactIn` with `onlySide`, in the colony's
system); the host recalculates the colonies a player's orders name when it reads them, then
sight everywhere; every reading of a game file recalculates every colony (Load Game, `--load`, a PBEM game read
by the host for the turn files and for processing, a local or hotseat simultaneous game passing to the next player or to
processing; `diplomacy::recalculateColonies`); a completed upgrade does not; the automatic
decloak is a full Decloak (`diplomacy::recalculateColony`).

Also on 2026-10-01, what other players see of a colony (spec 01 §6.9 "What other players see")
followed the spec. `sight::canSeePlanet` counts the viewer's EM Active sensor level as at least
1, and the system panel shows, counts, names and lets the player click only the stellar objects
that pass it (`sector_view.*` `shownStellarObjects`). Every window that asks whether a colony is
seen uses the detection rule (`canSeeColony`): the population bars, the planet report, the
Planets tabs, the colonize star, the galaxy presence marks (`map_style::presence`) and the
intelligence picker; the Planets statistics count real owners. The network view
(`redactForEmpire(rules, state, empire)`) leaves out a colony hidden from the viewer by its
cloak, and takes its planet out of the system's list, so a modified client cannot see it (an
OpenSE4 choice; the original sends everyone the whole game). Left: location lines can name a
hidden planet in local and e-mail games.

The starting assets followed spec 01 §2.1 and §3.6 on 2026-10-01 (`setup.cpp`,
`ai::designMinisterRun`, `game::StartExtras`): no empire gets a ship or a design at creation,
computer players design in their first turn, a New Game player has only its empire file's
designs, and Quick Start gives the player one Design minister run, which gives a Terran player
the eleven observed designs. The tutorials that teach with ships list them with the lesson key
`starting_ships`, an OpenSE4 extension (docs/LEARNING.md).

First contact follows spec 05 §3.1 since the third pass of 2026-10-01: the check runs in one
system at a time and only at the moments the spec names (a warp arrival, every decloak, a
logged event, a package's planet or vehicle, a rebellion project; every system at game
creation and after a surrender), by the current positions and sensors
(`diplomacy::firstContactIn`, `firstContactEverywhere`); the galaxy-wide passes after moves,
turns and events are gone. A pair where only one side has met the other (a simultaneous
Decloak, above) is completed by the next check in a system where both detect each other.
Whether a timed event's start message also counts is spec 05 question 57.

On 2026-10-03 the original's generated galaxies were read through a debugger and compared
with ours (spec 01 §3.8; 544 quadrants of every type and size, 41 placements): every
distribution agrees, and the link building, the connectivity pass and the warp point
sectors come out exactly as the original's. The empire placement showed that the original's
spread test counts two jumps more than the real number (spec 01 §3.6). Since 2026-10-03 the
engine's spread test does the same (J + 2 > L), the last-resort homeworld may reuse an
earlier home system when "Allowed to start in the same system" is on, and a template's
`Ring 0` is any sector of the inner 11 × 11 square (`generate.cpp`). `Ring 8` and `Ring 9`,
which the original places off the grid with no defined result (spec 01 §4.3, §14 Q46), stay
on `Ring 7` with a warning, an OpenSE4 choice.

These rows are where the engine differs:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Generation edge cases (`generate.cpp` `drawNames`, the connectivity pass of `buildLinks`, the "warp points anywhere" draw of `placeWarpPoints`, `placeHomeworlds`) | Systems beyond the name list get generated names; the connectivity pass marks only a system it cannot link, searches any distance and always ends; "warp points anywhere" stops after 1,000 draws; a map point on a sector another empire took is skipped | Spec 01 §3.4, §3.5, §3.6: such systems get no name; the pass marks the system with everything linked to it, looks only 68 squares far and can loop forever; the draws never stop; nothing checks a taken point. The engine's choices are deliberate: keep them | L |

## Client windows (spec 06)

On 2026-10-01 the Scrap window, Player Computer Control, Reset Passwords and the Settings.txt
keys the client ignored followed the specs:

- **Scrap window** (spec 03 §15; `src/game/scrap.*`): Analyze (one level per distinct
  requirement above the owner's level, `research::analyzeLevel`) and Fire On (another armed
  vehicle needed, Number Lost, one Construction entry); all seven actions carried out at once
  in a turn-based game and as the vehicle's only order in a simultaneous one, tested again at
  its first action; designs keep Number Scrapped. The window shows the last selected vehicle's
  research potential word and lights each button only when every selected vehicle qualifies.
  Our choices are spec 03 §19 Q78 (the computer players' Scrap and Retrofit go the same way).
- **Since 2026-10-13** (the REA parity audit, findings on orders): a Scrap-window order that
  fails when carried out counts as done (the orders behind it run), with "Cannot Mothball",
  "Cannot Unmothball" and "Cannot Retrofit" entries; Unmothball's button needs a yard but not
  the cost; the Scrap order scraps its group's first vehicle, so the Scrap minister's order on
  a fleet member (which no longer leaves its fleet) scraps the fleet's first member there
  (spec 05 Q74); the window opens with nothing selected, with Order By Name / Order By Class,
  Select All / Select None, "<design> Class" rows with status icons, the last selected
  vehicle's Status and sums only for lit buttons; Retrofit's Select New Design lists only
  buildable designs of the hull that are not obsolete, with picture, Name, Size and cost
  columns and the Design Report on a right-click; Abandon Planet is a colony order (kind 64
  imported and exported) carried out at once only in a turn-based game; Create Fleet makes an
  empty "Alpha Fleet" (and on), deleted when the window closes empty, under minister control
  as new vehicles are; Fleet Transfer lists only vehicles that may join and the fleets whose
  location is the sector; Systems To Avoid toggles the avoid mark on every tab. OpenSE4's own
  in these windows: the Scrap window's Facilities tab and status line, the reason a picker
  row gives when hovered, Fleet Transfer's Disband Fleet and Rename Fleet, Systems To Avoid's
  Release All Claims, and turn processing deleting an empty fleet a lost window left.
- **Player Computer Control** (spec 06 §1.2.1, §7 Q84; `ai::setComputerControl`): the check
  list behind a master password, the mark with all ministers and flags; local and hotseat games
  end when no human is left. Since 2026-10-03 every empire can be switched, neutral ones too:
  neutrality is a mark of its own (`Empire::neutral` while a human plays a neutral empire,
  `isNeutral()` for the rules), so a neutral empire handed to a human keeps the neutral rules.
  A player's copy of a game on different machines changes only there; its orders carry the
  minister switches and the fleets' flag (`cmd::SetMinisters::fleets`) and, of the ships,
  units and colonies, only the flags of those changed that turn (`ClassicSession::markChanged`,
  spec 06 §7 Q99(d)).
  The TCP/IP host's toggle flips only the mark (`ai::setComputerMark`) on every row, an empire
  that was a computer player from the start included, which the host then plays ("[Host]").
- **Reset Passwords** (spec 06 §1.9): six digits from a source apart from the game's, shown to
  the host only and written in after the next turn's orders are read, never saved; on the
  in-game host's Options window, by an admin of a headless server and by the e-mail host
  (`pbem process --reset-passwords`).
- **Settings.txt keys** (spec 06 §1.9, Q83): `Allow CD Music` (the Options music rows and the
  Combat Options lamp), `Allow Export of Weapon And Component Data` (the Weapons Report's
  Export button writing `Weapons.txt`, `Comps.txt`, `WeaponFamilies.txt` and
  `CompFamilies.txt` in the original's fixed-width columns since 2026-10-03),
  `System Ship Movement Delay Milliseconds` (a wait after each animated step, read as seconds
  as the original does; none in the movement log replay, spec 06 §7 Q62) and the Finale
  picture lists (the ending windows: Victory, Lose, Human Dead).

The client's own differences are written in spec 06 beside each answer ("Our client
differs"). Every one settled on 2026-10-01 is implemented; the client's own choices are
noted beside each answer. Spec 06 §7 Q83–Q98 were settled from the executable and from
screenshots on 2026-10-03 (the ring's pixels of Q85 match ours); the rows below list what
our client does differently.

Seen side by side with the running original on 2026-10-01 ([spec 07](spec/07-observations.md),
session 3, which lists each difference in full). Since 2026-10-01 the client follows what was
observed (spec 06 §1.11): every list heads picture columns "Pic"; on/off settings in button
columns are check boxes; Planets, Colonies, Construction Queues, Research, Designs, Create
Design, Empire Status, Empires, Ships\Units, the Log, the Combat Simulator, Tactical Combat
and the report panel have the original's labels, blocks, button order and slots.

Since 2026-10-03 the management windows follow the answers of spec 06 §7 Q83, Q84 and
Q88–Q96 (their remaining choices are noted beside each answer):

- **Every list** (`screens/list_widgets.cpp`, Q89): the 24 px arrow column with the
  `Arrows.bmp` cells in their four states, a thumb as long as the visible share, a press or
  drag in the track scrolling there, a held arrow repeating every 100 ms and the wheel a row
  per notch. The column's parts are UI tags (`<list>:up`, `:down`, `:track`, `:thumb`).
- **Research** (Q92): the project boxes with the name, Research Level N, Completion, Cost Per
  Turn and the bar of green blocks; Tech Tree only with the new game option "Players can see
  the complete tech tree" (`GameOptions::completeTechTree`, off in a new game; the Game
  Settings box, a lesson's `complete_tech_tree` and a server setup file's key set it).
- **Intelligence** (spec 07 session 5): the Research layout, the points in the title strip,
  silver group headings over 14 px rows, the same boxes, Divide Pts Evenly and Reorder Projects
  in their slots.
- **Empire Status** (Q95): the original's places, the amounts in full and right-aligned, icons
  on From Our Colonies only, Net Resources Per Turn a labelled row.
- **Colonies** (Q90): the summary's labels and places, amounts rounded up in thousands or
  millions, a click showing the colony, Set Colony Type asking for the planet, Scrap Facil
  Types on every colony.
- **Designs** and **Create Design** (Q93, Q94): the original's rows, alphabetical order,
  detail, component grid and places; a new design without a design type; the warnings after a
  red ball; To Hit Modifiers and Condensed View kept with the empire, the first changing the
  figures box (`game::designToHit`).
- **Borders** (Q96): a window of its own (`borders`), with a check box per empire, the
  contested systems in yellow, Legend, and claiming by a click.
- **Orders cells** (Q88), **Log Goto** (Q91), the **Weapons Report export** and the
  **endings** (Q83): the original's behaviour. A human-controlled empire found defeated plays a
  last turn (the Lose ending) and is marked dead at its end (`score::checkDestruction`), so a
  lone human sees Lose, then Human Dead at the next End Turn; the conquest of the galaxy shows
  the Victory picture and asks whether to play on. Since 2026-10-14 a turn-based game destroys
  the human at the end of that last turn's start, as the original does (Q99), plays one more
  game turn after its victory check is met, every human seeing the Victory ending at its own
  turn, and ends with the Game Completed box (spec 05 §6).
- **Help** and the **Galaxy Map** (spec 07 session 5): the original's layout, frame, places and
  button slots; Help's Weap Mount tab and Manual button.

The players' reports on v0.9.0 (GitHub issues #1 to #4, #6, #9 and #10) were answered on
2026-10-04:

- **Designs' Hide Obsolete and Stats\Strategy** are kept with the empire's options, saved with
  the game as the original does (spec 08 §3.6.7; save format 8, network protocol 6).
- **The system report's picture** sits inside the panel's frame rail (spec 06 §1.4); it is
  opaque and covered the rail.
- **The Treaty Grid and Scores** fit the original's large dialog: ten empires a page, each
  column headed by its flag; Scores is a column per empire with a row per figure, as spec 06
  §1.5 describes the original's window.
- **Empires' "Inbox: N waiting"**, OpenSE4's line, counts a message only while the Log lists
  it, the only place it can be answered (spec 06 §4.1).
- **Set Construction Queue's progress** counts each resource up to the item's cost (spec 02
  §6.3): it never shows more than 100 %.
- **Dear ImGui's errors** go to opense4.log; release builds never show their red tooltip or
  stop. The Log's details pane on an entry without a body no longer makes one.
- **The Log** restores its scroll position only with the stored entry (spec 06 §4.1).

The 2026-10-13 audit of the economy windows against the executable (spec 02 §6.4–§6.6, §10,
spec 04 §16, spec 06 §1.8.3, §7 Q28, Q90, Q94, spec 08 §3.6.6–§3.6.9) brought in: the
queue's own refusals only (no check of population, the ship limit or a yard for ships when an
item is queued), each refusal a "Warning" box and the "Note" before the add; the Units tab's
"Amount" list with One Turn's Worth; the available list grouped under grey headers with
unbuildable designs in grey; the Ships tab lit only with a working yard and the tab
remembered (`InterfaceOptions::setQueueTab`); the empire's queue types saved with the game
and carried to and from the original's saved games (`Empire::queueTypes`,
`cmd::SetQueueTypes`), Fill giving a "Warning" per refused item; Clear Queue's clear (Move To
too, the emergency kept) for a cloaked or mothballed ship and a planet handed over by treaty,
and since 2026-10-14 for a colony taken in ground combat (spec 02 §6.4);
Scrap Facil Types refunding the setting alone (`cmd::Scrap::typeWide`); the type list window
with Add Type and Delete Type for colony and design types; Complete AI as plain buttons that
no longer latch full control, the 25 minister switches written to the original's file as they
are, and Indiv. On/Off asking "Confirm"; the Strategies window's Damage page, its Launching
pick lists and Add's "Enter Strategy Name" with the original's new strategy; the Colonies
window's tab remembered (`InterfaceOptions::coloniesTab`); Shift+click doing nothing in
Planets. What stays different from that audit is in the table below.

OpenSE4's own additions in these windows, none of which moves an original control: the Find
box in Help's heading row; the Galaxy Map's Show Distances (in an empty slot), the legend (in
three empty slots) and the notes line under the map's hint; the Designs window's note in its
title strip, the scroll to a design just made and its Make Current label for an obsolete
design; the hover tooltips of the Research, Intelligence and Tech Tree lists. What remains
different:

| Where | Client now | Original | Impact |
|---|---|---|---|
| Cancel in a type list (`TypeListPicker`) | Set Colony Type and the designer's Design Type keep the type they had; the question a new colony asks keeps the suggested type, which the colony already has | Set Colony Type's Cancel empties the colony's type; the colonization question's Cancel leaves the new colony without a type; the designer's leaves the design without one (spec 02 §10, spec 03 §8, spec 06 §7 Q94) | L |
| Minister switches (`MinistersScreen`) | Each lamp, Select All and Select None take effect at once (`cmd::SetMinisters`) | The lamps take effect when the window closes (spec 02 §10) | L |
| Queue types (`queues.cpp`) | Select Queue Type also lists OpenSE4's types kept on this computer (`queue_types.txt`, Keep on Computer) and the built-in ones that fill the free facility slots, under headings of their own | The empire's types only (spec 02 §6.4) | L |
| Ships at the limit, computer players (`ShipBuilder::queueable`) | Place no ship or base while the empire is at its ship limit (inferred) | Not known whether the vehicle list checks the limit; the queues do not (spec 05 question 81) | L |
| E-mail address (`Empire::email`, `cmd::SetEmail`) | Kept with the empire since 2026-10-03 (Empire Setup's Email box, Change Email, saved with the game, and in empire files since 2026-10-14); it travels in a player's orders only as a Change Email command, and another player's address is left out of a player's view and of the lobby | Saved with the empire in the game file and in every orders file, so every player's game holds every address (spec 05 §9.2, spec 06 §7 Q95) | L |
| Moves shown in the system window (`ship_glides.cpp`, `turn_start.cpp`) | Since 2026-10-14 each square is its own step and, with the default "Ship movement: one group at a time", one group's run is shown after another, the view following the player's own groups. Ours also shows the other empires' moves the player sees, in the order they were made: a turn-based game's computer players before the player's own turn start, and a simultaneous turn's moves at the next turn's start. OpenSE4's "all at once" starts every group together; a network or e-mail client, which gets no steps, glides each changed group straight to its new square, one group after another; a battle that stops the call is shown before the moves that led to it; no minefield explosion and its half-second wait; the current sector stays put when a move order runs; a click or a key shows a turn's moves at once | Only the moves of the human whose turn it is, in a turn-based game, are animated: a computer player's turn shows a black main window with "Player N taking turn..." and its moves not at all, a simultaneous turn nothing outside the replay; a battle when it starts; a minefield hit draws a small explosion and waits half a second; a move order makes sector 0 current first (spec 06 §2.4, §2.7, §7 Q112, Q123) | L |
| Choosing one of the original's scenarios (`screens/learn_screens.cpp`) | The intro's Tutorial and Scenario open OpenSE4's Learn window; its Scenarios tab lists the original's scenarios beside the mods', and Start Game starts one. The scenario folder is the install's, or that of the last classic mod in use that has one | Tutorial starts the tutorial at once; Scenario opens the "Load Scenario" picker with Change Directory; the folder is that of the `Path.txt` mod when it has one (spec 06 §1.1, §5.6) | L |
| Scenario pages in network and e-mail games (`classic_mode.cpp` `openTurnPage`) | Each player's own computer shows that player's page at the start of their turn | The host's turn processing shows every human's page on the host's screen; a network player never sees them (spec 06 §5.6) | L |
| Scenario quirks not copied (`classic_scenario.hpp`) | `For Players` is a list of numbers; "Page i of n" counts i within the page's series; loading a scenario game from the intro keeps it one; a new game never carries the tutorial's flag; the page shown last is kept per empire | Player 1 also gets player 11's pages; i is the page's place in the whole file; Load Game and Resume Game from the intro drop scenario mode; the Game Menu's New keeps the tutorial's flag, "T" and pages (inferred); one page shown last for the game (spec 06 §5.6, spec 08 §3.4) | L |
| The endings that end the game (`ClassicMode::leaveToFront`) | Game Completed, Human Dead, No to playing on after the conquest and a fourth wrong Next Player password return to the intro | The program exits (spec 06 §1.7) | L |
| Victory in a simultaneous game (`score::checkVictory`) | The game ends at once: the Victory ending at the next turn's start, no further turn | Not described: a turn-based game plays one more game turn first (spec 05 §6, open question 79) | L |
| Sign-in passwords (`ClassicSession::passwordMatches`, `loginPasswordMatches`) | Only a hash is kept: the trimmed password in small letters, then as typed, is tried, so a password set with capitals opens Login To Game only with them; "master" is recognised in a local game's hash only | The plain password: Next Player trims and keeps case, Login To Game trims and ignores case (spec 06 §1.1, §1.2.1) | L |
| Login To Game's Game Master (`ClassicMode::drawLogin`) | Goes on to the first human's turn behind the Next Player window; a destroyed or computer-controlled empire's row signs nobody in; Cancel returns to the intro | The host's between-turns view (player 0) (spec 06 §1.1) | L |
| Save Game (`screens/game_menu.cpp`) | OpenSE4's window: a name box, the saves, Save, Change Folder (the original's Change Directory), Save for SE IV; questions before overwriting and deleting; it stays open after saving | The Load Game dialog titled "Save Game": a click overwrites at once, New Save Game asks for the name; Delete Game deletes at once and also removes the file of the same name in `temp/` (spec 06 §1.2) | L |
| Progress window (`ClassicMode::drawProgress`) | Shown while a local or hotseat simultaneous turn is processed, its steps ours | Also while the network host processes a turn and while a new game is created and its players placed (spec 06 §1.7) | L |
| Data file errors at start-up (`ClassicMode::drawDataErrors`) | A window per data file, then the game goes on with what loaded (installed data without mods); mods with errors start the game without them | A window per data file; whether the game goes on is not known (spec 06 §1.1, inferred) | L |

The 2026-10-11 audit of the original's designs, research and intelligence windows (spec 05
§2.1, spec 06 §7 Q92–Q94 and Q99(c), confirmed: binary) brought these windows in line: the
Designs window opens on the tab kept with the empire's options (`designsTab`, save format
10, carried in the original's saves with Hide Obsolete and Stats\Strategy), lights its
buttons as the original and lists on its enemy tabs every design seen of the living empires;
Create Design reorders the design itself under Condensed View, picks the size, the component
group and the mount in the original's list windows, refuses a design with warnings or a name
in use in its "Design Warnings" box, ignores components before a size, offers every line of
the name file and keeps its caption when editing; Research draws queued areas green with
their cost, its groups in alphabetical order, and opens an area's report on a right-click;
the Tech Tree lists every area in the original's columns and exports `TechAreas.txt` and
`TechLevels.txt`; the Weapons Report lists the weapons whatever the mount, offers the hull's
mounts and starts on Normal Mount; an intelligence project asks for its targets in the
original's list windows and keeps them, each queued project with its own progress. Choices
the executable did not settle are spec 06 §7 Q113. What stays different on purpose:

| Where | Client now | Original | Impact |
|---|---|---|---|
| A tech area's report (`techDetail`, `levelUnlocksFrom`) | Under "Level N brings", "Later levels bring": what each later level up to the area's maximum makes available, a line per level (GitHub issue #11) | Not known to list the later levels (spec 06 §1.11) | L (an addition) |
| Create Design's height (`designs.cpp`, `DialogSize::LargeTall`) | At 1024x768 as tall as the list windows (668 px), the height it gains in the Warnings and Component Details boxes, 269 px tall (GitHub issue #14); 780x475 at 800x600 | 780x475 in both layouts, the boxes 76 px tall (spec 06 §7 Q94) | L (cosmetic) |
| Create Design's name (`designs.cpp`, `cmd::CreateDesign`, `cmd::EditDesign`) | "Design Name" is a hint in an empty box, and an empty name is refused in the "Design Warnings" box | The box holds the words, so a design made without typing is named "Design Name", and an erased name is accepted as empty (spec 03 §4.1) | L |

Seen side by side with the running original on 2026-10-03 ([spec 07](spec/07-observations.md),
session 5, which gives each original layout in full): the setup screens, the front end,
Help, the Galaxy Map, Intelligence, Combat Replay and Ground Combat.

Since 2026-10-03 the setup screens and the front end follow session 5: Game Setup and Empire
Setup in the setup frame with every page's boxes (`screens/setup.cpp`, `setup_empire.cpp`,
`setup_widgets.cpp`), Game Setup opening on Players with an empty list and Add New starting
an empty empire with the Email box and the 340×370 list pickers; the new-game defaults
(random computer and neutral players at Medium with one count choice, Maximum Event Severity
Catastrophic, the victory values) and Quick Start keeping them, its opponents rolled from the
seed (`setup::quickStartGame`); Quick Start's picker in Settings.txt's style order, eight to a
page, column by column, with the empire's name as written; one Load Game dialog for the intro,
the Game Menu's Load and Delete and Add Existing's Load Empire (`screens/file_dialog.cpp`);
the intro's "Loading:" and "Complete". Since the REA audit of 2026-10-11 they also take the
original's spin steps and ranges (Victory Conditions, whose years are kept in turns, the
unit and ship caps, 1 % a click for the characteristics), the characteristics' level words
and effect labels (also in the Race Report), Create Empire's five checks in order, the
Culture Modifiers Comparison window and Change Directory's Select Directory folder browser
(spec 01 §2.2, §11, spec 02 §9, spec 06 §1.1). What still differs, and OpenSE4's own
additions:

- **Ours, kept** (impact none): the intro's Multiplayer, Settings and Manual buttons at the top
  right; a status line under the setup frame (errors, a summary); Game Setup's Seed box,
  system details under the pointer and map facts on Quadrant, the list of possible events on
  Events, Allow All and Remove All on Technology, Move Up and Move Down and a "Computer" mark
  on Players, Restore Defaults on Mechanics; Empire Setup's Preset Build and Race Name
  (General), an enabled Computer Controlled box (a listed empire may be a computer player),
  the homeworld's picture (Environment), Preset Values and All 100% (Characteristics), why a
  trait cannot be taken (Advanced Traits); Quick Start's double click and the yellow frame on
  the chosen portrait; `--quick-start --empires=N` and a lesson's `computer_players`, which
  give N − 1 (or that many) computer players and no neutral empire.
- **Dim in ours** (impact L): Game Settings' Game Master Password and Cheat codes allowed (no
  such options for a game on one computer; network games set a master password in
  Multiplayer); Mechanics' Different Machines, Multiplayer
  Game Filename, Save Game Directory Path and Connection Type (network and e-mail games are set
  up from Multiplayer).
- **Our choices where the observation is silent** (impact L): typing into the spin controls'
  value boxes (kept in the arrows' range; the original reads a typed unit or ship cap
  unchecked); Create Empire's messages in our words; the current culture in yellow in the
  Culture Modifiers Comparison, and its legend's places; Select Directory's places, its
  Drive box (the root outside Windows), a folder typed into Current Directory and the
  Default button; Begin Game without a chosen portrait asks for one; the pictures under the
  page buttons come from the seed; the lists use the shared arrow column.
- **The command line and the network game's sounds** (impact L, since 2026-10-11; the
  command line since 2026-10-14): the program reads the original's
  `<game file> <password> [<player number> [<mod folder>]]` (spec 05 §9.2). The fourth
  argument names a mod folder, which OpenSE4 plays as a classic mod for the run, since it
  reads the game's data from mods and never from `Path.txt`'s folder. Hosting takes
  OpenSE4's play-by-e-mail game file and processes it as `opense4-server pbem process` does:
  no question for a missing orders file (the computer plays that empire), and no hosting of
  a local game, a turn file or the original's saved game, whose `.plr` files OpenSE4 does
  not read. Signing in takes a turn file, an OpenSE4 saved game or the original's saved
  game; for OpenSE4's files the data check is the data set's identity and the mods, and the
  original's Quick Start files, whose checksums are 0, are not compared. A password set
  with capitals must be typed with them (OpenSE4 keeps only a hash). The not-found case,
  the boxes `-nd` suppresses and turn-based games are our choices (spec 05 question 84).
  The original's TCP/IP sounds
  play in OpenSE4's network games and lobby (spec 06 §5.5); its host generates the turn by
  itself once every order is in, so it has no alert for being ready to generate one.

Since 2026-10-03 the battle windows follow session 5 and spec 06 §7 Q86, Q87, Q97 and Q98:
Tactical Combat's places, panels, weapon cells and title-strip buttons, the Combat Piece
Report's pages, Combat Replay in the Tactical Combat frame, Combat Replay Options' headings
and check boxes (and Combat Options' alike, inferred), Ground Combat's grids and side marks,
the movement line during the movement log replay with sector 0 selected after it, and the
report's up-arrow. Their remaining choices are noted in spec 06 beside each answer; the
lists' 24 px arrow column is described above. OpenSE4's additions there, which
change nothing in the original's layout:

- **Combat Replay's events and summary** (spec 06 §7 Q39): an option of Combat Replay Options
  under an "OpenSE4" heading, off by default and kept per computer, that lists each combat
  turn's events in words, and the battle's summary at the end, in the replay's empty weapon
  grid.
- **The battle maps' view**: the wheel zooms and the middle button drags the Tactical Combat
  and Combat Replay maps; the replay's view opens on the player's pieces; hovering a weapon
  cell shows the weapon's figures.
- **`--open=ground-combat`**: a sample strategic simulation in which the player's troop
  transports land on its homeworld, to reach Ground Combat headless.

Since 2026-10-04 the client answers players' reports on v0.8.1:

- **Every window and question is modal** (spec 06 §1, §3.4): while one is open the main
  window takes no input at all (command buttons and their hover hints, order strip,
  selectors, report panel, map clicks and keys), only the window in front takes input, and
  End Turn is never carried out during a battle. Before, the command buttons stayed live,
  so End Turn could be pressed under the ship designer or a battle. The tutorial lock and
  its way back follow (docs/LEARNING.md "The input lock").
- **The Race Report** is drawn like every report opened on its own (spec 06 §1.4,
  §1.10.1): 310x420, the four tabs, a Close button; modal over Empires and closed with it.
- **The system report** follows its places (spec 06 §1.4) and wraps every line; a black
  hole's abilities no longer run out of the panel, and what does not fit scrolls.
- **No stray lines under warp points and asteroid fields**: each part cut from a sheet is a
  texture of its own, so a frame scaled by a fraction no longer smooths in the next cell's
  edge (spec 06 §2.4).
- **Text size** enlarges reading text and OpenSE4's own; the classic layouts' fixed places
  keep the original's raster sizes, and text that does not fit its box is cut short with an
  ellipsis and a tooltip (spec 06 §5.4). `assert-fits` checks it in input scripts.

Players' reports on 0.8.1, settled from the executable on 2026-10-04 and checked in a running
game where one could show it (spec 07 session 6; spec 06 §7 Q100–Q109), were implemented the
same day:

- **Colonize, Warp, Drop Cargo and a pursuing Attack** gather the candidates of the clicked
  sector and ask with the Pick Object window when there are several (spec 06 §2.9, Q100).
- **Log pictures**: every entry has its picture, drawn unframed with the title to its right; a
  developed item shows its details (spec 06 §4.1, Q101).
- **The view follows** the player's own turn-based moves through warp points, and at the
  turn's start selects each object with orders in turn (spec 06 §2.7, Q102); not yet for a
  network client.
- **Another system shown** keeps the selection, report, tab and tags; a right-click in the
  system panel does nothing but OpenSE4's optional Move To (spec 06 §2.4–§2.6, Q103).
- **Right-clicks**: Set Construction Queue's owner box opens its report (Q104); facilities on
  Facil and components on Comps open theirs (Q105), the pages still drawn as rows.
- **The Ability page** lists one `Descr` line per entry, with the planet's or the ship's
  racial, cultural, population and mood lines, never facilities or components (Q106).
- **Report tabs** open on Detail whenever a report is filled (Q107).
- **Sector marks**: only the shown planet carries the colony box or the star (Q108).
- **Only Latest** keeps the last of each run of neighbouring same-family items, and its boxes
  are the Empire Options rows (spec 02 §6.4, Q109).

Settled on 2026-10-04 with spec 03 §19 Q80:

- **Colonize candidates** (`MainWindow::pickCandidates`, `colonizeCandidates`): every planet
  of the clicked sector of an explored system, hidden ones included and listed by name; one
  hidden planet alone is taken without a window (spec 06 §2.9, spec 03 §8). Implemented on
  2026-10-04.

Players' reports on 0.9.0, settled from the executable on 2026-10-05 (spec 06 §7 Q110), were
implemented the same day:

- **A fleet in the sector's list and the Fleet Report** (`sector_view.cpp` `sectorListRows`,
  `main_window.cpp`, `reports.cpp` `fleetReport`, `cmd::SetFleetLeader`): one row per own
  fleet (its picture, name and status icons), the rows in the original's order; the Fleet
  Report alone; a left-click on a member makes it the leader, a right-click opens its Ship
  Report popup (spec 06 §2.5). A member selected any other way shows the Fleet Report too
  (inferred, spec 06 §7 Q111).

What remains, in network and e-mail games only:

| Where | Client now | Original | Impact |
|---|---|---|---|
| Colonize candidates in a network or e-mail game | A foreign colony whose planet the player does not see (its cloak, a storm, a nebula) is not in the player's view at all (`game::redactForEmpire`, spec 05 §9.5), so it is no candidate; an uncolonized hidden planet is | Every player holds the whole game (spec 05 §9.2), so such a planet is listed like any other | L; keeping the planet in the view without its colony would show it as an uncolonized planet the panel draws, so matching needs the view to say which planets are hidden, a protocol change of its own |

The 2026-10-11 audit's diplomacy windows (spec 06 §1.5, §4.2, §4.3) follow the original since
2026-10-14: Communicate's View Trade, View Gift and View Tribute, Edit Package and Edit Trade,
its message types and refusals, the text from the race's speech pools, the answers offered
when it is opened from a portrait and the portrait that does nothing once a message went out;
Select Package's lists and the dim Technology tab; the Empires window's Treaty, Trade and Tariff
columns (roles, Trade N%, every empire's mood, the status line, Trade Amount, Tariff Received or
Given) and its tab kept with the empire's options (`InterfaceOptions::empiresTab`, save format
10, imported and exported); the Treaty Grid's cells, codes, role marks and legend; Borders'
Allies and Enemies by Non-Aggression; Scores' figures, ordinal dense ranks and columns; the
Comparisons graph from the statistics record; Victory Conditions' marks and columns; History's
list on the left with Other Events and its lines oldest first under decade headings. What
remains different:

| Where | Client now | Original | Impact |
|---|---|---|---|
| Communicate's pickers (`screens/communicate.cpp`) | The type, treaty, empire, system and planet are drop-down lists in the window; a package type opens Select Package at once, which returns with Done | Choosing a type opens its pickers at once; cancelling one returns to Start Again | L (cosmetic) |
| Communicate's demands and requests | Listed flat with the other types | Behind a "Demand / Request / Warn" entry that switches the list to its 16 types | L (cosmetic) |
| Communicate's answers for a message opened from the Log's Send Reply | OpenSE4's Accept, Refuse, Counter and Reply buttons, the first three lit only for the newest recent message (as the portrait's) | Answers only from the portrait | L (OpenSE4's addition, kept) |
| Select Package's resources | Typed in, in steps of 1,000 | x10000 and x100000 toggles | L (OpenSE4's, kept) |
| Select Package's Comm Channels on the side asked for | The empires the recipient has met and we have not (inferred, spec 05 Q80) | Not described | L |
| History's lists | Our empire, every empire we know and every empire the record names, each with OpenSE4's founding line and statistics lines; a galaxy map under the list | The empires that appear in the history file | L (OpenSE4's additions, kept) |
| The Empires window's extra lines | After the original's: race, player, since, last war, anger value, "Inbox: N waiting"; trade of the maximum, what is shared; who pays whom | The original's lines only | None (OpenSE4's additions, kept) |

Settled from the executable on 2026-10-11 (the REA parity audit) and implemented on
2026-10-13:

- **Stellar Manipulation** (`screens/stellar.cpp`, spec 06 §1.3, spec 01 §9): a button is
  lit by the vehicle's own ability alone; Construct asks for the world's size
  (`Order::planetSize`), then Confirm Action asks while the empire option says so, and only
  then is the target chosen: none, the Galaxy Map's destination mode for Open Warp Point, or
  the object of the vehicle's sector, with Pick Object when there are several and a "Cannot
  Perform Action" box when there is none. The order is appended to the vehicle's own list
  (`cmd::SetOrders::ownList`), never its fleet's; a simultaneous game's window then closes,
  and a turn-based game carries the list out at once. Before, a button was lit only when
  the order's checks passed, the target was found at once, the order went to the front of
  the fleet's list and the window stayed open.
- **Galaxy Map** (`screens/galaxy_map.cpp`, spec 06 §2.6): the destination mode for Open Warp
  Point, which replaces OpenSE4's earlier pick of a sector of the destination system in the
  main window; Show Names and the destination mode's Show Distances kept with the empire's
  options (`InterfaceOptions::galaxyMapNames`, `galaxyMapDistances`; save format 10, read from
  and written to the original's saved games); a click anywhere opening the Notes window of
  the nearest system, explored or not, which names an unexplored one "Unexplored System"
  (the inline editor showed its real name, against spec 01 §6.1); hovering anywhere showing
  the nearest system's notes.

What remains:

| Where | Client now | Original | Impact |
|---|---|---|---|
| Stellar Manipulation's targets and Construct's sizes (`shipui::stellarCandidates`, `constructSizes`) | Every object of the kind in the vehicle's sector, seen or not, as Colonize's pick lists them; the size picker always shown, its sizes in PlanetSize.txt order | Not described (spec 06 §7 Q114) | L |
| Stellar Construction in the original's saved games (`classic_save_*`) | The order's size is neither read nor written; an imported order builds the size its device names | The record carries the size, in a place not known (spec 08 §11.1 Q7) | L |

The REA parity audit of the main window (2026-10-14, spec 06 §2.3, §2.4, §2.8, §3, §7 Q99(d),
Q115–Q118; spec 03 §8, §8.1) brought the client and engine in line with the executable:

- **One ship cycle** for Space, Ctrl+N, Ctrl+B and the top selector (`cycleShip`): own ships
  and bases that can move, not mothballed, with movement left (turn-based) or no orders
  (simultaneous), never one whose first order is Sentry; then the Empire Options' filters,
  "Stop once per location" with its table of places.
- **Location keys**: Alt+1..9 and Alt+0 (waypoint 10), Ctrl+T and Ctrl+R ask for a sector and
  the next click gives it, with nothing selected and in any system; waypoints are numbered 1
  to 10 everywhere (keys, map labels, default names, lists), as the original's saved games
  number them; deleting a waypoint resets the queues sending ships there.
- **Orders**: Set Patrol adds each point as it is clicked and ends on the object's own sector
  with Repeat on (a lone order cleared); Sentry pressed on a list that starts with one clears
  it; switching Repeat restarts the list at its first order (`repeatShift`, save format 10);
  Cloak and Decloak at once (`cmd::CloakVehicles`); Toggle Minister Control on a fleet
  (`cmd::SetMinister::fleet`); Change Name refuses a name any space object has, with the
  Invalid Name box; Explore, Resupply and Repair search from where the object is and add the
  Move To even there; Colonize loads colonists when the object's own cargo has none;
  Formation and Strategy as two pickers.
- **Clicks and lit buttons**: a click on a sector of a system not explored does nothing
  without a pick; Decloak is lit for anything cloaked; a fleet's speed, Colonize, Cloak and
  Decloak count its members at its location.
- **Windows**: Ships\Units' fleet rows (members at the location, formation, blank cells), a
  unit group's designs and the cargo one per line, the order list in place with the current
  order bracketed only with Repeat on, the maintenance total summed live, a fleet row's click
  selecting its first member there; Waypoints' "1 - name" rows, an empty Rename prompt, the
  ships en route by Move To Waypoint only, one by one, with a right-click report; View
  Orders' "(Repeat Orders)" row.
- **Changed objects** (Q99(d), `ClassicSession::markChanged`): the objects whose minister
  flags a player's copy carries are those the original marks: every order run for them,
  and the commands that change their orders, fleet, name, flag, cloak, queue, colony type or
  cargo.

What remains different:

| Where | Client now | Original | Impact |
|---|---|---|---|
| Change Name on the unchanged name | Taken as no change | Refused as a name in use, the prompt asked again | none (a quirk not copied) |
| Held keys (spec 06 §3) | Only the selection cycles and the movement log's single step repeat | Every main-window key repeats while held (inferred) | L |
| Set Patrol's end | Also Enter or a right click (OpenSE4's), Repeat on, a lone order kept as a plain move | Only a click on the object's own sector | none (an addition) |
| View Orders | Editable (Move Up, Move Down, Delete Order, Clear Orders, Repeat), the current order first | Read-only, the list in place | none (an addition) |
| Opening Set Construction Queue from Construction Queues | Marks the object changed only when something is changed there | Marks it as the window opens | L (which minister flags a switched player's copy carries) |
| Ships\Units' fleet rows | Move and Supplies over the members at the location (ours) | Not described | none |

## Computer player (spec 05 §7)

The four AI_Settings movement flags become the computer empire's own Ship Movement and
Ship Orders options each turn (`Empire::avoidTaggedMinefields`, `avoidRestrictedSystems`,
`clearOrdersOnEncounter`), and routes follow those options (spec 03 §6.2; the engine's
choices are spec 03 §19 Q58). Empire Setup sets the minister style and "Use Race Minister
Style" (`EmpireSetup::ministerStyle`, `useRaceMinisterStyle`), which the empire keeps
whether a human plays it or it is marked Computer Controlled (spec 02 §13 Q50).

The rows found on 2026-09-30 were implemented that day (`ai*.cpp`): the construction
backlog in whole turns per item, the date the ministers see (`ai::aiDate`), the units
file, transports, the random race's environment and traits, planet launches and the
layers, the Research minister's gate and mine sweeping, facility upgrades, the budget,
Attacking and Defending by the current player (`CombatRecord::currentPlayer`), the
strength rating in tenths, every jump count over all links, candidate values, the defend
list, the 4-jump test, design names (`Design::templateName`) and design typing, Allow
Surrender (`GameOptions::allowSurrender`, also a check box of the Game Settings page and the
server's `allow_surrender` key), colonization danger, the one-per-system list,
the Repair, Resupply, Space Yard Ship and Stellar Manipulation ministers, trade item
values, the replies and speech pools, and template entries of the vehicle list. Queued
facilities switched by an upgrade change in place and keep what was paid
(`cmd::QueueReplaceFacility`); a war declaration whose speech pool is empty declares
nothing but still sets the anger to 100 (`cmd::DecideWar`); a new design's fallback
strategy follows the design type as §7.5 lists it, so transports, colony ships and the
other unarmed types get Don't Get Hurt. Since 2026-10-14 a computer player starts with its
`AI_Strategies` table's strategies, and with `DefaultStrategies.txt` only when the table gives
none (spec 05 §7.7, complete listing); before, it had the data set's list with the table's
strategies added after it.

On 2026-09-30 the engine's own choices (spec 05 open question 37) were answered from the
executable, with the demand lists (questions 47 and 49). The rows found then were implemented
on 2026-10-01:

- The Politics minister answers only the newest message from an empire in its date window
  (`DiplomaticMessage::dated`: the unadvanced date for players' messages, the advanced one for
  those sent in a simultaneous start-of-turn step), keeps no answered mark, tests "waiting"
  on any message dated after the date − 2, runs the initiative again after a 50 % initiative
  that sent nothing, and rolls "wants war" and "wants to break" at every check.
- The demand lists keep duplicates as counts per empire (war, break, peace, promises); each
  check that gets far enough uses one up (`cmd::UseDemandEntry`); all, with the systems to
  avoid or attack, are emptied at the start of turns whose date is a multiple of 10, before
  the Politics minister. A promise names the empire the demand names (Q47, Q49).
- An accepted demand is carried out with its 50 % chance before the reply, even when the
  reply's pool is empty (`cmd::CarryOutDemand`). A request for a gift or tribute takes
  concrete items it cannot hand over, refuses with the General reply when its package is
  empty, and never reads the gifts option; accepted gifts move their items whatever it says.
- Transports deliver only more than half full with people aboard, never fall back to
  loading, deliver after a load only from their own sector, move only toward a target in
  another system and otherwise get the resupply orders.
- The units reserve is the value the nearest earlier empire's units step left
  (`TurnContext::unitReserve`, `ai::unitReserveLeft`).
- "Lacks a part to operate" means no working bridge, auxiliary control or Master Computer;
  the Repair minister handles mothballed vehicles, visits yards by system then object order
  and counts a vehicle's own yard; a fleet's supply totals leave out members with unlimited
  supply; Space Yard Ships count themselves as a yard and are planned in fleets too.
- Open Warp Point gets the resupply orders while a frontier point is free or no edge sector
  is drawn (99 draws, corners twice as likely, vehicles fill a sector); Destroy Black Hole and
  Destroy Nebulae seek sector 36, and Close Warp Point uses our presence (fighter, satellite
  and drone groups included, mines not) and the Sentry detection test.
- Mine and satellite layers add up the weights, cap only their own kind, evaluate the
  star-destroyer flag on dates that are multiples of 20 (our own designs that fought count,
  `AiMemory::designsFought`) and draw a quiet colony system first in the fallback.

OpenSE4 choices: a design name when every name is used (question 37); the one-turn Seek of
Destroy Black Hole and Destroy Nebulae ships, which the engine gave as a Move To to sector 36
that the minister planned again every turn, is a real Seek since 2026-10-02. The record of our designs that fought
lists the first design of a group that mixes designs. The "(inferred)" markers of question
37 in `ai*.cpp` are gone (the operating part, mothballed repairs, the yard ship's own yard,
the unlimited-supply test, the warp-point draws and the top-left sector, Close Warp Point's
sight, the layers' weights, cap and flag, the empty transport, the answer window, the
waiting test, the units reserve). No row from that check remains; the rows found later are
below.

**Observed pace** (2026-10-01, [spec 07](spec/07-observations.md) session 3): three
original games against twelve of ours, Small quadrant, five empires, simultaneous, 100
turns. With the starting assets following the spec, the early game agrees (colonies 1.5
against 1.7 at turn 10 and 5.3 against 5.3 at turn 25; homeworlds at 2000M through turn 6;
first ships after turn 2 or 3; the homeworld's Happy bonus lost in the statistics of turn 4
for most empires).

**Observed under a debugger** (2026-10-02, spec 07 "Pace observed under a debugger" and
"Resources, ships and colony losses under a debugger"): five original games in which all
five empires are computer players, each empire's AI state, lists, colonies, production,
happiness and ships read every turn, against 24 of ours with the same set-up and records.
The first three games sat at the favourable end of the original's spread; the rows give all
five. Research per colony agrees in this set-up (1,031 / 1,021 at turns 50 / 100 against our
1,123 / 1,089), and so do the colony types, population per colony and tech levels (35.6
against 34.8 at turn 100): the research lead measured in session 3 came from its set-up and
spread (spec 05 question 59); what is left of it (12.1k / 15.5k against 10.7k / 14.6k)
follows the colony count. What differs:

| Where | Engine now | Original (observed) | Impact |
|---|---|---|---|
| Time in Defend (Short Term) and Infrastructure (spec 05 questions 53, 75). Within the original's spread in every period once nine original games are counted | Defend (Short Term) 47 % of all turns, 67 % of turns 51–100 (240 seeds, after the rules of 2026-10-03 below; 48 and 70 % before them); Infrastructure 7 % and 7 %; Exploration 79 % of turns 1–25 | 48 % and 70 % (49–94 % per game); Infrastructure 9 % and 11 % (0–30 %); Exploration 61–95 % per game, 80 % over nine games | L |
| Bases. The placement rule is the original's (all 174 placements observed in five games went to the K-th queue, 25 reached a yard; ours 21 % of 742) and every placement is made in Infrastructure; since 2026-10-02 the soft cap, the scrap candidates and their ties are the original's too (below). With the construction budget's queue commitments fewer turns are over the soft cap, so fewer bases are scrapped, but fewer are placed early | 0.30 / 0.37 / 0.35 per empire at turns 50 / 75 / 100 (seeds 1–24; 0.29 / 0.33 / 0.34 over 120 seeds); built 0.51, lost 0.15 per empire in 100 turns | 0.4–0.6 in five games, 1.2 and 0.0 in two more (game 6: built 1.4, lost 0.2) | L |
| Resources from turn 50 (spec 05 question 61). The production rule matches (13,100 colony outputs observed); part of the gap is the original's lucky race draws, the rest its extra colonies, built from more colony ships in the first 50 turns (question 65) | Resources produced 22.8k / 29.0k / 32.4k at turns 50 / 75 / 100; colonies 11.2 / 14.5 / 15.7 (240 seeds, after the rules of 2026-10-03 below; 22.6k / 29.4k / 33.2k and 11.2 / 15.0 / 16.6 before them: the Defend (Short Term) fleet rule loses more colonies, question 76) | 25.6k / 34.5k / 36.9k (16–22 % above ours with the same race line-ups); 12.2 / 16.6 / 17.0 | M |
| Ships from turn 75 (spec 05 question 62). Both sides build as many attack ships; since the Defend (Short Term) fleet rule of 2026-10-03 ours fight as many decided battles away from colonies as the original (3.8 won per empire and 25 turns of turns 51–100, the original 2.1–4.8) and lose 4.4 attack ships in battle (3.8–8.0 in four games), and they keep more ships; the gap at turn 100 is within the original's spread | Ships 3.4 / 8.0 / 13.2 / 16.4 per empire at turns 25 / 50 / 75 / 100 (240 seeds, after the rules of 2026-10-03; 3.2 / 7.7 / 12.4 / 15.6 before them) | 3.5 / 8.6 / 14.1 / 17.4 (6.6–11.8 per game at turn 50) | L |
| Colonies changing hands (spec 05 questions 63, 67, 69, 76, 77). In the original every colony lost goes in a battle at its planet (none captured, none destroyed) and nearly every colony gained is founded on a free planet; a battle against a weak colony goes the same way in both. Since the Defend (Short Term) fleet rule of 2026-10-03 ours send one defence fleet to each defend-list entry, as the original does; what is left is the number of enemy colonies inside the territories (question 77) and the colonies held at turn 100 (question 76) | Per empire and 25 turns of turns 51–100 (240 seeds): founded or taken 4.1, lost 1.9; battles at an enemy colony 2.6, ending with the colony gone 1.8 (before the rules of 2026-10-03: 3.8, 1.2; 2.2, 1.1); colonies at turn 100 15.7 | Founded 2.8–6.3 (4.5), lost 1.7–3.1 (six games); battles 3.0–4.9, colony gone 1.9–2.9 (four games); 17.0 | M |

The rows found on 2026-10-02 (spec 05 §7.2, §7.5, question 60; confirmed: binary) were
implemented that day; spec 07 "Pace after the movement rules" measures them:

- **One-turn movement orders.** Every movement the ministers order (defence, attack, fleet
  goals, exploration, patrol, repair, Space Yard Ships, the Destroy Black Hole and Nebulae
  ships) is a Seek (`OrderKind::Seek`, given only by ministers) toward a sector or after a
  ship or planet: it lasts the movement phase in a simultaneous game, waiting at its goal,
  and goes after day 30; in a turn-based game it is done once it arrives or the run ends. A
  ship already on its target's sector gets the stored Attack (an Attack naming no target),
  carried out at once and done. So the warships are idle again at each start of turn.
- **Join Fleet pursuit** (`OrderKind::JoinFleet`): a recruit chases its fleet's position,
  joins where the fleet stands and counts toward its size from the moment it is ordered.
- **Attack candidates** are kept only when we could settle a planet of their kind or their
  owner is below None, and a kept one counts its planet a second time in its owner's
  strength there.
- **Exploration** (`planExploration`): the explorers, their point list and the Seek-then-Warp
  order of spec 05 §7.5. As the rule says, ships whose first order is a Seek explore too, so
  the Exploration minister takes some of the ships the Defense and Attack ministers ordered
  that turn.
- **Territory claims** are rewritten first thing in the Politics minister's run, so a system
  received in a trade it accepts stays claimed until the next rewrite; the ministers after
  it plan with the claims the state update used.
- **The economy step's lists** (`TurnContext::aiColonyTargets`): the first empire whose
  economy-step ministers run plans with the colonization targets of the last empire's
  start-of-turn step.
- **Queue list details** (question 60): a cloaked yard ship's yard does not count toward K;
  a system's queues follow the system's own object list (`Vehicle::arrival`, a new saved
  field with `GameState::arrivals`: the save format changes with them); the unit queue choice
  compares every unit in each colony's cargo.

OpenSE4 choices where the text leaves room are spec 05 question 64: a turn-based Seek's
end, the Exploration minister's stop when no point is free, an explorer on its point, the
movement a turn-based explorer compares, the fleets' Seek and Warp when they explore (a Move
To and the Warp since 2026-10-03, confirmed), the
borrowed targets' settle test, and where a planet made during play goes on its system's
list.

On 2026-10-01 spec 05 questions 53–56 were settled from the executable and the captures.
The rows found then were implemented that day (`ai.cpp`, `ai_anger.cpp`, `ai_economy.cpp`):
the exploration frontier holds only warp points into unexplored systems, whatever we know of
the link, for the state machine, the explorers, the Not Connected test and the Open Warp
Point gate; the territory is the set of claimed systems, which the Politics minister
rewrites (`ai::claimTerritory`; since 2026-10-02 first thing in its run), so the state
update and the ministers use the claims of the previous turn (and, since 2026-10-14, the
systems colonized since, which the Empire Options switch claims at founding, spec 06 §7
Q47), and its exclusions (another
computer player's home system, avoided systems) apply only to neighbour systems; a human
whose Politics minister is on claims by the same rule; colonization danger adds 1 per
non-friendly empire beyond each warp point; a Defense Base goes to the K-th queue of the
empire's queue list, or a random one, with the backlog test on that queue alone, and a base
sent to a colony without a yard is queued there and dropped by the construction step, as in
the original (since 2026-10-13; it used to be counted and paid for but never queued); mines, satellites, weapon platforms and fighters take the first
colony queue by backlog, free cargo, units of that kind held, planet size, production and
rate, and nothing is placed without room for the batch; no item tries a second queue;
facility upgrades come first on every fifth turn; the research and intelligence lists and the
finite-resource block follow §7.5; the best facility for an ability is the highest Value 1
for amount-type abilities, else the highest tech-requirement sum, the later on a tie.
OpenSE4 choices (spec 05 question 60, answered from the executable on 2026-10-02): the
ministers after Politics read the territory only through the lists, and a queued base item
counts its count, as in the original; a system's queues follow the game's object order, a
ship's yard works while its component is intact and not mothballed, and units "of that kind"
share the vehicle type, which differ (the queue list details above, implemented on
2026-10-02). The golden games of `tests/test_determinism.cpp` moved to seed 39 then, to
seed 19 on 2026-10-02, to seed 42 later that day (the varied battles to seed 19, whose
invasion still lands), and to seed 35 with the budget rule, whose games still cover
battles, events, intelligence and politics. The simultaneous golden game moved to seed 39
with the placement spread of spec 01 §3.6 (2026-10-03): seed 35 placed the homes closer and
fought no battle. It moved to seed 38 with the Colonize sight test (2026-10-04): seed 39
fought four battles, below the five the coverage asks for.

Found under a debugger on 2026-10-02 (spec 05 §7.2, §7.5, questions 65–71; spec 07
"Battles, bases and the first turns under a debugger"; confirmed: binary) and implemented
the same day; spec 07 "Pace after the scrap, cap and fleet rules" measures them (120 seeds,
paired):

- **Soft and hard caps without colony ships** (`capMaintenance`, `Planner::overCap`,
  `ai.cpp`). The test sums the maintenance of the empire's ships and bases of the moment,
  leaving out every vehicle whose hull has `Requirement Pct Colony Mods` above 0; ours used
  to compare the maintenance last paid, colony ships included. Turns 26–100 over the soft
  cap: 21 % before, 12 % with all the rules (the original 9–11 %); 4 % of turns 26–50
  against the original's none is question 71.
- **Scrap candidates** (`planScrap`, `ai_military.cpp`). The oldest design among all the
  empire's non-colony-ship vehicles that can move, wherever they are, and its bases at a
  yard; a mobile one elsewhere gets the Move To the nearest yard and the Scrap in one
  command (`cmd::Scrap::moveFirst`). Ours used to take only ships already at a yard, so
  bases went first. Bases at turn 100: 0.14 before, 0.36 after. The engine's own choices
  are spec 05 question 72.
- **Fleet leaders** (`canLeadFleet`, `ai_military.cpp`). A new fleet forms only around a
  ship that can move and that an attack or defence fleet could take, never a troop
  transport or a boarding ship (carriers and drone carriers only with their units aboard).
  Fleets of troop transports alone fell from 6 % of the fleet-turns of turns 51–100 to
  none; fleets of carriers alone stay at 5 % (a carrier with fighters aboard may lead).
- **Defend list colony threat** (`assess`, `ai.cpp`). A colony adds the ratings of the
  other objects in its sector but nothing for itself; ours added 1. 95 of 120 games played
  out identically.

With these and the combat step (Combat above), battles of turns 51–100 fell from 12.1 to
10.2 per empire and 25 turns and their drawn share from 47 to 37 %; decided battles away
from colonies stay at 4.8 against the original's 2.1–3.4, and attack ships lost in battle at
6.0 against 4.2–4.9 (spec 05 question 68).

Found later on 2026-10-02 (spec 02 §4, confirmed: binary) and implemented the same day: the
original saves no happiness event that waits for the next update, and a loaded game starts
with none, so a game hosted turn by turn never counts the `Ship Constructed` and `Facility
Constructed` events of the construction step, nor any other event logged after the
empire's update. `GameState::pendingMood` is now kept in memory only, outside the save
format, network messages and checksums. A play-by-e-mail host loads the game every turn and
a local or hotseat simultaneous game drops them where it stands for the original's reading
of the game file before processing (`ClassicSession::reloadGame`); the network host keeps
the game in memory between turns, as the original's TCP/IP host does (spec 05 §9.4), so its
games count them, as a turn-based game on one machine does.

Found later on 2026-10-02 (spec 05 §7.5 *Scrap* "Ties" and *Net income*, question 71
answered; confirmed: binary) and implemented the same day; spec 07 "Pace after the
tie-break and budget rules" measures them (120 seeds, paired):

- **Scrap ties** (`scrapOldest`, `ai_military.cpp`). Creation dates are whole turns and
  are compared strictly, so the first candidate met wins, and the candidates are met in
  slot order (`objectOrderKey`), not in vehicle-id order. 116 of 120 games played out
  differently, none of the measures moved.
- **The construction budget's queue commitments** (`Planner::netIncome`,
  `queueCommitments`, `ai.cpp`). Net income is the revenue less the maintenance of the
  vehicles of the moment and less what each colony queue will spend this turn on its first
  item (its cost less what was paid, at most the queue's rate; yard ships' queues not
  counted); the facility upgrades spend the figure of the start-of-turn step
  (`TurnContext::aiStartNet`, now `aiStartFigures`). The engine's own choices were spec 05 question 73. Turns over
  the soft cap fell from 3.5 to 1.2 % of turns 26–50 (the original none) and from 16 to
  11 % of turns 51–100 (the original 14 and 16 % in two games, at our upper quartile per
  game); colonies at turn 100 rose from 15.4 to 16.4 and resources from 30.8k to 33.2k
  (the original 17.0 and 36.9k).

Found under a debugger on 2026-10-03 (spec 05 §7.2, §7.5, spec 03 §6.3, spec 04 §2,
questions 68–73; spec 07 "The computer players' second round under a debugger"; confirmed:
binary) and implemented the same day; spec 07 "Pace after the second debugger round"
measures them (240 seeds, paired, per empire and 25 turns of turns 51–100 unless marked):

- **Fleets in Defend (Short Term)** (`planFleets`, `ai_military.cpp`). With enemies listed,
  each defend-list entry of the systems to defend, in the fleets' order, gets the nearest
  idle defence fleet with members at its location (one entry per fleet, no limit by the
  threat); then the minister gives no other fleet orders that turn, so attack fleets and
  leftover fleets stay idle. It used to take the raw enemy objects and then send every
  leftover fleet, attack fleets included, to the top entries, a patrol or an exploration.
- **The vehicle table's counts** (`ShipBuilder::takeCounts`, `ai_economy.cpp`). Taken once
  before the clean-up of obsolete items, so an item removed this turn still counts: the
  second attack ship comes on turn 9 (median) instead of 5 and the first colony ship on
  turn 7 instead of 9, as in the original; 1.1 attack ships per empire at turn 5 (the
  original 1.05).
- **Idle vehicles in the daily battle check** (`Mover::run`, `movement.cpp`). A vehicle with
  movement acts on its counter's days with or without orders and marks its sector.
- **Exploring fleets** (`planFleets`). The player's Move To toward the frontier point and
  the Warp, kept until done, instead of a Seek and the Warp.
- **Fleet leaders and recruits** (`planFleets`). The leader search walks the vehicle list
  back from its end in slot order, with no idle test; recruits are ships outside fleets
  without a Join Fleet order, within 1 jump, with no idle test.
- **Scrap** (`scrapOldest`, `cmd::Scrap` with `moveFirst`). The place is the nearest queue
  owner with a working yard, in the empire's queue list order (`queueList`, `workingYard`);
  the Move To (only for a candidate that can move and stands elsewhere) and the Scrap go
  straight onto the candidate's list without the Scrap window's checks, so a cloaked or
  busy candidate gets them too and the Scrap makes its test when carried out.
- **The start-of-turn figures** (`ai::startOfTurnFigures`, `TurnContext::aiStartFigures`).
  The net income is worked out first thing, before the AI state update and Politics, and
  so is the caps' revenue, afresh from the colonies (`economy::incomeReport`); the
  economy step reuses both.
- **The caps' threshold in single precision** (`aboveSingleShare`, `ai.cpp`): M / 100 and the
  product with the revenue rounded as 32-bit floats are, worked out exactly in integers
  (spec 05 §7.5 explains why it agrees with the original's float arithmetic for every
  value a game reaches).
- **Jump counts compared with numbers** (`aiJumpCount`, spec 05 §7.2 *Jumps*). The
  original's count is the jumps plus two: the strength test around the targets reaches 2
  jumps, recruiting 1, the drones the setting less 2, and the Attack minister's "more than
  one jump from the first target" in Prepare for Attack always holds, so its ships outside
  fleets go to the staging system.

Together: battles 10.3 → 8.8, decided battles won away from colonies 4.6 → 3.8 (the
original 2.1–4.8), attack ships lost in battle 5.7 → 4.4 (3.8–8.0), battles at an enemy
colony 2.2 → 2.6 (3.0–4.9) and ending with the colony gone 1.1 → 1.8 (1.9–2.9), colonies
founded or taken 3.8 → 4.1 (4.5) and lost 1.2 → 1.9 (1.7–3.6), Defend (Short Term) 70 → 67 %
of turns 51–100 (70 %), Exploration 76 → 79 % of turns 1–25 (80 %), ships at turns 50 /
100 7.7 / 15.6 → 8.0 / 16.4 (8.6 / 17.4), colonies at turn 100 16.6 → 15.7 (17.0), turns
over the soft cap 1.2 / 12.4 → 1.3 / 13.2 % of turns 26–50 / 51–100 (0–6 / 14–30 % in four
games). The rows above give what remains.

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Attack minister in Prepare for Attack (`planAttack`) | A ship outside fleets already in the staging system gets no order from the routine (inferred) | Every ship outside the staging system is sent there; the spec names nothing for those inside it | L |

The turn-based golden game of `tests/test_determinism.cpp` moved to seed 39 with these
rules (2026-10-03): seed 35 fought only four battles, below the coverage check's five.

Settled with the engine already matching: what enters the enemy-in-territory list (question
54), a colony whose row builds nothing and the colony-type tests (question 55), the state
machine's transitions (question 53).

