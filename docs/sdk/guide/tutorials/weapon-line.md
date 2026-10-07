# A weapon line from a generator

Six levels of a weapon written out by hand are six long records that differ in a few
numbers, and a change to the pattern means editing all six. A **data generator** writes
them from a rule. In this tutorial you read one: the example
[mods/examples/weapon-line](../../../../mods/examples/weapon-line/), which adds a technology,
**Ember Optics** (six levels), and the **Ember Lance I** to **VI**, a beam that hits harder
and reaches further with each level, with a firing sound of its own. The chapters behind it
are [Rules scripts](../rules-scripts.md#data-generators) and
[rules.md](../../rules.md#data-generators); the fields are explained in
[Components](../data/components.md) and [Technology](../data/techs.md).

```sh
opense4-sdk new --from-example weapon-line ember --id=me.ember
```

## 1. What a generator is

A generator is a Python file in a mod's `data/` folder with a function `generate()` that
returns a **patch**: a dict shaped exactly like a `.toml` patch file. When the game loads its
data it runs the generator, in the sandbox, and applies the patch in the file's turn among
the mod's patches (by file name). So these two are the same:

```toml
[[components.add]]
name = "Ember Lance I"
set = { "Tonnage Space Taken" = 22 }
```

```python
def generate():
    return {"components": {"add": [{"name": "Ember Lance I", "set": {"Tonnage Space Taken": 22}}]}}
```

A generator gets nothing from the game: it builds records from its own code. It must give
the same patch every time and only whole numbers, as the rest of the data does. Its file
name must be a Python name (`ember_line.py`).

## 2. The technology

[data/ember_line.py](../../../../mods/examples/weapon-line/data/ember_line.py) first makes
the tech area, written out in full so the mod fits any data set:

```python
AREA = "Ember Optics"
LEVELS = 6

def tech_area():
    return {
        "name": AREA,
        "set": {
            "Description": "Focusing light through ember crystals into a cutting beam.",
            "Group": "Weapons",
            # Six levels, each dearer than the last (docs/sdk/guide/data/techs.md).
            "Level Cost": 3000,
            "Maximum Level": LEVELS,
            # Empires start without it; the Medium tech start gives level 1.
            "Raise Level": 1,
            "Start Level": 0,
            # Open to every race, by research alone; a game may leave it out.
            "Can Be Removed": True,
            "Racial Area": 0,
            "Unique Area": 0,
            "Number of Tech Req": 0,
        },
    }
```

## 3. One lance per level

Then one component per level. Everything that grows with the level is a formula:

```python fragment
# continued: ROMAN (the numerals) and FAMILY are set at the top of the file
def lance(level):
    return {
        "name": "Ember Lance " + ROMAN[level - 1],
        "set": {
            "Tonnage Space Taken": 20 + 2 * level,
            "Cost Minerals": 30 + 15 * level,
            "Weapon Type": "Direct Fire",
            "Weapon Damage At Rng": damage_table(level),
            "Weapon Modifier": 5 * (level - 1),
            "Weapon Sound": "ember.wav",
            "Family": FAMILY,
            "Roman Numeral": level,
            # ... and the other fields, written out in full
        },
        # Each level needs that level of Ember Optics: an entry of the requirements list.
        "add": {"requirements": [{"Tech Area Req": AREA, "Tech Level Req": level}]},
    }


def damage_table(level):
    """Damage at ranges 1 to 20: strong up close, fading with range; each level hits
    harder and reaches one square further."""
    peak = 14 + 6 * level
    reach = 3 + level
    table = []
    for distance in range(1, 21):
        if distance > reach:
            table.append(0)
        else:
            fade = max(0, distance - 2) * 50 // max(1, reach - 2)
            table.append(peak * (100 - fade) // 100)
    return " ".join(str(d) for d in table)


def generate():
    return {
        "tech_areas": {"add": [tech_area()]},
        "components": {"add": [lance(level) for level in range(1, LEVELS + 1)]},
    }
```

- **One family, in order.** All six share a `Family`, and come in level order: a design's
  Upgrade replaces a component by the *last* of its family (in file order) that the empire
  can build, so the newest lance researched ([Components](../data/components.md#families)).
- **Whole numbers.** The damage falls with `//`, never `/`: a float in a patch is an error.
- **The tech area comes first** in the patch, so the components that require it find it.

## 4. A sound of its own

`Weapon Sound` names `ember.wav`, which the mod holds as `assets/Sounds/ember.wav`: the game
looks in the mods' `assets/` before the installed game's `Sounds/`
([Pictures, sounds and music](../assets.md#sounds-and-music)). The example's sound is a
falling tone made by a formula in
[tools/make_example_assets.py](../../../../tools/make_example_assets.py).

## 5. See what it makes

```sh
opense4-sdk check ember
opense4-sdk dump ember --out=dump        # Data/Components.txt and TechArea.txt hold the new records
```

`dump` writes the data set as the game reads it with the mod, so the generator's records
appear as ordinary records. A generator that raises is a load error with the mod, the file
and the traceback:

```text
mod me.ember: data/ember_line.py: generate() failed: Exception: ZeroDivisionError: ...
```

## 6. Test it

[tests/test_ember.py](../../../../mods/examples/weapon-line/tests/test_ember.py) reads the
generated records through the rules view:

```python fragment
# condensed from tests/test_ember.py (NAMES and the_rules() are set at its top)
def test_each_level_brings_a_stronger_lance():
    r = the_rules()
    area = r.tech_named("Ember Optics")
    last_damage = 0
    for level, name in enumerate(NAMES, 1):
        lance = r.component_named(name)
        assert [(q.area, q.level) for q in lance.requirements] == [(area.id, level)]
        assert lance.weapon.damage_at_range[1] > last_damage
        last_damage = lance.weapon.damage_at_range[1]
```

```sh
opense4-sdk test ember
```

## 7. Change the rule

Make it eight levels: set `LEVELS = 8`. The tech area gets eight levels and the line eight
lances, each requiring its level; `check` and `test` tell you whether everything still
holds. (The test's list of names would need two more.) Try a steeper damage curve, or a
second line of seekers from the same file: a generator is ordinary Python.
