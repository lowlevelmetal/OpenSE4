# The galaxy: planet sizes, stellar objects, systems and quadrants

Five tables decide what a generated galaxy holds. A quadrant type chooses how systems are
laid out and which system types they get; a system type lists the objects a system holds
and where they sit; the sector types are the catalogue of concrete objects (each planet,
star, storm or warp point appearance) that those objects are drawn from; the stellar
ability types are the dice that give objects their hazards and ruins; and the planet sizes
give each planet its room for facilities, population and cargo.

| Data file | Patch table | Records named by |
|---|---|---|
| PlanetSize.txt | `planet_sizes` | `Name` |
| SectType.txt | `sector_types` | no name: choose with `match` or `index` |
| StellarAbilityTypes.txt | `stellar_ability_types` | `Name` |
| SystemTypes.txt | `system_types` | `Name` |
| QuadrantTypes.txt | `quadrant_types` | `Name` |

Patches and their operations are described in
[packages-and-data.md](../../packages-and-data.md#data-patches). The abilities that system
types and stellar ability types hand out are described in [abilities.md](abilities.md); the
Settings.txt keys this chapter mentions (planet value ranges, the number of systems) in
[settings.md](settings.md).

Throughout this chapter `R[a,b]` is a uniform random whole number from a to b, both
included, as in the specs. A system is a grid of 13 × 13 sectors, x and y from 0 to 12,
with the centre at (6, 6); x grows to the right and y downward.

## The fields

### PlanetSize.txt (`planet_sizes`)

One record per size of planet or asteroid field. Natural sizes are named after their
stellar size (Tiny to Huge); a constructed world has a name of its own.

| Field | What it does | Values |
|---|---|---|
| `Name` | The size's name. A sector type's `Planet Size` names it, and so do the computer players' planet types (`Minimum Planet Size for Type`). | Text. Required. |
| `Physical Type` | Which kind of object uses the record: a planet or an asteroid field. Records are known by their physical type and name together: generation, and everything that reads an object's size, look a `Planet Size` up among the records of the object's own kind first. | `Planet` or `Asteroids`. Required. |
| `Stellar Size` | The size category that generation filters (`Obj Size`), homeworld sizes and `Create Planet Size` compare. | `Tiny`, `Small`, `Medium`, `Large` or `Huge`. A constructed world is normally `Huge`. Required. |
| `Max Facilities` | Facility slots of a colony whose races all breathe the planet's air. | Whole number. Required. |
| `Max Population` | The most population such a colony holds. | Millions (M). Required. |
| `Max Cargo Spaces` | Cargo space of such a colony, before `Cargo Storage`. | kT. Required. |
| `Max Facilities Domed`, `Max Population Domed`, `Max Cargo Spaces Domed` | The same three for a domed colony: one where any resident race cannot breathe the air. | As above. Optional: OpenSE4 reads a missing one as 0. |
| `Constructed` | True for a manufactured world (a ringworld or sphereworld). Generation never makes one; the Construct stellar manipulation does. | `True`/`False`. Optional, default `False`. |
| `Special Ability ID` | The number a `Create Constructed Planet` ability's `Val 1` names to build this world. | Whole number; give each constructed world its own. Optional, default 0. |

Asteroid records have capacities too, but no rule uses them: an asteroid field can never be
colonized.

### SectType.txt (`sector_types`)

Each record is one concrete appearance of an object: a picture with its description and the
attributes generation filters on. Records have no name: a patch chooses them with `match`
or `index`, and the game keeps them by position (an object stores the position of its
record, and saved games do too).

| Field | What it does | Values |
|---|---|---|
| `Physical Type` | The kind of object the record describes. Records of any other kind are never used. | `Planet`, `Asteroids`, `Star` (or `Sun`), `Storm`, `Warp Point`, `Destroyed Star`, `Comet`. Required. |
| `Picture Num` | The object's pictures: the cell of that number in `Pictures/Planets/Planets.bmp` (36×36 cells counted from 0, row by row) and the portrait `Pictures/Planets/pNNNN.bmp` numbered `Picture Num` + 1 (`p0001.bmp` for 0). Several records may share a picture. | Whole number from 0. Required. |
| `Description` | The text of the object's report. | Text. |
| `Planet Size` | Planets and asteroid fields: the name of a PlanetSize record, which gives the object its stellar size and its colony capacities. | A PlanetSize `Name`. |
| `Planet Physical Type` | Planets and asteroid fields: the surface. Colonizing it needs the matching `Colonize Planet - ...` ability. | `Rock`, `Ice` or `Gas Giant` (`Gas` is read as `Gas Giant`). |
| `Planet Atmosphere` | Planets and asteroid fields: the air. A race breathes exactly one gas; any other atmosphere (or none) means a domed colony. | `None`, `Methane`, `Oxygen`, `Hydrogen`, `Carbon Dioxide`. |
| `Star Size` | Stars and destroyed stars: compared with a template's `Obj Size`. | `Tiny` to `Huge`. |
| `Star Age`, `Star Color`, `Star Luminosity` | Stars and destroyed stars: compared with the template's `Obj Age`, `Obj Color` and `Obj Luminosity`, and shown in the star's report. No other rule reads them. | Age `Young`, `Average`, `Old`, `Ancient`; colour `Yellow`, `Red`, `Purple`, `Green`, `Blue`, `White`, `Orange`; luminosity `Dim`, `Average`, `Bright`, `Super Bright`. |
| `Storm Size` | Storms: compared with the template's `Obj Size`. | `Tiny` to `Huge`. |
| `Combat Tile` | Storms and asteroid fields: the tiles that fill a combat map in their sector, `Pictures/Systems/<Combat Tile>Tile1.bmp`, `...Tile2.bmp` and so on (72×72). | A base name. |
| `Warp Point Size` | Read and shown to scripts; no rule uses it. | `Small` or `Large`. |
| `Warp Point One-Way` | Read but never used: every warp point link works both ways. | `True`/`False`. |
| `Unusual` | Warp points: an appearance for warp points that rolled an ability (see "Warp points" below). | `True`/`False`. |

The original reads at most 1000 records of this file; OpenSE4 does not enforce a limit.

### StellarAbilityTypes.txt (`stellar_ability_types`)

A stellar ability type is a table of chances. Each object a system type creates names one,
and generation rolls it once for that object.

| Field | What it does | Values |
|---|---|---|
| `Name` | Named by system types' `Obj Stellar Abil Type` and `WP Stellar Abil Type`. | Text. Required. |
| `Number of Poss Abilities` | The number of entries. | 0 to 100. Required. |

The entries form the list `abilities` (each entry: `Ability Chance`, `Ability Type`,
`Ability Descr`, `Ability Val 1`, `Ability Val 2`). A patch names an entry to remove by its
`Ability Type` (every entry with that type goes) or its position, and an entry it adds needs
at least `Ability Type`.

| Entry field | What it does | Values |
|---|---|---|
| `Ability Chance` | The entry's chance of being the one granted. | Tenths of a percent: 0 to 1000 (1000 is certain). Required. |
| `Ability Type` | The ability granted. `None` grants nothing; it only takes its share of the roll. | An ability name or `None`. Required. |
| `Ability Descr` | The text shown with the ability. | Text. |
| `Ability Val 1`, `Ability Val 2` | The ability's values ([abilities.md](abilities.md)). | As the ability needs. |

### SystemTypes.txt (`system_types`)

A system type is a template: a backdrop, system-wide abilities, and a list of objects with
where they go and what they may be.

| Field | What it does | Values |
|---|---|---|
| `Name` | Named by quadrant types' `Type Name`. If two records share a name, the last one is used. | Text. Required. |
| `Description` | The text of the System Report. | Text. |
| `System Physical Type` | What the stellar manipulations check: Create Star refuses a `Nebulae` or `Black Hole` system, Destroy Nebulae and Destroy Black Hole need one. It has no effect of its own: a nebula hides ships because of its `Sector - Sight Obscuration` ability, not its type. | `Normal`, `Nebulae` or `Black Hole`. |
| `Background Bitmap` | The system panel's backdrop, `Pictures/Systems/1024X768/<name>.bmp` and `Pictures/Systems/800X600/<name>.bmp` (".bmp" is added when missing), and the 128×128 `Pictures/Systems/<name>.bmp` of the System Report. | A picture's base name. |
| `Mask Background Objs` | `True`: the system's objects are drawn with black transparent over the backdrop, and combat in the system uses the tiles `Pictures/Systems/<Background Bitmap>TileN.bmp` when they exist. | `True`/`False`. |
| `Non-Tiled Center Pic` | `True`: the Stellar Manipulation window's preview shows the plain star field instead of the backdrop. Nothing else reads it. | `True`/`False`. |
| `Empires Can Start In` | Whether homeworlds and extra starting planets may be placed in systems of this type. | `True`/`False`. Default `False`. |
| `Number of Abilities` | The number of system-wide abilities. | 0 to 20; the game reads no more than 20. |
| `WP Stellar Abil Type` | The stellar ability type rolled for each warp point link to a higher-numbered system; a link to a lower-numbered one copies that system's roll (see "Warp points"). | A stellar ability type's `Name`, or `None`. |
| `Number of System Objs` | The number of object entries. 0 is legal: such a system holds only its warp points. | Whole number. Required. |

The system-wide abilities form the list `abilities` (`Ability Type`, `Ability Descr`,
`Ability Val 1`, `Ability Val 2`). They are always present, with no roll, and apply to every
sector of the system; "How the fields work together" lists the ones that work there.

The objects form the list `objects`. A patch names an entry to remove by its
`Obj Physical Type`, which removes every entry of that kind; give a position (`2`) or a
table of fields (`{ "Obj Position" = "Ring 3" }`) to remove one. An entry a patch adds
needs at least `Obj Physical Type`.

| Entry field | What it does | Values |
|---|---|---|
| `Obj Physical Type` | The kind of object. `Comet` and `Warp Point` entries create nothing (below). | `Planet`, `Asteroids`, `Star` (or `Sun`), `Storm`, `Destroyed Star`, `Comet`, `Warp Point`. Required. |
| `Obj Position` | Where it goes: one of the position specifiers in the next table. | Text. Required. |
| `Obj Stellar Abil Type` | The stellar ability type rolled for this object. | A stellar ability type's `Name`, or `None`. |
| `Obj Size` | Planets and asteroid fields: the stellar size of the PlanetSize record the sector type names. Stars: `Star Size`. Storms: `Storm Size`. | `Any`, or `Tiny` to `Huge`. |
| `Obj Atmosphere` | Planets and asteroid fields: the sector type's `Planet Atmosphere`. | `Any`, or an atmosphere. |
| `Obj Composition` | Planets and asteroid fields: the sector type's `Planet Physical Type`. | `Any`, `Rock`, `Ice`, `Gas Giant` (or `Gas`). |
| `Obj Age`, `Obj Color`, `Obj Luminosity` | Stars: the sector type's `Star Age`, `Star Color`, `Star Luminosity`. | `Any`, or a value. |

A missing constraint counts as `Any`.

Position specifiers. The specifier is recognised by the word it contains, tried in this
order: `Ring`, `Coord`, `Same`, `Circle Radius`. "Taken" means a sector an earlier entry of
the same template went to.

| Specifier | Sector |
|---|---|
| `Ring 1` | The centre (6, 6). |
| `Ring 2` to `Ring 7` | A random sector on the square ring k − 1 sectors from the centre (k is the number), redrawn while taken, up to 101 draws (then the last draw stands). Corners are twice as likely as other sectors of the ring. `Ring 7` is the outer edge. |
| `Ring 0` | A random sector of the inner 11 × 11 square (x and y from 1 to 11), redrawn as for a ring. |
| `Ring 8`, `Ring 9` | Not defined in the original; OpenSE4 uses `Ring 7` and warns. |
| `Circle Radius R` | A random free sector whose distance from the centre, √(dx² + dy²) with the fraction dropped, is R. With none free, sector (0, 0). |
| `Coord X,Y` | Exactly sector (X, Y), with no check that it is free. |
| `Same As N` | The sector of entry N of this template (moons). If entry N is not placed yet, sector (0, 0). |
| anything else | Not defined in the original; OpenSE4 places the object on a random sector and warns. |

### QuadrantTypes.txt (`quadrant_types`)

A quadrant type is the choice the player makes in Game Setup's Quadrant Type list.

| Field | What it does | Values |
|---|---|---|
| `Name`, `Description` | Shown in Game Setup. | Text. `Name` required. |
| `Min Dist Between Systems` | D: a new system is refused when an earlier one is within D squares on both axes, so systems end at least D + 1 squares apart (counting diagonal steps as one). One square of the quadrant map is about 10 light years. | Whole number of squares; a negative value counts as 0. Default 0. |
| `System Placement` | How systems are laid out on the 67 × 46 square quadrant. | `Random`, `Diffuse` (as Random, with D raised by 2), `Grid` (a 13 × 9 lattice with a spacing of 5), `Clusters`, `Spiral`. Required. OpenSE4 reads an unknown value as `Random` and warns. |
| `Max Warp Points per Sys` | K: how many of the nearest systems each system considers linking to; half as many (at least 1) when *All Warp Points connected* is off. Despite its name it is not a cap: a system may end with more links, up to the hard limit of 10. | Whole number, 1 or more in practice. |
| `Min Angle Between WP` | Two links leaving one system must point at least this far apart (bearings on the quadrant map). The pass that connects isolated systems ignores it. | Degrees, 0 to 360. |
| `Number of System Types` | The number of entries in the list below. | Whole number. Required. |

The system types form the list `system_types` (`Type Name`, `Type Chance`). A patch names an
entry to remove by its `Type Name`.

| Entry field | What it does | Values |
|---|---|---|
| `Type Name` | A system type. It must exist: an unknown name is a data error. | A SystemTypes `Name`. Required. |
| `Type Chance` | Its weight when a system's type is rolled. | Tenths of a percent; the entries should add up to 1000. Required. |

## How the fields work together

### How a galaxy is made

OpenSE4 generates a quadrant in the original's order (spec 01 §3.7):

1. **The number of systems.** With M = the Settings key `Maximum Number Of Systems` (at
   most 255) and q = M div 5, a Small quadrant has q to 2q − 1 systems, a Medium one 2q to
   4q − 1, a Large one 4q to 5q − 1.
2. **Names** from SystemNames.txt.
3. **Placement** by the quadrant type's `System Placement` and `Min Dist Between Systems`.
   Each system gets up to 1,001 tries; when none fits, the quadrant keeps the systems placed
   so far. A tight minimum distance, or `Grid` (117 places), can therefore give fewer
   systems than the size asks for.
4. **Warp links**, from `Max Warp Points per Sys` and `Min Angle Between WP`, then (with
   *All Warp Points connected*, on by default) a pass that links every isolated group to
   the rest.
5. **Each system in turn:** its system type is rolled from the quadrant's list; its object
   entries are placed in template order, each with a sector type drawn for it, its stellar
   ability rolled, and a planet's or asteroid field's values and conditions rolled; then its
   warp points are placed.
6. **Empires** are placed in start-eligible systems, and their starting planets set up.

A change to these tables changes only galaxies generated afterwards: a game in progress
keeps its galaxy.

### Choosing a system type

For each system, r = R[1,1000]. The entries of the quadrant's list are walked in order, adding
up their chances; the first entry whose share contains r is chosen. When the chances add up
to 1000, each entry is chosen with probability `Type Chance` / 1000: with three invented
entries of 600, 300 and 100, about 60 %, 30 % and 10 % of the systems get each type.

- When the chances add up to **less** than 1000, the walk wraps round to the first entry, so
  the list repeats: the shares stay roughly proportional, with early entries slightly
  favoured.
- When they add up to **more**, entries past a running total of 1000 are never chosen.
- When they are all 0, every system gets the first record of SystemTypes.txt.

### Placing and drawing the objects

The entries of the template are taken in order. Each gets its sector (the position
specifiers above), then a sector type:

- The candidates are the SectType records of the entry's physical type, leaving out planet
  and asteroid records whose PlanetSize is `Constructed`.
- When the entry's `Obj Size` **and** `Obj Atmosphere` are both `Any`, every candidate is
  equally likely and the other constraints (composition, age, colour, luminosity) are
  ignored. This is a quirk of the original. A star entry has no atmosphere, so to choose a
  star's colour, age or luminosity, give its `Obj Size` too.
- Otherwise every constraint that is not `Any` must match, and the record is drawn
  uniformly among the matches.

So the catalogue is also a weighting table: how often an attribute turns up follows how
many records carry it. Add two more ice planet records and ice worlds become more common
wherever a template leaves the composition open.

When nothing matches, the original has no defined result. OpenSE4 relaxes the constraints
in steps (first atmosphere, age and luminosity, then composition and colour, then size)
and warns; when the data has no record of that kind at all, the object is left out.

A `Comet` or `Warp Point` entry creates nothing, but it is still placed: its sector counts
as taken for later entries and for `Same As`, and a record is drawn for it, so it changes
the random numbers of what follows.

Names: every star is named after its system plus the word for star; every storm is "Storm";
asteroid fields are numbered on their own ("<system> Asteroid Belt I", "... II"); a planet
alone in its sector takes the system's name and the next Roman numeral, and a planet that
shares its sector with an earlier entry (a moon placed with `Same As`) takes the first
entry's name and a letter.

### The ability roll

Each object whose entry names a stellar ability type rolls it once: r = R[1,1000], and the
entries are walked adding up their chances; the first entry whose running total reaches r
is granted. So each entry is granted with probability `Ability Chance` / 1000, an object gets
**at most one** rolled ability, and when r lies past the total, it gets none. With two
invented entries of 50 and 30, 5 % of the objects get the first ability, 3 % the second and
92 % nothing; a single entry of 1000 is granted every time.

With the game option *No Ruins*, a rolled `Ancient Ruins` or `Ancient Ruins Unique` is thrown
away (the roll still happens). Starting planets lose any ruins.

What a rolled ability does depends on the object ([abilities.md](abilities.md), spec 01
§5.3). In short: on a storm, `Sector - Damage` hurts ships that move into its sector
(a 50 % chance per step that every ship of the group takes Val 1 damage, and a group that
is hit stops for the turn; ships that stay there are not harmed), and `Sector - Sight
Obscuration`, `Sector - Shield Disruption` and `Sector - Sensor Interference` work in its
sector. A planet's or asteroid field's own `Sector - Sight Obscuration` hides its sector;
stars, warp points and comets never hide anything. A planet's `Ancient Ruins` (Val 1 = a
number of random tech advances) or `Ancient Ruins Unique` (Val 1 = a unique tech area
number) is used up when the planet is colonized. Once a planet is colonized, OpenSE4 counts
its own abilities with its facilities' (spec 02 §2 names `Cargo Storage`).

