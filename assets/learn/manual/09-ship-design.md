---
windows: designs, create-design
---
# Ship design

You design every ship, base and unit you build. A **design** is a hull filled with components.
This chapter explains the rules a design must meet, what its numbers mean, and how to manage
your designs as your technology improves.

## Hulls and components

A **hull** (also called a ship size) sets the class of the vehicle and how much fits in it:

| Class | What it is |
|---|---|
| Ship | Moves, warps and fights. Built at a space yard. |
| Base | A ship that cannot move. It never runs out of supply and pays half the upkeep. Built at a space yard. |
| Fighter | A small craft launched from carriers or planets. |
| Satellite | A stationary gun platform deployed in space. |
| Mine | A hidden explosive laid in a sector. |
| Drone | A guided craft that rams its target. |
| Troop | A ground unit for invading planets. |
| Weapon Platform | A planet's gun, stored in its cargo. |

Ships and bases are designed on the **Ship Designs** tab; the other classes are **units** and
use the **Unit Designs** tab (see [Units](units)).

Each hull has a **tonnage**, in kT. Each component takes some of that space. A design is legal
only while its components fit in the hull. Components come in groups: weapons, armor, shields,
engines, sensors, vehicle control, supply, cargo, launch bays, colony modules, construction and
more. The Help window's [Components](help:components) and [Ship Sizes](help:ship-sizes) tabs
describe each one you know.

## Design rules

The designer checks these rules and lists every one that fails. A design with any warning cannot
be created.

- The components must fit in the hull's tonnage.
- You must have researched the hull, every component and every mount.
- Hulls that need control must have **exactly one bridge**, and at most one **auxiliary control** (a backup bridge).
- Larger hulls need a minimum number of **life support** and **crew quarters** components.
- A **master computer** replaces the bridge, life support and crew quarters altogether.
- Some hulls take no engines, and others take only a limited number.
- Some components are limited per vehicle: for example, only one of a kind, counting every version of it.
- At most one **space yard** component.
- Special-purpose hulls need a share of their tonnage in one kind of component: carriers in fighter bays, colony ships in colony modules, transports in cargo space.

> If Create Design refuses the design, read the Warnings box: it names every rule that fails.

## Movement, supply and cost

