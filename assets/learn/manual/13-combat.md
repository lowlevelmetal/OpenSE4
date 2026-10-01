---
windows: strategies
---
# Combat

Sooner or later your ships will fight. This chapter explains when battles happen, how they are
laid out, how hits and damage work, and how strategies and formations steer your ships. The
windows you fight in are described in [Battle windows](combat-windows).

## When battles happen

Two empires fight when their treaty is **below Non-Aggression**: at War, in Non-Intercourse, with
no treaty, or before they have even met. From Non-Aggression up, their ships share sectors in
peace.

A battle also needs **sight**. Your ships fight only enemies that one side can see. A cloaked
ship that no enemy can see slips through enemy sectors without a fight (see
[The galaxy](galaxy#sight-and-sensors)).

- **Turn-based games**: a battle can start when a group of ships moves into a sector (warp jumps included) and sees a hostile object there, or is seen by one while cloaked. The **Attack** order starts one on purpose. Ships that simply sit in a sector never start a battle. Before your ships enter a sector with visible enemies, you are asked whether they should enter and attack: `Yes` (or `Y`) goes in; `No` stops the move and cancels the ship's orders.
- **Simultaneous games**: after each of the turn's 30 days, every sector where something carried out an order that day is checked, and a battle is fought where enemies see each other. A sector fights at most once a turn, unless newcomers arrive or a survivor of the last battle there was damaged.

Once a battle starts, **everything** owned in that sector takes part, including the ships and
colonies of empires that are at peace with everyone there. Every ship is decloaked until the
battle ends. Treaties still hold inside the battle: nobody fires at an empire it is not hostile
to. Mines never take part (see [Units](units#mines)).

In a turn-based game, a group whose move starts a battle loses the rest of its orders. In a
simultaneous game battles do not stop movement or clear orders, except that a Sentry order at the
top of a ship's list ends.

## Tactical or strategic

In a local or hotseat turn-based game, each battle with a human side asks you to choose (see
[Battle windows](combat-windows#how-battles-reach-you)):

- **Tactical** (`T`): you give the orders for your ships in every combat turn (see [Battle windows](combat-windows#tactical-combat)).
- **Strategic** (`S`): every ship follows its strategy, and you watch.

The rules are the same either way. Simultaneous games, network games, play-by-e-mail games and
games set up with **Strategic combat only** always fight strategically.

## The battlefield

A battle is fought on its own map of 72 by 63 squares. Ships, bases and unit groups take one
square each. Planets, stars, warp points and uncolonized planets take four by four squares; the
last three are obstacles that nobody can target. Storms and asteroid fields are not on the map.

Where each side starts depends on where it came from:

- ships that arrived from a neighbouring sector start at the matching edge or corner of the map;
- ships that arrived through a warp point start in the very centre;
- everything that was already in the sector starts in the middle, with planets near the centre.

An empire that had something in the sector before the battle is a **defender**; the others are
**attackers**. Each combat turn, the defenders act first, in a random order drawn at the start,
then the attackers.

A battle lasts at most 29 combat turns with the stock settings. It ends sooner once no two sides
left are hostile to each other. There is no retreat order: a side can only run with a cautious
strategy, survive until the end, or leave on the system map next turn.

## Movement in combat

A ship moves half its system-map speed, rounded up, each combat turn, plus any combat movement
bonus from its components. Each step to one of the eight neighbouring squares costs one point.
Planets, satellites, bases and mothballed ships do not move. Fighters and drones move as soon as
they are launched.

## Hitting the target

Direct-fire weapons roll to hit. The chance is:

- 100,
- plus the attacker's offense: components and hull, crew experience, fleet experience, racial aggressiveness, culture and facilities in the system,
- minus the target's defense: components and hull, crew and fleet experience, racial defensiveness and culture,
- minus **10 for every square** between the two,
- minus any sensor interference in the sector,
- kept between 1 % and 99 %.

For example: a destroyer whose hull and electronic countermeasures give it +40 defense, fired at
from 5 squares by a ship with +25 offense, is hit 100 + 25 − 40 − 50 = 35 % of the time.

Planets are almost impossible to miss. Seekers, drones, rams and mines never roll: they hit when
they arrive.

Each weapon has a **damage table** by range. A weapon can fire only where its table shows damage
above 0, and it must wait its **reload** time between shots. Each shot uses supplies; a ship
without supplies cannot fire. A ship can engage only one target per combat turn, unless it has
components that track several targets. Planets can engage ten.

> Close the distance. At 1 square the range penalty is 10 points; at 6 squares it is 60. Short-range weapons with strong armor beat long-range weapons more often than you might think.

## Damage

A hit that lands goes through these layers:

1. **Shields** absorb damage first, point for point, until they are down. Shields are full at the start of every battle; some components regenerate them during the battle. Phased shields stop every kind of shot; some weapons skip normal shields.
2. **Armor**: once the shields are down, armor components are destroyed before anything else.
3. **Other components** are destroyed one by one, in a random order that favours large ones.

Components are destroyed whole, never partly damaged. Damage too small to destroy the next
component is kept and added to the next hit on that ship. A ship is destroyed when its last
component is.

A damaged ship is weakened at once: lost engines slow it, lost weapons cannot fire, lost shield
generators lower its shields, lost cargo space loses its cargo, and a ship without its bridge,
life support or crew quarters moves at half speed or less.

Some weapons do special damage: they skip shields or armor, drain only shields, hit only engines
or weapons, slow the target's reload, push or pull it across the map, or even take over its crew.
The [Weapons Report](help:weapons) and each component's report tell you what a weapon does.

## Missiles, point-defense and ramming

- **Seekers** (missiles and torpedoes) fly toward their target at their own speed, starting the turn after launch, and hit when they reach it. Their damage depends on how far they have flown. They can be shot down on the way.
- **Point-defense** weapons fire on their own, at any moment, at fighters, drones and seekers that move within their reach.
- **Ramming**: a ship next to an enemy can ram it. The rammer deals damage based on its own remaining strength, takes the target's strength in return, and the warheads of both explode. Drones attack only by ramming.

## Planets in battle

A colony fights with the weapons of the **weapon platforms** in its cargo, can launch fighters,
satellites and drones, and engages up to ten targets a turn. Damage that passes its shields hits
the units in its cargo first, platforms before anything else. Only when they are gone does it
kill population: 1M for every 10 points of damage (stock settings). Facilities may be destroyed
as the population falls. A colony whose people are all killed is lost.

## Strategies

A **strategy** tells a ship how to fight when the computer is in control: in strategic battles,
under Auto or after Resolve Combat in tactical battles, and always for computer players. Each design has a default
strategy, and a fleet's strategy overrides it for the fleet's armed members.

Open [Strategies](window:strategies) from Empire Status or from the Combat Simulator. Its pages:

- **Movement**: a primary and a secondary movement strategy. The secondary is used when the primary is impossible (for example, dropping troops with no troops aboard).
- **Firing**: up to four target priorities (nearest, weakest, most damaged, has weapons and so on), a ranking of target types, the types never to fire on, and how badly damaged a target may be before the ship turns to another.
- **Launching**: how many fighters to launch per group.
- **Formation**: which kinds of ships leave their formation in combat.

The movement strategies are:

| Strategy | What the ship does |
|---|---|
| Optimal Weapons Range | Seeks the square where it deals the most damage for the least danger. |
| Short Weapons Range | Seeks the square where it deals the most damage. |
| Maximum Weapons Range | Stays as far away as it can while still hitting. |
| Point Blank | Goes right next to its target. |
| Board Enemy Ships | Closes in to capture ships ([Ground combat and capture](ground-combat-and-capture#capturing-ships)). |
| Ram | Rams its target. |
| Drop Troops | Lands troops on an enemy planet ([Ground combat and capture](ground-combat-and-capture)). |
| Don't Get Hurt | Keeps away from enemies. Unarmed ships use it. |

`Add` makes a new strategy, `Copy` copies one and `Remove` deletes it.

## Formations

A fleet fights in **formation**. Its leader takes its place, and each armed member takes the next
position of the formation around it, turned to face the way the leader faces. When the leader
moves, the members follow. A ship leaves the formation when its strategy says so, and the whole
formation breaks up when the leader is destroyed. The Help window's [Formations](help:formations)
tab draws each formation.

## After a battle

- Carriers take back the fighters and satellites they launched in the battle, if they have room.
- Shields refill at the next battle; other damage stays until it is repaired (see [Supply, cargo and repair](logistics#repair)).
- Ships that destroyed enemies gain experience (see [Ships and fleets](ships-and-fleets#experience)).
- Every side gets a battle report in the Log, and learns the designs it fought.
- Colonies in the system grow happier or angrier, depending on the result and on the race.
