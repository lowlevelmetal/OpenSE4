# Facilities

Facilities are the buildings of a colony: mines, research centres, space yards, shield
generators and the like. They are the records of `Facility.txt`, which data patches change
through the table `facilities`; each record is named by its `Name`. A facility fills one of
its planet's facility slots and does nothing by itself: everything it does comes from its
abilities, so read this chapter together with [abilities.md](abilities.md).

## The fields

OpenSE4 reads every field below. `opense4-datacheck` reports any other field a record has
as unread, and a patch that writes a field no data file of the table has is an error (a
misspelt field name, for instance).

### Name, text and picture

| Field | What it does | Values |
|---|---|---|
| `Name` | The facility's name in every list and report, and the name patches use for it (any letter case). | Text; required. A patch that adds a name the table already has is an error. |
| `Description` | Shown in the facility's report. No rule reads it, so it is where you tell players what the facility does and whether copies of it add up. | Text; optional. |
| `Facility Group` | Shown as the facility's group in its report. No rule reads it. | Text; optional. |
| `Pic Num` | The facility's picture. The small one is cell `Pic Num − 1` of `Pictures/Facilities/Facility.bmp`, a sheet of 36×36 cells read row by row; the large one is `Pictures/Facilities/Facil_NNN.bmp`, the number written with three digits (128×128). | Whole number from 1; optional. `opense4-sdk check` warns when it is past the sheet's last cell. |

