# Spec 01: Galaxy, game setup, systems, stellar objects, sight, victory

Status: reference spec for engine work. It was written clean-room from the SE4 Deluxe
manual, the self-documenting data files, the version history and the tutorial scenario
text. Everything is paraphrased. Where the sources are silent we say so and give a
recommended behaviour marked **(inferred)**. Section 14 lists the points to check in the
running game.

On 2026-09-29 the generation, setup, sight, hazard, warp point and stellar manipulation
rules were checked against the original executable. Rules marked **(confirmed: binary)**
describe what it does, quirks included; the engine should reproduce them unless a rule
says otherwise. On 2026-09-30 the remaining questions of §14 were settled the same way;
where the engine still differs, docs/PARITY_GAPS.md lists it.

Notation used for the confirmed rules: `R(n)` is a uniform random integer in 0..n−1,
`R[a,b]` a uniform random integer in a..b inclusive, `div` and `mod` are integer division
and remainder, and `round(x)` rounds to the nearest integer with halves going to the even
neighbour. `trunc(x)` drops the fraction.

Conventions used throughout:

- **Turn length.** One turn is 0.1 game year. The tutorial treats a 0.2-year build as
  two turns. Any rule stated "per year" or "after N years" must be converted (10 turns
  per year).
- **Chance units.** Unless noted otherwise, chance fields in the data files are in
  tenths of a percent: an integer 0..1000 compared against a uniform roll in 0..999.
- **Sizes as integers.** Abilities that take a planet size use 1=Tiny, 2=Small,
  3=Medium, 4=Large, 5=Huge. Stock components confirm this: a "medium" bomb carries the
  value 3.
- Every random draw goes through `GameState::rng` (see CLAUDE.md). Generation may use
  floats; turn resolution must not.

---

## 1. Data-file grammar (shared by every file in this spec)

- **Tolerate** CRLF line endings, a last line without a newline, tabs before `:=`,
  trailing spaces, and non-UTF-8 bytes.
- **Structure.** A free-text header runs until the first `*BEGIN*`. Some files repeat
  `*BEGIN*` between `====` rule lines, so skip any number of marker and rule lines.
  Records end at `*END*`. A record is a run of `Key := Value` lines ended by a blank
  line. Trim keys and values, and keep keys in file order.
- **Indexed groups.** A count field (`Number of System Objs`, `Number of Abilities`,
  `Number of Poss Abilities`, `Number of System Types`, `Num Messages`,
  `Num Start Messages`, `Text Number of Paragraphs`) is followed by keys such as
  `Obj 3 Position` or `Ability 2 Val 1`. Parse them by index, and warn when the count
  and the keys disagree.
- **Values.** Booleans are `TRUE`/`FALSE` in any case. Which fields a record has depends
  on its type: a SectType star has `Star ...` fields, while a planet has `Planet ...`
  fields.
- **Special files.** `Settings.txt` is a single flat record. `SystemNames.txt` has no
  header or markers and holds one name per line.
- **Aliases.** Accept `Sun`/`Star` and `Gas Giant`/`Gas` as equivalent. A `Sector - X`
  ability in a system-level list applies to every sector of that system.
- **Abilities.txt** only documents what each ability means; the game never reads it.
  Ability names are therefore a closed enumeration in the engine.
- **Mods.** Look a file up in the mod directory first, then in the base directory.

---

## 2. Game setup

### 2.1 Entry points

- **Intro window.** Quick Start, New Game, Resume (loads the most recent save), Load
  Game, Tutorial, Scenario, Credits, Quit. The buttons stay disabled until the data files
  have loaded.
- **Quick Start.** The player picks one race portrait. The candidates come from the
  `Settings.txt` keys `Number of Quick Start Styles` and `Quick Start Style N`. All other
  settings keep their defaults.
- **Scenario and Tutorial.** These load a prepared savegame plus a text script (§12).

### 2.2 New Game: eight tabs

**Quadrant tab**

- **Quadrant Type.** Picks one record from `QuadrantTypes.txt` (§3.1).
- **Quadrant Size.** Small, Medium (the default) or Large. With M = `Settings: Maximum
  Number Of Systems` (stock 100) and q = M div 5, the number of systems is (confirmed:
  binary):
  - Small: q + R(q), so q..2q−1 (stock 20–39);
  - Medium: 2q + R(2q), so 2q..4q−1 (stock 40–79);
  - Large: 4q + R(q), so 4q..5q−1 (stock 80–99).

  Placement can end with fewer systems when it runs out of room (§3.3).
- **Generate Map Now.** Builds and previews a quadrant. Pressing it again rerolls. The
  previewed map is the one the game starts with.
- **Load Map.** Uses a saved map (§12). **Save Map** writes the previewed map.
- **General options** (a list of seven checkboxes, confirmed: binary):
  - *All Warp Points connected* (on by default): adds the connectivity pass of §3.5, and
    each system considers the full `Max Warp Points per Sys` nearest systems instead of
    half as many. With it off, isolated clusters can exist.
  - *No Warp Points*: no warp points are generated. Travel then needs the Open Warp Point
    stellar manipulation (§9).
  - *Warp Points located anywhere in system*: each warp point moves up to 4 sectors inward
    from its edge position, onto an empty sector (§3.5). It does not mean any sector.
  - *All systems seen by all players*: every system starts explored for every empire
    (static contents known) without granting presence (§6).
  - *Omnipresent view of all systems*: every empire sees every system as if present.
    See §6.5 for how this interacts with cloaking.
  - *Finite resources*: planet value becomes a stock of resources that production
    depletes (§5.6).
  - *All player planets the same size* (on by default): homeworlds must have the size set
    by Home Planet Value (§3.6).

**Events tab**

- **Event Frequency** is None, Low (the default), Medium or High. Low, Medium and High map
  to `Settings: Event Percent Chance Low/Medium/High` (stock 5, 10 and 25); None disables
  new events (confirmed: binary).
- **Maximum Event Severity** is one of Low, Medium, High or Catastrophic. It filters
  `Events.txt` by `Severity` (§10).

**Technology tab**

- **Technology Cost** is a relative multiplier on research cost.
- **Technology Areas Allowed** holds one checkbox per tech group. A group that is
  unchecked is removed from research entirely.

**Player Settings tab**

The choices and defaults below are confirmed: binary.

- **Starting Resources**: Low 5,000, Medium 20,000 (the default) or High 100,000 (see
  spec 02 §9 for how the stockpile is filled).
- **Racial Points**: None (0), Low (2,000, the default), Medium (3,000) or High (5,000).
- **Home Planet Value**: Bad, Average (the default) or Good. It selects the
  `Plr Planet Value {Low,Medium,High} Percent` key (or the matching `... Resources` key in
  finite-resource games) and also the homeworld size: Small, Medium or Large (§3.6).
- **Number of Starting Planets**: 1 (the default), 3, 5 or 10. The extra planets sit in
  nearby systems (§3.6).
- **Empire Placement**: two checkboxes. *Allowed to start in the same system* (off by
  default) and *Evenly distributed through the quadrant* (on by default).
- **Score Display**: own score only, own plus allies (the default), or everyone's.
- **Technology Level for New Player**: Low (the default: base techs), Medium or High
  (every allowed area at its maximum). Medium raises every allowed area to a middle
  starting level taken from the tech area data (see spec 05).

**Players tab**

- The list of explicit empires, with Add New, Add Existing, Edit, Remove and Save.
- **Random computer players** has two checkboxes. *Regular* AIs are full empires;
  *neutral* AIs stay in their home system.
- **Number of Computer Players** (Low, Medium or High; Medium is the default) rolls the
  count within `Minimum/Maximum Computer Player {L,M,H} Setting`. Neutrals use the
  parallel neutral keys.
- **Difficulty** (Low, Medium or High, default Medium) and **Bonus** (None, Low, Medium or
  High, default None) handicap the AI (confirmed: binary for the lists; the bonus effect
  is in spec 05 §8).

**Victory Conditions tab**: see §11.

**Game Settings tab**

- The master password and the maximum units and ships per player. The caps default to
  `Default Number Of Units/Ships Per Player`. The window checks neither cap: each is read as
  a whole number and held in 16 bits (confirmed: binary).
- A list of twelve check boxes, in this order (confirmed: binary): Cheat codes allowed;
  Team Mode; No Tactical Combat; Players can see the complete tech tree; Allow
  gifts/tributes; Allow technology gifts, tributes and trades; Allow surrender; Allow
  intelligence projects; No Ruins; Only breathable atmosphere; Only home planet type;
  Players can save the map during the game. A new game starts with the four "Allow ..."
  boxes checked and all the others clear, so No Tactical Combat is off.
- **Team Mode** allies every computer player against the humans.
- **Allow intel projects**. The intel techs remain even when this is off, but are useless.
- **No Ruins** discards an `Ancient Ruins` or `Ancient Ruins Unique` result of the
  stellar ability roll (the roll itself still happens) (confirmed: binary). Starting
  planets never keep ruins, whatever this option says (§3.6).
- **Only breathable atmosphere** allows no domed colonies.
- **Only home planet type** limits colonization to the homeworld's physical type, even
  when the empire has the technology for others.
- **Players can save map during a game**.

**Mechanics tab**

The defaults below are confirmed: binary.

- Play style: Hotseat (the default) or Different Machines.
- Turn style: Turn-Based (sequential, the default) or Simultaneous.
- Multiplayer filename and save directory.
- Autosave: None (the default), or every 1, 2, 3, 5 or 10 turns; it can also be changed
  during the game. The save is made after a turn is processed, whenever the number of turns
  since 2400.0 (the game date in tenths of a year) is a multiple of N. The file is named
  after the last digit of that number, into the save directory, so there are at most ten
  autosave files: every 2 turns uses five of them, every 5 turns two, every 10 turns one.
  Because the rule follows the game date, loading a game changes nothing about it.
- Connection type, enabled only for Different Machines with Simultaneous: manual file
  moving (the default), TCP/IP Host or TCP/IP Player. Simultaneous games on different
  machines require a master password.

### 2.3 Settings.txt keys owned by this spec

| Key(s) | Meaning |
|---|---|
| `Maximum Number Of Systems` | Hard cap on systems in a quadrant (at most 255); also sets the Quadrant Size counts (§2.2). |
| `Planet Value Low/High Percent`, `... Resources` | Inclusive range for rolling each resource value of a natural planet: a percentage, or a stock in finite mode. `Asteroids Value ...` gives the same for asteroid fields (confirmed: binary). |
| `Plr Planet Value {Low,Medium,High} Percent/Resources` | Starting-planet value for each Home Planet Value setting (Bad, Average, Good). Percent values get a small random spread (§3.6) (confirmed: binary). |
| `Maximum/Minimum Planet Percent/Resource Value` | Clamps on value changes during play. |
| `Remote Mining Decreases Asteroid Value` | Whether remote mining depletes asteroids. |
| `Planet Value Percent Loss After Owner Death` | Value lost when a colony's population dies out and the colony is removed, not when its owner is eliminated: each value drops by this many points, or by this percentage of the stock in finite games (spec 02 §1.2) (confirmed: binary). |
| `Event Percent Chance Low/Medium/High` | Per-turn event chance for each frequency (§10). |
| `Created Storm Maximum {Obscuration Level, Turbulence Damage, Shield Disruption}` | Caps on the abilities of a manufactured storm (§9). |
| `Min/Max {Computer,Neutral} Player L/M/H Setting` | Ranges for the random AI counts. |
| `Default Number Of Units/Ships Per Player` | Default caps shown in Game Settings. |
| `Number of Quick Start Styles`, `Quick Start Style N` | Quick Start roster. |

---

## 3. Quadrant (galaxy) generation

### 3.1 QuadrantTypes.txt schema

