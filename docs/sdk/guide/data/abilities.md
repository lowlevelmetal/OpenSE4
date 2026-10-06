# Abilities

Abilities are what make the records of the data files do something. A component, facility,
hull, system type or stellar ability type carries a numbered list of ability entries; each
entry names one ability and gives it up to two values. There is no table of abilities to
patch: the names come from a fixed list built into OpenSE4 (the install's `Abilities.txt`
is a catalogue for readers, and OpenSE4 does not load it). Patches change ability entries
through the list `abilities` of the tables that carry them, and a mod adds new names with
`[[abilities.declare]]` (below).

## The fields of an entry

| Field | What it does | Values |
|---|---|---|
| `Number of Abilities` | How many entries the record has. | Whole number. At most 20 are read: a larger count is treated as 20. StellarAbilityTypes.txt counts its entries with `Number of Poss Abilities` instead, 0 to 100, without the limit of 20. |
| `Ability N Type` | The ability's name. | A name from the reference below, or one a mod declares. Names match in any letter case and with any run of spaces. An unknown name is an error; `None` or a blank name is an empty slot and is skipped. |
| `Ability N Descr` | The line reports show for this entry. No rule reads it. The text `[%ShieldPointsGenerated]` in it is replaced by the shield points the component makes with its mount. With no text, reports show the name and its values. | Text; optional. |
| `Ability N Val 1` | The first value. | A whole number, optionally signed. Blank reads as 0, and so does anything else that is not a whole number, without an error. `Cloak Level` and `Sensor Level` take a sight type here instead. |
| `Ability N Val 2` | The second value. | As `Val 1`. |
| `Ability N Chance` | StellarAbilityTypes.txt only: the chance that an object gets this entry. | 0 to 1000, in tenths of a percent; a record's chances should add up to 1000 at most ([galaxy.md](galaxy.md)). |

The sight types are `EM Active`, `EM Passive`, `Psychic`, `Gravitic` and `Temporal`.

