---
windows: game-menu, save-game, load-game, learn
---
# Getting started

OpenSE4 is an engine for Space Empires IV Deluxe. It plays the classic game with the data, art,
sound and music of your own copy. This chapter shows you how to start, which kind of game to
pick, and how to save and load.

## What you need

You need your own installed copy of Space Empires IV Deluxe. It is sold on Steam. OpenSE4 reads
its files where they are and never changes them.

When OpenSE4 starts, it looks for the game in every Steam library it can find. If your copy is
somewhere else, start OpenSE4 with `--classic-dir=` followed by the game folder (the folder that
holds `Data`, `Pictures` and `Sounds`, or the `Data` folder itself). Without a copy, OpenSE4
shows where it looked and closes: there is nothing to play without the original data.

> On Linux and macOS you only need the game's files, not a way to run the original. The setup guide that comes with OpenSE4 explains how to download them through Steam.

## The title screen

The title screen offers:

| Button | What it does |
|---|---|
| `Quick Start` | Pick an empire and start at once with the standard settings. |
| `New Game` | Set up every detail of a new game and the empires in it. |
| `Resume Game` | Continue the game you saved last. |
| `Load Game` | Continue a saved game. |
| `Tutorial` | Guided lessons that walk you through the game step by step (see [below](#learning-the-game)). |
| `Scenario` | Training games: practice games with objectives. |
| `Credits` | Who made OpenSE4, and what it is built with. |
| `Quit Game` | Leave OpenSE4. |

At the top right, `Multiplayer` hosts or joins a network game, or plays your turn of a game by
e-mail (see [Multiplayer](multiplayer)), `Settings` sets graphics, controls and sound (see
[Settings](settings)), and `Manual` opens this manual.

## Quick Start

Quick Start is the fastest way into a game. Each empire is shown with its portrait, name and
description. Click one to choose it, then press `Begin Game`; `Cancel` goes back.

You play against four computer empires, in a medium-sized quadrant, with turn-based play and
every other option at its default. It is a good way to learn the game.

Every empire starts with its homeworld, two scouts and a colony ship, and with ready-made designs
for a scout, a colony ship, an escort and a defense base.

> For your first game, pick an empire whose description sounds balanced rather than extreme. Racial strengths matter less than learning the flow of a turn.

## New Game

New Game opens **Game Setup**, which has eight pages. Pick a page with the buttons on the right,
then press `Begin Game`. The bottom line sums up the game you are about to start.

### Game setup

| Page | What you set there |
|---|---|
| Quadrant | The quadrant type and size, warp point options, whether every system starts explored, finite resources, and whether all homeworlds have the same size. A map preview shows the quadrant; `Generate Map Now` rolls a new one. You can also load and save maps here. |
| Events | How often random events happen, and how bad they may get (see [Events](events-and-stellar-manipulation)). |
| Technology | Your starting technology level, how expensive research is, and which technology areas exist in this game. |
| Player Settings | Starting resources, racial points for designing races, the value of homeworlds, the number of starting planets, how empires are spread out, and whose scores you can see. |
| Players | The empires in the game: add, edit and remove them, and set each one to Human or Computer. Here you also add random computer players and set the computer's difficulty and bonus. |
| Victory Conditions | What ends the game (see [Score and victory](score-and-victory)). All are off by default. |
| Game Settings | Diplomacy options (gifts, technology trades, intelligence, surrender, team mode), colonization limits, map saving and the ship and unit limits per empire. |
| Mechanics | The turn style, tactical combat or strategic combat only, and autosave. |

The most important choices for a first game:

- **Quadrant Size** sets the length of the game. Small quadrants make for short, crowded games.
- **Starting Resources** and **Starting Technology** set the pace of the opening.
- **Turn Style**: one player after another (turn-based) or simultaneous (see [Turns](turns#turn-styles)).
- **Random computer players** and **Difficulty** set how hard the game is.

### Designing your own empire

On the Players page, `Add New` opens **Empire Setup**, where you create an empire of your own:

- **General**: names, the race's look, the list of names for ship designs, human or computer player, the ministers' style, and a password.
- **Environment**: the atmosphere your race breathes and its home planet type.
- **Culture**: a set of bonuses and penalties for the whole society.
- **Characteristics**: fifteen racial characteristics, from mining and research to happiness and combat skill. Raising one above 100 % costs racial points; lowering one gives points back.
- **Advanced Traits**: special abilities, such as needing no spaceports or a racial technology area. They cost racial points.
- **Description**: the race's happiness type and its background texts.

The racial points left are shown at the top. `Create Empire` is refused while you are over
budget. `Save To File` on the Players page keeps an empire for later games, and `Add Existing`
loads one.

> A race's happiness type matters more than it looks: it decides how your people react to war, peace and losses. Read it before you choose.

## Saving and loading

Press `F2` (or the first command button) to open the [Game Menu](window:game-menu):

| Button | What it does |
|---|---|
| `New` | Leave this game and return to the title screen (it asks first). |
| `Load` | Open a saved game: click one in the list. |
| `Save Game` | Save the game. The name starts as your empire and the date; clicking an existing save copies its name, and saving over it asks first. In a play-by-e-mail game it saves your turn so far. |
| `Save Map` | Save the quadrant as a map. It is dim unless the game was set up with *Players can save the map during the game*. |
| `Save Empire` | Save your empire (its name, leader, race, ministers' style and combat strategies, and if you like its designs) for later games. Saved designs come back as fresh, current designs and replace the ones a new empire would have. |
| `Players` | Show which empires the computer plays. |
| `Options` | The [Options](window:options) of this computer: animation, sound, music and autosave (see [Settings](settings#the-options-window)). |
| `Delete Game` | Remove a saved game (it asks first). |
| `Quit` | Leave OpenSE4 (it asks first). |
| `Close` | Close the menu. |

Below the menu, `Learn` opens the tutorials, training games and this manual. Starting one replaces
the game you are playing, so the window asks first.

In these Yes/No questions, `Y` answers Yes; `N`, `Esc` and `Enter` answer No. There is no saving
in the middle of a battle. `Resume Game` on the title screen loads the game you last saved with
`Save Game` on this computer.

**Autosave** is off unless you choose it. Choose it on the Mechanics page of Game Setup, or later
under *Autosave For This Game* in the [Options](window:options) window, in local and hotseat
games. The game then saves every 1, 2, 3, 5 or 10 turns, into the files `AutoSav0` to `AutoSav9`,
named after the last digit of the turn. So you always have up to ten recent turns to go back
to.

Saved games, maps, empires and settings live in your OpenSE4 user folder, never in the game
folder:

| System | Folder |
|---|---|
| Linux | `~/.local/share/OpenSE4/` |
| Windows | `%APPDATA%\OpenSE4\` |
| macOS | `~/Library/Application Support/OpenSE4/` |

## Learning the game

The **Learn** window gathers OpenSE4's three ways to learn the game. The title screen's
`Tutorial` button opens it on its tutorials, `Scenario` on its training games and `Manual` on
this manual; during a game, the Game Menu's `Learn` button opens it.

- **Tutorials** are guided lessons. Each starts a small prepared game, and a panel at the bottom
  left of the system view tells you what to do, outlines the button it is about and moves on when
  you have done it. `Read More` opens the manual page about the step.
- **Training games** are practice games with objectives and deadlines. The panel shows the
  objectives, which light up as you meet them, the briefing pages and the hints.
- The **manual** has a contents tree, a search box and links that open the game's windows.

`Hide` puts the lesson panel away, and `Ctrl+H` or the **T** button at the top right of the status
bar brings it back. In any window, `Shift+F1` opens the manual page about it. The Learn window
marks the lessons and training games you have finished.

## Your first turns

A good plan for the first ten turns of a standard game:

1. Read the [Log](window:log) (`F10`): it opens by itself at the start of each turn when there is news.
2. Look at your homeworld: click it in the system panel and read its report.
3. Fill your research queue (`F8`) with a few useful areas (see [Research](research)).
4. Send your two scouts to explore: select each one and press `E` (see [The galaxy](galaxy#exploring)).
5. Find a good planet for your colony ship with the [Planets](window:planets) window (`F4`), and give the ship the Colonize order (`C`).
6. Queue more colony ships at your homeworld (`F7`), and keep its construction queue busy.
7. Press `F12` to end the turn.

The [Main window](main-window) chapter explains what you see on the screen, and
[Turns](turns) explains what happens when you end a turn.
