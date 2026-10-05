# Mod packages and data patches

This page is for people who make mods for OpenSE4. It covers what a mod package is,
how the game finds and orders mods, how a mod's pictures and data files are layered
over the installed game, and how **data patches** change the data files record by
record. The plan behind it is [docs/MODDING_SDK.md](../MODDING_SDK.md); this page
describes what OpenSE4 does today.

A mod never changes the installed game. OpenSE4 reads the install, then each mod over
it, and keeps the result in memory. Remove a mod from the list and the game is as it
was.

## What a mod can hold

| Folder | What | Changes the game? |
|---|---|---|
| `assets/` | Pictures, sounds, music, fonts and pointers, in the game folder's own layout | No: other players may have other pictures |
| `data/` | Data patches (`*.toml`), replacement data files (`*.txt`), data generators (`*.py`), and AI tables, race files and design-name lists in the game folder's layout | Yes |
| `ai/` | Computer players in Python (a later step: not run yet) | Yes |
| `scripts/` | Rules hooks in Python (a later step: not run yet) | Yes |
| `ui/` | Interface extensions (a later step) | No |
| `text/` | Strings and translations (a later step) | No |
| `tests/` | The mod's own tests, for `opense4-sdk test` (a later step) | No |

A mod that changes the game must be the same for every player of a game (see
"Multiplayer and saved games" below). A mod with only pictures and sounds need not.

## A package

A mod is a folder, or a `.zip` of one, with a manifest called `mod.toml` at its top:

```text
escort-hull/
  mod.toml
  data/hulls.toml
  assets/Pictures/RaceGeneric/Generic_Mini_EscortCarrier.bmp
  assets/Pictures/RaceGeneric/Generic_Portrait_EscortCarrier.bmp
```

A `.zip` may hold the files at its top or in one folder (as when you zip the mod's
folder). OpenSE4 unpacks a `.zip` once into `ModCache/` in its user folder and reads it
from there. It refuses archives with unsafe file names (`..`, absolute paths) and
archives that would unpack to more than 2 GiB.

Hidden files (names starting with `.`, such as `.git`) and `__pycache__` folders are
not part of a package.

### The manifest

```toml
[mod]
id = "example.better-carriers"     # unique: lowercase letters, digits, '.', '-', '_'
name = "Better Carriers"
version = "1.2.0"                  # whole numbers separated by dots
api = 1                            # the SDK interface the mod was written for
authors = ["Ada"]
description = "Carriers that carry more."

[requires]
"example.common-lib" = ">=1.0, <2"

[load]
after = ["example.common-lib", "example.ui-tweaks"]
```

- **id** identifies the mod everywhere: in saved games, in the lobby, on the command
  line. A reverse-domain style (`yourname.modname`) keeps ids apart.
- **version**: one to four whole numbers, `1.2.0`. `1.2` is the same as `1.2.0`.
- **api**: the SDK interface version. This OpenSE4 offers `1`; a mod that needs a newer
  one is refused with a message that says so.
- **requires**: mods that must be enabled too, each with a version range. A range is one
  or more conditions separated by commas or spaces, all of which must hold: `>=1.0`,
  `>1`, `<2`, `<=2.5`, `=1.2.3`, `1.2` (any 1.2.x), `^1.2` (from 1.2, below 2), `~1.2`
  (from 1.2, below 1.3), `*` (any version).
- **load.after**: mods this one should load after when they are enabled. It is a hint,
  not a requirement.

Unknown tables and keys are errors, so a typo does not pass unnoticed.

### Classic mods

A folder (or `.zip`) without `mod.toml` is a **classic mod**, as mods were made for the
original game: replacement data files and pictures. Its id is `classic.` and the folder
name, its version `0`.

- With a `Data` folder, the whole folder mirrors the game folder: `Data/`, `Pictures/`,
  `Ai/`, `Sounds/` and so on.
- Without one, the data files at its top that the game reads (`Components.txt`,
  `SystemNames.txt` and the others) replace the game's, and its folders mirror the game
  folder. Other files at its top, such as a readme, are left alone.

## Where mods go and how they are chosen

