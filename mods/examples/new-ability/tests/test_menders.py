# The Field Repair example's tests. The hook is a plain function: it can be called with a
# small stand-in for the game and the effects, which is how these tests check it, in the
# game's Python (opense4-sdk test) and under CPython (pytest, with scripts/ on the path).
# opense4-sdk test then plays a short game with the mod's rules on, which fails on any
# error a rules function raises.

from opense4 import rules, testing

import menders


class Empire:
    def __init__(self, id):
        self.id = id
        self.mod_data = {}


class Game:
    """What field_repairs reads of the game: the vehicles as maps, and abilities."""

    def __init__(self, vehicles, crews):
        self.raw = {"vehicles": vehicles}
        self.crews = crews

    def ability(self, thing, name, kind=None):
        assert name == menders.ABILITY and kind == "vehicle"
        return self.crews.get(thing)


class Effects:
    """Records the effects asked for; each repair mends what it is asked, up to 2."""

    def __init__(self):
        self.repairs = []
        self.logs = []

    def repair(self, vehicle, components=None):
        self.repairs.append((vehicle, components))
        return min(components, 2)

    def log(self, empire, text, title="", category="misc", location=None):
        self.logs.append(text)


def vehicle(id, owner, damage):
    return {"id": id, "owner": owner, "damage": damage, "name": "Ship " + str(id), "location": {"system": 0, "x": 6, "y": 6}}


def test_damaged_ships_with_crews_are_mended():
    game = Game([vehicle(1, 0, 30), vehicle(2, 0, 0), vehicle(3, 0, 12), vehicle(4, 1, 50)], crews={1: 3, 2: 1, 4: 1})
    fx = Effects()
    empire = Empire(0)
    menders.field_repairs(game, empire, "repair", "after", fx)
    # Ship 2 is not damaged, ship 3 has no crew, ship 4 is another empire's.
    assert fx.repairs == [(1, 3)]
    assert fx.logs == ["Field crews mended 2 components of Ship 1."]
    assert empire.mod_data == {"mended": 2}


def test_nothing_to_mend_leaves_no_trace():
    fx = Effects()
    empire = Empire(0)
    menders.field_repairs(Game([vehicle(1, 0, 0)], crews={1: 1}), empire, "repair", "after", fx)
    assert fx.repairs == [] and fx.logs == [] and empire.mod_data == {}


def test_the_hook_waits_for_the_repair_step():
    found = [r for r in rules.registrations() if r.fn is menders.field_repairs]
    assert found and found[0].name == "empire_end_of_turn"
    assert found[0].step == "repair" and found[0].when == "after"


def test_the_data_set_has_the_bay_and_the_ability():
    r = rules.Rules(testing.game_rules())
    bay = r.component_named("Mender Bay")
    assert bay is not None and bay.ability_value(menders.ABILITY) == 1
    assert r.aggregation(menders.ABILITY) == "sum"
