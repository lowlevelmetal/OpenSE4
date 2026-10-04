---
windows: queues, set-queue
---
# Construction

Everything you build comes out of a **construction queue**: facilities on your planets, ships
and bases, and units such as fighters and mines. This chapter explains who can build what, how
fast, and how to manage your queues.

## Who can build

- Every colony with people has a queue. It can build **facilities** and **units**. An empty colony (no population) can build nothing.
- Building **ships and bases** needs a **space yard**. A colony gets one by building the space yard facility; a planet can have only one. A ship or base can also carry a space yard component, which gives it a queue of its own.
- A ship whose space yard has items queued cannot move or warp.
- A cloaked ship cannot build, and cloaking clears its queue.

## How a queue builds

Each queue has a **rate** for each of minerals, organics and radioactives: the most it can spend
per turn.

- A colony **without** a space yard builds at a base rate (2,000 of each resource per turn with the stock settings), raised by its population bonus.
- A colony **with** a space yard builds at the yard's rate, raised by its population bonus and by your race's construction aptitude, culture and traits.
- A ship's yard builds at its component's rate, raised by your race's construction aptitude, culture and traits.
- A rioting colony builds nothing.

For example, a homeworld with a space yard and 2,000M people builds at 2,000 × 130 % = 2,600 per
resource per turn.

Every turn, **only the top item** of each queue makes progress:

- The queue needs up to its rate of each resource to finish the item, and pays only if your treasury can cover **all three** at once. Then the item's progress grows by the full rate.
- If the treasury cannot cover all three, the queue pays nothing, makes no progress, and the Log reports a lack of resources.
- When the item's progress covers its cost, it is built. A queue finishes at most **one item per turn**, and any rate left over is lost; it never flows on to the next item.

So an item that costs 6,000 minerals takes three turns in a queue with a rate of 2,000. The
windows show these estimates for you.

> Many small items in one queue waste rate, because each takes at least a whole turn. Group units into one item with a larger count, and spread facilities across several colonies' queues.

Queues whose top item is a spaceport, a resource producer or a resupply depot are served first
each turn, so they get your resources before the others.

## What gets built

- **Ships and bases** appear at the queue's location. They cannot be seen until they are finished. If you have reached the game's limit of ships per empire, nothing is built, and the item stays at the top to be paid for again.
- **Facilities** fill the planet's slots. You can queue only as many as there are free slots. If the planet has no free slot when the item finishes, nothing is built and its progress is lost.
- **Units** go into the builder's cargo if they fit, otherwise into the cargo of another of your planets or ships in the same sector. A unit with no room anywhere there is not built, and the Log says so.

An item can have a **count**: it then costs that many times as much and finishes all at once.

## Build modes

Each queue has three switches:

- **Emergency Build** raises the rate to 150 % for about ten turns (stock settings), or until you switch it off. Afterwards the yard is tired: it runs at 25 % for as many turns as the emergency lasted. You cannot start a new emergency until that slow period is over, and clearing the queue does not end it. Switching an emergency off before a turn has passed costs nothing.
- **Repeat Build** builds the top item again and again, for as long as it can be built.
- **Queue On Hold** freezes the queue. It spends nothing and builds nothing.

> Emergency Build is for real emergencies, such as an enemy fleet two turns away. The slow period afterwards costs you more than you gained.

A queue can also have a **Move To** waypoint: every ship it finishes is sent there automatically.

## Upgrading and scrapping facilities

When you research a better level of a facility, your existing facilities do not change. To
upgrade them, queue an **upgrade**: it converts every older facility of that kind on the planet
to the new level, for half the new facility's cost each (stock settings). The **Upgrade
Facilities** button of the Construction Queues window queues every possible upgrade at once, and
also switches facilities still waiting in your queues to the newest level.

**Scrapping** a facility frees its slot and refunds part of its cost at once: 30 % with the stock
settings, or more if you have a recycling facility in that sector. Scrapping a space yard also
removes the ships from that planet's queue.

