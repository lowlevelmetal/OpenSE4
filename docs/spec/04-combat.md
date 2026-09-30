# Spec 04: Combat (space, planetary, ground, capture, tooling)

Status: reference spec for engine work. It was first written clean-room from the SE4 Deluxe
manual (HTML and PDF), the self-documenting data files, and the version history that
ships with the game (`History.txt`, `DataFileHistory.txt`). Everything is paraphrased.
The version history records rule changes made after the manual was written, so **where
it contradicts the manual, the history wins** and we cite it as "(history 1.xx)".

On 2026-09-29 most rules below were checked against the original executable. Rules
marked **(confirmed: binary)** describe what the game really does, in our own words;
where the binary disagreed with an earlier reading, the rule was rewritten. Rules still
marked **(inferred)** are our own choices. Section 19 lists the open questions, with
the answers found so far.

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

**Turn-based games.**

- Combat is checked when a group of vehicles moves into a sector or carries out an order
  there. Mines strike first (§10.6), then the battle check runs (confirmed: binary).
  Vehicles that merely sit in a sector do not start a new battle.
- A battle starts when two hostile empires present in the sector can see each other.
- **Hostility** follows treaties: an empire is hostile to another when its treaty with
  it is below Non-Aggression, that is War, Non-Intercourse, "None" (met, no treaty) or
  not yet met. Non-Aggression and anything better never fight (confirmed: binary).
- Once a battle starts, **every owned object in the sector** becomes a piece, including
  the vehicles and planets of empires that are hostile to nobody there, and every piece
  is decloaked for the rest of the battle (confirmed: binary; history 1.28). Treaties
  still apply inside the battle: no piece fires on or is fired on by a non-hostile
  empire.
- Undetected cloaked vehicles do not count when deciding whether a battle starts.
- Mines are not combat pieces. They act when a vehicle group enters their sector (§10.6).
- Combat does not clear orders or stop movement. Its only effect on orders is that a
  Sentry order at the head of a participant's list is removed (spec 03 §6.3, confirmed:
  binary). A ship that changes owner in the battle loses its orders (§12).

**Simultaneous games.** Tactical combat is never offered, and the computer resolves every
battle. The manual's older rule (combat only on every 5th day) was replaced (history 1.15,
1.42). Movement now triggers combat, at most once per sector per movement phase, and only
in sectors where a vehicle executed orders while hostile pieces were present. Results
arrive as log entries. The Settings flag `Simultaneous Games Show Strategic Combat`
decides whether the strategic view is shown.

**Game option "No Tactical Combat".** It removes the tactical choice, so every battle is
strategic.

## 3. Battle setup

1. Each human participant chooses **Tactical** or **Strategic** resolution. The rules are
   identical, and only control differs.
2. **Pieces.** Every owned object in the sector becomes a piece: ships and bases, planets
   with a colony, satellite, fighter and drone groups already in space. Minefields and
   storms never become pieces. Unowned stars, warp points, comets and uncolonised
   planets become neutral 4×4 obstacles (confirmed: binary).
3. **Defenders and attackers.** Each vehicle remembers the sector it last left when it
   moved this game turn; a vehicle that has not moved since the last turn processing,
   and every planet, counts as having been in its current sector. An empire is a
   **defender** if one of its pieces was already in the battle sector, otherwise an
   attacker (confirmed: binary).
4. **Placement** (confirmed: binary).
   - The start area is a square box. Its side is 6 squares when the battle has at most
     20 pieces, 12 for 21 to 40, 18 for 41 to 60, and 24 (doubled to 48 along the edge
     it lies on) for more than 60.
   - Pieces that came from one of the 8 neighbouring sectors start in a box against
     the matching edge or corner of the map: from the north-west sector in the top-left
     corner, from the north sector at the top centre, and so on. Pieces that were
     already in the sector start in a box around the map centre (36, 31). When several
     empires start in the centre, each gets its own box beside the centre.
   - Planets and obstacles are placed first, then group leaders, then everything else.
     A formation member goes to its formation slot relative to its leader. Any other
     piece takes a random square inside its box. If that square is taken, the nearest
     free square is used.