| Field | Semantics |
|---|---|
| `Name`, `Description` | Shown in the Quadrant Type list. |
| `Min Dist Between Systems` | D. A new system is rejected when some earlier system lies within D squares on **both** axes (\|dx\| ≤ D and \|dy\| ≤ D). So the Chebyshev distance between systems is at least D + 1: with D = 1, no two systems occupy adjacent squares (confirmed: binary). |
| `System Placement` | One of `Random`, `Clusters`, `Spiral`, `Diffuse`, `Grid` (§3.3). |
| `Max Warp Points per Sys` | K. Despite the name, K is the number of **nearest systems each system considers** as link candidates (§3.5), halved when *All Warp Points connected* is off. It is not a cap: a system can end up with more links, up to the hard limit of 10 warp points per system (confirmed: binary). |
| `Min Angle Between WP` | Minimum difference in degrees between the galaxy bearings of two links leaving the same system (§3.5). The connectivity pass ignores it (confirmed: binary). |
| `Number of System Types` | Count of weighted entries. The loader must support at least 300. |
| `Type N Name` / `Type N Chance` | A SystemTypes.txt name and its weight in tenths of a percent. See §3.4 for how weights that do not sum to 1000 behave. |

The stock file has six quadrant types. For example, "Mid-Life" pairs `Random`
placement with 5 warp points per system and a 60° minimum angle, and "Spiral Arm"
uses `Spiral`.

### 3.2 Galaxy coordinates

- Systems sit on an integer galaxy grid of **67 × 46 squares**: x runs 1..67 and y runs
  1..46, and y grows downward on the map (confirmed: binary). One grid square is about
  **10 light years**.
- **Distance** between two systems is round(√(dx² + dy²)) in squares (confirmed: binary).
  Open Warp Point compares this distance with its Val 1, so that value is in squares
  (§9); the UI shows ten times it as light years.
- **Bearing** from system A to system B is a whole number of degrees, 0 pointing toward
  smaller y (up the map) and growing clockwise, so 90 points toward larger x (confirmed:
  binary). With dx = |xB − xA| and dy = |yB − yA|: if dy > 0, a = round(atan(dx / dy) in
  degrees); if dy = 0, a is 90 when B is to the right, 270 when to the left, 0 when the
  positions coincide. Then a becomes 180 − a when B is below-right or straight below
  (xA ≤ xB and yA < yB), a + 180 when below-left, 360 − a when above-left, and 360 becomes 0.
- **Angle difference** between two bearings a and b is |a − b|, except that when one is
  above 270 and the other below 90 it is measured across north (360 − the larger + the
  smaller) (confirmed: binary). Two bearings such as 260 and 10 therefore differ by 250.
- Every empire knows every system's position from turn 0. Names and contents stay hidden
  until the system is explored.

### 3.3 Placement algorithms

Systems are placed one at a time, in index order (confirmed: binary). For system number
i (counting from 1), the chosen algorithm proposes a position; x is then clamped into
1..67 and y into 1..46 (values of 0 or less become 1). The proposal is rejected if it is
too close to an earlier system (§3.1). Up to 1,001 proposals are tried. If all fail, the
quadrant keeps only the systems placed so far and generation continues with that smaller
count.

The five algorithms (confirmed: binary):

- **Random**: x = R(67), y = R(46). Because of the clamp, x = 1 and y = 1 are twice as
  likely as other values.
- **Diffuse**: like Random, but the minimum distance D is increased by 2.
- **Grid**: x = 5·R(13) + 2, y = 5·R(9) + 2, a random point of a 13 × 9 lattice with a
  spacing of 5.
- **Clusters**: with N the system count, pick a cluster width c and a gap g: c = 7, g = 8
  normally; c = 10, g = 5 when N > 80; c = 12, g = 3 when N > 150. With s = c + g, the
  quadrant is cut into cellsX = 67 div s columns and cellsY = 46 div s rows. Each cluster
  takes per = N div (cellsX · cellsY) + 1 consecutive systems: system i goes to cluster
  k = i div per, at column k mod cellsX and row k div cellsX, and
  x = R(c) + g div 2 + s · column + 4, y = R(c) + g div 2 + s · row + 1.
  The first cluster therefore gets one system fewer than the others, and the last
  clusters may stay empty.
- **Spiral**: systems lie on the outline of concentric squares centred near (34, 23).
  The size r = 6 + 3 · (i div 10) grows every ten systems. Draw a direction
  a = R(360) + 1 (degrees) and map it onto the outline with the square-outline function
  of §3.5 for size r: (ox, oy) = outline(a, r), then x = 34 − r + ox and y = 23 − r + oy.
  Large quadrants run off the grid edge and are clamped there.

### 3.4 System type, names and contents

1. **System names** are assigned when the system list is created (confirmed: binary).
   The non-empty lines of `SystemNames.txt` form the pool. For each system in order, a
   random start position R(n) is drawn and the first unused name from there (wrapping
   round) is taken, so names never repeat. Systems beyond the number of names get **no
   name** (an empty string). The engine instead falls back to generated names and warns
   **(OpenSE4 choice)**; the difference is listed in PARITY_GAPS.
2. **System type** (confirmed: binary): each system draws one entry from its quadrant's
   weighted list, in system order, after placement and the warp network. Draw
   r = R[1,1000]. Walk the entries in order, wrapping round to the first entry after the
   last, and keep the total T of the chances already passed; the first entry with
   T ≤ r < T + chance is chosen. When the chances sum to 1000, each entry is chosen with
   probability chance/1000. When they sum to less, the list effectively repeats (early
   entries are slightly favoured when the sum does not divide 1000). When they sum to
   more, entries beyond a running total of 1000 are never chosen. If all chances are 0,
   the system gets the first record of SystemTypes.txt. The name is looked up in
   SystemTypes.txt (the last record with that name wins).
3. The chosen SystemTypes record is instantiated (§4).
4. **Ruins.** With *No Ruins*, a rolled `Ancient Ruins` or `Ancient Ruins Unique` result
   is discarded (§5.2).

### 3.5 Warp network

Links come in **pairs**: a warp point in each of the two systems, each leading to the
other. The galaxy map draws a line only when both ends are known. Politics depends on
connectivity: contact between two empires ends when no path links their planets.

**Building the links** (confirmed: binary). Nothing is built with *No Warp Points*. Let K
be `Max Warp Points per Sys`, replaced by max(1, K div 2) when *All Warp Points connected*
is off. Every system may hold at most **10** warp points. Systems are processed in index
order. For system i:

1. **Candidates.** For d = 1, 2, 3, ... collect, in index order, every system j ≠ i not
   collected yet whose distance from i (§3.2) is exactly d. Stop after the round in which
   the list reaches at least K systems, or after d = 68.
2. **Linking.** If i has fewer than 10 links, take the candidates in list order and link
   i with j unless: i and j are already linked; the bearing from i to j differs by less
   than `Min Angle Between WP` from the bearing of any existing link of i; the bearing from
   j to i differs by less than the minimum from any existing link of j; or either system
   already has 10 links.

**Connectivity pass**, only with *All Warp Points connected* (confirmed: binary):

1. Pick a random system and mark every system reachable from it through the links.
2. Take the unmarked systems in index order. For each one, b, that has fewer than 10
   links: find the marked system c with fewer than 10 links at the smallest distance from
   b (ties go to the highest-numbered system), link b and c, ignoring the angle rule, then
   mark everything now reachable from b. Repeat passes until every system is marked.
   The search for c only looks up to distance 68, although two systems can be up to about
   80 squares apart.
3. Quirks. An unmarked system b that gets no link, because it already has 10 links or
   because no marked system with room lies within 68 squares, is still queued for marking:
   the next marking step marks it **and everything linked to it**, without a new link. If a
   whole pass ends with only such systems waiting and no marking step ran after them, the
   original never finishes (it loops forever). OpenSE4 stops instead.

**Placing the warp points** (confirmed: binary). After a system's objects are placed
(§4), it gets one warp point per link, in the order the links were made. The sector is
found from the bearing a from this system to the destination:

- **Square-outline function.** outline(a, r) maps a bearing onto the outline of a square
  of side 2r whose top-left corner is (0, 0). With t(u) = atan(u · π / 180) (the arctangent
  of the angle in radians, a quirk of the original, so corners are never reached):

  | Bearing a | x | y |
  |---|---|---|
  | 0 ≤ a < 45 | round(r + r·t(a)) | 0 |
  | 45 ≤ a < 90 | 2r − 1 | round(r − r·t(90 − a)) |
  | 90 ≤ a < 135 | 2r − 1 | round(r + r·t(a − 90)) |
  | 135 ≤ a < 180 | round(r + r·t(180 − a)) | 2r − 1 |
  | 180 ≤ a < 225 | round(r − r·t(a − 180)) | 2r − 1 |
  | 225 ≤ a < 270 | 0 | round(r + r·t(270 − a)) |
  | 270 ≤ a < 315 | 0 | round(r − r·t(a − 270)) |
  | 315 ≤ a ≤ 360 | round(r − r·t(360 − a)) | 0 |

  Warp points use r = 6.5, which gives the edge of the 13 × 13 system grid (2r − 1 = 12).
  A destination straight up the map gives (6, 0), the top edge's centre.
- **Edge placement** (the default): start from outline(a, 6.5). Then nudge the coordinate
  that runs along the edge by half a square and round it half to even: x for bearings from
  315 up to 45, y for 45 < a < 135, x for 135 ≤ a ≤ 215, y for 215 < a < 315. The nudge is
  +½ in the first two ranges and −½ in the last two. It is reversed when one of this
  system's earlier warp points stands on the outline sector (the sector before the nudge,
  compared with where the earlier warp points actually ended up) and a is smaller than the
  bearing toward that warp point's destination; with several such warp points, the one
  made last counts. Because of the half-to-even rounding, the nudge moves an odd
  coordinate one square to an even neighbour and leaves an even one where it is, so warp
  points can share a sector. The reversal therefore only matters in one case: a warp point
  with a bearing from 216 to 220 sits on the bottom edge at x = 3 but is nudged along y,
  so it keeps that odd x; a later warp point of the same system whose outline sector is
  that same square (a bearing from about 209 to 215) is then pushed right to x = 4 instead
  of left to x = 2.
- **With *Warp Points located anywhere in system***: start from outline(a, 6.5) and move
  R[0,4] squares straight inward (down for bearings up to 45 or from 315, left for
  45 < a ≤ 135, up for 135 < a ≤ 225, right for 225 < a < 315). Redraw until the sector
  holds no object; there is no limit on the redraws, so the original hangs if all five
  squares are taken (OpenSE4 stops after 1,000 draws).

**Warp point type and ability** (confirmed: binary). The two ends of a link share their
SectType record and their rolled ability. The end created first (in the lower-numbered
system, since systems are processed in order) rolls the ability from **its own system
type's** `WP Stellar Abil Type` (§5.2). The other end copies the first end's type and
ability. The type is the first `Warp Point` SectType record whose `Unusual` is FALSE; when
the roll grants an ability, a random `Warp Point` record is drawn instead, redrawn up to
100 times until one with `Unusual` TRUE comes up.

The `Warp Point One-Way` flag of SectType is read but never used: all warp points are
two-way (confirmed: binary). Only the Close Warp Point stellar manipulation could look at
`Warp Point Size`, and stock closers work on any size.

### 3.6 Empire placement

Home systems may only be system types with `Empires Can Start In = TRUE`. This rule was
added because players were starting in nebulae, black holes and asteroid systems.

**Map starting points** (§12) come first (confirmed: binary). In player order, every
player gets its specific point (if several are listed for it, the last one), else a
random remaining common point, which is then used up; players left without a point are
placed at random afterwards (below). The planet at a point is the first planet (not
asteroid field) in that sector. If its atmosphere is not the player's, it takes a random
Planet SectType record with the player's atmosphere and planet type and the same stellar
size, and keeps its name, values and conditions; a planet with the right atmosphere but
another physical type stays as it is. If no record fits, the original has no defined
result. If the sector holds no planet (it may be empty or hold an asteroid field), a
homeworld is created in that very sector, as described below. Nothing checks whether an
earlier player already took the same sector: two players could then share one
homeworld. OpenSE4 skips such a point instead (§14 Q36).