## The Construction Queues window

[Construction Queues](window:queues) (`F7`) lists every queue you have. The statistics at the top
show your resources and queue usage per turn, how many space yards you have (on planets and on
ships) and how many queues are on hold.

- Each row shows the queue's owner with its status icons, the column of the chosen tab, and the first three items of the queue with the time the whole queue needs: in years, or `Never` when it cannot finish, or `On Hold`. A yellow note shows an emergency or a slow period.
- The tabs choose that column: **Rate** and **Usage Per Turn** (each amount with its resource's icon), **Planet Value**, **Number of Facilities** (built and slots) and **Cargo Space**. Click a column heading to sort.
- The check boxes **Ships**, **Planets**, **Ship SY** and **Planet SY** choose which queues to list: ships and bases whose space yard is not working now (cloaked or mothballed), colonies without a working yard, ships and bases with a working yard, and colonies with one.
- **Click** a row to open that queue. **Shift+click** tags a queue for Multi-Add. **Right-click** shows the planet's or ship's report.

The window remembers the tab and the check boxes.

| Button | What it does |
|---|---|
| `Multi-Add` | Opens a queue editor whose items go to every tagged queue when you close it. Only ships (when every tagged queue can build them) and units can be added this way. |
| `Scrap Facilities` | Pick a colony, then tick the facilities to scrap there. |
| `Upgrade Facilities` | Queue every possible facility upgrade on all your colonies, and move facilities waiting in your queues to the newest level. |

## The Set Construction Queue window

[Set Construction Queue](window:set-queue) opens when you click a queue, or with the order `Q`
for a selected colony or ship.

- The top shows the queue's owner, its rate, your stockpile, its facility slots (and how many are free after the queue) and population, its cargo space, its mode and where new ships go.
- The list on the left shows what this queue can build, with the build time in turns. The tabs are **Ships**, **Facilities**, **Units** and **Upgrades** (ships and bases have only the first and third). **Only Latest** hides old facility levels (of facilities of one family listed one after another, only the last shows) and obsolete designs; it is the Empire Options' setting of the same name, so it stays as you leave it. On the Units tab, `1`, `5`, `10` and `20` choose how many to build at once. Items that cannot be built here are greyed; click one to see why. **Click** an item to add it to the end of the queue.
- The queue on the right shows each item's progress and when it will be done, in turns. `Top`, `Up`, `Down`, `Bottom` and `Remove` change it, and `-` and `+` change the count of a unit item; `Delete` removes the selected item too. Removing the first item asks first (an Empire Option), because its progress is lost.
- Hover over an item to see its cost, build time and details. **Right-click** a ship, base or unit, in either list, for its design's report (size, type, cost, maintenance, movement, shields, cargo, supplies and components); right-click a facility for the facility's report.

The buttons on the right:

| Button | What it does |
|---|---|
| `Emergency Build`, `Repeat Build`, `Queue On Hold` | Check boxes for the build modes above. Emergency Build is dim while the yard recovers from an emergency. |
| `Set Move To`, `Clear Move To` | Send new ships to one of your waypoints, or stop doing so. |
| `Fill Queue` | Add a saved list of items (a queue type). |
| `Clear Queue` | Empty the queue (it asks first). |

**Queue types** save typing. `Fill Queue` offers ready-made types that fill a planet's free
facility slots with the best producer of one kind (minerals, organics, radioactives, research,
intelligence) or a balanced mix, and the types you saved yourself. Type a name and press
`Add Type` to save the current queue as a new type, which you can use in every game; `Delete Type`
removes one of yours.

## Construction advice

- Give every colony with people something to build. An idle queue is wasted rate.
- New colonies first need a spaceport (if the system has none), then producers.
- Build ships at your best yards, and facilities everywhere else.
- Check the Log each turn for queues that ran short of resources.
