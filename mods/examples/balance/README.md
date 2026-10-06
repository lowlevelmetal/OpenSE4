# Measured Pace: a balance mod of data patches

An example mod of the OpenSE4 SDK: a balance mod made only of data patches, over several
tables of the classic data set. The tutorial
[A first balance mod](../../../docs/sdk/guide/tutorials/balance-mod.md) explains every
line.

| File | Changes |
|---|---|
| `data/10-technology.toml` | Propulsion research costs less; the racial tech areas of `Racial Area` 3 are removed with `cascade = true`, and with them everything that needs them |
| `data/20-ships.toml` | Every seeking weapon reloads every other turn (`match` and `all`); Ion Engine I gains an ability (a list entry added); satellite hulls cost no radioactives |
| `data/30-game.toml` | Space battles last 20 combat turns (Settings); the computer players' anger falls faster (an AI table, `files = "default"`); six new system names (a name list) |

The numbers are invented for the example. The mod names a few records of the classic data
set (`Propulsion`, `Ion Engine I`) and matches others by what they hold, so it is meant for
that data set: on another one, `opense4-sdk check` lists each record it cannot find, with
the file and line of the patch.

```sh
opense4-sdk check mods/examples/balance          # applies the patches to your installed game
opense4-sdk test mods/examples/balance           # tests/test_balance.py on the patched data
opense4-sdk dump mods/examples/balance --out=dump   # the data files as the game reads them with it
```

Removing the temporal tech areas without `cascade` would be refused: `check` would list
every component, facility, tech area and AI research row that still needs them, each with
where it is.
