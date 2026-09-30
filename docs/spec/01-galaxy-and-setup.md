# Spec 01: Galaxy, game setup, systems, stellar objects, sight, victory

Status: reference spec for engine work. It was written clean-room from the SE4 Deluxe
manual, the self-documenting data files, the version history and the tutorial scenario
text. Everything is paraphrased. Where the sources are silent we say so and give a
recommended behaviour marked **(inferred)**. Section 14 lists the points to check in the
running game.

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
- **Quadrant Size.** The number of systems. The concrete choices are not documented. The
  hard cap is `Settings: Maximum Number Of Systems` (stock 100; the file allows up to 255).
- **Generate Map Now.** Builds and previews a quadrant. Pressing it again rerolls. The
  previewed map is the one the game starts with.
- **Load Map.** Uses a saved map (§12). **Save Map** writes the previewed map.
- **General options** (booleans):
  - *All Warp Points Connected*: the warp graph must be a single connected component.
    With it off, isolated clusters can exist.
  - *No Warp Points*: no warp points are generated. Travel then needs the Open Warp Point
    stellar manipulation (§9).
  - *Warp Points located anywhere in system*: warp points go on any sector instead of
    only the outer edge.
  - *All systems seen by all players*: every system starts explored for every empire
    (static contents known) without granting presence (§6).
  - *Omnipresent view of all systems*: every empire sees every system as if present.
    See §6.5 for how this interacts with cloaking.
  - *Finite resources*: planet value becomes a stock of resources that production
    depletes (§5.6).

**Events tab**

- **Event Frequency** maps to `Settings: Event Percent Chance Low/Medium/High` (stock 5,
  10 and 25). A "none" choice probably also exists.
- **Maximum Event Severity** is one of Low, Medium, High or Catastrophic. It filters
  `Events.txt` by `Severity` (§10).

**Technology tab**

- **Technology Cost** is a relative multiplier on research cost.
- **Technology Areas Allowed** holds one checkbox per tech group. A group that is
  unchecked is removed from research entirely.

**Player Settings tab**

- **Starting Resources** sets the initial stockpile, and **Racial Points** sets the budget
  for race creation.
- **Home Planet Value** (Low, Medium or High) sets the homeworld's value from
  `Plr Planet Value {Low,Medium,High} Percent`, or from the matching `... Resources` key
  in finite-resource games.
- **Number of Starting Planets.** Above 1, the extra planets usually sit in nearby
  systems.
- **Empire Placement.** *Allowed to start in the same system* is off by default, which
  keeps home systems distinct. *Evenly distributed* is on by default and spreads empires
  as far apart as it can.
- **Score Display** controls whose scores you see. The default is yourself plus allies;
  you can also allow everyone's.
- **Technology Level for New Player** runs from Low (the default: base techs) to High
  (every tech).

**Players tab**

- The list of explicit empires, with Add New, Add Existing, Edit, Remove and Save.
- **Random computer players** has two checkboxes. *Regular* AIs are full empires;
  *neutral* AIs stay in their home system.
- **Number of Computer Players** (Low, Medium or High) rolls the count within
  `Minimum/Maximum Computer Player {L,M,H} Setting`. Neutrals use the parallel neutral
  keys.
- **Difficulty** and **Bonus** handicap the AI.

**Victory Conditions tab**: see §11.

**Game Settings tab**

- The master password and the maximum units and ships per player. The caps default to
  `Default Number Of Units/Ships Per Player`.
- Cheat codes; No Tactical Combat; Show complete tech tree; Allow gifts/tributes;
  Allow technology trades.
- **Team Mode** allies every computer player against the humans.
- **Allow intel projects**. The intel techs remain even when this is off, but are useless.
- **No Ruins** suppresses Ancient Ruins rolls.
- **Only breathable atmosphere** allows no domed colonies.
- **Only home planet type** limits colonization to the homeworld's physical type, even
  when the empire has the technology for others.
- **Players can save map during a game**.

**Mechanics tab**

- Play style: Hotseat or Different Machines.
- Turn style: Turn-Based (sequential) or Simultaneous.
- Multiplayer filename and save directory.
- Autosave every N turns, rotating through ten slots.
- Connection type, enabled only for Different Machines with Simultaneous: manual file
  moving, TCP/IP Host or TCP/IP Player. Simultaneous games on different machines
  require a master password.

