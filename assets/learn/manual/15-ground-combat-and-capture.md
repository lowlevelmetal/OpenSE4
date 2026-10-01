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
2. In the battle, move a ship carrying troops next to the planet and give the **Drop Troops** order. The ship drops all the troops it carries. Planetary shields do not stop a landing.
3. The ground combat is fought at once, in the middle of the space battle.

In a strategic battle, ships with the **Drop Troops** strategy do this by themselves. They wait
while the planet still has weapons and your side has armed ships to deal with them, then go in
and land. While your troop ships are waiting to land, your other ships hold their fire on enemy
planets that have no weapons left, so that you do not kill the people you came to conquer.

Only one invader at a time: no empire can land on a planet where two other empires are already
fighting. You can land more troops at any time to reinforce your own.

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

When the two empires make peace (Non-Aggression or better) before that, the fighting stops and
the landed troops simply join the colony's cargo.

The conquered people keep their race. A colony with several races is domed if any of them cannot
breathe its air (see [Planets and colonies](planets-and-colonies#size-and-capacity)), and its mood
may suffer.

> Bring more troops than you think you need. The defender's militia counts every 20M of population, so a big homeworld can field a large army even with empty barracks.

## The Ground Combat window

The **Ground Combat** window shows a ground combat fought in a battle: the
planet, its owner, its population and facilities, and the defending and attacking units. `Begin`
shows the outcome: how many rounds were fought, and whether the planet fell, the invasion failed
or the fight goes on. When a battle had several ground combats, `Previous` and `Next` step
between them.

## Capturing ships

A ship with **boarding parties** can try to capture an enemy ship or base:

- the target must be next to the boarding ship, on the battle map;
- the target's shields must be down.

Give the **Capture Ship** order in a tactical battle, or use the **Board Enemy Ships** strategy.
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