**Random placement** (confirmed: binary). Players are placed in player order. The
**home size** is Small, Medium or Large for Home Planet Value Bad, Average or Good, raised
to the smallest `Stellar Size` that any natural Planet SectType record with the player's
atmosphere and planet type has (so a combination that only exists in large sizes forces a
large homeworld). Up to three attempts build a candidate list; the first attempt with any
candidate wins, and the home is drawn uniformly from it:

- A candidate is an existing **planet** (not an asteroid field) whose atmosphere is the
  player's breathable atmosphere and whose physical type is the player's planet type. No
  planet is converted.
- Its system must be start-eligible, and not already another player's home system unless
  *Allowed to start in the same system* is on. It must not be another player's homeworld.
- In attempts 1 and 2, and in attempt 3 when *All player planets the same size* is on, its
  size must equal the home size.
- In attempts 1 and 2, with *Evenly distributed*: with S systems and P players, its system
  must be more than trunc(0.8 · (S div P)) warp jumps (attempt 1) or trunc(0.5 · (S div P))
  jumps (attempt 2) from the home system of every player placed before. Systems with no
  warp path count as very far.

If no attempt finds a candidate, the game draws up to 2,000 random systems looking for a
start-eligible one not used by another player (if none turns up, one more random system is
used whatever its type) and **creates** a homeworld there: a random sector without a
planet (any of the 169, redrawn until one has no planet; a star, storm, warp point or
asteroid field may share it), a random Planet SectType record with the player's
atmosphere and planet type (and the home size when *All player planets the same size* is
on), random values and conditions as for a natural planet, and a name made of the system
name and the Roman numeral one above the number of sectors that hold a planet (asteroid
fields do not count).

**Starting planets** (confirmed: binary; the full homeworld setup is in spec 02 §9). With
more than one starting planet (a neutral empire always gets one), further planets are
taken from the home system and systems up to one warp jump away (two jumps when the
quadrant has more than 60 % of `Maximum Number Of Systems`, compared exactly).

1. **Candidate systems, in order.** The list starts with the home system. Each round (one
   or two) goes through the list as it stands and appends, for each system in turn, the
   destinations of its warp points in the order of those warp points, skipping systems
   already listed. So the order is the home system, then its neighbours in the order of
   its warp points, then their neighbours.
2. **Filter.** Systems that are not start-eligible are dropped. Unless *Allowed to start
   in the same system* is on, so are systems where another empire has a colony and systems
   that are another player's home system, even when that player's homeworld has not been
   set up yet. The player's own home system always stays, whichever filter would drop it
   (confirmed: binary). Every system left in the list is marked explored for the player,
   whether or not it receives a planet.
3. **Existing planets.** For each candidate system in order, sectors are scanned in order
   0..168; in each sector only the first planet (not asteroid field) is looked at. It is
   taken if it has no colony, has the player's atmosphere and planet type (and the home
   size with *All player planets the same size*), and is nobody's homeworld, until enough
   planets are found.
4. **Created planets.** Each missing planet is created in a random candidate system, on a
   random sector of the inner 11 × 11 area that holds no object at all (redrawn until one
   is found, without limit), named with the Roman numeral one above the number of sectors
   that hold a planet.

Every starting planet, the homeworld included, is then set up the same way (spec 02 §9):
its system is explored, ruins are removed, the values are set, any old colony is removed,
and it becomes a colony of the player's race at maximum population that is a **capital**
of colony type "Homeworld". Neutral empires cannot use warp points at all (§8), so they
never leave their home system.

### 3.7 Pipeline

The original generates a quadrant in this order (confirmed: binary):

1. Roll the number of systems for the quadrant size (§2.2).
2. Draw the system names (§3.4).
3. Place the systems. Placement may end with fewer systems than rolled (§3.3).
4. Unless the game has no warp points, choose the links. If "all systems connected" is
   on, run the connectivity pass (§3.5).
5. For each system in order: roll its type (§3.4); place its objects and roll their
   abilities and values (§4, §5); then place its warp points, each pair sharing one type
   and one ability (§3.5).
6. Set each warp point's destination.
7. Place the players (§3.6).
8. Set up the players: technology, the all-seen option, starting planets and the
   stockpile (spec 02 §9).

---

## 4. Star systems

### 4.1 Sector grid

- Each system is a square grid of **13×13** sectors, x and y both 0..12, with the centre
  at (6,6) (confirmed: binary; the Deluxe manual's 196 sectors is wrong). X grows to the
  right and Y grows downward: (0,10) is near the bottom left. The original numbers the
  sectors y·13 + x, so the centre is sector 84.
- Any number of objects can share a sector. Stock types stack moons on a planet's sector.
- Clicking an empty sector opens the whole-system report: its type, description and
  system abilities.

### 4.2 SystemTypes.txt schema

| Field | Semantics |
|---|---|
| `Name`, `Description` | Identity and report text. |
| `System Physical Type` | `Normal`, `Nebulae` or `Black Hole`. Stellar manipulation checks this (§9). |
| `Background Bitmap`, `Mask Background Objs`, `Non-Tiled Center Pic` | Presentation only. They set the system backdrop, whether objects are masked over it, and whether the combat map uses one centred picture instead of tiles. Map them to our own art. |
| `Empires Can Start In` | Start eligibility (§3.6). |
| `Number of Abilities` + `Ability N Type/Descr/Val 1/Val 2` | **System-wide abilities, always present with no roll.** They apply to every sector. |
| `WP Stellar Abil Type` | StellarAbilityTypes name applied to every warp point in this system. |
| `Number of System Objs` | Non-warp-point objects. The value 0 is legal and common: "scenic" systems such as giants, comets or star-forming regions are just a backdrop plus warp points. |
| `Obj N Physical Type` | `Planet`, `Asteroids`, `Storm`, `Star`/`Sun`, `Destroyed Star` or `Comet`. Generation creates planets, asteroid fields, storms, stars and destroyed stars; a `Comet` (or `Warp Point`) template entry creates **nothing**. It is still placed like any other entry: its position is drawn (§4.3), its sector is marked occupied and kept for `Same As`, and a SectType record is drawn for it (§5.1), all with their usual random numbers (confirmed: binary). |
| `Obj N Position` | See §4.3. |
| `Obj N Stellar Abil Type` | StellarAbilityTypes name to roll for this object. |
| `Obj N Size` | `Any` or Tiny..Huge. For planets and asteroids it is compared with the PlanetSize record's `Stellar Size`. |
| `Obj N Atmosphere`, `Obj N Composition` | Planets and asteroids only. `Any` or a specific value. |
| `Obj N Age`, `Obj N Color`, `Obj N Luminosity` | Stars only. `Any` or a specific value. |

### 4.3 Position specifiers

Objects are placed in template order; "occupied" below means a sector already used by
an earlier object of the same system (warp points come later). The rules are confirmed:
binary.

- The specifier is recognised by the word it contains: `Ring`, then `Coord`, then `Same`,
  then `Circle Radius`. Anything else has no defined result in the original; the engine
  places the object on a random sector and warns **(OpenSE4 choice)**.
- `Ring 1`: the centre sector.
- `Ring k` for k = 2..7 (a single digit): a sector on the square ring k−1 squares from the
  centre (Chebyshev distance). With side s = 2k − 1 and o = 7 − k: a coin picks a
  horizontal or a vertical side, a second coin picks which one, and the position along it
  is o + R(s). Corners can be reached from two sides, so they are twice as likely. The draw
  is repeated while the sector is occupied, up to 101 draws, after which the last one is
  kept anyway.
- `Circle Radius R`: a uniformly random unoccupied sector whose Euclidean distance from
  the centre, **truncated**, equals R. If there is none, the object goes to sector (0,0).
- `Coord X,Y`: a fixed sector with no randomness and no occupancy check.
- `Same As N`: the same sector as object N. If N has not been placed yet, the result is
  sector (0,0). Used for moons.

### 4.4 System-wide ability types

These occur in system records. Val 1 has the meaning shown; Val 2 is unused in stock
data.

| Ability | Effect |
|---|---|
| `Sector - Sight Obscuration` | Every affected object in the system gets obscuration Val1 in **all five** sight types (§6). This is what makes a nebula. The system value is at least 1. |
| `Sector - Shield Disruption` | Val1 shield points lost in combat. Stock uses a huge value, so shields are useless. |
| `System - Movement Towards Center` | Each turn, every ship and unit group is pulled Val1 sectors toward the centre (black holes) (§7). |
| `System - Destructive Center` | Val1 normal damage each turn to every ship and unit group in the centre sector (black holes) (§7). |
| `System - Movement Random` | Each turn, every ship and unit group moves Val1 steps toward one random sector (spatial ruptures) (§7). |
| `Sector - Damage`, `Sector - Sensor Interference` | May also appear at system level, with the same meaning as at sector level, added to the value of the sector (§5.3). |

Several abilities of the same kind add up, except sight obscuration, where the largest
value counts (confirmed: binary).

The header also lists `System - Sensor Interference`, `System - Damage` and
`System - Ability Required`. They are not in the game's list of ability names, so they
have no effect (confirmed: binary). Stock data does not use them. The engine should load
them with a warning and ignore them.

Timing: the movement effects and the centre damage happen once per turn in the event
step at the end of the turn (§7) (confirmed: binary).

---

## 5. Stellar objects

### 5.1 SectType.txt: the catalogue of concrete objects

Each record describes one concrete kind of object with its picture. The file allows at
most 1000 records.

| Field | Applies to | Semantics |
|---|---|---|
| `Physical Type` | all | `Planet`, `Asteroids`, `Star`, `Storm`, `Warp Point`, `Destroyed Star`, `Comet` (or `None`). |
| `Picture Num` | all | Index into the icon strip and portrait set. Values may repeat. |
| `Description` | all | Report text. |
| `Planet Size` | Planet, Asteroids | `Tiny`..`Huge`, or a constructed-planet name (`Ringworld`, `Sphereworld`). This is a PlanetSize.txt name. |
| `Planet Physical Type` | Planet, Asteroids | `Rock`, `Ice` or `Gas Giant`. |
| `Planet Atmosphere` | Planet, Asteroids | `None`, `Methane`, `Oxygen`, `Hydrogen` or `Carbon Dioxide`. The combination Gas Giant with None is declared illegal, yet stock data contains two such records. Load them with a warning. |
| `Star Size`, `Star Age`, `Star Color`, `Star Luminosity` | Star, Destroyed Star | Size is Tiny..Huge. Age is Young, Average, Old or Ancient. Colour is Yellow, Red, Purple, Green, Blue, White or Orange. Luminosity is Dim, Average, Bright or Super Bright. |
| `Storm Size` | Storm | Tiny..Huge. |
| `Combat Tile` | Storm, Asteroids | Name of the tactical-map fill tile. Asteroid and storm sectors fill the combat map. |
| `Warp Point Size` | Warp Point | `Small` or `Large`. |
| `Warp Point One-Way` | Warp Point | Boolean. Read but never used by the game (confirmed: binary). |
| `Unusual` | Warp Point | Boolean. Natural warp points with a rolled ability use an Unusual record, the others a plain one (§3.5). |

**Instantiation** (confirmed: binary). The candidates for a SystemTypes object are the
SectType records of its physical type, excluding planet and asteroid records whose
PlanetSize is `Constructed`. When the object's size **and** atmosphere are both `Any`, the
record is drawn uniformly among all candidates and the other constraints (composition,
age, colour, luminosity) are **ignored** (a quirk; stock data never combines them). Otherwise
every constraint that is not `Any` must match, and the record is drawn uniformly among the
matches, so the frequency of an attribute follows how many records carry it. When nothing
matches the original has no defined result; the engine warns and relaxes the constraints
**(OpenSE4 choice)**.

**Star attributes.** Age, colour and luminosity are descriptive only: besides the
generation filter above, the game only turns them into text for the star's report; no rule
reads them (confirmed: binary). The number of stars matters, because the "Solar ..."
abilities (solar supply and solar resource generation) scale per star in the system, and
destroyed stars count as stars there (§5.4).

### 5.2 StellarAbilityTypes.txt and the ability roll

| Field | Semantics |
|---|---|
| `Name` | Referenced from SystemTypes (`Obj N Stellar Abil Type`, `WP Stellar Abil Type`). |
| `Number of Poss Abilities` | 0..100. |
| `Ability N Chance` | 0..1000, in tenths of a percent. The chances in one record should total at most 1000. |
| `Ability N Type/Descr/Val 1/Val 2` | The ability granted if this entry is chosen. |

**Roll** (confirmed: binary). Draw r = R[1,1000] and walk the entries, accumulating their
chances. The entry whose running total first reaches r is granted (so each entry wins
with probability chance/1000); if r lies past the total, nothing is granted. Each object
gets **at most one** rolled ability. Two stock examples show the consequence. The storm set splits the
full 1000 among four effects, so every natural storm has exactly one. The planet set
totals only a few percent, and all of it is ruins.

### 5.3 Object-level ability semantics

| Ability | Val 1 (Val 2) |
|---|---|
| `Sector - Damage` | Normal damage to ships **moving into** the sector, not to ships that stay there (§7) (confirmed: binary). Stock storms carry Val2 = 1, which the game ignores. |
| `Sector - Sight Obscuration` | Obscuration level in all sight types for affected objects in the sector: planets, asteroid fields, ships, unit groups and comets, but not stars, storms or warp points (§6) (confirmed: binary). A ship or planet carrying this ability obscures its sector too. |
| `Sector - Sensor Interference` | Modifier to combat to-hit rolls in the sector. Stock uses a negative value. It does not affect detection. |
| `Sector - Shield Disruption` | Shield points lost in combat in the sector. |
| `Star - Unstable` | No game logic reads it (confirmed: binary); it is descriptive only. The same holds for `Warp Point - Unstable`, `Warp Point - Periodic`, `Warp Point - Ability Required` and `Sector - Ability Required`. |
| `Warp Point - Turbulence` | Damage to ships that jump through this warp point: a 50 % chance per jump (§8) (confirmed: binary). |
| `Ancient Ruins` | On colonization, the colonizer receives Val1 random tech areas. |
| `Ancient Ruins Unique` | On colonization, the colonizer receives the unique tech area with id Val1. |

Stars, storms and warp points are never hidden by storm or nebula obscuration (confirmed:
binary).

### 5.4 Object kinds

- **Star.** Anchors the system and is the target for Destroy Star, Create Nebulae, Create
  Black Hole and constructed planets. Every generated star is named after its system plus
  the word for star, so all stars of a multi-star system share one name (confirmed:
  binary).
- **Destroyed Star.** A dead stellar core, created only by generation; Destroy Star does
  not leave one (§9). Once generated the game makes no difference between it and a star:
  same object kind and naming, so star manipulations can target it and it counts for the
  one-star limit of Create Star and the star that Create Planet needs. Ship solar supply
  and planet solar resource generation count it as a star too (confirmed: binary).
- **Planet.** Colonizable (§5.5 and §5.6).
- **Asteroids.** Cannot be colonized in stock (there is no asteroid colonize ability) but
  can be remotely mined. They are the required input for Create Planet. A destroyed planet
  becomes an asteroid field.
- **Storm.** A sector hazard with its rolled ability. It has no owner and never moves: the
  pull and drift of §7 only move ships, bases and unit groups, and nothing else relocates a
  storm (confirmed: binary). Every storm is simply called "Storm" (confirmed: binary). The
  Storm report shows the picture, name, size, description and ability list.
- **Warp Point** (§8). Its name is the word for warp point, followed by the destination
  system's name when the viewer has explored that system (confirmed: binary).
- **Comet.** A legal physical type that generation never creates (confirmed: binary).
  Treat it as inert scenery. (Quirk: the original would name a comet with the word for
  storm.)

### 5.5 PlanetSize.txt: capacity by size

Records are keyed by (`Physical Type`, `Name`). `Physical Type` is `Planet` or
`Asteroids`. `Name` is Tiny..Huge, or a constructed name.

| Field | Semantics |
|---|---|
| `Stellar Size` | The Tiny..Huge category used by generation filters, homeworld sizes and Create Planet. A constructed world is size Huge. Destroy Planet instead compares the record's position in the file (§9). |
| `Max Facilities`, `Max Population`, `Max Cargo Spaces` | Capacity of a normal colony. |
| `Max Facilities Domed`, `Max Population Domed`, `Max Cargo Spaces Domed` | Capacity when any resident race cannot breathe the atmosphere. |
| `Constructed` | TRUE for manufactured worlds, which natural generation never creates. |
| `Special Ability ID` | Nonzero for constructed worlds. `Create Constructed Planet` Val1 refers to it. |

Capacity roughly doubles with each size step. For example, a Medium planet holds
3 facilities when domed against 15 normally, and constructed worlds are an order of
magnitude larger. Asteroid rows also define capacities, which only matter for mods.

### 5.6 Other planet properties

- **Name** (confirmed: binary). Planets are numbered in template order: a planet alone in
  its sector gets the system name plus the next Roman numeral ("Xyz IV"). A planet placed
  in a sector that an earlier object of the template already uses (a moon) does not take a
  numeral: it gets the first object's name plus a letter, A for the second object in that
  sector, B for the third. That first object may be a star, which gives names such as
  "Xyz Star A". (Quirk: a `Comet` or `Warp Point` template entry creates nothing but still
  claims its sector with an empty name, so a planet placed on it would be named with a
  space and a letter only.) Precisely, the letter counts every template entry recorded in
  that sector, the planet included; entries not yet placed count as sector (0,0), so a
  planet placed at (0,0) while later entries remain takes a letter too, after the first
  entry recorded there, which may be itself with its name still empty (confirmed: binary).
  Asteroid fields are always named "*system* Asteroid Belt" plus their own Roman numeral,
  even when there is only one; they count separately from the planets, so "Xyz II" can sit
  next to "Xyz Asteroid Belt I" (confirmed: binary).
- **Names of made planets** (confirmed: binary). Create Planet and Construct (§9) name
  the new planet with the system name and the Roman numeral one above the highest numeral
  that ends a planet's name in that system. Only real planets count (asteroid fields do
  not), only the first planet in each sector is looked at, and only the numerals I to XXX
  are recognised. The homeworlds and starting planets that setup creates use their own
  rule (§3.6). The owner may rename a colonized planet.
