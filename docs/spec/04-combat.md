# Spec 04: Combat (space, planetary, ground, capture, tooling)

Status: reference spec for engine work. It was first written clean-room from the SE4 Deluxe
manual (HTML and PDF), the self-documenting data files, and the version history that
ships with the game (`History.txt`, `DataFileHistory.txt`). Everything is paraphrased.
The version history records rule changes made after the manual was written, so **where
it contradicts the manual, the history wins** and we cite it as "(history 1.xx)".

On 2026-09-29 most rules below were checked against the original executable. Rules
marked **(confirmed: binary)** describe what the game really does, in our own words;
where the binary disagreed with an earlier reading, the rule was rewritten. Rules still
marked **(inferred)** are our own choices. Section 19 lists the questions that were
open; on 2026-09-30 the last of them were answered from the executable, §19.1 keeps
the engine's own choices and OpenSE4's extensions, and §19.2 the questions that came up
while implementing the settled rules.

Conventions:

- Every tunable below belongs in data (a `combat` table in `rules.toml`) with the SE4
  default shown. Settings.txt names are given verbatim so a loader can map them.
- Rounding is stated per rule. "Truncate" means round toward zero. "Round" means round
  to nearest with ties to even (banker's rounding), as the original does. Integer
  division truncates.
- All randomness uses `GameState::rng`. A "roll" is a uniform integer in 1..100.
- "Intact" means a component that has not been destroyed. Destroyed components provide no
  abilities, no weapons and no movement. A component is either intact or destroyed:
  there is no partial component damage (confirmed: binary).

---

## 1. Vocabulary

| Term | Meaning |
|---|---|
| Battle | One combat at one sector (one grid location of one system map). |
| Combat map | A grid 72 squares wide and 63 squares tall; columns 0..71, rows 0..62 (confirmed: binary). |
| Piece | One occupant of the combat map: a ship or base, a colonised planet, a fighter group, a satellite group, a drone group, a seeker (group), or a neutral obstacle. |
| Neutral obstacle | An unowned object in the sector: star, warp point, comet or uncolonised planet (not storms, not uncolonised asteroid fields). It blocks squares and can never be targeted (confirmed: binary). |
| Unit group | One piece that holds several units (fighters, satellites or drones). It moves and is targeted as one piece. |
| Combat turn | One round of the battle (see §4 for the count). |
| Phase | The part of a combat turn in which one empire acts. |
| Combat group | A leader piece and member pieces that follow it in a formation. |
| Footprint | Planets and neutral obstacles cover 4×4 squares; every other piece covers one square (confirmed: binary). A big piece's position is its top-left square; its *centre square* is the top-left square plus 1 on each axis. |
| Distance | Chebyshev distance (a diagonal step counts 1) (confirmed: binary). Two different distances are used: **range distance** (for weapon damage and reach) is the distance to the nearest square of a big piece's footprint, and **aim distance** (for the to-hit penalty) is the distance between the two pieces' top-left squares (confirmed: binary). For one-square pieces both are the same. |

## 2. When combat happens

**Turn-based games** (confirmed: binary unless marked).

- Only three things run a battle check, at the group's sector: every movement step of a
  group (a warp jump included), after mines have struck (§10.6); the Attack order; and a
  Seek order whose group is already at its target. Sentry and every other order never
  start a battle, and vehicles that merely sit in a sector do not either.
- The Attack order spends 1 movement point and runs the check; it is then removed, and
  without movement left it is removed doing nothing. A Seek order at its target decloaks
  the group, attacks, and stays at the head of the list, so it attacks again (and may
  start a new battle) every time the list runs: at the start of each of the owner's turns
  and whenever orders are given.
- **Who must see whom.** The check is one-directional: some empire of the moving group
  must see an object in the sector belonging to an empire it is hostile to. If the whole
  moving group is cloaked, another empire present there with an uncloaked object must
  instead see one of the group's objects and be hostile to it. Mothballed ships,
  planets and minefields all count as present; nothing is skipped for being mothballed,
  so a hostile group moving in on a lone mothballed ship it can see starts a battle.
- **Hostility** follows treaties: an empire is hostile to another when its treaty with
  it is below Non-Aggression, that is War, Non-Intercourse, "None" (met, no treaty) or
  not yet met. Non-Aggression and anything better never fight.
- Once a battle starts, **every owned object in the sector** becomes a piece, including
  the vehicles and planets of empires that are hostile to nobody there, and every piece
  is decloaked for the rest of the battle (history 1.28). Treaties still apply inside
  the battle: no piece fires on or is fired on by a non-hostile empire.
- Mines are not combat pieces. They act when a vehicle group enters their sector (§10.6).
- **Orders.** A group whose movement step or warp jump started a battle fails its order,
  and every member's whole order list is cleared (spec 03 §6.4). An Attack order is used
  up; a Seek order stays. Every other participant keeps its orders, except that a Sentry
  order at the head of its list is removed (spec 03 §6.3), and a ship that changes owner
  in the battle loses its orders (§12).
- **Who sees the battle.** A battle is shown only when at least one human-controlled
  empire has a piece in it, hostile or not; otherwise it is resolved unseen. In a game
  played on one machine, one question per battle, answered at the machine, applies to
  all human empires in it (§3 step 1). In a game played on different machines nobody is
  asked: the battle is shown in the strategic window when the player whose turn it is is
  human, and resolved unseen when it is a computer empire.

**Simultaneous games** (confirmed: binary). Tactical combat is never offered, and the
computer resolves every battle. The manual's older rule (combat only on every 5th day)
was replaced (history 1.15, 1.42). After each of the 30 daily steps of the movement phase,
every sector where an object (a vehicle or a colony) carried out an order that day is
checked, whatever the order: a Sentry that just waits counts too. The check passes when an
empire with an uncloaked vehicle or unit group in the sector sees an object (not a
minefield) of an empire it is hostile to; a colony alone never counts as the side that
sees. A sector that already had a battle this game turn is skipped when every owned
object now there took part in it, so a standing order can start one battle per game turn,
and a second only when newcomers arrive. No order list is cleared. Results arrive as log
entries. Battles are not shown unless the Settings flag `Simultaneous Games Show
Strategic Combat` is set; then every battle is shown in the strategic window.

**Game option "No Tactical Combat".** It is one of the game's general options. With it
on, the combat window offers only the strategic view, so every battle is strategic
(confirmed: binary).

## 3. Battle setup

1. **Tactical or Strategic** (confirmed: binary). In a game played on one machine, one
   question per battle, answered at the machine, chooses **Tactical** or **Strategic**
   for all human empires in it (hostile or not); when the player whose turn it is is a
   computer empire, a notice naming the system and the empires comes first. With
   Tactical, every human-controlled empire in the battle plays its phases by hand and
   computer empires follow their strategies. With the "No Tactical Combat" option only
   the strategic view is offered. Games on different machines, and simultaneous games,
   never ask (§2). The rules are identical; only control differs.
2. **Pieces.** Every owned object in the sector becomes a piece: ships and bases, planets
   with a colony, satellite, fighter and drone groups already in space. Minefields and
   storms never become pieces. Unowned stars, warp points, comets and uncolonised
   planets become neutral 4×4 obstacles (confirmed: binary).
3. **Defenders and attackers.** Each vehicle remembers the sector it last left when it
   moved this game turn; a vehicle that has not moved since the last turn processing,
   and every planet and satellite group, counts as having been in its current sector.
   An empire is a **defender** if one of its pieces was already in the battle sector,
   otherwise an attacker (confirmed: binary). A vehicle that came through a warp point
   left a sector of another system, so its empire is an attacker unless another of its
   pieces was already there (confirmed: binary).
4. **Placement** (confirmed: binary).
   - **Box size.** Let S be 6 when the battle has at most 20 pieces, 12 for 21 to 40, 18
     for 41 to 60 and 24 for more than 60. A box is given below by its top-left square
     and its width and height; a piece's square is drawn with both ends included, so a
     box of width S spans S + 1 columns. Let m be 2 in battles of more than 60 pieces and
     1 otherwise.
   - **Arrivals from a neighbouring sector** of the same system start against the
     matching edge or corner of the map (north is the top of the map):

     | Came from | Top-left square | Width × height | Facing |
     |---|---|---|---|
     | North-west | (0, 0) | mS × S | 2 |
     | North | (36 − mS/2, 0) | mS × S | 2 |
     | North-east | (71 − mS, 0) | mS × S | 2 |
     | West | (0, 31 − mS/2) | S × mS | 1 |
     | East | (71 − S, 31 − mS/2) | S × mS | 3 |
     | South-west | (0, 62 − S) | mS × S | 0 |
     | South | (36 − mS/2, 62 − S) | mS × S | 0 |
     | South-east | (71 − mS, 62 − S) | mS × S | 0 |

     So in battles of more than 60 pieces the corner boxes are 48 wide and 24 deep, like
     the other boxes on the top and bottom edges. Facings are the numbers of spec 03 §10
     (0 keeps the formation as drawn, 1 turns it a quarter turn clockwise, 2 a half
     turn, 3 three quarters).
   - **Arrivals through a warp point** (the sector they left is in another system) start
     in a small box at the exact centre, whatever the battle size: top-left square
     (34, 29), 4 × 4, so x 34 to 38 and y 29 to 33. They face 2.
   - **Pieces already in the sector.** Planets and obstacles take a random top-left square
     in x 33 to 39, y 28 to 34, whatever the battle size. Every other piece already in
     the sector (satellite groups included) uses the centre box: top-left (36 − S/2,
     31 − S/2), S × S. Each piece in the middle gets a random facing from 1 to 4 (never 0).
   - **Several empires in the middle.** Count the empires (not neutral obstacles) that
     have a piece already in the battle sector, and number them 1, 2, 3… in the order in
     which the system lists their first object. If there are two or more, the owner of a
     colonised planet in the sector keeps the centre box for all its pieces, and every
     other such empire uses the box for its number, each S × S with the top-left square:
     1 (36 − 2S, 31 − 2S) up-left; 2 (36 + 2S, 31 + 2S) down-right; 3 (36 + 2S, 31 − 2S)
     up-right; 4 (36 − 2S, 31 + 2S) down-left; 5 (36 − S/2, 31 − 2S) up; 6 (36 − S/2,
     31 + 2S) down; 7 (36 + 2S, 31 − S/2) right; 8 (36 − 2S, 31 − S/2) left. Numbers
     above 8 use the centre box. The layout is not symmetric (the down and right boxes
     lie farther out), and in bigger battles some of these boxes lie partly or wholly off
     the map; a piece drawn off the map is moved as below. If the sector holds colonies
     of two empires, only the one listed last keeps the centre. Warp arrivals are not
     counted here. Their facing stays random (1 to 4).
   - **Order and squares.** Planets and obstacles are placed first, then group leaders,
     then everything else. A group leader in an edge box starts on the box's inner line
     (the row or column nearest the map centre, for example row S for the top edge) at a
     random point along it; elsewhere it takes a random square of its box. A formation
     member goes to its formation slot relative to its leader (spec 03 §10). Any other
     piece takes a random square inside its box. If that square is taken or off the map,
     the piece first makes up to ten random hops of its own size in one of the four
     straight directions, stopping on the first free square; failing that, it takes the
     first free square in growing squares around the square first drawn (up to 100
     squares away).
5. **Combat groups.** Each fleet becomes one combat group: the fleet leader leads it and
   the fleet's formation applies.
6. **Starting values** (confirmed: binary). Shields start at their maximum (§9.2),
   except that a ship or fighter group with zero supplies starts with none. Every
   weapon's reload counter is 0 (ready). Movement points are full.
7. **Phase order** (confirmed: binary). The order in which empires act is drawn once,
   at setup, and kept for the whole battle: all defenders first in a random order, then
   all attackers in a random order. An empire that has pieces both already in the sector
   and arriving acts once per combat turn, in the defenders' part.
8. Assign each piece a strategy. While a ship is in its fleet's combat group, it uses the
   fleet strategy. Once it leaves the group (broken formation, group dissolved), it
   uses its design's default strategy (history 1.84, the latest ruling).

## 4. Combat turn sequence

**Length** (confirmed: binary). A turn counter starts at 1 and goes up by one when all
empires have had their phase. The battle ends as soon as the counter reaches Settings
`Number Of Space Combat Turns` (30), so at most **29** combat turns are fought with the
default setting. It also ends as soon as no two empires that still have pieces are
hostile. A side with no weapons left does not end the battle.

**Start of every combat turn after the first** (confirmed: binary), for each piece:
lower every non-zero reload counter by 1; add shield regeneration and organic armor
regeneration (§9.2, §9.3); restore movement points; clear weapon targets, the
per-turn launch counts and the list of targets engaged this turn.

**Each empire's phase**, in the fixed order (confirmed: binary):

1. Its drones move and attack (drones are always computer-controlled).
2. Its seekers move and may strike (§10.1).
3. Its other pieces act: by hand for a human player, or through their strategies for
   the computer (§16.1).

Steps 1 and 2 run for human sides too, before the player gets control, so a player
cannot launch before the side's drones and seekers move. Point-defense may fire at any
moment, including during other empires' phases (§10.2).

**The tactical window** (confirmed: binary).

- A player's pieces act only by order: move, fire (§6), and the special orders Launch
  Units, Launch Fighters in Groups, Drop Troops, Ram Ship, Capture Ship, Resolve Combat and
  the four group orders (§5). Drones cannot be moved or fired by hand; drones a player
  launches first act at the start of the side's next phase. Pieces the player leaves
  idle do not fire by themselves when the phase ends.
- **Auto** is one toggle for the whole battle and every empire. Pressing it does not
  play the current phase; from the next phase on, every empire follows its strategies,
  and play pauses after the phase of the last human empire in each combat turn (End Turn
  goes on). Releasing it gives each empire back its normal control (in the simulator,
  only side 1 goes back to hand control).
- **Resolve Combat** asks for confirmation, then hands every empire to its strategies,
  disables the window's buttons and runs the battle to its end.
- The battle's end (§4 Length) is checked only after a phase, never in the middle of a
  player's phase; in the strategic window only after a whole combat turn.
- **No saving during a battle.** Both combat windows are modal and offer no way to save;
  their options hold display settings only. Neither can be closed before the battle is
  over (the simulator alone has Stop Combat), and a strategic battle runs to its end once
  begun.

## 5. Movement in combat

- **Movement points** (confirmed: binary). A ship's combat movement is half its
  system-map speed, rounded up (speed/2 + 0.01, then Round), plus the best single
  `Combat Movement` value among its intact components. The system-map speed already
  includes the 1-point override for missing supplies or command (see the ships spec).
  Fighter and drone groups use the same formula with the group's speed. Planets,
  satellites, bases with no engines and mothballed ships have 0. A seeker's movement is
  its `Weapon Seeker Speed`.
- Each step to one of the 8 neighbouring squares costs 1 point (confirmed: binary).
  When the next square is blocked, the piece tries up to four other squares around it,
  then stops. Unused points are lost at turn end. A computer-controlled piece makes one
  move per phase and then has no points left.
- **Occupancy.** A square holds one piece; big pieces fill their 4×4 footprint. Seekers
  may share squares.
- **Launched units** (fighters, drones) get their full movement in the turn they launch
  (confirmed: binary; history 1.55, 1.71), except that a player's hand-launched drones
  first act at the side's next phase (§4). A seeker cannot move in the turn it is
  launched (§10.1).
- **Combat groups** (confirmed: binary unless marked). When a leader moves, by hand or
  by strategy, each member moves toward its formation slot around the leader's new
  square (spec 03 §10, turned by the leader's facing), spending its own movement points.
  The move itself never dissolves a group: a blocked leader keeps it. In the tactical
  window groups are numbered, and the player picks the number from a list:
  - **Set Group Leader** is refused if another piece of that player already leads that
    number, or if the piece belongs to a group. The player then picks a formation;
    cancelling sets no group.
  - **Set Group Member** needs an existing leader with that number and is refused for a
    piece that already leads or belongs to a group. The member takes the next position
    of the leader's formation, not the square where it stands.
  - **Clear Group Assignment** clears only that piece. Members of a cleared leader keep
    their number and follow whichever piece leads that number later. **Clear All Group
    Assignments** clears every piece of the side.
  - Moving a member by hand keeps it in the group.
  Hotkeys give 10 groups (0–9). A computer piece leaves formation when its strategy in
  effect or its category's `Break Formation` flag says so (§16.1); the manual's
  "leader surrounded" case (history 1.21) was not found as a separate rule.
- **Point-defense on the move** (confirmed: binary). After every step of a fighter or
  drone group, hostile point-defense in range may fire at it. After every step of any
  other piece, that piece's own point-defense may fire at a hostile target now in
  range.
- **Forced movement** from push, pull and teleport weapons is described in §9.5.

## 6. Firing

Weapons of a piece fire one at a time in the order of the design (confirmed: binary).
A weapon may fire at its assigned target only if all of the following hold (confirmed:
binary):

1. The component is intact, its Weapon Type is not `None` or `Warhead`, and its reload
   counter is 0.
2. For a ship or base and for a fighter group: its current supplies are above 0.
   Satellites, drones, weapon platforms and planets never need supplies to fire. Bases
   (hull vehicle type Base) and vehicles with an intact Quantum Reactor have unlimited
   supplies. A ship with no supply storage at all has no supplies, so it can never fire
   (confirmed: binary).
3. The target is hostile, and its category is in the weapon's target set (§18.1).
4. The weapon's damage at the range distance is greater than 0 (§8).

**Firing by hand** (confirmed: binary). In the tactical window a player fires the weapons
ticked in the piece's weapon list by clicking an enemy. Firing at a non-hostile piece or a
neutral obstacle is refused. Each ticked weapon takes the target within the target budget
(below); weapons that did not fire lose it again. The conditions above apply as for the
computer, but the damage type is not checked (the computer never assigns a weapon whose
type cannot affect the target, §16), and point-defense may be fired this way.

**Supply use.** Each shot uses the component's `Supply Amount Used`, scaled by the
mount's `Supply Percent` (Round), times the number of weapons fired together (confirmed:
binary). Supplies never go below 0.

**Target budget** (confirmed: binary). The first time in a combat turn that a direct-fire
or seeking weapon fires at a target, that target is added to the piece's engaged list.
A weapon may be given a new target only while the engaged list is shorter than the
piece's budget (or the target is already on it):

- A ship or base: its best single `Multiplex Tracking` value, or 1 if it has none.
- A planet: 10.
- A fighter group: 1.
- A satellite group or drone group: the larger of its best `Multiplex Tracking` value
  (at least 1) and the number of units still alive in it.

Point-defense neither uses nor is limited by the budget.

After firing, the reload counter is set to `Weapon Reload Rate`: 1 fires every turn, 2
every other turn. Push, pull and teleport weapons spread over as many different enemies
as possible rather than piling onto one (history 1.73).

**Grouped fire** (confirmed: binary). In a fighter group, all identical weapons (same
component and mount) fire together as one shot: each member weapon rolls to hit on its
own, and the damage of those that hit is added up into **one** hit. Every other piece
fires each weapon separately: a satellite group fires each weapon of each satellite on
its own, and a planet fires each weapon of each weapon platform on its own, each with
its own roll.

## 7. Chance to hit

Direct-fire and point-defense shots roll to hit. Seekers, drones, rams and warheads never
roll. A seeker hits when it reaches its target, unless it is shot down first.

```
chance = Base + Offense + SystemBonus + WeaponModifier
         − Defense − PerSquare × aimDistance − Interference
chance = clamp(chance, 1, 99)
hit if roll(1..100) ≤ chance
```

(confirmed: binary)

- `Base` = Settings `Combat Base To Hit Value` (100). `PerSquare` = Settings `Combat To
  Hit Modifier Per Square Distance` (10). `aimDistance` is the distance between the two
  pieces' top-left squares (§1), so a planet is aimed at, and aims from, its top-left
  square.
- `WeaponModifier` = the weapon's `Weapon Modifier` plus the mount's `Weapon To Hit
  Modifier`.
- `SystemBonus` = the firing empire's `Combat Modifier - System` total in this system
  (below). It helps offense only; there is no system bonus on defense.
- `Interference` = the total `Sector - Sensor Interference` of the system's own
  abilities and of the objects in the battle sector.
- The clamp happens once, after every modifier (history 1.56).
- **Weapons Always Hit**: if the firing vehicle (or, for a planet, one of its
  facilities or weapon platforms) has this ability, the chance becomes 100 after the
  clamp. This applies to point-defense as well as direct fire (confirmed: binary).

**How abilities add up** (confirmed: binary). For `Combat To Hit Offense Plus/Minus` and
`Combat To Hit Defense Plus/Minus`, only the best intact component of each component
`Family` counts, and the best values of different families add up. The hull's own
values add as well. So ECM, stealth armor and scattering armor (different families)
stack, but two ECM parts do not.

**Racial modifiers** (confirmed: binary). The culture's `Space Combat` value applies to
both offense and defense. The Aggressiveness characteristic adds (value − 100) to
offense, Defensiveness adds (value − 100) to defense. Racial traits that modify the
same things add their values too.

**Offense** of the firing piece:

| Piece | Offense |
|---|---|
| Ship or base | Components and hull as above + truncated crew experience + racial offense + truncated fleet experience (when in a fleet). A mothballed ship has 0. |
| Ship with a Neural Combat Net (`Combat Best Experience`) | The larger of its own offense and the best truncated crew experience among its empire's pieces in the battle (the whole offense is replaced, not only the experience part). |
| Planet | Offense plus/minus of its facilities and weapon platforms + racial offense + Settings `Planet Combat Offense Modifier` (+30). No experience. |
| Unit group | The best (Offense plus − minus) among its unit designs, never below 0, + racial offense. No experience. |
| Seeker | 0 (seekers do not fire). |

**Defense** of the target piece:

| Piece | Defense |
|---|---|
| Ship or base | As offense, with the defense abilities and Defensiveness. The Neural Combat Net rule applies the same way. |
| Planet | Settings `Planet Combat Defense Modifier` (−200) only. In practice planets are hit 99% of the time. |
| Unit group | The best (Defense plus − minus) among its unit designs, never below 0, + racial defense. |
| Seeker | Settings `Seeker Combat Defense Modifier` (+40) only. |

**System modifiers** (confirmed: binary). For each empire, `Combat Modifier - System`,
`Damage Modifier - System` and `Shield Modifier - System` are totals over the system:
the system's own value, plus, for every colony and vehicle the empire owns anywhere in
the system, the best single value among that colony's facilities or that vehicle's
components. Several colonies with such facilities therefore add up. These totals, and
the sector's interference and shield disruption, are worked out once, when the battle
is set up, and never recomputed during it: losing parts or facilities, or a piece
changing sides, does not change them (a piece that changes sides uses its new owner's
totals). The combat simulator never works out the empire totals, so they are 0 there;
the location's interference and disruption do apply (§17).

Worked example: a destroyer (hull +20 defense) with ECM I (+20) whose top-left square is
5 squares away, fired on by a ship with Combat Sensors I (+25) and no other modifiers:
100 − 50 + 25 − 40 = 35%.

## 8. Damage at range, mounts, damage modifiers

- **Encoding.** `Weapon Damage At Rng` holds 20 space-separated integers. Entry `k`
  (1-based) is the damage when the target is `k` squares away (range distance), so entry
  1 means adjacent. An entry of 0 means the weapon cannot reach at that distance. Tables
  need not be monotonic. Examples: a basic cannon lists "20 20 0 …", which is 20 damage
  at 1–2 squares. A falloff missile lists 70 down to 45 over 1–6 squares.
- **Seekers** index the table by the squares the seeker has travelled since launch, not
  by the launcher's distance at firing (confirmed: binary).
- **Warheads** (in rams and mines) and the weapons of ground troops count with their
  *largest* table entry at any range, with the mount (confirmed: binary); for stock
  warheads that is entry 1.
- **Special damage types** reinterpret the numbers (§9.5).
- **Mounts** (CompEnhancement.txt, §18.2) apply only when the mount is valid for the
  component (weapon type and family requirements). With a valid mount (confirmed:
  binary): the table index is `clamp(r − RangeModifier, 1, 20)`, and the entry is then
  multiplied by `Damage Percent` / 100 and Rounded. Without a mount, the index is `r`,
  and ranges outside 1..20 do no damage. Note the clamp at 20: with a mount, any range
  beyond the table reads entry 20.
- **Cap.** No single weapon deals more than 50000 per shot (history 1.14); the cap is
  applied to the table value with the mount (confirmed: binary).
- **Damage Modifier - System** (confirmed: binary): when a hit lands, its damage is
  multiplied by (100 + the attacker empire's `Damage Modifier - System` total) / 100 and
  Rounded. This happens before shields, and also changes the value that special types
  use (reload turns, conversion chance, push distance).

## 9. Damage application

### 9.1 Pipeline for one hit (confirmed: binary)

Let D be the hit's damage after §8.

1. **Damage Modifier - System** (§8).
2. **Damage pool.** Every ship, base and seeker keeps a pool of damage that was too
   small to destroy anything. For the *hull-damaging types* (Normal, Skips Normal
   Shields, Skips Armor, Skips Shields And Armor, Skips All Shields, Quad/Double/
   Half/Quarter Damage To Shields), the pool is added to D and emptied. A unit group, and
   the units stored on a planet, keep their own pools (§9.4); a planet piece itself keeps
   none. No pool outlives the battle: a ship's pool belongs to its piece, and the pools
   of unit groups and stored units go back to 0 at the end of every battle (and after
   storm, warp-turbulence, mine and stellar damage), when the units killed are removed.
   Pools are never saved.
3. **Push, pull and teleport** move the target now (§9.5) and the hit then continues.
4. If the weapon cannot target this kind of piece, D becomes 0.
5. **Crew Conversion**, **Increase Reload Time** and **Disrupt Reload Time** take effect
   now (§9.5) and set D to 0. Shields do not stop them.
6. **Shields** (§9.2), unless the type skips them. Unit groups and seekers have no
   piece-level shields (their pool counts as 0); a unit's shields count as hit points
   instead (§9.4). The Quad/Double/Half/Quarter scaling still happens against a pool of 0
   (§9.5).
7. **Crystalline armor** (§9.3), for Normal, Skips Normal Shields, Skips All Shields and
   Quad/Double/Half/Quarter only.
8. **Emissive armor** (§9.3), for the same types. Only ships and bases have it: unit
   groups and planets never use emissive armor.
9. If D is still above 0 (and the type is not Shields Only), the damage goes to the
   target: components of a ship (§9.1a), units of a group (§9.4), cargo and population
   of a planet (§11), or a seeker (§10.1).
10. If the target survives, its shields are capped at their new maximum, and its
    movement points at the new movement allowance.

**§9.1a Components of a ship or base** (confirmed: binary).

- **Candidates.** All intact components, narrowed by the damage type: `Only Weapons` →
  weapons; `Only Engines` → components with `Standard Ship Movement`; `Only Shield
  Generators` → components that generate shields; `Only Master Computers`, `Only
  Boarding Parties`, `Only Security Stations`, `Only Planet Destroyers` → components with
  `Master Computer`, `Boarding Attack`, `Boarding Defense`, `Destroy Planet Size`; `Skips
  Armor` and `Skips Shields And Armor` → components without `Armor`, but if only armor is
  left, all of it.
- **Order.** The candidates are drawn one at a time at random, each draw weighted by the
  component's structure (`Tonnage Structure` × the mount's `Tonnage Structure Percent` /
  100, Rounded). A drawn non-armor component goes to the end of the queue; a drawn armor
  component goes to the front. The result: all armor first, then everything else, in a
  random order that favours big components. A new queue is drawn for every hit.
- **Destruction.** Walk the queue: while D is at least the structure of the next
  component, destroy that component and subtract its structure from D. Stop at the first
  component D cannot destroy; it is not damaged at all.
- **Leftover.** For a hull-damaging type, what is left of D goes into the ship's damage
  pool (step 2) and joins the next such hit, before that hit's shields. For the "Only"
  types the leftover is lost.
- The ship is destroyed when all its components are destroyed.

Stock SE4 is **not leaky**. Nothing passes the shields until they reach 0, and nothing
reaches internals while any armor is intact. The damage types in §9.5 are the only
built-in bypasses.

### 9.2 Shields (confirmed: binary)

- **Maximum** = total `Shield Generation` + total `Phased Shield Generation` (for a
  planet: + total `Planet - Shield Generation`) of intact components or facilities,
  each component scaled by its mount's `Shield Percent` / 100 and Rounded. If that is
  above 0, add the empire's `Shield Modifier - System` total (only when positive).
  Then subtract the battle sector's total `Sector - Shield Disruption`, never going
  below 0. Weapon platform shield parts do not add to planet shields.
- A ship or fighter group with zero supplies has no shields (history 1.65). A mothballed
  ship has none.
- **One pool, one kind.** A piece has a single shield pool. It counts as *normal*
  shields if it has any normal `Shield Generation`, and as *phased* only if all its
  shields are phased. Planets whose shields come only from `Planet - Shield Generation`
  count as neither, so phased weapons pass them.
- Shields absorb `min(shields, D)` for normal damage. `Skips Normal Shields` ignores
  shields unless the pool is phased. `Skips All Shields`, `Skips Shields And Armor`,
  `Only Weapons`, `Only Shield Generators` and `Only Master Computers` ignore shields.
  Every other type (including `Only Engines`, the other "Only" types, the planet types
  and push/pull/teleport) is absorbed by shields first.
- **Shield Regeneration**: at the start of each combat turn after the first, a ship or
  base regains the total `Shield Regeneration` of its intact parts, up to its maximum,
  while it has supplies. Planets and unit groups never regenerate shields in combat.
- When a generator is destroyed, the maximum drops and current shields are capped at the
  new maximum. The system shield total used then is the one taken at setup (§7) for the
  piece's current owner.
- Shields refill at the start of the next battle.

### 9.3 Armor specials (confirmed: binary)

- **Emissive** (`Emissive Armor`, largest single value E). Compare E with the hit's
  damage *before* shields (after the system damage modifier, without the pool). If the
  hit is E or less, nothing reaches the hull (shields were still drained) and the pool
  keeps its old value. Otherwise E is subtracted from what got past the shields.
  Emissive does not act against Skips Armor, Skips Shields And Armor or the "Only"
  types. Only ships and bases use it; unit groups and planets never do.
- **Organic** (`Armor Regeneration`, total V). At the start of each combat turn after the
  first, while it has supplies, the ship adds V to a regeneration pool (at most 10000).
  Destroyed components with `Armor Regeneration` are then restored whole, in design order,
  each one costing its structure (with the mount) from the pool. A destroyed component
  that costs more than what is left is skipped, and cheaper ones later in the order are
  still restored. When no such component was destroyed at the start of this step, the
  pool empties, so nothing is saved up before the first loss (history 1.80); when some
  were, whatever is left after restoring stays in the pool. At the end of the battle, up
  to 10000 points' worth are restored at once (history 1.79). A restored component takes
  effect at the piece's next recalculation, that is after the next hit on it.
- **Crystalline** (`Shield Generation From Damage`, total V). From each hit that gets
  past the shields, the piece gains min(V, damage) shield points, up to its shield
  maximum (only if the maximum is above 0). The damage itself is **not** reduced. It has
  no effect on unit groups, whose piece-level shields never absorb anything.
- **Stealth and scattering armor** add a defense bonus that stacks with ECM (§7), plus
  cloaking or scanner-jamming effects covered in the sight spec.

### 9.4 Destroyed and crippled

- **Destroyed:** every component destroyed; a self-destruct; the loser of a ram; the
  self-destruct triggered by boarding (§12).
- **Crippled** (the vehicle survives but is impaired): lost command gives 1 speed on the
  system map and so 1 combat movement; lost engines reduce movement; lost weapons cannot
  fire; lost shield generators cut shields. When a part with `Cargo Storage` is
  destroyed, the cargo is cut to the new capacity at once, during the battle: population
  first, 1M at a time from the first population group, then units one at a time from
  the first stack (spec 03 §11). Restoring the part (organic armor, repair) does not
  bring the cargo back. The same happens when mines, storms or any other damage destroy
  the part (confirmed: binary).
- **Unit groups** (confirmed: binary). A group (and the units stored on a planet) has a
  damage pool P and a shield pool Q, both starting at 0.
  - A `Shields Only` hit adds D to Q and kills nothing.
  - A hull-damaging hit adds D to P (P at most 50000). Any other type is judged on its
    own D alone: P is set aside for that hit and comes back unchanged afterwards, and
    what that hit leaves is lost.
  - Then, up to 20 times, one of the group's design entries is drawn at random, all
    equally likely; an entry whose units are all dead wastes the draw. A unit's hit
    points H are its design's structure plus its shields X, where fighters, troops and
    weapon platforms count X twice (their structure already includes X once). If the
    damage type skips the unit's shields, a unit of that entry dies when P reaches H − X,
    and Q does not help. Otherwise it dies when P + Q reaches H.
  - After a kill: if Q is 0, H leaves P; otherwise H − X leaves P and X leaves Q, neither
    going below 0. The rest stays for later hits. The group dies with its last unit.
  - Killed units stay in the group's records until the battle ends, and the group's
    abilities (to-hit values, tracking and the like) are still totalled over all its
    units, killed ones included. At the end of the battle the killed units are removed and
    P and Q go back to 0.
  - Unit groups have no piece-level shields and no emissive armor (§9.1).

### 9.5 Damage types (`Weapon Damage Type`) (confirmed: binary unless marked)

| Type | Effect |
|---|---|
| `Normal` | §9.1. |
| `Shields Only` | Drains shields only. Harmless once shields are 0. Against a unit group it fills the group's shield pool. |
| `Quad/Double/Half/Quarter Damage To Shields` | D is first multiplied by 4 or 2, or divided by 2 or 4 (truncated), and drains the shields. What is left is converted back (divided by 4 or 2, truncated, or multiplied by 2 or 4) and continues as Normal. This happens on every hit with D above 0, even when the shields are 0 and on pieces without shields (unit groups, seekers): Quad and Double then come back unchanged, while Half loses an odd remainder and Quarter a remainder below 4, so Half with 1 damage or Quarter with 3 does nothing. |
| `Skips Normal Shields` | Ignores the shield pool unless it is phased, then Normal. |
| `Skips All Shields` | Ignores shields, then Normal. |
| `Skips Armor` | Shields first, then only non-armor components; armor only when nothing else is left. |
| `Skips Shields And Armor` | As Skips Armor, without shields. |
| `Only Engines` | Shields absorb (history 1.70), then only engine components. |
| `Only Weapons`, `Only Shield Generators`, `Only Master Computers` | Ignore shields; only those components. |
| `Only Boarding Parties`, `Only Security Stations`, `Only Planet Destroyers` | Shields absorb; only those components. Also used internally by boarding (§12). |
| `Increase Reload Time` | Adds D (turns) to every weapon's reload counter on the target, at most 250. No effect on a vehicle with an intact Master Computer. Not stopped by shields. |
| `Disrupt Reload Time` | Same, but also works against Master Computers (history 1.13). |
| `Crew Conversion` | A roll ≤ D converts a ship to the attacker. Always fails if the ship has a Master Computer, intact or destroyed, in its design (history 1.81). Not stopped by shields. See §12. |
| `Pushes Target` / `Pulls Target` | Moves the target up to D squares directly away from / toward the firer, one square at a time; it stops at the first taken square, at the map edge, or (pull) next to the firer. If the two share a square, a push follows the firer's facing. Planets cannot be moved, unit groups cannot push or pull, and a ship can push or pull another ship only if its hull `Tonnage` is at least the target hull's. After moving, D still counts as damage against shields and components. |
| `Random Target Movement` | Moves the target to a random free square at least 3 squares from the top and left edges (up to 100 tries). D is ignored for the move; size limits do not apply; planets cannot be moved. D then still counts as damage. |
| `Plague Level 1..5` | Planets only, after shields. Raises the plague to that level (never lowers it) unless the owner's race is immune. |
| `Only Planet Population` | After shields, kills D ÷ `Damage Points To Kill One Population` million population (truncated, at least 1), from the first population group on. |
| `Only Planet Conditions` | After shields, lowers the planet's conditions (the 0–1.5 scale of spec 02) by D × 0.1, never below 0. D is after the damage modifier and shields, so a stock 20-damage weapon that gets through wipes a planet's conditions to 0. |
| `Only Resupply Depots`, `Only Spaceports` | After shields, destroys one facility with `Supply Generation` / `Spaceport`: one intact facility of the first such stack in the planet's list. Like every facility lost in combat, it keeps working until the battle ends (§11). |

**Against planets** (confirmed: binary): `Only Engines`, `Only Boarding Parties`, `Only
Security Stations`, `Only Planet Destroyers`, `Pushes Target`, `Pulls Target` and `Random
Target Movement` drain the planet's shields and then do nothing (planets never move).
`Only Weapons`, `Only Shield Generators` and `Only Master Computers` skip shields and do
nothing at all; in particular `Only Weapons` does not hit weapon platforms. `Increase
Reload Time` and `Disrupt Reload Time` add D to every planet weapon's reload counter (at
most 250; a planet has no Master Computer). A `Crew Conversion` weapon able to target
planets (no stock weapon is) that succeeds makes the planet's piece fight for the
converting empire for the rest of the battle, without changing the colony's owner.

