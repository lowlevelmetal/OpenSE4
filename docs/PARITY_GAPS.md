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
Built units go to the holders in the game's object order (spec 02 §6.5): with the
engine's object slots every planet comes before every vehicle, so after the builder they
try the planets, then the ships and bases by slot (spec 02 §13 Q52). A queued facility
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
change (Q56's command check stays an OpenSE4 choice). Two rows remain, each left to other
work:

| Item | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Object slots (`Vehicle::slot`, `placeUnits`) | Every planet comes before every vehicle, so built units try the planets in the sector before the ships | One slot order shared by every object, a new planet or vehicle taking the first slot any removed object freed (spec 02 §13 Q52, spec 03 §19 Q62). Held back until a related movement question is settled | L |
| Cargo trimmed in battle (`combat_space.cpp`) | Planets marked damaged by the planet-only types too; trimmed once after the battle, when the killed units are gone | Trimmed after each qualifying hit, against the capacity and cargo at that moment with the battle's dead still counted, never for the planet-only types (spec 02 §2 "Domes", §13 Q54). Left to the combat work | L |

## Vehicles, movement and logistics (spec 03)

Design names are unique in the whole game (`uniqueDesignName`; the original allows duplicates
from turn and empire files, an OpenSE4 choice that stands, spec 03 §19 Q50), composite orders
are expanded when given (`orders.hpp`), and units in space are held in groups that mix designs
(`Vehicle::mixed` and the group helpers of `design.hpp`). On 2026-09-30 the engine was brought in
line with the rules of spec 03 §19 as settled from the executable: the stored-double day counter,
chained actions in object-slot order, ad-hoc groups, the Ship Orders options, launches and
recovery, hazard damage, repeat battles, Sweep Mines, repair and training sources, ruins, the
destructive-centre cost map, design editing and the smaller rules. The engine's own choices
where the spec is silent are spec 03 §19 Q60–Q71. Long-range scanning follows §3.3: a
design is learned only when a human opens the report of a vehicle the scanners reach
(`cmd::OpenVehicleReport`), a ranged scanner works from any own object within its reach,
and `Long Range Scanner - System` works from any own object in the system (colonies
without population too) and covers ships and bases, not unit groups (`sight.cpp`
`scannerReaches`). A turn-based Attack goes to the sector its target was in when the order
was given and attacks there (§8, Q71). No row remains.

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
newcomers arrive or a survivor was damaged. The details the spec leaves open are the
engine's choices in spec 04 §19.1. The questions of §19.2, answered from the executable
on 2026-09-30, found the differences below.

| Item | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Planning targets (`planTarget`, `chooseMode`, `assigned_`, `incomingSeekerDamage`, `buildWeapons`) | Each weapon gets the first sorted target it can hit and affect, with no budget, no overkill total and no push/pull spreading; point-defense and warheads get none; a fighter group's weapons get separate targets; the main target is the first some weapon can engage. The overkill total adds up a side's assignments over the combat turn, counts seekers in flight and uses current hit points | Spec 04 §16 "How the computer gives out targets" and "Overkill limit", §19.2 Q60: the firing steps without the distance check; first B candidates in rounds; totals from 0 at every choice, full hit points, seekers in flight never counted | M |
| Attack map and square choice (`attackMap`, `rangeSquare`, Optimal, Maximum Range) | The mover may keep its own square; the attack map leaves point-defense and warheads out, compares unweighted damage with emissive armor, then scales and truncates, and has no border over-count; Optimal takes the lowest ratio even at 9999 or more; Maximum Range leaves point-defense out and breaks ring ties 1 in 10 | Spec 04 §16.1 "Attack map", "Choosing the square", Optimal and Maximum Range; §19.2 Q61 | M |
| Surrounded pieces and group dissolving (`combat_space.cpp`, after the leader's walk; `afterDamage`) | Surrounded is checked after the leader's move, for leaders only; a hit dissolves the group only when it takes the leader's maximum movement to 0 and the leader had movement at the start of the combat turn | Surrounded is checked once, when any computer piece not following a slot is picked, before it plans: it does not move but fires, and a leader's whole group dissolves first; after any hit, even one fully absorbed, a leader of an automated side left with 0 movement this combat turn dissolves its group, so any hit after its action is enough (spec 04 §5, §9.1 step 10, §16.1; spec 03 §10, §19 Q60) | L |
| Launching (`phasePieces`, `launchFrom`) | Each carrier is handled once per phase, so it never launches a second wave; Anti-Planet Drone designs are launched like other drones | The search restarts after every action, so a carrier launches again in the same phase when its limits and rate allow; Anti-Planet Drone stacks are not launched by the computer's batches (spec 04 §16.1, §10.7, §19.2 Q62) | M |
| Board, Ram and Drop Troops strategies (`boardTarget`, the ram target, `pathToward`, `troopTarget`) | Board without a target uses Don't Get Hurt and targets any shields-down ship, nearest first; the ram target is the first sorted target (planets and unit groups possible) and Ram without one uses Don't Get Hurt; ramming steps greedily; Drop Troops skips contested colonies and reads only Attack orders | Spec 04 §16.1 Board, Ram, Drop Troops; §19.2 Q69 | M |
| Combat groups (`setGroup`, fleet groups in `Battle::place`, `strategyIndex`) | A member keeps the offset it got when it joined; member numbers come from a counter that never reuses them; fleet groups have no number; a fleet ship in a tactical group uses its design's strategy | Places from the member number and the current leader's formation; 1 + the highest number held; fleet groups numbered 1, 2, 3… with the tactical groups; a fleet ship in any group uses the fleet's strategy (spec 04 §3 steps 5 and 8, §5, §19.2 Q64, §19.1 Q50) | L |
| Start positions (`Battle::place`, `Battle::settle`) | Middle empires ranked by colonies and vehicles anywhere in the system, vehicles by creation order; hops may leave the map; the search scans row by row and refuses a 4×4 footprint that sticks out | Ranked by the first piece already in the battle sector, by object slot; hops clamped to the map; column-by-column search; footprint squares off the map ignored (spec 04 §3 step 4, §19.2 Q57, Q58) | L |
| Battle checks (`battleForces`, `visibleTo`, `seesHostile`, `combat.cpp`) | A passed check with nobody hostile having pieces fights no battle and the mover keeps its orders; colonies are always seen and every colony counts as an uncloaked watcher; vehicles have an extra "uncloaked in an unobscured sector" fallback; in simultaneous games a minefield makes its owner a seeing side | A passed check always fights (ending at the first end check), clearing the mover's orders; colonies follow the sight rules and a cloaked colony is no watcher; no fallback; minefields take no part in the simultaneous check (spec 04 §2, §19.2 Q73–Q75) | L |
| Pursuits meeting a battle (`movement.cpp`, `entryCombat`) | A battle on a pursuit's step fails its order and clears the lists | The pursuit only stops moving for this run; its order and list are kept, also after mines, storm or turbulence damage (spec 04 §2, spec 03 §6.4, §19.2 Q76) | M |
| Mine strikes on unit groups (`combat.cpp`, mine strike) | One shield pool per unit group for the whole strike; emptied stacks kept until it ends; the mine's credit uses the tonnage at the group's first strike | Both pools reset and the dead removed after every warhead; credit per mine, from the units the group had when that mine picked it (spec 04 §10.6, §15, §19.2 Q68) | L |
| Combat simulator strategies (`simulator.cpp`) | Every side uses the viewer's strategies | Each side keeps its copied empire's list; design strategies come from the design's real owner; planets use the viewer's planet strategy (spec 04 §17, §19.2 Q71) | L |
| Unit group supply (`combat_space.cpp`, `joinUnit`, `shoot`) | Supply per unit; a group that mixes designs keeps the smallest full load | One pool per group, capacity the total `Supply Storage` of its units, refilled at every launch into it (spec 04 §19.1, Q56 bullet) | L |
| Cloaking after a battle | The battle leaves a vehicle's cloaked status as it was; end-of-turn upkeep decloaks one that can no longer cloak | Every surviving piece cloaked at the start is decloaked and cloaks again at the battle's end only if it still can; a captured ship cloaks for its new owner (spec 04 §2) | L |
| Small details (`seekerHit`, `Battle::advance`) | Seekers ignore the planet-only types and Crew Conversion; a strategic or unseen battle ends without the next turn's upkeep | Those types damage seekers like other non-hull types (modded data only); the upkeep runs once more before the end check (one more organic armor restore) (spec 04 §10.1, §4, §19.2 Q66, Q70) | L |

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
- `Ship - Moved` draws one sector number and disbands the ship's fleet (§4).

Q45 and Q46 needed no change. The "(inferred)" markers of these choices are gone (the third
empire draw, the silent Conditions Change, the log copy's default and the files' widths, the
rebel's pictures, the theft's built count, the capitals standing for home locations, the
report's cargo designs, the culprit's log in anger term 2), and the 21-order limit's is now
"(confirmed: binary)". The engine's remaining choices are open questions 50 (when the
empires present at a stellar manipulation are taken) and 51 (a stop-hostilities demand that
names no empire). No row remains.

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
Options). Spec 01 §14 Q41–Q43 are the engine's remaining guesses here (Q43: how a comet or
warp point entry of a system template claims a sector for the planets' names). This row
is where the engine differs:

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
waiting test, the units reserve). No row remains.

