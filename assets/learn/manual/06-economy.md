---
windows: empire-status
---
# Economy

Your economy pays for everything: ships, facilities and the upkeep of your fleets. This chapter
explains the resources, how colonies produce them, how they reach your treasury and where they
go.

## The resources

| Resource | Used for |
|---|---|
| Minerals | Building: every hull, component and facility has a cost in some mix of the three materials. |
| Organics | Building, as above. |
| Radioactives | Building, as above. |
| Research points | Research projects (see [Research](research)). |
| Intelligence points | Intelligence projects (see [Intelligence](intelligence)). |

The Help window shows the cost of each item in the three materials. Minerals, organics and
radioactives are stored in your treasury; the status bar shows how much you have. Research and
intelligence points are spent the turn after they are produced and are never stored.

## How a colony produces

A colony produces through its **facilities**: mines produce minerals, farms produce organics,
refineries produce radioactives, research centres produce research points and so on. A colony
without the right facility produces none of that resource, however many people live there.

For each resource, a colony's output is worked out like this:

1. Add up what its facilities produce.
2. Multiply by the planet's **value** for that resource (minerals, organics and radioactives only).
3. Apply the best production bonus among its facilities, if any.
4. Apply the colony's **modifiers**, added together: your race's aptitude for that resource (and its culture), the colony's **mood** and its **population**.
5. Add solar output, from facilities that draw power from the system's stars.

A colony produces **nothing** while it is rioting, has no population, or is blockaded by an enemy
ship in its sector.

Here is a worked example. A homeworld has organics value 98 % and one farm that produces 800.
Its people are Happy (+10 %) and number 2,000M (+30 %). The output is 800 × 98 % = 784, then
784 × 140 % = 1,097 organics per turn.

> A planet's value multiplies every mine on it. Build mines on your mineral-rich planets and farms on your organic-rich ones; the Value tab of the Colonies window shows them side by side.

Some facilities raise output for every colony of yours in their system. Their bonus applies to
the system's total.

## Spaceports

Output does not reach your treasury by itself. It is shipped from each system through a
**spaceport**:

- A system where you have a spaceport on any colony delivers everything its colonies produce.
- A system without one delivers nothing. The output is lost, not saved.
- Your **home system** is the exception: without a spaceport it still delivers a quarter of its output.

This applies to research and intelligence points too. Races with the trait that removes the need
for spaceports skip this rule entirely.

The Colonies window's Production tab shows whether each colony's output is delivered, and Empire
Status shows how much was lost for lack of a spaceport.

> Every system you settle needs one spaceport, on one colony, before its colonies are worth anything to you. Put the spaceport first in a new colony's queue.

## Other income

- **Trade** with other empires, from a Trade Alliance up (see [Diplomacy](diplomacy#trade)).
- **Tariffs** paid by an empire you have subjugated or protect.
- **Remote mining**: ships with remote mining components extract resources from asteroid fields and uncolonized planets in their sector. Only one miner works in each sector, and in the stock rules each turn of mining lowers that object's value by one point.
- A few components and facilities produce resources directly.

If your colonies deliver none at all of one resource in a turn, you receive a small minimum
amount of it instead (200 with the stock settings).

## Storage

Each of minerals, organics and radioactives has a storage capacity: 50,000 with the stock
settings, plus what your storage facilities add. Late in each turn, after construction, anything
above the capacity is lost. Research and intelligence points have no limit.

> If your treasury sits at its capacity, you are wasting income. Build more, research storage facilities, or convert the surplus into ships.

## Maintenance

Every turn, each of your ships and bases costs a share of its build cost in **maintenance**, in
each resource. The share is 25 % with the stock settings. Races good at maintenance, and their
cultures, pay less, but never below 5 %. Bases pay half. Some facilities lower the maintenance of
ships in their system.

Fighters, satellites, mines, drones, troops and weapon platforms pay nothing, and neither do
mothballed ships. Planets pay nothing either.

Maintenance is paid after income and before construction. If your treasury cannot cover it, the
treasury drops to 0 and **ships are lost**: one ship for any shortfall up to 19,999, and one more
for each further 20,000 (stock settings). Ships out of supplies are abandoned first; if there are
none, any ship or unit group may be.

> Maintenance grows with your fleet. Before you build a large fleet, check Net Per Turn in Empire Status and make sure the economy can carry it. Mothball ships you do not need (see [Supply, cargo and repair](logistics#scrapping-and-mothballing)).

## Construction spending

Construction queues spend resources too. Each queue takes up to its build rate from your
treasury every turn, but only if it can pay for every resource at once (see
[Construction](construction#how-a-queue-builds)). A queue that cannot pay builds nothing that
turn.

## The Empire Status window

[Empire Status](window:empire-status) (`F11`) shows your budget per turn, with one column each for
minerals, organics and radioactives:

| Line | Meaning |
|---|---|
| Income: Colonies, Trade, Tariffs, Remote Mining, Other | Where this turn's income comes from. |
| Total Income | All income together. |
| Expenses: Tariffs, Maintenance, Construction Queues | Where it goes. |
| Total Expenses | All expenses together. |
| Net Per Turn | Income minus expenses: green when you gain, red when you lose. |
| Not delivered (no spaceport) | Output lost in systems without a spaceport. |
| Lost to full storage | Income lost because storage is full. |
| Stored, Storage Capacity | Your treasury and its limit. |

Below the table are your research and intelligence points per turn.

The buttons open your empire's settings windows:

| Button | Window |
|---|---|
| `Empire Options` | Interface and behaviour switches ([Settings](settings#empire-options)). |
| `Ministers` | Hand parts of your empire to the computer ([Computer players and ministers](computer-players-and-ministers)). |
| `Systems To Avoid` | Systems your ships route around ([The galaxy](galaxy#claims-and-systems-to-avoid)). |
| `Waypoints` | Your ten waypoints ([Ships and fleets](ships-and-fleets#waypoints)). |
| `Strategies` | Combat strategies ([Combat](combat#strategies)). |
| `Repair Priorities` | The order in which components are repaired ([Supply, cargo and repair](logistics#repair)). |
| `Change Password` | Set or remove your empire's password. |

The switch about missed turns matters only in simultaneous multiplayer games: with it on, when
the computer plays a turn for you because your orders did not arrive, it changes nothing.

## Economy advice

- Expand early. More colonies mean more output, more research and more places to build.
- A spaceport in every system you settle; a resupply depot where your fleets gather.
- Keep each colony happy: a Jubilant colony produces half as much again as an Angry one.
- Watch Net Per Turn. A large negative number means trouble within a few turns.
- In a game with finite resources, planets run dry. Keep expanding to new ones.
