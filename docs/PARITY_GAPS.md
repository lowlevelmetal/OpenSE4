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

Designs record the enemy tonnage their vehicles destroyed (`Design::enemyTonnageDestroyed`,
spec 04 §15). Tactical combat and the combat simulator follow spec 04 §3, §4 and §17; the
details the spec leaves open are the engine's choices in spec 04 §19.1. The client has the
watch-only Strategic Combat window and the Ground Combat window (spec 06 §1.6; its choices
are spec 06 question 23), and stops at a phase's launch step so that a player launches
before the side's drones and seekers move. On 2026-09-30 the remaining open questions of
spec 04 §19 were settled from the executable; these rows are where the engine differs.

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Start positions (`combat_space.cpp:425-545` `Battle::place`, `combat.cpp:953` `arrivalDirection`) | Warp arrivals use the centre box and count as an empire in the middle. Several middle empires, sorted by id and the colony owner included, get boxes next to the centre (west, east, north, south, then diagonals) and face it; edge arrivals face away from their edge (corners diagonally); with more than 60 pieces corner boxes are 24 × 24; a box spans exactly S squares; planets use the general centre box; group leaders take any square of the box | Spec 04 §3 step 4: warp arrivals in a fixed small box at the centre facing 2; middle empires numbered in the system's object order, the colony owner keeps the centre, the others take boxes two box sizes out in a fixed order; middle pieces face 1 to 4 at random, corners face 2 or 0; corner boxes 48 × 24; boxes span S + 1 squares; planets and obstacles in a fixed box; leaders on the inner line of an edge box; random hops before the nearest-free search | M |
| Shield-multiplier types (`combat.cpp:741`) | Not scaled when shields are 0; unit groups and seekers never scaled; an odd remainder is kept | Spec 04 §9.5: scaled and scaled back on every hit, so Half and Quarter lose their remainders | L |
| Unit group damage (`combat_space.cpp:1438-1500`) | Emissive armor of the unit designs acts; a non-hull hit adds the old pool and caps it back; the shield pool uses another formula; a fighter group without supplies loses the shield part of its hit points; the random draw is among living stacks only | Spec 04 §9.4: no emissive on groups; a non-hull hit is judged alone and leaves the pool unchanged; kill when P + Q reaches H, then H − X from P and X from Q; supplies play no part; every design entry is drawn, dead ones waste the draw | M |
| Ram hit points (`combat_space.cpp:890-899`) | A ship's pool is not subtracted; a unit group counts shields once more and subtracts its pool; a seeker counts all members | Spec 04 §10.3 | L |
| Organic armor (`combat.cpp:925`, `combat_space.cpp:750`) | Restoration stops at the first part the pool cannot pay; the pool empties whenever nothing is left destroyed after restoring | Spec 04 §9.3: unaffordable parts are skipped; the pool empties only when nothing was destroyed at the step's start | L |
| Seekers (`combat_space.cpp:1355-1357`, `:1508`) | Only a seeker that has not moved, of the same part and mount, takes new members; a member dies when pool + D reaches its hit points | Spec 04 §10.1: any seeker of the same empire, component and target on the square, mounts not compared; a hull-type hit kills when D + 2 × pool reaches the resistance | L |
| Mines (`combat.cpp:1134`, `:1139-1165`) | Vehicles immune to every warhead are removed before the draw; a warhead hits the parts of a group's front unit and kills at most one unit; the damage-type check also applies to unit groups | Spec 04 §10.6: the draw includes them and the mine is spent; on a unit group the damage follows §9.4 (several units may die); a unit group is affected by every type | M |
| Troops' side (`combat_space.cpp:2383`, `combat_ground.cpp:139-140`, `combat.cpp:1230`) | A troop unit fights for its design's owner: a captor cannot drop a captured ship's troops, and stored troops of a captured design count as invaders of their own planet | Spec 04 §13: troops fight for the owner of the ship that drops them; stored units serve the planet's owner | M |
| Special damage on planets (`combat_space.cpp:1552`, `:1554-1560`, `:1403-1417`) | Only Weapons kills weapon platforms; push, pull, teleport and the shield-absorbed "Only" types leave the shields alone; conversion and the reload types never affect planets | Spec 04 §9.5 "Against planets": Only Weapons does nothing; those types drain shields; reload types lengthen planet reloads, a planet-capable conversion flips the piece | M |
| Hull hits on planets (`combat_space.cpp:1565`, `:1573-1583`, `:1620`, `:1643-1651`, `:726`, `:752-755`) | The pool is added before shields and reaches population damage; other stored units are not hit when the last platform dies; population loss takes the largest group; facilities vanish at once, none when per is 0, and the 1-in-3 roll is made only when a loss is possible; planet shields drop with a lost facility and regenerate from facilities | Spec 04 §11: only the stored units keep a pool; the rest of the units take the full hit again; first group first; lost facilities work until the battle ends, all intact ones may fall when per is 0, the roll is always made; planet shields neither drop nor regenerate | L |
| Ground combat (`combat_ground.cpp:75`, `:165-172`, `:190`) | Each troop design's hull values add up; the sides' values are taken once per fight; the planet and racial modifiers both come from the base total; unarmed troops are hit before the militia | Spec 04 §13: best single hull; recomputed every round; modifiers chained; armed troops, then militia, then everything else | L |
| Ground combat between turns (`turn.cpp:99`, `combat_ground.cpp:272-301`) | Fought in the attacker's end-of-turn processing; on peace the landed troops simply wait, and fight again if war returns | Spec 04 §13: fought in the colony owner's end-of-turn processing (its ground-combat step); if the owner is at Non-Aggression or better with the landed empire (or is it), the landed troops join the colony's cargo, serve the owner, and the invasion ends | M |
| Don't Get Hurt (`combat_space.cpp:2143`, `:1010-1017`) | Minimises weapon exposure with enemies moving twice their speed, prefers room from the edges, stays when unexposed | Spec 04 §16.1: weighted straight-line distance from hostile and own pieces, weapons ignored, always the full move | M |
| Computer range strategies (`combat_space.cpp:1998-2105`) | A wanted distance (Optimal: best damage ratio; Short: 1 to 3; Maximum: longest range; one less on planets) approached greedily, exposure and the nearest seeker as tie-breaks | Spec 04 §16.1: a destination square from the danger map (including seekers aimed at the piece) and the attack map, per strategy; planning centred on the target's top-left square, no planet adjustment | M |
| Computer firing order (`combat_space.cpp:1264`, `:1281`) | Fires first when the end of its path is farther (footprint distance) from its movement target; does not fire again after moving | Spec 04 §16.1: measured from the target of its first ready weapon, aim distance, destination against current square; always fires again after moving | L |
| Holding fire for an invasion (`combat_space.cpp:2476`, `:1096`, `:675-679`) | Decided once per combat turn from the primary or secondary strategy; point-defense counts as armed; no rule for planets where friendly troops fight | Spec 04 §16: the strategy in effect, rechecked at every target choice; only weapons other than point-defense and warheads count; never targets a planet where its own or a friendly empire's troops are fighting | L |
| Design statistics (`combat_space.cpp:1709-1713`, `:1488-1489`, `:1609-1611`, `:190-191`, `:1757-1758`, `:2581`; client `designs.cpp:307`) | Keeps and shows a kills counter; credits tonnage for each unit killed; a mixed group's kill goes to the firing stack's design; a capture or conversion counts as a kill for the captor's design and a loss for the victim's | Spec 04 §15: no kill counter; a unit group's tonnage only when the whole group dies, for all its units; every design of the killing object is credited; captures change no statistic | L |
| Ram credit (`combat_space.cpp:2309-2370`) | The recoil is credited to the target (experience, tonnage, its damage modifier); no second credit for the rammer | Spec 04 §10.3: the recoil has no attacker; the rammer's design gets the target's tonnage twice and a surviving rammer +1.0 more experience | L |
| Computer launches (`combat_space.cpp:1869-1884`) | Launches satellites; drones in groups of Drones Per Target with no total limit; a fighter amount of 0 counts as 1 | Spec 04 §10.4, §10.5, §10.7: never satellites or mines; one drone per group, at most Drones Per Target × hostile ships and bases − own drones alive; 0 puts all fighters (or drones) in one launch | M |
| Player launches (`combat_tactical.cpp:388`) | One design per order; satellites one group per order; fighters and drones in the chosen group size | Spec 04 §10.4: groups per "Launch Units" window session, designs may mix, drones one each; Launch Fighters in Groups is fighters only, sizes 5 to 50 | L |
| Unrecovered units (`combat_space.cpp:2765-2780`) | Merge into the owner's group of their kind in the sector | Spec 04 §10.4: stay separate groups | L |
| Experience labels (client `reports.cpp:261`, `screens/ships.cpp:220`) | A bare "N%" | Spec 04 §15: level name plus the bonus | L |
| Battle checks (`movement.cpp:1297`, `:1362`, `:483-487`, `:839`) | Turn-based: Load, Drop, Launch/Recover, Cloak, Sweep Mines, Use Component and Stellar Manipulation orders also run a battle check. Simultaneous: only moves and orders that acted make a sector a candidate | Spec 04 §2: turn-based only movement steps, Attack and a Seek at its target; simultaneous any order carried out that day (a waiting Sentry included) | L |
| Attack and Seek into a battle (`movement.cpp:1347-1360`) | An Attack order entering its target's sector skips the arrival battle | Spec 04 §2: every movement step checks; a Seek fights on arrival, keeps the order and attacks again after decloaking the next time the list runs | L |
| Who is asked (`combat_space.cpp:2818-2836`) | Each human side hostile to another is asked separately, so sides can mix Tactical and Strategic | Spec 04 §3 step 1: one question per battle for all human empires in it, hostile or not; a notice first when the current player is a computer empire | L |
| A player's phase (`tactical.hpp:14-29`, `combat_tactical.cpp:50`, `:289`) | A launch step comes before the side's drones and seekers, and hand-launched drones act at once; Auto is per side and per phase; Resolve Combat covers one side | Spec 04 §4: drones and seekers always move before the player gets control; hand-launched drones first act next phase; Auto is one toggle for every empire from the next phase on; Resolve Combat hands every empire over to the end | M |
| Tactical groups (`combat_tactical.cpp:210-222`, `:358-420`; `combat_space.cpp:2061-2066`) | A new leader takes over a number already led; a member keeps its joining position and does not turn with the leader; no formation choice; clearing a leader dissolves its group; a computer leader blocked on its way dissolves its group | Spec 04 §5: refused; the member takes the next formation slot, turned by the leader's facing; the player picks a formation; members keep their number; no group is dissolved by a blocked move | L |
| Combat simulator (`simulator.cpp:155-180`; client `screens/simulator.cpp:28`) | Up to 4 sides, all copies of the player's empire, in a new empty system without abilities, every side beside the centre | Spec 04 §17: up to 10 sides, each a copy of the real empire owning its first item, fought in the player's home sector with its interference and disruption, start positions by side number | L |

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

