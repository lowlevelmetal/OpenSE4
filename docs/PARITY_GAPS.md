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
| Turn-based games (`turn_based.cpp`, `net/host.cpp`, `net/pbem.cpp`) | Played locally, hotseat, over the network and by e-mail. On different machines a host is in charge: over the network (an OpenSE4 extension) it carries out the commands of the player whose turn it is; by e-mail each player sends the commands of their turn (`.plr`), and the host replays them and sends the game on to the next player. A player who is away, out of time or without a `.plr` is played by the computer for that turn (spec 05 open question 33). The game client opens a PBEM `.gam` (Multiplayer, Play by E-mail, or `--pbem`), plays the player's turn in either style and writes the `.plr` at End Turn (spec 05 open question 36). A computer player's (or a minister's) orders of one planning pass are carried out together after the pass, not one at a time as issued. Battles in network and e-mail games are strategic: the host never asks Tactical or Strategic, so tactical combat over the network is left for later (local and hotseat turn-based games offer it) | Spec 05 §9.1: on different machines the save file passes from player to player, and TCP/IP is for simultaneous games only; spec 04 §3 step 1; spec 06 §2.7 | M |

## Economy and population (spec 02)

The rows found on 2026-09-29 were implemented that day. The economy's end-of-turn work is
one function per step of spec 02 §12 (`economy.hpp`), run inside each empire's end-of-turn
processing (`turn.cpp`, spec 05 §8). Conditions are the original's 64-bit double, kept as
its bit pattern and changed in x87 arithmetic everywhere (`conditions.hpp`: generation,
facilities, events, combat, stellar manipulation), and setup's racial point cost sums
`economy::characteristicPointCost`, which costs each characteristic as stored. On
2026-09-30 the engine's remaining guesses (spec 02 §13 items 33 and 37–50) and the other
open items of spec 02 were settled from the executable, and the rows found then were
implemented the same day, except the one below (the opening pools of spec 02 §9 followed
with the turn order). The engine's own choices where the spec is silent are spec 02 §13
items 51–56. This row is where the engine differs:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Where built units go (`economy_queue.cpp` `placeUnits`) | After the builder, the empire's other planets in the sector (object order), then its ships and bases (vehicle order) | Spec 02 §6.5: the other holders in the game's object order, planets and ships mixed. The engine keeps planets and vehicles in separate lists with no common order, so it cannot mix them (spec 02 §13 Q52) | L |

## Vehicles, movement and logistics (spec 03)

Design names are unique in the whole game (`uniqueDesignName`; the original allows duplicates
from turn and empire files, an OpenSE4 choice that stands, spec 03 §19 Q50), composite orders
are expanded when given (`orders.hpp`), and units in space are held in groups that mix designs
(`Vehicle::mixed` and the group helpers of `design.hpp`). On 2026-09-30 the engine was brought in
line with the rules of spec 03 §19 as settled from the executable: the stored-double day counter,
chained actions in object-slot order, ad-hoc groups, the Ship Orders options, launches and
recovery, hazard damage, repeat battles, Sweep Mines, repair and training sources, ruins, the
destructive-centre cost map, design editing and the smaller rules. The engine's own choices
where the spec is silent are spec 03 §19 Q60–Q70. What is left:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Long-range scanning (`sight.cpp` `scannerReaches`) | A design is learned only when a human opens the report of a vehicle the scanners reach (`cmd::OpenVehicleReport`), as the spec says; but `Long Range Scanner - System` works only from a populated colony and also covers unit groups | §3.3: `Long Range Scanner - System` works from any own object in the system that has it (planet facilities without population) and does not cover unit groups | L |

## Combat (spec 04)

The combat engine follows the rules of spec 04 settled on 2026-09-30 (§2-§19.1): start
positions, unit group and seeker damage, mines, cargo lost at once, troops fighting for
the ship's owner and ground combat in the colony owner's step, planets, the strategies
with the danger and attack maps, launches, design statistics, the tactical window's
phases, Auto, Resolve Combat and groups, and the combat simulator. The details the spec
leaves open are the engine's choices in spec 04 §19.1 and the questions of §19.2. The two
rows left are in the movement phase's order execution (`movement.cpp`), not in combat.

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Battle checks (`movement.cpp:1297`, `:1362`, `:483-487`, `:839`) | Turn-based: Load, Drop, Launch/Recover, Cloak, Sweep Mines, Use Component and Stellar Manipulation orders also run a battle check. Simultaneous: only moves and orders that acted make a sector a candidate | Spec 04 §2: turn-based only movement steps, Attack and a Seek at its target; simultaneous any order carried out that day (a waiting Sentry included) | L |
| Attack and Seek into a battle (`movement.cpp:1347-1360`) | An Attack order entering its target's sector skips the arrival battle | Spec 04 §2: every movement step checks; a Seek fights on arrival, keeps the order and attacks again after decloaking the next time the list runs | L |

## Research, intelligence, diplomacy, events, score, turn order (spec 05 §1–§6, §8–§9)

Every rule of this section follows the spec (2026-09-30). Where the spec leaves a detail
open, the engine's choices are spec 05 open questions 38–46: third empires for "Any"
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
after the last digit of the turn count. Spec 01 §14 Q41 and Q42 are the engine's
remaining guesses here. These rows are where the engine differs:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Generation edge cases (`generate.cpp:428-452` `drawNames`, `:577-615` connectivity pass, `:906`, `:1081-1093` `placeHomeworlds`) | Systems beyond the name list get generated names; the connectivity pass marks only a system it cannot link, searches any distance and always ends; "warp points anywhere" stops after 1,000 draws; a map point on a sector another empire took is skipped | Spec 01 §3.4, §3.5, §3.6: such systems get no name; the pass marks the system with everything linked to it, looks only 68 squares far and can loop forever; the draws never stop; nothing checks a taken point. The engine's choices are deliberate: keep them | L |

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
values, the replies and speech pools, and template entries of the vehicle list. The
engine's own choices where the spec is silent are spec 05 open question 37. These rows
remain, for want of a command the AI could give:

| Where | Engine now | Original | Impact |
|---|---|---|---|
| Queued facilities switched by an upgrade (`ai_economy.cpp` `planUpgrades`) | The older queued facility is removed and the newest queued in its place, so what was paid into it is lost | Spec 05 §7.5: the queued item switches to the newest version (a command that replaces a queued item in place is needed) | L |
| A war declaration with an empty speech pool (`ai_diplomacy.cpp` `initiative`) | Nothing happens: anger changes only through the declaration the AI sends | Spec 05 §7.5 `AI_Speech`: anger still becomes 100, only the declaration is not made (a command that sets anger is needed) | L |