| | |
|---|---|
| Your mods folder | `Mods/` in OpenSE4's user folder (`~/.local/share/OpenSE4/Mods` on Linux, `%APPDATA%\OpenSE4\Mods` on Windows). Each subfolder and `.zip` there is a mod. |
| The game | `classic_settings.toml` in the user folder lists the ids of the mods to play with, in order: `[mods] enabled = ["example.common-lib", "example.better-carriers"]`. |
| One run | `opense4 --mod=PATH` (a folder or `.zip`, or the id of a mod in the mods folder), repeated for several, takes the place of the settings' list; `--no-mods` plays without mods; `--mods-dir=DIR` looks for ids in another folder. |
| The dedicated server | `opense4-server --mod=...` (every command that reads the data set takes `--mod` and `--mods-dir`), or `mods = [...]` in a setup file (paths relative to the setup file, or ids). |
| A saved game | A host loading a saved game, or processing a play-by-e-mail game, finds the game's own mods in the mods folder by id and identity when no `--mod` is given. |

### Load order

The mods load in an order that puts every mod after the mods it requires and after the
enabled mods its `[load] after` names; otherwise they keep the order you gave. A later
mod wins: its files replace earlier ones, and its patches apply to what the earlier ones
made.

These stop a mod set from loading, with a message that names the mods:

- a required mod is not enabled, or its version is outside the range;
- the same id is enabled twice;
- mods require (or load after) each other in a circle.

### Identity

Each mod has an **identity**: a hash of its manifest and every file outside `assets/`
and `ui/`, with text files' line ends read as LF (so a mod saved on Windows has the
same identity as on Linux). Changing a picture does not change it; changing a patch, a
data file or the manifest does. `opense4-sdk info` and `check` print it.

A game's mod set has an identity too: the ids, versions and identities of its mods that
change the game, in load order. It is part of the data set's identity
(`game::dataSetIdentity`), which network and e-mail games compare.

## Pictures, sounds, music and fonts

Every lookup of a picture, sound, music track, font or pointer tries the mods' `assets/`
folders first, the last mod first, then the installed game. Names match in any letter
case, on every platform. Without mods, everything is found as before:

- The mod folder that the game's `Path.txt` names is still read for fonts and pointers
  only, after the mods' files and before the game's own.
- Music: a mod can add tracks under `Music/`; the playlists in `Settings.txt` name them.

**Hull pictures.** A hull names its pictures with `Primary Bitmap Name` and `Alternate
Bitmap Name`. For a race style `Terran` and a bitmap `EscortCarrier`, the game looks for
`Pictures/Races/Terran/Terran_Mini_EscortCarrier.bmp` and
`Pictures/Races/Terran/Terran_Portrait_EscortCarrier.bmp` (also under
`Pictures/RaceNeutral/`), then `Pictures/RaceGeneric/Generic_Mini_EscortCarrier.bmp` and
`Generic_Portrait_EscortCarrier.bmp` for every race. Minis are 36×36 pixels, portraits
128×128; black is transparent. When the primary bitmap has no picture, the alternate's
is shown.

The game asks for pictures by names ending in `.bmp`; the file may hold BMP, PNG or JPG
data. Sounds and music are WAV and MP3, fonts Windows raster fonts or TrueType. OGG is not
supported yet.

## Game files

The game reads more than the data folder: the computer players' tables (`Ai/` and the
`*_AI_*.txt` files of the race folders), the race files of `Pictures/Races/` and
`Pictures/RaceNeutral/`, and the design-name lists of `Dsgnname/`. A mod puts its own in
`data/`, in the game folder's layout:

| In the mod | In the game folder |
|---|---|
| `data/Components.txt` (any data file, at the top of `data/`) | `Data/Components.txt` |
| `data/Ai/Default_AI_Research.txt` | `Ai/Default_AI_Research.txt` |
| `data/Ai/Cautious/Cautious_AI_Anger.txt` | a new minister style, `Cautious` |
| `data/Pictures/Races/Zorg/Zorg_AI_General.txt` | a new race, `Zorg` (its pictures go in `assets/Pictures/Races/Zorg/`) |
| `data/Dsgnname/Zorg.txt` | a design-name list |

A file there replaces the install's (or an earlier mod's) file of the same path; a new
path adds a file. Game files under `assets/` are ignored, because `assets/` is not part
of the mod's identity: `opense4-sdk check` warns about them.

Replacing a whole data file is how classic mods work, but two mods that replace the same
file cannot be combined. Data patches can.

## Data patches

A data patch is a TOML file in `data/`. The patches of a mod apply in the order of their
file names, and the operations of a file in the order it has them, after the mod's
replacement files. They change the records the game reads, by the data files' own field
names, before the game loads them, so every table can be patched and the game's checks
of the data still apply to the result.

