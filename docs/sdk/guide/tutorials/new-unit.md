# A first new unit

In this tutorial you add a ship hull with pictures of its own, and a component to build it
around. The finished mod is the example [mods/examples/new-hull](../../../../mods/examples/new-hull/):
the **Wren Courier**, a small cargo hull, and the **Wren Cargo Rack**. The chapters behind it
are [Hulls](../data/hulls.md), [Components and weapon mounts](../data/components.md) and
[Pictures, sounds and music](../assets.md).

```sh
opense4-sdk new --from-example new-hull wren --id=me.wren    # the finished mod, to compare with
```

## 1. Make the mod

```sh
opense4-sdk new data wren --id=me.wren --name="Wren Courier"
```

Rename `data/changes.toml` to `data/wren.toml`.

## 2. The hull

A hull is a record of `VehicleSize.txt`, patch table `vehicle_sizes`. You can start from a
copy of a hull of your data set (`copy_from`) and change a few fields, or write it out in
full. Written out in full, the mod needs no record of the player's game and fits any data
set, so that is what the example does:

```toml
[[vehicle_sizes.add]]
name = "Wren Courier"

[vehicle_sizes.add.set]
# What it is: a ship hull, and how lists and reports name it.
"Vehicle Type" = "Ship"
"Short Name" = "Courier"
"Code" = "WC"
"Description" = "A slim courier hull with two cargo pods. Small, cheap and quick to build."
# Its pictures. The game looks for Generic_Mini_WrenCourier and Generic_Portrait_WrenCourier
# (assets/Pictures/RaceGeneric/), or <Race>_Mini_WrenCourier in a race's folder.
"Primary Bitmap Name" = "WrenCourier"
"Alternate Bitmap Name" = "WrenCourier"
# Its space for components (kT), and its price.
"Tonnage" = 120
"Cost Radioactives" = 10
"Cost Minerals" = 120
"Cost Organics" = 0
# Engines: one engine for each point of movement, four at most.
"Requirement Max Engines" = 4
"Requirement Uses Engines" = true
"Engines Per Move" = 1
# The designer's rules for a crewed ship.
"Requirement Min Crew Quarters" = 1
"Requirement Min Life Support" = 1
"Requirement Can Have Aux Con" = true
"Requirement Must Have Bridge" = true
# At least 30 % of its space must hold cargo; no share for bays or colony modules.
"Requirement Pct Cargo" = 30
"Requirement Pct Colony Mods" = 0
"Requirement Pct Fighter Bays" = 0
# No technology needed, and no abilities of its own (empty lists).
"Number of Tech Req" = 0
"Number of Abilities" = 0
```

The `Requirement` fields are the designer's rules for this hull: a design must have a
bridge, at least one life support and crew quarters, at most four engines, and cargo in at
least 30 % of its space. [Hulls](../data/hulls.md#how-the-fields-work-together) lists every
rule and how a design's figures follow from its parts.

## 3. The component

A component is a record of `Components.txt`, patch table `components`. The rack holds cargo:
its effect is the ability `Cargo Storage`, an entry of its abilities list, added with the
entry's own field names:

```toml
[[components.add]]
name = "Wren Cargo Rack"

[components.add.set]
"Description" = "Folding racks for a small hull's hold."
# Components share one picture sheet, so a new component borrows a cell of it: Pic Num 1
# is a cell of every data set's sheet (docs/sdk/guide/data/components.md, "Pictures").
"Pic Num" = 1
# Its size and strength, and its price.
"Tonnage Structure" = 15
"Tonnage Space Taken" = 20
"Cost Radioactives" = 0
"Cost Organics" = 10
"Cost Minerals" = 40
# Where it may go, and how the designer groups it.
"Vehicle Type" = "Ship\\Base"
"General Group" = "Cargo"
"Custom Group" = 0
"Restrictions" = "None"
# A family of its own: upgrades replace a component by a later one of the same family.
"Family" = 7101
"Roman Numeral" = 1
# It uses no supply, needs no technology, and is no weapon.
"Supply Amount Used" = 0
"Number of Tech Req" = 0
"Weapon Type" = "None"

[components.add.add]
abilities = [{ "Ability Type" = "Cargo Storage", "Ability Descr" = "Holds cargo.", "Ability Val 1" = 60 }]
```

The hull asks for 30 % of its 120 kT in cargo, 36 kT, so a courier needs two racks (40 kT).
With a bridge, life support and crew quarters of 10 kT each, that leaves room for four
engines. A design that breaks a hull's rule
cannot be created; the designer, and the `design_figures` query of computer players, say
which rule.

**Component pictures** are cells of one picture sheet, `Components.bmp`, chosen by
`Pic Num`; the sheet has a fixed number of cells, so a new component borrows one. Number 1
exists in every data set ([Components](../data/components.md#pictures)).

## 4. The pictures

The game looks for the hull's pictures by its bitmap name: for every race,
`Pictures/RaceGeneric/Generic_Mini_WrenCourier` (36x36) and
`Generic_Portrait_WrenCourier` (128x128). Draw them facing up, as PNGs with transparency,
and put them in the mod:

```text
wren/assets/Pictures/RaceGeneric/Generic_Mini_WrenCourier.png
wren/assets/Pictures/RaceGeneric/Generic_Portrait_WrenCourier.png
```

The example's are drawn by [tools/make_example_assets.py](../../../../tools/make_example_assets.py)
from a few polygons and ellipses. A race of yours can have its own:
`Pictures/Races/<Style>/<Style>_Mini_WrenCourier.png` wins for that race.

## 5. Check, test, play

```sh
opense4-sdk check wren
```

`check` applies the patch to your installed game and reports every problem: a misspelt field
(`components records have no field 'Tonage Space Taken' (check the spelling)`), a value that
should be a number, an unknown ability, a missing hull picture.

A test reads the patched data as computer players do. The example's
[tests/test_wren.py](../../../../mods/examples/new-hull/tests/test_wren.py) checks the hull and
rack, and that the smallest bridge, life support, crew quarters and engine a new empire has
fit in a courier with racks:

```python
from opense4 import rules, testing


def test_the_hull_is_in_the_data_set():
    hull = rules.Rules(testing.game_rules()).hull_named("Wren Courier")
    assert hull is not None and hull.type == "ship" and hull.tonnage == 120
```

```sh
opense4-sdk test wren
opense4-sdk run wren -- --quick-start=Terran        # then open the ship designer
```

In the designer, the Wren Courier is among the hulls, with your pictures, and the Wren
Cargo Rack among the components.

## 6. Going further

- **Give it a technology.** Add requirement entries:
  `add = { requirements = [{ "Tech Area Req" = "<a tech area>", "Tech Level Req" = 2 }] }`
  ([Technology](../data/techs.md)).
- **A family of racks.** Add `Wren Cargo Rack II` with `copy_from = "Wren Cargo Rack"`,
  `after = "Wren Cargo Rack"`, a higher `Roman Numeral` and more `Cargo Storage`: an Upgrade
  replaces a component by the last of its family in file order. A
  [generator](weapon-line.md) writes such lines for you.
- **Abilities on the hull itself**, such as a movement bonus, go in the hull's own abilities
  list ([Abilities](../data/abilities.md)).
- **A new ability** that no rule reads yet needs a rules hook: [A new ability with an effect](new-ability.md).
