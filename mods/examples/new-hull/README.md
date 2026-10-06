# Wren Courier: a new hull and a new component

An example mod of the OpenSE4 SDK. It adds a ship hull, the **Wren Courier**, with
pictures of its own, and a component, the **Wren Cargo Rack**, that it is built around.
The tutorial [A first new unit](../../../docs/sdk/guide/tutorials/new-unit.md) builds it
step by step.

```text
new-hull/
  mod.toml                                                    the manifest
  data/wren.toml                                              the hull and the component, as a data patch
  assets/Pictures/RaceGeneric/Generic_Mini_WrenCourier.png    36x36, with transparency
  assets/Pictures/RaceGeneric/Generic_Portrait_WrenCourier.png  128x128
  tests/test_wren.py                                          its tests
```

- Both records are written out in full (no `copy_from`), with no technology, so the mod
  fits any data set and every empire can build a courier from its first turn.
- The hull's `Primary Bitmap Name` is `WrenCourier`: the game finds the two pictures by
  that name for every race. A race's own `<Race>_Mini_WrenCourier` in its style folder
  would win for that race.
- Components share one picture sheet, so the rack borrows cell 1 of it (`Pic Num`).
- The pictures are PNGs with an alpha channel, drawn by
  [tools/make_example_assets.py](../../../tools/make_example_assets.py).

```sh
opense4-sdk check mods/examples/new-hull
opense4-sdk test mods/examples/new-hull
opense4-sdk run mods/examples/new-hull -- --quick-start=Terran    # then open the ship designer
```

The tests read the patched data through the rules view: the hull and rack are there with
their figures, and the smallest crew parts and engine of a new empire fit in the hull with
the two racks it asks for (`Requirement Pct Cargo` is a least share of its space).
