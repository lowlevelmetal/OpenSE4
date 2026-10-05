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
| Turn-based export before the player's turn has started (`classic_save_export.cpp`) | The current player's vehicles get the movement of the turn's start, but their continuing orders have not run | The original continues a vehicle's orders only at a turn start and never on loading, so those orders wait one turn unless the player moves the vehicles; carrying out OpenSE4's start of that player's turn before writing would match (spec 08 §9.2, observed; End Turn not running them is inferred) | L |

Since 2026-10-04 the original's saved games (spec 08) are read and written, closing the row
that said OpenSE4 could neither import nor export them: Load Game and `--load` import a
`.gam` of the original, Save Game's *Save for SE IV* and `opense4-convert` write one
(`game/classic_save.hpp`, docs/SETUP.md "Games of the original"). What a conversion still
approximates is spec 08 §7.6 and §12. An imported game, like every load in the original,
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
vehicles the movement of the turn's start. The engine keeps each colony's never-reset
counts of destroyed facilities (`Colony::destroyedFacilities`): every removal pass after
sabotage or an event takes them off again (spec 08 §11.2), and the original's saved games
carry them both ways. Battles still remove what they destroyed once, the choice of spec 04
§19.1, and OpenSE4 has no hazard damage to colonies.
The save format is 8 and the network protocol 6 since then.

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

Players' reports on 0.9.0, settled from the executable on 2026-10-05 (spec 03 §19 Q81, Q82):

