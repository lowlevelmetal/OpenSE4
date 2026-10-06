# Field Repair: a new ability with an effect

An example mod of the OpenSE4 SDK: a new ability, **Field Repair**, declared in a data
patch, carried by a new component, the **Mender Bay**, and given its effect by a rules
hook. The tutorial [A new ability with an effect](../../../docs/sdk/guide/tutorials/new-ability.md)
explains it.

```text
new-ability/
  mod.toml            [rules] players_see_mod_data: computer players see what was mended
  data/menders.toml   [[abilities.declare]] Field Repair (combine "sum"), and the Mender Bay
  scripts/menders.py  the effect: after each empire's repair step, ships mend components
  tests/test_menders.py the hook with stand-ins for the game and the effects
```

After each empire's repair step every one of its damaged vehicles that carries Field
Repair mends that many components (the value, summed over its bays), wherever it is. The
empire's Log says so, and its mod data counts what was mended in the game.

```sh
opense4-sdk check mods/examples/new-ability
opense4-sdk test mods/examples/new-ability    # its tests, then a game with its rules on
```