A patch writes an entry with the data file's field names without the number:
`{ "Ability Type" = "Supply Storage", "Ability Descr" = "...", "Ability Val 1" = 500, "Ability Val 2" = 0 }`,
with `"Ability Chance"` as well in `stellar_ability_types`. An entry to remove is named by
its type (which removes every entry of that type), by its position from 1, or by a table of
fields that must all match ([packages-and-data.md](../../packages-and-data.md#lists-in-a-record)).

## Where abilities are carried

| Carrier | Data file, patch table | When its abilities count |
|---|---|---|
| Components | Components.txt, `components` ([components.md](components.md)) | While the component is working: a destroyed component gives nothing until it is repaired. |
| Hulls | VehicleSize.txt, `vehicle_sizes` ([hulls.md](hulls.md)) | Always, on every vehicle of the hull: a hull is never damaged. |
| Facilities | Facility.txt, `facilities` ([facilities.md](facilities.md)) | From the turn the facility is built until it is scrapped or destroyed. |
| System types | SystemTypes.txt, `system_types` ([galaxy.md](galaxy.md)) | Always, for the whole system, whoever is there. |
| Stellar ability types | StellarAbilityTypes.txt, `stellar_ability_types` ([galaxy.md](galaxy.md)) | When the galaxy is made, each object whose system type names a stellar ability type for it (`Obj N Stellar Abil Type`, `WP Stellar Abil Type`) draws at most one of that type's entries. The entry drawn becomes the object's own ability (a planet's, storm's, warp point's...) for the rest of the game. |

Racial traits carry no ability entries. They have trait types of their own, some of which
act on the same rules, such as `Vehicle Speed` on movement or `Planet Storage Space` on a
colony's slots ([races.md](races.md)).

Which list a rule reads decides where an ability works:

- **A vehicle** (ship, base or unit group): its hull and its working components. A
  mothballed vehicle has no abilities at all. A unit group lists each unit's abilities once
  per unit, so values that add up grow with the number of units.
- **A colony:** its facilities, and for some rules the planet's own drawn abilities too.
  Most facility abilities work without population; output needs population.
- **A system or a sector:** the system type's abilities and the drawn abilities of the
  objects there, and for the "- System" abilities the empire's colonies and ships in the
  system.
- **Orders.** Some abilities act only through an order: the colonise abilities (Colonize),
  `Emergency Energy` and `Emergency Resupply` (Use Component), the stellar-manipulation
  abilities (Stellar Manipulation), `Cloak Level` of ships and bases (Cloak) and
  `Self-Destruct`.
- **Design rules** ([hulls.md](hulls.md)) count components, not entries: a component counts
  once for an ability however many entries of it it has, and rules based on size add the
  component's size with its mount.

The reference below says for each ability which carriers it works on when that is not
obvious.

## How several entries combine

When a vehicle, colony or system has several entries of one ability, OpenSE4 combines them
in one fixed way per ability (spec 03 §3.2). The rules view names the way in its
[`aggregation`](../../view.md#aggregation) values:

| Mode | Result |
|---|---|
| `sum` | All the values added up, at most 2,000,000,000. |
| `largest` | The largest value, starting from 0: with no entry, or only negative ones, the result is 0. A negative value of such an ability never takes effect. |
| `smallest` | The smallest Val 1, or 0 when there is none. (A smallest value of exactly 99,999 also reads as 0, as in the original.) |
| `count` | How many entries there are. |
| `present` | Whether there is at least one. |
| `per_sight_type` | For each sight type, the largest Val 2 among the entries whose Val 1 names that type. |
| `first_per_id` | Entries grouped by Val 2; each group gives the Val 1 of its first entry in list order (later, larger ones are ignored); the groups' values are added up. |
| `per_family` | Entries from the hull add in full. Entries from components are grouped by the component's `Family`; each family gives its largest value; the families' values are added up. |
| `unspecified` | The rule that reads the ability combines it its own way (the reference says how). |

On top of the mode, many colony and system abilities have a **scope**: a colony's own
facilities ("per colony"), the largest value among the empire's colonies and ships in the
system ("largest in the system"), or the empire's objects in one sector ("per sector").

## The abilities

V1 and V2 stand for `Val 1` and `Val 2`; a dash means the value is not read. Where a unit
matters it is given: MP are movement points, kT kilotons of space, "a year" is 10 turns.

### Engines and movement

A ship's movement points per turn come from its engines (spec 03 §6.1):

```text
E    = sum of Standard Ship Movement V1
base = E ÷ the hull's Engines Per Move            (whole division; 0 if that field is 0)
B    = smallest Movement Bonus V1 + Extra Movement Generation (first per id)
if base > 0 and B < 100: base = base + B
if base > 0:             base = base + the racial Vehicle Speed total
```

Then, at 0 supply the result is exactly 1, and unless the ship has a working
`Master Computer` it is halved (whole division, at least 1) once for each of these that it
has none of: a bridge or auxiliary control, crew quarters, life support.

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Standard Ship Movement` | V1: movement it gives | sum | Marks a component as an engine and gives its share of movement, as above. Engines are counted against the hull's `Requirement Uses Engines` and `Requirement Max Engines`. |
| `Movement Bonus` | V1: MP | smallest | Added to the engines' movement. The smallest value on the vehicle counts: a part with 0 cancels a larger bonus elsewhere, while a part without the ability is not counted at all. |
| `Extra Movement Generation` | V1: MP; V2: a stacking id | first_per_id | Added with `Movement Bonus`. Entries with the same V2 do not stack; entries with different V2 do. Hulls carry it too. |
| `Emergency Energy` | V1: MP | per use | Use Component on a ship or base: in a turn-based game adds V1 movement points to this turn; in a simultaneous game V1 extra days of action. The ship needs a maximum above 0. |
| `Combat Movement` | V1: squares | largest | Added to the ship's movement in battle, which is half its map speed rounded up. |

Every step and warp costs the `Supply Amount Used` of the working components whose
`Standard Ship Movement`, `Movement Bonus` or `Extra Movement Generation` is above 0
(spec 03 §7).

### Control and crew

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Ship Bridge` | – | present | The control component. A hull with `Requirement Must Have Bridge` needs exactly one. The cores of fighters, troops, satellites, drones and weapon platforms use it too. |
| `Ship Auxiliary Control` | – | present | Keeps control when the bridge is lost. With `Requirement Can Have Aux Con` a design may have at most one. |
| `Ship Life Support` | – | count | Counted against `Requirement Min Life Support`. |
| `Ship Crew Quarters` | – | count | Counted against `Requirement Min Crew Quarters`. Each working one adds 4 to the ship's defence against boarding. |
| `Master Computer` | – | present | Replaces bridge, life support and crew quarters: a design with one needs none of them, and losing them does not slow the ship. |

### Supply

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Supply Storage` | V1: supply | sum | The vehicle's maximum supply. When storage is destroyed, supply is cut to the new maximum. |
| `Quantum Reactor` | – | present | The vehicle never runs out of supply. |
| `Solar Supply Generation` | V1: supply per star | sum | At the end of each turn adds V1 × the stars in the vehicle's system (destroyed stars count), up to the maximum. |
| `Emergency Resupply` | V1: supply | per use | Use Component on a ship or base adds V1 supply, up to the maximum. |
| `Supply Generation` | – | present | A resupply depot: a colonised planet with it refills, to full, the vehicles in its sector of its owner and of empires in a Military Alliance or Partnership with the owner. No population is needed. Read from a colony's facilities and the planet's own abilities; vehicles are never depots. |
| `Component Destroyed On Use` | – | present | The component is destroyed when it is used (Use Component, or a stellar manipulation). |

### Cargo, colonies, units and yards

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Cargo Storage` | V1: kT | sum | On a vehicle, cargo space for population and units. On a facility or a planet, it adds to the colony's cargo space. The hull rule `Requirement Pct Cargo` counts the size of components with it. |
| `Colonize Planet - Rock`, `Colonize Planet - Ice`, `Colonize Planet - Gas` | – | present | The vehicle can colonise a planet of that surface with the Colonize order. The hull rule `Requirement Pct Colony Mods` counts their size. |
| `Launch/Recover Fighters` | V1: per combat turn; V2: per game turn | sum (V1 and V2 apart) | How many fighters the vehicle may launch in one combat turn and outside combat in one game turn. Also lets it recover fighters. The hull rule `Requirement Pct Fighter Bays` counts their size. |
| `Launch/Recover Satellites` | as above | sum | The same for satellites. |
| `Lay Mines` | as above | sum | The same for mines; launch only. |
| `Launch Drones` | as above | sum | The same for drones; launch only, each drone a group of its own. |
| `Mine Sweeping` | V1: mines | sum | Hostile mines a group removes when it enters a mined sector or sweeps one, from its uncloaked members. |
| `Space Yard` | V1: resource (1 minerals, 2 organics, 3 radioactives); V2: rate per turn | present; rates added up per resource | Gives a ship a construction queue, or lets a colony build ships and bases. The queue's rate for a resource is the sum of V2 of the entries for it. At most one component with it per design and one facility per planet. A working yard at the location is needed to mothball, unmothball, scrap, analyse and retrofit vehicles. |

A colonised planet launches and recovers units without any ability (spec 03 §12).

### Sensors, cloaking and scanning

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Sensor Level` | V1: sight type; V2: level | per_sight_type | Detection in that sight type. Every sensor source has at least 1 in `EM Active`. An empire sees an object in a system when its best level there in some type reaches the object's obscuration in that type (spec 01 §6). On facilities, it sets the colony's sensor levels. |
| `Cloak Level` | V1: sight type; V2: level | per_sight_type | Obscuration in that sight type. Every object has at least 1 in each type. A ship or base uses its levels only while cloaked, and can cloak when some level is 2 or more; while cloaked it pays its cloaking components' `Supply Amount Used` every turn. Unit groups always use theirs. A colony uses its facilities' levels while it is cloaked, and cloaks at no cost. |
| `Long Range Scanner` | V1: range in sectors | largest | Lets the empire inspect a foreign ship, base or unit group in the same system when one of its own objects there with this ability is within V1 sectors of it: its design, details and cargo. |
| `Long Range Scanner - System` | – | present | The same for every foreign ship and base in the system, at any distance, while any object of the empire there has it (facilities count without population). Unit groups are not covered. |
| `Scanner Jammer` | – | present | Long range scanners cannot inspect this vehicle. |
| `Sector - Sight Obscuration` | V1: level | largest | Raises the obscuration of what is in the sector to V1 in all five sight types: planets, asteroid fields, ships, bases and unit groups, never stars, storms or warp points. On a system type it covers the whole system (a nebula). Storms, planets and asteroid fields spread it through their drawn abilities, ships and bases through theirs; unit groups and a colony's facilities never spread it. |

### Shields and armour

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Shield Generation` | V1: shield points | sum | Shields raised at the start of each battle, scaled by the mount's `Shield Percent`. On facilities, the planet's shields. |
| `Phased Shield Generation` | V1: shield points | sum | Phased shields. A piece's shields form one pool; only a pool made wholly of phased shields also stops weapons whose damage type skips normal shields. |
| `Planet - Shield Generation` | V1: shield points | sum | Planetary shields from facilities. A pool with any of them is not phased. |
| `Shield Modifier - System` | V1: shield points | system total | Added to the shields of the empire's ships, bases and planets in battles in that system, when they have shields at all. |
| `Shield Regeneration` | V1: points per combat turn | sum | A ship or base with supply regains V1 shield points at the start of each combat turn after the first. Planets and unit groups never regenerate. |
| `Shield Generation From Damage` | V1: points per hit | sum | Each hit that gets past the shields gives the smaller of V1 and the damage as shield points, up to the maximum. The damage is not reduced. |
| `Armor` | – | present | Marks the component as armour: hits destroy armour components before the others. Damage types that skip armour pass it. |
| `Armor Regeneration` | V1: points per combat turn | sum | From the second combat turn, while the ship has supply, V1 a turn goes into a pool (at most 10,000) that restores destroyed components with this ability, each costing its structure. At the end of the battle, up to 10,000 points' worth are restored. |
| `Emissive Armor` | V1: points | largest | Each ordinary hit is reduced by V1, and a hit of V1 or less does nothing past the shields. Ships and bases only. |

"System total" here means: the system type's own value, plus, for every colony (with
population) and every vehicle of the empire in the system, its single best value, all
added up. The totals are taken when a battle begins (spec 04 §7).

### Combat

The to-hit chance of a shot is, in percent and clamped to 1–99 (spec 04 §7):

```text
chance = Combat Base To Hit Value + offence + Combat Modifier - System total + weapon modifiers
         − defence − Combat To Hit Modifier Per Square Distance × distance − Sector - Sensor Interference
```

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Combat To Hit Offense Plus`, `Combat To Hit Offense Minus` | V1: percentage points | per_family | The vehicle's offence from abilities is Plus − Minus. Two components of one family do not stack; different families do; the hull adds in full. A planet's offence comes from its facilities and weapon platforms. |
| `Combat To Hit Defense Plus`, `Combat To Hit Defense Minus` | V1: percentage points | per_family | The same for defence. Planets do not use them: a planet's defence is the Settings.txt `Planet Combat Defense Modifier`. |
| `Multiplex Tracking` | V1: targets | largest | How many targets a ship or base may engage in one combat turn (1 without it). |
| `Weapons Always Hit` | – | present | The vehicle's direct-fire and point-defence shots always hit. On a facility, the planet's. |
| `Combat Best Experience` | – | present | The vehicle's offence and defence become the larger of its own and the best crew experience (whole points) among its empire's pieces in the battle. |
| `Combat Modifier - System` | V1: percentage points | system total | Added to the to-hit chance of the empire's shots in battles in the system. |
| `Damage Modifier - System` | V1: percent | system total | Every hit the empire lands there is multiplied by (100 + total) / 100, rounded, before shields. |
| `Boarding Attack` | V1: strength | sum | Strength in capturing ships. It also adds to the vehicle's own defence against capture. |
| `Boarding Defense` | V1: strength | sum | Defence against capture. |
| `Self-Destruct` | – | present | Allows a ship or base the Self-Destruct order, and fires by itself when the ship is about to be captured. |
| `Planet - Change Ground Defense` | V1: percent | sum per colony | Raises (negative: lowers) the defenders' damage in ground combat on this planet, before the racial modifier. |

### Experience

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Ship Training` | V1: experience per turn; V2: the cap | largest (V1 and V2 each on their own) | Every own object with it (a colony through its facilities, with or without population; a ship, base or unit group) trains the empire's ships and bases in its sector that are not mothballed: they gain V1 a turn until they reach V2. V2 = 0 trains nobody. Ship experience never passes 50. |
| `Fleet Training` | as above | as above | The same for the empire's fleets in the sector. |
| `Ship Training - System`, `Fleet Training - System` | as above | largest over the empire's objects in the system | The same for the whole system, after the sector sources. |

### Construction, repair and upkeep

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Component Repair` | V1: components per turn | sum per sector | The empire's ships, bases, unit groups and colonised planets (facilities and the planet's own abilities, no population needed) in a sector pool their V1. Racial repair modifiers scale the pool, and each point restores one destroyed component of the empire's vehicles there, by their repair priorities (spec 03 §13). Allies' sources do not count. |
| `Resource Reclamation` | V1: percent | largest in the sector | Scrapping in the sector refunds this percent of the cost when it is above the Settings.txt scrap percent. Read from the empire's planets and ships there. |
| `Modified Maintenance Cost` | V1: percent | sum over the design | The vehicle's maintenance is multiplied by (100 + sum) / 100: −50 halves it. Read from the hull and all of the design's components. |
| `Reduced Maintenance Cost - System` | V1: percent | largest in the system | Maintenance of the empire's ships and bases in the system is multiplied by (100 − V1) / 100. The rules view lists its mode as `smallest`; the maintenance rule takes the largest value among the empire's facilities and ships in the system (spec 02 §7), and a negative value never raises maintenance. |

`Space Yard` is listed with the cargo abilities above.

### Production, research and intelligence

How these make a colony's output is in [facilities.md](facilities.md#production).

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Resource Generation - Minerals`, `- Organics`, `- Radioactives` | V1: amount per turn | sum per colony | A colony's base output of that resource. Facilities only. |
| `Point Generation - Research`, `Point Generation - Intelligence` | V1: points per turn | sum per colony | A colony's base research or intelligence points. The planet's value is not applied. Facilities only. |
| `Solar Resource Generation - Minerals`, `- Organics`, `- Radioactives` | V1: amount per star | sum per colony | Adds V1 × the stars in the system to the colony's output, after its own modifiers and outside any finite stock. The colony still needs population and must not riot or be blockaded. |
| `Remote Resource Generation - Minerals`, `- Organics`, `- Radioactives` | V1: amount per turn | sum over the ship's working components | In each sector, the first ship of the empire with it mines every uncolonised planet and asteroid field there: round(V1 × the object's value / 100) of each resource (finite resources: up to the stock left). With `Remote Mining Decreases Asteroid Value` on, each mined value drops 1 point a turn. |
| `Generate Points Minerals`, `Generate Points Organics`, `Generate Points Radioactives`, `Generate Points Research`, `Generate Points Intelligence` | V1: amount per turn | sum over everything the empire owns | Flat income from colonies (facilities and planets) and vehicles, with no modifiers and no spaceport needed. |
| `Resource Gen Modifier Planet - Minerals`, `- Organics`, `- Radioactives` | V1: percent | largest per colony | Multiplies the colony's output of that resource by (100 + V1) / 100, rounded. |
| `Resource Gen Modifier System - Minerals`, `- Organics`, `- Radioactives` | V1: percent | largest among the empire's colonies in the system | Multiplies the empire's total from the system by (100 + V1) / 100, rounded. |
| `Planet Point Generation Modifier - Research`, `- Intelligence` | V1: percent | largest per colony | As the planet resource modifier, for research or intelligence. |
| `System Point Generation Modifier - Research`, `- Intelligence` | V1: percent | largest among the empire's colonies in the system | As the system resource modifier. |
| `Spaceport` | – | present among the empire's colonies in the system | The system's output reaches the treasury. |
| `Resource Storage - Mineral`, `Resource Storage - Organics`, `Resource Storage - Radioactives` | V1: amount | sum over all the empire's colonies | Raises the empire's storage cap for that resource, scaled by the racial storage trait. Facilities only. |
| `Resource Conversion` | V1: percent lost | largest among the colony's facilities | Gives the colony the Convert Resources order; each conversion loses V1 percent. The highest loss on the colony counts. |

### Population and mood

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Planet - Change Population Happiness` | V1: whole percent per turn | sum per colony | Added to the colony's change of anger each turn: a positive value makes it angrier, a negative one calmer. |
| `Change Population Happiness - System` | V1: points per turn | largest in the system | Each of the empire's colonies in the system loses V1 points of anger a turn. |
| `Modify Reproduction - System` | V1: percent per year | largest in the system | Added to the growth of the empire's colonies in the system. |
| `Change Population - System` | V1: millions per turn | largest in the system | Added to each of the empire's colonies there, shared among its races, up to the colony's maximum. |
| `Plague Prevention - System` | V1: plague level | largest in the system | Each turn cures every colony of the empire in the system whose plague is at most V1. |
| `Medical Bay` | V1: plague level | largest on a vehicle | Cures a plague of level V1 or less on a planet in the vehicle's sector owned by its owner or by an empire in a Military Alliance or better with it. Vehicles only. |

### The planet itself

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Planet - Change Minerals Value`, `Planet - Change Organics Value`, `Planet - Change Radioactives Value` | V1: points (finite resources: percent of the stock) | sum per colony | Every tenth turn the planet's value of that resource changes by V1 points, or its stock by V1 percent of itself, within the Settings.txt limits. |
| `Planet - Change Conditions` | V1: percent | sum per colony | Every tenth turn, if the sum is above 0, the planet's conditions are multiplied by (100 + V1) / 100, up to the best band. |
| `Planet Value Change - System` | V1: points (finite: percent) | largest in the system | As `Planet - Change ... Value`, for all three resources of every colony of the empire in the system. |
| `Planet Conditions Change - System` | V1: percent | largest in the system | As `Planet - Change Conditions`, for every colony of the empire in the system. |
| `Planet - Change Atmosphere` | V1: turns | largest per colony | When the planet has spent more than V1 turns with an atmosphere its majority race does not breathe, it takes that race's atmosphere, which can remove the colony's domes. |

### Stellar manipulation

These act through a ship's or base's Stellar Manipulation order (spec 01 §9). Each needs a
working component with the ability, supply, and the ship uncloaked at the target.

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Create Planet Size` | V1: the largest stellar size, 1 (Tiny) to 5 (Huge) | largest | Turns an uncolonised asteroid field in the sector into a planet of the smaller of V1 and the field's size. The system needs a star. |
| `Destroy Planet Size` | V1: a position in PlanetSize.txt | largest | Turns a planet whose PlanetSize record is at most that far down the file into an asteroid field; its colony is lost. |
| `Create Star`, `Destroy Star` | – | present | Places a star in a system without one; destroys a star with a shockwave that wipes out the system, the user included. |
| `Create Storm`, `Destroy Storm` | – | present | A created storm gets one ability whose value comes from the Settings.txt `Created Storm ...` settings. |
| `Create Nebulae`, `Destroy Nebulae`, `Create Black Hole`, `Destroy Black Hole` | – | present | Turn a system into a nebula or black hole (consuming its star with a shockwave), or back into a normal system. |
| `Open Warp Point Distance` | V1: distance in galaxy-map squares | largest | Opens a warp point to a chosen system at most V1 away. |
| `Close Warp Point` | – | present | Removes both ends of a warp point. |
| `Create Constructed Planet` | V1: a PlanetSize `Special Ability ID` | largest | Builds the constructed world of that PlanetSize record in a star's sector, consuming the star and the builders. |
| `Constructed Planet Requirements` | V1: a `Custom Group`; V2: kT | each entry on its own | Every entry must be met before `Create Constructed Planet` works: the ships and bases in the sector must carry at least V2 kT of components of that custom group. Read from the acting ship. |
| `Stop Planet Destroyer`, `Stop Star Destroyer`, `Stop Nebulae Creator`, `Stop Black Hole Creator`, `Stop Open Warp Point`, `Stop Close Warp Point` | – | present | Blocks that action in the whole system (`Stop Planet Destroyer` only in its sector; for warp points, in either system), whoever owns the blocking ship or colony, the acting empire included. |

### Systems and stellar objects

These belong on system types and stellar ability types. Apart from
`Sector - Sight Obscuration` (above), they do nothing on components and facilities.

| Ability | Values | Combines | What it does |
|---|---|---|---|
| `Sector - Sensor Interference` | V1: percentage points | sum of the system's and the sector's objects' | Subtracted from every to-hit chance in battles there: a positive value makes hits rarer, a negative one more likely. |
| `Sector - Shield Disruption` | V1: shield points | sum, as above | Subtracted from the shields of every piece in battles there. |
| `Sector - Damage` | V1: damage | sum of the system's and the sector's objects' | A group stepping into the sector has a 50 % chance that each of its vehicles takes V1 normal damage, and then stops for the turn. Vehicles that stay, or arrive through a warp point, take none. |
| `Warp Point - Turbulence` | V1: damage | sum on the warp point | On each jump from it, a 50 % chance that every vehicle of the group takes V1; the group arrives but stops for the turn. |
| `System - Movement Towards Center` | V1: squares per turn | sum | At the end of each turn every ship and unit group in the system moves V1 squares towards the centre. |
| `System - Movement Random` | V1: squares per turn | sum | At the end of each turn every ship and unit group moves V1 squares towards one random square. |
| `System - Destructive Center` | V1: damage per turn | sum | At the end of each turn every ship and unit group in the centre square takes V1 normal damage. |
| `Ancient Ruins` | V1: tech levels | largest on the planet | When the planet is colonised, V1 times a random tech area the colonist can research gains a level; then the ability goes. |
| `Ancient Ruins Unique` | V1: a unique area number | largest on the planet | When the planet is colonised (and has no `Ancient Ruins` above 0), the colonist gains the unique tech areas with that `Unique Area` number, each raised one level; then the ability goes ([techs.md](techs.md)). |
| `Change Bad Event Chance - System` | V1: percent | largest in the system | A random event about to pick a target in the system is turned away with a chance of V1 percent (100 means no effect). Only entries that belong to no empire count, those of the system and of its stellar objects (stars, storms, warp points, planets); a colony's facilities and a vehicle's components never count, so on them it does nothing. |
| `Change Bad Intelligence Chance - System` | V1: percent | as above | The same for hostile intelligence operations that choose their target at random. |

### Names without an effect

OpenSE4 accepts these names but no rule reads them, as in the original:

| Ability | Notes |
|---|---|
| `AI Tag 01` to `AI Tag 20` | Labels for the computer players; reports hide them. OpenSE4 accepts any name that begins with `AI Tag`. |
| `Palace` | No rule reads it: it does not mark a homeworld. The computer players' facility minister treats it as needed once per system. |
| `Point-Defense` | A weapon is point-defence through its `Weapon Type` ([components.md](components.md)), not through this ability. |
| `Drop Troops` | Any ship or base with troops in its cargo can drop them in battle without it. Only the computer players look at it, when they rank components by the value of an ability. |
| `Star - Unstable`, `Random`, `Warp Point - Unstable`, `Warp Point - Periodic`, `Warp Point - Ability Required`, `Sector - Ability Required`, `Resupply Pod`, `Maximum Population` | No effect. |
| `System - Sensor Interference`, `System - Damage`, `System - Ability Required` | Not names the original knows: they load with the warning `ability type '...' has no effect`. On a system type, use `Sector - Sensor Interference` and `Sector - Damage`, which count system-wide there. |
| `None` | An empty slot; the loader skips it. |

## Declaring new abilities

A mod may use names of its own once a patch declares them
([packages-and-data.md](../../packages-and-data.md#new-ability-names)):

```toml
[[abilities.declare]]
name = "Gravity Anchor"
combine = "max"           # "sum" (the default), "max" or "min"
```

- `name` is required; `combine` says how Val 1 of several entries combines over a list: the
  sum (at most 2,000,000,000), the largest or the smallest, 0 when there is none. Val 2 is
  not read.
- Once declared, the name may appear on components, facilities, hulls, system types and
  stellar ability types of any mod, in any letter case.
- **A declared ability does nothing by itself.** No rule of the game reads it: a mod's rules
  script gives it its effect, reading its value with `game.ability(thing, name, kind)`,
  combined as declared, on a vehicle (its design), a design (the hull and every component),
  a colony (its facilities), a system (its own abilities and its objects'), a component, a
  facility or a hull ([rules.md](../../rules.md#abilities); the tutorial
  [A new ability with an effect](../tutorials/new-ability.md) makes one).
- Two mods may declare the same name only with the same `combine`.
- A game whose data uses declared names cannot be saved for the original (Save for SE IV
  refuses it).

## Changing them with patches

### A value on a component

Engines of your data set given a smaller bonus. Removing by type removes every
`Movement Bonus` entry of the record:

```toml
[[components.change]]
name = "<an engine of your data set>"
remove = { abilities = ["Movement Bonus"] }
add = { abilities = [{ "Ability Type" = "Movement Bonus", "Ability Descr" = "Adds 1 to speed.", "Ability Val 1" = 1 }] }
```

### One entry of several, and a sight type

A sensor component that sees in `Psychic` instead of `Gravitic`: the table names the one
entry to remove.

```toml
[[components.change]]
name = "<a sensor component of your data set>"

[components.change.remove]
abilities = [{ "Ability Type" = "Sensor Level", "Ability Val 1" = "Gravitic" }]

[components.change.add]
abilities = [{ "Ability Type" = "Sensor Level", "Ability Descr" = "Psychic sensors, level 3.", "Ability Val 1" = "Psychic", "Ability Val 2" = 3 }]
```

### Systems and stellar objects

Dust that spoils aim and hides ships across a whole system, and a 5 % chance of ruins worth
two tech levels on the objects that draw from a stellar ability type:

```toml
[[system_types.change]]
name = "<a system type of your data set>"

[system_types.change.add]
abilities = [
  { "Ability Type" = "Sector - Sight Obscuration", "Ability Descr" = "Thick dust hides ships.", "Ability Val 1" = 3 },
  { "Ability Type" = "Sector - Sensor Interference", "Ability Descr" = "Dust spoils aim.", "Ability Val 1" = 15 },
]

[[stellar_ability_types.change]]
name = "<a stellar ability type of your data set>"
add = { abilities = [{ "Ability Chance" = 50, "Ability Type" = "Ancient Ruins", "Ability Descr" = "Ruins of a lost people.", "Ability Val 1" = 2 }] }
```

An object draws at most one entry of its stellar ability type, walking the entries in order
with a draw from 1 to 1000. Keep the record's chances at 1000 or less in total: a new entry
whose share lies past 1000 is never drawn. `Ancient Ruins` only matters on planets, the
objects that can be colonised.

### A declared ability on a facility

```toml
[[abilities.declare]]
name = "Gravity Anchor"
combine = "max"

[[facilities.change]]
name = "<a facility of your data set>"
add = { abilities = [{ "Ability Type" = "Gravity Anchor", "Ability Descr" = "Holds ships in orbit.", "Ability Val 1" = 4 }] }
```

The facility loads and shows the line in its report; the ability does nothing until a
rules script reads it.

## Things to watch

- **Exact names.** `Resource Storage - Mineral` is singular, `Generate Points Minerals`
  has no dash, `Point-Defense` has a hyphen. A name OpenSE4 does not know and no mod
  declares is an error that names the record: `unknown ability type '...'`.
- **Values that are not whole numbers read as 0 without an error.** `2.5`, `10%` or a
  misspelt sight type (`EM-Active`) silently give nothing; check the ability in the game's
  reports or in the rules view.
- **Negative values** of abilities that combine as `largest` never take effect; to lower
  something, use an ability that adds up, or the matching `Minus` ability.
- **`Movement Bonus` takes the smallest value**, so giving it to one more component can
  lower a ship's speed.
- **The carrier matters.** An ability on a carrier whose list no rule reads does nothing,
  with no warning: for example `Resource Generation - ...` on a component,
  `Emergency Energy` on a facility, `Medical Bay` on a facility, `Sector - Damage` on a
  component, or `Change Bad Event Chance - System` on anything an empire owns.
- **At most 20 entries** per record: a patch that adds a 21st is an error, because the
  game would not read it.
- **Removing by type removes every entry of that type.** Use a table of fields or a
  position to remove one.
- **Declarations:** `combine` other than `"sum"`, `"max"` or `"min"` is an error; so are a
  declaration without a name, any other key, declaring one of the game's own names
  (`'...' is one of the game's own abilities: it needs no declaring`), and two mods
  declaring one name with different `combine` values.
- **Scripts:** the script view's `abilities` query reports a declared ability with the
  aggregation `unspecified` and its Val 1 values added up, whatever its `combine`.

## More detail

- Spec: [03 Vehicles and abilities](../../../spec/03-vehicles-and-abilities.md) §2.1 (the
  entry format), §3 (sources, aggregation and the reference), §4.2 (design rules), §6.1
  (movement), §7 (supply), §12 (units), §13 (repair);
  [02 Empires and economy](../../../spec/02-empires-and-economy.md) §1.5 (facility
  abilities and their scopes), §5, §6, §7; [04 Combat](../../../spec/04-combat.md) §7 to §12
  and §18.4; [01 Galaxy and setup](../../../spec/01-galaxy-and-setup.md) §4.4, §5.2, §5.3
  (system and stellar abilities), §6 (sight), §7 to §9 (hazards, warp points, stellar
  manipulation).
- Patches and declarations: [packages-and-data.md](../../packages-and-data.md#data-patches)
  and [New ability names](../../packages-and-data.md#new-ability-names).
- What scripts and computer players read ([view.md](../../view.md#the-rules-view)): the
  [`ability`](../../view.md#ability) entries of the `component`, `facility`, `hull` and
  `system_type` records; the rules view's `abilities` list of
  [`ability_kind`](../../view.md#ability_kind), each engine name with its
  [`aggregation`](../../view.md#aggregation) (declared names are not in it); and the
  [abilities query](../../view.md#abilities), which combines the entries of a vehicle,
  planet, system, design, component, facility or hull.
- Related chapters: [components.md](components.md), [hulls.md](hulls.md),
  [facilities.md](facilities.md), [galaxy.md](galaxy.md), [races.md](races.md),
  [combat.md](combat.md), [ai-tables.md](ai-tables.md).