5. **Combat groups.** Each fleet becomes one combat group: the fleet leader leads it and
   the fleet's formation applies.
6. **Starting values** (confirmed: binary). Shields start at their maximum (§9.2),
   except that a ship or fighter group with zero supplies starts with none. Every
   weapon's reload counter is 0 (ready). Movement points are full.
7. **Phase order** (confirmed: binary). The order in which empires act is drawn once,
   at setup, and kept for the whole battle: all defenders first in a random order, then
   all attackers in a random order.
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
   the computer (and for humans with **Auto** on). **Resolve Combat** is the same as
   Auto for the rest of the battle.

Point-defense may fire at any moment, including during other empires' phases (§10.2).

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
  (confirmed: binary; history 1.55, 1.71). A seeker cannot move in the turn it is
  launched (§10.1).
- **Combat groups.** When the leader moves, each member moves toward its formation slot
  relative to the new leader position, spending its own movement points
  **(inferred)**. The group dissolves when the leader is destroyed, removed from the
  group, or blocked in its movement (history 1.03). In tactical mode, groups are set
  with Set Group Leader, Set Group Member, Clear Group Assignment and Clear All Group
  Assignments. Hotkeys give 10 groups (0–9). Members of an AI side leave formation
  according to the strategy's `Break Formation` flags (§16) or when the leader is
  surrounded (history 1.21).
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
components. Several colonies with such facilities therefore add up.

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
2. **Damage pool.** Every ship, planet, unit group and seeker keeps a pool of damage
   that was too small to destroy anything. For the *hull-damaging types* (Normal, Skips
   Normal Shields, Skips Armor, Skips Shields And Armor, Skips All Shields, Quad/Double/
   Half/Quarter Damage To Shields), the pool is added to D and emptied.
3. **Push, pull and teleport** move the target now (§9.5) and the hit then continues.
4. If the weapon cannot target this kind of piece, D becomes 0.
5. **Crew Conversion**, **Increase Reload Time** and **Disrupt Reload Time** take effect
   now (§9.5) and set D to 0. Shields do not stop them.
6. **Shields** (§9.2), unless the type skips them. Unit groups have no piece-level
   shields; their units' shields count as hit points (§9.4).
7. **Crystalline armor** (§9.3), for Normal, Skips Normal Shields, Skips All Shields and
   Quad/Double/Half/Quarter only.
8. **Emissive armor** (§9.3), for the same types.
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
- **Shield Regeneration**: at the start of each combat turn after the first, a piece
  regains the total `Shield Regeneration` of its intact parts, up to its maximum, while it
  has supplies.
- When a generator is destroyed, the maximum drops and current shields are capped at the
  new maximum.
- Shields refill at the start of the next battle.

### 9.3 Armor specials (confirmed: binary)

- **Emissive** (`Emissive Armor`, largest single value E). Compare E with the hit's
  damage *before* shields (after the system damage modifier, without the pool). If the
  hit is E or less, nothing reaches the hull (shields were still drained) and the pool
  keeps its old value. Otherwise E is subtracted from what got past the shields.
  Emissive does not act against Skips Armor, Skips Shields And Armor or the "Only"
  types.
- **Organic** (`Armor Regeneration`, total V). At the start of each combat turn after the
  first, while it has supplies, the ship adds V to a regeneration pool (at most 10000).
  Destroyed components with `Armor Regeneration` are then restored whole, in design order,
  each one costing its structure from the pool. When no such component is destroyed,
  the pool empties, so nothing is saved up before the first loss (history 1.80). At the
  end of the battle, up to 10000 points' worth are restored at once (history 1.79).
- **Crystalline** (`Shield Generation From Damage`, total V). From each hit that gets
  past the shields, the piece gains min(V, damage) shield points, up to its shield
  maximum (only if the maximum is above 0). The damage itself is **not** reduced.
- **Stealth and scattering armor** add a defense bonus that stacks with ECM (§7), plus
  cloaking or scanner-jamming effects covered in the sight spec.

### 9.4 Destroyed and crippled

