---
windows: ground-combat
---
# Ground combat and capture

Destroying an enemy is one way to win. Taking what is theirs is often better: a captured planet
keeps its people and facilities, and a captured ship joins your fleet. This chapter explains
invasions with troops, and capturing ships by boarding.

## Invading a planet

To take a planet you need **troops**: ground units you design on the Unit Designs tab and carry
in the cargo of your ships (see [Units](units)).

1. Bring the troop ships to the enemy colony's sector. A battle starts there (see [Combat](combat#when-battles-happen)).
2. In the battle, move a ship carrying troops next to the planet, select it and press `T` (or **Drop Troops** in the Orders window). The ship lands all the troops it carries at once on the adjacent colony of another empire; when several are next to it, the one that came last into the battle. Planetary shields do not stop a landing.
3. The ground combat is fought at once, in the middle of the space battle.

> A landing does not ask about treaties: troops dropped next to an ally's colony land on it and fight it like any other, and take it if they win. Keep troop ships away from your friends' planets.

Every landing also gives the planet a fresh start in the battle: its shields are full again and all its weapons are ready, whether or not it falls.

What counts as "another empire" is the side the planet fights for at that moment. A planet whose crew was turned to your side by crew conversion is no landing site for you, while its old owner may land on it. A colony of yours can also land the troops stored in its cargo on another side's colony beside it: select the planet and press `T`.

When a landing is refused, a message box says why: no colony of another side is next to the ship, troops of a third empire already fight on that colony, or the ship carries no units at all. A ship that carries other units (fighters, mines, satellites) but no troops is simply not landed, without a message.

In a strategic battle, ships with the **Drop Troops** strategy do this by themselves. They head
only for enemy colonies: they wait while the planet still has weapons and your side has armed
ships to deal with them, then go in and land. After every move they land on whatever colony of
another empire is then next to them, an ally's included. While your troop ships are waiting to
land, your other ships hold their fire on enemy planets that have no weapons left, so that you do
not kill the people you came to conquer.

Only one invader at a time: no empire can land on a planet where another empire's troops are
already landed. You can land more troops at any time to reinforce your own.

## How ground combat works

The **defenders** are the units in the planet's cargo, mainly its own troops, plus a **militia**
raised from its people: one militia unit for every 20M of population with the stock settings. A
colony of fewer than 20M has no militia. Militia losses do not cost population.

The **attackers** are the troops that landed.

A ground combat lasts up to 10 rounds (stock settings). In each round every unit has a chance to
hit, about 50 %, raised by its side's offense and lowered by the other side's defense. The hits
of each side add up, and the total destroys enemy units one by one, troops that fight first. Your
race's ground combat skill (its physical strength, culture and traits) changes the totals.

- If the defenders have no troops or militia left and the attackers still have troops, **the planet is yours**, with its people, its facilities and the units in its cargo. No facilities are destroyed in ground combat.
- If the attackers are all gone, the invasion has failed.
- If both sides hold on after the last round, the fight goes on next turn, during the defender's end-of-turn processing, and so on until one side wins.

When the two empires are at Non-Aggression or better by then, the fighting stops and the landed
troops simply join the colony's cargo. This is the only place where treaties count: a landing and
the fight it starts ignore them.

The conquered people keep their race. A colony with several races is domed if any of them cannot
breathe its air (see [Planets and colonies](planets-and-colonies#size-and-capacity)), and its mood
may suffer.

> Bring more troops than you think you need. The defender's militia counts every 20M of population, so a big homeworld can field a large army even with empty barracks.

## The Ground Combat window

The **Ground Combat** window shows a ground combat: the planet with its type, atmosphere,
conditions, value, population and facilities (each with its level), and the defending and
attacking troops, each side under its flag, one stack to a cell with its count at the cell's
corner (the militia, drawn as the planet's people, among the defenders). `Begin` fights it round
by round: after each round the round counter and the counts change, and an explosion flashes
over the planet. *Victorious!* appears beside the winner, and `Close` stays dim until the end.

It opens by itself once your troops have landed in the Tactical Combat window, or when a
computer player's troops land during a battle you watch, unless both empires are played by the
computer. Each landing opens its own window, one after another. In a turn-based game it also
opens at the end of the defending empire's turn, after a notice, for every planet where the
fight goes on, when one of the two empires is played by a person.

## Capturing ships

A ship with **boarding parties** can try to capture an enemy ship or base:

- the target must be next to the boarding ship, on the battle map;
- the target's shields must be down.

Give the **Capture Ship** order in a tactical battle (`C`, then click the target), or use the **Board Enemy Ships** strategy.
The game compares the two sides:

- the boarders' strength is the total of their boarding components;
- the defenders' strength is the target's security components, plus 4 for each crew quarters component, plus its own boarding parties.

If the boarders are **stronger** (no luck is involved), the ship is captured at once. It changes
sides, loses its orders and its crew's experience, and its weapons need ten extra combat turns to
reload. Both ships' boarding and security components are used up. If the boarders are not
stronger, the attempt fails and the attacker's boarding parties are lost.

A ship with a working **self-destruct device** does not let itself be taken: when the boarders
would win, it blows up, damaging the boarding ship as well.

A captured ship keeps its cargo, and any troops aboard now fight for you.

## Crew conversion

A few weapons work on the minds of the crew instead of the hull. Each hit has a chance, equal to
its damage value, to make the target ship change sides at once. Ships with a master computer are
immune. Converted ships keep their crew experience.

> Captured ships come with components you may not have researched. You cannot repair those components yourself, so a captured ship may never be fully repaired.