- **Physical type.** Rock, Ice or Gas (giant). Colonizing a type requires the matching
  `Colonize Planet - X` ability. Each empire starts able to colonize its home type.
- **Atmosphere.** A race breathes exactly one of the four gases. A planet with any other
  atmosphere, or with none, can only hold a **domed** colony, which uses the Domed limits.
  A colony counts as domed if any resident race cannot breathe the air.
  `Planet - Change Atmosphere` switches the atmosphere after Val1 turns to what most of the
  population breathes.
- **Conditions** (confirmed: binary). A real number shown as a band from Optimal (best)
  down to Deadly; the bands and their effect are in spec 02 §2. A natural planet rolls
  R[0,10]/10 + 0.5, one of 0.5, 0.6, ... 1.5 with equal chance (Unpleasant 5 in 11, Mild
  3 in 11, Good 2 in 11, Optimal 1 in 11). An asteroid field rolls half of that, 0.25 to
  0.75. Conditions change through events and abilities.
- **Value.** Three numbers, one each for minerals, organics and radioactives.
  - *Normal mode*: a percentage multiplier on production at that planet. Roll it per
    resource, uniformly in `Planet Value Low..High Percent`, both ends included
    (asteroids use their own keys) (confirmed: binary), and clamp later changes to
    `Min..Max Planet Percent Value`.
  - *Finite mode*: the remaining resource stock. Roll it in `... Low..High Resources`
    (confirmed: binary).
    Production subtracts from the stock, and output stops at 0. Solar generators neither
    use nor depend on the stock (version history).
- **Ruins** come from the roll in §5.2.
- **Spaceport requirement and blockade** belong to the economy spec. A planet with enemy
  ships in orbit ships no resources to its empire.

---

## 6. Sight and detection

### 6.1 Knowledge levels (per empire, per system)

1. **Unexplored.** The position is known. The name, contents and warp destinations are
   not. It is drawn dark grey and is never named in any UI, including map notes,
   order views and colonize previews. The version history fixed several leaks of this
   kind.
2. **Explored, no presence.** The system becomes explored the first time any of the
   empire's ships enters it. Stellar bodies (stars, planets, asteroids, storms, warp
   points) are remembered from then on. Enemy vehicles are invisible.
3. **Present.** The empire has a **sensor source** in the system: a ship or base, a
   fighter, satellite or drone group, or an owned planet (populated or not). An owned
   planet's sensor levels come from its colony's facilities only, not from the planet's
   own rolled abilities, whether or not anyone lives there (§6.9). Mine fields are not
   sensor sources (confirmed: binary). The empire then sees the live system through the
   sight rules below.

*All systems seen* marks every system explored for every empire at the start (level 2)
(confirmed: binary). A **Partnership** treaty shares sight: in each system, an empire's
sensor levels are raised to those of every empire it holds a Partnership with, and this is
repeated five times, so chains of partnerships (A–B plus B–C) pass sight along (confirmed:
binary). The check is one-way: A gets B's sensors when A's treaty with B is a Partnership.
Partners also exchange their maps (spec 05).

### 6.2 Sight types

There are five types: `EM Active`, `EM Passive`, `Psychic`, `Gravitic`, `Temporal`.

The rules of this section are confirmed: binary.

- **Sensor level per type.** Every sensor source has a baseline of EM Active 1 and 0 in the
  other types. `Sensor Level` abilities (Val1 = type name, Val2 = level) raise it; the
  highest value per type counts.
- **Obscuration per type.** Every object's baseline is 1 in all types, so a sensor level
  of 1 in any type sees it. Obscuration can be raised by:
  - `Cloak Level` abilities (Val1 = type, Val2 = level), highest value per type. A ship's
    cloak levels count **only while the ship is cloaked**. Unit groups (mines, satellites,
    fighters, drones) always use theirs. A planet uses its colony's cloak levels only while
    the colony is cloaked (§6.9).
  - The **environment**: the largest `Sector - Sight Obscuration` among the storms, ships
    and planets in the object's sector, and the system-wide value (at least 1). It raises
    all five types alike. It applies to planets, asteroid fields, ships, unit groups and
    comets, but never to stars, storms or warp points. What counts in the sector: a
    storm's rolled ability, a planet's or asteroid field's own rolled abilities (not its
    colony's facilities), and a ship's or base's abilities. Unit groups, stars, warp points
    and comets never obscure a sector, whatever abilities they carry.
- Several sources are combined by taking the **maximum**, never the sum.

### 6.3 Detection rule

Sight is resolved **per system**, not per sector (confirmed: binary). The empire's sensor
vector in system S is the per-type maximum over its sensor sources in S, raised by its
partners' (§6.1). An object the empire owns is always visible to it. Any other object is
visible when the empire has explored the system and there is **any** type t with
sensor[t] ≥ obscuration[t]. Objects of partners get no exception.

```
visible(E, obj) = owner(obj) == E ||
                  (explored(E, sys) && exists t: sensor(E, sys)[t] >= obsc(obj)[t])
obsc(obj)[t] = max(cloakedLevel(obj)[t],                       # 1 when not cloaked
                   affected(obj) ? max(sectorObsc(obj.sector), systemObsc(sys)) : 0)
```

With no sensor source in the system, every sensor level is 0 and nothing is visible, so
the explored test only matters together with presence. Storms are an exception: a storm is
visible to an empire that has explored the system and has any sensor level above 0 there.

Worked example: a stock level-1 cloak sets 2 in all types. Base sensors (EM Active 1)
miss the ship, and a level-2 sensor in any one type reveals it. A cloak that raises only
EM Active and EM Passive to 3 is still seen by Psychic, Gravitic or Temporal level 1.

Stock mine hulls carry cloak level 5 in every type, which is above the best stock sensor
(level 4). Mines are therefore undetectable and are found by sweeping.

