# The data files

Most of the game's content is data: every component, hull, facility, technology, race,
planet type, event and setting comes from plain text files in the installed game's `Data`
folder, and the computer players' behaviour from the tables in `Ai/` and the race folders.
This chapter explains how OpenSE4 reads those files and how a mod changes them. The
chapters after it take one group of tables each and say what every field does in
OpenSE4's rules.

Everything here is about OpenSE4's own reading of the format, written from OpenSE4's specs
([docs/spec](../../../spec/)) and its loader (`src/ruleset/`). The patch format itself is
specified in [Mod packages and data patches](../../packages-and-data.md#data-patches).

## The tables

| Chapter | Data file | Patch table | Records named by |
|---|---|---|---|
| [Components and weapon mounts](components.md) | `Components.txt` | `components` | `Name` |
| | `CompEnhancement.txt` | `weapon_mounts` | `Long Name` |
| [Hulls](hulls.md) | `VehicleSize.txt` | `vehicle_sizes` | `Name` |
| [Facilities](facilities.md) | `Facility.txt` | `facilities` | `Name` |
| [Abilities](abilities.md) | (entries on other records) | `abilities.declare` for new names | |
| [Technology](techs.md) | `TechArea.txt` | `tech_areas` | `Name` |
| [Races, traits, cultures and happiness](races.md) | race files in `Pictures/Races/<Style>/` | `ai.general` (the race's file) | |
| | `RacialTraits.txt` | `racial_traits` | `Name` |
| | `Cultures.txt` | `cultures` | `Name` |
| | `Happiness.txt` | `happiness` | `Name` |
| [The galaxy](galaxy.md) | `PlanetSize.txt` | `planet_sizes` | `Name` |
| | `SectType.txt` | `sector_types` | no name: `match` or `index` |
| | `StellarAbilityTypes.txt` | `stellar_ability_types` | `Name` |
| | `SystemTypes.txt` | `system_types` | `Name` |
| | `QuadrantTypes.txt` | `quadrant_types` | `Name` |
| [Events](events.md) | `Events.txt` | `events` | `Type` |
| [Intelligence projects](intel.md) | `IntelProjects.txt` | `intel_projects` | `Name` |
| [Combat strategies and formations](combat.md) | `DefaultStrategies.txt` | `strategies` | `Name` |
| | `Formations.txt` | `formations` | `Name` |
| [Settings and lists](settings.md) | `Settings.txt` | `settings` | one record |
| | the name lists | `empire_names`, `system_names`, ... | lists of names |
| | `DefaultDesignTypes.txt`, `DefaultColonyTypes.txt` | `design_types`, `colony_types` | `Name` |
| [The computer players' tables](ai-tables.md) | `Ai/*_AI_*.txt`, race folders | `ai.anger`, `ai.research`, ... | per table |

## How the files are written

A data file is a header, then records between `*BEGIN*` and `*END*`. A record is a run of
`Key := Value` lines; a blank line ends it. This is invented content in that format:

```text
*BEGIN*
Name                  := Lantern Drive
Description           := A small, quiet engine.
Tonnage Space Taken   := 10
Cost Minerals         := 30
Vehicle Type          := Ship\Base
Number of Tech Req    := 1
Tech Area Req 1       := Drive Systems
Tech Level Req 1      := 2
Number of Abilities   := 1
Ability 1 Type        := Standard Ship Movement
Ability 1 Val 1       := 1
*END*
```

- **Values are text, whole numbers or switches.** OpenSE4 counts in whole numbers
  everywhere; a switch is `True` or `False`. Some fields hold several values: a list
  separated by backslashes (`Ship\Base`), or numbers separated by spaces (a weapon's damage
  at each range).
- **Numbered lists.** Repeated parts of a record are numbered: `Number of Abilities` and
  `Ability 1 Type`, `Ability 1 Val 1`, ...; `Number of Tech Req` and `Tech Area Req 1`,
  `Tech Level Req 1`, ... The count says how many entries there are. A patch adds and
  removes whole entries and OpenSE4 renumbers the list ([below](#changing-the-data)).
- **Records refer to each other by name.** A component names the tech areas it needs, a
  quadrant type names its system types, a race names its culture. Rename or remove a record
  and what names it breaks, which `opense4-sdk check` reports.
- **Order matters.** OpenSE4 refers to records by their position: saved games do, the
  scripts' rules view does (`component index`), and an Upgrade replaces a component by the
  *last* one of its family in file order. A patch places new records with `after` and
  `before`.
- **Field names match in any letter case**, and the original's misspellings are accepted
  where the classic files have them.

## How OpenSE4 reads them

- **Every problem names the file, the line and the record**: a missing required field, a
  value that should be a number, an unknown ability or tech area. A data set with errors
  does not load. `opense4-datacheck` checks a data folder on its own; `opense4-sdk check`
  checks a mod's patches applied to your game.
- **Fields OpenSE4 does not read are counted**, so you can tell a field that does nothing
  from one that does. `opense4-datacheck` lists them. The chapters say which fields are
  read but used by no rule.
- **Abilities are a closed list.** An ability name the game does not know is an error,
  unless a mod declares it ([Abilities](abilities.md#declaring-new-abilities)).
- **The rules view.** Computer players and rules scripts read the data as OpenSE4 loaded
  it, patches applied: `view.rules` and `game.rules` ([view.md](../../view.md#the-rules-view)).

## Changing the data

There are three ways, from the most to the least combinable:

1. **Data patches** (`data/*.toml`): operations on records by name. Several mods can patch
   the same file, each applying to what the mods before it made. Use these.
2. **Data generators** (`data/*.py`): Python that returns a patch, for records that follow
   a pattern, such as twelve levels of a weapon ([A weapon line from a generator](../tutorials/weapon-line.md)).
3. **Replacement files** (`data/Components.txt`): a whole file in place of the game's, as
   mods for the original were made. Two mods that replace the same file cannot be combined,
   and a replacement copies the original's content into your mod, which you must not share.
   Prefer patches.

### A patch, step by step

1. **Look up what you change.** `opense4-sdk dump --out=dump` writes the data set as the
   game reads it. Find the record's name and the field names there.
2. **Write the operation** in a `.toml` file in your mod's `data/`. Files apply in the
   order of their names (`10-tech.toml` before `20-ships.toml`), operations in the order
   written.
3. **Check it**: `opense4-sdk check mymod`. Then dump again with the mod
   (`opense4-sdk dump mymod --out=dump2`) and compare the two folders: that is exactly what
   your mod does.
4. **Test it** with a test that reads the patched data ([Testing and measuring](../testing-and-measuring.md)).

### The operations

```toml
# Change fields of one record.
[[components.change]]
name = "<a component of your data set>"
set = { "Cost Minerals" = 40, "Supply Amount Used" = 2 }

# Add a record: a copy of another with some fields changed, placed after it.
[[components.add]]
name = "Lantern Drive II"
copy_from = "Lantern Drive"
after = "Lantern Drive"
set = { "Roman Numeral" = 2 }

# Add or remove entries of a record's lists, by the entry's own field names.
[[components.change]]
name = "Lantern Drive II"
remove = { abilities = ["Standard Ship Movement"] }
add = { abilities = [{ "Ability Type" = "Standard Ship Movement", "Ability Val 1" = 2 }] }

# Change every record whose fields have these values.
[[components.change]]
match = { "Weapon Type" = "Seeking" }
all = true
set = { "Weapon Reload Rate" = 2 }

# Remove a record, and what refers to it.
[[tech_areas.remove]]
name = "<a tech area of your data set>"
cascade = true
```

| Key | Means |
|---|---|
| `name` | The record (any letter case); for `add`, the new record's name |
| `match`, `index`, `all` | Instead of `name`: the record whose fields have these values, the record at this position (from 1), and `all = true` for every record that matches (otherwise more than one match is an error) |
| `copy_from` | `add` starts from a copy of this record (a name or a position); without it, the record has only the fields you set |
| `after`, `before` | Where an added record goes (default: the end) |
| `set` | Fields and values; a field the record lacks is added |
| `add`, `remove` | Entries of the record's lists (each table's lists are in [packages-and-data.md](../../packages-and-data.md#lists-in-a-record)) |
| `cascade` | `remove`: also remove or empty what refers to the record |
| `files` | The computer players' tables: which AI files (`"default"`, `"style:<folder>"`, `"race:<folder>"`, `"all"`) |

A `change` applies `set`, then removes list entries, then adds them. The name lists take
`add` and `remove` of names directly (`[system_names] add = ["Quillon"]`).

**Writing a record out in full** (no `copy_from`) makes a mod that fits any data set, since
it needs no record of the player's game. The [new-hull](../../../../mods/examples/new-hull/)
and [weapon-line](../../../../mods/examples/weapon-line/) examples do that. **Copying** an
existing record is shorter, but needs that record in the player's data set.

### Removing records

When the mods are applied, OpenSE4 looks for everything that still names a record a patch
removed, or renamed with `set`. Each such reference is an error that names its file and
line. Remove or change the referring records too, or remove with `cascade = true`, which
deals with them as [packages-and-data.md](../../packages-and-data.md#removing-a-record)
lists: a removed tech area takes the components, facilities, hulls, mounts, intelligence
projects and other tech areas that need it, and the AI research rows that name it; a
removed system type leaves its quadrants' lists, and so on. A culture or happiness type a
race uses cannot be removed until the race is changed.

## Files beside the data: AI tables, races and design names

The game reads more than `Data/`: the computer players' tables (`Ai/` and the race
folders' `*_AI_*.txt`), the race files of `Pictures/Races/` and the design-name lists of
`Dsgnname/`. A mod puts its own under `data/`, in the game folder's layout
(`data/Ai/Default_AI_Research.txt`, `data/Pictures/Races/Zorg/Zorg_AI_General.txt`), and
patches the existing ones with the `ai.*` tables. A new race is a folder of such files plus
its pictures under `assets/` ([Races](races.md), [The computer players' tables](ai-tables.md)).

## What a data change means for a game

A data change is game-affecting: it is part of the mod's identity, recorded in saved games
and compared in network games ([Multiplayer, saved games and identity](../multiplayer-and-saves.md)).
A game started with a mod keeps needing it: load it without the mod and OpenSE4 says
which mod is missing. A game whose mods only change data within the original's format
can still be saved for the original ([packages-and-data.md](../../packages-and-data.md#saving-for-the-original)).
