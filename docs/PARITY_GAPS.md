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
| Turn-based games (`turn_based.cpp`, `net/host.cpp`, `net/pbem.cpp`) | Played locally, hotseat, over the network and by e-mail. On different machines a host is in charge: over the network (an OpenSE4 extension) it carries out the commands of the player whose turn it is; by e-mail each player sends the commands of their turn (`.plr`), and the host replays them and sends the game on to the next player. A player who is away, out of time or without a `.plr` is played by the computer for that turn (spec 05 open question 33). The game client opens a PBEM `.gam` (Multiplayer, Play by E-mail, or `--pbem`), plays the player's turn in either style and writes the `.plr` at End Turn (spec 05 open question 36). A computer player's (or a minister's) orders of one planning pass are carried out together after the pass, not one at a time as issued. As in the original, nobody is asked Tactical or Strategic in a game played on different machines: the host resolves the battle (local and hotseat games stop at each battle they show). The host never stops, so a client shows those battles afterwards, where the original shows the player whose turn it is the Strategic Combat window before the battle (spec 06 §7 Q74) | Spec 05 §9.1: on different machines the save file passes from player to player, and TCP/IP is for simultaneous games only; spec 04 §2, §3 step 1; spec 06 §2.7 | M |

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
These rows remain:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Ship Orders options after a warp (`movement.cpp` `encounter()`) | Clears only the holders' lists (`setLists(g, {})`), and in a turn-based game writes the Move To back (`setLists(g, {o})`), so it resumes on a later turn | Spec 03 §6.4, §19 Q77: every member of the acting group, a computer player's companions included, has its list emptied with Repeat off; a turn-based Move To keeps stepping only within the current run | M |
| Low supply of drone groups at a Sentry's end (`movement.cpp` `sentry()`; client `order_rules.cpp` `lowOnSupply()`) | The tenth of `Supply Amount for Low Supply Warning` applies to fighter groups only | Spec 06 §4.4, §7 Q61: drone groups use the same tenth, and need at least one unit | L |

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
2026-10-01 (Q89 matches). These rows remain:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Landing on a converted planet (`combat_space.cpp` `Battle::landingColony`) | "Another empire" is the colony's owner (`colonyHolder`), so the converter's ships may land on a planet piece it converted | Spec 04 §11, §19.4 Q87: judged by the side the planet piece fights for; the converter may not land there, every other empire may, its owner included | L |
| Refusals of a landing (`Battle::landingProblem`; `combat_tactical.cpp` `TacticalBattle::check`) | "No troops" also when other units are aboard; every piece other than a ship or base is refused | Spec 04 §11, §19.4 Q88: a ship with units but no troops is refused silently; a colony's planet piece drops the troops of its colony's cargo; a unit group meets the three tests | L |
| Simultaneous battles shown (`combat_space.cpp` `resolve()`, `turn.cpp`) | With `Simultaneous Games Show Strategic Combat` on, only battles with a human side stop the turn | Spec 06 §1.10.5, §7 Q76: every battle on that machine is shown, computer-only battles included | L |
| A drone's target after a piece leaves (`Battle::chooseDroneTarget`) | Piece numbers are never reused; a drone whose target left chooses again | Spec 04 §10.7: a new piece takes the number one above the highest present, so it can inherit a dead piece's number and become a drone's target without a choice | L |
| Random numbers of a battle shown tactically (client drawing) | The display draws no random numbers | Spec 04 §19.1, spec 06 §7 Q77: a miss's direction and a planet's point are drawn from the battle's sequence, so a shown battle continues differently | M |

## Research, intelligence, diplomacy, events, score, turn order (spec 05 §1–§6, §8–§9)

Every rule of this section follows the spec. On 2026-09-30 the engine's own choices (spec
05 open questions 38–46) were answered from the executable, and contact loss was found
(question 13, corrected). The rows found then were implemented on 2026-10-01:

