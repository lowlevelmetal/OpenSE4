# Learning to play: tutorials, training and the manual

OpenSE4 teaches the game in three ways, all reached from the intro screen's **Tutorial**
button and from the Game Menu during a game:

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
(`OPENSE4_EMBED_RESOURCES`); files on disk win in developer builds.

```
assets/learn/manual/NN-slug.md       manual chapters, in contents order
assets/learn/tutorials/NN-slug.toml  guided lessons, in list order
assets/learn/training/NN-slug.toml   practice games, in list order
```

`slug` is the page or lesson id used in links and on the command line.

## Manual pages

A chapter is a Markdown file in a small subset:

- `#` gives the chapter title (the first line); `##` and `###` give sections, which get
  anchors from their text (lower case, spaces as `-`).
- Paragraphs, `-` bullet lists (one level of nesting), and `1.` numbered lists.
- `**bold**`, `*italic*`, and `` `F1` `` for keys and on-screen labels.
- Pipe tables, and `> ` for a tip box.
- Links:
  - `[text](slug)` or `[text](slug#anchor)` to another manual page;
  - `[text](window:research)` opens a game window (only during a game);
  - `[text](help:components)` opens a tab of the Help window (F1).

An optional front-matter block lists the windows the page explains, for Shift+F1:

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
manual = "colonies#your-homeworld"

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
when = { turn = 10 }
text = """..."""

[fail]                      # optional: the training game is lost when this holds
when = { colonies = 0 }
```

### Conditions

A condition is a table with one key, or `all = [...]` / `any = [...]` / `not = {...}`
over conditions. Numbers mean "at least". The set is fixed in code; an unknown key is a
load error that names the file and line.

| Key | Holds when |
|---|---|
| `window = "<id>"` | that window is open |
| `selected = "planet" \| "ship" \| "fleet" \| "system"` | the player has that kind of thing selected |
| `command = "<name>"` | the player gave that kind of command since the step began |
| `turn = N` | the game has reached turn N (the first turn is 0) |
| `turns_passed = N` | N turns have ended since the step (or the game) began |
| `colonies`, `ships`, `bases`, `fleets`, `designs` `= N` | the player's empire has N of them |
| `research_queued = N`, `construction_queued = N` | so many items wait in the queues |
| `techs_researched = N` | N tech levels researched since the start |
| `systems_explored = N`, `empires_met = N`, `treaties = N` | as named |
| `enemy_ships_destroyed = N` | the player's empire has destroyed N enemy vehicles |
| `score = N` | the player's score |

The reference section at the end lists the exact keys and command names once the code
exists.

## UI tags

Windows and widgets that lessons point at register a tag each frame with their screen
rectangle. A step's `highlight` outlines the tagged rectangle. Tags are:

- `window:<id>` for each window (ids as in `window:` links);
- `command:<id>` for the main window's command buttons, `order:<id>` for the order
  strip, `button:end-turn`, `status:<item>` for the status bar;
- `panel:system`, `panel:report`, `panel:galaxy`;
- a few widgets inside windows that lessons need (listed in the reference).

## The lesson panel

A movable panel shows the lesson's title, the step (N of M) or the objectives with
check marks, and the text. It has Back and Next (Next waits for the step's condition),
**Read more** (the manual link), and **Leave lesson**. Ctrl+H or the status bar's **T**
button re-opens it after it was closed. Finishing a lesson or training game shows the
result, and the Learn screen marks it done. Progress is stored with the client
settings, never in the game.

The panel only reads the game. Lessons change the game only through the player's own
commands, so the engine stays as it is (CLAUDE.md, determinism).

## Command line

```sh
opense4 --tutorial=<slug>        # start a lesson
opense4 --training=<slug>        # start a training game
opense4 --manual[=<slug>]        # open the manual
```

They combine with `--screenshot` for headless checks.

## Tests

A test loads every built-in lesson, training game and manual page and checks:

- every condition key and command name is known;
- every manual link points at an existing page and anchor;
- every window id and UI tag exists.

Unit tests cover the Markdown parser and each condition against the engine fixture.

## Reference

Filled in from the code: the setup keys, command names, window ids and UI tags.
