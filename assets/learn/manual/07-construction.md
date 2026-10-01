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
show how many queues are building, idle or on hold, and your total rate, usage and stockpile.

- Each row shows the queue, its system, what it is building and how long that will take. Hover over the time to see how long the whole queue will take.
- The tabs add columns: **Rate** (rate and mode), **Usage** (what it spends), **Planet Value**, **Facilities** (slots free after the queue) and **Cargo**.
- The lamps **Ships**, **Planets**, **Ship SY** and **Planet SY** choose which queues to list: mobile ships with a yard, colonies without a yard, bases with a yard and colonies with a yard.
- **Click** a row to open that queue. **Ctrl+click** or **Shift+click** selects several rows. **Right-click** shows the planet's or ship's report.

The buttons:

| Button | What it does |
|---|---|
| `Multi-Add` | Add the same ship, unit or facility to every selected queue. |
| `Scrap Facilities` | Scrap facilities on the selected colonies. |
| `Upgrade Facilities` | Queue every possible facility upgrade, on the selected colonies or on all of them. |
| `Select All` | Select every listed queue, or none. |

## The Set Construction Queue window

[Set Construction Queue](window:set-queue) opens when you click a queue, or with the order `Q`
for a selected colony or ship.

- The top shows the queue's owner, its rate, your stockpile, its facility slots and population, its cargo space, its mode and where new ships go.
- The list on the left shows what this queue can build, with the build time. The tabs are **Ships**, **Facilities**, **Units** and **Upgrades**. **Only Latest** hides old facility levels and obsolete designs. On the Units tab, choose how many to build at once. Items that cannot be built here are greyed; click one to see why. **Click** an item to add it to the end of the queue.
- The queue on the right shows each item's progress. `Top`, `Up`, `Down`, `Bottom` and `Remove` change it; `Delete` removes the selected item too. Removing the item in progress asks first, because its progress is lost.
- Hover over an item to see its cost, build time and details.

The buttons on the right:

| Button | What it does |
|---|---|
| `Emergency Build`, `Repeat Build`, `Queue On Hold` | The build modes above. |
| `Set Move To`, `Clear Move To` | Send new ships to a waypoint, or stop doing so. |
| `Fill Queue` | Add a saved list of items (a queue type). |
| `Clear Queue` | Empty the queue. |

**Queue types** save typing. `Fill Queue` offers ready-made types that fill a planet's free
facility slots with the best producer of one kind (minerals, organics, radioactives, research,
intelligence) or a balanced mix, and the types you saved yourself. `Add Type` saves the current
queue as a new type, which you can use in every game.

## Construction advice

- Give every colony with people something to build. An idle queue is wasted rate.
- New colonies first need a spaceport (if the system has none), then producers.
- Build ships at your best yards, and facilities everywhere else.
- Check the Log each turn for queues that ran short of resources.