### 2.3 Settings.txt keys owned by this spec

| Key(s) | Meaning |
|---|---|
| `Maximum Number Of Systems` | Hard cap on systems in a quadrant (at most 255). |
| `Planet Value Low/High Percent`, `... Resources` | Range for rolling each resource value of a natural planet: a percentage, or a stock in finite mode. `Asteroids Value ...` gives the same for asteroid fields. |
| `Plr Planet Value {Low,Medium,High} Percent/Resources` | Homeworld value for each Home Planet Value setting. |
| `Maximum/Minimum Planet Percent/Resource Value` | Clamps on value changes during play. |
| `Remote Mining Decreases Asteroid Value` | Whether remote mining depletes asteroids. |
| `Planet Value Percent Loss After Owner Death` | Value lost when a colony's owner is eliminated **(inferred)**. |
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
| `Min Dist Between Systems` | Number of **empty** galaxy squares required between any two systems. A value of 1 means no two systems occupy adjacent squares, so the Chebyshev distance is at least 2 **(inferred metric)**. |
| `System Placement` | One of `Random`, `Clusters`, `Spiral`, `Diffuse`, `Grid`. |
| `Max Warp Points per Sys` | Cap on warp points per system in the first pass. It may be exceeded to guarantee connectivity. |
| `Min Angle Between WP` | Minimum angle in degrees between two warp links leaving the same system, measured as galaxy-map bearings. The connectivity pass may break it. |
| `Number of System Types` | Count of weighted entries. The loader must support at least 300. |
| `Type N Name` / `Type N Chance` | A SystemTypes.txt name and its weight in tenths of a percent. The weights should sum to 1000; renormalize with a warning if they do not. |

The stock file has six quadrant types. For example, "Mid-Life" pairs `Random`
placement with 5 warp points per system and a 60° minimum angle, and "Spiral Arm"
uses `Spiral`.

### 3.2 Galaxy coordinates

- Systems sit on an integer galaxy grid. One grid square is about **10 light years**.
- Distances shown in the UI and used by Open Warp Point are Euclidean distances between
  system positions, times 10 ly. Evidence: the stock warp-opening components use 10, 20,
  ... 50 for descriptions reading 100 to 500 ly, while Abilities.txt claims the value is
  in light years. Treat the value as grid squares **(inferred)**.
- Every empire knows every system's position from turn 0. Names and contents stay hidden
  until the system is explored.
- The galaxy grid's dimensions are not documented. Choose them so that the system count
  fits at the requested minimum distance.

### 3.3 Placement algorithms

Only the names and the minimum-distance constraint are documented. Implement each as a
generator that honours `Min Dist Between Systems` and the requested count, retrying or
shrinking when it runs out of room:

- `Random`: uniform rejection sampling.
- `Clusters`: several groups of nearby systems with gaps between them. The history notes
  that this mode must scale beyond 200 systems.
- `Spiral`: points along logarithmic arms.
- `Diffuse`: sparse, even coverage (a galactic-edge feel), for example Poisson-disc
  sampling with a large radius.
- `Grid`: a regular lattice with optional jitter.

### 3.4 System type, names and contents

1. For each system, draw one entry from the quadrant's weighted list.
2. Instantiate the chosen SystemTypes record (§4).
3. **System names** are drawn from `SystemNames.txt` without replacement **(inferred)**.
   The stock list is only slightly longer than the default system cap. Behaviour once
   names run out is unknown, so fall back to generated names.
4. **Ruins.** When *No Ruins* is set, drop the `Ancient Ruins` and `Ancient Ruins Unique`
   entries before rolling (§5.2).

### 3.5 Warp network

Documented facts:

- Links come in **pairs**: a warp point in each of the two systems, each leading to the
  other. The galaxy map draws a line only when both ends are known.
- Warp points normally sit on the **outermost edge** of the system grid. The tutorial
  shows them at positions such as (0,10), on the left edge.
- Each system has at most `Max Warp Points per Sys` links in the first pass, with at
  least `Min Angle Between WP` degrees between links. A second pass adds links to make
  the graph connected when *All Warp Points Connected* is set, even if that breaks the
  caps.