Drones apply special types too (history 1.86). A mine's warhead whose damage type cannot
affect the vehicle it picked is skipped (history 1.70), but the mine is used up all the
same (§10.6).

## 10. Special weapons and units

### 10.1 Seekers (confirmed: binary)

- Firing a `Seeking` weapon uses supply and reload as usual and creates a seeker piece on
  the launcher's square (for a planet, one of its four central squares, at random),
  aimed at the weapon's target. If a seeker of the same empire, component and target
  already sits on that square, the new one joins it instead: a seeker group counts its
  members. That is so even if the seeker there has already moved or was launched by
  another piece; the group keeps its travelled count and its first launcher (who gets
  the kill credit). Mounts are not compared.
- A seeker has 0 movement in the turn it is launched. From the next combat turn it
  moves up to `Weapon Seeker Speed` squares in its owner's phase, before the owner's
  other pieces act. Each step moves one square toward the target's centre square (its
  own square for one-square pieces), diagonally when needed.
- After each step the seeker's travelled count goes up by 1. It **expires** when its
  target is gone or no longer hostile (history 1.04, 1.18), or when the table entry at
  its travelled count (capped at 20) is 0 — so a gap in the table ends the flight.
- **Impact.** When the seeker reaches the target's centre square, it deals
  table[travelled] × (members in the group) as one hit of the weapon's damage type, with
  the launching empire's damage modifier. No roll. The launcher gets the kill credit
  (history 1.87).
