# The interface tier

The fourth tier of the modding SDK (docs/MODDING_SDK.md, section 8): what a mod adds to the
classic windows. A mod can

- give its **orders** (docs/sdk/rules.md "Orders") a picture, a suggested key and the way
  their arguments are asked for; the player gives them from the order strip's **Mod
  Orders**, from a panel's button or with the key;
- add **panels** to the reports of ships, fleets, planets, colonies and systems;
- add **columns** to the Ships\Units, Planets, Colonies and Designs windows;
- add **pages** to the Empires window, each a table over the empires;
- give its names in several languages (**text**);
- suggest **keys** for its orders, panels and pages, which join the rebindable keys.

The interface runs on each player's own computer. It reads only what the player's empire
knows, the same view a computer player of that empire gets ([view.md](view.md)), and it
changes the game only through commands: a button or key gives a mod order exactly as the
menu does. A mod whose files hold only `ui/`, `text/` and `assets/` does not change the game,
and none of these folders is part of a mod's identity, so players of one game may have
different interfaces and translations.

Without mods that add to it, every window is exactly the original's: the additions take
places the classic layouts leave free (the order strip's empty place, free slots of a
window's button column, a small button at a report's top right), and appear only when a
mod has something to show there.

- Engine side: `src/sdk/ui.hpp` (the declarations, the values, the text),
  `src/sdk/ui.cpp`, `src/sdk/ui_values.cpp`.
