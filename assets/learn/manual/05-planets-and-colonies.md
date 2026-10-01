---
windows: planets, colonies
---
# Planets and colonies

Colonies are the heart of your empire. They produce your resources and research, build your
ships and hold your people. This chapter explains what makes a planet worth settling, how to
colonize it, and how to keep its population growing and content.

## What a planet is like

Every planet has these properties. Click a planet in the system panel to see them in its
report.

| Property | What it means |
|---|---|
| Type | Rock, Ice or Gas Giant. You need a different colonization technology for each type. |
| Size | Tiny, Small, Medium, Large or Huge. Rare constructed worlds are far larger. |
| Atmosphere | None, Methane, Oxygen, Hydrogen or Carbon Dioxide. |
| Conditions | How pleasant the planet is to live on, from Deadly to Optimal. |
| Value | Three percentages, one each for minerals, organics and radioactives. |
| Abilities | Special features, such as ancient ruins. |

Your race breathes exactly one of the four gases. A planet whose atmosphere your race breathes
is **breathable**. You can also settle a planet with any other atmosphere, or none, but then
your people live under domes.

## Size and capacity

A planet's size sets three limits for a colony on it: how many **facilities** it can hold, how
much **population**, and how much **cargo** (stored units and people). The limits roughly
double with each size step.

A **domed** colony gets much lower limits: about a fifth of the facility slots and a tenth of
the population and cargo. A colony is domed when any race living on it cannot breathe its air.
For example, a Medium planet holds 15 facilities with a breathable atmosphere, but only 3 under
domes.

> A domed colony can still be worth having for its position, for a resupply depot or a space yard, or for a single high-value mine. But breathable planets are where your empire grows.

If a colony becomes domed later (for example because people of another race move in), nothing
is removed. Facilities above the new limit stay, but you cannot build more. Population above
the new limit stays, but the colony stops growing until it falls below the limit.

Some facilities slowly change a planet's atmosphere to the one most of its people breathe.
When that happens, the dome comes off and the full limits apply.

## Conditions

Conditions affect only how fast the population grows.

| Conditions | Growth per year |
|---|---|
| Deadly | −20 points |
| Harsh | −5 points |
| Unpleasant | −2 points |
| Mild | no change |
| Good | +2 points |
| Optimal | +5 points |

Natural planets are mostly Unpleasant or Mild; Good and Optimal planets are rarer. Some
facilities improve conditions every ten turns, and events or weapons can make them worse.

## Value

A planet's value multiplies what its facilities produce. A mine on a planet with a mineral
value of 120 % produces 20 % more than on a planet at 100 %. Each resource has its own value,
so a planet can be rich in minerals and poor in organics. Values change through facilities,
events, intelligence operations and remote mining.

In a game with **finite resources**, value is instead a stock of each resource. Production
draws the stock down, and a resource whose stock reaches 0 produces nothing more. Solar
facilities do not use the stock.

## Ancient ruins

A few planets hold the ruins of an older civilization. The first empire to colonize such a
planet gains one or more random technology levels at once, and some ruins open a unique
technology area that no one else can research. The ruins are then used up. Look for them with
the **Special** filter of the [Planets](window:planets) window.

## Colonizing

To found a colony you need a **colony ship**: a ship with a colony module for the planet's
type (Rock, Ice or Gas Giant). At the start you can colonize only your home planet type;
research the other colonization technologies to settle more types. Asteroid fields can never be
colonized.

In the system panel, a small star on a planet tells you whether you can settle it:

| Marker | Meaning |
|---|---|
| Green star | You can colonize it, and your race breathes its air. |
| Red star | You can colonize it, but the colony would be domed. |
| No star | You cannot colonize it (wrong type for your technology, or already taken). |

To colonize:

1. Build a colony ship at a colony with a space yard (see [Construction](construction)).
2. Select the ship and give the **Colonize** order (`C`), then click the planet.
3. If the ship carries no population and is at one of your colonies, it first loads people there. It then flies to the planet and settles it.

When the ship arrives, it is used up. The new colony gets the people and units the ship carried,
as far as they fit; whatever does not fit is lost. In a turn-based game you may be asked to
choose a **colony type** for it (see below). If two ships try to settle the same planet on the
same turn, the first one wins and the other's orders fail.