- **Destroyed:** every component destroyed; a self-destruct; the loser of a ram; the
  self-destruct triggered by boarding (§12).
- **Crippled** (the vehicle survives but is impaired): lost command gives 1 speed on the
  system map and so 1 combat movement; lost engines reduce movement; lost weapons cannot
  fire; lost shield generators cut shields. Lost cargo components destroy the cargo they
  held. Which items are lost when capacity only partly drops is open.
- **Unit groups** (confirmed: binary). A group has a damage pool. A hit adds its damage to
  the pool (for the "Only" types and other non-hull types only for that hit). Then, up
  to 20 times, one of the group's unit designs is picked at random; if the pool is at
  least one unit's hit points, one unit of that design dies and its hit points leave the
  pool. A unit's hit points are its design's structure plus its shields; for fighters,
  troops and weapon platforms the shields are counted twice (once less when the damage
  type skips shields). The rest stays in the pool for later hits. The group dies with
  its last unit. `Shields Only` damage fills a separate shield pool that makes later
  kills easier.

### 9.5 Damage types (`Weapon Damage Type`) (confirmed: binary unless marked)

| Type | Effect |
|---|---|
| `Normal` | §9.1. |
| `Shields Only` | Drains shields only. Harmless once shields are 0. Against a unit group it fills the group's shield pool. |
| `Quad/Double/Half/Quarter Damage To Shields` | Against shields, D is first multiplied by 4 or 2, or divided by 2 or 4 (truncated). What is left after the shields is converted back (divided by 4 or 2, truncated, or multiplied by 2 or 4) and continues as Normal. |
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
| `Plague Level 1..5` | Planets only, after shields. Sets that plague level unless the owner's race is immune. |
| `Only Planet Population` | After shields, kills D ÷ `Damage Points To Kill One Population` million population (truncated, at least 1). |
| `Only Planet Conditions` | After shields, lowers the planet's conditions by D × 0.1. |
| `Only Resupply Depots`, `Only Spaceports` | After shields, destroys one facility with `Supply Generation` / `Spaceport` (the first one found). |

Drones apply special types too (history 1.86). A mine does not detonate against a target
its damage type cannot affect (history 1.70).

## 10. Special weapons and units

### 10.1 Seekers (confirmed: binary)