A mod that brings its own portrait puts it under `assets/Pictures/Facilities/`
([packages-and-data.md](../../packages-and-data.md#pictures-sounds-music-and-fonts));
PNG files and larger pictures work as for any other picture.

### Family and level

| Field | What it does | Values |
|---|---|---|
| `Facility Family` | Ties the levels of one facility line together. Upgrades replace a facility with a higher level of the same family, the queue's Only Latest filter hides older levels by family, and a planet's to-hit bonuses stack by family (below). | Whole number; optional. Give each line a number of its own: 0 is an ordinary family, not "no family". The original's notes allow 1 to 64,000. |
| `Roman Numeral` | The facility's level within its family. An upgrade goes to the highest level the empire has researched. | Whole number; 0 means no level. |

### Limit

| Field | What it does | Values |
|---|---|---|
| `Restrictions` | Read, shown as the facility's limit in its report and given to scripts, but no rule enforces it (spec 02 §1.4). The one-yard-per-planet rule comes from the `Space Yard` ability, whatever this field says. | `None` or `One Per Planet`, the two values the original knows; optional. |

### Cost

| Field | What it does | Values |
|---|---|---|
| `Cost Minerals`, `Cost Organics`, `Cost Radioactives` | What one facility costs to build. Upgrades and scrap refunds are worked out from it. Facilities pay no upkeep. | Whole numbers, 0 or more. All three are required (a missing one is an error); a blank value reads as 0. |

### Technology (list `requirements`)

| Field | What it does | Values |
|---|---|---|
| `Number of Tech Req` | How many requirement entries follow. | Whole number; optional. With none, every empire can build the facility from the start. |
| `Tech Area Req N` | One entry's tech area. | The `Name` of a tech area of the data set; an unknown name is an error ([techs.md](techs.md)). |
| `Tech Level Req N` | The level the empire must have reached in it. | Whole number. |

Every entry must be met before the facility can be queued, or chosen as an upgrade target.
A patch adds and removes entries of the list `requirements`, each written
`{ "Tech Area Req" = "...", "Tech Level Req" = 3 }`.

### Abilities (list `abilities`)

| Field | What it does | Values |
|---|---|---|
| `Number of Abilities` | How many ability entries follow. | Whole number; at most 20 are read. |
| `Ability N Type` | The ability's name. | A name from [abilities.md](abilities.md), or one a mod declares. An unknown name is an error; `None` or a blank name is an empty slot and is skipped. |
| `Ability N Descr` | The line the facility's report shows for this ability. No rule reads it. | Text. |
| `Ability N Val 1`, `Ability N Val 2` | The ability's two values; their meaning depends on the ability. | Usually whole numbers; blank reads as 0. |

A patch adds and removes entries of the list `abilities`, each written
`{ "Ability Type" = "...", "Ability Descr" = "...", "Ability Val 1" = 0, "Ability Val 2" = 0 }`.

## How the fields work together

### Facility slots

A colony holds as many facilities as its planet's size allows, taken from the planet's
`PlanetSize.txt` record ([galaxy.md](galaxy.md)):

```text
slots = Max Facilities                       (every race there breathes the atmosphere)
slots = Max Facilities Domed                 (any race there cannot: a domed colony)
slots = trunc(slots × (100 + T) / 100)       (T: the owner's Planet Storage Space trait, if any)
```

- Each facility takes one slot, whatever it does, and stays on the planet where it was
  built. A facility built three times is three facilities, and its abilities count three
  times wherever a rule adds values up.
- When a colony's slots shrink (a race that cannot breathe the air moves in, so the domed
  number applies), it keeps every facility it has but cannot build more until it is below
  the new number.
- Asteroid fields are never colonised, so the asteroid rows of PlanetSize.txt never give
  slots.

### Building a facility

A facility is built by its colony's construction queue (spec 02 §6). Ships never build
facilities.

1. **Queueing.** The colony needs population; the empire must meet every requirement; and
   the colony must have a free slot, counting the facility items already in its queue. A
   facility with `Space Yard` is refused when the planet has a yard already or one is
   queued.
2. **Paying.** Each turn the queue pays towards its top item only, up to its rate in each
   resource. Payment is all or nothing: if the treasury cannot pay every resource's share,
   nothing is paid and nothing progresses that turn. At most one item is finished per
   queue per turn, and rate left over is not carried to the next item.
3. **The rate.** A colony without a space yard builds at the Settings.txt
   `Empire Base Planet Minerals Usage Rate` (and the organics and radioactives ones),
   changed only by the population column of the Settings table. A colony with a yard
   builds at its yard's `Space Yard` rates, changed by construction aptitude, the culture,
   racial traits and population. Emergency and slow building then scale either rate.
4. **Time.** At the top of the queue a facility takes the largest, over the three
   resources, of ceil(cost ÷ rate) turns. With invented numbers: 3,000 minerals and 600
   radioactives at a rate of 1,000 a resource take 3 turns.
5. **Completion.** If the colony has fewer facilities than slots, the item's whole count is
   added (even past the slots). Otherwise nothing is built, the progress is lost and the
   item stays at the top, to be paid for again.
6. The new facility works from then on. Each one counts as a `Facility Constructed` event
   for the colony's mood ([races.md](races.md)).

Each empire's queues whose top item is a facility with `Spaceport`,
`Resource Generation - Minerals`, `Resource Generation - Organics`,
`Resource Generation - Radioactives` or `Supply Generation` are processed first, then the
others. Repeat Build goes on building a facility item only while the colony has a free slot
and, for a yard, has no yard yet.

### Upgrades

- **Target.** For a facility on a colony, the target is the facility of the same
  `Facility Family` with the highest `Roman Numeral` above its own that the empire has
  researched; on a tie, the first in the file. With none, it has no upgrade.
- **Count.** An upgrade item converts the facilities of that family on the colony whose
  level is below the target's. How many is fixed when the item is queued. It needs no free
  slot.
- **Price**, per resource: trunc(target's cost × `Upgrade Facility Cost Percent` / 100) ×
  count, with `Upgrade Facility Cost Percent` from Settings.txt.
- The Upgrade Facilities button upgrades every colony this way and moves facility items
  already queued to the newest level.
- **Only Latest.** The Set Construction Queue window's Only Latest box hides every facility
  whose next facility in the file has the same family. Keep the levels of a family next to
  each other in Facility.txt, lowest first, each with a higher numeral than the one before;
  `after` in a patch places a new level.
- A colony's cloak and sensor levels are recalculated only at certain moments, and an
  upgrade is not one of them (spec 01 §6.9): an upgraded cloaking or sensor facility counts
  at its new level from the colony's next recalculation, such as the next facility built
  there.

### Scrapping and losing them

- Scrapping a facility refunds round(cost × p / 100) of each resource at once, where p is
  the larger of the Settings.txt `Scrap Facility Percent Returned` and the best
  `Resource Reclamation` among the empire's planets and ships in that sector.
- In a battle at the planet, hits that reach the surface can destroy facilities (spec 04
  §11). Facilities lost in a battle keep working until it ends and are removed then.
  Weapons whose damage type hits only spaceports or only resupply depots each destroy one
  facility with `Spaceport` or `Supply Generation`.
- Intelligence projects and events can damage facilities too ([intel.md](intel.md),
  [events.md](events.md)).

### Production

What a colony produces comes from its facilities (spec 02 §5.1). For minerals, organics and
radioactives (research and intelligence in brackets):

1. base = the sum of Val 1 of `Resource Generation - X` (`Point Generation - Research`,
   `Point Generation - Intelligence`) over the colony's facilities. Every facility counts,
   however small the population.
2. In a normal game: base = round(base × planet value / 100), the planet's percentage for
   that resource. (Not for research and intelligence.)
3. If the colony's best `Resource Gen Modifier Planet - X`
   (`Planet Point Generation Modifier - ...`) is m > 0: base = round(base × (100 + m) / 100).
4. base = trunc(base × p / 100), where p = 100 + the racial effect + (mood % − 100) +
   (population % − 100), at least 0.
5. Add the sum of `Solar Resource Generation - X` Val 1 × the number of stars in the system.
6. A colony that riots, has no population or is blockaded makes nothing.

In a game with finite resources the planet's value is the stock left: step 2 is skipped, the
result of step 4 is capped by the stock and drawn from it, and solar output is neither capped
nor drawn.

The empire then adds up its colonies' output system by system. If the best
`Resource Gen Modifier System - X` (`System Point Generation Modifier - ...`) among its
colonies there is s > 0, the system's total is multiplied by (100 + s) / 100, rounded. The
total reaches the treasury only if one of the empire's colonies in that system has a
facility with `Spaceport`. Without one, its home system still delivers the Settings.txt
`Home System Percentage Value With No Spaceport` percent and every other system nothing; a
race with the `No Spaceports` trait needs none. What is not delivered is lost.

`Generate Points ...` abilities add flat amounts outside these steps, and
`Resource Storage - ...` abilities raise how much of each resource the empire can keep
(spec 02 §5.6).

### What facilities usually carry

Most facility abilities act at one of three scopes:

- **Colony:** the colony's own facilities (sometimes with the planet's own abilities).
- **System:** the empire's colonies in the system, and for many abilities its ships there
  too. Usually only the largest value in the system counts, so a second facility of the
  kind adds nothing there.
- **Sector:** the empire's objects in the planet's sector.

| Role | Abilities | Notes |
|---|---|---|
| Spaceport | `Spaceport` | One in a system delivers the output of all the empire's colonies there. |
| Space yard | `Space Yard`, one entry per resource: Val 1 the resource (1 minerals, 2 organics, 3 radioactives), Val 2 its rate | Lets the colony build ships and bases and sets its queue's rate. One yard facility per planet. |
| Resource extraction | `Resource Generation - Minerals`, `- Organics`, `- Radioactives`; `Solar Resource Generation - ...`; `Resource Gen Modifier Planet - ...` and `Resource Gen Modifier System - ...` | See Production above. |
| Research | `Point Generation - Research`; `Planet Point Generation Modifier - Research`, `System Point Generation Modifier - Research` | Points go to research ([techs.md](techs.md)). |
| Intelligence | `Point Generation - Intelligence`; the matching planet and system modifiers | Points go to intelligence projects ([intel.md](intel.md)). |
| Storage | `Resource Storage - Mineral` (singular), `Resource Storage - Organics`, `Resource Storage - Radioactives`; `Cargo Storage` | The empire's storage caps; the planet's cargo space for units and population. |
| Supply | `Supply Generation` | A resupply depot for the sector. |
| Repair | `Component Repair` | Repairs the empire's vehicles in the sector. |
| Sensors | `Sensor Level`, `Long Range Scanner - System` | The colony's sensor levels; inspecting foreign ships in the system. |
| Cloaking | `Cloak Level` | A colony with a cloak level of 2 or more in some sight type can cloak, at no cost. |
| Planetary shields | `Planet - Shield Generation`, `Shield Generation`, `Phased Shield Generation`; `Shield Modifier - System` | All add to the planet's shields in battle. |
| Planetary combat | `Combat To Hit Offense Plus`, `Weapons Always Hit`, `Combat Modifier - System`, `Damage Modifier - System`, `Planet - Change Ground Defense` | A planet fires the weapons of the weapon platforms in its cargo; facilities carry no weapons themselves (spec 04 §11). |
| Population and mood | `Change Population Happiness - System`, `Planet - Change Population Happiness`, `Modify Reproduction - System`, `Change Population - System`, `Plague Prevention - System` | |
| The planet itself | `Planet - Change Minerals Value` (and the other two), `Planet - Change Conditions`, `Planet Value Change - System`, `Planet Conditions Change - System`, `Planet - Change Atmosphere` | |
| Training | `Ship Training`, `Fleet Training`, `Ship Training - System`, `Fleet Training - System` | |
| Upkeep, conversion, scrap | `Reduced Maintenance Cost - System`, `Resource Conversion`, `Resource Reclamation` | |
| Protection | `Stop Planet Destroyer`, `Stop Star Destroyer`, `Stop Nebulae Creator`, `Stop Black Hole Creator`, `Stop Open Warp Point`, `Stop Close Warp Point` | Block that stellar manipulation in the system. |

[abilities.md](abilities.md) gives each one's values and the rule that reads it. Some
abilities do nothing on a facility; they are listed under "Things to watch" below.

### Starting planets and computer players

- **Starting planets** (spec 02 §9). Each starting planet gets, while it has free slots, the
  empire's best researched facility for each role, in this order: a spaceport (unless the
  race has `No Spaceports`), a space yard, a supply depot, one producer each of minerals,
  radioactives and organics, a research facility (unless every technology is known), then
  mineral producers and research facilities in turn until the slots are full. "Best" is the
  facility with the largest value of that ability; for a yard, the largest total of its
  rates. A role the empire has no facility for is skipped. So new facilities with these
  abilities, or the lack of them, change every empire's start.
