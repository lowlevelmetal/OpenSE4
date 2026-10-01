---
windows: launch-recover
---
# Units

**Units** are small vehicles that live in the cargo of ships and planets: fighters, satellites,
mines, drones, troops and weapon platforms. They cost no maintenance, and used well they can
decide a war. This chapter explains each kind and how to launch and recover them.

## Building units

You design units on the **Unit Designs** tab of the [Designs](window:designs) window and build
them on the **Units** tab of a construction queue, where you choose how many to build at once.
A finished unit goes into the cargo of the planet or ship that built it, or of another of your
planets or ships in the same sector if it does not fit. Make sure there is room before you build.

Units never pay maintenance, in cargo or in space.

## The kinds of units

| Kind | Launched by | What it does |
|---|---|---|
| Fighter | Carriers with fighter bays, and planets | Small, fast attack craft. Moves inside its system but cannot use warp points. |
| Satellite | Ships with satellite bays, and planets | A stationary weapon platform in space. It guards its sector and fights any enemy there. |
| Mine | Mine layers, and planets | Hidden explosives. They strike enemy ships that enter the sector. |
| Drone | Drone launchers, and planets | A guided craft that flies to its target and rams it, warp points included. |
| Troop | Never launched | Carried in cargo and dropped on enemy planets during battles ([Ground combat and capture](ground-combat-and-capture)). |
| Weapon Platform | Never launched | Stored in a planet's cargo, it becomes one of the planet's guns in battle. |

Units in space are held in **groups**: all your fighters of one sector form a group, and so do
your satellites and your mines. A group can mix designs. Each drone is a group of its own.

## Launching and recovering

A ship launches a kind of unit only if it has the matching launcher, and only up to its launch
rate each turn. A colony needs no launcher: it can launch up to 1,000 units of each kind a turn.

Only **fighters** and **satellites** can be recovered. A ship needs the matching bay to take them
back; a colony needs nothing. Free cargo space is the only limit. In a turn-based game, a fighter
group can be recovered only before it has moved that turn.

Limits per empire, with the stock settings:

- at most 100 of your mines and 100 of your satellites in one sector;
- at most 1,000 units in space in all (the game setup can change this). Units in cargo do not count.

A launched group joins the last group of its kind in the sector, if there is one. A new group can
move at once in a turn-based game, but only from the next turn in a simultaneous game.

## The Launch and Recover window

The [Launch and Recover Units](window:launch-recover) window (`U`, titled
`Launch \ Recover Units`) works for a selected ship or colony in turn-based games.

- The left list, **Units in sector**, shows your ships with bays or units aboard, with their launch rates per turn (or *No launch bays*), and your colonies that hold units (1,000 of each kind a turn). Click a unit type under a holder to launch it.
- The right list, **Units in space**, shows your unit groups in the sector. Click a group (or one design of a mixed group) to recover it into the ship or colony selected on the left.
- `Move One`, `Move Five`, `Move Ten` and `Move All` choose how many units each click moves.
- `Launch Remotely` and `Recover Remotely` give the ship an order to launch or recover at a sector you pick later.

Each click becomes an order at the head of the holder's list (for a ship in a fleet, the fleet's),
and is carried out at once; the orders given are listed at the bottom. In a simultaneous game,
use the orders **Launch Units Remotely** (`I`) and **Recover Units Remotely** (`O`) instead: they
are carried out during the turn's movement.

In tactical combat, `L` opens **Launch Units** for the selected piece.

## Fighters

Fighters are carried to battle by **carriers** and launched in combat, or launched beforehand to
guard a system. In battle a fighter group is one piece: all its identical weapons fire at the
same target together, and their hits add up into one big hit. A group dies one fighter at a time.

Fighters use supplies: 5 per fighter per turn with the stock settings, on top of moving. Out of
supplies a group crawls at 1 sector a turn and cannot fire, but it is not destroyed. A colony with
a resupply depot refills it. Fighter groups can join fleets.

After a battle, each carrier recovers the fighters it launched in that battle, as far as its
cargo allows.

## Satellites

Satellites never move and need no supplies. Launch them over your colonies and at warp points to
add firepower to every battle there. In combat a satellite group can engage as many targets as it
has satellites left.

## Mines

Mines are invisible to every stock sensor, so your enemies cannot see your minefields, and you
cannot see theirs. When a group of ships enters a sector with mines of an empire that is hostile
to it (any treaty below Non-Aggression), each mine strikes a random ship of the group and is used
up. Mine damage goes straight to the components: shields do not help. A group that meets mines
stops moving, and its orders are cleared.

Mines do not fight in battles and are not combat pieces. They have no supplies and never run out.

**Mine sweepers** protect against mines. When a group with mine sweeping components enters a
mined sector, the sweepers first remove up to their sweeping capacity in enemy mines; only the
rest strike. The **Sweep Mines** order (`Ctrl+M`) runs this again in the current sector: the sweepers clear
what they can, then any remaining mines strike.

> Lay mines at the warp points into your core systems. An attacker loses ships before the battle even begins, and you do not need to be there.

## Drones

Drones are launched without a target. Give a drone group an **Attack** order and it pursues its
target, through warp points if needed, and rams it in battle with its warheads. In battle, drones
are always controlled by the computer.

Drones pay 200 supplies a turn (stock settings) and can never be resupplied. A drone group whose
supplies run out is destroyed.

## Troops and weapon platforms

**Troops** are carried by ships and dropped onto an enemy colony during a battle. See
[Ground combat and capture](ground-combat-and-capture).

**Weapon platforms** turn a planet into a fortress. Each weapon of each platform in a colony's
cargo fires as one of the planet's weapons, and platforms also absorb damage before the planet's
people do.

## Getting rid of units

Fighter and satellite groups can be scrapped at a space yard for part of their cost. Satellite
groups, minefields and drone groups can self-destruct at any time; fighter groups cannot.
Minefields and drone groups cannot be scrapped.