- **Durability.** A seeker has `Weapon Seeker Dmg Res` hit points R (the mount does
  not change it) and defense +40 (§7). Its remaining hit points are R minus its pool P, for one member
  whatever the group's size. A hull-damaging hit already carries P (§9.1 step 2), so it
  destroys one member when D + 2 × P reaches R; otherwise the pool becomes P + D. Another
  type destroys one member when D reaches R − P; otherwise D joins the pool.
  Point-defense may fire after every seeker step.
- Example: a speed-5 missile with 60 damage out to 8 squares launched at a target 8
  squares away waits one turn, then needs two more turns to arrive.

### 10.2 Point-defense (confirmed: binary)

- A `Point-Defense` weapon rolls to hit like direct fire (seekers get +40 defense).
- It fires on its own, outside the target budget, whenever a hostile piece it can reach
  moves: after each step of a seeker, fighter group or drone group. When the carrier
  itself moves, its point-defense fires at a hostile target now in range. It can also be
  given targets in its own phase like any weapon.
- Each point-defense weapon fires at most once per reload cycle, and only at categories
  in its target set.

### 10.3 Ramming and warheads (confirmed: binary)

- **Ram order:** the rammer must have movement left, the target must be adjacent and
  hostile. Strategies with Ram movement (Kamikaze, Drone Attack) do this automatically.