- **Computer players** choose facilities by ability, not by name: the rows of their
  `Construction_Facilities` tables name abilities and how many facilities with each a colony
  should have ([ai-tables.md](ai-tables.md)). A new facility is built by them when it
  carries an ability those rows name and is the best they have researched for it.

## Changing them with patches

### A new facility line, written out in full

Two levels of an invented mineral works, the first written out in full so that it depends
on nothing in your data set but one tech area:

```toml
# data/mirefield.toml
[[facilities.add]]
name = "Mirefield Ore Works"

[facilities.add.set]
"Description" = "Mines the crust and keeps a reserve of what it digs."
"Facility Group" = "Resource Extraction"
"Pic Num" = 12
"Facility Family" = 7301
"Roman Numeral" = 1
"Cost Minerals" = 900
"Cost Organics" = 0
"Cost Radioactives" = 150
"Restrictions" = "None"

[facilities.add.add]
requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 1 }]
abilities = [
  { "Ability Type" = "Resource Generation - Minerals", "Ability Descr" = "Digs 600 minerals a turn.", "Ability Val 1" = 600 },
  { "Ability Type" = "Resource Storage - Mineral", "Ability Descr" = "Stores 4000 more minerals.", "Ability Val 1" = 4000 },
]

[[facilities.add]]
name = "Mirefield Ore Works II"
copy_from = "Mirefield Ore Works"
after = "Mirefield Ore Works"
set = { "Roman Numeral" = 2, "Cost Minerals" = 1300 }

[facilities.add.remove]
abilities = ["Resource Generation - Minerals"]
requirements = [1]

[facilities.add.add]
requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 3 }]
abilities = [{ "Ability Type" = "Resource Generation - Minerals", "Ability Descr" = "Digs 850 minerals a turn.", "Ability Val 1" = 850 }]
```

