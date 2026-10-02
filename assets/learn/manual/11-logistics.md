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

[Cargo Transfer](window:cargo-transfer) (`T`, the window titled *Transfer Cargo*) moves cargo at
once, in both turn styles. Click a holder in **Cargo From** and one in **Cargo To**: each lists its
cargo below it. Choose how much to move with `Move One`, `Move Five`, `Move Ten` (the default),
`Move Hundred` or `Move All`, then click an item to move it across. To have a ship (or its fleet)
load or unload population or a kind of unit later, at a sector you pick on the map, use the
**Load Cargo** and **Drop Cargo** order buttons instead.

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

The [Scrap](window:scrap) window (`G`) acts on your ships and unit groups in the sector that are
in no fleet and not cloaked. Click them in the list to select or unselect them; right-click one
for its report. The statistics show the scrap value, the research potential, the cost to
unmothball, and whether the selection can self-destruct or be fired on. A button lights only when
**every** selected vehicle can do it. Scrap, Analyze, Retrofit and Mothball need a working
**space yard** in the sector: one of your colonies or ships with a yard, not cloaked.

| Action | What it does |
|---|---|
| `Scrap` | Destroys the vehicles and refunds part of their cost: 30 % with the stock settings, more with a recycling facility in the sector. Cargo aboard is lost. Drone groups and minefields cannot be scrapped. It asks first. |
| `Analyze` | Takes a ship or base apart to learn from it. Nothing is refunded. For every different technology level its intact components and its hull need that your empire has not reached, you gain one level in that field, never past the highest level it needs. It asks first. |
| `Retrofit` | Changes the ships, all of one design, to another design on the same hull: pick it from a list that shows the cost of each. |
| `Mothball` | Stores a ship with an empty hold: no upkeep, no abilities, no movement. |
| `Unmothball` | Brings a mothballed ship back into service, for a fifth of its cost (stock settings); no space yard is needed. It has no supplies until it reaches a depot, unless it is a base. |
| `Self-Destruct` | Destroys the vehicle, if it has a self-destruct device; satellites, mines and drones always can, fighters never (no space yard needed). It asks first. |
| `Fire On` | Your own armed ships in the sector shoot the vehicle down: it is simply removed, with no battle. Another armed ship of yours must be there, so the last armed ship in a sector cannot be fired on. It asks first. |

The **research potential** says how much the last selected vehicle could teach: None, Minor,
Moderate, Sizable or Major. Your own designs teach nothing; a captured ship can teach a lot.

When the actions happen depends on the turn style:

- In a **turn-based** game each selected vehicle's action happens at once, one after another, and
  its other orders stay. Each is checked again just before it happens, so when you fire on several
  armed ships at once, the last of them finds nobody left to fire and stays.
- In a **simultaneous** game nothing happens yet. Each vehicle's orders are replaced by the action
  (its order list shows it), and it is carried out when the vehicle first acts in the turn. If it
  can no longer be done then, the order fails and the vehicle's orders are cleared.

The same window scraps **facilities**: open it on a colony with the Scrap Facilities order, or use
its `Facilities` tab when one of your colonies is in the sector. Click facilities to select them,
check the refund, and press `Scrap Facilities` (`Select All` and `Select None` help). Scrapping
asks you to confirm, while the Empire Option *Confirm scrapping* is on.

**Retrofitting** needs a space yard in the sector, the same hull, an empty cargo hold and enough
resources. You pay more than the price of each component added and a little for each one removed;
the hull is free. The new design may cost at most half as much again as the old one (stock
settings), and you cannot add a space yard or a colony module. Every added component starts
**destroyed** and must be repaired before it works.

> Mothball the ships you will need later instead of scrapping them. A mothballed fleet costs nothing and comes back for a fifth of its price.