- Every warp point in a system receives the stellar ability set named in that system
  type's `WP Stellar Abil Type`. For example, black-hole systems use an "unstable" set
  that adds turbulence damage.
- Politics depends on connectivity: contact between two empires ends when no path links
  their planets.

Recommended algorithm **(inferred)**:

1. Build candidate edges between near neighbours, sorted by length.
2. Greedily accept an edge if both endpoints are under the cap and its bearing is at
   least the minimum angle from every existing link at both endpoints.
3. If connectivity is required, join components with the shortest inter-component edges,
   ignoring the caps.
4. Place each end on the edge sector nearest the bearing toward the other system. With
   "anywhere", use a random free sector instead.
5. Pick each warp point's SectType record from the `Warp Point` entries (§5.1),
   preferring `Unusual = FALSE` for natural links.

### 3.6 Empire placement

- Home systems may only be system types with `Empires Can Start In = TRUE`. This rule was
  added because players were starting in nebulae, black holes and asteroid systems.
- With *Evenly distributed*, spread empires to maximize separation, measured in warp jumps
  or galaxy distance (for example, iterative farthest-point selection).
- Without *Allowed same system*, home systems must be distinct.
- The homeworld is a planet in the home system, converted to match the race: its
  breathable atmosphere and its native physical type from the Environment setup. Its size
  is kept. The version history documents this conversion for map starting points; applying
  it to random maps is **(inferred)**. Among the candidates, the engine prefers the planet
  whose size is closest to Medium, the size of the homeworld observed in a stock Quick Start
  game **(inferred)**.
- Some start-eligible stock types, such as one trinary layout, contain no planets. The
  generator must then create or convert a planet, or reject that system.
- The homeworld value comes from the Home Planet Value setting (§2.2).
- Extra starting planets go in nearby start-eligible systems, or in the same system.
- Neutral empires get a single home system and never leave it.
- A home system is explored for its owner at turn 0.

### 3.7 Pipeline

```
generateQuadrant(setup):
  positions  = place(setup.quadrantType.placement, setup.size, minDist)
  for each pos: sys = instantiate(weightedPick(quadrantType.types)); sys.name = drawName()
  if !noWarpPoints: buildWarpNetwork(); placeWarpPoints(anywhere?)
  rollStellarAbilities(all objects, noRuins)
  assignPlanetValues(finite?)
  placeEmpires(); convertHomeworlds(); applyHomeValue()
  initKnowledge(allSeen?, omnipresent?)
```

---

## 4. Star systems

### 4.1 Sector grid

- Each system is a square grid. The SystemTypes header says X and Y both run **0..12**,
  so the grid is 13×13 and the centre is (6,6). X grows to the right and Y grows downward:
  (0,10) is near the bottom left. The Deluxe manual instead says 196 sectors, which would
  be 14×14. See §14.
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
| `Obj N Physical Type` | `Planet`, `Asteroids`, `Storm`, `Star`/`Sun`, `Destroyed Star` or `Comet`. |
| `Obj N Position` | See §4.3. |
| `Obj N Stellar Abil Type` | StellarAbilityTypes name to roll for this object. |
| `Obj N Size` | `Any` or Tiny..Huge. |
| `Obj N Atmosphere`, `Obj N Composition` | Planets and asteroids only. `Any` or a specific value. |
| `Obj N Age`, `Obj N Color`, `Obj N Luminosity` | Stars only. `Any` or a specific value. |

### 4.3 Position specifiers

- `Ring 1`: the centre sector.
- `Ring k` for k = 2..7: a random sector exactly k−1 squares from the centre. Use
  Chebyshev distance, meaning square rings **(inferred)**.
- `Circle Radius R`: a random sector whose rounded Euclidean distance from the centre
  equals R. The header calls it a "true circle", in contrast to the square rings.
- `Coord X,Y`: a fixed sector with no randomness.
- `Same As N`: the same sector as object N, which must come earlier in the record. Used
  for moons.
- Random placements should avoid sectors already holding an object unless the specifier
  is `Same As` **(inferred)**. Warp points should also avoid occupied sectors.

### 4.4 System-wide ability types

These occur in system records. Val 1 has the meaning shown; Val 2 is unused in stock
data.

