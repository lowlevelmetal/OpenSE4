---
windows: main, help
---
# The main window

You spend most of the game in the main window. It shows one star system in detail, the whole
quadrant in miniature, and everything you need to give orders. This chapter takes you around
it.

## The layout

| Area | Where | What it shows |
|---|---|---|
| Status bar | Top | Your empire, your leader, the game date and your stored minerals, organics and radioactives. |
| Command panel | Below the status bar | The command buttons, the order buttons and the selection buttons. |
| System panel | Large area on the left | One star system, sector by sector. |
| Report panel | Top right | A report on what you selected, or a list of everything in the selected sector. |
| Galaxy panel | Bottom right | The whole quadrant (see [The galaxy](galaxy#the-galaxy-panel-and-the-galaxy-map)). |

The **game date** starts at 2400.0 and each turn adds 0.1, so ten turns make a year. The status
bar shows `Waiting...` while a network game waits for the other players.

## Command buttons

The twelve square buttons on the left of the command panel open the main windows. Each has a
function key.

| Button | Key | Window |
|---|---|---|
| Game Menu | `F2` | Save, load, options and quit ([Getting started](getting-started#saving-and-loading)). |
| Designs | `F3` | Your ship and unit designs ([Ship design](ship-design)). |
| Planets | `F4` | Every planet you have seen ([Planets and colonies](planets-and-colonies#colonizing)). |
| Colonies | `F5` | Your colonies ([Planets and colonies](planets-and-colonies#the-colonies-window)). |
| `Ships\Units` | `F6` | Your ships, unit groups and fleets ([Ships and fleets](ships-and-fleets#the-ships-and-units-window)). |
| Construction Queues | `F7` | Everything being built ([Construction](construction)). |
| Research | `F8` | Research projects ([Research](research)). |
| Empires | `F9` | Diplomacy, intelligence and scores ([Diplomacy](diplomacy)). |
| Log | `F10` | This turn's news ([Turns](turns#the-log)). |
| Empire Status | `F11` | Your budget and empire settings ([Economy](economy#the-empire-status-window)). |
| Help | `F1` | The encyclopedia (below). |
| End Turn | `F12` | Ends your turn ([Turns](turns)). |

## Order buttons

The strip of small buttons in the middle of the command panel holds the **orders** for what you
have selected. Buttons that do not apply are dimmed. Point at a button to see its name and its
key at the top of the system panel. Most orders have a key, listed in [Hotkeys](hotkeys). To give
one order to several ships, hold `Shift` and click them in the list of a sector (`Shift+A` tags
them all); the next order goes to every tagged ship. In a turn-based game the tagged ships carry
it out at once, together: they move while every one of them has movement left, and fight as one
group. What is left for later turns each ship then carries out on its own.

With one of your **ships** selected you can, for example, move it, warp, attack, colonize,
explore, resupply, repair, set a patrol, change its fleet, transfer cargo, launch units, cloak,
open its space yard's queue, manipulate stars and planets, scrap it, view or clear its orders
and rename it. See [Ships and fleets](ships-and-fleets#orders) for every order.

With one of your **colonies** selected you can open its construction queue, transfer cargo,
scrap facilities, launch or recover units, jettison cargo, convert resources, cloak it, rename
it, put it under minister control and abandon it. See
[Planets and colonies](planets-and-colonies#colony-orders-and-cloaking).

Some orders need you to pick a place. The bottom of the system panel then shows a prompt, such
as `Move To: pick a destination`, and the sector under the pointer gets green corner marks. Click
a sector in the system panel, or click a system in the galaxy panel first to show another system
and then click a sector there. Press `Esc` to cancel.

**Colonize**, **Warp**, **Drop Cargo** and an **Attack** that chases its target need one thing in
the sector you click: a planet, a warp point, one of your own colonies or ships, or a target. When
the sector holds several (a planet and its moons, say), a small window lists them: click the one
you mean, or `Cancel`. Nothing is checked yet: a colony ship sent to a planet it cannot settle
finds out when it arrives, and its orders are then cleared.

**Mods' orders.** Mods can add orders of their own (see [Mods](settings#mods)). The strip then
uses its one empty place, the last column but two at the top: it lights up as `Mod Orders`
whenever your selection, or your empire itself, has such orders (in the 800 by 600 layout it is
on the strip's last page). It opens a menu of them, each with the mod's picture and a
description under the pointer. An order may ask questions first, one after another: a number, a
choice from a list, some text, or a sector to click on the map. The order then works at once,
like a cargo transfer; a refused one says why at the bottom of the system panel. Mods can also
give their orders a key ([Settings](settings#controls)).

In a turn-based game your orders are carried out as you give them, and the system panel follows
your ships: when one jumps through a warp point, the panel shows the system it arrived in, with
the warp point it came out of marked, and its report stays open. At the start of your turn each
of your ships with orders left acts in turn, and the turn opens on the last one.

## Selecting things

The **system panel** shows the selected system as a grid of 13 by 13 sectors. Stars, planets,
asteroid fields, storms, warp points and ships appear at their sectors. The system's name is at
the top left, followed by the coordinates of the sector under the pointer.

- **Left-click** a sector. If it holds one thing, the report panel shows its report. If it holds several, the panel lists them; click one in the list to see its report, and the up-arrow button at the report's top right to go back.
- **Left-click empty space** to read about the whole system: its type and its special features. The sector stays selected, so that you can set a waypoint there, but it is not marked.
- **Right-click** a sector while one of your ships is selected, and the ship moves there (an OpenSE4 shortcut; you can turn it off in [Settings](settings#controls)). Otherwise a right-click in the system panel does nothing.
- Showing another system, with a click on the galaxy panel or the Galaxy Map's `Goto System`, changes nothing else: the report, its tab and the order buttons stay, so you can give the selected ship an order with a target in that system.
- The selected sector has yellow corner marks while it holds something you can see; the coordinate line then also gives the range from it to the sector under the pointer.

What else you see in the system panel:

- A small box in the owner's colour at the top right of each colony, with up to three bars for its population. Only the planet a sector shows carries it: a colony on a moon beside a larger planet has no box.
- For a single stack of ships, one ship picture with the number of ships at its bottom right. When several empires' ships share a sector, or ships sit beside a planet, small flags with counts take its place.
- A ring around a cloaked ship, in its empire's colour.
- A small green or red star on planets you could colonize (see [Planets and colonies](planets-and-colonies#colonizing)), again only on the planet a sector shows, and never together with a colony box.
- The name of the system a warp point leads to, once you have explored it.
- Your waypoints, as a cyan frame around the sector with the waypoint's number, and sectors you tagged as minefields, with a cyan `M`.
- With the matching option on, the route of your selected ship as a dashed line (`Ctrl+L`).

[Empire Options](settings#empire-options) can add a grid, planet names and letters marking the
facilities of your colonies, and remove the warp point names, the stars and the coordinates. An
unexplored system shows only a star field and the word *Unexplored*.

The **selection buttons** at the top right of the command panel step through your ships, fleets
and colonies: the left arrow goes back, the right arrow goes forward. The keyboard does the same
(see [Hotkeys](hotkeys#selecting)). `Space` jumps to the next ship without orders; in a
turn-based game, to the next ship with movement left.

## The report panel

A report has tabs along its bottom:

- **Ships**: `Detail` (class, size, movement, damage, supplies, experience, fleet, orders), `Comps` (its components, with destroyed ones in red), `Cargo` and `Ability`.
- **Planets**: `Detail` (type, atmosphere, conditions, value and, for your colonies, population, growth, mood, production and construction), `Facil` (its facilities), `Cargo` and `Ability`.

A report always opens on `Detail`, also when you click the same object again. Right-click a
facility on `Facil`, or a component on `Comps`, to read its report. `Ability` lists one line per
special ability: a planet's own, then for your colonies what your race, its culture, the
population and the mood add; a ship's hull's, then what your race and culture add. A facility's
or component's abilities are on its own report.

Each of your fleets is **one row** in the list of a sector, with the fleet's picture and name.
It opens the **fleet's report** in place of a ship's: the fleet's speed, supply pool,
experience, formation, strategy and members, and no tabs. A ship of the fleet selected any other
way shows it too. In its list of members, **click** a ship to make it the fleet's leader (the
leader matters for the formation), or **right-click** it to read its own report in a window.

You see less about other empires' ships and planets: never their cargo, and their components
only if your scanners reach them (see [The galaxy](galaxy#scanning-enemy-ships)).

When a mod adds **panels** about what a report shows, a small `MOD` button appears at the
report's top right. It switches the report to the mods' panels: labelled values, which the mods
work out from what your empire knows, and buttons for their orders. Press it again, or a tab, for
the report's own page. The choice stays while you select other things. A panel that fails shows a
small red box with the reason in its place; the game goes on.

In the list of a sector, your own ships and colonies carry small **status icons**: for example
low or no supplies, damaged, cloaked, mothballed, on sentry, repeating orders, under minister
control, building something, with a space yard, able to repair, or carrying troops, fighters,
mines, satellites, platforms, drones or people. A colony whose system delivers nothing to your
empire for lack of a spaceport carries an icon too. Hold `Shift` and click ships in the list to
tag them: the list then says how many are tagged, and the next order goes to all of them.

## Windows

The game's windows open over the main window. Each has its buttons in a column on the right,
with `Close` at the bottom; `Esc` closes it too, and so does `Enter` when the bottom button is
`Close`. In most lists, **left-click** acts (selects,
adds, goes to) and **right-click** shows a report about the item. Click a column heading to sort
a list.

## The Help window

The [Help](window:help) window (`F1`) is the game's encyclopedia. It covers only what your
empire knows. Each tab lists names in alphabetical order on the left (a green lamp marks the one
shown); the box on the right shows the chosen item: its picture, name and description, its
figures (cost, size and so on) and its abilities. Its tabs:

- [Components](help:components), [Weap Mount](help:weap-mount), [Facilities](help:facilities), [Ship Sizes](help:ship-sizes) and [Unit Sizes](help:unit-sizes): every part, weapon mount, building and hull;
- [Tech Areas](help:tech-areas), [Treaties](help:treaties) and [Intel Projects](help:intel-projects): what each one is;
- [Formations](help:formations): the shape of each fleet formation;
- [Hotkeys](help:hotkeys): the keys of each group (all windows, the main window's commands, orders and selection, tactical combat), the main window's as they are bound now, including any you changed in [Settings](settings#controls) (see also [Hotkeys](hotkeys)).

The `Find` box beside **Items** filters the list. The [Weapons Report](help:weapons) compares
weapons: their size, reload time and damage at each range, with filters by weapon type and a
choice of weapon mount. `Manual` opens this manual. Right-click an item for its full report.

`Shift+F1` opens this manual at the page about the window in front.

> Before you design a warship, open the Weapons Report and compare damage at short and long range. The chance to hit falls with every square of distance, so damage at long range is worth less than it looks.
