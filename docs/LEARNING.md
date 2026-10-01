# Learning to play: tutorials, training and the manual

OpenSE4 teaches the game in three ways, all reached from the **Learn** window. The
intro screen's **Tutorial** button opens it on its Tutorials tab and **Scenario** on its
Training tab (the original's intro has both buttons); **Manual** opens the manual
alone; during a game the Game Menu's **Learn** button opens it:

| Part | What it is |
|---|---|
| **Tutorials** | Guided lessons. A lesson starts a prepared game and walks the player through it one step at a time. Each step explains something, outlines the button or window it is about, and waits until the player has done it (or pressed Next). |
| **Training** | Practice games with objectives ("found five colonies by turn 40"), briefing pages, hints and a result. They follow the original's scenario model: titled pages tied to turns, browsed with previous and next, and re-opened with Ctrl+H or the **T** button in the status bar (spec 06 §1.7). |
| **Manual** | Our own manual, in chapters, with a contents tree, search and links. Its links can open the game's windows. Shift+F1 opens the page for the window in front. If the player's install has the original's HTML manual, a button opens that in the browser. |

All three are OpenSE4 extensions. The original's own tutorial needs its binary starting
game (`Scenarios/*.gam`, spec 06 §6), which OpenSE4 cannot read, so OpenSE4 ships its own
lessons instead.

## Clean-room rules for the content

The lessons, training scenarios and manual are **our own writing** (CLEANROOM.md):

- Write them from `docs/spec/` and from OpenSE4's own client. Never read the original's
  manual, tutorial or scenario texts to write them, and never copy or paraphrase them.
- Don't name things only the original's data files define (a race, a component, a tech
  area) unless the lesson cannot do without it. Prefer generic conditions ("any
  research").
- Run `python3 tools/cleanroom_check.py` after writing content; it must report 0.

## Where the content lives

Everything is our own, so it is built into the executable like the fonts
(`OPENSE4_EMBED_RESOURCES`, `core/embedded.hpp`). In developer builds
(`OPENSE4_DEV_PATHS`) the files under `assets/learn` on disk win, so content can be
edited and checked without building again. `--learn-dir=<dir>` reads the content from
another folder only (the tests' `tests/fixtures/learn` is such a folder).

```
assets/learn/manual/NN-slug.md       manual chapters, in contents order
assets/learn/tutorials/NN-slug.toml  guided lessons, in list order
assets/learn/training/NN-slug.toml   practice games, in list order
```

`slug` is the page or lesson id used in links and on the command line: the file name
without the number and the extension. Files at the top of `assets/learn` (notes for
writers) are left alone; any other file is reported.

## Manual pages

A chapter is a Markdown file in a small subset:

- `#` gives the chapter title (the first line); `##` and `###` give sections, which get
  anchors from their text: lower case, letters and digits kept, spaces and hyphens as
  `-`, everything else dropped ("Ships & fleets" gives `ships--fleets`). A second
  section with the same anchor gets `-1`, a third `-2`. The contents tree lists the
  `##` sections.
- Paragraphs, `-` bullet lists (one level of nesting), and `1.` numbered lists.
- `**bold**`, `*italic*`, and `` `F1` `` for keys and on-screen labels.
- Pipe tables (a `|---|:-:|` row under the header sets the alignment), and `> ` for
  a tip box, which holds paragraphs and lists.
- Links:
  - `[text](slug)` or `[text](slug#anchor)` to another manual page, `[text](#anchor)`
    to a section of the same page;
  - `[text](window:research)` opens a game window (only during a game);
  - `[text](help:components)` opens a tab of the Help window (F1);
  - `https://...` opens the browser.

Anything outside the subset shows as plain text; the parser reports what it did not
use.

An optional front-matter block lists the windows the page explains, for Shift+F1
(`main` stands for the main window, the page Shift+F1 opens when no window is open):

```
---
windows: research, tech-tree
---
# Research
```

## Lessons and training games

Both are TOML files.

```toml
title = "First steps"
summary = "The main window, your home system and your first turn."
minutes = 10

[setup]                     # how the lesson's game is created
seed = 12345                # fixed, so the galaxy is the same every time
computer_players = 0
# ... further keys as the setup model supports (see the reference below)

[[step]]                    # tutorials: steps in order
title = "Your home system"
text = """Markdown, as in the manual."""
highlight = ["panel:system"]     # UI tags to outline (see "UI tags")
done = { selected = "planet" }   # omitted: the player presses Next
manual = "colonies#your-homeworld"   # Read more

# Training games use objectives, pages and hints instead of steps:
[[objective]]
text = "Found five colonies"
when = { colonies = 5 }
by_turn = 40                # optional deadline

[[page]]                    # shown at the start of the turn, as the original's scenario pages
turn = 0                    # 0 = the first turn
series = "briefing"         # previous and next browse one series
title = "Your orders"
text = """..."""

[[hint]]                    # shown once, when its condition first holds
title = "Hint"              # optional
when = { turn = 10 }
text = """..."""

[fail]                      # optional: the training game is lost when this holds
when = { not = { colonies = 1 } }   # "no colony left": numbers mean "at least"
text = "Every colony was lost."     # optional: shown with the result
```

### Conditions

A condition is a table with one key, or `all = [...]` / `any = [...]` / `not = {...}`
over conditions; a table with several keys needs all of them. Numbers mean "at least",
so "fewer than" is written with `not` (`not = { colonies = 1 }`: no colony). The set is
fixed in code (`learn/condition.hpp`); an unknown key, window id, command name, order
kind or kind of selection is a load error that names the file and line.

Counters marked "since" count from where the step began (the first time it was shown)
in a tutorial, and from the start of the game in a training game.

| Key | Holds when |
|---|---|
| `window = "<id>"` | that window is open |
| `selected = "<kind>"` | the main window has that kind of thing selected (planet, ship, fleet, system, ...) |
| `command = "<name>"` | since: the player gave a command of that type |
| `order = "<kind>"` | since: the player gave a ship, fleet or planet an order of that kind |
| `turn = N` | the game has reached turn N (the first turn is 0) |
| `turns_passed = N` | since: N turns have ended |
| `colonies`, `ships`, `bases`, `units`, `fleets`, `designs` `= N` | the player's empire has N of them |
| `research_queued = N`, `construction_queued = N` | so many items wait in the queues |
| `techs_researched = N` | since: N tech levels researched |
| `systems_explored = N`, `empires_met = N`, `treaties = N` | as named |
| `enemy_ships_destroyed = N` | since: N enemy ships or bases destroyed in the empire's battles |
| `score`, `population`, `minerals`, `organics`, `radioactives` `= N` | as named |

The reference section at the end lists the exact keys, values and command names.

## UI tags

Windows and widgets that lessons point at register a tag each frame with their screen
rectangle (`UiContext::tags`). A step's `highlight` outlines the tagged rectangles with
a pulsing frame, over the classic windows and under the lesson panel, until the step is
done. Tags are:

- `window:<id>` for each window (ids as in `window:` links);
- `command:<id>` for the main window's command buttons, `order:<id>` for the order
  strip, `button:end-turn`, `status:<item>` for the status bar;
- `panel:system`, `panel:report`, `panel:galaxy` and a few more parts of the main
  window;
- `<window id>:<widget>` for a few widgets inside windows that lessons need, and
  `lesson:<button>` for the lesson panel's own (all listed in the reference).

## The lesson panel

A movable panel, at the bottom left of the system view at first, shows the lesson's
title, the step (N of M) or the objectives with check marks, and the text.

- Tutorials: Back and Next. A step with a `done` condition moves on by itself the
  moment its condition holds (at once if it already holds when it is shown); Next stays
  dim until then. A step without one waits for Next. Back shows earlier steps, which
  stay done. **Read more** opens the step's `manual` page; **Leave Lesson** ends the
  lesson's game (after asking).
- Training games: the objectives with lamps (green when met, red when a deadline
  passed), the hint that came up last (with OK), and the briefing page with Previous
  and Next through its series. Pages come up at the start of their turn.
- **Hide** closes the panel; Ctrl+H or the status bar's **T** button re-opens it. A new
  step, page or hint opens it again.

Finishing a lesson or winning a training game shows the result (Next Lesson, Keep
Playing, Learn); losing one offers Try Again. The Learn window then marks it done.
Progress is stored with the client settings (`classic_settings.toml`, `[learn]
done`), never in the game.

The panel only reads the game. Lessons change the game only through the player's own
commands (every command `ClassicSession::issue()` accepts is reported to the lesson), so
the engine stays as it is (CLAUDE.md, determinism). `learn::LessonProgress` holds the
rules that move a lesson on; the client's `LessonRunner` draws it.

## The Learn window and the manual

The **Learn** window has three tabs. Tutorials and Training list the lessons in file
order with their length and a Done mark, and show the chosen one's summary and its
steps or objectives; **Start** (or a double click) starts its game. During a game this
first asks, since the lesson's game replaces the one being played. Manual lists the
chapters and their sections.

The manual viewer shows the contents tree and a search box (every word must appear in
a section) on the left and the page on the right. **Back** and **Forward** (or Alt+Left
and Alt+Right) go through the pages read, **Contents** goes to the first page. Window
and Help links work during a game; in the front end they are dimmed. Shift+F1 opens the
page whose front matter names the window in front (or `main`). When the player's
install has the original's HTML manual (a `Manual` folder with a web page in it),
**Original Manual** opens that page in the browser.

## Command line

```sh
opense4 --tutorial=<slug>        # start a lesson
opense4 --tutorial=<slug>:<N>    # ... at step N, the steps before it done (checking content)
opense4 --training=<slug>        # start a training game
opense4 --manual[=<slug>[#<anchor>]]   # open the manual
opense4 --learn-dir=<dir>        # read the content from this folder only
```

They combine with `--screenshot` for headless checks, and `--tutorial` with `--open` to
show a window over the lesson. `--open=learn[:<tab>]` shows the front end's Learn
window, and `--open=manual` the manual in a quick game.

## Tests

A test (`tests/test_learn.cpp`) loads every built-in lesson, training game and manual
page from `assets/learn` and checks:

- every condition key, command name, order kind and kind of selection is known;
- every manual link (and every `manual` of a step) points at an existing page and
  anchor, every `window:` link at a window that can be opened, every `help:` link at a
  Help tab;
- every window id and UI tag exists.

Unit tests cover the Markdown parser, the loaders (with their errors), each condition
against the engine fixture, and a tutorial and a training game played through
`learn::LessonProgress`. `tests/test_learn_client.cpp` checks that the client's window
ids are the ones lessons use.

## Reference

The code that defines these lists: `src/learn/lesson.cpp` (keys of the files),
`src/learn/condition.cpp` (condition keys), `src/learn/ids.cpp` (window ids, Help tabs,
kinds of selection, order kinds, UI tags); the command names come from
`src/game/commands.hpp`.

### Lesson and training files

| Key | Where | Value |
|---|---|---|
| `title` | top, required | the name in the Learn window and the panel |
| `summary` | top | one or two sentences for the Learn window |
| `minutes` | top | about how long it takes |
| `[setup]` | top | how the game is created (below) |
| `[[step]]` | tutorials | `title` and `text` (required), `highlight` (a UI tag or a list of them), `done` (a condition), `manual` (`"slug"` or `"slug#anchor"`) |
| `[[objective]]` | training | `text` and `when` (required), `by_turn` |
| `[[page]]` | training | `title` and `text` (required), `turn` (default 0), `series` (default none) |
| `[[hint]]` | training | `text` and `when` (required), `title` (default "Hint") |
| `[fail]` | training | `when` (required), `text` |

Texts are Markdown as in the manual; their links are checked like the manual's.

### Setup keys

The lesson's game is a quick start (the intro's Quick Start path, `quickStartSetup`)
for `race` with `computer_players` opponents picked from the seed; the other keys then
set the game options. Keys left out keep the quick start's values.

| Key | Value | Without it |
|---|---|---|
| `seed` | a whole number: the same galaxy every time | the command line's `--seed` (random) |
| `race` | a race preset (folder or name); avoid it unless the lesson needs one | the data set's first race |
| `computer_players` | 0 to 19 | 0 |
| `systems` | 1 to 255 star systems | rolled from the quadrant size |
| `quadrant` | a quadrant type of the data set | the default |
| `quadrant_size` | `"small"`, `"medium"`, `"large"` | medium |
| `turn_style` | `"turn-based"`, `"simultaneous"` | turn-based |
| `tech_level` | starting technology: `"low"`, `"medium"`, `"high"` | low |
| `tech_cost` | `"low"`, `"medium"`, `"high"` | medium |
| `starting_resources` | of each resource | 20000 |
| `starting_planets` | 1, 3, 5 or 10 | 1 |
| `events` | `"none"`, `"low"`, `"medium"`, `"high"` | low |
| `ai_difficulty` | `"low"`, `"medium"`, `"high"` | medium |
| `no_tactical_combat` | true or false | false |
| `all_systems_seen` | true or false | false |
| `omnipresent` | true or false | false |
| `no_ruins` | true or false | false |

### Condition keys

Besides `all = [...]`, `any = [...]` and `not = {...}`:

| Key | Value | Holds when |
|---|---|---|
| `window` | a window id | that window is open |
| `selected` | a kind of selection | the main window has it selected |
| `command` | a command name | since: the player gave a command of that type that the game accepted |
| `order` | an order kind | since: the player gave a ship, fleet or planet an order of that kind |
| `turn` | N | the game has reached turn N (the first turn is 0) |
| `turns_passed` | N | since: N turns have ended (in a turn-based game, game turns) |
| `colonies` | N | the empire has N colonies |
| `population` | N | its colonies hold N million people |
| `ships` | N | it has N ships (mothballed ones not counted) |
| `bases` | N | it has N bases (mothballed ones not counted) |
| `units` | N | it has N units: fighters, satellites, mines, troops, drones and weapon platforms, in space and in cargo |
| `fleets` | N | it has N fleets |
| `designs` | N | it has N designs that are not obsolete (the starting designs included) |
| `research_queued` | N | N research projects are queued |
| `construction_queued` | N | N items wait in its construction queues, all of them together |
| `techs_researched` | N | since: N tech levels were researched |
| `systems_explored` | N | it has explored N systems, its home system included |
| `empires_met` | N | it has met N other empires that are still alive |
| `treaties` | N | it holds N treaties of Non-Aggression or better |
| `enemy_ships_destroyed` | N | since: N ships or bases of other empires were destroyed in battles it fought (the engine keeps no kill counter, so each battle is counted when the client sees it) |
| `score` | N | its score is N |
| `minerals`, `organics`, `radioactives` | N | it has N of that resource stored |

"Since" counts from where the step began in a tutorial, from the start of the game in a
training game. `command` and `order` see only commands given in the client during the
lesson.

**Kinds of selection** (`selected`): `planet` (a planet or asteroid field), `colony`
(one of the player's; `planet` holds too), `ship`, `base`, `unit` (a group of units in
space), `fleet` (the selected ship is in one of the player's fleets; `ship` holds too),
`star`, `warp-point`, `sector` (the report lists everything in a sector), `system`
(nothing is selected and the report shows the system).

**Order kinds** (`order`): `move-to`, `warp`, `attack`, `resupply`, `repair`,
`explore`, `colonize`, `sentry`, `load-cargo`, `drop-cargo`, `launch-units`,
`recover-units`, `cloak`, `decloak`, `sweep-mines`, `use-component`,
`stellar-manipulation`, `move-to-waypoint`, `self-destruct`.

### Command names

The command types of `src/game/commands.hpp`, as `game::commandName` gives them. The
ones a lesson is likely to wait for:

| Name | The player... |
|---|---|
| `SetOrders` | gave a ship, fleet or planet orders (`order` names the kind) |
| `CreateFleet`, `JoinFleet`, `LeaveFleet`, `DisbandFleet`, `SetFleetOptions` | worked with fleets (Fleet Transfer) |
| `QueueAdd`, `QueueRemove`, `QueueMove`, `QueueSetCount`, `QueueFlags` | changed a construction queue |
| `CreateDesign`, `EditDesign` | created a design, saved an edited one |
| `SetResearch` | changed the research projects or their options |
| `SetIntel` | changed the intelligence projects |
| `SendMessage`, `AnswerMessage` | sent or answered a diplomatic message |
| `SetColonyType`, `AbandonPlanet`, `TransferCargo` | as named |
| `SetWaypoint` | set or cleared a waypoint |

The others: `SetVehicleStrategy`, `Rename`, `Scrap`, `Mothball`, `SetMinister`,
`Retrofit`, `SetDesignObsolete`, `DeleteDesign`, `SetSystemFlags`, `SetSystemNote`,
`TagMinefield`, `SetStrategy`, `SetRepairPriorities`, `SetDesignTypes`,
`SetColonyTypes`, `SetEmpireOptions`, `SetMinisters`, `SetEncounterOptions`,
`EnterSector`, `OpenVehicleReport`, `QueueReplaceFacility`, `DecideWar`. Ending the turn
is no command: wait for it with `turns_passed`.

### Window ids

The kebab-case of the client's `ScreenId` names (`client/classic/screen_id.hpp`), for
`window:` links, `window = "<id>"`, `window:<id>` tags and front matter:

`game-menu`, `designs`, `create-design`, `planets`, `colonies`, `ships`, `queues`,
`set-queue`, `research`, `tech-tree`, `empires`, `log`, `empire-status`, `help`,
`galaxy-map`, `empire-options`, `ministers`, `systems-to-avoid`, `waypoints`,
`strategies`, `repair-priorities`, `fleet-transfer`, `cargo-transfer`,
`launch-recover`, `scrap`, `view-orders`, `select-waypoint`, `stellar-manipulation`,
`rename`, `communicate`, `intelligence`, `treaty-grid`, `scores`, `comparisons`,
`history`, `race-report`, `victory-conditions`, `combat-replay`, `tactical-combat`,
`tactical-orders`, `tactical-options`, `combat-simulator`, `strategic-combat`,
`ground-combat`, `save-game`, `load-game`, `settings`, `learn`, `manual`.

A `window:` link cannot open the battle windows (`combat-replay`, `tactical-combat`,
`tactical-orders`, `tactical-options`, `strategic-combat`, `ground-combat`): they need a
battle. Front matter may also name `main`, the main window.

**Help tabs** (`help:` links): `components`, `facilities`, `ship-sizes`, `unit-sizes`,
`tech-areas`, `treaties`, `intel-projects`, `formations`, `hotkeys`, and `weapons` for
the Weapons Report.

### UI tags

| Tag | What it outlines |
|---|---|
| `window:<id>` | the window, while it is open |
| `command:<id>` | a command button: `game-menu`, `designs`, `planets`, `colonies`, `ships`, `queues`, `research`, `empires`, `log`, `empire-status`, `help` |
| `button:end-turn` | the End Turn button |
| `order:<id>` | a button of the order strip (below) |
| `status:empire`, `status:leader`, `status:date` | the status bar's flag and empire name, leader, game date |
| `status:resources`, `status:minerals`, `status:organics`, `status:radioactives` | the treasury, all of it or one resource |
| `status:lesson` | the T button (only during a lesson) |
| `panel:system`, `panel:report`, `panel:galaxy` | the system view, the report panel, the galaxy panel |
| `panel:commands`, `panel:orders`, `panel:report-tabs` | the command buttons, the order strip, the report's tabs |
| `cycle:ship`, `cycle:fleet`, `cycle:colony` | the previous and next selectors |
| `research:areas`, `research:queue`, `research:tech-tree` | Research: the list of areas (a click adds a project), the current projects, Tech Tree |
| `set-queue:available`, `set-queue:queue` | Set Construction Queue: what can be built (a click adds it), the queue |
| `queues:list` | Construction Queues: the list of queues |
| `designs:list`, `designs:create`, `designs:simulator` | Designs: the list, Create, Simulator |
| `create-design:hull`, `create-design:name` | Create Design: the size and the name |
| `create-design:on-design`, `create-design:components` | the components on the design, those that can be added |
| `create-design:warnings`, `create-design:save` | the problems box, Create Design (Save Design when editing) |
| `fleet-transfer:ships`, `fleet-transfer:fleets`, `fleet-transfer:create-fleet` | Fleet Transfer: the ships outside fleets, the fleets, Create Fleet |
| `combat-simulator:begin` | the Combat Simulator's Begin |
| `planets:list`, `planets:send-colony-ship` | Planets: the list, Send Colony Ship |
| `colonies:list`, `colonies:queue` | Colonies: the list, Constr. Queue |
| `log:messages` | the Log's messages |
| `empires:list` | the Empires window's empires |
| `help:tabs` | the Help window's tabs |
| `lesson:panel`, `lesson:next`, `lesson:read-more` | the lesson panel, its Next and Read More |

Order strip ids (`order:<id>`): `move-to`, `warp`, `move-to-waypoint`, `colonize`,
`attack`, `fleet-transfer`, `resupply`, `repair`, `clear-orders`, `build-queue`,
`cargo-transfer`, `launch-recover`, `load-cargo`, `drop-cargo`, `launch-remote`,
`recover-remote`, `sentry`, `explore`, `patrol`, `repeat-orders`,
`stellar-manipulation`, `rename`, `scrap`, `strategy`, `view-orders`, `sweep-mines`,
`scrap-facilities`, `jettison`, `cloak`, `decloak`, `use-component`, `use-facility`,
`abandon-planet`, `convert-resources`, `minister`, `replay-play`, `replay-ship`,
`replay-step`, `replay-rewind`.

To add a tag: tag the item in the client (`ui.tagItem("<tag>")` after it, or
`ui.tagFrame` with a frame rectangle) and list it in `src/learn/ids.cpp`.
