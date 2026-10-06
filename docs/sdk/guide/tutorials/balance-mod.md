# A first balance mod

In this tutorial you change the balance of the game with data patches alone: records changed
by name, records chosen by what they hold, list entries added, a setting, a computer
players' table, a name list, and a removal that takes everything depending on it along. The
finished mod is the example [mods/examples/balance](../../../../mods/examples/balance/),
*Measured Pace*. It names a few records of the classic data set, so it is made for that data
set. The chapter behind it is [The data files](../data/README.md).

```sh
opense4-sdk new --from-example balance pace --id=me.pace    # the finished mod, to compare with
```

## 1. Look at the data

```sh
opense4-sdk dump --out=before
```

writes your installed game's data, as the game reads it, into `before/`: `Data/*.txt` and
the computer players' tables under `Ai/`. That is where you find the records' names, the
field names and the values you are about to change. (It never writes into the installed
game.)

## 2. Make the mod

```sh
opense4-sdk new data pace --id=me.pace --name="Measured Pace"
```

Patch files apply in the order of their names, and the operations of a file in the order
written. The example uses three files: `10-technology.toml`, `20-ships.toml`,
`30-game.toml`.

## 3. Change records by name

```toml
# data/10-technology.toml: cheaper propulsion research.
[[tech_areas.change]]
name = "Propulsion"
set = { "Level Cost" = 4000 }
```

`change` finds the record by its name, in any letter case, and `set` replaces the fields
named. What `Level Cost` does, and how a level's cost grows from it, is in
[Technology](../data/techs.md).

## 4. Change every record that matches

A balance rule often applies to a kind of record rather than one: every seeking weapon,
every satellite hull. `match` chooses records by the values of their fields, and `all =
true` allows more than one:

```toml
# data/20-ships.toml: seeking weapons reload every other turn.
[[components.change]]
match = { "Weapon Type" = "Seeking" }
all = true
set = { "Weapon Reload Rate" = 2 }

# Satellites cost no radioactives.
[[vehicle_sizes.change]]
match = { "Vehicle Type" = "Satellite" }
all = true
set = { "Cost Radioactives" = 0 }
```

Without `all = true`, a `match` that finds several records is an error: patches never change
more than you meant by accident.

## 5. Add to a record's lists

Abilities and requirements are numbered lists in the data files (`Number of Abilities`,
`Ability 1 Type`, ...). A patch adds and removes **entries** by the entry's own field names,
and OpenSE4 numbers the list again:

```toml
[[components.change]]
name = "Ion Engine I"
add = { abilities = [{ "Ability Type" = "Combat To Hit Defense Plus", "Ability Descr" = "Steadies the ship.", "Ability Val 1" = 5 }] }
```

To remove an entry, name it by its first field's value, its position, or a table of fields:
`remove = { abilities = ["Combat To Hit Defense Plus"] }`. The lists of every table are in
[packages-and-data.md](../../packages-and-data.md#lists-in-a-record), and what each
ability does in [Abilities](../data/abilities.md).

## 6. Settings, the computer players' tables and name lists

```toml
# data/30-game.toml
[[settings.change]]                     # Settings.txt has one record: no name
set = { "Number Of Space Combat Turns" = 20 }

[[ai.anger.change]]                     # a computer players' table
files = "default"                       # Ai/Default_AI_Anger.txt: every race without a file of its own
set = { "Regular Decrease" = -4 }

[system_names]                          # a name list
add = ["Quillon", "Marrowgate", "Sable Reach", "Tessaly", "Upper Wend", "Corvane"]
```

`files` says which of a table's files an operation applies to: `"default"`,
`"style:<folder>"` (a minister style), `"race:<folder>"` (a race's own), or `"all"`, the
default. What each table's fields do is in [Settings and lists](../data/settings.md) and
[The computer players' tables](../data/ai-tables.md).

## 7. Remove records, and what depends on them

Removing a record that others name would leave them pointing at nothing. Try removing the
temporal racial technology, every tech area whose `Racial Area` is 3, without `cascade`:

```toml
[[tech_areas.remove]]
match = { "Racial Area" = 3 }
all = true
```

`opense4-sdk check pace` refuses it, with one error for each record that still needs those
areas, and where that record is:

```text
error: mod me.pace, data/10-technology.toml:16: removing tech_areas '<area>' leaves a reference to it in Components.txt:<line> [<record>] ('<area>'): remove or change that too, or remove with cascade = true
```

Remove those records too, or let `cascade = true` do it:

```toml
[[tech_areas.remove]]
match = { "Racial Area" = 3 }
all = true
cascade = true
```

A tech area removed with `cascade` takes along the other tech areas that require it, the
components, facilities, hulls, mounts and intelligence projects that need it, and the
computer players' research rows that name it, and what needs those in turn. Each table's
`cascade` rule is in [packages-and-data.md](../../packages-and-data.md#removing-a-record).

## 8. Check, compare, test

```sh
opense4-sdk check pace                      # every patch applied to your game: No problems found.
opense4-sdk dump pace --out=after           # the data with the mod
diff -r before after                        # exactly what the mod changes
```

Write tests that read the patched data through the rules view, as computer players do. The
example's [tests/test_balance.py](../../../../mods/examples/balance/tests/test_balance.py):

```python
def test_seeking_weapons_reload_every_other_turn():
    seekers = [c for c in the_rules().components if c.weapon is not None and c.weapon.kind == "seeking"]
    assert seekers and all(c.weapon.reload_rate == 2 for c in seekers)


def test_no_temporal_technology_is_left():
    assert not [t.name for t in the_rules().techs if t.racial_area == 3]
```

```sh
opense4-sdk test pace
```

Then play it (`opense4-sdk run pace -- --quick-start=Terran`), and measure what it does to
the game: the arena plays computer players on the patched data, so
`opense4-sdk arena --mod=pace --ai=builtin --ai=builtin --games=20` shows how games go with
your balance ([Testing and measuring](../testing-and-measuring.md#the-arena)).

## Things to keep in mind

- **A balance mod is game-affecting**: every player of a network game needs it, and a game
  started with it needs it to load ([Multiplayer, saved games and identity](../multiplayer-and-saves.md)).
- **Name records, do not copy them.** A patch that names a record of the original and sets
  new values is yours to share. A replacement data file is a copy of the original's file:
  never share one.
- **Patches combine.** Another mod loaded after yours applies its patches to what yours
  made; a record yours removed is gone for it too. `[load] after` and `[requires]` order
  mods that must come in an order.