- Contact is lost when no warp path leads from an empire's colonies to a colony of the
  other: checked once per game turn in both turn styles, after the design cleanup and before
  the victory check, it returns that side to "no contact", drops its intelligence projects
  against the other and logs "Contact Lost", which the human player's history file records;
  first contact needs the same path (`diplomacy::checkContacts`, `updateContacts`,
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

Question 52 and spec 06 §7 Q70 were settled from the executable later on 2026-10-01. These
rows remain:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Requests the computer starts (`ai_diplomacy.cpp` `demand()`) | Step 3 names the first empire in contact the AI is hostile to (None included), ignoring the recipient's treaty with it; step 5 names one of the attack candidates; each step tests its flag and falls through on a refusal; step 2 makes one roll, colonies first | Spec 05 §7.4, question 52: step 3 names the lowest-numbered empire the AI holds at War or Non-Intercourse and the recipient at Trade Alliance or better; step 5 comes from the AI's newest battle lost while defending; the first applicable request is chosen, then its flag tested, a refusal ending steps 1–5; ships and colonies are two rolls, ships first, 1 in 3 per candidate | M |
| Third-empire check of messages (`commands.cpp` `SendMessage`) | Refuses unchecked requests from every human-played empire, its Politics minister's included | Question 52: only messages the player writes go through the picker; a minister's are not checked | L |
| Log of a completed package (`diplomacy.cpp` `acceptPackage`, `executePackage`, `setTreaty`, `makeContact`) | One "… Completed" entry per party; "Items Unavailable" entries; treaty items log "New Treaty" (accepted proposals too) and channel items "First Contact"; channels need the giver's contact and a living empire; technology refused without `allowTechTrades`; a System item only drops the giver's claim; star charts need the giver's exploration and copy its warp links | Spec 06 §7 Q70, spec 05 §3.4: one entry per item, receiver then giver, with the titles and targets listed; invalid items skipped silently; "Treaty Enacted" plus a history contact line; channels set both sides to None without checks; no option tested; the receiver claims the system; no exploration test, no warp links; an accepted proposal logs only its message | M |

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
empire's side of a first contact (`diplomacy::updateContacts` with `onlySide`); the host
recalculates the colonies a player's orders name when it reads them, then sight everywhere;
every reading of a game file recalculates every colony (Load Game, `--load`, a PBEM file opened
by a player or the host, a local or hotseat simultaneous game passing to the next player or to
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

These rows are where the engine differs:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Starting assets (`setup.cpp`, "Starting designs and ships"; `client/classic/frontend.cpp` `quickStartSetup`) | Every empire gets four designs of its own (`autoDesign`), two scouts and a colonizer | Spec 01 §3.6 "Starting assets", §2.1: no empire gets ships or designs at creation; computer players design in their first turn; Quick Start gives the human one Design minister run | H |
| When first contact is checked (`diplomacy.cpp` `updateContacts`; callers in `turn_based.cpp`, `turn.cpp`, `commands.cpp`) | Galaxy-wide, after every live move, each player's and game turn's end, movement, combat and events; never at setup | Spec 05 §3.1: one system at a time, only at game creation, warp arrival, any decloak, a logged event, a surrender, a package's planet or vehicle and a rebellion project | M |
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
  wait after each animated step) and the Finale picture lists (the ending window: Victory,
  Lose, Human Dead).

The client's own differences are written in spec 06 beside each answer ("Our client
differs"). Those settled on 2026-10-01 in the last round, still to implement:

- **Colonies and Ships\Units** (§1.8.3, §7 Q56): a sort key is a column with one identity
  in every tab; the original's Colonies columns, directions and keyless columns; clickable
  pictures; fleet rows last and unsorted; a queued design's right-click opens its Design
  Report (`screens/planets.cpp`, `screens/ships.cpp`, `screens/queues.cpp`).
- **Supply icons** (§4.4, §7 Q61): drone groups like fighter groups
  (`status_icons.cpp`).
- **Coordinate line** (§2.4, §7 Q64): the range and the selection marker only while the
  selected sector holds an object the viewer sees (`main_window.cpp`).
- **Movement log replay** (§7 Q62): one entry per vehicle and step in movement order, each
  animated alone, headings kept from the start of the turn (the engine must keep a
  heading), step keys ignored during a day (`movement_replay.cpp`). Moves as they are made
  turn and slide as in §2.4 (`ship_glides.cpp`).
- **Strategic Combat** (§7 Q73): fight on step by step until the next refresh is due
  (`screens/strategic_combat.cpp`).
- **Tactical animation** (§1.10.3, §7 Q77): 36-frame slides, 9 frames per 45°, torpedo
  and beam steps, explosions only for structure damage, no flash, the 0.3 s only after a
  surviving seeker impact, misses off-centre (`replay.cpp`, `screens/combat_map.cpp`).
- **Combat Piece Report** (§1.10.1, §7 Q78): the 310×420 window with Close under the
  tabs; the Ability tab's sets (`screens/tactical.cpp`).
- **Combat Simulator** (§1.10.4, §7 Q38, Q79, Q80, Q82): the side's box in the Combat
  Vehicles list with its Cargo/Units and Fleet lines, 26×18 boxes, the window titles and
  hints; Existing Fleets shown; Change Cargo against a Storehouse with population kept and
  no order buttons; side boxes without outline (`screens/simulator.cpp`,
  `fleet_transfer.cpp`, `cargo_transfer.cpp`, `combat_logic.cpp`, `combat_map.cpp`).
- **Save Empire** (§7 Q72): the file keeps the strategies; designs lose the obsolete
  mark, replace the empire's designs and must pass the validity rules (`setup.cpp`,
  `setup_model.cpp`).

