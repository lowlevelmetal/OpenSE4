# The balance mod's tests, on the data set it makes from your installed game (opense4-sdk
# test). They read the patched data through the rules view, as computer players do.

from opense4 import rules, testing


def the_rules():
    return rules.Rules(testing.game_rules())


def test_propulsion_is_cheaper():
    assert the_rules().tech_named("Propulsion").level_cost == 4000


def test_no_temporal_technology_is_left():
    r = the_rules()
    assert not [t.name for t in r.techs if t.racial_area == 3]
    # cascade took what needed those areas: every requirement names an area that is there.
    for table in (r.components, r.facilities, r.hulls, r.techs):
        for record in table:
            for q in record.requirements:
                assert q.area is None or 0 <= q.area < len(r.techs), record.name


def test_seeking_weapons_reload_every_other_turn():
    seekers = [c for c in the_rules().components if c.weapon is not None and c.weapon.kind == "seeking"]
    assert seekers, "the data set has seeking weapons"
    assert all(c.weapon.reload_rate == 2 for c in seekers)


def test_the_first_ion_engine_steadies_ships():
    engine = the_rules().component_named("Ion Engine I")
    assert engine.ability_value("Combat To Hit Defense Plus") == 5


def test_satellites_cost_no_radioactives():
    satellites = [h for h in the_rules().hulls if h.type == "satellite"]
    assert satellites and all(h.cost.radioactives == 0 for h in satellites)