| Ability | Effect |
|---|---|
| `Sector - Sight Obscuration` | Every object in the system gets obscuration Val1 in **all five** sight types (§6). This is what makes a nebula. |
| `Sector - Shield Disruption` | Val1 shield points lost in combat. Stock uses a huge value, so shields are useless. |
| `System - Movement Towards Center` | Each turn, every ship is pulled Val1 sectors toward the centre (black holes). |
| `System - Destructive Center` | Val1 normal damage to ships in the centre sector (black holes). |
| `System - Movement Random` | Each turn, ships are displaced Val1 sectors in a random direction (spatial ruptures). |
| `Sector - Damage`, `Sector - Sensor Interference` | May also appear at system level, with the same meaning as at sector level (§5.3). |

The header also lists `System - Sensor Interference`, `System - Damage` and
`System - Ability Required`. Stock data does not use them. Accept them as aliases. The
meaning of `Ability Required` is unknown.

Timing **(inferred)**: apply the movement effects in the turn's movement phase, after
orders execute. Apply centre damage whenever a ship ends a move in the centre or is pulled
into it.

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
| `Warp Point One-Way` | Warp Point | Boolean. |
| `Unusual` | Warp Point | Boolean. |

**Instantiation.** For each SystemTypes object, filter SectType by physical type and by
every constraint that is not `Any`. Constructed sizes are excluded from natural rolls.
Pick uniformly among the matching records **(inferred)**, so the frequency of an
attribute follows how many records carry it. If nothing matches, warn and relax the
constraints.

**Star attributes.** Age, colour and luminosity appear to be descriptive only. The number
of stars matters, because the "Solar ..." abilities (solar supply and solar resource
generation) scale per star in the system.

### 5.2 StellarAbilityTypes.txt and the ability roll

| Field | Semantics |
|---|---|
| `Name` | Referenced from SystemTypes (`Obj N Stellar Abil Type`, `WP Stellar Abil Type`). |
| `Number of Poss Abilities` | 0..100. |
| `Ability N Chance` | 0..1000, in tenths of a percent. The chances in one record should total at most 1000. |
| `Ability N Type/Descr/Val 1/Val 2` | The ability granted if this entry is chosen. |

**Roll.** Draw one value r in [0,1000) and walk the entries, accumulating their chances.
The first entry whose running total exceeds r is granted; if r lands past the total,
nothing is granted. So each object gets **at most one** rolled ability **(inferred from
the ≤1000 rule)**. Two stock examples show the consequence. The storm set splits the
full 1000 among four effects, so every natural storm has exactly one. The planet set
totals only a few percent, and all of it is ruins.

### 5.3 Object-level ability semantics

| Ability | Val 1 (Val 2) |
|---|---|
| `Sector - Damage` | Normal damage per turn to every object in the sector. Stock storms carry Val2 = 1 with no documented meaning. |
| `Sector - Sight Obscuration` | Obscuration level in all sight types for objects in the sector. Units do not benefit from it **(interpretation)**. |
| `Sector - Sensor Interference` | Modifier to combat to-hit rolls in the sector. Stock uses a negative value. It does not affect detection. |
| `Sector - Shield Disruption` | Shield points lost in combat in the sector. |
| `Star - Unstable` | Chance that the star explodes **per year**. Stock uses 0 with only descriptive text. See §10 and §14. |
| `Warp Point - Turbulence` | Normal damage to each object that passes through this warp point. |
| `Ancient Ruins` | On colonization, the colonizer receives Val1 random tech areas. |
| `Ancient Ruins Unique` | On colonization, the colonizer receives the unique tech area with id Val1. |

Stars are never hidden by storm or nebula obscuration (version history).

### 5.4 Object kinds

- **Star.** Anchors the system and is the target for Destroy Star, Create Nebulae, Create
  Black Hole and constructed planets.
- **Destroyed Star.** A dead stellar core. It is star-like in data, but assume it is not
  a valid target for star manipulations or solar generation **(inferred)**.
- **Planet.** Colonizable (§5.5 and §5.6).
- **Asteroids.** Cannot be colonized in stock (there is no asteroid colonize ability) but
  can be remotely mined. They are the required input for Create Planet. A destroyed planet
  becomes an asteroid field.
- **Storm.** A sector hazard with its rolled ability. It has no owner and does not move
  **(inferred)**. The Storm report shows the picture, name, size, description and ability
  list.
