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
- **Player Computer Control** (spec 06 §1.2.1; `ai::setComputerControl`): the check list
  behind a master password, the mark with all ministers and flags; local and hotseat games end
  when no human is left; a player's copy of a game on different machines changes only there and
  sends the player's own minister switches. The TCP/IP host's toggle flips only the mark
  (`ai::setComputerMark`). Our choices are spec 06 §7 Q84.
- **Reset Passwords** (spec 06 §1.9): six digits from a source apart from the game's, shown to
  the host only and written in after the next turn's orders are read, never saved; on the
  in-game host's Options window, by an admin of a headless server and by the e-mail host
  (`pbem process --reset-passwords`).
- **Settings.txt keys** (spec 06 §1.9, Q83): `Allow CD Music` (the Options music rows and the
  Combat Options lamp), `Allow Export of Weapon And Component Data` (a Weapons Report Export
  button writing four tables of our own layout), `System Ship Movement Delay Milliseconds` (a
  wait after each animated step, read as seconds as the original does; none in the movement
  log replay, spec 06 §7 Q62) and the Finale picture lists (the ending window: Victory,
  Lose, Human Dead).

The client's own differences are written in spec 06 beside each answer ("Our client
differs"). Every one settled on 2026-10-01 is implemented; the client's own choices are
noted beside each answer. Spec 06 §7 Q83–Q98 were settled from the executable and from
screenshots on 2026-10-03 (the ring's pixels of Q85 match ours); the rows below list what
our client does differently.

Seen side by side with the running original on 2026-10-01 ([spec 07](spec/07-observations.md),
session 3, which lists each difference in full). Since 2026-10-01 the client follows what was
observed (spec 06 §1.11): every list scrolls with an arrow column and heads picture columns
"Pic"; on/off settings in button columns are check boxes; Planets, Colonies, Construction
Queues, Research, Designs, Create Design, Empire Status, Empires, Ships\Units, the Log, the
Combat Simulator, Tactical Combat and the report panel have the original's labels, blocks,
button order and slots. The details the observation does not give are our choices, spec 06 §7
Q89–Q98. What remains different (impact visual only unless noted):

| Where | Client now | Original | Impact |
|---|---|---|---|
| Research (`screens/research.cpp`) | Tech Tree always in slot 12; project boxes with the name, level and an estimate centred, the small box writing "paid / cost" | Tech Tree only when the game lets players see the complete tech tree, a Game Settings check box our setup and `GameOptions` lack (spec 01 §2.2), and the only way to the Tech Tree window; 140×130 boxes with the name, "Research Level N", Completion and Cost Per Turn, the small box a bar of up to 19 green blocks (spec 06 §7 Q92) | L |
| Empire Status (`screens/empire_status.cpp`) | Change Email dim: the engine keeps no e-mail address for an empire; one table per block, icons on each block's first row, the net as an unlabelled block | The address is the empire's (asked for by Change Email and by Empire Setup's Email box), saved with the game and carried in the orders file but never used to send mail (spec 05 §9.2, spec 06 §7 Q95); needs an `Empire` field. Amounts right-aligned at fixed places, icons only on From Our Colonies, Net Resources Per Turn a labelled row | L |
| Every list (`screens/list_widgets.cpp`) | A 16 px column with arrows of our own drawing, no thumb; the wheel scrolls | A 24 px column with the `Arrows.bmp` cells (normal, under the pointer, held, disabled), a thumb as long as the visible share, a click or drag in the track jumps there, holding an arrow repeats every 100 ms (spec 06 §7 Q89) | L |
| Empires (`screens/empires.cpp`) | Borders a view of the Empires window, one filter at a time, overlaps in white, no claiming | Borders a window of its own: a check box per empire, the claimed systems in their colours, contested ones yellow with a Legend, a click claims a system or gives it up (spec 06 §7 Q96) | L |
| Weapons Report export (`data_export.cpp`) | Four tab-separated `OpenSE4_*.txt` tables of our own columns | `Weapons.txt`, `Comps.txt`, `WeaponFamilies.txt`, `CompFamilies.txt`, fixed-width, every component of the data set (spec 06 §7 Q83) | L |
| Ending (`finale_screen.cpp`) | One kind chosen (Victory, Human Dead, Lose); a lone human sees Human Dead at once | Each ending shown as it comes: Lose at the start of a last turn, Human Dead at the next End Turn (spec 06 §7 Q83) | L |
| Players window, TCP/IP host (`ai.cpp`, `net/host.cpp`) | Neutral empires cannot be switched; the host's toggle refuses empires that were computer players from the start; a player's copy sends every flag | Any empire can be switched (neutrality stays); the host's toggle works on every row ("[Host]"); the orders carry only the flags of objects changed that turn (spec 06 §7 Q84) | L |
| Orders cells, Ships\Units and Colonies (`screens/ships_common.cpp`) | "None" or "REPEAT ORDERS" for an empty list; the current order in round brackets | One order per line, no brackets, an empty cell when there are none; "REPEAT ORDERS" only in the sort text; the Colonies Orders heading does not sort (spec 06 §7 Q88) | L |
| Colonies (`screens/planets.cpp`) | "Systems with Colonies"; research and intelligence values at x 289; storage truncated ("52kT"); a click selects, a double click shows the colony; the buttons act on the selection | "System with Colonies"; those values at x 271 with icons; storage rounded up ("53kT"); a click shows the colony and closes the window; Set Colony Type asks for the planet; Scrap Facil Types acts on every colony (spec 06 §7 Q90) | L |
| Log Goto (`screens/log.cpp`) | Lit whenever no entry is selected | Keeps its last state when nothing is selected (spec 06 §7 Q91) | L |
| Designs (`screens/designs.cpp`) | Our row places, headings in design-type order, obsolete names grey | The original's row places, headings and designs in alphabetical order (spec 06 §7 Q93) | L |
| Create Design (`screens/designs.cpp`) | Our places; Weapons Report in slot 7; yellow warnings; To Hit Modifiers changes the tiles; an empty name box and a design type already chosen | The places of spec 06 §7 Q94; Weapons Report in slot 11; white warnings after a red ball; To Hit Modifiers shows Offense and Defense Bonus in the figures box; the boxes read "Design Type" and "Design Name" | L |

Seen side by side with the running original on 2026-10-03 ([spec 07](spec/07-observations.md),
session 5, which gives each original layout in full): the setup screens, the front end,
Help, the Galaxy Map, Intelligence, Combat Replay and Ground Combat. Impact visual only
unless noted:

| Where | Client now | Original | Impact |
|---|---|---|---|
| Game Setup (`screens/setup.cpp`, `setup_widgets.cpp`) | A layout of our own: title strip, the pages as a column at the right, radio rows with ranges and explanations; opens on Quadrant; Technology Level on the Technology page; a Combat choice on Mechanics; no Game Master Password, Cheat Codes, No Tactical Combat or complete-tech-tree boxes, Multiplayer Game Filename, Save Game Directory Path or Connection Type | The setup frame and the eight page layouts of spec 07 session 5 (an 800×600 area, page buttons at the left, content frame, Begin Game and Cancel under it); opens on Players | L |
| New-game and Quick Start defaults (`state.hpp` `GameOptions`, `setup_model.cpp` `defaultSettings`, `frontend.cpp` `quickStartSetup`) | Maximum Event Severity High; random neutral players off; victory values 50000 points, 100 years, 200 %, 75 %, 20 years, 10 years; one human empire already in the list; Quick Start gives the player 4 computer opponents and no neutral empire | Catastrophic; random computer and neutral empires both on with one count choice (Medium: 3–7 of each); 5,000,000, 10.0, 300 %, 50 %, 1.0 and 5.0; the list starts empty; Quick Start keeps these defaults, so it rolls 3–7 computer players and 3–7 neutral empires (spec 01 §2.1, §2.2, §11; spec 07 session 5) | M |
| Empire Setup (`screens/setup_empire.cpp`) | A layout of our own, filled from the race's preset, no Email box, drop-down pickers | The six page layouts of spec 07 session 5; starts empty; an Email box; 340×370 list pickers | L |
| Quick Start (`frontend.cpp` `QuickStartScreen`) | Every race alphabetically, row by row, scrolled by row with 16 px arrows; Begin Game dim until a portrait is chosen; the Cryslonite named "Cryslonite Imperium Imperium" | Settings.txt's Quick Start Style order, column by column, pages of eight turned by 24×24 arrows; the empire's name as written (spec 07 session 5) | L |
| Load Game (`frontend.cpp` `LoadGameScreen`, the in-game window) | Two different windows (a 600×540 panel from the intro, a 536 px window in the game), no dates, no Change Directory | One 420×520 dialog with Save Game Name and Date columns, Change Directory and Cancel (spec 07 session 5) | L |
| Help (`screens/help.cpp`) | Find box, items grouped with pictures and sizes, our own detail lines, no Weap Mount tab or Manual button, Weapons Report in slot 11 | Alphabetical name lists with lamps, the detail layout of spec 07 session 5, Weap Mount tab (slot 2), Weapons Report (slot 12), Manual (slot 13) | L |
| Galaxy Map (`screens/galaxy_map.cpp`) | Map 5–6 px up and left in a darker frame, a frame round the content, Goto System in slot 8, an extra Show Distances box and a legend | Map frame (144,189)–(687,564), hint under it, Goto System in slot 11, Show Names in slot 13 (spec 07 session 5) | L |
| Intro (`frontend.cpp`) | Multiplayer, Settings and Manual buttons at the top right; "Data: se4" at the right of the version line | "Loading:" and "Complete" at the right of the version line; no other buttons (our extra buttons are OpenSE4's) | L |
| Intelligence (`screens/intelligence.cpp`) | Points as a line in the content, a two-column table with the group in orange, the four project boxes stacked at the right, a description box, Divide Evenly, Reorder Projects in slot 7 | The Research layout: points in the title strip, silver group headings, 14 px rows with the cost right-aligned, four 140 px boxes side by side with their small boxes, Divide Pts Evenly, Reorder Projects in slot 13 (spec 07 session 5) | L |

Since 2026-10-03 the battle windows follow session 5 and spec 06 §7 Q86, Q87, Q97 and Q98:
Tactical Combat's places, panels, weapon cells and title-strip buttons, the Combat Piece
Report's pages, Combat Replay in the Tactical Combat frame, Combat Replay Options' headings
and check boxes (and Combat Options' alike, inferred), Ground Combat's grids and side marks,
the movement line during the movement log replay with sector 0 selected after it, and the
report's up-arrow. Their remaining choices are noted in spec 06 beside each answer; the
lists' 24 px arrow column is the "Every list" row above. OpenSE4's additions there, which
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
| Jump-count thresholds (`ai*.cpp` with `warpJumps`; spec 01 §14 Q45) | Thresholds such as "within 4 jumps" and "within 3 jumps" compare real jump counts | The original's jump count is the real number plus two (spec 01 §3.6, confirmed: binary); whether spec 05's thresholds already allow for it is not checked yet. If they do not, the original's tests reach two jumps less far than ours | M (to check) |
| Time in Defend (Short Term) and Infrastructure (spec 05 question 53). With five original games the later shares are close; the difference left is the first 25 turns, where ours leave Exploration sooner (spec 05 question 65) | Defend (Short Term) 50 % of all turns, 71 % of turns 51–100 (seeds 1–24, after the rules of 2026-10-02 below); Infrastructure 8 % and 7 %; Exploration 74 % of turns 1–25; 15 % of listed war colonies gone within 10 turns (before those rules) | 48 % and 70 % (49–94 % per game); Infrastructure 9 % and 11 % (0–30 %); Exploration 85 %; 18 % (33 % in the first three games) | L |
| Bases. The placement rule is the original's (all 174 placements observed in five games went to the K-th queue, 25 reached a yard; ours 21 % of 742) and every placement is made in Infrastructure; since 2026-10-02 the soft cap, the scrap candidates and their ties are the original's too (below). With the construction budget's queue commitments fewer turns are over the soft cap, so fewer bases are scrapped, but fewer are placed early | 0.30 / 0.37 / 0.35 per empire at turns 50 / 75 / 100 (seeds 1–24; 0.29 / 0.33 / 0.34 over 120 seeds); built 0.51, lost 0.15 per empire in 100 turns | 0.4–0.6 in five games, 1.2 and 0.0 in two more (game 6: built 1.4, lost 0.2) | L |
| Resources from turn 50 (spec 05 question 61). The production rule matches (13,100 colony outputs observed); part of the gap is the original's lucky race draws, the rest its extra colonies, built from more colony ships in the first 50 turns (question 65) | Resources produced 22.5k / 28.9k / 32.5k at turns 50 / 75 / 100; colonies 10.7 / 14.3 / 16.4 (seeds 1–24; 11.0 / 14.6 / 16.4 over 120 seeds, a colony more at turn 100 since the construction budget takes off the queues' commitments) | 25.6k / 34.5k / 36.9k (16–22 % above ours with the same race line-ups); 12.2 / 16.6 / 17.0 | M |
| Ships from turn 75 (spec 05 question 62). Both sides build as many attack ships; ours lose more, in decided battles away from colonies, which ours fight more often (questions 66, 68; drawn battles vary as much in the original). Fewer are scrapped since the soft cap leaves colony ships out; fewer are built in the first 50 turns since the budget takes off the queues' commitments | Ships 3.2 / 7.3 / 12.1 / 13.9 per empire at turns 25 / 50 / 75 / 100 (attack ships 7.1 / 8.2 at 75 / 100; 120 seeds: 3.3 / 7.6 / 12.3 / 15.0); over turns 26–100 23.1 attack ships built and 17.2 lost per empire; 4.3 % lost per attack ship and turn in turns 51–100 (3.9 % over 120 seeds) | 14.1 / 17.4 (8.6 / 10.5); 21.4 built and 12.5 lost; 1.9 % | M |
| Battles at enemy colonies (spec 05 questions 63, 67, 69): hostile colonies go by bombardment in both, and a battle against a weak colony goes the same way in both; the original fights more of them, while both make failed attacks on strong colonies | Per empire and 25 turns of turns 51–100: battles ending with the enemy colony gone 1.0; colonies lost 1.1 | 1.9–2.2 (two games); 1.8–3.6 (four games) | M |

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
movement a turn-based explorer compares, the fleets' Seek and Warp when they explore, the
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
fought no battle.

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
  (`TurnContext::aiStartNet`). The engine's own choices are spec 05 question 73. Turns over
  the soft cap fell from 3.5 to 1.2 % of turns 26–50 (the original none) and from 16 to
  11 % of turns 51–100 (the original 14 and 16 % in two games, at our upper quartile per
  game); colonies at turn 100 rose from 15.4 to 16.4 and resources from 30.8k to 33.2k
  (the original 17.0 and 36.9k).

Two details of the cap test are not followed and change little: the revenue comes from
the last income report rather than the production of the moment (within 3 % at the median
in turns 11–50), and the threshold is not taken in single precision (spec 05 §7.5).

Settled with the engine already matching: what enters the enemy-in-territory list (question
54), a colony whose row builds nothing and the colony-type tests (question 55), the state
machine's transitions (question 53).

