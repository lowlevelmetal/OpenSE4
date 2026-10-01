---
windows: stellar-manipulation
---
# Events and stellar manipulation

The galaxy is not a quiet place. Random events strike ships and planets, some systems are
dangerous by nature, and advanced empires can reshape the stars themselves. This chapter covers
all three.

## Random events

Random events can help or harm. A ship may be damaged, lose supplies or gain experience, or be flung
to a random place far away (leaving its fleet); a planet
may gain or lose value, suffer a plague, a riot or a rebellion, or see its conditions change; in
the worst cases a star explodes or a planet is destroyed.

How often they happen is set when the game is created:

| Event Frequency | Chance of an event each turn (stock settings) |
|---|---|
| None | No events |
| Low | 5 % |
| Medium | 10 % |
| High | 25 % |

The chance is for the **whole galaxy**, not for each empire: at most one new event a turn, which
strikes a random target anywhere. There are no events at all during the first two years.
**Maximum Event Severity** (Low, Medium, High or Catastrophic) keeps the worst events out of the
game.

Some events give a warning first: the start message arrives at once, and the event strikes some
turns later. A catastrophe announced that way may take 10 to 30 turns to arrive, enough time to
evacuate.

Lucky races are hit less often, by good and bad events alike. Your homeworld's own location is
safe from the most severe planet events.

## Hazards in space

Some places are dangerous every turn:

| Hazard | What it does |
|---|---|
| Damaging storm | Each time a ship flies into its sector, an even chance of damage to every ship entering; the ships then stop for the turn. Ships that stay inside are not harmed. |
| Turbulent warp point | Each jump through it has an even chance of damaging every jumping ship, which then stops for the turn. |
| Black hole | Pulls every ship and unit group in the system toward the centre each turn, and heavily damages whatever is in the centre sector. |
| Random currents | Push every ship and unit group in the system a few sectors toward a random point each turn. |
| Nebula, obscuring storm | Hide what is inside them from weak sensors. |
| Shield-disrupting storm or system | Weakens every ship's shields in battles fought there. |
| Sensor interference | Lowers every ship's chance to hit in battles fought there. |

Damage from storms, warp points and black holes goes straight to the components, armor first;
shields do not help. Your ships route around damaging storms when they can.

## Stellar manipulation

Ships with special components can change the galaxy itself: make and destroy planets, stars,
storms, nebulae and black holes, and open and close warp points. Select the ship and press `B` to
open the [Stellar Manipulation](window:stellar-manipulation) window. It shows the ship's sector
and one button per action. A button is enabled only when the ship has the right component and the
action is possible right now; hover over it to see what it needs and what it will do, with an
animation of the effect. For Open Warp Point, the window closes and you pick a sector of the
destination system on the map.

The common rules:

- The ship must be **in the target's sector**, with movement left (none is used up), enough supplies for the component, and **not cloaked**.
- No visible enemy ship, base or colony may be in the target sector, except for Destroy Planet.
- Some facilities and components block a manipulation in their whole system, whoever owns them (planet destruction only in their own sector).
- Some components are used up by the action.

| Action | Needs | Result |
|---|---|---|
| Create Planet | An uncolonized asteroid field, and a star in the system. | The asteroid field becomes a planet of random type and atmosphere, up to the component's size. |
| Destroy Planet | A planet no larger than the component allows. | The planet becomes an asteroid field. Any colony on it is lost. |
| Create Star | A normal system with no star. | A new star in the ship's sector. |
| Destroy Star | A star. | A shockwave (below). |
| Open Warp Point | A target system within the component's range, not yet linked; fewer than ten warp points in each system. | A new pair of warp points, from this sector to the edge of the target system. You pick the target on the map. |
| Close Warp Point | A warp point. | Both ends of the link disappear. |
| Create Storm | Nothing more. | A storm with one random effect: it hides, damages or disrupts shields. |
| Destroy Storm | A storm. | The storm disappears. |
| Create Nebulae | A star. | A shockwave, then the system becomes a nebula that hides everything inside. |
| Destroy Nebulae | A nebula system. | It becomes a normal system. |
| Create Black Hole | A star. | A shockwave, then the system becomes a black hole. |
| Destroy Black Hole | A black hole system. | It becomes a normal system. |
| Construct | A star, and enough of the right components on ships in the sector. | A huge constructed world with optimal conditions and high value. The star and the building ships are used up. |

A **shockwave** wipes out the system: every planet and asteroid field becomes a bare asteroid field
(all colonies on them are lost), and every star, storm, ship, base and unit group in the system is
destroyed, **the ship that caused it included**. Only the warp points survive. Every action asks
you to confirm it first, while the Empire Option *Confirm stellar manipulation* is on (the
default).

> Destroying stars and planets angers every computer player that has something in the system. Use it as a last resort, or where nobody is watching.

The window adds the action at the top of the ship's orders (for a ship in a fleet, the fleet's),
so it is the next thing the ship does: at once in a turn-based game, during the turn's processing
in a simultaneous one.
