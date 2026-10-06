# The Ember Lance example's tests, on the data set the mod makes (opense4-sdk test runs
# the generator as the game does; under CPython these skip).

from opense4 import rules, testing

NAMES = ["Ember Lance " + n for n in ("I", "II", "III", "IV", "V", "VI")]


def the_rules():
    return rules.Rules(testing.game_rules())


def test_the_area_has_six_levels():
    area = the_rules().tech_named("Ember Optics")
    assert area is not None, "the generator did not add the tech area"
    assert area.max_level == 6
    assert area.requirements == []


def test_each_level_brings_a_stronger_lance():
    r = the_rules()
    area = r.tech_named("Ember Optics")
    last_damage = 0
    last_reach = 0
    for level, name in enumerate(NAMES, 1):
        lance = r.component_named(name)
        assert lance is not None, name
        assert lance.is_weapon and lance.weapon.kind == "direct_fire"
        assert [(q.area, q.level) for q in lance.requirements] == [(area.id, level)]
        damage = lance.weapon.damage_at_range[1]
        assert damage > last_damage, name + " hits harder than the level before"
        assert lance.weapon.max_range > last_reach, name + " reaches further"
        last_damage, last_reach = damage, lance.weapon.max_range


def test_the_line_is_one_family_in_order():
    r = the_rules()
    lances = [r.component_named(n) for n in NAMES]
    assert len({c.family for c in lances}) == 1
    # Upgrades take the last of a family in file order: the line must be in level order.
    positions = [c.id for c in lances]
    assert positions == sorted(positions)