```toml
# data/carriers.toml
[[components.add]]                       # a new record, a copy of another
name = "Heavy Fighter Bay"
copy_from = "Fighter Bay"
after = "Fighter Bay"                    # where it goes; default: at the end
set = { "Tonnage Space Taken" = 40, "Supply Amount Used" = 2 }

[[components.change]]                    # change fields of a record
name = "Ion Engine I"
set = { "Supply Amount Used" = 3 }

[[components.remove]]                    # remove a record
name = "Old Fighter Bay"

[[vehicle_sizes.add]]                    # a new hull, written out in full
name = "Escort Carrier"
[vehicle_sizes.add.set]
"Primary Bitmap Name" = "EscortCarrier"
"Alternate Bitmap Name" = "EscortCarrier"
"Vehicle Type" = "Ship"
"Tonnage" = 350
# ... and the hull's other fields
```

(`Fighter Bay`, `Ion Engine I` and the rest stand for records of your data set.)

### Tables

| Patch table | Data file | Records named by |
|---|---|---|
| `tech_areas` | TechArea.txt | `Name` |
| `vehicle_sizes` | VehicleSize.txt | `Name` |
| `components` | Components.txt | `Name` |
| `facilities` | Facility.txt | `Name` |
| `planet_sizes` | PlanetSize.txt | `Name` |
| `racial_traits` | RacialTraits.txt | `Name` |
| `cultures` | Cultures.txt | `Name` |
| `sector_types` | SectType.txt | (no name: `match` or `index`) |
| `stellar_ability_types` | StellarAbilityTypes.txt | `Name` |
| `system_types` | SystemTypes.txt | `Name` |
| `quadrant_types` | QuadrantTypes.txt | `Name` |
| `weapon_mounts` | CompEnhancement.txt | `Long Name` |
| `formations` | Formations.txt | `Name` |
| `happiness` | Happiness.txt | `Name` |
| `intel_projects` | IntelProjects.txt | `Name` |
| `events` | Events.txt | `Type` |
| `strategies` | DefaultStrategies.txt | `Name` |
| `settings` | Settings.txt | (one record) |
| `design_types` | DefaultDesignTypes.txt | `Name` |
| `colony_types` | DefaultColonyTypes.txt | `Name` |
| `empire_names`, `empire_types`, `emperor_names`, `emperor_titles`, `demeanors`, `system_names`, `repair_priorities` | the name lists | (lists of names) |
| `ai.anger`, `ai.politics`, `ai.settings`, `ai.general`, `ai.fleets`, `ai.speech` | `<prefix>_AI_Anger.txt` and so on | (one record) |
| `ai.research`, `ai.planet_types`, `ai.construction_facilities`, `ai.construction_vehicles`, `ai.construction_units` | `<prefix>_AI_Research.txt` and so on | (rows: `match` or `index`) |
| `ai.design_creation`, `ai.strategies` | `<prefix>_AI_DesignCreation.txt`, `<prefix>_AI_Strategies.txt` | `Name` |

### Operations

Each operation is a TOML table under `[[<table>.add]]`, `[[<table>.change]]` or
`[[<table>.remove]]`.

| Key | In | Meaning |
|---|---|---|
| `name` | all | `add`: the new record's name. `change`, `remove`: the record with this name (any letter case). |
| `match` | change, remove | `{ "Field" = "value", ... }`: the record whose fields all have these values. |
| `index` | change, remove | the record at this position in the file, from 1. |
| `all` | change, remove | `true`: every record that matches, not exactly one (otherwise several matches are an error). |
| `copy_from` | add | the record to start from: a name, or a position. Without it the record has only the fields you set. |
| `after`, `before` | add | the name of the record to place it after or before. |
| `set` | add, change | fields and their values: text, whole numbers or `true`/`false` (written `True`/`False` in the data file). A field the record lacks is added. |
| `add`, `remove` | add, change | entries of the record's lists (below). |
| `cascade` | remove | `true`: also remove what refers to the record (below). |
| `files` | `ai.*` | which files of the table: `"default"` (`Ai/Default_AI_*.txt`), `"style:<folder>"` (`Ai/<folder>/`), `"race:<folder>"` (a race folder); a list of these; or `"all"`, the default: every file of the table. |