- Let R = the rammer's remaining hit points and T = the target's remaining hit points,
  taken when the ram happens (after the rammer's move). Shields never count:
  - ship or base: the structure of its intact components (with mounts) minus its damage
    pool, never below 0;
  - unit group: the structure of its living units, with the shields counted once for
    fighters, troops and weapon platforms and not at all for other units; the group's
    pools are not subtracted;
  - seeker: one member's `Weapon Seeker Dmg Res` minus its pool, whatever the group's size;
  - planet: its hit points (§11), though as a target it takes B = 500000 anyway.
  - A = R × `Ram Ship Source Modifier Percent` / 100, truncated.
  - B = T × `Ram Ship Target Modifier Percent` / 100, truncated.
  - Against a planet or obstacle: A is divided by 4 (truncated) and B is 500000.
  - A drone rammer always takes B = 500000.
  - W = the warheads of the rammer's design that can hit the target, plus all warheads of
    the target's design (both explode), counting only warheads whose damage type is
    Normal, Skips Normal Shields, Only Engines, Only Weapons, Only Shield Generators,
    Skips Armor, Skips Shields And Armor, Skips All Shields or Only Master Computers.
- The target takes one Normal hit of A + W. A drone instead strikes with each of its
  warheads as a separate hit with that warhead's own damage type, then (if the target
  survives) with A as a Normal hit.
- The rammer takes B + W as a Skips All Shields hit.
- Warhead components are not used up by themselves; they go with the rammer.
- Kills by ramming give experience and count in design statistics (history 1.22), and
  count twice (confirmed: binary): the blow on the target credits the rammer as usual
  (§15), and when the target is destroyed the rammer's design is credited with a ship
  target's hull `Tonnage` once more, and a surviving rammer's crew gains another +1.0 (so
  +2.0 for a ship, +1.1 for a unit group). The recoil hit on the rammer has no attacker:
  no `Damage Modifier - System` applies to it, and if the rammer dies nobody is credited
  and nobody gains experience.

### 10.4 Fighters (confirmed: binary unless marked)

- **Launch.** Each combat turn a ship may launch as many fighters as the total `Launch/
  Recover Fighters` Value1 of its intact bays. A planet may launch up to 100 of each kind
  of unit it holds per combat turn. The per-player unit cap does not apply in combat
  (history 1.46).
- **Grouping** (confirmed: binary). The computer launches a carrier's fighters in
  batches of the strategy's `Fighters Launch Group Amount` (stock 10): each batch is a new
  group taken from one cargo stack, so it holds a single design; an amount of 0 puts all
  of them in one group. The tactical "Launch Fighters in Groups" order does the same with
  a size the player picks from 5, 8, 10, 15, 20, 30, 40 and 50; it launches fighters
  only. The tactical "Launch Units" window launches 1, 5, 10 or all units of a stack at a
  time; while the window stays open, units of the same kind launched from that piece join
  the group made earlier in the same window, whatever their design, so a group can mix
  designs; opening the window again starts new groups. Every launch stays within the
  per-turn launch rate. The Settings `Combat Fighter Group Amount`, `Combat Mine Group
  Amount` and `Combat Satellite Group Amount` are loaded but never used.
- A fighter group has a target budget of 1 and fires its identical weapons as one
  combined hit (§6).
- Supplies are per fighter and never pooled with the fleet (history 1.68). With zero
  supplies a fighter group cannot fire and has no shields.
- **After combat** each carrier or planet recovers the fighter and satellite groups that
  it launched in this battle (ships first, then planets), if it still has the same owner
  and room for them. Groups nobody recovers, and all drones, stay in space as the
  separate groups they are; they are not merged into other groups in the sector
  (confirmed: binary).

### 10.5 Satellites and weapon platforms

- Satellites are stationary pieces in groups. Their target budget is covered in §6. They
  are launched with `Launch/Recover Satellites` (Value1 per combat turn). A player may
  have at most `Maximum Satellites Per Player Per Sector` (100) in one sector; the cap
  holds in combat too, as does the mine cap: at the cap a launch is refused, below it the
  launch is cut to what is left (confirmed: binary).
- The computer never launches satellites (or mines) in combat; only a player does, with
  the "Launch Units" window, which groups them as in §10.4 (confirmed: binary).
- Weapon platforms are never pieces. They are the planet's guns (§11): every weapon of
  every platform is a separate weapon of the planet (confirmed: binary).

### 10.6 Mines (confirmed: binary unless marked)

- Mines are invisible and never appear on the combat map (history 1.04). When a group of
  vehicles moves into the sector, before any battle check, the mines there strike **that
  group only**, cloaked vehicles included.
- A minefield does nothing if the entering group contains a vehicle of the mine owner or
  of an empire at Non-Aggression or better with the owner.