Observers inside the same storm are treated like any other observer: the storm raises
their own obscuration, not their sensors, so they see each other when their sensors
reach the storm's level.

Sight should be recomputed whenever the inputs change: facilities built or lost,
components destroyed, cloak toggled, movement, or a nebula created or destroyed.

### 6.4 Consequences of not being seen

- A hidden object cannot be attacked, and enemies pass through its sector without combat.
  A cloaked ship can likewise pass through enemy sectors. If any enemy there sees it,
  combat starts automatically.
- To attack, a ship must decloak. Combat decloaks every participant until it ends.
- Uncloaked, visible ships inside an obscuring storm still fight visible enemies passing
  through.
- Cloaked ships cannot build; cloaking clears their construction queue and disables space
  yards. They may launch and recover units, and they do not upset populations.
- A cloaked colony keeps building everything but ships and bases, because only its space
  yard stops working (§6.9).
- Planets hidden by a storm or nebula are not drawn for observers who cannot see them.

### 6.5 Omnipresent view

The manual says omnipresent view shows everything, cloaked objects included. The version
history later *fixed* omnipresent view revealing cloaked ships. The executable does this
(confirmed: binary): with omnipresent view, the explored test is dropped and every
empire's EM Active sensor level is at least 1 in every system. Uncloaked objects outside
obscuring storms and nebulae are therefore visible everywhere, while cloaked or obscured
objects still need real sensors.

### 6.6 Scanning (detail, not detection)

- `Long Range Scanner`: Val1 is the range in sectors at which a visible enemy ship can be
  scanned in detail. Scanning reveals its design (added to "seen designs") and its
  contents.
- `Scanner Jammer` on the target blocks long-range scans.
- `Long Range Scanner - System` (a facility) scans any ship in its system.

### 6.7 Galaxy-map presence display

The renderer derives each system's marker from the viewer's knowledge:

- Unexplored: dark grey.
- Explored, no presence: light grey or white.
- Only you present: your colour.
- A visible foreign empire present: that empire's colour.
- Several empires present: a triangle.
- Selected system: a double ring with a filled centre.

A warp point that has been seen but never traversed is drawn as a short stub. After
traversal it becomes a full line, and the system view labels the warp point with its
destination. Show Distances gives light years between systems, and Show Names labels
explored systems only.

### 6.8 Per-empire map annotations

- **Borders.** A public set of claimed systems. Overlapping claims are allowed and drawn
  highlighted. Every empire sees every claim. Filters: all, allies, enemies, us.
- **Systems to avoid.** A private set that long-range pathfinding tries to route around.
- **Notes.** Free text for each system.

### 6.9 Colony (planetary) cloaking

This subsection gathers every rule about cloaked colonies. All of it is (confirmed: binary)
unless marked otherwise.

**Cloak levels.**
- For each sight type t, a colony's cloak level is max(1, the largest `Cloak Level` V2 among
  its facilities' abilities whose V1 is t).
- Only facilities count. The planet's own abilities (its rolled abilities, its size, its
  ruins) are not read. Population, facility damage and racial traits play no part.
- A colony's sensor levels (§6.1) are worked out the same way: the largest `Sensor Level` per
  type among its facilities, with EM Active at least 1.
- Both are stored with the colony and recalculated only:
  - when a facility is completed there;
  - when Scrap Facilities is used;
  - when the planet takes damage that destroys facilities (in combat, or from a storm);
  - when the colony is founded;
  - when the game is loaded.
- Facilities destroyed by the intelligence project `Planet - Facility Damage` do not cause a
  recalculation. A colony that loses its cloaking or sensor facility that way keeps its old
  levels until the next recalculation.

**Can cloak, and cost.** A colony can cloak when its cloak level is 2 or more in some sight
type. There is no supply test and no cost: a cloaked colony pays nothing, at once or per
turn. A ship differs: it needs supply and pays upkeep (spec 03 §8).

**The Cloak and Decloak orders.**
- Both are for an own colony. Cloak (Z) is lit when the colony can cloak and is not cloaked.
  Decloak (X) is lit whenever the colony is cloaked, even when it can no longer cloak.
- Both act at once and are never queued, in both turn styles. They play the `cloakon` and
  `cloakoff` sounds and write no log entry.
- Cloak marks the colony cloaked and recalculates the system's sight. It does not touch the
  construction queue (cloaking a ship clears its queue).
- Decloak clears the mark, recalculates sight and runs the first-contact check of spec 05
  §3.1 at once. Cloaking runs no contact check.
- Open: needs observation. How the host of a simultaneous game receives a colony's Cloak or
  Decloak given during the turn. The player's copy changes at once; presumably the turn file
  carries it like the rest of the colony (inferred). OpenSE4's choice is below (§14 Q44).

**When a colony decloaks or cloaks by itself.**
- **Recalculation.** At each recalculation above, a cloaked colony that can no longer cloak
  is decloaked, without a message. Nothing else checks it each turn, unlike ships (spec 03
  §8).
- **Battle.** Every colony in the sector is decloaked when a battle begins (spec 04 §2).
  After the battle, a colony that was cloaked when it began cloaks again if the planet still
  has a colony. Its cloaking facilities do not matter here: even one that lost them in the
  battle cloaks again. So does a colony captured in the battle, because the cloaked mark
  survives every change of owner (ground combat, surrender, gift, intelligence). Such a
  colony is marked cloaked while hiding nothing (its cloak level may be 1). Its yard does not
  work, Decloak stays lit and the cloak icon shows, until the player decloaks it or the next
  recalculation does.
- **Computer players and the Ship Cloaking minister.** The colony is decloaked before it
  carries out any of these colony orders, and cloaked again afterwards if it can, whether or
  not it was cloaked before: Launch Units Remotely, Recover Units Remotely, Use Facility and
  Convert Resources. Before Abandon Planet it is only decloaked. This applies to every
  computer player's colony, and to a human player's colony under minister control while the
  empire's Ship Cloaking minister is on (spec 05 §7.1). Computer players give their colonies
  Launch Units Remotely orders for satellites, so their colonies with a cloaking facility end
  up cloaked. Nothing else ever cloaks a computer player's colony.
- **Abandon Planet** removes the colony, and its cloak with it.

**Effects of a cloaked colony.**
- **Detection.** While the colony is cloaked, the planet's obscuration in each sight type is
  the colony's cloak level, raised by the sector's and the system's obscuration (§6.2, §6.3).
  The Omnipresent view does not reveal it (§6.5), and partners get no exception. Everything
  that depends on seeing a colony follows from this:
  - first contact (spec 05 §3.1);
  - Sentry (spec 03 §8);
  - the turn-based battle check (spec 04 §2);
  - the "visible hostile" test that blocks stellar manipulation (§9);
  - the computer players' Close Warp Point choice (spec 05 §7.5);
  - the Planets window and the map (below).
- **Targets.** A ship ordered to colonize a planet whose colony it cannot see fails with a
  "no planet here to colonize" notice (spec 03 §8). Destroy Planet also needs a visible
  target (§9).
- **Unaffected.**
  - The colony stays a full sensor source for its owner (§6.1).
  - It stays a supply depot for its owner and for empires with a Military Alliance or
    Partnership treaty (spec 03 §7).
  - It still counts as a repair source and for training.
  - Its production, population, spaceport connection, happiness and score do not change.
- **Space yard.** The colony's yard does not work while it is cloaked.
  - Ship and base items are removed from its queue every turn (spec 02 §6.1).
  - Vehicles in its sector cannot be scrapped, analyzed, mothballed, unmothballed or
    retrofitted through it (spec 03 §15).
  - Repairs there cannot fix `Emergency Energy` or `Emergency Resupply` parts (spec 03 §13).
  - The computer players' searches for a yard (repair, yard ships) pass it over (spec 05
    §7.5).
  - Its status cell shows no yard (spec 06 §4.4).
- **Construction.**
  - The colony still builds facilities, units and upgrades, at its normal rate (the yard's
    rate counts), and its queue is kept.
  - The one-space-yard limit still refuses a second yard.
  - Its emergency or slow-mode counter (spec 02 §6.4) moves twice per turn: once in normal
    processing, and once more in a pass that handles only cloaked objects' queues.
- **Combat.**
  - The colony fights normally, because a battle decloaks it for its whole length (above).
  - In the turn-based check for a wholly cloaked moving group, it is not its owner's
    "uncloaked object" (spec 04 §2).
  - The simultaneous battle check, which reads cloak marks only for vehicles and unit groups,
    is unaffected.
- **Intelligence.** The player's picker for a project's target planet lists only planets the
  player sees now. A project aimed at "Any" target can hit a cloaked colony (spec 05 §2).
- **Computer players.**
  - Their scan of planets has no sight or cloak test, so they know about cloaked colonies:
    as attack candidates, in their colonization lists, and as enemies in their territory.
  - Their Destroy Planet minister skips every planet whose colony is marked cloaked, even
    one it can see (spec 05 §7.5).
- **Lists and map.** See **What other players see** below. The planet of a hidden colony is
  not drawn on the map at all and is in no tab of the Planets window. (An earlier version of
  this section said the planet was always drawn; that was wrong.)
- **Status icons** (own colonies only, spec 06 §4.4). The cloaked cell (9) is drawn first.
  The space-yard cell (0) needs a working yard. A cloaked colony with a yard therefore shows
  the can-repair cell (11) instead, when it has `Component Repair`. The building cell (12)
  is unaffected.

**What other players see** (confirmed: binary).

The original keeps no memory of planets or colonies: there is no "last seen" owner or
population. Every window applies the sight rules when it is drawn, to the live state. Two tests
are used.

- **Seeing the planet.** The planet's owner always passes. Anyone else passes when, in some
  sight type, the planet's obscuration (§6.2: the colony's cloak levels while it is cloaked,
  raised by storms and nebulae) is no more than the viewer's sensor level in that system, with
  EM Active counted as at least 1. There is no explored test and no presence is needed: this
  is the test of the Omnipresent view (§6.5), used here whatever the game's option. A planet
  outside storms and nebulae without a cloaked colony therefore always passes. The same test
  decides which stellar objects (planets, asteroid fields, stars, storms, warp points, comets)
  a sector shows; vehicles use the detection rule.
- **Seeing the colony.** The detection rule of §6.3 applied to the planet: the system is
  explored, and the viewer's real sensor levels there (its own sensor sources and its
  partners', §6.1) reach the obscuration in some type, with no floor. Without a sensor source
  in the system no foreign colony is seen, cloaked or not.

A colony is **hidden** from a viewer when its planet fails the first test. That needs a cloak
level of 2 or more in EM Active above the viewer's EM Active sensor level, and in every other
type above the viewer's sensor in that type. Stock data has no facility with `Cloak Level`, so
hidden colonies occur only with modified data.

For a viewer from whom the colony is **hidden**:
- **System panel.** The planet is not drawn at all: no sprite, no population bars, no colonize
  star, no planet name, and it is not counted in the sector's number of stellar objects. The
  sector shows whatever else the viewer sees there, by the usual rules (spec 06 §2.4).