- **Warp Point** (§8).
- **Comet.** A legal physical type that stock data never places. Treat it as inert
  scenery.

### 5.5 PlanetSize.txt: capacity by size

Records are keyed by (`Physical Type`, `Name`). `Physical Type` is `Planet` or
`Asteroids`. `Name` is Tiny..Huge, or a constructed name.

| Field | Semantics |
|---|---|
| `Stellar Size` | The Tiny..Huge category used by generation filters and by size comparisons, such as a destroy-planet limit. A constructed world is size Huge. |
| `Max Facilities`, `Max Population`, `Max Cargo Spaces` | Capacity of a normal colony. |
| `Max Facilities Domed`, `Max Population Domed`, `Max Cargo Spaces Domed` | Capacity when any resident race cannot breathe the atmosphere. |
| `Constructed` | TRUE for manufactured worlds, which natural generation never creates. |
| `Special Ability ID` | Nonzero for constructed worlds. `Create Constructed Planet` Val1 refers to it. |

Capacity roughly doubles with each size step. For example, a Medium planet holds
3 facilities when domed against 15 normally, and constructed worlds are an order of
magnitude larger. Asteroid rows also define capacities, which only matter for mods.

### 5.6 Other planet properties

- **Name.** The system name plus a Roman numeral ("Xyz IV"). The owner may rename a
  colonized planet. The numbering order (object index or distance from the star) is
  unconfirmed.
- **Physical type.** Rock, Ice or Gas (giant). Colonizing a type requires the matching
  `Colonize Planet - X` ability. Each empire starts able to colonize its home type.
- **Atmosphere.** A race breathes exactly one of the four gases. A planet with any other
  atmosphere, or with none, can only hold a **domed** colony, which uses the Domed limits.
  A colony counts as domed if any resident race cannot breathe the air.
  `Planet - Change Atmosphere` switches the atmosphere after Val1 turns to what most of the
  population breathes.
- **Conditions.** An ordinal from Pleasant (best) to Deadly (worst). Worse conditions
  lower happiness and reproduction. The generation distribution and intermediate labels
  are unknown. Conditions change through events and abilities, by a percentage per turn.
- **Value.** Three numbers, one each for minerals, organics and radioactives.
  - *Normal mode*: a percentage multiplier on production at that planet. Roll it per
    resource, uniformly in `Planet Value Low..High Percent` (asteroids use their own keys),
    and clamp later changes to `Min..Max Planet Percent Value`.
  - *Finite mode*: the remaining resource stock. Roll it in `... Low..High Resources`.
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
3. **Present.** The empire owns a colony, ship, base or unit in the system. The empire
   then sees the live system through the sight rules below. Whether mines or other units
   grant presence is unconfirmed.

*All systems seen* starts every system at level 2. A **Partnership** treaty shares sight:
you see every system your partner sees as if you were present, and you learn every
system they explore. Sharing is transitive across chains of partnerships (A–B plus B–C
lets A see what C sees).

### 6.2 Sight types

There are five types: `EM Active`, `EM Passive`, `Psychic`, `Gravitic`, `Temporal`.

- **Sensor level per type.** Every object has a baseline of EM Active 1 and 0 in the other
  types. `Sensor Level` abilities (Val1 = type name, Val2 = level) raise it.
- **Obscuration per type.** Every object's baseline is 1 in all types, so a sensor level
  of 1 in any type sees it. Obscuration can be raised by:
  - `Cloak Level` abilities (Val1 = type, Val2 = level) from components, armour or hulls.
    These count **only while the object is cloaked**. Hull cloak levels, such as those on
    mines, act as always-on **(inferred)**.
  - `Sector - Sight Obscuration` from a storm in the object's sector or from a system-wide
    nebula. It raises all five types. It never applies to stars.
- Combine several sources per type by taking the **maximum** **(inferred)**.

### 6.3 Detection rule

Sight is resolved **per system**, not per sector. The empire's sensor vector in system S
is the per-type maximum over everything it has in S. Planets count, since they carry the
baseline, and so do partners' assets. A foreign object is visible when there is **any**
type t with sensor[t] ≥ obscuration[t].