- First, the entering group's own uncloaked sweepers remove up to their total `Mine
  Sweeping` in mines from hostile minefields.
- Then each mine picks a random vehicle of the group. Fighter and drone groups are
  skipped (and another is picked, without using the mine) unless Settings `Fighters Can
  Be Hit By Mines` / `Drones Can Be Hit By Mines` allow them. The mine's warheads strike
  it one after another, skipping those whose damage type cannot affect it, until it is
  destroyed. The mine is used up, even when none of its warheads applied. A unit group
  counts as affected by every damage type, and a warhead's `Weapon Target` set is never
  checked.
- Mine damage on a ship goes **straight to the components** (§9.1a): shields, emissive
  and crystalline armor do not act against mines. On a unit group it goes to the units
  by the §9.4 rule, so the units' shields count as hit points and one warhead can kill
  several units. Leftover damage of hull-damaging warheads is shared by the whole mine
  strike and joins the next warhead, whichever vehicle it hits (history 1.70, 1.78); a
  warhead of another type that strikes a unit group wipes that shared leftover.
- A player may have at most `Maximum Mines Per Player Per Sector` (100) in one sector.

### 10.7 Drones

- Drones are launched by `Launch Drones` components (Value1 per combat turn; a planet 100)
  and have full movement immediately (confirmed: binary).
- Every drone launched in combat is a group of its own (confirmed: binary). The
  computer launches drones in batches of the strategy's "Drones Per Target" option
  (default 3), and in all at most "Drones Per Target" × (hostile ships and bases in the
  battle) − (the side's drones already alive in the battle), within the per-turn launch
  rate; an option of 0 launches all of them. That option is set in the strategies window
  and stored with the game; it is not a key of DefaultStrategies.txt (confirmed: binary).
  A player launches drones with the "Launch Units" window, still one per group.
- A drone picks its own targets among ships, planets and satellites, never fighters,
  seekers, mines or other drones (history 1.53, 1.65). If its target is absent or gone,
  it picks temporary targets until the battle ends (history 1.58). Drones aimed at a
  ship that changes owner pick a new target (confirmed: binary).
- A drone attacks by ramming (§10.3); its warheads strike separately. Drones also fire
  any weapons they carry. Drone hulls give +50 defense.

## 11. Planets in combat (confirmed: binary unless marked)

- A colony owned by a participant is a 4×4 piece.
- **Offense.** Its weapons are the weapons of the weapon platforms in its cargo. It
  engages up to 10 targets per turn, gets +30 offense, and measures range from its
  footprint (history 1.72). It can launch fighters, satellites and drones from cargo.
- **Defense.** −200 (§7). Its shields are §9.2.
- **Planet hit points** = population (millions) × `Damage Points To Kill One Population`
  + the hit points of the units in its cargo − the stored units' damage pool. The planet
  piece itself keeps no pool; its stored units keep one, as a unit group does (§9.4),
  which is cleared when the battle ends.
- **Damage order for a hull-damaging hit** (after shields):
  1. If the cargo holds weapon platforms, the hit goes to the platforms only (as a unit
     group, §9.4). If that kills the last platform and other units remain, those units
     then take the same full hit again.
  2. Otherwise, the units in cargo take it (as a unit group).
  3. Only when no stored unit of any kind is left does the population lose D ÷ `Damage
     Points To Kill One Population` million (truncated, at least 1), from the first
     population group on. D is the whole hit after shields, however much the units
     took; the population has no pool, so every such hit kills at least 1M.
  4. Then the facilities: let n be the number of facilities at the battle's start and H0
     the planet's hit points then; per = H0 ÷ n and allowed = (current hit points) ÷ per,
     both truncated, with allowed = 0 when per is 0. A roll of 1 to 3 is always made;
     on a 1, max(0, intact facilities − allowed) facilities are destroyed, one at a time.
     Each is taken from a facility stack drawn at random, weighted by the stack's size
     (destroyed ones included); a stack with none intact is drawn again.
- **Lost facilities** keep working (abilities, shields, cargo space) until the battle
  ends, and are removed from the planet then (confirmed: binary). The original never
  clears a stack's count of destroyed facilities after removing them, so every later
  battle at that planet removes the same number from that stack again, and the stale
  count also lowers the intact count used by later facility steps. This is a defect of
  the original; see §19.1 for the engine's choice.
- Planets never regenerate shields in combat, and a planet's shield maximum does not
  drop when a shield facility is destroyed (the facility keeps working until the end).
- A colony whose population reaches 0 is lost; the planet stays on the map as an
  unowned obstacle for the rest of the battle.
- **Drop Troops.** A ship carrying troops may drop all of them onto an **adjacent**
  hostile colony that is not already contested by another empire's troops. Ground combat
  (§13) is fought **at once**, in the middle of the space battle. If the planet falls, its
  piece changes sides immediately. Planet shields do not stop the landing. An AI ship with
  a Drop Troops or Capture Planet strategy heads for the planet it was ordered to take,
  otherwise for the most populous enemy planet (history 1.59).

## 12. Boarding, capture, conversion, self-destruct (confirmed: binary)

- **Preconditions** for Capture Ship: both pieces are ships or bases of different
  empires, the target is adjacent (history 1.73), the attacker has some `Boarding
  Attack`, and the target's shields are 0.
- **Strength.** Offense = total `Boarding Attack` of the attacker's intact components.
  Defense = total `Boarding Defense` + 4 × (number of intact `Ship Crew Quarters`
  components) + total `Boarding Attack` of the target's intact components.
- **Self-destruct.** If the target has an intact Self-Destruct device and offense is
  greater than defense, both ships take 10000 Normal damage instead of a capture.
- **Success** (offense strictly greater than defense, no roll): the ship changes owner at
  once and becomes the capturer's piece. Its orders are cleared, its crew experience is
  lost (history 1.15), and every weapon's reload counter gets Settings `Captured Ship
  Additional Reload Combat Turns` (10) added (at most 250). Then the attacker takes 5000
  `Only Boarding Parties` damage (its boarding parties are spent) and the captured ship
  takes 5000 `Only Security Stations` damage. The log records it as "Taken".
- **Failure:** the attacker takes 5000 `Only Boarding Parties` damage, and the target
  takes 1 to 5 (random) `Only Security Stations` damage.
- **Crew Conversion** (§9.5) changes the owner the same way, but with no reload penalty,
  no loss of experience and no self-destruct. The new owner's seekers already aimed at it
  expire at their next step, and drones aimed at it pick new targets (history 1.73).

## 13. Ground combat (confirmed: binary unless marked)

- **Triggers:** troops dropped during a space battle (fought at once), or troops still
  landed on a colony at turn processing. The latter is fought in the **colony owner's**
  end-of-turn processing, at its ground-combat step (spec 05 §8), for each of the owner's
  colonies that has landed troops, without asking anyone (strategic). If by then the
  colony's owner is the landed empire, or is at Non-Aggression or better with it, there
  is no fight: the landed troops join the colony's cargo, where they serve the owner,
  and the invasion ends (confirmed: binary).
- **Sides.** The attacker has its landed troops. The defender has the units in the
  planet's cargo plus militia. Units in cargo have no owner of their own: a ship drops
  every troop unit aboard, of whatever design, for the empire that owns the ship at that
  moment (so a captured or converted ship's troops fight for the captor), and the units
  stored on a planet always serve the planet's owner. If the invasion wins, the
  surviving invaders join the planet's cargo (confirmed: binary).
- **Militia.** When the first invading troops land on a colony, the colony gets a militia
  pool: for each population group, its population in millions ÷ `Defending Units Per
  Population` (20), truncated, summed. There is no minimum, so a colony below 20M has no
  militia. At each ground combat, each population group raises that same number of
  militia, limited by what is left of the pool; the survivors become the new pool. Each
  militia unit has `Population Defender Attack Strength` (10) attack and `Population
  Defender Hit Points` (30). Militia losses cost no population.
- **Length.** At most Settings `Number Of Ground Combat Turns` (10) rounds per ground
  combat, ending early when either side has no troops or militia left. If both survive,
  the fight resumes at the colony owner's next end-of-turn processing (above). It stops
  on peace or surrender, when the troops change sides as described above (history 1.03,
  1.20; confirmed: binary).
- **Round.**
  1. Each side's offense and defense are (`Combat To Hit Offense/Defense Plus` −
     `Minus`) ÷ 2, truncated, counted over the side's troop designs only (militia and
     other stored units add nothing): the single best hull value among them, plus the
     best value of each component `Family`, the families added up (§7). They are
     recomputed every round.
  2. Every unit rolls: it hits if roll ≤ (its side's offense + 50 − the other side's
     defense). There is no clamp.
  3. A hit adds the unit's attack: militia use the setting; a troop unit uses the sum,
     over its weapons, of each weapon's largest table entry (with mount). Units without
     weapons, and stored units other than troops and militia, add nothing.
  4. Each side's total, plus damage carried from the previous round, is multiplied by
     `Ground Combat Damage Modifier Percent` (30) / 100 and truncated.
  5. The modifiers are chained. The defender first adds Round(total × the planet's
     `Planet - Change Ground Defense` / 100), then Round(new total × its racial ground
     modifier / 100). The attacker adds only Round(total × its racial ground modifier /
     100). The racial ground modifier is the culture's `Ground Combat` value plus
     (Physical Strength − 100) plus matching racial traits.
  6. Each total is applied to the other side's stacks in two passes: first the units
     that attack (troops with weapons, in cargo order, then the militia, which are added
     at the end of the cargo list), then everything left, in cargo order (troops without
     weapons, then fighters, satellites, platforms, mines and drones in storage). In each
     stack, whole units die while the damage covers their hit points (structure plus
     shields for troops, fighters and platforms; structure alone for other stored units;
     the setting for militia); the rest moves on to the next stack. What remains at the
     end is carried to the next round (divided back by the percentage, truncated).
     Stored units other than troops absorb damage but do not stop a capture.
- **Victory.** When the defender has no troops or militia left and the attacker still
  has troops, the attacker takes the planet with its surviving facilities, stored units
  and population. When the attackers are gone, the fight ends. No facilities are lost in
  ground combat.
- **Reinforcing.** Either side may drop more troops at any time. A third empire may not
  land on a planet already contested by two other empires.

## 14. Retreat and disengagement

Stock SE4 has no retreat order and no way to leave the map (confirmed: binary: no such
command exists). The only ways to disengage are:

- a `Don't Get Hurt` strategy, which runs from the mass of enemy pieces each turn (§16.1);
- surviving until the turn limit, then moving away on the system map next game turn;
- cloaking, which does not help inside a battle, because every piece is decloaked.

## 15. After a battle

- Carriers recover their own launched groups (§10.4).
- Restore regenerating armor (§9.3). Shields refill at the next battle's start. All other
  damage stays until repaired (repair spec).
- **Experience** (confirmed: binary). Experience is earned only for kills, during the
  battle:
  - Destroying a ship, base or planet gives the killer's crew +1.0; destroying a whole
    unit group or a seeker gives +0.1. Seeker kills credit the launcher (history 1.87).
    Firing alone gives nothing.
  - Each time a ship in a fleet gains crew experience this way, the fleet has a 1-in-4
    chance to gain +0.1 fleet experience.
  - Crew and fleet experience are capped at 50. Their truncated values are to-hit points
    (§7). Unit groups and planets gain no experience.
  - A ship with a Neural Combat Net uses the §7 rule. A fleet's bonus is lost if the
    fleet breaks up.
  - Ramming counts a kill twice (§10.3). A capture or a Crew Conversion gives the captor
    no experience; a boarded ship's crew drops to 0, a converted ship keeps its
    experience (§12). The combat simulator gives no experience.
  - **Level names** (confirmed: binary). Crew and fleet experience use one scale: 5 or
    less Novice, up to 10 Experienced, up to 20 Veteran, up to 30 Elite, above 30
    Legendary. The label shows the level name followed by the truncated experience as a
    percentage bonus, for example "Veteran (+14%)".
- **Design statistics** (confirmed: binary). A design keeps four counters: Number
  Constructed, Number Lost, Number Scrapped and Enemy Tonnage Destroyed (In Service is
  constructed − lost − scrapped). There is no kill counter. A vehicle that self-destructs
  counts as scrapped, not lost. Units killed count as lost for their design as they die.
  Enemy tonnage destroyed, credited when a piece is destroyed:
  - the value is the victim's hull `Tonnage` for a ship or base; for a unit group, hull
    `Tonnage` × every unit it had in the battle (killed ones included), credited only when
    the whole group dies (units killed from a group that survives credit nothing); 0 for a
    planet or a seeker;
  - it goes, in full, to every design in the killing object: a ship's design, each design
    in a unit group, the launcher's design(s) for a seeker; a planet credits nobody;
  - a mine credits its own design with the victim's value;
  - in ground combat, each time units die, a randomly drawn stack of the killing side
    gets the dead units' hull `Tonnage` (a truncated proportional share for part of a
    stack), and nothing when the drawn stack is militia;
  - nothing is credited in the combat simulator.
  A capture or conversion changes no design statistic: the victim's design records no
  loss, and the captured ship keeps its design, so it still counts as in service. Only
  the owning empire sees its designs' statistics (in the Designs window with statistics
  shown); the enemy design lists never show them.
- **Log.** One battle report per participant, listing losses and ships "Taken". A piece
  is reported "Taken" when it is missing from its owner's list at the end but its name is
  in another empire's list: captures, conversions and planets taken by troops during the
  battle alike (confirmed: binary). A human player also gets a message box on a capture.
  If Settings `Create Combat Replay` is on, also record a replay stream (§17); a boarding
  capture is recorded in it, a conversion is not.
- Each participant learns the designs it fought.
- Happiness events (battle won or lost in a system, ships lost) are covered in the
  happiness spec.

## 16. Strategic combat and strategies

Strategic combat uses exactly the tactical rules, with every piece driven by its
strategy. Strategies are records defined in DefaultStrategies.txt and edited per empire.
The stock set contains Optimal Firing Range, Don't Get Hurt, Capture Enemy Ships,
Capture Planet, Fighter Attack, Kamikaze, Maximum Weapons Range, Short Weapons Range,
Point Blank and Drone Attack.

| Field | Semantics |
|---|---|
| `Primary Movement Strategy`, `Secondary Movement Strategy` | One of eight: the three range strategies (Optimal, Short and Maximum Weapons Range), Point Blank, Ram, Board Enemy Ships, Drop Troops and Don't Get Hurt. What each does is §16.1. Use the secondary strategy when the primary is impossible, for example Drop Troops with no troops. |
| `Targeting Priority 1..4` | Sort keys for choosing a target, applied in order: Nearest, Farthest, Largest, Smallest, Most Damaged, Least Damaged, Fastest, Slowest, Strongest, Weakest, Has Weapons, Does Not Have Weapons. The first key filters or sorts, and later keys break ties. |
| `Use Type Priority First`, `Type Priority <Cat>` | Rank per category (1 = engage first) over 14 categories: Planets, Fighters, Seekers(On Us), Seekers(On Others), Mines, Carriers, Colony Ships, Transports, Bases(No Weapons), Ships(No Weapons), Bases, Ships, Satellites, Drones. The flag decides whether category rank is applied before the targeting keys. The UI also offers turning type priority off. |
| `Dont Fire On <Cat>` | Never engage that category. |
| `Fighters Launch Group Amount` | Fighter group size at launch; 0 puts all of a carrier's fighters in one group (§10.4). |
| "Drones Per Target" | Drone launch batch and limit (default 3); each drone is still its own group (§10.7). Not a DefaultStrategies.txt key. |
| `Break Formation <Cat>` | Whether own pieces of that category leave the formation in combat. |
| `Damage Percent Per Ship / Planet / Fighter Group / Satellite Group` | (confirmed: binary) While choosing targets, a ship, planet, fighter group or satellite group already damaged by more than this percentage is skipped, unless `Damage Until All Weapons Gone` is set and it still has weapons. Drones and seekers are never skipped this way. If no target is left, the choice is made again without this filter. |
| `Damage Until All Weapons Gone` | See the row above. |