- **Clicking the sector.** Only visible objects are listed. With none, the system report opens
  (the system's picture, name, description and abilities; it lists no planets). The planet
  cannot be selected, so its report cannot be opened and none of its orders light.
- **Planets window.** The planet is in no tab, so Send Colony Ship cannot pick it. The
  statistics block still counts it (spec 06 §1.8.1): its counts use every planet of the
  explored systems with its real owner, so they can exceed what the tabs list.
- **Galaxy map.** A system's presence markers (§6.7) come from the objects the viewer sees by
  the detection rule, so a hidden colony adds no colour.
- **Intelligence.** The player's target picker leaves it out. A project aimed at "Any" can
  still hit it (above).
- **Colonize.** A ship ordered to colonize the planet fails with the "no planet here to
  colonize" notice (spec 03 §8). That check comes first, so the colony-type picker never
  appears.
- **Scores and Comparisons** use real totals: the colony counts in its owner's systems, planets
  and population. Who may see which empire's scores is spec 05 §5.
- **Log.** Entries are never filtered by sight. An entry written when something happens to the
  colony (an intelligence project aimed at "Any", a battle, which decloaks it) names its planet
  as usual.

For a viewer who **sees the planet but not the colony**: any foreign colony in a system where
the viewer has no sensor source, and a cloaked colony whose EM Active cloak level is 1 while
the viewer has no EM Active sensor there.
- **Map.** The planet is drawn without its population bars. It gets the colonize star (green or
  red) when the viewer could colonize its type, as if it were empty.
- **Planet report.** As for an uncolonized planet: picture, name, type, atmosphere, conditions,
  value, description and the planet's own abilities. No owner flag, no siege or blockade note,
  no Population line.
- **Planets window.** Listed as an uncolonized planet: in All; in Colonizable, Coloniz\Empty and
  Coloniz\Breathe when the type (and atmosphere) fits; never in All Colonies, Enemy Colonies or
  Ally Colonies. Send Colony Ship can pick it; the colony ship then finds the colony on arrival
  (its own sensors now see it) and fails with the "already a colony on this planet" notice.
- **Galaxy map and intelligence.** It adds no presence colour, and the intelligence picker does
  not offer it.

For a viewer who **sees the colony**, everything is as usual: the population bars on the map; in
the planet report the owner's flag (and the invader's flag under it during a siege), the siege
and blockade notes and a Population line. A foreign colony's report shows nothing more: no
owner name and no colony type (those are shown for own colonies only).

**What a network or e-mail game sends** (confirmed: binary). There is one game file for all
players, holding every empire's record, every design and every object, hidden ones included;
nothing is filtered per player, and the hiding above happens only in the windows (spec 05
§9.2). OpenSE4's own network redaction only needs to give the same visible result (spec 05
§9.5).

**The engine follows this subsection since 2026-10-01.** The colony keeps its cloaked mark and
its cloak and sensor levels (`Colony::cloaked`, `cloakLevels`, `sensorLevels`), recalculated
only at the moments above (`sight::recalculateColony`); the immediate command
`cmd::CloakColony` cloaks and decloaks; sight, contact, the battle checks, yards, construction,
the orders that need sight, the computer players and the client follow the effects above. Its
own choices, marked (inferred) in the code (§14 Q44):
- A simultaneous game's host carries out a colony Cloak or Decloak given during the turn when
  it applies that player's orders at the start of turn processing, in player and command
  order, like every other command; a Decloak's contact check runs then.
- The "game is loaded" recalculation happens when a saved game is loaded (Load Game, a
  network host started from a save). A play-by-e-mail game file, which the players and the
  host pass between them, is not recalculated when it is opened, so that both sides keep the
  same state.
- An upgrade that converts facilities counts as completing them: it recalculates too.

**The client and engine differ from "What other players see"** (2026-10-01):
- **System panel** (`MainWindow::objectsAt` and `prepareSectors` in
  `src/client/classic/main_window.cpp`). Every stellar object of an explored system is drawn,
  counted, named and clickable with no sight test, so a hidden colony's planet (and a planet
  hidden by a storm or nebula, §6.4) is drawn and selectable. Drawing, the stellar count, the
  planet names, the sector click and the sector list must use the "seeing the planet" test.
  `sight::canSeePlanet` (`src/game/sight.cpp`) is that test except in one narrow case: a
  cloaked colony with EM Active cloak level 1 and higher levels in other types, watched by a
  viewer with no sensors there; ours hides that planet, the original shows it.
- **Seen colony.** `sight::colonyShown` (`src/game/sight.cpp`) is true for every uncloaked
  colony, with no sensor source needed. The original uses the detection rule (our
  `canSeeColony`) wherever it asks whether a colony is seen: the map's population bars
  (`main_window.cpp`), the Planets window's colony status and tabs (`surveyPlanets` in
  `src/client/classic/screens/colony_logic.cpp`) and the intelligence target picker
  (`knownPlanets` in `src/client/classic/screens/intelligence.cpp`). Ours also draws the
  colonize star from the real colony (`colonizeProblem`), so it never marks an unseen colony's
  planet as colonizable; the original does.
- **Planet report** (`planetReport` in `src/client/classic/reports.cpp`). For any foreign
  colony ours shows the owner's flag and Owner, Colony Type and Population lines. The original
  shows the flag and Population only when the colony is seen, and never Owner or Colony Type
  for a foreign colony.
- **Planets statistics** (`planetStatistics` in `colony_logic.cpp`) count only the listed
  planets; the original counts every non-asteroid planet of the explored systems with its real
  owner.
- **Galaxy-map presence** (`systemPresence` in `src/client/classic/quadrant_map.cpp`) marks every
  colony of an explored system; the original marks only objects the viewer sees by the
  detection rule.
- **Network.** `src/game/redact.cpp` keeps the foreign colonies of explored systems (owner,
  population, cloak mark and levels) and drops the vehicles the viewer cannot see; the e-mail
  game file (`src/net/pbem.cpp`) is the full state, as in the original. With the points above
  fixed in the client there is no visible difference; leaving hidden colonies out of the
  redacted view would be optional hardening, not parity.

---

## 7. Hazards per turn (storms, nebulae, black holes, ruptures)

The rules of this section are confirmed: binary.

