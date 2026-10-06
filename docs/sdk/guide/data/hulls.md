# Hulls

A hull, which the data files call a vehicle size, is the frame of a design: it sets the
design's class (ship, base or one of the six kinds of unit), its space, part of its price,
how engines turn into speed, and which parts every design on it must carry. It can also
carry abilities of its own. Hulls come from `VehicleSize.txt`, patch table
`vehicle_sizes`, whose records are named by `Name`. The parts that fill a hull are in
[components.md](components.md); the abilities a hull may carry are listed in
[abilities.md](abilities.md).

## The fields

Field names are matched in any letter case. "Required" means the loader reports an error
when the field is missing. True/False fields also accept 1 and 0; a missing one is False.

### Names and pictures

| Field | What it does | Values |
|---|---|---|
| `Name` | The hull's name in the designer's Size list, in reports and in the Designs window, and the name patches use. | Text, required. Keep names unique. |
| `Short Name` | Read, and given to scripts in the rules view. The OpenSE4 client does not show it. | Text, optional. |
| `Description` | Shown at the foot of the hull's report. | Text, optional. |
| `Code` | A short abbreviation, shown in brackets after a ship's or base's name in the battle rows of the Log. | Text, optional; two letters fit the original's style. |
| `Primary Bitmap Name` | The base name of the hull's pictures (below). | A picture base name without folder or extension, optional. |
| `Alternate Bitmap Name` | The base name used when the primary one has no picture. | The same, optional. |

**How the pictures are found.** For a race whose picture style is `S` and a bitmap name
`B`, the game looks for `S_Mini_B.bmp` (36×36: the map, lists and design strips) and
`S_Portrait_B.bmp` (128×128: reports and the designer) in the race's folder,
`Pictures/Races/S/` or `Pictures/RaceNeutral/S/`, and then for `Generic_Mini_B.bmp` and
`Generic_Portrait_B.bmp` in `Pictures/RaceGeneric/`, which serve every race. When the
primary name finds nothing, the alternate name is tried the same way. A mod puts its
pictures under `assets/` in that layout, as BMP or PNG, and may make them larger by whole
multiples ([packages-and-data.md](../../packages-and-data.md) "Pictures"). Draw a mini
facing up: on the map the minis of hulls that use engines, and of fighter and drone hulls,
are turned to the vehicle's heading in steps of 45°; the others are drawn as they are.
Fleets and some views of unit groups use fixed pictures of the race (`Fleet`,
`FighterGroup`, `SatelliteGroup`, `MineGroup`) rather than a hull's. A design may also
show a picture of its own in place of its hull's (packages-and-data.md "A design's own
picture").

### Class, space, cost and speed

| Field | What it does | Values |
|---|---|---|
| `Vehicle Type` | The class of every design on this hull: what it can do, how it is built and which components fit (see "The eight vehicle types"). | Exactly one of `Ship`, `Base`, `Fighter` (or `Ftr`), `Satellite` (or `Sat`), `Mine`, `Troop` (or `Trp`), `Drone`, `Weapon Platform` (or `WeapPlatform`, `WeapPlat`). Any letter case. Required; naming none or several is an error. |
| `Tonnage` | The space for components. It is also the cargo space one unit of this hull takes, the size a mount's bounds are compared with, and the size that decides who can push whom in battle. | kT, whole number, required. |
| `Cost Minerals`, `Cost Organics`, `Cost Radioactives` | The hull's price, the base of every design's cost. | Whole numbers, all three required (0 for none). |
| `Engines Per Move` | The divisor that turns engine output into movement: the design's summed `Standard Ship Movement` divided by it, rounded down, is its base movement. | Whole number, optional. 0 (or less): engines give no movement, so a ship never moves; a fighter or drone then moves only by its movement bonuses (see "Movement"). |

### Design requirements

These fields set rules every design on the hull must meet before the designer accepts it.
The order and the warnings are in "Design rules" below.

