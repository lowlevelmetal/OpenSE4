---
windows: cargo-transfer, repair-priorities, scrap
---
# Supply, cargo and repair

Ships need supplies to move and fight, carry people and units in their holds, and wear out in
battle. This chapter explains how to keep them supplied, how to move cargo, and how to repair,
refit, store and scrap them.

## Supply

Ships use **supplies** for moving, firing and cloaking:

- each step and each warp costs the supply use of the ship's engines;
- each shot costs the weapon's supply use;
- a cloaked ship pays its cloaking device's supply use every turn.

A ship with **no supplies** can move only one sector a turn, cannot fire and has no shields. In a
simultaneous game it cannot move at all. The **Supplies** column of the `Ships \ Units` window
shows how much each ship has left.

A ship is refilled completely, for free, whenever it is in a sector with a **resupply depot**: a
colony of yours with the resupply facility, or of an empire with which you have a Military
Alliance or better. Passing through is enough. The **Resupply** order (`S`) sends a ship to the
nearest depot.

Ships in a fleet share their supplies at the end of each turn. Bases never run out of supply, and
some components produce supply from the system's stars or give unlimited supply.

> Put resupply depots on colonies along your frontier. A fleet far from a depot is a fleet on borrowed time.

## Cargo

Ships with cargo space, bases and colonies can carry **cargo**: population and units. One million
people take 5 kT with the stock settings; a unit takes its hull's size. Cargo can only move
between your own ships, bases and colonies in the same sector. You can never take a colony's last
1M of people.

[Cargo Transfer](window:cargo-transfer) (`T`) moves cargo at once, in both turn styles. Pick a holder on each side,
choose how much to move with `Move One`, `Move Five`, `Move Ten` or `Move All`, then click an item
to move it across. `Load Cargo Order` and `Drop Cargo Order` instead give the ship an order to
load or unload at a sector you pick later.

When a ship's cargo space is damaged, cargo that no longer fits is lost, people first.

## Repair

Damaged components are repaired at the end of each turn, wherever you have a repair source: a
colony with a facility that repairs ships, or a ship or base with repair components. The Help
window shows which facilities and components can repair. Each sector has a pool of repair points
from all your sources there, and each point restores one destroyed component. Allies' sources do
not help you. Repair is free. The **Repair** order (`R`) sends a ship to the nearest
repair source, preferring planets and bases over repair ships.

[Repair Priorities](window:repair-priorities) (from Empire Status) sets which component groups
are repaired first. Click groups on the left to add them to the order on the right.

## Scrapping and mothballing

The [Scrap](window:scrap) window (`G`) acts on your ships in the sector. Click ships in the list
to select them. Most actions need a **space yard** in the sector.

| Action | What it does |
|---|---|
| `Scrap` | Destroys the ships and refunds part of their cost: 30 % with the stock settings, more with a recycling facility in the sector. Cargo aboard is lost. |
| `Retrofit` | Changes the ships to another design on the same hull. |
| `Mothball` | Stores an empty ship: no upkeep, no abilities, no movement. |
| `Unmothball` | Brings a mothballed ship back into service, for a fifth of its cost (stock settings). It has no supplies until it reaches a depot, unless it is a base. |
| `Self-Destruct` | Destroys the ship when the turn is processed, if it has a self-destruct device. |

`Analyze` and `Fire On` are shown but not available yet in OpenSE4.

The same window scraps **facilities**: open it on a colony with the Scrap Facilities order, or use
its `Facilities` tab when one of your colonies is in the sector. Click facilities to select them,
check the refund, and press `Scrap Facilities`. Scrapping asks you to confirm.

**Retrofitting** needs a space yard in the sector, the same hull, an empty cargo hold and enough
resources. You pay more than the price of each component added and a little for each one removed;
the hull is free. The new design may cost at most half as much again as the old one (stock
settings), and you cannot add a space yard or a colony module. Every added component starts
**destroyed** and must be repaired before it works.

> Mothball the ships you will need later instead of scrapping them. A mothballed fleet costs nothing and comes back for a fifth of its price.