**Once per turn**, at the start of the event step at the end of the turn (after movement,
combat, the empires' end-of-turn processing and the victory check, spec 05 §8), each
system is processed in system order:

1. **Pull.** With P = the sum of the system's `System - Movement Towards Center` values,
   every ship (bases included) and every unit group (fighters, mines, satellites, drones)
   in the system takes P steps toward the centre. A step changes x and y by at most one
   each, toward the target (a king's move), and stops at the target.
2. **Random drift.** With D = the sum of `System - Movement Random`, every ship and unit
   group takes D steps toward one random target sector. The target is drawn once per turn,
   for all systems, as sector number R[0,144] (y·13 + x, so rows 0 to 10 and the first two
   squares of row 11). Drift therefore never leaves the grid.
3. **Centre damage.** With C = the sum of `System - Destructive Center`, every ship and unit
   group in the centre sector takes C normal damage. Each empire is told which of its
   ships were damaged or destroyed by natural events.

**Sector damage is not a per-turn effect.** `Sector - Damage` (a storm's value plus any
system-wide value in that system) hits ships **as they move**: each time a moving ship or
fleet steps into a sector with a total above 0, there is a 50 % chance that every ship in
the group takes that much normal damage, and if it does, the group stops moving for the
turn. Ships that stay in a storm are not harmed. The movement pathfinding of empire ships
also steps around damaging sectors when it can (movement spec).

Displacement does not start combat by itself: the hazard step runs no battle check
(confirmed: binary). A ship pulled or drifted into a sector with enemies fights only when a
later battle check runs there, that is when a group moves into that sector or carries out
an order there (spec 04 §2).

Shield disruption and sensor interference apply only inside combat in the affected sector
or system: the system-wide value plus the values of the objects in that sector.

---

## 8. Warp points

- Warp points are the normal way to travel between systems. A jump is instantaneous and
  lands on the paired warp point in the destination system.
- A ship must stand in the warp point's sector, and the jump itself costs 1 movement point
  and one step's supply (confirmed: binary; spec 03 §6.2 and the Warp order in §8).
  Reaching the sector is an ordinary step; no extra stop is needed.
- A Move To order across systems routes through known links. The explicit Warp order is
  needed only when the link leads to an unexplored system.
- **Turbulence** (confirmed: binary). On each jump there is a 50 % chance that every ship
  of the jumping group takes damage equal to the sum of the `Warp Point - Turbulence`
  values of the warp point it leaves. The group still arrives, but its movement ends for
  the turn. Both ends of a natural link carry the same ability (§3.5), so the direction
  does not matter for generated links.
- **Neutral empires** cannot jump through warp points at all (confirmed: binary).
- Knowledge: an empire that has seen a warp point knows it exists. Arriving in a system
  explores it (confirmed: binary). The warp point's name shows its destination once the
  viewer has explored the destination system. Partner sharing and omnipresence also
  reveal destinations.
- The data allows `One-Way` warp points and `Size` (Small/Large). The game never reads
  the one-way flag, so every link can be used in both directions (confirmed: binary).
  Only `Close` could check size, and stock closers work on any size.
- Opening or closing a warp point creates or removes **both** ends (§9). A system can hold
  at most 10 warp points.

---

## 9. Stellar manipulation

Stellar manipulation is given to a ship (or base) through the Stellar Manipulation order and
its window. Planets cannot manipulate, and Use Component and Use Facility never reach it: those
two orders use only `Emergency Resupply` and `Emergency Energy`, and Use Facility has no effect
at all (spec 03 §8) (confirmed: binary). Common rules:

- The ship must be **at the target location** when the order executes. The history fixed
  execution after the ship had moved away.
- Every action except Construct needs movement remaining; the movement is not spent
  (confirmed: binary).
- Every action needs a working component with the ability, enough supply for it (which is
  then spent), and the ship **not cloaked** (confirmed: binary).
- Every action except Destroy Planet also needs **no visible hostile object** in the target
  sector (confirmed: binary). A hostile object is a ship or base that is not mothballed, or
  a colonized planet or asteroid field, that the acting empire can see and whose owner
  has no treaty of Non-Aggression or better with it (war, non-intercourse, no treaty or no
  contact). Unit groups (fighters, mines, satellites, drones) never block. Destroy Planet
  has no such check at all: its "enemies present" refusal can never happen.
- Components with `Component Destroyed On Use` are consumed. Those whose effect destroys
  the ship itself carry no such flag.
- A button is enabled only when the ship has the matching ability and the precondition
  holds.
- Blocking abilities (`Stop ...`) count on **any owned object** in the target system: a
  ship's own abilities or a planet's colony abilities (facilities), whoever the owner, the
  acting empire included (confirmed: binary). For warp points, a blocker in either system
  prevents it. `Stop Planet Destroyer` only counts in the planet's own sector.
- Each outcome raises the matching log event: Planet/Star Created or Destroyed, Warp Point
  Opened or Closed.

The results below are confirmed: binary. "Shockwave" in the table means: every planet and
asteroid field in the system is replaced by a new asteroid field (a random natural
Asteroids record of any size) that keeps its name, values and conditions, and every
colony on them is lost; every other object except warp points (stars, storms, ships and
bases, unit groups, comets) is destroyed, the acting ship included.

"Replaced" always means that a new object is made and the old one removed, never a change in
place. The new object is added first, while the old one still holds its slot, so it takes the
lowest empty slot (or a new one at the end); then the old object is removed and its slot left
empty. The shockwave is one pass over the system's objects in slot order, so a replacement made
late in the pass can reuse a slot emptied earlier in it. Orders aimed at the old object find it
gone (spec 03 §19 Q72) (confirmed: binary).

| Action (ability) | Precondition | Result |
|---|---|---|
| Create Planet (`Create Planet Size` = max size) | A visible asteroid field **without a colony** in the sector, and at least one star (destroyed stars count) in the system. A colonized asteroid field is not a valid target. | The asteroid field is replaced by a planet of stellar size exactly min(Val1, the field's size): a random Planet record of that size (so its atmosphere and type are random). It keeps the field's values and is named with the next numeral (§5.6); conditions are rolled as for a natural planet. If no Planet record of that size exists, no planet is made, but the field is still removed, the cost is paid and the result reported (confirmed: binary). |
| Destroy Planet (`Destroy Planet Size` = max size) | A visible planet in the sector whose PlanetSize record number (its position in PlanetSize.txt; stock Tiny..Huge are 1..5, the constructed worlds come after) is at most Val1. Blocked by `Stop Planet Destroyer` on any owned object in that sector. | The planet is replaced by a random natural asteroid field of the same stellar size that keeps its name, values and conditions. The colony is lost. |
| Create Star (`Create Star`) | The system is neither Nebulae nor Black Hole, has no star (destroyed stars count) and no constructed planet. The centre sector is **not** required. | A random natural star record is placed in the ship's sector and named after the system. |
| Destroy Star (`Destroy Star`) | A visible star in the sector. Blocked by `Stop Star Destroyer` in the system. | Shockwave (above). The system type and its abilities do not change, and no destroyed star remains. |
| Open Warp Point (`Open Warp Point Distance`) | The target is another system at distance (§3.2) at most Val1, with no link to it yet, and both systems have fewer than 10 warp points. Blocked by `Stop Open Warp Point` in either system. | A new warp point is added at this sector, and its pair on the target system's edge facing the origin (edge placement of §3.5). Both ends use the first plain (not `Unusual`) warp point record and carry no ability. The target is picked on the galaxy map. |
| Close Warp Point (`Close Warp Point`) | A warp point in the sector. Blocked by `Stop Close Warp Point` in either system. | Both ends are removed. Closing an already-closed link must be a harmless no-op (a simultaneous-turn race). |
| Create Storm (`Create Storm`) | The common checks only; the sector need not be empty. | A storm of a random natural storm record, named "Storm", with **one** ability: draw one of obscuration, damage or shield disruption uniformly, redrawing any whose `Created Storm Maximum ...` setting is 0 or less (none at all if all three are). The ability is `Sector - Sight Obscuration`, `Sector - Damage` or `Sector - Shield Disruption` with Val1 **equal to** the matching setting (Obscuration Level, Turbulence Damage or Shield Disruption), not a random value. |
| Destroy Storm (`Destroy Storm`) | A storm in the sector | The storm is removed. |
| Create Nebulae (`Create Nebulae`) | A visible star in the sector. Blocked by `Stop Nebulae Creator` in the system. | Shockwave, then the system becomes physical type Nebulae with a random nebula backdrop, is no longer start-eligible, and its system abilities are replaced by `Sector - Sight Obscuration` with Val1 = 3. |
| Destroy Nebulae (`Destroy Nebulae`) | The system is type Nebulae | The system becomes a Normal, start-eligible standard system with no system abilities. Its objects are untouched. |
| Create Black Hole (`Create Black Hole`) | A visible star in the sector. Blocked by `Stop Black Hole Creator` in the system. | Shockwave, then the system becomes physical type Black Hole, with system abilities `System - Movement Towards Center` 2, `System - Destructive Center` 5000 and `Sector - Shield Disruption` 5000. Existing warp points keep their abilities. |
| Destroy Black Hole (`Destroy Black Hole`) | The system is type Black Hole | As Destroy Nebulae: a Normal, start-eligible system with no system abilities. |
| Construct (`Create Constructed Planet` = PlanetSize `Special Ability ID`) | A visible star in the sector, and a PlanetSize record whose `Special Ability ID` equals Val1. Every `Constructed Planet Requirements` entry must be met: the ships and bases in **this sector**, whoever owns them, must carry designs with at least Val2 kT of components whose `Custom Group` equals Val1. The requirements are read from the acting ship's abilities. The count goes by design: every such component of the design counts with its mounted size, damaged or destroyed or not; mothballed ships and unit groups do not count. No movement is needed. | A planet of that PlanetSize is created in the sector, using a Planet record of that size with the builder's planet type and atmosphere if one exists (the last such record), else a random one of that size; if no Planet record has that size, no planet is made but everything else still happens. Its three values are set to `Planet Value High Percent` (`... High Resources` in finite games), its conditions to 1.5 (Optimal), and it is named with the next numeral (§5.6). The **star is removed**. Every object of the builder in the sector that carries the construction ability or whose design has any component of a required group is destroyed, whole ship or base included, mothballed ones too. |

---

## 10. Natural events (Events.txt)

The record fields:

- `Type` is one of: Ship - Damage, Ship - Lose Movement, Ship - Lose Supply,
  Ship - Experience Change, Ship - Cargo Damage, Ship - Moved,
  Planet - Conditions Change, Planet - Value Change, Planet - Population Change,
  Planet - Population Anger Change, Planet - Population Riot, Planet - Population Rebel,
  Planet - Cargo Damage, Planet - Facility Damage, Points - Change,
  Research - Delete Project, Intel - Delete Project, Planet - Created,
  Planet - Destroyed, Star - Created, Star - Destroyed, Warp Point - Closed,
  Warp Point - Opened, Planet - Plague, Planet - Plague Cured.
- `Severity` is Low, Medium, High or Catastrophic.
- `Effect Amount` is interpreted by the type. For example, it is damage for ship damage,
  a percentage change for value or conditions, and a sector count for ship moved.
- `Message To` is None, Owner, Sector, System or All.
- `Num Messages` with `Message Title N` / `Message N`, and `Num Start Messages` with
  `Start Message Title N` / `Start Message N`, define the texts. Each time a message is
  sent, one number N is drawn uniformly among the record's messages (start messages for
  the start of a timed event), and title N goes with text N. A record without messages
  sends none (confirmed: binary).
- `Picture` names the event image.
- `Time Till Completion`: when above 0, the event is timed. The start message is sent
  immediately, and the effect plus the final message follow after about that many turns.

Message placeholders: `[%SystemName]`, `[%SectorName]`, `[%SourceEmperorName]`,
`[%SourceEmpireName]`, `[%VehicleName]`, `[%VehicleSize]`, `[%PlanetName]`,
`[%DesignName]`, `[%TechName]`, `[%TreatyName]`, `[%FacilityName]`, `[%StarName]`,
`[%WarpPointName]`, `[%ActualAmount]`. `[%ActualAmount]` is not available in start
messages.

**Resolution** (confirmed: binary; the full rules are in spec 05 §4). The event step runs
once per turn for the whole quadrant, after the hazards of §7 and the timed events that
are due:

1. **One roll for the whole galaxy**, not one per empire: R[1,100] ≤ the chance of the
   chosen frequency (None never rolls). There are no new events before the date 2402.0
   (the first 20 turns).
2. One record is drawn among those allowed by Maximum Event Severity (with the quirk
   described in spec 05 §4), then a target of the right kind is drawn from the whole
   quadrant, retrying up to 1,000 times; luck and the homeworld protection are checked per
   candidate.
3. The effect applies at once, or is scheduled if the event is timed.

Bad-event reducers: the `Luck` racial trait (stock "Lucky" is −50%) and
`Change Bad Event Chance - System`; see spec 05 §4 for exactly how they act.

Stock timed catastrophes include a star destruction on a long timer and a planet
destruction on a shorter one.

---

## 11. Victory conditions

Setup offers these; any number can be enabled, each with its own value. The full rules are
in spec 05 §6; in short (confirmed: binary):

1. **Score threshold.** The game ends when some living empire's score reaches X. Initial
   value 100,000.
2. **Years elapsed.** The game ends when the date reaches 2400.0 + N years. Initial value
   10 years.
3. **Lead over second place.** The game ends when some living empire's score is at least
   P % of every other living empire's score. Initial value 150 %. When enabled, this test
   overrides the results of conditions 1 and 2 in the same check (a quirk of the original).
4. **Research share.** The game ends when some living empire's tech levels (each capped at
   its maximum) add up to at least P % of the maximum levels of the allowed areas its race
   can see. It counts levels, not areas. Initial value 10 %.
5. **Quadrant at peace** for N consecutive years: every pair of living empires holds
   Non-Aggression or better (having no contact or no treaty breaks the peace). Initial
   value 2 years.
6. **Qualifier.** Nothing is checked before 2400.0 + N years. This is not a condition by
   itself. Initial value 5 years.

The setup stores every "years" value as a number of turns (years × 10). The values above
are the original's initial values; the setup window may show its own.

Meeting any condition ends the game after that turn; the original does not pick a winner,
and the Scores window's ranking shows the result. With nothing enabled, nothing ends the
game automatically (the manual speaks of playing until one empire remains). The check runs
once per game turn, after every empire's end-of-turn processing and before the random event
(spec 05 §8), in turn-based and simultaneous games alike. The status window shows a grid of
conditions against empires, in pages of ten, up to 20 empires. Active conditions are
highlighted, and an X marks each empire that has met one.

---

## 12. Maps and scenarios on disk

- **Maps/.** Empty in this install. Map files come from Save Map (File menu, or the
  Quadrant tab) and from the separate map editor. We found no text-format map, so treat
  the format as ours to define.
  - A map should hold the systems with positions, names and type, every object with its
    SectType, abilities and name, the warp links, and optional **starting points**.
  - A *specific* starting point belongs to one player slot. A *common* one can take
    anyone.
  - Placement order: a player's specific point first, then a random remaining common
    point, then random placement. A player starting on a planet whose atmosphere is not
    theirs has that planet converted to their atmosphere and planet type at the same size
    (confirmed: binary, §3.6).
  - Loading a map must clear the previous starting points.
  - **Save Map during a game** (confirmed: binary) is offered only when the game's
    *Players can save the map during the game* box was checked (off by default, §2.2). It
    writes the systems, their stellar objects (stars, planets and asteroid fields, storms,
    warp points; no ships or units, no per-empire knowledge) and the starting points the
    game still holds: for a game started from a map, all its specific points and the
    common points that no player used; for a generated game, none. The empires' capitals
    are not written as starting points.
  - OpenSE4's map format is described in [docs/MAPS.md](../MAPS.md). Maps live in the
    user's data folder, never in the install. The file layout is an OpenSE4 extension; the
    original writes its own binary format.
- **Scenarios/.** Each scenario is a triple:
  - `<Name>_Settings.txt`: one record with `Name`, `Description` and `Starting Game`,
    the filename of a prepared savegame in the same folder.
  - The savegame (binary; ours would be our own save format).
  - `<Name>_Text.txt`: records with `ID` (unique), `Series` (group), `Segment` (order
    within the series, for previous and next), `Turn` (the turn on which the page
    appears, counting the first turn as 0), `For Players` (player numbers), `Text Title`,
    `Text Number of Paragraphs`, `Text Paragraph N`, and `Image` (a picture in the
    scenario folder).

---

## 13. State the engine must hold

- **Galaxy**: its systems, warp links, quadrant type and options.
- **System**: id, name, galaxy position, SystemType, physical type, system-wide abilities
  and objects.
- **Object**: id, kind, sector, SectType, name and abilities. A planet adds size, type,
  atmosphere, conditions (a real number, §5.6), three values and ruins. A warp point adds
  its link and size (the one-way flag has no effect, §8).
- **Link**: the two (system, object) ends.
- **Per-empire knowledge**: explored systems, known links, seen warp points, a last-seen
  snapshot of each system, claims, avoid list and notes.
- **Victory config**: which of the six conditions are on, and their values.

---

## 14. Open questions to verify in the running game

1. **System grid.** *Answered* (confirmed: binary): 13×13 (§4.1). The warp-point edge is
   the outer ring, reached through the bearing mapping of §3.5 (corners are almost never
   used). Rings are square (Chebyshev) rings (§4.3). Movement inside a system is in
   spec 03 §6.2: a step goes to any of the eight neighbours at the same cost.
2. **Galaxy grid.** *Answered* (confirmed: binary): Small, Medium and Large with the
   counts of §2.2; the grid is 67 × 46 squares (§3.2).
3. **Placement.** *Answered* (confirmed: binary): exact rules in §3.3.
4. **Warp placement.** *Answered* (confirmed: binary): yes to both; edge position follows
   the bearing to the destination and Min Angle compares galaxy bearings (§3.5).
5. **Ability roll.** *Answered* (confirmed: binary): exclusive, at most one ability per
   object; no natural storm has two (§5.2).
6. **Unstable stars.** *Answered* (confirmed: binary): `Star - Unstable` is never read, so
   stars only explode through the Star - Destroyed event or the Destroy Star manipulation.
   The event can target any star (spec 05 §4).
7. **Events.** *Answered* (confirmed: binary): one roll per turn for the whole galaxy; the
   target is drawn from the whole quadrant; a timed event strikes exactly
   `Time Till Completion` turns later; the frequency list is None, Low, Medium, High (§10,
   spec 05 §4).
8. **Obscuration.** *Answered* (confirmed: binary): maximum, never sum. Storms and nebulae
   hide planets and asteroid fields (and ships, units, comets) but never warp points, stars
   or storms. Observers in the same storm see each other when their sensors reach the
   storm's level (§6.2, §6.3).
9. **Omnipresent view.** *Answered* (confirmed: binary): no. It gives baseline sensors in
   every system; cloaked or obscured objects still need real sensors (§6.5).
10. **Presence.** *Answered* (confirmed: binary): ships, bases, fighter, satellite and
    drone groups and owned planets (populated or not) give sensors and so presence; mine
    fields do not (§6.1).
11. **Black holes.** *Answered* (confirmed: binary): pull, then random drift, then centre
    damage, once per turn in the event step. The centre damage is ordinary damage (5,000 for
    a created black hole, so lethal in practice). Pulled ships stop at the centre and
    drifting ships at their random target; nothing stops at warp points (§7).
12. **Turbulence.** *Answered* (confirmed: binary): it is rolled on departure (50 %) from
    the departure warp point's ability, and both ends of a generated link carry the same
    ability (§3.5, §8).
13. **Homeworld.** *Answered* (confirmed: binary): a natural planet that already has the
    player's atmosphere and planet type, of the Home Planet Value size, chosen as in §3.6;
    its conditions stay as generated. If none fits, a new planet is created in a
    start-eligible system (§3.6).
14. **Names.** *Answered* (confirmed: binary): planets are numbered in template order and
    moons get letters (§5.6); every star is "system + Star" whatever the number of stars; a
    warp point is named after its destination once that is explored (§5.4); systems beyond
    the name list get no name (§3.4).
15. **Conditions.** *Answered* (confirmed: binary): six bands on a 0–1.5 scale (spec 02 §2)
    and the generation of §5.6.
16. **Victory.** *Answered* (confirmed: binary): the initial values are in §11, and
    the full rules are in spec 05 §6. Research share counts levels, not areas. The original
    never names a winner: meeting any condition, "at peace" included, ends the game, and
    the Scores ranking shows the result, so there are no ties to break. An empire is
    eliminated when it has no populated planet and no ship or base (spec 05 §6). The setup
    window enforces no minimum or maximum on any victory value (confirmed: binary): each is
    read as a decimal number (the two percentages without their % sign) and truncated to a
    whole number, the year values after multiplying by ten (whole turns); the research
    share is held in 16 bits. Text that is not a number is refused by the conversion
    itself. The unit and ship caps are not checked either (§2.2).
17. **Starting year.** *Answered* (confirmed: binary): the game date starts at 2400.0.
18. **Option lists.** *Answered* for most (confirmed: binary): Starting Resources 5,000 /
    20,000 / 100,000; Racial Points 0 / 2,000 / 3,000 / 5,000; Tech Level Low / Medium /
    High; AI Difficulty Low / Medium / High; AI Bonus None / Low / Medium / High; Score
    Display own / allies / all; Autosave None (the default) or every 1, 2, 3, 5 or 10 turns
    (§2.2). Technology Cost is Low / Medium (the default) / High; the cost formulas are in
    spec 05 §1.3 (confirmed: binary).
19. **Manipulation aftermath.** *Answered* (confirmed: binary): see the shockwave and the
    table in §9. No SystemType record is used for created nebulae and black holes; the
    system's type and abilities are set directly. Destroy Planet removes the colony with
    its population and facilities.
20. **Scenic systems.** *Answered* (confirmed: binary): a zero-object type gets no objects
    other than its warp points; only its system-wide abilities, if any, apply.
21. **Warp variants.** *Answered* (confirmed: binary): the one-way flag is never used;
    generated links use a plain record unless they carry an ability (then an Unusual one);
    opened links always use the first plain record (§3.5, §9).
22. **Warp cost.** *Answered* (confirmed: binary; settled by spec 03 open question 5 and the
    Warp order in spec 03 §8): a jump costs 1 movement point and one step's supply, and
    the ship must be in the warp point's sector. Reaching that sector is an ordinary step;
    there is no separate stop (§8).
23. **One-way links.** *Answered* (confirmed: binary): neither; the original ignores the
    flag and every link is two-way. The engine's one-way handling should be removed.
24. **Learning links.** *Answered* (settled by the knowledge rule of §8): a warp point shows
    its destination once the viewer has explored the destination system. Travelling from A
    to B explores B, and A is already explored, so both ends are known and B's end shows
    that it leads to A. The engine's reading (a traversed link is known in both
    directions) matches.