| Field | What it does | Values |
|---|---|---|
| `Requirement Must Have Bridge` | True: the design needs **exactly one** component with `Ship Bridge`. The designer calls it a cockpit on fighter and troop hulls and a computer core on satellite, drone and weapon platform hulls. | True/False. |
| `Requirement Can Have Aux Con` | True: at most one component with `Ship Auxiliary Control`. False does **not** forbid them: nothing is checked. | True/False. |
| `Requirement Min Life Support` | The design needs at least this many components with `Ship Life Support`. | Count, 0 for none. |
| `Requirement Min Crew Quarters` | The same for `Ship Crew Quarters`. | Count, 0 for none. |
| `Requirement Uses Engines` | False: no engine may be fitted. An engine is any component with the `Standard Ship Movement` ability, whatever its value. | True/False. |
| `Requirement Max Engines` | With `Uses Engines` True, the most engines a design may have. | Count; 0 means no limit for the designer (but see "Things to watch"). |
| `Requirement Pct Fighter Bays` | At least this share of `Tonnage` must be components with `Launch/Recover Fighters`, counted by their mounted size. | Percent of the hull, 0 for no rule. |
| `Requirement Pct Colony Mods` | The same for components with any of the three `Colonize Planet - ...` abilities. | Percent, 0 for no rule. |
| `Requirement Pct Cargo` | The same for components with `Cargo Storage`. | Percent, 0 for no rule. |
| `Launched from Ship`, `Launched from Planet` | Documented by the data file's header, never used by the original. OpenSE4 reads and ignores them. | True/False. |

### Technology and abilities (numbered lists)

| List | Fields of one entry | Patch list | What it does |
|---|---|---|---|
| `Number of Tech Req` | `Tech Area Req N`, `Tech Level Req N` | `requirements` | Every listed area must be at or above its level before the empire can use the hull. The `Create` button of the Designs window offers only the vehicle types the empire has a researched hull of, and the Size list only researched hulls. An unknown area name is an error; a level above the area's maximum makes the hull unobtainable, without a message. |
| `Number of Abilities` | `Ability N Type`, `Ability N Descr`, `Ability N Val 1`, `Ability N Val 2` | `abilities` | Abilities the hull itself gives (see "Hull abilities"). At most 20 are read; an unknown type is an error unless a mod declares it; `None` entries are skipped. The Ship Report's Ability tab lists the hull's abilities by their `Descr` lines. Every type: [abilities.md](abilities.md). |

### The eight vehicle types

Ships and bases are vehicles of their own; the other six classes are **units**, which are
built into cargo (the builder's, or another holder's in the same sector), form groups when
they are in space, and pay no maintenance.

| `Vehicle Type` | Moves | Supply | Holds cargo | Built and used |
|---|---|---|---|---|
| `Ship` | By its engines, through warp points too. | Holds what its `Supply Storage` gives; engines, weapons and cloaks spend it. At 0 it moves 1 sector a turn and cannot fire. | Yes | Built in a construction queue with a space yard. |
| `Base` | Never, whatever its engines. | Unlimited ("Endless"). | Yes | Built in a queue with a space yard. Joins fleets only when the setting `Bases Can Join Fleets` is on. |
| `Fighter` | Like a ship, but only inside its system: it cannot use warp points. | Each fighter holds its `Supply Storage` and pays an upkeep each turn (setting `Fighter Supply Usage Per Turn`); refilled only at a resupply planet. At 0 it moves 1 sector and cannot fire. | No | Launched from a bay with `Launch/Recover Fighters` or from a planet, and recovered the same way. |
| `Satellite` | Never. | None at all. | No | Deployed with `Launch/Recover Satellites` or from a planet; stays in its sector and fights there; can be recovered. |
| `Mine` | Never. | None at all. | No | Laid with `Lay Mines` or from a planet. It never appears in battle; on the map it is as hidden as its `Cloak Level` makes it. Its warheads strike hostile vehicles that enter the sector, and it is used up. |
| `Troop` | Never. | None. | No | Carried as cargo and dropped on a planet during a battle, where it fights the ground war. |
| `Drone` | By its orders, through warp points too. | Holds supply and pays an upkeep (setting `Drone Supply Usage Per Turn`); never resupplied; destroyed at 0. | No | Launched by `Launch Drones` or from a planet, each drone a group of its own; attacks by ramming. |
| `Weapon Platform` | Never. | None. | No | Moved to a planet as cargo, or built there; its weapons fire as the planet's own. |

## How the fields work together

### Design rules

The designer lists every rule a design breaks, and a design with any warning cannot be
created. OpenSE4 checks these in the original's order, then one rule of its own:

| # | Rule | OpenSE4's warning |
|---|---|---|
| 1 | The design has a hull (if not, this is the only warning). | Choose a hull for the design |
| 2 | The empire has the technology of the hull and of every component. | ... is not yet researched |
| 3 | The components' mounted sizes add up to at most `Tonnage`. | Components use ... kT of ... kT |
| 4 | At most one component with `Space Yard`. | A design can have only one space yard |
| 5 | Every mount allows the hull's `Tonnage`, and its technology is known. | A weapon mount is not allowed on a hull of this size / ... beyond our technology |
| 6 | No family exceeds a component's `Restrictions` ([components.md](components.md)). | At most N of the ... family per vehicle |
| 7 | Bridge, auxiliary control, life support and crew quarters, unless a component has `Master Computer`. | Needs exactly one bridge (cockpit, computer core); At most one auxiliary control; Needs N life support; Needs N crew quarters |
| 8 | Engines: none when `Uses Engines` is False; at most `Max Engines` when that is above 0. A Master Computer does not lift this. There is no minimum. | This hull cannot use engines; At most N engines |
| 9 | Each percentage p above 0: the mounted sizes of the matching components add up to at least truncate(`Tonnage` × p / 100). | At least p% of the hull must be fighter bays (colony modules, cargo space) |
| 10 | (OpenSE4) Every component's `Vehicle Type` allows the hull's class. The original's designer simply never offers such parts. | ... cannot be placed on a ... |

These rules count **components**: a component with several entries of one ability counts
once, and the percentage rules add mounted sizes. A part with two colonize abilities counts
once in each; fighter bays and colony modules count towards `Pct Cargo` only if they carry
`Cargo Storage` themselves. Abilities on the hull never count here.

### How a design's figures follow from hull and components

| Figure | From the hull | From the components |
|---|---|---|
| Space | `Tonnage` is the space. | Their mounted sizes fill it. |
| Cost, per resource | The hull's cost. | Plus every mounted component cost. |
| Structure | Nothing. | The sum of their mounted structures. |
| Movement | `Engines Per Move`, the class, and any movement abilities. | `Standard Ship Movement`, `Movement Bonus`, `Extra Movement Generation` (below). |
| Supply capacity | `Supply Storage` on the hull. | Plus theirs. Satellites, mines, troops and platforms hold none; bases and vehicles with a working `Quantum Reactor` are unlimited. |
| Cargo space | `Cargo Storage` on the hull. | Plus theirs. Only ships and bases have a hold. |
| Shields | `Shield Generation` and `Phased Shield Generation` on the hull. | Plus theirs, each component's scaled by its mount. |
| To-hit bonuses | The hull's `Combat To Hit ...` values add in full. | The best value of each component family, added up. |
| Maintenance | `Modified Maintenance Cost` on the hull. | Through the cost. |

