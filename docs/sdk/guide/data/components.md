# Components and weapon mounts

A design is a hull plus a list of components: engines, weapons, armour, shields, bays,
control rooms and everything else a ship or unit carries. Components come from
`Components.txt`, patch table `components`, whose records are named by `Name`. The second
part of this chapter covers weapon mounts, which change a component on one design: they
come from `CompEnhancement.txt`, patch table `weapon_mounts`, whose records are named by
`Long Name`. Hulls, the other half of a design, are in [hulls.md](hulls.md); the abilities
that give components most of their effects are listed in [abilities.md](abilities.md).

## The fields

Every field below is read by OpenSE4 unless the table says otherwise. Field names are
matched in any letter case. "Required" means the loader reports an error when the field
is missing; an empty number counts as 0.

### Identity, size and cost

| Field | What it does | Values |
|---|---|---|
| `Name` | The component's name in every list and report, and the name patches use. | Text, required. Keep names unique: patches find a record by name, in any letter case. |
| `Description` | Shown at the foot of the component's report. | Text, optional. |
| `Pic Num` | Its picture (see "Pictures" below). | Whole number from 1. 0 or missing: no picture. |
| `Tonnage Space Taken` | The hull space it fills. | kT, whole number, required. A mount scales it. |
| `Tonnage Structure` | The damage it absorbs before it is destroyed, and its weight when a hit picks a part. Need not equal its size. | Points, required. A mount scales it. |
| `Cost Minerals`, `Cost Organics`, `Cost Radioactives` | Its price, added to the design's cost. | Whole numbers, all three required (0 for none). A mount scales them. |
| `Supply Amount Used` | Supply spent each time the component is used: per sector moved for an engine, per shot for a weapon, per turn while cloaked for a cloak, per use for a stellar manipulator. | Supply points, optional (0). A mount scales it. |

### Where it fits and how it is grouped

