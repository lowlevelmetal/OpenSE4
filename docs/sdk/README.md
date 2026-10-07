# Modding OpenSE4

OpenSE4 can be modded at several levels: pictures and sounds, the data files, computer
players in Python, and rules scripts that change how the game plays. Mods are layered over
your installed copy of the original and never change it. This is the index of the modding
SDK's documentation: a guide to read in order, tutorials to follow, and reference pages to
look things up in. The plan behind the SDK is [docs/MODDING_SDK.md](../MODDING_SDK.md).

## The modder's guide

| Chapter | What it covers |
|---|---|
| [Getting started](guide/getting-started.md) | What a mod is, the `opense4-sdk` tool, where mods live, load order, enabling and sharing a mod |
| [The data files](guide/data/README.md) | How OpenSE4 reads the data files, how patches change them, and a chapter per table group (below) |
| [Pictures, sounds and music](guide/assets.md) | Picture kinds and their classic sizes, PNG and BMP, larger pictures, design pictures, sounds and music, race style folders |
| [Computer players](guide/computer-players.md) | From a minimal player to a full one: the view, queries, commands, memory, notes, the classic AI as a library |
| [Budgets and performance](guide/performance.md) | What a player may spend in the game's runtime, what things cost, and how to stay fast |
| [Testing and measuring](guide/testing-and-measuring.md) | `opense4-sdk test`, the arena, external bots and the training environment |
| [Rules scripts](guide/rules-scripts.md) | Hooks, effects, mod data, declared abilities, orders, events, options, victory conditions, scenarios and data generators, built up from the examples |
| [Multiplayer, saved games and identity](guide/multiplayer-and-saves.md) | What "game-affecting" means, a mod's identity, network and e-mail games, saved games, saving for the original |
| [Troubleshooting](guide/troubleshooting.md) | Common errors, what they mean and how to fix them |

### The data files, table by table

| Chapter | Tables |
|---|---|
| [Components and weapon mounts](guide/data/components.md) | `Components.txt`, `CompEnhancement.txt` |
| [Hulls](guide/data/hulls.md) | `VehicleSize.txt`, and how a design's figures follow from hull and components |
| [Facilities](guide/data/facilities.md) | `Facility.txt` |
| [Abilities](guide/data/abilities.md) | Every ability name, what its values mean, how entries combine; declaring new ones |
| [Technology](guide/data/techs.md) | `TechArea.txt` |
| [Races, traits, cultures and happiness](guide/data/races.md) | Race files, `RacialTraits.txt`, `Cultures.txt`, `Happiness.txt` |
| [The galaxy](guide/data/galaxy.md) | `PlanetSize.txt`, `SectType.txt`, `StellarAbilityTypes.txt`, `SystemTypes.txt`, `QuadrantTypes.txt` |
| [Events](guide/data/events.md) | `Events.txt` |
| [Intelligence projects](guide/data/intel.md) | `IntelProjects.txt` |
| [Combat strategies and formations](guide/data/combat.md) | `DefaultStrategies.txt`, `Formations.txt` |
| [Settings and lists](guide/data/settings.md) | `Settings.txt`, the name lists, design and colony types, repair priorities, design names |
| [The computer players' tables](guide/data/ai-tables.md) | `Ai/` and the race folders' `*_AI_*.txt` |

### Tutorials

| Tutorial | You make | Example |
|---|---|---|
| [A first picture mod](guide/tutorials/picture-mod.md) | A ship picture any design can wear, as PNGs with transparency | (made in the tutorial) |
| [A first new unit](guide/tutorials/new-unit.md) | A hull with its own pictures and a component to fill it | [mods/examples/new-hull](../../mods/examples/new-hull/) |
| [A first balance mod](guide/tutorials/balance-mod.md) | Data patches across several tables, a removal with `cascade` | [mods/examples/balance](../../mods/examples/balance/) |
| [A first computer player](guide/tutorials/computer-player.md) | A player that explores and settles, then a reading of a complete one | [mods/examples/small-ai](../../mods/examples/small-ai/), [mods/examples/classic-ai-research](../../mods/examples/classic-ai-research/) |
| [A weapon line from a generator](guide/tutorials/weapon-line.md) | A technology and six levels of a weapon, built in Python | [mods/examples/weapon-line](../../mods/examples/weapon-line/) |
| [A new ability with an effect](guide/tutorials/new-ability.md) | An ability of your own and the hook that makes it work | [mods/examples/new-ability](../../mods/examples/new-ability/) |
| [A scenario](guide/tutorials/scenario.md) | A setup with objectives and actions | [mods/examples/scenario](../../mods/examples/scenario/) |

The example mods are listed in [mods/examples/README.md](../../mods/examples/README.md);
`opense4-sdk new --from-example <name> <folder>` copies one to start from.

**Interface extensions** (buttons for mod orders, panels in reports, columns in lists,
pages in the Empires window, text and key bindings) are described in
[interface.md](interface.md).

## Reference

| Page | What it is |
|---|---|
| [Mod packages and data patches](packages-and-data.md) | The package, the manifest, load order, identity, assets and their formats, the patch format, `opense4-sdk` |
| [The script runtime](runtime.md) | The Python inside the game: what it has and lacks, its limits, its errors |
| [The `opense4` Python package](python-api.md) | Players, the view, commands, random numbers, testing, external bots, costs |
| [The API reference](reference/README.md) | Every module, class and function of the package, generated from its source |
| [The script view](view.md) | Every field of the view, the rules view and the queries |
| [Script commands](commands.md) | Every command, order and tactical order, with their fields |
| [The computer-player protocol](ai-protocol.md) | Sessions, requests, responses, services, budgets, the journal, external connections |
| [Bots, the arena and the mod tools](bots-and-arena.md) | External bots, `opense4-sdk arena`, the training environment, `test` and `run` |
| [Rules scripts](rules.md) | Hooks, effects, mod data, orders, events, intelligence projects, options, victory conditions, scenarios, generators |

The API reference is made by `python3 tools/gen_sdk_reference.py` from `python/opense4`;
the SDK's tests fail when it is out of date.

The examples in these pages, in [MODDING_SDK.md](../MODDING_SDK.md) and in the mods'
READMEs are checked by the SDK's tests too. A `toml` block must read as the file it shows (a
manifest, a data patch, a `ui/` or `text/` file, a scenario) and a `json` block as JSON. A
`python` block is a complete module: it must run with the `opense4` package on the path, and
the computer players it defines play a turn's planning calls on a test game's view
(`tests/sdk/python/check_doc_snippets.py`). A block marked `python no-run` needs the game or
a mod's other modules and must only compile; one marked `fragment` (after its language) is
an excerpt and is not checked.
