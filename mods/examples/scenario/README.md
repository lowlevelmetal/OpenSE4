# Frontier Charter: a scenario

An example mod of the OpenSE4 SDK: a scenario, `scenarios/frontier.toml`, with its setup
(a small quadrant, three empires), an option of the mod's own that it sets, and two
objectives: an empire's second colony earns a grant (an action in `scripts/frontier.py`),
and the first to hold eight colonies wins. The tutorial
[A scenario](../../../docs/sdk/guide/tutorials/scenario.md) explains it.

```sh
opense4-sdk check mods/examples/scenario     # the scenario reads; its options and actions are the mod's
opense4-sdk test mods/examples/scenario --turns=60   # its tests, then the scenario played for 60 turns
```

`opense4-sdk test` starts the scenario as the game does (`sdk::startScenario`) and lets the
computer play every empire, its human one too, then says which objectives were met and how
the game ended.