| Field | What it does | Values |
|---|---|---|
| `Vehicle Type` | The hull classes that may carry it. The designer offers it only on those hulls, and OpenSE4 refuses a design that has it on another. | Tokens joined with `\`: `Ship`, `Base`, `Fighter` (or `Ftr`), `Satellite` (or `Sat`), `Mine`, `Troop` (or `Trp`), `Drone`, `WeapPlatform` (or `WeapPlat`), or `All`. Any letter case. Required; an unknown token is an error. |
| `Vechicle List Type Override` | Replaces `Vehicle Type` when present. The text is lower-cased and every class whose keyword (`ship`, `base`, `fighter`, `satellite`, `mine`, `troop`, `drone`, `weapplatform`) appears anywhere in it is allowed. | Text, optional. The misspelt key is the classic one; OpenSE4 also accepts `Vehicle List Type Override`. |
| `Vehicle List Type Description` | Shown in the component's report in place of the list of classes. | Text, optional. |
| `Restrictions` | The most components of this component's **family** one design may carry (see "Families" below). | `None` (or empty), or `One Per Vehicle` up to `Ten Per Vehicle`. Anything else is an error. |
| `General Group` | The designer's `Comp Type` filter, and the group the repair priority list orders (damaged parts of the groups listed first are repaired first). | Free text, optional. Compared in any letter case. |
| `Custom Group` | A tag that the `Constructed Planet Requirements` ability looks for: building a ringworld or sphereworld needs so many kT of components with this tag in the sector. | Whole number, optional. |

### Families, numerals and upgrades

| Field | What it does | Values |
|---|---|---|
| `Family` | The component's line. It drives `Restrictions`, the designer's `Only Latest`, a design's `Upgrade`, the stacking of to-hit abilities and the family lists of mounts (see "How the fields work together"). | Whole number, optional. 0 is a family like any other. |
| `Roman Numeral` | The version within the family. Upgrade and Only Latest ignore it: they go by file order. The computer players redesign a template when a component of its design has a researched relative of the same non-zero family with a higher numeral. When OpenSE4 gives a new empire the technology to colonize its home planet type, it prefers a colonize component with fewer tech requirements or a lower numeral. | Whole number, 0 for none. The original uses 0 to 20; OpenSE4 does not check the range. |

### Pictures

`Pic Num` names a cell of the sheet `Pictures/Components/Components.bmp`: cells of 36×36
pixels, counted row by row from the top left, the first cell being `Pic Num` 1. Lists, the
designer and the design grids show that cell. A component's full report shows the portrait
`Pictures/Components/Comp_NNN.bmp`, 128×128, where NNN is `Pic Num` with three digits
(`Comp_007.bmp` for 7); when that file is missing, the report shows the cell.

A mod can replace both kinds of picture under `assets/` (PNG works too,
[packages-and-data.md](../../packages-and-data.md) "Pictures"). The sheet's layout is the
installed sheet's: a mod's larger `Components.bmp` is read as the same cells drawn at a
higher resolution, not as a sheet with more cells. So a new component either uses a
`Pic Num` that the sheet has (the picture of an existing component, or a cell your mod's
sheet redraws, which changes it for every component that uses it), or a number past the
sheet with a `Comp_NNN` portrait of its own, in which case lists show no icon for it.
`opense4-sdk check` warns about a `Pic Num` that is not a cell of the sheet.

### Technology and abilities (numbered lists)

| List | Fields of one entry | Patch list | What it does |
|---|---|---|---|
| `Number of Tech Req` | `Tech Area Req N`, `Tech Level Req N` | `requirements` | Every listed area must be at or above its level before the empire can use the component: see it in the designer, put it in a design, upgrade to it or repair it. An unknown area name is an error. A level above the area's maximum makes the component unobtainable, without a message. |
| `Number of Abilities` | `Ability N Type`, `Ability N Descr`, `Ability N Val 1`, `Ability N Val 2` | `abilities` | What the component does while it is not destroyed (on a mothballed vehicle nothing works). At most 20 are read. An unknown type is an error unless a mod declares it; an entry of type `None` is skipped. `Descr` is the line the report shows; `[%ShieldPointsGenerated]` in it is replaced by `Val 1` scaled by the mount's `Shield Percent`. Every type and its values: [abilities.md](abilities.md). |

### Weapon fields

A component is a weapon when `Weapon Type` is not `None`. Then `Weapon Target`,
`Weapon Damage At Rng`, `Weapon Damage Type` and `Weapon Reload Rate` are required.

| Field | What it does | Values |
|---|---|---|
| `Weapon Type` | How it attacks. `Direct Fire` rolls to hit. `Seeking` launches a seeker that flies to its target. `Point-Defense` rolls to hit and also fires by itself at moving seekers, fighters and drones. `Warhead` never fires: it strikes when its vehicle rams, and is what a mine explodes with. | `None` (or empty), `Direct Fire`, `Seeking`, `Point-Defense`, `Warhead`. Any other text is an error. |
| `Weapon Target` | What it may fire at. | Words joined with `\`: `Ships` (ships and bases), `Planets`, `Fighters` or `Ftr`, `Satellites` or `Sat`, `Drones`, `Seekers`, or `All` for all six. Mines, troops and weapon platforms are never targets. A word OpenSE4 does not recognise adds nothing, without a message. |
| `Weapon List Target Override`, `Weapon List Target Description` | In the original, an override replaces the target set with the category words found in it. | **Not read by OpenSE4**: it uses `Weapon Target` alone. |
| `Weapon Damage At Rng` | Damage by distance: the first number is the damage at 1 square (adjacent), the second at 2, and so on. 0 means it cannot reach that far. The numbers need not fall with range. | Up to 20 whole numbers separated by spaces. Missing entries count as 0; entries past the 20th are never used. |
| `Weapon Damage Type` | How the damage acts (table below). | One of the damage type names, matched ignoring letter case, spaces and punctuation. **An unknown name reads as `Normal`**, without a message. |
| `Weapon Reload Rate` | Combat turns between shots: 1 fires every turn, 2 every other turn. | Whole number. OpenSE4 treats values below 1 as 1. Warheads use 0. |
| `Weapon Modifier` | A to-hit bonus for direct-fire and point-defense shots. | Percentage points, signed, optional. |
| `Weapon Seeker Speed` | A seeker's speed. | Squares per combat turn. Seekers only. OpenSE4 does not read the header's other spelling, `Weapon Speed`. |
| `Weapon Seeker Dmg Res` | A seeker's hit points while in flight. A mount does not change it. | Points. Seekers only. |
| `Weapon Family` | A grouping of weapon lines, separate from `Family`. The computer players' design templates pick their weapons by it ([ai-tables.md](ai-tables.md)). No combat rule uses it. | Whole number, optional. |
| `Weapon Display Type` | How a shot is drawn in battle. | `Beam`, `Torp` or `Seeker`. Presentation only. |
| `Weapon Display` | Which picture. | `Beam`: a 20×20 cell of `Pictures/Combat/Beams.bmp`, from 1. `Torp`: a cell of `Pictures/Combat/Torps.bmp`, from 1; 0 for no picture. `Seeker`: 0, 1 or 2, one of the three projectile pictures of the firing race's `_Main.bmp`. |
| `Weapon Sound` | The sound played when it fires. | A file name under `Sounds/` (and `Sounds/New/` for the remastered set); `.wav` is added when the name has no extension. A mod may add its own, as WAV or as OGG under the same base name. |

### Damage types

| `Weapon Damage Type` | Effect |
|---|---|
| `Normal` | Shields absorb first; then armour parts take damage before any other part. |
| `Shields Only` | Drains shields and nothing else. |
| `Quad Damage To Shields`, `Double Damage To Shields`, `Half Damage To Shields`, `Quarter Damage To Shields` | Against shields the damage counts four times, twice, half or a quarter; what gets through is converted back and goes on as `Normal`. Halving and quartering lose remainders, even against a target without shields. |
| `Skips Normal Shields` | Passes ordinary shields; phased shields still stop it. |
| `Skips All Shields` | Passes every shield, then acts as `Normal`. |
| `Skips Armor` | Shields absorb; then it hits parts that are not armour, and armour only when nothing else is left. |
| `Skips Shields And Armor` | As the one above, ignoring shields as well. |
| `Only Engines` | Shields absorb; then it hits only parts with `Standard Ship Movement`. |
| `Only Weapons`; `Only Shield Generators`; `Only Master Computers` | Ignore shields; hit only weapons, shield generators or parts with `Master Computer`. |
| `Only Boarding Parties`; `Only Security Stations`; `Only Planet Destroyers` | Shields absorb; then they hit only parts with `Boarding Attack`, `Boarding Defense` or `Destroy Planet Size`. |
| `Increase Reload Time` | Adds the damage, as combat turns, to every reload counter of the target (at most 250). Shields do not stop it; a target with a working `Master Computer` is immune. |
| `Disrupt Reload Time` | The same, and a Master Computer does not protect. |
| `Crew Conversion` | A roll from 1 to 100 at or below the damage turns the ship over to the firer. Always fails against a design that has a Master Computer. |
| `Pushes Target`; `Pulls Target` | Moves the target up to the damage in squares away from or towards the firer, then counts as damage. A ship moves another ship only when its hull's `Tonnage` is at least the target's; planets never move. |
| `Random Target Movement` | Moves the target to a random free square, then counts as damage. |
| `Plague Level 1` to `Plague Level 5` | Planets only, after shields: raises the planet's plague to that level. |
| `Only Planet Population` | Planets only: kills the damage ÷ the setting `Damage Points To Kill One Population` in millions (at least 1). |
| `Only Planet Conditions` | Planets only: lowers the planet's conditions by a tenth of the damage. |
| `Only Resupply Depots`; `Only Spaceports` | Planets only: destroys one facility with `Supply Generation` or `Spaceport`. |

The full rules, including how planets, unit groups and seekers take each type, are in
spec 04 §9.5.

### Weapon mount fields (CompEnhancement.txt)

A mount is chosen for one component entry of one design; a design may mix mounts. Every
field but `Long Name` is optional.

| Field | What it does | Values |
|---|---|---|
| `Long Name` | The mount's name in the designer's `Weap Mount` list and in reports, and the name patches use. | Text, required. |
| `Short Name` | Written before the component's name in ship reports and the battle window's weapon list. | Text. |
| `Description` | Shown in the mount's report. | Text. |
| `Code` | A short mark drawn on the icon of a mounted component in the designer and the Designs window. | Short text. |
| `Cost Percent` | Scales each of the component's three costs. | Percent; missing means 100. |
| `Tonnage Percent` | Scales its size. | Percent, 100. |
| `Tonnage Structure Percent` | Scales its structure. | Percent, 100. |
| `Damage Percent` | Scales every entry of its damage table. | Percent, 100. |
| `Supply Percent` | Scales its `Supply Amount Used`. | Percent, 100. |
| `Shield Percent` | Scales its `Shield Generation` and `Phased Shield Generation`. No other ability changes. | Percent, 100. |
| `Range Modifier` | Shifts the damage table outwards: the mounted weapon does at r squares what the bare one does at r − `Range Modifier`. | Squares, signed, 0. |
| `Weapon To Hit Modifier` | Added to the weapon's `Weapon Modifier`. | Percentage points, signed, 0. |
| `Vehicle Size Minimum` | The smallest hull `Tonnage` the mount is allowed on. | kT, inclusive, 0. |
| `Vehicle Size Maximum` | The largest. | kT, inclusive. Missing or 0: no upper bound. |
| `Comp Family Requirement` | The component families the mount changes. | Whole numbers separated by commas; empty or missing: every family. Anything that is not a number is an error. |
| `Weapon Type Requirement` | The kinds of component the mount changes. | `None`: only components that are not weapons. `Direct Fire`, `Seeking`, `Point-Defense` or `Warhead`: only that kind of weapon. `Any`: every weapon, but no non-weapon. Any letter case. Empty: OpenSE4 lets it change every component (inferred). Other text matches nothing. |
| `Vehicle Type` | The hull classes the designer offers the mount on. | Text that contains the hull class's name as written here, with matching letter case: `Ship`, `Base`, `Fighter`, `Satellite`, `Mine`, `Troop`, `Drone` or `Weapon Platform`. `Any`, in any letter case, offers it everywhere; OpenSE4 also offers an empty one everywhere. |
| `Number of Tech Req` with `Tech Area Req N`, `Tech Level Req N` | The technology the mount needs, as for components. | Patch list `requirements`. |

## How the fields work together

### What a component adds to a design

- **Space**: the design's components, at their mounted sizes, must fit in the hull's
  `Tonnage`.
- **Cost**: the design costs the hull's cost plus every component's mounted cost, per
  resource. That total sets the build time, the maintenance, scrapping returns and
  retrofit costs.
- **Structure**: the design's structure is the sum of the mounted structures; the hull adds
  none. A vehicle is destroyed when every component is destroyed. A component is either
  working or destroyed, with no partial damage.
- **Abilities**: a component's abilities count while it is working. The design rules
  (bridge, engines, bays...) count components, each once whatever it carries, and add up
  their mounted sizes: see [hulls.md](hulls.md) "Design rules".

The full set of design figures (movement, supply, cargo, shields, to-hit) is in
[hulls.md](hulls.md) "How a design's figures follow".

### Hits and repair

Each hit on a ship or base orders the candidate parts afresh: every working armour part
(one with the `Armor` ability) first, then the others in a random order where a part with
more structure tends to come earlier. The hit destroys parts in that order while its damage
covers the next part's structure; the part it cannot cover takes nothing, and what is left
of the damage waits for the next hit (for most damage types; the "Only" types lose it). So
`Tonnage Structure` decides both how much a part soaks up and how soon it is picked.

Repair restores whole components, one per repair point, choosing them group by group in
the order of the empire's repair priorities (`General Group`). A component whose
technology the owner lacks (a captured part) cannot be repaired.

### Supply

| Use | Supply spent |
|---|---|
| Each sector moved, each warp, each Attack step and Sweep Mines order | The mounted `Supply Amount Used` of every working part with `Standard Ship Movement`, `Movement Bonus` or `Extra Movement Generation` above 0, added up, then scaled by the race's supply trait. |
| Each shot | The weapon's mounted `Supply Amount Used`. |
| Each end of turn while cloaked | The mounted `Supply Amount Used` of the working parts with `Cloak Level`. |
| A stellar manipulation | The part's mounted `Supply Amount Used`, which the vehicle must have. |
| Use Component (`Emergency Energy`, `Emergency Resupply`) | Nothing. |

A ship (not a base) whose supply is 0 moves at most 1 sector a turn and cannot fire.
Supply capacity comes from `Supply Storage` on the hull and the components.

### Families

- **Restrictions** count the whole family. A component limited to N per vehicle may not
  share the design with N or more **other** components of its `Family`, whatever their own
  limits, numerals or mounts.
- **Only Latest** in the designer (and the Weapons Report) walks the offered components in
  file order and keeps only the last of each run of neighbours that share a family.
- **Upgrade** replaces each component of a design with the **last component in the file**
  of the same family whose technology the owner has; with none, the entry stays. Names,
  numerals and vehicle types play no part, and each entry keeps its mount.
- **To-hit abilities** (`Combat To Hit Offense Plus` and the other three) add only the best
  value of each family; different families add up, and the hull's value always adds. Two
  parts of one family therefore do not stack.
- **Mounts** with a `Comp Family Requirement` change only the families they list.

So the order of records in the file matters, and new versions of a part belong after the
old ones (`after` in a patch). Give a new line of parts a family number of its own.

### Weapons in combat

A weapon fires at its target when it is working, is not a warhead, its reload counter is
0, its target's category is in `Weapon Target`, and its damage at that distance is above 0.
A ship or fighter group also needs supply above 0; bases, ships with a working
`Quantum Reactor`, satellites, drones, weapon platforms and planets do not. Weapons fire in
the design's order. Distances are combat squares, counted diagonally like straight lines,
to the nearest square of a large target such as a planet.

The chance to hit for a direct-fire or point-defense shot is

```text
chance = base + offence + system bonus + Weapon Modifier + mount's Weapon To Hit Modifier
         − defence − per-square penalty × distance − sensor interference