- Client: `src/client/classic/mod_ui.*` (the extensions of the data set in use, values per
  game state, the pieces the windows draw), `src/client/classic/main_window_mods.cpp` (Mod
  Orders, the report's MOD page, the keys), and the windows that show them.
- Python: `opense4.ui` ([python-api.md](python-api.md), "Interface values").
- Tests: `tests/sdk/test_sdk_ui.cpp`, `tests/sdk/python/test_sdk_ui.py`, the input scripts
  `tests/input/mod-*.script`, with the fixture mod `tests/fixtures/mods/ui-fixture`.

## A first interface

```text
shields/
  mod.toml               [rules] with the order "overcharge"
  scripts/shields.py     its effect
  ui/shields.toml        how the interface shows it, and a panel
  ui/shield_values.py    a value the panel works out
  text/fr.toml           French names
  assets/Pictures/Shields/Overcharge.png
```

```toml
# ui/shields.toml
[[order]]
name = "overcharge"
icon = "Pictures/Shields/Overcharge.png"
key = "Ctrl+Shift+O"
[order.args.power]
label = "How much power"
choices = [1, 2, 3]

[[panel]]
name = "shields"
title = "Shields"
report = "ship"
whose = "mine"
[[panel.row]]
name = "charged"
label = "Overcharged on turn"
mod_data = "overcharged"
[[panel.row]]
label = "Supply"
field = "supply"
format = "{} units"
[[panel.row]]
label = "Charge"
value = "charge"
[[panel.button]]
order = "overcharge"
```

```python
# ui/shield_values.py
from opense4 import ui


@ui.value("charge")
def charge(view, ship):
    capacity = ship.supply_capacity or 0
    return None if capacity == 0 else str(ship.supply * 100 // capacity) + "%"
```

The mod needs `players_see_mod_data = true` in `[rules]` for the panel's first row: the
player's view has a mod's data on the player's own things only then.

## The files

| | |
|---|---|
| `ui/*.toml` | The declarations, read in the order of their names. Every table below may be in any of them; names are unique within the mod. |
| `ui/*.py` | Python modules for computed values. `ui/charge.py` is the module `charge`, as `scripts/` files are for rules: name modules after the mod. |
| `text/<language>.toml` | The mod's names in a language: `text/en.toml`, `text/fr.toml`, `text/pt-br.toml`. |
| `assets/` | Pictures the declarations name (order icons), as every picture of a mod. |

Unknown tables and keys are errors, with the file and line, as everywhere in a mod. A file
that does not read is left out with its errors; the rest of the mod's interface stays. The
Mods window shows the problems with the mod's details, the game's log (opense4.log) lists
them, and `opense4-sdk check` reports them.

## Mod orders

The order strip has one place the original leaves empty (the last column but two, top
row). It stays empty and dim without mod orders. When the game's rules mods declare orders
the selection takes, it lights as **Mod Orders** (at 800x600 it is on the strip's last
page): a menu of the orders, first those of the selected ship, fleet or colony, then the
empire's own (`applies_to = "self"`), and for another empire's ship or colony the orders
given to that empire. Each shows its label, its picture when the mod gives one, and its
description under the pointer.

Choosing one asks for its arguments one after another, in their declared order:

| Argument | Asked as |
|---|---|
| `int` | a number in its range, starting at its default; or a choice when the mod lists `choices` |
| `bool` | Yes or No |
| `text` | a line of text; or a choice when the mod lists `choices` |
| `empire` | a choice of the player's empire and the empires it knows (and "(none)") |
| `design` | a choice of the player's designs (and "(none)") |
| `system`, `object`, `colony`, `vehicle`, `fleet` | a pick on the map: click the sector; when it holds several, a list asks which |

Then the order is given as a command (`cmd::ModCommand`, built with
`sdk::modOrderCommand`), checked as every command is: the declaration, then the mod's own
check. A refused order says why in the main window's note line; the order's effect shows at
once (it runs as it is given, in both turn styles). Esc or Cancel at any question gives no
order.

```toml
[[order]]                 # one per order the mod styles; an order without one still works
name = "overcharge"       # the mod's order, or "<mod id>:<name>" for another mod's
icon = "Pictures/Shields/Overcharge.png"   # a .png or .bmp under the mod's assets/, shown 24x24
key = "Ctrl+Shift+O"      # a suggested key (below)
[order.args.power]        # how one argument is asked for
label = "How much power"  # the question; default: the argument's name
choices = [1, 2, 3]       # int and text arguments: a list to choose from
```

A panel's button (`[[panel.button]]`) and a `[[button]]` (a panel that is only a button)
give the order to what the report shows: a ship's report gives a vehicle order to that ship,
a colony's a colony order, any report an empire's own order. Another mod's order is named
`"<mod id>:<order>"`.

## Report panels

The reports of the main window's report panel get a small **MOD** button at their top right
while some mod has a panel for what they show.
It switches the report between its classic page and the mods' panels; a classic tab
(Detail, Comps, Cargo, Ability, Facil) brings the classic page back. The choice stays as the
selection changes, so the panels can be watched from ship to ship.

```toml
[[panel]]
name = "shields"          # lowercase letters, digits and '_'
title = "Shields"         # default: the mod's name
report = "ship"           # ship, fleet, planet, colony or system
whose = "mine"            # mine, others or any (the default): whose things it shows on
key = "Ctrl+Shift+S"      # a suggested key that shows or hides the mods' panels
[[panel.row]]
name = "charged"          # optional: names the row's label in text/
label = "Overcharged on turn"
mod_data = "overcharged"  # its value (below)
[[panel.button]]
order = "overcharge"
label = "Overcharge"      # default: the order's label
```

- `ship` is a ship's, base's or unit group's report, `fleet` one of the player's fleets
  (the Fleet Report), `planet` a planet's or asteroid field's (colonized or not), `colony` a
  planet with a colony the player sees, `system` the report of a system (an empty sector).
- Rows are a label and a value; buttons give orders.
- A panel whose value fails shows a small red box in its place: the panel, its mod and the
  error, with the traceback under the pointer. Nothing else stops.

## List columns

```toml
[[column]]
name = "charge"
list = "ships"            # ships, planets, colonies or designs
label = "Charge"
width = 70                # pixels of the classic window, 20 to 400; default 80
value = "charge"
```

The Ships\Units and Colonies windows get a **Mods** tab, in the first free slot of their
button column; Planets and Designs, whose tabs are filters, get a **Mod Columns** switch.
They show the rows of the window (in its order, for its tab or filter) with their picture,
their name and the mods' columns; a click selects as the window's own list does (in Designs
it shows that design's detail again), and in Ships\Units, Planets and Colonies a
right-click opens the report. The values for a list are worked out together; a failed one
reads "error" in red, with the reason under the pointer.

Rows are: for `ships`, the player's vehicles (`vehicle`) and fleets (`fleet`); for
`planets`, stellar objects (`object`); for `colonies`, the player's colonies (`colony`); for
`designs`, designs (`design`) of the tab.

## Empires pages

```toml
[[empire_page]]
name = "relics"
title = "Relics"          # the page's tab in the Empires window
key = "Ctrl+Shift+R"      # a suggested key that opens the Empires window on it
[[empire_page.column]]
name = "count"
label = "Relics held"
width = 90
mod_data = "relics"
[[empire_page.column]]
label = "Treaty"
field = "relation.treaty"
```

Each page is a tab of the Empires window, after Treaty, Trade and Tariff (the first takes
the gap below them). It shows a table: the player's empire first, then the empires it
knows, with the page's columns. A failed value shows an error box in place of the table.

## Values

Every row and column has exactly one source:

| Key | The value |
|---|---|
| `field = "supply"` | A field of the thing's record in the player's view ([view.md](view.md)): dotted keys for nested ones (`"location.x"`, `"relation.treaty"`), a number for a list's place (`"queue.items.0.name"`). |
| `mod_data = "relics"` | A key of a mod's data on the thing (dotted keys too), as the player's view holds it: the player's own empire, colonies and vehicles, of rules mods that say `players_see_mod_data = true`. `mod = "<mod id>"` reads another mod's data. |
| `ability = "Supply Storage"` | An ability's value (a declared one or the game's own) on a vehicle (its design), design, colony (its facilities; a planet's colony) or system, combined as the ability combines; only for things the player's view lists. |
| `value = "charge"` | A function of the mod's `ui/*.py`, registered with `@ui.value("charge")`. |

`format = "{} kT"` puts the value where `{}` is. Values show as text: whole numbers as they
are, true and false as Yes and No, lists joined with ", ", none as "-".

The things are the view's: a value about another empire's ship, colony or design sees what
the player knows of it and nothing more.

### Computed values

```python
from opense4 import ui

@ui.value("charge")
def charge(view, thing):
    ...
```

The function gets `view`, the player's own view (an `opense4.view.View`, read from the
engine a part at a time: `view.my`, `view.vehicle(id)`, `view.rules`...; `view.ability(thing,
name)` reads an ability), and the thing: a `Vehicle`, `Fleet`, `SpaceObject`, `Colony`,
`System`, `Empire` or `Design` of that view. It answers a whole number, text, True or False,
None (no value) or a list of these; an object of the view stands for its name.

Computed values run in the game's script runtime ([runtime.md](runtime.md)), in the sandbox,
with nothing to change: there are no effects and no commands here.

### When values are worked out, and their budgets

Values are worked out when a window first shows them for a state of the game, and kept until
the game changes (a command given, a new turn, another game loaded): never every frame. All
the computed values a panel, a list or a page needs at once run in one interpreter (a
*batch*).

| Limit | |
|---|---|
| One value's function | 2 million bytecodes |
| One batch together | 40 million bytecodes, besides importing the mod's `ui/` modules (50 million) |
| The interpreter's heap | 16 MiB |
| Reading | 5 bytecodes per value the engine builds, 100 per ability, as for rules scripts |

A value over its budget, one that raises, and one that answers what cannot be shown fails:
its panel shows the error box, its cell "error". Running out of a batch's budget fails the
values left. Nothing fails the game.

The script runtime holds one interpreter per process (runtime.md), and a game's computer
players and rules use it while a turn is played: a value asked for meanwhile shows "..." and
is worked out at the next frame the runtime is free.

## Text

`text/<language>.toml` gives a mod's names in a language. The file's name is the language:
two or three lowercase letters, then optionally `-` and a region or script (`en`, `fr`,
`de`, `pt-br`). Keys are dotted; TOML's tables nest them, so these are the same:

```toml
[order.overcharge]
label = "Surcharger les boucliers"

"order.overcharge.description" = "Double les boucliers pour un tour."
```

| Key | Names |
|---|---|
| `mod.name` | the mod (headings of its keys and panels' error boxes) |
| `order.<name>.label`, `order.<name>.description` | an order, in Mod Orders, buttons and the Controls page |
| `order.<name>.arg.<argument>` | an argument's question |
| `option.<name>.label`, `option.<name>.description` | a game option, in Mod Options |
| `panel.<name>.title`, `panel.<name>.<row name or number>` | a panel and its rows (rows by `name`, else their place from 1) |
| `column.<name>.label` | a list column |
| `page.<name>.title`, `page.<name>.<column name or number>` | an Empires page and its columns |
| `scenario.<name>.title`, `scenario.<name>.summary`, `scenario.<name>.<objective>` | a scenario of the mod (docs/sdk/rules.md "Scenarios") |

A string is looked up in the language chosen in Settings → Modding ("Language of the mods'
text", shown when the mods in use have text in a language besides English), then its base
language (`pt` for `pt-br`),
then English, then the mod's own words in its `mod.toml` and `ui/` files. The game's own
windows stay in English.

## Key bindings

A mod suggests keys with `key = "..."` on an `[[order]]`, a `[[panel]]` (or a `[[button]]`,
which is a panel) or an `[[empire_page]]`, written as the Controls page writes them: `"Ctrl+Shift+O"`, `"F5"`,
`"Alt+Delete"`. Each order of the game's rules mods, and each panel and page with a key,
is a row of the Settings window's Controls page, under "Mod: <mod name>", and can be
changed there like any key:

- the player's own choice always wins;
- a suggestion is used only while no other binding has that key, the game's own or an
  earlier mod's: otherwise the action stays without a key, and the Controls page says so
  under it ("Suggests F5: not used, F5 is the key of Colonies."); the log says it too;
- a key the player gives one action is taken from any other that had it, and the page says
  so, as for the game's own keys. Restore default keys clears the player's choices for the
  mods' keys too.

The player's choices are kept in `settings.toml` (`[controls.mod_keys]`, by
`"<mod id>:order:<name>"`, `":panel:"`, `":page:"`).

What the keys do in the main window: an order's key gives the order to the selection (its
arguments asked as from the menu); a panel's key switches the report to the mods' panels
and back; a page's key opens the Empires window on it.

## Setup and the lobby

The game options of rules mods (docs/sdk/rules.md "Game options") are set where a game is
set up ([docs/SETUP.md](../SETUP.md) "Mods' options"):

- **Game Setup**: the Game Settings page's **Mod Options** button opens a window with every
  option of the game's mods, in the mod's language: a switch, or a whole number in its range.
- **Quick Start**: a line above the Mods line says the options' values, with a **Mod
  Options** button.
- **The network lobby** ([docs/MULTIPLAYER.md](../MULTIPLAYER.md) "The lobby"): the line
  under the mods says the values; the host changes them with **Mod Options**, the players who
  join see them with **See Mod Options**. The host also sets the computer players' limits
  with **Computer Player Limits**, beside "Computer players see everything".
- **Scenarios** of the mods in use are on the Learn window's **Scenarios** tab, beside
  Training (the title screen's Scenario button opens the window): each with its mod,
  summary, empires and objectives; Start Game begins it, played by its first human empire. A dedicated server starts one from a setup
  file's `scenario = "<mod id>:<name>"` (MULTIPLAYER.md "Setup files").

## Checking

`opense4-sdk check` reads a mod's `ui/` and `text/` files with every rule above, checks
the orders they name against the mods that load with it and their pictures against the
mod's `assets/`, imports its `ui/*.py` modules in the game's runtime, and checks that every
computed value a file names is registered (a value registered that nothing shows is a
warning).

## For engine developers

- `sdk::loadUiExtensions` reads the declarations of a data set's mods once; the client keeps
  them per data set and language (`client::classic::modUi`), with the texts.
- `sdk::UiValues` works out values for one player's view of one game state: field, mod data
  and ability values in C++ from an `sdk::Perspective` (the redacted copy made once), computed
  values in a batch on a thread of their own (`sdk::Worker`, 8 MiB of stack), when
  `sdk::interpreterSlot()` is free. The client makes a new one at each new revision of the
  session (`client::classic::modValues`).
- The batch's interpreter has the `opense4` package, the UI mods' `ui/*.py` at its root and
  the native module `_opense4_ui` (`read`, `ability`, `rules`); `opense4.ui.dispatch` loads
  the modules (`call = "load"`) and works out one value per call (`call = "value"`), each
  with its own budget.
- `sdk::uiArgumentSteps` and `sdk::uiOrderArguments` are the arguments' questions and their
  answers' check, without any drawing; the main window draws them.
- Key resolution is `client::resolveModKeys` (client/input.hpp), pure and tested.