The computer assigns a weapon to a target only if the weapon can reach it and its damage
type can affect it (confirmed: binary). It stops assigning direct fire to a ship or
planet once the damage already assigned to it this turn reaches 1.5 × (its shields + hit
points), and to a unit group or seeker once it reaches 1 × that; seekers in flight are
counted separately against the same limits, so enough seekers aimed at one target move
new launches to the next (confirmed: binary; history 1.82, 1.86).

**Holding fire for an invasion** (confirmed: binary). While a side has in the battle at
least one ship carrying troops whose strategy in effect is Drop Troops (§16.1), none of
its pieces targets an enemy planet that has no intact weapon other than point-defense
and warheads. This is checked afresh every time targets are chosen, for moving and for
firing. Separately, and always, the computer never targets a planet on which its own
troops, or those of an empire it is not hostile to, are fighting a ground combat that
is not over.

### 16.1 How the computer moves (confirmed: binary)

**Order.** In a computer side's phase the pieces act one at a time: group leaders first,
then the others, in piece order. Only ships, fighter groups and drone groups move;
planets only launch. Each piece: chooses a destination square; fires first if that
square is farther (aim distance, §1) from the target of its first ready weapon (design
order, not point-defense) that has a target than its current square is; moves; then
drops troops, rams or boards; then fires again with whatever is still ready. After all
have moved, every piece of the side that has not acted fires.

**Strategy in effect.** The primary strategy is used unless it is impossible: Drop Troops
needs a ship carrying troops; the four range strategies (Maximum, Optimal, Short, Point
Blank) need an intact weapon other than point-defense and warheads; Board needs a ship
with `Boarding Attack`; Ram, for a drone group, needs a drone target. Otherwise the
secondary is tested the same way, and if it is impossible too, Don't Get Hurt applies (a
secondary Ram never falls back). An Optimal ship with no weapons at all rams. A piece
whose strategy in effect is Don't Get Hurt, Drop Troops, Board or Ram leaves its
formation, as does one whose category has `Break Formation` set; a member that keeps its
formation slot heads for that slot.