```
visible(E, obj) = present(E, sys) &&
                  exists t in 5 types: sensor(E, sys)[t] >= obsc(obj)[t]
obsc(obj)[t] = max(1, obj.cloaked ? cloak[t] : 0,
                   obj.isStar ? 0 : max(sectorObsc(obj.sector), systemObsc(sys)))
```

Worked example: a stock level-1 cloak sets 2 in all types. Base sensors (EM Active 1)
miss the ship, and a level-2 sensor in any one type reveals it. A cloak that raises only
EM Active and EM Passive to 3 is still seen by Psychic, Gravitic or Temporal level 1.

Stock mine hulls carry cloak level 5 in every type, which is above the best stock sensor
(level 4). Mines are therefore undetectable and are found by sweeping.

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
- Planets hidden by a storm or nebula are not drawn for observers who cannot see them.

### 6.5 Omnipresent view

The manual says omnipresent view shows everything, cloaked objects included. The version
history later *fixed* omnipresent view revealing cloaked ships. Recommendation: give
presence (baseline sensors) in every system, but reveal cloaked objects only through
real sensors. Check this in the game.

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

---

## 7. Hazards per turn (storms, nebulae, black holes, ruptures)

Order within the turn **(inferred)**: after movement, apply displacements (pull toward
the centre, random moves), then centre damage, then `Sector - Damage` to objects in hazard
sectors. Resolve these deterministically in ship-id order.

- A ship displaced into a sector with visible hostiles should trigger combat as normal
  movement would **(inferred)**.
- Shield disruption and sensor interference apply only inside combat that takes place in
  the affected sector or system.

---

## 8. Warp points

- Warp points are the normal way to travel between systems. A jump is instantaneous and
  lands on the paired warp point in the destination system.
- Tutorial evidence suggests a ship must stand on the warp point, and the jump itself
  costs 1 movement point. Movement details belong in the movement spec.
- A Move To order across systems routes through known links. The explicit Warp order is
  needed only when the link leads to an unexplored system.
- `Warp Point - Turbulence` on the departure warp point damages every object that passes
  through. Whether arrivals through the paired end take damage is unknown.
- Knowledge: an empire that has seen a warp point knows it exists. It learns the
  destination only by traversing it, or through partner sharing or omnipresence.
- The data allows `One-Way` warp points (travel in one direction only) and `Size`
  (Small/Large). Stock generation does not appear to use either. Only `Close` checks size,
  and stock closers work on any size.
- Opening or closing a warp point creates or removes **both** ends (§9).

---

## 9. Stellar manipulation

Stellar manipulation is triggered by a ship through a special window; ship and planet
orders can also reach it through Use Component or Use Facility. Common rules:

- The ship must be **at the target location** when the order executes. The history fixed
  execution after the ship had moved away.
- The ship needs movement remaining (confirmed at least for Create Storm) and the
  component's supply cost.
- Components with `Component Destroyed On Use` are consumed. Those whose effect destroys
  the ship itself carry no such flag.
- A button is enabled only when the ship has the matching ability and the precondition
  holds.
- Blocking abilities (`Stop ...`) on a facility in the target system prevent the action.
  For warp points, a blocker at either end prevents it, and it blocks everyone, including
  its owner.
- Each outcome raises the matching log event: Planet/Star Created or Destroyed, Warp Point
  Opened or Closed.

