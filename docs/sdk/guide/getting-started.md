# Getting started

This chapter takes you from nothing to a mod you can play and share. It names the tools
and folders once; the later chapters go deeper. The reference for everything here is
[Mod packages and data patches](../packages-and-data.md).

## What a mod can do

A mod is a folder (or a `.zip` of one) with a manifest, `mod.toml`. What it holds decides
what it changes:

| It holds | It changes | Code? | Must every player of a game have it? |
|---|---|---|---|
| Pictures, sounds, music, fonts (`assets/`) | How the game looks and sounds | No | No |
| Data patches and data files (`data/`) | Components, hulls, facilities, techs, races, the galaxy, events, settings, the computer players' tables... | No (a data generator is Python) | Yes |
| Computer players (`ai/`) | How computer empires play | Python | Yes, when they play inside the game |
| Rules scripts (`scripts/`, `scenarios/`) | The rules themselves: new abilities, orders, events, options, victory conditions, scenarios | Python | Yes |
| Interface extensions (`ui/`, `text/`) | Buttons for a mod's orders, panels in reports, list columns, Empires pages, key suggestions, translations ([interface.md](../interface.md)) | TOML (computed values in Python) | No |

Data, computer players and rules change how a game plays, so they are **game-affecting**:
a saved game and a network game record them, and every player must have the same ones
([Multiplayer, saved games and identity](multiplayer-and-saves.md)). A mod of pictures,
sounds and interface is not: each player may use their own.

A mod never touches your installed copy of the original. OpenSE4 reads the install, then
each mod over it, and keeps the result in memory: switch a mod off and the game is as it
was. OpenSE4 still needs the original installed, whatever the mods replace.

## What you need

- **OpenSE4 and your installed copy of the original game.** A release of OpenSE4 has
  `opense4-sdk` beside the game, and the SDK's documentation and example mods in
  `sdk/docs` and `sdk/examples`. Building from source makes `build/debug/opense4-sdk`
  ([docs/BUILDING.md](../../BUILDING.md)).
- **A text editor.** Data patches are TOML, scripts are Python.
- **Python is not needed** for mods that run inside the game: OpenSE4 carries its own
  ([The script runtime](../runtime.md)). CPython 3.10 or newer is needed only for external
  bots, the training environment and running a mod's tests under pytest.

`opense4-sdk` finds your installed game as the game does. Give it another with
`--data=DIR` (the game folder or its `Data` folder).

## A first mod in five minutes

```sh
opense4-sdk new data cheaper-engines --id=me.cheaper-engines
```

makes a folder with a manifest and a commented patch file:

```text
cheaper-engines/
  mod.toml
  README.md
  data/changes.toml
```

Write a patch in `data/changes.toml`. Records are named by their `Name` and fields by the
data files' own field names; `opense4-sdk dump` (below) shows you both:

```toml
[[components.change]]
name = "<an engine of your data set>"
set = { "Cost Minerals" = 40 }
```

Then check it against your installed game, and play with it:

```sh
opense4-sdk check cheaper-engines                     # every problem with its file and line
opense4-sdk run cheaper-engines -- --quick-start=Terran
```

`check` applies the patches to your game's data and reports every mistake: an unknown
table or field, a record that is not there, a reference a removal leaves behind, a picture
of the wrong size. It ends with `No problems found.` or a count of errors and warnings,
and exits with 1 on errors. Run it after every change.

## The other templates and the examples

| Command | Makes |
|---|---|
| `opense4-sdk new assets DIR` | A mod of pictures and sounds: an `assets/` folder in the game folder's layout |
| `opense4-sdk new data DIR` | A mod of data patches |
| `opense4-sdk new ai DIR` | A mod with one computer player that keeps the classic AI but sends its idle scouts exploring |
| `opense4-sdk new rules DIR` | A mod with a rules script |
| `opense4-sdk new --from-example NAME DIR` | A copy of one of the [example mods](../../../mods/examples/README.md) under a mod id of yours (`--id`, else `my.<folder>`); an unknown name lists them |

Each takes `--id=` (lowercase letters, digits, `.`, `-` and `_`; a reverse-domain style
such as `yourname.modname` keeps ids apart) and `--name=`.

## The parts of a mod

```text
better-carriers/
  mod.toml          the manifest
  data/             data patches (*.toml), replacement data files (*.txt), generators (*.py),
                    and AI tables, race files and design-name lists in the game folder's layout
  assets/           Pictures/, Sounds/, Music/, Fonts/, in the game folder's layout
  ai/               computer players (Python)
  scripts/          rules scripts (Python)
  scenarios/        scenarios (*.toml)
  ui/               interface extensions (*.toml, computed values in *.py)
  text/             the mod's names in other languages (<language>.toml)
  tests/            the mod's own tests, run by opense4-sdk test
  README.md         for people (any file at the top is fine)
```

The manifest:

```toml
[mod]
id = "me.better-carriers"          # unique; it names the mod in saved games and the lobby
name = "Better Carriers"
version = "1.2.0"                  # whole numbers separated by dots
api = 1                            # the SDK interface the mod is written for
authors = ["Me"]
description = "Carriers that carry more."

[requires]
"me.common-lib" = ">=1.0, <2"      # mods that must be enabled too, with a version range

[load]
after = ["me.common-lib"]          # load after these when they are enabled (a hint)
```

Computer players add `[[ai.players]]` tables ([Computer players](computer-players.md)),
rules scripts a `[rules]` table ([Rules scripts](rules-scripts.md)). Unknown tables and
keys are errors, so a typo does not pass unnoticed.

## Where mods live and how they are chosen

| | |
|---|---|
| Your mods folder | `Mods/` in OpenSE4's user folder: `~/.local/share/OpenSE4/Mods` on Linux, `%APPDATA%\OpenSE4\Mods` on Windows. Each folder and `.zip` there is a mod. |
| Mods that come with OpenSE4 | `mods/` beside the programs (Hegemon). Looked for after your mods folder: a mod of yours with the same id replaces one of these ([packages-and-data.md](../packages-and-data.md) "Where mods are found"). |
| In the game | The title screen's **Mods** button: switch mods on and off, order them, see what each holds and why a choice does not load. **Done** reads the data again with them and keeps the choice for the next game you start or load. Game Setup and Quick Start show the mods a new game will use, with a Mods button of their own. |
| For one run | `opense4 --mod=PATH` (a folder, a `.zip`, or the id of a mod in your mods folder or of one that comes with OpenSE4; repeat it for several) takes the place of your choice; `--no-mods` plays without; `--mods-dir=DIR` looks for ids elsewhere; `--no-bundled-mods` leaves out the mods that come with OpenSE4 (`opense4-sdk` and `opense4-server` take it too). |
| The dedicated server | `opense4-server --mod=...`, or `mods = [...]` in a setup file ([docs/MULTIPLAYER.md](../../MULTIPLAYER.md)). |
| `opense4-sdk run` | Starts the game with the mod and the mods it requires; what follows `--` goes to the game. With `-- --quick-start=RACE`, the mod's first computer player plays the quick game's computer empires (in Game Setup, choose under Computer Players). |

**Load order.** The mods load in the order you chose, moved so that every mod comes after
the mods it requires and after the enabled mods its `[load] after` names. A later mod wins:
its files replace earlier ones, and its patches apply to what the earlier ones made. A
required mod that is off, a version outside the range, the same id twice, or mods that
require each other in a circle stop the choice from loading, with a message that names
them.

## Looking at the data

```sh
opense4-sdk dump --out=dump                       # your installed game's data, as the game reads it
opense4-sdk dump cheaper-engines --out=dump2      # ... with the mod applied
opense4-sdk info cheaper-engines                  # what the mod is, holds and its identity
```

`dump` writes `Data/*.txt` and the computer players' tables into a folder of your choice,
never into the installed game: the place to find record names, field names and the values
you are changing. Compare the two folders to see exactly what a mod does.

## Testing

```sh
opense4-sdk test cheaper-engines
```

runs the Python tests in the mod's `tests/` folder in the game's own Python, then plays a
short game for each computer player, rules script and scenario the mod has, and fails on
any error a script raises. A data mod's tests can read the patched data through the rules
view ([Testing and measuring](testing-and-measuring.md)):

```python
# tests/test_engines.py
from opense4 import rules, testing


def test_the_engine_is_cheaper():
    engine = rules.Rules(testing.game_rules()).component_named("<the engine you changed>")
    assert engine.cost.minerals == 40
```

## Sharing a mod

```sh
opense4-sdk pack cheaper-engines             # me.cheaper-engines-0.1.0.zip, with its identity recorded
```

Give the `.zip` to others: they put it in their mods folder as it is (OpenSE4 unpacks it
once into `ModCache/` in its user folder) and switch it on in the Mods window. `pack`
leaves out hidden files and Python caches and writes `mod.identity` into the archive; a
package whose files no longer match it gets a warning.

- Change `version` with every release you share: saved games and the lobby name a mod by
  its id, version and identity.
- Players of a network game need the same game-affecting mods as the host; pictures and
  sounds may differ.
- Publishing on the Steam Workshop (`opense4-sdk publish`) waits for OpenSE4's Steam
  release. Until then, share the `.zip`.
- Keep your mod your own work: never put files or text from the original game in a mod
  you share. A mod that changes the original's records names them and sets new values;
  it does not copy them.

## Where to go next

- [The data files](data/README.md), then [A first balance mod](tutorials/balance-mod.md)
  or [A first new unit](tutorials/new-unit.md).
- [Pictures, sounds and music](assets.md), and [A first picture mod](tutorials/picture-mod.md).
- [Computer players](computer-players.md), and [A first computer player](tutorials/computer-player.md).
- [Rules scripts](rules-scripts.md).
- When something goes wrong: [Troubleshooting](troubleshooting.md).