Seen side by side with the running original on 2026-10-01 ([spec 07](spec/07-observations.md),
session 3, which lists each difference in full). Impact is visual only unless noted:

| Where | Client now | Original (observed) | Impact |
|---|---|---|---|
| Selection on the system panel (`main_window.cpp`) | Four corner lines | `Dialogs/Selection.bmp` (eight yellow marks) over the sector's 36x36 sprite square, black transparent (spec 06 §2.4 "Selection") | M |
| Strategic Combat pace (`screens/strategic_combat.cpp`) | One empire's phase per displayed frame: a 3-sided 30-turn battle takes 90 frames (1.5 s at 60 Hz) | 7–15 frames (0.10–0.23 s) for the same battles, 2–5 combat turns per frame (spec 06 §7 Q73) | M |
| Lists in every window | A thin scroll bar, no "Pic" heading | Up/down arrow buttons and a "Pic" heading over picture columns | L |
| On/off settings in button columns (Construction Queues filters, Ships\Units Show buttons, Designs Hide Obsolete and Stats\Strategy, Planets No Sys To Avoid) | Lamp buttons, or no box | A check box holding the lamp (07 "UI": on/off settings) | L |
| Colonies (`screens/planets.cpp`) | "Statistics" and "Output" blocks (the second cut off by the minimap); General columns Name, Type, Colony Type, Population, Mood, Facil.; Constr. Queue and Goto buttons; Close in slot 15 | Summary lines (systems, colonies, blockaded colonies, population, research and intelligence produced, resources produced, storage); General columns Pic, Name with planet type, Atmosphere, Conditions, Pop, Mood; no Constr. Queue or Goto; Close in slot 14 | M |
| Planets (`screens/planets.cpp`) | Short labels, "Min./Org./Rad." headings, atmosphere centred in the row, own homeworld in yellow | The longer labels of 07 session 3, three "Value" headings with resource icons, atmosphere on the name's line, all names white | L |
| Research (`screens/research.cpp`) | Completed areas left out; Tech Tree button; Reorder Projects in slot 9 | Completed areas listed, dimmed, cost "Complete"; a small box under each project box; Reorder Projects in slot 13 | M |
| Designs (`screens/designs.cpp`) | Plain rows; detail with Class, Space, Structure, Weapons; components as a text list | Rows under design-type headings with lamp, picture, name, hull and "Prototype"; detail Cost, Movement, Shields, Cargo Space, Supply Capacity; components as an icon grid; the note on obsolete designs | M |
| Create Design (`screens/designs.cpp`) | Opens on Escort with a suggested name; vertical component list by group; warnings in red at the bottom; Cancel low | Asks the vehicle type first and titles the window after it; starts without a size; component strip and a paged 3-column tile grid; Warnings and Component Details boxes; To Hit Modifiers, Condensed View and Only Latest as check boxes; Create Design and Cancel in slots 13 and 14 | M |
| Empire Status (`screens/empire_status.cpp`) | Budget table with Other, Not delivered, Lost to full storage, points, password and an option line; no Change Email | Three blocks: production per turn, expenses per turn and net, treasury; Change Email (slot 12) and Change Password (slot 13) | L |
| Empires (`screens/empires.cpp`) | Victory Conditions before Scores, Our Race in slot 12, Borders with a check box, a "Treaties" heading and explanation | Scores, Victory Conditions, Comparisons, a gap, Our Race (slot 13); Intelligence dim with no contact | L |
| Ships\Units, Log (`screens/ships.cpp`, `screens/log.cpp`) | A hint paragraph; "Ships \ Units"; Show buttons in slots 7–9; "Nothing to report this turn." in an empty log | No hint; "Ships\Units"; Show check boxes in slots 11–13; an empty log stays empty | L |
| Combat Simulator (`screens/simulator.cpp`) | Empire flag pictures, design order, buttons from slot 3 | Numbered colour boxes, alphabetical items, Name with Cargo and Fleet lines, buttons from slot 7 with Begin in slot 13, hints under the lists | L |
| Tactical Combat (`screens/tactical.cpp`, `screens/combat_map.cpp`) | Titled after the simulator; heading line and side list above the map; boxes around pieces; hint texts; one column of buttons beside a small overview | Title strip with Location, Turn, Empires and navigation arrows; map fills the left; piece and target reports with weapon grid; Options, Orders, Auto and End Turn as a 2x2 group; overview at the bottom right | M |

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