The [Planets](window:planets) window (`F4`) helps you find targets (see
[below](planets-and-colonies#the-planets-window)).

> Fill colony ships with population at your homeworld before you send them. A colony that starts with people grows and produces from the first turn; an empty one can build nothing.

Game options can forbid domed colonies, or limit you to your home planet type.

## Population

Population is counted in millions (M). A colony's people grow every turn by its **growth rate**,
which is shown as a percentage per year (ten turns):

- every race starts with a base rate (10 % with the stock settings), changed by its Reproduction characteristic and traits;
- mood adds from −5 points (Angry) to +5 (Jubilant);
- conditions add from −20 to +5 points (table above);
- the race's Environmental Resistance changes it a little on every planet;
- some facilities raise the rate for every colony in their system.

A colony that is rioting, plagued or empty does not grow. A growing colony always gains at least
1M a turn, until it reaches its limit.

Larger populations also work better. A colony's output and its construction rate rise with its
population, from no bonus below 20M to double at about 10,000M. A full Medium homeworld of
2,000M gets about +30 %.

You can move people between planets as cargo: each 1M takes 5 kT of cargo space with the stock
settings. You can never take a colony's last 1M away. To give up a small colony, select it and
use the **Abandon Planet** order button. It works only for colonies of 50M or less (stock
settings), never for your homeworld, and it acts at once, without asking: the colony and its
facilities are gone. Scrap the facilities first if you want part of their cost back.

## Mood

Each colony has one mood for all its people. Behind the mood is an **anger** value from 0 to
100:

| Anger | Mood | Output with stock settings |
|---|---|---|
| 0 to 14 | Jubilant | 120 % |
| 15 to 29 | Happy | 110 % |
| 30 to 44 | Indifferent | 100 % |
| 45 to 59 | Unhappy | 90 % |
| 60 to 89 | Angry | 80 % |
| 90 to 100 | Rioting | nothing |

A new colony starts Happy (anger 25). Over time moods drift back toward Indifferent from either
side. Many things push a colony one way or the other. Each race has its own **happiness type**,
which sets how much each of these matters and in which direction:

- planets won, lost or captured anywhere in your empire;
- ships built or lost;
- new treaties, including declarations of war;
- battles in the colony's system or sector, won or lost;
- your own ships, and enemy ships, in the colony's system or sector;
- troops on the planet, people killed, and plague;
- facilities that calm a planet or a whole system;
- the race's Happiness characteristic, which calms every colony a little every turn.

A peaceful race may hate wars, while a warlike race may cheer for them. The **Our Race**
button of the [Empires](window:empires) window shows your race's happiness type.

A **rioting** colony produces nothing, builds nothing and does not grow. Ships in orbit and
troops on the planet calm it. Riots never turn into rebellion by themselves: a planet only
breaks away through a random event or an enemy's intelligence operation.

Your homeworld, and every planet you start the game with, is a **capital**. A capital's anger
never rises above 80, so a capital never riots.

> If a colony's mood keeps sliding, park a ship in orbit or land some of your own troops. Both help most races.

Some races are **emotionless**: they have no moods at all, their colonies never riot, and their
growth gets no mood bonus.

## Blockades

A colony is **blockaded** while an enemy ship or base that you can see sits in its sector. Enemy
means any empire with which you have no treaty of Non-Aggression or better. A blockaded colony
produces nothing that turn: no resources, research or intelligence. Fighters, satellites, mines
and drones never blockade.

## Plague

Events and enemy agents can infect a colony with plague. A plagued colony loses people every
turn: from about 10M a turn at the mildest level to about 500M at the worst. It does not grow,
its people grow angrier, and nothing can be loaded from it. The level never rises by itself.
Medical facilities cure plagues up to their level in their system, and ships with a medical bay
cure them on your own planets and your allies' planets in their sector.

## Colony types

A colony type is a label that tells your ministers and the computer what to build there:
homeworld, mining, farming, refining, resupply base, research, intelligence, construction yard
or military. It has no effect of its own. You set it with **Set Colony Type** in the
[Colonies](window:colonies) window, or when you found the colony.

## The Planets window

The [Planets](window:planets) window (`F4`) lists every planet you have seen, with its system,
type, atmosphere, conditions, values and status: yours, another empire's, colonizable, needing a
dome, or a ship already on its way. Hover over a dash in the status column to see why a planet
cannot be colonized. The statistics at the top count your explored systems, planets and colonies,
and a small map marks where the listed planets are.

The filter buttons choose what to list:

| Filter | Shows |
|---|---|
| `All` | Every planet you have seen. |
| `Colonizable` | Planets you can colonize now. |
| `All Colonies`, `Enemy Colonies`, `Ally Colonies` | Colonies, by owner. |
| `Coloniz\Empty` | Colonizable planets in systems where no other empire has a colony. |
| `Coloniz\Breathe` | Colonizable planets your race can breathe on. |
| `Ship Enroute` | Planets one of your colony ships is heading for. |
| `Asteroids` | Asteroid fields. |
| `Special` | Planets with special abilities, such as ruins. |

`No Sys To Avoid` hides planets in your systems to avoid. Click a column heading to sort.

Click a planet to select it, double-click it (or press `Goto`) to show it in the main window, and
right-click it for its report. **Send Colony Ship** gives the selected planet to your nearest idle
colony ship (not in a fleet) that can settle it.

> Sort the Colonizable list by conditions or by mineral value to find your best targets first.

## The Colonies window

The [Colonies](window:colonies) window (`F5`) lists all your colonies, with statistics for your
whole empire at the top: population, facilities, space yards, idle queues, unhappy colonies and
your output per turn.

| Tab | Columns |
|---|---|
| General | System, type, colony type, population, mood, facilities. |
| Value | Type, atmosphere (domes are marked), conditions, the three values. |
| Production | Output of each resource, research and intelligence, and whether it is delivered, lost for lack of a spaceport, or blockaded. |
| Facilities | Slots used and the facilities, by type. |
| Cargo | Cargo space used, and what is stored. |
| Construction | What is being built, its progress and time, and the queue's mode. |
| Status | Status icons, mood and anger. |
| Races | The population of each race. |
| Orders | The orders given to the colony this turn. |

Click a column heading to sort. Click a colony to select it, and `Ctrl+click` or `Shift+click` to
select several. Double-click a colony (or press `Goto`) to show it in the main window, and
right-click it for its report.

| Button | What it does |
|---|---|
| `Scrap Facil Types` | Scrap every facility of one type, on the selected colonies or on all of them. |
| `Set Colony Type` | Change the colony type of the selected colonies. |
| `Constr. Queue` | Open the selected colony's construction queue. |
| `Goto` | Show the selected colony in the main window. |
