---
windows: intelligence
---
# Intelligence

Intelligence lets you spy on other empires and sabotage them, and protect yourself from their
agents. It runs much like research: your colonies produce intelligence points, and you spend
them on a queue of projects.

## Intelligence points

Intelligence facilities produce **intelligence points**. The race's Cunning characteristic, its
culture, the colony's mood and population and intelligence-boosting facilities change the
output, as for research. A Partnership treaty adds a share of your partner's points.

The points work on the same one-turn delay as research: what your colonies produce at the end of
one turn is spent at the end of the next, and points no project can use are lost. Your
intelligence pool starts the game empty.

If the game was set up without **Allow Intelligence Projects**, there is no intelligence at all:
the window says so and its controls do nothing.

## Projects

Each project has a cost in points and a target. When its progress reaches the cost, it runs.
There is no luck involved: an attack succeeds unless the target's counter-intelligence stops
it (see below). Every project that runs leaves the queue, whatever happened, unless **Repeat
Projects** is on; then it starts again from nothing.

The projects you can choose depend on your technology, and the window lists them in groups.
Between them they can do these things:

| Against | What projects can do |
|---|---|
| Ships | Damage a ship, drain its supplies, destroy its cargo, scramble its orders, or make it defect to you. |
| Planets | Destroy facilities, kill or add population, change conditions or value, anger the people, or incite a rebellion. |
| Treasuries and queues | Steal or destroy minerals, organics and radioactives; delete a research or intelligence project. |
| Knowledge | Report on an empire, its technology, its ships or a planet; map a system it has explored; steal a ship or unit design or a level of technology. |
| Politics | Read or block the messages between two empires, disrupt their trade, report their treaty, or fake a declaration of war between them. |
| Defense | Protect your empire against enemy projects. |

You can aim projects only at empires you are in contact with, friend or foe. Projects aimed at an empire you lose contact with are deleted, and when you are in contact with nobody at all, your whole queue is cleared, defense projects included. Depending on the project you
also pick a planet, a ship, a technology area or a third empire. Most projects also offer an
**Any** target, which lets your agents choose for you.

> An "Any" technology theft never works: your agents only pick areas where you are already ahead. Name the area you want to steal instead.

When an attack succeeds against you, you are told what happened. One time in five you are also
told who did it. When you defeat an attack, you always learn who sent it.

## Defense projects

A **defense project** stores points while it waits in your queue. When an enemy project against
you runs, your defense projects are used from the **bottom** of your queue upward. Each one adds
its stored points, multiplied by its strength, to your defense, and loses them. As soon as your
defense reaches the attack's cost, the attack is defeated. If all your defenses together fall
short, the attack goes ahead, and those defenses are still used up.

When a defense project's progress reaches its own cost, it runs: it finds one enemy project aimed
at you, among those your defense level can reach, and deletes it.

Because of the bottom-up rule, keep your defense projects at the end of your queue and let them
fill up there. Repeat is useful for them.

> Order matters within a turn. Empires process their intelligence in empire order. If your empire comes before the attacker's, your defenses have already received this turn's points when the attack strikes.

## The Intelligence window

The [Intelligence](window:intelligence) window opens from the **Intelligence** button of the
[Empires](window:empires) window (`F9`).

It is laid out like the [Research](research#the-research-window) window.

- The title strip shows the **Intelligence Points Available** this turn.
- The list fills the top of the window: the projects you can start, under their groups, with their cost. **Click a project** to add it. One that needs a target then asks for the empire, and after that for the planet, ship, technology area or third empire (or **Any**). Point at a project to read its description.
- Below the list, your current projects appear four at a time: each box shows the project, its target, when it will finish, the points it gets each turn and its progress in the small box under it. **Click a project** to cancel it; the game asks first while the Empire Option for it is on.
- `Projects 1-4`, `Projects 5-8` and `Projects 9-12` switch pages. `Repeat Projects`, `Divide Pts Evenly` and `Reorder Projects` work as in the [Research](research#the-research-queue) window. Divide Pts Evenly is on for a new empire.

## Intelligence advice

- A small, steady defense is cheap insurance once a neighbour grows hostile.
- Stealing designs shows you what an enemy fields before you meet it in battle.
- Sabotage is most useful against a planet you are about to invade, or a fleet you are about to fight.
- Computer players get angry at empires they catch spying on them (see [Diplomacy](diplomacy#how-computer-players-feel-about-you)).
