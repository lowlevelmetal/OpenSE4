# Learning to play: tutorials, training and the manual

OpenSE4 teaches the game in three ways, all reached from the **Learn** window. The
intro screen's **Tutorial** button opens it on its Tutorials tab and **Scenario** on its
Training tab (the original's intro has both buttons); **Manual** opens the manual
alone; during a game the Game Menu's **Learn** button opens it:

| Part | What it is |
|---|---|
| **Tutorials** | Guided lessons. A lesson starts a prepared game and walks the player through it one step at a time. Each step explains something, outlines the button or window it is about, and waits until the player has done it (or pressed Next). While a step waits, only what it is about responds (see "The input lock"). |
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
learned = ["Selecting planets and ships", "Ending a turn"]   # the result's recap
suggest = "training:land-rush"   # what the result offers next (default: the next in the list)

[setup]                     # how the lesson's game is created
seed = 12345                # fixed, so the galaxy is the same every time
computer_players = 0
# ... further keys as the setup model supports (see the reference below)

[[step]]                    # tutorials: steps in order
title = "Your home system"
text = """Markdown, as in the manual."""
highlight = ["panel:system"]     # UI tags to outline (see "UI tags")
allow = ["panel:report"]         # more UI tags the player may use (see "The input lock")
keys = ["Ctrl+L"]                # more keys the player may press
done = { selected = "planet" }   # omitted: the player presses Next
progress = ["turns_passed"]      # more counts for the progress line (see "The lesson panel")
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
so "fewer than" is written with `not` (`not = { colonies = 1 }`: no colony); a count of
0 always holds and is an error (`turn = 0`, "from the first turn", is allowed). The set is
fixed in code (`learn/condition.hpp`); an unknown key, or a value outside its key's
vocabulary (window id, kind of selection, command name, order kind, window tab, option,
treaty kind), is a load error that names the file and line. A few keys take `true` or
`false`.

Counters marked "since" count from where the step began (the first time it was shown)
in a tutorial, and from the start of the game in a training game. `selected` is one of
them: it holds only for a selection the player made since the step began, so the
homeworld a game starts with, or a ship selected for an earlier step, does not count
until the player selects it again.

**`design_type`** qualifies the `selected`, `order` and `command` keys written beside it in
one table: only a vehicle (or design) of that design type counts, so a step about an attack
ship is not done by the colony ship next to it:

```toml
done = { order = "explore", design_type = "Attack Ship" }   # Explore, given to an attack ship
done = { selected = "ship", design_type = "Attack Ship" }   # an attack ship is selected
done = { command = "QueueAdd", design_type = "Colony" }     # a colony ship was queued
```

The value is one of the 39 AI design types of spec 05 §7.7 (`"Attack Ship"`, `"Defense
Base"`, ...), or `"Colony"` for every colony ship type (`"Colony (Rock)"` and the others);
letter case does not matter. An order counts when it went to a vehicle of that type, or to a
fleet with one in it (so a ship added to a fleet by mistake never blocks a later step);
`selected` reads the selected vehicle's design (`ship`, `base`, `unit` or `fleet` only);
among the commands it qualifies `SetOrders`, `QueueAdd` (a ship or base of that type),
`CreateDesign`, `JoinFleet` (the ship that joined) and `CreateFleet` (one of its ships). Anywhere else it is a load error. Prefer it whenever a step names a kind of ship:
a step that any ship can do can be done with the wrong one.

| Key | Holds when |
|---|---|
| `window = "<id>"` | that window is open |
| `tab = "<window>:<tab>"` | an open window shows that tab or filter (`log:combat`, `planets:colonizable`) |
| `selected = "<kind>"` | since: the player selected that kind of thing in the main window (planet, ship, fleet, system, ...) |
| `command = "<name>"` | since: the player gave a command of that type |
| `order = "<kind>"` | since: the player gave a ship, fleet or planet an order of that kind |
| `design_components = N`, `design_hull_chosen = true` | the open Create Design window's design has N components; the player picked its hull |
| `design_type_chosen = "<type>"`, `design_named = true` | the open Create Design window's Design Type box shows that type; its Design Name box holds a name no other design has |
| `simulator_owners = N`, `simulator_items = N` | the open Combat Simulator has items for N races; N items |
| `option = "<name>"` | that setting of the empire is on (`research-evenly`, `planet-names`, ...; `not` for off) |
| `treaty = "<kind>"` | the empire holds that treaty with another empire (`war`: is at war with one) |
| `turn = N` | the game has reached turn N (the first turn is 0) |
| `turns_passed = N` | since: N turns have ended |
| `colonies`, `ships`, `bases`, `units`, `fleets`, `designs` `= N` | the player's empire has N of them |
| `research_queued = N`, `construction_queued = N` | so many items wait in the queues |
| `techs_researched = N` | since: N tech levels researched |
| `systems_explored = N`, `empires_met = N`, `treaties = N` | as named |
| `enemy_ships_destroyed = N` | since: N enemy ships or bases destroyed in the empire's battles |
| `planets_captured = N` | since: the empire took N colonies from empires it is hostile to |
| `battle_begun = true`, `battle_order = "<kind>"` | the open Tactical Combat window's battle has begun; since: the player gave a tactical order of that kind (`move`, `fire`, `end-turn`, ...) |
| `battle_turn = N` | the open Tactical Combat window's battle has begun and reached combat turn N |
| `score`, `population`, `minerals`, `organics`, `radioactives` `= N` | as named |

The reference section at the end lists the exact keys, values and command names.

## Text tokens

Lessons name design types ("your attack ship"), but the game's windows list designs by
their names, which come from the race's list and differ from game to game. A text token in
a step, a briefing page, a hint or a `learned` line is filled in from the player's game when
the panel shows it:

| Token | Shows |
|---|---|
| `{design:<type>}` | the name of the player's newest design of that design type that is not obsolete (of designs made in one turn, the one made last); `{design:Colony}` the colony ship of the race's own planet type first. Without such a design, the type itself |

```toml
text = """**Click {design:Attack Ship} twice** in **Items to choose**."""
```

The type is checked when the file is read, like `design_type` (an unknown token or type is a
load error with its line). A brace that is not followed by a lower-case word and a colon is
plain text. The Combat Simulator's item list and Set Construction Queue's lists show each
design's type at the right of its row (and the simulator in a tooltip), an OpenSE4 addition,
so the player can check the name against the type.

## UI tags

Windows and widgets that lessons point at register a tag each frame with their screen
rectangle (`UiContext::tags`) and the window it was drawn in. A step's `highlight`
outlines the tagged rectangles with a pulsing frame until the step is done. Each frame is
drawn in its part's own window, so whatever lies above that window covers the frame as
it covers the part: a prompt such as "Leave the lesson?", the Game Menu and its Quit
question, or another window. Tags are:

- `window:<id>` for each window (ids as in `window:` links);
- `command:<id>` for the main window's command buttons, `order:<id>` for the order
  strip, `button:end-turn`, `status:<item>` for the status bar;
- `panel:system`, `panel:report`, `panel:galaxy` and a few more parts of the main
  window, and single things in it: `sector:home` (the homeworld's sector) and
  `report:colony` (the player's colony in the report's list of a sector);
- `<window id>:<widget>` for a few widgets inside windows that lessons need,
  `<window id>:<tab>` for the tabs and filters of some windows (the same names `tab`
  conditions use), and `lesson:<button>` for the lesson panel's own (all listed in
  the reference). Debug builds log a tag the client registers that the list lacks;
- `<window id>:close` for the Close button of every window that has one.

The same tags say what the player may use while the input lock is on (next sections).

## The lesson panel

A movable panel shows the lesson's title, the step (N of M) or the objectives with check
marks, and the text. Until the player moves it, it sits over the galaxy panel while
only the main window shows, and at the bottom left of the system view while a window is
open, where it hides the least of the classic windows (their buttons are on the right).
When that spot would hide what the active step outlines or allows, it takes the system
view's top left or a corner of the screen instead: the spot that hides the smallest share
of the step's tags wins (each tag counts by the share of it hidden, so a small button
weighs as much as a large map). Once dragged, it stays where it was put.