- Firing a `Seeking` weapon uses supply and reload as usual and creates a seeker piece on
  the launcher's square (for a planet, one of its central squares), aimed at the
  weapon's target. If a seeker of the same empire, weapon and target already sits on
  that square, the new one joins it instead: a seeker group counts its members.
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
- **Durability.** A seeker has `Weapon Seeker Dmg Res` hit points and defense +40 (§7).
  A hit whose damage (plus the seeker's pool) reaches its remaining hit points destroys
  one member of the group; smaller hits go into the pool. Point-defense may fire after
  every seeker step.
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
- Let R = the rammer's remaining hit points and T = the target's remaining hit points.
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
- Kills by ramming give experience and count in design statistics (history 1.22).

### 10.4 Fighters (confirmed: binary unless marked)

- **Launch.** Each combat turn a ship may launch as many fighters as the total `Launch/
  Recover Fighters` Value1 of its intact bays. A planet may launch up to 100 of each kind
  of unit it holds per combat turn. The per-player unit cap does not apply in combat
  (history 1.46).
- **Grouping.** Launched fighters form groups of the strategy's `Fighters Launch Group
  Amount` (stock 10), or the tactical "Launch Fighters in Groups" choice. The Settings
  `Combat Fighter Group Amount`, `Combat Mine Group Amount` and `Combat Satellite Group
  Amount` are loaded but never used.
- A fighter group has a target budget of 1 and fires its identical weapons as one
  combined hit (§6).
- Supplies are per fighter and never pooled with the fleet (history 1.68). With zero
  supplies a fighter group cannot fire and has no shields.
- **After combat** each carrier or planet recovers the fighter and satellite groups that
  it launched in this battle (ships first, then planets). Groups whose carrier is gone
  stay in space.

### 10.5 Satellites and weapon platforms

- Satellites are stationary pieces in groups. Their target budget is covered in §6. They
  are launched with `Launch/Recover Satellites` (Value1 per combat turn). A player may
  have at most `Maximum Satellites Per Player Per Sector` (100) in one sector.
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
- Then each mine picks a random vehicle of the group. Fighters and drones are skipped
  (and another is picked) unless Settings `Fighters Can Be Hit By Mines` / `Drones Can Be
  Hit By Mines` allow them. The mine's warheads strike it one after another, skipping
  those whose damage type cannot affect it, until it is destroyed. The mine is used up.
- Mine damage goes **straight to the components** (§9.1a): shields, emissive and
  crystalline armor do not act against mines. Leftover damage is shared by the whole mine
  strike and joins the next warhead, whichever vehicle it hits (history 1.70, 1.78).
- A player may have at most `Maximum Mines Per Player Per Sector` (100) in one sector.

### 10.7 Drones

- Drones are launched by `Launch Drones` components (Value1 per combat turn; a planet 100)
  and have full movement immediately (confirmed: binary).
- Drones launch in groups whose size is the strategy's "Drones Per Target" option
  (default 3). That option is set in the strategies window and stored with the game; it
  is not a key of DefaultStrategies.txt (confirmed: binary).
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
  + the hit points of the units in its cargo − its damage pool.
- **Damage order for a hull-damaging hit** (after shields):
  1. If the cargo holds weapon platforms, the hit goes to the platforms only (as a unit
     group, §9.4).
  2. Otherwise, other units in cargo take it (as a unit group).
  3. If, after this, no units in cargo are left, the population loses D ÷ `Damage
     Points To Kill One Population` million (truncated, at least 1), using the whole
     hit D.
  4. Then, with a 1-in-3 chance, facilities are destroyed so that the number of
     facilities is at most (current planet hit points) ÷ (starting hit points ÷ total
     facilities), both divisions truncated.
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

- **Triggers:** troops dropped during a space battle (fought at once), or hostile troops
  on a planet during turn processing.
- **Sides.** The attacker has its landed troops. The defender has the units in the
  planet's cargo plus militia.
- **Militia.** When the first invading troops land on a colony, the colony gets a militia
  pool: for each population group, its population in millions ÷ `Defending Units Per
  Population` (20), truncated, summed. There is no minimum, so a colony below 20M has no
  militia. At each ground combat, each population group raises that same number of
  militia, limited by what is left of the pool; the survivors become the new pool. Each
  militia unit has `Population Defender Attack Strength` (10) attack and `Population
  Defender Hit Points` (30). Militia losses cost no population.
- **Length.** At most Settings `Number Of Ground Combat Turns` (10) rounds per ground
  combat, ending early when either side has no troops or militia left. If both survive,
  the fight resumes next game turn. It stops at once on peace or surrender (history 1.03,
  1.20).
- **Round.**
  1. Each side's offense and defense are (total `Combat To Hit Offense/Defense Plus` −
     `Minus` over all its units' abilities, family-best as in §7) ÷ 2, truncated.
  2. Every troop and militia unit rolls: it hits if roll ≤ (its side's offense + 50 −
     the other side's defense). There is no clamp.
  3. A hit adds the unit's attack: militia use the setting; a troop unit uses the sum,
     over its weapons, of each weapon's largest table entry (with mount). Units without
     weapons do not attack.
  4. Each side's total, plus damage carried from the previous round, is multiplied by
     `Ground Combat Damage Modifier Percent` (30) / 100 and truncated.
  5. The defender adds Round(total × the planet's `Planet - Change Ground Defense` /
     100). Each side adds Round(total × its racial ground modifier / 100), where the
     racial ground modifier is the culture's `Ground Combat` value plus (Physical
     Strength − 100) plus matching racial traits.
  6. Each total is applied to the other side's stacks in cargo order: troops and militia
     first, then any other units. In each stack, whole units die while the damage covers
     their hit points (structure plus shields for troops, the setting for militia); the
     rest moves on to the next stack. What remains at the end is carried to the next
     round (divided back by the percentage, truncated).
- **Victory.** When the defender has no troops or militia left and the attacker still
  has troops, the attacker takes the planet with its surviving facilities, stored units
  and population. When the attackers are gone, the fight ends. No facilities are lost in
  ground combat.
- **Reinforcing.** Either side may drop more troops at any time. A third empire may not
  land on a planet already contested by two other empires.

## 14. Retreat and disengagement

Stock SE4 has no retreat order and no way to leave the map (confirmed: binary: no such
command exists). The only ways to disengage are:

- a `Don't Get Hurt` strategy, which keeps the piece out of enemy range;
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
- **Design statistics:** kills and enemy tonnage destroyed are credited to the killer's
  design(s); losses to the victim's.
- **Log.** One battle report per participant, listing losses and ships "Taken". If
  Settings `Create Combat Replay` is on, also record a replay stream (§17).
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
| `Primary Movement Strategy`, `Secondary Movement Strategy` | One of: Don't Get Hurt (stay where no enemy can fire on you); Drop Troops (close on a planet and land troops); Maximum Weapons Range (sit at your longest range from the target); Optimal Weapons Range (the square giving the best ratio of damage dealt to damage taken); Short Weapons Range (1–3 squares, least exposure); Point Blank (as close as possible); Board Enemy Ships (close to adjacent and capture); Ram. Use the secondary strategy when the primary is impossible, for example Drop Troops with no troops or no planet. |
| `Targeting Priority 1..4` | Sort keys for choosing a target, applied in order: Nearest, Farthest, Largest, Smallest, Most Damaged, Least Damaged, Fastest, Slowest, Strongest, Weakest, Has Weapons, Does Not Have Weapons. The first key filters or sorts, and later keys break ties. |
| `Use Type Priority First`, `Type Priority <Cat>` | Rank per category (1 = engage first) over 14 categories: Planets, Fighters, Seekers(On Us), Seekers(On Others), Mines, Carriers, Colony Ships, Transports, Bases(No Weapons), Ships(No Weapons), Bases, Ships, Satellites, Drones. The flag decides whether category rank is applied before the targeting keys. The UI also offers turning type priority off. |
| `Dont Fire On <Cat>` | Never engage that category. |
| `Fighters Launch Group Amount` | Fighter group size at launch. |
| "Drones Per Target" | Drone group size at launch (default 3). Not a DefaultStrategies.txt key (§10.7). |
| `Break Formation <Cat>` | Whether own pieces of that category leave the formation in combat. |
| `Damage Percent Per Ship / Planet / Fighter Group / Satellite Group` | (confirmed: binary) While choosing targets, a ship, planet, fighter group or satellite group already damaged by more than this percentage is skipped, unless `Damage Until All Weapons Gone` is set and it still has weapons. Drones and seekers are never skipped this way. If no target is left, the choice is made again without this filter. |
| `Damage Until All Weapons Gone` | See the row above. |

The computer assigns a weapon to a target only if the weapon can reach it and its damage
type can affect it (confirmed: binary). It stops assigning direct fire to a ship or
planet once the damage already assigned to it this turn reaches 1.5 × (its shields + hit
points), and to a unit group or seeker once it reaches 1 × that; seekers in flight are
counted separately against the same limits, so enough seekers aimed at one target move
new launches to the next (confirmed: binary; history 1.82, 1.86). The AI fires before
moving only when its move takes it farther from its target; otherwise it moves first
and then fires. It dodges enemy seekers and closes one extra square on planets
(history 1.60).

## 17. Combat tooling

- **Combat simulator.** A mock battle in which nothing is really lost. Choose from your
  designs, enemy designs you have seen, and sample planets from your home system. Assign
  each item to one of several virtual empires, which count as separate empires and are
  all hostile to each other (confirmed: binary). You may edit cargo (fighters on
  carriers, platforms on planets), form fleets, edit strategies, and choose which virtual
  empires the computer controls. Run it tactically or strategically. Minefields cannot
  be added. Obsolete designs can be hidden. It must never change real game state
  (history 1.46).
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

Answers found in the executable are given in place. The remaining questions still need
checking in the running game.

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
18. **Repeat battles.** Partly answered (confirmed: binary): battles are checked only when
    a vehicle group moves or carries out an order in the sector. Still open: whether any
    order other than movement (for example Sentry or Attack) restarts a battle every game
    turn.
19. **Experience.** Answered (confirmed: binary): §15. The level names shown in the UI are
    not covered here.
20. **Cargo loss.** Which stored items are lost first when cargo components are
    destroyed? Open.
21. **Treaty "None".** Answered (confirmed: binary): yes, "None" and "not yet met" fight.
22. **Designs without supply storage.** Answered (confirmed: binary): bases always have
    unlimited supplies; ships without supply storage have none, so they cannot fire and
    have no shields; satellites, drones, weapon platforms and planets never need supplies
    in combat.
23. **Mothballed ships.** Partly answered (confirmed: binary): a mothballed ship in a
    battle sector becomes a piece with no abilities, no shields, no movement and no
    offense or defense. Whether its presence alone can start a battle is open.
24. **Mine timing.** Answered (confirmed: binary): only the group that just moved in
    (§10.6).
25. **Don't Get Hurt.** How far ahead does it look? Open.
26. **Holding fire for an invasion.** The AI skips some planets when choosing targets;
    the exact condition is open.
27. **Captures in the statistics.** Open.
28. **Conversion and self-destruct.** Answered (confirmed: binary): no, a conversion never
    sets off a self-destruct device.
29. **Captured troops.** Whose side are troop units on after the ship carrying them is
    captured? Open.
30. **Drone launches.** Answered (confirmed: binary): in groups of the strategy's "Drones
    Per Target" (default 3), §10.7.
31. **Militia losses.** Answered (confirmed: binary): militia deaths do not reduce
    population; the colony's militia pool shrinks instead (§13).
32. **Damage pool between battles.** Does a ship's damage pool (§9.1) survive the end of a
    battle? Open.
33. **Shield-multiplier types against empty shields.** Are Quad/Double/Half/Quarter Damage
    To Shields scaled when the shield pool is already 0? Open.
34. **Hit points in a ram.** Which "remaining hit points" do R and T use (§10.3): intact
    structure only, or also shields and the damage pool? Open.
35. **Warp arrivals.** Where does a vehicle start that entered the sector through a warp
    point, since the sector it left is not a neighbour? Open.
36. **Several empires in the middle.** How are the boxes beside the centre laid out, in
    which order do empires get them, and which way do their pieces face? Open. The same
    for corner boxes when a battle has more than 60 pieces.
37. **Satellites launched in combat.** How many satellites form one group? Open.
38. **Unit groups.** How does the Shields Only pool make later kills easier, and what
    exactly happens to the pool after a hit of a non-hull type (§9.4)? Does emissive
    armor act on a unit group? Open.
39. **Planet pool and facilities.** Is a planet's damage pool emptied when the population
    takes a hit (§11 step 3)? Which facilities are destroyed in step 4? Open.
40. **Other types against planets.** What do the "Only" types other than Only Weapons,
    push, pull, teleport and the reload types do to a planet? Open.
41. **Ground combat details.** Are militia hit before or after the defender's troops? Do
    stored units other than troops count toward a side's offense and defense? Are the
    planet's and the races' ground modifiers both taken from the same total (§13 step 5)?
    Open.
42. **Organic armor.** When the pool cannot pay for the next destroyed component in design
    order, does restoration stop, or skip to a cheaper one? Open.
43. **System modifiers during a battle.** Are the combat, damage and shield modifier totals
    taken once when the battle starts, or recomputed as parts are destroyed? Open.
44. **Planet conditions.** `Only Planet Conditions` lowers conditions by D × 0.1 on the
    0–1.5 scale: confirm the scale of D against a known weapon. Open.
45. **Mines against unit groups.** How do mine warheads damage a group of units in space?
    Does a mine check the warhead's target set as well as its damage type? Open.
46. **Seeker merging.** Does a new seeker join one on the launch square that has already
    moved? Open.
47. **Enemy tonnage destroyed.** Which tonnage does the design statistic add: the victim's
    hull `Tonnage`, or the tonnage its components fill? Do captured ships and destroyed
    planets add anything? Who sees another empire's statistics? Open.
48. **Orders after combat in turn-based games.** §2 says combat neither clears orders nor
    stops movement, while spec 03 §6.4 says that in turn-based games combat on entry stops
    the move and clears the list. OpenSE4 fails the entering group's order (spec 03) and
    otherwise only removes a Sentry at the head of a participant's list. Which groups lose
    their orders in the original? (inferred)
49. **A player's phase.** May a player launch units before the side's drones and seekers
    move (a computer side launches first, so its new drones act with the others)? Does
    Auto hand the rest of this phase to the strategies, or every phase from then on (as
    Resolve Combat does)? Does the battle end in the middle of a player's phase when the
    last enemy dies, or when the phase ends? (inferred)
50. **Tactical groups.** Where does a ship take its place when it joins a group: where it
    stands, or in a formation slot? What happens to a group when its number is given to a
    new leader? Does a player's leader that is blocked on its way dissolve its group, as
    a computer leader's does? (inferred)
51. **Launch Fighters in Groups.** Does the chosen group size apply to drones too, and how
    many satellites form one group when a player launches them? (inferred; see Q37)
52. **Firing by hand.** May a player fire a weapon whose damage type cannot affect the
    target (the computer never does), and fire point-defense by order? (inferred)
53. **Who is asked.** Is a human empire that is present in a battle but hostile to nobody
    there asked Tactical or Strategic? (inferred)
54. **Combat simulator.** Which race do the virtual empires have: the player's, or that of
    the empire whose design they fly? Where is the mock battle fought, and do the system's
    abilities apply? Do the ships start with experience? How many virtual empires can
    there be? (inferred)

### 19.1 What the engine does until the remaining questions are answered

The engine (`src/game/combat*.cpp`) marks each of these choices "(inferred)". The
confirmed rules above take precedence over any older engine behaviour.

- **Cargo loss (Q20).** Stored items are removed only when a vehicle is destroyed.
- **Repeat battles (Q18).** Combat is checked wherever movement asks for it.
- **Mothballs (Q23).** A mothballed ship alone can start a battle.
- **Don't Get Hurt (Q25).** Assumes each enemy can move twice its speed before we act
  again. Among safe squares it prefers room to keep evading. A piece that has neither
  weapons nor another task falls back to it.
- **Holding fire for an invasion (Q26).** An invading empire holds fire on a planet only
  once its guns are gone.
- **Captures in the statistics (Q27).** A capture counts as a loss for the design and a
  kill for the captor.
- **Enemy tonnage destroyed (Q47).** Each destroyed ship, base or unit adds its hull's
  `Tonnage` to the killer's design (a seeker's launcher; mines their own design).
  Captures and planets add none. A new design starts at 0, and no empire sees the
  statistics of another empire's designs.