```

clamped to 1–99 % (the base and the per-square penalty are the settings
`Combat Base To Hit Value` and `Combat To Hit Modifier Per Square Distance`). Seekers,
warheads and rams never roll. The offence and defence terms come from abilities, crew
experience and the race: spec 04 §7.

- **Seekers** appear on the launcher's square and wait a combat turn, then move up to
  `Weapon Seeker Speed` squares each turn towards the target. The damage on impact is the
  table entry for the number of squares flown so far (the 20th entry beyond 20), so a 0 in
  the table ends the flight there. Point-defense and other weapons can shoot a seeker down;
  it has `Weapon Seeker Dmg Res` hit points.
- **Point-defense** fires by itself, outside the firing piece's target budget, whenever a
  hostile seeker, fighter group or drone group in reach moves, at most once per reload.
- **Warheads** count with their largest table entry (with the mount). When a vehicle
  rams, its warheads that can hit the target and all the target's own warheads explode and
  add to the blow; a drone's warheads strike one by one, each with its own damage type. A
  mine's warheads strike the vehicle it chose, one after another, straight at the
  components: shields do not help against mines.
- **Troops** fight on the ground with the largest table entry of each of their weapons,
  added up.
- **Fighter groups** fire identical weapons (same component and mount) together: each rolls
  to hit and the hits add up into one blow.

### What a mount does

A mount **applies** to a component when the component meets its `Weapon Type Requirement`
and its family is on the mount's `Comp Family Requirement` list (or the list is empty). A
mount that does not apply changes nothing about that component.

When it applies, each value is worked out once and rounded to the nearest whole number,
halves to even:

| Value | Mounted value |
|---|---|
| size | size × `Tonnage Percent` / 100 |
| structure | structure × `Tonnage Structure Percent` / 100 |
| each cost | cost × `Cost Percent` / 100 |
| supply per use | `Supply Amount Used` × `Supply Percent` / 100 |
| shields | the component's summed `Shield Generation` (or `Phased Shield Generation`) × `Shield Percent` / 100 |
| damage at r squares | D(i) × `Damage Percent` / 100, at most 50,000, where i = r − `Range Modifier`, kept between 1 and 20 |
| to-hit | `Weapon Modifier` + `Weapon To Hit Modifier` |

Because the index is kept between 1 and 20, a mounted weapon does its range-1 damage at
every distance up to 1 + `Range Modifier`, and its 20th entry at every distance past 20 +
`Range Modifier`. For example, a bare table that starts `30 30 20 0` with a mount of
`Range Modifier` 2 and `Damage Percent` 150 does 45 at 1 to 4 squares, 30 at 5 and nothing
from 6 on. A bare weapon (or one whose mount does not apply) does nothing past 20 squares.

The mounted values are the component's values everywhere: in the design's size, cost and
structure, in the hull's percentage rules, in maintenance, in retrofits (which pair parts
by component and mount) and in battle. A mount never changes the reload rate, the seeker's
speed or hit points, the targets, the damage type, or any ability but the two shield ones.

Where a mount may go:

- **The designer offers** a mount when the empire has its technology, the hull's class is
  named in its `Vehicle Type`, the hull's `Tonnage` is within its size bounds, and it
  applies to some component the hull can carry.
- **A design is valid** only when every mounted entry's mount allows the hull's `Tonnage`
  ("A weapon mount is not allowed on a hull of this size") and its technology is known
  ("A weapon mount is beyond our technology"). These two checks hold even for a mount that
  does not apply to its component; `Vehicle Type` is not checked here.
- **Computer players** give each part the last mount in the file that they have researched,
  that the designer would offer for it, and that still lets the design fit.

## Changing them with patches

These examples use invented components and mounts; text in angle brackets stands for a
record of your data set.

A new weapon written out in full, so it does not depend on any component of the data set:

```toml
# data/lances.toml
[[components.add]]
name = "Lantern Lance"
after = "<a weapon of your data set>"       # where it shows in the lists