- Tutorials: the step's title, and under it the **progress line** while the active step
  waits for something that can be counted: each numeric fact of its `done` condition with
  its value and target ("Systems explored: 3 of 5", "Turns: 1 of 3"; facts under `not`
  are left out), then the step's `progress` facts without a target once they are above 0 ("Turns: 2",
  "Combat turn: 3"), for a step whose `done` is not a count ("end turns until you meet someone").
  `learn::LessonProgress::counters` makes it.
- Tutorials: Back and Next. A step with a `done` condition moves on by itself the
  moment its condition holds (at once if it already holds when it is shown); Next stays
  dim until then. A step without one waits for Next. Back shows earlier steps again for
  reading, which stay done; the step the lesson is at (the *active step*) keeps its
  outlines, its lock and its condition meanwhile, and Next goes back to it (a note in
  the panel says so). When the active step's condition is met while an earlier step is
  shown, the panel moves on to the next step. **Read more** opens the step's `manual`
  page; **Leave** ends the lesson's game (after asking).
- **Skip**: when the active step's outlined targets have been off the screen for three
  seconds, or the step has waited two minutes, Next turns into Skip, which gives the step
  up and goes on. A lesson can never get stuck on a step whose condition can no longer
  be met (a scout destroyed, a battle ended early).
- **Free Play** switches the input lock off (and on again); see the next section.
- Training games: the objectives with lamps (green when met, red when a deadline
  passed), the hint that came up last (with OK), and the briefing page with Previous
  and Next through its series. Pages come up at the start of their turn.
