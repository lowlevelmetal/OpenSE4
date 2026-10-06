# Troubleshooting

Most problems show up in `opense4-sdk check` (the mod's files and data) or
`opense4-sdk test` (its scripts at work), with the file, line and record or the
traceback. This chapter lists the common messages, what they mean and how to fix them.
Messages are quoted as the tools print them; `<...>` stands for your own names.

**First steps for any problem:**

1. `opense4-sdk check mymod`: the manifest, dependencies, patches, scripts and assets.
2. `opense4-sdk test mymod`: the mod's tests, then its players, rules and scenarios in short
   games, with every error and traceback.
3. `opense4.log` in OpenSE4's user folder (`~/.local/share/OpenSE4` on Linux,
   `%APPDATA%\OpenSE4` on Windows): everything the game logged, players' and rules
   functions' failures with their tracebacks, and the lines your scripts wrote with `log`.

## The manifest and the mod set

| Message | Fix |
|---|---|
| `mod.toml:6: unknown key 'flavour' in [mod] (id, name, version, api, authors, description)` | A misspelt or unknown key: unknown keys are errors so that typos do not pass. Use one of those listed. |
| `id 'T.C' may hold only lowercase letters, digits, '.', '-' and '_' (at most 64)` | Mod ids are lowercase: `me.my-mod`. |
| `the mod needs SDK interface 2; this OpenSE4 offers 1: it needs a newer OpenSE4` | `api` is the SDK version the mod is written for. Write `api = 1`, or update OpenSE4. |
| `mod <id> 1.0.0 needs mod <other> (>=1), which is not enabled` | A `[requires]` mod is missing. Enable it, put it in the mods folder, or give it to `opense4-sdk` with `--mod=PATH`. A version outside the range is refused the same way. |
| The same id twice; mods that require each other in a circle | Remove the duplicate (an old copy in the mods folder, or a folder and its `.zip`); break the circle. |
| `opense4-sdk: no installed game found: give its folder with --data=DIR` | The tools look for the original as the game does. Give the game folder (or its `Data` folder) with `--data=DIR`. |

## Data patches