[components.add.set]
# In alphabetical order: a patch may set fields in any order.
"Cost Minerals" = 60
"Cost Organics" = 0
"Cost Radioactives" = 15
"Custom Group" = 0
"Description" = "A beam that hits hard up close and fades with distance."
"Family" = 9101                              # a family of its own
"General Group" = "Weapons"
"Pic Num" = 14                               # a cell of the sheet
"Restrictions" = "None"
"Roman Numeral" = 1
"Supply Amount Used" = 2
"Tonnage Space Taken" = 25
"Tonnage Structure" = 20
"Vehicle Type" = "Ship\\Base\\Sat"           # TOML needs \\ for one backslash
"Weapon Damage At Rng" = "33 31 27 23 19 17 13 11 9 7 6 5 4 3 2 0 0 0 0 0"   # from 1 square out
"Weapon Damage Type" = "Normal"
"Weapon Display" = 3
"Weapon Display Type" = "Beam"
"Weapon Family" = 9101
"Weapon Modifier" = 10
"Weapon Reload Rate" = 1
"Weapon Sound" = "lantern.wav"               # assets/Sounds/lantern.wav or .ogg
"Weapon Target" = "Ships\\Ftr\\Drones"
"Weapon Type" = "Direct Fire"

[components.add.add]
requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 2 }]
```

Its next version, a copy placed right after it so that Upgrade and Only Latest move to it,
with its tech requirement replaced:

```toml
[[components.add]]
name = "Lantern Lance II"
copy_from = "Lantern Lance"
after = "Lantern Lance"
set = { "Roman Numeral" = 2, "Cost Minerals" = 75, "Weapon Damage At Rng" = "38 36 31 27 22 19 15 12 10 8 7 6 5 4 3 2 0 0 0 0" }
remove = { requirements = [1] }
add = { requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 4 }] }
```

Changes to records of your data set: several at once with `match` and `all`, a list entry
swapped, and a component removed:

```toml
[[components.change]]                        # every seeker gets sturdier
match = { "Weapon Type" = "Seeking" }
all = true
set = { "Weapon Seeker Dmg Res" = 40 }