### Warp points

Every link is a pair of warp points, one in each system, placed on the edge of the system
facing the other. The end made first, in the lower-numbered system, rolls the ability from
**its own** system type's `WP Stellar Abil Type`; the other end copies that ability and that
appearance. So both ends always match, and the far system's `WP Stellar Abil Type` does not
matter for that link.

The appearance is the **first** `Warp Point` record whose `Unusual` is `False`; a link that
rolled an ability takes a random `Warp Point` record instead, redrawn up to 100 times until
an `Unusual` one comes up. Warp points opened by the Open Warp Point manipulation use the
first plain record and carry no ability. Keep at least one plain and one unusual warp point
record.

`Warp Point - Turbulence` (Val 1 damage) gives a ship group that jumps through a 50 %
chance that every ship takes that much damage; a group that is hit still arrives, but stops
for the turn.

### Where empires start

Homeworlds may only be placed in systems whose type has `Empires Can Start In` `True`
(spec 01 §3.6):

- The **home size** is Small, Medium or Large for the Home Planet Value Bad, Average or
  Good, raised to the smallest size that any natural Planet sector type with the race's
  atmosphere and physical type has.
- The homeworld is an existing planet of the race's atmosphere and physical type (and, with
  *All player planets the same size*, the home size), in a start-eligible system, spread
  out from earlier homes when *Evenly distributed* is on. No planet is converted.
