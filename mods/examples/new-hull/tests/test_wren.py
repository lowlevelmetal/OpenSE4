# The example's tests: `opense4-sdk test mods/examples/new-hull` runs them in the game's
# own Python, on a new game of your data set with the mod applied. Under CPython (pytest)
# the tests that need that game skip.

from opense4 import rules, testing

# The abilities a ship design needs, whatever the data set calls its components
# (docs/sdk/guide/data/hulls.md, "Design rules").
CREW = ("Ship Bridge", "Ship Life Support", "Ship Crew Quarters")


def the_rules():
    return rules.Rules(testing.game_rules())


def test_the_hull_is_in_the_data_set():
    hull = the_rules().hull_named("Wren Courier")
    assert hull is not None, "the patch did not add the hull"
    assert hull.type == "ship"
    assert hull.tonnage == 120
    assert hull.requirements == [], "no technology needed"
    assert hull.max_percent_cargo == 30


def test_the_cargo_rack_holds_cargo():
    rack = the_rules().component_named("Wren Cargo Rack")
    assert rack is not None, "the patch did not add the component"
    assert rack.ability_value("Cargo Storage") == 60
    assert rack.tonnage == 20
    assert "ship" in rack.vehicle_types and "base" in rack.vehicle_types


def test_an_empire_can_fit_a_courier_from_the_start():
    """The smallest crew components and engine an empire has on its first turn, and two
    racks, fit the hull's 120 kT; two racks are the 30 % of it the hull asks to hold cargo."""
    r = the_rules()
    view = testing.game_view()
    levels = view["my"]["research"]["levels"]

    def available(c):
        return all(q.area is None or levels[q.area] >= q.level for q in c.requirements) and "ship" in c.vehicle_types

    def smallest_with(ability):
        found = [c for c in r.components if available(c) and c.has_ability(ability)]
        if not found:
            raise testing.Skip("this data set has no ship component with " + ability)
        return min(found, key=lambda c: c.tonnage)

    hull = r.hull_named("Wren Courier")
    rack = r.component_named("Wren Cargo Rack")
    parts = [smallest_with(a) for a in CREW] + [smallest_with("Standard Ship Movement")]
    used = sum(c.tonnage for c in parts) + 2 * rack.tonnage
    assert used <= hull.tonnage, "a courier needs {} kT".format(used)
    # "Requirement Pct Cargo" is a least share: cargo components must fill at least it.
    assert 2 * rack.tonnage * 100 >= hull.max_percent_cargo * hull.tonnage