| Action (ability) | Precondition | Result |
|---|---|---|
| Create Planet (`Create Planet Size` = max size) | Asteroids in the sector | The asteroid field becomes a planet no larger than Val1, with random atmosphere and type. |
| Destroy Planet (`Destroy Planet Size` = max size) | A planet of size ≤ Val1 in the sector. Blocked by `Stop Planet Destroyer` on that planet (only one such facility per planet counts). | The planet becomes an asteroid field. Its colony is lost **(inferred)**. |
| Create Star (`Create Star`) | Not allowed in Nebulae or Black Hole systems | A random star is created in the sector. |
| Destroy Star (`Destroy Star`) | A star in the sector. Blocked by `Stop Star Destroyer`. | A shockwave destroys **everything in the system except warp points**, the acting ship included. |
| Open Warp Point (`Open Warp Point Distance`) | The target is another system within Val1 × 10 ly and not the origin itself. Blocked by `Stop Open Warp Point` at either end. | A new warp point is added at this sector, and its pair in the target system. The target is picked on the galaxy map. |
| Close Warp Point (`Close Warp Point`) | A warp point in the sector. Blocked by `Stop Close Warp Point` at either end. | Both ends are removed. Closing an already-closed link must be a harmless no-op (a simultaneous-turn race). |
| Create Storm (`Create Storm`) | Movement remaining | A random storm whose abilities are capped by the `Created Storm Maximum ...` settings. |
| Destroy Storm (`Destroy Storm`) | A storm in the sector | The storm is removed. |
| Create Nebulae (`Create Nebulae`) | A star in the sector. Blocked by `Stop Nebulae Creator`. | Everything in the system is destroyed, and the system becomes type Nebulae with system-wide obscuration **(inferred: pick a Nebulae SystemType)**. |
| Destroy Nebulae (`Destroy Nebulae`) | The system is type Nebulae | Obscuration and the nebula are removed completely, leaving no residual hiding. |
| Create Black Hole (`Create Black Hole`) | A star in the sector. Blocked by `Stop Black Hole Creator`. | Everything in the system is destroyed, and the system becomes a black hole with pull, centre damage, shield disruption and unstable warp points **(inferred)**. |
| Destroy Black Hole (`Destroy Black Hole`) | The system is type Black Hole | The black hole's system abilities are removed. |
| Construct (`Create Constructed Planet` = PlanetSize `Special Ability ID`) | A star in the sector. Every `Constructed Planet Requirements` entry must be met: at least Val2 kT of components whose `Custom Group` equals Val1 must be present **in this sector** (not the whole system). | A ringworld or sphereworld is created around the star. The required materials are consumed **(inferred)**. |

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
  `Start Message Title N` / `Start Message N`, define the texts. Pick one at random
  **(inferred)**.
- `Picture` names the event image.
- `Time Till Completion`: when above 0, the event is timed. The start message is sent
  immediately, and the effect plus the final message follow after about that many turns.

Message placeholders: `[%SystemName]`, `[%SectorName]`, `[%SourceEmperorName]`,
`[%SourceEmpireName]`, `[%VehicleName]`, `[%VehicleSize]`, `[%PlanetName]`,
`[%DesignName]`, `[%TechName]`, `[%TreatyName]`, `[%FacilityName]`, `[%StarName]`,
`[%WarpPointName]`, `[%ActualAmount]`. `[%ActualAmount]` is not available in start
messages.

Proposed resolution **(inferred)**:

1. Each turn, for each empire, roll the frequency's percentage chance, modified by
   bad-event reducers.
2. On success, choose uniformly among the records whose severity is at most the maximum
   and that have a valid target the empire owns.
3. Apply the effect, or schedule it if the event is timed.

Bad-event reducers: the `Luck` racial trait (stock "Lucky" is −50%) and
`Change Bad Event Chance - System` (a system facility; only one per system counts). Luck
can also avert a star explosion.

Stock timed catastrophes include a star destruction on a long timer and a planet
destruction on a shorter one.

---

## 11. Victory conditions

Setup offers these; any number can be enabled, each with its own value:

1. **Score threshold.** The first empire to reach score X wins. Default 50,000.
2. **Years elapsed.** After N years the game ends and the highest score wins.
3. **Lead over second place.** An empire whose score is at least P% of the second-place
   score wins. The minimum P is 100.
4. **Research share.** An empire that has researched at least P% of all tech (areas or
   levels; the sources disagree) wins.
5. **Quadrant at peace** for N consecutive years. The winner is undefined; see §14.
6. **Qualifier.** Victory checks are suppressed until year N. This is not a condition by
   itself.

With nothing enabled, the game runs until one empire remains. Evaluate at the end of
turn processing, including in simultaneous games, where the history notes the check
could be skipped. The status window shows a grid of conditions against empires, in
pages of ten, up to 20 empires. Active conditions are highlighted, and an X marks each
empire that has met one.

---

## 12. Maps and scenarios on disk