- **Captured troops (Q29).** A troop unit fights for the empire that owns its design.
- **Damage pool (Q32).** It lasts for one battle and is not saved.
- **Shield multipliers (Q33).** With the shields at 0 the hit is not scaled.
- **Ram hit points (Q34).** A ship's intact structure; a unit group's units' hit points
  minus its pool; a planet's hit points (§11).
- **Warp arrivals (Q35).** They start in the middle, but their empire is an attacker.
- **The middle (Q36).** A single empire in the middle gets the box around (36, 31). With
  several, empires in id order get boxes west, east, north and south of the centre, then
  the diagonals, and face the centre. Pieces arriving from an edge face away from it. A
  corner box stays square (24) in battles with more than 60 pieces.
- **Satellites (Q37).** All satellites a carrier launches in one combat turn form one group.
- **Unit groups (Q38).** Shields Only damage cancels up to one unit's shield part in the
  next kill. After a non-hull hit the pool keeps at most its old value. Emissive armor of
  the unit design acts as on a ship. Space unit groups have no partial damage.
- **Planets (Q39, Q40).** The pool is emptied when the population is hit; facilities are
  removed at random. Only Weapons hits the weapon platforms (as a non-hull hit); the other
  types listed in Q40 do nothing to planets. Planets regenerate shields from facilities
  with `Shield Regeneration`.
