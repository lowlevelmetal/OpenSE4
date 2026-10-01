---
windows: ships, fleet-transfer, view-orders, select-waypoint, rename, waypoints
---
# Ships and fleets

Ships explore, colonize, carry cargo and fight. This chapter explains how to give them orders,
how they move, how to group them into fleets and how to send them to waypoints. Keeping them
supplied, loaded and repaired is explained in [Supply, cargo and repair](logistics).

## Orders

Every ship (and every fleet) has a **list of orders**. Select a ship, then click an order button
or press its key.

- New orders are **added to the end** of the list. If a ship seems to ignore you, it is probably still busy with older orders: press `V` to see them, or `Del` to clear them first.
- The ship carries out its orders from the top. An order that is done is removed, and the next one starts at once.
- An order that **fails** (for example, a destination that cannot be reached) clears the whole list.
- With **Repeat Orders** (`K`) on, finished orders go back to the end of the list instead, so the ship repeats its list forever.

| Order | Key | What it does |
|---|---|---|
| Move To | `M` | Go to a sector, in this system or another explored one. Right-clicking a sector does the same. |
| Move To Waypoint | `Ctrl+0` to `Ctrl+9` | Go to one of your waypoints. |
| Warp | `W` | Jump through a warp point, even into an unexplored system. |
| Attack | `A` | Go after an enemy ship or colony and fight it. |
| Colonize | `C` | Settle a planet ([Planets and colonies](planets-and-colonies#colonizing)). |
| Explore | `E` | Go through the nearest warp point that leads somewhere new. |
| Resupply | `S` | Go to the nearest resupply depot ([Supply, cargo and repair](logistics#supply)). |
| Repair | `R` | Go to the nearest place that can repair ships ([Supply, cargo and repair](logistics#repair)). |
| Sentry | `Y` | Wait until an enemy is seen in the system or supplies run low. |
| Set Patrol | `P` | Click several sectors, then press `Enter` or right-click: the ship moves between them forever. |
| Repeat Orders | `K` | Switch repeating on or off. |
| Clear Orders | `Del` | Remove every order. |
| Fleet Transfer | `F` | Form fleets (below). |
| Load Cargo, Drop Cargo | `L`, `D` | Load or unload population at a sector you pick. |
| Cargo Transfer | `T` | Move cargo now ([Supply, cargo and repair](logistics#cargo)). |
| Launch and Recover Units | `U` | Launch or pick up fighters, satellites, mines and drones ([Units](units)). |
| Cloak, Decloak | `Z`, `X` | Hide the ship, or show it again (below). |
| Sweep Mines | button | Clear mines in this sector ([Units](units#mines)). |
| Build Queue | `Q` | The ship's own construction queue, if it has a space yard. |
| Stellar Manipulation | `B` | Create or destroy planets, stars and more ([Events and stellar manipulation](events-and-stellar-manipulation#stellar-manipulation)). |
| Scrap | `G` | Scrap, mothball, retrofit or self-destruct ([Supply, cargo and repair](logistics#scrapping-and-mothballing)). |
| View Orders | `V` | See and edit the order list. |
| Change Name | `N` | Rename the ship. |
| Minister Control | button | Let the computer handle this ship ([Computer players and ministers](computer-players-and-ministers)). |

The **Attack** order works differently in the two turn styles. In a turn-based game the ship
goes to the target's sector and attacks it there. In a simultaneous game it pursues the target
wherever it goes, and the battle comes when they meet.

**Sentry** is useful for guards: the order ends the moment an enemy appears in the system, so
the ship's next orders start then.

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
- With the Empire Options switches on, your ships clear their orders when they warp into a system with an enemy (on for a new empire) or with any other empire. This stops ships from flying blindly into danger.

Your selected ship's route is shown as a dashed line in the system panel; `Ctrl+L` hides or
shows it.

## Fleets

A **fleet** is a group of ships that move and fight together. Ships and fighter groups can join
fleets; drones, satellites and mines cannot. Bases cannot either, with the stock settings.

- All members must be in the same sector to join. The fleet moves at the speed of its slowest member.
- The fleet has one list of orders, which every member at the fleet's location follows.
- The fleet's **formation** sets where its ships stand in combat, and its **strategy** tells the armed members how to fight (see [Combat](combat#formations)).
- A fleet earns **fleet experience** in battle, which helps every member hit. It is kept as long as the fleet exists, even when members join or leave.
- A fleet whose last member leaves is gone.

Open [Fleet Transfer](window:fleet-transfer) with `F`. It lists the ships in the sector that are
not in a fleet, and the fleets there. `Create Fleet` makes a new fleet; then **click** ships to
add them, and click members to take them out. `Formation` and `Strategy` choose the fleet's
combat settings. `Add All`, `Remove All`, `Disband Fleet` and `Rename Fleet` do what they say.

## Waypoints

**Waypoints** are ten named places, numbered 0 to 9, that you can send ships to quickly.

- `Alt+0` to `Alt+9` sets that waypoint at the selected sector.
- `Ctrl+0` to `Ctrl+9` orders the selected ship to go there. The **Move To Waypoint** button opens a list instead.
- A construction queue's **Move To** sends every new ship to a waypoint.

The [Waypoints](window:waypoints) window (from Empire Status) lists your waypoints, the ships
heading to each one and the yards that send ships there. `Set` lets you pick a new sector in the
main window, `Delete` clears a waypoint and `Rename` names it. A ship heading for a deleted
waypoint fails its order.

## Experience

A ship's crew gains experience by destroying enemies in battle: +1 for a ship, base or planet and
+0.1 for a unit group. Training facilities also train crews over time. Each point of experience
adds one point to the ship's chance to hit. The levels are Novice (up to 5), Experienced (up to
10), Veteran (up to 20), Elite (up to 30) and Legendary (above 30), up to a maximum of 50.

## Cloaking

A ship with a cloaking device can **cloak** (`Z`). While cloaked, enemies whose sensors are too
weak cannot see it (see [The galaxy](galaxy#sight-and-sensors)), so it can pass through their
sectors without a fight. Cloaking costs supply every turn, and the ship decloaks when its supplies
run out. A cloaked ship cannot colonize, build, manipulate stars or be scrapped. Every ship in a
battle is decloaked until the battle ends.

## The Ships and Units window

The [Ships and Units](window:ships) window (`F6`, titled `Ships \ Units`) lists your ships, unit
groups in space and fleets, with your total maintenance per turn. Its tabs show different
columns:

- **General**: size, type, movement, damage and supplies;
- **Orders**: each ship's first order;
- **Cargo**: space used and what is carried;
- **Fleet**: experience and fleet;
- **Maintenance**: the cost of each ship per turn.

`Show Ships`, `Show Units` and `Show Fleets` choose what to list. Click a column heading to sort.
**Left-click** a row to select it in the main window; **right-click** it for its report.
