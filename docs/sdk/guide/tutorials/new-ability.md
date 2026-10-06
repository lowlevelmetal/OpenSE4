# A new ability with an effect

The game's abilities are a fixed list: what each does is written in the engine. A mod can
add its own: it **declares** a name, puts it on components (or facilities, hulls, system
types) like any other ability, and gives it its **effect** in a rules hook. In this tutorial
you read the example [mods/examples/new-ability](../../../../mods/examples/new-ability/):
**Field Repair**, carried by a new component, the **Mender Bay**. After each empire's repair
step, every one of its damaged ships that carries Field Repair mends that many components,
wherever it is. The chapters behind it are [Rules scripts](../rules-scripts.md) and
[rules.md](../../rules.md#abilities).

```sh
opense4-sdk new --from-example new-ability menders --id=me.menders
```

```text
new-ability/
  mod.toml              [rules] players_see_mod_data = true
  data/menders.toml     the ability's declaration and the Mender Bay
  scripts/menders.py    the effect
  tests/test_menders.py its tests
```

## 1. Declare the ability

In a data patch, [data/menders.toml](../../../../mods/examples/new-ability/data/menders.toml):

```toml
[[abilities.declare]]
name = "Field Repair"
combine = "sum"
```

`combine` says how several entries of the ability on one thing add up: `"sum"` (a ship
with two bays mends two components a turn), `"max"` or `"min"`. Any mod may now use the
name; two mods may declare the same name if they agree on how it combines. A name nobody
declares, or a misspelt one, stays an error ([Abilities](../data/abilities.md#declaring-new-abilities)).

## 2. A component that carries it

In the same file, the Mender Bay, written out in full so the mod fits any data set; its
ability is an entry of its abilities list:

```toml
[[components.add]]
name = "Mender Bay"

[components.add.set]
"Tonnage Space Taken" = 30
"Cost Minerals" = 80
"Vehicle Type" = "Ship\\Base"
"General Group" = "Repair"
"Family" = 7201
# ... and the other fields

[components.add.add]
abilities = [{ "Ability Type" = "Field Repair", "Ability Descr" = "Mends one damaged component a turn.", "Ability Val 1" = 1 }]
```

At this point the ability exists in the data and nothing reads it: designs can carry Mender
Bays, and nothing happens.

## 3. Give it an effect

[scripts/menders.py](../../../../mods/examples/new-ability/scripts/menders.py):

```python
from opense4 import rules

ABILITY = "Field Repair"


@rules.on("empire_end_of_turn", step="repair", when="after")
def field_repairs(game, empire, step, when, fx):
    mended = 0
    for raw in game.raw["vehicles"]:
        if raw["owner"] != empire.id or not raw["damage"]:
            continue
        crews = game.ability(raw["id"], ABILITY, "vehicle")
        if not crews:
            continue
        done = fx.repair(raw["id"], components=crews)
        if done:
            mended += done
            fx.log(empire, "Field crews mended {} component{} of {}.".format(done, "" if done == 1 else "s", raw["name"]),
                   title="Field repairs", location=raw["location"])
    if mended:
        empire.mod_data["mended"] = empire.mod_data.get("mended", 0) + mended
```

- **The moment.** `empire_end_of_turn` runs before and after each step of an empire's end of
  turn; `step="repair", when="after"` narrows it to just after the repair step, where docks
  and repair facilities have done their work ([rules.md](../../rules.md#hooks)).
- **Reading.** `game.raw["vehicles"]` gives every vehicle as a map, cheaper than the typed
  records in a loop over many things. `game.ability(id, name, "vehicle")` is the ability's
  value on a vehicle's design, its entries combined as declared (`sum`), and None when it
  carries none.
- **Changing.** `fx.repair` mends up to that many damaged components, in design order, and
  says how many it mended; `fx.log` tells the empire in its Log.
- **Remembering.** `empire.mod_data` is the mod's own data on the empire: saved with the
  game and checksummed. With `players_see_mod_data = true` in `mod.toml`, computer players
  see it on their own empire (`view.my.mod_data`).

The functions of a mod's scripts are registered when the game imports them, so the file
needs nothing else.

## 4. Test it

A hook is a plain function: call it with stand-ins that have just what it uses, and check
what it asked for. [tests/test_menders.py](../../../../mods/examples/new-ability/tests/test_menders.py):

```python
class Game:
    def __init__(self, vehicles, crews):
        self.raw = {"vehicles": vehicles}
        self.crews = crews

    def ability(self, thing, name, kind=None):
        return self.crews.get(thing)


class Effects:
    def __init__(self):
        self.repairs = []
        self.logs = []

    def repair(self, vehicle, components=None):
        self.repairs.append((vehicle, components))
        return min(components, 2)

    def log(self, empire, text, title="", category="misc", location=None):
        self.logs.append(text)


def test_damaged_ships_with_crews_are_mended():
    game = Game([vehicle(1, 0, 30), vehicle(2, 0, 0), vehicle(3, 0, 12), vehicle(4, 1, 50)], crews={1: 3, 2: 1, 4: 1})
    fx = Effects()
    empire = Empire(0)
    menders.field_repairs(game, empire, "repair", "after", fx)
    assert fx.repairs == [(1, 3)]                 # 2 is not damaged, 3 has no crew, 4 is not ours
    assert empire.mod_data == {"mended": 2}
```

Another test checks the data: the bay carries the ability, and the rules view knows how it
combines (`rules.aggregation("Field Repair") == "sum"`).

```sh
opense4-sdk check menders     # the declaration, the component, and the scripts import
opense4-sdk test menders      # the tests, then a game with the mod's rules on
```

`opense4-sdk test` plays a short game between two classic AIs with the mod's rules on, and
fails if `field_repairs` ever raises; the line it prints says so:

```text
Games (seed 1, 10 turns, simultaneous):
  ok    example.new-ability's rules in a game of the classic AI: 10 turns, no failures (0.4 s)
```

## 5. Going further

- **Put it elsewhere.** A facility with Field Repair would make a whole system a repair
  yard; `game.ability(colony, ...)` reads a colony's facilities. Decide in the hook what each
  carrier means.
- **Teach computer players.** Pioneer's parts are chosen by ability: a player can look for
  `component.ability_value("Field Repair")` like any other.
- **Keep it cheap.** The hook runs for every empire every turn: it skips undamaged ships
  first, and reads only maps ([Budgets and performance](../performance.md)).
