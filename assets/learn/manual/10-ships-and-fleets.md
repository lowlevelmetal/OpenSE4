---
windows: ships, fleet-transfer, view-orders, select-waypoint, rename, waypoints
---
# Ships and fleets

Ships explore, colonize, carry cargo and fight. This chapter explains how to give them orders,
how they move, how to group them into fleets and how to send them to waypoints. Keeping them
supplied, loaded and repaired is explained in [Supply, cargo and repair](logistics).

## Orders

Every ship (and every fleet) has a **list of orders**. Select a ship, then click an order button
or press its key. New ships and bases are named after their design and a four-digit number, such
as *Escort 0003*; `N` renames them.

- New orders are **added to the end** of the list. If a ship seems to ignore you, it is probably still busy with older orders: press `V` to see them, or `Del` to clear them first.
- The ship carries out its orders from the top. An order that is done is removed, and the next one starts at once.
- An order that **fails** (for example, a destination that cannot be reached) clears the whole list.
- With **Repeat Orders** (`K`) on, finished orders go back to the end of the list instead, so the ship repeats its list forever. Switching Repeat on or off starts the list again from its first order.

| Order | Key | What it does |
|---|---|---|
| Move To | `M` | Go to a sector, in this system or another explored one. Right-clicking a sector does the same. |
| Move To Waypoint | `Ctrl+W`, or `Ctrl+1` to `Ctrl+9` and `Ctrl+0` | Go to one of your waypoints: pick it from a list, or name it by its number (`Ctrl+0` is waypoint 10). |
| Warp | `W` | Jump through a warp point, even into an unexplored system. |
| Attack | `A` | Go after an enemy ship or colony and fight it. |
| Colonize | `C` | Settle a planet ([Planets and colonies](planets-and-colonies#colonizing)). |
| Explore | `E` | Go through the nearest warp point that leads somewhere new. |
| Resupply At Nearest | `S` | Go to the nearest resupply depot ([Supply, cargo and repair](logistics#supply)). |
| Repair At Nearest | `R` | Go to the nearest place that can repair ships ([Supply, cargo and repair](logistics#repair)). |
| Sentry | `Y` | Wait until an enemy is seen in the system or supplies run low. Pressed again while the list starts with a Sentry, it clears the list. |
| Set Patrol | `P` | Click the sectors of the route one by one: each adds a Move To to the ship's orders. Click the ship's own sector to end the route, which adds that move too and switches Repeat on, so the ship patrols forever (a route of one order is cleared instead). `Enter` or a right-click also ends it. |
| Repeat Orders | `K` | Switch repeating on or off. |
| Clear Orders | `Del` | Remove every order. |
| Fleet Transfer | `F` | Form fleets (below). |
| Change Formation \ Strategy | `H` | Choose a fleet's formation and strategy. |
| Load Cargo, Drop Cargo | `L`, `D` | Pick what (population or a kind of unit), then the sector where the ship loads or unloads it. |
| Cargo Transfer | `T` | Move cargo now ([Supply, cargo and repair](logistics#cargo)). |
| Launch and Recover Units | `U` | Launch or pick up fighters, satellites, mines and drones now, in turn-based games ([Units](units)). |
| Launch Units Remotely, Recover Units Remotely | `I`, `O` | Pick a kind of unit, then the sector where the ship launches or recovers it. |
| Cloak, Decloak | `Z`, `X` | Hide the ship, or show it again, at once (below). |
| Sweep Mines | `Ctrl+M` | Clear mines in this sector ([Units](units#mines)). |
| Use Component | `Ctrl+Z` | Use a component that works on demand, such as an emergency supply or energy component. |
| Build Queue | `Q` | The ship's own construction queue, if it has a space yard. |
| Stellar Manipulation | `B` | Create or destroy planets, stars and more ([Events and stellar manipulation](events-and-stellar-manipulation#stellar-manipulation)). |
| Scrap | `G` | Scrap, mothball, retrofit or self-destruct ([Supply, cargo and repair](logistics#scrapping-and-mothballing)). |
| View Orders | `V` | See and edit the order list. |
| Change Name | `N` | Rename the ship. No two objects in space can share a name: a name some ship, unit group, planet, star or warp point already has is refused, and you are asked again. |
| Toggle Minister Control | `Ctrl+Y` | Let the computer handle this ship, or a whole fleet ([Computer players and ministers](computer-players-and-ministers)). |

The **Attack** order works differently in the two turn styles. In a turn-based game the ship
goes to the sector the target was in when you gave the order and attacks there. In a
simultaneous game it pursues the target wherever it goes, and the battle comes when they meet.
Attacking does not decloak a ship; only drones decloak to strike their target.

**Sentry** is useful for guards: the order ends the moment an enemy appears in the system, so
the ship's next orders start then. It also ends when supplies run low: for a ship below the
low-supply warning level, for a group of fighters or drones below a tenth of it.

[View Orders](window:view-orders) (`V`) shows the order list of the selected ship, or of its
fleet. Select an order and use `Move Up`, `Move Down` or `Delete Order`; `Clear Orders` empties the
list, and the `Repeat Orders` box switches repeating. [Change Name](window:rename) (`N`) renames
the ship, and the Move To Waypoint button opens [Select Waypoint](window:select-waypoint), a list
of your waypoints.

## Moving around

A ship's **movement points** (its speed) come from its engines. Each step to a neighbouring
sector, diagonals included, costs one point, and so does each warp jump. Points left at the end
of a turn are lost; the ship gets its full speed again next turn.

- Routes between systems go through the warp points you know, and only through systems you have explored.
- Ships step around storms that damage ships and around sectors with visible enemies, when they can.
- Flying into a damaging storm has an even chance of hurting every ship that enters. If it does, the ships stop for the turn and their orders are cleared. The same goes for a turbulent warp point, and for a minefield.
- With the Empire Options switches on, your ships clear their orders when they warp into a system with an enemy (on for a new empire) or with any other empire. This stops ships from flying blindly into danger. Every ship that made the jump together loses its orders. In a turn-based game a ship that was on a Move To still uses up the rest of its movement for this turn heading on toward its destination, but the order is gone afterwards.

`Ctrl+L` (or *Display Ship Movement Lines* in the [Options](settings#the-options-window) window)
shows the route of your selected ship as a dashed line in the system panel.

## Fleets

A **fleet** is a group of ships that move and fight together. Ships and fighter groups can join
fleets; drones, satellites and mines cannot. Bases cannot either, with the stock settings.

- All members must be in the same sector to join. The fleet moves at the speed of its slowest member.
- Orders given to the fleet, or to any of its ships, go to every member at the fleet's location, and they carry them out together. Each member keeps its own copy, so a ship that joins gets only the orders given after it joined; joining or leaving a fleet clears the ship's own orders.
- The fleet's **formation** sets where its ships stand in combat, and its **strategy** tells the armed members how to fight (see [Combat](combat#formations)).
- A fleet earns **fleet experience** in battle, which helps every member hit. It is kept as long as the fleet exists, even when members join or leave.
- A fleet is gone once no member is left where the fleet is; any members elsewhere leave it and lose their orders.

Open [Fleet Transfer](window:fleet-transfer) with `F`. It lists the ships in the sector that are
not in a fleet and can join one (**Vehicles in sector**: ships, fighter groups, and bases when the
settings let bases join fleets) and the fleets whose place is this sector with their members here
(**Fleets in sector**, the leader marked). `Create Fleet` asks for a name, offering Alpha Fleet,
Beta Fleet and so on, and makes an empty fleet; then **click** ships to add them, and click
members to take them out. A fleet still empty when the window closes is deleted. `Formation` and `Strategy` choose the fleet's
combat settings. `Add All`, `Remove All`, `Disband Fleet` and `Rename Fleet` do what they say, and
`Existing Fleets` opens the Ships and Units window, with the tab and the Show switches you left it with, to look at your fleets; clicking a row there does nothing.

A ship can also leave its fleet without you: when it is captured or given away, or when an event
or an enemy agent sends it somewhere else.

## Waypoints

**Waypoints** are ten named places, numbered 1 to 10, that you can send ships to quickly.

- `Alt+1` to `Alt+9` and `Alt+0` (waypoint 10) set that waypoint: press the key, then click a sector of the system view (an empty one will do, in any system shown, explored or not). Nothing needs to be selected. The waypoint keeps its name, or is called *Waypoint 3* and so on.
- `Ctrl+1` to `Ctrl+9` and `Ctrl+0` order the selected ship to go there. The **Move To Waypoint** button (`Ctrl+W`) opens a list instead.
- A construction queue's **Move To** sends every new ship to a waypoint.

The [Waypoints](window:waypoints) window (from Empire Status) lists your waypoints, the ships
heading to each one and the yards that send ships there. `Set` lets you pick a new sector in the
main window, `Delete` clears a waypoint and `Rename` names it; right-click a ship heading there
for its report. Deleting a waypoint also stops every queue sending ships there, and a ship
heading for a deleted waypoint fails its order.

## Experience

A ship's crew gains experience by destroying enemies in battle: +1 for a ship, base or planet and
+0.1 for a unit group. Training facilities also train crews over time. Each point of experience
adds one point to the ship's chance to hit. The levels are Novice (up to 5), Experienced (up to
10), Veteran (up to 20), Elite (up to 30) and Legendary (above 30), up to a maximum of 50.

## Cloaking

A ship with a cloaking device can **cloak** (`Z`), at once: it is not an order in its list, and
`X` decloaks it the same way, even when it could no longer cloak. Cloak and Decloak work on a
fleet's ships at its location and on tagged ships too. While cloaked, enemies whose sensors are too
weak cannot see it (see [The galaxy](galaxy#sight-and-sensors)), so it can pass through their
sectors without a fight. Cloaking costs supply every turn, and the ship decloaks when its supplies
run out. A cloaked ship cannot colonize, build, manipulate stars or be scrapped. Every ship in a
battle is decloaked until the battle ends.

## The Ships and Units window

The [Ships and Units](window:ships) window (`F6`, titled `Ships\Units`) lists your ships, unit
groups in space and fleets. The totals at the top count your ships, units in space and fleets,
and your maintenance per turn. Its tabs show different columns:

- **General**: size, type (for a unit group, each design and its number of units), movement, destroyed components and supplies;
- **Orders**: the class and the ship's orders, one a line;
- **Cargo**: space used, the capacity and what is carried, one entry a line;
- **Fleet**: experience and fleet;
- **Maintenance**: the cost of each ship per turn.

The check boxes `Show Ships`, `Show Units` and `Show Fleets`, just above `Close`, choose what to
list; fleets always come last. Click a
column heading, the picture's too, to sort by it. The picture and Size sort by hull size, with
unit groups after every ship; supplies and experience sort lowest first, the fleet column by
fleet, and other numbers highest first. A sort stays when you change tab.
A fleet's row shows the number of its ships at the fleet's location as its size and its
formation as its type. The maintenance total is the sum over your ships and units as they are now.
**Left-click** a row to close the window and select the ship in the main window (for a fleet, its first ship at the fleet's location); **right-click** it for its report.