- **Maps/.** Empty in this install. Map files come from Save Map (File menu, or the
  Quadrant tab) and from the separate map editor. We found no text-format map, so treat
  the format as ours to define.
  - A map should hold the systems with positions, names and type, every object with its
    SectType, abilities and name, the warp links, and optional **starting points**.
  - A *specific* starting point belongs to one player slot. A *common* one can take
    anyone.
  - Placement order: a player's specific point first, then the next free common point,
    then random placement. A player starting on a planet has that planet converted to
    their atmosphere at the same size.
  - Loading a map must clear the previous starting points.
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
  atmosphere, conditions, three values and ruins. A warp point adds its link, one-way
  flag and size.
- **Link**: the two (system, object) ends.
- **Per-empire knowledge**: explored systems, known links, seen warp points, a last-seen
  snapshot of each system, claims, avoid list and notes.
- **Victory config**: which of the six conditions are on, and their values.

---

## 14. Open questions to verify in the running game

1. **System grid.** Is it 13×13 (header: 0..12) or 14×14 (manual: 196 sectors)? Which
   sectors form the warp-point "edge"? Do the rings and movement use Chebyshev distance?
2. **Galaxy grid.** What are the Quadrant Size choices, their system counts, and the
   galaxy grid dimensions?
3. **Placement.** What do Clusters, Spiral and Diffuse produce? Screenshot several
   generations of each.
4. **Warp placement.** Does edge position follow the galaxy bearing to the destination,
   and is Min Angle measured on galaxy bearings?
5. **Ability roll.** Is it exclusive or independent per entry? Does any natural storm
   carry two abilities?
6. **Unstable stars.** Stock Val1 is 0. Do they ever explode on their own, or only
   through the Star - Destroyed event? Can that event hit stable stars?
7. **Events.** Is the roll per empire or global? How is the target chosen? How much does
   the timer vary? Which frequency options exist (is there "None")?
8. **Obscuration.** Do cloak and storm combine by max or by sum? Do storms and nebulae
   hide warp points and asteroids? Can observers inside the same storm see each other?
9. **Omnipresent view.** Does it reveal cloaked objects? The manual and the history
   disagree.
10. **Presence.** Do mines, satellites, fighters or drones grant presence and sensors?
    Do unpopulated owned planets?
11. **Black holes.** In what order do the pull and centre damage apply? Is the centre
    damage lethal outright? Do pulled or randomly moved ships stop at warp points or the
    grid edge?
12. **Turbulence.** Does it hit departures, arrivals or both?
13. **Homeworld.** Which planet is chosen, with what size and conditions? What happens in
    a start-eligible system with no planets?
14. **Names.** How are planets numbered? How are stars and warp points named in
    multi-star systems? What happens when system names run out?
15. **Conditions.** What is the full label scale, and the generation distribution?
16. **Victory.** What are each condition's min, max and default? Is research share by
    areas or levels? Who wins "at peace"? How are ties broken? What exactly eliminates an
    empire?
17. **Starting year.** Commonly remembered as 2400; not in our sources.
18. **Option lists.** What choices exist for Starting Resources, Racial Points, Tech
    Cost, Tech Level (is there a Medium?), AI Difficulty, AI Bonus, Score Display and
    Autosave? Until observed, our Game Setup offers free numbers for resources and racial
    points, cost growth of 0/50/100/150/200 % per level, difficulty Easy to Expert (0-3)
    and bonus None to High (0-3) **(inferred)**.
19. **Manipulation aftermath.** What does a system look like after Destroy Star? Which
    SystemType do created nebulae and black holes use? Does Destroy Planet kill the
    colony's population and facilities?
20. **Scenic systems.** Are the zero-object types free of planets and without hidden
    effects?
21. **Warp variants.** Do one-way or small warp points ever appear in generated or
    opened links?
22. **Warp cost.** Is a jump 1 movement point, and must the ship first stop on the warp
    point?
23. **One-way links.** The engine lets ships leave through an unflagged end only when the far
    end is not flagged either. Which end does the `One-Way` flag mark?
24. **Learning links.** The engine treats a traversed link as known in both directions. Does
    travelling A to B also reveal where B's end leads?
25. **Hazard order.** The engine applies black-hole pull and random drift to ships, fighters and
    drones after the 30 movement phases, stops drift at the grid edge, then applies centre
    damage and summed sector damage. Ships displaced next to hostiles are offered to combat.
26. **Sensors.** The engine counts sensor facilities only on populated colonies, while every
    colony (even an empty one) and every vehicle, units included, gives presence.