The second level copies the first, takes the next numeral, sits right after it in the file
(so Only Latest and upgrades see one line), and swaps its production entry and its
requirement. The patch numbers both lists again and sets their counts.

### Faster yards

To change a space yard of your data set, remove its `Space Yard` entries (naming an entry by
its type removes every entry of that type) and add new ones:

```toml
[[facilities.change]]
name = "<a space yard facility of your data set>"

[facilities.change.remove]
abilities = ["Space Yard"]

[facilities.change.add]
abilities = [
  { "Ability Type" = "Space Yard", "Ability Descr" = "Spends 2500 minerals a turn.", "Ability Val 1" = 1, "Ability Val 2" = 2500 },
  { "Ability Type" = "Space Yard", "Ability Descr" = "Spends 2500 organics a turn.", "Ability Val 1" = 2, "Ability Val 2" = 2500 },
  { "Ability Type" = "Space Yard", "Ability Descr" = "Spends 2500 radioactives a turn.", "Ability Val 1" = 3, "Ability Val 2" = 2500 },
]
```

To change one entry only, name it by a table of its fields:
`remove = { abilities = [{ "Ability Type" = "Space Yard", "Ability Val 1" = 2 }] }`.

### Every level of a family, and a removal

```toml
[[facilities.change]]
match = { "Facility Family" = 7301 }   # both Mirefield levels
all = true
set = { "Facility Group" = "Mining" }

[[facilities.remove]]
name = "<a facility of your data set>"
```

