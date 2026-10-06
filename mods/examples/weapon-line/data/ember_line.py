"""A data generator (docs/sdk/rules.md, "Data generators"): the Ember Lance line.

When the game loads its data, it calls generate() and applies what it returns exactly
as it would a .toml patch of this mod. Here that is one new tech area, Ember Optics,
and six components, Ember Lance I to VI, one a level, each written out in full so the
mod needs no record of the data set it is played with. Change LEVELS or the formulas
below and the whole line follows; `opense4-sdk dump` shows the records it makes.

A generator runs in the sandbox with nothing from the game: only its own code. It must
give the same patch every time (no randomness of its own), and whole numbers only.
"""

AREA = "Ember Optics"
LEVELS = 6
ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"]

# One family for the whole line, so that a design's Upgrade replaces a lance by the
# latest one researched (docs/sdk/guide/data/components.md, "Families").
FAMILY = 7301
WEAPON_FAMILY = 7301


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
            # Full damage for the first two squares, then down to half at the far end.
            fade = max(0, distance - 2) * 50 // max(1, reach - 2)
            table.append(peak * (100 - fade) // 100)
    return " ".join(str(d) for d in table)


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


def lance(level):
    name = "Ember Lance " + ROMAN[level - 1]
    return {
        "name": name,
        "set": {
            "Description": "An ember beam, level {} of {}.".format(level, LEVELS),
            "Pic Num": 1,
            # Larger, sturdier and dearer with each level.
            "Tonnage Space Taken": 20 + 2 * level,
            "Tonnage Structure": 10 + 3 * level,
            "Cost Minerals": 30 + 15 * level,
            "Cost Organics": 0,
            "Cost Radioactives": 5 * level,
            # Where it goes, and how the designer groups it.
            "Vehicle Type": "Ship\\Base\\Sat\\WeapPlat",
            "General Group": "Weapons",
            "Custom Group": 0,
            "Restrictions": "None",
            # One family for the whole line, numbered by level.
            "Family": FAMILY,
            "Roman Numeral": level,
            # The weapon.
            "Weapon Type": "Direct Fire",
            "Weapon Damage At Rng": damage_table(level),
            "Weapon Target": "Ships\\Planets",
            "Weapon Modifier": 5 * (level - 1),
            "Weapon Reload Rate": 1,
            "Weapon Family": WEAPON_FAMILY,
            "Weapon Damage Type": "Normal",
            # How it looks and sounds: the sound is the mod's own, assets/Sounds/ember.wav.
            "Weapon Display": 4,
            "Weapon Display Type": "Beam",
            "Weapon Sound": "ember.wav",
            # Not a seeker.
            "Weapon Seeker Dmg Res": 0,
            "Weapon Seeker Speed": 0,
            # One supply a shot, and no abilities.
            "Supply Amount Used": 1,
            "Number of Abilities": 0,
        },
        # Each level needs that level of Ember Optics: a list entry, which the patch numbers.
        "add": {"requirements": [{"Tech Area Req": AREA, "Tech Level Req": level}]},
    }


def generate():
    return {
        "tech_areas": {"add": [tech_area()]},
        "components": {"add": [lance(level) for level in range(1, LEVELS + 1)]},
    }