[[components.change]]
name = "<an engine of your data set>"
remove = { abilities = ["Movement Bonus"] }
add = { abilities = [{ "Ability Type" = "Movement Bonus", "Ability Descr" = "Adds 1 to the ship's speed.", "Ability Val 1" = 1 }] }

[[components.remove]]
name = "<a component of your data set>"
```

No other data table names components, so removing one leaves no reference behind and
`cascade` changes nothing. Removing a tech area with `cascade = true` removes the
components that need it.

Two mounts: one for large hulls that adds range to the lance family above, and one that
strengthens shield generators (a mount for non-weapons):

```toml
[[weapon_mounts.add]]
name = "Spinal Cradle"

[weapon_mounts.add.set]
"Code" = "S"
"Comp Family Requirement" = "9101"           # the lances' Family
"Cost Percent" = 200
"Damage Percent" = 125
"Description" = "Runs the weapon along the keel for more reach."
"Range Modifier" = 3
"Short Name" = "Spinal"
"Supply Percent" = 150
"Tonnage Percent" = 250
"Tonnage Structure Percent" = 250
"Vehicle Size Maximum" = 0                   # no upper bound
"Vehicle Size Minimum" = 600
"Vehicle Type" = "Ship"
"Weapon To Hit Modifier" = 5
"Weapon Type Requirement" = "Direct Fire"