The design's cost is what a construction queue pays for it, a share of it each turn at the
queue's rate, so it also sets the build time. Each turn every ship and base costs, per
resource, truncate(cost × the empire's maintenance percentage / 100), then that ×
(100 + the sum of `Modified Maintenance Cost`) / 100, truncated, then a reduction from
system facilities. Units and mothballed vehicles pay nothing. Scrapping returns a share of
the cost, and a retrofit, which keeps the hull, pays only for the components that change.

### Movement

The designer shows a design's movement as

```text
movement = (sum of Standard Ship Movement) ÷ Engines Per Move, rounded down (0 when Engines Per Move is 0)
         + B, when B is between 1 and 99
B        = smallest Movement Bonus on the design (0 if none) + Extra Movement Generation (first entry per stacking id, summed)
```

OpenSE4 gives 0 for bases, satellites, mines, troops and weapon platforms, whatever they
carry. This figure is also the speed of a fighter or drone; a group of units moves at its
slowest design's speed, and at 1 when out of supply.

A ship's movement in play starts from the same division, using the working components
only (and, as in the original, only the low 8 bits of the engine total, so keep it below
256), and then:

1. B is added only when the engines give at least 1, and only when B is below 100 (a
   negative B is added too; OpenSE4 never goes below 0).
2. When the result is above 0, the race's `Vehicle Speed` trait is added.
3. At 0 supply the result is exactly 1. A mothballed ship has 0.
4. Unless a working `Master Computer` is aboard, the result is halved (rounded down, never
   below 1) once for each of: no bridge and no auxiliary control left; no crew quarters
   left; no life support left. This tests what is left, not the hull's minimums, so it
   also halves ships on hulls that require none of these parts.

In battle a ship moves half its map speed, rounded up, plus its best `Combat Movement`.

For example, a hull with `Engines Per Move` 2 and three engines of `Standard Ship
Movement` 1 moves 1 sector a turn; give each engine `Movement Bonus` 1 and it moves 2.

### Hull abilities

A hull's abilities always work: no hit can destroy them, though a mothballed vehicle has
no abilities at all. They join the vehicle's ability list wherever the rules read it:
supply and cargo capacity, movement bonuses, cloaking and sensors, the to-hit bonuses (the
hull's always add in full), maintenance, and the control test of step 4 above (a hull with
`Ship Bridge`, `Ship Life Support` and `Ship Crew Quarters` of its own never loses speed
that way). They do not count for the design rules, which look at components only: a hull's
`Master Computer` does not lift rule 7. In a unit group every unit lists its design's
abilities, the hull's included, so summed abilities grow with the number of units.

### Tonnage elsewhere

- **Mounts** are allowed only between their `Vehicle Size Minimum` and `Vehicle Size
  Maximum`, compared with the hull's `Tonnage` ([components.md](components.md)).
- **Cargo**: a unit takes its hull's `Tonnage` of cargo space on a ship, base or planet.
- **Battle**: a ship can push or pull another ship only if its hull's `Tonnage` is at least
  the target's. A design's "enemy tonnage destroyed" adds the hull `Tonnage` of each enemy
  ship or base its vehicles destroy (for a unit group, times its units).
- **Computer players** take the largest researched hull of the right class that fits their
  template's tonnage window ([ai-tables.md](ai-tables.md)).

## Changing them with patches

These examples use invented hulls; text in angle brackets stands for a record of your
data set. A new ship hull written out in full, with its technology and two abilities:

```toml
# data/hulls.toml
[[vehicle_sizes.add]]
name = "Sable Hull"
after = "<a ship hull of your data set>"     # its place in the Size list

[vehicle_sizes.add.set]
# In alphabetical order: a patch may set fields in any order.
"Alternate Bitmap Name" = "<a bitmap name of your data set's hulls>"
"Code" = "SB"
"Cost Minerals" = 180
"Cost Organics" = 0
"Cost Radioactives" = 20
"Description" = "A long, narrow hull for fast raiders."
"Engines Per Move" = 1
"Primary Bitmap Name" = "Sable"              # assets/Pictures/RaceGeneric/Generic_Mini_Sable.png ...
"Requirement Can Have Aux Con" = true
"Requirement Max Engines" = 5
"Requirement Min Crew Quarters" = 1
"Requirement Min Life Support" = 1
"Requirement Must Have Bridge" = true
"Requirement Pct Cargo" = 0
"Requirement Pct Colony Mods" = 0
"Requirement Pct Fighter Bays" = 0
"Requirement Uses Engines" = true
"Short Name" = "Sable"
"Tonnage" = 260
"Vehicle Type" = "Ship"

[vehicle_sizes.add.add]
requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 3 }]
abilities = [
  { "Ability Type" = "Combat To Hit Defense Plus", "Ability Descr" = "Narrow profile: harder to hit.", "Ability Val 1" = 10 },
  { "Ability Type" = "Supply Storage", "Ability Descr" = "Holds some supply in the keel.", "Ability Val 1" = 150 },
]
```

Put its pictures in the mod as `assets/Pictures/RaceGeneric/Generic_Mini_Sable.png` and
`Generic_Portrait_Sable.png` (or per race under `Pictures/Races/`).

A heavier mine casing copied from one of your data set's mine hulls: every mine of it will
take 30 kT of a mine layer's cargo.

```toml
[[vehicle_sizes.add]]
name = "Mirefield Casing"
copy_from = "<a mine hull of your data set>"
after = "<a mine hull of your data set>"
set = { "Short Name" = "Mirefield", "Tonnage" = 30, "Cost Minerals" = 45 }
remove = { requirements = [1] }
add = { requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 5 }] }
```

Changes to hulls of your data set. Bases never move, so the speed losses of a ship
without crew quarters or life support do not touch them (crew quarters still help against
boarding: each working one adds 4 to the defence):

```toml
[[vehicle_sizes.change]]
match = { "Vehicle Type" = "Base" }
all = true
set = { "Requirement Min Crew Quarters" = 0, "Requirement Min Life Support" = 0 }

[[vehicle_sizes.change]]
name = "<a large ship hull of your data set>"
set = { "Requirement Max Engines" = 8 }
remove = { abilities = ["Combat To Hit Defense Minus"] }

[[vehicle_sizes.remove]]
name = "<a hull of your data set>"
```

No other data table names hulls, so a removal leaves no reference and `cascade` changes
nothing; removing a tech area with `cascade = true` removes the hulls that need it.

## Things to watch

- **One class only.** `Vehicle Type` takes one class; `Ship\Base` is an error.
- **Bases and the other fixed classes never move.** Engines on them change nothing; set
  `Requirement Uses Engines` to False for such hulls. A ship hull with `Engines Per Move`
  0 can carry engines but never moves.
- **`Max Engines` 0** means no limit in the designer, but the computer players read it as
  a hard limit: their designer adds engines only while it has fewer than `Max Engines`, and
  never uses a hull whose `Max Engines` is below a template's minimum speed. Give a hull
  that should move a positive `Max Engines`.
- **Computer players pair hulls with templates**: a hull with `Pct Fighter Bays` above 0 is
  used only by templates whose main ability is fighter bays, and the same holds for colony
  modules and cargo ([ai-tables.md](ai-tables.md)).
- **`Can Have Aux Con` False** allows any number of auxiliary controls; it is True that
  limits them to one.
- **Two bridges** break `Must Have Bridge`, which asks for exactly one.
- **Percentages above 100**, or several that add up to more than 100, can only be met by
  parts that count for more than one rule (a bay that also has `Cargo Storage`).
- **Speed losses in flight.** A ship without a bridge (or auxiliary control), crew quarters
  or life support moves at half speed for each one missing, even when its hull requires
  none of them. Put those abilities on the hull, or expect designs to carry them.
- **Supply.** A ship design with no `Supply Storage` on the hull or its parts has no supply:
  it moves 1 sector a turn and its weapons never fire.
- **Unit size is cargo size.** Raising a unit hull's `Tonnage` gives more room for parts
  and makes every unit of it fill more of a carrier.
- **Changing `Tonnage`** moves the hull into or out of mounts' size ranges; Copy or Upgrade
  of a design made before then shows the mount warning.
- **Renaming or removing a hull**: the empire files players save name their designs' hulls,
  and a design whose hull is gone is left out, with a warning, when such an empire is
  loaded.
- **Pictures.** `opense4-sdk check` warns when a hull your mod adds or changes has no mini
  or portrait for its `Primary Bitmap Name` in `RaceGeneric` or any race folder (saying
  when the alternate's is shown instead), and about ship pictures in your mod that no hull
  names.
- **Errors `opense4-sdk check` reports**: a field the table does not have ("check the
  spelling"); a numbered field past its list's count; a missing `Name`, `Vehicle Type`,
  `Tonnage` or cost; a `Vehicle Type` naming no class or several; a value that should be a
  whole number or True/False; an unknown tech area or ability type; a name added twice; a
  record to change or remove that is not there.

## More detail

- [Spec 03](../../../spec/03-vehicles-and-abilities.md): §1 (vehicle classes), §2.2 (the
  fields), §3 (abilities and how they add up), §4.2 (design rules), §4.4 (design figures),
  §6.1 (movement), §7 (supply), §11 (cargo), §12 (units), §16 (maintenance).
- [Spec 04](../../../spec/04-combat.md): §5 (movement in battle), §7 (to-hit bonuses), §9.5
  (push and pull), §18.5.
- [Spec 06](../../../spec/06-ui-and-assets.md) §5.3 (race picture folders) and
  [packages-and-data.md](../../packages-and-data.md) for pictures and the patch format.
- Computer players read hulls as the `hull` records of the rules view and a design's
  figures as `design_figures` ([view.md](../../view.md) "The rules view", "Designs"). The
  record's `min_percent_fighter_bays`, `min_percent_colony_modules` and `min_percent_cargo`
  are the minimum shares described above, and `can_have_aux_control` only limits
  auxiliary controls to one.
