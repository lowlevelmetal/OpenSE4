---
windows: galaxy-map, systems-to-avoid
---
# The galaxy

Every game is played in one quadrant of a galaxy: a few dozen star systems joined by warp
points. This chapter explains how the quadrant is laid out, how you explore it and what you
can see in it.

## The quadrant

The quadrant is a grid of 67 by 46 squares. One square is about ten light years. Each star
system sits on one square, and no two systems are close neighbours.

You choose the size of the quadrant when you set up a new game:

| Quadrant Size | Systems (stock data) |
|---|---|
| Small | 20 to 39 |
| Medium (default) | 40 to 79 |
| Large | 80 to 99 |

The quadrant type decides how the systems are spread out: at random, in clusters, along a
spiral, thinly scattered or on a regular grid. It also sets how many warp links each system
tends to get. See [Starting a game](getting-started#game-setup) for the other quadrant options.

You know where every system is from the first turn. You do not know its name, what is in it or
where its warp points lead until one of your ships goes there.

## Star systems and sectors

A star system is a grid of 13 by 13 **sectors**. The star usually sits in the centre sector,
planets lie in rings around it, and warp points sit on the outer edge. Any number of objects
can share a sector: a planet and its moons, a fleet in orbit, a storm.

Inside a system, a ship moves one sector per movement point, and a diagonal step costs the same
as a straight one. So the distance between two sectors is the larger of the two coordinate
differences.

A system can contain:

- **Stars.** Most systems have one, some have several, and a few have none. Stars matter for solar power and for some kinds of [stellar manipulation](events-and-stellar-manipulation).
- **Planets**, which you can colonize. See [Planets and colonies](planets-and-colonies).
- **Asteroid fields.** You cannot colonize them, but ships with remote mining components can mine them, and some ships can turn them into new planets.
- **Storms.** A storm can hide what is inside it, weaken shields in combat, upset sensors in combat, or damage ships that fly into it.
- **Warp points**, the doors to other systems.

Some systems are not ordinary star fields. A **nebula** hides everything inside it from weak
sensors. A **black hole** pulls ships toward its centre every turn and heavily damages whatever
sits there. Some systems have random currents that push ships around. Read the system report
(click empty space in the system panel) before you send ships into an unusual system.

## Warp points

Warp points always come in pairs: one in each of two systems, each leading to the other.
Every link works in both directions.

- A ship must stand in the warp point's sector to jump. The jump costs one movement point and the same supply as one step, and it lands the ship on the matching warp point at once.
- A **Move To** order to a sector in another system finds a route through warp points you know, and jumps as needed. It only routes through systems you have explored.
- To jump into a system you have never visited, give a **Warp** order (`W`) for that warp point. The **Explore** order (`E`) does this for you: it sends the ship to the nearest warp point that leads somewhere new.
- Some warp points are **turbulent**. Each jump through one has an even chance of damaging every ship that jumps; the ships still arrive, but they stop for the turn.
- A system holds at most ten warp points. Special ships can open new warp points and close existing ones (see [stellar manipulation](events-and-stellar-manipulation#stellar-manipulation)).

A warp point shows the name of its destination once you have explored that system.

> When a Move To order fails at a warp point, check whether the far side is still unexplored. Give a Warp order instead, or use Explore.

## Exploring

A system becomes **explored** the first time one of your ships enters it. From then on you
remember its stars, planets, asteroid fields, storms and warp points, even when you have no
ship there.

Exploring is the most important thing you do in the first years of a game. It shows you which
planets you can colonize, where your neighbours are and which warp points lead to dead ends.

- The **Explore** order picks a destination by itself. Give it, end the turn, and give it again when the ship arrives.
- Two ships with Explore orders do not pick the same warp point.
- A Move To order cannot enter an unexplored system; Explore and Warp can.

> Build one or two cheap, fast scouts early and keep them on Explore. They pay for themselves many times over.

## Sight and sensors

Knowing that a system exists is not the same as seeing what is in it now. You see enemy ships
and colonies in a system only while you have a **sensor source** there: one of your ships,
bases, fighter, satellite or drone groups, or one of your planets. Mines do not count.

Every sensor source sees at level 1 by default. Every object you might look at has an
**obscuration** of 1 by default, so an ordinary ship is seen by anything. Things that raise
obscuration:

- a **cloaking device**, while the ship is cloaked;
- a **storm** or **nebula** that hides its contents, for planets, ships and units in it.

There are five kinds of sight: EM Active, EM Passive, Psychic, Gravitic and Temporal. A sensor
of level N in one kind sees anything whose obscuration in that kind is N or less. One kind is
enough. Sensor components and facilities raise your level in one kind.

Sight is worked out for the whole system, not per sector: if any of your objects in the system
sees something, you see it everywhere in that system. Your own objects are always visible to
you.

What you cannot see, you cannot attack. Enemy ships pass through your sectors without a fight
if neither side sees the other. See [Combat](combat#when-battles-happen).

> Stock mines are hidden from every stock sensor. You usually find a minefield by losing a ship to it. Add that system to your systems to avoid (below), or send a mine sweeper first (see [Units](units#mines)).

## Scanning enemy ships

Seeing a ship is not the same as knowing its design. A **long range scanner** lets you look
inside an enemy ship, base or unit group in the same system within its range: open the ship's
report and you see its components and cargo, and you learn its design. A **scanner jammer**
on the target stops this. Some facilities scan every ship in their system.

You also learn designs by fighting them, and through intelligence.

## The galaxy panel and the Galaxy Map

The galaxy panel at the bottom right of the main window shows the whole quadrant on a fine grid.
Systems are small rings. Warp lines start from the systems you have explored; a link to a system
you have not explored is a short stub.

| Marker | Meaning |
|---|---|
| Dark grey ring | A system you have not explored |
| Light grey ring | Explored, and no empire seen there |
| Ring in an empire's colour | That empire, and only that one, is seen there (your colour for you) |
| Triangle | Several empires are there: in your colour if you are one of them |
| Filled marker with a ring around it | The system shown in the system panel |

Each empire's colour is the colour swatch of its race's flag. Point at the panel to see the name
of the nearest explored system in cyan. Left-click a system to show it
in the system panel. Right-click the panel to open the [Galaxy Map](window:galaxy-map).

The Galaxy Map is a larger map with overlay tabs: **Presence**, **Avoid** (the systems you avoid),
**Ally Claimed**, **Enemy Claimed**, **Spaceports** and **Resupply Depots**. Tick `Show Names`
to label the systems and `Show Distances` to see the distance in light years from the system
under the pointer. `Goto System` lists the systems you have explored, with a search box; pick
one to show it in the main window. Click a system to write notes about it, which only you can
read; hover over it to read them again.

## Claims and systems to avoid

Each empire can publicly **claim** systems as its territory. Claims can overlap, and every
empire sees them. Computer players treat the systems they hold, and the systems next to them,
as their territory. Your ships and colonies there make them angry while you have no
Non-Aggression treaty or better with them. See [Diplomacy](diplomacy#how-computer-players-feel-about-you).

You can also mark systems to **avoid** in the [Systems To Avoid](window:systems-to-avoid)
window (from [Empire Status](window:empire-status)). Click a system on its map to mark it or
unmark it; `Clear All` removes every mark. With the Empire Options switch **Never route through
the systems to avoid** on (it is on for a new empire), your ships never route through an avoided
system unless they start or end there. If no other route exists, the move fails instead.

The same window has tabs that show your presence and the systems your allies and enemies
claim. On the **Ally Claimed** tab, clicking a system claims it for you or releases your
claim, and `Release All Claims` gives them all up.

> Avoid a system with a known minefield or a strong enemy fleet, and your Move To orders will steer around it on their own.