- **Ground combat (Q41).** Damage reaches the defender's troops in cargo order, then the
  militia, then the other stored units, which never attack and do not count toward
  offense or defense. Each troop design's hull values count once. Both modifiers of step 5
  are taken from the step 4 total. An invasion that ends resets the militia pool.
- **Organic armor (Q42).** Restoration stops at the first component the pool cannot pay.
- **System modifiers (Q43).** Taken once, when the battle starts.
- **Conditions (Q44).** `SpaceObject::conditions` holds hundredths of the 0–1.5 scale
  (spec 02 §2), so a hit lowers it by D × 10, never below 0.
- **Mines (Q45).** A warhead strikes the components of the group's front unit; a unit
  destroyed is removed and the next warhead hits the next unit. Only the damage type is
  checked. Without a record of the entering group (a battle outside the movement phase),
  every vehicle in the sector counts as entering, one group per empire.
- **Seekers (Q46).** Only a seeker that has not moved yet takes new members in.
- **Captures and experience.** A capture or conversion gives no experience.
- **Dodging seekers.** Among equally good squares, the computer takes the one farthest from
  the nearest seeker aimed at the piece.
- **Satellite cap.** The per-sector satellite cap (spec 03 §12) also limits launches in
  combat.
- **A player's phase (Q49).** The engine plays a player's phase in the same order as a
  computer side's: first a launch step, in which the player may launch units (and switch
  weapons) before anything else happens; the first other order ends it, the side's
  drones act (with those just launched) and its seekers move, then the order is carried
  out. Units launched after that step get their full movement at once, and launched
  drones act at once. Any number of orders follow, in any order; a piece moves while it
  has movement points and fires each weapon whose reload counter is 0. At the end of the
  phase unused movement points are lost, and every piece of the side counts as having
  acted: a ship that changes sides later in the combat turn does not act again. Auto
  hands the rest of this phase to the strategies (what the side may still launch, then
  every piece whose strategy has not acted yet); Auto with a piece has that piece act by
  its strategy now; Resolve Combat hands the side to its strategies for the rest of the
  battle. The battle ends only when a phase ends, as after a computer phase; OpenSE4's
  client ends a player's phase by itself when no enemy is left (an option). The client
  ends the launch step at once when the phase starts, so a player's launches come after
  the side's drones and seekers move.