- When no planet fits, a new one is created in a random start-eligible system, from a
  Planet record with the race's atmosphere and type.
- Extra starting planets come from the home system and its neighbours, again only in
  start-eligible systems.

So a race whose atmosphere and planet type have no natural Planet record, or have them only
in sizes the start-eligible templates never ask for, gets created homeworlds. OpenSE4 then
falls back to another size or any planet record and gives it the race's air and surface
(an OpenSE4 choice).

### Planet values, conditions and atmospheres

- **Surface, atmosphere and size** come from the sector type drawn for the planet, so they
  follow the template's constraints and the catalogue's weights.
- **Values.** Each of the three resource values (minerals, organics, radioactives) is rolled
  separately and uniformly between the Settings keys `Planet Value Low Percent` and
  `Planet Value High Percent` (asteroid fields: `Asteroids Value Low/High Percent`; in a
  Finite Resources game the `... Resources` keys, as a stock that production uses up). In a
  normal game a value is a percentage that multiplies the planet's output.
- **Conditions** are R[0,10] / 10 + 0.5: one of 0.5, 0.6, ... 1.5, each equally likely.
  An asteroid field gets half that. The bands are Deadly below 0.3, Harsh to 0.5,
  Unpleasant to 1.0, Mild to 1.3, Good to 1.5 and Optimal at 1.5; they change population
  growth only (spec 02 §2).