A `change` applies `set` first, then removes list entries, then adds them. A table of
one record (`settings`, `ai.anger`, ...) needs no `name`. The positions a record keeps in
the file matter to the game (saved games refer to records by position, and a component
upgrade picks the last of its family in file order), so `after` and `before` let you
place a new record.

### Lists in a record

Some fields form numbered lists, such as `Number of Abilities` with `Ability 1 Type`,
`Ability 1 Val 1`, and so on. A patch adds and removes **entries**, and OpenSE4 numbers
the list again and sets its count. An entry's fields are the data file's field names
without the number:

```toml
[[components.change]]
name = "Ion Engine I"
remove = { abilities = ["Movement Bonus"], requirements = [1] }
add = { abilities = [{ "Ability Type" = "Movement Bonus", "Ability Val 1" = 2 }] }
```

An entry to remove is named by its first field's value (`"Movement Bonus"`), its position
from 1 (`1`), or a table of fields that must all match.

| Table | Lists | An entry's fields |
|---|---|---|
| tech_areas, vehicle_sizes, components, facilities, weapon_mounts, intel_projects | `requirements` | `Tech Area Req`, `Tech Level Req` |
| vehicle_sizes, components, facilities, system_types | `abilities` | `Ability Type`, `Ability Descr`, `Ability Val 1`, `Ability Val 2` |
| stellar_ability_types | `abilities` | `Ability Type`, `Ability Chance`, `Ability Descr`, `Ability Val 1`, `Ability Val 2` |
| system_types | `objects` | `Obj Physical Type`, `Obj Position`, `Obj Stellar Abil Type`, `Obj Size`, `Obj Age`, `Obj Color`, `Obj Luminosity`, `Obj Atmosphere`, `Obj Composition` |
| quadrant_types | `system_types` | `Type Name`, `Type Chance` |
| formations | `positions` | `Position Xpos`, `Position Ypos`, `Position Type` |
| racial_traits | `values`, `required_traits`, `restricted_traits` | `Value`; `Required Trait`; `Restricted Trait` |
| intel_projects | `source_messages`, `target_messages` | `Source Message`; `Target Message Title`, `Target Message` |
| events | `messages`, `start_messages` | `Message Title`, `Message`; `Start Message Title`, `Start Message` |
| ai.general | `characteristics_1` to `_3`, `advanced_traits_1` to `_3` | `Race Opt 1 Characteristic Type`, `Race Opt 1 Characteristic Amount`; `Race Opt 1 Adv Trait` |
| ai.design_creation | `must_have`, `misc_abilities` | `Must Have Ability`; `Misc Ability Name`, `Misc Ability Spaces Per One` |
| ai.construction_facilities | `entries` | `Facility Ability`, `Facility Amount` |
| ai.construction_vehicles | `entries` | `Entry Type`, `Entry Planet Per Item`, `Entry Must Have At Least` |
| ai.construction_units | `entries` | `Entry Type`, `Entry Maximum in kT` |

A record has at most 20 abilities: the game reads no more.

### The name lists

```toml
[system_names]
add = ["Kepler", "Lantern"]
remove = ["Old Name"]
```

### The computer players' tables

```toml
[[ai.anger.change]]
files = "default"
set = { "Regular Decrease" = -3 }

[[ai.research.add]]
files = ["default", "style:Cautious"]
set = { "AI State" = "Exploration", "Tech Area Name" = "Propulsion", "Tech Area Level" = 2, "Tech Area Min Percent" = 20 }

[[ai.general.change]]
files = "race:Zorg"
set = { "Culture" = "Industrious" }
```

An operation applies to every file of the table that `files` names; it is an error when
it applies to none. A race without a file of its own uses the default file, so patching
`"default"` reaches it too.

### Mistakes are errors

Every problem names the mod, the patch file, its line and the record:

```text
mod example.carriers, data/carriers.toml:7: Components.txt [Heavy Fighter Bay]: components records have no field 'Tonage Space Taken' (check the spelling)
```

- An unknown table, operation or key, a list or an entry field the table does not have.
- A field no data file of the table has and the game does not read: typos do not pass
  silently. A field the data files have that OpenSE4 does not read yet is accepted.
- A numbered field past the end of its list (`Ability 3 Type` when `Number of Abilities`
  is 2): use `add` for list entries.
- A record that is not there, a name that is there already, several records that match
  without `all = true`.