- **Tactical groups (Q50).** Set Group Leader makes the piece lead group 0 to 9 (a
  leader keeps its members); a piece that led that group before hands its members over
  and follows the new leader too. A member keeps the place it has when it joins,
  relative to its leader, and does not turn with it. When the player moves a leader,
  each member moves toward its place with its own movement points; moving a member by
  hand keeps it in the group, and a player's leader blocked on its way keeps its group.
  A group a player formed is not a fleet group: its pieces use their design's strategy
  when Auto plays them.
- **Launch groups (Q51).** The chosen group size applies to fighters and drones; the
  satellites of one launch order form one group.
- **Firing by hand (Q52).** A weapon may fire at any hostile piece of its target set that
  it reaches (a seeker: within its travel of the target's centre square), with its
  reload, the piece's supplies and the target budget checked, as the computer's fire
  is; the damage type is not checked, and point-defense may be fired by order.
- **Who is asked (Q53).** Only human empires hostile to another empire in the battle,
  in turn-based games without "No Tactical Combat". Network games are simultaneous, so
  they never ask.
- **Combat simulator (Q54).** Each virtual empire is a copy of the player's empire (race,
  culture, technology and strategies) under the side's name, at war with the other
  sides. The mock battle is in the middle of a new, empty system of the home system's
  type, without system abilities; every side starts beside the centre. Each design used
  is copied for its side with the chosen strategy; ships start undamaged, with full
  supplies and no experience. A sample planet is a copy of the colony (population,
  facilities, stored units) moved into the battle sector. The window offers up to four
  sides.