OpenSE4 choices: a design name when every name is used (question 37), and the one-turn Seek
of Destroy Black Hole and Destroy Nebulae ships, which the engine gives as a Move To to
sector 36 that the minister plans again every turn. The record of our designs that fought
lists the first design of a group that mixes designs. The "(inferred)" markers of question
37 in `ai*.cpp` are gone (the operating part, mothballed repairs, the yard ship's own yard,
the unlimited-supply test, the warp-point draws and the top-left sector, Close Warp Point's
sight, the layers' weights, cap and flag, the empty transport, the answer window, the
waiting test, the units reserve). No row remains from the executable.

**Observed pace** (2026-10-01, [spec 07](spec/07-observations.md) session 3): three
original games against twelve of ours, Small quadrant, five empires, simultaneous, 100
turns. Early on our computers are one colony ahead (2.9 against 1.7 at turn 10, 6.8
against 5.3 at turn 25), a result of the starting assets row above, as are an earlier loss
of the homeworld's Happy bonus in the original (turn 4 against our 6–8) and our 4M of
colonists on turns 1 and 3. Two differences remain once that is set aside:

| Where | Engine now | Original (observed) | Impact |
|---|---|---|---|
| Research after turn 25 (likely, 1–2 standard errors; cause not traced; `ai_research.cpp`, `ai_economy.cpp`, facility choices) | Mean research points per empire 11.2k at turn 50, 13.6k at 75, 14.5k at 100; 34.6 tech levels at 100 | 13.9k, 17.9k, 23.5k; 39.4 tech levels at 100; score 125k against our 96k | M |
| Bases (about two standard errors; `ai_economy.cpp`, the vehicle queue) | 0.1–0.2 bases per empire from turn 50 | 0.6–0.9 | M |

Ships, systems, colonies after turn 50 and units agree within the noise. Recheck both rows
with more original games once the starting assets follow the spec, since an earlier start
changes the whole game.