Nothing in the data files names a facility, so removing one needs no `cascade` and leaves
nothing behind to fix. Think about the roles it filled, though (above): a data set without
any facility carrying `Spaceport` or `Space Yard` gives starting planets neither.

## Things to watch

- **Ability names must be exact**, apart from letter case and spacing:
  `Resource Storage - Mineral` is singular while the other two are plural, and
  `Generate Points Minerals` has no dash. A wrong name stops the data from loading:
  `Facility.txt [Mirefield Ore Works] (...): unknown ability type 'Resource Storage - Minerals'`.
- **Values that are not whole numbers read as 0 without an error** (`600.5`, `600 kT`). Only
  `Cloak Level` and `Sensor Level` take text, a sight type, in Val 1.
- **The cost fields are required.** A record written out without them fails with
  `missing field 'Cost Organics'`; a number field holding text fails with
  `'Facility Family' should be a whole number, not '...'`.
- **An unknown tech area** in a requirement is an error: `unknown tech area '...'`.
- **At most 20 abilities.** A patch that adds a 21st is an error: the entry is past the end
  of its list and nothing would read it.
- **`Restrictions` limits nothing.** Only `Space Yard` limits a facility to one per planet.
  The queue accepts a second copy of any other facility, and a system-wide ability on two
  colonies of a system usually counts once, the larger value, so the second adds nothing.
  The exception is the combat totals `Combat Modifier - System`, `Damage Modifier - System`
  and `Shield Modifier - System`: there each colony adds its own best value (spec 04 §7).
- **Abilities that do nothing on a facility:**
  - `Emergency Energy` and `Emergency Resupply`: the Use Facility order has no effect
    (spec 03 §8);
  - `Shield Regeneration`: planets never regenerate shields in combat;
  - `Combat To Hit Defense Plus` and `Minus`: a planet's defence is a fixed setting;
  - `Emissive Armor`, `Armor`, `Medical Bay`, the movement and supply-storage abilities:
    rules read them from vehicles only;
  - the two "bad chance" abilities, for events (`Change Bad Event Chance - System`) as well
    as for intelligence (`Change Bad Intelligence Chance - System`): only entries that
    belong to no empire count;
  - `Resource Generation - ...` and `Point Generation - ...` work only on facilities, not as
    a planet's own abilities or on ships (use `Generate Points ...` or
    `Remote Resource Generation - ...` there).
- **Population.** Facilities work without population for most rules, but produce nothing
  without it. In OpenSE4 a colony without population also raises no planetary shields
  (inferred) and adds nothing to its empire's system combat totals.
- **Family and file order.** Levels of one family that are not next to each other in the
  file show up as separate lines under Only Latest; levels whose numerals do not rise make
  upgrades skip or repeat them.
- **Pictures.** `Pic Num` past the sheet's end is only a warning; the facility then shows no
  small picture.

## More detail

- Spec: [02 Empires and economy](../../../spec/02-empires-and-economy.md) §1.4 (the file),
  §1.5 (facility abilities, their scopes and stacking), §2 (domes and capacities), §5
  (production and delivery), §6 (construction queues, upgrades, scrapping), §9 (starting
  planets); [04 Combat](../../../spec/04-combat.md) §7, §9.2 and §11 (planets in battle);
  [01 Galaxy and setup](../../../spec/01-galaxy-and-setup.md) §5.5 (planet sizes) and §6.9
  (cloaked colonies); [03 Vehicles and abilities](../../../spec/03-vehicles-and-abilities.md)
  §7 (supply depots) and §13 (repair).
- Patches: [packages-and-data.md](../../packages-and-data.md#data-patches), and the list
  names in "Lists in a record" there.
- What scripts and computer players read: the rules view's
  [`facility`](../../view.md#facility) record (index, name, description, group, family,
  numeral, restriction, cost, requirements and abilities; not the picture), and a colony's
  `facilities`, `facility_slots` and `queue` in the [`colony`](../../view.md#colony) record
  ([view.md](../../view.md#the-rules-view)).
- Related chapters: [abilities.md](abilities.md), [galaxy.md](galaxy.md) (planet sizes),
  [techs.md](techs.md), [races.md](races.md), [ai-tables.md](ai-tables.md),
  [settings.md](settings.md) (Settings.txt and the default colony types).