- Whatever the game's own checks of the data find, such as an unknown tech area or a
  value that should be a number, names the patch that wrote the field or the record.

### Removing a record

When every mod is applied, OpenSE4 looks for what still names a record a patch removed
(or renamed with `set`). Each such reference is an error that says where it is. Remove
or change the referring records too, or remove with `cascade = true`, which deals with
them as the table below says.

| What is removed | What refers to it | With `cascade = true` |
|---|---|---|
| a tech area | the `Tech Area Req` of tech areas, hulls, components, facilities, mounts and intelligence projects; `Tech Area Name` of AI research rows | those records go too (and what refers to them, in turn) |
| a system type | a quadrant type's `Type Name` | that entry of the quadrant's list goes |
| a stellar ability type | a system type's `WP Stellar Abil Type`, `Obj Stellar Abil Type` | the field becomes `None` |
| a racial trait | `Required Trait`, `Restricted Trait` of other traits; a race's advanced traits | those list entries go |
| a culture or happiness type | a race's `Culture`, `Happiness Type` | refused: change the race first |
| a planet size | `Minimum Planet Size for Type` of AI planet types | the field is emptied |
| a design type | `Design Type` of AI design templates | those templates go |
| a combat strategy, formation | the AI's default strategy and formation fields | the field is emptied |
| a colony type | `Construction Queue Type` of AI facility queues | those queues go |

A later mod that adds the name again leaves nothing to refer to a removed record.

## New ability names

The game knows a fixed list of ability names, and data with another name is an error.
A mod can declare new names, so its data may use them:

```toml
[[abilities.declare]]
name = "Hyperspace Anchor"
combine = "max"          # how values combine over a list: "sum" (the default), "max" or "min"
```

Any mod may then put the ability on components, facilities, hulls and system types
like any other. A declared ability has no effect of its own yet: rules scripts (a later
step) will give them effects, and read them through the engine's `game::Rules` (the value
on a design, component, facility, hull, colony or system, combined as declared: the sum,
the largest or the smallest `Val 1`). Two mods may declare the same name if they agree on
how it combines. The game's own names need no declaring.

## Data generators

A generator (`data/*.py`) is a Python script that builds records, such as twelve levels
of a weapon line. It runs when the data loads and returns an ordinary patch: a table
shaped like a patch file. Generators need the script runtime, which a later step of the
SDK brings; until then a mod with one is refused with a message that says so.

## Multiplayer and saved games

- A new game records its mods (ids, versions, identities, and whether each changes the
  game) in the saved game (format 9).
- Loading a game whose game-affecting mods differ from yours is refused, and the message
  names what is missing or different. Mods with only pictures, sounds or interface may
  differ.
- In a network game the lobby carries the host's mods (protocol 7). A player whose
  game-affecting mods differ is refused with the same kind of message; pictures and
  sounds may differ.
- Saved games of format 8 and older (no mods) load as before.

## opense4-sdk

```sh
opense4-sdk new data mymod --id=me.mymod     # a new mod from a template: assets, data, ai or rules
opense4-sdk check mymod                       # everything below, against your installed game
opense4-sdk info mymod                        # what it is, holds and its identity
opense4-sdk dump mymod other.zip --out=dump   # the data set with these mods, as data files
opense4-sdk pack mymod                        # mymod's id-version.zip, with its identity recorded
```

`check` reads the manifest, finds the mods it requires (in the mods folder, or given
with `--mod`), applies every patch to your installed game (`--data=DIR` for another) and
reports every error above. It also checks the mod's files: the pictures its hulls name,
component and facility picture numbers against their sheets, formats the game cannot
read, pictures no hull uses, and files where the game does not look. It exits with 1
when it finds errors.

`dump` writes the data folder's files and the AI tables as the game would read them
with the mods, never into the installed game. `pack` leaves out hidden files and adds
`mod.identity`, which records the identity; a package whose files no longer match it
gets a warning.

`run`, `test`, `arena` and `publish` come with later steps of the SDK.

## Example: a new hull with pictures

The test fixture `tests/fixtures/mods/escort-hull` is a complete small mod: a manifest, a
patch that adds the Escort Carrier hull written out in full (so it needs no record of
your data set), and its two pictures in `assets/Pictures/RaceGeneric/`. Start OpenSE4
with `--mod=tests/fixtures/mods/escort-hull` and the hull is in the ship designer for
every race.
