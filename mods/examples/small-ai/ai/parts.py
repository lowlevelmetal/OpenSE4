"""What the empire can build now: technology checks and the best part for a job.

Everything here is chosen by what records do (their abilities, vehicle types and
figures), never by their names, so Pioneer plays on any data set: the classic one,
or one a mod changed. Ability names are the data files' own
(docs/sdk/guide/data/abilities.md).
"""

# The abilities a ship's crew needs, in the order a design lists them.
BRIDGE = "Ship Bridge"
LIFE_SUPPORT = "Ship Life Support"
CREW_QUARTERS = "Ship Crew Quarters"
ENGINE = "Standard Ship Movement"
ARMOR = "Armor"
SUPPLY = "Supply Storage"

# A colony module's ability for each planet surface ("Rock", "Ice", "Gas Giant").
COLONIZE = {"Rock": "Colonize Planet - Rock", "Ice": "Colonize Planet - Ice", "Gas Giant": "Colonize Planet - Gas"}


class Parts:
    """The rules view seen through our technology levels."""

    def __init__(self, rules, levels):
        self.rules = rules
        self.levels = levels

    def available(self, record):
        """Our technology meets every requirement of a component, facility or hull."""
        for q in record.requirements:
            if q.area is not None and self.levels[q.area] < q.level:
                return False
        return True

    def components(self, vehicle_type="ship"):
        """The components we can put on a vehicle of this type."""
        return [c for c in self.rules.components if vehicle_type in c.vehicle_types and self.available(c)]

    def best_with(self, ability, vehicle_type="ship", without=None):
        """The component with the largest value of `ability`, the smallest first on a tie;
        None when we have none. (For a bridge or life support the value is 0 everywhere,
        so this is the smallest.) `without`: an ability it must not have, so that the
        best supply store is not an engine that also stores supply."""
        best = None
        for c in self.components(vehicle_type):
            if not c.has_ability(ability) or (without is not None and c.has_ability(without)):
                continue
            key = (_number(c.ability_value(ability)), -c.tonnage, c.roman_numeral)
            if best is None or key > best[0]:
                best = (key, c)
        return None if best is None else best[1]

    def best_weapon(self, vehicle_type="ship"):
        """The direct-fire weapon that does the most damage at short range for its space."""
        best = None
        for c in self.components(vehicle_type):
            w = c.weapon
            if w is None or w.kind != "direct_fire" or c.tonnage <= 0:
                continue
            if w.targets and not any("Ship" in t for t in w.targets):
                continue          # point-defence and planet-only weapons
            damage = sum(w.damage_at_range[1:4])          # ranges 1 to 3
            key = (damage * 1000 // c.tonnage, c.roman_numeral)
            if best is None or key > best[0]:
                best = (key, c)
        return None if best is None else best[1]

    def hulls(self, vehicle_type="ship"):
        return [h for h in self.rules.hulls if h.type == vehicle_type and self.available(h)]

    def facility_with(self, ability):
        """The facility with the largest value of `ability`, or None."""
        best = None
        for f in self.rules.facilities:
            if not self.available(f) or not f.has_ability(ability):
                continue
            key = (_number(f.ability_value(ability)), f.roman_numeral)
            if best is None or key > best[0]:
                best = (key, f)
        return None if best is None else best[1]


def _number(value):
    """An ability value as a number (the data writes some as text)."""
    return value if isinstance(value, int) else 0