**Danger map.** Built once at the start of the side's movement, not updated as pieces
move. For every hostile piece (neutral obstacles count as hostile here), with M its full
movement allowance: each ready intact weapon other than point-defense (direct fire,
seeking and warheads alike; an enemy ship's weapons that cannot target ships are left
out) adds, to each square at distance d from the enemy's top-left square, the weapon's
damage at range max(1, d − M), out to its longest range + M; the enemy's own square gets
the damage at range 1. An enemy ship with total `Boarding Attack` B adds B ÷ 2 + 1
(truncated) to every square within M. Each hostile piece's own square adds 30. Because
of the way the rings are cut at the map's border, squares on the border are counted
again for every larger ring, so danger is overstated along the edges. **Dodging
seekers:** for the piece being moved, each enemy seeker group aimed at it adds its
weapon's damage at range d to every square at distance d from the seeker, out to the
last non-zero entry (range 1 on the seeker's own square), once per group whatever its
size, without regard to the distance already travelled.

**Attack map.** For each of the mover's ready weapons that has a target it can hit, the
damage it would deal from each square, measured from the target's top-left square. It
counts in full, but a fifth for Shields Only, Only Engines and Only Master Computers, and
not at all for push, pull and teleport. Against a ship whose shields are no more than
its shield regeneration, values below its emissive armor count 0 (unless the mover is
a fighter or satellite group).

**Choosing the square.** Squares with a piece on them are never chosen by the range
strategies, and neither are the map's border rows and columns. Full ties are broken by
replacing the choice with a 1-in-10 chance as the scan goes on.

- **Optimal Weapons Range:** a square with no danger where it can deal damage, the most
  such damage, nearest its target. Failing that, the lowest 1000 × danger ÷ damage
  (truncated), nearest its target. Failing that (it can deal damage nowhere): stay put
  if there is danger within 10 squares and it has no target, otherwise the least-danger
  square, the one farthest from its target (without a target, the one nearest to
  itself).
- **Short Weapons Range:** the square with the most damage, ties to less danger. If it
  can deal damage nowhere, as Optimal. There is no 1-to-3-squares limit.
- **Maximum Weapons Range:** without a target, stay put if there is danger within 10
  squares, otherwise Don't Get Hurt. With no ready weapon, or when it can already hit
  from where it stands, it heads for the ring at distance (the target's longest weapon
  range + the target's movement + 2) from the target's top-left square: the least-danger
  square of that ring within its own movement, else the nearest square of the ring, else
  Don't Get Hurt. Otherwise, among squares where it can deal damage: within its
  movement, the one farthest from the target; else the farthest overall (ties nearer to
  itself, then a coin flip).
- **Point Blank:** the square next to the target's top-left square, one step toward the
  mover. Without a target, as Optimal.
- **Drop Troops:** its planet is a hostile colony named by one of the ship's own Attack or
  Seek orders, else the most populous hostile colony. While that planet still has a
  weapon other than point-defense and warheads and the side has an armed escort (a piece
  with such a weapon and a range strategy in effect), the carrier waits (Don't Get Hurt);
  otherwise it goes to the planet's Point Blank square and lands.
- **Don't Get Hurt:** it looks only at where pieces stand, not at weapons, ranges or the
  danger map. Every square within its remaining movement on both axes is a candidate,
  taken or not. A candidate's score adds, for each square holding hostile pieces
  (obstacles and hostile seekers included), 10 × their number × the straight-line
  (Euclidean) distance, truncated; and for each square holding only own pieces (the
  mover itself and own seekers included), 3 × their number × the straight-line distance,
  truncated. The highest score wins, ties to the smallest column, then the smallest row.
  So it runs from the mass of enemies while spreading away from its friends, and always
  uses its full move. (When the scan reaches a square holding hostile pieces, that
  square's own score so far is reset to 0.) It is also the fallback of Drop Troops,
  Maximum Weapons Range, Board and Ram as described above.

All of this planning is centred on the target's top-left square, so ships end up to 3
squares closer to a planet on its right and lower sides, and a Point Blank or landing
square next to a planet approached from the right or from below lies inside the planet
(the step rules then stop short). There is no other rule for planets (history 1.60
describes this effect).

## 17. Combat tooling

- **Combat simulator.** A mock battle in which nothing is really lost. Choose from your
  designs, enemy designs you have seen, and sample planets from your home system. Assign
  each item to one of several virtual empires, which count as separate empires and are
  all hostile to each other (confirmed: binary). You may edit cargo (fighters on
  carriers, platforms on planets), form fleets, edit strategies, and choose which virtual
  empires the computer controls. Run it tactically or strategically. Minefields cannot
  be added. Obsolete designs can be hidden. It must never change real game state
  (history 1.46). Details (confirmed: binary):
  - There are up to 10 virtual empires (sides 1 to 10). The first design or planet added
    to a side makes that side a copy of the real empire that owns it (race, culture,
    technology), so a side whose first item is an enemy design is a copy of that enemy.
  - The battle is fought in the player's home system, in the home planet's sector. The
    sector's and system's real `Sector - Sensor Interference` and `Sector - Shield
    Disruption` apply; the empires' system modifier totals (§7) are never worked out, so
    they are 0.
  - Ships are new vehicles built from the designs, fully supplied, with no experience. A
    sample planet is a copy of the real colony, owned by its side and moved to the battle
    sector.
  - Start positions go by side number, as if each side had arrived from a neighbouring
    sector (§3): 1 from the north, 2 south, 3 west, 4 east, 5 north-west, 6 south-west, 7
    north-east, 8 south-east; sides 9 and 10, and any side that owns a planet or a base,
    start in the middle.
  - Side 1 is played by hand and the others by the computer unless changed with Computer
    Control. Tactical or Strategic is chosen in the simulator window; Stop Combat (in the
    tactical window's options) exists only here. No experience or design statistics are
    gained (§15).
- **Combat replay.** A view-only playback from the log, advanced one combat turn at a
  time with Next. It records only piece movement, including forced moves, and weapon
  firings. It holds no damage or reload state. Hovering a piece shows its design, not
  its damaged state. In simultaneous games only the latest turn's replay is available,
  and the host sends it to the players.
- **Weapons report.** Lists available weapons with size, reload and damage at ranges
  1–10 and 11–20. Filters: All, Direct Fire, Seeking, Point-Defense, Warhead, Only
  Latest. A mount selector shows the numbers as they would be with that mount. Seeker
  ranges are travel distances.
- **Weapon mounts window.** In the design screen, picking a mount applies it to every
  weapon added afterwards. Only mounts valid for the hull are listed (§18.2). A mounted
  component shows the mount's code letter.

## 18. Data reference

### 18.1 Components.txt weapon fields

| Field | Semantics |
|---|---|
| `Weapon Type` | `None`, `Direct Fire`, `Seeking`, `Point-Defense`, `Warhead` (`Any` exists for mounts). If not None, the fields below are present. |
| `Weapon Target` | One value from a fixed list; each value names a set of categories, and any other text is an error (confirmed: binary). Single categories: `Ships` (ships and bases), `Planets`, `Fighters`, `Satellites`, `Drones`, `Seekers`. `All` means all six. The combined values join category names with `\`, using `Ftr` for fighters and `Sat` for satellites: ships with planets; ships with satellites; ships, planets and satellites (with or without drones); everything except seekers; and the small-craft set of fighters, satellites, seekers and drones. |
| `Weapon List Target Override`, `Weapon List Target Description` | Optional. When present, the override replaces the set: it contains each of the words ships, planets, seekers, fighters, satellites, drones that appears in it (confirmed: binary); plus UI text. |
| `Weapon Damage At Rng` | 20 integers (§8). The key's capitalisation varies ("at"/"At"), so match without regard to case. |
| `Weapon Damage Type` | One of the §9.5 identifiers. |
| `Weapon Reload Rate` | Turns between shots. 0 for warheads. |
| `Weapon Display Type`, `Weapon Display`, `Weapon Sound` | Presentation only: Beam, Torp or Seeker, a sprite index, and a sound file. |
| `Weapon Modifier` | To-hit bonus (§7). |
| `Weapon Seeker Speed`, `Weapon Seeker Dmg Res` | Seekers only: squares per turn and hit points. The header calls the speed field `Weapon Speed`, but stock records use the seeker names. Accept both. |
| `Weapon Family` | Integer grouping weapon lines, separate from `Family`. Parse and keep, with no combat rules attached. |
| Shared fields | `Supply Amount Used` (per shot), `Tonnage Structure` (hit points), `Family` (groups components for the §7 stacking rule), `Vehicle Type` / `Vechicle List Type Override` (the misspelling is in the data; accept it) for which vehicles may carry the part, and `Restrictions` (max per vehicle). |

### 18.2 CompEnhancement.txt (weapon mounts)

Each record has: `Long Name`, `Short Name`, `Description`, `Code` (letter badge); percent
modifiers `Cost Percent`, `Tonnage Percent`, `Tonnage Structure Percent`, `Damage
Percent`, `Supply Percent`, and optional `Shield Percent` (default 100); `Range Modifier`
(± squares, §8); `Weapon To Hit Modifier` (± points, §7); and eligibility filters:
`Vehicle Size Minimum`, optional `Vehicle Size Maximum`, optional `Comp Family
Requirement` (comma list of component `Family` values), `Weapon Type Requirement`
(`Any` meaning any weapon; `None` meaning non-weapons only; or a single weapon type),
`Vehicle Type` (a single type or `Any`), and optional tech requirements. The damage,
range, structure and shield effects apply only to components that meet the weapon type
and family requirements (confirmed: binary). The stock pattern: ship mounts multiply
damage ×2, ×3 or ×5 at hull minimums of 400, 800 and 1200 kT with no range change.
Base and weapon-platform mounts also add range and to-hit. The satellite mount adds
range only. All stock mounts are direct-fire only.

### 18.3 Settings.txt entries used by combat

| Key | Default | Use |
|---|---|---|
| `Number Of Space Combat Turns` | 30 | Turn limit; the battle stops when the counter reaches it (§4) |
| `Number Of Ground Combat Turns` | 10 | Rounds per ground combat (§13) |
| `Combat Base To Hit Value` | 100 | §7 |
| `Combat To Hit Modifier Per Square Distance` | 10 | §7 |
| `Seeker Combat Defense Modifier` | 40 | §7 |
| `Planet Combat Offense Modifier` | 30 | §7 |
| `Planet Combat Defense Modifier` | −200 | §7 |
| `Ram Ship Source Modifier Percent` / `Ram Ship Target Modifier Percent` | 60 / 100 | §10.3 |
| `Captured Ship Additional Reload Combat Turns` | 10 | §12 |
| `Combat Fighter/Mine/Satellite Group Amount` | 20 each | Loaded, never used (§10.4) |
| `Maximum Mines/Satellites Per Player Per Sector` | 100 each | §10.5, §10.6 |
| `Fighters Can Be Hit By Mines`, `Drones Can Be Hit By Mines` | true | §10.6 |
| `Fighter Supply Usage Per Turn`, `Drone Supply Usage Per Turn` | 5, 200 | Idle supply use of launched units |
| `Defending Units Per Population`, `Population Defender Attack Strength`, `Population Defender Hit Points` | 20, 10, 30 | §13 |
| `Ground Combat Damage Modifier Percent` | 30 | §13 |
| `Damage Points To Kill One Population` | 10 | §9.5, §11 |
| `Create Combat Replay` | true | §17 |
| `Simultaneous Games Show Strategic Combat` | false | §2 |

### 18.4 Abilities used by combat

How values combine (confirmed: binary): "sum" adds all intact components (and hull);
"best" takes the largest single value; "family-best" takes the best within each
component `Family` and adds the families (§7).

- **Hit chance:** `Combat To Hit Offense Plus/Minus`, `Combat To Hit Defense Plus/Minus`
  (family-best), `Weapons Always Hit`, `Combat Modifier - System` (§7 system rule).
- **Targets and movement:** `Multiplex Tracking` (best), `Combat Movement` (best),
  `Point-Defense` (marker on point-defense weapons).
- **Shields and armor:** `Shield Generation`, `Phased Shield Generation`, `Planet - Shield
  Generation` (sum), `Shield Regeneration` (sum), `Shield Modifier - System`, `Armor`
  (marks the armor layer), `Emissive Armor` (best), `Armor Regeneration` (sum), `Shield
  Generation From Damage` (sum).
- **Damage and environment:** `Damage Modifier - System`; the sector or system abilities
  `Sector - Sensor Interference`, `Sector - Shield Disruption` (sum of the system's own
  values and the objects in the battle sector) and `Damage` (per game turn, outside
  combat).
- **Launching:** `Launch/Recover Fighters`, `Launch/Recover Satellites`, `Launch Drones`,
  `Lay Mines` (Value1 per combat turn, Value2 per game turn), `Mine Sweeping`, `Drop
  Troops` (declared, unused in stock data).
- **Capture and command:** `Boarding Attack`, `Boarding Defense`, `Ship Crew Quarters`
  (boarding defense), `Self-Destruct`, `Master Computer`, `Ship Bridge`, `Ship Auxiliary
  Control`, `Ship Life Support`.
- **Experience and ground:** `Combat Best Experience`, `Ship Training`, `Fleet Training`
  (Value1 per turn, Value2 as a cap), `Planet - Change Ground Defense`.

### 18.5 Formations.txt, VehicleSize.txt, Cultures.txt

- **Formations.** The header holds only ASCII diagrams. Each record has `Name`,
  `Description`, `Leader Position Xpos/Ypos` and `Leader Design Type`, then `Number of
  Positions` (up to 100, history 1.68) followed by `Position N Xpos/Ypos/Type` on a
  19×19 template. Slots are filled in order by the leader's followers. A slot's offset
  is its position minus the leader's position. `Type` restricts which design types may
  take the slot, and is `Any` in all stock data.
- **VehicleSize.** The hull's to-hit abilities (§7), its vehicle type (bases have
  unlimited supplies, §6) and `Engines Per Move`.
- **Cultures.** `Space Combat` and `Ground Combat` are signed percentages (§7, §13).

## 19. Open questions

Answers found in the executable are given in place. Since 2026-09-30 every question below
is answered from the executable; §19.1 keeps the engine's own choices where the original
has no rule, and OpenSE4's extensions.

1. **Grid.** Answered (confirmed: binary): 72 × 63 squares; start boxes by arrival
   direction (§3). Stars, warp points, comets and uncolonised planets are 4×4 obstacles
   that block movement; storms and uncolonised asteroid fields do not appear. Distance is
   Chebyshev.
2. **Distance to planets.** Answered (confirmed: binary): range distance to the nearest
   footprint square; aim distance to the top-left square (§1, §7).
3. **Turn order.** Answered (confirmed: binary): defenders are empires with a piece that
   was already in the sector (§3). The order is drawn once per battle. Seekers move in
   their owner's phase, after its drones and before its other pieces, from the turn after
   launch. Reload countdown and regeneration happen at the start of every combat turn
   after the first.
4. **Hit roll.** Answered (confirmed: binary): roll ≤ chance. Culture `Space Combat`
   counts for offense and defense, Aggressiveness/Defensiveness as (value − 100),
   experience truncated. `Combat Modifier - System` helps offense only. ECM and stealth
   armor stack when they are different component families.
5. **Mount range shift.** Answered (confirmed: binary): index `clamp(r − m, 1, 20)` (§8).
6. **Damage order.** Answered (confirmed: binary): components are destroyed whole, in a
   random order weighted by structure, armor first; armor-skipping damage hits armor
   once nothing else is left (§9.1a).
7. **Shields.** Answered (confirmed: binary): one pool whose kind is normal if any normal
   generator exists; current shields are capped when a generator dies; Shield Disruption
   lowers the maximum whenever it is computed (§9.2).
8. **Emissive.** Answered (confirmed: binary): hits no larger than E do nothing to the
   hull, larger hits lose E; checked after shields against the pre-shield hit; the
   largest value counts (§9.3).
9. **Crystalline and organic.** Answered (confirmed: binary): crystalline shield gain is
   capped at the maximum and does not reduce damage; organic armor restores whole
   destroyed components from a pool (§9.3).
10. **Unit groups.** Answered (confirmed: binary): each member weapon rolls; a fighter
    group's hits combine into one; damage accumulates in a group pool and kills whole
    units; the `Combat * Group Amount` settings are unused (§6, §9.4, §10.4).
11. **Point-defense.** Answered (confirmed: binary): once per reload cycle, outside the
    budget, only at categories in its target set (§10.2).
12. **Ramming.** Answered (confirmed: binary): see §10.3. Source percent scales the
    rammer's own hit points into the damage it deals; target shields absorb the rammer's
    blow but not the return damage; warheads of both sides explode.
13. **Boarding.** Answered (confirmed: binary): strict comparison, no roll; each intact
    crew quarters component adds 4 to defense; the self-destruct device must be intact
    (§12).
14. **Crew Conversion.** Answered (confirmed: binary): the chance is the damage value
    after the system damage modifier; no reload penalty (§12).
15. **Random Target Movement and push/pull.** Answered (confirmed: binary): §9.5.
16. **Planets.** Answered (confirmed: binary): facilities fall with a 1-in-3 chance per
    population hit (§11); a planet at 0 population loses its colony; shields do block
    the planet-only types; platform shields do not add; a planet launches up to 100 of
    each unit kind per turn.
17. **Ground combat.** Answered (confirmed: binary): §13. No facilities are lost in
    ground combat.
18. **Repeat battles.** Answered (confirmed: binary): in turn-based games only movement
    steps, the Attack order and a Seek order at its target run a check; a Seek at its
    target attacks again every time the list runs, Sentry never does. In simultaneous
    games any order carried out that day (a waiting Sentry included) makes its sector a
    candidate, at most one battle per sector per game turn unless newcomers arrive (§2).
19. **Experience.** Answered (confirmed: binary): §15, including the level names (Novice
    up to 5, Experienced up to 10, Veteran up to 20, Elite up to 30, then Legendary).
20. **Cargo loss.** Answered (confirmed: binary): at once, during the battle, when the
    part is destroyed: population first, 1M at a time from the first group, then units
    one at a time from the first stack; restoring the part does not bring it back (§9.4,
    spec 03 §11).
21. **Treaty "None".** Answered (confirmed: binary): yes, "None" and "not yet met" fight.
22. **Designs without supply storage.** Answered (confirmed: binary): bases always have
    unlimited supplies; ships without supply storage have none, so they cannot fire and
    have no shields; satellites, drones, weapon platforms and planets never need supplies
    in combat.
23. **Mothballed ships.** Answered (confirmed: binary): a mothballed ship in a battle
    sector becomes a piece with no abilities, no shields, no movement and no offense or
    defense. It never starts a check itself (it neither moves nor carries out an order),
    but it is never skipped either: a hostile group that moves in and sees it starts a
    battle (§2).
24. **Mine timing.** Answered (confirmed: binary): only the group that just moved in
    (§10.6).
25. **Don't Get Hurt.** Answered (confirmed: binary): it does not look ahead at all. It
    ignores weapons and ranges and picks, within its remaining movement, the square
    farthest (by weighted straight-line distance) from the hostile pieces and, less
    strongly, from its own (§16.1). Seekers are dodged by the range strategies through the
    danger map, not by Don't Get Hurt.
26. **Holding fire for an invasion.** Answered (confirmed: binary): while a side has a
    troop ship whose strategy in effect is Drop Troops, it does not target enemy planets
    that have no weapon other than point-defense and warheads; and it never targets a
    planet where its own or a friendly empire's troops are still fighting (§16).
27. **Captures in the statistics.** Answered (confirmed: binary): a capture or conversion
    changes no design statistic and gives no experience; the battle report shows the ship
    as "Taken" (§15).
28. **Conversion and self-destruct.** Answered (confirmed: binary): no, a conversion never
    sets off a self-destruct device.
29. **Captured troops.** Answered (confirmed: binary): units in cargo have no owner of
    their own; a ship drops all its troops for whoever owns it at that moment, so a
    captured ship's troops fight for the captor (§13).
30. **Drone launches.** Answered (confirmed: binary; corrected on 2026-09-30): every drone
    is its own group; "Drones Per Target" is the computer's launch batch and, times the
    hostile ships and bases, its limit (§10.7).
31. **Militia losses.** Answered (confirmed: binary): militia deaths do not reduce
    population; the colony's militia pool shrinks instead (§13).
32. **Damage pool between battles.** Answered (confirmed: binary): no. A ship's pool
    belongs to its battle piece; unit groups and a planet's stored units clear theirs at
    the end of every battle; pools are never saved (§9.1).
33. **Shield-multiplier types against empty shields.** Answered (confirmed: binary): yes,
    they are scaled and scaled back on every hit, so with no shields Half and Quarter
    lose their remainders (§9.5).
34. **Hit points in a ram.** Answered (confirmed: binary): never shields; a ship's intact
    structure minus its pool; a unit group's living units without its pools; one
    seeker member's resistance minus its pool (§10.3).
35. **Warp arrivals.** Answered (confirmed: binary): in a fixed 4 × 4 box at the exact
    centre (top-left squares x 34 to 38, y 29 to 33), facing 2, whatever the battle size;
    its empire is an attacker unless it also has a piece that was already in the sector
    (§3).
36. **Several empires in the middle.** Answered (confirmed: binary): the empires are
    numbered in the order the system lists their first object; with two or more, a
    colony's owner keeps the centre box and the others take fixed boxes two box sizes out
    (up-left, down-right, up-right, down-left, up, down, right, left, by number). Pieces
    in the middle face a random direction from 1 to 4. Pieces from an edge face 2 (top
    row, corners included), 1 (left), 3 (right) or 0 (bottom row). With more than 60
    pieces a corner box is 48 wide and 24 deep (§3).
37. **Satellites launched in combat.** Answered (confirmed: binary): the computer never
    launches them; a player's satellites launched from one piece in one "Launch Units"
    window form one group, and the per-sector cap holds (§10.4, §10.5).
38. **Unit groups.** Answered (confirmed: binary): the shield pool pays the shield part of
    a unit's hit points; a non-hull hit is judged on its own damage and leaves the pool
    as it was; emissive armor never acts on unit groups (§9.4).
39. **Planet pool and facilities.** Answered (confirmed: binary): the planet itself keeps
    no pool (only its stored units do), so every population hit kills at least 1M; lost
    facilities are drawn from random stacks weighted by size, keep working until the
    battle ends and are removed then (§11).
40. **Other types against planets.** Answered (confirmed: binary): the shield-absorbed
    "Only" types and push, pull and teleport drain shields and do nothing else; Only
    Weapons, Only Shield Generators and Only Master Computers do nothing at all; the
    reload types lengthen the planet's reloads (§9.5).
41. **Ground combat details.** Answered (confirmed: binary): armed troops are hit first,
    then militia, then everything else; only troop designs count for offense and defense
    (the best single hull plus family-best components); the planet's and the race's
    modifiers are chained (§13).
42. **Organic armor.** Answered (confirmed: binary): a part the pool cannot pay for is
    skipped and cheaper ones later in design order are still restored (§9.3).
43. **System modifiers during a battle.** Answered (confirmed: binary): taken once, at
    setup, never recomputed (§7).
44. **Planet conditions.** Answered (confirmed: binary): conditions on the 0–1.5 scale drop
    by 0.1 × D (after shields), never below 0; a 20-damage hit wipes them (§9.5).
45. **Mines against unit groups.** Answered (confirmed: binary): the units take the damage
    by the §9.4 rule (their shields count as hit points, several may die per warhead); a
    unit group counts as affected by every damage type, and the warhead's target set is
    never checked (§10.6).
46. **Seeker merging.** Answered (confirmed: binary): yes; any seeker of the same empire,
    component and target on the launch square takes the new one in, and keeps its
    travelled count and first launcher (§10.1).
47. **Enemy tonnage destroyed.** Answered (confirmed: binary): the victim's hull
    `Tonnage` (a unit group's for all its units, when the whole group dies); planets,
    seekers and captures add nothing; every design in the killing object gets it; no
    empire sees another's design statistics (§15).
48. **Orders after combat in turn-based games.** Answered (confirmed: binary): the group
    whose movement step or warp jump started the battle has its whole order list cleared
    (spec 03 §6.4); an Attack order is used up, a Seek order stays; other participants
    only lose a Sentry at the head of their list; simultaneous games clear nothing (§2).
49. **A player's phase.** Answered (confirmed: binary): the side's drones and seekers always
    move first, so a player launches afterwards, and hand-launched drones first act at
    the next phase; Auto is one toggle for all empires from the next phase on; Resolve
    Combat hands every empire to its strategies until the end; the end is checked only
    after a phase (§4).
50. **Tactical groups.** Answered (confirmed: binary): a member takes the next slot of the
    leader's formation; a number that already has a leader cannot be given to another;
    no leader, blocked or not, dissolves its group by moving (§5).
51. **Launch Fighters in Groups.** Answered (confirmed: binary): it launches fighters only,
    in single-design groups of 5 to 50; drones are always one per group, and satellites
    are grouped per "Launch Units" window (§10.4).
52. **Firing by hand.** Answered (confirmed: binary): yes to both; the damage type is not
    checked and point-defense may be fired by order (§6).
53. **Who is asked.** Answered (confirmed: binary): on one machine, one question per battle
    for all human empires in it, hostile or not; on different machines and in
    simultaneous games, nobody (§2, §3).
54. **Combat simulator.** Answered (confirmed: binary): up to 10 sides, each a copy of the
    real empire owning its first item; fought in the player's home sector with that
    sector's interference and disruption but no system modifier totals; new ships without
    experience; start positions by side number (§17).
55. **Saving during a battle.** Answered (confirmed: binary): impossible; the combat windows
    are modal, cannot save and cannot be closed before the battle ends (§4).
56. **Groups of several designs in a battle.** Answered (confirmed: binary): a kill by a
    mixed group credits every design in it; no armor special of the units acts (§9.4);
    launched units that no carrier recovers stay separate groups (§10.4). How the engine
    handles the rest of such a group is §19.1.

### 19.1 The engine's own choices and OpenSE4 extensions

The engine (`src/game/combat*.cpp`) marks each of these choices "(inferred)". The
confirmed rules above take precedence over any older engine behaviour; where the engine
still differs from them, [PARITY_GAPS.md](../PARITY_GAPS.md) lists it. Section 19.2 names
the engine's current choice where the settled rules leave a detail open.

- **Groups of several designs (Q56).** A group that mixes designs is one piece: a unit
  group in space, or a group launched from one "Launch Units" window (§10.4). The §9.4
  draws pick among all its design entries, dead ones too; a unit's hit points are its
  design's. A fighter group's identical weapons (same part and mount) fire together
  whichever designs carry them; a satellite or drone group fires each weapon of each unit
  of each design. Offense and defense are the best design's (§7), counting the designs
  whose units have all died, speed the slowest design's (spec 03 §12), and the target
  budget counts the living units. Each loss of units is recorded (a `UnitsLost` event,
  for the Strategic Combat window and the replay). A launched group that mixes designs
  keeps the smallest full supply load of its designs.
- **Facility losses (Q39).** OpenSE4 does not copy the original's stale count of
  destroyed facilities (§11): each destroyed facility keeps working until the battle
  ends and is then removed once.
- **Mines outside the movement phase.** Without a record of the entering group (a
  battle outside the movement phase), every vehicle in the sector counts as entering,
  one group per empire. In the original, mines only ever strike a group as it moves in.
- **Tactical groups (Q50).** A group a player formed is not a fleet group: its pieces use
  their design's strategy when the computer plays them.
- **Planning dice.** Each empire's pieces break their planning ties (the 1-in-10 and
  coin-flip choices of §16.1) with random numbers of their own, forked from the battle's
  at setup. The original uses one stream for everything; ours keeps a side's moves, given
  as orders by hand, playing out exactly as its strategies' do (the tactical tests rely
  on it). Both are deterministic.
- **OpenSE4 extensions in the tactical window (Q49).** Auto can be given to a single
  piece, which then acts by its strategy at once, and "Auto This Phase" lets the
  strategies play the rest of the player's phase (the battle's Auto toggle and Resolve
  Combat follow §4). The client can end a player's phase by itself when no enemy is left
  (an option); the battle still ends only when a phase ends, as in the original.
- **Quitting during a battle (Q55); OpenSE4 detail.** As in the original, a battle is
  never saved in progress. If the program is quit while a battle asks Tactical or
  Strategic, or is being fought, the order that started it (or End Turn) is dropped: the
  game loads as it was before that order. Once the battle is over, the game saves with
  its results.

### 19.2 Questions from implementing the settled rules

Each names the engine's choice, marked "(inferred)" in the code.

57. **Numbering the empires in the middle.** Our game keeps vehicles apart from a
    system's list of objects. The engine ranks an empire by its first colony in the
    system's object list, else by its first vehicle in the system in the game's vehicle
    order (§3 step 4). Is the original's list ordered the same way?
58. **Placement hops.** The ten random hops walk on from one another, a hop onto a square
    off the map simply continuing, and the growing-squares search starts from the square
    first drawn. Is that the original's walk?
59. **Arrivals from farther away.** A vehicle that came from a sector of the same system
    that is not a neighbour (never the case for a step) starts at the edge or corner in
    that sector's direction.
60. **A weapon's target while its piece plans.** For the attack map and the fire-first
    test (§16.1) a ready weapon's target is the first of the sorted targets it can hit and
    affect, wherever the piece stands: range and the target budget are left to the moment
    it fires.
61. **Choosing the square.** The range strategies scan column by column (as Don't Get
    Hurt's tie rule suggests), and the piece's own square may be chosen (staying put) even
    though a piece stands on it. "Danger within 10 squares" is read as a square with
    danger within 10 squares (either axis) of the piece. The attack map leaves
    point-defense out, and the emissive cut compares one shot's value.
62. **When the computer launches.** A computer carrier or planet launches as it acts, a
    carrier before it moves; the new groups act later in the same phase. Drones go in
    batches of "Drones Per Target" until the limit or the rate is reached, the last batch
    possibly smaller.
63. **Launch Units windows.** When the group made earlier in the same window has died,
    the next launch starts a new group.
64. **Tactical groups.** A member given a number whose leader's formation has no free
    place left keeps its number but no place, and acts on its own. A new leader of a
    number keeps the members' earlier places. Clearing a fleet's leader leaves its members
    without a leader (a fleet's group has no number).
65. **Ground combat.** The stack of the killing side credited with dead units' tonnage is
    drawn among its stacks alive as the round began. Units without weapons do not roll
    (they would add nothing).
66. **Seekers.** When a hit of another type (not hull-damaging) destroys a member, the
    group's pool stays as it was.
67. **The ram's blow.** The rammer's `Damage Modifier - System` applies to its blow on the
    target, as to any hit with an attacker; the recoil has none (§10.3).
68. **Shield pools.** A unit group's shield pool has no cap; during one mine strike each
    unit group keeps its own shield pool, and it is cleared afterwards.
69. **Strategies without a target.** Drop Troops with no hostile colony to land on, and
    Board or Ram with no target, fall back to Don't Get Hurt; Ram is possible for any ship.
70. **The end of an unseen battle.** A battle without player sides (strategic resolution)
    checks its end after whole combat turns, as the strategic window does (§4), so the
    phases left in the combat turn are still played.
71. **The simulator's location.** The battle stands for one in the viewer's home sector
    (that sector's interference and disruption apply), but is fought in an empty system of
    the home system's type so that nothing else there takes part. Every side uses the
    viewer's strategies, which the simulator's items and fleets pick from.
72. **Invaders of an empire that is gone.** Landed troops of an empire no longer in the
    game fight on in the colony owner's step.
73. **A check that finds nobody to fight.** The battle check of §2 counts minefields (and,
    in a turn-based game, mothballed ships) as objects to see, but minefields never become
    pieces. When the check passes and no two empires with pieces in the sector are hostile
    to each other (in either direction), the engine fights no battle, so the moving group
    keeps its orders; the mines have already struck it. Does the original start an empty
    battle there, clearing the group's orders?
74. **Colonies in the check.** A colony in the sector is always seen by the empire making
    the check, as planets cannot hide in combat; a vehicle is seen when the viewer's sensors
    detect it, or, uncloaked, unless a storm or nebula obscures its sector. Does the check
    apply sight obscuration to colonies?
75. **"Uncloaked" in the simultaneous check.** An empire counts as able to see when it has
    a vehicle in the sector whose status is not cloaked, a unit group, even a minefield,
    included, although a unit group's cloak applies whatever its status (spec 03 §19 Q43).
    Is a unit group whose cloak works a seeing side?
76. **A Seek stepping in.** A drone group's pursuit that steps into its target's sector
    makes a movement step, so a battle there clears its list like any step's (spec 03 §6.4);
    only a Seek already at its target keeps its order after the battle it starts.
77. **The Attack order and Repeat.** "Used up" is read as done: with Repeat on, the Attack
    goes to the end of the list like any completed order. With no movement left it is
    removed the same way, and its owner is told in the log (an OpenSE4 message).