| Item | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Design Upgrade (`client/classic/screens/design_tools.cpp` `upgradeEntries`, `Rules::latestComponentOfFamily`, `designs.cpp`) | Each component becomes the highest numeral available in its family, the first on a tie (so engines of numeral I or II of any line become Ion Engine III); family 0 is skipped; an entry stays when that numeral is not higher; with nothing to change the designer does not open | Each entry becomes the last component of its family in Components.txt order whose tech the owner meets, any family, numerals ignored, mount kept; the designer always opens (spec 03 §4.1). With Contra-Terrene Engine I researched, Ion Engine III becomes Contra-Terrene Engine I (observed: an Ion Engine I became Quantum Engine III, spec 07 session 7). Facility upgrades keep the highest numeral (`latestFacilityOfFamily`, spec 02 §6.6), as now | M |
| Orders to tagged vehicles in a turn-based game (`MainWindow::giveOrder`, `turn_based.cpp` `applyEach`, `movement.cpp` `Mover::build`, `EntryQuestion` and `cmd::EnterSector`, the network host's `runLive`) | One command per tagged vehicle or fleet, each carried out before the next is applied: each ship moves, is asked about the enemy sector and fights alone. The live group takes only vehicles in no fleet that hold the actor's first order | The order goes into every tagged vehicle's list, then runs at once as one group acting through the first one tagged: every tagged vehicle, fleets and own lists regardless; they step together while all have movement, are asked once and fight one battle; a completed order leaves every tagged list, a failure clears them all. What is left at the next turn's start runs vehicle by vehicle (spec 03 §8 "Tagged vehicles"; observed, spec 07 session 7) | M |

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

## Galaxy, setup and sight (spec 01, spec 02 §9)

Generation, empire placement, starting planets and stockpile, the setup option lists,
racial point costs, sight and stellar manipulation follow the specs as settled on
2026-09-30 (`generate.cpp`, `setup.cpp`, `sight.cpp`, `movement_stellar.cpp`). Every
starting planet is a capital of colony type "Homeworld", and the home system is recorded
when the game is created (`Empire::homeSystem`). A new game and Quick Start are
turn-based. Maps are saved and loaded in our own format ([MAPS.md](MAPS.md),
`map_file.hpp`) with starting points placed first; the game keeps the points a later Save
Map writes (`GameState::startingPoints`). The autosave choices are applied after each
processed game turn in local and hotseat games of either turn style, into files named
after the last digit of the turn count, and can be changed during the game (Empire
Options). Spec 01 §14 Q41–Q43 were answered from the executable; the engine follows them,
and since 2026-10-01 a comet or warp point entry of a system template is placed like any
other entry (its position and a record drawn, its sector marked and recorded for `Same As`)
and keeps an empty name for the letters of later planets there (Q43).

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
`Ring 0` is any sector of the inner 11 × 11 square (`generate.cpp`). `Ring 8` and `Ring 9`
stay on `Ring 7` with a warning: the spec has no rule for them (spec 01 §14 Q46).

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
- **Player Computer Control** (spec 06 §1.2.1, §7 Q84; `ai::setComputerControl`): the check
  list behind a master password, the mark with all ministers and flags; local and hotseat games
  end when no human is left. Since 2026-10-03 every empire can be switched, neutral ones too:
  neutrality is a mark of its own (`Empire::neutral` while a human plays a neutral empire,
  `isNeutral()` for the rules), so a neutral empire handed to a human keeps the neutral rules.
  A player's copy of a game on different machines changes only there; its orders carry the
  minister switches and the fleets' flag (`cmd::SetMinisters::fleets`) and, of the ships,
  units and colonies, only the flags of those given orders that turn (`ClassicSession::carryFlags`).
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
  the Victory picture and asks whether to play on.
- **Help** and the **Galaxy Map** (spec 07 session 5): the original's layout, frame, places and
  button slots; Help's Weap Mount tab and Manual button.

OpenSE4's own additions in these windows, none of which moves an original control: the Find
box in Help's heading row; the Galaxy Map's Show Distances (in an empty slot), the legend (in
three empty slots) and the notes line under the map's hint; the target pickers of an
intelligence project (the original's are not described); the Designs window's note in its
title strip, the scroll to a design just made and its Make Current label for an obsolete
design; the hover tooltips of the Research and Intelligence lists. What remains different:

| Where | Client now | Original | Impact |
|---|---|---|---|
| E-mail address (`Empire::email`, `cmd::SetEmail`) | Kept with the empire since 2026-10-03 (Empire Setup's Email box, Change Email, saved with the game); it travels in a player's orders only as a Change Email command, empire files do not keep it, and another player's address is left out of a player's view and of the lobby | Saved with the empire in the game file and in every orders file, so every player's game holds every address (spec 05 §9.2, spec 06 §7 Q95) | L |
| Last turn of a human defeated in its own turn-based turn (`turn_based.cpp` `finishPlayerTurn`) | Marked dead at the end of that turn, without a Lose ending first | Marked dead only after its Lose window (spec 06 §7 Q83); the original's check when a human loses everything during its own turn is not described (inferred) | L |

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
the intro's "Loading:" and "Complete". What still differs, and OpenSE4's own additions:

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
- **Our choices where the observation is silent** (impact L): the spin controls' steps (5 % a
  click for characteristics, 100,000 points, one year, 10 % and 5 % for the victory values, 50
  units and 10 ships) and typing into their value boxes; the characteristics' level words
  other than "Average" and the effect wording; Begin Game without a chosen portrait asks for
  one; Change Directory asks for a folder by name (with Default); the Compare Culture
  Modifiers window is our table; the pictures under the page buttons come from the seed;
  Victory Conditions keeps whole years; the lists use the shared arrow column; loading a game shows no "Login To Game" list of its empires and the Game Master.

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

Players' reports on 0.9.0, settled from the executable on 2026-10-05 (spec 06 §7 Q110):

| Where | Client now | Original | Impact |
|---|---|---|---|
| A fleet in the sector's list and the Fleet Report (`client/classic/main_window.cpp`, `reports.cpp` `fleetReport`) | Every fleet member is a ship row; a click shows the Fleet Report with that ship's report under it; the member rows take no clicks, and no command sets the leader | One row per own fleet (its picture, name and status icons), the rows sorted by stellar objects first, fleet number, owner, hull and name; the Fleet Report alone; a left-click on a member makes it the leader, a right-click opens its Ship Report popup (spec 06 §2.5; observed, spec 07 session 7) | L |

What remains, in network and e-mail games only:

| Where | Client now | Original | Impact |
|---|---|---|---|
| Colonize candidates in a network or e-mail game | A foreign colony whose planet the player does not see (its cloak, a storm, a nebula) is not in the player's view at all (`game::redactForEmpire`, spec 05 §9.5), so it is no candidate; an uncolonized hidden planet is | Every player holds the whole game (spec 05 §9.2), so such a planet is listed like any other | L; keeping the planet in the view without its colony would show it as an uncolonized planet the panel draws, so matching needs the view to say which planets are hidden, a protocol change of its own |

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
other unarmed types get Don't Get Hurt.

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
update and the ministers use the claims of the previous turn, and its exclusions (another
computer player's home system, avoided systems) apply only to neighbour systems; a human
whose Politics minister is on claims by the same rule; colonization danger adds 1 per
non-friendly empire beyond each warp point; a Defense Base goes to the K-th queue of the
empire's queue list, or a random one, with the backlog test on that queue alone, and a base
sent to a colony without a yard is counted and paid for but never queued (the original's
construction step drops it); mines, satellites, weapon platforms and fighters take the first
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
| A Scrap order on a fleet member (`scrapOldest`; spec 05 question 74) | The candidate leaves its fleet before it gets the Move To and the Scrap (inferred) | The orders go onto the member's own list; its fleet's group carries them out, so the Move To takes the whole fleet and the Scrap takes the group's first member (read in the executable, not traced) | L |
| Attack minister in Prepare for Attack (`planAttack`) | A ship outside fleets already in the staging system gets no order from the routine (inferred) | Every ship outside the staging system is sent there; the spec names nothing for those inside it | L |

The turn-based golden game of `tests/test_determinism.cpp` moved to seed 39 with these
rules (2026-10-03): seed 35 fought only four battles, below the coverage check's five.

Settled with the engine already matching: what enters the enemy-in-territory list (question
54), a colony whose row builds nothing and the colony-type tests (question 55), the state
machine's transitions (question 53).