- **Hide** closes the panel; Ctrl+H or the status bar's **T** button re-opens it. A new
  step, page or hint opens it again.
- Leave Lesson and Start (in the Learn window) ask with a Yes/No box: Y means Yes; N,
  Esc and Enter mean No (spec 06 §3.4). Esc or Enter in the result box is Keep Playing.

Finishing a lesson or winning a training game shows the result: the lesson's `learned`
lines as a recap ("What you learned"; "What you practised" for a training game), and what
comes next, "Next: <title> (<minutes> min)" with its summary: the lesson's `suggest`, else the
next one of its kind in the list (`Library::following`). Its button says **Next Lesson** for a
tutorial, **Training Game** for a training game after a tutorial (the last tutorial suggests
the first training game) and **Next Game** after a training game; with nothing next the
dialog says so and has no such button. Then **Keep Playing** and **Learn**. Losing a training
game offers Try Again. The Learn window then marks it done.
Progress is stored with the client settings (`classic_settings.toml`, `[learn]
done`), never in the game.

The panel only reads the game. Lessons change the game only through the player's own
commands (every command `ClassicSession::issue()` accepts is reported to the lesson), so
the engine stays as it is (CLAUDE.md, determinism). `learn::LessonProgress` holds the
rules that move a lesson on; the client's `LessonRunner` draws it.

## The input lock

While a tutorial step is active, the player can use only what it is about. Everything
else is dimmed (a spotlight over the classic windows, under the panel) and does not
respond: clicks, drags, the mouse wheel and hovering do nothing there, and keys do
nothing unless the step allows them. The lock is for tutorials only; training games
are free play.

What a step allows:

- **An action step** (one with `done`): its `highlight` tags and its `allow` tags. A
  click, drag or wheel turn over their rectangles passes.
- **An explanation step** (no `done`): its `highlight` tags can be pointed at (tooltips
  show) and scrolled, but not clicked, so "hover over the buttons" never opens a
  window by mistake. Only its `allow` tags can be clicked (the report's tabs, a design
  list to browse, a name box to type in).
- Always: the lesson panel (Back, Next or Skip, Read More, Hide, Free Play, Leave) and
  the status bar's **T** button.
- **Windows the step works in that are closed**: the tags that open them, two levels
  deep (a step about the designer's components allows Create in Designs, and the
  Designs button when Designs is closed too). A step never waits behind a closed window.
- **Windows the step says nothing about** are the player's: a window an allowed click
  opened (a component report, the waypoint list, the Log a turn opened), with Esc and
  Enter when it is in front. A window *is* about the step when one of the step's tags
  names it (`research:areas` and `window:research` name the Research window): then only
  the tagged parts of it respond.
- **The game's own questions** always work: the End Turn question, battle notices and
  the Tactical or Strategic question, Colony Type, Attack Sector, Combat Complete,
  Yes/No boxes and error boxes (every ImGui popup and every window that calls
  `UiContext::promptWindow()`), with their answer keys (Y, N, T, S, Enter, Esc).
- **Text fields**: while one has the keyboard, every key passes.

