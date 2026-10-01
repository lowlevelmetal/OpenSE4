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
| Turn-based games (`turn_based.cpp`, `net/host.cpp`, `net/pbem.cpp`) | Played locally, hotseat, over the network and by e-mail. On different machines a host is in charge: over the network (an OpenSE4 extension) it carries out the commands of the player whose turn it is; by e-mail each player sends the commands of their turn (`.plr`), and the host replays them and sends the game on to the next player. A player who is away, out of time or without a `.plr` is played by the computer for that turn (spec 05 open question 33). The game client opens a PBEM `.gam` (Multiplayer, Play by E-mail, or `--pbem`), plays the player's turn in either style and writes the `.plr` at End Turn (spec 05 open question 36). A computer player's (or a minister's) orders of one planning pass are carried out together after the pass, not one at a time as issued. As in the original, nobody is asked Tactical or Strategic in a game played on different machines: the host resolves the battle (local and hotseat turn-based games ask) | Spec 05 §9.1: on different machines the save file passes from player to player, and TCP/IP is for simultaneous games only; spec 04 §2, §3 step 1; spec 06 §2.7 | M |

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
spec 02 §6.6), from the Upgrade Facilities button and the computer's upgrades. The
engine's own choices where the spec is silent are spec 02 §13 items 51–56. No row
remains.

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

The engine's own choices where the spec is silent are spec 03 §19 Q72–Q76: objects changed
in place by stellar manipulation, a fleet member away from the fleet's location that acts,
mothballed members, fleet members in a computer player's ad-hoc group, and changes to a
fleet's orders other than adding. No row remains.

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

The engine's own choices where the spec is silent are spec 04 §19.1 and the open
questions of §19.3 (Q78-Q86). No row remains. Combat reads a drone group's first order
and a troop ship's Attack orders from the vehicle's own list, which holds a fleet member's
copy of its fleet's orders (spec 03 §19 Q65).

## Research, intelligence, diplomacy, events, score, turn order (spec 05 §1–§6, §8–§9)

Every rule of this section follows the spec (2026-09-30); the `Ship - Moved` event draws
one sector number and disbands the moved ship's fleet since 2026-10-01 (spec 05 §4). Where
the spec leaves a detail open, the engine's choices are spec 05 open questions 38–46: third empires for "Any"
political operations, who hears of an operation that tells nobody, the place and layout
of the players' statistics, history and log files, rebel empire details, home planet
locations, when opening a report dates a design, how a turn-based political step knows
what it counted, the 21-order limit and step 16.

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
and keeps an empty name for the letters of later planets there (Q43). This row is where
the engine differs:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Generation edge cases (`generate.cpp` `drawNames`, the connectivity pass of `buildLinks`, the "warp points anywhere" draw of `placeWarpPoints`, `placeHomeworlds`) | Systems beyond the name list get generated names; the connectivity pass marks only a system it cannot link, searches any distance and always ends; "warp points anywhere" stops after 1,000 draws; a map point on a sector another empire took is skipped | Spec 01 §3.4, §3.5, §3.6: such systems get no name; the pass marks the system with everything linked to it, looks only 68 squares far and can loop forever; the draws never stop; nothing checks a taken point. The engine's choices are deliberate: keep them | L |

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
other unarmed types get Don't Get Hurt. The engine's own choices where the spec is silent
are spec 05 open question 37. No row remains.

