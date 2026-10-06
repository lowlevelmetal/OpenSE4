# Ember Lances: a weapon line from a data generator

An example mod of the OpenSE4 SDK: a Python data generator, `data/ember_line.py`, that
builds a technology, **Ember Optics** (six levels), and a weapon for each level, **Ember
Lance I** to **VI**, each stronger and longer-ranged than the one before. The tutorial
[A weapon line from a generator](../../../docs/sdk/guide/tutorials/weapon-line.md) explains it.

- When the data loads, the game runs `generate()` and applies the dict it returns exactly
  as a `.toml` patch ([rules.md](../../../docs/sdk/rules.md), "Data generators").
- The records are written out in full, so the mod fits any data set. The line is one
  family, in level order, so a design's Upgrade replaces a lance by the latest one.
- The weapon's sound, `assets/Sounds/ember.wav`, is the mod's own (a tone made by
  [tools/make_example_assets.py](../../../tools/make_example_assets.py)); the weapons name it
  with `Weapon Sound`.

```sh
opense4-sdk check mods/examples/weapon-line
opense4-sdk test mods/examples/weapon-line
opense4-sdk dump mods/examples/weapon-line --out=dump    # Data/Components.txt shows the six lances
```