Keys: a step's `keys` list (`"F12"`, `"Ctrl+L"`, `"Alt+1"`, `"Escape"`; modifiers
`Ctrl+`, `Shift+` and `Alt+` before a letter, digit, `F1`-`F12` or a named key: `Enter`,
`Escape`, `Space`, `Tab`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`,
`PageDown`, the arrows `LeftArrow` to `DownArrow`, `Comma`, `Period`, `Minus`, `Equal`,
`Slash`). Besides those, an allowed tag brings the key that does what a click on it
does, as the player has bound it: a command button its F-key, `button:end-turn` F12,
an order button its letter, `cycle:ship` Space and the ship keys, and a `<window>:close`
tag Esc and Enter. Ctrl+H (the panel) always passes. Keys of tags inside windows (the
Tactical Combat window's E) are not known to the lock: list them in `keys`.

How it works: at the end of each frame the client builds the lock
(`client/classic/lesson_lock.hpp`, `makeLockState`) from that frame's tagged rectangles,
the open windows, the prompts and whether a text field has the keyboard. In the next
frame every SDL event passes through it (`Mode::filterEvent`) before Dear ImGui and the
main window see it: a press outside the allowed areas is dropped with its release, a
press inside lets its drag go anywhere (so the panel and sliders can be dragged), and
pointer motion over a locked area tells ImGui the pointer is nowhere, so nothing there
lights up. Nothing is disabled in the widgets themselves. A refused click flashes the
outlines white and shows a short note by the pointer ("the lesson is waiting for the
outlined part", or "press Next" on an explanation step).

**Free Play**, a check box in the lesson panel, switches the lock off for every
tutorial (outlines and conditions stay); it is a client setting (`classic_settings.toml`,
`[learn] free_play`), off by default. The manual's window links are dimmed while the
lock is on.

Writing steps:

- **The action first, in bold**, then a short why: "**Open the Planets window** with its
  command button or `F4`. It lists every planet you have seen." A step that waits for an
  action starts with it in bold (the content test checks this); an explanation step starts
  with its point and ends with "Press **Next**".
- **Short.** The panel shows about 60 words without scrolling. Aim for about 50; a tutorial
  step may have **75 words at most** (the content test fails above that and reports steps
  above 60; `python3 tools/check_lessons.py --words` prints the counts). Move details into
  the manual page that **Read More** opens, and split a step that asks for two actions in
  two steps.
- **Name the thing on the screen.** Lists show design names, not types: write
  `{design:Attack Ship}` (see "Text tokens"), and tell the player what to check ("its
  **Class** must say **{design:Attack Ship}**").
- **Guard against the plausible wrong action.** A condition that the wrong ship, the wrong
  item or an earlier click can satisfy moves on too early or leaves the lesson stuck: use
  `design_type`, order the steps so that the thing that could be misused is used up first (the
  colony ship gets its orders before the attack ships are selected), and check every step for
  a wrong action that makes a later one impossible.
- **Say what happens by itself.** The Log opens at the start of a turn and the Colony Type
  question comes when it closes: a step after End Turn says so. A step whose outlined button
  lies under an open window says "Close the window (`Esc`), then press **End Turn**".
- **Count what takes time.** A step that waits for turns, systems or combat turns has a
  numeric `done` or a `progress` list, so the panel shows how far it has come.

Writing steps for the lock:

- An action step's `highlight` and `allow` must include what its `done` needs: a tag
  that opens the window it waits for, the order button of the order, the widget that
  gives the command, a way to close a window (`window:<id>`, `<id>:close` or the
  `Escape` key), End Turn for anything that comes with turns, an `Alt+digit` key for
  `SetWaypoint`. The content test checks this (`learn::reachProblems`).
- Say exactly what to click, and only what is allowed: "another way" hints belong in a
  later step or are written as something for later.
- An explanation step says to press **Next** (the last step: **Finish**); an action
  step does not, since it moves on by itself.
- A step that the player cannot do in one go (end turns until...) allows what the
  player needs meanwhile (the ship arrows, the order button).

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

They combine with `--screenshot` for headless checks. `--lesson-check` (with
`--tutorial=<slug>:<N>`) opens the windows step N works in (a sample battle for the
battle windows), and a few frames later prints
`lesson-check <slug>:<N> areas=<k> ok` or `missing: <tags>` for highlighted or allowed
tags that are not on the screen, and exits with 1 when some are (or the lock is off).
A few tags that depend on the moment (a selected piece's weapons, Communicate's lists, the
colony's row in the report's list of a sector) only warn (`situational:`), as do outlines the panel covers (`under-panel:`).
`tools/check_lessons.py` first counts the words of every tutorial step (above 75 fails,
above 60 is reported; `--words` stops there and needs no game), then runs this for every
step of every tutorial, headless (`--screenshots <dir>` saves one picture per step; keep
them out of the repository). With `--tutorial` and
`--training`, `--turns=N` lets the computer play every empire for N turns first (the
lesson's counters still start at its first turn), and `--open` shows a window over the
lesson as with a quick start, `--open=tactical` (and the other battle windows) a sample
battle, `--open=none` just the main window. `--open=learn[:<tab>]` shows the front end's
Learn window, and `--open=manual` the manual in a quick game.

## Tests

A test (`tests/test_learn.cpp`) loads every built-in lesson, training game and manual
page from `assets/learn` and checks:

- every condition key and value (command name, order kind, kind of selection, window
  tab, option, treaty kind) is known;
- every manual link (and every `manual` of a step) points at an existing page and
  anchor, every `window:` link at a window that can be opened, every `help:` link at a
  Help tab;
- every window id and UI tag exists.

A second content test checks every tutorial step against the input lock: a step
without `done` says Next (or Finish), one with `done` does not, outlines something, and
its `done` can be reached with only its highlighted and allowed tags and keys
(`learn::reachProblems`). A third keeps the steps short: at most 75 words of text each (a
token counts as one word; above 60 is reported), and a step with `done` starts with its
action in bold.

The input scripts of `tests/input` (docs/BUILDING.md "Input scripts") play every
tutorial from its first step to its result through the client's own input, under the
input lock: each step done by clicking what it tells the player to click, Next only on
steps that explain, never Skip or Free Play; `tutorial-wrong-ship.script` gives the colony ship the Explore
order that tutorial 2 asks of an attack ship, and checks that the step waits for an attack
ship and the colony is still founded; `tutorial-wrong-fleet.script` adds the colony ship to
tutorial 5's fleet by mistake, and checks that the step waits and the fleet's orders still
count. Others play each training game's briefing,
first turns and Leave Game, the result dialog won and lost, the Learn window and the
manual. They need your installed game:
`OPENSE4_CLASSIC_DATA=auto python3 tools/run_input_tests.py`. A lesson whose steps
change needs its script changed with it.

Unit tests cover the Markdown parser, the loaders (with their errors), each condition
against the engine fixture (`design_type` among them), the text tokens, the counters of the
progress line, `learned` and `suggest`, a tutorial (with Back, the active step and Skip) and a
training game played through `learn::LessonProgress`, the step access rules, and the
input lock (`tests/test_lesson_lock.cpp`: hit-testing, drags, keys, prompts, open and
closed windows). `tests/test_learn_client.cpp` checks that the client's window
ids are the ones lessons use.

## Reference

The code that defines these lists: `src/learn/lesson.cpp` (keys of the files),
`src/learn/condition.cpp` (condition keys), `src/learn/ids.cpp` (window ids, Help tabs,
kinds of selection, order kinds, window tabs, options, treaty kinds, UI tags); the
command names come from `src/game/commands.hpp`.

### Lesson and training files

| Key | Where | Value |
|---|---|---|
| `title` | top, required | the name in the Learn window and the panel |
| `summary` | top | one or two sentences for the Learn window |
| `minutes` | top | about how long it takes |
| `learned` | top | a list of short sentences: the result's recap (tokens allowed) |
| `suggest` | top | `"tutorial:<slug>"` or `"training:<slug>"`: what the result offers next, instead of the next one in the list (checked) |
| `[setup]` | top | how the game is created (below) |
| `[[step]]` | tutorials | `title` and `text` (required), `highlight` (a UI tag or a list of them), `allow` (the same), `keys` (a key chord or a list of them), `done` (a condition), `progress` (a numeric condition key or a list of them, shown without a target), `manual` (`"slug"` or `"slug#anchor"`) |
| `[[objective]]` | training | `text` and `when` (required), `by_turn` |
| `[[page]]` | training | `title` and `text` (required), `turn` (default 0), `series` (default none) |
| `[[hint]]` | training | `text` and `when` (required), `title` (default "Hint") |
| `[fail]` | training | `when` (required), `text` |

Texts are Markdown as in the manual; their links are checked like the manual's, and their
tokens like `design_type`.

### Setup keys

The lesson's game is a quick start (the intro's Quick Start path, `quickStartSetup`)
for `race` with `computer_players` opponents picked from the seed and no neutral empire
(the intro's Quick Start instead rolls random computer and neutral players, as a new game
does); the other keys then set the game options. Keys left out keep the quick start's values. As in any quick
start, the player's empire gets the designs of one Design minister run and no ships
(spec 01 §2.1, §3.6); `starting_ships` is the only way a lesson adds ships.

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
| `ai_difficulty` | `"low"`, `"medium"`, `"high"`: the level of the lesson's computer empires | medium |
| `no_tactical_combat` | true or false | false |
| `complete_tech_tree` | true or false: "Players can see the complete tech tree", the Research window's Tech Tree button | false |
| `all_systems_seen` | true or false | false |
| `omnipresent` | true or false | false |
| `no_ruins` | true or false | false |
| `starting_ships` | a list of design types, such as `["Attack Ship", "Attack Ship", "Colony"]`: **OpenSE4 lesson extension**, see below | none, as in every game |

**Starting ships** (an OpenSE4 lesson extension, not a setting of the original). A normal
game starts with no ships at all (spec 01 §3.6). A lesson that teaches with ships from its
first step lists them in `starting_ships`: each entry builds one ship at the player's
homeworld when the game is created, from the newest of the player's quick start designs
with that design type (the 39 AI design types of spec 05 §7.7, such as `"Attack Ship"`,
`"Colony (Rock)"`, `"Population Transport"`). `"Colony"` stands for the colony ship of the
race's own planet type, so a lesson does not depend on its race. An entry the player has
no design for builds nothing; any other text is a load error. The ships are ordinary
ships, built with the game (`game::StartExtras::lessonShips`, never saved or sent over the
network). Tutorials 1, 2, 5 and 7 use it (two attack ships, and a colony ship where the
lesson settles a planet), and the first lesson tells the player that a normal game starts
without them. The training games do not use it: they are played from a normal start.

### Condition keys

Besides `all = [...]`, `any = [...]` and `not = {...}`, and the `design_type` qualifier of
`selected`, `order` and `command` (see "Conditions"):

| Key | Value | Holds when |
|---|---|---|
| `window` | a window id | that window is open |
| `tab` | a window tab (below) | an open window shows that tab or filter |
| `selected` | a kind of selection | since: the player selected it in the main window, and it is still selected |
| `command` | a command name | since: the player gave a command of that type that the game accepted |
| `order` | an order kind | since: the player gave a ship, fleet or planet an order of that kind |
| `design_components` | N | the Create Design window is open and its design has N components |
| `design_hull_chosen` | true or false | the Create Design window is open and the player picked a hull in its Size list (`false`: has not yet) |
| `design_type_chosen` | a design type | the Create Design window is open and its Design Type box shows that type (`"Colony"`: any colony ship type) |
| `design_named` | true or false | the Create Design window is open and its Design Name box holds a name no other design has |
| `simulator_owners` | N | the Combat Simulator is open and N races ("Owner for item") have items in the battle (an unowned object is a neutral obstacle and counts for none) |
| `simulator_items` | N | the Combat Simulator is open and the battle has N items |
| `battle_begun` | true or false | the Tactical Combat window is open and its battle has begun (Begin was pressed) |
| `battle_order` | a battle order kind (below) | since: the player gave an order of that kind in a tactical battle that the battle accepted |
| `battle_turn` | N | the Tactical Combat window is open, its battle has begun and it has reached combat turn N |
| `option` | an option (below) | that setting of the empire is on; write `not = { option = "..." }` for off |
| `treaty` | a treaty kind (below) | the empire holds that treaty with another empire it has met that is still alive (`war`: is at war with one; `subjugation` and `protectorate` hold for either side) |
| `turn` | N | the game has reached turn N (the first turn is 0) |
| `turns_passed` | N | since: N turns have ended (in a turn-based game, game turns) |
| `colonies` | N | the empire has N colonies |
| `population` | N | its colonies hold N million people |
| `ships` | N | it has N ships (mothballed ones not counted) |
| `bases` | N | it has N bases (mothballed ones not counted) |
| `units` | N | it has N units: fighters, satellites, mines, troops, drones and weapon platforms, in space and in cargo |
| `fleets` | N | it has N fleets |
| `designs` | N | it has N designs that are not obsolete (a quick start's designs included) |
| `research_queued` | N | N research projects are queued |
| `construction_queued` | N | N items wait in its construction queues, all of them together |
| `techs_researched` | N | since: N tech levels were researched |
| `systems_explored` | N | it has explored N systems, its home system included |
| `empires_met` | N | it has met N other empires that are still alive |
| `treaties` | N | it holds N treaties of Non-Aggression or better |
| `enemy_ships_destroyed` | N | since: N ships or bases of other empires were destroyed in battles it fought (the engine keeps no kill counter, so each battle is counted when the client sees it) |
| `planets_captured` | N | since: N colonies passed to it from empires it is hostile to (taken by invasion; a colony handed over by a friend does not count) |
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
`stellar-manipulation`, `move-to-waypoint`, `self-destruct`, `use-facility`,
`convert-resources`, the Scrap window's orders of a simultaneous game: `scrap`,
`analyze`, `mothball`, `unmothball`, `retrofit`, `fire-on`, and the ministers' own
`seek` and `join-fleet`.

**Battle order kinds** (`battle_order`): `move`, `fire`, `toggle-weapon`, `launch`,
`launch-fighters`, `drop-troops`, `ram`, `capture`, `set-leader`, `set-member`,
`clear-group`, `clear-all-groups`, `auto`, `auto-phase`, `end-turn`, `resolve-combat`.

**Window tabs** (`tab`, and each is a UI tag too), `<window>:<tab>`:

| Window | Tabs |
|---|---|
| `planets` (its filters) | `all`, `colonizable`, `all-colonies`, `enemy-colonies`, `ally-colonies`, `colonizable-empty`, `colonizable-breathable`, `ship-enroute`, `asteroids`, `special` |
| `colonies` | `general`, `value`, `production`, `facilities`, `cargo`, `construction`, `status`, `races`, `orders` |
| `ships` | `general`, `orders`, `cargo`, `fleet`, `maintenance` |
| `designs` | `ship-designs`, `unit-designs`, `enemy-ship-designs`, `enemy-unit-designs` |
| `queues` | `rate`, `usage`, `planet-value`, `facilities`, `cargo` |
| `set-queue` | `ships`, `facilities`, `units`, `upgrades` |
| `tech-tree` | `tech-areas`, `tech-levels` |
| `empires` | `treaty`, `trade`, `tariff` |
| `log` (its categories) | `all`, `construction`, `research`, `intelligence`, `events`, `politics`, `combat`, `misc` |
| `combat-simulator` | `tactical`, `strategic` |

**Options** (`option`), the empire's on/off settings:

- the Research and Intelligence windows: `research-evenly` (Divide Pts Evenly),
  `research-repeat`, `intel-evenly`, `intel-repeat`;
- movement and colonization: `avoid-tagged-minefields`, `avoid-restricted-systems`,
  `choose-colony-type`;
- the Empire Options window: `show-log-at-turn-start`, `confirm-end-turn`,
  `confirm-scrap`, `confirm-stellar-manipulation`, `confirm-delete-research`,
  `confirm-delete-intel`, `confirm-delete-first-queue-item`, `note-similar-abilities`,
  `skip-under-construction`, `skip-damaged`, `stop-once-per-location`,
  `skip-in-fleets`, `warp-point-names`, `planet-names`, `colonizable-markers`,
  `system-grid`, `coordinate-location`, `galaxy-grid-lines`, `galaxy-warp-lines`,
  `latest-construction-only`, `latest-components-only`, `auto-claim-colonized`;
- what windows remember: `planets-no-sys-to-avoid`, `simulator-no-obsolete`,
  `replay-animate`, `replay-fast`, `replay-view-rect`, `replay-grid`, and the designer's
  `design-to-hit` (To Hit Modifiers) and `design-condensed` (Condensed View).

**Treaty kinds** (`treaty`): `war`, `non-intercourse`, `non-aggression`,
`subjugation`, `protectorate`, `trade-alliance`, `trade-research-alliance`,
`military-alliance`, `partnership`.

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
`EnterSector`, `OpenVehicleReport`, `QueueReplaceFacility`, `DecideWar`, `JettisonCargo`,
`CloakColony`, `Analyze`, `SelfDestruct`, `FireOn`. Ending the turn is no command: wait for it
with `turns_passed`.

### Window ids

The kebab-case of the client's `ScreenId` names (`client/classic/screen_id.hpp`), for
`window:` links, `window = "<id>"`, `window:<id>` tags and front matter:

`game-menu`, `designs`, `create-design`, `planets`, `colonies`, `ships`, `queues`,
`set-queue`, `research`, `tech-tree`, `empires`, `log`, `empire-status`, `help`,
`galaxy-map`, `empire-options`, `ministers`, `systems-to-avoid`, `waypoints`,
`strategies`, `repair-priorities`, `fleet-transfer`, `cargo-transfer`,
`launch-recover`, `scrap`, `view-orders`, `select-waypoint`, `stellar-manipulation`,
`rename`, `abandon-planet`, `jettison-cargo`, `convert-resources`, `communicate`, `intelligence`, `treaty-grid`, `scores`,
`comparisons`, `history`, `race-report`, `victory-conditions`, `borders`, `combat-replay`,
`tactical-combat`, `tactical-orders`, `tactical-options` (Combat Options),
`tactical-launch` (Launch Units), `combat-piece-report`, `combat-replay-options`,
`combat-simulator`, `strategic-combat`, `ground-combat`, `finale` (the ending window),
`save-game`, `load-game`,
`options` (Game Menu → Options), `settings`, `learn`, `manual`.

A `window:` link cannot open the battle windows (`combat-replay`, `tactical-combat`,
`tactical-orders`, `tactical-options`, `tactical-launch`, `combat-piece-report`,
`combat-replay-options`, `strategic-combat`, `ground-combat`): they need a battle; nor
`finale`, which only the end of a game opens; nor
`abandon-planet`, `jettison-cargo` and `convert-resources`, which need a planet or a ship. Front matter may also
name `main`, the main window.

**Help tabs** (`help:` links): `components`, `weap-mount`, `facilities`, `ship-sizes`, `unit-sizes`,
`tech-areas`, `treaties`, `intel-projects`, `formations`, `hotkeys`, and `weapons` for
the Weapons Report.

### UI tags

| Tag | What it outlines |
|---|---|
| `window:<id>` | the window, while it is open |
| `<id>:close` | the window's Close button (every window drawn with one) |
| `command:<id>` | a command button: `game-menu`, `designs`, `planets`, `colonies`, `ships`, `queues`, `research`, `empires`, `log`, `empire-status`, `help` |
| `button:end-turn` | the End Turn button |
| `order:<id>` | a button of the order strip (below) |
| `status:empire`, `status:leader`, `status:date` | the status bar's flag and empire name, leader, game date |
| `status:resources`, `status:minerals`, `status:organics`, `status:radioactives` | the treasury, all of it or one resource |
| `status:lesson` | the T button (only during a lesson) |
| `panel:system`, `panel:report`, `panel:galaxy` | the system view, the report panel, the galaxy panel |
| `panel:commands`, `panel:orders`, `panel:report-tabs` | the command buttons, the order strip, the report's tabs |
| `sector:home`, `report:colony` | the homeworld's sector in the system view (while the home system is shown); the player's colony in the report's list of a sector |
| `cycle:ship`, `cycle:fleet`, `cycle:colony` | the previous and next selectors |
| `research:areas`, `research:queue`, `research:tech-tree` | Research: the list of areas (a click adds a project), the current projects, Tech Tree |
| `set-queue:available`, `set-queue:queue` | Set Construction Queue: what can be built (a click adds it), the queue |
| `queues:list` | Construction Queues: the list of queues |
| `designs:list`, `designs:create`, `designs:simulator` | Designs: the list, Create (it asks the vehicle type first), Simulator |
| `create-design:hull`, `create-design:type` | Create Design: the Size box with its list button, the Design Type box with its list button |
| `create-design:name`, `create-design:suggest` | the Design Name box, and the button beside it that lists names |
| `create-design:on-design`, `create-design:components` | the Components on Design strip, the Components Available grid |
| `create-design:warnings`, `create-design:save` | the Warnings box, Create Design (Save Design when editing) |
| `fleet-transfer:ships`, `fleet-transfer:fleets`, `fleet-transfer:create-fleet` | Fleet Transfer: the ships outside fleets, the fleets, Create Fleet |
| `combat-simulator:vehicles`, `combat-simulator:items`, `combat-simulator:owners` | Combat Simulator: the Combat Vehicles list, the Items to choose list (a click adds the item for the chosen race), the Owner for item list (Race 1 to Race 10) |
| `combat-simulator:strategies`, `combat-simulator:begin` | its Strategies and Begin buttons (its Tactical and Strategic tabs: `combat-simulator:tactical`, `combat-simulator:strategic`) |
| `tactical-combat:map`, `tactical-combat:piece`, `tactical-combat:target` | Tactical Combat: the battle map, the selected piece's panel, the target's panel |
| `tactical-combat:weapons` | the selected piece's weapon list (a click switches a weapon on or off) |
| `tactical-combat:options`, `tactical-combat:orders`, `tactical-combat:auto`, `tactical-combat:end-turn` | its Options, Orders and Auto buttons, and Begin (End Turn once the battle has begun) |
| `strategic-combat:begin` | Strategic Combat's Begin |
| `planets:list`, `planets:send-colony-ship` | Planets: the list, Send Colony Ship |
| `planets:filters`, `planets:no-sys-to-avoid` | the Planets filters (each one is `planets:<filter>`, above), No Sys To Avoid |
| `colonies:list` | Colonies: the list |
| `research:divide-evenly`, `research:repeat` | Research: Divide Pts Evenly, Repeat Projects |
| `log:messages`, `log:categories`, `log:send-reply` | the Log's messages, its category buttons (All to Misc; each one is `log:<category>`, above), Send Reply |
| `empires:list`, `empires:intelligence` | the Empires window's empires, its Intelligence button |
| `intelligence:projects`, `intelligence:queue` | Intelligence: the list of projects (a click adds one, asking for its target), the current projects |
| `communicate:message-type`, `communicate:treaty`, `communicate:send` | Communicate: the Message Type list, the treaty list (for treaty messages), Send Message |
| `<window>:<tab>` | a tab or filter button of the windows above (window tabs) |
| `<list>:up`, `<list>:down`, `<list>:track`, `<list>:thumb` | the arrow column of a list: its up and down arrows, the track between them and the thumb in it. `<list>` is the list's tag above (`planets:list:down`) or, for every list of a window, `<window>:<list id>` (`log:list:down`; the list id is the client's, without its `##`) |
| `help:tabs` | the Help window's tabs |
| `lesson:panel`, `lesson:next`, `lesson:read-more` | the lesson panel, its Next and Read More |
| `lesson:free-play`, `lesson:leave` | its Free Play and Leave buttons |

Order strip ids (`order:<id>`): `move-to`, `warp`, `move-to-waypoint`, `colonize`,
`attack`, `fleet-transfer`, `resupply`, `repair`, `clear-orders`, `build-queue`,
`cargo-transfer`, `launch-recover`, `load-cargo`, `drop-cargo`, `launch-remote`,
`recover-remote`, `sentry`, `explore`, `patrol`, `repeat-orders`,
`stellar-manipulation`, `rename`, `scrap`, `strategy`, `view-orders`, `sweep-mines`,
`scrap-facilities`, `jettison`, `cloak`, `decloak`, `use-component`, `use-facility`,
`abandon-planet`, `convert-resources`, `minister`, `replay-play`, `replay-ship`,
`replay-step`, `replay-rewind`.

To add a tag: tag the item in the client (`ui.tagItem("<tag>")` after it, `ui.tagFrame`
with a frame rectangle, or `ui.tagTab("<tab>", shown)` after a tab button) and list it in
`src/learn/ids.cpp`.