25. **Hazard order.** *Answered* (confirmed: binary): see §7. The pull and drift move every
    ship, base and unit group, drift heads for one random sector per turn, centre damage
    follows, and sector damage only hits ships moving into the sector (50 %).
26. **Sensors.** *Answered* (confirmed: binary): every owned planet and every ship, base,
    fighter, satellite and drone group is a sensor source; mine fields are not. An
    unpopulated colony's facilities do give their sensor levels: nothing in the sensor
    rule looks at population (§6.1).
27. **Edge placement reversal.** *Answered* (confirmed: binary): the original compares the
    outline sector with the sectors the earlier warp points actually ended up on, as the
    engine does, and takes the last matching warp point. The reversal is not dead: it
    moves a bottom-edge warp point in the one case described in §3.5. The engine takes the
    first match instead of the last, which gives the same result in that case.
28. **Asteroid numerals.** *Answered* (confirmed: binary): asteroid fields have their own
    count, separate from the planets' (§5.6).
29. **Numerals of made planets.** *Answered* (confirmed: binary): one above the highest
    numeral from I to XXX that ends the name of the first planet in each sector; asteroid
    fields are ignored (§5.6). Homeworlds and starting planets that setup creates use the
    number of sectors that hold a planet instead (§3.6).
30. **Order of the extra starting planets.** *Answered* (confirmed: binary): the home system,
    then its neighbours in the order of its warp points, then their neighbours in the same
    way; other players' home systems are excluded unless systems may be shared; every
    candidate system starts explored (§3.6).
31. **Missing records.** *Answered* (confirmed: binary). The home size is raised to the
    smallest natural size that exists for the race's atmosphere and type, so with *All player
    planets the same size* a record always exists when any record of that atmosphere and
    type exists; when none exists, or when a map point's planet has no record of its size,
    the original draws from an empty list and the result is undefined. The engine's
    fallbacks there (another size, then any planet record given the race's atmosphere and
    type) are an OpenSE4 choice and stand. With *Warp Points located anywhere* the original
    redraws without limit and hangs when the five squares are full; the engine stops after
    1,000 draws. In the connectivity pass the original marks a system it cannot link
    together with everything linked to it, never looks beyond 68 squares, and can loop
    forever (§3.5); the engine marks only that system, searches any distance and always
    finishes (PARITY_GAPS).
32. **What obscures a sector.** *Answered* (confirmed: binary): storms, planets and asteroid
    fields (their own rolled abilities only, not their colonies' facilities) and ships and
    bases (§6.2). Unit groups, stars, warp points and comets never count.
33. **Construct materials.** *Answered* (confirmed: binary): the sum goes by design, so every
    component of the group counts, damaged or not, with its mounted size; ships of any
    owner count, mothballed ships and unit groups do not. Every object of the builder in the
    sector that carries the construction ability or a component of a required group is
    destroyed, mothballed ships included (§9). Whether bases count as material carriers is
    open question 42.
34. **Hostile objects for stellar manipulation.** *Answered* (confirmed: binary): visible ships
    and bases that are not mothballed, and visible colonized planets and asteroid fields, of
    empires below Non-Aggression or without contact; unit groups never block, and Destroy
    Planet makes no such check at all (§9). The asteroid field's rolled ability does not
    carry over to a planet made from it, and the new planet rolls none. Create Planet also
    refuses a colonized asteroid field.
35. **Setup details.** *Answered* (confirmed: binary). The racial point cost uses each
    characteristic as stored, without clamping; only the race window's up and down buttons
    keep a value within its Min/Max Pct, so a race read from a file with a value outside
    them is costed at that value. Every starting planet, extra ones included, is a capital
    of colony type "Homeworld" (§3.6). "More than 60 %" is compared exactly (the original's
    floating-point test gives the same answer for every count). The engine clamps the
    cost (PARITY_GAPS).
36. **Map starting points.** *Answered* (confirmed: binary): the order and the conversion are
    as the engine does them (§3.6): only a different atmosphere triggers the conversion, and
    the converted planet keeps its name, values, conditions and stellar size. With no
    record of that size the original's result is undefined, so the engine's fallback (a
    record of another size) stands. The original does not check whether a point's sector
    was already taken; the engine's skip is an OpenSE4 choice (PARITY_GAPS).
37. **Save Map during a game.** *Answered* (confirmed: binary): the option is off by default
    and the menu entry is disabled without it. The original writes the starting points the
    game still holds (a loaded map's specific points and unused common points; none for a
    generated game), not the capitals (§12). The map file layout is an OpenSE4 extension.
38. **Autosave.** *Answered* (confirmed: binary): after a turn is processed, when the number
    of turns since 2400.0 is a multiple of N, into a file named after that number's last
    digit, so at most ten files, and fewer for N = 2, 5 or 10 (§2.2). It follows the game
    date, so nothing restarts on loading. OpenSE4's file names, save format and the network
    host's own autosave setting are OpenSE4 extensions. The choice can be changed during a
    local or hotseat game in the Empire Options window; network and e-mail games are saved
    by their host.
39. **Default turn style.** *Answered* (confirmed: binary): Turn-Based. The Mechanics tab
    selects it for a new game. Quick Start sets no turn style of its own, and the game
    options the program starts with are turn-based too. OpenSE4's network host and its
    setup files, an OpenSE4 extension, still default to simultaneous turns (the
    original offers its network connection for simultaneous games only).
40. **No Tactical Combat default.** *Answered* (confirmed: binary): the box is clear for a new
    game (§2.2), as in OpenSE4, so turn-based games ask each human side Tactical or
    Strategic (spec 04 §3).
41. **The home system among the starting-planet candidates.** **Answer:** the home system is
    never dropped (confirmed: binary). Both filters of step 2 (§3.6) spare it: it stays
    when it is not start-eligible, and when another empire has a colony there or it is
    another player's home system. So when a map's starting points put two empires in one
    system without *Allowed to start in the same system*, each keeps that system as a
    candidate, and the scan still skips the other player's homeworld. The engine does the
    same.
42. **Bases carrying Construct materials.** **Answer:** bases count like ships (confirmed:
    binary). The materials are summed over every object in the sector that has a design,
    which is every ship and base, whoever owns it, except mothballed ones; unit groups have
    none. Afterwards the builder's bases whose design holds a component of a required group
    are destroyed like its ships, mothballed ones included. The engine does the same.
43. **Comet and warp point entries in the names.** **Answer:** the original draws a position
    for the entry exactly as for any other entry (confirmed: binary). The specifier is
    resolved with its usual random numbers (`Ring`, `Circle Radius`) or its usual sector
    (`Coord`, `Same As`). The sector is then marked occupied, so later `Ring` and `Circle
    Radius` entries avoid it, and recorded for `Same As`. A SectType record of the entry's
    physical type is drawn as for any entry (§5.1), using a random number when there is at
    least one candidate, and also when size and atmosphere are both `Any` even if there is
    none (no `Comet` records exist in stock data). Then nothing is made: no object, no
    ability roll, no values. The entry keeps an empty name and counts for the letters of
    later planets in its sector (§5.6). The engine does the same.
44. **Colony cloaking: the engine's choices (§6.9).** OpenSE4 chose, marked (inferred) in the
    code:
    - A colony Cloak or Decloak given during a simultaneous turn reaches the host in the
      player's orders and is carried out when the host applies them at the start of turn
      processing, in player and command order; a Decloak's first-contact check runs then. How
      does the original's host receive it, and when does its contact check run?
    - "When the game is loaded" is taken as loading a saved game (Load Game, a network host
      started from a save). A play-by-e-mail game file opened by a player or by the host is
      not recalculated, so both sides keep the same state. Does opening a PBEM turn file
      recalculate?
    - An upgrade that converts a colony's facilities counts as completing facilities and
      recalculates the levels. Does it?