[weapon_mounts.add.add]
requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 3 }]

[[weapon_mounts.add]]
name = "Braced Emitter Housing"
set = { "Short Name" = "Braced", "Code" = "B", "Cost Percent" = 150, "Tonnage Percent" = 150, "Shield Percent" = 140, "Vehicle Size Minimum" = 300, "Comp Family Requirement" = "<the Family number of your shield generators>", "Weapon Type Requirement" = "None", "Vehicle Type" = "Ship\\Base" }

[[weapon_mounts.change]]
name = "<a mount of your data set>"
set = { "Vehicle Size Maximum" = 900 }
```

`Comp Family Requirement` must hold numbers by the time the data loads: write your data
set's family number in place of the placeholder.

## Things to watch

- **Family 0 and shared families.** A restricted component limits every other component of
  its family, and Only Latest folds neighbours of one family into one, family 0 included.
  Leaving `Family` at 0 on a restricted part, or on several unrelated parts that sit next
  to each other, gives surprising limits and hidden parts.
- **Changing `Family` or the file order** changes what Upgrade, Only Latest and the
  computer players choose, also for designs made before.
- **Silent weapon text.** An unknown `Weapon Damage Type` reads as `Normal` and an unknown
  word in `Weapon Target` adds nothing, with no message. The original accepts only a fixed
  set of target values: each single category, `All`, and a few joined with `\` (ships with
  planets, ships with satellites, ships with planets and satellites with or without drones,
  everything but seekers, and fighters, satellites, seekers and drones together). Keep to those if your data
  should also play in the original.
- **Damage tables.** Fewer than 20 numbers leave the rest at 0. A weapon whose 20th entry
  is above 0 reaches every distance on the combat map once a mount applies to it.
- **Seekers** with `Weapon Seeker Speed` 0 never arrive, and a 0 early in the table ends
  every flight that reaches it.
- **Ships need supply to fire.** A ship design with no `Supply Storage` anywhere has no
  supply, so its weapons never fire and it crawls at 1 sector a turn.
- **`Movement Bonus`** takes the smallest value on the vehicle, so a part with a lower value
  cancels the bonus of the others ([hulls.md](hulls.md) "Movement"). The total of
  `Standard Ship Movement` on one vehicle should stay below 256: in flight only its low 8
  bits count, as in the original, while the designer shows the full figure.
- **The override's keywords** are found anywhere in the text: a word such as "examine"
  allows mines, and "weapon platform" with a space allows nothing (the keyword is
  `weapplatform`).
- **Mount `Vehicle Type`** must spell the class as `Weapon Platform`, with its capitals, to
  be offered on platforms; `WeapPlatform` is never offered there.
- **A mount with an empty `Weapon Type Requirement`** changes every component it is put
  on, armour and engines too.
- **Renaming or removing** a component or mount: the empire files players save name their
  designs' parts by name, and a design whose part is gone is left out, with a warning, when
  such an empire is loaded.
- **Errors `opense4-sdk check` reports** for these tables: a field the table does not have
  ("check the spelling"); a numbered field past its list's count; a missing required field;
  a value that should be a whole number or True/False; an unknown tech area, ability type,
  vehicle type token, restriction or weapon type; a `Comp Family Requirement` entry that is
  not a number; a name added twice; a record to change or remove that is not there. It
  warns about a `Pic Num` outside the component sheet.

## More detail

- [Spec 03](../../../spec/03-vehicles-and-abilities.md): §2.3 and §2.4 (the fields), §3
  (how abilities add up), §4.1 (Upgrade), §4.2 (design rules), §4.3 (mounts), §7 (supply),
  §13 (repair).
- [Spec 04](../../../spec/04-combat.md): §6 (firing), §7 (chance to hit), §8 (damage at
  range and mounts), §9 (damage, shields, armour, damage types), §10 (seekers,
  point-defense, warheads, fighters, mines, drones), §18.1 and §18.2 (data reference).
- [Spec 06](../../../spec/06-ui-and-assets.md) §5.2 (picture numbers) and
  [spec 02](../../../spec/02-empires-and-economy.md) §6.4 (Only Latest).
- [packages-and-data.md](../../packages-and-data.md) for the patch format and pictures.
- Computer players read these tables as the `component`, `weapon` and `mount` records of
  the rules view ([view.md](../../view.md) "The rules view"). The `weapon` record's
  `damage_at_range` list holds the data file's numbers, its first number being the damage
  at 1 square.