| Message | Fix |
|---|---|
| `mod <id>, data/p.toml:3: Components.txt [<record>]: components records have no field 'Tonage Space Taken' (check the spelling)` | A misspelt field name. Field names are the data files' own: look them up in `opense4-sdk dump --out=dump` or the [data chapters](data/README.md). |
| `Components.txt has no record '<name>' to change` | No record of that name in the data set as the mods before yours left it. Check the name in `dump`; a mod written for another data set (or one that needs another mod loaded first) shows this on yours. |
| `2 records of Components.txt match match {Custom Group = 0}: add fields to match, or all = true` | A `match` found several records: add fields to the match to find one, or write `all = true` to change them all. |
| `Components.txt already has '<name>' (Components.txt:5 [<name>]): change it instead of adding it` | `add` makes a new record: its name must be new. Use `change` to change an existing one. |
| `removing tech_areas '<area>' leaves a reference to it in Components.txt:21 [<record>] ('Tech Area Req 1'): remove or change that too, or remove with cascade = true` | Something still names the record you removed. Remove or change it too, or remove with `cascade = true` ([The data files](data/README.md#removing-records)). |
| `Components.txt:5 [<record>] (changed by mod <id>, data/p.toml:3): unknown tech area '<area>'` | The data set's own check, after your patches: a requirement names a tech area that is not there (removed, renamed, or misspelt). The message names the patch that wrote the field. |
| `... unknown ability type '<name>'` | Abilities are a closed list: check the name in [Abilities](data/abilities.md), or declare a new one with `[[abilities.declare]]`. |
| A numbered field past the end of its list (`Ability 3 Type` when there are 2) | Add list entries with `add = { abilities = [...] }`, not numbered fields. |
| `no ai.anger file of those 'files' names (<prefix>_AI_Anger.txt) to patch` | An `ai.*` operation applies to no file: check `files` (`"default"`, `"style:<folder>"`, `"race:<folder>"`); the data set may have no such table. |
| A value that should be a number, or `True`/`False` | Write numbers without quotes (`= 40`), switches as `true`/`false`. |

A patch you meant to change something does nothing? Compare `opense4-sdk dump --out=a`
with `opense4-sdk dump mymod --out=b`: what differs is exactly what your mod does.

## Pictures and sounds

| Message | Fix |
|---|---|
| `...: it is 30x30, smaller than its kind's 36x36: it is drawn stretched to that size` | Draw it at the classic size or a whole multiple ([Pictures, sounds and music](assets.md#kinds-and-classic-sizes)). |
| `...: not a whole multiple of its kind's 128x128: it is drawn scaled ... and may look uneven` | Use twice or three times the classic size. |
| `...: the game asks for pictures by names ending in .bmp (or .png), so it never finds this one` | A `.jpg` file: save it as PNG, or rename it `.bmp` (JPEG data inside is read). |
| `...: no hull's Primary or Alternate Bitmap Name is '<name>': only designs that choose it as their own picture show it` | Expected for a picture pack. Otherwise, check the hull's bitmap name and the file name (`Generic_Mini_<name>`). |
| `...: the hull [<name>] has no Mini picture '<bitmap>' (looked for Pictures/RaceGeneric/Generic_Mini_<bitmap>.bmp and in the race folders)` | Add `assets/Pictures/RaceGeneric/Generic_Mini_<bitmap>.png` (and the portrait). |
| `Pic Num 300 is not a cell of Pictures/Components/Components.bmp (180 cells)` | A component or facility picture number beyond the sheet: use a number the sheet has. |
| `...: the game reads pictures, sounds, music and fonts only (Pictures/, Sounds/, Music/, Fonts/)` | A file under `assets/` where the game never looks. Game files (AI tables, race files) belong under `data/`. |
| A sound that does not play | Interface sounds have fixed names (`button`, `close`...); a weapon's is its `Weapon Sound`, with its extension. OGG files must be OGG Vorbis. |

## Computer players

| Message | Fix |
|---|---|
| `mod.toml:7: the computer player 'P' is in module nothere, but ai/ has no nothere.py nor nothere/__init__.py` | `module` names a file of `ai/` without `.py` (`ai/pioneer.py` is `pioneer`), or a package folder. |
| `ai/my-helpers.py: not a Python module name, so no player can import it` | Python module names use letters, digits and `_`: rename it `my_helpers.py`. |
| `ai/bad.py ...: Syntax: SyntaxError: invalid syntax` | A line the game's Python does not take. Besides real mistakes: no `match` statement, no `{**a, ...}` in dict displays ([runtime.md](../runtime.md#the-language)). Code that runs under CPython may still fail here. |
| `{kind: "set_orders", vehical: 3} refused: vehical: unknown field` (in the log) | A command the engine could not read: a misspelt field or kind. Build commands with `opense4.cmd` and `opense4.order`, which check names at once. The refused commands are in `self.refused` at the next planning call. |
| `<command> refused: <reason>` (in the log) | A well-formed command the rules refuse, with the same reason a human player would get (not your ship, no room in the queue, an invalid design). Ask first with a query (`design_figures`, `colonize_problem`, `queue_item_problem`). |
| `Conversion: the result['memory']['ratio'] is a float; only whole numbers cross to the engine (use round() or int())` | Commands, answers and memory hold whole numbers only. Use `//`, `round()` or `int()`. |
| `BudgetExceeded: the call used up its budget of 200000000 bytecodes` | The call ran too long: a loop that never ends, or work that grows too fast. See [Budgets and performance](performance.md). |
| A traceback ending in your file (`File "bad.py", line 10, in economy` / `IndexError: list index out of range`) | An exception in your code: the classic AI made that decision instead. Fix it as any Python error; `opense4-sdk test` fails on it. |
| `MemoryError: the memory takes 1300000 bytes as JSON; the game allows 1048576` | `self.memory` must stay under its limit (1 MiB as JSON by default): keep ids and counts, not copies of the view. |
| An answer the call does not take (a colony type not in `question.choices`) | Answer one of the choices, or None for the classic answer. |
| A player in the setup that names `<mod>:<player>` "which the game does not use" | The mod must be enabled in the game (or given to the arena with `--mod`). |
| The player seems to do nothing | Is it chosen for the computer empires (Game Setup's Computer Players, `--ai=`)? Does it override the call you think (`orders`, not `order`)? Switch on the AI notes view (`Ctrl+Shift+N`) and write notes; read `opense4.log`. |
| `Computer players: <mod> has <file>, which another mod's computer player already has: left out` (in the log) | Two mods with a module of the same name: the later one's is left out. Name modules after your mod. |

After three failures in one turn the classic AI plays the empire for the rest of that turn;
the next turn starts afresh. The host's main window shows a notice for each failure, with
**Details** listing their tracebacks.

## Rules scripts

| Message | Fix |
|---|---|
| `Rules mod <id>: turn_end failed: hook turn_end: TypeError: argument 1 of _opense4_rules.effect['args']['minerals'] is a float; ...` | Effects take whole numbers. |
| `ValueError: ...` from an effect | The engine refused the effect, saying why (no such empire, no free facility slot, not a planet). Check before you ask, or catch the `ValueError`. |
| `RuntimeError: ...` from an effect | The effect is not allowed at this moment (`replace_galaxy` outside `generate_galaxy`, `victory` outside the victory check). |
| `check`: an order, event, project or victory condition without its function, or a function without its declaration | Every `[[rules.orders]]` (and the others) in `mod.toml` needs its `@rules.order(...)` function, and the other way round. |
| `check`: a scenario's option is not the mod's, or its action is not registered | A scenario's `[options]` are the mod's own `[[rules.options]]`; its `action` names a `@rules.objective(...)` function. |
| `mod <id>: data/<file>.py: generate() failed: ...` | The generator raised: the traceback's lines follow. Its file name must be a Python name; it must return a dict shaped like a patch, with whole numbers. |
| A rule that works once and then forgets | Module globals do not last between calls: keep state in `mod_data`. |
| A rule that does not run at all | Is the mod in the game's mod set (enabled when the game was created)? Is the hook name right (`rules.on` raises for an unknown one)? For `empire_end_of_turn`, are `step` and `when` right? |

A failing rules function is skipped for the rest of the turn; after three failures in a turn
the mod's rules are off until the next. `opense4-sdk test` fails on any failure, with the
traceback.

## Saved games and network games

| Problem | Fix |
|---|---|
| A saved game will not load: its mods differ from yours | The message lists each difference. Put the game's mods (the same versions) in your mods folder and use **Load with Its Mods**. A changed mod is a new identity: keep the old version's `.zip` for old games. |
| Joining a network game is refused: game-affecting mods differ | Use the lobby's **Mods** button to choose the host's mods, in the same versions. Pictures and sounds may differ. |
| **Save for SE IV** is refused | The game uses what the original cannot hold: declared abilities, mods' computer players or rules, or a design picture the installed game lacks ([Multiplayer, saved games and identity](multiplayer-and-saves.md#saving-for-the-original)). |

## Still stuck

Make the smallest mod that shows the problem, run `opense4-sdk check` and `test` on it, and
look at `opense4.log`. The example mods ([mods/examples](../../../mods/examples/README.md))
are known to pass both: comparing yours with the nearest one often shows the difference.
