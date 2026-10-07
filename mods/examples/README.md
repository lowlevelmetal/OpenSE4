# Example mods

Small, complete mods of OpenSE4's modding SDK, each written to be read and changed. The
modder's guide ([docs/sdk/README.md](../../docs/sdk/README.md)) teaches with them.
Everything in them is invented for OpenSE4: the records, the numbers, the pictures and
the sound (drawn and made by [tools/make_example_assets.py](../../tools/make_example_assets.py)).

| Example | What it shows | Tier | Guide |
|---|---|---|---|
| [new-hull](new-hull/) | A new hull with its own pictures, and a new component, written out in full | data, assets | [A first new unit](../../docs/sdk/guide/tutorials/new-unit.md) |
| [balance](balance/) | A balance mod of data patches over several tables of the classic data set, with a removal by `cascade` | data | [A first balance mod](../../docs/sdk/guide/tutorials/balance-mod.md) |
| [classic-ai-research](classic-ai-research/) | The classic computer player with a research planner of its own (`ai.builtin` for the rest) | AI | [Computer players](../../docs/sdk/guide/computer-players.md) |
| [small-ai](small-ai/) | Pioneer: a complete small computer player of its own (economy, expansion, designs, defence) | AI | [A first computer player](../../docs/sdk/guide/tutorials/computer-player.md) |
| [weapon-line](weapon-line/) | A data generator: a technology and six levels of a weapon, built in Python | data (generator), assets | [A weapon line from a generator](../../docs/sdk/guide/tutorials/weapon-line.md) |
| [new-ability](new-ability/) | A new ability: declared in a patch, carried by a component, given its effect by a rules hook | data, rules | [A new ability with an effect](../../docs/sdk/guide/tutorials/new-ability.md) |
| [scenario](scenario/) | A scenario: a setup, objectives, an option of the mod's own and an objective's action | rules | [A scenario](../../docs/sdk/guide/tutorials/scenario.md) |

## Trying one

```sh
opense4-sdk check mods/examples/new-hull            # against your installed game
opense4-sdk test mods/examples/small-ai             # its tests/, then a short game
opense4-sdk run mods/examples/small-ai -- --quick-start=Terran
opense4-sdk new --from-example small-ai my-ai       # a copy to change, under a mod id of yours
```

In a release the examples are in `sdk/examples` beside `opense4-sdk`, and `new
--from-example` finds them there. Unlike the mods that come with OpenSE4 (in `mods/`
beside the programs), the examples are not in the game's Mods window until you copy one
into your mods folder (`Mods` in OpenSE4's user folder); `opense4 --mod=mods/examples/new-hull`
plays with one for a single run.

For a full-strength computer player written with the SDK, read
[Hegemon](../hegemon/README.md), which comes with OpenSE4: its design, its tuning and how
it was measured.

## Their tests

Each example has a `tests/` folder that `opense4-sdk test` runs in the game's own Python,
then plays its computer players, rules and scenarios for a few turns. The SDK's own tests
([tests/sdk/test_sdk_guide.cpp](../../tests/sdk/test_sdk_guide.cpp)) run `check` and
`test` on every example on OpenSE4's test fixtures in continuous integration, and on your
installed game with `OPENSE4_CLASSIC_DATA=auto`. `balance` names records of the classic
data set, so on the fixtures it only shows that its patches read.