- **Starting planets** take the Settings key `Plr Planet Value {Low,Medium,High} Percent`
  for the Home Planet Value chosen, plus a spread of −4 to +5 per resource (the
  `... Resources` key exactly, in a Finite Resources game). Their conditions stay as
  generated.
- **Later changes** come from facilities, events (`Planet - Conditions Change`,
  `Planet - Value Change`, [events.md](events.md)) and `Planet - Change Atmosphere`.

### Colony capacity

For a colony on a planet whose PlanetSize record is P:

- facility slots = P's `Max Facilities` (or `Max Facilities Domed`),
- maximum population = P's `Max Population` (or `Max Population Domed`), in millions,
- cargo = P's `Max Cargo Spaces` (or `Max Cargo Spaces Domed`) plus the `Cargo Storage` of
  the planet and its facilities, in kT,

each then multiplied by (100 + V) / 100 and truncated when the race has the `Planet Storage
Space` trait with value V. The domed column is used while any race living there cannot
breathe the atmosphere (with no population, while the owner's race cannot). A colony whose
capacity shrinks keeps the facilities and population it has but cannot add more (spec 02
§2). The game option *Only breathable atmosphere* forbids domed colonies.

Constructed worlds: the Construct manipulation needs a star in the sector and a PlanetSize
record with `Constructed` `True` and `Special Ability ID` equal to the ability's Val 1. The
new world uses a Planet sector type whose `Planet Size` names that record (preferring one
with the builder's surface and air); its values are set to `Planet Value High Percent` (or
`... High Resources`), its conditions to 1.5, and the star is used up. Without a Planet
sector type of that size, no planet is made.

Two manipulations read planet sizes differently: `Create Planet Size` compares **stellar
sizes** (Tiny 1 to Huge 5), while `Destroy Planet Size` compares the planet's PlanetSize
**record position** in the file, counted from 1. Inserting a record before others changes
what every planet destroyer can destroy.

### System-wide abilities

These work in a system type's `abilities` list (spec 01 §4.4):

| Ability | Effect | Val 1 |
|---|---|---|
| `Sector - Sight Obscuration` | Raises the obscuration of planets, asteroid fields, ships, unit groups and comets in every sector, in all five sight types. This is what makes a nebula. | Obscuration level. |
| `Sector - Shield Disruption` | Shield points lost in combat anywhere in the system. | Shield points. |
| `System - Movement Towards Center` | Each turn every ship, base and unit group takes this many steps toward the centre. | Steps per turn. |
| `System - Movement Random` | Each turn every ship, base and unit group takes this many steps toward one random sector. | Steps per turn. |
| `System - Destructive Center` | Each turn every ship, base and unit group in the centre sector takes this much damage. | Damage per turn. |
| `Sector - Damage`, `Sector - Sensor Interference` | As on a storm, added to each sector's own value. | As on objects. |

Several abilities of one kind add up, except sight obscuration, where the largest value
counts. The three movement and damage effects happen once per turn, in the event step at
the end of the turn. `System - Sensor Interference`, `System - Damage` and
`System - Ability Required` load with a warning and have no effect.

## Changing them with patches

### A new kind of system

This patch adds a stellar ability type and a system type written out in full, then gives the
new type a place in an existing quadrant type. All names and numbers are invented.

```toml
# data/cinder-drift.toml
[[stellar_ability_types.add]]
name = "Cinder Hazards"
add.abilities = [
  { "Ability Chance" = 600, "Ability Type" = "Sector - Damage", "Ability Descr" = "Hot cinders scour ships that enter.", "Ability Val 1" = 15 },
  { "Ability Chance" = 400, "Ability Type" = "Sector - Sight Obscuration", "Ability Descr" = "Glowing dust hides the sector.", "Ability Val 1" = 3 },
]

[[system_types.add]]
name = "Cinder Drift"
add.objects = [
  { "Obj Physical Type" = "Star", "Obj Position" = "Ring 1", "Obj Stellar Abil Type" = "None", "Obj Size" = "Small", "Obj Age" = "Old", "Obj Color" = "Red", "Obj Luminosity" = "Dim" },
  { "Obj Physical Type" = "Planet", "Obj Position" = "Ring 3", "Obj Stellar Abil Type" = "None", "Obj Size" = "Any", "Obj Atmosphere" = "None", "Obj Composition" = "Rock" },
  { "Obj Physical Type" = "Asteroids", "Obj Position" = "Ring 5", "Obj Stellar Abil Type" = "Cinder Hazards", "Obj Size" = "Any", "Obj Atmosphere" = "Any", "Obj Composition" = "Any" },
  { "Obj Physical Type" = "Storm", "Obj Position" = "Circle Radius 4", "Obj Stellar Abil Type" = "Cinder Hazards", "Obj Size" = "Any" },
]

[system_types.add.set]
"Description" = "A dim red star wrapped in drifting cinders."
"System Physical Type" = "Normal"
"Background Bitmap" = "<a backdrop of your data set>"
"Empires Can Start In" = false
"Mask Background Objs" = false
"Number of Abilities" = 0
"WP Stellar Abil Type" = "None"

[[quadrant_types.change]]
name = "<a quadrant type of your data set>"
remove = { system_types = ["<a system type in its list>"] }
add.system_types = [
  { "Type Name" = "<the same system type>", "Type Chance" = 250 },   # its old chance, less 50
  { "Type Name" = "Cinder Drift", "Type Chance" = 50 },
]
```

- The star entry gives a size, so its colour, age and luminosity count; with `Obj Size` =
  `Any` they would be ignored. If your catalogue has no star record with all four, OpenSE4
  relaxes the constraints and warns.
- The planet entry asks for an airless rock of any size; the asteroid field and the storm
  each roll `Cinder Hazards` and get exactly one of its two abilities (the chances add up
  to 1000).
- The quadrant's chances still add up to 1000, so about 5 % of its systems become Cinder
  Drift. Removing and adding the entry again moves it to the end of the list, which does
  not matter while the chances add up to 1000. An existing entry's chance can also be set
  directly when you know its position: `set = { "Type 3 Chance" = 250 }`.
- `opense4-sdk check` reports an `Obj Stellar Abil Type` or `Type Name` that does not exist,
  and an unknown ability.

### Planet sizes and a constructed world

```toml
# data/lattice-shell.toml
# More room under domes on medium planets. Name alone would also match a medium
# asteroid record, so match on the physical type as well.
[[planet_sizes.change]]
match = { "Name" = "Medium", "Physical Type" = "Planet" }
set = { "Max Facilities Domed" = 5, "Max Population Domed" = 450 }

# A new constructed world, appended at the end so that no other record moves.
[[planet_sizes.add]]
name = "Lattice Shell"
set = { "Physical Type" = "Planet", "Stellar Size" = "Huge", "Max Facilities" = 90, "Max Population" = 24000, "Max Cargo Spaces" = 6000, "Max Facilities Domed" = 45, "Max Population Domed" = 12000, "Max Cargo Spaces Domed" = 3000, "Constructed" = true, "Special Ability ID" = 7 }

# Its appearance: a Planet record of that size.
[[sector_types.add]]
set = { "Physical Type" = "Planet", "Picture Num" = 12, "Description" = "Girders and soil wrapped round the light of a vanished star.", "Planet Size" = "Lattice Shell", "Planet Physical Type" = "Rock", "Planet Atmosphere" = "Oxygen" }
```

`Picture Num` 12 here stands for a picture your installed game already has; a mod that
brings its own needs the cell in `Planets.bmp` and the portrait `p0013.bmp`. To build the
world, a component needs `Create Constructed Planet` with Val 1 = 7, and the builders'
`Constructed Planet Requirements` ([components.md](components.md)). Because generation
leaves out records whose size is `Constructed`, the new sector type never appears in a
generated galaxy.

### Sector types, chances, and removing with cascade

```toml
# data/emberfall.toml
# Every medium storm fills its combat maps with the mod's tiles
# (assets/Pictures/Systems/EmberfallTile1.bmp, Tile2.bmp, ...).
[[sector_types.change]]
match = { "Physical Type" = "Storm", "Storm Size" = "Medium" }
all = true
set = { "Combat Tile" = "Emberfall" }

# A new storm appearance, copied from the record at position 31 of your SectType.txt.
[[sector_types.add]]
copy_from = 31
set = { "Description" = "A slow whirl of glowing ash.", "Storm Size" = "Large" }

# Rarer ruins: the second entry of a stellar ability type, set in place.
[[stellar_ability_types.change]]
name = "<a stellar ability type of your data set>"
set = { "Ability 2 Chance" = 5 }

# Remove a stellar ability type; system types that roll it get None instead.
[[stellar_ability_types.remove]]
name = "<another stellar ability type of your data set>"
cascade = true

# Remove a system type; quadrant types drop it from their lists.
[[system_types.remove]]
name = "<a system type of your data set>"
cascade = true
```

Without `cascade = true`, each place that still names a removed system type or stellar
ability type is an error that says where it is. With it, the quadrant's remaining chances
add up to less than 1000 and the list repeats (see "Choosing a system type"); add or raise
another entry if you want the shares exact. A removed planet size clears the computer
players' `Minimum Planet Size for Type` with `cascade`; sector types have no names, so
nothing refers to them and nothing is checked.

## Things to watch

- **Data errors** that `opense4-sdk check` reports, naming the file, record and patch:
  `unknown system type '...'` (a quadrant's `Type Name`), `unknown stellar ability type
  '...'` (a system type's `WP Stellar Abil Type` or `Obj N Stellar Abil Type`; `None` is
  fine), `unknown ability type '...'`, `missing field '...'` for the required fields above,
  and `'...' should be a whole number` or `should be True or False`. The ability names
  `System - Sensor Interference`, `System - Damage` and `System - Ability Required` give a
  warning (`has no effect`).
- **Patch selection.** Planet and asteroid records often share a `Name` (Small, Medium...):
  `name = "Small"` then matches both and is an error (`2 records of PlanetSize.txt match`).
  Use `match` with `Physical Type`. For the same reason `planet_sizes.add` refuses a name the
  file already has, even for the other physical type. `sector_types` takes no `name`, no
  `after` or `before` (a new record goes at the end), and `copy_from` takes a position.
- **Positions matter.** Saved games keep sector types and planet sizes by position, the
  first plain warp point record is the one every plain warp point uses, and `Destroy Planet
  Size` compares PlanetSize positions. Prefer appending to inserting.
- **Size and atmosphere both `Any`** makes generation ignore composition, age, colour and
  luminosity. A template that seems to ask for red stars or ice worlds may get any.
- **Chances.** Quadrant entries and stellar ability entries are in tenths of a percent. A
  stellar ability table that adds up to more than 1000 never grants its later entries; one
  that adds up to less leaves the rest of the objects without an ability.
- **Every race needs somewhere to live.** For each atmosphere and physical type the races
  use, keep natural Planet sector types in the home sizes (Small, Medium, Large), and
  start-eligible system types whose templates can produce them. A quadrant with no
  start-eligible type puts every homeworld in a created planet of a random system.
- **An empty quadrant list.** If `cascade` removes every entry of a quadrant type's list,
  starting a game with it fails with "Quadrant type '...' lists no system types."
- **Missing domed values** are read as 0: a domed colony on that size could hold nothing.
- **Planet sizes that sector types name.** OpenSE4 does not check that a sector type's
  `Planet Size` names an existing record. A planet whose size has no record falls back to a
  record of the same physical type whose `Stellar Size` has that name; failing that, its
  colony has no room at all. For a colony's capacity OpenSE4 takes the first record of the
  planet's size name, whatever its physical type, so keep a size's Planet record ahead of an
  Asteroids record of the same name.
- **Generation problems are not data errors.** Constraints that match no sector type,
  unknown position specifiers, `Ring 8` or `Ring 9`, an unknown `System Placement`, or a
  quadrant too crowded for its systems are worked around while the galaxy is made (the
  OpenSE4 choices above). `opense4-sdk check` does not generate a galaxy, so it does not
  report them: generate a few quadrants in Game Setup (Generate Map Now) or start a game
  with `opense4-sdk run` and look.
- **Physical type is not an effect.** A `Nebulae` system without `Sector - Sight
  Obscuration` hides nothing, and a `Black Hole` system without the movement and centre
  abilities pulls nothing. Created nebulae and black holes do not use a system type's
  abilities: the game sets their abilities itself (spec 01 §9).
- **The mod changes the game.** Every one of these tables is part of the data set's
  identity, so all players of a network game need the same mod, and games started without
  it do not load with it.

## More detail

- [Spec 01](../../../spec/01-galaxy-and-setup.md): §2.2 (the Quadrant tab and its options),
  §3 (quadrant generation: placement, system types, the warp network, empire placement, the
  pipeline), §4 (star systems, SystemTypes.txt, position specifiers, system-wide abilities),
  §5 (SectType.txt, the ability roll, object abilities, PlanetSize.txt, planet properties),
  §7 (hazards per turn), §8 (warp points), §9 (stellar manipulation).
- [Spec 02](../../../spec/02-empires-and-economy.md): §1.3 (PlanetSize.txt), §2 (planets,
  colonies, domes, capacities, conditions, value), §9 (setting up starting planets).
- [Spec 03](../../../spec/03-vehicles-and-abilities.md) §3.3 for every ability name.
- [packages-and-data.md](../../packages-and-data.md): "Data patches", "Lists in a record"
  and "Removing a record".
- The rules view that computer players read ([view.md](../../view.md#the-rules-view)):
  `planet_sizes` ([`planet_size`](../../view.md#planet_size)), `system_types`
  ([`system_type`](../../view.md#system_type)) and `sector_types`
  ([`sector_type`](../../view.md#sector_type)). Stellar ability types and quadrant types are
  not in it; the galaxy itself is seen through the view's `system` and `space_object`
  records.
- Sibling chapters: [abilities.md](abilities.md), [components.md](components.md) (the
  stellar manipulation components), [settings.md](settings.md) (planet value ranges, the
  number of systems), [ai-tables.md](ai-tables.md) (the computer players' planet types),
  [events.md](events.md).