- **Movement**: add up the movement of the engines, and divide by the hull's engines-per-move value. If you mix engine types, the smallest speed bonus among them counts, so mixing a fast engine with a standard one wastes the fast one. Your race's speed traits add to the total.
- A ship that loses its bridge (and has no auxiliary control), or all its life support, or all its crew quarters, has its movement halved for each missing item, down to 1. A master computer prevents this.
- **Supply capacity** comes from supply storage components, and engines carry some too. Moving and firing use supply (see [Supply, cargo and repair](logistics#supply)).
- **Cost** is the hull's cost plus every component's cost. It decides build time, maintenance and scrap value.

## Weapon mounts

A **mount** changes a weapon component: more damage, longer range or better aim, usually for more
size and cost. Larger mounts need larger hulls. In the designer, choose a mount with
`Weap Mount`; it applies to the weapons you add after that. A mounted component shows the mount's
letter. One design can mix mounts.

The [Weapons Report](help:weapons) shows what each weapon does with each mount.

## Combat numbers worth knowing

These rules from [Combat](combat) matter when you design:

- The chance to hit falls by 10 points for every square between the ships. Components that raise your attack or defense rating add to it; two components of the same family do not stack, but different families do.
- Shields absorb damage first, then **armor** components are destroyed before any other component. Until the armor is gone, the rest of the ship is safe.
- Components are destroyed whole. A big hit can destroy several at once, and damage too small to destroy the next component waits for the next hit.
- A ship without supplies cannot fire its weapons and has no shields.

> A good warship balances weapons, shields and armor, with enough supply for a long trip. Test a new design in the Combat Simulator before you build a fleet of it.

## Prototypes, editing and upgrading

- A new design is a **prototype** until you build one. You can **Edit** a prototype freely, as long as it is not in a construction queue.
- Once a design has been built, it cannot change. Use **Copy** to start a new design from it.
- **Upgrade** opens the designer on a copy of the design in which every component is replaced by the newest one of its family that you have researched, the one listed last in the game's component list (so a newer line of engines replaces an older line's best). Create it as a new design; the old one stays. Then build the new design, or retrofit old ships to it (see [Supply, cargo and repair](logistics#scrapping-and-mothballing)).
- **Make Obsolete** marks a design you no longer want; **Hide Obsolete** hides such designs from your lists. An obsolete design is removed for good once no vehicle of it exists, it is in no queue and no other empire has seen it for 50 turns.
- Design names must be unique among all the designs in the game, every empire's included.

Each design has a **design type**, such as attack ship, colony ship or transport. The type is a
label: your ministers use it to find ships for each job. Each design also has a **default
strategy** for combat, which a fleet's own strategy overrides.

## Enemy designs

You learn other empires' designs by fighting them, by scanning their ships with a long range
scanner, and through intelligence. They appear on the **Enemy Ship Dsgn** and **Enemy Unit Dsgn**
tabs. You forget an enemy design you have not seen for 50 turns.

## The Designs window

[Designs](window:designs) (`F3`) lists your designs on four tabs: **Ship Designs**, **Unit
Designs**, **Enemy Ship Dsgn** and **Enemy Unit Dsgn**. Your designs are grouped under their
design types (enemy designs under their empire's name), the groups and the designs in each in
alphabetical order; each row has a lamp (green for the selected design), the design's picture,
its name and hull, and **Prototype** while it has never been built. Select a design to see its
size, design type and creation date beside its picture, then its cost and maintenance cost, its
movement, shields, cargo space and supply capacity, *(Insufficient technology)* when you could not
build it yourself, and its components as a grid of icons, eight to a row (click one for its
report; the big arrows above the grid scroll it when there are more than 32). Obsolete designs
that no vehicle or queue uses any more are deleted by themselves, as the note under the list says.

| Button | What it does |
|---|---|
| `Create` | Ask which kind of vehicle to design (ship, base, satellite, ...), then open the designer for it. |
| `Copy` | Open the designer with a copy of the selected design. |
| `Edit` | Change a prototype that has never been built and is in no queue. |
| `Upgrade` | Make a new design with the newest components. |
| `Make Obsolete` | Mark the design obsolete, or current again. |
| `Hide Obsolete` | A check box: show or hide obsolete designs. |
| `Stats\Strategy` | A check box: show the design's service record (constructed, in service, lost, scrapped, enemy tonnage destroyed) and its default strategy, which you can change there. The enemy tabs switch it off. |
| `Simulator` | Open the [Combat Simulator](window:combat-simulator) (see [Battle windows](combat-windows#the-combat-simulator)). |

## The Create Design window

[Create Design](window:create-design) is the designer. It is titled after the kind of vehicle
you chose (**Ship Design**, **Base Design**, ...) and starts without a size, a design type or a
name: their boxes read *Size*, *Design Type* and *Design Name*.

1. Press the arrow button beside **Size** and pick the hull. Pick a **Design Type** the same
   way, and type a **Design Name**, or press the arrow button beside it to pick one from your
   race's list.
2. **Click** a component in **Components Available** (a grid, three to a row) to add it. The
   design's components appear in the strip of **Components on Design**; **click** one there to
   remove it, and use the strip's arrows when it is longer than the window. **Right-click** a
   component for its full report. Point at a component to see it in **Component Details**.
3. Watch the figures at the top right (space used, total cost, movement, shields, cargo space,
   supply capacity) and the **Warnings** box at the bottom left, which lists every rule the
   design breaks, a missing size or name too, each after a red dot.
4. Press `Create Design` when the Warnings box has nothing left; `Cancel` closes the designer.

With mods that add ship pictures (see [Mods](settings#mods)), a small arrow appears in the
corner of the design's picture: it lists the hull's picture and the mods' pictures, and the
one you pick becomes the design's own. Copies and upgrades of the design keep it.

The buttons on the right help:

| Button | What it does |
|---|---|
| `Comp Type` | Show only one group of components. |
| `Weap Mount` | Choose the mount for the weapons you add next (once a size is chosen). |
| `To Hit Modifiers` | A check box: the figures show the design's **Offense Bonus** and **Defense Bonus** (its combat to-hit bonuses) instead of shields, cargo space and supply capacity. It is kept with your empire. |
| `Condensed View` | A check box: show identical components (the same component and mount) once in the strip, with their count. It is kept with your empire. |
| `Only Latest` | A check box: of components of one family listed one after another, show only the last (the newest). It is an Empire Options setting, kept as you leave it. |
| `Weapons Report` | Compare weapons, with the chosen mount. |
